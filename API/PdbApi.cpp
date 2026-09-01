#include <API/PdbApi.h>

#include <Core/PdbToolset.hpp>
#include <Core/Util/Error/DumpError.hpp>

#include <mutex>
#include <string>
#include <cwchar>

namespace
{
// DIA / PdbToolset::instance() is a single shared session, serialize all
// access to it. If you need concurrent sessions for different PDBs later,
// this whole file needs to move to a handle-based design instead of the
// Singleton
//
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

/// Shared buffer-copy logic implementing the two-phase size/copy convention.
PdbApiResult copyToBuffer(const std::wstring& a_source,
    wchar_t* a_outBuffer,
    uint32_t a_bufferSize,
    uint32_t* a_outRequiredSize)
{
    const auto needed = static_cast<uint32_t>(a_source.size() + 1); // + '\0'

    if (a_outRequiredSize != nullptr)
    {
        *a_outRequiredSize = needed;
    }

    if (a_outBuffer == nullptr || a_bufferSize == 0)
    {
        return PDBAPI_OK; // size-query mode
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

/// Common wrapper: runs a_fn under the lock, catches everything,
/// routes exceptions into g_lastError + a proper result code.
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

/// Shared implementation for all "find by name -> dump -> copy" entry points.
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
    // PdbToolset is a Singleton wrapping DiaSession
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

            DumpConfig cfg;
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

PdbApiResult PdbApi_EnumerateSymbolNames(
    wchar_t* a_outBuffer, uint32_t a_bufferSize, uint32_t* a_outRequiredSize, int32_t a_topLevelOnly)
{
    return guarded(
        [&]() -> PdbApiResult
        {
            if (PdbToolset::instance().session() == nullptr)
            {
                return PDBAPI_ERROR_NOT_INITIALIZED;
            }

            std::wstring result
                = PdbToolset::instance().enumerateSymbolNames(a_topLevelOnly != 0);

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

            std::wstring result = PdbToolset::instance().findStringsInFile(
                a_filePath, a_minLength, a_encodingFlags, a_outStringFlags, a_sectionNames, a_regexPattern);

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

            std::wstring result
                = PdbToolset::instance().findSignaturesInFile(a_filePath, a_pattern, a_sectionNames);

            if (result.empty())
            {
                setLastError(L"No signature matches found, file could not be loaded, or pattern is invalid");
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
