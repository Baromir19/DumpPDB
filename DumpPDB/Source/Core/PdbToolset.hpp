#pragma once

#include <string>
#include <vector>

#include <Core/DiaSession.hpp>
#include <Core/SymbolDumper.hpp>
#include <Core/SymbolFinder.hpp>
#include <Core/TypeWalker.hpp>
#include <Util/Container/Singleton.hpp>

/// Facade that owns the DIA session lifetime and provides the core dumping/searching API.
/// Wraps DiaSession + SymbolDumper + SymbolFinder into a long-lived singleton,
/// solving the lifetime problem from Application::initialize where DiaSession
/// was a local variable destroyed at the end of the if-block.
///
/// Usage (Variant A - Singleton):
///   if (!PdbToolset::instance().initialize(pdbPath)) { /* error */ }
///   auto text = PdbToolset::instance().dumpTypeByName(L"MyClass", false);
///   ConsoleManager::print(text.c_str());

class PdbToolset : public Singleton<PdbToolset>
{
    SET_SINGLETON_FRIEND(PdbToolset)

protected:
    PdbToolset() = default;

    DiaSession   m_session;
    SymbolDumper m_dumper;

public:
    /// Initialize the DIA session and wire up the dumper.
    /// Returns true on success, false on any error (exceptions from DiaSession are caught).
    bool initialize(const std::wstring& a_pdbPath)
    {
        try
        {
            m_session.initialize(a_pdbPath);
            m_dumper.setSession(m_session.session());
            return true;
        }
        catch (const DumpError&)
        {
            return false;
        }
    }

    /// Access underlying DIA objects for advanced use.
    IDiaSession* session() const { return m_session.session(); }
    IDiaSymbol* globalScope() const { return m_session.globalScope(); }
    SymbolDumper& dumper() { return m_dumper; }
    const SymbolDumper& dumper() const { return m_dumper; }

    /// Dump all types matching the given name.
    /// Searches by exact name first (all tags), then falls back to namespace prefix search
    /// if no exact matches found
    std::wstring dumpTypeByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring _out;
        if (!a_name) return _out;

        // Step 1: Search by exact name across all symbol types
        auto _matches = SymbolFinder::findAll(m_session.globalScope(), SymTagNull, a_name, a_caseSensitive);
        for (auto& _sym : _matches)
        {
            m_dumper.processType(_sym.get(), _out);
        }

        // Step 2: Fallback to namespace prefix search (like old displayTypePrefixed)
        if (_matches.empty())
        {
            auto _prefixed = SymbolFinder::findByNamespacePrefix(m_session.globalScope(), a_name, a_caseSensitive);
            for (auto& _sym : _prefixed)
            {
                m_dumper.processType(_sym.get(), _out);
            }
        }

        return _out;
    }

    /// Dump a class/enum/typedef by name using findFirst (return first match only).
    /// This mirrors the old displayClass(name)/displayEnum(name)/displayTypedef(name) behavior.
    std::wstring dumpClassByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring _out;
        if (!a_name) return _out;

        auto _sym = SymbolFinder::findFirst(m_session.globalScope(), SymTagUDT, a_name, a_caseSensitive);
        if (_sym)
        {
            _out += m_dumper.dumpClass(_sym.get());
        }
        return _out;
    }

    std::wstring dumpEnumByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring _out;
        if (!a_name) return _out;

        auto _sym = SymbolFinder::findFirst(m_session.globalScope(), SymTagEnum, a_name, a_caseSensitive);
        if (_sym)
        {
            _out += m_dumper.dumpEnum(_sym.get());
        }
        return _out;
    }

    std::wstring dumpTypedefByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring _out;
        if (!a_name) return _out;

        auto _sym = SymbolFinder::findFirst(m_session.globalScope(), SymTagTypedef, a_name, a_caseSensitive);
        if (_sym)
        {
            _out += m_dumper.dumpTypedef(_sym.get());
        }
        return _out;
    }

    // Dump all compilands
    std::wstring dumpCompilands()
    {
        std::wstring _out;

        ComPtr<IDiaEnumSymbols> _enumSymbols;
        if (FAILED(m_session.globalScope()->findChildren(SymTagCompiland, nullptr, nsNone, &_enumSymbols)) || !_enumSymbols)
            return _out;

        ComPtr<IDiaSymbol> _compiland;
        ULONG _celt = 0;
        while (SUCCEEDED(_enumSymbols->Next(1, &_compiland, &_celt)) && _celt == 1)
        {
            BSTR _name = nullptr;
            if (SUCCEEDED(_compiland->get_name(&_name)) && _name)
            {
                _out += _name;
                _out += L"\n";
                SysFreeString(_name);
            }
            _compiland.Release();
        }

        return _out;
    }

    // Dump compilands with environment details
    std::wstring dumpCompilandsEnv()
    {
        std::wstring _out;

        ComPtr<IDiaEnumSymbols> _enum;
        if (FAILED(m_session.globalScope()->findChildren(SymTagCompiland, nullptr, nsNone, &_enum)) || !_enum)
            return _out;

        ComPtr<IDiaSymbol> _compiland;
        ULONG _celt = 0;
        while (SUCCEEDED(_enum->Next(1, &_compiland, &_celt)) && _celt == 1)
        {
            std::wstring _name = TypeWalker::getName(_compiland.get());
            _out += L"[OBJ] ";
            _out += _name;
            _out += L"\n";

            // Compiland details
            ComPtr<IDiaEnumSymbols> _details;
            if (SUCCEEDED(_compiland->findChildren(SymTagCompilandDetails, nullptr, nsNone, &_details)) && _details)
            {
                ComPtr<IDiaSymbol> _detail;
                while (SUCCEEDED(_details->Next(1, &_detail, &_celt)) && _celt == 1)
                {
                    DWORD _platform = 0, _language = 0;
                    _detail->get_platform(&_platform);
                    _detail->get_language(&_language);

                    BSTR _compilerName = nullptr;
                    _detail->get_compilerName(&_compilerName);

                    BOOL _isDebug = FALSE;
                    _detail->get_hasDebugInfo(&_isDebug);

                    wchar_t _buf[256];
                    swprintf_s(_buf, L"[ABOUT] Compiler: %s; Language: %u; Platform: %u; Debug: %s\n",
                        _compilerName ? _compilerName : L"unknown", _language, _platform, _isDebug ? L"true" : L"false");
                    _out += _buf;
                    if (_compilerName) SysFreeString(_compilerName);
                }
            }

            // Compiland environment
            ComPtr<IDiaEnumSymbols> _env;
            if (SUCCEEDED(_compiland->findChildren(SymTagCompilandEnv, nullptr, nsNone, &_env)) && _env)
            {
                ComPtr<IDiaSymbol> _envSym;
                while (SUCCEEDED(_env->Next(1, &_envSym, &_celt)) && _celt == 1)
                {
                    std::wstring _envName = TypeWalker::getName(_envSym.get());

                    VARIANT _val;
                    VariantInit(&_val);
                    if (SUCCEEDED(_envSym->get_value(&_val)) && _val.bstrVal && _val.vt == VT_BSTR)
                    {
                        _out += L"[ENV] ";
                        _out += _envName;
                        _out += L" = ";
                        _out += _val.bstrVal;
                        _out += L"\n";
                        VariantClear(&_val);
                    }
                    else
                    {
                        _out += L"[ENV] ";
                        _out += _envName;
                        _out += L"\n";
                    }
                }
            }
        }

        return _out;
    }

    // Dump all source files
    std::wstring dumpSourceFiles()
    {
        std::wstring _out;

        ComPtr<IDiaEnumSourceFiles> _enumSourceFiles;
        if (FAILED(m_session.session()->findFile(nullptr, nullptr, nsNone, &_enumSourceFiles)) || !_enumSourceFiles)
            return _out;

        ComPtr<IDiaSourceFile> _sourceFile;
        ULONG _celt = 0;
        while (SUCCEEDED(_enumSourceFiles->Next(1, &_sourceFile, &_celt)) && _celt == 1)
        {
            BSTR _fileName = nullptr;
            if (SUCCEEDED(_sourceFile->get_fileName(&_fileName)) && _fileName)
            {
                _out += _fileName;
                _out += L"\n";
                SysFreeString(_fileName);
            }
        }

        return _out;
    }
};