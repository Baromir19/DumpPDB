#pragma once

#include <dia2.h>
#include <diacreate.h>
#pragma comment(lib, "diaguids.lib")

#include <string>

#include "..\..\Util\Container\Singleton.hpp"

class DiaManager : public Singleton<DiaManager>
{
    SET_SINGLETON_FRIEND(DiaManager)

protected:
    DiaManager() : m_source(nullptr), m_session(nullptr), m_globalScope(nullptr) {}

    IDiaDataSource* m_source;
    IDiaSession* m_session;
    IDiaSymbol* m_globalScope;

public:
    bool initialize(const std::wstring& a_pdbPath) 
    {
        if (FAILED(CoInitialize(nullptr))) return false;

        HRESULT hr = CoCreateInstance(__uuidof(DiaSource), nullptr, CLSCTX_INPROC_SERVER,
            __uuidof(IDiaDataSource), (void**)&m_source);
        if (FAILED(hr)) return false;

        hr = m_source->loadDataFromPdb(a_pdbPath.c_str());
        if (FAILED(hr)) return false;

        hr = m_source->openSession(&m_session);
        if (FAILED(hr)) return false;

        hr = m_session->get_globalScope(&m_globalScope);
        return SUCCEEDED(hr);
    }

    IDiaSession* session() const { return m_session; }
    IDiaSymbol* globalScope() const { return m_globalScope; }

    ~DiaManager() 
    {
        if (m_globalScope) m_globalScope->Release();
        if (m_session) m_session->Release();
        if (m_source) m_source->Release();
        CoUninitialize();
    }
};