#include <API/PdbApi.h>

#include <Core/PdbToolset.hpp>
#include <Core/RTTI/ExeToolset.hpp>
#include <Core/Util/Error/DumpError.hpp>

#include <functional>
#include <mutex>
#include <string>
#include <cwchar>

namespace
{
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::mutex g_mutex;
std::wstring g_lastError;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void setLastError(std::wstring a_msg)
{
    g_lastError = std::move(a_msg);
}

std::wstring exceptionToWString(const std::exception& a_ex)
{
    std::string what = a_ex.what();
    return {what.begin(), what.end()};
}

PdbApiResult copyToBuffer(const std::wstring& a_source,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    const auto needed = static_cast<uint32_t>(a_source.size() + 1);

    if (a_outRequiredSize != nullptr)
    {
        *a_outRequiredSize = needed;
    }

    if (a_outBuffer == nullptr || a_bufferSize == 0)
    {
        return PDBAPI_OK;
    }

    if (a_bufferSize < needed)
    {
        return PDBAPI_ERROR_BUFFER_TOO_SMALL;
    }

    wcscpy_s(a_outBuffer, a_bufferSize, a_source.c_str());
    return PDBAPI_OK;
}

PdbApiDumpConfig toApi(const DumpConfig& a_cfg)
{
    PdbApiDumpConfig out{};
    out.showSize = static_cast<int32_t>(a_cfg.m_showSize);
    out.showOffset = static_cast<int32_t>(a_cfg.m_showOffset);
    out.showAccess = static_cast<int32_t>(a_cfg.m_showAccess);
    out.showInfoComment = static_cast<int32_t>(a_cfg.m_showInfoComment);
    out.showNonScoped = static_cast<int32_t>(a_cfg.m_showNonScoped);
    out.showEnumHex = static_cast<int32_t>(a_cfg.m_showEnumHex);
    out.showTypeSource = static_cast<int32_t>(a_cfg.m_showTypeSource);
    out.curlyBraceNewline = static_cast<int32_t>(a_cfg.m_curlyBraceNewline);
    out.hideCompilerGenerated = static_cast<int32_t>(a_cfg.m_hideCompilerGenerated);
    out.templateParams = static_cast<int32_t>(a_cfg.m_templateParams);
    out.baseAccessType = static_cast<uint32_t>(a_cfg.m_baseAccessType);
    out.intStyle = static_cast<int32_t>(a_cfg.m_intStyle);
    return out;
}

bool fromApi(const PdbApiDumpConfig& a_in, DumpConfig& a_out)
{
    if (!isValidIntStyle(a_in.intStyle))
    {
        return false;
    }

    a_out.m_showSize = a_in.showSize != 0;
    a_out.m_showOffset = a_in.showOffset != 0;
    a_out.m_showAccess = a_in.showAccess != 0;
    a_out.m_showInfoComment = a_in.showInfoComment != 0;
    a_out.m_showNonScoped = a_in.showNonScoped != 0;
    a_out.m_showEnumHex = a_in.showEnumHex != 0;
    a_out.m_showTypeSource = a_in.showTypeSource != 0;
    a_out.m_curlyBraceNewline = a_in.curlyBraceNewline != 0;
    a_out.m_hideCompilerGenerated = a_in.hideCompilerGenerated != 0;
    a_out.m_templateParams = a_in.templateParams != 0;
    a_out.m_baseAccessType = a_in.baseAccessType;
    a_out.m_intStyle = static_cast<IntStyle>(a_in.intStyle);
    return true;
}

/// Runs a_fn under the global mutex, catches all exceptions into g_lastError.
template <class Fn>
PdbApiResult guarded(Fn&& a_fn)
{
    std::scoped_lock lock(g_mutex);
    try
    {
        return std::forward<Fn>(a_fn)();
    }
    catch (const DumpError& dumpEx)
    {
        setLastError(dumpEx.wideMessage());
        return PDBAPI_ERROR_EXCEPTION;
    }
    catch (const std::exception& ex)
    {
        setLastError(exceptionToWString(ex));
        return PDBAPI_ERROR_EXCEPTION;
    }
    catch (...)
    {
        setLastError(L"Unknown exception");
        return PDBAPI_ERROR_EXCEPTION;
    }
}

/// Shared implementation for "find by name -> dump -> copy" entry points.
PdbApiResult dumpByNameImpl(std::wstring (PdbToolset::*a_method)(const wchar_t*, bool),
    const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    if (a_name == nullptr)
    {
        setLastError(L"a_name is null");
        return PDBAPI_ERROR_INVALID_ARG;
    }
    if (PdbToolset::instance().session() == nullptr)
    {
        return PDBAPI_ERROR_NOT_INITIALIZED;
    }

    std::wstring result = (PdbToolset::instance().*a_method)(a_name, a_caseSensitive != 0);

    if (result.empty())
    {
        setLastError(L"Symbol not found");
        if (a_outRequiredSize != nullptr)
        {
            *a_outRequiredSize = 0;
        }
        return PDBAPI_ERROR_NOT_FOUND;
    }

    return copyToBuffer(result, a_outBuffer, a_bufferSize, a_outRequiredSize);
}

PdbApiGlobalsConfig globalsToApi(const DumpConfig& a_cfg)
{
    PdbApiGlobalsConfig out{};
    out.showAddress = static_cast<int32_t>(a_cfg.m_showAddress);
    out.showCallingConv = static_cast<int32_t>(a_cfg.m_showCallingConv);
    out.showFuncAttrs = static_cast<int32_t>(a_cfg.m_showFuncAttrs);
    out.showSourceLine = static_cast<int32_t>(a_cfg.m_showSourceLine);
    out.showMemberAddress = static_cast<int32_t>(a_cfg.m_showMemberAddress);
    out.includeMembers = static_cast<int32_t>(a_cfg.m_globalsIncludeMembers);
    out.includeLocalStatics = static_cast<int32_t>(a_cfg.m_globalsIncludeLocalStatics);
    out.sortByAddress = static_cast<int32_t>(a_cfg.m_globalsSortByAddress);
    return out;
}

void globalsFromApi(const PdbApiGlobalsConfig& a_in, DumpConfig& a_out)
{
    a_out.m_showAddress = a_in.showAddress != 0;
    a_out.m_showCallingConv = a_in.showCallingConv != 0;
    a_out.m_showFuncAttrs = a_in.showFuncAttrs != 0;
    a_out.m_showSourceLine = a_in.showSourceLine != 0;
    a_out.m_showMemberAddress = a_in.showMemberAddress != 0;
    a_out.m_globalsIncludeMembers = a_in.includeMembers != 0;
    a_out.m_globalsIncludeLocalStatics = a_in.includeLocalStatics != 0;
    a_out.m_globalsSortByAddress = a_in.sortByAddress != 0;
}

/// Shared implementation for the listing entry points (functions / variables /
/// publics). An empty listing is a valid answer, not "not found".
PdbApiResult listImpl(std::wstring (PdbToolset::*a_method)(const wchar_t*, bool),
    const wchar_t* a_filter,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    if (PdbToolset::instance().session() == nullptr)
    {
        setLastError(L"DIA session not initialized");
        return PDBAPI_ERROR_NOT_INITIALIZED;
    }

    const std::wstring result = (PdbToolset::instance().*a_method)(a_filter, a_caseSensitive != 0);
    return copyToBuffer(result, a_outBuffer, a_bufferSize, a_outRequiredSize);
}
} // namespace

// --- Lifecycle ---

PdbApiResult PdbApi_Initialize(const wchar_t* a_pdbPath)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (!a_pdbPath)
            {
                setLastError(L"a_pdbPath is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }

            if (!PdbToolset::instance().initialize(a_pdbPath))
            {
                setLastError(L"Failed to initialize DIA session for the given PDB");
                return PDBAPI_ERROR_PDB_LOAD_FAILED;
            }
            return PDBAPI_OK;
        });
}

void PdbApi_Shutdown()
{
    std::scoped_lock lock(g_mutex);
    g_lastError.clear();
}

int32_t PdbApi_IsInitialized()
{
    std::scoped_lock lock(g_mutex);
    return PdbToolset::instance().session() != nullptr ? 1 : 0;
}

// --- Config ---

PdbApiResult PdbApi_SetConfig(const PdbApiDumpConfig* a_config)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (!a_config)
            {
                setLastError(L"a_config is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }

            // Start from the current configuration: fromApi() only knows the legacy
            // fields, and the extended (globals) options must not be reset here.
            DumpConfig cfg = PdbToolset::instance().dumper().config();
            if (!fromApi(*a_config, cfg))
            {
                setLastError(L"Invalid intStyle value");
                return PDBAPI_ERROR_INVALID_ARG;
            }

            PdbToolset::instance().dumper().setConfig(cfg);
            return PDBAPI_OK;
        });
}

PdbApiResult PdbApi_GetConfig(PdbApiDumpConfig* a_outConfig)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (!a_outConfig)
            {
                setLastError(L"a_outConfig is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }
            *a_outConfig = toApi(PdbToolset::instance().dumper().config());
            return PDBAPI_OK;
        });
}

PdbApiResult PdbApi_SetGlobalsConfig(const PdbApiGlobalsConfig* a_config)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (!a_config)
            {
                setLastError(L"a_config is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }

            DumpConfig cfg = PdbToolset::instance().dumper().config();
            globalsFromApi(*a_config, cfg);
            PdbToolset::instance().dumper().setConfig(cfg);
            return PDBAPI_OK;
        });
}

PdbApiResult PdbApi_GetGlobalsConfig(PdbApiGlobalsConfig* a_outConfig)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (!a_outConfig)
            {
                setLastError(L"a_outConfig is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }
            *a_outConfig = globalsToApi(PdbToolset::instance().dumper().config());
            return PDBAPI_OK;
        });
}

// --- Global functions / variables / statics / public symbols ---

PdbApiResult PdbApi_DumpGlobalFunctions(const wchar_t* a_filter,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]()
        {
            return listImpl(&PdbToolset::dumpGlobalFunctions,
                a_filter,
                a_caseSensitive,
                a_outBuffer,
                a_bufferSize,
                a_outRequiredSize);
        });
}

PdbApiResult PdbApi_DumpGlobalVariables(const wchar_t* a_filter,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]()
        {
            return listImpl(&PdbToolset::dumpGlobalVariables,
                a_filter,
                a_caseSensitive,
                a_outBuffer,
                a_bufferSize,
                a_outRequiredSize);
        });
}

PdbApiResult PdbApi_DumpPublicSymbols(const wchar_t* a_filter,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]()
        {
            return listImpl(&PdbToolset::dumpPublicSymbols,
                a_filter,
                a_caseSensitive,
                a_outBuffer,
                a_bufferSize,
                a_outRequiredSize);
        });
}

PdbApiResult PdbApi_DumpSymbolTable(const wchar_t* a_filter,
    int32_t a_caseSensitive,
    int32_t a_withSource,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (PdbToolset::instance().session() == nullptr)
            {
                setLastError(L"DIA session not initialized");
                return PDBAPI_ERROR_NOT_INITIALIZED;
            }

            const std::wstring result = PdbToolset::instance().dumpSymbolTable(
                a_filter, a_caseSensitive != 0, a_withSource != 0);
            return copyToBuffer(result, a_outBuffer, a_bufferSize, a_outRequiredSize);
        });
}

PdbApiResult PdbApi_GetPdbInfo(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (PdbToolset::instance().session() == nullptr)
            {
                setLastError(L"DIA session not initialized");
                return PDBAPI_ERROR_NOT_INITIALIZED;
            }
            return copyToBuffer(
                PdbToolset::instance().pdbInfo(), a_outBuffer, a_bufferSize, a_outRequiredSize);
        });
}

PdbApiResult PdbApi_ExportToFile(
    int32_t a_kind, const wchar_t* a_filter, int32_t a_caseSensitive, const wchar_t* a_outPath)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (!a_outPath)
            {
                setLastError(L"a_outPath is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }
            if (PdbToolset::instance().session() == nullptr)
            {
                setLastError(L"DIA session not initialized");
                return PDBAPI_ERROR_NOT_INITIALIZED;
            }

            std::wstring error;
            if (!PdbToolset::instance().exportToFile(
                    a_kind, a_filter, a_caseSensitive != 0, a_outPath, error))
            {
                setLastError(error);
                return error == L"unknown export kind" ? PDBAPI_ERROR_INVALID_ARG
                                                       : PDBAPI_ERROR_EXCEPTION;
            }
            return PDBAPI_OK;
        });
}

// --- Dump API ---

PdbApiResult PdbApi_DumpClassByName(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]()
        {
            return dumpByNameImpl(&PdbToolset::dumpClassByName,
                a_name,
                a_caseSensitive,
                a_outBuffer,
                a_bufferSize,
                a_outRequiredSize);
        });
}

PdbApiResult PdbApi_DumpEnumByName(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]()
        {
            return dumpByNameImpl(&PdbToolset::dumpEnumByName,
                a_name,
                a_caseSensitive,
                a_outBuffer,
                a_bufferSize,
                a_outRequiredSize);
        });
}

PdbApiResult PdbApi_DumpTypedefByName(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]()
        {
            return dumpByNameImpl(&PdbToolset::dumpTypedefByName,
                a_name,
                a_caseSensitive,
                a_outBuffer,
                a_bufferSize,
                a_outRequiredSize);
        });
}

PdbApiResult PdbApi_DumpTypeByName(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]()
        {
            return dumpByNameImpl(&PdbToolset::dumpTypeByName,
                a_name,
                a_caseSensitive,
                a_outBuffer,
                a_bufferSize,
                a_outRequiredSize);
        });
}

PdbApiResult PdbApi_EnumerateNestedTypeNames(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (a_name == nullptr)
            {
                setLastError(L"a_name is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }
            if (PdbToolset::instance().session() == nullptr)
            {
                setLastError(L"DIA session not initialized");
                return PDBAPI_ERROR_NOT_INITIALIZED;
            }

            std::wstring result
                = PdbToolset::instance().enumerateNestedTypeNames(a_name, a_caseSensitive != 0);

            if (result.empty())
            {
                setLastError(L"Symbol not found or has no nested types");
                if (a_outRequiredSize != nullptr)
                {
                    *a_outRequiredSize = 0;
                }
                return PDBAPI_ERROR_NOT_FOUND;
            }

            return copyToBuffer(result, a_outBuffer, a_bufferSize, a_outRequiredSize);
        });
}

// --- Symbol enumeration ---

PdbApiResult PdbApi_EnumerateSymbolNames(wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize,
    int32_t a_topLevelOnly)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (PdbToolset::instance().session() == nullptr)
            {
                return PDBAPI_ERROR_NOT_INITIALIZED;
            }

            std::wstring result = PdbToolset::instance().enumerateSymbolNames(a_topLevelOnly != 0);

            return copyToBuffer(result, a_outBuffer, a_bufferSize, a_outRequiredSize);
        });
}

PdbApiResult PdbApi_GetSymbolSourceFiles(const wchar_t* a_name,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (a_name == nullptr)
            {
                setLastError(L"a_name is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }
            if (PdbToolset::instance().session() == nullptr)
            {
                setLastError(L"DIA session not initialized");
                return PDBAPI_ERROR_NOT_INITIALIZED;
            }

            std::wstring result;

            try
            {
                result
                    = PdbToolset::instance().getTypeSourceFilesByName(a_name, a_caseSensitive != 0);
            }
            catch (const DumpError& e)
            {
                setLastError(L"DumpError: " + e.wideMessage());
                return PDBAPI_ERROR_EXCEPTION;
            }

            catch (const std::exception& e)
            {
                setLastError(L"std::exception: " + exceptionToWString(e));
                return PDBAPI_ERROR_EXCEPTION;
            }

            catch (...)
            {
                setLastError(L"Unknown C++ exception");
                return PDBAPI_ERROR_EXCEPTION;
            }

            if (result.empty())
            {
                setLastError(L"Symbol not found or has no source files");
                if (a_outRequiredSize != nullptr)
                {
                    *a_outRequiredSize = 0;
                }
                return PDBAPI_ERROR_NOT_FOUND;
            }

            return copyToBuffer(result, a_outBuffer, a_bufferSize, a_outRequiredSize);
        });
}

PdbApiResult PdbApi_EnumerateSourceFiles(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (PdbToolset::instance().session() == nullptr)
            {
                setLastError(L"DIA session not initialized");
                return PDBAPI_ERROR_NOT_INITIALIZED;
            }

            std::wstring result = PdbToolset::instance().dumpSourceFiles();

            if (result.empty())
            {
                setLastError(L"No source files found");
                if (a_outRequiredSize != nullptr)
                {
                    *a_outRequiredSize = 0;
                }
                return PDBAPI_ERROR_NOT_FOUND;
            }

            return copyToBuffer(result, a_outBuffer, a_bufferSize, a_outRequiredSize);
        });
}

/*PdbApiResult PdbApi_GetSymbolsBySourceFile(const wchar_t* a_fileName,
    int32_t a_caseSensitive,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (a_fileName == nullptr)
            {
                setLastError(L"a_fileName is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }
            if (PdbToolset::instance().session() == nullptr)
            {
                setLastError(L"DIA session not initialized");
                return PDBAPI_ERROR_NOT_INITIALIZED;
            }

            std::wstring result
                = PdbToolset::instance().getSymbolsBySourceFile(a_fileName, a_caseSensitive != 0);

            if (result.empty())
            {
                setLastError(L"No symbols found for the given source file");
                if (a_outRequiredSize != nullptr)
                {
                    *a_outRequiredSize = 0;
                }
                return PDBAPI_ERROR_NOT_FOUND;
            }

            return copyToBuffer(result, a_outBuffer, a_bufferSize, a_outRequiredSize);
        });
}*/

// --- Binary file search (strings / signatures) ---

PdbApiResult PdbApi_FindStringsInFile(const wchar_t* a_filePath,
    uint32_t a_minLength,
    int32_t a_encodingFlags,
    uint32_t a_outStringFlags,
    const char* a_sectionNames,
    const char* a_regexPattern,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (a_filePath == nullptr)
            {
                setLastError(L"a_filePath is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }
            if (a_encodingFlags == 0)
            {
                setLastError(L"a_encodingFlags is 0 (no encodings selected)");
                return PDBAPI_ERROR_INVALID_ARG;
            }

            std::wstring result = PdbToolset::instance().findStringsInFile(a_filePath,
                a_minLength,
                a_encodingFlags,
                a_outStringFlags,
                a_sectionNames,
                a_regexPattern);

            if (result.empty())
            {
                setLastError(L"No strings found or file could not be loaded");
                if (a_outRequiredSize != nullptr)
                {
                    *a_outRequiredSize = 0;
                }
                return PDBAPI_ERROR_NOT_FOUND;
            }

            return copyToBuffer(result, a_outBuffer, a_bufferSize, a_outRequiredSize);
        });
}

PdbApiResult PdbApi_FindSignaturesInFile(const wchar_t* a_filePath,
    const char* a_pattern,
    const char* a_sectionNames,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (a_filePath == nullptr)
            {
                setLastError(L"a_filePath is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }
            if (a_pattern == nullptr)
            {
                setLastError(L"a_pattern is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }

            std::wstring result = PdbToolset::instance().findSignaturesInFile(
                a_filePath, a_pattern, a_sectionNames);

            if (result.empty())
            {
                setLastError(
                    L"No signature matches found, file could not be loaded, or pattern is invalid");
                if (a_outRequiredSize != nullptr)
                {
                    *a_outRequiredSize = 0;
                }
                return PDBAPI_ERROR_NOT_FOUND;
            }

            return copyToBuffer(result, a_outBuffer, a_bufferSize, a_outRequiredSize);
        });
}

// --- Diagnostics ---

PdbApiResult PdbApi_GetLastError(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize)
{
    std::scoped_lock lock(g_mutex);
    return copyToBuffer(g_lastError, a_outBuffer, a_bufferSize, a_outRequiredSize);
}

// ============================================================================
// EXE / RTTI API implementation
// ============================================================================

PdbApiResult ExeApi_LoadExe(const wchar_t* a_exePath)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (!a_exePath)
            {
                setLastError(L"a_exePath is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }

            if (!DumpPDB::ExeToolset::instance().load(a_exePath))
            {
                setLastError(L"Failed to load EXE: not a valid x64 MSVC PE image");
                return PDBAPI_ERROR_PDB_LOAD_FAILED;
            }

            return PDBAPI_OK;
        });
}

PdbApiResult ExeApi_ReconstructType(const wchar_t* a_typeName,
    int32_t a_paddingStyle,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (!a_typeName)
            {
                setLastError(L"a_typeName is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }

            auto& toolset = DumpPDB::ExeToolset::instance();
            if (!toolset.isLoaded())
            {
                setLastError(L"ExeApi not initialized. Call ExeApi_LoadExe first.");
                return PDBAPI_ERROR_NOT_INITIALIZED;
            }

            const DumpPDB::PaddingStyle style = (a_paddingStyle == 1)
                ? DumpPDB::PaddingStyle::Expanded
                : DumpPDB::PaddingStyle::Array;

            std::wstring result = toolset.reconstructType(a_typeName, style);

            if (result.empty())
            {
                setLastError(L"Type not found");
                if (a_outRequiredSize) *a_outRequiredSize = 0;
                return PDBAPI_ERROR_NOT_FOUND;
            }

            return copyToBuffer(result, a_outBuffer, a_bufferSize, a_outRequiredSize);
        });
}

PdbApiResult ExeApi_EnumerateVftableTypes(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            auto& toolset = DumpPDB::ExeToolset::instance();
            if (!toolset.isLoaded())
            {
                setLastError(L"ExeApi not initialized. Call ExeApi_LoadExe first.");
                return PDBAPI_ERROR_NOT_INITIALIZED;
            }

            std::wstring result = toolset.enumerateVftableTypes();

            if (result.empty())
            {
                setLastError(L"No RTTI types found in the loaded executable");
                if (a_outRequiredSize) *a_outRequiredSize = 0;
                return PDBAPI_ERROR_NOT_FOUND;
            }

            return copyToBuffer(result, a_outBuffer, a_bufferSize, a_outRequiredSize);
        });
}

uint32_t ExeApi_GetVftableCount()
{
    std::scoped_lock lock(g_mutex);
    return static_cast<uint32_t>(DumpPDB::ExeToolset::instance().vftableCount());
}

uint32_t ExeApi_GetTypeCount()
{
    std::scoped_lock lock(g_mutex);
    return static_cast<uint32_t>(DumpPDB::ExeToolset::instance().typeCount());
}

// --- Whole-image analysis ---

namespace
{
PdbApiResult exeTextImpl(const std::function<std::wstring(DumpPDB::ExeToolset&)>& a_fn,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    auto& toolset = DumpPDB::ExeToolset::instance();
    if (!toolset.isLoaded())
    {
        setLastError(L"ExeApi not initialized. Call ExeApi_LoadExe first.");
        return PDBAPI_ERROR_NOT_INITIALIZED;
    }
    return copyToBuffer(a_fn(toolset), a_outBuffer, a_bufferSize, a_outRequiredSize);
}
} // namespace

PdbApiResult ExeApi_GetInfo(wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]()
        {
            return exeTextImpl(
                [](DumpPDB::ExeToolset& t) { return t.infoText(); },
                a_outBuffer,
                a_bufferSize,
                a_outRequiredSize);
        });
}

PdbApiResult ExeApi_GetClassInfo(const wchar_t* a_typeName,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (!a_typeName)
            {
                setLastError(L"a_typeName is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }
            return exeTextImpl(
                [&](DumpPDB::ExeToolset& t) { return t.classInfo(a_typeName); },
                a_outBuffer,
                a_bufferSize,
                a_outRequiredSize);
        });
}

PdbApiResult ExeApi_DescribeAddress(uint64_t a_rvaOrVa,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]()
        {
            return exeTextImpl(
                [&](DumpPDB::ExeToolset& t) { return t.addressInfo(a_rvaOrVa); },
                a_outBuffer,
                a_bufferSize,
                a_outRequiredSize);
        });
}

PdbApiResult ExeApi_DescribeAddressEx(uint64_t a_address,
    int32_t a_kind,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (a_kind < 0 || a_kind > 2)
            {
                setLastError(L"a_kind must be 0 (auto), 1 (RVA) or 2 (VA)");
                return PDBAPI_ERROR_INVALID_ARG;
            }
            return exeTextImpl(
                [&](DumpPDB::ExeToolset& t)
                {
                    return t.addressInfo(
                        a_address, static_cast<DumpPDB::ExeInspector::AddressKind>(a_kind));
                },
                a_outBuffer,
                a_bufferSize,
                a_outRequiredSize);
        });
}

PdbApiResult ExeApi_ExportAnalysis(const wchar_t* a_outDir,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (!a_outDir)
            {
                setLastError(L"a_outDir is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }

            auto& toolset = DumpPDB::ExeToolset::instance();
            if (!toolset.isLoaded())
            {
                setLastError(L"ExeApi not initialized. Call ExeApi_LoadExe first.");
                return PDBAPI_ERROR_NOT_INITIALIZED;
            }

            // The buffer convention calls us twice (size query, then copy). The export
            // is heavy and has side effects, so the first call's log is remembered and
            // handed out by the second call instead of exporting again.
            static std::wstring cachedDir;
            static std::wstring cachedLog;
            static bool cachedOk = false;
            static bool haveCache = false;

            const bool wantsCopy = a_outBuffer != nullptr && a_bufferSize != 0;
            if (!(wantsCopy && haveCache && cachedDir == a_outDir))
            {
                bool ok = false;
                cachedLog = toolset.exportAnalysis(a_outDir, ok);
                cachedOk = ok;
                cachedDir = a_outDir;
                haveCache = true;
            }

            const PdbApiResult rc
                = copyToBuffer(cachedLog, a_outBuffer, a_bufferSize, a_outRequiredSize);

            if (rc == PDBAPI_OK && wantsCopy)
            {
                haveCache = false; // consumed
                if (!cachedOk)
                {
                    setLastError(L"Export finished with errors (see the log)");
                    return PDBAPI_ERROR_EXCEPTION;
                }
            }
            return rc;
        });
}

PdbApiResult ExeApi_ExportResources(const wchar_t* a_outDir,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (!a_outDir)
            {
                setLastError(L"a_outDir is null");
                return PDBAPI_ERROR_INVALID_ARG;
            }

            bool ok = false;
            const PdbApiResult rc = exeTextImpl(
                [&](DumpPDB::ExeToolset& t) { return t.exportResources(a_outDir, ok); },
                a_outBuffer,
                a_bufferSize,
                a_outRequiredSize);
            if (rc == PDBAPI_OK && !ok && a_outBuffer != nullptr)
            {
                setLastError(L"Resource export finished with errors (see the log)");
                return PDBAPI_ERROR_EXCEPTION;
            }
            return rc;
        });
}
