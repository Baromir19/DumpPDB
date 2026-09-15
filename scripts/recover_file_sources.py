#!/usr/bin/env python3
"""Backward-compatible entry point for the refactored recover_sources script.

Use ``scripts/recover_sources.py`` for the canonical version.
"""

import argparse
from pathlib import Path
import sys

from dumppdb_tools import PdbClient
from dumppdb_tools.database import SourceDatabase
from dumppdb_tools.filesystem import create_source_tree, rename_source_tree
from dumppdb_tools.recovery import (
    build_case_dictionary,
    normalize_extension,
    normalize_path,
    normalize_paths,
    recover_case,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Reconstruct source tree from PDB and executable."
    )

    parser.add_argument(
        "--dll",
        default="./PdbAPI.dll",
        help="Path to PdbAPI.dll",
    )

    parser.add_argument(
        "--pdb",
        required=True,
        help="Path to PDB file",
    )

    parser.add_argument(
        "--exe",
        required=True,
        help="Path to executable or DLL used for path recovery",
    )

    parser.add_argument(
        "--path-prefix",
        required=True,
        help="Only process source files whose path begins with this prefix",
    )

    parser.add_argument(
        "--output",
        default="reconstructed",
        help="Output directory for reconstructed source tree",
    )

    parser.add_argument(
        "--output-extension",
        default=".hpp",
        help="Extension for generated files (default: .hpp)",
    )

    parser.add_argument(
        "--template",
        help="Header template copied into every generated file",
    )

    parser.add_argument(
        "--db",
        default="sources.db",
        help="SQLite database path",
    )

    parser.add_argument(
        "--write-db",
        action="store_true",
        help="Write recovered paths into the database",
    )

    parser.add_argument(
        "--create-tree",
        action="store_true",
        help="Create reconstructed directory tree",
    )

    parser.add_argument(
        "--rename-tree",
        action="store_true",
        help="Rename existing reconstructed tree in place using score comparison, without creating new files",
    )

    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print additional information",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.create_tree and args.template:
        template_path = Path(args.template)

        if not template_path.exists():
            print(
                f"Error: template file not found: {template_path}",
                file=sys.stderr,
            )
            return 1

    output_extension = normalize_extension(
        args.output_extension
    )

    pdb = PdbClient(args.dll)

    try:
        pdb.open(args.pdb)

        print("DLL           :", args.dll)
        print("PDB           :", args.pdb)
        print("EXE           :", args.exe)
        print("Path prefix   :", args.path_prefix)
        print("Output        :", args.output)
        print("Template      :", args.template or "<empty>")
        print("Database      :", args.db)
        print("Create tree   :", args.create_tree)
        print("Write DB      :", args.write_db)
        print()

        print("Enumerating source files...\n")

        raw = pdb.enumerate_source_files()
        paths = [p.strip() for p in raw.splitlines() if p.strip()]

        filtered = normalize_paths(paths, args.path_prefix)

        case_dict = build_case_dictionary(pdb, args.exe)
        recover_case(filtered, case_dict)

        if not args.create_tree and not args.write_db and not args.rename_tree:
            for path in filtered:
                print(path.normalized)

            return 0

        if args.write_db:
            db = SourceDatabase(args.db)

            try:
                db.open()
                db.insert_many(filtered)

                # Persist singleton settings for resolve_type_sources.
                db.set_setting("output_extension", output_extension)
                db.set_setting("path_prefix", normalize_path(args.path_prefix))
            finally:
                db.close()

        if args.create_tree:
            create_source_tree(
                filtered,
                Path(args.output),
                output_extension,
                Path(args.template)
                if args.template
                else None,
            )

        if args.rename_tree:
            rename_source_tree(
                filtered,
                Path(args.output),
                verbose=args.verbose,
            )

    except Exception as e:
        print(f"Error: {pdb.last_error()}")
        print(f"Exception: {e}")
        return 1

    finally:
        pdb.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())