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

def parse_encodings(value):
    result = 0

    for item in value.lower().split(","):
        item = item.strip()

        if item == "ascii":
            result |= 1

        elif item == "utf8":
            result |= 2

        elif item in ("utf16", "utf16le"):
            result |= 4

    return result

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
        #required=True,
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
    parser.add_argument(
        "--signatures",
        metavar="PATTERN",
        help="Search for a byte signature (e.g. 'FF ?? 01 BD ?? CA')",
    )
    parser.add_argument(
        "--strings",
        action="store_true",
        help="Search for strings",
    )
    parser.add_argument(
        "--file",
        help="Path to a binary file (exe/dll/bin/etc)",
    )
    parser.add_argument(
        "--min-length",
        type=int,
        default=4,
        help="Minimum string length (default: 4)",
    )
    parser.add_argument(
        "--encodings",
        default="ascii,utf16",
        help="String encodings: ascii,utf8,utf16",
    )
    parser.add_argument(
        "--sections",
        default="",
        help="PE sections to scan (.rdata,.data)",
    )
    parser.add_argument(
        "--regex",
        default="",
        help="Regex filter for strings",
    )

    args = parser.parse_args()

    pdb = PdbClient(args.dll)

    try:
        binary_mode = (
            args.file and
            (args.strings or args.signatures)
        )

        pdb_mode = (
            args.pdb and
            any([
                args.type_name,
                args.class_name,
                args.enum_name,
                args.typedef_name,
                args.source_name,
                args.symbols
            ])
        )

        if pdb_mode:
            if not args.pdb:
                parser.error("--pdb is required")

            pdb.open(args.pdb)

        if binary_mode:
            if not args.file:
                parser.error("--file is required")

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

        if args.signatures:
            if not args.file:
                parser.error("--file is required with --signatures")

            print(
                pdb.find_signatures(
                    args.file,
                    args.signatures
                )
            )

        if args.strings:
            print(
                pdb.find_strings(
                    args.file,
                    min_length=args.min_length,
                    encodings=parse_encodings(args.encodings),
                    section_names=args.sections,
                    regex_pattern=args.regex,
                )
            )

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