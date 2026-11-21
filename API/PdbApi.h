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
    int32_t templateParams; ///< Emit template<...> for requested template instantiations.
    uint32_t baseAccessType;
    int32_t intStyle; ///< 0 = MsvcNative (__int32), 1 = Cstdint (int32_t).
};

/// Options for global functions / variables / statics. Kept as a SEPARATE struct
/// (see PdbApi_SetGlobalsConfig) so that the layout of PdbApiDumpConfig, and with
/// it every existing client, stays binary compatible.
struct PdbApiGlobalsConfig
{
    int32_t showAddress;          ///< "// RVA 0x... [sec:off] len ..." after functions/variables.
    int32_t showCallingConv;      ///< __cdecl/__stdcall/__fastcall (not printed for x64 PDBs).
    int32_t showFuncAttrs;        ///< __declspec(naked/noinline/noreturn), inline.
    int32_t showSourceLine;       ///< "@ file:line" for functions.
    int32_t showMemberAddress;    ///< Annotate class members with their RVA in class dumps.
    int32_t includeMembers;       ///< List member functions / static members in global listings.
    int32_t includeLocalStatics;  ///< List function-local statics too.
    int32_t sortByAddress;        ///< Sort listings by RVA instead of by name.
};

// --- Lifecycle ---

PDBAPI_API PdbApiResult PdbApi_Initialize(const wchar_t* a_pdbPath);
PDBAPI_API void PdbApi_Shutdown();
PDBAPI_API int32_t PdbApi_IsInitialized(); ///< Returns 0 or 1 (not bool, for ABI safety).

// --- Config ---

PDBAPI_API PdbApiResult PdbApi_SetConfig(const PdbApiDumpConfig* a_config);
PDBAPI_API PdbApiResult PdbApi_GetConfig(PdbApiDumpConfig* a_outConfig);

/// Options of the global-symbol listings (all other options are untouched).
PDBAPI_API PdbApiResult PdbApi_SetGlobalsConfig(const PdbApiGlobalsConfig* a_config);
PDBAPI_API PdbApiResult PdbApi_GetGlobalsConfig(PdbApiGlobalsConfig* a_outConfig);

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

// --- Global functions / variables / statics / public symbols ---
/// All of these use the buffer convention described above. a_filter is a substring
/// of the qualified name (nullptr or "" = everything). An empty result is NOT an
/// error: the call succeeds and the text is just the header comment.

/// Free functions grouped by namespace (member definitions with includeMembers).
PDBAPI_API PdbApiResult PdbApi_DumpGlobalFunctions(const wchar_t* a_filter,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Global variables, file statics and constants.
PDBAPI_API PdbApiResult PdbApi_DumpGlobalVariables(const wchar_t* a_filter,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Public symbols (decorated name + address, no type information).
PDBAPI_API PdbApiResult PdbApi_DumpPublicSymbols(const wchar_t* a_filter,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Tab-separated table of every function / variable / thunk / public symbol:
/// kind, name, undecorated, rva, section, offset, length, class, compiland,
/// source, line, flags, type. The first line is a header.
/// a_withSource = 1 also resolves the source file:line of each function (slower).
PDBAPI_API PdbApiResult PdbApi_DumpSymbolTable(const wchar_t* a_filter,
    int32_t a_caseSensitive,
    int32_t a_withSource,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Export kinds for PdbApi_ExportToFile.
enum PdbApiExportKind : int32_t
{
    PDBAPI_EXPORT_EVERYTHING = 0,
    PDBAPI_EXPORT_FUNCTIONS = 1,
    PDBAPI_EXPORT_VARIABLES = 2,
    PDBAPI_EXPORT_PUBLICS = 3,
    PDBAPI_EXPORT_SYMBOL_TABLE = 4,
    PDBAPI_EXPORT_SYMBOL_TABLE_SOURCE = 5,
};

/// Writes a dump directly to a UTF-8 file. Prefer this over the buffer functions
/// for large outputs (the buffer convention builds the text twice).
PDBAPI_API PdbApiResult PdbApi_ExportToFile(int32_t a_kind,
    const wchar_t* a_filter,
    int32_t a_caseSensitive,
    const wchar_t* a_outPath);

/// Identity of the loaded PDB as "guid=...\nage=N\nsignature=0x...\nplatform=0x...\n".
/// Compare guid+age with the CodeView data of an executable before transferring
/// addresses from the PDB to it.
PDBAPI_API PdbApiResult PdbApi_GetPdbInfo(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);

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

// ============================================================================
// EXE / RTTI API
// Reconstructs C++ types directly from an executable using MSVC RTTI data.
// Does NOT require a .pdb file.
// ============================================================================

/// Load (or reload) an executable for RTTI analysis.
/// Must be called before any ExeApi_* function.
/// Returns PDBAPI_OK on success, PDBAPI_ERROR_PDB_LOAD_FAILED if the file is
/// not a valid x64 MSVC PE image.
PDBAPI_API PdbApiResult ExeApi_LoadExe(const wchar_t* a_exePath);

/// Reconstruct a single type by name and return a C++ declaration.
/// Uses the buffer convention (see above).
/// Searches by exact name first, falls back to substring match.
/// a_paddingStyle: 0 = Array (default), 1 = Expanded (one field per element).
PDBAPI_API PdbApiResult ExeApi_ReconstructType(const wchar_t* a_typeName,
    int32_t a_paddingStyle,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Enumerate all types found via RTTI in the loaded executable.
/// Returns a newline-separated list of fully-qualified demangled type names.
/// Also includes a summary line at the top.
PDBAPI_API PdbApiResult ExeApi_EnumerateVftableTypes(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);

/// Returns the number of vftable entries found in the loaded executable.
PDBAPI_API uint32_t ExeApi_GetVftableCount();

/// Returns the number of unique type names found.
PDBAPI_API uint32_t ExeApi_GetTypeCount();

// ============================================================================
// Whole-image analysis (no disassembler, no PDB). Requires ExeApi_LoadExe.
// ============================================================================

/// PE/RTTI/PDB-identity summary (timestamp, PDB path+GUID+age, toolchain, sections,
/// imports, RTTI and analysis statistics).
PDBAPI_API PdbApiResult ExeApi_GetInfo(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize);

/// Hierarchy, vftables with named slots, constructors, static instances, source
/// files and referenced strings of the class(es) matching a_typeName.
PDBAPI_API PdbApiResult ExeApi_GetClassInfo(const wchar_t* a_typeName,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Describes an address (auto-detects RVA vs VA): containing function, vftable slot,
/// string, global, import slot and who references it. A value that is valid as both
/// is read as a VA and the text says so; use ExeApi_DescribeAddressEx to be explicit.
PDBAPI_API PdbApiResult ExeApi_DescribeAddress(uint64_t a_rvaOrVa,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Like ExeApi_DescribeAddress with an explicit interpretation:
/// a_kind 0 = auto, 1 = RVA, 2 = VA.
PDBAPI_API PdbApiResult ExeApi_DescribeAddressEx(uint64_t a_address,
    int32_t a_kind,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Writes all reports and disassembler scripts into a_outDir (see docs/user/exe-analysis.md).
/// The buffer receives a log of the written files.
PDBAPI_API PdbApiResult ExeApi_ExportAnalysis(const wchar_t* a_outDir,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);

/// Extracts every resource of the loaded EXE into a_outDir: resources.rc, resource.h,
/// a byte-exact resources.res, icons/*.ico, cursors/*.cur, bitmaps/*.bmp,
/// manifest/*.manifest, binary/*, resources.csv. The buffer receives a log.
PDBAPI_API PdbApiResult ExeApi_ExportResources(const wchar_t* a_outDir,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize);
