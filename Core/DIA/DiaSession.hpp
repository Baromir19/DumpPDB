#pragma once

#include <dia2.h>
#include <diacreate.h>
#pragma comment(lib, "diaguids.lib")

#include <string>

#include <Core/Util/Com/ComPtr.hpp>
#include <Core/Util/Error/DumpError.hpp>

/// Manages the COM/DIA session lifecycle.
/// Owns IDiaDataSource, IDiaSession, IDiaSymbol (global scope) via RAII ComPtrs.
class DiaSession
{
public:

    DiaSession() = default;

    bool initialize(const std::wstring& a_pdbPath)
    {
        HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comHr))
        {
            wchar_t buf[64];
            swprintf_s(buf, L"CoInitializeEx failed: 0x%X", comHr);
            throw DumpError(buf);
        }
        // S_FALSE means COM was already initialized on this thread; CoUninitialize still needed.
        m_comInitialized = true;

        ComPtr<IDiaDataSource> source;
        HRESULT hrResult = E_FAIL;

        hrResult = NoRegCoCreate(L"msdia140.dll",
            __uuidof(DiaSource),
            __uuidof(IDiaDataSource),
            reinterpret_cast<void**>(&source));

        if (FAILED(hrResult))
        {
            hrResult = CoCreateInstance(__uuidof(DiaSource),
                nullptr,
                CLSCTX_INPROC_SERVER,
                __uuidof(IDiaDataSource),
                reinterpret_cast<void**>(&source));
        }

        if (FAILED(hrResult))
        {
            wchar_t buf[64];
            swprintf_s(buf, L"Unable to create DIA source: 0x%08X", hrResult);
            throw DumpError(buf);
        }

        hrResult = source->loadDataFromPdb(a_pdbPath.c_str());
        if (FAILED(hrResult))
        {
            wchar_t buf[64];
            swprintf_s(buf, L"loadDataFromPdb failed: 0x%X", hrResult);
            throw DumpError(buf);
        }

        ComPtr<IDiaSession> session;
        hrResult = source->openSession(&session);
        if (FAILED(hrResult))
        {
            wchar_t buf[64];
            swprintf_s(buf, L"openSession failed: 0x%X", hrResult);
            throw DumpError(buf);
        }

        ComPtr<IDiaSymbol> globalScope;
        hrResult = session->get_globalScope(&globalScope);
        if (FAILED(hrResult))
        {
            wchar_t buf[64];
            swprintf_s(buf, L"get_globalScope failed: 0x%X", hrResult);
            throw DumpError(buf);
        }

        m_source = std::move(source);
        m_session = std::move(session);
        m_globalScope = std::move(globalScope);
        return true;
    }

    [[nodiscard]] IDiaSession* session() const
    {
        return m_session.get();
    }
    [[nodiscard]] IDiaSymbol* globalScope() const
    {
        return m_globalScope.get();
    }

    ~DiaSession()
    {
        m_globalScope.Release();
        m_session.Release();
        m_source.Release();
        if (m_comInitialized)
        {
            CoUninitialize();
        }
    }

    DiaSession(const DiaSession&) = delete;
    DiaSession& operator=(const DiaSession&) = delete;

    DiaSession(DiaSession&& a_other) noexcept
        : m_source(std::move(a_other.m_source))
        , m_session(std::move(a_other.m_session))
        , m_globalScope(std::move(a_other.m_globalScope))
        , m_comInitialized(a_other.m_comInitialized)
    {
        a_other.m_comInitialized = false;
    }

    DiaSession& operator=(DiaSession&& a_other) noexcept
    {
        if (this != &a_other)
        {
            m_globalScope.Release();
            m_session.Release();
            m_source.Release();
            if (m_comInitialized)
            {
                CoUninitialize();
            }

            m_source = std::move(a_other.m_source);
            m_session = std::move(a_other.m_session);
            m_globalScope = std::move(a_other.m_globalScope);
            m_comInitialized = a_other.m_comInitialized;

            a_other.m_comInitialized = false;
        }
        return *this;
    }

private:

    ComPtr<IDiaDataSource> m_source;
    ComPtr<IDiaSession> m_session;
    ComPtr<IDiaSymbol> m_globalScope;
    bool m_comInitialized = false;
};
