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
#include <Core/Util/String/Utf8.hpp>

#include <filesystem>
#include <fstream>

/// Facade that owns the DIA session and provides the core dumping/searching API.
/// Wraps DiaSession + SymbolDumper + SymbolFinder into a long-lived singleton.
class PdbToolset : public Singleton<PdbToolset>
{
    SET_SINGLETON_FRIEND(PdbToolset)

protected:

    PdbToolset() = default;

    DiaSession m_session;
    SymbolDumper m_dumper;

public:

    /// Initialize the DIA session and wire up the dumper.
    /// Returns false on any error (exceptions from DiaSession are caught).
    bool initialize(const std::wstring& a_pdbPath)
    {
        try
        {
            m_session.initialize(a_pdbPath);
            m_dumper.setSession(m_session.session());
            m_dumper.setPlatform(detectPlatform());
            return true;
        }
        catch (const DumpError& e)
        {
            std::wcerr << L"PdbToolset initialize failed: " << e.wideMessage() << L'\n';
            return false;
        }
    }

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
    /// Searches by exact name first (all tags), then falls back to namespace-prefix search.
    /// Multiple symbols in the same namespace are grouped into one "namespace X { ... }" block.
    std::wstring dumpTypeByName(const wchar_t* a_name, bool a_caseSensitive)
    {
        std::wstring out;
        if (!a_name)
            return out;

        auto matches
            = SymbolFinder::findAll(m_session.globalScope(), SymTagNull, a_name, a_caseSensitive);

        if (matches.empty())
        {
            matches = SymbolFinder::findByNamespacePrefix(
                m_session.globalScope(), a_name, a_caseSensitive);
        }

        if (matches.empty())
        {
            // Not a type: look for a function / variable / public symbol of that
            // name. This also searches the per-object-file symbols, where `static`
            // functions and file statics live.
            return m_dumper.globals().dumpByName(m_session.globalScope(), a_name, a_caseSensitive);
        }

        bool haveTemplate = false;
        TypeWalker::TemplateInstantiation ti;
        if (m_dumper.config().m_templateParams)
        {
            ti = TypeWalker::makeTemplateInstantiation(a_name);
            haveTemplate = ti.active;
            if (haveTemplate)
            {
                m_dumper.setTemplateInstantiation(ti);
            }
        }

        dumpSymbolsGrouped(matches, out);

        if (haveTemplate)
        {
            out = TypeWalker::substituteTemplateArgs(out, ti);
            m_dumper.setTemplateInstantiation(TypeWalker::TemplateInstantiation{});
        }

        return out;
    }

    /// Find a type by name and enumerate fully-qualified names of its nested
    /// UDT/enum/typedef children, newline-separated.
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
            DWORD symTag = SymTagNull;
            sym->get_symTag(&symTag);
            if (symTag != SymTagUDT)
                continue;

            ComPtr<IDiaEnumSymbols> children;
            if (FAILED(sym->findChildren(SymTagNull, nullptr, nsNone, &children)) || !children)
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

    /// Dump the first matching class/enum/typedef symbol.
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
    /// When a_topLevelOnly is true, only direct children of the global scope are returned.
    std::wstring enumerateSymbolNames(bool a_topLevelOnly = true)
    {
        std::wstring out;
        std::unordered_set<std::wstring> seen;

        ComPtr<IDiaEnumSymbols> symbols;
        if (FAILED(m_session.globalScope()->findChildren(SymTagNull, nullptr, nsNone, &symbols))
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

        return TypeWalker::isTopLevelSymbol(symbol);
    }

    /// Recursively enumerate nested UDT/enum/typedef symbol names within a UDT.
    void enumerateNestedSymbolNamesRecursive(
        IDiaSymbol* a_udt, std::wstring& a_out, std::unordered_set<std::wstring>& a_seen)
    {
        ComPtr<IDiaEnumSymbols> children;
        if (FAILED(a_udt->findChildren(SymTagNull, nullptr, nsNone, &children)) || !children)
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

            if (childTag == SymTagUDT)
            {
                enumerateNestedSymbolNamesRecursive(child.get(), a_out, a_seen);
            }

            child.Release();
        }
    }

    /// Get source files for a named type, newline-separated.
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

    // ============================================================================
    // Global functions / variables / statics / public symbols
    // ============================================================================

    /// Free functions (with GlobalsIncludeMembers also member definitions), grouped
    /// by namespace. a_filter is a substring of the qualified name (nullptr/empty = all).
    std::wstring dumpGlobalFunctions(const wchar_t* a_filter, bool a_caseSensitive)
    {
        return m_dumper.globals().dumpFunctions(m_session.globalScope(), makeQuery(a_filter, a_caseSensitive));
    }

    /// Global variables, file statics and constants (optionally static members and
    /// function-local statics, see DumpConfig).
    std::wstring dumpGlobalVariables(const wchar_t* a_filter, bool a_caseSensitive)
    {
        return m_dumper.globals().dumpVariables(m_session.globalScope(), makeQuery(a_filter, a_caseSensitive));
    }

    /// Public symbols: decorated name + address, no type information.
    std::wstring dumpPublicSymbols(const wchar_t* a_filter, bool a_caseSensitive)
    {
        return m_dumper.globals().dumpPublics(m_session.globalScope(), makeQuery(a_filter, a_caseSensitive));
    }

    /// Tab-separated table of every function, variable, thunk and public symbol
    /// (kind, name, undecorated, rva, section, offset, length, class, compiland,
    /// source, line, flags, type). Header line included.
    std::wstring dumpSymbolTable(const wchar_t* a_filter, bool a_caseSensitive, bool a_withSource)
    {
        return m_dumper.globals().dumpSymbolTable(
            m_session.globalScope(), makeQuery(a_filter, a_caseSensitive), a_withSource);
    }

    /// Everything in the PDB as C++-like text: all top-level types followed by
    /// functions, variables and public symbols. Can be very large.
    std::wstring dumpEverything()
    {
        std::wstring out;

        std::vector<ComPtr<IDiaSymbol>> types;
        const enum SymTagEnum typeTags[] = {SymTagUDT, SymTagEnum, SymTagTypedef};
        for (enum SymTagEnum tag : typeTags)
        {
            ComPtr<IDiaEnumSymbols> e;
            if (FAILED(m_session.globalScope()->findChildren(tag, nullptr, nsNone, &e)) || !e)
                continue;

            ComPtr<IDiaSymbol> sym;
            ULONG celt = 0;
            while (SUCCEEDED(e->Next(1, &sym, &celt)) && celt == 1)
            {
                BSTR raw = nullptr;
                std::wstring name;
                if (SUCCEEDED(sym->get_name(&raw)) && raw)
                {
                    name = raw;
                    SysFreeString(raw);
                }
                if (!name.empty() && !TypeWalker::isSyntheticName(name) && isTopLevelType(sym.get()))
                    types.push_back(sym);
                sym = nullptr;
            }
        }

        out += L"// ============ TYPES ============\n";
        dumpSymbolsGrouped(types, out);

        out += L"\n// ============ FUNCTIONS ============\n";
        out += dumpGlobalFunctions(nullptr, false);
        out += L"\n// ============ VARIABLES ============\n";
        out += dumpGlobalVariables(nullptr, false);
        out += L"\n// ============ PUBLIC SYMBOLS ============\n";
        out += dumpPublicSymbols(nullptr, false);
        return out;
    }

    /// Identity of the loaded PDB: "guid=XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX\nage=N\nsignature=0x...\n".
    /// Compare it with the CodeView GUID/age of an executable (-exeinfo) before
    /// transferring RVAs from the PDB to that executable.
    std::wstring pdbInfo()
    {
        std::wstring out;
        IDiaSymbol* scope = m_session.globalScope();
        if (!scope)
            return out;

        GUID guid = {};
        wchar_t buf[96];
        if (SUCCEEDED(scope->get_guid(&guid)))
        {
            swprintf_s(buf,
                L"guid=%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\n",
                static_cast<unsigned>(guid.Data1),
                static_cast<unsigned>(guid.Data2),
                static_cast<unsigned>(guid.Data3),
                guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
            out += buf;
        }

        DWORD age = 0;
        if (SUCCEEDED(scope->get_age(&age)))
        {
            swprintf_s(buf, L"age=%u\n", static_cast<unsigned>(age));
            out += buf;
        }

        DWORD signature = 0;
        if (SUCCEEDED(scope->get_signature(&signature)))
        {
            swprintf_s(buf, L"signature=0x%08X\n", static_cast<unsigned>(signature));
            out += buf;
        }

        DWORD platform = m_dumper.platform();
        swprintf_s(buf, L"platform=0x%X\n", static_cast<unsigned>(platform));
        out += buf;
        return out;
    }

    /// What PdbToolset::exportToFile can write.
    enum ExportKind : int
    {
        EXPORT_EVERYTHING = 0,          ///< Types + functions + variables + publics.
        EXPORT_FUNCTIONS = 1,
        EXPORT_VARIABLES = 2,
        EXPORT_PUBLICS = 3,
        EXPORT_SYMBOL_TABLE = 4,        ///< TSV, no source lines (fast).
        EXPORT_SYMBOL_TABLE_SOURCE = 5, ///< TSV with source file:line per function.
    };

    /// Writes a (possibly huge) dump straight to a UTF-8 file. This avoids building
    /// the text twice for the size-query/copy buffer convention of the C API.
    /// Returns false and fills a_error on failure.
    bool exportToFile(int a_kind,
        const wchar_t* a_filter,
        bool a_caseSensitive,
        const wchar_t* a_path,
        std::wstring& a_error)
    {
        if (!a_path || !*a_path)
        {
            a_error = L"output path is empty";
            return false;
        }

        std::wstring text;
        switch (a_kind)
        {
        case EXPORT_EVERYTHING:
            text = dumpEverything();
            break;
        case EXPORT_FUNCTIONS:
            text = dumpGlobalFunctions(a_filter, a_caseSensitive);
            break;
        case EXPORT_VARIABLES:
            text = dumpGlobalVariables(a_filter, a_caseSensitive);
            break;
        case EXPORT_PUBLICS:
            text = dumpPublicSymbols(a_filter, a_caseSensitive);
            break;
        case EXPORT_SYMBOL_TABLE:
            text = dumpSymbolTable(a_filter, a_caseSensitive, false);
            break;
        case EXPORT_SYMBOL_TABLE_SOURCE:
            text = dumpSymbolTable(a_filter, a_caseSensitive, true);
            break;
        default:
            a_error = L"unknown export kind";
            return false;
        }

        std::ofstream file(std::filesystem::path(a_path), std::ios::binary);
        if (!file)
        {
            a_error = L"cannot open output file";
            return false;
        }

        const std::string utf8 = wideToUtf8(text);
        file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        if (!file)
        {
            a_error = L"write failed";
            return false;
        }
        return true;
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
                    if (SUCCEEDED(envSym->get_value(&val)) && val.vt == VT_BSTR && val.bstrVal)
                    {
                        out += L"[ENV] ";
                        out += envName;
                        out += L" = ";
                        out += val.bstrVal;
                        out += L"\n";
                    }
                    else
                    {
                        out += L"[ENV] ";
                        out += envName;
                        out += L"\n";
                    }
                    VariantClear(&val);
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
    /// newline-separated. Matched by file name.
    std::wstring getSymbolsBySourceFile(
        const wchar_t* a_fileName, bool a_caseSensitive) // NOTE: doesn't work
    {
        std::wstring out;
        if (!a_fileName)
        {
            return out;
        }

        auto searchType = a_caseSensitive ? nsCaseSensitive : nsCaseInsensitive;

        ComPtr<IDiaEnumSourceFiles> enumSourceFiles;
        if (FAILED(m_session.session()->findFile(nullptr, a_fileName, searchType, &enumSourceFiles))
            || !enumSourceFiles)
        {
            return out;
        }

        ComPtr<IDiaSourceFile> sourceFile;
        ULONG celt = 0;
        std::unordered_set<std::wstring> seen;

        while (SUCCEEDED(enumSourceFiles->Next(1, &sourceFile, &celt)) && celt == 1)
        {
            ComPtr<IDiaEnumSymbols> enumCompilands;
            if (FAILED(sourceFile->get_compilands(&enumCompilands)) || !enumCompilands)
            {
                sourceFile.Release();
                continue;
            }

            ComPtr<IDiaSymbol> compiland;
            while (SUCCEEDED(enumCompilands->Next(1, &compiland, &celt)) && celt == 1)
            {
                ComPtr<IDiaEnumSymbols> enumSymbols;
                if (FAILED(compiland->findChildren(SymTagNull, nullptr, nsNone, &enumSymbols))
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

    /// Search for strings in a binary file.
    /// For PE files, searches are scoped to string-candidate sections.
    /// @param a_encodingFlags  Bitwise OR of StringEncoding values.
    /// @param a_sectionNames   Comma-separated PE section names; empty = all string-candidate sections.
    /// @param a_regexPattern   Optional regex filter; empty = no filter.
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
    /// Supported formats: "FF ?? 01 BD", "FF??01BD", "0xFF??01BD", "{ FF ?? 01 BD }".
    /// @param a_sectionNames  Comma-separated PE section names; empty = all sections.
    /// Returns newline-separated hex offsets, or empty on failure/invalid pattern.
    std::wstring findSignaturesInFile(
        const wchar_t* a_filePath, const char* a_pattern, const char* a_sectionNames = "")
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

    static GlobalQuery makeQuery(const wchar_t* a_filter, bool a_caseSensitive)
    {
        GlobalQuery q;
        if (a_filter)
            q.filter = a_filter;
        q.caseSensitive = a_caseSensitive;
        return q;
    }

    /// CPU the PDB was built for: the platform of the first compiland that has
    /// compiland details (0 when none does).
    DWORD detectPlatform()
    {
        ComPtr<IDiaEnumSymbols> compilands;
        if (FAILED(m_session.globalScope()->findChildren(
                SymTagCompiland, nullptr, nsNone, &compilands))
            || !compilands)
            return 0;

        ComPtr<IDiaSymbol> compiland;
        ULONG celt = 0;
        for (int i = 0; i < 64 && SUCCEEDED(compilands->Next(1, &compiland, &celt)) && celt == 1;
             ++i)
        {
            ComPtr<IDiaEnumSymbols> details;
            if (SUCCEEDED(compiland->findChildren(SymTagCompilandDetails, nullptr, nsNone, &details))
                && details)
            {
                ComPtr<IDiaSymbol> detail;
                ULONG n = 0;
                if (SUCCEEDED(details->Next(1, &detail, &n)) && n == 1)
                {
                    DWORD platform = 0;
                    if (SUCCEEDED(detail->get_platform(&platform)) && platform != 0)
                        return platform;
                }
            }
            compiland = nullptr;
        }
        return 0;
    }

    /// Dump a set of symbols, grouping those in the same namespace into one block.
    void dumpSymbolsGrouped(const std::vector<ComPtr<IDiaSymbol>>& a_symbols, std::wstring& aoutput)
    {
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
                for (const auto& sym : syms)
                {
                    aoutput += m_dumper.dumpTopLevelAny(sym.get());
                }
            }
            else
            {
                aoutput += TypeWalker::namespaceBlockOpen(ns);

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
                    case SymTagFunction:
                    case SymTagData:
                    case SymTagPublicSymbol:
                    case SymTagThunk:
                        aoutput += m_dumper.dumpGlobalSymbol(sym.get(), 1);
                        aoutput += L"\n";
                        break;
                    default:
                        break;
                    }
                }

                size_t partCount = TypeWalker::namespacePartCount(ns);
                m_dumper.popQualifiedScope(partCount);

                aoutput += TypeWalker::namespaceBlockClose(ns);
            }
        }
    }
};
