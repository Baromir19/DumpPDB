#pragma once

#include <string>
#include <vector>
#include <map>

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
    /// if no exact matches found.
    /// Multiple symbols in the same namespace are grouped into one "namespace X { ... }" block.
    std::wstring dumpTypeByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring out;
        if (!a_name) return out;

        // Step 1: Search by exact name across all symbol types
        auto matches = SymbolFinder::findAll(m_session.globalScope(), SymTagNull, a_name, a_caseSensitive);

        // Step 2: Fallback to namespace prefix search (like old displayTypePrefixed)
        if (matches.empty())
        {
            matches = SymbolFinder::findByNamespacePrefix(m_session.globalScope(), a_name, a_caseSensitive);
        }

        dumpSymbolsGrouped(matches, out);
        return out;
    }

    /// Dump a class/enum/typedef by name using findFirst (return first match only).
    /// This mirrors the old displayClass(name)/displayEnum(name)/displayTypedef(name) behavior.
    std::wstring dumpClassByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring out;
        if (!a_name) return out;

        auto _sym = SymbolFinder::findFirst(m_session.globalScope(), SymTagUDT, a_name, a_caseSensitive);
        if (_sym)
        {
            out += m_dumper.dumpTopLevelAny(_sym.get());
        }
        return out;
    }

    std::wstring dumpEnumByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring out;
        if (!a_name) return out;

        auto _sym = SymbolFinder::findFirst(m_session.globalScope(), SymTagEnum, a_name, a_caseSensitive);
        if (_sym)
        {
            out += m_dumper.dumpTopLevelAny(_sym.get());
        }
        return out;
    }

    std::wstring dumpTypedefByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring out;
        if (!a_name) return out;

        auto _sym = SymbolFinder::findFirst(m_session.globalScope(), SymTagTypedef, a_name, a_caseSensitive);
        if (_sym)
        {
            out += m_dumper.dumpTopLevelAny(_sym.get());
        }
        return out;
    }

    // Dump all compilands
    std::wstring dumpCompilands()
    {
        std::wstring out;

        ComPtr<IDiaEnumSymbols> enum_symbolsSymbols;
        if (FAILED(m_session.globalScope()->findChildren(SymTagCompiland, nullptr, nsNone, &enum_symbolsSymbols)) || !enum_symbolsSymbols)
            return out;

        ComPtr<IDiaSymbol> compiland;
        ULONG celt = 0;
        while (SUCCEEDED(enum_symbolsSymbols->Next(1, &compiland, &celt)) && celt == 1)
        {
            BSTR name = nullptr;
            if (SUCCEEDED(compiland->get_name(&name)) && name)
            {
                out += name;
                out += L"\n";
                SysFreeString(name);
            }
            compiland.Release();
        }

        return out;
    }

    // Dump compilands with environment details
    std::wstring dumpCompilandsEnv()
    {
        std::wstring out;

        ComPtr<IDiaEnumSymbols> enum_symbols;
        if (FAILED(m_session.globalScope()->findChildren(SymTagCompiland, nullptr, nsNone, &enum_symbols)) || !enum_symbols)
            return out;

        ComPtr<IDiaSymbol> compiland;
        ULONG celt = 0;
        while (SUCCEEDED(enum_symbols->Next(1, &compiland, &celt)) && celt == 1)
        {
            std::wstring name = TypeWalker::getName(compiland.get());
            out += L"[OBJ] ";
            out += name;
            out += L"\n";

            // Compiland details
            ComPtr<IDiaEnumSymbols> _details;
            if (SUCCEEDED(compiland->findChildren(SymTagCompilandDetails, nullptr, nsNone, &_details)) && _details)
            {
                ComPtr<IDiaSymbol> _detail;
                while (SUCCEEDED(_details->Next(1, &_detail, &celt)) && celt == 1)
                {
                    DWORD _platform = 0, _language = 0;
                    _detail->get_platform(&_platform);
                    _detail->get_language(&_language);

                    BSTR _compilerName = nullptr;
                    _detail->get_compilerName(&_compilerName);

                    BOOL _isDebug = FALSE;
                    _detail->get_hasDebugInfo(&_isDebug);

                    wchar_t buf[256];
                    swprintf_s(buf, L"[ABOUT] Compiler: %s; Language: %u; Platform: %u; Debug: %s\n",
                        _compilerName ? _compilerName : L"unknown", _language, _platform, _isDebug ? L"true" : L"false");
                    out += buf;
                    if (_compilerName) SysFreeString(_compilerName);
                }
            }

            // Compiland environment
            ComPtr<IDiaEnumSymbols> env;
            if (SUCCEEDED(compiland->findChildren(SymTagCompilandEnv, nullptr, nsNone, &env)) && env)
            {
                ComPtr<IDiaSymbol> envSym;
                while (SUCCEEDED(env->Next(1, &envSym, &celt)) && celt == 1)
                {
                    std::wstring envName = TypeWalker::getName(envSym.get());

                    VARIANT val;
                    VariantInit(&val);
                    if (SUCCEEDED(envSym->get_value(&val)) && val.bstrVal && val.vt == VT_BSTR)
                    {
                        out += L"[ENV] ";
                        out += envName;
                        out += L" = ";
                        out += val.bstrVal;
                        out += L"\n";
                        VariantClear(&val);
                    }
                    else
                    {
                        out += L"[ENV] ";
                        out += envName;
                        out += L"\n";
                    }
                }
            }
        }

        return out;
    }

    // Dump all source files
    std::wstring dumpSourceFiles()
    {
        std::wstring out;

        ComPtr<IDiaEnumSourceFiles> enumSourceFiles;
        if (FAILED(m_session.session()->findFile(nullptr, nullptr, nsNone, &enumSourceFiles)) || !enumSourceFiles)
            return out;

        ComPtr<IDiaSourceFile> sourceFile;
        ULONG celt = 0;
        while (SUCCEEDED(enumSourceFiles->Next(1, &sourceFile, &celt)) && celt == 1)
        {
            BSTR fileName = nullptr;
            if (SUCCEEDED(sourceFile->get_fileName(&fileName)) && fileName)
            {
                out += fileName;
                out += L"\n";
                SysFreeString(fileName);
            }
        }

        return out;
    }

private:
    /// Dump a set of symbols, grouping those in the same namespace into one
    /// "namespace X { ... }" block. Symbols without a namespace are dumped as-is.
    void dumpSymbolsGrouped(
        const std::vector<ComPtr<IDiaSymbol>>& a_symbols,
        std::wstring& aoutput)
    {
        // Group symbols by namespace key.
        // Key "<empty>" for global (no namespace) symbols.
        std::map<std::wstring, std::vector<ComPtr<IDiaSymbol>>> groups;

        for (auto& sym : a_symbols)
        {
            std::wstring ns;
            if (TypeWalker::isTopLevelSymbol(sym.get()))
            {
                ns = TypeWalker::parseQualifiedName(sym.get()).ns;
            }
            groups[ns].push_back(sym);
        }

        for (auto& [ns, syms] : groups)
        {
            if (ns.empty())
            {
                // Global scope — dump each symbol directly.
                for (auto& sym : syms)
                {
                    aoutput += m_dumper.dumpTopLevelAny(sym.get());
                }
            }
            else
            {
                // Open one namespace block per group.
                aoutput += L"namespace ";
                aoutput += ns;
                aoutput += L"\n{\n";

                m_dumper.pushQualifiedScope(ns);

                for (auto& sym : syms)
                {
                    DWORD symTag = SymTagNull;
                    sym->get_symTag((DWORD*)&symTag);

                    switch (symTag)
                    {
                    case SymTagUDT:     aoutput += m_dumper.dumpClass(sym.get(), 1); break;
                    case SymTagEnum:    aoutput += m_dumper.dumpEnum(sym.get(), 1); break;
                    case SymTagTypedef: aoutput += m_dumper.dumpTypedef(sym.get(), 1); break;
                    default: break;
                    }
                }

                size_t partCount = 1;
                for (size_t i = 0; i + 1 < ns.size(); ++i)
                {
                    if (ns[i] == L':' && ns[i + 1] == L':')
                    {
                        ++partCount;
                        ++i;
                    }
                }
                m_dumper.popQualifiedScope(partCount);

                aoutput += L"}\n";
            }
        }
    }
};