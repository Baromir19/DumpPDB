#pragma once

#include <dia2.h>
#include <diacreate.h>
#pragma comment(lib, "diaguids.lib")

#include <string>

#include <Util/Com/ComPtr.hpp>
#include <Util/Error/DumpError.hpp>

/// Manages the COM/DIA session lifecycle.
/// Owns IDiaDataSource, IDiaSession, IDiaSymbol (global scope).
/// Uses ComPtr for RAII - no manual Release() calls needed.

class DiaSession
{
public:
    DiaSession() = default;

    bool initialize(const std::wstring& a_pdbPath)
    {
        HRESULT _comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(_comHr))
        {
            wchar_t _buf[64];
            swprintf_s(_buf, L"CoInitializeEx failed: 0x%X", _comHr);
            throw DumpError(_buf);
        }
        // S_FALSE means COM was already initialized on this thread.
        // We still need to call CoUninitialize for this call.
        m_comInitialized = true;

        ComPtr<IDiaDataSource> _source;
        HRESULT hr = CoCreateInstance(__uuidof(DiaSource), nullptr, CLSCTX_INPROC_SERVER,
            __uuidof(IDiaDataSource), (void**)&_source);
        if (FAILED(hr))
        {
            wchar_t _buf[64];
            swprintf_s(_buf, L"CoCreateInstance failed: 0x%X", hr);
            throw DumpError(_buf);
        }

        hr = _source->loadDataFromPdb(a_pdbPath.c_str());
        if (FAILED(hr))
        {
            wchar_t _buf[64];
            swprintf_s(_buf, L"loadDataFromPdb failed: 0x%X", hr);
            throw DumpError(_buf);
        }

        ComPtr<IDiaSession> _session;
        hr = _source->openSession(&_session);
        if (FAILED(hr))
        {
            wchar_t _buf[64];
            swprintf_s(_buf, L"openSession failed: 0x%X", hr);
            throw DumpError(_buf);
        }

        ComPtr<IDiaSymbol> _globalScope;
        hr = _session->get_globalScope(&_globalScope);
        if (FAILED(hr))
        {
            wchar_t _buf[64];
            swprintf_s(_buf, L"get_globalScope failed: 0x%X", hr);
            throw DumpError(_buf);
        }

        m_source = std::move(_source);
        m_session = std::move(_session);
        m_globalScope = std::move(_globalScope);
        return true;
    }

    IDiaSession* session() const { return m_session.get(); }
    IDiaSymbol* globalScope() const { return m_globalScope.get(); }

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

    // Non-copyable
    DiaSession(const DiaSession&) = delete;
    DiaSession& operator=(const DiaSession&) = delete;

    // Move: transfer COM ownership and reset source to prevent double CoUninitialize
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
            // Release current resources
            m_globalScope.Release();
            m_session.Release();
            m_source.Release();
            if (m_comInitialized)
            {
                CoUninitialize();
            }

            // Transfer ownership
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