"""Scoring logic for type-to-source-file matching.

All numeric constants are deliberately kept at their calibrated values;
see inline comments for the intent behind each one.
"""

import re
from collections import Counter
from pathlib import Path

from dumppdb_tools.recovery import remove_extension


# ---------------------------------------------------------------------------
# Score buckets — descending tiers for filename matches.
# ---------------------------------------------------------------------------

MAX_SCORE = 10_000

EXACT_FILENAME_SCORE = 10_000  # type name == file stem (case-insensitive)
TOKEN_FILENAME_SCORE = 7_000   # all long tokens of the last scope matched
PARTIAL_FILENAME_SCORE = 4_000  # partial weighted token match

EXACT_PATH_SCORE = 500  # directory component == scope name
PATH_TOKEN_SCORE = 100  # token found inside a directory component

EXTENSION_SCORE = 1_000  # bonus per unique extension bucket (max 3)

# ---------------------------------------------------------------------------
# Scope weights — last scope first; each outer scope gets progressively less.
# E.g. AI::NavMeshPrivate::AABSPTree → weights [0.4, 0.6, 1.0].
# ---------------------------------------------------------------------------

SCOPE_WEIGHTS = (1.0, 0.6, 0.4, 0.3, 0.25)

# Deeper namespaces carry negligible signal; stop after this many scopes.
SCOPE_DEPTH_LIMIT = 10

# Tokens shorter than this tend to match spuriously (e.g. "ai" in "main").
MIN_TOKEN_LENGTH = 4

# ---------------------------------------------------------------------------
# Penalty coefficients.
# ---------------------------------------------------------------------------

# Per-character penalty for stray characters in the file name that are
# not covered by the type's tokens.  Letters cost more (suggest an extra
# word); digits less (often a revision / copy suffix).
EXTRA_LETTER_PENALTY = 30
EXTRA_DIGIT_PENALTY = 8

# Frequency penalty: cap at this fraction of MAX_SCORE.
MAX_FREQUENCY_PENALTY = 0.5

# Small bonus for files referenced by fewer than 5 % of types.
RARE_FILE_BONUS = 0.02

# ---------------------------------------------------------------------------
# Confidence thresholds.
# ---------------------------------------------------------------------------

# Below this score the type's own sources are considered unreliable and a
# global search over all PDB sources is attempted.
MIN_CONFIDENCE = 5_000

# Penalty applied to scores coming from the global (all-sources) search.
GLOBAL_SEARCH_PENALTY = 0.4

# If the gap between best and second-best is smaller than this fraction of
# the best score, the result is marked "low_confidence".
LOW_CONFIDENCE_GAP = 0.1


# ---------------------------------------------------------------------------
# Template helpers
# ---------------------------------------------------------------------------

def strip_template_args(type_name: str) -> str:
    """Remove template argument contents while preserving the surrounding name.

    Examples::

        WeakRef<Actor>                     → WeakRef
        std::vector<int>                  → std::vector
        Foo<Bar>::Baz                     → Foo::Baz
        std::map<int, std::vector<float>> → std::map
    """
    result: list[str] = []
    depth = 0

    for char in type_name:
        if char == "<":
            depth += 1
        elif char == ">":
            if depth > 0:
                depth -= 1
        elif depth == 0:
            result.append(char)

    return "".join(result)


# ---------------------------------------------------------------------------
# Token helpers
# ---------------------------------------------------------------------------

def split_type_tokens(value: str) -> list[str]:
    """Split a camelCase / PascalCase / snake_case identifier into tokens.

    Returns lowercase tokens.
    """
    value = value.replace("_", " ")

    return [
        token.lower()
        for token in re.findall(
            r"[A-Z]+(?=[A-Z][a-z]|[0-9]|\b)|[A-Z]?[a-z]+|[0-9]+",
            value,
        )
    ]


def _get_scope_weights(num_scopes: int) -> list[float]:
    """Weights for type-name scopes ordered outermost → innermost.

    The last scope (type's own name) always gets weight 1.0.
    Earlier scopes get decreasing weights per :data:`SCOPE_WEIGHTS`.
    """
    if num_scopes <= 0:
        return []

    limit = min(num_scopes, SCOPE_DEPTH_LIMIT)

    weights = list(SCOPE_WEIGHTS)
    if len(weights) < limit:
        weights.extend([SCOPE_WEIGHTS[-1]] * (limit - len(weights)))
    elif len(weights) > limit:
        weights = weights[:limit]

    weights.reverse()
    return weights


# ---------------------------------------------------------------------------
# Individual score components
# ---------------------------------------------------------------------------

def get_filename_match_score(type_name: str, file_name: str) -> int:
    """Score how well *type_name* matches a file stem *file_name*."""
    type_lower = type_name.lower()
    file_lower = file_name.lower()

    if type_lower == file_lower:
        return MAX_SCORE

    scopes = [s for s in type_name.split("::") if s]
    if not scopes:
        return 0

    scope_weights = _get_scope_weights(len(scopes))

    last_tokens = {
        t for t in split_type_tokens(scopes[-1]) if len(t) >= MIN_TOKEN_LENGTH
    }
    if last_tokens and all(t in file_lower for t in last_tokens):
        return TOKEN_FILENAME_SCORE

    weighted = 0.0
    last_scope_partial = 0.0

    for index, (scope, weight) in enumerate(zip(scopes, scope_weights)):
        scope_tokens = {
            t for t in split_type_tokens(scope) if len(t) >= MIN_TOKEN_LENGTH
        }
        if not scope_tokens:
            continue

        matched = sum(1 for t in scope_tokens if t in file_lower)
        contribution = weight * (matched / len(scope_tokens))

        if index == len(scopes) - 1:
            last_scope_partial = contribution
        else:
            weighted += contribution

    if weighted <= 0 and last_scope_partial <= 0:
        return 0

    best = max(weighted, last_scope_partial)
    return min(int(best * PARTIAL_FILENAME_SCORE), PARTIAL_FILENAME_SCORE)


def get_path_match_score(type_name: str, path: str) -> int:
    """Score how well *type_name*'s namespace scopes appear in *path*."""
    scopes = [s for s in type_name.split("::") if s]
    if not scopes:
        return 0

    scope_weights = _get_scope_weights(len(scopes))
    parts = path.lower().split("/")
    score = 0

    for scope, weight in zip(scopes, scope_weights):
        scope_tokens = {
            t for t in split_type_tokens(scope) if len(t) >= MIN_TOKEN_LENGTH
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
    """Bonus for a set of files covering multiple distinct extensions (max 3 buckets)."""
    extensions = {Path(s).suffix.lower() for s in sources}
    return min(len(extensions), 3) * EXTENSION_SCORE


def get_extra_character_penalty(type_name: str, file_name: str) -> int:
    """Penalise stray characters in *file_name* not covered by *type_name*.

    Anchors on the last scope (after ``::``) so "AI::NavMeshPrivate::AABSPTree"
    only checks "aabsptree" against the file stem.
    Letters cost :data:`EXTRA_LETTER_PENALTY` each, digits :data:`EXTRA_DIGIT_PENALTY`.
    """
    if not type_name or not file_name:
        return 0

    scopes = type_name.lower().split("::")
    anchor = scopes[-1] if scopes else type_name.lower()

    remainder = file_name.lower().replace(anchor, "")
    if not remainder:
        return 0

    penalty = 0
    for char in remainder:
        if char.isalpha():
            penalty += EXTRA_LETTER_PENALTY
        elif char.isdigit():
            penalty += EXTRA_DIGIT_PENALTY

    return penalty


def get_frequency_penalty(
    source: str,
    source_type_counts: Counter[str],
    total_types: int,
) -> int:
    """Penalise source files referenced by many types.

    A file referenced by 90 % of types gets a large penalty; 1 % gets a
    small one.
    """
    if total_types == 0:
        return 0

    frequency = source_type_counts.get(source, 0) / total_types
    return int(frequency * MAX_SCORE * MAX_FREQUENCY_PENALTY)


def get_frequency_bonus(
    source: str,
    source_type_counts: Counter[str],
    total_types: int,
) -> int:
    """Give a small bonus to rare source files (referenced by < 5 % of types)."""
    if total_types == 0:
        return 0

    frequency = source_type_counts.get(source, 0) / total_types

    if frequency < 0.05:
        return int(MAX_SCORE * RARE_FILE_BONUS)

    return 0


# ---------------------------------------------------------------------------
# Composite scorer
# ---------------------------------------------------------------------------

def score_sources(
    type_name: str,
    file_sources: set[str],
    source_type_counts: Counter[str],
    total_types: int,
) -> dict[str, int] | None:
    """Score every candidate source file for *type_name*.

    Sources are grouped by their extension-stripped path so that a
    ``.h``/``.cpp`` pair is scored once.  Returns ``None`` if
    *file_sources* is empty.
    """
    scoring_name = strip_template_args(type_name)

    # Group by extension-stripped path.
    cleaned_map: dict[str, set[str]] = {}
    for source in file_sources:
        cleaned = remove_extension(source)
        cleaned_map.setdefault(cleaned, set()).add(source)

    if not cleaned_map:
        return None

    scores: dict[str, int] = {}

    for cleaned, sources in cleaned_map.items():
        filename = cleaned.rsplit("/")[-1]
        directory = cleaned.rsplit("/", 1)[0] if "/" in cleaned else ""

        s = get_filename_match_score(scoring_name, filename)
        s += get_path_match_score(scoring_name, directory)
        s += get_extension_score(sources)
        s -= get_extra_character_penalty(scoring_name, filename)
        s -= get_frequency_penalty(cleaned, source_type_counts, total_types)
        s += get_frequency_bonus(cleaned, source_type_counts, total_types)

        scores[cleaned] = s

    return scores
