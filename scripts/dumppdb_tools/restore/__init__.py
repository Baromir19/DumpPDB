"""Project restoration: symbol table + types + exe analysis -> source tree."""

from dumppdb_tools.restore.exe_analysis import (
    ExeAnalysisData,
    ExeClass,
    ExeFunction,
    PdbIdentity,
    load_names,
    parse_pdb_info,
)
from dumppdb_tools.restore.names import (
    check_pdb_matches_exe,
    collect_names,
    ghidra_script,
    ida_script,
    x64dbg_json,
)
from dumppdb_tools.restore.project import (
    FilePlan,
    ProjectBuilder,
    ProjectPlan,
    detect_root,
    render_file,
    write_project,
)
from dumppdb_tools.restore.symbols import (
    SymbolEntry,
    format_symbol_table,
    parse_symbol_table,
    split_qualified,
)

__all__ = [
    "ExeAnalysisData", "ExeClass", "ExeFunction", "PdbIdentity", "load_names", "parse_pdb_info",
    "check_pdb_matches_exe", "collect_names", "ghidra_script", "ida_script", "x64dbg_json",
    "FilePlan", "ProjectBuilder", "ProjectPlan", "detect_root", "render_file", "write_project",
    "SymbolEntry", "format_symbol_table", "parse_symbol_table", "split_qualified",
]
