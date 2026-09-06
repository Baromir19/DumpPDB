#!/usr/bin/env python3

import argparse
import sqlite3
import sys
from pathlib import Path

from dumppdb_tools.sources import (
    BrowseDefinition,
    TypeSource,
    find_definitions_in_browse,
    load_type_sources,
    lookup_file_name,
    paths_match,
    scan_source_tree,
)


def locate_vs_definitions(
    browse_db_path: str | Path,
    items: list[TypeSource],
) -> tuple[
    dict[str, BrowseDefinition],
    set[str],
    list[tuple[str, str | None, str | None, str]],
]:
    """Locate the definition file of each item inside the VS browse DB.

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

            if not paths_match(item.path, actual):
                discrepancies.append(
                    (type_name, item.path, actual, definition.kind)
                )

        return definitions, not_found, discrepancies

    finally:
        conn.close()


def report_vs_definitions(definitions, not_found, discrepancies) -> None:
    """Print the browse-database comparison results to the console."""
    print(
        f"Browse comparison: {len(definitions)} definitions located, "
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


def report_source_tree(
    results: list[tuple[str, str | None, TypeSource | None, bool | None]],
) -> None:
    """Print the source-tree macro findings (same comparison as the browse step)."""
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
        help="Root directory to scan for macro-defined types.",
    )
    parser.add_argument(
        "--macro-patterns",
        help="Git-ignore-style file describing macros that define types. "
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

    definitions, not_found, discrepancies = locate_vs_definitions(args.browse_db, items)

    report_vs_definitions(definitions, not_found, discrepancies)

    if args.macro_patterns:
        results = scan_source_tree(
            args.source_path,
            args.macro_patterns,
            items,
            set(definitions),
        )
        report_source_tree(results)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())