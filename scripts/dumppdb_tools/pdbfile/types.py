"""TPI stream: CodeView type records and their rendering as C++ declarations."""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass, field

from dumppdb_tools.restore.symbols import split_qualified

# --- leaf kinds ---------------------------------------------------------------------------
LF_VTSHAPE = 0x000A
LF_MODIFIER = 0x1001
LF_POINTER = 0x1002
LF_PROCEDURE = 0x1008
LF_MFUNCTION = 0x1009
LF_ARGLIST = 0x1201
LF_FIELDLIST = 0x1203
LF_BITFIELD = 0x1205
LF_METHODLIST = 0x1206
LF_BCLASS = 0x1400
LF_VBCLASS = 0x1401
LF_IVBCLASS = 0x1402
LF_INDEX = 0x1404
LF_VFUNCTAB = 0x1409
LF_ENUMERATE = 0x1502
LF_ARRAY = 0x1503
LF_CLASS = 0x1504
LF_STRUCTURE = 0x1505
LF_UNION = 0x1506
LF_ENUM = 0x1507
LF_MEMBER = 0x150D
LF_STMEMBER = 0x150E
LF_METHOD = 0x150F
LF_NESTTYPE = 0x1510
LF_ONEMETHOD = 0x1511
LF_INTERFACE = 0x1519

UDT_KINDS = (LF_CLASS, LF_STRUCTURE, LF_UNION, LF_INTERFACE)

# property bits of UDT / enum records
PROP_PACKED = 0x0001
PROP_HAS_CTOR = 0x0002
PROP_NESTED = 0x0008
PROP_FWDREF = 0x0080
PROP_SCOPED = 0x0100
PROP_HAS_UNIQUE = 0x0200

# field attribute bits
ACCESS_NAMES = {1: "private", 2: "protected", 3: "public"}
MPROP_VANILLA, MPROP_VIRTUAL, MPROP_STATIC, MPROP_FRIEND, MPROP_INTRO, MPROP_PURE, MPROP_PURE_INTRO = range(7)

_PRIMS = {
    0x03: "void", 0x08: "HRESULT", 0x10: "char", 0x11: "short", 0x12: "long", 0x13: "__int64",
    0x20: "unsigned char", 0x21: "unsigned short", 0x22: "unsigned long", 0x23: "unsigned __int64",
    0x30: "bool", 0x31: "bool", 0x32: "bool", 0x33: "bool",
    0x40: "float", 0x41: "double", 0x42: "long double",
    0x68: "int8", 0x69: "uint8", 0x70: "char", 0x71: "wchar_t", 0x72: "int16", 0x73: "uint16",
    0x74: "int32", 0x75: "uint32", 0x76: "int64", 0x77: "uint64", 0x7A: "char16_t", 0x7B: "char32_t",
}
_CSTDINT = {"int8": "int8_t", "uint8": "uint8_t", "int16": "int16_t", "uint16": "uint16_t",
            "int32": "int32_t", "uint32": "uint32_t", "int64": "int64_t", "uint64": "uint64_t",
            "short": "int16_t", "unsigned short": "uint16_t", "long": "int32_t",
            "unsigned long": "uint32_t", "__int64": "int64_t", "unsigned __int64": "uint64_t"}
_MSVC = {"int8": "__int8", "uint8": "unsigned __int8", "int16": "__int16", "uint16": "unsigned __int16",
         "int32": "int", "uint32": "unsigned int", "int64": "__int64", "uint64": "unsigned __int64"}

_CALLCONV = {0: "__cdecl", 1: "__cdecl", 2: "__pascal", 3: "__pascal", 4: "__fastcall", 5: "__fastcall",
             7: "__stdcall", 8: "__stdcall", 11: "__thiscall", 12: "__clrcall"}


# Compiler-generated helpers that are not part of the source-level interface.
_HIDDEN_METHODS = frozenset({"__local_vftable_ctor_closure", "__vecDelDtor", "__vdtor", "__vftable",
                             "__vbaseDtor", "__default_ctor_closure", "__local_vftable_ctor_closure"})


def read_numeric(data, pos: int) -> tuple[int, int]:
    """CodeView variable-length number -> (value, new position)."""
    (v,) = struct.unpack_from("<H", data, pos)
    pos += 2
    if v < 0x8000:
        return v, pos
    if v == 0x8000:
        return struct.unpack_from("<b", data, pos)[0], pos + 1
    if v == 0x8001:
        return struct.unpack_from("<h", data, pos)[0], pos + 2
    if v == 0x8002:
        return struct.unpack_from("<H", data, pos)[0], pos + 2
    if v == 0x8003:
        return struct.unpack_from("<i", data, pos)[0], pos + 4
    if v == 0x8004:
        return struct.unpack_from("<I", data, pos)[0], pos + 4
    if v == 0x8005:
        return int(struct.unpack_from("<f", data, pos)[0]), pos + 4
    if v == 0x8006:
        return int(struct.unpack_from("<d", data, pos)[0]), pos + 8
    if v == 0x8009:
        return struct.unpack_from("<q", data, pos)[0], pos + 8
    if v == 0x800A:
        return struct.unpack_from("<Q", data, pos)[0], pos + 8
    return 0, pos


def _cstr(data, pos: int) -> tuple[str, int]:
    end = data.find(b"\0", pos)
    if end < 0:
        end = len(data)
    return bytes(data[pos:end]).decode("utf-8", "replace"), end + 1


# --- parsed records -------------------------------------------------------------------------------

@dataclass
class Udt:
    index: int
    kind: int
    name: str
    unique: str
    size: int
    prop: int
    field_list: int
    count: int

    @property
    def is_fwdref(self) -> bool:
        return bool(self.prop & PROP_FWDREF)

    @property
    def keyword(self) -> str:
        return {LF_CLASS: "class", LF_STRUCTURE: "struct", LF_UNION: "union",
                LF_INTERFACE: "struct"}[self.kind]

    @property
    def is_anonymous(self) -> bool:
        n = self.name
        return (not n) or "<unnamed" in n or "<anonymous" in n or n.startswith("__unnamed") or n.endswith("::<unnamed-tag>")


@dataclass
class EnumT:
    index: int
    name: str
    unique: str
    base: int
    prop: int
    field_list: int

    @property
    def is_fwdref(self) -> bool:
        return bool(self.prop & PROP_FWDREF)

    @property
    def is_anonymous(self) -> bool:
        return (not self.name) or "<unnamed" in self.name or "<anonymous" in self.name


@dataclass
class Member:
    kind: str                 # field, static, method, base, vbase, enumerator, nested, vfptr
    name: str = ""
    type: int = 0
    offset: int = 0
    attr: int = 0
    value: int = 0
    methods: list = field(default_factory=list)   # [(attr, type, vbaseoff)] for kind == method
    vbptr: int = 0
    vbpoff: int = 0


class TypeTable:
    """Lazily parsed view of the TPI stream."""

    def __init__(self, data: bytes, int_style: str = "cstdint"):
        self.data = data
        self.int_style = int_style
        (_ver, hsize, self.begin, self.end, _tb, *_rest) = struct.unpack_from("<IIIIIHHII", data, 0)
        self._rec: list[tuple[int, int, int]] = []   # (kind, payload start, record end)
        p = hsize
        n = len(data)
        while p + 4 <= n:
            length, kind = struct.unpack_from("<HH", data, p)
            self._rec.append((kind, p + 4, p + 2 + length))
            p += 2 + length
        self._cache: dict[int, object] = {}
        self._fields_cache: dict[int, list[Member]] = {}
        self._complete: dict[str, int] | None = None

    # -- low level ----------------------------------------------------------------------------------

    def kind(self, ti: int) -> int:
        if ti < self.begin or ti >= self.end:
            return 0
        return self._rec[ti - self.begin][0]

    def parse(self, ti: int):
        """Parsed record (Udt / EnumT / tuple) or None for primitives and unknown indices."""
        if ti < self.begin or ti >= self.end:
            return None
        hit = self._cache.get(ti)
        if hit is not None:
            return hit
        kind, p, end = self._rec[ti - self.begin]
        d = self.data
        rec = None

        if kind == LF_MODIFIER:
            utype, attrs = struct.unpack_from("<IH", d, p)
            rec = ("mod", utype, attrs)
        elif kind == LF_POINTER:
            utype, attr = struct.unpack_from("<II", d, p)
            extra = struct.unpack_from("<I", d, p + 8)[0] if end - p >= 12 else 0
            rec = ("ptr", utype, attr, extra)
        elif kind == LF_ARRAY:
            elem, idx = struct.unpack_from("<II", d, p)
            size, _ = read_numeric(d, p + 8)
            rec = ("arr", elem, idx, size)
        elif kind in UDT_KINDS:
            count, prop, fl, _derived, _vshape = struct.unpack_from("<HHIII", d, p)
            size, q = read_numeric(d, p + 16)
            name, q = _cstr(d, q)
            unique = _cstr(d, q)[0] if prop & PROP_HAS_UNIQUE else ""
            rec = Udt(ti, kind, name, unique, size, prop, fl, count)
        elif kind == LF_UNION:
            count, prop, fl = struct.unpack_from("<HHI", d, p)
            size, q = read_numeric(d, p + 8)
            name, q = _cstr(d, q)
            unique = _cstr(d, q)[0] if prop & PROP_HAS_UNIQUE else ""
            rec = Udt(ti, kind, name, unique, size, prop, fl, count)
        elif kind == LF_ENUM:
            count, prop, base, fl = struct.unpack_from("<HHII", d, p)
            name, q = _cstr(d, p + 12)
            unique = _cstr(d, q)[0] if prop & PROP_HAS_UNIQUE else ""
            rec = EnumT(ti, name, unique, base, prop, fl)
        elif kind == LF_PROCEDURE:
            rv, cc, fattr, nparm, arglist = struct.unpack_from("<IBBHI", d, p)
            rec = ("proc", rv, cc, nparm, arglist)
        elif kind == LF_MFUNCTION:
            rv, cls, this, cc, fattr, nparm, arglist, adj = struct.unpack_from("<IIIBBHIi", d, p)
            rec = ("mfunc", rv, cls, this, cc, fattr, nparm, arglist)
        elif kind == LF_ARGLIST:
            (cnt,) = struct.unpack_from("<I", d, p)
            rec = ("args", list(struct.unpack_from(f"<{cnt}I", d, p + 4)))
        elif kind == LF_BITFIELD:
            utype, length, pos = struct.unpack_from("<IBB", d, p)
            rec = ("bits", utype, length, pos)
        elif kind == LF_METHODLIST:
            items = []
            q = p
            while q + 8 <= end:
                attr, _pad, typ = struct.unpack_from("<HHI", d, q)
                q += 8
                vbase = 0
                if (attr >> 2) & 7 in (MPROP_INTRO, MPROP_PURE_INTRO):
                    (vbase,) = struct.unpack_from("<I", d, q)
                    q += 4
                items.append((attr, typ, vbase))
            rec = ("mlist", items)
        else:
            rec = ("other", kind)

        self._cache[ti] = rec
        return rec

    # -- field lists -------------------------------------------------------------------------------------

    def fields(self, fl: int) -> list[Member]:
        """Members of a field list, following LF_INDEX continuations."""
        if fl in self._fields_cache:
            return self._fields_cache[fl]
        out: list[Member] = []
        cur = fl
        guard = 0
        while cur and self.kind(cur) == LF_FIELDLIST and guard < 64:
            guard += 1
            _k, p, end = self._rec[cur - self.begin]
            d = self.data
            nxt = 0
            while p < end:
                (leaf,) = struct.unpack_from("<H", d, p)
                if leaf >= 0xF0 and leaf < 0x1000:      # LF_PADn written as a single byte
                    p += 1
                    continue
                if d[p] >= 0xF0 and d[p] <= 0xFF:
                    p += d[p] & 0x0F
                    continue
                p += 2
                if leaf == LF_MEMBER:
                    attr, typ = struct.unpack_from("<HI", d, p)
                    off, p = read_numeric(d, p + 6)
                    name, p = _cstr(d, p)
                    out.append(Member("field", name, typ, off, attr))
                elif leaf == LF_STMEMBER:
                    attr, typ = struct.unpack_from("<HI", d, p)
                    name, p = _cstr(d, p + 6)
                    out.append(Member("static", name, typ, 0, attr))
                elif leaf == LF_ONEMETHOD:
                    attr, typ = struct.unpack_from("<HI", d, p)
                    p += 6
                    vbase = 0
                    if (attr >> 2) & 7 in (MPROP_INTRO, MPROP_PURE_INTRO):
                        (vbase,) = struct.unpack_from("<I", d, p)
                        p += 4
                    name, p = _cstr(d, p)
                    m = Member("method", name, 0, 0, attr)
                    m.methods = [(attr, typ, vbase)]
                    out.append(m)
                elif leaf == LF_METHOD:
                    count, mlist = struct.unpack_from("<HI", d, p)
                    name, p = _cstr(d, p + 6)
                    m = Member("method", name, 0, 0, 0)
                    rec = self.parse(mlist)
                    m.methods = list(rec[1]) if isinstance(rec, tuple) and rec[0] == "mlist" else []
                    out.append(m)
                elif leaf == LF_BCLASS:
                    attr, typ = struct.unpack_from("<HI", d, p)
                    off, p = read_numeric(d, p + 6)
                    out.append(Member("base", "", typ, off, attr))
                elif leaf in (LF_VBCLASS, LF_IVBCLASS):
                    attr, typ, vbptr = struct.unpack_from("<HII", d, p)
                    vbpoff, p = read_numeric(d, p + 10)
                    vboff, p = read_numeric(d, p)
                    m = Member("vbase", "", typ, 0, attr)
                    m.vbptr, m.vbpoff, m.offset = vbptr, vbpoff, vboff
                    out.append(m)
                elif leaf == LF_ENUMERATE:
                    (attr,) = struct.unpack_from("<H", d, p)
                    val, p = read_numeric(d, p + 2)
                    name, p = _cstr(d, p)
                    out.append(Member("enumerator", name, 0, 0, attr, val))
                elif leaf == LF_NESTTYPE:
                    _pad, typ = struct.unpack_from("<HI", d, p)
                    name, p = _cstr(d, p + 6)
                    out.append(Member("nested", name, typ))
                elif leaf == LF_VFUNCTAB:
                    _pad, typ = struct.unpack_from("<HI", d, p)
                    p += 6
                    out.append(Member("vfptr", "__vfptr", typ))
                elif leaf == LF_INDEX:
                    _pad, nxt = struct.unpack_from("<HI", d, p)
                    p += 6
                else:
                    break           # unknown leaf: stop rather than mis-parse
            cur = nxt
        self._fields_cache[fl] = out
        return out

    # -- name resolution -------------------------------------------------------------------------------------

    def complete_definitions(self) -> dict[str, int]:
        """Qualified name -> index of the complete (non forward-ref) UDT/enum record."""
        if self._complete is None:
            comp: dict[str, int] = {}
            for i, (kind, _p, _e) in enumerate(self._rec):
                if kind in UDT_KINDS or kind in (LF_UNION, LF_ENUM):
                    rec = self.parse(self.begin + i)
                    if rec is not None and not rec.is_fwdref and rec.name:
                        comp.setdefault(rec.name, self.begin + i)
            self._complete = comp
        return self._complete

    def resolve(self, ti: int) -> int:
        """Forward references are replaced by their definition when one exists."""
        rec = self.parse(ti)
        if isinstance(rec, (Udt, EnumT)) and rec.is_fwdref:
            return self.complete_definitions().get(rec.name, ti)
        return ti

    def nested_target(self, parent: "Udt", member: "Member") -> int:
        """Type index of a *real* nested type (``Parent::Name``), or 0 for a typedef alias.

        LF_NESTTYPE covers both nested classes/enums and nested typedefs; only the
        former have the qualified name ``Parent::<member name>``.
        """
        ti = self.resolve(member.type)
        rec = self.parse(ti) if ti >= 0x1000 else None
        if isinstance(rec, (Udt, EnumT)) and rec.name == f"{parent.name}::{member.name}":
            return ti
        return 0

    def iter_definitions(self):
        """All complete, named UDT and enum records, in type-index order."""
        for i, (kind, _p, _e) in enumerate(self._rec):
            if kind in UDT_KINDS or kind in (LF_UNION, LF_ENUM):
                rec = self.parse(self.begin + i)
                if rec is not None and not rec.is_fwdref and rec.name:
                    yield rec


class TypeRenderer:
    """Renders type indices as C++ declarators and UDTs as declarations."""

    def __init__(self, table: TypeTable, int_style: str = "cstdint", show_offsets: bool = True):
        self.t = table
        self.int_style = int_style
        self.show_offsets = show_offsets

    # -- primitives -----------------------------------------------------------------------------------

    def _prim(self, ti: int) -> str:
        base = ti & 0xFF
        mode = (ti >> 8) & 0x7
        name = _PRIMS.get(base, f"<prim_{base:02x}>")
        if self.int_style == "cstdint":
            name = _CSTDINT.get(name, name)
        elif self.int_style == "msvc":
            name = _MSVC.get(name, name)
        else:
            name = {"int8": "signed char", "uint8": "unsigned char", "int16": "short",
                    "uint16": "unsigned short", "int32": "int", "uint32": "unsigned int",
                    "int64": "__int64", "uint64": "unsigned __int64"}.get(name, name)
        if mode:
            name += "*"
        return name

    # -- declarators ---------------------------------------------------------------------------------------

    @staticmethod
    def _join(base: str, inner: str) -> str:
        if not inner:
            return base
        return base + inner if inner[0] in "*&" else f"{base} {inner}"

    _STAR_GAP = re.compile(r"([*&]) (?=[*&])")

    def declare(self, ti: int, inner: str = "", depth: int = 0) -> str:
        """``declare(t, "name")`` -> C++ declaration of *name* with type *t*."""
        text = self._declare(ti, inner, depth)
        if depth == 0:
            text = self._STAR_GAP.sub(r"\1", text).replace("(* ", "(*").replace("(& ", "(&")
            text = text.replace(" )", ")").replace("( ", "(")
        return text

    def _declare(self, ti: int, inner: str = "", depth: int = 0) -> str:
        if depth > 40:
            return "/*deep*/ " + inner
        t = self.t
        if ti < 0x1000:
            return self._join(self._prim(ti), inner)
        rec = t.parse(ti)
        if rec is None:
            return f"/*type 0x{ti:x}*/ {inner}".rstrip()

        if isinstance(rec, (Udt, EnumT)):
            n = rec.name if rec.name and not rec.is_anonymous else ("/*anonymous*/ " + ("union" if getattr(rec, "kind", 0) == LF_UNION else "struct"))
            return self._join(n, inner)

        tag = rec[0]
        if tag == "mod":
            _, utype, attrs = rec
            quals = ("const " if attrs & 1 else "") + ("volatile " if attrs & 2 else "")
            target = t.parse(utype) if utype >= 0x1000 else None
            if target is not None and not isinstance(target, (Udt, EnumT)) and target[0] == "ptr":
                # "T * const name": the qualifier belongs to the pointer, so it goes into the declarator.
                suffix = ("const " if attrs & 1 else "") + ("volatile " if attrs & 2 else "")
                return self._declare(utype, (suffix + inner).strip(), depth + 1)
            return quals + self._declare(utype, inner, depth + 1)

        if tag == "ptr":
            _, utype, attr, extra = rec
            mode = (attr >> 5) & 7
            quals = (" const" if attr & 0x400 else "") + (" volatile" if attr & 0x200 else "")
            if mode in (2, 3):                      # pointer to member
                cls = self._declare(extra, "", depth + 1) if extra else "?"
                star = f"{cls}::*"
            else:
                star = "&&" if mode == 4 else "&" if mode == 1 else "*"
            new_inner = star + quals + ((" " + inner) if inner else "")
            target = t.parse(utype) if utype >= 0x1000 else None
            needs_paren = target is not None and not isinstance(target, (Udt, EnumT)) \
                and target[0] in ("proc", "mfunc", "arr")
            if needs_paren:
                new_inner = "(" + new_inner + ")"
            return self._declare(utype, new_inner, depth + 1)

        if tag == "arr":
            _, elem, _idx, size = rec
            esize = self.size_of(elem)
            count = size // esize if esize else 0
            return self._declare(elem, f"{inner}[{count}]" if count else f"{inner}[]", depth + 1)

        if tag in ("proc", "mfunc"):
            ret = rec[1]
            cc = rec[2] if tag == "proc" else rec[4]
            args = rec[4] if tag == "proc" else rec[7]
            arg_text = self._arglist(args, depth)
            ccname = _CALLCONV.get(cc, "")
            head = inner if not ccname or ccname in ("__cdecl", "__thiscall") else f"{ccname} {inner}".strip()
            if inner.startswith("("):
                head = inner if not ccname or ccname in ("__cdecl", "__thiscall") else \
                    "(" + ccname + " " + inner[1:]
            return self._declare(ret, f"{head}({arg_text})", depth + 1)

        if tag == "bits":
            return self._declare(rec[1], inner, depth + 1)

        return f"/*{tag}*/ {inner}".rstrip()

    def _arglist(self, arglist_ti: int, depth: int) -> str:
        rec = self.t.parse(arglist_ti)
        if not isinstance(rec, tuple) or rec[0] != "args":
            return ""
        items = rec[1]
        if not items:
            return "void"
        parts = []
        for a in items:
            if a == 0:                      # T_NOTYPE marks "..."
                parts.append("...")
            else:
                parts.append(self.declare(a, "", depth + 1))
        return ", ".join(parts)

    # -- sizes ---------------------------------------------------------------------------------------------------

    def size_of(self, ti: int) -> int:
        if ti < 0x1000:
            base = ti & 0xFF
            if (ti >> 8) & 7:
                return 4 if ((ti >> 8) & 7) != 6 else 8
            return {0x10: 1, 0x20: 1, 0x68: 1, 0x69: 1, 0x70: 1, 0x30: 1, 0x11: 2, 0x21: 2, 0x72: 2,
                    0x73: 2, 0x71: 2, 0x7A: 2, 0x31: 2, 0x12: 4, 0x22: 4, 0x74: 4, 0x75: 4, 0x32: 4,
                    0x40: 4, 0x08: 4, 0x7B: 4, 0x13: 8, 0x23: 8, 0x76: 8, 0x77: 8, 0x41: 8, 0x42: 10}.get(base, 4)
        rec = self.t.parse(ti)
        if isinstance(rec, Udt):
            if rec.is_fwdref:
                rec = self.t.parse(self.t.resolve(ti))
            return rec.size if isinstance(rec, Udt) else 0
        if isinstance(rec, EnumT):
            return self.size_of(rec.base)
        if not isinstance(rec, tuple):
            return 0
        if rec[0] == "mod":
            return self.size_of(rec[1])
        if rec[0] == "ptr":
            return 4 if (rec[2] >> 13) & 0x3F in (0, 4) else (rec[2] >> 13) & 0x3F
        if rec[0] == "arr":
            return rec[3]
        if rec[0] == "bits":
            return self.size_of(rec[1])
        return 0

    # -- function signatures -------------------------------------------------------------------------------------------------

    def function_info(self, typind: int) -> dict | None:
        """Return/arguments/convention/qualifiers of a LF_PROCEDURE / LF_MFUNCTION."""
        rec = self.t.parse(typind)
        if not isinstance(rec, tuple) or rec[0] not in ("proc", "mfunc"):
            return None
        is_m = rec[0] == "mfunc"
        ret = rec[1]
        cc = rec[4] if is_m else rec[2]
        args = self.t.parse(rec[7] if is_m else rec[4])
        arg_types = list(args[1]) if isinstance(args, tuple) and args[0] == "args" else []
        const = volatile = False
        cls = 0
        if is_m:
            cls = rec[2]
            this = rec[3]
            if this >= 0x1000:
                p = self.t.parse(this)
                if isinstance(p, tuple) and p[0] == "ptr" and p[1] >= 0x1000:
                    m = self.t.parse(p[1])
                    if isinstance(m, tuple) and m[0] == "mod":
                        const, volatile = bool(m[2] & 1), bool(m[2] & 2)
        return {"ret": ret, "cc": _CALLCONV.get(cc, ""), "args": arg_types, "const": const,
                "volatile": volatile, "class": cls, "member": is_m}

    # -- UDT declarations --------------------------------------------------------------------------------------------------------

    def udt_declaration(self, ti: int, indent: int = 0, nested_seen: set[int] | None = None,
                        display_name: str | None = None) -> str:
        """Declaration of a UDT or enum. The enclosing namespace/class is expected to be
        written by the caller, so only the leaf name is printed (override: display_name)."""
        t = self.t
        rec = t.parse(t.resolve(ti))
        if isinstance(rec, EnumT):
            return self._enum(rec, indent, display_name)
        if not isinstance(rec, Udt):
            return ""
        nested_seen = set() if nested_seen is None else nested_seen
        pad = "    " * indent
        out: list[str] = []

        leaf = display_name or split_qualified(rec.name)[1]
        head = f"{rec.keyword} {leaf}" if rec.name and not rec.is_anonymous else rec.keyword
        members = t.fields(rec.field_list)

        bases = []
        for m in members:
            if m.kind in ("base", "vbase"):
                acc = ACCESS_NAMES.get(m.attr & 3, "public")
                virt = "virtual " if m.kind == "vbase" else ""
                bases.append(f"{acc} {virt}{self.declare(m.type)}")
        if bases and rec.kind != LF_UNION:
            head += " : " + ", ".join(bases)

        size_comment = f"  // size 0x{rec.size:X}" if rec.size else ""
        out.append(f"{pad}{head}{size_comment}")
        out.append(f"{pad}{{")

        default_access = 1 if rec.kind == LF_CLASS else 3
        current = default_access if rec.kind != LF_UNION else 3
        class_leaf = split_qualified(rec.name)[1] if rec.name else ""
        if "<" in class_leaf:
            class_leaf = class_leaf[:class_leaf.index("<")]

        def label(acc: int):
            nonlocal current
            if acc != current and rec.kind != LF_UNION:
                out.append(f"{pad}{ACCESS_NAMES.get(acc, 'public')}:")
                current = acc

        # data members first (in layout order), then methods, statics, nested types
        for m in members:
            if m.kind == "field":
                label(m.attr & 3)
                out.append(pad + "    " + self._field_line(m, indent + 1, nested_seen))
            elif m.kind == "vfptr":
                label(3)
                out.append(f"{pad}    // vfptr at +0x0")

        for m in members:
            if m.kind == "static":
                label(m.attr & 3)
                out.append(f"{pad}    static {self.declare(m.type, m.name)};")

        for m in members:
            if m.kind != "method" or m.name in _HIDDEN_METHODS:
                continue
            for attr, typ, vbase in m.methods:
                label(attr & 3)
                out.append(pad + "    " + self._method_line(m.name, attr, typ, vbase, class_leaf))

        for m in members:
            if m.kind == "nested":
                inner_ti = t.nested_target(rec, m)
                label(3)
                if inner_ti and inner_ti not in nested_seen:
                    nested_seen.add(inner_ti)
                    out.append(self.udt_declaration(inner_ti, indent + 1, nested_seen))
                elif not inner_ti:
                    out.append(f"{pad}    typedef {self.declare(m.type, m.name)};")

        for m in members:
            if m.kind == "enumerator":
                out.append(f"{pad}    {m.name} = {m.value},")

        out.append(f"{pad}}};")
        return "\n".join(out)

    def _field_line(self, m: Member, indent: int, nested_seen: set[int]) -> str:
        t = self.t
        typ = m.type
        rec = t.parse(typ) if typ >= 0x1000 else None
        comment = f"  // +0x{m.offset:X}" if self.show_offsets else ""
        if rec is not None and not isinstance(rec, (Udt, EnumT)) and rec[0] == "bits":
            return f"{self.declare(rec[1], m.name)} : {rec[2]};{comment} bit {rec[3]}"
        if isinstance(rec, (Udt, EnumT)) and rec.is_anonymous:
            target = t.resolve(typ)
            body = self.udt_declaration(target, indent, nested_seen).lstrip()
            body = body.rstrip(";")
            return f"{body} {m.name};{comment}"
        return f"{self.declare(typ, m.name)};{comment}"

    def _method_line(self, name: str, attr: int, typ: int, vbase: int, class_leaf: str) -> str:
        info = self.function_info(typ)
        mprop = (attr >> 2) & 7
        prefix = ""
        if mprop in (MPROP_VIRTUAL, MPROP_INTRO, MPROP_PURE, MPROP_PURE_INTRO):
            prefix = "virtual "
        elif mprop == MPROP_STATIC:
            prefix = "static "
        elif mprop == MPROP_FRIEND:
            prefix = "friend "
        if not info:
            return f"{prefix}void {name}(/*?*/);"

        bare = name.lstrip("~").split("<")[0]
        is_dtor = name.startswith("~")
        is_ctor = not is_dtor and bare == class_leaf
        if is_ctor or is_dtor:
            name = ("~" if is_dtor else "") + bare        # injected-class-name: Ref, not Ref<T>
        is_conv = name.startswith("operator ") and name[9:10].isalpha()
        args = ", ".join(self.declare(a, "") for a in info["args"]) or "void"
        if args == "void" and not info["args"]:
            args = ""
        ret = "" if (is_ctor or is_dtor or is_conv) else self.declare(info["ret"], "") + " "
        cc = info["cc"]
        cc_text = f"{cc} " if cc and cc not in ("__cdecl", "__thiscall") else ""
        tail = (" const" if info["const"] else "") + (" volatile" if info["volatile"] else "")
        pure = " = 0" if mprop in (MPROP_PURE, MPROP_PURE_INTRO) else ""
        vslot = f"  // vtable +0x{vbase:X}" if mprop in (MPROP_INTRO, MPROP_PURE_INTRO) else ""
        return f"{prefix}{ret}{cc_text}{name}({args}){tail}{pure};{vslot}"

    def _enum(self, rec: EnumT, indent: int, display_name: str | None = None) -> str:
        pad = "    " * indent
        base = self.declare(rec.base)
        leaf = display_name or split_qualified(rec.name)[1]
        head = f"enum {leaf}" if rec.name and not rec.is_anonymous else "enum"
        lines = [f"{pad}{head} : {base}", f"{pad}{{"]
        for m in self.t.fields(rec.field_list):
            if m.kind == "enumerator":
                lines.append(f"{pad}    {m.name} = {m.value},")
        lines.append(f"{pad}}};")
        return "\n".join(lines)
