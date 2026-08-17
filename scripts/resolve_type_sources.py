#!/usr/bin/env python3
"""Dump source files for every type found in a PDB."""

import argparse
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import re
import sys

from dumppdb_tools import PdbClient
from dumppdb_tools import normalize_path
from dumppdb_tools import remove_extension

@dataclass
class TypeScore:
    path_name: str
    score: int

MAX_SCORE = 10_000

EXACT_FILENAME_SCORE = 10_000
TOKEN_FILENAME_SCORE = 7_000
PARTIAL_FILENAME_SCORE = 1_000
EXACT_PATH_SCORE = 500
PATH_TOKEN_SCORE = 100

EXTENSION_SCORE = 1_000

# TODO:
# meta prefixes and suffixes!!!
# exact match exists in the files, but no relationship - 10-50%???

# remove the internal types

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

# TODO: add frequency penalty - if the source file is referenced by many types - lower the score for this source file
"""
get_frequency_penalty(
    source,
    source_type_counts,
    total_types,
)
"""

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

def get_type_source(type: str, file_sources: set[str]) -> dict[str, int] | None:
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

        scores[cleaned] = get_filename_match_score(type, filename)
        scores[cleaned] += get_path_match_score(type, directory)
        scores[cleaned] += get_extension_score(sources)

    if not scores:
        return None

    return scores

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
        # source file.
        source_type_counts: Counter[str] = Counter()

        for sources in type_sources.values():
            for source in sources:
                source_type_counts[source] += 1

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
        results: dict[str, dict[str, int]] = {}

        prefix = normalize_path("e:/perforce/lanoire/shared/code/").lower()
        for type_name, sources in type_sources.items():
            scores = get_type_source(
                type_name,
                sources,
            )

            if scores is None:
                continue


            # here - find the best one. If lower than 50% of the best score - try to find in one of all of source files

            # can have the same score for multiple sources !!!! RESOLVE!! (manually??)

            # if one of ambigous sources - resolve...

            best_score = max(scores.values())
            best = [
                path
                for path, score in scores.items()
                if score == best_score
            ]

            results[type_name] = scores

        output_path = Path("pdb_type_sources.txt")

        with output_path.open(
            "w",
            encoding="utf-8",
        ) as output:
            for type_name, result in results.items():
                output.write(
                    f"{type_name} -> {result}\n"
                )

        # Debug output.
        """
        for type_name, sources in type_sources.items():
            print(type_name)

            for source in sources:
                print(f"  {source}")

            print()

        prefix = normalize_path("e:/perforce/lanoire/shared/code/").lower()

        unique_sources = {
            source
            for sources in type_sources.values()
            for source in sources
        }

        total_links = sum(
            len(sources)
            for sources in type_sources.values()
        )

        prefix_sources = {
            source
            for source in unique_sources
            if source.lower().startswith(prefix)
        }

        print()
        print("Statistics:")
        print(f"  Types found          : {len(types)}")
        print(f"  Types with sources   : {len(type_sources)}")
        print(f"  Unique source files  : {len(unique_sources)}")
        print(f"  Prefix source files  : {len(prefix_sources)}")
        print(f"  Type/source links    : {total_links}")
        """

    except Exception as e:
        print(f"Error: {pdb.last_error()}", file=sys.stderr)
        print(f"Exception: {e}", file=sys.stderr)
        return 1

    finally:
        pdb.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
    """
    cases = [
        ("VehicleManager", ["e:/perforce/lanoire/shared/code/game/vehiclemanager.h",
                            "e:/perforce/lanoire/shared/code/game/vehiclemanager.cpp"]),
        ("VehicleManager", ["e:/perforce/lanoire/shared/code/game/vehiclemanager.h",
                            "e:/perforce/lanoire/shared/code/game/vehiclemanager.h",
                            "e:/perforce/lanoire/shared/code/game/othertype.h"]),
        ("CPedFactory", ["e:/perforce/lanoire/shared/code/game/ped/pedfactory.cpp"]),
        ("CPedFactory", ["e:/perforce/lanoire/shared/code/game/ped/ped.cpp",  # only partial token match
                            "e:/perforce/lanoire/shared/code/game/unrelated/config.h"]),
    ]

    for type_name, sources in cases:
        result = get_type_source(type_name, sources)
        print(type_name, "->", result)
    """