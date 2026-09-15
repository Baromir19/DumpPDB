#pragma once

#include <string>

#include <dia2.h>

#include <Core/DIA/TypeBuilder.hpp>
#include <Core/Dump/DumpContext.hpp>
#include <Core/Dump/Format/DumpFormatter.hpp>
#include <Core/Util/Com/ComPtr.hpp>

/// Renders typedef/using-alias declarations by resolving the underlying type.
class TypedefRenderer
{
public:

    TypedefRenderer(DumpContext& a_ctx, DumpFormatter& a_fmt)
        : m_ctx(a_ctx)
        , m_fmt(a_fmt)
    {
    }

    std::wstring dumpTypedef(IDiaSymbol* a_symbol, int a_nestingLevel)
    {
        std::wstring ret;
        ret += m_fmt.tab(a_nestingLevel);
        ret += m_fmt.modPrefix(a_symbol);
        ret += L"typedef ";

        // Get the typedef name
        std::wstring typedefName = TypeWalker::getName(a_symbol, m_ctx.scope());

        // Resolve the underlying type (the type this typedef aliases)
        // We need to get the type of the typedef symbol, not the typedef itself
        std::wstring typeText;
        try
        {
            ComPtr<IDiaSymbol> underlyingType;
            if (SUCCEEDED(a_symbol->get_type(&underlyingType)) && underlyingType)
            {
                // Build the underlying type's full declaration
                TypeBuilder builder = TypeWalker::resolveType(
                    underlyingType.get(), m_ctx.scope(), true, m_ctx.config().m_intStyle);
                // Set the typedef name as the "variable name" in the declaration
                builder.name(typedefName);
                typeText = builder.build();
            }
            else
            {
                // Fallback: just use the typedef name itself
                typeText = typedefName;
            }
        }
        catch (...)
        {
            typeText = L"/* <error resolving type> */";
        }
        ret += typeText;
        ret += L";\n";
        return ret;
    }

private:

    DumpContext& m_ctx;
    DumpFormatter& m_fmt;
};
