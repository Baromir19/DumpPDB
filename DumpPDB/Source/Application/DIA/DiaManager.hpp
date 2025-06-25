#pragma once

#include <dia2.h>
#include <diacreate.h>
#pragma comment(lib, "diaguids.lib")

#include <string>
#include <sstream>
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

    static inline std::wstring s_parentClassName = L"";
    static constexpr DWORD s_noRetValue = 0xFFFFFFFC;

    struct DiaSymbolType
    {
        bool m_isConstPointed = false;
        bool m_isConst = false;
        bool m_isVolatile = false;
        bool m_isRoundBrackets = false;
        std::wstring m_type = L"";
        std::wstring m_name = L"";
        std::wstring m_pointer = L"";
        std::wstring m_array = L"";
        std::wstring m_pointedFunctionArgs = L"";
        std::wstring m_bitfield = L"";
        
        enum SymTagEnum m_tag = SymTagNull;

        DiaSymbolType& operator+=(const DiaSymbolType& other)
        {
            if (!m_pointer.empty() && m_array.empty() && !other.m_array.empty())
            {
                m_isRoundBrackets = true;
            }

            m_isConstPointed = m_isConstPointed || other.m_isConstPointed;
            m_isConst = m_isConst || other.m_isConst;
            m_isVolatile = m_isVolatile || other.m_isVolatile;
            m_isRoundBrackets = m_isRoundBrackets || other.m_isRoundBrackets;

            if (!other.m_name.empty()) 
            { 
                if (!m_name.empty()) { m_name += L" "; }
                m_name += other.m_name;
            }

            if (!other.m_type.empty())
            {
                if (!m_type.empty()) { m_type += L" "; }
                m_type += other.m_type;
            }

            //m_pointer = other.m_pointer + m_pointer;
            //m_array = other.m_array + m_array;            
            m_pointer += other.m_pointer;
            m_array += other.m_array;
            m_pointedFunctionArgs += other.m_pointedFunctionArgs; // ? by ,
            m_bitfield += other.m_bitfield;

            if (!m_pointedFunctionArgs.empty()) { m_isRoundBrackets = true; }

            return *this;
        }
    };

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
        const auto _prevParentName = s_parentClassName;
        s_parentClassName = getName(a_symbol);
        //---------------------------------------------

        displayTabulation(a_nestingLevel);
        displaySize(a_symbol);

        displayTabulation(a_nestingLevel);
        displayModType_C(a_symbol);
        displaySym_C(a_symbol);
        ConsoleManager::print(getSymTypeText(getSymType(a_symbol, false)).c_str());

        displayClassInheritence_C(a_symbol);

        displayScopeBegin(a_nestingLevel); // {

        displayClassMembers_C(a_symbol, a_nestingLevel);
        
        displayScopeEnd(a_nestingLevel); // }

        displayTypeSources();

        //---------------------------------------------
        s_parentClassName = _prevParentName;
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
        displayTabulation(a_nestingLevel);
        displaySize(a_symbol);

        displayTabulation(a_nestingLevel);
        displayModType_C(a_symbol);
        displaySym_C(a_symbol);
        ConsoleManager::print(getName(a_symbol, false).c_str()); 

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
        displayTabulation(a_nestingLevel);

        displayModType_C(a_symbol);
        displaySym_C(a_symbol);

        ConsoleManager::print(getSymTypeText(getSymType(a_symbol, false)).c_str());

        ConsoleManager::instance().print(L";\n");
    }

    bool displayTypedef(const wchar_t* a_typeName) // attention: there's no template using alias
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
        // displayName(a_symbol); 
        ConsoleManager::print(getSymTypeText(getSymType(a_symbol, false)).c_str());
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
        ConsoleManager::print(getSymTypeText(getSymType(a_symbol, false)).c_str());
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
                ConsoleManager::print(getSymTypeText(getSymType(_child, false)).c_str());
                ConsoleManager::print(L"\n");
            }
        }
    }

    static int displayFunctionArgs(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        bool _isFirst = true;
        auto _entriesCount = 0;

        IDiaEnumSymbols* _enumParams = nullptr;
        auto hr = a_symbol->findChildren(SymTagData, nullptr, nsNone, &_enumParams);

        if (SUCCEEDED(hr) && _enumParams != nullptr) 
        {
            IDiaSymbol* _param = nullptr;
            ULONG _fetched = 0;
            while (SUCCEEDED(_enumParams->Next(1, &_param, &_fetched)) && _fetched == 1) 
            {
                // displaySymTag(_param);
                // ConsoleManager::print(getSymTypeText(getSymType(_param, false)).c_str());
                DWORD _kind = 0;
                if (SUCCEEDED(_param->get_dataKind(&_kind)) && _kind == DataIsParam)
                {
                    ++_entriesCount;
                    if (!_isFirst) { ConsoleManager::print(L", "); }

                    ConsoleManager::print(getSymTypeText(getSymType(_param, false)).c_str()); 

                    _isFirst = false;
                }

                _param->Release();
            }
            _enumParams->Release();
        }

        return _entriesCount;
    }

    static void displayFunction(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        displayTabulation(a_nestingLevel);
        const wchar_t* _names[] = { getVirtualName(a_symbol), getStaticName(a_symbol) };
        for (auto _name : _names) { if (_name) { ConsoleManager::print(L"%s ", _name); } }

        auto _funtionType = getType(a_symbol); // SymTagFunctionType

        auto _argCount = getChildsCount(_funtionType, SymTagFunctionArgType);

        auto _retType = getType(_funtionType);
        if (_retType)
        {
            ConsoleManager::print(getSymTypeText(getSymType(_retType, false)).c_str());
            ConsoleManager::print(L" ");

            _retType->Release();
        }

        ConsoleManager::print(getName(a_symbol).c_str());

        ConsoleManager::print(getFunctionArgsBegin_C());

        auto _namedArgCount = displayFunctionArgs(a_symbol);

        if (_namedArgCount != _argCount) // IS SEARCH ERROR: <- TO REPLACE
        {
            // ConsoleManager::print(L" <- %i ", _argCount); 
            if (_namedArgCount) { ConsoleManager::print(L" <- "); } // If named args are present
            ConsoleManager::print(getFuncArgsType(_funtionType).m_pointedFunctionArgs.c_str());
        }

        ConsoleManager::print(getFunctionArgsEnd_C());

        auto _const = getConstName(a_symbol);
        if (_const ? _const : getConstName(_funtionType)) { ConsoleManager::print(L" %s", _const); }

        if (_funtionType) { _funtionType->Release(); }

        ConsoleManager::print(L";");
    }

    static std::wstring getPointer(IDiaSymbol* a_symbol)
    {
        std::wstringstream _ss;

        auto _ref = getReferenceName(a_symbol);

        _ss << (_ref ? _ref : L"*");

        return _ss.str();
    }

    static std::wstring getArray(IDiaSymbol* a_symbol)
    {
        std::wstringstream _ss;

        DWORD _count = s_noRetValue;
        if (SUCCEEDED(a_symbol->get_count(&_count)) && _count != s_noRetValue)
        {
            _ss << L"[" << _count << L"]";
        }

        return _ss.str();
    }

    static std::pair<DWORD, ULONGLONG> getBitField_C(IDiaSymbol* a_symbol)
    {
        DWORD _bitPosition = 0;
        ULONGLONG _bitLength = 0;

        if (FAILED(a_symbol->get_bitPosition(&_bitPosition)) || FAILED(a_symbol->get_length(&_bitLength)))
        {
            return { UINT_MAX, MAXULONGLONG };
        }

        return { _bitPosition, _bitLength };
    }

    static std::wstring _getBitField_C(IDiaSymbol* a_symbol)
    {
        DWORD _bitPosition = UINT_MAX;
        ULONGLONG _bitLength = MAXULONGLONG;

        if (FAILED(a_symbol->get_bitPosition(&_bitPosition)) 
            || FAILED(a_symbol->get_length(&_bitLength)) 
            || _bitLength == 0 || _bitLength == MAXULONGLONG || _bitPosition == UINT_MAX)
        {
            return L"";
        }

        std::wstringstream _ss;

        _ss << L" : " << _bitLength;

        if (GlobalSettings::s_isOffsetInfo)
        {
            _ss << L"; // 0x" << std::uppercase << std::hex << _bitPosition << std::dec;
        }

        return _ss.str();
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
        IDiaEnumSymbols* _enumSymbols;

        auto _caseType = getNameSearchType();
        auto hr = m_globalScope->findChildren(SymTagNull, a_typeName, _caseType, &_enumSymbols);

        if (SUCCEEDED(hr))
        {
            IDiaSymbol* _symbol;
            ULONG _celt = 0;

            while (SUCCEEDED(_enumSymbols->Next(1, &_symbol, &_celt)) && _celt == 1)
            {
                DWORD _symTag = 0;
                if (SUCCEEDED(_symbol->get_symTag(&_symTag)))
                {
                    switch (_symTag)
                    {
                    case SymTagTypedef: displayTypedef(_symbol); break;
                    case SymTagUDT: displayClass(_symbol); break;
                    case SymTagEnum: displayEnum(_symbol); break;
                    }
                }

                _symbol->Release();
            }

            _enumSymbols->Release();
        }

        return true;
    }

    bool displayCompilands()
    {
        // ConsoleManager::print(L"\n=== COMPILANDS ===\n");

        IDiaEnumSymbols* _enumSymbols = nullptr;
        IDiaSymbol* _globalScope = nullptr;

        if (FAILED(m_session->get_globalScope(&_globalScope))) return false;

        if (FAILED(_globalScope->findChildren(SymTagCompiland, nullptr, nsNone, &_enumSymbols)))
        {
            _globalScope->Release();
            return false;
        }

        IDiaSymbol* _compiland = nullptr;
        ULONG _celt = 0;
        DWORD _compilandId = 0;

        while (SUCCEEDED(_enumSymbols->Next(1, &_compiland, &_celt)) && _celt == 1)
        {
            BSTR _name = nullptr;
            if (SUCCEEDED(_compiland->get_name(&_name)) && _name)
            {
                ConsoleManager::print(L"%s\n", _name);

                SysFreeString(_name);
            }

            /*IDiaEnumSourceFiles* enumFiles = nullptr;
            if (SUCCEEDED(m_session->findFile(compiland, nullptr, nsNone, &enumFiles)))
            {
                IDiaSourceFile* srcFile = nullptr;
                ULONG fileCelt = 0;

                while (SUCCEEDED(enumFiles->Next(1, &srcFile, &fileCelt)) && fileCelt == 1)
                {
                    BSTR srcFileName = nullptr;
                    if (SUCCEEDED(srcFile->get_fileName(&srcFileName)) && srcFileName)
                    {
                        ConsoleManager::print(L"%s%s\n", L"  Source: ", srcFileName);
                        SysFreeString(srcFileName);
                    }
                    srcFile->Release();
                }
                enumFiles->Release();
            }*/

            _compiland->Release();
        }

        _enumSymbols->Release();
        _globalScope->Release();

        return true;
    }

    bool displayCompilandsEnv()
    {
        IDiaEnumSymbols* _enum;
        IDiaSymbol* _global;

        if (FAILED(m_session->get_globalScope(&_global))) { return false; }

        if (FAILED(_global->findChildren(SymTagCompiland, nullptr, nsNone, &_enum))) { return false; }

        IDiaSymbol* _compiland;
        ULONG _celt = 0;

        while (SUCCEEDED(_enum->Next(1, &_compiland, &_celt)) && _celt == 1)
        {
            std::wstring _name = getName(_compiland);
            ConsoleManager::print(L"[OBJ] %s\n", _name.c_str());

            IDiaEnumSymbols* _details;
            if (SUCCEEDED(_compiland->findChildren(SymTagCompilandDetails, nullptr, nsNone, &_details))) 
            {
                IDiaSymbol* _detail;
                while (SUCCEEDED(_details->Next(1, &_detail, &_celt)) && _celt == 1) 
                {
                    DWORD _platform, _language;
                    _detail->get_platform(&_platform);
                    _detail->get_language(&_language);

                    BSTR _compilerName;
                    _detail->get_compilerName(&_compilerName);//= getName(_detail); 

                    BOOL _isDebug;
                    _detail->get_hasDebugInfo(&_isDebug);
                    ConsoleManager::print(L"[ABOUT] Compiler: %s; Language: %u; Platform: %u; Debug: %s\n", _compilerName, _language, _platform, _isDebug ? L"true" : L"false");
                    SysFreeString(_compilerName);
                    _detail->Release();
                }

                _details->Release();
            }
            
            IDiaEnumSymbols* _env;
            if (SUCCEEDED(_compiland->findChildren(SymTagCompilandEnv, nullptr, nsNone, &_env))) 
            {
                IDiaSymbol* _envSym;
                while (SUCCEEDED(_env->Next(1, &_envSym, &_celt)) && _celt == 1) 
                {
                    std::wstring _envName = getName(_envSym);

                    VARIANT _val; 
                    VariantInit(&_val);
                    if (SUCCEEDED(_envSym->get_value(&_val)) && _val.bstrVal && _val.vt == VT_BSTR)
                    {
                        ConsoleManager::print(L"[ENV] %s = %s\n", _envName.c_str(), _val.bstrVal);

                        VariantClear(&_val);
                    }
                    else 
                    {
                        ConsoleManager::print(L"[ENV] %s\n", _envName.c_str());
                    }
                    _envSym->Release();
                }

                _env->Release();
            }
        }

        _enum->Release();
        _global->Release();
        _compiland->Release();

        return true;
    }

    bool displaySourceFiles()
    {
        // ConsoleManager::print(L"%s\n", L"=== SOURCE FILES ===");

        IDiaEnumSourceFiles* _enumSourceFiles = nullptr;
        if (FAILED(m_session->findFile(nullptr, nullptr, nsNone, &_enumSourceFiles))) { return false; }

        IDiaSourceFile* _sourceFile = nullptr;
        ULONG _celt = 0;
        DWORD _fileId = 0;

        while (SUCCEEDED(_enumSourceFiles->Next(1, &_sourceFile, &_celt)) && _celt == 1)
        {
            BSTR _fileName = nullptr;
            if (SUCCEEDED(_sourceFile->get_fileName(&_fileName)) && _fileName)
            {
                ConsoleManager::print(L"%s\n", _fileName);
                SysFreeString(_fileName);
            }

            _sourceFile->Release();
        }

        _enumSourceFiles->Release();

        return true;
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
        return nullptr;
    }

    static const wchar_t* getAccessName(IDiaSymbol* a_symbol)
    {
        DWORD _access = 0;
        if (SUCCEEDED(a_symbol->get_access(&_access)))
        {
            if (GlobalSettings::s_baseAccessType) { _access = GlobalSettings::s_baseAccessType; }

            switch (_access)
            {
            case CV_private: return L"private";
            case CV_protected: return L"protected";
            case CV_public: return L"public";
            default: return nullptr;
            }
        }

        return nullptr;
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
        return nullptr;
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
        const wchar_t* _sym = nullptr;

        if (_sym = getSymTagBegin_C(a_symbol))
        {
            ConsoleManager::print(_sym);
            ConsoleManager::print(L" ");
        }

        return _sym;
    }

    static int getChildsCount(IDiaSymbol* a_symbol, enum SymTagEnum a_type)
    {
        int _ret = 0;

        IDiaEnumSymbols* _enumSymbols = nullptr;
        if (SUCCEEDED(a_symbol->findChildren(a_type, nullptr, nsNone, &_enumSymbols)) && _enumSymbols)
        {
            IDiaSymbol* _childSymbol = nullptr; ULONG _celt = 0;
            while (SUCCEEDED(_enumSymbols->Next(1, &_childSymbol, &_celt)) && _celt == 1)
            {
                ++_ret;
            }
        }

        return _ret;
    }

    static DiaSymbolType getFuncArgsType(IDiaSymbol* a_symbol)
    {
        DiaSymbolType _ret;

        bool _isFirst = true;

        IDiaEnumSymbols* _enumSymbols = nullptr;
        if (SUCCEEDED(a_symbol->findChildren(SymTagFunctionArgType, nullptr, nsNone, &_enumSymbols)) && _enumSymbols)
        {
            IDiaSymbol* _childSymbol = nullptr; ULONG _celt = 0;
            while (SUCCEEDED(_enumSymbols->Next(1, &_childSymbol, &_celt)) && _celt == 1)
            {
                // displaySymTag(_childSymbol);
                IDiaSymbol* _argType = nullptr;
                if (SUCCEEDED(_childSymbol->get_type(&_argType)) && _argType)
                {
                    if (!_isFirst)
                    {
                        _ret.m_pointedFunctionArgs += L", ";
                    }

                    auto _typeText = getSymTypeText(getSymType(_argType));

                    _ret.m_pointedFunctionArgs += _typeText;

                    _isFirst = false;
                }
            }
        }

        return _ret;
    }

    static std::wstring getSymTypeText(DiaSymbolType a_type)
    {
        std::wstring _ret = L"";

        bool _toSpace = false;
        auto _space = [&]() { if (_toSpace) { _ret += L" "; } _toSpace = false; };

        /*if (!a_type.m_pointedFunctionArgs.empty()
            || (!a_type.m_pointer.empty() && !a_type.m_array.empty()))
        {
            a_type.m_isRoundBrackets = true;
        }*/

        if (a_type.m_isVolatile)        { _space(); _toSpace = true; _ret += L"volatile"; }
        if (a_type.m_isConst)           { _space(); _toSpace = true; _ret += L"const"; }
        if (!a_type.m_type.empty())     { _space(); _toSpace = true; _ret += a_type.m_type.c_str(); }
        if (a_type.m_isRoundBrackets)   { _space();                  _ret += L"("; }
        if (!a_type.m_pointer.empty())  { if (!a_type.m_isRoundBrackets) _toSpace = true; _ret += a_type.m_pointer.c_str(); }
        if (a_type.m_isConstPointed)    { _space(); _toSpace = true; _ret += L"const"; }
        if (!a_type.m_name.empty())     { _space(); _toSpace = true; _ret += a_type.m_name.c_str(); }
        if (a_type.m_isRoundBrackets)   {           _toSpace = true; _ret += L")"; }
        if (!a_type.m_pointedFunctionArgs.empty())                 { _ret += L"("; _ret += a_type.m_pointedFunctionArgs.c_str(); _ret += L")"; }
        if (!a_type.m_array.empty())    {           _toSpace = true; _ret += a_type.m_array.c_str(); }
        if (!a_type.m_bitfield.empty()) {           _toSpace = true; _ret += a_type.m_bitfield.c_str(); }

        return _ret;
    }

    static DiaSymbolType getSymType(IDiaSymbol* a_symbol, bool a_nonScope = true)
    {
        std::wstringstream _ss;
        DiaSymbolType _ret;

        DiaSymbolType _subTypeSymbol;
        IDiaSymbol* _subType = getType(a_symbol);

        auto _symTag = SymTagNull;
        _ret.m_tag = SUCCEEDED(a_symbol->get_symTag((DWORD*)&_symTag)) ? _symTag : SymTagNull;

        if (_subType) { _subTypeSymbol = getSymType(_subType); }

        std::wstring _bitfield;
        // std::wstring _array;

        auto _name = getName(a_symbol);
        auto _volatile = getVolatileName(a_symbol);
        auto _const = getConstName(a_symbol);

        //bool _toSpace = false;

        //auto _space = [&]() { if (_toSpace) { _ss << " "; } _toSpace = false; };

        // auto _pointer = getPointer(a_symbol);

        if (_ret.m_tag != SymTagPointerType && _ret.m_tag != SymTagArrayType)
        {
            if (_volatile) { _ret.m_isVolatile = true; }
            if (_const) { _ret.m_isConst = true; }
        }

        switch (_ret.m_tag)
        {
        case SymTagBaseType: 
        {
            _ret.m_type = getBaseTypeName_C(a_symbol); break;
        }
        case SymTagPointerType: 
        {
            _ret.m_pointer = getPointer(a_symbol); break;
        }
        case SymTagArrayType: 
        {
            _ret.m_array = getArray(a_symbol); break;
        }
        case SymTagData:
        {
            _ret.m_bitfield = _getBitField_C(a_symbol);
            if (!_name.empty()) { _ret.m_name = _name; }  
            break;
        }
        case SymTagFunctionType:
        {
            _ret += getFuncArgsType(a_symbol);

            // _ss << getFunctionArgsBegin_C() /* << getFunctionArgs(a_symbol)*/ << getFunctionArgsEnd_C();
            break;
        }
        case SymTagUDT:
        case SymTagTypedef:
        case SymTagEnum:
        {
            if (!_name.empty()) { _ret.m_type = _name; } break;
        }

        default: break;
        }

        if (_ret.m_tag == SymTagPointerType || _ret.m_tag == SymTagArrayType)
        {
            // if (_volatile) { _space(); _toSpace = true; _ss << _volatile << L""; }
            if (_const) { _ret.m_isConstPointed = true; }
        }


        // if (!_array.empty()) { _toSpace = true; _ss << _array; }

        if (_ret.m_tag != SymTagEnum) _ret += _subTypeSymbol;

        // _ret.m_name = _ss.str();

        if (_subType) { _subType->Release(); }

        return _ret;
    }

    static std::wstring getName(IDiaSymbol* a_symbol, bool a_nonScope = true)
    {
        static const wchar_t* s_hiddenName = L"__formal";

        BSTR _bstrName = nullptr;
        if (SUCCEEDED(a_symbol->get_name(&_bstrName)) && _bstrName)
        {
            auto _parent = getClassParent(a_symbol);
            std::wstring _parentScope = s_parentClassName + L"::"; // To save last class name -> print -> restore

            /*if (_parent)
            {
                _parentScope = getName(_parent, false) + _parentScope;
                _parent->Release();
            }*/

            auto _nameBegin = GlobalSettings::s_isNonScoped && a_nonScope ? getNonscopedNameBegin(_bstrName, _parentScope.c_str()) : 0;
            auto _ret = std::wstring(&_bstrName[_nameBegin]);
            SysFreeString(_bstrName);
            return _ret;
        }

        return L"";
    }

    static bool displayEnumMemberValue_C(IDiaSymbol* a_symbol)
    {
        auto _res = false;

        ConsoleManager::print(getName(a_symbol).c_str());

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
            IDiaSymbol* _baseSymbol = nullptr;
            ULONG _celt = 0;
            while (SUCCEEDED(_baseEnum->Next(1, &_baseSymbol, &_celt)) && _celt == 1)
            {
                ConsoleManager::print(_isBegin ? getInheritenceName_C() : L", ");
                _isBegin = false;

                // DebugManager::WaitDebugger();

                const wchar_t* _access = nullptr;
                if (_access = getAccessName(_baseSymbol))
                {
                    ConsoleManager::print(L"%s ", _access);
                }

                ConsoleManager::print(getName(_baseSymbol).c_str());
                
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

            bool _isFirst = true;

            if (!_childsContainers[6].empty() && GlobalSettings::s_infoComment)
            {
                if (!_isFirst) { ConsoleManager::print(L"\n"); } _isFirst = false;
                displayTabulation(a_nestingLevel);
                ConsoleManager::print(L"/// FRIENDS:\n");
            }

            for (auto& _friend : _childsContainers[6]) // SymTagFriend
            {
                displayFriend(_friend, a_nestingLevel);
            }

            if (!_childsContainers[3].empty() && GlobalSettings::s_infoComment)
            {
                if (!_isFirst) { ConsoleManager::print(L"\n"); } _isFirst = false;
                displayTabulation(a_nestingLevel);
                ConsoleManager::print(L"/// ENUMS:\n");
            }

            for (auto& _enum : _childsContainers[3]) // SymTagEnum
            {
                displayEnum(_enum, a_nestingLevel);
            }

            if (!_childsContainers[4].empty() && GlobalSettings::s_infoComment)
            {
                if (!_isFirst) { ConsoleManager::print(L"\n"); } _isFirst = false;
                displayTabulation(a_nestingLevel);
                ConsoleManager::print(L"/// TYPEDEFS:\n");
            }

            for (auto& _typedef : _childsContainers[4]) // SymTagTypedef
            {
                displayTypedef(_typedef, a_nestingLevel);
            }

            if (!_childsContainers[2].empty() && GlobalSettings::s_infoComment)
            {
                if (!_isFirst) { ConsoleManager::print(L"\n"); } _isFirst = false;
                displayTabulation(a_nestingLevel);
                ConsoleManager::print(L"/// CLASSES:\n");
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

            if (!_vfuncs.empty() && GlobalSettings::s_infoComment)
            {
                if (!_isFirst) { ConsoleManager::print(L"\n"); } _isFirst = false;
                displayTabulation(a_nestingLevel);
                ConsoleManager::print(L"/// VIRTUALS:\n");
            }

            for (auto& _vfunc : _vfuncs)
            {
                displayFunction(_vfunc, a_nestingLevel);

                if (GlobalSettings::s_isOffsetInfo)
                {
                    // displayTabulation(a_nestingLevel);
                    DWORD _offset = s_noRetValue;
                    if (SUCCEEDED(_vfunc->get_virtualBaseOffset(&_offset)) && _offset != s_noRetValue)
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

            if (!_fields.empty() && GlobalSettings::s_infoComment)
            {
                if (!_isFirst) { ConsoleManager::print(L"\n"); } _isFirst = false;
                displayTabulation(a_nestingLevel);
                ConsoleManager::print(L"/// FIELDS:\n");
            }

            for (auto& _field : _fields) 
            {
                displayTabulation(a_nestingLevel);
                // displayName(_field);

                // DebugManager::WaitDebugger();

                ConsoleManager::print(getSymTypeText(getSymType(_field)).c_str());

                ConsoleManager::print(L"; ");

                if (GlobalSettings::s_isOffsetInfo)
                {
                    LONG _offset = s_noRetValue;
                    if (SUCCEEDED(_field->get_offset(&_offset)) && _offset != s_noRetValue)
                    {
                        ConsoleManager::setCursorNoDiscard(60, -1, GlobalSettings::s_isTabulation);
                        ConsoleManager::print(L"%s0x%X", getCommentName_C(), _offset);
                    }
                }
                ConsoleManager::print(L"\n");
            }

            /// ----------------------------------------  </SymTagData> ----------------------------------------

            int _firstFunc = true;
            for (auto& _function : _childsContainers[1]) // SymTagFunction + !get_virtual
            {
                BOOL _isVirtual = true;
                if (FAILED(_function->get_virtual(&_isVirtual)) || _isVirtual) { continue; }

                if (_firstFunc && GlobalSettings::s_infoComment)
                {
                    if (!_isFirst) { ConsoleManager::print(L"\n"); } _isFirst = false;
                    displayTabulation(a_nestingLevel);
                    ConsoleManager::print(L"/// FUNCS:\n");
                }
                _firstFunc = false;
                
                // displayTabulation(a_nestingLevel);
                registerTypeSource(_function);

                displayFunction(_function, a_nestingLevel);
                ConsoleManager::print(L"\n");
            }

            int _firstStatic = true;
            for (auto& _field : _childsContainers[0]) // SymTagData / static - const
            {
                DWORD _kind = 0;
                if (FAILED(_field->get_dataKind(&_kind)) || _kind == DataIsMember) { continue; }

                if (_firstStatic && GlobalSettings::s_infoComment)
                {
                    if (!_isFirst) { ConsoleManager::print(L"\n"); } _isFirst = false;
                    displayTabulation(a_nestingLevel);
                    ConsoleManager::print(L"/// OTHER MEMBERS:\n");
                }
                _firstStatic = false;

                displayTabulation(a_nestingLevel);

                const wchar_t* _modificator = nullptr;
                switch (_kind)
                {
                case DataIsStaticMember: _modificator = L"static "; break;
                case DataIsConstant: _modificator = L"constexpr "; break;
                default: break;
                }
                
                ConsoleManager::print(_modificator);

                ConsoleManager::print(getSymTypeText(getSymType(_field)).c_str());
                // displayName(_field);

                ConsoleManager::print(L";\n");
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
                ConsoleManager::print(getSymTypeText(getSymType(_param)).c_str());
                // displayName(_param);
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
        IDiaSymbol* _baseType = nullptr;
        if (SUCCEEDED(a_symbol->get_type(&_baseType)))
        {
            return _baseType;
        }
        return nullptr;
    }

    static IDiaSymbol* getClassParent(IDiaSymbol* a_symbol)
    {
        IDiaSymbol* _ret = nullptr;
        if (SUCCEEDED(a_symbol->get_classParent(&_ret)))
        {
            //return _ret ? _ret : (_ret->Release(), nullptr);
            return _ret;
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