"""DumpPDB Python tools.

This package provides a Python client for the native PdbAPI DLL,
plus source-recovery, database, filesystem, and type-resolution utilities.
"""

from dumppdb_tools.pdb_client import PdbClient
from dumppdb_tools.models import SourcePath, SourceRecord, NormalizedPath
from dumppdb_tools.recovery import (
    CaseDict,
    build_case_dictionary,
    normalize_paths,
    normalize_path,
    remove_extension,
    recover_case,
    trim_to_normalized,
)
from dumppdb_tools.database import SourceDatabase
from dumppdb_tools.filesystem import (
    RenameStats,
    create_source_tree,
    rename_source_tree,
)
from dumppdb_tools.resolution import (
    TypeAggregator,
    is_type_excluded,
    load_exclude_patterns,
    get_meta_variants,
    get_type_source,
    resolve_best,
    strip_template_args,
    score_sources,
)

__all__ = [
    # core
    "PdbClient",
    "SourcePath",
    "SourceRecord",
    "NormalizedPath",
    # recovery
    "CaseDict",
    "build_case_dictionary",
    "normalize_paths",
    "normalize_path",
    "remove_extension",
    "recover_case",
    "trim_to_normalized",
    # database
    "SourceDatabase",
    # filesystem
    "RenameStats",
    "create_source_tree",
    "rename_source_tree",
    # resolution
    "TypeAggregator",
    "is_type_excluded",
    "load_exclude_patterns",
    "get_meta_variants",
    "get_type_source",
    "resolve_best",
    "strip_template_args",
    "score_sources",
]
__version__ = "0.1.0"
