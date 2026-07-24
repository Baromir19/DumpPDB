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
        if (FAILED(CoInitialize(nullptr)))
        {
            throw DumpError(L"Failed to initialize COM");
        }
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

    // Non-copyable, movable
    DiaSession(const DiaSession&) = delete;
    DiaSession& operator=(const DiaSession&) = delete;
    DiaSession(DiaSession&&) = default;
    DiaSession& operator=(DiaSession&&) = default;

private:
    ComPtr<IDiaDataSource> m_source;
    ComPtr<IDiaSession> m_session;
    ComPtr<IDiaSymbol> m_globalScope;
    bool m_comInitialized = false;
};