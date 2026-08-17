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

/// Find a type by name and enumerate the fully-qualified names of its
/// nested UDT/enum/typedef children, newline-separated.
/// E.g. for "Test::Actor", returns "Test::Actor::Weapon", "Test::Actor::SaveData",
/// "Test::Actor::NestedEnum", etc.
PDBAPI_API PdbApiResult PdbApi_EnumerateNestedTypeNames(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

// --- Symbol enumeration ---

/// Enumerate names of all UDT/enum/typedef symbols, newline-separated
/// (same separator convention as dumpCompilands).
/// a_topLevelOnly: 1 = only top-level types (global/namespace scope),
///                 0 = all types including nested ones.
/// Uses the standard Dump*/GetLastError buffer convention.
PDBAPI_API PdbApiResult PdbApi_EnumerateSymbolNames(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize, int32_t a_topLevelOnly
);

/// Get source file names for a type by name, newline-separated.
/// Uses the standard Dump*/GetLastError buffer convention.
PDBAPI_API PdbApiResult PdbApi_GetSymbolSourceFiles(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Enumerate names of all source files present in the PDB, newline-separated.
/// Uses the standard Dump*/GetLastError buffer convention.
PDBAPI_API PdbApiResult PdbApi_EnumerateSourceFiles(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);

/// Enumerate names of all UDT/enum/typedef symbols defined in a given
/// source file, newline-separated.
/// Uses the standard Dump*/GetLastError buffer convention.
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
/// For PE files, searches are scoped to string-candidate sections
/// (.text, .rdata, .data, etc.) rather than the whole file.
/// a_encodingFlags is a bitwise OR of PdbApiStringEncoding values.
/// a_sectionNames is a comma-separated list of PE section names to search
/// (e.g. ".rdata,.data"). Empty string means "all string-candidate sections".
/// a_regexPattern is an optional regex to filter results (empty = no filter).
/// Result is a newline-separated report: "0x<offset>: [ENC] <text>".
/// Uses the standard Dump*/GetLastError buffer convention.
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
/// Supports patterns like "FF ?? 01 BD ?? CA", "FF??01BD??CA", "0xFF??01BD??CA",
/// and "{ FF ?? 01 BD }" (YARA-style).
/// a_sectionNames is a comma-separated list of PE section names to search
/// (empty = all string-candidate sections for PE, whole file for non-PE).
/// Result is a newline-separated list of hex offsets.
/// Uses the standard Dump*/GetLastError buffer convention.
/// Returns PDBAPI_ERROR_NOT_FOUND if no matches, file load failed, or pattern invalid.
PDBAPI_API PdbApiResult PdbApi_FindSignaturesInFile(const wchar_t* a_filePath,
    const char* a_pattern,
    const char* a_sectionNames,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

// --- Diagnostics ---

/// Returns the last error message via the same buffer convention.
PDBAPI_API PdbApiResult PdbApi_GetLastError(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);
