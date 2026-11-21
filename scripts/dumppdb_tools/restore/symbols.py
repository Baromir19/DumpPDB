"""Parsing of the symbol table exported by ``PdbApi_DumpSymbolTable`` / ``-symbols``.

The native side writes one tab-separated line per function, variable, thunk and
public symbol (see ``GlobalRenderer::tsvHeader`` in the C++ sources)::

    kind name undecorated rva section offset length class compiland source line
    flags type params qualifiers

This module turns those lines into :class:`SymbolEntry` objects and provides
the small amount of C++ name handling the restoration code needs (splitting a
qualified name into scope and leaf without being fooled by templates).
"""

from __future__ import annotations

from dataclasses import dataclass, field

ANON_NS = "`anonymous namespace'"

# Calling conventions that are the default for the kind of function, i.e. the
# ones that must NOT be written into a definition (they would be redundant,
# and ``__thiscall`` cannot even be spelled for members in modern MSVC).
_DEFAULT_CCS = frozenset({"__cdecl", "__thiscall"})

FUNCTION_KINDS = frozenset({"function", "thunk", "public-function"})
DATA_KINDS = frozenset(
    {"global", "file-static", "static-member", "static-local", "constant", "public-data"})


def split_qualified(name: str) -> tuple[str, str]:
    """Split ``A::B::f<C::D>`` into ``("A::B", "f<C::D>")``.

    ``::`` inside ``<>``, ``()`` or ``[]`` and inside the quoted
    ```anonymous namespace'`` marker is not a scope separator.
    """
    depth = 0
    last = -1
    i = 0
    n = len(name)
    while i < n:
        c = name[i]
        if c in "<([":
            depth += 1
        elif c in ">)]":
            depth = max(0, depth - 1)
        elif c == "`":
            close = name.find("'", i + 1)
            if close != -1:
                i = close
        elif c == ":" and depth == 0 and name.startswith("::", i):
            last = i
            i += 1
        i += 1
    if last == -1:
        return "", name
    return name[:last], name[last + 2:]


@dataclass(slots=True)
class SymbolEntry:
    kind: str = ""
    name: str = ""
    undecorated: str = ""
    rva: int | None = None
    section: int = 0
    offset: int = 0
    length: int = 0
    class_name: str = ""
    compiland: str = ""
    source: str = ""
    line: int = 0
    flags: set[str] = field(default_factory=set)
    type_text: str = ""
    params: str = ""
    qualifiers: str = ""

    # -- classification -----------------------------------------------------

    @property
    def is_function(self) -> bool:
        return self.kind == "function"

    @property
    def is_data(self) -> bool:
        return self.kind in ("global", "file-static", "static-member", "static-local", "constant")

    @property
    def is_public(self) -> bool:
        return self.kind.startswith("public-")

    @property
    def is_member(self) -> bool:
        return bool(self.class_name)

    @property
    def is_file_local(self) -> bool:
        return self.kind == "file-static" or "file-local" in self.flags

    @property
    def is_compiler_generated(self) -> bool:
        return "compiler-generated" in self.flags

    @property
    def is_template_instance(self) -> bool:
        scope, leaf = split_qualified(self.name)
        return "<" in leaf or "<" in scope

    @property
    def scope(self) -> str:
        return split_qualified(self.name)[0]

    @property
    def leaf(self) -> str:
        return split_qualified(self.name)[1]

    @property
    def calling_convention(self) -> str:
        for f in sorted(self.flags):
            if f.startswith("__"):
                return f
        return ""

    @property
    def written_calling_convention(self) -> str:
        """Calling convention that has to appear in a definition ('' = default)."""
        cc = self.calling_convention
        return "" if cc in _DEFAULT_CCS else cc

    # -- rendering ------------------------------------------------------------

    def definition_header(self) -> str:
        """``ret [cc] Scope::name(params) qualifiers`` — a C++ function head."""
        parts: list[str] = []
        if self.is_file_local and not self.is_member:
            parts.append("static")
        if self.type_text:
            parts.append(self.type_text)
        cc = self.written_calling_convention
        if cc:
            parts.append(cc)
        parts.append(self.name.replace(ANON_NS + "::", ""))
        head = " ".join(parts) + "(" + self.params + ")"
        if self.qualifiers:
            head += " " + self.qualifiers
        return head


def _int(text: str, base: int = 10) -> int:
    text = text.strip()
    if not text:
        return 0
    try:
        return int(text, base)
    except ValueError:
        return 0


def parse_symbol_table(text: str) -> list[SymbolEntry]:
    """Parse the TSV produced by the native symbol-table export.

    Tolerates a missing header, extra/missing trailing columns (older DLLs
    exported fewer columns) and blank lines.
    """
    entries: list[SymbolEntry] = []
    for raw in text.splitlines():
        if not raw.strip() or raw.startswith("kind\t"):
            continue
        cols = raw.split("\t")
        cols += [""] * (15 - len(cols))

        rva_text = cols[3].strip()
        entry = SymbolEntry(
            kind=cols[0],
            name=cols[1],
            undecorated=cols[2],
            rva=int(rva_text, 16) if rva_text else None,
            section=_int(cols[4]),
            offset=_int(cols[5], 16) if cols[5].strip().lower().startswith("0x") else _int(cols[5]),
            length=_int(cols[6]),
            class_name=cols[7],
            compiland=cols[8],
            source=cols[9],
            line=_int(cols[10]),
            flags={f for f in cols[11].split(",") if f},
            type_text=cols[12],
            params=cols[13],
            qualifiers=cols[14],
        )
        if entry.kind and entry.name:
            entries.append(entry)
    return entries


TSV_HEADER = ("kind\tname\tundecorated\trva\tsection\toffset\tlength\tclass\tcompiland\tsource\tline"
              "\tflags\ttype\tparams\tqualifiers")


def format_symbol_table(entries: list[SymbolEntry]) -> str:
    """Inverse of :func:`parse_symbol_table` (same layout as the native TSV export)."""
    def clean(text: str) -> str:
        return text.replace("\t", " ").replace("\n", " ").replace("\r", " ")

    lines = [TSV_HEADER]
    for e in entries:
        lines.append("\t".join([
            clean(e.kind), clean(e.name), clean(e.undecorated),
            f"0x{e.rva:08X}" if e.rva is not None else "",
            str(e.section) if e.section else "",
            f"0x{e.offset:08X}" if e.section else "",
            str(e.length) if e.length else "",
            clean(e.class_name), clean(e.compiland), clean(e.source),
            str(e.line) if e.line else "",
            ",".join(sorted(e.flags)),
            clean(e.type_text), clean(e.params), clean(e.qualifiers),
        ]))
    return "\n".join(lines) + "\n"
