#pragma once

#include <string>

#include <Core/Dump/DumpContext.hpp>

/// Scope-stack control: tracks the current namespace/class hierarchy so DIA
/// symbol names can be rendered relative to the current scope.
class ScopeTracker
{
public:

    explicit ScopeTracker(DumpContext& a_ctx)
        : m_ctx(a_ctx)
    {
    }

    void pushScope(const std::wstring& a_name)
    {
        m_ctx.scope().push(a_name);
    }

    void popScope()
    {
        m_ctx.scope().pop();
    }

    /// Push a fully-qualified namespace onto the scope stack one part at a time.
    /// e.g. pushQualifiedScope(L"User::Math") pushes "User" then "Math",
    /// so the scope stack remains a flat list of individual scope parts.
    void pushQualifiedScope(const std::wstring& a_qualifiedName)
    {
        size_t start = 0;
        while (start <= a_qualifiedName.size())
        {
            auto sep = a_qualifiedName.find(L"::", start);
            if (sep == std::wstring::npos)
            {
                pushScope(a_qualifiedName.substr(start));
                break;
            }
            pushScope(a_qualifiedName.substr(start, sep - start));
            start = sep + 2;
        }
    }

    void popQualifiedScope(size_t a_partCount)
    {
        for (size_t i = 0; i < a_partCount; ++i)
        {
            popScope();
        }
    }

private:

    DumpContext& m_ctx;
};
