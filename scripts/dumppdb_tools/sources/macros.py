"""Scan a source tree for types introduced by generating macros.

These helpers are shared by the ``compare_type_sources`` (reporting types
defined via macros) and ``reconstruct_sources`` (skipping types that are
already produced by macros) CLI tools.
"""

import re
from pathlib import Path

from dumppdb_tools.sources.project import SOURCE_EXTENSIONS, paths_match
from dumppdb_tools.sources.type_sources import TypeSource

# An identifier-or-qualified-identifier used as the macro type-name argument.
TYPE_NAME_RE = r"[A-Za-z_][A-Za-z0-9_:]*"


def compile_pattern(line: str) -> re.Pattern:
    """Compile a ``<type>`` call template into a regex that captures the type name.

    The line is the function-like macro that opens a type definition, with
    ``<type>`` standing for the type name (the first macro argument). Everything
    between the captured name and the closing parenthesis is ignored, so extra
    arguments such as ``, struct`` or ``, class Foo : Base`` are handled
    transparently:

        BUILD_DEFINED_INHERITED_TYPE_BEGIN(<type>)
            matches  BUILD_DEFINED_INHERITED_TYPE_BEGIN(Foo, class Foo : Base)

    If the line contains no ``<type>`` placeholder it is treated as a bare macro
    name prefix, i.e. ``BUILD_X_BEGIN`` matches ``BUILD_X_BEGIN(Foo, ...)`` and
    captures ``Foo``.
    """
    if "<type>" in line:
        head, tail = line.split("<type>", 1)
        pattern = (
            re.escape(head)
            + "("
            + TYPE_NAME_RE
            + ")"
            + r"[^)]*"
            + re.escape(tail)
        )
    else:
        pattern = re.escape(line) + r"\s*\(" + "(" + TYPE_NAME_RE + ")"

    return re.compile(pattern)


def load_patterns(patterns_path: str | Path) -> list[tuple[re.Pattern, str]]:
    """Load macro patterns from a git-ignore-style text file.

    Blank lines and lines starting with ``#`` are skipped. Each remaining line is
    either a bare macro name (``BUILD_X_BEGIN``) or a call template carrying a
    ``<type>`` placeholder for the type name. Returns ``(compiled, original)``
    pairs so the report can show the human-readable pattern.
    """
    patterns: list[tuple[re.Pattern, str]] = []
    text = Path(patterns_path).read_text(encoding="utf-8-sig")

    for raw in text.splitlines():
        line = raw.strip()

        if not line or line.startswith("#"):
            continue

        patterns.append((compile_pattern(line), line))

    return patterns


def scan_source_tree(
    source_path: str | Path,
    patterns_path: str | Path,
    items: list[TypeSource],
    already_found: set[str],
) -> list[tuple[str, str | None, TypeSource | None, bool | None]]:
    """Find macro-defined types in source and cross-check them with items.

    Scans every supported source file under *source_path*, matching each compiled
    pattern against the file text. Returns ``(type_name, file, item, match)`` for
    every detected type that was *not* already located in the browse DB (i.e. it
    was already handled there and is skipped). *item* is the ``TypeSource`` from
    *items* for this name, or ``None`` if DumpPDB has no such type. *match* is a
    path comparison of ``item.path`` against *file*, mirroring the browse step
    (``None`` when there is nothing to compare).
    """
    patterns = load_patterns(patterns_path)
    items_by_name = {it.type_name: it for it in items}

    found: dict[str, str] = {}

    for file_path in sorted(Path(source_path).rglob("*")):
        if not file_path.is_file():
            continue
        if file_path.suffix.lower() not in SOURCE_EXTENSIONS:
            continue

        try:
            text = file_path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue

        for regex, _ in patterns:
            for match in regex.finditer(text):
                found.setdefault(match.group(1), str(file_path))

    results: list[tuple[str, str | None, TypeSource | None, bool | None]] = []

    for type_name, file in sorted(found.items()):
        if type_name in already_found:
            continue

        item = items_by_name.get(type_name)

        if item is None or item.path is None:
            match = None
        else:
            match = paths_match(item.path, file)

        results.append((type_name, file, item, match))

    return results


def macro_found_types(
    source_path: str | Path,
    patterns_path: str | Path,
    items: list[TypeSource],
    already_found: set[str],
) -> set[str]:
    """Return the type names detected via macros, excluding *already_found*."""
    return {name for name, _, _, _ in scan_source_tree(source_path, patterns_path, items, already_found)}