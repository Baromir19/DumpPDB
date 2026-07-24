#pragma once

#include <dia2.h>
#include <string>
#include <vector>

#include <Util/Com/ComPtr.hpp>
#include <Core/TypeBuilder.hpp>

/// Walks IDiaSymbol trees and builds TypeBuilder chains.
/// Separated from DiaManager: this class only knows about DIA symbols and TypeBuilder.
/// It does NOT know about ConsoleManager, output, or formatting.

class TypeWalker
{
public:
    /// Get the base type name for a SymTagBaseType symbol.
    static const wchar_t* getBaseTypeName(IDiaSymbol* a_symbol)
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
                case 1: return L"__int8";
                case 2: return L"__int16";
                case 4: return L"__int32";
                case 8: return L"__int64";
                default: return L"int";
                }

            case btUInt:
                switch (_length)
                {
                case 1: return L"unsigned __int8";
                case 2: return L"unsigned __int16";
                case 4: return L"unsigned __int32";
                case 8: return L"unsigned __int64";
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
    static TypeBuilder resolveType(IDiaSymbol* a_symbol, const std::wstring& a_parentClassName = L"")
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

        // Get name
        std::wstring _name = getName(a_symbol, a_parentClassName);

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

            TypeBuilder _subBuilder = resolveType(_subType.get(), a_parentClassName);
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
            std::wstring _args = getFuncArgsString(a_symbol);
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
    static std::wstring getFuncArgsString(IDiaSymbol* a_symbol)
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
                    _result += resolveType(_argType.get()).build();
                    _isFirst = false;
                }
            }
        }

        return _result;
    }

    /// Get the name of a symbol, optionally stripping the parent scope.
    static std::wstring getName(IDiaSymbol* a_symbol, const std::wstring& a_parentClassName = L"")
    {
        BSTR _bstrName = nullptr;
        if (SUCCEEDED(a_symbol->get_name(&_bstrName)) && _bstrName)
        {
            std::wstring _ret(_bstrName);
            SysFreeString(_bstrName);

            // Strip parent scope if requested
            if (!a_parentClassName.empty() && _ret.find(a_parentClassName + L"::") == 0)
            {
                _ret = _ret.substr(a_parentClassName.size() + 2);
            }

            return _ret;
        }
        return L"";
    }

    /// Check if a symbol is an anonymous union/struct (empty name + UDT kind).
    static bool isAnonymousUDT(IDiaSymbol* a_symbol)
    {
        BSTR _bstrName = nullptr;
        bool _hasName = SUCCEEDED(a_symbol->get_name(&_bstrName)) && _bstrName && wcslen(_bstrName) > 0;
        if (_bstrName) SysFreeString(_bstrName);

        if (_hasName) return false;

        DWORD _symTag = SymTagNull;
        a_symbol->get_symTag((DWORD*)&_symTag);

        if (_symTag != SymTagUDT) return false;

        DWORD _udtKind = 0;
        a_symbol->get_udtKind(&_udtKind);
        return (_udtKind == UdtStruct || _udtKind == UdtUnion);
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

    /// Get the calling convention name.
    static const wchar_t* getCallingConvention(IDiaSymbol* a_symbol)
    {
        DWORD _cc = 0;
        if (SUCCEEDED(a_symbol->get_callingConvention(&_cc)))
        {
            switch (_cc)
            {
            case CV_CALL_NEAR_C:    return L"__cdecl";
            case CV_CALL_NEAR_FAST: return L"__fastcall";
            case CV_CALL_NEAR_STD:  return L"__stdcall";
            case CV_CALL_NEAR_SYS:  return L"__syscall";
            case CV_CALL_THISCALL:  return L"__thiscall";
            case CV_CALL_CLRCALL:   return L"__clrcall";
            default:                return nullptr;
            }
        }
        return nullptr;
    }
};