"""Low-level ctypes bindings for the native PdbAPI DLL.

This module only knows how to talk to the DLL. It does not contain any
high-level logic -- that lives in :mod:`dumppdb_tools.pdb_client`.
"""

import ctypes
from ctypes import wintypes


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
            ctypes.c_void_p
        ]
        self.dll.PdbApi_SetConfig.restype = ctypes.c_int

        self.dll.PdbApi_GetConfig.argtypes = [
            ctypes.c_void_p
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

        # --- Symbol enumeration ---

        self.dll.PdbApi_EnumerateSymbolNames.argtypes = [
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)
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

        # --- Diagnostics ---

        self.dll.PdbApi_GetLastError.argtypes = [
            wintypes.LPWSTR,
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32)
        ]
        self.dll.PdbApi_GetLastError.restype = ctypes.c_int


def read_string_call(call):
    """Run a two-phase buffer API call and return the resulting string.

    ``call`` is a callable with the signature::

        call(buffer, size, required) -> result_code

    The first call is made with ``(None, 0, byref(size))`` to query the
    required buffer size, then a second call fills the buffer.

    Raises:
        RuntimeError: if the API returns a non-zero result code.
    """
    size = ctypes.c_uint32()

    result = call(
        None,
        0,
        ctypes.byref(size)
    )

    if result != 0:
        raise RuntimeError(f"API error ({result})")

    buffer = ctypes.create_unicode_buffer(size.value)

    result = call(
        buffer,
        size.value,
        ctypes.byref(size)
    )

    if result != 0:
        raise RuntimeError("API error")

    return buffer.value