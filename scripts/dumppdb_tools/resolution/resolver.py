"""High-level resolution: meta variants and best-candidate selection.

:func:`strip_template_args` lives in :mod:`dumppdb_tools.resolution.scorer`
and is re-exported here for convenience.
"""

from collections import Counter

from dumppdb_tools.resolution.scorer import strip_template_args  # re-export


def get_meta_variants(
    type_name: str,
    meta_prefixes: list[str],
    meta_suffixes: list[str],
) -> list[str]:
    """Return base type names for a type that carries a meta prefix or suffix.

    E.g. with ``meta_suffixes=["Definition"]``,
    ``"VehicleDefinition"`` → ``["Vehicle"]``.
    """
    variants: list[str] = []

    for prefix in meta_prefixes:
        if type_name.startswith(prefix) and len(type_name) > len(prefix):
            variants.append(type_name[len(prefix):])

    for suffix in meta_suffixes:
        if type_name.endswith(suffix) and len(type_name) > len(suffix):
            variants.append(type_name[: -len(suffix)])

    return variants


def get_type_source(
    type_name: str,
    file_sources: set[str],
    source_type_counts: Counter[str],
    total_types: int,
    global_sources: set[str] | None = None,
) -> dict[str, int] | None:
    """Score candidate source files for *type_name*.

    First pass: score only the sources the PDB linked to this type.
    If the best score is below :data:`~dumppdb_tools.resolution.scorer.MIN_CONFIDENCE`,
    do a second pass over *global_sources* with a penalty applied.
    """
    from dumppdb_tools.resolution.scorer import (
        MIN_CONFIDENCE,
        GLOBAL_SEARCH_PENALTY,
        score_sources,
    )

    scores = score_sources(type_name, file_sources, source_type_counts, total_types)
    if scores is None:
        return None

    best_score = max(scores.values())

    if best_score < MIN_CONFIDENCE and global_sources:
        global_scores = score_sources(
            type_name, global_sources, source_type_counts, total_types
        )
        if global_scores:
            for path in global_scores:
                global_scores[path] = int(
                    global_scores[path] * (1 - GLOBAL_SEARCH_PENALTY)
                )
            scores = global_scores

    return scores


def resolve_best(
    scores: dict[str, int],
) -> tuple[list[str] | str | None, int, str]:
    """Select the best candidate from a score map.

    Returns ``(best, best_score, status)`` where *status* is one of:

    * ``"resolved"``       — one clear winner
    * ``"low_confidence"`` — winner exists but gap to second is small
    * ``"ambiguous"``      — multiple candidates tied for first
    * ``"no_match"``       — no candidate scored above zero
    """
    from dumppdb_tools.resolution.scorer import LOW_CONFIDENCE_GAP

    if not scores:
        return None, 0, "no_match"

    sorted_scores = sorted(scores.items(), key=lambda item: item[1], reverse=True)
    best_path, best_score = sorted_scores[0]

    if best_score == 0:
        return None, 0, "no_match"

    tied = [p for p, s in sorted_scores if s == best_score]
    if len(tied) > 1:
        return tied, best_score, "ambiguous"

    if len(sorted_scores) > 1:
        second_score = sorted_scores[1][1]
        if best_score - second_score < best_score * LOW_CONFIDENCE_GAP:
            return best_path, best_score, "low_confidence"

    return best_path, best_score, "resolved"
