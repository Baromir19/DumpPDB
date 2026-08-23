#!/usr/bin/env python3
"""Dump source files for every type found in a PDB."""

import argparse
from collections import Counter
from datetime import datetime
import logging
from pathlib import Path
import sys

from dumppdb_tools import (
    PdbClient,
    SourceDatabase,
    TypeAggregator,
    get_type_source,
    is_type_excluded,
    load_exclude_patterns,
    normalize_path,
    remove_extension,
    resolve_best,
)

DEFAULT_EXCLUDE_FILE = "exclude_types.txt"
DEFAULT_LOG_DIR = "logs"
DEFAULT_DB = "sources.db"
DEFAULT_OUTPUT = "pdb_type_sources.txt"


def setup_logging(log_dir: str = DEFAULT_LOG_DIR) -> Path:
    """Configure file + stdout logging. Returns the log file path."""
    log_path = Path(log_dir)
    log_path.mkdir(parents=True, exist_ok=True)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = log_path / f"resolve_type_sources_{timestamp}.log"

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        handlers=[
            logging.FileHandler(log_file, encoding="utf-8"),
            logging.StreamHandler(sys.stdout),
        ],
    )

    return log_file


def validate_db(db_path: str) -> bool:
    """Return ``False`` if the DB is missing, or ``normalized_paths`` is absent / empty."""
    db = SourceDatabase(db_path)

    if not db.validate_for_resolution():
        logging.error(
            "Database %s is not ready: normalized_paths table is missing or empty. "
            "Run recover_file_sources --write-db first.",
            db_path,
        )
        return False

    return True


def collect_type_sources(pdb: PdbClient) -> dict[str, set[str]]:
    """Enumerate PDB types and their associated source files."""
    symbols = pdb.enumerate_symbols(True)
    types = {s.strip() for s in symbols.splitlines() if s.strip()}

    type_sources: dict[str, set[str]] = {}

    for type_name in types:
        try:
            raw = pdb.get_source_files(type_name, True)
        except Exception:
            continue

        sources = [
            normalize_path(s.strip())
            for s in raw.splitlines()
            if s.strip()
        ]

        if sources:
            type_sources[type_name] = set(sources)

    return type_sources


def collect_all_sources(pdb: PdbClient, fallback: dict[str, set[str]]) -> set[str]:
    """Enumerate all PDB source files; falls back to the union of per-type sources."""
    try:
        raw = pdb.enumerate_source_files()
        return {
            normalize_path(s.strip())
            for s in raw.splitlines()
            if s.strip()
        }
    except Exception:
        return {s for srcs in fallback.values() for s in srcs}


def build_source_counts(
    aggregated: dict[str, set[str]],
) -> tuple[Counter[str], int]:
    """Count how many types reference each extension-stripped source path.

    Returns ``(source_type_counts, total_types)``.
    Aggregation is done before this step so template specialisations do not
    inflate shared header frequencies.
    """
    counts: Counter[str] = Counter()

    for sources in aggregated.values():
        for source in sources:
            counts[remove_extension(source)] += 1

    return counts, len(aggregated)


def log_frequent_sources(
    source_type_counts: Counter[str],
    total_types: int,
) -> None:
    """Log source files referenced by >= 5 % of types (top 10)."""
    if not total_types:
        return

    threshold = total_types * 0.05
    frequent = sorted(
        [(s, c) for s, c in source_type_counts.items() if c >= threshold],
        key=lambda x: x[1],
        reverse=True,
    )

    logging.info("Frequently referenced source files:")

    if frequent:
        for source, count in frequent[:10]:
            logging.info("  %5d (%5.1f%%)  %s", count, count / total_types * 100, source)
    else:
        logging.info("  None")


def resolve_all(
    aggregated: dict[str, set[str]],
    source_type_counts: Counter[str],
    total_types: int,
    all_sources: set[str],
) -> tuple[dict[str, dict[str, int]], dict[str, str]]:
    """Score and resolve every aggregated type. Returns ``(results, statuses)``."""
    results: dict[str, dict[str, int]] = {}
    statuses: dict[str, str] = {}

    for type_name, merged in aggregated.items():
        scores = get_type_source(
            type_name, merged, source_type_counts, total_types,
            global_sources=all_sources,
        )

        if scores is None:
            continue

        _, _, status = resolve_best(scores)
        results[type_name] = scores
        statuses[type_name] = status

    return results, statuses


def persist_results(
    db: SourceDatabase,
    aggregated: dict[str, set[str]],
    agg: TypeAggregator,
    results: dict[str, dict[str, int]],
) -> None:
    """Write types, relations, and type_source_files links to the database."""
    path_prefix = db.get_setting("path_prefix") or ""
    if not path_prefix:
        logging.warning("No 'path_prefix' setting in DB; paths will not be trimmed.")

    output_ext = db.get_setting("output_extension") or ".hpp"
    if db.get_setting("output_extension") is None:
        logging.warning("No 'output_extension' setting in DB; using '.hpp'.")

    type_id_cache: dict[str, int] = {}
    path_id_cache: dict[str, int] = {}

    # Upsert all needed types (resolved + relations).
    types_needed = set(aggregated)
    types_needed.update(agg.meta_variant_types)
    for insts in agg.template_type_links.values():
        types_needed.update(insts)

    for name in types_needed:
        type_id_cache[name] = db.upsert_type(name)

    # Type-to-type relations.
    for base, variants in agg.meta_type_links.items():
        base_id = type_id_cache[base]
        for variant in variants:
            db.add_type_relation(base_id, type_id_cache[variant], "meta_variant")

    for base, insts in agg.template_type_links.items():
        base_id = type_id_cache[base]
        for inst in insts:
            db.add_type_relation(base_id, type_id_cache[inst], "template_instance")

    # Type-source-file links.
    for type_name, scores in results.items():
        type_id = type_id_cache.get(type_name)
        if type_id is None:
            continue

        raw_sources = aggregated.get(type_name, set())

        for path, score in scores.items():
            for raw in raw_sources:
                if remove_extension(raw) == path:
                    file_id = db.resolve_path_id(raw, path_prefix, cache=path_id_cache)
                    if file_id is not None:
                        db.add_type_source_file(type_id, file_id, score=score)
                    break


def write_output(
    results: dict[str, dict[str, int]],
    agg: TypeAggregator,
    output_path: Path,
) -> None:
    """Write scores and type-link sections to the output text file."""
    with output_path.open("w", encoding="utf-8") as f:
        for type_name, result in results.items():
            f.write(f"{type_name} -> {result}\n")

        if agg.meta_type_links:
            f.write("\n# Meta type links (base -> meta variants)\n")
            for base, variants in sorted(agg.meta_type_links.items()):
                f.write(f"{base} -> {sorted(set(variants))}\n")

        if agg.template_type_links:
            f.write("\n# Template type links (base -> instantiations)\n")
            for base, insts in sorted(agg.template_type_links.items()):
                f.write(f"{base} -> {sorted(insts)}\n")


def log_summary(
    results: dict[str, dict[str, int]],
    statuses: dict[str, str],
) -> None:
    """Log resolution status counts and low-confidence / ambiguous details."""
    status_counts = Counter(statuses.values())

    logging.info("Resolution summary:")
    for status, count in sorted(status_counts.items()):
        logging.info("  %-16s %5d", status, count)

    for type_name, status in sorted(statuses.items()):
        if status not in ("ambiguous", "low_confidence"):
            continue

        top = sorted(results[type_name].items(), key=lambda x: x[1], reverse=True)[:3]
        logging.info("%s [%s]:", type_name, status)
        for path, score in top:
            logging.info("  %6d  %s", score, path)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="List source files for every type found in a PDB."
    )

    parser.add_argument("--dll", default="./PdbAPI.dll", help="Path to PdbAPI.dll")
    parser.add_argument("--pdb", required=True, help="Path to the PDB file")

    parser.add_argument(
        "--meta-prefix",
        action="append", default=[], metavar="PREFIX",
        help=(
            "Meta prefix marking a generated type. "
            "E.g. 'C' means CVehicle inherits sources from Vehicle. "
            "Can be specified multiple times."
        ),
    )
    parser.add_argument(
        "--meta-suffix",
        action="append", default=[], metavar="SUFFIX",
        help=(
            "Meta suffix marking a generated type. "
            "E.g. 'Definition' means VehicleDefinition inherits sources from Vehicle. "
            "Can be specified multiple times."
        ),
    )

    parser.add_argument(
        "--exclude-file", default=DEFAULT_EXCLUDE_FILE,
        help=f"File with type-exclusion patterns (git-ignore style). Default: {DEFAULT_EXCLUDE_FILE}",
    )
    parser.add_argument(
        "--log-dir", default=DEFAULT_LOG_DIR,
        help=f"Directory for log files. Default: {DEFAULT_LOG_DIR}",
    )
    parser.add_argument(
        "--db", default=DEFAULT_DB,
        help=f"SQLite database path. Default: {DEFAULT_DB}",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    log_file = setup_logging(args.log_dir)
    logging.info("Log file: %s", log_file)

    if not validate_db(args.db):
        logging.error("Aborting: database not ready for type-source resolution.")
        return 1

    exclude_patterns = load_exclude_patterns(args.exclude_file)

    pdb = PdbClient(args.dll)

    try:
        pdb.open(args.pdb)
        logging.info("PDB: %s", args.pdb)

        type_sources = collect_type_sources(pdb)

        if exclude_patterns:
            before = len(type_sources)
            type_sources = {
                t: s for t, s in type_sources.items()
                if not is_type_excluded(t, exclude_patterns)
            }
            logging.info(
                "Excluded %d type(s) via exclude patterns (%d remaining)",
                before - len(type_sources),
                len(type_sources),
            )

        agg = TypeAggregator(args.meta_prefix, args.meta_suffix)
        agg.process(type_sources)

        source_type_counts, total_types = build_source_counts(agg.aggregated_sources)
        log_frequent_sources(source_type_counts, total_types)

        all_sources = collect_all_sources(pdb, type_sources)

        results, statuses = resolve_all(
            agg.aggregated_sources, source_type_counts, total_types, all_sources
        )

        db = SourceDatabase(args.db)
        db.open()

        try:
            persist_results(db, agg.aggregated_sources, agg, results)
            db.commit()
            logging.info("Database updated: %s", args.db)
        finally:
            db.close()

        output_path = Path(DEFAULT_OUTPUT)
        write_output(results, agg, output_path)
        logging.info("Results written to %s", output_path)

        log_summary(results, statuses)

    except Exception as e:
        logging.error("Error: %s", pdb.last_error())
        logging.error("Exception: %s", e)
        return 1

    finally:
        pdb.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
