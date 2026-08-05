#!/usr/bin/env python3
"""Import all symbols and their source files from a PDB into SQLite.

Usage (from the scripts/ directory or wherever PdbAPI.dll is placed):

    python import_symbols.py --pdb Dev.pdb --db symbols.db
    python import_symbols.py --pdb Dev.pdb --db symbols.db --prefix "e:\\perforce\\example"
    python import_symbols.py --pdb Dev.pdb --db symbols.db --dll ./PdbAPI.dll --batch 50

What it does
------------
1. Open the PDB via PdbClient.
2. Enumerate *all* symbol names.
3. For each symbol, fetch the list of source files from DIA.
4. Normalise every path (strip prefix, lower-case, strip code extension).
5. Write symbols / files / links to the database.
6. Rebuild file_statistics so the scorer has fresh counts.
"""

import argparse
import sys
import time
from pathlib import Path

# Allow running from the scripts/ directory without installing the package
sys.path.insert(0, str(Path(__file__).parent))

from dumppdb_tools import PdbClient
from dumppdb_tools.analysis.file_normalizer import FileNormalizer
from dumppdb_tools.storage.database import Database
from dumppdb_tools.storage.models import create_all
from dumppdb_tools.storage.repository import SymbolRepository


# How often (in symbols) to commit and print a progress line
_COMMIT_EVERY = 200
_PRINT_EVERY  = 500


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Import PDB symbols + source files into SQLite."
    )
    p.add_argument("--pdb",    required=True,         help="Path to .pdb file")
    p.add_argument("--db",     default="symbols.db",  help="SQLite database path (default: symbols.db)")
    p.add_argument("--dll",    default="./PdbAPI.dll", help="Path to PdbAPI.dll")
    p.add_argument(
        "--prefix",
        default="",
        help="Directory prefix to strip from file paths, e.g. 'e:\\\\perforce\\\\code'",
    )
    p.add_argument(
        "--batch",
        type=int,
        default=_COMMIT_EVERY,
        help=f"Commit every N symbols (default: {_COMMIT_EVERY})",
    )
    return p.parse_args()


class SymbolImporter:
    """Orchestrates the import pipeline.

    Parameters
    ----------
    pdb:
        Open and initialised :class:`PdbClient`.
    repo:
        Repository for the target database.
    normalizer:
        :class:`FileNormalizer` configured with the path prefix.
    batch_size:
        Commit to SQLite every *batch_size* symbols.
    """

    def __init__(
        self,
        pdb: PdbClient,
        repo: SymbolRepository,
        normalizer: FileNormalizer,
        batch_size: int = _COMMIT_EVERY,
    ) -> None:
        self._pdb        = pdb
        self._repo       = repo
        self._normalizer = normalizer
        self._batch      = batch_size

    def run(self) -> None:
        print("Enumerating symbols ...", flush=True)
        raw = self._pdb.enumerate_symbols()
        symbols = [s.strip() for s in raw.splitlines() if s.strip()]
        total = len(symbols)
        print(f"Found {total} symbols.", flush=True)

        t0 = time.monotonic()
        errors = 0

        for idx, name in enumerate(symbols, start=1):
            try:
                self._import_one(name)
            except Exception as exc:
                errors += 1
                print(f"  [WARN] {name}: {exc}", file=sys.stderr)

            if idx % self._batch == 0:
                self._repo._db.commit()

            if idx % _PRINT_EVERY == 0 or idx == total:
                elapsed = time.monotonic() - t0
                print(
                    f"  {idx:>6}/{total}  ({elapsed:.1f}s)",
                    flush=True,
                )

        # Final commit
        self._repo._db.commit()

        print("\nRebuilding file statistics ...", flush=True)
        self._repo.rebuild_file_statistics()

        elapsed = time.monotonic() - t0
        print(
            f"\nDone.  symbols={self._repo.symbol_count()}"
            f"  files={self._repo.file_count()}"
            f"  errors={errors}"
            f"  time={elapsed:.1f}s"
        )

    def _import_one(self, symbol_name: str) -> None:
        symbol_id = self._repo.get_or_create_symbol(symbol_name)

        # get_source_files returns newline-separated paths (empty string when none)
        raw_files = self._pdb.get_source_files(symbol_name)
        paths = [p.strip() for p in raw_files.splitlines() if p.strip()]

        canonical_paths = self._normalizer.normalize_many(paths)

        for canon in canonical_paths:
            file_id = self._repo.get_or_create_file(canon)
            self._repo.link_symbol_file(symbol_id, file_id)


def main() -> int:
    args = _parse_args()

    pdb_path = Path(args.pdb)
    if not pdb_path.exists():
        print(f"Error: PDB file not found: {pdb_path}", file=sys.stderr)
        return 1

    print(f"PDB  : {pdb_path}")
    print(f"DB   : {args.db}")
    print(f"DLL  : {args.dll}")
    if args.prefix:
        print(f"Prefix strip: {args.prefix}")

    pdb = PdbClient(args.dll)
    try:
        pdb.open(str(pdb_path))
    except RuntimeError as exc:
        print(f"Error opening PDB: {exc}", file=sys.stderr)
        return 1

    normalizer = FileNormalizer(args.prefix or None)

    try:
        with Database(args.db) as db:
            create_all(db)
            repo     = SymbolRepository(db)
            importer = SymbolImporter(pdb, repo, normalizer, args.batch)
            importer.run()
    finally:
        pdb.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
