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

/// Walks IDiaSymbol trees and builds TypeBuilder chains.
/// Separated from DiaManager: this class only knows about DIA symbols and TypeBuilder.
/// It does NOT know about ConsoleManager, output, or formatting.

class TypeWalker
{
public:
    /// Get the base type name for a SymTagBaseType symbol.
    static const wchar_t* getBaseTypeName(IDiaSymbol* a_symbol, IntStyle a_intStyle = IntStyle::MsvcNative)
    {
        DWORD _baseType = 0;
        ULONGLONG _length = 0;

        if (SUCCEEDED(a_symbol->get_baseType(&_baseType)))
        {
            a_symbol->get_length(&_length);

            switch (_baseType)
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
                switch (_length)
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
                switch (_length)
                {
                case 1: return a_intStyle == IntStyle::Cstdint ? L"int8_t"  : L"__int8";
                case 2: return a_intStyle == IntStyle::Cstdint ? L"int16_t" : L"__int16";
                case 4: return a_intStyle == IntStyle::Cstdint ? L"int32_t" : L"__int32";
                case 8: return a_intStyle == IntStyle::Cstdint ? L"int64_t" : L"__int64";
                default: return L"int";
                }

            case btUInt:
                switch (_length)
                {
                case 1: return a_intStyle == IntStyle::Cstdint ? L"uint8_t"        : L"unsigned __int8";
                case 2: return a_intStyle == IntStyle::Cstdint ? L"uint16_t"       : L"unsigned __int16";
                case 4: return a_intStyle == IntStyle::Cstdint ? L"uint32_t"       : L"unsigned __int32";
                case 8: return a_intStyle == IntStyle::Cstdint ? L"uint64_t"       : L"unsigned __int64";
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
    ///                     (corresponds to DumpConfig::showNonScoped).
    static TypeBuilder resolveType(IDiaSymbol* a_symbol, const std::wstring& a_parentClassName = L"",
                                    bool a_stripScope = true)
    {
        TypeBuilder _builder;

        if (!a_symbol) return _builder;

        DWORD _symTag = SymTagNull;
        a_symbol->get_symTag((DWORD*)&_symTag);

        // Get qualifiers
        BOOL _isConst = FALSE;
        BOOL _isVolatile = FALSE;
        a_symbol->get_constType(&_isConst);
        a_symbol->get_volatileType(&_isVolatile);

        // Get name (strip scope based on a_stripScope parameter)
        std::wstring _name = getName(a_symbol, a_parentClassName, a_stripScope);

        // Get sub-type (recursive)
        ComPtr<IDiaSymbol> _subType;
        if (SUCCEEDED(a_symbol->get_type(&_subType)))
        {
            // For pointer/array, const/volatile apply to the pointed-to type
            if (_symTag == SymTagPointerType || _symTag == SymTagArrayType)
            {
                if (_isConst) _builder.constPointed();
                // Don't set const/volatile on the pointer itself
            }
            else
            {
                if (_isConst) _builder.constQual();
                if (_isVolatile) _builder.volatileQual();
            }

            TypeBuilder _subBuilder = resolveType(_subType.get(), a_parentClassName, a_stripScope);
            // Merge sub-builder into this one
            _builder = std::move(_subBuilder);
        }
        else
        {
            // No sub-type: this is the base
            if (_isConst) _builder.constQual();
            if (_isVolatile) _builder.volatileQual();
        }

        // Apply this symbol's modifier
        switch (_symTag)
        {
        case SymTagBaseType:
            if (auto _baseName = getBaseTypeName(a_symbol))
            {
                _builder.base(_baseName);
            }
            break;

        case SymTagPointerType:
        {
            BOOL _isRef = FALSE;
            a_symbol->get_reference(&_isRef);
            if (_isRef)
            {
                _builder.reference();
            }
            else
            {
                _builder.pointer();
            }
            break;
        }

        case SymTagArrayType:
        {
            DWORD _count = 0;
            if (SUCCEEDED(a_symbol->get_count(&_count)) && _count != 0xFFFFFFFC)
            {
                _builder.array(_count);
            }
            else
            {
                _builder.array(0);
            }
            break;
        }

        case SymTagFunctionType:
        {
            std::wstring _args = getFuncArgsString(a_symbol, a_stripScope);
            _builder.function(std::move(_args));
            break;
        }

        case SymTagData:
        {
            if (!_name.empty()) { _builder.name(_name); }

            // Bit field
            DWORD _bitPos = 0;
            ULONGLONG _bitLen = 0;
            if (SUCCEEDED(a_symbol->get_bitPosition(&_bitPos)) &&
                SUCCEEDED(a_symbol->get_length(&_bitLen)) &&
                _bitLen > 0 && _bitLen != MAXULONGLONG)
            {
                _builder.bitField(_bitPos, _bitLen);
            }
            break;
        }

        case SymTagUDT:
        case SymTagEnum:
        {
            if (!_name.empty()) { _builder.base(_name); }
            break;
        }

        case SymTagTypedef:
        {
            if (!_name.empty()) { _builder.base(_name); }
            break;
        }

        default:
            break;
        }

        return _builder;
    }

    /// Get function arguments as a comma-separated string.
    static std::wstring getFuncArgsString(IDiaSymbol* a_symbol, bool a_stripScope = true)
    {
        std::wstring _result;
        bool _isFirst = true;

        ComPtr<IDiaEnumSymbols> _enumParams;
        if (SUCCEEDED(a_symbol->findChildren(SymTagFunctionArgType, nullptr, nsNone, &_enumParams)) && _enumParams)
        {
            ComPtr<IDiaSymbol> _child;
            ULONG _celt = 0;
            while (SUCCEEDED(_enumParams->Next(1, &_child, &_celt)) && _celt == 1)
            {
                ComPtr<IDiaSymbol> _argType;
                if (SUCCEEDED(_child->get_type(&_argType)) && _argType)
                {
                    if (!_isFirst) { _result += L", "; }
                    _result += resolveType(_argType.get(), L"", a_stripScope).build();
                    _isFirst = false;
                }
            }
        }

        return _result;
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
    ///                         Controls the "showNonScoped" behavior: when true, only the
    ///                         short/non-scoped name is returned. When false, the full scoped
    ///                         name (e.g. "ParentClass::Child") is preserved.
    static std::wstring getName(
        IDiaSymbol* a_symbol, 
        const std::wstring& a_parentClassName = L"",
        bool a_stripScope = true
    )
    {
        BSTR _bstrName = nullptr;
        if (SUCCEEDED(a_symbol->get_name(&_bstrName)) && _bstrName)
        {
            std::wstring _ret(_bstrName);
            SysFreeString(_bstrName);

            if (a_stripScope)
            {
                bool _isCleanIdentifier = !_ret.empty() &&
                    _ret.find_first_of(L"<>*&()[] ") == std::wstring::npos;

                if (_isCleanIdentifier)
                {
                    auto _pos = _ret.rfind(L"::");
                    if (_pos != std::wstring::npos)
                    {
                        _ret = _ret.substr(_pos + 2);
                    }
                }
            }

            return _ret;
        }
        return L"";
    }

    /// Check if a symbol is an anonymous union/struct (empty name + UDT kind)
    /// or has a compiler-generated synthetic name.
    static bool isAnonymousUDT(IDiaSymbol* a_symbol)
    {
        BSTR _bstrName = nullptr;
        std::wstring _name;
        bool _gotName = SUCCEEDED(a_symbol->get_name(&_bstrName)) && _bstrName;
        if (_gotName)
        {
            _name = _bstrName;
            SysFreeString(_bstrName);
        }

        // Check for synthetic/anonymous names
        if (!_gotName || isSyntheticName(_name))
        {
            DWORD _symTag = SymTagNull;
            a_symbol->get_symTag((DWORD*)&_symTag);

            if (_symTag != SymTagUDT) return false;

            DWORD _udtKind = 0;
            a_symbol->get_udtKind(&_udtKind);
            return (_udtKind == UdtStruct || _udtKind == UdtUnion);
        }

        return false;
    }

    /// Get the access specifier as a string.
    static const wchar_t* getAccessName(IDiaSymbol* a_symbol, DWORD a_baseAccessType = 0)
    {
        DWORD _access = 0;
        if (SUCCEEDED(a_symbol->get_access(&_access)))
        {
            if (a_baseAccessType) { _access = a_baseAccessType; }

            switch (_access)
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