#pragma once

#include <dia2.h>
#include <diacreate.h>
#pragma comment(lib, "diaguids.lib")

#include <string>
#include <algorithm>

#include "..\..\Util\Container\Singleton.hpp"

#include "..\Console\ConsoleManager.hpp"
#include "..\Settings\GlobalSettings.hpp"
#include "..\Debug\DebugManager.hpp"

class DiaManager : public Singleton<DiaManager>
{
    SET_SINGLETON_FRIEND(DiaManager)

protected:
    DiaManager() : m_source(nullptr), m_session(nullptr), m_globalScope(nullptr) {}

    IDiaDataSource* m_source;
    IDiaSession* m_session;
    IDiaSymbol* m_globalScope;

public:
    std::vector<BSTR> m_typeSources;

    bool initialize(const std::wstring& a_pdbPath) 
    {
        if (FAILED(CoInitialize(nullptr))) return false;

        HRESULT hr = CoCreateInstance(__uuidof(DiaSource), nullptr, CLSCTX_INPROC_SERVER,
            __uuidof(IDiaDataSource), (void**)&m_source);
        if (FAILED(hr)) return false;

        hr = m_source->loadDataFromPdb(a_pdbPath.c_str());
        if (FAILED(hr)) return false;

        hr = m_source->openSession(&m_session);
        if (FAILED(hr)) return false;

        hr = m_session->get_globalScope(&m_globalScope);
        return SUCCEEDED(hr);
    }

    IDiaSession* session() const { return m_session; }
    IDiaSymbol* globalScope() const { return m_globalScope; }

    ~DiaManager() 
    {
        if (m_globalScope) m_globalScope->Release();
        if (m_session) m_session->Release();
        if (m_source) m_source->Release();
        CoUninitialize();
    }

    /// cmd api:
    static void displayClass(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        // displayTabulation(a_nestingLevel);
        // displayFileSource(a_symbol);

        displayTabulation(a_nestingLevel);
        displaySize(a_symbol);

        displayTabulation(a_nestingLevel);
        displayModType_C(a_symbol);
        displaySym_C(a_symbol);
        displayName(a_symbol);
        displayClassInheritence_C(a_symbol);

        displayScopeBegin(a_nestingLevel); // {

        displayClassMembers_C(a_symbol, a_nestingLevel);
        
        displayScopeEnd(a_nestingLevel); // }

        displayTypeSources();
    }

    bool displayClass(const wchar_t* a_typeName)
    {
        IDiaEnumSymbols* _enumSymbols;

        auto _caseType = getNameSearchType();
        auto hr = m_globalScope->findChildren(SymTagUDT, a_typeName, _caseType, &_enumSymbols);

        if (SUCCEEDED(hr)) 
        {
            IDiaSymbol* _symbol;
            ULONG _celt = 0;

            while (SUCCEEDED(_enumSymbols->Next(1, &_symbol, &_celt)) && _celt == 1) 
            {
                displayClass(_symbol);

                _symbol->Release();
                return true;
            }

            _enumSymbols->Release();
        }

        return false;
    }

    static void displayEnum(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        // displayTabulation(a_nestingLevel);
        // displayTypeSource(a_symbol);

        displayTabulation(a_nestingLevel);
        displaySize(a_symbol);

        // ConsoleManager::print(L"%s ", getAccessName(_symbol));
        displayTabulation(a_nestingLevel);
        displayModType_C(a_symbol);
        displaySym_C(a_symbol);
        displayName(a_symbol);

        displayBaseTypeInheritence(a_symbol); // : TYPE

        displayScopeBegin(a_nestingLevel); // { 

        IDiaEnumSymbols* _enumMembers;
        if (SUCCEEDED(a_symbol->findChildren(SymTagData, NULL, nsNone, &_enumMembers)))
        {
            IDiaSymbol* _member;
            ULONG _celt = 0;
            while (SUCCEEDED(_enumMembers->Next(1, &_member, &_celt)) && _celt == 1)
            {
                displayTabulation(a_nestingLevel);
                displayEnumMemberValue_C(_member);
                _member->Release();
            }

            _enumMembers->Release();
        }

        displayScopeEnd(a_nestingLevel); // }
    }

    bool displayEnum(const wchar_t* a_typeName)
    {
        IDiaEnumSymbols* _enumSymbols;

        auto _caseType = getNameSearchType();
        auto hr = m_globalScope->findChildren(SymTagEnum, a_typeName, _caseType, &_enumSymbols);

        if (SUCCEEDED(hr))
        {
            IDiaSymbol* _symbol;
            ULONG _celt = 0;

            while (SUCCEEDED(_enumSymbols->Next(1, &_symbol, &_celt)) && _celt == 1)
            {
                displayEnum(_symbol);

                _symbol->Release();
                return true;
            }

            _enumSymbols->Release();
        }

        return false;
    }

    static void displayTypedef(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        // displaySize(_symbol);

        // displayTabulation(a_nestingLevel);
        // displayFileSource(a_symbol);

        displayTabulation(a_nestingLevel);

        displayModType_C(a_symbol);
        displaySym_C(a_symbol);

        if (auto _base = getType(a_symbol))
        {
            displayName(_base);
            ConsoleManager::instance().print(L" ");
            _base->Release();
        }

        displayName(a_symbol);

        ConsoleManager::instance().print(L";\n");
    }

    bool displayTypedef(const wchar_t* a_typeName) // attention: no template using alias
    {
        IDiaEnumSymbols* _enumSymbols;

        auto _caseType = getNameSearchType();
        auto hr = m_globalScope->findChildren(SymTagTypedef, a_typeName, _caseType, &_enumSymbols);

        if (SUCCEEDED(hr))
        {
            IDiaSymbol* _symbol;
            ULONG _celt = 0;

            while (SUCCEEDED(_enumSymbols->Next(1, &_symbol, &_celt)) && _celt == 1)
            {
                displayTypedef(_symbol);
                _symbol->Release();
                return true;
            }

            _enumSymbols->Release();
        }

        return false;
    }

    static void displayFriend(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        displayTabulation(a_nestingLevel);

        displayModType_C(a_symbol);
        displaySym_C(a_symbol);
        displayName(a_symbol);
        ConsoleManager::instance().print(L";\n");
    }

    static void displayVTable(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        displayTabulation(a_nestingLevel);
        displayRVA(a_symbol);
        displayTabulation(a_nestingLevel);
        displayVA(a_symbol);

        displayTabulation(a_nestingLevel);
        ConsoleManager::print(getCommentName_C());
        displayName(a_symbol);
        ConsoleManager::print(L"\n");

        IDiaEnumSymbols* _enum;
        if (SUCCEEDED(a_symbol->findChildren(SymTagNull, nullptr, nsNone, &_enum)) && _enum) 
        {
            IDiaSymbol* _child;
            ULONG _celt = 0;
            while (SUCCEEDED(_enum->Next(1, &_child, &_celt)) && _celt == 1) 
            {
                displayTabulation(a_nestingLevel);
                ConsoleManager::print(getCommentName_C());
                displayName(_child);
                ConsoleManager::print(L"\n");
                // vfuncs
            }
        }
    }

    static void displayFunctionArgs(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        bool _isFirst = true;

        IDiaEnumSymbols* _enumParams = nullptr;
        auto hr = a_symbol->findChildren(SymTagData, nullptr, nsNone, &_enumParams);

        if (SUCCEEDED(hr) && _enumParams != nullptr) 
        {
            IDiaSymbol* _param = nullptr;
            ULONG fetched = 0;
            while (SUCCEEDED(_enumParams->Next(1, &_param, &fetched)) && fetched == 1) 
            {
                DWORD _kind = 0;
                if (SUCCEEDED(_param->get_dataKind(&_kind)) && _kind == DataIsParam)
                {
                    if (!_isFirst) { ConsoleManager::print(L", "); }

                    // SymTagEnum;
                    // displaySymTag(_param);
                    displayName(_param, false);
                    //auto _type = getType(_param);
                    // displaySymTag(_type); 
                    //displayName(_type);
                    // displayChildInfo(_type);
                    //_type->Release();

                    _isFirst = false;
                }

                _param->Release();
            }
            _enumParams->Release();
        }
    }

    static void displayFunction(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        // displayTabulation(a_nestingLevel);
        // displayRVA(_function);
        // displayTabulation(a_nestingLevel);
        // displayVA(_function);
        // displayLocationType(_function);

        // displayTabulation(a_nestingLevel);
        // displaySize(_function);

        // displayTypeSource(a_symbol);

        // displayTabulation(a_nestingLevel);
        // displayTabulation(a_nestingLevel - 1);
        // ConsoleManager::print(L"%s: ", getAccessName(a_symbol));

        // ConsoleManager::print(getCommentName_C());
        // auto _isMod = false;

        displayTabulation(a_nestingLevel);
        const wchar_t* _names[] = { getVirtualName(a_symbol), getStaticName(a_symbol) };
        for (auto _name : _names) { if (_name) { ConsoleManager::print(L"%s ", _name); } }

        // DebugManager::WaitDebugger();

        auto _funtionType = getType(a_symbol); // SymTagFunctionType
        /*if (_funtionType)
        {
            displayName(_funtionType, false);
            ConsoleManager::print(L" ");
        }*/

        auto _retType = getType(_funtionType);
        if (_retType)
        {
            // displaySymTag(_retType); SymTagEnum;
            displayName(_retType, false);
            ConsoleManager::print(L" ");

            _retType->Release();
        }

        displayName(a_symbol);

        ConsoleManager::print(getFunctionArgsBegin_C());

        displayFunctionArgs(a_symbol);

        // displayChildInfo(a_symbol);

        ConsoleManager::print(getFunctionArgsEnd_C());

        // displayFileSource(a_symbol);

        auto _const = getConstName(a_symbol);
        if (_const ? _const : getConstName(_funtionType)) { ConsoleManager::print(L" %s", _const); }

        // if (getVirtualName(a_symbol)) { displayVTableOffset(a_symbol); } // didn't work correctly

        // ConsoleManager::print(L" %s", getPureName(a_symbol));

        if (_funtionType) { _funtionType->Release(); }

        ConsoleManager::print(L";");
    }

    static bool displayPointer(IDiaSymbol* a_symbol)
    {
        auto _ref = getReferenceName(a_symbol);
        auto _const = getConstName(a_symbol);

        if (auto _type = getType(a_symbol))
        {
            displayName(_type);
            _type->Release();
            ConsoleManager::print(L"%s", _ref ? _ref : L"*");
            if (_const) { ConsoleManager::print(L" %s", _const); }
            return true;
        }
        else 
        {
            ConsoleManager::print(L"void%s", _ref ? _ref : L"*");
            if (_const) { ConsoleManager::print(L" %s", _const); }
            return true;
        }

        return false;
    }

    static DWORD getArrayDisplayData_C(IDiaSymbol* a_symbol)
    {
        DWORD _count = 0;
        a_symbol->get_count(&_count);
        auto _const = getConstName(a_symbol);

        if (auto _type = getType(a_symbol))
        {
            displayName(_type);
            _type->Release();
            if (_const) { ConsoleManager::print(L" %s", _const); }
        }
        else
        {
            if (_const) { ConsoleManager::print(L"void %s", _const); }
        }

        return _count;
    }

    static bool displayArray_C(DWORD a_count)
    {
        ConsoleManager::print(L"%s%u%s", L"[", a_count, L"]");
        return true;
    }

    static std::pair<DWORD, ULONGLONG> getBitField_C(IDiaSymbol* a_symbol)
    {
        DWORD _bitPosition = 0;
        ULONGLONG _bitLength = 0;

        if (FAILED(a_symbol->get_bitPosition(&_bitPosition)) || FAILED(a_symbol->get_length(&_bitLength)))
        {
            return { UINT_MAX, MAXULONGLONG };
        }
        /*
        if (auto _type = getType(a_symbol))
        {
            displayName(_type);
            _type->Release();
        }
        else
        {
            ConsoleManager::print(L"unsigned int");
        }*/

        return { _bitPosition, _bitLength };
    }

    static bool displayBitField_C(DWORD a_bitPosition, ULONGLONG a_bitLength)
    {
        ConsoleManager::print(L" : %llu", a_bitLength);

        if (GlobalSettings::s_isOffsetInfo)
        {
            ConsoleManager::print(L"; // 0x%X", a_bitPosition); // attention
        }

        return true;
    }

    bool displayType(const wchar_t* a_typeName)
    {
        if (displayClass(a_typeName) 
            || displayEnum(a_typeName) 
            || displayTypedef(a_typeName)) 
        {
            return true; 
        }

        return false;
    }

protected:
    static NameSearchOptions getNameSearchType()
    {
        return GlobalSettings::s_isCaseSensitiveSearch ? nsCaseSensitive : nsCaseInsensitive;
    }

    template<typename T>
    static void safePrinter(T(*a_type))
    {
        if (a_type) { ConsoleManager::print(a_type()); }
    }

    static const wchar_t* getBaseTypeName_C(IDiaSymbol* a_symbol)
    {
        DWORD _baseType = 0;

        if (SUCCEEDED(a_symbol->get_baseType(&_baseType)))
        {
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
            case btFloat: return L"float";
            case btBool: return L"bool";
            case btInt: return L"int";
            case btUInt: return L"unsigned int";
            case btChar: return L"char";
            case btWChar: return L"wchar_t";
            case btLong: return L"long";
            case btULong: return L"unsigned long";

            case btBCD:
            case btBit:
            case btNoType:
            default: break; // ConsoleManager::print(L"Base type: unknown (%u)", _baseType); break;
            }
        }
        return nullptr;
    }

    static const wchar_t* getUDTName_C(IDiaSymbol* a_symbol)
    {
        DWORD _udt;
        if (SUCCEEDED(a_symbol->get_udtKind(&_udt)))
        {
            switch (_udt)
            {
            case UdtStruct: return L"struct";
            case UdtClass: return L"class";
            case UdtUnion: return L"union";

            case UdtInterface:
            case UdtTaggedUnion:
            default: return nullptr;
            }
        }
    }

    static const wchar_t* getAccessName(IDiaSymbol* a_symbol)
    {
        DWORD _access;
        if (SUCCEEDED(a_symbol->get_access(&_access)))
        {
            if (GlobalSettings::s_baseAccessType) { _access = GlobalSettings::s_baseAccessType; }

            switch (_access)
            {
            case CV_private: return L"private";
            case CV_protected: return L"protected";
            case CV_public: return L"public";
            case 0: return nullptr;
            }
        }
    }

    static const wchar_t* getConstName(IDiaSymbol* a_symbol)
    {
        BOOL _isConst;
        return SUCCEEDED(a_symbol->get_constType(&_isConst)) && _isConst ? L"const" : nullptr;
    }

    static const wchar_t* getVolatileName(IDiaSymbol* a_symbol)
    {
        BOOL _isVolatile;
        return SUCCEEDED(a_symbol->get_volatileType(&_isVolatile)) && _isVolatile ? L"volatile" : nullptr;
    }

    static const wchar_t* getVirtualName(IDiaSymbol* a_symbol)
    {
        BOOL _isVirtual;
        return SUCCEEDED(a_symbol->get_virtual(&_isVirtual)) && _isVirtual ? L"virtual" : nullptr;
    }

    static const wchar_t* getReferenceName(IDiaSymbol* a_symbol)
    {
        BOOL _isRef;
        return SUCCEEDED(a_symbol->get_reference(&_isRef)) && _isRef ? L"&" : nullptr;
    }

    static const wchar_t* getStaticName(IDiaSymbol* a_symbol)
    {
        BOOL _isStatic;
        return SUCCEEDED(a_symbol->get_isStatic(&_isStatic)) && _isStatic ? L"static" : nullptr;
        /*IDiaSymbol* _parent = nullptr;
        if (SUCCEEDED(a_symbol->get_classParent(&_parent)))
        {
            if (_parent) { _parent->Release(); return L"static"; }
        }

        return nullptr;*/
        //DWORD _locationType = 0;
        //return SUCCEEDED(a_symbol->get_locationType(&_locationType)) && _locationType == LocIsStatic ? L"static" : nullptr; // not guaranteed
    }

    static const wchar_t* getInlineName(IDiaSymbol* a_symbol)
    {
        BOOL _isInline;
        return SUCCEEDED(a_symbol->get_inlSpec(&_isInline)) && !_isInline ? L"inline" : nullptr; // __forceinline / __inline / inline
    }

    static const wchar_t* getNakedName(IDiaSymbol* a_symbol)
    {
        BOOL _isNaked;
        return SUCCEEDED(a_symbol->get_isNaked(&_isNaked)) && !_isNaked ? L"naked" : nullptr; // __declspec(naked)
    }

    static const wchar_t* getNoReturnName(IDiaSymbol* a_symbol)
    {
        BOOL _isNoRet; 
        return SUCCEEDED(a_symbol->get_noReturn(&_isNoRet)) && !_isNoRet ? L"noreturn" : nullptr; // __declspec(noreturn)
    }

    static const wchar_t* getNoInlineName(IDiaSymbol* a_symbol)
    {
        BOOL _isNoInline;
        return SUCCEEDED(a_symbol->get_noInline(&_isNoInline)) && !_isNoInline ? L"noinline" : nullptr; // __declspec(noinline)
    }

    static const wchar_t* getPureName(IDiaSymbol* a_symbol)
    {
        BOOL _isPure;
        return SUCCEEDED(a_symbol->get_pure(&_isPure)) && !_isPure ? L"= 0" : nullptr;
    }

    static const wchar_t* getFirstBracketName_C() { return  L"{\n"; }

    static const wchar_t* getLastBracketName_C() { return L"};\n"; } // \n

    static const wchar_t* getCommentName_C() { return L"// "; }

    static const wchar_t* getMultilineCommentBeginName_C() { return L"/* "; }

    static const wchar_t* getMultilineCommentEndName_C() { return L" */"; }

    static const wchar_t* getFunctionArgsBegin_C() { return L"("; }

    static const wchar_t* getFunctionArgsEnd_C() { return L")"; }

    static const wchar_t* getInheritenceName_C() { return L" : "; }

    static const wchar_t* getTabulationName_C() { return L"    "; } // \t

    static const wchar_t* getSymTagBegin_C(IDiaSymbol* a_symbol)
    {
        DWORD _dwSymTag;
        if (SUCCEEDED(a_symbol->get_symTag(&_dwSymTag)))
        {
            switch (_dwSymTag)
            {
            case SymTagTypedef: return L"typedef"; // typdef : using
            case SymTagEnum: return L"enum"; // enum class : enum
            case SymTagBaseType: return getBaseTypeName_C(a_symbol);
            case SymTagUDT: return getUDTName_C(a_symbol);

            case SymTagFriend: return L"friend";
            case SymTagLabel: return L"label";
            case SymTagUsingNamespace: return L"using namespace";

            // SymTagData, SymTagFunction, SymTagPublicSymbol? SymTagFunctionType SymTagPointerType SymTagArrayType SymTagBaseClass SymTagFunctionArgType
            // SymTagHLSLType, SymTagMatrixType, SymTagVectorType, SymTagBaseInterface, SymTagTaggedUnionCase

            case SymTagManagedType: // System.String, CLR
            case SymTagInlinee:
            case SymTagCoffGroup:
            case SymTagHeapAllocationSite:
            case SymTagExport:
            case SymTagCallee:
            case SymTagCaller:
            case SymTagInlineSite:
            case SymTagCallSite:
            case SymTagDimension:
            case SymTagCustomType:
            case SymTagThunk:
            case SymTagCustom:
            case SymTagVTable:
            case SymTagVTableShape:
            case SymTagFuncDebugEnd:
            case SymTagFuncDebugStart:
            case SymTagAnnotation:
            case SymTagBlock:
            case SymTagNull:
            case SymTagExe:
            case SymTagCompiland:
            case SymTagCompilandDetails:
            case SymTagCompilandEnv:
            default: return nullptr;
            }
        }
    }

    static int getNonscopedNameBegin(const wchar_t* a_name, const wchar_t* a_separator = L"::")
    {
        auto _iterator = (int)wcslen(a_name);
        const auto _separatorSize = wcslen(a_separator);

        while (--_iterator >= 0)
        {
            if (_iterator >= _separatorSize - 1
                && a_separator[_separatorSize - 1] == a_name[_iterator]
                && wcsncmp(&a_name[_iterator - _separatorSize + 1], a_separator, _separatorSize) == 0)
            {
                return _iterator + 1;
            }
        }
        
        return 0;
    }

    static void registerTypeSource(IDiaSymbol* a_symbol)
    {
        if (!GlobalSettings::s_typeSource) { return; }

        IDiaSourceFile* _sourceFile = nullptr;
        IDiaLineNumber* _lineNumber = nullptr;
        IDiaEnumLineNumbers* _enumLineNumbers = nullptr;

        DWORD _addressSection = 0;
        DWORD _addressOffset = 0;

        if (SUCCEEDED(a_symbol->get_addressSection(&_addressSection)) && SUCCEEDED(a_symbol->get_addressOffset(&_addressOffset)))
        {
            instance().m_session->findLinesByAddr(_addressSection, _addressOffset, 1, &_enumLineNumbers);

            ULONG _celt = 0;
            if (_enumLineNumbers && SUCCEEDED(_enumLineNumbers->Next(1, &_lineNumber, &_celt)) && _celt == 1)
            {
                if (SUCCEEDED(_lineNumber->get_sourceFile(&_sourceFile)) && _sourceFile)
                {
                    BSTR _filename;
                    if (SUCCEEDED(_sourceFile->get_fileName(&_filename)))
                    {
                        instance().m_typeSources.push_back(_filename);
                    }
                    _sourceFile->Release();
                }
                _lineNumber->Release();
            }
            if (_enumLineNumbers)
            {
                _enumLineNumbers->Release();
            }
        }
    }

    static void displayTypeSources()
    {
        auto& _sources = instance().m_typeSources;

        for (auto& _source : _sources)
        {
            ConsoleManager::print(L"%s%s\n", getCommentName_C(), _source);
            SysFreeString(_source);
        }

        _sources.clear();
    }

    /// Note: Debug
    static void displayLocationType(IDiaSymbol* a_symbol)
    {
        DWORD _locationType = 0;
        if (SUCCEEDED(a_symbol->get_locationType(&_locationType))) 
        {
            ConsoleManager::print(L"\n%s ", getCommentName_C());

            switch (_locationType) 
            {
            case LocIsNull: ConsoleManager::print(L"Location: None\n"); break;
            case LocIsStatic: ConsoleManager::print(L"Location: Static\n"); break;
            case LocIsTLS: ConsoleManager::print(L"Location: TLS\n"); break;
            case LocIsRegRel: ConsoleManager::print(L"Location: Register-relative (stack variable)\n"); break;
            case LocIsThisRel: ConsoleManager::print(L"Location: This-relative (class member)\n"); break;
            case LocIsEnregistered: ConsoleManager::print(L"Location: Enregistered (in register)\n"); break;
            case LocIsBitField: ConsoleManager::print(L"Location: Bit field\n"); break;
            case LocIsConstant: ConsoleManager::print(L"Location: Constant\n"); break;
            default: ConsoleManager::print(L"Location: Unknown (%u)\n", _locationType); 
            }
        }
    }

    static bool displaySize(IDiaSymbol* a_symbol, const wchar_t* (*a_commentName)() = getCommentName_C)
    {
        ULONGLONG _len;
        if (SUCCEEDED(a_symbol->get_length(&_len)) && GlobalSettings::s_isSizeInfo)
        {
            if (a_commentName) { ConsoleManager::print(a_commentName()); }
            ConsoleManager::print(L"size: %llu byte\n", _len);
            return true;
        }
        return false;
    }

    static bool displayVTableOffset(IDiaSymbol* a_symbol, const wchar_t* (*a_commentName)() = getCommentName_C)
    {
        DWORD _offset;
        if (SUCCEEDED(a_symbol->get_virtualBaseOffset(&_offset)))
        {
            if (a_commentName) { ConsoleManager::print(a_commentName()); }
            ConsoleManager::print(L"0x%X\n", _offset);
            return true;
        }
        return false;
    }

    /*static bool displayLength(IDiaSymbol* a_symbol, const wchar_t* (*a_commentName)() = getCommentName_C) // same as size
    {
        ULONGLONG _length;
        if (SUCCEEDED(a_symbol->get_length(&_length)))
        {
            ConsoleManager::print(L"%s length: %llu bytes\n", a_commentName(), _length);
        }
    }*/

    static bool displayRVA(IDiaSymbol* a_symbol, const wchar_t* (*a_commentName)() = getCommentName_C)
    {
        DWORD _rva = 0;
        if (SUCCEEDED(a_symbol->get_relativeVirtualAddress(&_rva))) 
        {
            if (a_commentName) { ConsoleManager::print(a_commentName()); }
            ConsoleManager::print(L"RVA: 0x%X\n", _rva);
        }
        return _rva;
    }

    static bool displayVA(IDiaSymbol* a_symbol, const wchar_t* (*a_commentName)() = getCommentName_C)
    {
        ULONGLONG _va = 0;
        if (SUCCEEDED(a_symbol->get_virtualAddress(&_va)))
        {
            if (a_commentName) { ConsoleManager::print(a_commentName()); }
            ConsoleManager::print(L"VA: 0x%llX\n", _va);
        }
        return _va;
    }

    static bool displayVariantValue(VARIANT a_variant)
    {
        if (GlobalSettings::s_isEnumHex) { ConsoleManager::print(L" = %X", a_variant.llVal); return true; }

        switch (a_variant.vt)
        {
        case VT_I4: ConsoleManager::print(L" = %d", a_variant.lVal); return true;
        case VT_UI4: ConsoleManager::print(L" = %u", a_variant.ulVal); return true;
        case VT_I2: ConsoleManager::print(L" = %d", a_variant.iVal); return true;
        case VT_UI2: ConsoleManager::print(L" = %u", a_variant.uiVal); return true;
        case VT_I1: ConsoleManager::print(L" = %d", a_variant.bVal); return true;
        case VT_UI1: ConsoleManager::print(L" = %u", a_variant.bVal); return true;
        default: return false;
        }
    }

    /// NOTE: Debug
    static bool displaySymTag(IDiaSymbol* a_symbol)
    {
        DWORD _dwSymTag;
        if (SUCCEEDED(a_symbol->get_symTag(&_dwSymTag)))
        {
            ConsoleManager::instance().print(L"symTag: %u\n", _dwSymTag);
            return true;
        }
        return false;
    }

    static bool displaySym_C(IDiaSymbol* a_symbol)
    {
        auto _sym = getSymTagBegin_C(a_symbol);

        if (_sym)
        {
            ConsoleManager::print(_sym);
            ConsoleManager::print(L" ");
        }

        return _sym;
    }

    static bool displayName(IDiaSymbol* a_symbol, bool a_nonScope = true)
    {
        static const wchar_t* s_hiddenName = L"__formal"; // you can use it to hide a name
        auto _symTagSubType = SymTagNull;
        DWORD _arraySize = UINT_MAX;

        ULONGLONG _bitfieldLength = UINT_MAX;
        DWORD _bitfieldPosition = MAXULONGLONG;

        auto _res = false;

        auto _const = getConstName(a_symbol);
        if (auto _volatile = getVolatileName(a_symbol)) { ConsoleManager::print(L"%s ", _volatile); }

        // displaySymTag(a_symbol); SymTagEnum;
        auto _symTag = SymTagNull;
        if (SUCCEEDED(a_symbol->get_symTag((DWORD*)&_symTag)))
        {
            switch (_symTag)
            {
            case SymTagBaseType: 
                if (_const) { ConsoleManager::print(L"%s ", _const); } 
                if (auto _base = getBaseTypeName_C(a_symbol)) { ConsoleManager::print(_base); } return true;
            case SymTagPointerType: return displayPointer(a_symbol);
            case SymTagArrayType: return displayArray_C(getArrayDisplayData_C(a_symbol));
            case SymTagData: 
                if (auto _type = getType(a_symbol)) 
                { 
                    std::tie(_bitfieldPosition, _bitfieldLength) = getBitField_C(a_symbol);

                    if (SUCCEEDED(_type->get_symTag((DWORD*)&_symTagSubType)) && _symTagSubType == SymTagArrayType)
                    {
                        _arraySize = getArrayDisplayData_C(_type);
                    }
                    else
                    {
                        displayName(_type, a_nonScope);
                    }
                    ConsoleManager::print(L" "); 
                } 
                break;
            default: break;
            }
        }

        BSTR _bstrName = nullptr;
        if (SUCCEEDED(a_symbol->get_name(&_bstrName)))
        {
            if (_const) { ConsoleManager::print(L"%s ", _const); }

            if (_bstrName != nullptr)
            {
                auto _nameBegin = GlobalSettings::s_isNonScoped && a_nonScope ? getNonscopedNameBegin(_bstrName) : 0;
                ConsoleManager::print(L"%s", &_bstrName[_nameBegin]);
                _res = true;
            }

            SysFreeString(_bstrName);
        }

        if (_symTagSubType == SymTagArrayType && _arraySize != UINT_MAX)
        {
            displayArray_C(_arraySize);
        }

        if (_bitfieldLength != MAXULONGLONG && _bitfieldLength != 0 && _bitfieldPosition != UINT_MAX) // attention to _bitfieldLength != 0
        {
            displayBitField_C(_bitfieldPosition, _bitfieldLength);
        }

        return _res;
    }

    static bool displayUndecoratedName(IDiaSymbol* a_symbol)
    {
        auto _res = false;

        BSTR _bstrName = nullptr;
        if (SUCCEEDED(a_symbol->get_undecoratedName(&_bstrName)))
        {
            if (_bstrName != nullptr)
            {
                // auto _nameBegin = GlobalSettings::s_isNonScoped ? getNonscopedNameBegin(_bstrName) : 0;
                ConsoleManager::print(L"%s", _bstrName);
                _res = true;
            }
            else 
            {
                displayName(a_symbol);
            }

            SysFreeString(_bstrName);
        }
        return _res;
    }

    static bool displayEnumMemberValue_C(IDiaSymbol* a_symbol)
    {
        auto _res = false;

        displayName(a_symbol);

        VARIANT v;
        VariantInit(&v);
        if (SUCCEEDED(a_symbol->get_value(&v))) 
        {
            _res = displayVariantValue(v);
        }
        VariantClear(&v);

        ConsoleManager::print(L",\n");
        return _res;
    }

    static bool displayClassInheritence_C(IDiaSymbol* a_symbol)
    {
        IDiaEnumSymbols* _baseEnum;

        bool _isBegin = true;

        if (SUCCEEDED(a_symbol->findChildren(SymTagBaseClass, NULL, nsNone, &_baseEnum)))
        {
            IDiaSymbol* _baseSymbol;
            ULONG _celt = 0;
            while (SUCCEEDED(_baseEnum->Next(1, &_baseSymbol, &_celt)) && _celt == 1)
            {
                ConsoleManager::print(_isBegin ? getInheritenceName_C() : L", ");
                _isBegin = false;

                if (auto _access = getAccessName(_baseSymbol))
                {
                    ConsoleManager::print(L"%s ", _access);
                }

                displayName(_baseSymbol);
                
                _baseSymbol->Release();
            }
        }

        return _isBegin;
    }

    static void displayClassMembers_C(IDiaSymbol* a_symbol, int a_nestingLevel)
    {
        // auto& _dia = DiaManager::instance();

        IDiaEnumSymbols* _children;
        if (SUCCEEDED(a_symbol->findChildren(SymTagNull, NULL, nsNone, &_children)))
        {
            std::vector<IDiaSymbol*> _childsContainers[7];

            IDiaSymbol* _child;
            ULONG _celt = 0;
            while (SUCCEEDED(_children->Next(1, &_child, &_celt)) && _celt == 1)
            {
                DWORD symTag = 0;
                _child->get_symTag(&symTag);

                // displayName(_child);

                switch (symTag)
                {
                case SymTagData:        _childsContainers[0].push_back(_child); break; // get_dataKind              7
                case SymTagFunction:    _childsContainers[1].push_back(_child); break; // isVirtual, isPureVirtual  5 or 6
                case SymTagUDT:         _childsContainers[2].push_back(_child); break; // subclasses                4       +
                case SymTagEnum:        _childsContainers[3].push_back(_child); break; // enums                     2       +
                case SymTagTypedef:     _childsContainers[4].push_back(_child); break; // typedef / using           3       +
                // case SymTagVTable:   _childsContainers[5].push_back(_child); break; // vtable                    5 or 6
                case SymTagFriend:      _childsContainers[6].push_back(_child); break; // friend                    1       +
                default:                _child->Release(); break;
                }
            }

            for (auto& _friend : _childsContainers[6]) // SymTagFriend
            {
                displayFriend(_friend, a_nestingLevel);
            }

            for (auto& _enum : _childsContainers[3]) // SymTagEnum
            {
                displayEnum(_enum, a_nestingLevel);
            }

            for (auto& _typedef : _childsContainers[4]) // SymTagTypedef
            {
                displayTypedef(_typedef, a_nestingLevel);
            }

            for (auto& _class : _childsContainers[2]) // SymTagUDT
            {
                displayClass(_class, a_nestingLevel);
            }

            /* 
            for (auto& _vftable : _childsContainers[5]) // SymTagVTable
            {
                displayVTable(_vftable, a_nestingLevel);
            } 
            */

            /// --------------------------------------- <SymTagFunction> ---------------------------------------

            std::vector<IDiaSymbol*> _vfuncs;

            for (auto& _function : _childsContainers[1]) // SymTagFunction + get_virtual
            {
                BOOL _isVirtual = false;
                if (FAILED(_function->get_virtual(&_isVirtual)) || !_isVirtual) { continue; }
                registerTypeSource(_function);
                _vfuncs.push_back(_function);
            }

            std::sort(_vfuncs.begin(), _vfuncs.end(),
                [](IDiaSymbol* a, IDiaSymbol* b)
                {
                    DWORD _offsetA, _offsetB;
                    a->get_virtualBaseOffset(&_offsetA);
                    b->get_virtualBaseOffset(&_offsetB);
                    return _offsetA < _offsetB;
                });

            for (auto& _vfunc : _vfuncs)
            {
                displayFunction(_vfunc, a_nestingLevel);

                if (GlobalSettings::s_isOffsetInfo)
                {
                    // displayTabulation(a_nestingLevel);
                    DWORD _offset = 0xFFFFFFFC;
                    if (SUCCEEDED(_vfunc->get_virtualBaseOffset(&_offset)) && _offset != 0xFFFFFFFC) 
                    { 
                        ConsoleManager::print(L" %s0x%X", getCommentName_C(), _offset); 
                    }
                }
                
                ConsoleManager::print(L"\n");
            }

            /// -------------------------------------- </SymTagFunction> ---------------------------------------

            /// ----------------------------------------- <SymTagData> -----------------------------------------

            std::vector<IDiaSymbol*> _fields;

            for (auto& _field : _childsContainers[0]) // SymTagData
            {
                DWORD _kind = 0;
                if (SUCCEEDED(_field->get_dataKind(&_kind)) && _kind == DataIsMember) 
                {  
                    _fields.push_back(_field);
                }
            }

            std::sort(_fields.begin(), _fields.end(),
                [](IDiaSymbol* a, IDiaSymbol* b)
                {
                    LONG _offsetA, _offsetB;
                    a->get_offset(&_offsetA);
                    b->get_offset(&_offsetB);
                    return _offsetA < _offsetB;
                });

            for (auto& _field : _fields) 
            {
                displayTabulation(a_nestingLevel);
                displayName(_field);

                ConsoleManager::print(L"; ");

                if (GlobalSettings::s_isOffsetInfo)
                {
                    LONG _offset = 0xFFFFFFFC;
                    if (SUCCEEDED(_field->get_offset(&_offset)) && _offset != 0xFFFFFFFC)
                    {
                        ConsoleManager::print(L"%s0x%X", getCommentName_C(), _offset);
                    }
                }
                ConsoleManager::print(L"\n");
            }

            /// ----------------------------------------  </SymTagData> ----------------------------------------

            for (auto& _function : _childsContainers[1]) // SymTagFunction + !get_virtual
            {
                BOOL _isVirtual = true;
                if (FAILED(_function->get_virtual(&_isVirtual)) || _isVirtual) { continue; }

                // displayTabulation(a_nestingLevel);
                registerTypeSource(_function);

                displayFunction(_function, a_nestingLevel);
                ConsoleManager::print(L"\n");
            }

            for (auto& _field : _childsContainers[0]) // SymTagData / static - const
            {
                DWORD _kind = 0;
                if (FAILED(_field->get_dataKind(&_kind)) || _kind == DataIsMember) { continue; }

                displayTabulation(a_nestingLevel);

                const wchar_t* _modificator = nullptr;
                switch (_kind)
                {
                case DataIsStaticMember: _modificator = L"static "; break;
                case DataIsConstant: _modificator = L"constexpr "; break;
                default: break;
                }
                
                ConsoleManager::print(_modificator);

                displayName(_field);

                ConsoleManager::print(L";\n ");
            }

            for (auto& _childsContainer : _childsContainers)
            {
                for (auto& _child : _childsContainer)
                {
                    _child->Release();
                }
            }
        }

        _children->Release();
    }

    static bool displayModType_C(IDiaSymbol* a_symbol)
    {
        auto _toSpace = false;
        const wchar_t* _modificators[] = { getConstName(a_symbol), getVolatileName(a_symbol) };

        for (auto _mod : _modificators)
        {
            if (!_mod) continue;

            if (_toSpace) { ConsoleManager::print(L" "); _toSpace = false; }
            ConsoleManager::print(_mod);
            _toSpace = true;
        }

        return _toSpace;
    }

    static bool displayAccessScope_C(IDiaSymbol* a_symbol, bool a_isChild = false)
    {
        auto _res = false;
        auto _name = getAccessName(a_symbol);
        if (_name && (GlobalSettings::s_isShowAccess || a_isChild))
        {
            ConsoleManager::print(_name);
            ConsoleManager::print(L": ");
        }

        return _name;
    }     

    static bool displayBaseTypeInheritence(IDiaSymbol* a_symbol, const wchar_t* (*a_inheritGet)() = getInheritenceName_C)
    {
        auto _base = getBaseTypeName_C(a_symbol);

        if (_base)
        {
            safePrinter(a_inheritGet);
            ConsoleManager::print(_base);
        }

        return _base;
    }

    static void displayScopeBegin(const wchar_t* (*a_begin)() = getFirstBracketName_C)
    {
        GlobalSettings::s_isCurlyBracketTransfer ? ConsoleManager::print(L"\n") : ConsoleManager::print(L" ");
        safePrinter(a_begin);
    }

    static void displayScopeBegin(int& a_nestingLevel, const wchar_t* (*a_begin)() = getFirstBracketName_C)
    {
        GlobalSettings::s_isCurlyBracketTransfer ? ConsoleManager::print(L"\n"), displayTabulation(a_nestingLevel) : ConsoleManager::print(L" ");
        safePrinter(a_begin);
        ++a_nestingLevel;
    }

    static void displayScopeEnd(const wchar_t* (*a_end)() = getLastBracketName_C)
    {
        safePrinter(a_end);
    }

    static void displayScopeEnd(int& a_nestingLevel, const wchar_t* (*a_end)() = getLastBracketName_C)
    {
        displayTabulation(--a_nestingLevel);
        displayScopeEnd(a_end);
    }

    static void displayTabulation(int a_repeatTimes = 1, const wchar_t* (*a_lTab)() = getTabulationName_C)
    {
        while (a_repeatTimes-- > 0) { safePrinter(a_lTab); }
    }

    static void displayChildInfo(IDiaSymbol* a_symbol)
    {
        IDiaEnumSymbols* _enumParams = nullptr;
        auto hr = a_symbol->findChildren(SymTagNull, nullptr, nsNone, &_enumParams);

        if (SUCCEEDED(hr) && _enumParams != nullptr)
        {
            IDiaSymbol* _param = nullptr;
            ULONG _fetched = 0;
            while (SUCCEEDED(_enumParams->Next(1, &_param, &_fetched)) && _fetched == 1)
            {
                ConsoleManager::print(L"\n");
                displaySymTag(_param);
                displayTabulation();
                displayTabulation();
                displayName(_param);
                displayChildInfo(_param);
            }
            _enumParams->Release();
        }
    }

    static IDiaSymbol* getParent(IDiaSymbol* a_symbol)
    {
        IDiaSymbol* _parent;
        if (SUCCEEDED(a_symbol->get_lexicalParent(&_parent)))
        {
            return _parent;
        }
        return nullptr;
    }

    static IDiaSymbol* getType(IDiaSymbol* a_symbol)
    {
        IDiaSymbol* _baseType;
        if (SUCCEEDED(a_symbol->get_type(&_baseType)))
        {
            return _baseType;
        }
        return nullptr;
    }

    static bool isScoped(IDiaSymbol* a_symbol)
    {
        DWORD _tag;
        if (SUCCEEDED(a_symbol->get_symTag(&_tag)))
        {
            switch(_tag)
            {
            case SymTagUDT: return true;

            default: return false;
            }
        }

        return false;
    }
};