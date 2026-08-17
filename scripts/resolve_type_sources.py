#!/usr/bin/env python3
"""Dump source files for every type found in a PDB."""

import argparse
from collections import Counter
from pathlib import Path
import re
import sys

from dumppdb_tools import PdbClient
from dumppdb_tools import normalize_path
from dumppdb_tools import remove_extension

MAX_SCORE = 10_000

EXACT_FILENAME_SCORE = 10_000
TOKEN_FILENAME_SCORE = 7_000
PARTIAL_FILENAME_SCORE = 1_000
EXACT_PATH_SCORE = 500
PATH_TOKEN_SCORE = 100

EXTENSION_SCORE = 1_000

# Minimum score to trust a candidate from the type's own sources.
MIN_CONFIDENCE = 5_000

# Penalty applied to scores from the global (all-sources) search.
GLOBAL_SEARCH_PENALTY = 0.4

# Maximum frequency penalty (as a fraction of MAX_SCORE).
MAX_FREQUENCY_PENALTY = 0.5

# Bonus for rare source files (as a fraction of MAX_SCORE).
RARE_FILE_BONUS = 0.02

# Threshold for "low confidence" — if the gap between best and second
# is smaller than this fraction of the best score, mark as low confidence.
LOW_CONFIDENCE_GAP = 0.1


def split_type_tokens(value: str) -> list[str]:
    value = value.replace("_", " ")

    return [
        token.lower()
        for token in re.findall(
            r"[A-Z]+(?=[A-Z][a-z]|[0-9]|\b)|[A-Z]?[a-z]+|[0-9]+",
            value,
        )
    ]


def get_filename_match_score(type_name: str, file_name: str) -> int:
    if type_name.lower() == file_name.lower():
        return MAX_SCORE

    type_lower = type_name.lower()
    file_lower = file_name.lower()

    # Exact match.
    if type_lower == file_lower:
        return MAX_SCORE

    type_tokens = set(split_type_tokens(type_name))
    if not type_tokens:
        return 0

    file_tokens = set(split_type_tokens(file_name))

    intersection = type_tokens & file_tokens

    matched_tokens = {
        token
        for token in type_tokens
        if token in file_lower
    }

    if type_tokens and matched_tokens == type_tokens:
        return TOKEN_FILENAME_SCORE

    if matched_tokens:
        return int(
            len(matched_tokens) / len(type_tokens)
            * PARTIAL_FILENAME_SCORE
        )

    return 0


def get_frequency_penalty(
    source: str,
    source_type_counts: Counter[str],
    total_types: int,
) -> int:
    """Penalize source files that are referenced by many types.

    A file referenced by 90% of types gets a large penalty, while a file
    referenced by 1% of types gets a small one.
    """
    if total_types == 0:
        return 0

    count = source_type_counts.get(source, 0)
    frequency = count / total_types

    return int(frequency * MAX_SCORE * MAX_FREQUENCY_PENALTY)


def get_frequency_bonus(
    source: str,
    source_type_counts: Counter[str],
    total_types: int,
) -> int:
    """Give a small bonus to rare source files."""
    if total_types == 0:
        return 0

    count = source_type_counts.get(source, 0)
    frequency = count / total_types

    # Rare files (referenced by <5% of types) get a small bonus.
    if frequency < 0.05:
        return int(MAX_SCORE * RARE_FILE_BONUS)

    return 0


def get_path_match_score(
    type_name: str,
    path: str,
) -> int:
    type_lower = type_name.lower()
    type_tokens = {
        token
        for token in split_type_tokens(type_name)
        if len(token) >= 4
    }

    parts = path.lower().split("/")

    score = 0

    for part in parts:
        if part == type_lower:
            score += EXACT_PATH_SCORE
            continue

        for token in type_tokens:
            if token in part:
                score += PATH_TOKEN_SCORE

    return score


def get_extension_score(sources: set[str]) -> int:
    extensions = {
        Path(source).suffix.lower()
        for source in sources
    }

    return min(len(extensions), 3) * EXTENSION_SCORE


def _score_sources(
    type_name: str,
    file_sources: set[str],
    source_type_counts: Counter[str],
    total_types: int,
) -> dict[str, int] | None:
    scores: dict[str, int] = {}

    cleaned_filesources: dict[str, set[str]] = {}

    for source in file_sources:
        cleaned = remove_extension(source)

        if cleaned not in cleaned_filesources:
            cleaned_filesources[cleaned] = set()

        cleaned_filesources[cleaned].add(source)

    for cleaned, sources in cleaned_filesources.items():
        filename = cleaned.rsplit("/")[-1]

        if "/" in cleaned:
            directory = cleaned.rsplit("/", 1)[0]
        else:
            directory = ""

        score = get_filename_match_score(type_name, filename)
        score += get_path_match_score(type_name, directory)
        score += get_extension_score(sources)

        # Frequency penalty / bonus.
        score -= get_frequency_penalty(
            cleaned,
            source_type_counts,
            total_types,
        )
        score += get_frequency_bonus(
            cleaned,
            source_type_counts,
            total_types,
        )

        scores[cleaned] = score

    if not scores:
        return None

    return scores


def get_type_source(
    type_name: str,
    file_sources: set[str],
    source_type_counts: Counter[str],
    total_types: int,
    global_sources: set[str] | None = None,
) -> dict[str, int] | None:
    """Score candidate source files for a type.

    First pass: score only the sources the PDB linked to this type.
    If the best score is below MIN_CONFIDENCE, do a second pass over
    ALL source files in the PDB, applying a penalty.
    """
    scores = _score_sources(
        type_name,
        file_sources,
        source_type_counts,
        total_types,
    )

    if scores is None:
        return None

    best_score = max(scores.values())

    if best_score < MIN_CONFIDENCE and global_sources:
        global_scores = _score_sources(
            type_name,
            global_sources,
            source_type_counts,
            total_types,
        )

        if global_scores:
            # Apply global-search penalty.
            for path in global_scores:
                global_scores[path] = int(
                    global_scores[path] * (1 - GLOBAL_SEARCH_PENALTY)
                )

            scores = global_scores

    return scores


def resolve_best(
    scores: dict[str, int],
) -> tuple[list[str] | str | None, int, str]:
    """Resolve the best candidate from a score map.

    Returns (best, best_score, status) where status is one of:
      - "resolved": exactly one clear winner
      - "low_confidence": winner exists but gap to second is small
      - "ambiguous": multiple candidates tied for first
      - "no_match": no candidate scored above zero
    """
    if not scores:
        return None, 0, "no_match"

    sorted_scores = sorted(
        scores.items(),
        key=lambda item: item[1],
        reverse=True,
    )

    best_path, best_score = sorted_scores[0]

    if best_score == 0:
        return None, 0, "no_match"

    # Multiple candidates tied for first place.
    tied = [
        path
        for path, score in sorted_scores
        if score == best_score
    ]

    if len(tied) > 1:
        return tied, best_score, "ambiguous"

    # Check the gap to the second-best candidate.
    if len(sorted_scores) > 1:
        second_score = sorted_scores[1][1]

        if best_score - second_score < best_score * LOW_CONFIDENCE_GAP:
            return best_path, best_score, "low_confidence"

    return best_path, best_score, "resolved"


def get_meta_variants(
    type_name: str,
    meta_prefixes: list[str],
    meta_suffixes: list[str],
) -> list[str]:
    """Return base type names for a type that carries a meta prefix/suffix.

    E.g. with meta_suffixes=["Definition"], "VehicleDefinition" -> ["Vehicle"].
    With meta_prefixes=["C"], "CVehicle" -> ["Vehicle"].
    """
    variants: list[str] = []

    for prefix in meta_prefixes:
        if (
            type_name.startswith(prefix)
            and len(type_name) > len(prefix)
        ):
            variants.append(type_name[len(prefix):])

    for suffix in meta_suffixes:
        if (
            type_name.endswith(suffix)
            and len(type_name) > len(suffix)
        ):
            variants.append(type_name[:-len(suffix)])

    return variants


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="List source files for every type found in a PDB."
    )

    parser.add_argument(
        "--dll",
        default="./PdbAPI.dll",
        help="Path to PdbAPI.dll",
    )

    parser.add_argument(
        "--pdb",
        required=True,
        help="Path to the PDB file",
    )

    parser.add_argument(
        "--meta-prefix",
        action="append",
        default=[],
        metavar="PREFIX",
        help=(
            "Meta prefix that marks a generated type. "
            "E.g. 'C' means CVehicle inherits sources from Vehicle. "
            "Can be specified multiple times."
        ),
    )

    parser.add_argument(
        "--meta-suffix",
        action="append",
        default=[],
        metavar="SUFFIX",
        help=(
            "Meta suffix that marks a generated type. "
            "E.g. 'Definition' means VehicleDefinition inherits sources "
            "from Vehicle. Can be specified multiple times."
        ),
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    pdb = PdbClient(args.dll)

    try:
        pdb.open(args.pdb)

        print("PDB:", args.pdb)
        print()

        symbols = pdb.enumerate_symbols(True)

        types = [
            symbol.strip()
            for symbol in symbols.splitlines()
            if symbol.strip()
        ]

        type_sources: dict[str, set[str]] = {}

        for type_name in types:
            try:
                raw_sources = pdb.get_source_files(
                    type_name,
                    True,
                )
            except Exception:
                # No source files / failed lookup.
                continue

            sources = [
                normalize_path(source.strip())
                for source in raw_sources.splitlines()
                if source.strip()
            ]

            if sources:
                type_sources[type_name] = set(sources)

        # ==============================================
        # Frequently referenced source files.
        #
        # Count how many different types reference each
        # source file (by extension-stripped path).
        source_type_counts: Counter[str] = Counter()

        for sources in type_sources.values():
            for source in sources:
                cleaned = remove_extension(source)
                source_type_counts[cleaned] += 1

        total_types = len(type_sources)

        if total_types:
            suspicious_threshold = total_types * 0.05

            frequent_sources = [
                (source, count)
                for source, count in source_type_counts.items()
                if count >= suspicious_threshold
            ]

            frequent_sources.sort(
                key=lambda item: item[1],
                reverse=True,
            )

            print("Frequently referenced source files:")

            if frequent_sources:
                for source, count in frequent_sources[:10]:
                    percentage = count / total_types * 100

                    print(
                        f"  {count:5d} ({percentage:5.1f}%)  {source}"
                    )
            else:
                print("  None")

            print()

        # ==============================================
        # Meta-prefix / meta-suffix handling.
        #
        # If a type carries a meta prefix/suffix (e.g. VehicleDefinition),
        # its sources are merged into the base type (Vehicle).
        meta_sources: dict[str, set[str]] = {}

        if args.meta_prefix or args.meta_suffix:
            for type_name in type_sources:
                variants = get_meta_variants(
                    type_name,
                    args.meta_prefix,
                    args.meta_suffix,
                )

                for variant in variants:
                    if variant in type_sources:
                        meta_sources.setdefault(
                            variant,
                            set(),
                        ).update(type_sources[type_name])

            if meta_sources:
                print("Meta type sources merged:")
                for base, sources in sorted(meta_sources.items()):
                    print(f"  {base} += {len(sources)} source(s)")
                print()

        # ==============================================
        # Global source set for the second-pass search.
        all_sources: set[str] = set()

        try:
            raw_all = pdb.enumerate_source_files()
            all_sources = {
                normalize_path(source.strip())
                for source in raw_all.splitlines()
                if source.strip()
            }
        except Exception:
            # Fall back to the union of per-type sources.
            all_sources = {
                source
                for sources in type_sources.values()
                for source in sources
            }

        # ==============================================
        results: dict[str, dict[str, int]] = {}
        statuses: dict[str, str] = {}

        for type_name, sources in type_sources.items():
            # Merge meta-variant sources into this type.
            merged_sources = set(sources)
            merged_sources.update(meta_sources.get(type_name, set()))

            # If this type is itself a meta-variant, merge the base
            # type's sources so both resolve to the same file.
            for variant in get_meta_variants(
                type_name,
                args.meta_prefix,
                args.meta_suffix,
            ):
                if variant in type_sources:
                    merged_sources.update(type_sources[variant])

            scores = get_type_source(
                type_name,
                merged_sources,
                source_type_counts,
                total_types,
                global_sources=all_sources,
            )

            if scores is None:
                continue

            best, best_score, status = resolve_best(scores)

            results[type_name] = scores
            statuses[type_name] = status

        output_path = Path("pdb_type_sources.txt")

        with output_path.open(
            "w",
            encoding="utf-8",
        ) as output:
            for type_name, result in results.items():
                output.write(
                    f"{type_name} -> {result}\n"
                )

        # Print a summary of resolution statuses.
        status_counts = Counter(statuses.values())

        print("Resolution summary:")
        for status, count in sorted(status_counts.items()):
            print(f"  {status:16s} {count:5d}")

        print()

        # Print low-confidence / ambiguous types.
        for type_name, status in sorted(statuses.items()):
            if status in ("ambiguous", "low_confidence"):
                scores = results[type_name]
                sorted_scores = sorted(
                    scores.items(),
                    key=lambda item: item[1],
                    reverse=True,
                )[:3]

                print(f"{type_name} [{status}]:")
                for path, score in sorted_scores:
                    print(f"  {score:6d}  {path}")

                print()

    except Exception as e:
        print(f"Error: {pdb.last_error()}", file=sys.stderr)
        print(f"Exception: {e}", file=sys.stderr)
        return 1

    finally:
        pdb.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())