#pragma once

#include <string>

#include <dia2.h>

#include <Core/Dump/Constants/ConstantRenderer.hpp>
#include <Core/Dump/DumpContext.hpp>
#include <Core/Dump/Format/DumpFormatter.hpp>

/// Renders enum declarations with their enumerators and the ": base-type"
/// suffix.
class EnumRenderer
{
public:

    EnumRenderer(DumpContext& a_ctx, DumpFormatter& a_fmt, ConstantRenderer& a_constants)
        : m_ctx(a_ctx)
        , m_fmt(a_fmt)
        , m_constants(a_constants)
    {
    }

    std::wstring dumpEnum(IDiaSymbol* a_symbol, int a_nestingLevel)
    {
        std::wstring ret;

        ret += m_fmt.tab(a_nestingLevel);
        ret += m_fmt.sizeComment(a_symbol);

        ret += m_fmt.tab(a_nestingLevel);
        ret += m_fmt.modPrefix(a_symbol);
        ret += L"enum";

        // Derive a friendly name: filters synthetic names like <unnamed-tag>,
        // <undefined-type> or $HASH names, and turns inplace anonymous enum names
        // like <unnamed-type-m_Member> into a usable "MemberEnum" identifier.

        std::wstring enum_symbolsName = TypeWalker::prettyTypeName(
            TypeWalker::getName(a_symbol, m_ctx.scope(), m_ctx.config().m_showNonScoped), L"Enum");
        if (!enum_symbolsName.empty())
        {
            ret += L" ";
            ret += enum_symbolsName;
        }

        ret += baseTypeInheritance(a_symbol);
        ret += m_fmt.scopeBegin(a_nestingLevel);

        ComPtr<IDiaEnumSymbols> enum_symbolsMembers;
        if (SUCCEEDED(a_symbol->findChildren(SymTagData, nullptr, nsNone, &enum_symbolsMembers)))
        {
            ComPtr<IDiaSymbol> member;
            ULONG celt = 0;
            while (SUCCEEDED(enum_symbolsMembers->Next(1, &member, &celt)) && celt == 1)
            {
                ret += m_fmt.tab(a_nestingLevel + 1);
                ret += TypeWalker::getName(member.get(), m_ctx.scope());
                ret += m_constants.constantValueSuffix(member.get());
                ret += L",\n";
            }
        }

        ret += m_fmt.scopeEnd(a_nestingLevel);
        return ret;
    }

    std::wstring baseTypeInheritance(IDiaSymbol* a_symbol) const
    {
        // if (!m_config.m_showInfoComment) return L"";

        auto base = TypeWalker::getBaseTypeName(a_symbol, m_ctx.config().m_intStyle);
        if (base)
        {
            std::wstring ret = L" : ";
            ret += base;
            return ret;
        }
        return L"";
    }

private:

    DumpContext& m_ctx;
    DumpFormatter& m_fmt;
    ConstantRenderer& m_constants;
};
