"""Type-source resolution: scoring, aggregation, and exclusion."""

from dumppdb_tools.resolution.exclude import is_type_excluded, load_exclude_patterns
from dumppdb_tools.resolution.resolver import (
    get_meta_variants,
    get_type_source,
    resolve_best,
    strip_template_args,
)
from dumppdb_tools.resolution.scorer import (
    EXACT_FILENAME_SCORE,
    EXACT_PATH_SCORE,
    EXTENSION_SCORE,
    EXTRA_DIGIT_PENALTY,
    EXTRA_LETTER_PENALTY,
    GLOBAL_SEARCH_PENALTY,
    LOW_CONFIDENCE_GAP,
    MAX_FREQUENCY_PENALTY,
    MAX_SCORE,
    MIN_CONFIDENCE,
    MIN_TOKEN_LENGTH,
    PARTIAL_FILENAME_SCORE,
    PATH_TOKEN_SCORE,
    RARE_FILE_BONUS,
    SCOPE_DEPTH_LIMIT,
    SCOPE_WEIGHTS,
    TOKEN_FILENAME_SCORE,
    get_extension_score,
    get_extra_character_penalty,
    get_filename_match_score,
    get_frequency_bonus,
    get_frequency_penalty,
    get_path_match_score,
    score_sources,
    split_type_tokens,
)
from dumppdb_tools.resolution.type_aggregator import TypeAggregator

__all__ = [
    # exclude
    "is_type_excluded",
    "load_exclude_patterns",
    # resolver
    "get_meta_variants",
    "get_type_source",
    "resolve_best",
    "strip_template_args",
    # scorer — constants
    "EXACT_FILENAME_SCORE",
    "EXACT_PATH_SCORE",
    "EXTENSION_SCORE",
    "EXTRA_DIGIT_PENALTY",
    "EXTRA_LETTER_PENALTY",
    "GLOBAL_SEARCH_PENALTY",
    "LOW_CONFIDENCE_GAP",
    "MAX_FREQUENCY_PENALTY",
    "MAX_SCORE",
    "MIN_CONFIDENCE",
    "MIN_TOKEN_LENGTH",
    "PARTIAL_FILENAME_SCORE",
    "PATH_TOKEN_SCORE",
    "RARE_FILE_BONUS",
    "SCOPE_DEPTH_LIMIT",
    "SCOPE_WEIGHTS",
    "TOKEN_FILENAME_SCORE",
    # scorer — functions
    "get_extension_score",
    "get_extra_character_penalty",
    "get_filename_match_score",
    "get_frequency_bonus",
    "get_frequency_penalty",
    "get_path_match_score",
    "score_sources",
    "split_type_tokens",
    # aggregator
    "TypeAggregator",
]
