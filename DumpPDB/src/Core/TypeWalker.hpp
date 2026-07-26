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

/// Walks IDiaSymbol trees and builds TypeBuilder chains.

class TypeWalker
{
public:
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
    /// @param a_stripScope Controls whether parent scope prefix is stripped from names
    ///                     (corresponds to DumpConfig::m_showNonScoped).
    static TypeBuilder resolveType(
        IDiaSymbol* a_symbol, 
        const std::wstring& a_parentClassName = L"",
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
        std::wstring name = getName(a_symbol, a_parentClassName, a_stripScope);

        // Get sub-type (recursive)
        ComPtr<IDiaSymbol> subType;
        if (SUCCEEDED(a_symbol->get_type(&subType)))
        {
            // For pointer/array, const/volatile apply to the pointed-to type
            if (symTag == SymTagPointerType || symTag == SymTagArrayType)
            {
                if (isConst) builder.constPointed();
                // Don't set const/volatile on the pointer itself
            }
            else
            {
                if (isConst) builder.constQual();
                if (isVolatile) builder.volatileQual();
            }

            TypeBuilder subBuilder = resolveType(
                subType.get(), 
                a_parentClassName, 
                a_stripScope,
                a_intStyle
            );
            // Merge sub-builder into this one
            builder = std::move(subBuilder);
        }
        else
        {
            // No sub-type: this is the base
            if (isConst) builder.constQual();
            if (isVolatile) builder.volatileQual();
        }

        // Apply this symbol's modifier
        switch (symTag)
        {
        case SymTagBaseType:
            if (auto baseName = getBaseTypeName(a_symbol, a_intStyle))
            {
                builder.base(baseName);
            }
            break;

        case SymTagPointerType:
        {
            BOOL isRef = FALSE;
            a_symbol->get_reference(&isRef);
            if (isRef)
            {
                builder.reference();
            }
            else
            {
                builder.pointer();
            }
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
            std::wstring args = getFuncArgsString(a_symbol, a_stripScope);
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
            break;
        }

        case SymTagTypedef:
        {
            if (!name.empty()) { builder.base(name); }
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
                    result += resolveType(_argType.get(), L"", a_stripScope, a_intStyle).build();
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

    /// Get the name of a symbol, optionally stripping the parent scope.
    /// @param a_symbol        The DIA symbol to get the name from.
    /// @param a_parentClassName If non-empty, scopes the lookup (used for children).
    /// @param a_stripScope    If true (default), strips the parent scope prefix from the name.
    ///                         Controls the "m_showNonScoped" behavior: when true, only the
    ///                         short/non-scoped name is returned. When false, the full scoped
    ///                         name (e.g. "ParentClass::Child") is preserved.
    static std::wstring getName(
        IDiaSymbol* a_symbol, 
        const std::wstring& a_parentClassName = L"",
        bool a_stripScope = true
    )
    {
        BSTR bstrName = nullptr;
        if (SUCCEEDED(a_symbol->get_name(&bstrName)) && bstrName)
        {
            std::wstring ret(bstrName);
            SysFreeString(bstrName);

            if (a_stripScope)
            {
                bool isCleanIdentifier = !ret.empty() &&
                    ret.find_first_of(L"<>*&()[] ") == std::wstring::npos;

                if (isCleanIdentifier)
                {
                    auto pos = ret.rfind(L"::");
                    if (pos != std::wstring::npos)
                    {
                        ret = ret.substr(pos + 2);
                    }
                }
            }

            return ret;
        }
        return L"";
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