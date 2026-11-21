"""``NativePdbClient``: the subset of ``PdbClient`` the restoration pipeline needs,
implemented on top of :class:`PdbFile` (pure Python, no DLL, any OS)."""

from __future__ import annotations

from dumppdb_tools.pdbfile import PdbFile
from dumppdb_tools.restore.symbols import SymbolEntry, format_symbol_table


class NativePdbClient:
    def __init__(self, int_style: str = "cstdint", progress=None):
        self._pdb: PdbFile | None = None
        self._int_style = int_style
        self._progress = progress

    # -- lifecycle -----------------------------------------------------------------------

    def open(self, pdb_path: str) -> None:
        self._pdb = PdbFile(pdb_path, self._int_style)

    def close(self) -> None:
        self._pdb = None

    @property
    def pdb(self) -> PdbFile:
        if self._pdb is None:
            raise RuntimeError("no PDB open")
        return self._pdb

    # -- what restore_project needs -------------------------------------------------------------

    def pdb_info(self) -> dict[str, str]:
        return self.pdb.identity()

    def set_globals_config(self, **_options) -> None:
        """Accepted for interface compatibility; the native reader always lists everything."""

    def symbol_table(self, name_filter: str = "", case_sensitive: bool = False,
                     with_source: bool = True) -> list[SymbolEntry]:
        syms = self.pdb.symbols(self._progress)
        if not name_filter:
            return list(syms)
        needle = name_filter if case_sensitive else name_filter.lower()
        return [s for s in syms
                if needle in (s.name if case_sensitive else s.name.lower())]

    def dump_symbol_table(self, name_filter: str = "", case_sensitive: bool = False,
                          with_source: bool = True) -> str:
        return format_symbol_table(self.symbol_table(name_filter, case_sensitive, with_source))

    def enumerate_source_files(self) -> str:
        self.pdb.symbols(self._progress)            # fills the file set from the line tables
        return "\n".join(self.pdb.source_files())

    def enumerate_symbols(self, top_level_only: bool = True) -> str:
        return "\n".join(self.pdb.type_names())

    def dump_type(self, name: str, case_sensitive: bool = False) -> str:
        return self.pdb.dump_type(name)

    def get_source_files(self, name: str, case_sensitive: bool = False) -> str:
        return "\n".join(self.pdb.type_sources(name))
