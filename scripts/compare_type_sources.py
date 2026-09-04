#!/usr/bin/env python3

import argparse
import re
import sqlite3
import sys
from dataclasses import dataclass
from enum import IntFlag
from pathlib import Path


@dataclass
class TypeSource:
    type_name: str
    related_name: str | None
    path: str | None


class CodeItemAttributes(IntFlag):
    NONE = 0
    DECLARATION = 1
    DEFINITION = 2


@dataclass
class BrowseDefinition:
    """A *definition* of a type located inside the Browse.VC.db database."""

    type_name: str
    kind: str
    file_id: int | None
    code_item_id: int


# Kinds we care about when looking for the actual type definition.
DEFINITION_KINDS = {"enum", "union", "struct", "class", "interface"}

# Flag a code_item must carry to be treated as the definition.
DEFINITION_ATTRIBUTE = CodeItemAttributes.DEFINITION

# File extensions scanned by pass 2.
SOURCE_EXTENSIONS = {".h", ".hpp", ".hxx", ".hh", ".inl", ".c", ".cc", ".cxx", ".cpp"}

# An identifier-or-qualified-identifier used as the macro type-name argument.
TYPE_NAME_RE = r"[A-Za-z_][A-Za-z0-9_:]*"


def load_type_sources(db_path: str | Path) -> list[TypeSource]:
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    try:
        types = conn.execute(
            """
            SELECT id, name
            FROM types
            WHERE disabled = 0
            """
        ).fetchall()

        result = []

        for type_row in types:
            type_id = type_row["id"]
            type_name = type_row["name"]

            is_related = conn.execute(
                """
                SELECT 1
                FROM type_relations
                WHERE related_type_id = ?
                LIMIT 1
                """,
                (type_id,),
            ).fetchone()

            if is_related:
                continue

            relation = conn.execute(
                """
                SELECT related_type_id
                FROM type_relations
                WHERE type_id = ?
                ORDER BY related_type_id
                LIMIT 1
                """,
                (type_id,),
            ).fetchone()

            related_name = None

            if relation:
                related_row = conn.execute(
                    """
                    SELECT name
                    FROM types
                    WHERE id = ?
                    """,
                    (relation["related_type_id"],),
                ).fetchone()

                if related_row:
                    related_name = related_row["name"]

            source = conn.execute(
                """
                SELECT
                    tsf.source_file_id,
                    np.path
                FROM type_source_files tsf
                JOIN normalized_paths np
                    ON np.id = tsf.source_file_id
                WHERE tsf.type_id = ?
                ORDER BY
                    tsf.user_preferred DESC,
                    tsf.score DESC,
                    tsf.source_file_id ASC
                LIMIT 1
                """,
                (type_id,),
            ).fetchone()

            path = source["path"] if source else None

            result.append(
                TypeSource(
                    type_name=type_name,
                    related_name=related_name,
                    path=path,
                )
            )

        return result

    finally:
        conn.close()
def load_kinds(conn) -> dict[int, str]:
    """Snapshot ``code_item_kinds`` as ``{id: name}``."""
    return {
        row["id"]: row["name"]
        for row in conn.execute("SELECT id, name FROM code_item_kinds").fetchall()
    }


def find_definitions_in_browse(conn, items: list[TypeSource]) -> dict[str, BrowseDefinition]:
    """Find the relevant-kind DEFINITION of every *item* inside the browse DB.

    For each type name we scan its ``code_items`` rows and keep the first one
    whose ``kind`` name is one of ``DEFINITION_KINDS`` and whose ``attributes``
    carry the ``DEFINITION`` flag.
    """
    kinds = load_kinds(conn)

    definitions: dict[str, BrowseDefinition] = {}

    for item in items:
        type_name = item.type_name

        rows = conn.execute(
            """
            SELECT ci.id AS code_item_id,
                   ci.file_id,
                   ci.kind,
                   ci.attributes
            FROM code_items ci
            WHERE ci.name = ? COLLATE NOCASE
            """,
            (type_name,),
        ).fetchall()

        for row in rows:
            kind_name = kinds.get(row["kind"])

            if kind_name not in DEFINITION_KINDS:
                continue

            if not (row["attributes"] & DEFINITION_ATTRIBUTE):
                continue

            definitions[type_name] = BrowseDefinition(
                type_name=type_name,
                kind=kind_name,
                file_id=row["file_id"],
                code_item_id=row["code_item_id"],
            )
            break

    return definitions


def lookup_file_name(conn, file_id) -> str | None:
    """Resolve a ``files.id`` to its ``name`` (compared case-insensitively)."""
    row = conn.execute(
        "SELECT name FROM files WHERE id = ?",
        (file_id,),
    ).fetchone()

    return row["name"] if row else None


def _paths_match(expected: str, actual: str) -> bool:
    """Case-insensitive containment/equality of two paths.

    Separators are normalised to ``/`` and both sides are lowercased. Our
    expected path (project-relative, extension-stripped) is considered present
    in the browse ``files.name`` as soon as it appears as a substring of it —
    the browse DB usually stores the full path including the extension.
    """
    def norm(p: str | None) -> str:
        return (p or "").replace("\\", "/").strip().lower()

    a, b = norm(expected), norm(actual)

    if not a or not b:
        return True

    return a == b or a in b or b in a
def run_pass1(
    browse_db_path: str | Path,
    items: list[TypeSource],
) -> tuple[
    dict[str, BrowseDefinition],
    set[str],
    list[tuple[str, str | None, str | None, str]],
]:
    """Pass 1: locate the definition file of each item and compare it.

    Returns ``(definitions, not_found, discrepancies)`` where ``discrepancies``
    is a list of ``(type_name, expected_path, browse_path, kind)`` for every
    type whose definition file differs from the one recorded in *items*.
    """
    items_by_name = {it.type_name: it for it in items}

    conn = sqlite3.connect(browse_db_path)
    conn.row_factory = sqlite3.Row

    try:
        definitions = find_definitions_in_browse(conn, items)

        found = set(definitions)
        not_found = {it.type_name for it in items if it.type_name not in found}

        discrepancies: list[tuple[str, str | None, str | None, str]] = []

        for type_name, definition in definitions.items():
            item = items_by_name[type_name]
            actual = lookup_file_name(conn, definition.file_id)

            if item.path is None or actual is None:
                continue

            if not _paths_match(item.path, actual):
                discrepancies.append(
                    (type_name, item.path, actual, definition.kind)
                )

        return definitions, not_found, discrepancies

    finally:
        conn.close()


def report(definitions, not_found, discrepancies) -> None:
    """Print the pass-1 results to the console."""
    print(
        f"Pass 1: {len(definitions)} definitions located, "
        f"{len(not_found)} types not found in Browse.VC.db."
    )

    if discrepancies:
        print("\nDiscrepancies (definition file differs from recorded type source):")
        for type_name, expected, actual, kind in discrepancies:
            print(f"  [{kind}] {type_name}")
            print(f"      expected : {expected}")
            print(f"      browse   : {actual}")

    remaining = sorted(not_found)[:10]
    if remaining:
        print("\nTypes not found in the browse DB (to be handled separately):")
        for name in remaining:
            print(f"  - {name}")

        if len(not_found) > len(remaining):
            print(f"  ... and {len(not_found) - len(remaining)} more.")


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
    """Load pass-2 macro patterns from a git-ignore-style text file.

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


def run_pass2(
    source_path: str | Path,
    patterns_path: str | Path,
    items: list[TypeSource],
    already_found: set[str],
) -> list[tuple[str, str | None, TypeSource | None, bool | None]]:
    """Pass 2: find macro-defined types in source and cross-check them with items.

    Scans every supported source file under *source_path*, matching each compiled
    pattern against the file text. Returns ``(type_name, file, item, match)`` for
    every detected type that pass 1 did *not* locate in the browse DB (i.e. it
    was already handled there and is skipped). *item* is the ``TypeSource`` from
    *items* for this name, or ``None`` if DumpPDB has no such type. *match* is a
    path comparison of ``item.path`` against *file*, mirroring pass 1 (``None``
    when there is nothing to compare).
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
            match = _paths_match(item.path, file)

        results.append((type_name, file, item, match))

    return results


def report_pass2(
    results: list[tuple[str, str | None, TypeSource | None, bool | None]],
) -> None:
    """Print the pass-2 results (the same comparison as pass 1, against items)."""
    print("\nMacro-defined types not found in the browse DB:")

    if not results:
        print("  (none)")
        return

    not_in_items = [r for r in results if r[2] is None]
    no_expected = [r for r in results if r[2] is not None and r[3] is None]
    discrepancies = [r for r in results if r[2] is not None and r[3] is False]
    matches = [r for r in results if r[2] is not None and r[3] is True]

    if not_in_items:
        print("\n  Not in DumpPDB types (no expected source recorded):")
        for type_name, file, _, _ in not_in_items:
            print(f"    {type_name}")
            print(f"      file    : {file}")

    if no_expected:
        print("\n  In DumpPDB but no expected source path to compare:")
        for type_name, file, _, _ in no_expected:
            print(f"    {type_name}")
            print(f"      file    : {file}")

    if discrepancies:
        print("\n  Path discrepancies (recorded item path differs from macro file):")
        for type_name, file, item, _ in discrepancies:
            print(f"    {type_name}")
            print(f"      item  : {item.path}")
            print(f"      file  : {file}")

    if matches:
        print("\n  Path matches (macro file already recorded correctly):")
        for type_name, file, item, _ in matches:
            print(f"    {type_name}")
            print(f"      item  : {item.path}")
            print(f"      file  : {file}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare DumpPDB type sources against the VS Browse.VC.db database."
    )
    parser.add_argument(
        "--sources-db",
        help="DumpPDB SQLite database with the `types` table.",
    )
    parser.add_argument(
        "--browse-db",
        default="Browse.VC.db",
        help="Visual Studio browse database (default: Browse.VC.db).",
    )
    parser.add_argument(
        "--source-path",
        help="Root directory to scan for macro-defined types (pass 2).",
    )
    parser.add_argument(
        "--macro-patterns",
        help="Git-ignore-style file describing macros that define types (pass 2). "
        "Each non-comment line is a call template with a <type> placeholder for "
        "the type name, e.g. BUILD_EXPOSED_STRUCTURE_STRUCT_BEGIN(<type>). "
        "Requires --source-path.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.macro_patterns and not args.source_path:
        print(
            "--source-path is required when --macro-patterns is provided.",
            file=sys.stderr,
        )
        return 2

    items = load_type_sources(args.sources_db)

    definitions, not_found, discrepancies = run_pass1(args.browse_db, items)

    report(definitions, not_found, discrepancies)

    if args.macro_patterns:
        results = run_pass2(
            args.source_path,
            args.macro_patterns,
            items,
            set(definitions),
        )
        report_pass2(results)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())