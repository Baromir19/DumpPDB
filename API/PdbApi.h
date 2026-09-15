#pragma once

#include <cstdint>

#ifdef PDBAPI_EXPORTS
#define PDBAPI_API extern "C" __declspec(dllexport)
#else
#define PDBAPI_API extern "C" __declspec(dllimport)
#endif

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

/// POD mirror of DumpConfig, safe across the DLL boundary.
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
    int32_t templateParams;  ///< Emit template<...> for requested template instantiations.
    uint32_t baseAccessType;
    int32_t intStyle;        ///< 0 = MsvcNative (__int32), 1 = Cstdint (int32_t).
};

// --- Lifecycle ---

PDBAPI_API PdbApiResult PdbApi_Initialize(const wchar_t* a_pdbPath);
PDBAPI_API void PdbApi_Shutdown();
PDBAPI_API int32_t PdbApi_IsInitialized(); ///< Returns 0 or 1 (not bool, for ABI safety).

// --- Config ---

PDBAPI_API PdbApiResult PdbApi_SetConfig(const PdbApiDumpConfig* a_config);
PDBAPI_API PdbApiResult PdbApi_GetConfig(PdbApiDumpConfig* a_outConfig);

// --- Dump API ---
/// Buffer convention (applies to all Dump* / GetLastError functions):
///   a_outBuffer == nullptr or a_bufferSize == 0
///       -> size-query mode: writes required wchar_t count (incl. null) to *a_outRequiredSize.
///   a_outBuffer != nullptr
///       -> copies result if it fits; returns PDBAPI_ERROR_BUFFER_TOO_SMALL otherwise,
///          and still writes the required size to *a_outRequiredSize.
/// a_outRequiredSize may be nullptr.

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

/// Exact name search across all tags, with namespace-prefix fallback.
/// Multiple symbols in the same namespace are grouped into one block.
PDBAPI_API PdbApiResult PdbApi_DumpTypeByName(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Find a type by name and enumerate fully-qualified names of its nested
/// UDT/enum/typedef children, newline-separated.
PDBAPI_API PdbApiResult PdbApi_EnumerateNestedTypeNames(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

// --- Symbol enumeration ---

/// Enumerate names of all UDT/enum/typedef symbols, newline-separated.
/// a_topLevelOnly: 1 = global/namespace scope only, 0 = all including nested.
PDBAPI_API PdbApiResult PdbApi_EnumerateSymbolNames(wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize,
    int32_t a_topLevelOnly);

/// Get source file names for a type by name, newline-separated.
PDBAPI_API PdbApiResult PdbApi_GetSymbolSourceFiles(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Enumerate names of all source files present in the PDB, newline-separated.
PDBAPI_API PdbApiResult PdbApi_EnumerateSourceFiles(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);

/*PDBAPI_API PdbApiResult PdbApi_GetSymbolsBySourceFile(const wchar_t* a_fileName,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);*/

// --- Binary file search (strings / signatures) ---

/// String encoding flags for PdbApi_FindStringsInFile.
enum PdbApiStringEncoding : int32_t
{
    PDBAPI_ENC_ASCII = 1 << 0,
    PDBAPI_ENC_UTF8 = 1 << 1,
    PDBAPI_ENC_UTF16LE = 1 << 2,
};

/// Endianness for signature values.
enum PdbApiEndian : int32_t
{
    PDBAPI_ENDIAN_LITTLE = 0,
    PDBAPI_ENDIAN_BIG = 1,
};

/// Search for strings in a binary file (exe/dll/etc).
/// For PE files, searches are scoped to string-candidate sections rather than the whole file.
/// @param a_encodingFlags  Bitwise OR of PdbApiStringEncoding values.
/// @param a_sectionNames   Comma-separated PE section names (e.g. ".rdata,.data");
///                         empty = all string-candidate sections.
/// @param a_regexPattern   Optional regex filter; empty = no filter.
/// Result format: "0x<offset>: [ENC] <text>", newline-separated.
/// Returns PDBAPI_ERROR_NOT_FOUND if no strings found or file load failed.
PDBAPI_API PdbApiResult PdbApi_FindStringsInFile(const wchar_t* a_filePath,
    uint32_t a_minLength,
    int32_t a_encodingFlags,
    uint32_t a_outStringFlags,
    const char* a_sectionNames,
    const char* a_regexPattern,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Search for a byte signature (with wildcards) in a binary file.
/// Supported pattern formats: "FF ?? 01 BD", "FF??01BD", "0xFF??01BD", "{ FF ?? 01 BD }".
/// @param a_sectionNames  Comma-separated PE section names; empty = all sections.
/// Result: newline-separated hex offsets.
/// Returns PDBAPI_ERROR_NOT_FOUND if no matches, file load failed, or pattern invalid.
PDBAPI_API PdbApiResult PdbApi_FindSignaturesInFile(const wchar_t* a_filePath,
    const char* a_pattern,
    const char* a_sectionNames,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

// --- Diagnostics ---

/// Returns the last error message via the standard buffer convention.
PDBAPI_API PdbApiResult PdbApi_GetLastError(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);
