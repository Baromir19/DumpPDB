#pragma once

#include <dia2.h>
#include <string>
#include <vector>

#include <Util/Com/ComPtr.hpp>

/// Searches for DIA symbols by name within a given scope.

class SymbolFinder
{
public:
    /// Find the first symbol matching the given tag and name within a_scope.
    /// Returns nullptr if not found.
    /// This mirrors the old behavior of displayClass(name)/displayEnum(name)/displayTypedef(name)
    /// which returned after the first match.
    static ComPtr<IDiaSymbol> findFirst(IDiaSymbol* a_scope, enum SymTagEnum a_tag,
                                         const wchar_t* a_name, bool a_caseSensitive)
    {
        if (!a_scope || !a_name) return ComPtr<IDiaSymbol>();

        auto searchType = a_caseSensitive ? nsCaseSensitive : nsCaseInsensitive;

        ComPtr<IDiaEnumSymbols> enum_symbolsSymbols;
        if (FAILED(a_scope->findChildren(a_tag, a_name, searchType, &enum_symbolsSymbols)) || !enum_symbolsSymbols)
            return ComPtr<IDiaSymbol>();

        ComPtr<IDiaSymbol> symbol;
        ULONG celt = 0;
        if (SUCCEEDED(enum_symbolsSymbols->Next(1, &symbol, &celt)) && celt == 1)
        {
            return symbol;
        }

        return ComPtr<IDiaSymbol>();
    }

    /// Find all symbols matching the given tag and name within a_scope.
    /// This mirrors the old behavior of displayType(name) which processed all matches.
    static std::vector<ComPtr<IDiaSymbol>> findAll(
        IDiaSymbol* a_scope,
        enum SymTagEnum a_tag,
        const wchar_t* a_name, 
        bool a_caseSensitive
    )
    {
        std::vector<ComPtr<IDiaSymbol>> results;
        if (!a_scope || !a_name) return results;

        auto _searchType = a_caseSensitive ? nsCaseSensitive : nsCaseInsensitive;

        ComPtr<IDiaEnumSymbols> enum_symbolsSymbols;
        if (FAILED(a_scope->findChildren(a_tag, a_name, _searchType, &enum_symbolsSymbols)) || !enum_symbolsSymbols)
            return results;

        ComPtr<IDiaSymbol> symbol;
        ULONG celt = 0;
        while (SUCCEEDED(enum_symbolsSymbols->Next(1, &symbol, &celt)) && celt == 1)
        {
            results.push_back(std::move(symbol));
        }

        return results;
    }

    /// Find symbols by namespace prefix fallback.
    /// Searches all top-level symbols and matches those whose name starts with a_prefix
    /// (or a_prefix + "::") and has no additional "::" after the prefix.
    /// This mirrors the old displayTypePrefixed behavior.
    static std::vector<ComPtr<IDiaSymbol>> findByNamespacePrefix(IDiaSymbol* a_scope,
                                                                    const wchar_t* a_prefix, bool a_caseSensitive)
    {
        std::vector<ComPtr<IDiaSymbol>> results;
        if (!a_scope || !a_prefix) return results;

        ComPtr<IDiaEnumSymbols> enum_symbolsSymbols;
        if (FAILED(a_scope->findChildren(SymTagNull, nullptr, nsNone, &enum_symbolsSymbols)) || !enum_symbolsSymbols)
            return results;

        // Build the prefix to search for
        std::wstring prefix = a_prefix;
        if (prefix.size() < 2 || prefix[prefix.size() - 2] != L':' || prefix[prefix.size() - 1] != L':')
        {
            prefix += L"::";
        }

        ComPtr<IDiaSymbol> symbol;
        ULONG celt = 0;
        while (SUCCEEDED(enum_symbolsSymbols->Next(1, &symbol, &celt)) && celt == 1)
        {
            BSTR bstrName = nullptr;
            if (SUCCEEDED(symbol->get_name(&bstrName)) && bstrName)
            {
                std::wstring name(bstrName);
                SysFreeString(bstrName);

                // Check if name starts with prefix and has no additional "::" after the prefix
                bool matches = false;
                if (a_caseSensitive)
                {
                    matches = (name.compare(0, prefix.size(), prefix) == 0
                        && name.find(L"::", prefix.size()) == std::wstring::npos);
                }
                else
                {
                    // Case-insensitive comparison
                    std::wstring lowerName = name;
                    std::wstring lowerPrefix = prefix;
                    for (auto& c : lowerName) c = towlower(c);
                    for (auto& c : lowerPrefix) c = towlower(c);
                    matches = (lowerName.compare(0, lowerPrefix.size(), lowerPrefix) == 0
                        && lowerName.find(L"::", lowerPrefix.size()) == std::wstring::npos);
                }

                if (matches)
                {
                    results.push_back(std::move(symbol));
                }
            }
        }

        return results;
    }
};