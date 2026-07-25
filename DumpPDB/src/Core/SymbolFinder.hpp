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

        auto _searchType = a_caseSensitive ? nsCaseSensitive : nsCaseInsensitive;

        ComPtr<IDiaEnumSymbols> _enumSymbols;
        if (FAILED(a_scope->findChildren(a_tag, a_name, _searchType, &_enumSymbols)) || !_enumSymbols)
            return ComPtr<IDiaSymbol>();

        ComPtr<IDiaSymbol> _symbol;
        ULONG _celt = 0;
        if (SUCCEEDED(_enumSymbols->Next(1, &_symbol, &_celt)) && _celt == 1)
        {
            return _symbol;
        }

        return ComPtr<IDiaSymbol>();
    }

    /// Find all symbols matching the given tag and name within a_scope.
    /// This mirrors the old behavior of displayType(name) which processed all matches.
    static std::vector<ComPtr<IDiaSymbol>> findAll(IDiaSymbol* a_scope, enum SymTagEnum a_tag,
                                                    const wchar_t* a_name, bool a_caseSensitive)
    {
        std::vector<ComPtr<IDiaSymbol>> _results;
        if (!a_scope || !a_name) return _results;

        auto _searchType = a_caseSensitive ? nsCaseSensitive : nsCaseInsensitive;

        ComPtr<IDiaEnumSymbols> _enumSymbols;
        if (FAILED(a_scope->findChildren(a_tag, a_name, _searchType, &_enumSymbols)) || !_enumSymbols)
            return _results;

        ComPtr<IDiaSymbol> _symbol;
        ULONG _celt = 0;
        while (SUCCEEDED(_enumSymbols->Next(1, &_symbol, &_celt)) && _celt == 1)
        {
            _results.push_back(std::move(_symbol));
        }

        return _results;
    }

    /// Find symbols by namespace prefix fallback.
    /// Searches all top-level symbols and matches those whose name starts with a_prefix
    /// (or a_prefix + "::") and has no additional "::" after the prefix.
    /// This mirrors the old displayTypePrefixed behavior.
    static std::vector<ComPtr<IDiaSymbol>> findByNamespacePrefix(IDiaSymbol* a_scope,
                                                                    const wchar_t* a_prefix, bool a_caseSensitive)
    {
        std::vector<ComPtr<IDiaSymbol>> _results;
        if (!a_scope || !a_prefix) return _results;

        ComPtr<IDiaEnumSymbols> _enumSymbols;
        if (FAILED(a_scope->findChildren(SymTagNull, nullptr, nsNone, &_enumSymbols)) || !_enumSymbols)
            return _results;

        // Build the prefix to search for
        std::wstring _prefix = a_prefix;
        if (_prefix.size() < 2 || _prefix[_prefix.size() - 2] != L':' || _prefix[_prefix.size() - 1] != L':')
        {
            _prefix += L"::";
        }

        ComPtr<IDiaSymbol> _symbol;
        ULONG _celt = 0;
        while (SUCCEEDED(_enumSymbols->Next(1, &_symbol, &_celt)) && _celt == 1)
        {
            BSTR _bstrName = nullptr;
            if (SUCCEEDED(_symbol->get_name(&_bstrName)) && _bstrName)
            {
                std::wstring _name(_bstrName);
                SysFreeString(_bstrName);

                // Check if name starts with prefix and has no additional "::" after the prefix
                bool _matches = false;
                if (a_caseSensitive)
                {
                    _matches = (_name.compare(0, _prefix.size(), _prefix) == 0
                        && _name.find(L"::", _prefix.size()) == std::wstring::npos);
                }
                else
                {
                    // Case-insensitive comparison
                    std::wstring _lowerName = _name;
                    std::wstring _lowerPrefix = _prefix;
                    for (auto& c : _lowerName) c = towlower(c);
                    for (auto& c : _lowerPrefix) c = towlower(c);
                    _matches = (_lowerName.compare(0, _lowerPrefix.size(), _lowerPrefix) == 0
                        && _lowerName.find(L"::", _lowerPrefix.size()) == std::wstring::npos);
                }

                if (_matches)
                {
                    _results.push_back(std::move(_symbol));
                }
            }
        }

        return _results;
    }
};