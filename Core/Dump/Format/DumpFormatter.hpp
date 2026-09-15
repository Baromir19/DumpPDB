#pragma once

#include <cstdio>
#include <string>

#include <dia2.h>

#include <Core/DIA/TypeWalker.hpp>
#include <Core/Dump/DumpContext.hpp>

/// Formatting primitives shared by all renderers: indentation, size comments,
/// const/volatile prefixes, UDT keywords, scope braces, section comments and
/// access-specifier labels.
class DumpFormatter
{
public:

    explicit DumpFormatter(DumpContext& a_ctx)
        : m_ctx(a_ctx)
    {
    }

    std::wstring tab(int a_repeat) const
    {
        return std::wstring(a_repeat * 4, L' ');
    }

    std::wstring sizeComment(IDiaSymbol* a_symbol) const
    {
        if (!m_ctx.config().m_showSize)
            return L"";

        ULONGLONG len;
        if (SUCCEEDED(a_symbol->get_length(&len)))
        {
            wchar_t buf[64];
            swprintf_s(buf, L"// size: %llu byte\n", len);
            return buf;
        }
        return L"";
    }

    std::wstring modPrefix(IDiaSymbol* a_symbol) const
    {
        std::wstring ret;
        BOOL isConst = FALSE;
        BOOL isVol = FALSE;
        if (SUCCEEDED(a_symbol->get_constType(&isConst)) && isConst)
            ret += L"const ";
        if (SUCCEEDED(a_symbol->get_volatileType(&isVol)) && isVol)
            ret += L"volatile ";
        return ret;
    }

    std::wstring udtKeyword(IDiaSymbol* a_symbol) const
    {
        auto name = TypeWalker::getUDTKindName(a_symbol);
        if (name)
        {
            std::wstring ret = name;
            ret += L" ";
            return ret;
        }

        // Check for anonymous union/struct
        if (TypeWalker::isAnonymousUDT(a_symbol))
        {
            DWORD udtKind = 0;
            a_symbol->get_udtKind(&udtKind);
            switch (udtKind)
            {
            case UdtStruct:
                return L"struct ";
            case UdtUnion:
                return L"union ";
            default:
                break;
            }
        }

        return L"";
    }

    std::wstring scopeBegin(int a_nestingLevel)
    {
        std::wstring ret;
        if (m_ctx.config().m_curlyBraceNewline)
        {
            ret += L"\n";
            ret += tab(a_nestingLevel);
        }
        else
        {
            ret += L" ";
        }
        ret += L"{\n";
        return ret;
    }

    std::wstring scopeEnd(int a_nestingLevel)
    {
        std::wstring ret = tab(a_nestingLevel);
        ret += L"};\n";
        return ret;
    }

    bool headerComment(
        std::wstring& o_out, const wchar_t* a_label, int a_nesting, bool a_hasContent)
    {
        if (a_hasContent)
        {
            o_out += L"\n";
        }
        o_out += tab(a_nesting);
        o_out += L"///";
        o_out += a_label;
        o_out += L"\n";
        return true;
    }

    /// Emit access specifier label if access has changed.
    /// Returns the new lastAccess value.
    DWORD emitAccessLabel(
        std::wstring& a_output, IDiaSymbol* a_symbol, DWORD a_lastAccess, int a_nestingLevel)
    {
        if (!m_ctx.config().m_showAccess)
            return a_lastAccess;

        DWORD access = 0;
        if (SUCCEEDED(a_symbol->get_access(&access)) && access != a_lastAccess)
        {
            a_lastAccess = access;
            a_output += tab(a_nestingLevel - 1);
            const wchar_t* accessName = nullptr;
            if (m_ctx.config().m_baseAccessType)
            {
                access = m_ctx.config().m_baseAccessType;
            }
            switch (access)
            {
            case CV_private:
                accessName = L"private";
                break;
            case CV_protected:
                accessName = L"protected";
                break;
            case CV_public:
                accessName = L"public";
                break;
            case 0:
                accessName = L"public";
                break; // undefined !!!
            }
            if (accessName)
            {
                a_output += accessName;
                a_output += L":\n";
            }
        }
        return a_lastAccess;
    }

private:

    DumpContext& m_ctx;
};
