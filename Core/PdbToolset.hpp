#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include <iostream>

#include <Core/DIA/DiaSession.hpp>
#include <Core/DIA/SymbolDumper.hpp>
#include <Core/DIA/SymbolFinder.hpp>
#include <Core/DIA/TypeWalker.hpp>

#include <Core/Search/BinaryScanner.hpp>
#include <Core/Search/Signature.hpp>
#include <Core/Search/StringScanner.hpp>
#include <Core/Util/Container/Singleton.hpp>

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

    DiaSession m_session;
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
        catch (const DumpError& e)
        {
            std::wcerr << L"PdbToolset initialize failed: " << e.wideMessage() << L'\n';
            return false;
        }
    }

    /// Access underlying DIA objects for advanced use.
    [[nodiscard]]
    IDiaSession* session() const
    {
        return m_session.session();
    }

    [[nodiscard]]
    IDiaSymbol* globalScope() const
    {
        return m_session.globalScope();
    }

    SymbolDumper& dumper()
    {
        return m_dumper;
    }

    [[nodiscard]]
    const SymbolDumper& dumper() const
    {
        return m_dumper;
    }

    /// Dump all types matching the given name.
    /// Searches by exact name first (all tags), then falls back to namespace prefix search
    /// if no exact matches found.
    /// Multiple symbols in the same namespace are grouped into one "namespace X { ... }" block.
    std::wstring dumpTypeByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring out;
        if (!a_name)
            return out;

        // Step 1: Search by exact name across all symbol types
        auto matches
            = SymbolFinder::findAll(m_session.globalScope(), SymTagNull, a_name, a_caseSensitive);

        // Step 2: Fallback to namespace prefix search (like old displayTypePrefixed)
        if (matches.empty())
        {
            matches = SymbolFinder::findByNamespacePrefix(
                m_session.globalScope(), a_name, a_caseSensitive);
        }

        dumpSymbolsGrouped(matches, out);
        return out;
    }

    /// Find a type by name and enumerate its nested UDT/enum/typedef children,
    /// returning their fully-qualified names, newline-separated.
    /// E.g. for "Test::Actor", returns "Test::Actor::Weapon", "Test::Actor::SaveData", etc.
    std::wstring enumerateNestedTypeNames(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring out;
        if (!a_name)
            return out;

        auto matches
            = SymbolFinder::findAll(m_session.globalScope(), SymTagNull, a_name, a_caseSensitive);
        if (matches.empty())
            return out;

        std::unordered_set<std::wstring> seen;
        for (auto& sym : matches)
        {
            // Only look at UDT types — enums/typedefs can't have nested types.
            DWORD symTag = SymTagNull;
            sym->get_symTag(&symTag);
            if (symTag != SymTagUDT)
                continue;

            ComPtr<IDiaEnumSymbols> children;
            if (FAILED(sym->findChildren(SymTagNull, nullptr, nsNone, &children))
                || !children)
                continue;

            ComPtr<IDiaSymbol> child;
            ULONG celt = 0;
            while (SUCCEEDED(children->Next(1, &child, &celt)) && celt == 1)
            {
                DWORD childTag = SymTagNull;
                child->get_symTag(&childTag);
                if (childTag == SymTagUDT || childTag == SymTagEnum || childTag == SymTagTypedef)
                {
                    std::wstring childName = TypeWalker::getName(child.get());
                    if (!TypeWalker::isSyntheticName(childName) && seen.insert(childName).second)
                    {
                        out += childName;
                        out += L"\n";
                    }
                }
                child.Release();
            }
        }

        return out;
    }

    /// Dump a class/enum/typedef by name using findFirst (return first match only).
    /// This mirrors the old displayClass(name)/displayEnum(name)/displayTypedef(name) behavior.
    std::wstring dumpClassByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring out;
        if (!a_name)
            return out;

        auto _sym
            = SymbolFinder::findFirst(m_session.globalScope(), SymTagUDT, a_name, a_caseSensitive);
        if (_sym)
        {
            out += m_dumper.dumpTopLevelAny(_sym.get());
        }
        return out;
    }

    std::wstring dumpEnumByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring out;
        if (!a_name)
            return out;

        auto _sym
            = SymbolFinder::findFirst(m_session.globalScope(), SymTagEnum, a_name, a_caseSensitive);
        if (_sym)
        {
            out += m_dumper.dumpTopLevelAny(_sym.get());
        }
        return out;
    }

    std::wstring dumpTypedefByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring out;
        if (!a_name)
            return out;

        auto _sym = SymbolFinder::findFirst(
            m_session.globalScope(), SymTagTypedef, a_name, a_caseSensitive);
        if (_sym)
        {
            out += m_dumper.dumpTopLevelAny(_sym.get());
        }
        return out;
    }

    /// Enumerate names of all UDT/enum/typedef symbols, newline-separated.
    /// When a_topLevelOnly is true (default), only direct children of the global
    /// scope are returned (top-level types). When false, ALL types including
    /// nested types are recursively enumerated.
    std::wstring enumerateSymbolNames(bool a_topLevelOnly = true)
    {
        std::wstring out;
        std::unordered_set<std::wstring> seen;

        ComPtr<IDiaEnumSymbols> symbols;
        if (FAILED(m_session.globalScope()->findChildren(
                SymTagNull, nullptr, nsNone, &symbols))
            || !symbols)
        {
            return out;
        }

        ComPtr<IDiaSymbol> symbol;
        ULONG celt = 0;
        while (SUCCEEDED(symbols->Next(1, &symbol, &celt)) && celt == 1)
        {
            DWORD symTag = SymTagNull;

            if (FAILED(symbol->get_symTag(&symTag)))
            {
                symbol = nullptr;
                continue;
            }

            const bool isType
                = symTag == SymTagUDT || symTag == SymTagEnum || symTag == SymTagTypedef;

            if (!isType)
            {
                symbol = nullptr;
                continue;
            }
            
            if (a_topLevelOnly && !isTopLevelType(symbol.get()))
            {
                symbol = nullptr;
                continue;
            }

            BSTR name = nullptr;
            if (SUCCEEDED(symbol->get_name(&name)) && name)
            {
                out += name;
                out += L"\n";
                SysFreeString(name);
            }
        }
       
        return out;
    }

    bool isTopLevelType(IDiaSymbol* symbol)
    {
        if (!symbol)
            return false;

        DWORD tag = SymTagNull;
        if (FAILED(symbol->get_symTag(&tag)))
            return false;

        if (tag != SymTagUDT && tag != SymTagEnum && tag != SymTagTypedef)
            return false;

        ComPtr<IDiaSymbol> parent;

        if (SUCCEEDED(symbol->get_classParent(&parent)) && parent)
        {
            return false;
        }

        return true;
    }

    /// Recursively enumerate nested UDT/enum/typedef symbol names within a UDT.
    void enumerateNestedSymbolNamesRecursive(
        IDiaSymbol* a_udt, std::wstring& a_out, std::unordered_set<std::wstring>& a_seen)
    {
        ComPtr<IDiaEnumSymbols> children;
        if (FAILED(a_udt->findChildren(SymTagNull, nullptr, nsNone, &children))
            || !children)
            return;

        ComPtr<IDiaSymbol> child;
        ULONG celt = 0;
        while (SUCCEEDED(children->Next(1, &child, &celt)) && celt == 1)
        {
            DWORD childTag = SymTagNull;
            child->get_symTag(&childTag);

            if (childTag == SymTagUDT || childTag == SymTagEnum || childTag == SymTagTypedef)
            {
                std::wstring childName = TypeWalker::getName(child.get());
                if (!TypeWalker::isSyntheticName(childName) && a_seen.insert(childName).second)
                {
                    a_out += childName;
                    a_out += L"\n";
                }
            }

            // Recurse deeper into nested UDTs.
            if (childTag == SymTagUDT)
            {
                enumerateNestedSymbolNamesRecursive(child.get(), a_out, a_seen);
            }

            child.Release();
        }
    }

    /// Get source files for a named type, newline-separated.
    /// Uses the same address->line->sourceFile lookup as registerTypeSource.
    std::wstring getTypeSourceFilesByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring out;
        if (!a_name)
        {
            return out;
        }

        auto searchType = a_caseSensitive ? nsCaseSensitive : nsCaseInsensitive;

        ComPtr<IDiaEnumSymbols> enum_symbolsSymbols;
        if (FAILED(m_session.globalScope()->findChildren(
                SymTagNull, a_name, searchType, &enum_symbolsSymbols))
            || !enum_symbolsSymbols)
        {
            return out;
        }

        ComPtr<IDiaSymbol> symbol;
        ULONG celt = 0;
        std::unordered_set<DWORD> visited;
        while (SUCCEEDED(enum_symbolsSymbols->Next(1, &symbol, &celt)) && celt == 1)
        {
            DWORD symTag = SymTagNull;
            if (SUCCEEDED(symbol->get_symTag(&symTag)))
            {
                if (symTag == SymTagUDT || symTag == SymTagEnum || symTag == SymTagTypedef)
                {
                    out += m_dumper.getTypeSourceFilesRecursive(symbol.get(), visited);
                }
            }
        }

        return out;
    }

    // Dump all compilands
    std::wstring dumpCompilands()
    {
        std::wstring out;

        ComPtr<IDiaEnumSymbols> enum_symbolsSymbols;
        if (FAILED(m_session.globalScope()->findChildren(
                SymTagCompiland, nullptr, nsNone, &enum_symbolsSymbols))
            || !enum_symbolsSymbols)
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
        if (FAILED(m_session.globalScope()->findChildren(
                SymTagCompiland, nullptr, nsNone, &enum_symbols))
            || !enum_symbols)
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
            if (SUCCEEDED(
                    compiland->findChildren(SymTagCompilandDetails, nullptr, nsNone, &_details))
                && _details)
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
                    swprintf_s(buf,
                        L"[ABOUT] Compiler: %s; Language: %u; Platform: %u; Debug: %s\n",
                        _compilerName ? _compilerName : L"unknown",
                        _language,
                        _platform,
                        _isDebug ? L"true" : L"false");
                    out += buf;
                    if (_compilerName)
                        SysFreeString(_compilerName);
                }
            }

            // Compiland environment
            ComPtr<IDiaEnumSymbols> env;
            if (SUCCEEDED(compiland->findChildren(SymTagCompilandEnv, nullptr, nsNone, &env))
                && env)
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
        if (FAILED(m_session.session()->findFile(nullptr, nullptr, nsNone, &enumSourceFiles))
            || !enumSourceFiles)
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

    /// Get all UDT/enum/typedef symbol names defined in a given source file,
    /// newline-separated. The file is matched by name (case-insensitive).
    /// Uses IDiaSourceFile::get_compilands to find compilands that reference
    /// the file, then enumerates their UDT/enum/typedef children.
    std::wstring getSymbolsBySourceFile(const wchar_t* a_fileName, bool a_caseSensitive) // NOTE: doesn't work
    {
        std::wstring out;
        if (!a_fileName)
        {
            return out;
        }

        auto searchType = a_caseSensitive ? nsCaseSensitive : nsCaseInsensitive;

        // Find the source file by name
        ComPtr<IDiaEnumSourceFiles> enumSourceFiles;
        if (FAILED(m_session.session()->findFile(
                nullptr, a_fileName, searchType, &enumSourceFiles))
            || !enumSourceFiles)
        {
            return out;
        }

        ComPtr<IDiaSourceFile> sourceFile;
        ULONG celt = 0;
        std::unordered_set<std::wstring> seen;

        while (SUCCEEDED(enumSourceFiles->Next(1, &sourceFile, &celt)) && celt == 1)
        {
            // Get compilands that reference this source file
            ComPtr<IDiaEnumSymbols> enumCompilands;
            if (FAILED(sourceFile->get_compilands(&enumCompilands)) || !enumCompilands)
            {
                sourceFile.Release();
                continue;
            }

            ComPtr<IDiaSymbol> compiland;
            while (SUCCEEDED(enumCompilands->Next(1, &compiland, &celt)) && celt == 1)
            {
                // Find UDT/enum/typedef symbols in this compiland
                ComPtr<IDiaEnumSymbols> enumSymbols;
                if (FAILED(compiland->findChildren(
                        SymTagNull, nullptr, nsNone, &enumSymbols))
                    || !enumSymbols)
                {
                    compiland.Release();
                    continue;
                }

                ComPtr<IDiaSymbol> symbol;
                while (SUCCEEDED(enumSymbols->Next(1, &symbol, &celt)) && celt == 1)
                {
                    DWORD symTag = SymTagNull;
                    if (SUCCEEDED(symbol->get_symTag(&symTag)))
                    {
                        if (symTag == SymTagUDT || symTag == SymTagEnum || symTag == SymTagTypedef)
                        {
                            BSTR name = nullptr;
                            if (SUCCEEDED(symbol->get_name(&name)) && name)
                            {
                                std::wstring symbolName(name);
                                SysFreeString(name);

                                // Skip synthetic/anonymous names
                                if (!TypeWalker::isSyntheticName(symbolName)
                                    && seen.insert(symbolName).second)
                                {
                                    out += symbolName;
                                    out += L"\n";
                                }
                            }
                        }
                    }
                    symbol.Release();
                }
                compiland.Release();
            }
            sourceFile.Release();
        }

        return out;
    }

    // ============================================================================
    // Binary file search (strings / signatures)
    // ============================================================================

    

    enum PdbApiStringOutputFlags : uint32_t
    {
        PDBAPI_STRING_SHOW_OFFSET = 1 << 0,
        PDBAPI_STRING_SHOW_ENCODING = 1 << 1,
    };

    /// Search for strings in a binary file (exe/dll/etc).
    /// For PE files, searches are scoped to string-candidate sections
    /// (.text, .rdata, .data, etc.) rather than the whole file.
    /// a_encodingFlags is a bitwise OR of StringEncoding values.
    /// a_sectionNames is a comma-separated list of PE section names to search
    /// (e.g. ".rdata,.data"). Empty string means "all string-candidate sections".
    /// a_regexPattern is an optional regex to filter results (empty = no filter).
    /// Returns a newline-separated text report: "offset: [encoding] text".
    /// Returns empty string on file load failure.
    std::wstring findStringsInFile(const wchar_t* a_filePath,
        uint32_t a_minLength,
        uint32_t a_encodingFlags,
        uint32_t a_outStringFlags,
        const char* a_sectionNames = "",
        const char* a_regexPattern = "")
    {
        std::wstring out;
        if (!a_filePath)
        {
            return out;
        }

        DumpPDB::BinaryScanner scanner;
        if (!scanner.load(a_filePath))
        {
            return out;
        }

        std::string sections = a_sectionNames ? a_sectionNames : "";
        std::string regex = a_regexPattern ? a_regexPattern : "";

        auto matches = scanner.findStrings(a_minLength, a_encodingFlags, sections, regex);

        wchar_t buf[64];
        for (const auto& m : matches)
        {
            // TODO: to string builder!!! And flags too
            // e.g. You can find only offsets, string values, or both
            if (a_outStringFlags & PdbApiStringOutputFlags::PDBAPI_STRING_SHOW_OFFSET)
            {
                swprintf_s(buf, L"0x%08llX: ", static_cast<unsigned long long>(m.offset));
                out += buf;
            }

            if (a_outStringFlags & PdbApiStringOutputFlags::PDBAPI_STRING_SHOW_ENCODING)
            {
                switch (m.encoding)
                {
                case DumpPDB::StringEncoding::ASCII:
                    out += L"[ASCII] ";
                    break;
                case DumpPDB::StringEncoding::UTF8:
                    out += L"[UTF-8] ";
                    break;
                case DumpPDB::StringEncoding::UTF16LE:
                    out += L"[UTF-16LE] ";
                    break;
                default:
                    out += L"[?] ";
                    break;
                }
            }

            out += m.text;
            out += L"\n";
        }

        return out;
    }

    /// Search for a byte signature (with wildcards) in a binary file.
    /// Supports patterns like "FF ?? 01 BD ?? CA", "FF??01BD??CA", "0xFF??01BD??CA",
    /// and "{ FF ?? 01 BD }" (YARA-style).
    /// a_sectionNames is a comma-separated list of PE section names to search
    /// (empty = all string-candidate sections for PE, whole file for non-PE).
    /// Returns a newline-separated list of offsets in hex.
    /// Returns empty string on file load failure or invalid pattern.
    std::wstring findSignaturesInFile(const wchar_t* a_filePath,
        const char* a_pattern,
        const char* a_sectionNames = "")
    {
        std::wstring out;
        if (!a_filePath || !a_pattern)
        {
            return out;
        }

        DumpPDB::BinaryScanner scanner;
        if (!scanner.load(a_filePath))
        {
            return out;
        }

        std::string sections = a_sectionNames ? a_sectionNames : "";
        auto matches = scanner.findSignatures(a_pattern, sections);

        wchar_t buf[64];
        for (const auto& m : matches)
        {
            swprintf_s(buf, L"0x%08llX\n", static_cast<unsigned long long>(m.offset));
            out += buf;
        }

        return out;
    }

private:

    /// Dump a set of symbols, grouping those in the same namespace into one
    /// "namespace X { ... }" block. Symbols without a namespace are dumped as-is.
    void dumpSymbolsGrouped(const std::vector<ComPtr<IDiaSymbol>>& a_symbols, std::wstring& aoutput)
    {
        // Group symbols by namespace key.
        // Key "<empty>" for global (no namespace) symbols.
        std::map<std::wstring, std::vector<ComPtr<IDiaSymbol>>> groups;

        for (const auto& sym : a_symbols)
        {
            std::wstring ns;
            if (TypeWalker::isTopLevelSymbol(sym.get()))
            {
                ns = TypeWalker::parseQualifiedName(sym.get()).ns;
            }
            groups[ns].push_back(sym);
        }

        for (const auto& [ns, syms] : groups)
        {
            if (ns.empty())
            {
                // Global scope — dump each symbol directly.
                for (const auto& sym : syms)
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

                for (const auto& sym : syms)
                {
                    DWORD symTag = SymTagNull;
                    sym->get_symTag(&symTag);

                    switch (symTag)
                    {
                    case SymTagUDT:
                        aoutput += m_dumper.dumpClass(sym.get(), 1);
                        break;
                    case SymTagEnum:
                        aoutput += m_dumper.dumpEnum(sym.get(), 1);
                        break;
                    case SymTagTypedef:
                        aoutput += m_dumper.dumpTypedef(sym.get(), 1);
                        break;
                    default:
                        break;
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