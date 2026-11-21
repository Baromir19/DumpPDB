"""Plan and write a restored source tree.

Inputs (all optional, use what you have):

* the symbol table of a PDB (functions with source file:line, globals,
  file statics, static members),
* the C++ declaration text of every type and the source files it appears in,
* the exe analysis (class -> source file, function -> source file, every
  spelling of every embedded source path).

The result is a directory of ``.h`` / ``.cpp`` files that mirrors the original
layout, plus ``_meta/`` with machine-readable maps (RVA -> file:line -> name)
that the disassembler scripts and later restoration passes use.

Nothing here talks to the native DLL, so it can be unit-tested anywhere.
"""

from __future__ import annotations

import csv
import re
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

from dumppdb_tools.recovery.case_dict import CaseDict
from dumppdb_tools.restore.exe_analysis import ExeAnalysisData
from dumppdb_tools.restore.symbols import ANON_NS, SymbolEntry

HEADER_EXTS = (".h", ".hpp", ".hxx", ".inl", ".inc")
IMPL_EXTS = (".cpp", ".cc", ".cxx", ".c")
_BAD_CHARS = re.compile(r'[<>:"|?*\x00-\x1f]')
_DRIVE = re.compile(r"^[A-Za-z]:")


# ---------------------------------------------------------------------------
# Path helpers
# ---------------------------------------------------------------------------

def _split_path(path: str) -> list[str]:
    """``E:\\a/b\\..\\c.cpp`` -> ``["a", "c.cpp"]`` (drive dropped, dots resolved)."""
    path = path.replace("\\", "/")
    path = _DRIVE.sub("", path)
    out: list[str] = []
    for part in path.split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            if out:
                out.pop()
            continue
        out.append(part)
    return out


def _ext(name: str) -> str:
    dot = name.rfind(".")
    return name[dot:].lower() if dot > 0 else ""


def _stem(name: str) -> str:
    dot = name.rfind(".")
    return name[:dot] if dot > 0 else name


def is_header(path: str) -> bool:
    return _ext(path) in HEADER_EXTS


def is_impl(path: str) -> bool:
    return _ext(path) in IMPL_EXTS


def detect_root(paths: list[str], dominant: float = 0.6, sibling: float = 0.10) -> list[str]:
    """Project root: the directory where the source tree starts to branch.

    PDB source paths mix the project (``e:/perforce/game/...``) with the CRT, the
    SDK and third-party code, and a typical project is laid out as
    ``<root>/{Engine,Game,Util,...}``. Starting at the top, the walk follows the
    dominant child directory for as long as it holds at least *dominant* of the
    files below and no other branch (or loose file) reaches *sibling*. It stops at
    the first real fork, which is the project root; small outliers (CRT, SDK) are
    below *sibling* and therefore do not stop the descent. Everything outside the
    root is filed under ``_external``.

    Files are counted once however often (and in however many spellings) they
    are mentioned.
    """
    unique: dict[str, str] = {}
    for x in paths:
        if x:
            unique.setdefault("/".join(p.lower() for p in _split_path(x)), x)
    files = list(unique.values())

    dirs = [tuple(p.lower() for p in _split_path(x)[:-1]) for x in files]
    originals: dict[tuple[str, ...], list[str]] = {}
    for x in files:
        parts = _split_path(x)[:-1]
        originals.setdefault(tuple(p.lower() for p in parts), parts)

    prefix: tuple[str, ...] = ()
    while True:
        below = [d for d in dirs if d[:len(prefix)] == prefix]
        if not below:
            break
        direct = sum(1 for d in below if len(d) == len(prefix))
        deeper = [d for d in below if len(d) > len(prefix)]
        if not deeper:
            break
        counts = Counter(d[len(prefix)] for d in deeper)
        ranked = counts.most_common(2)
        top, top_n = ranked[0]
        second_n = ranked[1][1] if len(ranked) > 1 else 0
        total = len(below)
        if top_n / total < dominant:
            break
        if second_n / total >= sibling or direct / total >= sibling:
            break
        prefix += (top,)

    for key, parts in originals.items():
        if key[:len(prefix)] == prefix:
            return parts[:len(prefix)]
    return list(prefix)


# ---------------------------------------------------------------------------
# Plan model
# ---------------------------------------------------------------------------

@dataclass(slots=True)
class FilePlan:
    rel: str                      # relative path with forward slashes
    external: bool = False
    types: list[tuple[str, str]] = field(default_factory=list)   # (name, declaration text)
    functions: list[SymbolEntry] = field(default_factory=list)
    variables: list[SymbolEntry] = field(default_factory=list)
    origin: str = ""              # original source path as seen in the PDB/exe

    @property
    def stem_key(self) -> str:
        return (_stem(self.rel)).lower()


@dataclass(slots=True)
class ProjectPlan:
    files: dict[str, FilePlan] = field(default_factory=dict)
    unassigned_functions: list[SymbolEntry] = field(default_factory=list)
    unassigned_variables: list[SymbolEntry] = field(default_factory=list)
    publics: list[SymbolEntry] = field(default_factory=list)
    root: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    def stats(self) -> dict[str, int]:
        return {
            "files": len(self.files),
            "headers": sum(1 for f in self.files.values() if is_header(f.rel)),
            "implementations": sum(1 for f in self.files.values() if is_impl(f.rel)),
            "types": sum(len(f.types) for f in self.files.values()),
            "functions": sum(len(f.functions) for f in self.files.values()),
            "variables": sum(len(f.variables) for f in self.files.values()),
            "unassigned_functions": len(self.unassigned_functions),
            "unassigned_variables": len(self.unassigned_variables),
            "publics": len(self.publics),
        }


# ---------------------------------------------------------------------------
# Builder
# ---------------------------------------------------------------------------

class ProjectBuilder:
    """Collects symbols/types and decides which file each one belongs to."""

    def __init__(self, root: str | None = None, keep_external: bool = True,
                 include_compiler_generated: bool = False,
                 exe: ExeAnalysisData | None = None):
        self._root_override = root
        self.keep_external = keep_external
        self.include_compiler_generated = include_compiler_generated
        self.exe = exe

        self._symbols: list[SymbolEntry] = []
        self._types: list[tuple[str, list[str], str]] = []  # name, sources, text
        self._extra_paths: list[str] = []

    # -- input --------------------------------------------------------------------

    def add_symbols(self, entries: list[SymbolEntry]) -> None:
        self._symbols.extend(entries)

    def add_type(self, name: str, sources: list[str], text: str) -> None:
        self._types.append((name, [s for s in sources if s], text))

    def add_exe_functions(self, min_confidence: int = 2) -> int:
        """Add functions named by the exe analysis (no PDB needed).

        They carry no signature (RTTI cannot tell parameters), so they are
        written as commented stubs that record name, address and size. Returns
        the number of functions added.
        """
        if not self.exe:
            return 0
        added = 0
        for f in self.exe.functions.values():
            if not f.name or f.confidence < min_confidence:
                continue
            self._symbols.append(SymbolEntry(
                kind="function", name=f.name, rva=f.rva, length=f.size,
                class_name=f.class_name, source=f.source,
                flags={"no-signature"}))
            added += 1
        return added

    def add_known_paths(self, paths: list[str]) -> None:
        """Extra spellings (e.g. from exe strings) used for case recovery / root detection."""
        self._extra_paths.extend(paths)

    # -- planning -------------------------------------------------------------------

    def build(self) -> ProjectPlan:
        plan = ProjectPlan()

        all_paths: list[str] = list(self._extra_paths)
        all_paths += [s.source for s in self._symbols if s.source]
        for _, srcs, _ in self._types:
            all_paths += srcs
        if self.exe:
            all_paths += self.exe.spellings
            all_paths += [c.source for c in self.exe.classes.values() if c.source]

        if self._root_override:
            root = _split_path(self._root_override)
        else:
            root = detect_root([p for p in all_paths if p])
        plan.root = root
        self._root = [r.lower() for r in root]
        self._case = self._build_case_dict(all_paths)
        self._canon_cache: dict[str, tuple[str, bool]] = {}

        # (1) which file does each function live in?
        compiland_files: dict[str, Counter] = defaultdict(Counter)
        for s in self._symbols:
            if s.is_function and s.source:
                compiland_files[s.compiland][self.canon(s.source)[0]] += 1
        compiland_main = {c: cnt.most_common(1)[0][0] for c, cnt in compiland_files.items() if c}

        class_files: dict[str, Counter] = defaultdict(Counter)

        # Where each type lives; a member without line info is implemented next to it.
        type_dest: dict[str, str] = {}
        for name, srcs, _ in self._types:
            dest = self._type_dest(name, srcs, class_files)
            if dest:
                type_dest[name] = dest

        def function_dest(s: SymbolEntry) -> str | None:
            if s.source:
                return self.canon(s.source)[0]
            if s.compiland and s.compiland in compiland_main:
                return compiland_main[s.compiland]
            if self.exe and s.rva is not None:
                src = self.exe.function_source(s.rva)
                if src:
                    return self.canon(src)[0]
            if s.class_name and s.class_name in type_dest:
                return self._impl_for(type_dest[s.class_name])
            return None

        function_dests: list[tuple[SymbolEntry, str | None]] = []
        for s in self._symbols:
            if s.is_public:
                plan.publics.append(s)
                continue
            if not s.is_function:
                continue
            if s.is_compiler_generated and not self.include_compiler_generated:
                continue
            dest = function_dest(s)
            function_dests.append((s, dest))
            if dest and s.class_name:
                class_files[s.class_name][dest] += 1

        for s, dest in function_dests:
            self._place_function(plan, s, dest)

        # (2) variables
        for s in self._symbols:
            if not s.is_data:
                continue
            dest = self._variable_dest(s, compiland_main, class_files)
            if dest is None and s.class_name in type_dest:
                dest = self._impl_for(type_dest[s.class_name])
            if dest is None:
                plan.unassigned_variables.append(s)
            else:
                self._file(plan, dest).variables.append(s)

        # (3) types
        for name, srcs, text in self._types:
            dest = type_dest.get(name) or self._type_dest(name, srcs, class_files)
            if dest is None:
                # Keep it anyway: a type with no known file is still a type.
                dest = "_unassigned/types.h"
                plan.notes.append(f"type without a source file (-> {dest}): {name}")
            self._file(plan, dest).types.append((name, text))

        # (4) classes known only from RTTI (no PDB type text) -> skeleton note in header
        if self.exe:
            have = {n for f in plan.files.values() for n, _ in f.types}
            for cname, c in self.exe.classes.items():
                if cname in have or not c.source:
                    continue
                dest = self._header_for(c.source)
                self._file(plan, dest).types.append((cname, self._rtti_stub(c)))

        self._finalize(plan)
        return plan

    # -- helpers ------------------------------------------------------------------

    @staticmethod
    def _rtti_stub(c) -> str:
        bases = (" : " + ", ".join("public " + b for b in c.bases)) if c.bases else ""
        lines = [f"class {c.name}{bases}", "{", "    // Recovered from RTTI only (no PDB type information)."]
        lines.append(f"    // vftable RVA 0x{c.primary_vftable_rva:08X}, {c.slot_count} virtual slots")
        for i, rva in enumerate(c.ctor_rvas):
            lines.append(f"    // constructor candidate {i}: RVA 0x{rva:08X}")
        if c.dtor_rva is not None:
            lines.append(f"    // destructor: RVA 0x{c.dtor_rva:08X}")
        lines.append("};")
        return "\n".join(lines)

    def _build_case_dict(self, paths: list[str]) -> CaseDict:
        cd = CaseDict()
        for p in paths:
            parts = _split_path(p)
            for i, part in enumerate(parts):
                cd.add(_stem(part) if i == len(parts) - 1 else part)
        return cd

    def canon(self, path: str) -> tuple[str, bool]:
        """(relative path with recovered case, is_external)."""
        hit = self._canon_cache.get(path)
        if hit:
            return hit

        parts = _split_path(path)
        lower = [p.lower() for p in parts]
        n = len(self._root)
        external = not (n and lower[:n] == self._root) if n else False
        rel_parts = parts[n:] if (n and not external) else parts

        fixed: list[str] = []
        for i, part in enumerate(rel_parts):
            last = i == len(rel_parts) - 1
            if last:
                stem, ext = _stem(part), _ext(part)
                stem = self._case.get(stem) or stem
                part = stem + ext
            else:
                part = self._case.get(part) or part
            fixed.append(_BAD_CHARS.sub("_", part))

        rel = "/".join(fixed) if fixed else "_unknown.cpp"
        if external:
            rel = "_external/" + rel
        self._canon_cache[path] = (rel, external)
        return rel, external

    def _file(self, plan: ProjectPlan, rel: str) -> FilePlan:
        fp = plan.files.get(rel)
        if fp is None:
            fp = FilePlan(rel=rel, external=rel.startswith("_external/"))
            plan.files[rel] = fp
        return fp

    def _place_function(self, plan: ProjectPlan, s: SymbolEntry, dest: str | None) -> None:
        if dest is None:
            plan.unassigned_functions.append(s)
            return
        if dest.startswith("_external/") and not self.keep_external:
            return
        self._file(plan, dest).functions.append(s)

    def _variable_dest(self, s: SymbolEntry, compiland_main: dict[str, str],
                       class_files: dict[str, Counter]) -> str | None:
        if s.kind == "static-member" or (s.class_name and s.kind != "file-static"):
            cnt = class_files.get(s.class_name)
            if cnt:
                impl = [(f, n) for f, n in cnt.most_common() if is_impl(f)]
                return (impl or cnt.most_common())[0][0]
        if s.compiland and s.compiland in compiland_main:
            return compiland_main[s.compiland]
        if self.exe and s.class_name:
            src = self.exe.class_source(s.class_name)
            if src:
                return self.canon(src)[0]
        return None

    @staticmethod
    def _impl_for(rel: str) -> str:
        """``a/b.h`` -> ``a/b.cpp``; implementation files are returned unchanged."""
        return rel if is_impl(rel) else _stem(rel) + ".cpp"

    @staticmethod
    def _header_for_rel(rel: str) -> str:
        return rel if is_header(rel) else _stem(rel) + ".h"

    def _header_for(self, source: str) -> str:
        rel = self.canon(source)[0]
        if is_header(rel):
            return rel
        return _stem(rel) + ".h"

    def _type_dest(self, name: str, srcs: list[str],
                   class_files: dict[str, Counter]) -> str | None:
        cands = [self.canon(s)[0] for s in srcs]
        headers = [c for c in cands if is_header(c)]
        if headers:
            return Counter(headers).most_common(1)[0][0]
        if cands:
            # Only member bodies in .cpp files were seen (the declaration itself carries no
            # line info): the class lives in the header next to the most common .cpp.
            return self._header_for_rel(Counter(cands).most_common(1)[0][0])
        if self.exe:
            src = self.exe.class_source(name)
            if src:
                return self._header_for(src)
        cnt = class_files.get(name)
        if cnt:
            rel = cnt.most_common(1)[0][0]
            return rel if is_header(rel) else _stem(rel) + ".h"
        return None

    def _finalize(self, plan: ProjectPlan) -> None:
        for fp in plan.files.values():
            fp.types.sort(key=lambda t: t[0].lower())
            fp.functions.sort(key=lambda f: (f.line or 10**9, f.rva or 0, f.name))
            fp.variables.sort(key=lambda v: (v.rva or 0, v.name))


# ---------------------------------------------------------------------------
# Emission
# ---------------------------------------------------------------------------

def _function_comment(s: SymbolEntry) -> str:
    bits = []
    if s.rva is not None:
        bits.append(f"RVA 0x{s.rva:08X}")
    if s.length:
        bits.append(f"{s.length} bytes")
    if s.line:
        bits.append(f"line {s.line}")
    return ", ".join(bits)


def render_function(s: SymbolEntry) -> str:
    """A function skeleton: signature + TODO body (or a comment for templates)."""
    note = _function_comment(s)
    if "no-signature" in s.flags:
        return f"// {s.name}(...)  // {note}  (signature unknown: recovered from RTTI only)\n"
    head = s.definition_header()

    if s.is_template_instance:
        return f"// template instantiation (the definition lives in a header)\n// {head};  // {note}\n"

    scope = s.scope
    wrap = scope.startswith(ANON_NS)
    body = (
        f"// {note}\n"
        f"{head}\n"
        "{\n"
        f"    // TODO: not recovered ({note})\n"
        "}\n"
    )
    if wrap:
        return "namespace\n{\n" + body + "}\n"
    return body


def render_variable(s: SymbolEntry) -> str:
    note = f"  // RVA 0x{s.rva:08X}" if s.rva is not None else ""
    decl = s.type_text.replace(ANON_NS + "::", "")
    if not decl:
        return f"// {s.name}{note}\n"
    if s.kind == "file-static" and not decl.startswith("static "):
        decl = "static " + decl
    if s.kind == "constant":
        decl = "constexpr " + decl
    if s.kind == "static-local":
        return f"// static local: {decl};{note}\n"
    return f"{decl};{note}\n"


def render_file(fp: FilePlan, header_stems: set[str]) -> str:
    out: list[str] = []
    header = is_header(fp.rel)

    out.append(f"// {fp.rel}")
    if fp.origin:
        out.append(f"// original path: {fp.origin}")
    out.append("// Restored by DumpPDB. Bodies are not recovered; signatures and")
    out.append("// addresses come from the PDB / RTTI.")
    out.append("")

    if header:
        out.append("#pragma once")
        out.append("")
        for _, text in fp.types:
            out.append(text.rstrip())
            out.append("")
        return "\n".join(out).rstrip() + "\n"

    stem = _stem(fp.rel)
    if stem.lower() in header_stems:
        out.append(f'#include "{stem.split("/")[-1]}.h"')
        out.append("")

    for _, text in fp.types:  # types that only exist in this translation unit
        out.append(text.rstrip())
        out.append("")

    if fp.variables:
        out.append("// ---- variables ----")
        for v in fp.variables:
            out.append(render_variable(v).rstrip())
        out.append("")

    if fp.functions:
        out.append("// ---- functions ----")
        out.append("")
        for f in fp.functions:
            out.append(render_function(f).rstrip())
            out.append("")

    return "\n".join(out).rstrip() + "\n"


def write_project(plan: ProjectPlan, out_dir: str | Path) -> dict[str, int]:
    """Write the planned tree and the ``_meta`` maps. Returns plan statistics."""
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    meta = out / "_meta"
    meta.mkdir(exist_ok=True)

    header_stems = {fp.stem_key for fp in plan.files.values() if is_header(fp.rel)}

    for fp in plan.files.values():
        path = out / fp.rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(render_file(fp, header_stems), encoding="utf-8", newline="\n")

    # Everything that could not be placed.
    if plan.unassigned_functions or plan.unassigned_variables:
        fp = FilePlan(rel="_unassigned/unassigned.cpp",
                      functions=plan.unassigned_functions,
                      variables=plan.unassigned_variables)
        fp.functions.sort(key=lambda f: (f.rva or 0, f.name))
        p = out / fp.rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(render_file(fp, header_stems), encoding="utf-8", newline="\n")

    with (meta / "function_map.csv").open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["rva", "length", "name", "file", "line", "compiland", "class"])
        for fp in plan.files.values():
            for f in fp.functions:
                w.writerow([f"0x{f.rva:08X}" if f.rva is not None else "", f.length,
                            f.name, fp.rel, f.line or "", f.compiland, f.class_name])
        for f in plan.unassigned_functions:
            w.writerow([f"0x{f.rva:08X}" if f.rva is not None else "", f.length,
                        f.name, "_unassigned/unassigned.cpp", "", f.compiland, f.class_name])

    with (meta / "variable_map.csv").open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["rva", "size", "kind", "name", "file", "type"])
        for fp in plan.files.values():
            for v in fp.variables:
                w.writerow([f"0x{v.rva:08X}" if v.rva is not None else "", v.length,
                            v.kind, v.name, fp.rel, v.type_text])
        for v in plan.unassigned_variables:
            w.writerow([f"0x{v.rva:08X}" if v.rva is not None else "", v.length,
                        v.kind, v.name, "_unassigned/unassigned.cpp", v.type_text])

    with (meta / "publics.txt").open("w", encoding="utf-8", newline="\n") as fh:
        for p_ in sorted(plan.publics, key=lambda s: s.rva or 0):
            rva = f"0x{p_.rva:08X}" if p_.rva is not None else "          "
            fh.write(f"{rva}  {p_.kind:<16} {p_.undecorated or p_.name}\n")

    stats = plan.stats()
    with (meta / "restore_report.txt").open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("DumpPDB restoration report\n==========================\n")
        fh.write("project root: " + ("/".join(plan.root) or "(none detected)") + "\n\n")
        for k, v in stats.items():
            fh.write(f"{k:<24}{v}\n")
        if plan.notes:
            fh.write("\nNotes\n-----\n")
            for n in plan.notes[:2000]:
                fh.write(n + "\n")
    return stats
