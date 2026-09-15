#pragma once

#include <string>

#include <dia2.h>

#include <Core/DIA/TypeWalker.hpp>
#include <Core/Dump/DumpContext.hpp>
#include <Core/Dump/Format/DumpFormatter.hpp>
#include <Core/Dump/IDumpCoordinator.hpp>
#include <Core/Util/Com/ComPtr.hpp>

/// Renders class/struct/union declarations: name, inheritance list, nested
/// members and template headers for reconstructed instantiations.
class ClassRenderer
{
public:

    ClassRenderer(DumpContext& a_ctx, DumpFormatter& a_fmt, IDumpCoordinator& a_host)
        : m_ctx(a_ctx)
        , m_fmt(a_fmt)
        , m_host(a_host)
    {
    }

    std::wstring dumpClass(IDiaSymbol* a_symbol, int a_nestingLevel)
    {
        std::wstring ret;

        // Get class name relative to current scope
        std::wstring className
            = TypeWalker::getName(a_symbol, m_ctx.scope(), m_ctx.config().m_showNonScoped);

        ret += m_fmt.tab(a_nestingLevel);
        ret += m_fmt.sizeComment(a_symbol);

        // When the requested name is a template instantiation that we reconstruct
        // into a generic template<...> definition (m_template.active), the concrete
        // argument list (e.g. "Singleton<WeaponManager>") must be dropped from the
        // class declaration — a template definition uses the bare class-name. The
        // full folded/instantiated name is preserved for member scope resolution.
        std::wstring scopeName = className;

        // Emit the generated template<...> header only once — on the outermost
        // top-level declaration of the requested instantiation. Nested members
        // (enum/class/function declarations) must NOT repeat it.
        if (m_ctx.templateInstantiation().active && !m_ctx.templateHeaderDone())
        {
            // Remember the concrete instantiation this dump was derived from
            // (e.g. "Singleton<WeaponManager>") before the class-name is turned
            // back into its bare class-template name ("Singleton"). This comment
            // is left untouched by the later argument substitution so the concrete
            // argument list stays visible.
            const std::wstring instantiationName = className;
            const auto lt = className.find(L'<');
            if (lt != std::wstring::npos)
            {
                className = className.substr(0, lt);
            }

            if (!instantiationName.empty())
            {
                ret += m_fmt.tab(a_nestingLevel);
                ret += L"// reconstructed by ";
                ret += instantiationName;
                ret += L"\n";
            }

            ret += m_fmt.tab(a_nestingLevel);
            ret += m_ctx.templateInstantiation().decl;
            ret += L"\n";
            m_ctx.setTemplateHeaderDone(true);
        }

        ret += m_fmt.tab(a_nestingLevel);
        ret += m_fmt.modPrefix(a_symbol);
        ret += m_fmt.udtKeyword(a_symbol);
        ret += className;

        ret += classInheritance(a_symbol);
        ret += m_fmt.scopeBegin(a_nestingLevel);

        // Push this class onto the scope stack (the full instantiated name so
        // constructor detection and member name resolution keep working).
        m_ctx.scope().push(scopeName);
        ret += m_host.dumpMembers(a_symbol, a_nestingLevel + 1);
        m_ctx.scope().pop();

        ret += m_fmt.scopeEnd(a_nestingLevel);
        ret += m_host.typeSources();

        return ret;
    }

    /// Dump an anonymous UDT (union/struct) as an inline block, without a name.
    /// Used when a data member's type is itself an anonymous union/struct
    /// (e.g. compiler-generated $HASH types wrapping bitfields).
    /// Emits "struct { ... };" or "union { ... };" with no variable name.
    std::wstring dumpAnonymousUDT(IDiaSymbol* a_udtSymbol, int a_nestingLevel)
    {
        std::wstring ret;

        ret += m_fmt.tab(a_nestingLevel);
        ret += m_fmt.modPrefix(a_udtSymbol);
        ret += m_fmt.udtKeyword(a_udtSymbol); // "struct " / "union " — no name follows

        ret += m_fmt.scopeBegin(a_nestingLevel);
        ret += m_host.dumpMembers(a_udtSymbol, a_nestingLevel + 1);
        ret += m_fmt.scopeEnd(a_nestingLevel);

        return ret;
    }

    std::wstring classInheritance(IDiaSymbol* a_symbol) const
    {
        std::wstring ret;
        ComPtr<IDiaEnumSymbols> baseEnum;

        bool isBegin = true;
        if (SUCCEEDED(a_symbol->findChildren(SymTagBaseClass, nullptr, nsNone, &baseEnum)))
        {
            ComPtr<IDiaSymbol> baseSymbol;
            ULONG celt = 0;
            while (SUCCEEDED(baseEnum->Next(1, &baseSymbol, &celt)) && celt == 1)
            {
                ret += isBegin ? L" : " : L", ";
                isBegin = false;

                auto access = TypeWalker::getAccessName(
                    baseSymbol.get(), m_ctx.config().m_baseAccessType);
                if (access)
                {
                    ret += access;
                    ret += L" ";
                }

                BOOL isVirtualBase = FALSE;
                baseSymbol->get_virtualBaseClass(&isVirtualBase);
                if (isVirtualBase)
                    ret += L"virtual ";

                ret += TypeWalker::getName(baseSymbol.get(), m_ctx.scope());
            }
        }
        return ret;
    }

private:

    DumpContext& m_ctx;
    DumpFormatter& m_fmt;
    IDumpCoordinator& m_host;
};
