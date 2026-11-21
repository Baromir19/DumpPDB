"""Read a PDB directly (no DIA SDK, no Windows).

``PdbFile`` parses the MSF container, the DBI stream (modules, sections), the
module symbol streams (functions, data, line numbers) and the TPI stream (types),
and exposes them in the same shapes the restoration pipeline already uses
(:class:`~dumppdb_tools.restore.symbols.SymbolEntry`, C++ declaration text).

It is an independent implementation of the PDB 7 format, not a wrapper of the C++
tool: results are comparable with, but not guaranteed identical to, what the DIA
based ``PdbAPI.dll`` prints.
"""

from __future__ import annotations

import re
from collections import Counter, defaultdict
from pathlib import Path

from dumppdb_tools.pdbfile.dbi import Dbi, parse_dbi
from dumppdb_tools.pdbfile.info import PdbInfo, parse_info
from dumppdb_tools.pdbfile.msf import Msf, PdbFormatError
from dumppdb_tools.pdbfile.symbols import (
    LineTable,
    ModuleSymbols,
    Proc,
    parse_c13_lines,
    parse_module_symbols,
    parse_names_stream,
    parse_symrecord_stream,
)
from dumppdb_tools.pdbfile.types import (
    EnumT,
    Member,
    TypeRenderer,
    TypeTable,
    Udt,
)
from dumppdb_tools.restore.symbols import SymbolEntry, split_qualified

__all__ = ["PdbFile", "PdbFormatError"]

_GENERATED_HELPERS = ("__vecDelDtor", "__local_vftable_ctor_closure", "__vdtor", "__default_ctor_closure")


def _is_compiler_generated(name: str) -> bool:
    return name.startswith("`") or "`" in name or any(h in name for h in _GENERATED_HELPERS)


class PdbFile:
    """A parsed PDB. Heavy parts are computed on first use and cached."""

    def __init__(self, path: str | Path, int_style: str = "cstdint"):
        self.path = Path(path)
        self.msf = Msf(self.path)
        self.info: PdbInfo = parse_info(self.msf.stream(1))
        self.dbi: Dbi = parse_dbi(self.msf.stream(3), self.msf)
        self.types = TypeTable(self.msf.stream(2), int_style)
        self.renderer = TypeRenderer(self.types, int_style)
        self.int_style = int_style
        self._names = parse_names_stream(self.msf.stream(self.info.named_streams.get("/names", -1)))
        self._symbols: list[SymbolEntry] | None = None
        self._type_files: dict[str, Counter] = defaultdict(Counter)
        self._all_files: set[str] = set()

    # -- identity ----------------------------------------------------------------------------

    def identity(self) -> dict[str, str]:
        """Same keys as ``PdbApi_GetPdbInfo``."""
        return {"guid": self.info.guid, "age": str(self.info.age),
                "signature": f"0x{self.info.signature:08X}",
                "platform": f"0x{self.dbi.machine:X}"}

    # -- source files ---------------------------------------------------------------------------

    def source_files(self) -> list[str]:
        """Every source file the PDB mentions (modules' file lists + line tables)."""
        files = set(self._all_files)
        for lst in self.dbi.module_files:
            files.update(lst)
        return sorted(files, key=str.lower)

    # -- symbols --------------------------------------------------------------------------------------

    def symbols(self, progress=None) -> list[SymbolEntry]:
        """Functions, variables, statics, thunks and public symbols."""
        if self._symbols is None:
            self._symbols = self._build_symbols(progress)
        return self._symbols

    def _build_symbols(self, progress) -> list[SymbolEntry]:
        dbi, tt, rd = self.dbi, self.types, self.renderer
        out: list[SymbolEntry] = []
        complete = tt.complete_definitions()
        seen_data: set[tuple[int, int, str]] = set()
        udt_names = set(complete)

        def static_member_class(name: str) -> str:
            scope, _leaf = split_qualified(name)
            return scope if scope in udt_names else ""

        def data_entry(d, kind_hint: str | None, owner: str = "") -> SymbolEntry | None:
            rva = dbi.rva(d.section, d.offset)
            key = (d.section, d.offset, d.name)
            if key in seen_data:
                return None
            seen_data.add(key)
            cls = static_member_class(d.name) if not owner else ""
            if kind_hint:
                kind = kind_hint
            elif cls:
                kind = "static-member"
            else:
                kind = "global" if d.is_global else "file-static"
            decl = rd.declare(d.typind, d.name)
            flags = {"thread-local"} if d.thread_local else set()
            return SymbolEntry(kind=kind, name=d.name, rva=rva or None, section=d.section,
                               offset=d.offset, length=rd.size_of(d.typind), class_name=cls,
                               flags=flags, type_text=decl,
                               compiland=self._module_name(d.module))

        n_modules = len(dbi.modules)
        for mod in dbi.modules:
            if progress and mod.index % 100 == 0:
                progress(f"modules {mod.index}/{n_modules}")
            if mod.stream == 0xFFFF:
                continue
            data = self.msf.stream(mod.stream)
            ms = parse_module_symbols(data, mod.sym_size, mod.index)
            lines = parse_c13_lines(data, mod.sym_size + mod.c11_size, mod.c13_size,
                                    self._names, dbi.rva)
            self._all_files.update(set(lines.files))
            for proc in ms.procs:
                out.append(self._function_entry(proc, mod.name, lines))
            for d in ms.data:
                e = data_entry(d, None)
                if e:
                    out.append(e)
            for name, typ, seg, off, proc_idx in ms.static_locals:
                owner = ms.procs[proc_idx].name
                rva = dbi.rva(seg, off)
                dedupe = (seg, off, name)
                if dedupe in seen_data:
                    continue
                seen_data.add(dedupe)
                out.append(SymbolEntry(
                    kind="static-local", name=f"{owner}::{name}", rva=rva or None, section=seg,
                    offset=off, length=rd.size_of(typ), compiland=mod.name,
                    type_text=rd.declare(typ, name), flags={"static-local"}))
            for tname, seg, off, length in ms.thunks:
                out.append(SymbolEntry(kind="thunk", name=tname, rva=dbi.rva(seg, off) or None,
                                       section=seg, offset=off, length=length, compiland=mod.name))

        gdata, publics, _udts = parse_symrecord_stream(self.msf.stream(dbi.symrecord_stream))
        for d in gdata:
            e = data_entry(d, None)
            if e:
                out.append(e)
        for pub in publics:
            fn = pub.is_function
            out.append(SymbolEntry(
                kind="public-function" if fn else "public-data", name=pub.name,
                undecorated=_simple_undecorate(pub.name), rva=dbi.rva(pub.section, pub.offset) or None,
                section=pub.section, offset=pub.offset))
        return out

    def _module_name(self, index: int) -> str:
        return self.dbi.modules[index].name if 0 <= index < len(self.dbi.modules) else ""

    # -- one function -------------------------------------------------------------------------------------

    def _function_entry(self, proc: Proc, module_name: str, lines: LineTable) -> SymbolEntry:
        rd, tt, dbi = self.renderer, self.types, self.dbi
        rva = dbi.rva(proc.section, proc.offset)
        fi = rd.function_info(proc.typind)

        class_name = ""
        flags: set[str] = set()
        if fi and fi["member"] and fi["class"]:
            cls = tt.parse(tt.resolve(fi["class"]))
            if isinstance(cls, Udt):
                class_name = cls.name
        if _is_compiler_generated(proc.name):
            flags.add("compiler-generated")
        if not proc.is_global and not class_name:
            flags.add("file-local")

        ret_text = params = quals = ""
        if fi:
            if fi["cc"] and fi["cc"] != "__thiscall":
                flags.add(fi["cc"])
            elif fi["cc"] == "__thiscall":
                flags.add("__thiscall")
            leaf = split_qualified(proc.name)[1]
            bare = leaf.lstrip("~").split("<")[0]
            scope_leaf = split_qualified(split_qualified(proc.name)[0])[1].split("<")[0]
            no_return = leaf.startswith("~") or (class_name and bare == scope_leaf) or \
                (leaf.startswith("operator ") and leaf[9:10].isalpha())
            if not no_return:
                ret_text = rd.declare(fi["ret"], "")
            params = self._param_list(proc, fi)
            quals = " ".join(q for q, on in (("const", fi["const"]), ("volatile", fi["volatile"])) if on)

        src = line = 0
        source = ""
        if rva:
            span = lines.span(rva, proc.length)
            if span:
                source, line = span[0], span[1]
                if class_name:
                    self._type_files[class_name][source] += 1

        return SymbolEntry(kind="function", name=proc.name, rva=rva or None, section=proc.section,
                           offset=proc.offset, length=proc.length, class_name=class_name,
                           compiland=module_name, source=source, line=line, flags=flags,
                           type_text=ret_text, params=params, qualifiers=quals)

    def _param_list(self, proc: Proc, fi: dict) -> str:
        """Parameter list with names when the local symbols identify them safely."""
        rd = self.renderer
        arg_types = list(fi["args"])
        varargs = bool(arg_types) and arg_types[-1] == 0
        if varargs:
            arg_types = arg_types[:-1]

        cand = sorted((p for p in proc.params
                       if p.offset > 0 and p.name and p.name != "this" and not p.name.startswith("$")),
                      key=lambda p: p.offset)
        named = len(cand) == len(arg_types) and all(c.name.isidentifier() for c in cand)

        parts = []
        for i, a in enumerate(arg_types):
            parts.append(rd.declare(a, cand[i].name if named else ""))
        if varargs:
            parts.append("...")
        return ", ".join(parts)

    # -- types ---------------------------------------------------------------------------------------------------

    def type_names(self) -> list[str]:
        """Names of the top-level types: complete, named, not nested in another type."""
        tt = self.types
        nested: set[int] = set()
        for rec in tt.iter_definitions():
            if isinstance(rec, Udt):
                for m in tt.fields(rec.field_list):
                    if m.kind == "nested":
                        target = tt.nested_target(rec, m)
                        if target:
                            nested.add(target)
        names = []
        seen = set()
        for rec in tt.iter_definitions():
            if rec.index in nested or rec.is_anonymous:
                continue
            if rec.name.startswith(("$", "_s__", "__")) or "<unnamed" in rec.name:
                continue                      # compiler-generated EH/RTTI helper types
            if rec.name in seen:
                continue
            seen.add(rec.name)
            names.append(rec.name)
        return sorted(names, key=str.lower)

    def dump_type(self, name: str) -> str:
        """C++ declaration of a type, wrapped in its namespaces."""
        comp = self.types.complete_definitions()
        ti = comp.get(name)
        if ti is None:
            raise KeyError(name)
        scope, _leaf = split_qualified(name)
        parts = _split_scope(scope) if scope else []
        # A type nested in a class is printed inside it, so only real namespaces wrap the type.
        namespaces = [p for p in parts]
        body = self.renderer.udt_declaration(ti, indent=len(namespaces), display_name=_leaf)
        out: list[str] = []
        for i, ns in enumerate(namespaces):
            out.append("    " * i + ("namespace" if "anonymous namespace" in ns else f"namespace {ns}"))
            out.append("    " * i + "{")
        out.append(body)
        for i in range(len(namespaces) - 1, -1, -1):
            out.append("    " * i + "}")
        return "\n".join(out)

    def type_sources(self, name: str) -> list[str]:
        """Files that define members of the type, most frequent first (needs symbols())."""
        self.symbols()
        files = self._type_files.get(name)
        if not files:
            return []
        return [f for f, _ in files.most_common()]


def _split_scope(scope: str) -> list[str]:
    """``A::B<C::D>::E`` -> ``["A", "B<C::D>", "E"]`` (template aware)."""
    parts, depth, cur, i = [], 0, "", 0
    while i < len(scope):
        c = scope[i]
        if c in "<([":
            depth += 1
        elif c in ">)]":
            depth = max(0, depth - 1)
        if c == ":" and depth == 0 and scope.startswith("::", i):
            parts.append(cur)
            cur = ""
            i += 2
            continue
        cur += c
        i += 1
    if cur:
        parts.append(cur)
    return parts


_C_DECORATION = re.compile(r"^[_@]?([A-Za-z_]\w*)(?:@\d+)?$")


def _simple_undecorate(name: str) -> str:
    """Undecorate plain C names (``_foo@8`` -> ``foo``); C++ names are left to a real undecorator."""
    if name.startswith("?"):
        return ""
    m = _C_DECORATION.match(name)
    return m.group(1) if m else ""
