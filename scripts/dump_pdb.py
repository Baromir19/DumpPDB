#!/usr/bin/env python3
"""
Public example script that uses the dumppdb_tools package.

Run from the repository root or from scripts/:

    python scripts/dump_pdb.py --pdb Dev.pdb --type Actor
    python scripts/dump_pdb.py --pdb Dev.pdb --symbols
"""

import argparse
import sys

from dumppdb_tools import PdbClient


def main():
    parser = argparse.ArgumentParser(
        description="Dump type information from a PDB file."
    )
    parser.add_argument(
        "--dll",
        default="./PdbAPI.dll",
        help="Path to PdbAPI.dll (default: ./PdbAPI.dll)",
    )
    parser.add_argument(
        "--pdb",
        required=True,
        help="Path to the .pdb file to open",
    )
    parser.add_argument(
        "--type",
        dest="type_name",
        help="Dump a type by name (e.g. Actor)",
    )
    parser.add_argument(
        "--class",
        dest="class_name",
        help="Dump a class by name",
    )
    parser.add_argument(
        "--enum",
        dest="enum_name",
        help="Dump an enum by name",
    )
    parser.add_argument(
        "--typedef",
        dest="typedef_name",
        help="Dump a typedef by name",
    )
    parser.add_argument(
        "--source-files",
        dest="source_name",
        help="List source files for a type by name",
    )
    parser.add_argument(
        "--symbols",
        action="store_true",
        help="Enumerate all symbol names",
    )
    parser.add_argument(
        "--case-sensitive",
        action="store_true",
        help="Use case-sensitive name lookup",
    )

    args = parser.parse_args()

    pdb = PdbClient(args.dll)

    try:
        pdb.open(args.pdb)

        if args.type_name:
            print(pdb.dump_type(args.type_name, args.case_sensitive))

        if args.class_name:
            print(pdb.dump_class(args.class_name, args.case_sensitive))

        if args.enum_name:
            print(pdb.dump_enum(args.enum_name, args.case_sensitive))

        if args.typedef_name:
            print(pdb.dump_typedef(args.typedef_name, args.case_sensitive))

        if args.source_name:
            print(pdb.get_source_files(args.source_name, args.case_sensitive))

        if args.symbols:
            print(pdb.enumerate_symbols())

    except Exception as e:
        last_error = pdb.last_error()
        print(f"Error: {last_error}")
        print(f"Exception: {e}")

    finally:
        pdb.close()


if __name__ == "__main__":
    sys.exit(main())