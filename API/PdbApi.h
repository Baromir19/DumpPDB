#pragma once

#include <cstdint>

#ifdef PDBAPI_EXPORTS
#define PDBAPI_API extern "C" __declspec(dllexport)
#else
#define PDBAPI_API extern "C" __declspec(dllimport)
#endif

// --- Result codes ---

enum PdbApiResultCode : int32_t
{
    PDBAPI_OK = 0,
    PDBAPI_ERROR_NOT_INITIALIZED = -1,
    PDBAPI_ERROR_INVALID_ARG = -2,
    PDBAPI_ERROR_PDB_LOAD_FAILED = -3,
    PDBAPI_ERROR_NOT_FOUND = -4,
    PDBAPI_ERROR_BUFFER_TOO_SMALL = -5,
    PDBAPI_ERROR_EXCEPTION = -6,
};

typedef int32_t PdbApiResult;

// --- Config mirror of DumpConfig (POD only, safe across DLL boundary) ---

struct PdbApiDumpConfig
{
    int32_t showSize;
    int32_t showOffset;
    int32_t showAccess;
    int32_t showInfoComment;
    int32_t showNonScoped;
    int32_t showEnumHex;
    int32_t showTypeSource;
    int32_t curlyBraceNewline;
    int32_t hideCompilerGenerated;
    uint32_t baseAccessType;
    int32_t intStyle; // 0 = MsvcNative, 1 = Cstdint, see IntStyle enum in Core
};

// --- Lifecycle ---

PDBAPI_API PdbApiResult PdbApi_Initialize(const wchar_t* a_pdbPath);
PDBAPI_API void PdbApi_Shutdown();
PDBAPI_API int32_t PdbApi_IsInitialized(); // 0/1, not bool (ABI safety)

// --- Config ---

PDBAPI_API PdbApiResult PdbApi_SetConfig(const PdbApiDumpConfig* a_config);
PDBAPI_API PdbApiResult PdbApi_GetConfig(PdbApiDumpConfig* a_outConfig);

// --- Dump API ---
// Buffer convention (applies to ALL Dump*/GetLastError functions):
//   a_outBuffer == nullptr or a_bufferSize == 0
//       -> only computes size, writes it (in wchar_t count, incl. null terminator) to *a_outRequiredSize
//   a_outBuffer != nullptr
//       -> copies result if it fits; otherwise returns PDBAPI_ERROR_BUFFER_TOO_SMALL
//          and still writes the ACTUAL required size to *a_outRequiredSize so the caller
//          can reallocate and call again.
// a_outRequiredSize may be nullptr if the caller doesn't care.

PDBAPI_API PdbApiResult PdbApi_DumpClassByName(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

PDBAPI_API PdbApiResult PdbApi_DumpEnumByName(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

PDBAPI_API PdbApiResult PdbApi_DumpTypedefByName(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Exact name search across all tags, with namespace-prefix fallback,
/// grouped into namespace blocks. Mirrors PdbToolset::dumpTypeByName.
PDBAPI_API PdbApiResult PdbApi_DumpTypeByName(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

// --- Diagnostics ---

/// Returns the last error message via the same buffer convention.
PDBAPI_API PdbApiResult PdbApi_GetLastError(wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);