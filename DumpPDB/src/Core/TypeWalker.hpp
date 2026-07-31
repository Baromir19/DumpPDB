#pragma once

#include <dia2.h>
#include <string>
#include <vector>

#include <Util/Com/ComPtr.hpp>
#include <Core/TypeBuilder.hpp>

/// Integer style for base type names.
enum class IntStyle
{
    MsvcNative,  // __int32, __int64, etc.
    Cstdint      // int32_t, int64_t, etc.
};

inline bool isValidIntStyle(long a_value) noexcept
{
    return a_value >= static_cast<long>(IntStyle::MsvcNative)
        && a_value <= static_cast<long>(IntStyle::Cstdint);
}

/// Scope context that tracks the current nested class/struct hierarchy.
/// Used to strip the current scope prefix from DIA symbol names.
struct ScopeContext
{
    std::vector<std::wstring> m_parts;

    void push(const std::wstring& a_name)
    {
        m_parts.push_back(a_name);
    }

    void pop()
    {
        if (!m_parts.empty()) { m_parts.pop_back(); }
    }

    std::wstring top() const { return m_parts.empty() ? L"" : m_parts.back(); }

    std::wstring full() const
    {
        std::wstring result;

        for (size_t i = 0; i < m_parts.size(); ++i)
        {
            if (i > 0)
                result += L"::";

            result += m_parts[i];
        }

        return result;
    }

    bool empty() const
    {
        return m_parts.empty();
    }
};

/// A fully-qualified name split into namespace path + leaf name.
struct QualifiedName
{
    std::wstring ns;   // e.g. L"A::B" for L"A::B::Hello"; empty for L"Hello"
    std::wstring leaf; // e.g. L"Hello" for L"A::B::Hello"
};

/// Walks IDiaSymbol trees and builds TypeBuilder chains.

class TypeWalker
{
public:
    /// Returns true if a_symbol's lexical parent is SymTagExe (i.e. it's a true
    /// top-level symbol — global, or inside a namespace but NOT a nested class).
    static bool isTopLevelSymbol(IDiaSymbol* a_symbol)
    {
        ComPtr<IDiaSymbol> parent;
        if (FAILED(a_symbol->get_lexicalParent(&parent)) || !parent)
            return false;

        DWORD tag = SymTagNull;
        parent->get_symTag((DWORD*)&tag);
        return tag == SymTagExe;
    }

    /// Parses a fully-qualified name string into namespace path + leaf name.
    /// e.g. "User::Hello" -> ns="User",   leaf="Hello"
    ///      "A::B::Hello" -> ns="A::B",   leaf="Hello"
    ///      "Hello"       -> ns="",       leaf="Hello"
    static QualifiedName parseQualifiedName(const std::wstring& a_fullyQualifiedName)
    {
        QualifiedName result;

        auto lastSep = a_fullyQualifiedName.rfind(L"::");
        if (lastSep == std::wstring::npos)
        {
            result.leaf = a_fullyQualifiedName;
            return result;
        }

        result.ns   = a_fullyQualifiedName.substr(0, lastSep);
        result.leaf = a_fullyQualifiedName.substr(lastSep + 2);
        return result;
    }

    /// Parses a fully-qualified name from a DIA symbol into namespace path + leaf name.
    /// Only call this when isTopLevelSymbol(a_symbol) is true — for nested classes
    /// the "::" in the name refers to enclosing classes, not namespaces, and this
    /// function must NOT be used there (ScopeContext handles that case instead).
    /// Assumes that any qualifier of a top-level symbol is a namespace,
    /// since nested classes have lexicalParent != SymTagExe.
    static QualifiedName parseQualifiedName(IDiaSymbol* a_symbol)
    {
        QualifiedName result;

        BSTR bstrName = nullptr;
        if (FAILED(a_symbol->get_name(&bstrName)) || !bstrName)
            return result;

        std::wstring fullName(bstrName);
        SysFreeString(bstrName);

        return parseQualifiedName(fullName);
    }

    /// Get the base type name for a SymTagBaseType symbol.
    static const wchar_t* getBaseTypeName(IDiaSymbol* a_symbol, IntStyle a_intStyle = IntStyle::MsvcNative)
    {
        DWORD baseType = 0;
        ULONGLONG length = 0;

        if (SUCCEEDED(a_symbol->get_baseType(&baseType)))
        {
            a_symbol->get_length(&length);

            switch (baseType)
            {
            case btCurrency: return L"CY";
            case btDate: return L"DATE";
            case btVariant: return L"VARIANT";
            case btComplex: return L"std::complex";
            case btBSTR: return L"BSTR";
            case btHresult: return L"HRESULT";

            case btChar16: return L"char16_t";
            case btChar32: return L"char32_t";
            case btChar8: return L"char8_t";

            case btVoid: return L"void";

            case btFloat:
                switch (length)
                {
                case 4: return L"float";
                case 8: return L"double";
                case 0x10: return L"long double";
                default: return L"float";
                }

            case btBool: return L"bool";
            case btChar: return L"char";
            case btWChar: return L"wchar_t";

            case btInt:
                switch (length)
                {
                case 1: return a_intStyle == IntStyle::Cstdint ? L"int8_t"  : L"__int8";
                case 2: return a_intStyle == IntStyle::Cstdint ? L"int16_t" : L"__int16";
                case 4: return a_intStyle == IntStyle::Cstdint ? L"int32_t" : L"__int32";
                case 8: return a_intStyle == IntStyle::Cstdint ? L"int64_t" : L"__int64";
                default: return L"int";
                }

            case btUInt:
                switch (length)
                {
                case 1: return a_intStyle == IntStyle::Cstdint ? L"uint8_t"  : L"unsigned __int8";
                case 2: return a_intStyle == IntStyle::Cstdint ? L"uint16_t" : L"unsigned __int16";
                case 4: return a_intStyle == IntStyle::Cstdint ? L"uint32_t" : L"unsigned __int32";
                case 8: return a_intStyle == IntStyle::Cstdint ? L"uint64_t" : L"unsigned __int64";
                default: return L"unsigned int";
                }

            case btLong: return L"long";
            case btULong: return L"unsigned long";

            case btBCD:
            case btBit:
            case btNoType:
            default: break;
            }
        }
        return nullptr;
    }

    static const wchar_t* getUDTKindName(IDiaSymbol* a_symbol)
    {
        DWORD _udt;
        if (SUCCEEDED(a_symbol->get_udtKind(&_udt)))
        {
            switch (_udt)
            {
            case UdtStruct: return L"struct";
            case UdtClass: return L"class";
            case UdtUnion: return L"union";
            case UdtInterface: return L"interface";
            default: return nullptr;
            }
        }
        return nullptr;
    }

    /// Build a TypeBuilder chain by recursively walking the DIA type tree.
    /// Returns a TypeBuilder populated with the full type chain.
    /// @param a_stripScope Controls whether current scope prefix is stripped from names
    ///                     (corresponds to DumpConfig::m_showNonScoped).
    static TypeBuilder resolveType(
        IDiaSymbol* a_symbol, 
        const ScopeContext& a_scope = ScopeContext(),
        bool a_stripScope = true,
        IntStyle a_intStyle = IntStyle::MsvcNative
    )
    {
        TypeBuilder builder;

        if (!a_symbol) return builder;

        DWORD symTag = SymTagNull;
        a_symbol->get_symTag((DWORD*)&symTag);

        // Get qualifiers
        BOOL isConst = FALSE;
        BOOL isVolatile = FALSE;
        a_symbol->get_constType(&isConst);
        a_symbol->get_volatileType(&isVolatile);

        // Get name (strip scope based on a_stripScope parameter)
        std::wstring name = getName(a_symbol, a_scope, a_stripScope);

        // Recurse into sub-type first (inner types are built first)
        ComPtr<IDiaSymbol> subType;
        if (SUCCEEDED(a_symbol->get_type(&subType)))
        {
            TypeBuilder subBuilder = resolveType(
                subType.get(), 
                a_scope, 
                a_stripScope,
                a_intStyle
            );
            // Merge sub-builder into this one (inner type becomes the builder state)
            builder = std::move(subBuilder);
        }

        // Apply this symbol's modifier and qualifiers.
        // Since we recurse first, we build from inner to outer:
        //   recursion builds the inner type, then we add the outer modifier.
        switch (symTag)
        {
        case SymTagBaseType:
        {
            if (auto baseName = getBaseTypeName(a_symbol, a_intStyle))
            {
                builder.base(baseName);
            }

            // const/volatile on BaseType applies to the base type itself
            // e.g. const int, volatile int
            if (isConst)
                builder.constQual();
            if (isVolatile)
                builder.volatileQual();
            break;
        }

        case SymTagPointerType:
        {
            BOOL isRef = FALSE;
            BOOL isRVRef = FALSE;
            a_symbol->get_reference(&isRef);
            a_symbol->get_RValueReference(&isRVRef);

            if (isRVRef)    { builder.rvalueReference(); }
            else if (isRef) { builder.reference(); }
            else            { builder.pointer(); }

            if (isConst)    { builder.constPointer(); }
            if (isVolatile) { builder.volatilePointer(); }
            break;
        }

        case SymTagArrayType:
        {
            DWORD count = 0;
            if (SUCCEEDED(a_symbol->get_count(&count)) && count != 0xFFFFFFFC)
            {
                builder.array(count);
            }
            else
            {
                builder.array(0);
            }
            break;
        }

        case SymTagFunctionType:
        {
            std::wstring args = getFuncArgsString(a_symbol, a_scope, a_stripScope);
            builder.function(std::move(args));
            break;
        }

        case SymTagData:
        {
            if (!name.empty()) { builder.name(name); }

            // Bit field
            DWORD bitPos = 0;
            ULONGLONG bitLen = 0;
            if (SUCCEEDED(a_symbol->get_bitPosition(&bitPos)) &&
                SUCCEEDED(a_symbol->get_length(&bitLen)) &&
                bitLen > 0 && bitLen != MAXULONGLONG)
            {
                builder.bitField(bitPos, bitLen);
            }
            break;
        }

        case SymTagUDT:
        case SymTagEnum:
        {
            if (!name.empty()) { builder.base(name); }
            if (isConst) { builder.constQual(); }
            if (isVolatile) { builder.volatileQual(); }
            break;
        }

        case SymTagTypedef:
        {
            if (!name.empty()) { builder.base(name); }
            if (isConst) { builder.constQual(); }
            if (isVolatile) { builder.volatileQual(); }
            break;
        }

        default:
            break;
        }

        return builder;
    }

    /// Get function arguments as a comma-separated string.
    static std::wstring getFuncArgsString(
        IDiaSymbol* a_symbol, 
        const ScopeContext& a_scope = ScopeContext(),
        bool a_stripScope = true, 
        IntStyle a_intStyle = IntStyle::MsvcNative
    )
    {
        std::wstring result;
        bool isFirst = true;

        ComPtr<IDiaEnumSymbols> enum_symbolsParams;
        if (SUCCEEDED(a_symbol->findChildren(SymTagFunctionArgType, nullptr, nsNone, &enum_symbolsParams)) && enum_symbolsParams)
        {
            ComPtr<IDiaSymbol> child;
            ULONG celt = 0;
            while (SUCCEEDED(enum_symbolsParams->Next(1, &child, &celt)) && celt == 1)
            {
                ComPtr<IDiaSymbol> _argType;
                if (SUCCEEDED(child->get_type(&_argType)) && _argType)
                {
                    if (!isFirst) { result += L", "; }
                    result += resolveType(_argType.get(), a_scope, a_stripScope, a_intStyle).build();
                    isFirst = false;
                }
            }
        }

        return result;
    }

    /// Check if a name is a compiler-generated synthetic name (anonymous or hash-based).
    static bool isSyntheticName(const std::wstring& a_name)
    {
        return a_name.empty()
            || a_name == L"<unnamed-tag>"
            || (a_name.size() > 0 && a_name[0] == L'$');
    }

    /// Get the name of a symbol, optionally stripping the current scope prefix.
    /// @param a_symbol        The DIA symbol to get the name from.
    /// @param a_scope         The current scope context (stack of enclosing class names).
    /// @param a_stripScope    If true (default), strips the current scope prefix from the name.
    ///                         Controls the "m_showNonScoped" behavior: when true, only the
    ///                         short/non-scoped name is returned. When false, the full scoped
    ///                         name (e.g. "ParentClass::Child") is preserved.
    static std::wstring getName(
        IDiaSymbol* a_symbol, 
        const ScopeContext& a_scope = ScopeContext(),
        bool a_stripScope = true
    )
    {
        BSTR bstrName = nullptr;
        if (SUCCEEDED(a_symbol->get_name(&bstrName)) && bstrName)
        {
            std::wstring name(bstrName);
            SysFreeString(bstrName);

            if (a_stripScope && !a_scope.empty())
            {
                const std::wstring scope = a_scope.full();
                const std::wstring prefix = scope + L"::";

                if (name.size() > prefix.size() &&
                    name.compare(0, prefix.size(), prefix) == 0)
                {
                    return name.substr(prefix.size());
                }
            }

            return name;
        }
        return L"";
    }

    static std::wstring leafName(const std::wstring& a_qualifiedName)
    {
        auto pos = a_qualifiedName.rfind(L"::");
        return (pos == std::wstring::npos) ? a_qualifiedName : a_qualifiedName.substr(pos + 2);
    }

    /// Check if a symbol is an anonymous union/struct (empty name + UDT kind)
    /// or has a compiler-generated synthetic name.
    static bool isAnonymousUDT(IDiaSymbol* a_symbol)
    {
        BSTR bstrName = nullptr;
        std::wstring name;
        bool gotName = SUCCEEDED(a_symbol->get_name(&bstrName)) && bstrName;
        if (gotName)
        {
            name = bstrName;
            SysFreeString(bstrName);
        }

        // Check for synthetic/anonymous names
        if (!gotName || isSyntheticName(name))
        {
            DWORD symTag = SymTagNull;
            a_symbol->get_symTag((DWORD*)&symTag);

            if (symTag != SymTagUDT) return false;

            DWORD udtKind = 0;
            a_symbol->get_udtKind(&udtKind);
            return (udtKind == UdtStruct || udtKind == UdtUnion);
        }

        return false;
    }

    /// Get the access specifier as a string.
    static const wchar_t* getAccessName(IDiaSymbol* a_symbol, DWORD abaseAccessType = 0)
    {
        DWORD access = 0;
        if (SUCCEEDED(a_symbol->get_access(&access)))
        {
            if (abaseAccessType) { access = abaseAccessType; }

            switch (access)
            {
            case CV_private:   return L"private";
            case CV_protected: return L"protected";
            case CV_public:    return L"public";
            default: return nullptr;
            }
        }
        return nullptr;
    }

    /// C++ calling convention enum.
    enum class CallingConvention
    {
        Unknown,
        Cdecl,
        Fastcall,
        Stdcall,
        Thiscall,
        Syscall,
        Clrcall
    };

    /// Get the calling convention from a DIA symbol.
    static CallingConvention getCallingConvention(IDiaSymbol* a_symbol)
    {
        DWORD _cc = 0;
        if (SUCCEEDED(a_symbol->get_callingConvention(&_cc)))
        {
            switch (_cc)
            {
            case CV_CALL_NEAR_C:    return CallingConvention::Cdecl;
            case CV_CALL_NEAR_FAST: return CallingConvention::Fastcall;
            case CV_CALL_NEAR_STD:  return CallingConvention::Stdcall;
            case CV_CALL_NEAR_SYS:  return CallingConvention::Syscall;
            case CV_CALL_THISCALL:  return CallingConvention::Thiscall;
            case CV_CALL_CLRCALL:   return CallingConvention::Clrcall;
            default:                return CallingConvention::Unknown;
            }
        }
        return CallingConvention::Unknown;
    }

    /// Render a calling convention enum to its C++ keyword string.
    static const wchar_t* renderCallingConvention(CallingConvention a_cc)
    {
        switch (a_cc)
        {
        case CallingConvention::Cdecl:    return L"__cdecl";
        case CallingConvention::Fastcall: return L"__fastcall";
        case CallingConvention::Stdcall:  return L"__stdcall";
        case CallingConvention::Syscall:  return L"__syscall";
        case CallingConvention::Thiscall: return L"__thiscall";
        case CallingConvention::Clrcall:  return L"__clrcall";
        default:                          return nullptr;
        }
    }
};