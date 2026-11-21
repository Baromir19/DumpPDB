"""High-level client for the native PdbAPI DLL.

This is the convenient layer users should interact with. It wraps the
low-level ctypes bindings (:mod:`dumppdb_tools.api.pdb_api`) and hides
the two-phase buffer protocol.
"""

import ctypes

from dumppdb_tools.api.pdb_api import (
    EXPORT_EVERYTHING,
    EXPORT_FUNCTIONS,
    EXPORT_PUBLICS,
    EXPORT_SYMBOL_TABLE,
    EXPORT_SYMBOL_TABLE_SOURCE,
    EXPORT_VARIABLES,
    PdbApiConfig,
    PdbApiError,
    PdbApiGlobalsConfig,
    PdbApiNative,
    read_string_call,
)


class PdbClient:
    """Convenient wrapper around the native PdbAPI DLL."""

    def __init__(self, dll):
        self.api = PdbApiNative(dll)

    def open(self, pdb):
        result = self.api.dll.PdbApi_Initialize(pdb)

        if result != 0:
            raise RuntimeError(
                self.last_error()
            )

    def _read(self, call):
        """Two-phase buffer call; a failure carries the native last-error text."""
        try:
            return read_string_call(call)
        except PdbApiError as e:
            detail = ""
            try:
                detail = self.last_error()
            except Exception:  # last_error itself may fail when nothing was set
                pass
            raise PdbApiError(e.code, detail) from None

    def close(self):
        self.api.dll.PdbApi_Shutdown()

    def set_config(self, **options):
        """Update dump-config options. Only the passed keys are changed; the
        rest keep whatever the native side currently has.

        Keys match the fields of PdbApiDumpConfig (see api.pdb_api.PdbApiConfig),
        e.g. ``templateParams=True`` enables user-defined template parameterization.
        """
        cfg = PdbApiConfig()
        result = self.api.dll.PdbApi_GetConfig(ctypes.byref(cfg))
        if result != 0:
            raise RuntimeError(self.last_error())

        for key, value in options.items():
            if not hasattr(cfg, key):
                raise ValueError(f"Unknown config option: {key}")
            setattr(cfg, key, int(value))

        result = self.api.dll.PdbApi_SetConfig(ctypes.byref(cfg))
        if result != 0:
            raise RuntimeError(self.last_error())

    def is_initialized(self):
        return self.api.dll.PdbApi_IsInitialized() != 0

    def dump_type(self, name, case_sensitive=False):
        def call(buffer, size, required):
            return self.api.dll.PdbApi_DumpTypeByName(
                name,
                int(case_sensitive),
                buffer,
                size,
                required
            )

        return self._read(call)

    def enumerate_nested_types(self, name, case_sensitive=False):
        """Find a type by name and enumerate the fully-qualified names of its
        nested UDT/enum/typedef children, newline-separated.

        E.g. for "Test::Actor", returns "Test::Actor::Weapon",
        "Test::Actor::SaveData", "Test::Actor::NestedEnum", etc.
        """
        def call(buffer, size, required):
            return self.api.dll.PdbApi_EnumerateNestedTypeNames(
                name,
                int(case_sensitive),
                buffer,
                size,
                required
            )

        return self._read(call)

    def dump_class(self, name, case_sensitive=False):
        return self._dump(
            self.api.dll.PdbApi_DumpClassByName,
            name,
            case_sensitive
        )

    def dump_enum(self, name, case_sensitive=False):
        return self._dump(
            self.api.dll.PdbApi_DumpEnumByName,
            name,
            case_sensitive
        )

    def dump_typedef(self, name, case_sensitive=False):
        return self._dump(
            self.api.dll.PdbApi_DumpTypedefByName,
            name,
            case_sensitive
        )

    def enumerate_symbols(self, top_level_only=True):
        """Enumerate names of all UDT/enum/typedef symbols.

        Args:
            top_level_only: If True (default), only top-level types (global
                or namespace scope) are returned. If False, all types
                including nested ones are recursively enumerated.

        Returns:
            Newline-separated list of symbol names.
        """
        def call(buffer, size, required):
            return self.api.dll.PdbApi_EnumerateSymbolNames(
                buffer,
                size,
                required,
                int(top_level_only),
            )

        return self._read(call)

    def get_source_files(self, name, case_sensitive=False):
        def call(buffer, size, required):
            return self.api.dll.PdbApi_GetSymbolSourceFiles(
                name,
                int(case_sensitive),
                buffer,
                size,
                required
            )

        return self._read(call)

    def enumerate_source_files(self):
        def call(buffer, size, required):
            return self.api.dll.PdbApi_EnumerateSourceFiles(
                buffer,
                size,
                required
            )

        return self._read(call)

    """
    def get_symbols_by_source_file(self, file_name, case_sensitive=False):
        def call(buffer, size, required):
            return self.api.dll.PdbApi_GetSymbolsBySourceFile(
                file_name,
                int(case_sensitive),
                buffer,
                size,
                required
            )

        return self._read(call)
    """
        
    def find_strings(self, file_path, min_length=4, encodings=None,
                     section_names="", regex_pattern="", string_flags=None):
        """Search for strings in a binary file.

        Args:
            file_path: Path to the binary file (exe/dll/etc).
            min_length: Minimum string length (default 4).
            encodings: Bitwise OR of encoding flags:
                PDBAPI_ENC_ASCII=1, PDBAPI_ENC_UTF8=2, PDBAPI_ENC_UTF16LE=4.
                Defaults to ASCII | UTF16LE.
            section_names: Comma-separated list of PE section names to search
                (e.g. ".rdata,.data"). Empty string means "all string-candidate
                sections" for PE files, or the whole file for non-PE files.
            regex_pattern: Optional regex to filter results (empty = no filter).

        Returns:
            Newline-separated report: "0x<offset>: [ENC] <text>".
        """
        if encodings is None:
            encodings = 1 | 4  # ASCII | UTF16LE

        if string_flags is None:
            string_flags = 0 # None, Encodings + Offset

        def call(buffer, size, required):
            return self.api.dll.PdbApi_FindStringsInFile(
                file_path,
                min_length,
                encodings,
                string_flags,
                section_names.encode("utf-8"),
                regex_pattern.encode("utf-8"),
                buffer,
                size,
                required
            )

        return self._read(call)

    def find_signatures(self, file_path, pattern, section_names=""):
        """Search for a byte signature (with wildcards) in a binary file.

        Args:
            file_path: Path to the binary file.
            pattern: Signature pattern, e.g. "FF ?? 01 BD ?? CA",
                "FF??01BD??CA", "0xFF??01BD??CA", or "{ FF ?? 01 BD }".
            section_names: Comma-separated list of PE section names to search
                (empty = all string-candidate sections for PE, whole file for
                non-PE).

        Returns:
            Newline-separated list of hex offsets.
        """
        def call(buffer, size, required):
            return self.api.dll.PdbApi_FindSignaturesInFile(
                file_path,
                pattern.encode("ascii"),
                section_names.encode("ascii"),
                buffer,
                size,
                required
            )

        return self._read(call)

    def last_error(self):
        def call(buffer, size, required):
            return self.api.dll.PdbApi_GetLastError(
                buffer,
                size,
                required
            )

        return read_string_call(call)


    # ── Global functions / variables / statics / publics ────────────────────────

    def set_globals_config(self, **options):
        """Update the options of the global-symbol listings.

        Keys (see PdbApiGlobalsConfig): ``showAddress``, ``showCallingConv``,
        ``showFuncAttrs``, ``showSourceLine``, ``showMemberAddress`` (annotate
        class members with their RVA in class dumps), ``includeMembers`` (list
        member definitions / static members), ``includeLocalStatics``,
        ``sortByAddress``. Only the passed keys change.
        """
        cfg = PdbApiGlobalsConfig()
        if self.api.dll.PdbApi_GetGlobalsConfig(ctypes.byref(cfg)) != 0:
            raise RuntimeError(self.last_error())
        for key, value in options.items():
            if not hasattr(cfg, key):
                raise ValueError(f"Unknown globals option: {key}")
            setattr(cfg, key, int(value))
        if self.api.dll.PdbApi_SetGlobalsConfig(ctypes.byref(cfg)) != 0:
            raise RuntimeError(self.last_error())

    def dump_functions(self, name_filter="", case_sensitive=False):
        """Free functions grouped by namespace, with calling convention,
        attributes, RVA and ``@ file:line``. ``name_filter`` is a substring of
        the qualified name."""
        return self._list(self.api.dll.PdbApi_DumpGlobalFunctions, name_filter, case_sensitive)

    def dump_variables(self, name_filter="", case_sensitive=False):
        """Global variables, file statics and constants with their RVA and size."""
        return self._list(self.api.dll.PdbApi_DumpGlobalVariables, name_filter, case_sensitive)

    def dump_publics(self, name_filter="", case_sensitive=False):
        """Public symbols (decorated + undecorated name, RVA). Works on stripped PDBs."""
        return self._list(self.api.dll.PdbApi_DumpPublicSymbols, name_filter, case_sensitive)

    def dump_symbol_table(self, name_filter="", case_sensitive=False, with_source=True):
        """Tab-separated table of every function/variable/thunk/public symbol."""
        def call(buffer, size, required):
            return self.api.dll.PdbApi_DumpSymbolTable(
                name_filter or None, int(case_sensitive), int(with_source),
                buffer, size, required)
        return self._read(call)

    def symbol_table(self, name_filter="", case_sensitive=False, with_source=True):
        """:meth:`dump_symbol_table` parsed into ``SymbolEntry`` objects."""
        from dumppdb_tools.restore.symbols import parse_symbol_table
        return parse_symbol_table(
            self.dump_symbol_table(name_filter, case_sensitive, with_source))

    def pdb_info(self):
        """Identity of the loaded PDB: ``{"guid", "age", "signature", "platform"}``.

        Compare ``guid`` with the CodeView GUID of an executable before using
        RVAs from this PDB on it.
        """
        from dumppdb_tools.restore.exe_analysis import parse_pdb_info

        def call(buffer, size, required):
            return self.api.dll.PdbApi_GetPdbInfo(buffer, size, required)
        return parse_pdb_info(self._read(call))

    def export_to_file(self, kind, path, name_filter="", case_sensitive=False):
        """Write a (possibly huge) dump straight to a UTF-8 file.

        ``kind`` is one of the ``EXPORT_*`` constants of ``api.pdb_api``
        (``EXPORT_EVERYTHING``, ``EXPORT_FUNCTIONS``, ``EXPORT_VARIABLES``,
        ``EXPORT_PUBLICS``, ``EXPORT_SYMBOL_TABLE``, ``EXPORT_SYMBOL_TABLE_SOURCE``).
        """
        result = self.api.dll.PdbApi_ExportToFile(
            int(kind), name_filter or None, int(case_sensitive), str(path))
        if result != 0:
            raise PdbApiError(result, self.last_error())

    def _list(self, fn, name_filter, case_sensitive):
        def call(buffer, size, required):
            return fn(name_filter or None, int(case_sensitive), buffer, size, required)
        return self._read(call)

    # ── EXE / RTTI methods ────────────────────────────────────────────────────

    def load_exe(self, exe_path: str):
        """Load a PE executable for RTTI-based type reconstruction.

        Does NOT require a .pdb file. Detects x86/x64 automatically.
        Must be called before exe_reconstruct_type() and exe_enumerate_types().

        Raises:
            RuntimeError: if the file cannot be opened or is not a valid MSVC PE.
        """
        result = self.api.dll.ExeApi_LoadExe(exe_path)
        if result != 0:
            raise RuntimeError(
                f"ExeApi_LoadExe failed (code {result}): {self.last_error()}"
            )

    def exe_reconstruct_type(self, type_name: str, expanded: bool = False) -> str:
        """Reconstruct a C++ type declaration from RTTI data in the loaded EXE.

        Searches by exact demangled name first, falls back to substring match.

        Args:
            type_name: Demangled type name (e.g. "ActorInstance").
            expanded: If True, padding is rendered as individual fields instead
                      of arrays (one int32_t per 4 bytes).

        Returns:
            A C++ declaration string (class/struct with vftable slots + fields).

        Raises:
            RuntimeError: if the type is not found or the EXE is not loaded.
        """
        padding_flag = 1 if expanded else 0
        return self._read(
            lambda buf, sz, req: self.api.dll.ExeApi_ReconstructType(
                type_name, padding_flag, buf, sz, req
            )
        )

    def exe_enumerate_types(self) -> str:
        """Return a newline-separated list of all RTTI type names in the EXE.

        Includes a summary line at the top.

        Raises:
            RuntimeError: if the EXE is not loaded.
        """
        def call(buffer, size, required):
            return self.api.dll.ExeApi_EnumerateVftableTypes(buffer, size, required)
        return self._read(call)

    def exe_vftable_count(self) -> int:
        """Total number of vftable entries found in the loaded EXE."""
        return int(self.api.dll.ExeApi_GetVftableCount())

    def exe_type_count(self) -> int:
        """Number of unique demangled type names found in the loaded EXE."""
        return int(self.api.dll.ExeApi_GetTypeCount())


    # ── Whole-image analysis (no disassembler, no PDB) ──────────────────────────

    def exe_info(self) -> str:
        """PE/RTTI/PDB-identity summary of the loaded EXE (timestamp, PDB path,
        GUID and age, toolchain, sections, imports, analysis statistics)."""
        def call(buffer, size, required):
            return self.api.dll.ExeApi_GetInfo(buffer, size, required)
        return self._read(call)

    def exe_class_info(self, type_name: str) -> str:
        """Hierarchy, named vftable slots, constructors, static instances, source
        files and strings of the class(es) matching ``type_name``."""
        def call(buffer, size, required):
            return self.api.dll.ExeApi_GetClassInfo(type_name, buffer, size, required)
        return self._read(call)

    def exe_describe_address(self, address: int, kind: str = "auto") -> str:
        """What is at an address and who references it.

        ``kind`` is ``"auto"``, ``"rva"`` or ``"va"``. With ``"auto"`` a value that
        is valid as both (e.g. 0x915020 in a 0x400000-based, 29 MB image) is read as a
        VA and the returned text says so; pass ``"rva"`` to override.
        """
        kinds = {"auto": 0, "rva": 1, "va": 2}
        if kind not in kinds:
            raise ValueError(f"kind must be one of {sorted(kinds)}")

        def call(buffer, size, required):
            return self.api.dll.ExeApi_DescribeAddressEx(
                int(address), kinds[kind], buffer, size, required)
        return self._read(call)

    def exe_export_resources(self, out_dir) -> str:
        """Extract all resources of the loaded EXE into ``out_dir``: ``resources.rc``,
        ``resource.h``, a byte-exact ``resources.res``, ``icons/*.ico``, ``cursors/*.cur``,
        ``bitmaps/*.bmp``, ``manifest/*.manifest``, ``binary/*``. Returns the log."""
        def call(buffer, size, required):
            return self.api.dll.ExeApi_ExportResources(str(out_dir), buffer, size, required)
        return self._read(call)

    def exe_export_analysis(self, out_dir) -> str:
        """Write all reports and the IDA/Ghidra/x64dbg scripts into ``out_dir``.
        Returns the log of written files."""
        def call(buffer, size, required):
            return self.api.dll.ExeApi_ExportAnalysis(str(out_dir), buffer, size, required)
        return self._read(call)

    def _dump(self, fn, name, case_sensitive):
        def call(buffer, size, required):
            return fn(
                name,
                int(case_sensitive),
                buffer,
                size,
                required
            )

        return self._read(call)
