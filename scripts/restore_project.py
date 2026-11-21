#!/usr/bin/env python3
"""Restore a source tree from a PDB and/or an executable.

What it produces (``--out DIR``)::

    DIR/<original layout>/Foo.h    types (from the PDB; from RTTI when there is no PDB)
    DIR/<original layout>/Foo.cpp  variables and function skeletons with RVA / size / line
    DIR/_external/...              code outside the project root (CRT, SDK, third party)
    DIR/_unassigned/               symbols whose file could not be determined
    DIR/_meta/                     RVA -> file:line -> name maps, publics, report, name scripts

Three ways to run it
--------------------

1. PDB (best result) -- types, signatures with parameter names, globals/statics,
   source file and line of every function::

       python scripts/restore_project.py --dll PdbAPI.dll --pdb Game.pdb --out restored

2. PDB + EXE -- additionally recovers the on-disk case of every path from the
   executable's embedded ``__FILE__`` strings, checks that the PDB really
   belongs to the executable, and writes IDA/Ghidra/x64dbg name scripts::

       python scripts/restore_project.py --dll PdbAPI.dll --pdb Game.pdb --exe Game.exe --out restored

   No Windows or DLL? ``--native`` reads the PDB with the built-in pure-Python reader
   (same output, any OS; selected automatically when the DLL is not found)::

       python scripts/restore_project.py --native --pdb Game.pdb --exe-analysis analysis --out restored

3. EXE only -- no PDB: classes (names, bases, vftable layout, constructor and
   destructor addresses), function stubs and the file layout come from RTTI and
   the strings the compiler embedded. ``--reconstruct-types`` additionally asks
   the native library for fields via disassembly::

       python scripts/restore_project.py --dll PdbAPI.dll --exe Game.exe --out restored

   Or, with NO native library at all, from an analysis directory that was
   written earlier by ``dump_pdb.py --exe-analyze``::

       python scripts/restore_project.py --exe-analysis analysis_dir --out restored
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

from dumppdb_tools.restore import (
    ExeAnalysisData,
    ProjectBuilder,
    check_pdb_matches_exe,
    collect_names,
    ghidra_script,
    ida_script,
    write_project,
    x64dbg_json,
)


@dataclass
class RestoreOptions:
    out_dir: Path
    pdb: str | None = None
    exe: str | None = None
    exe_analysis_dir: str | None = None
    root: str | None = None
    include_types: bool = True
    reconstruct_types: bool = False
    include_compiler_generated: bool = False
    keep_external: bool = True
    max_types: int | None = None
    check_match: str = "warn"  # strict | warn | off
    write_names: bool = True
    include_members: bool = True
    verbose: bool = True
    log: list[str] = field(default_factory=list)


def _say(opts: RestoreOptions, message: str) -> None:
    opts.log.append(message)
    if opts.verbose:
        print(message, flush=True)


def _split_lines(text: str) -> list[str]:
    return [ln.strip() for ln in text.splitlines() if ln.strip()]


def restore(client, opts: RestoreOptions) -> dict[str, int]:
    """Run the whole pipeline. *client* is a ``PdbClient`` (or None for --exe-analysis only)."""
    out = opts.out_dir
    meta = out / "_meta"
    meta.mkdir(parents=True, exist_ok=True)

    # ---------------------------------------------------------------- executable
    exe_data: ExeAnalysisData | None = None
    if opts.exe_analysis_dir:
        exe_data = ExeAnalysisData(opts.exe_analysis_dir)
        _say(opts, f"[exe] analysis loaded from {opts.exe_analysis_dir}: "
                   f"{len(exe_data.classes)} classes, {len(exe_data.functions)} functions")
    elif opts.exe:
        if client is None:
            raise SystemExit("--exe needs the native library (--dll); "
                             "or pass a finished analysis with --exe-analysis")
        client.load_exe(opts.exe)
        analysis_dir = meta / "exe_analysis"
        _say(opts, f"[exe] analysing {opts.exe} ...")
        client.exe_export_analysis(analysis_dir)
        exe_data = ExeAnalysisData(analysis_dir)
        _say(opts, f"[exe] {len(exe_data.classes)} classes, {len(exe_data.functions)} functions")

    # ----------------------------------------------------------------------- PDB
    builder = ProjectBuilder(
        root=opts.root,
        keep_external=opts.keep_external,
        include_compiler_generated=opts.include_compiler_generated,
        exe=exe_data,
    )

    symbols = []
    pdb_matches = None
    if opts.pdb:
        if client is None:
            raise SystemExit("--pdb needs the native library (--dll)")
        client.open(opts.pdb)

        if exe_data is not None:
            info = client.pdb_info()
            ok, why = check_pdb_matches_exe(info, exe_data.pdb)
            pdb_matches = ok
            _say(opts, f"[pdb] match check: {why}")
            if not ok and opts.check_match == "strict":
                raise SystemExit("PDB does not match the executable (use --check-match warn to continue)")

        client.set_globals_config(
            showAddress=True, showSourceLine=True,
            includeMembers=opts.include_members, includeLocalStatics=True)

        _say(opts, "[pdb] exporting symbol table (functions, variables, publics) ...")
        symbols = client.symbol_table(with_source=True)
        builder.add_symbols(symbols)
        (meta / "symbols.tsv").write_text(client.dump_symbol_table(with_source=True),
                                          encoding="utf-8", newline="\n")
        fn_count = sum(1 for s in symbols if s.is_function)
        _say(opts, f"[pdb] {len(symbols)} symbols ({fn_count} functions)")

        known = _split_lines(client.enumerate_source_files())
        builder.add_known_paths(known)
        _say(opts, f"[pdb] {len(known)} source files known to the PDB")

        if opts.include_types:
            names = _split_lines(client.enumerate_symbols(top_level_only=True))
            if opts.max_types:
                names = names[:opts.max_types]
            _say(opts, f"[pdb] dumping {len(names)} top-level types ...")
            failed = 0
            for i, name in enumerate(names, 1):
                try:
                    text = client.dump_type(name, True)
                    sources = _split_lines(client.get_source_files(name, True))
                except Exception as e:  # keep going: one broken type must not stop a restore
                    failed += 1
                    if failed <= 20:
                        _say(opts, f"[pdb] skipped {name}: {e}")
                    continue
                builder.add_type(name, sources, text)
                if i % 1000 == 0:
                    _say(opts, f"[pdb]   {i}/{len(names)}")
            if failed:
                _say(opts, f"[pdb] {failed} types could not be dumped")

    # ----------------------------------------------- RTTI types for what the PDB lacks
    if exe_data is not None and not opts.pdb:
        added = builder.add_exe_functions()
        _say(opts, f"[exe] {added} named functions added as stubs")

        if opts.reconstruct_types and client is not None:
            _say(opts, "[exe] reconstructing class bodies (field recovery) ...")
            done = 0
            for name, c in exe_data.classes.items():
                if not c.source:
                    continue
                try:
                    text = client.exe_reconstruct_type(name)
                except Exception:
                    continue
                builder.add_type(name, [c.source], text)
                done += 1
            _say(opts, f"[exe] {done} classes reconstructed")

    # ------------------------------------------------------------------ write out
    plan = builder.build()
    stats = write_project(plan, out)
    _say(opts, "[out] " + ", ".join(f"{k}={v}" for k, v in stats.items()))

    # ------------------------------------------------------------------ resources
    if exe_data is not None:
        res_src = Path(exe_data.directory) / "resources"
        if (res_src / "resources.rc").is_file():
            res_dst = out / "Resources"
            if res_dst.exists():
                shutil.rmtree(res_dst)
            shutil.copytree(res_src, res_dst)
            _say(opts, "[res] resources.rc, resource.h, resources.res, icons, manifest ... -> Resources/")

    # --------------------------------------------------------------- name scripts
    if opts.write_names and symbols:
        if pdb_matches is False:
            _say(opts, "[names] skipped: the PDB does not match the executable")
        else:
            module = Path(opts.exe).name if opts.exe else "module.exe"
            entries = collect_names(symbols)
            (meta / "pdb_names_ida.py").write_text(ida_script(entries, module), encoding="utf-8")
            (meta / "pdb_names_ghidra.py").write_text(ghidra_script(entries, module), encoding="utf-8")
            (meta / "pdb_names_x64dbg.dd32.json").write_text(x64dbg_json(entries, module), encoding="utf-8")
            _say(opts, f"[names] {len(entries)} names written to _meta/pdb_names_*")
            if pdb_matches is None:
                _say(opts, "[names] WARNING: pass --exe so the PDB/exe match can be verified "
                           "before applying these names")

    if opts.pdb and client is not None:
        client.close()
    return stats


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Restore a source tree from a PDB and/or an executable.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Three ways to run it")[0] if __doc__ else None,
    )
    p.add_argument("--dll", default="./PdbAPI.dll", help="Path to PdbAPI.dll (default: ./PdbAPI.dll)")
    p.add_argument("--native", action="store_true",
                   help="Read the PDB with the built-in Python reader instead of PdbAPI.dll "
                        "(automatic when the DLL does not exist or this is not Windows)")
    p.add_argument("--pdb", help="Path to the .pdb")
    p.add_argument("--exe", help="Path to the executable (enables case recovery and the match check)")
    p.add_argument("--exe-analysis", dest="exe_analysis",
                   help="Directory written by 'dump_pdb.py --exe-analyze' (no DLL needed)")
    p.add_argument("--out", required=True, help="Output directory")
    p.add_argument("--root", help="Project root inside the original paths, e.g. e:/perforce/game "
                                  "(default: detected automatically)")
    p.add_argument("--no-types", action="store_true", help="Do not dump types (symbols only)")
    p.add_argument("--reconstruct-types", action="store_true",
                   help="EXE only: recover class fields via disassembly (slow)")
    p.add_argument("--include-compiler-generated", action="store_true")
    p.add_argument("--no-external", action="store_true",
                   help="Drop functions from files outside the project root")
    p.add_argument("--max-types", type=int, help="Dump only the first N types (for quick tests)")
    p.add_argument("--check-match", choices=["strict", "warn", "off"], default="warn",
                   help="What to do when the PDB GUID differs from the exe's (default: warn)")
    p.add_argument("--no-names", action="store_true", help="Do not write the IDA/Ghidra/x64dbg scripts")
    p.add_argument("--quiet", action="store_true")
    return p.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    if not (args.pdb or args.exe or args.exe_analysis):
        print("error: give at least one of --pdb, --exe, --exe-analysis", file=sys.stderr)
        return 2

    opts = RestoreOptions(
        out_dir=Path(args.out),
        pdb=args.pdb,
        exe=args.exe,
        exe_analysis_dir=args.exe_analysis,
        root=args.root,
        include_types=not args.no_types,
        reconstruct_types=args.reconstruct_types,
        include_compiler_generated=args.include_compiler_generated,
        keep_external=not args.no_external,
        max_types=args.max_types,
        check_match=args.check_match,
        write_names=not args.no_names,
        verbose=not args.quiet,
    )

    client = None
    use_native = args.native or (args.pdb and (os.name != "nt" or not Path(args.dll).is_file()))
    if args.pdb and use_native:
        from dumppdb_tools.pdbfile.client import NativePdbClient
        client = NativePdbClient(progress=(None if args.quiet else lambda m: print("[pdb]  " + m, flush=True)))
        if args.exe and not args.exe_analysis:
            raise SystemExit("--exe needs the native DLL; with --native pass a finished analysis "
                             "directory via --exe-analysis instead (written by dump_pdb.py --exe-analyze)")
    elif args.pdb or (args.exe and not args.exe_analysis):
        from dumppdb_tools import PdbClient
        client = PdbClient(args.dll)

    restore(client, opts)
    return 0


if __name__ == "__main__":
    sys.exit(main())
