"""DumpPDB Python tools.

This package provides a Python client for the native PdbAPI DLL,
plus source-recovery, database, and filesystem utilities.
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
)
from dumppdb_tools.database import SourceDatabase
from dumppdb_tools.filesystem import (
    RenameStats,
    create_source_tree,
    rename_source_tree,
)

__all__ = [
    "PdbClient",
    "SourcePath",
    "SourceRecord",
    "NormalizedPath",
    "CaseDict",
    "build_case_dictionary",
    "normalize_paths",
    "normalize_path",
    "remove_extension",
    "recover_case",
    "SourceDatabase",
    "RenameStats",
    "create_source_tree",
    "rename_source_tree",
]
__version__ = "0.1.0"