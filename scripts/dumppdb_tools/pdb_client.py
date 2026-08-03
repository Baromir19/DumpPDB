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

    def enumerate_symbols(self):
        def call(buffer, size, required):
            return self.api.dll.PdbApi_EnumerateSymbolNames(
                buffer,
                size,
                required
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