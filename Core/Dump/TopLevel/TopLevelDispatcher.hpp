#pragma once

#include <string>

#include <dia2.h>

#include <Core/DIA/TypeBuilder.hpp>
#include <Core/DIA/TypeWalker.hpp>
#include <Core/Dump/DumpContext.hpp>
#include <Core/Dump/IDumpCoordinator.hpp>

/// Entry point of the dump domain: unwraps namespace blocks around top-level
/// symbols and dispatches them to the owning type renderer.
class TopLevelDispatcher
{
public:

    TopLevelDispatcher(DumpContext& a_ctx, IDumpCoordinator& a_host)
        : m_ctx(a_ctx)
        , m_host(a_host)
    {
    }

    /// Dump any top-level symbol (class/enum/typedef), wrapping it in its
    /// namespace block if it lives inside a namespace.
    /// Uses the compact C++17 form "namespace User::Math { ... }".
    std::wstring dumpTopLevelAny(IDiaSymbol* a_symbol)
    {
        std::wstring ret;

        if (!a_symbol)
            return ret;

        std::wstring ns;
        bool hasNs = false;

        if (TypeWalker::isTopLevelSymbol(a_symbol))
        {
            auto qname = TypeWalker::parseQualifiedName(a_symbol);
            ns = qname.ns;
            hasNs = !ns.empty();
        }

        if (hasNs)
        {
            ret += TypeWalker::namespaceBlockOpen(ns);
            m_host.pushQualifiedScope(ns);
        }

        DWORD symTag = SymTagNull;
        a_symbol->get_symTag((DWORD*)&symTag);

        int nesting = hasNs ? 1 : 0;
        switch (symTag)
        {
        case SymTagUDT:
            ret += m_host.dumpClass(a_symbol, nesting);
            break;
        case SymTagEnum:
            ret += m_host.dumpEnum(a_symbol, nesting);
            break;
        case SymTagTypedef:
            ret += m_host.dumpTypedef(a_symbol, nesting);
            break;
        default:
            break;
        }

        if (hasNs)
        {
            size_t partCount = TypeWalker::namespacePartCount(ns);
            m_host.popQualifiedScope(partCount);
            ret += TypeWalker::namespaceBlockClose(ns);
        }

        return ret;
    }

    void processType(IDiaSymbol* a_symbol, std::wstring& a_output)
    {
        DWORD symTag = 0;
        if (!SUCCEEDED(a_symbol->get_symTag(&symTag)))
            return;

        switch (symTag)
        {
        case SymTagTypedef:
        case SymTagUDT:
        case SymTagEnum:
            a_output += dumpTopLevelAny(a_symbol);
            break;
        case SymTagData:
        {
            std::wstring typeText;
            try
            {
                typeText = TypeWalker::resolveType(
                    a_symbol, m_ctx.scope(), true, m_ctx.config().m_intStyle)
                               .build();
            }
            catch (...)
            {
                typeText = L"/* <error> */";
            }
            a_output += typeText;
            break;
        }
        case SymTagFunction:
            a_output += m_host.dumpFunction(a_symbol, 0);
            break;
        default:
            break;
        }
    }

private:

    DumpContext& m_ctx;
    IDumpCoordinator& m_host;
};
