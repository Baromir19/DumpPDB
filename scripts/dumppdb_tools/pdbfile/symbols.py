"""CodeView symbol records and the C13 line-number subsections of a module stream."""

from __future__ import annotations

import bisect
import struct
from dataclasses import dataclass, field

# --- symbol record kinds -------------------------------------------------------------------
S_END = 0x0006
S_FRAMEPROC = 0x1012
S_BLOCK32 = 0x1103
S_LABEL32 = 0x1105
S_REGISTER = 0x1106
S_CONSTANT = 0x1107
S_UDT = 0x1108
S_BPREL32 = 0x110B
S_LDATA32 = 0x110C
S_GDATA32 = 0x110D
S_PUB32 = 0x110E
S_LPROC32 = 0x110F
S_GPROC32 = 0x1110
S_REGREL32 = 0x1111
S_LTHREAD32 = 0x1112
S_GTHREAD32 = 0x1113
S_THUNK32 = 0x1102
S_LOCAL = 0x113E
S_LPROC32_ID = 0x1146
S_GPROC32_ID = 0x1147
S_INLINESITE = 0x114D
S_INLINESITE_END = 0x114E
S_PROC_ID_END = 0x114F

PROC_KINDS = (S_GPROC32, S_LPROC32, S_GPROC32_ID, S_LPROC32_ID)
DATA_KINDS = (S_GDATA32, S_LDATA32, S_GTHREAD32, S_LTHREAD32)


def cstr(data, pos: int) -> tuple[str, int]:
    end = data.find(b"\0", pos)
    if end < 0:
        end = len(data)
    return bytes(data[pos:end]).decode("utf-8", "replace"), end + 1


def iter_records(data: bytes, start: int = 0, end: int | None = None):
    """Yield (offset, kind, payload_offset, payload_end) for each symbol record."""
    end = len(data) if end is None else end
    p = start
    while p + 4 <= end:
        (length, kind) = struct.unpack_from("<HH", data, p)
        if length < 2:
            break
        yield p, kind, p + 4, p + 2 + length
        p += 2 + length


@dataclass
class Param:
    name: str
    typind: int
    offset: int


@dataclass
class Proc:
    name: str
    typind: int
    section: int
    offset: int
    length: int
    is_global: bool
    is_id: bool
    module: int
    params: list[Param] = field(default_factory=list)
    flags: int = 0
    sym_offset: int = 0       # position in the module stream
    parent: int = 0


@dataclass
class DataSym:
    name: str
    typind: int
    section: int
    offset: int
    is_global: bool
    thread_local: bool
    module: int = -1          # -1 = global symbol stream


@dataclass
class Public:
    name: str                 # decorated
    section: int
    offset: int
    flags: int

    @property
    def is_function(self) -> bool:
        return bool(self.flags & 2)


@dataclass
class UdtSym:
    name: str
    typind: int
    module: int


@dataclass
class ModuleSymbols:
    procs: list[Proc] = field(default_factory=list)
    data: list[DataSym] = field(default_factory=list)
    udts: list[UdtSym] = field(default_factory=list)
    thunks: list[tuple[str, int, int, int]] = field(default_factory=list)   # name, seg, off, len
    static_locals: list[tuple[str, int, int, int, int]] = field(default_factory=list)  # name, typ, seg, off, proc idx


def parse_module_symbols(data: bytes, sym_size: int, module: int) -> ModuleSymbols:
    """Walk the symbol area of a module stream (after its 4-byte signature)."""
    out = ModuleSymbols()
    stack: list[tuple[str, int]] = []   # ("proc", index) or ("scope", -1)
    current_proc = -1

    for off, kind, p, end in iter_records(data, 4, sym_size):
        if kind in PROC_KINDS:
            (_par, _end, _nxt, length, _dbs, _dbe, typ, offs, seg, flags) = struct.unpack_from(
                "<IIIIIIIIHB", data, p)
            name, _ = cstr(data, p + 35)
            out.procs.append(Proc(name, typ, seg, offs, length, kind in (S_GPROC32, S_GPROC32_ID),
                                  kind in (S_GPROC32_ID, S_LPROC32_ID), module, flags=flags,
                                  sym_offset=off))
            stack.append(("proc", len(out.procs) - 1))
            current_proc = len(out.procs) - 1
        elif kind in (S_BLOCK32, S_THUNK32, S_INLINESITE):
            if kind == S_THUNK32:
                (_par, _end, _nxt, offs, seg, length) = struct.unpack_from("<IIIIHH", data, p)
                name, _ = cstr(data, p + 25)
                out.thunks.append((name, seg, offs, length))
            stack.append(("scope", -1))
        elif kind in (S_END, S_PROC_ID_END, S_INLINESITE_END):
            if stack:
                top = stack.pop()
                if top[0] == "proc":
                    current_proc = next((i for k, i in reversed(stack) if k == "proc"), -1)
        elif kind in DATA_KINDS:
            typ, offs, seg = struct.unpack_from("<IIH", data, p)
            name, _ = cstr(data, p + 10)
            sym = DataSym(name, typ, seg, offs, kind in (S_GDATA32, S_GTHREAD32),
                          kind in (S_GTHREAD32, S_LTHREAD32), module)
            # A data symbol nested inside a procedure is a function-local static.
            if current_proc >= 0:
                out.static_locals.append((name, typ, seg, offs, current_proc))
            else:
                out.data.append(sym)
        elif kind == S_BPREL32 and current_proc >= 0:
            offs, typ = struct.unpack_from("<iI", data, p)
            name, _ = cstr(data, p + 8)
            out.procs[current_proc].params.append(Param(name, typ, offs))
        elif kind == S_REGREL32 and current_proc >= 0:
            offs, typ, _reg = struct.unpack_from("<iIH", data, p)
            name, _ = cstr(data, p + 10)
            out.procs[current_proc].params.append(Param(name, typ, offs))
        elif kind == S_UDT:
            (typ,) = struct.unpack_from("<I", data, p)
            name, _ = cstr(data, p + 4)
            out.udts.append(UdtSym(name, typ, module))
    return out


def parse_symrecord_stream(data: bytes) -> tuple[list[DataSym], list[Public], list[UdtSym]]:
    """The global symbol record stream: publics, global data, UDT records."""
    gdata: list[DataSym] = []
    publics: list[Public] = []
    udts: list[UdtSym] = []
    for _off, kind, p, _end in iter_records(data):
        if kind == S_PUB32:
            flags, offs, seg = struct.unpack_from("<IIH", data, p)
            name, _ = cstr(data, p + 10)
            publics.append(Public(name, seg, offs, flags))
        elif kind in DATA_KINDS:
            typ, offs, seg = struct.unpack_from("<IIH", data, p)
            name, _ = cstr(data, p + 10)
            gdata.append(DataSym(name, typ, seg, offs, kind in (S_GDATA32, S_GTHREAD32),
                                 kind in (S_GTHREAD32, S_LTHREAD32)))
        elif kind == S_UDT:
            (typ,) = struct.unpack_from("<I", data, p)
            name, _ = cstr(data, p + 4)
            udts.append(UdtSym(name, typ, -1))
    return gdata, publics, udts


# --- names stream and C13 line information --------------------------------------------------------

def parse_names_stream(data: bytes) -> dict[int, str]:
    """The /names string table: offset (into its string buffer) -> string."""
    if len(data) < 12:
        return {}
    sig, ver, cb = struct.unpack_from("<III", data, 0)
    buf = data[12:12 + cb]
    names: dict[int, str] = {}
    pos = 0
    while pos < len(buf):
        end = buf.find(b"\0", pos)
        if end < 0:
            break
        names[pos] = buf[pos:end].decode("utf-8", "replace")
        pos = end + 1
    return names


DEBUG_S_LINES = 0xF2
DEBUG_S_STRINGTABLE = 0xF3
DEBUG_S_FILECHKSMS = 0xF4


@dataclass
class LineTable:
    """Sorted line entries of one module: parallel arrays keyed by RVA."""
    rvas: list[int] = field(default_factory=list)
    files: list[str] = field(default_factory=list)
    lines: list[int] = field(default_factory=list)

    def first_in(self, start: int, length: int) -> tuple[str, int] | None:
        i = bisect.bisect_left(self.rvas, start)
        if i < len(self.rvas) and self.rvas[i] < start + max(length, 1):
            return self.files[i], self.lines[i]
        return None

    def span(self, start: int, length: int) -> tuple[str, int, int] | None:
        i = bisect.bisect_left(self.rvas, start)
        if i >= len(self.rvas) or self.rvas[i] >= start + max(length, 1):
            return None
        j = i
        lo = hi = self.lines[i]
        while j < len(self.rvas) and self.rvas[j] < start + max(length, 1):
            lo = min(lo, self.lines[j])
            hi = max(hi, self.lines[j])
            j += 1
        return self.files[i], lo, hi


def parse_c13_lines(data: bytes, c13_start: int, c13_size: int, names: dict[int, str],
                    section_rva) -> LineTable:
    """Collect all line entries of a module's C13 subsections."""
    table = LineTable()
    end = c13_start + c13_size
    if c13_size <= 0 or end > len(data):
        return table

    # pass 1: file checksum table (maps file ids to name offsets)
    file_names: dict[int, str] = {}
    local_strings: dict[int, str] = {}
    p = c13_start
    while p + 8 <= end:
        kind, length = struct.unpack_from("<II", data, p)
        body = p + 8
        if kind == DEBUG_S_FILECHKSMS:
            # Entries are 4-byte aligned: {u32 name offset, u8 checksum size, u8 kind, bytes}.
            q = 0
            while q + 6 <= length:
                name_off, cb, _type = struct.unpack_from("<IBB", data, body + q)
                file_names[q] = names.get(name_off, f"<name@{name_off}>")
                q = (q + 6 + cb + 3) & ~3
        elif kind == DEBUG_S_STRINGTABLE:
            local_strings = parse_names_stream(b"\0" * 12 + data[body:body + length])
        p = body + length
        p = (p + 3) & ~3

    # pass 2: line blocks
    entries: list[tuple[int, str, int]] = []
    p = c13_start
    while p + 8 <= end:
        kind, length = struct.unpack_from("<II", data, p)
        body = p + 8
        if kind == DEBUG_S_LINES:
            off_con, seg_con, flags, cb_con = struct.unpack_from("<IHHI", data, body)
            base = section_rva(seg_con, off_con)
            q = body + 12
            has_cols = bool(flags & 1)
            while q + 12 <= body + length:
                file_id, n_lines, cb_block = struct.unpack_from("<III", data, q)
                fname = file_names.get(file_id, f"<file@{file_id}>")
                lp = q + 12
                for _ in range(n_lines):
                    off, packed = struct.unpack_from("<II", data, lp)
                    lp += 8
                    entries.append((base + off, fname, packed & 0x00FFFFFF))
                if has_cols:
                    lp += 4 * n_lines
                q += cb_block if cb_block >= 12 else 12 + 8 * n_lines
        p = body + length
        p = (p + 3) & ~3

    entries.sort(key=lambda e: e[0])
    for rva, fname, line in entries:
        table.rvas.append(rva)
        table.files.append(fname)
        table.lines.append(line)
    return table
