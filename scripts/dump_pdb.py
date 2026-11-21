#!/usr/bin/env python3
"""
Public example script that uses the dumppdb_tools package.

Run from the repository root or from scripts/:

    python scripts/dump_pdb.py --pdb Dev.pdb --type Actor
    python scripts/dump_pdb.py --pdb Dev.pdb --symbols
    python scripts/dump_pdb.py --pdb Dev.pdb --functions Update --include-members
    python scripts/dump_pdb.py --pdb Dev.pdb --globals g_
    python scripts/dump_pdb.py --pdb Dev.pdb --symbol-table symbols.tsv
    python scripts/dump_pdb.py --exe Game.exe --exe-info
    python scripts/dump_pdb.py --exe Game.exe --exe-analyze out_dir
"""

import argparse
import sys

from dumppdb_tools import PdbClient
from dumppdb_tools.api.pdb_api import EXPORT_EVERYTHING, EXPORT_SYMBOL_TABLE_SOURCE

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
        "--nested-types",
        dest="nested_types",
        help="Enumerate nested types of a type by name (e.g. Actor)",
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
        "--enumerate-files",
        action="store_true",
        help="Enumerate all source files in the PDB",
    )
    #parser.add_argument(
    #    "--symbols-by-file",
    #    dest="symbols_file",
    #    help="List UDT/enum/typedef symbols defined in a source file",
    #)
    parser.add_argument(
        "--symbols",
        action="store_true",
        help="Enumerate all symbol names (top-level only)",
    )
    parser.add_argument(
        "--all-types",
        action="store_true",
        help="With --symbols, enumerate ALL types including nested ones",
    )
    parser.add_argument(
        "--case-sensitive",
        action="store_true",
        help="Use case-sensitive name lookup",
    )
    parser.add_argument(
        "--template-params",
        action="store_true",
        help="When dumping a template instantiation by name (e.g. Type<ExType, 11, HighRes>), "
             "emit a template<...> declaration and substitute the concrete arguments "
             "with generic parameter names everywhere.",
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
    parser.add_argument(
        "--exe",
        metavar="EXE",
        help="Path to a PE executable (.exe/.dll) for RTTI analysis (no PDB needed)",
    )
    parser.add_argument(
        "--exe-type",
        dest="exe_type",
        metavar="TYPENAME",
        help="Reconstruct a type by name from EXE RTTI (requires --exe)",
    )
    parser.add_argument(
        "--exe-types",
        dest="exe_types",
        action="store_true",
        help="Enumerate all RTTI types found in the EXE (requires --exe)",
    )
    parser.add_argument(
        "--padding-style",
        dest="padding_style",
        choices=["array", "expanded"],
        default="array",
        help="Padding style: 'array' (default, one int32_t[] per gap) or "
             "'expanded' (one field per 4/2/1 bytes)",
    )

    # -- global functions / variables / statics / publics --------------------------
    parser.add_argument(
        "--functions", nargs="?", const="", metavar="FILTER",
        help="List global functions (optional name substring) with RVA and source line",
    )
    parser.add_argument(
        "--globals", nargs="?", const="", metavar="FILTER", dest="globals_",
        help="List global variables / file statics / constants (optional name substring)",
    )
    parser.add_argument(
        "--publics", nargs="?", const="", metavar="FILTER",
        help="List public symbols (works on stripped PDBs)",
    )
    parser.add_argument(
        "--symbol-table", metavar="OUT.tsv",
        help="Write every function/variable/public symbol as a TSV table",
    )
    parser.add_argument(
        "--dump-all", metavar="OUT.txt",
        help="Write all types, functions, variables and publics to a file",
    )
    parser.add_argument(
        "--pdb-info", action="store_true",
        help="Print the GUID/age of the PDB (compare with --exe-info)",
    )
    parser.add_argument("--include-members", action="store_true",
                        help="With --functions/--globals: also list member definitions / static members")
    parser.add_argument("--local-statics", action="store_true",
                        help="With --globals: also list function-local statics")
    parser.add_argument("--sort-by-address", action="store_true",
                        help="Sort listings by RVA instead of by name")
    parser.add_argument("--member-addresses", action="store_true",
                        help="Annotate members with their RVA in class dumps")
    parser.add_argument("--no-address", action="store_true",
                        help="Do not print RVA/source comments for functions and variables")
    # -- whole-image exe analysis -----------------------------------------------------
    parser.add_argument("--exe-info", action="store_true",
                        help="PE/RTTI/PDB-identity summary (requires --exe)")
    parser.add_argument("--exe-class", metavar="TYPENAME",
                        help="Class details from RTTI + analysis (requires --exe)")
    parser.add_argument("--exe-addr", metavar="HEX",
                        help="Describe an address, e.g. 915020, rva:915020 or va:915020 (requires --exe)")
    parser.add_argument("--exe-resources", metavar="OUTDIR",
                        help="Extract resources: .rc + .ico + manifest + .res (requires --exe)")
    parser.add_argument("--exe-analyze", metavar="OUTDIR",
                        help="Export all reports + IDA/Ghidra/x64dbg scripts (requires --exe)")

    args = parser.parse_args()

    pdb = PdbClient(args.dll)

    try:
        binary_mode = (
            args.file and
            (args.strings or args.signatures)
        )

        exe_mode = bool(args.exe and (args.exe_type or args.exe_types or args.exe_info
                                      or args.exe_class or args.exe_addr or args.exe_analyze
                                      or args.exe_resources))

        pdb_mode = (
            args.pdb and
            any([
                args.type_name,
                args.nested_types,
                args.class_name,
                args.enum_name,
                args.typedef_name,
                args.source_name,
                args.enumerate_files,
                #args.symbols_file,
                args.symbols,
                args.functions is not None,
                args.globals_ is not None,
                args.publics is not None,
                args.symbol_table,
                args.dump_all,
                args.pdb_info,
            ])
        )

        if pdb_mode:
            if not args.pdb:
                parser.error("--pdb is required")

            pdb.open(args.pdb)

            if args.template_params:
                pdb.set_config(templateParams=True)

            pdb.set_globals_config(
                includeMembers=args.include_members,
                includeLocalStatics=args.local_statics,
                sortByAddress=args.sort_by_address,
                showMemberAddress=args.member_addresses,
                showAddress=not args.no_address,
                showSourceLine=not args.no_address,
            )

        if binary_mode:
            if not args.file:
                parser.error("--file is required")

        if exe_mode:
            if not args.exe:
                parser.error("--exe is required for --exe-type / --exe-types")
            pdb.load_exe(args.exe)
            vft = pdb.exe_vftable_count()
            typ = pdb.exe_type_count()
            print(f"// Loaded {args.exe}")
            print(f"// Found {vft} vftables across {typ} unique types.\n")

        if args.type_name:
            print(pdb.dump_type(args.type_name, args.case_sensitive))

        if args.nested_types:
            print(pdb.enumerate_nested_types(args.nested_types, args.case_sensitive))

        if args.class_name:
            print(pdb.dump_class(args.class_name, args.case_sensitive))

        if args.enum_name:
            print(pdb.dump_enum(args.enum_name, args.case_sensitive))

        if args.typedef_name:
            print(pdb.dump_typedef(args.typedef_name, args.case_sensitive))

        if args.source_name:
            print(pdb.get_source_files(args.source_name, args.case_sensitive))

        if args.enumerate_files:
            print(pdb.enumerate_source_files())

        #if args.symbols_file:
        #    print(pdb.get_symbols_by_source_file(args.symbols_file, args.case_sensitive))

        if args.signatures:
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
            print(pdb.enumerate_symbols(top_level_only=not args.all_types))

        if args.functions is not None:
            print(pdb.dump_functions(args.functions, args.case_sensitive))

        if args.globals_ is not None:
            print(pdb.dump_variables(args.globals_, args.case_sensitive))

        if args.publics is not None:
            print(pdb.dump_publics(args.publics, args.case_sensitive))

        if args.pdb_info:
            for key, value in pdb.pdb_info().items():
                print(f"{key}: {value}")

        if args.symbol_table:
            pdb.export_to_file(EXPORT_SYMBOL_TABLE_SOURCE, args.symbol_table)
            print(f"// Symbol table written to {args.symbol_table}")

        if args.dump_all:
            pdb.export_to_file(EXPORT_EVERYTHING, args.dump_all)
            print(f"// Dump written to {args.dump_all}")

        if args.exe_info:
            print(pdb.exe_info())

        if args.exe_class:
            print(pdb.exe_class_info(args.exe_class))

        if args.exe_addr:
            text, kind = args.exe_addr.strip(), "auto"
            for prefix in ("rva:", "va:"):
                if text.lower().startswith(prefix):
                    text, kind = text[len(prefix):], prefix[:-1]
            print(pdb.exe_describe_address(int(text, 16), kind))

        if args.exe_resources:
            print(pdb.exe_export_resources(args.exe_resources))

        if args.exe_analyze:
            print(pdb.exe_export_analysis(args.exe_analyze))

        if args.exe_type:
            expanded = (args.padding_style == "expanded")
            print(pdb.exe_reconstruct_type(args.exe_type, expanded=expanded))

        if args.exe_types:
            print(pdb.exe_enumerate_types())

    except Exception as e:
        # last_error() can itself fail (e.g. the DLL never loaded), so guard it.
        try:
            print(f"Error: {pdb.last_error()}")
        except Exception:
            pass
        print(f"Exception: {e}")
        sys.exit(1)

    finally:
        pdb.close()


if __name__ == "__main__":
    sys.exit(main())