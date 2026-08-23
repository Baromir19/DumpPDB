"""Path recovery utilities: normalization and case recovery."""

from dumppdb_tools.recovery.case_dict import CaseDict, score
from dumppdb_tools.recovery.path_normalizer import (
    CODE_EXTENSIONS,
    normalize_extension,
    normalize_path,
    normalize_paths,
    remove_extension,
    split_stem_ext,
    trim_to_normalized,
)
from dumppdb_tools.recovery.recover_case import (
    build_case_dictionary,
    recover_case,
)

__all__ = [
    "CaseDict",
    "score",
    "CODE_EXTENSIONS",
    "normalize_extension",
    "normalize_path",
    "normalize_paths",
    "remove_extension",
    "split_stem_ext",
    "trim_to_normalized",
    "build_case_dictionary",
    "recover_case",
]