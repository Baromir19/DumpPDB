"""High-level client for the native PdbAPI DLL.

This is the convenient layer users should interact with. It wraps the
low-level ctypes bindings (:mod:`dumppdb_tools.api.pdb_api`) and hides
the two-phase buffer protocol.
"""

from dumppdb_tools.api.pdb_api import PdbApiNative, read_string_call


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

    def close(self):
        self.api.dll.PdbApi_Shutdown()

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

        return read_string_call(call)

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

        return read_string_call(call)

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

        return read_string_call(call)

    def get_source_files(self, name, case_sensitive=False):
        def call(buffer, size, required):
            return self.api.dll.PdbApi_GetSymbolSourceFiles(
                name,
                int(case_sensitive),
                buffer,
                size,
                required
            )

        return read_string_call(call)

    def enumerate_source_files(self):
        def call(buffer, size, required):
            return self.api.dll.PdbApi_EnumerateSourceFiles(
                buffer,
                size,
                required
            )

        return read_string_call(call)

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

        return read_string_call(call)
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

        return read_string_call(call)

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

        return read_string_call(call)

    def last_error(self):
        def call(buffer, size, required):
            return self.api.dll.PdbApi_GetLastError(
                buffer,
                size,
                required
            )

        return read_string_call(call)

    def _dump(self, fn, name, case_sensitive):
        def call(buffer, size, required):
            return fn(
                name,
                int(case_sensitive),
                buffer,
                size,
                required
            )

        return read_string_call(call)