"""Shared type-source helpers for the compare / reconstruct CLI tools.

This package centralises the logic that ``compare_type_sources`` and
``reconstruct_sources`` both need: loading the persisted :class:`TypeSource`
records from the SQLite database and probing a reconstructed source tree for
files that already exist, so a caller can decide what still needs work.
"""

from dumppdb_tools.sources.type_sources import TypeSource, load_type_sources
from dumppdb_tools.sources.project import (
    SOURCE_EXTENSIONS,
    collect_project_paths,
    missing_from_project,
    paths_match,
    walk_project_sources,
)
from dumppdb_tools.sources.browse import (
    BrowseDefinition,
    CodeItemAttributes,
    find_definitions_in_browse,
    load_kinds,
    locate_browse_definitions,
    lookup_file_name,
    open_browse_db,
)
from dumppdb_tools.sources.macros import (
    TYPE_NAME_RE,
    compile_pattern,
    load_patterns,
    macro_found_types,
    scan_source_tree,
)

__all__ = [
    "TypeSource",
    "load_type_sources",
    "SOURCE_EXTENSIONS",
    "collect_project_paths",
    "missing_from_project",
    "paths_match",
    "walk_project_sources",
    "BrowseDefinition",
    "CodeItemAttributes",
    "find_definitions_in_browse",
    "load_kinds",
    "locate_browse_definitions",
    "lookup_file_name",
    "open_browse_db",
    "TYPE_NAME_RE",
    "compile_pattern",
    "load_patterns",
    "macro_found_types",
    "scan_source_tree",
]