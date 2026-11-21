"""Low-level ctypes bindings for the native PdbAPI DLL.

This module only knows how to talk to the DLL. It does not contain any
high-level logic -- that lives in :mod:`dumppdb_tools.pdb_client`.
"""

import ctypes
from ctypes import wintypes


class PdbApiConfig(ctypes.Structure):
    """POD mirror of PdbApiDumpConfig (PdbApi.h). Field order matters."""
    _fields_ = [
        ("showSize", ctypes.c_int32),
        ("showOffset", ctypes.c_int32),
        ("showAccess", ctypes.c_int32),
        ("showInfoComment", ctypes.c_int32),
        ("showNonScoped", ctypes.c_int32),
        ("showEnumHex", ctypes.c_int32),
        ("showTypeSource", ctypes.c_int32),
        ("curlyBraceNewline", ctypes.c_int32),
        ("hideCompilerGenerated", ctypes.c_int32),
        ("templateParams", ctypes.c_int32),
        ("baseAccessType", ctypes.c_uint32),
        ("intStyle", ctypes.c_int32),
    ]


class PdbApiGlobalsConfig(ctypes.Structure):
    """POD mirror of PdbApiGlobalsConfig (PdbApi.h). Field order matters.

    Kept separate from PdbApiConfig so the layout of the older struct (and thus
    every existing client) is unchanged.
    """
    _fields_ = [
        ("showAddress", ctypes.c_int32),
        ("showCallingConv", ctypes.c_int32),
        ("showFuncAttrs", ctypes.c_int32),
        ("showSourceLine", ctypes.c_int32),
        ("showMemberAddress", ctypes.c_int32),
        ("includeMembers", ctypes.c_int32),
        ("includeLocalStatics", ctypes.c_int32),
        ("sortByAddress", ctypes.c_int32),
    ]


# PdbApiResult values (PdbApi.h)
PDBAPI_OK = 0
PDBAPI_ERROR_NOT_INITIALIZED = -1
PDBAPI_ERROR_INVALID_ARG = -2
PDBAPI_ERROR_PDB_LOAD_FAILED = -3
PDBAPI_ERROR_NOT_FOUND = -4
PDBAPI_ERROR_BUFFER_TOO_SMALL = -5
PDBAPI_ERROR_EXCEPTION = -6

_RESULT_NAMES = {
    PDBAPI_OK: "OK",
    PDBAPI_ERROR_NOT_INITIALIZED: "NOT_INITIALIZED",
    PDBAPI_ERROR_INVALID_ARG: "INVALID_ARG",
    PDBAPI_ERROR_PDB_LOAD_FAILED: "PDB_LOAD_FAILED",
    PDBAPI_ERROR_NOT_FOUND: "NOT_FOUND",
    PDBAPI_ERROR_BUFFER_TOO_SMALL: "BUFFER_TOO_SMALL",
    PDBAPI_ERROR_EXCEPTION: "EXCEPTION",
}

# PdbApiExportKind (PdbApi.h)
EXPORT_EVERYTHING = 0
EXPORT_FUNCTIONS = 1
EXPORT_VARIABLES = 2
EXPORT_PUBLICS = 3
EXPORT_SYMBOL_TABLE = 4
EXPORT_SYMBOL_TABLE_SOURCE = 5


class PdbApiError(RuntimeError):
    """A native call failed. ``code`` is the PdbApiResult, ``name`` its symbolic name."""

    def __init__(self, code, detail=""):
        self.code = code
        self.name = _RESULT_NAMES.get(code, "UNKNOWN")
        message = f"API error {code} ({self.name})"
        if detail:
            message += f": {detail}"
        super().__init__(message)


class PdbApiNative:
    """Thin ctypes wrapper around PdbAPI.dll.

    Every exported function is configured with the correct ``argtypes`` /
    ``restype`` so calls are type-checked by ctypes.
    """

    def __init__(self, dll_path):
        self.dll = ctypes.WinDLL(dll_path)

        # --- Lifecycle ---

        self.dll.PdbApi_Initialize.argtypes = [
            wintypes.LPCWSTR
        ]
        self.dll.PdbApi_Initialize.restype = ctypes.c_int

        self.dll.PdbApi_Shutdown.argtypes = []
        self.dll.PdbApi_Shutdown.restype = None

        self.dll.PdbApi_IsInitialized.argtypes = []
        self.dll.PdbApi_IsInitialized.restype = ctypes.c_int

        # --- Config ---

        self.dll.PdbApi_SetConfig.argtypes = [
            ctypes.POINTER(PdbApiConfig)
        ]
        self.dll.PdbApi_SetConfig.restype = ctypes.c_int

        self.dll.PdbApi_GetConfig.argtypes = [
            ctypes.POINTER(PdbApiConfig)
        ]
        self.dll.PdbApi_GetConfig.restype = ctypes.c_int

        # --- Dump API ---

        self.dll.PdbApi_DumpClassByName.argtypes = [
            wintypes.LPCWSTR,
            ctypes.c_int,
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)
        ]
        self.dll.PdbApi_DumpClassByName.restype = ctypes.c_int

        self.dll.PdbApi_DumpEnumByName.argtypes = [
            wintypes.LPCWSTR,
            ctypes.c_int,
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)
        ]
        self.dll.PdbApi_DumpEnumByName.restype = ctypes.c_int

        self.dll.PdbApi_DumpTypedefByName.argtypes = [
            wintypes.LPCWSTR,
            ctypes.c_int,
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)
        ]
        self.dll.PdbApi_DumpTypedefByName.restype = ctypes.c_int

        self.dll.PdbApi_DumpTypeByName.argtypes = [
            wintypes.LPCWSTR,
            ctypes.c_int,
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)
        ]
        self.dll.PdbApi_DumpTypeByName.restype = ctypes.c_int

        self.dll.PdbApi_EnumerateNestedTypeNames.argtypes = [
            wintypes.LPCWSTR,
            ctypes.c_int,
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)
        ]
        self.dll.PdbApi_EnumerateNestedTypeNames.restype = ctypes.c_int

        # --- Symbol enumeration ---

        self.dll.PdbApi_EnumerateSymbolNames.argtypes = [
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32),
            ctypes.c_int,
        ]
        self.dll.PdbApi_EnumerateSymbolNames.restype = ctypes.c_int

        self.dll.PdbApi_GetSymbolSourceFiles.argtypes = [
            wintypes.LPCWSTR,
            ctypes.c_int,
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)
        ]
        self.dll.PdbApi_GetSymbolSourceFiles.restype = ctypes.c_int

        self.dll.PdbApi_EnumerateSourceFiles.argtypes = [
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)
        ]
        self.dll.PdbApi_EnumerateSourceFiles.restype = ctypes.c_int

        """
        self.dll.PdbApi_GetSymbolsBySourceFile.argtypes = [
            wintypes.LPCWSTR,
            ctypes.c_int,
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)
        ]
        self.dll.PdbApi_GetSymbolsBySourceFile.restype = ctypes.c_int
        """

        # --- Binary file search (strings / signatures) ---

        self.dll.PdbApi_FindStringsInFile.argtypes = [
            wintypes.LPCWSTR,
            ctypes.c_uint32,
            ctypes.c_int,
            ctypes.c_uint32,
            ctypes.c_char_p,
            ctypes.c_char_p,
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)
        ]
        self.dll.PdbApi_FindStringsInFile.restype = ctypes.c_int

        self.dll.PdbApi_FindSignaturesInFile.argtypes = [
            wintypes.LPCWSTR,
            ctypes.c_char_p,
            ctypes.c_char_p,
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)
        ]
        self.dll.PdbApi_FindSignaturesInFile.restype = ctypes.c_int

        # --- Globals config ---

        self.dll.PdbApi_SetGlobalsConfig.argtypes = [ctypes.POINTER(PdbApiGlobalsConfig)]
        self.dll.PdbApi_SetGlobalsConfig.restype = ctypes.c_int
        self.dll.PdbApi_GetGlobalsConfig.argtypes = [ctypes.POINTER(PdbApiGlobalsConfig)]
        self.dll.PdbApi_GetGlobalsConfig.restype = ctypes.c_int

        # --- Global functions / variables / statics / publics ---
        # (filter, caseSensitive, buf, size, required)
        for name in ("PdbApi_DumpGlobalFunctions", "PdbApi_DumpGlobalVariables",
                     "PdbApi_DumpPublicSymbols"):
            fn = getattr(self.dll, name)
            fn.argtypes = [
                wintypes.LPCWSTR,
                ctypes.c_int32,
                wintypes.LPWSTR,
                ctypes.c_uint32,
                ctypes.POINTER(ctypes.c_uint32),
            ]
            fn.restype = ctypes.c_int

        # (filter, caseSensitive, withSource, buf, size, required)
        self.dll.PdbApi_DumpSymbolTable.argtypes = [
            wintypes.LPCWSTR,
            ctypes.c_int32,
            ctypes.c_int32,
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self.dll.PdbApi_DumpSymbolTable.restype = ctypes.c_int

        self.dll.PdbApi_GetPdbInfo.argtypes = [
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self.dll.PdbApi_GetPdbInfo.restype = ctypes.c_int

        # (kind, filter, caseSensitive, outPath)
        self.dll.PdbApi_ExportToFile.argtypes = [
            ctypes.c_int32,
            wintypes.LPCWSTR,
            ctypes.c_int32,
            wintypes.LPCWSTR,
        ]
        self.dll.PdbApi_ExportToFile.restype = ctypes.c_int

        # --- Diagnostics ---

        self.dll.PdbApi_GetLastError.argtypes = [
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)
        ]
        self.dll.PdbApi_GetLastError.restype = ctypes.c_int

        # ── EXE / RTTI API ────────────────────────────────────────────────────
        # Works on a plain .exe without any .pdb. Call ExeApi_LoadExe first.

        # ExeApi_LoadExe(exePath) -> int32
        self.dll.ExeApi_LoadExe.argtypes = [wintypes.LPCWSTR]
        self.dll.ExeApi_LoadExe.restype  = ctypes.c_int32

        # ExeApi_ReconstructType(typeName, paddingStyle, buf, bufSize, outRequired) -> int32
        # paddingStyle: 0 = Array, 1 = Expanded
        self.dll.ExeApi_ReconstructType.argtypes = [
            wintypes.LPCWSTR,
            ctypes.c_int32,
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self.dll.ExeApi_ReconstructType.restype = ctypes.c_int32

        # ExeApi_EnumerateVftableTypes(buf, bufSize, outRequired) -> int32
        self.dll.ExeApi_EnumerateVftableTypes.argtypes = [
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self.dll.ExeApi_EnumerateVftableTypes.restype = ctypes.c_int32

        # ExeApi_GetVftableCount() -> uint32
        self.dll.ExeApi_GetVftableCount.argtypes = []
        self.dll.ExeApi_GetVftableCount.restype  = ctypes.c_uint32

        # ExeApi_GetTypeCount() -> uint32
        self.dll.ExeApi_GetTypeCount.argtypes = []
        self.dll.ExeApi_GetTypeCount.restype  = ctypes.c_uint32

        # Whole-image analysis (no disassembler needed)
        self.dll.ExeApi_GetInfo.argtypes = [
            wintypes.LPWSTR, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
        self.dll.ExeApi_GetInfo.restype = ctypes.c_int32

        self.dll.ExeApi_GetClassInfo.argtypes = [
            wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
        self.dll.ExeApi_GetClassInfo.restype = ctypes.c_int32

        self.dll.ExeApi_DescribeAddress.argtypes = [
            ctypes.c_uint64, wintypes.LPWSTR, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
        self.dll.ExeApi_DescribeAddress.restype = ctypes.c_int32

        self.dll.ExeApi_DescribeAddressEx.argtypes = [
            ctypes.c_uint64, ctypes.c_int32, wintypes.LPWSTR, ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)]
        self.dll.ExeApi_DescribeAddressEx.restype = ctypes.c_int32

        self.dll.ExeApi_ExportResources.argtypes = [
            wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
        self.dll.ExeApi_ExportResources.restype = ctypes.c_int32

        self.dll.ExeApi_ExportAnalysis.argtypes = [
            wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
        self.dll.ExeApi_ExportAnalysis.restype = ctypes.c_int32


def read_string_call(call):
    """Run a two-phase buffer API call and return the resulting string.

    ``call`` is a callable with the signature::

        call(buffer, size, required) -> result_code

    The first call is made with ``(None, 0, byref(size))`` to query the
    required buffer size, then a second call fills the buffer.

    Raises:
        PdbApiError: (a RuntimeError) if the API returns a non-zero result code.
            The native last-error text is NOT fetched here; PdbClient adds it.
    """
    size = ctypes.c_uint32()

    result = call(None, 0, ctypes.byref(size))
    if result != 0:
        raise PdbApiError(result)

    buffer = ctypes.create_unicode_buffer(size.value)

    result = call(buffer, size.value, ctypes.byref(size))
    if result != 0:
        raise PdbApiError(result)

    return buffer.value
