#pragma once

#include <string>
#include <unordered_set>

#include <dia2.h>

#include <Core/Dump/DumpContext.hpp>
#include <Core/Util/Com/ComPtr.hpp>

/// Resolves and caches "type source" comments: maps DIA symbols to the source
/// files where they are defined via address -> line -> source file lookup.
class TypeSourceTracker
{
public:

    explicit TypeSourceTracker(DumpContext& a_ctx)
        : m_ctx(a_ctx)
    {
    }

    /// Get source file names for a symbol, newline-separated.
    /// Returns the result instead of storing it for later output.
    std::wstring getTypeSourceFiles(IDiaSymbol* a_symbol)
    {
        std::wstring ret;
        if (!a_symbol)
            return ret;

        ComPtr<IDiaEnumLineNumbers> enum_symbolsLines;
        ComPtr<IDiaSourceFile> sourceFile;
        ComPtr<IDiaLineNumber> lineNumber;

        DWORD addressSection = 0;
        DWORD addressOffset = 0;

        if (SUCCEEDED(a_symbol->get_addressSection(&addressSection))
            && SUCCEEDED(a_symbol->get_addressOffset(&addressOffset)))
        {
            if (m_ctx.session()
                && SUCCEEDED(m_ctx.session()->findLinesByAddr(
                    addressSection, addressOffset, 1, &enum_symbolsLines))
                && enum_symbolsLines)
            {
                ULONG celt = 0;
                while (SUCCEEDED(enum_symbolsLines->Next(1, &lineNumber, &celt)) && celt == 1)
                {
                    if (SUCCEEDED(lineNumber->get_sourceFile(&sourceFile)) && sourceFile)
                    {
                        BSTR _filename;
                        if (SUCCEEDED(sourceFile->get_fileName(&_filename)))
                        {
                            // Convert BSTR to std::wstring immediately to avoid
                            // ownership issues (double-free, use-after-free, leaks)
                            ret += _filename;
                            ret += L"\n";
                            SysFreeString(_filename);
                        }
                    }
                }
            }
        }
        return ret;
    }

    /// Register source file info for a symbol (stores for later output).
    void registerTypeSource(IDiaSymbol* a_symbol)
    {
        if (!m_ctx.config().m_showTypeSource)
            return;

        std::wstring srcs = getTypeSourceFiles(a_symbol);
        if (srcs.empty())
            return;

        // Split the newline-separated result and store each file.
        size_t start = 0;
        while (start < srcs.size())
        {
            auto nl = srcs.find(L'\n', start);
            if (nl == std::wstring::npos)
            {
                m_ctx.typeSources().emplace_back(srcs.substr(start));
                break;
            }
            m_ctx.typeSources().emplace_back(srcs.substr(start, nl - start));
            start = nl + 1;
        }
    }

    std::wstring typeSources()
    {
        if (m_ctx.typeSources().empty())
            return L"";

        std::wstring ret;
        for (const auto& _src : m_ctx.typeSources())
        {
            ret += L"// ";
            ret += _src;
            ret += L"\n";
        }
        m_ctx.typeSources().clear();
        return ret;
    }

    std::wstring getTypeSourceFilesRecursive(
        IDiaSymbol* a_symbol, std::unordered_set<DWORD>& a_visited, int depth = 0)
    {
        if (depth > kMaxDepth)
            return {};

        std::wstring ret;

        if (!a_symbol)
            return ret;

        DWORD id = 0;
        a_symbol->get_symIndexId(&id);

        if (!a_visited.insert(id).second)
        {
            return {};
        }

        ret += getTypeSourceFiles(a_symbol);

        ComPtr<IDiaEnumSymbols> children;

        if (FAILED(a_symbol->findChildren(SymTagNull, nullptr, nsNone, &children)) || !children)
        {
            return ret;
        }

        ComPtr<IDiaSymbol> child;
        ULONG celt = 0;

        while (SUCCEEDED(children->Next(1, &child, &celt)) && celt == 1)
        {
            DWORD tag = SymTagNull;
            child->get_symTag(&tag);

            switch (tag)
            {
            case SymTagFunction:
            case SymTagData:
            case SymTagUDT:
            case SymTagEnum:
            case SymTagTypedef:
                ret += getTypeSourceFilesRecursive(child.get(), a_visited, depth + 1);
                break;

            default:
                break;
            }
        }

        return ret;
    }

private:

    static constexpr int kMaxDepth = 256;

    DumpContext& m_ctx;
};
