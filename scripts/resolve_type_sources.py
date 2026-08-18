#!/usr/bin/env python3
"""Dump source files for every type found in a PDB."""

import argparse
from collections import Counter
from datetime import datetime
import fnmatch
import logging
from pathlib import Path
import re
import sys

from dumppdb_tools import PdbClient
from dumppdb_tools import normalize_path
from dumppdb_tools import remove_extension

MAX_SCORE = 10_000

EXACT_FILENAME_SCORE = 10_000
TOKEN_FILENAME_SCORE = 7_000
PARTIAL_FILENAME_SCORE = 4_000
EXACT_PATH_SCORE = 500
PATH_TOKEN_SCORE = 100

EXTENSION_SCORE = 1_000

# Per-scope weights, last scope first: the type's own name always gets
# 100% of its potential score; each earlier namespace scope gets a
# progressively smaller share (60%, 40%, 30%, 25%).
# E.g. AI::NavMeshPrivate::AABSPTree -> weights [0.4, 0.6, 1.0].
SCOPE_WEIGHTS = (1.0, 0.6, 0.4, 0.3, 0.25)
# Maximum number of type-name scopes to consider; deeper namespaces are
# ignored (they carry little signal).
SCOPE_DEPTH_LIMIT = 10

# Minimum token length for a meaningful substring match signal.
# Short tokens (e.g. "ai" from "AI") match as substrings in too many
# unrelated words (rainmap, aimmode, main, ...) and add noise.
MIN_TOKEN_LENGTH = 4

# Per-character penalty for stray characters that appear in a file name
# but are NOT covered by the type's matched tokens.
# Letters cost more (they usually mean an extra word), digits less
# (they often mean a revision / copy number).
EXTRA_LETTER_PENALTY = 30
EXTRA_DIGIT_PENALTY = 8

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

DEFAULT_EXCLUDE_FILE = "exclude_types.txt"
DEFAULT_LOG_DIR = "logs"


def setup_logging(log_dir: str = DEFAULT_LOG_DIR) -> Path:
    """Configure file logging. Returns the log file path."""
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


def load_exclude_patterns(path: str | Path) -> list[str]:
    """Load exclude patterns from a file (git-ignore style).

    Lines starting with '#' are comments. Blank lines are ignored.
    Patterns support '*' and '?' wildcards via fnmatch.
    """
    exclude_path = Path(path)

    if not exclude_path.exists():
        logging.info("Exclude file not found: %s (no types excluded)", exclude_path)
        return []

    patterns: list[str] = []

    with exclude_path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()

            if not line or line.startswith("#"):
                continue

            patterns.append(line)

    logging.info("Loaded %d exclude pattern(s) from %s", len(patterns), exclude_path)
    return patterns


def is_type_excluded(type_name: str, patterns: list[str]) -> bool:
    """Check if a type name matches any exclude pattern (git-ignore style).

    A pattern like 'std::*' matches any type starting with 'std::'.
    A pattern like '*FdbData' matches any type ending with 'FdbData'.
    A pattern like 'hkPreferences' matches exactly that type.
    """
    if not patterns:
        return False

    for pattern in patterns:
        if fnmatch.fnmatch(type_name, pattern):
            return True

    return False


def split_type_tokens(value: str) -> list[str]:
    value = value.replace("_", " ")

    return [
        token.lower()
        for token in re.findall(
            r"[A-Z]+(?=[A-Z][a-z]|[0-9]|\b)|[A-Z]?[a-z]+|[0-9]+",
            value,
        )
    ]


def strip_template_args(type_name: str) -> str:
    """Remove template argument contents while preserving surrounding name.

    Examples:
        WeakRef<Actor> -> WeakRef
        std::vector<int> -> std::vector
        Foo<Bar>::Baz -> Foo::Baz
        std::map<int, std::vector<float>> -> std::map
    """
    result: list[str] = []
    depth = 0

    for char in type_name:
        if char == "<":
            depth += 1
            continue

        if char == ">":
            if depth > 0:
                depth -= 1
                continue

        if depth == 0:
            result.append(char)

    return "".join(result)


def _get_scope_weights(num_scopes: int) -> list[float]:
    """Weights for type-name scopes, from first to last.

    The last scope (the type's own name) always gets weight 1.0 (100%);
    each earlier namespace scope gets a smaller share per SCOPE_WEIGHTS.
    Namespaces deeper than SCOPE_DEPTH_LIMIT are ignored.

    Example (3 scopes): AI::NavMeshPrivate::AABSPTree -> [0.4, 0.6, 1.0]
    """
    if num_scopes <= 0:
        return []

    limit = min(num_scopes, SCOPE_DEPTH_LIMIT)

    # SCOPE_WEIGHTS is ordered last->first; pad with the smallest weight
    # if the type is very deeply nested, then trim to the depth limit.
    weights = list(SCOPE_WEIGHTS)
    if len(weights) < limit:
        weights.extend([SCOPE_WEIGHTS[-1]] * (limit - len(weights)))
    elif len(weights) > limit:
        weights = weights[:limit]

    # Reverse so index 0 == first (outermost) scope.
    weights.reverse()
    return weights


def get_filename_match_score(type_name: str, file_name: str) -> int:
    type_lower = type_name.lower()
    file_lower = file_name.lower()

    # Exact match.
    if type_lower == file_lower:
        return MAX_SCORE

    scopes = [scope for scope in type_name.split("::") if scope]
    if not scopes:
        return 0

    scope_weights = _get_scope_weights(len(scopes))

    # The last scope is the type's own name. If every one of its tokens
    # appears in the file name, that is the strongest possible signal
    # (e.g. AI::NavMeshPrivate::AABSPTree matches "aabsptree").
    last_tokens = {
        token
        for token in split_type_tokens(scopes[-1])
        if len(token) >= MIN_TOKEN_LENGTH
    }
    if last_tokens and all(token in file_lower for token in last_tokens):
        return TOKEN_FILENAME_SCORE

    # Otherwise accumulate per-scope token matches. The last scope (the
    # type's own name) always carries full weight; each earlier
    # namespace scope contributes a smaller weight, and scopes are NOT
    # divided by the total weight (each is independent).
    #
    # Short tokens (e.g. "ai" from "AI") are filtered out: they match as
    # substrings in too many unrelated words and add noise.
    weighted = 0.0
    last_scope_partial = 0.0

    for index, (scope, weight) in enumerate(zip(scopes, scope_weights)):
        scope_tokens = {
            token
            for token in split_type_tokens(scope)
            if len(token) >= MIN_TOKEN_LENGTH
        }
        if not scope_tokens:
            continue

        matched = sum(1 for token in scope_tokens if token in file_lower)

        contribution = weight * (matched / len(scope_tokens))

        if index == len(scopes) - 1:
            # The last scope (the type's own name) is the strongest
            # signal. Keep its partial match separate so it is not
            # diluted by noisy namespace scopes.
            last_scope_partial = contribution
        else:
            weighted += contribution

    if weighted <= 0 and last_scope_partial <= 0:
        return 0

    # The last scope's partial match takes priority over the sum of the
    # namespace scopes: a partial match of the type's own name is a
    # stronger signal than a full match of a short namespace token.
    best = max(weighted, last_scope_partial)

    # Cap at PARTIAL so a non-fully-matched last scope stays below
    # TOKEN_FILENAME_SCORE (which is earned by a full last-scope match).
    return min(
        int(best * PARTIAL_FILENAME_SCORE),
        PARTIAL_FILENAME_SCORE,
    )


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
    scopes = [scope for scope in type_name.split("::") if scope]
    if not scopes:
        return 0

    scope_weights = _get_scope_weights(len(scopes))
    parts = path.lower().split("/")

    score = 0

    for scope, weight in zip(scopes, scope_weights):
        scope_tokens = {
            token
            for token in split_type_tokens(scope)
            if len(token) >= MIN_TOKEN_LENGTH
        }
        if not scope_tokens:
            continue

        for part in parts:
            if part == scope.lower():
                score += int(EXACT_PATH_SCORE * weight)
                continue

            for token in scope_tokens:
                if token in part:
                    score += int(PATH_TOKEN_SCORE * weight)

    return score


def get_extension_score(sources: set[str]) -> int:
    extensions = {Path(source).suffix.lower() for source in sources}

    return min(len(extensions), 3) * EXTENSION_SCORE


def get_extra_character_penalty(
    type_name: str,
    file_name: str,
) -> int:
    """Penalize stray characters in a file name not covered by the type.

    E.g. for type "AbstractBuffer":
      - "abstractbuffer11"      -> penalized for digits "11"
      - "abstractbuffercube11"  -> penalized for "cube" (letters) + "11"

    The penalty is per-character so longer stray words cost more.
    Letters cost more than digits.
    """
    if not type_name or not file_name:
        return 0

    type_lower = type_name.lower()
    file_lower = file_name.lower()

    # Anchor on the type's own name (the last scope), so for
    # "AI::NavMeshPrivate::AABSPTree" the meaningful part of "aabsptree.h"
    # is "aabsptree" — no penalty is applied for the matching part.
    scopes = type_lower.split("::")
    anchor = scopes[-1] if scopes else type_lower

    remainder = file_lower.replace(anchor, "")

    if not remainder:
        return 0

    penalty = 0

    for char in remainder:
        if char.isalpha():
            penalty += EXTRA_LETTER_PENALTY
        elif char.isdigit():
            penalty += EXTRA_DIGIT_PENALTY
        # Ignore separators (_, /, -, etc.) — they carry no meaning.

    return penalty


def _score_sources(
    type_name: str,
    file_sources: set[str],
    source_type_counts: Counter[str],
    total_types: int,
) -> dict[str, int] | None:
    scores: dict[str, int] = {}

    # Score against the template-stripped name: template arguments carry
    # no signal about where the template itself is declared.
    scoring_name = strip_template_args(type_name)

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

        score = get_filename_match_score(scoring_name, filename)
        score += get_path_match_score(scoring_name, directory)
        score += get_extension_score(sources)

        # Penalize stray characters that are not covered by the type name.
        score -= get_extra_character_penalty(scoring_name, filename)

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
    tied = [path for path, score in sorted_scores if score == best_score]

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
        if type_name.startswith(prefix) and len(type_name) > len(prefix):
            variants.append(type_name[len(prefix) :])

    for suffix in meta_suffixes:
        if type_name.endswith(suffix) and len(type_name) > len(suffix):
            variants.append(type_name[: -len(suffix)])

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

    parser.add_argument(
        "--exclude-file",
        default=DEFAULT_EXCLUDE_FILE,
        help=(
            "Path to a file with type-exclusion patterns (git-ignore style). "
            f"Default: {DEFAULT_EXCLUDE_FILE}"
        ),
    )

    parser.add_argument(
        "--log-dir",
        default=DEFAULT_LOG_DIR,
        help=("Directory for log files. " f"Default: {DEFAULT_LOG_DIR}"),
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    log_file = setup_logging(args.log_dir)
    logging.info("Log file: %s", log_file)

    exclude_patterns = load_exclude_patterns(args.exclude_file)

    pdb = PdbClient(args.dll)

    try:
        pdb.open(args.pdb)

        logging.info("PDB: %s", args.pdb)

        symbols = pdb.enumerate_symbols(True)

        types = {symbol.strip() for symbol in symbols.splitlines() if symbol.strip()}

        # Filter out excluded types.
        if exclude_patterns:
            before = len(types)
            types = {t for t in types if not is_type_excluded(t, exclude_patterns)}
            excluded_count = before - len(types)
            logging.info(
                "Excluded %d type(s) via exclude patterns (%d remaining)",
                excluded_count,
                len(types),
            )

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

            logging.info("Frequently referenced source files:")

            if frequent_sources:
                for source, count in frequent_sources[:10]:
                    percentage = count / total_types * 100

                    logging.info(
                        "  %5d (%5.1f%%)  %s",
                        count,
                        percentage,
                        source,
                    )
            else:
                logging.info("  None")

        # ==============================================
        # Meta-prefix / meta-suffix handling.
        #
        # If a type carries a meta prefix/suffix (e.g. VehicleDefinition),
        # its sources are merged into the base type (Vehicle), and the
        # meta-variant type itself is removed from the resolution set.
        meta_sources: dict[str, set[str]] = {}
        meta_variant_types: set[str] = set()
        # Preserve the link between a base type and the meta-variant
        # types whose sources were folded into it (e.g. Vehicle ->
        # [VehicleDefinition]), so a future DB can record that
        # "Vehicle has metatype VehicleDefinition".
        meta_type_links: dict[str, list[str]] = {}

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
                        meta_variant_types.add(type_name)
                        meta_type_links.setdefault(
                            variant,
                            [],
                        ).append(type_name)

            if meta_sources:
                logging.info("Meta type sources merged:")
                for base, sources in sorted(meta_sources.items()):
                    logging.info("  %s += %d source(s)", base, len(sources))

            if meta_variant_types:
                logging.info(
                    "Meta-variant types removed from resolution: %d",
                    len(meta_variant_types),
                )

            if meta_type_links:
                logging.info("Meta type links (base -> meta variants):")
                for base, variants in sorted(meta_type_links.items()):
                    logging.info(
                        "  %s -> %s",
                        base,
                        ", ".join(sorted(set(variants))),
                    )

        # ==============================================
        # Template specialization handling.
        #
        # Template instantiations (e.g. WeakRef<Actor>, WeakRef<Vehicle>)
        # are grouped into a single template-stripped base type (WeakRef).
        # The union of all of their source files is folded into the base
        # type, which is then resolved exactly once. The link between the
        # base type and each specialization is preserved so a future DB
        # can record that "WeakRef has template instance WeakRef<Actor>".
        template_sources: dict[str, set[str]] = {}
        template_variant_types: set[str] = set()
        template_type_links: dict[str, set[str]] = {}

        for type_name, sources in type_sources.items():
            base = strip_template_args(type_name)

            # Only real template instantiations (whose <...> were actually
            # stripped out) with a meaningful base name are grouped.
            if base == type_name or not base:
                continue

            template_variant_types.add(type_name)
            template_sources.setdefault(base, set()).update(sources)
            template_type_links.setdefault(base, set()).add(type_name)

        if template_sources:
            logging.info("Template specialization sources merged:")
            for base, sources in sorted(template_sources.items()):
                inst_count = len(template_type_links.get(base, set()))
                logging.info(
                    "  %s += %d source(s) from %d specialization(s)",
                    base,
                    len(sources),
                    inst_count,
                )

        if template_variant_types:
            logging.info(
                "Template instantiation types removed from resolution: %d",
                len(template_variant_types),
            )

        if template_type_links:
            logging.info("Template type links (base -> instantiations):")
            for base, instantiations in sorted(template_type_links.items()):
                logging.info(
                    "  %s -> %s",
                    base,
                    ", ".join(sorted(instantiations)),
                )

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
                source for sources in type_sources.values() for source in sources
            }

        # ==============================================
        results: dict[str, dict[str, int]] = {}
        statuses: dict[str, str] = {}

        for type_name, sources in type_sources.items():
            # Skip meta-variant types — they are merged into their base type.
            if type_name in meta_variant_types:
                continue

            # Skip template instantiations — they are grouped into their
            # template base type and resolved only once.
            if type_name in template_variant_types:
                continue

            # Merge meta-variant sources into this type.
            merged_sources = set(sources)
            merged_sources.update(meta_sources.get(type_name, set()))

            # If this type is the base of any template specializations,
            # merge the union of their source files.
            merged_sources.update(template_sources.get(type_name, set()))

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

        # Template base types that do not themselves appear as standalone
        # symbols (e.g. "Array" when only "Array<Foo>" exists). Resolve each
        # one once from the union of all of its specializations' sources.
        for base_name in template_sources:
            if base_name in results:
                continue
            if base_name in meta_variant_types:
                # The template base is itself a meta variant; already merged.
                continue

            merged_sources = set(template_sources[base_name])
            merged_sources.update(type_sources.get(base_name, set()))
            merged_sources.update(meta_sources.get(base_name, set()))

            # If this template base is itself a meta-variant, fold the
            # base type's sources in as well.
            for variant in get_meta_variants(
                base_name,
                args.meta_prefix,
                args.meta_suffix,
            ):
                if variant in type_sources:
                    merged_sources.update(type_sources[variant])

            scores = get_type_source(
                base_name,
                merged_sources,
                source_type_counts,
                total_types,
                global_sources=all_sources,
            )

            if scores is None:
                continue

            best, best_score, status = resolve_best(scores)

            results[base_name] = scores
            statuses[base_name] = status

        output_path = Path("pdb_type_sources.txt")

        with output_path.open(
            "w",
            encoding="utf-8",
        ) as output:
            for type_name, result in results.items():
                output.write(f"{type_name} -> {result}\n")

            # Preserve meta-type relationships so the DB can link each
            # base type to the meta variants that were folded into it.
            if meta_type_links:
                output.write("\n# Meta type links (base -> meta variants)\n")
                for base, variants in sorted(meta_type_links.items()):
                    output.write(f"{base} -> {sorted(set(variants))}\n")

            # Preserve template-group relationships so the DB can link each
            # base type to the specializations folded into it.
            if template_type_links:
                output.write("\n# Template type links (base -> instantiations)\n")
                for base, instantiations in sorted(template_type_links.items()):
                    output.write(f"{base} -> {sorted(instantiations)}\n")

        logging.info("Results written to %s", output_path)

        # Print a summary of resolution statuses.
        status_counts = Counter(statuses.values())

        logging.info("Resolution summary:")
        for status, count in sorted(status_counts.items()):
            logging.info("  %-16s %5d", status, count)

        # Print low-confidence / ambiguous types.
        for type_name, status in sorted(statuses.items()):
            if status in ("ambiguous", "low_confidence"):
                scores = results[type_name]
                sorted_scores = sorted(
                    scores.items(),
                    key=lambda item: item[1],
                    reverse=True,
                )[:3]

                logging.info("%s [%s]:", type_name, status)
                for path, score in sorted_scores:
                    logging.info("  %6d  %s", score, path)

    except Exception as e:
        logging.error("Error: %s", pdb.last_error())
        logging.error("Exception: %s", e)
        return 1

    finally:
        pdb.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
