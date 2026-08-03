#pragma once

#include <dia2.h>
#include <string>
#include <iostream>

#include <Core/Util/Com/ComPtr.hpp>

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

        DWORD symTag = 0;
        a_symbol->get_symTag(&symTag);

        printIndent(a_indent);
        wprintf(L"============================================================\n");

        printIndent(a_indent);
        wprintf(L"SYMBOL\n");

        printIndent(a_indent);
        wprintf(L"  symTag:        %lu (%s)\n", symTag, symTagName(symTag));

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
        case SymTagNull:
            return L"Null";
        case SymTagExe:
            return L"Exe";
        case SymTagCompiland:
            return L"Compiland";
        case SymTagCompilandDetails:
            return L"CompilandDetails";
        case SymTagCompilandEnv:
            return L"CompilandEnv";
        case SymTagFunction:
            return L"Function";
        case SymTagBlock:
            return L"Block";
        case SymTagData:
            return L"Data";
        case SymTagAnnotation:
            return L"Annotation";
        case SymTagLabel:
            return L"Label";
        case SymTagPublicSymbol:
            return L"PublicSymbol";
        case SymTagUDT:
            return L"UDT";
        case SymTagEnum:
            return L"Enum";
        case SymTagFunctionType:
            return L"FunctionType";
        case SymTagPointerType:
            return L"PointerType";
        case SymTagArrayType:
            return L"ArrayType";
        case SymTagBaseType:
            return L"BaseType";
        case SymTagTypedef:
            return L"Typedef";
        case SymTagBaseClass:
            return L"BaseClass";
        case SymTagFriend:
            return L"Friend";
        case SymTagFunctionArgType:
            return L"FunctionArgType";
        case SymTagVTableShape:
            return L"VTableShape";
        case SymTagVTable:
            return L"VTable";
        case SymTagCustom:
            return L"Custom";
        case SymTagThunk:
            return L"Thunk";
        case SymTagCustomType:
            return L"CustomType";
        case SymTagManagedType:
            return L"ManagedType";
        case SymTagDimension:
            return L"Dimension";
        default:
            return L"Unknown";
        }
    }

    static const wchar_t* dataKindName(DWORD a_kind)
    {
        switch (a_kind)
        {
        case DataIsUnknown:
            return L"Unknown";
        case DataIsLocal:
            return L"Local";
        case DataIsStaticLocal:
            return L"StaticLocal";
        case DataIsParam:
            return L"Param";
        case DataIsObjectPtr:
            return L"ObjectPtr";
        case DataIsFileStatic:
            return L"FileStatic";
        case DataIsGlobal:
            return L"Global";
        case DataIsMember:
            return L"Member";
        case DataIsStaticMember:
            return L"StaticMember";
        case DataIsConstant:
            return L"Constant";
        default:
            return L"Unknown";
        }
    }

    static const wchar_t* locationTypeName(DWORD a_type)
    {
        switch (a_type)
        {
        case LocIsNull:
            return L"Null";
        case LocIsStatic:
            return L"Static";
        case LocIsTLS:
            return L"TLS";
        case LocIsRegRel:
            return L"RegRel";
        case LocIsThisRel:
            return L"ThisRel";
        case LocIsEnregistered:
            return L"Enregistered";
        case LocIsBitField:
            return L"BitField";
        case LocIsSlot:
            return L"Slot";
        case LocIsIlRel:
            return L"IlRel";
        case LocInMetaData:
            return L"MetaData";
        case LocIsConstant:
            return L"Constant";
        default:
            return L"Unknown";
        }
    }

    static const wchar_t* udtKindName(DWORD a_kind)
    {
        switch (a_kind)
        {
        case UdtStruct:
            return L"Struct";
        case UdtClass:
            return L"Class";
        case UdtUnion:
            return L"Union";
        default:
            return L"Unknown";
        }
    }

    static const wchar_t* accessName(DWORD aaccess)
    {
        switch (aaccess)
        {
        case CV_private:
            return L"private";
        case CV_protected:
            return L"protected";
        case CV_public:
            return L"public";
        default:
            return L"unknown";
        }
    }

    static void dumpName(IDiaSymbol* a_symbol, int a_indent)
    {
        BSTR name = nullptr;

        if (SUCCEEDED(a_symbol->get_name(&name)) && name)
        {
            printIndent(a_indent);
            wprintf(L"name:           \"%s\"\n", name);

            SysFreeString(name);
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

            BSTR name = nullptr;
            _type->get_name(&name);

            printIndent(a_indent);
            wprintf(L"type:           tag=%lu (%s), name=\"%s\"\n",
                _tag,
                symTagName(_tag),
                name ? name : L"<none>");

            if (name)
                SysFreeString(name);
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
            BSTR name = nullptr;
            _parent->get_name(&name);

            printIndent(a_indent);
            wprintf(L"classParent:    \"%s\"\n", name ? name : L"<anonymous>");

            if (name)
                SysFreeString(name);
        }
        else
        {
            printIndent(a_indent);
            wprintf(L"classParent:    <none>\n");
        }

        _parent.Release();

        if (SUCCEEDED(a_symbol->get_lexicalParent(&_parent)) && _parent)
        {
            BSTR name = nullptr;
            _parent->get_name(&name);

            printIndent(a_indent);
            wprintf(L"lexicalParent:  \"%s\"\n", name ? name : L"<anonymous>");

            if (name)
                SysFreeString(name);
        }
        else
        {
            printIndent(a_indent);
            wprintf(L"lexicalParent:  <none>\n");
        }
    }

    static void dumpData(IDiaSymbol* a_symbol, int a_indent)
    {
        DWORD kind = 0;

        if (SUCCEEDED(a_symbol->get_dataKind(&kind)))
        {
            printIndent(a_indent);
            wprintf(L"dataKind:       %lu (%s)\n", kind, dataKindName(kind));
        }

        DWORD access = 0;

        if (SUCCEEDED(a_symbol->get_access(&access)))
        {
            printIndent(a_indent);
            wprintf(L"access:         %lu (%s)\n", access, accessName(access));
        }

        BOOL _isStatic = FALSE;

        if (SUCCEEDED(a_symbol->get_isStatic(&_isStatic)))
        {
            printIndent(a_indent);
            wprintf(L"isStatic:       %s\n", _isStatic ? L"true" : L"false");
        }

        BOOL isConst = FALSE;

        if (SUCCEEDED(a_symbol->get_constType(&isConst)))
        {
            printIndent(a_indent);
            wprintf(L"isConst:        %s\n", isConst ? L"true" : L"false");
        }

        BOOL isVolatile = FALSE;

        if (SUCCEEDED(a_symbol->get_volatileType(&isVolatile)))
        {
            printIndent(a_indent);
            wprintf(L"isVolatile:     %s\n", isVolatile ? L"true" : L"false");
        }
    }

    static void dumpLocation(IDiaSymbol* a_symbol, int a_indent)
    {
        DWORD locationType = 0;

        if (SUCCEEDED(a_symbol->get_locationType(&locationType)))
        {
            printIndent(a_indent);
            wprintf(L"locationType:   %lu (%s)\n", locationType, locationTypeName(locationType));
        }

        LONG offset = 0;

        if (SUCCEEDED(a_symbol->get_offset(&offset)))
        {
            printIndent(a_indent);
            wprintf(L"offset:         %ld (0x%lX)\n", offset, offset);
        }

        DWORD bitPosition = 0;

        if (SUCCEEDED(a_symbol->get_bitPosition(&bitPosition)))
        {
            printIndent(a_indent);
            wprintf(L"bitPosition:    %lu (0x%lX)\n", bitPosition, bitPosition);
        }

        ULONGLONG length = 0;

        if (SUCCEEDED(a_symbol->get_length(&length)))
        {
            printIndent(a_indent);
            wprintf(L"length:         %llu bytes\n", length);
        }
    }

    static void dumpLayout(IDiaSymbol* a_symbol, int a_indent)
    {
        DWORD rank = 0;

        if (SUCCEEDED(a_symbol->get_rank(&rank)))
        {
            printIndent(a_indent);
            wprintf(L"rank:           %lu\n", rank);
        }

        DWORD count = 0;

        if (SUCCEEDED(a_symbol->get_count(&count)))
        {
            printIndent(a_indent);
            wprintf(L"count:          %lu\n", count);
        }

        DWORD stride = 0;

        if (SUCCEEDED(a_symbol->get_stride(&stride)))
        {
            printIndent(a_indent);
            wprintf(L"stride:         %lu\n", stride);
        }
    }

    static void dumpFlags(IDiaSymbol* a_symbol, int a_indent)
    {
        BOOL isVirtual = FALSE;

        if (SUCCEEDED(a_symbol->get_virtual(&isVirtual)))
        {
            printIndent(a_indent);
            wprintf(L"isVirtual:      %s\n", isVirtual ? L"true" : L"false");
        }

        BOOL isPure = FALSE;

        if (SUCCEEDED(a_symbol->get_pure(&isPure)))
        {
            printIndent(a_indent);
            wprintf(L"isPure:         %s\n", isPure ? L"true" : L"false");
        }

        BOOL isIntroVirtual = FALSE;

        if (SUCCEEDED(a_symbol->get_intro(&isIntroVirtual)))
        {
            printIndent(a_indent);
            wprintf(L"isIntroVirtual: %s\n", isIntroVirtual ? L"true" : L"false");
        }
    }

    static void dumpFunction(IDiaSymbol* a_symbol, int a_indent)
    {
        DWORD vtableOffset = 0;

        if (SUCCEEDED(a_symbol->get_virtualBaseOffset(&vtableOffset)))
        {
            printIndent(a_indent);
            wprintf(L"virtualOffset:  0x%lX\n", vtableOffset);
        }
    }

    static void dumpUDT(IDiaSymbol* a_symbol, int a_indent)
    {
        DWORD udtKind = 0;

        if (SUCCEEDED(a_symbol->get_udtKind(&udtKind)))
        {
            printIndent(a_indent);
            wprintf(L"udtKind:        %lu (%s)\n", udtKind, udtKindName(udtKind));
        }

        BOOL isNested = FALSE;

        if (SUCCEEDED(a_symbol->get_nested(&isNested)))
        {
            printIndent(a_indent);
            wprintf(L"isNested:       %s\n", isNested ? L"true" : L"false");
        }
    }

    static void dumpValue(IDiaSymbol* a_symbol, int a_indent)
    {
        VARIANT value;
        VariantInit(&value);

        if (SUCCEEDED(a_symbol->get_value(&value)))
        {
            printIndent(a_indent);
            wprintf(L"value:          ");

            switch (value.vt)
            {
            case VT_I1:
                wprintf(L"%d", value.cVal);
                break;

            case VT_UI1:
                wprintf(L"%u", value.bVal);
                break;

            case VT_I2:
                wprintf(L"%d", value.iVal);
                break;

            case VT_UI2:
                wprintf(L"%u", value.uiVal);
                break;

            case VT_I4:
                wprintf(L"%ld", value.lVal);
                break;

            case VT_UI4:
                wprintf(L"%lu", value.ulVal);
                break;

            case VT_I8:
                wprintf(L"%lld", value.llVal);
                break;

            case VT_UI8:
                wprintf(L"%llu", value.ullVal);
                break;

            default:
                wprintf(L"<VARIANT type %u>", value.vt);
                break;
            }

            wprintf(L"\n");

            VariantClear(&value);
        }
    }
};