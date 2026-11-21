"""DBI stream (stream 3): modules (compilands), section headers, stream indices."""

from __future__ import annotations

import struct
from dataclasses import dataclass, field


@dataclass
class Module:
    index: int
    name: str                 # module name (object file or library member)
    obj_name: str             # object / library file
    stream: int               # module symbol stream (0xFFFF = none)
    sym_size: int             # bytes of symbols (includes the 4-byte signature)
    c11_size: int
    c13_size: int
    source_file_count: int


@dataclass
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    characteristics: int


@dataclass
class Dbi:
    age: int = 0
    machine: int = 0
    global_stream: int = 0xFFFF
    public_stream: int = 0xFFFF
    symrecord_stream: int = 0xFFFF
    modules: list[Module] = field(default_factory=list)
    sections: list[Section] = field(default_factory=list)
    section_header_stream: int = 0xFFFF
    # per-module list of source files (as written in the file-info substream)
    module_files: list[list[str]] = field(default_factory=list)

    def rva(self, section: int, offset: int) -> int:
        """section is 1-based, as in every CodeView address."""
        if 1 <= section <= len(self.sections):
            return self.sections[section - 1].virtual_address + offset
        return 0

    def section_name(self, section: int) -> str:
        return self.sections[section - 1].name if 1 <= section <= len(self.sections) else ""


def _cstr(data: bytes, pos: int) -> tuple[str, int]:
    end = data.index(b"\0", pos)
    return data[pos:end].decode("utf-8", "replace"), end + 1


def parse_dbi(data: bytes, msf) -> Dbi:
    (sig, ver, age, gsi, build, psi, dll_ver, symrec, dll_rbld, mod_size, contrib_size, map_size,
     src_size, tsm_size, mfc_ts, opt_size, ec_size, flags, machine, _pad) = struct.unpack_from(
        "<iIIHHHHHHiiiiiIiiHHI", data, 0)
    if sig != -1:
        raise ValueError("old DBI format (pre-VC7) is not supported")

    dbi = Dbi(age=age, machine=machine, global_stream=gsi, public_stream=psi,
              symrecord_stream=symrec)

    pos = 64
    mod_data = data[pos:pos + mod_size]
    pos += mod_size
    pos += contrib_size + map_size
    src_data = data[pos:pos + src_size]
    pos += src_size + tsm_size + ec_size
    opt_data = data[pos:pos + opt_size]

    # -- modules -----------------------------------------------------------------------
    p = 0
    index = 0
    while p + 64 <= len(mod_data):
        (_u1,) = struct.unpack_from("<I", mod_data, p)
        flags2, stream, sym_size, c11, c13, nfiles = struct.unpack_from("<HHIIIH", mod_data, p + 4 + 28)
        name, q = _cstr(mod_data, p + 64)
        obj, q = _cstr(mod_data, q)
        p = (q + 3) & ~3
        dbi.modules.append(Module(index, name, obj, stream, sym_size, c11, c13, nfiles))
        index += 1

    # -- file info substream -----------------------------------------------------------------
    dbi.module_files = _parse_file_info(src_data, len(dbi.modules))

    # -- section headers (needed for section:offset -> RVA) ----------------------------------------
    if len(opt_data) >= 12:
        idx = struct.unpack_from("<HHHHHHHHHHH", opt_data, 0) if len(opt_data) >= 22 else ()
        if len(idx) > 5 and idx[5] != 0xFFFF:
            dbi.section_header_stream = idx[5]
            sh = msf.stream(idx[5])
            for o in range(0, len(sh) - 39, 40):
                name = sh[o:o + 8].rstrip(b"\0").decode("latin1")
                vs, va = struct.unpack_from("<II", sh, o + 8)
                ch = struct.unpack_from("<I", sh, o + 36)[0]
                dbi.sections.append(Section(name, va, vs, ch))
    return dbi


def _parse_file_info(data: bytes, n_modules_hint: int) -> list[list[str]]:
    """FileInfo substream: per-module lists of source files (null-terminated names)."""
    if len(data) < 4:
        return []
    n_modules, _n_files = struct.unpack_from("<HH", data, 0)
    p = 4
    p += 2 * n_modules                        # ModIndices (unused)
    counts = struct.unpack_from(f"<{n_modules}H", data, p)
    p += 2 * n_modules
    total = sum(counts)
    offsets = struct.unpack_from(f"<{total}I", data, p)
    p += 4 * total
    names = data[p:]

    out: list[list[str]] = []
    k = 0
    for m in range(n_modules):
        files = []
        for _ in range(counts[m]):
            off = offsets[k]
            k += 1
            end = names.find(b"\0", off)
            files.append(names[off:end].decode("utf-8", "replace"))
        out.append(files)
    return out
