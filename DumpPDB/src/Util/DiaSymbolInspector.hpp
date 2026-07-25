#pragma once

#include <dia2.h>
#include <string>
#include <iostream>

#include <Util/Com/ComPtr.hpp>

class DiaSymbolInspector
{
public:

    static void dump(IDiaSymbol* a_symbol, int a_indent = 0)
    {
        if (!a_symbol)
        {
            printIndent(a_indent);
            wprintf(L"<NULL SYMBOL>\n");
            return;
        }

        DWORD _symTag = 0;
        a_symbol->get_symTag(&_symTag);

        printIndent(a_indent);
        wprintf(
            L"============================================================\n"
        );

        printIndent(a_indent);
        wprintf(L"SYMBOL\n");

        printIndent(a_indent);
        wprintf(L"  symTag:        %lu (%s)\n",
            _symTag,
            symTagName(_symTag));

        dumpName(a_symbol, a_indent);
        dumpType(a_symbol, a_indent);
        dumpParent(a_symbol, a_indent);
        dumpData(a_symbol, a_indent);
        dumpLocation(a_symbol, a_indent);
        dumpLayout(a_symbol, a_indent);
        dumpFlags(a_symbol, a_indent);
        dumpFunction(a_symbol, a_indent);
        dumpUDT(a_symbol, a_indent);
        dumpValue(a_symbol, a_indent);

        printIndent(a_indent);
        wprintf(L"============================================================\n");
    }

private:

    static void printIndent(int a_indent)
    {
        for (int i = 0; i < a_indent; ++i)
            wprintf(L"    ");
    }

    static const wchar_t* symTagName(DWORD a_tag)
    {
        switch (a_tag)
        {
        case SymTagNull:            return L"Null";
        case SymTagExe:             return L"Exe";
        case SymTagCompiland:       return L"Compiland";
        case SymTagCompilandDetails:return L"CompilandDetails";
        case SymTagCompilandEnv:    return L"CompilandEnv";
        case SymTagFunction:        return L"Function";
        case SymTagBlock:           return L"Block";
        case SymTagData:            return L"Data";
        case SymTagAnnotation:      return L"Annotation";
        case SymTagLabel:           return L"Label";
        case SymTagPublicSymbol:    return L"PublicSymbol";
        case SymTagUDT:             return L"UDT";
        case SymTagEnum:            return L"Enum";
        case SymTagFunctionType:    return L"FunctionType";
        case SymTagPointerType:     return L"PointerType";
        case SymTagArrayType:       return L"ArrayType";
        case SymTagBaseType:        return L"BaseType";
        case SymTagTypedef:         return L"Typedef";
        case SymTagBaseClass:       return L"BaseClass";
        case SymTagFriend:          return L"Friend";
        case SymTagFunctionArgType: return L"FunctionArgType";
        case SymTagVTableShape:     return L"VTableShape";
        case SymTagVTable:          return L"VTable";
        case SymTagCustom:          return L"Custom";
        case SymTagThunk:           return L"Thunk";
        case SymTagCustomType:      return L"CustomType";
        case SymTagManagedType:     return L"ManagedType";
        case SymTagDimension:       return L"Dimension";
        default:                    return L"Unknown";
        }
    }

    static const wchar_t* dataKindName(DWORD a_kind)
    {
        switch (a_kind)
        {
        case DataIsUnknown:       return L"Unknown";
        case DataIsLocal:         return L"Local";
        case DataIsStaticLocal:   return L"StaticLocal";
        case DataIsParam:         return L"Param";
        case DataIsObjectPtr:     return L"ObjectPtr";
        case DataIsFileStatic:    return L"FileStatic";
        case DataIsGlobal:        return L"Global";
        case DataIsMember:        return L"Member";
        case DataIsStaticMember:  return L"StaticMember";
        case DataIsConstant:      return L"Constant";
        default:                  return L"Unknown";
        }
    }

    static const wchar_t* locationTypeName(DWORD a_type)
    {
        switch (a_type)
        {
        case LocIsNull:        return L"Null";
        case LocIsStatic:      return L"Static";
        case LocIsTLS:         return L"TLS";
        case LocIsRegRel:      return L"RegRel";
        case LocIsThisRel:     return L"ThisRel";
        case LocIsEnregistered:return L"Enregistered";
        case LocIsBitField:    return L"BitField";
        case LocIsSlot:        return L"Slot";
        case LocIsIlRel:       return L"IlRel";
        case LocInMetaData:    return L"MetaData";
        case LocIsConstant:    return L"Constant";
        default:               return L"Unknown";
        }
    }

    static const wchar_t* udtKindName(DWORD a_kind)
    {
        switch (a_kind)
        {
        case UdtStruct: return L"Struct";
        case UdtClass:  return L"Class";
        case UdtUnion:  return L"Union";
        default:        return L"Unknown";
        }
    }

    static const wchar_t* accessName(DWORD a_access)
    {
        switch (a_access)
        {
        case CV_private:   return L"private";
        case CV_protected: return L"protected";
        case CV_public:    return L"public";
        default:           return L"unknown";
        }
    }

    static void dumpName(IDiaSymbol* a_symbol, int a_indent)
    {
        BSTR _name = nullptr;

        if (SUCCEEDED(a_symbol->get_name(&_name)) && _name)
        {
            printIndent(a_indent);
            wprintf(L"name:           \"%s\"\n", _name);

            SysFreeString(_name);
        }
        else
        {
            printIndent(a_indent);
            wprintf(L"name:           <none>\n");
        }
    }

    static void dumpType(IDiaSymbol* a_symbol, int a_indent)
    {
        ComPtr<IDiaSymbol> _type;

        if (SUCCEEDED(a_symbol->get_type(&_type)) && _type)
        {
            DWORD _tag = 0;
            _type->get_symTag(&_tag);

            BSTR _name = nullptr;
            _type->get_name(&_name);

            printIndent(a_indent);
            wprintf(
                L"type:           tag=%lu (%s), name=\"%s\"\n",
                _tag,
                symTagName(_tag),
                _name ? _name : L"<none>"
            );

            if (_name)
                SysFreeString(_name);
        }
        else
        {
            printIndent(a_indent);
            wprintf(L"type:           <none>\n");
        }
    }

    static void dumpParent(IDiaSymbol* a_symbol, int a_indent)
    {
        ComPtr<IDiaSymbol> _parent;

        if (SUCCEEDED(a_symbol->get_classParent(&_parent)) && _parent)
        {
            BSTR _name = nullptr;
            _parent->get_name(&_name);

            printIndent(a_indent);
            wprintf(
                L"classParent:    \"%s\"\n",
                _name ? _name : L"<anonymous>"
            );

            if (_name)
                SysFreeString(_name);
        }
        else
        {
            printIndent(a_indent);
            wprintf(L"classParent:    <none>\n");
        }

        _parent.Release();

        if (SUCCEEDED(a_symbol->get_lexicalParent(&_parent)) && _parent)
        {
            BSTR _name = nullptr;
            _parent->get_name(&_name);

            printIndent(a_indent);
            wprintf(
                L"lexicalParent:  \"%s\"\n",
                _name ? _name : L"<anonymous>"
            );

            if (_name)
                SysFreeString(_name);
        }
        else
        {
            printIndent(a_indent);
            wprintf(L"lexicalParent:  <none>\n");
        }
    }

    static void dumpData(IDiaSymbol* a_symbol, int a_indent)
    {
        DWORD _kind = 0;

        if (SUCCEEDED(a_symbol->get_dataKind(&_kind)))
        {
            printIndent(a_indent);
            wprintf(
                L"dataKind:       %lu (%s)\n",
                _kind,
                dataKindName(_kind)
            );
        }

        DWORD _access = 0;

        if (SUCCEEDED(a_symbol->get_access(&_access)))
        {
            printIndent(a_indent);
            wprintf(
                L"access:         %lu (%s)\n",
                _access,
                accessName(_access)
            );
        }

        BOOL _isStatic = FALSE;

        if (SUCCEEDED(a_symbol->get_isStatic(&_isStatic)))
        {
            printIndent(a_indent);
            wprintf(
                L"isStatic:       %s\n",
                _isStatic ? L"true" : L"false"
            );
        }

        BOOL _isConst = FALSE;

        if (SUCCEEDED(a_symbol->get_constType(&_isConst)))
        {
            printIndent(a_indent);
            wprintf(
                L"isConst:        %s\n",
                _isConst ? L"true" : L"false"
            );
        }

        BOOL _isVolatile = FALSE;

        if (SUCCEEDED(a_symbol->get_volatileType(&_isVolatile)))
        {
            printIndent(a_indent);
            wprintf(
                L"isVolatile:     %s\n",
                _isVolatile ? L"true" : L"false"
            );
        }
    }

    static void dumpLocation(IDiaSymbol* a_symbol, int a_indent)
    {
        DWORD _locationType = 0;

        if (SUCCEEDED(a_symbol->get_locationType(&_locationType)))
        {
            printIndent(a_indent);
            wprintf(
                L"locationType:   %lu (%s)\n",
                _locationType,
                locationTypeName(_locationType)
            );
        }

        LONG _offset = 0;

        if (SUCCEEDED(a_symbol->get_offset(&_offset)))
        {
            printIndent(a_indent);
            wprintf(
                L"offset:         %ld (0x%lX)\n",
                _offset,
                _offset
            );
        }

        DWORD _bitPosition = 0;

        if (SUCCEEDED(a_symbol->get_bitPosition(&_bitPosition)))
        {
            printIndent(a_indent);
            wprintf(
                L"bitPosition:    %lu (0x%lX)\n",
                _bitPosition,
                _bitPosition
            );
        }

        ULONGLONG _length = 0;

        if (SUCCEEDED(a_symbol->get_length(&_length)))
        {
            printIndent(a_indent);
            wprintf(
                L"length:         %llu bytes\n",
                _length
            );
        }
    }

    static void dumpLayout(IDiaSymbol* a_symbol, int a_indent)
    {
        DWORD _rank = 0;

        if (SUCCEEDED(a_symbol->get_rank(&_rank)))
        {
            printIndent(a_indent);
            wprintf(L"rank:           %lu\n", _rank);
        }

        DWORD _count = 0;

        if (SUCCEEDED(a_symbol->get_count(&_count)))
        {
            printIndent(a_indent);
            wprintf(L"count:          %lu\n", _count);
        }

        DWORD _stride = 0;

        if (SUCCEEDED(a_symbol->get_stride(&_stride)))
        {
            printIndent(a_indent);
            wprintf(L"stride:         %lu\n", _stride);
        }
    }

    static void dumpFlags(IDiaSymbol* a_symbol, int a_indent)
    {
        BOOL _isVirtual = FALSE;

        if (SUCCEEDED(a_symbol->get_virtual(&_isVirtual)))
        {
            printIndent(a_indent);
            wprintf(
                L"isVirtual:      %s\n",
                _isVirtual ? L"true" : L"false"
            );
        }

        BOOL _isPure = FALSE;

        if (SUCCEEDED(a_symbol->get_pure(&_isPure)))
        {
            printIndent(a_indent);
            wprintf(
                L"isPure:         %s\n",
                _isPure ? L"true" : L"false"
            );
        }

        BOOL _isIntroVirtual = FALSE;

        if (SUCCEEDED(a_symbol->get_intro(&_isIntroVirtual)))
        {
            printIndent(a_indent);
            wprintf(
                L"isIntroVirtual: %s\n",
                _isIntroVirtual ? L"true" : L"false"
            );
        }
    }

    static void dumpFunction(IDiaSymbol* a_symbol, int a_indent)
    {
        DWORD _vtableOffset = 0;

        if (SUCCEEDED(a_symbol->get_virtualBaseOffset(&_vtableOffset)))
        {
            printIndent(a_indent);
            wprintf(
                L"virtualOffset:  0x%lX\n",
                _vtableOffset
            );
        }
    }

    static void dumpUDT(IDiaSymbol* a_symbol, int a_indent)
    {
        DWORD _udtKind = 0;

        if (SUCCEEDED(a_symbol->get_udtKind(&_udtKind)))
        {
            printIndent(a_indent);
            wprintf(
                L"udtKind:        %lu (%s)\n",
                _udtKind,
                udtKindName(_udtKind)
            );
        }

        BOOL _isNested = FALSE;

        if (SUCCEEDED(a_symbol->get_nested(&_isNested)))
        {
            printIndent(a_indent);
            wprintf(
                L"isNested:       %s\n",
                _isNested ? L"true" : L"false"
            );
        }
    }

    static void dumpValue(IDiaSymbol* a_symbol, int a_indent)
    {
        VARIANT _value;
        VariantInit(&_value);

        if (SUCCEEDED(a_symbol->get_value(&_value)))
        {
            printIndent(a_indent);
            wprintf(L"value:          ");

            switch (_value.vt)
            {
            case VT_I1:
                wprintf(L"%d", _value.cVal);
                break;

            case VT_UI1:
                wprintf(L"%u", _value.bVal);
                break;

            case VT_I2:
                wprintf(L"%d", _value.iVal);
                break;

            case VT_UI2:
                wprintf(L"%u", _value.uiVal);
                break;

            case VT_I4:
                wprintf(L"%ld", _value.lVal);
                break;

            case VT_UI4:
                wprintf(L"%lu", _value.ulVal);
                break;

            case VT_I8:
                wprintf(L"%lld", _value.llVal);
                break;

            case VT_UI8:
                wprintf(L"%llu", _value.ullVal);
                break;

            default:
                wprintf(L"<VARIANT type %u>", _value.vt);
                break;
            }

            wprintf(L"\n");

            VariantClear(&_value);
        }
    }
};