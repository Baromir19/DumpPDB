#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

#include <dia2.h>

#include <Util/Com/ComPtr.hpp>
#include <Core/TypeWalker.hpp>
#include <Core/TypeBuilder.hpp>

/// Configuration for dumping output.
struct DumpConfig
{
    bool showSize        = true;
    bool showOffset      = true;
    bool showAccess      = true;
    bool showInfoComment = false;
    bool showNonScoped   = true;
    bool showEnumHex     = false;
    bool showTypeSource  = true;
    bool curlyBraceNewline = true;
    bool hideCompilerGenerated = true; // hide __local_vftable_ctor_closure, etc.
    DWORD baseAccessType = 0; // override access type
    IntStyle intStyle = IntStyle::Cstdint; // __int32 vs int32_t
};

/// Produces formatted C++ declaration strings from DIA symbols.
/// This class replaces the display* methods from the old DiaManager.
/// It has NO dependency on ConsoleManager or any output mechanism.
/// All output is returned as std::wstring.

class SymbolDumper
{
public:
    explicit SymbolDumper(const DumpConfig& a_config = DumpConfig())
        : m_config(a_config)
    {
    }

    void setConfig(const DumpConfig& a_config) { m_config = a_config; }
    const DumpConfig& config() const { return m_config; }

    // --- Top-level dump methods ---

    std::wstring dumpClass(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        std::wstring _ret;
        std::wstring _prevParent = m_parentClassName;
        m_parentClassName = TypeWalker::getName(a_symbol, L"", m_config.showNonScoped);
        
        _ret += tab(a_nestingLevel);
        _ret += sizeComment(a_symbol);

        _ret += tab(a_nestingLevel);
        _ret += modPrefix(a_symbol);
        _ret += udtKeyword(a_symbol);

        std::wstring _typeText;
        try
        {
            _typeText = TypeWalker::resolveType(a_symbol, _prevParent, m_config.showNonScoped).build();
        }
        catch (...)
        {
            _typeText = L"/* <error resolving type> */";
        }
        _ret += _typeText;

        _ret += classInheritance(a_symbol);
        _ret += scopeBegin(a_nestingLevel);

        _ret += dumpMembers(a_symbol, a_nestingLevel + 1);

        _ret += scopeEnd(a_nestingLevel);
        _ret += typeSources();

        m_parentClassName = _prevParent;
        return _ret;
    }

    std::wstring dumpEnum(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        std::wstring _ret;

        _ret += tab(a_nestingLevel);
        _ret += sizeComment(a_symbol);

        _ret += tab(a_nestingLevel);
        _ret += modPrefix(a_symbol);
        _ret += L"enum";

        // Filter synthetic names like <unnamed-tag> or $HASH names
        std::wstring _enumName = TypeWalker::getName(a_symbol, L"", m_config.showNonScoped);
        if (!TypeWalker::isSyntheticName(_enumName))
        {
            _ret += L" ";
            _ret += _enumName;
        }

        _ret += baseTypeInheritance(a_symbol);
        _ret += scopeBegin(a_nestingLevel);

        ComPtr<IDiaEnumSymbols> _enumMembers;
        if (SUCCEEDED(a_symbol->findChildren(SymTagData, nullptr, nsNone, &_enumMembers)))
        {
            ComPtr<IDiaSymbol> _member;
            ULONG _celt = 0;
            while (SUCCEEDED(_enumMembers->Next(1, &_member, &_celt)) && _celt == 1)
            {
                _ret += tab(a_nestingLevel + 1);
                _ret += TypeWalker::getName(_member.get());
                _ret += enumMemberValue(_member.get());
                _ret += L",\n";
            }
        }

        _ret += scopeEnd(a_nestingLevel);
        return _ret;
    }

    std::wstring dumpTypedef(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        std::wstring _ret;
        _ret += tab(a_nestingLevel);
        _ret += modPrefix(a_symbol);
        _ret += L"typedef ";

        // Get the typedef name
        std::wstring _typedefName = TypeWalker::getName(a_symbol, m_parentClassName);

        // Resolve the underlying type (the type this typedef aliases)
        // We need to get the type of the typedef symbol, not the typedef itself
        std::wstring _typeText;
        try
        {
            ComPtr<IDiaSymbol> _underlyingType;
            if (SUCCEEDED(a_symbol->get_type(&_underlyingType)) && _underlyingType)
            {
                // Build the underlying type's full declaration
                TypeBuilder _builder = TypeWalker::resolveType(_underlyingType.get(), m_parentClassName);
                // Set the typedef name as the "variable name" in the declaration
                _builder.name(_typedefName);
                _typeText = _builder.build();
            }
            else
            {
                // Fallback: just use the typedef name itself
                _typeText = _typedefName;
            }
        }
        catch (...)
        {
            _typeText = L"/* <error resolving type> */";
        }
        _ret += _typeText;
        _ret += L";\n";
        return _ret;
    }

    std::wstring dumpFriend(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        std::wstring _ret;
        _ret += tab(a_nestingLevel);
        _ret += modPrefix(a_symbol);
        _ret += L"friend ";

        std::wstring _typeText;
        try
        {
            _typeText = TypeWalker::resolveType(a_symbol, m_parentClassName).build();
        }
        catch (...)
        {
            _typeText = L"/* <error resolving type> */";
        }
        _ret += _typeText;
        _ret += L";\n";
        return _ret;
    }

    std::wstring dumpFunction(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        std::wstring _ret;
        _ret += tab(a_nestingLevel);

        // Virtual/static qualifiers
        const wchar_t* _names[] = { getVirtualName(a_symbol), getStaticName(a_symbol) };
        for (auto _name : _names) { if (_name) { _ret += _name; _ret += L" "; } }

        auto _funtionType = getTypeCom(a_symbol); // SymTagFunctionType

        DWORD _argCount = 0;
        if (_funtionType)
        {
            _argCount = countChildren(_funtionType.get(), SymTagFunctionArgType);
        }

        // Return type
        if (_funtionType)
        {
            ComPtr<IDiaSymbol> _retType;
            if (SUCCEEDED(_funtionType->get_type(&_retType)) && _retType)
            {
                std::wstring _retTypeStr;
                try
                {
                    _retTypeStr = TypeWalker::resolveType(_retType.get()).build();
                }
                catch (...)
                {
                    _retTypeStr = L"/* <error> */";
                }
                _ret += _retTypeStr;
                _ret += L" ";
            }
        }

        _ret += TypeWalker::getName(a_symbol, m_parentClassName);
        _ret += L"(";

        // Named parameters (searched on SymTagFunction itself)
        auto _namedArgCount = dumpFunctionArgsToString(a_symbol, _ret);

        // If named arg count doesn't match the actual function type arg count,
        // fall back to the function type's args (which may have unnamed params).
        // This fixes constructors/copy-constructors where params are on FunctionType
        // but not directly on the Function symbol.
        if (_funtionType && _namedArgCount != (int)_argCount)
        {
            if (_namedArgCount > 0) { _ret += L", "; }
            _ret += TypeWalker::getFuncArgsString(_funtionType.get(), m_config.showNonScoped);
        }

        _ret += L")";

        // Const qualifier on function
        if (_funtionType)
        {
            BOOL _isConst = FALSE;
            if (SUCCEEDED(_funtionType->get_constType(&_isConst)) && _isConst)
            {
                _ret += L" const";
            }
        }

        _ret += L";";

        registerTypeSource(a_symbol);

        return _ret;
    }

    /// Check if a function symbol is compiler-generated (starts with __).
    static bool isCompilerGenerated(IDiaSymbol* a_symbol)
    {
        BSTR _bstrName = nullptr;
        if (SUCCEEDED(a_symbol->get_name(&_bstrName)) && _bstrName)
        {
            std::wstring _name(_bstrName);
            SysFreeString(_bstrName);
            return _name.size() >= 2 && _name[0] == L'_' && _name[1] == L'_';
        }
        return false;
    }

    /// Emit access specifier label if access has changed.
    /// Returns the new lastAccess value.
    DWORD emitAccessLabel(std::wstring& a_out, IDiaSymbol* a_symbol, DWORD a_lastAccess, int a_nestingLevel) const
    {
        if (!m_config.showAccess) return a_lastAccess;

        DWORD _access = 0;
        if (SUCCEEDED(a_symbol->get_access(&_access)) && _access != a_lastAccess)
        {
            a_lastAccess = _access;
            a_out += tab(a_nestingLevel - 1);
            const wchar_t* _accessName = nullptr;
            if (m_config.baseAccessType) { _access = m_config.baseAccessType; }
            switch (_access)
            {
            case CV_private:   _accessName = L"private"; break;
            case CV_protected: _accessName = L"protected"; break;
            case CV_public:    _accessName = L"public"; break;
            case 0:            _accessName = L"public"; break; // undefined !!!
            }
            if (_accessName)
            {
                a_out += _accessName;
                a_out += L":\n";
            }
        }
        return a_lastAccess;
    }

    std::wstring dumpMembers(IDiaSymbol* a_symbol, int a_nestingLevel)
    {
        std::wstring _ret;

        ComPtr<IDiaEnumSymbols> _children;
        if (FAILED(a_symbol->findChildren(SymTagNull, nullptr, nsNone, &_children)))
            return _ret;

        std::vector<ComPtr<IDiaSymbol>> _childContainers[7];

        ComPtr<IDiaSymbol> _child;
        ULONG _celt = 0;
        while (SUCCEEDED(_children->Next(1, &_child, &_celt)) && _celt == 1)
        {
            DWORD _symTag = 0;
            _child->get_symTag(&_symTag);

            try
            {
                switch (_symTag)
                {
                case SymTagData:        _childContainers[0].push_back(_child); break;
                case SymTagFunction:    _childContainers[1].push_back(_child); break;
                case SymTagUDT:         _childContainers[2].push_back(_child); break;
                case SymTagEnum:        _childContainers[3].push_back(_child); break;
                case SymTagTypedef:     _childContainers[4].push_back(_child); break;
                case SymTagFriend:      _childContainers[6].push_back(_child); break;
                default:
                    // SymTagVTable, etc. - skip
                    break;
                }
            }
            catch (...)
            {
                _ret += tab(a_nestingLevel) + L"/* <error processing child symbol> */\n";
            }
        }

        bool _hasContent = false;
        DWORD _lastAccess = (DWORD)-1; // sentinel value - no previous access

        // Friends
        if (!_childContainers[6].empty() && m_config.showInfoComment)
        {
            _hasContent = headerComment(_ret, L" FRIENDS:", a_nestingLevel, _hasContent);
        }
        for (auto& _friend : _childContainers[6])
        {
            _lastAccess = emitAccessLabel(_ret, _friend.get(), _lastAccess, a_nestingLevel);
            _ret += dumpFriend(_friend.get(), a_nestingLevel);
        }

        // Enums
        if (!_childContainers[3].empty() && m_config.showInfoComment)
        {
            _hasContent = headerComment(_ret, L" ENUMS:", a_nestingLevel, _hasContent);
        }
        for (auto& _enum : _childContainers[3])
        {
            _lastAccess = emitAccessLabel(_ret, _enum.get(), _lastAccess, a_nestingLevel);
            _ret += dumpEnum(_enum.get(), a_nestingLevel);
        }

        // Typedefs
        if (!_childContainers[4].empty() && m_config.showInfoComment)
        {
            _hasContent = headerComment(_ret, L" TYPEDEFS:", a_nestingLevel, _hasContent);
        }
        for (auto& _typedef : _childContainers[4])
        {
            _lastAccess = emitAccessLabel(_ret, _typedef.get(), _lastAccess, a_nestingLevel);
            _ret += dumpTypedef(_typedef.get(), a_nestingLevel);
        }

        // Nested classes
        if (!_childContainers[2].empty() && m_config.showInfoComment)
        {
            _hasContent = headerComment(_ret, L" CLASSES:", a_nestingLevel, _hasContent);
        }
        for (auto& _class : _childContainers[2])
        {
            _lastAccess = emitAccessLabel(_ret, _class.get(), _lastAccess, a_nestingLevel);
            _ret += dumpClass(_class.get(), a_nestingLevel);
        }

        // Virtual functions (sorted by vtable offset, filter compiler-generated)
        std::vector<ComPtr<IDiaSymbol>> _vfuncs;
        for (auto& _func : _childContainers[1])
        {
            BOOL _isVirtual = FALSE;
            if (SUCCEEDED(_func->get_virtual(&_isVirtual)) && _isVirtual)
            {
                if (m_config.hideCompilerGenerated && isCompilerGenerated(_func.get())) { continue; }
                _lastAccess = emitAccessLabel(_ret, _func.get(), _lastAccess, a_nestingLevel);
                _vfuncs.push_back(_func);
            }
        }

        std::sort(_vfuncs.begin(), _vfuncs.end(),
            [](const ComPtr<IDiaSymbol>& a, const ComPtr<IDiaSymbol>& b)
            {
                DWORD _offsetA = 0, _offsetB = 0;
                a->get_virtualBaseOffset(&_offsetA);
                b->get_virtualBaseOffset(&_offsetB);
                return _offsetA < _offsetB;
            });

        if (!_vfuncs.empty() && m_config.showInfoComment)
        {
            _hasContent = headerComment(_ret, L" VIRTUALS:", a_nestingLevel, _hasContent);
        }
        for (auto& _vfunc : _vfuncs)
        {
            _lastAccess = emitAccessLabel(_ret, _vfunc.get(), _lastAccess, a_nestingLevel);
            _ret += dumpFunction(_vfunc.get(), a_nestingLevel);
            if (m_config.showOffset)
            {
                DWORD _offset = 0xFFFFFFFC;
                if (SUCCEEDED(_vfunc->get_virtualBaseOffset(&_offset)) && _offset != 0xFFFFFFFC)
                {
                    wchar_t _buf[32];
                    swprintf_s(_buf, L" // 0x%X", _offset);
                    _ret += _buf;
                }
            }
            _ret += L"\n";
        }

        // Fields (SymTagData, DataIsMember, sorted by offset)
        std::vector<ComPtr<IDiaSymbol>> _fields;
        for (auto& _field : _childContainers[0])
        {
            DWORD _kind = 0;
            if (SUCCEEDED(_field->get_dataKind(&_kind)) && _kind == DataIsMember)
            {
                _fields.push_back(_field);
            }
        }

        std::sort(_fields.begin(), _fields.end(),
            [](const ComPtr<IDiaSymbol>& a, const ComPtr<IDiaSymbol>& b)
            {
                LONG _offsetA = 0, _offsetB = 0;
                a->get_offset(&_offsetA);
                b->get_offset(&_offsetB);
                return _offsetA < _offsetB;
            });

        if (!_fields.empty() && m_config.showInfoComment)
        {
            _hasContent = headerComment(_ret, L" FIELDS:", a_nestingLevel, _hasContent);
        }
        for (auto& _field : _fields)
        {
            _lastAccess = emitAccessLabel(_ret, _field.get(), _lastAccess, a_nestingLevel);

            _ret += tab(a_nestingLevel);
            try
            {
                _ret += TypeWalker::resolveType(_field.get(), m_parentClassName).build();
            }
            catch (...)
            {
                _ret += L"/* <error resolving field type> */";
            }
            _ret += L"; ";

            if (m_config.showOffset)
            {
                LONG _offset = 0xFFFFFFFC;
                if (SUCCEEDED(_field->get_offset(&_offset)) && _offset != 0xFFFFFFFC)
                {
                    wchar_t _buf[32];
                    swprintf_s(_buf, L"// 0x%X", _offset);
                    // Padding to column 60
                    size_t _padNeeded = _ret.length() < 60 ? 60 - _ret.length() : 1;
                    _ret.append(_padNeeded, L' ');
                    _ret += _buf;
                }
            }
            _ret += L"\n";
        }

        // Non-virtual functions (filter compiler-generated like __local_vftable_ctor_closure)
        bool _firstFunc = true;
        for (auto& _func : _childContainers[1])
        {
            BOOL _isVirtual = TRUE;
            if (FAILED(_func->get_virtual(&_isVirtual)) || _isVirtual) { continue; }

            // Skip compiler-generated functions (e.g. __local_vftable_ctor_closure)
            if (m_config.hideCompilerGenerated && isCompilerGenerated(_func.get())) { continue; }

            if (_firstFunc && m_config.showInfoComment)
            {
                _hasContent = headerComment(_ret, L" FUNCS:", a_nestingLevel, _hasContent);
                _firstFunc = false;
            }

            _ret += dumpFunction(_func.get(), a_nestingLevel);
            _ret += L"\n";
        }

        // Static/const data members
        bool _firstStatic = true;
        for (auto& _field : _childContainers[0])
        {
            DWORD _kind = 0;
            if (FAILED(_field->get_dataKind(&_kind)) || _kind == DataIsMember) { continue; }

            if (_firstStatic && m_config.showInfoComment)
            {
                _hasContent = headerComment(_ret, L" OTHER MEMBERS:", a_nestingLevel, _hasContent);
                _firstStatic = false;
            }

            _ret += tab(a_nestingLevel);

            switch (_kind)
            {
            case DataIsStaticMember: _ret += L"static "; break;
            case DataIsConstant: _ret += L"constexpr "; break;
            default: break;
            }

            try
            {
                _ret += TypeWalker::resolveType(_field.get(), m_parentClassName).build();
            }
            catch (...)
            {
                _ret += L"/* <error> */";
            }
            _ret += L";\n";
        }

        return _ret;
    }

    /// Register source file info for a symbol (stores for later output).
    void registerTypeSource(IDiaSymbol* a_symbol)
    {
        if (!m_config.showTypeSource) return;

        ComPtr<IDiaEnumLineNumbers> _enumLines;
        ComPtr<IDiaSourceFile> _sourceFile;
        ComPtr<IDiaLineNumber> _lineNumber;

        DWORD _addressSection = 0;
        DWORD _addressOffset = 0;

        if (SUCCEEDED(a_symbol->get_addressSection(&_addressSection)) &&
            SUCCEEDED(a_symbol->get_addressOffset(&_addressOffset)))
        {
            if (m_session && SUCCEEDED(m_session->findLinesByAddr(
                _addressSection, _addressOffset, 1, &_enumLines)) && _enumLines)
            {
                ULONG _celt = 0;
                if (SUCCEEDED(_enumLines->Next(1, &_lineNumber, &_celt)) && _celt == 1)
                {
                    if (SUCCEEDED(_lineNumber->get_sourceFile(&_sourceFile)) && _sourceFile)
                    {
                        BSTR _filename;
                        if (SUCCEEDED(_sourceFile->get_fileName(&_filename)))
                        {
                            // Convert BSTR to std::wstring immediately to avoid
                            // ownership issues (double-free, use-after-free, leaks)
                            m_typeSources.emplace_back(_filename, SysStringLen(_filename));
                            SysFreeString(_filename);
                        }
                    }
                }
            }
        }
    }

    void setSession(IDiaSession* a_session) { m_session = a_session; }

    std::wstring typeSources()
    {
        if (m_typeSources.empty()) return L"";

        std::wstring _ret;
        for (const auto& _src : m_typeSources)
        {
            _ret += L"// ";
            _ret += _src;
            _ret += L"\n";
        }
        m_typeSources.clear();
        return _ret;
    }

    void processType(IDiaSymbol* a_symbol, std::wstring& a_output)
    {
        DWORD _symTag = 0;
        if (SUCCEEDED(a_symbol->get_symTag(&_symTag)))
        {
            switch (_symTag)
            {
            case SymTagTypedef: a_output += dumpTypedef(a_symbol); break;
            case SymTagUDT:    a_output += dumpClass(a_symbol); break;
            case SymTagEnum:   a_output += dumpEnum(a_symbol); break;
            case SymTagData:
            {
                std::wstring _typeText;
                try { _typeText = TypeWalker::resolveType(a_symbol).build(); }
                catch (...) { _typeText = L"/* <error> */"; }
                a_output += _typeText;
                break;
            }
            case SymTagFunction:
                a_output += dumpFunction(a_symbol);
                break;
            }
        }
    }

    // --- Helpers ---

private:
    std::wstring tab(int a_repeat = 1) const
    {
        return std::wstring(a_repeat * 4, L' ');
    }

    std::wstring sizeComment(IDiaSymbol* a_symbol) const
    {
        if (!m_config.showSize) return L"";

        ULONGLONG _len;
        if (SUCCEEDED(a_symbol->get_length(&_len)))
        {
            wchar_t _buf[64];
            swprintf_s(_buf, L"// size: %llu byte\n", _len);
            return _buf;
        }
        return L"";
    }

    std::wstring modPrefix(IDiaSymbol* a_symbol) const
    {
        std::wstring _ret;
        BOOL _isConst = FALSE;
        BOOL _isVol = FALSE;
        if (SUCCEEDED(a_symbol->get_constType(&_isConst)) && _isConst) _ret += L"const ";
        if (SUCCEEDED(a_symbol->get_volatileType(&_isVol)) && _isVol) _ret += L"volatile ";
        return _ret;
    }

    std::wstring udtKeyword(IDiaSymbol* a_symbol) const
    {
        auto _name = TypeWalker::getUDTKindName(a_symbol);
        if (_name)
        {
            std::wstring _ret = _name;
            _ret += L" ";
            return _ret;
        }

        // Check for anonymous union/struct
        if (TypeWalker::isAnonymousUDT(a_symbol))
        {
            DWORD _udtKind = 0;
            a_symbol->get_udtKind(&_udtKind);
            switch (_udtKind)
            {
            case UdtStruct: return L"struct ";
            case UdtUnion:  return L"union ";
            default: break;
            }
        }

        return L"";
    }

    std::wstring classInheritance(IDiaSymbol* a_symbol) const
    {
        std::wstring _ret;
        ComPtr<IDiaEnumSymbols> _baseEnum;

        bool _isBegin = true;
        if (SUCCEEDED(a_symbol->findChildren(SymTagBaseClass, nullptr, nsNone, &_baseEnum)))
        {
            ComPtr<IDiaSymbol> _baseSymbol;
            ULONG _celt = 0;
            while (SUCCEEDED(_baseEnum->Next(1, &_baseSymbol, &_celt)) && _celt == 1)
            {
                _ret += _isBegin ? L" : " : L", ";
                _isBegin = false;

                auto _access = TypeWalker::getAccessName(_baseSymbol.get(), m_config.baseAccessType);
                if (_access) { _ret += _access; _ret += L" "; }

                _ret += TypeWalker::getName(_baseSymbol.get());
            }
        }
        return _ret;
    }

    std::wstring baseTypeInheritance(IDiaSymbol* a_symbol) const
    {
        // if (!m_config.showInfoComment) return L"";

        auto _base = TypeWalker::getBaseTypeName(a_symbol);
        if (_base)
        {
            std::wstring _ret = L" : ";
            _ret += _base;
            return _ret;
        }
        return L"";
    }

    std::wstring scopeBegin(int a_nestingLevel)
    {
        std::wstring _ret;
        if (m_config.curlyBraceNewline)
        {
            _ret += L"\n";
            _ret += tab(a_nestingLevel);
        }
        else
        {
            _ret += L" ";
        }
        _ret += L"{\n";
        return _ret;
    }

    std::wstring scopeEnd(int a_nestingLevel)
    {
        std::wstring _ret = tab(a_nestingLevel);
        _ret += L"};\n";
        return _ret;
    }

    int dumpFunctionArgsToString(IDiaSymbol* a_symbol, std::wstring& a_out)
    {
        int _count = 0;
        bool _isFirst = true;

        ComPtr<IDiaEnumSymbols> _enumParams;
        if (SUCCEEDED(a_symbol->findChildren(SymTagData, nullptr, nsNone, &_enumParams)))
        {
            ComPtr<IDiaSymbol> _param;
            ULONG _fetched = 0;
            while (SUCCEEDED(_enumParams->Next(1, &_param, &_fetched)) && _fetched == 1)
            {
                DWORD _kind = 0;
                if (SUCCEEDED(_param->get_dataKind(&_kind)) && _kind == DataIsParam)
                {
                    ++_count;
                    if (!_isFirst) { a_out += L", "; }

                    try
                    {
                        a_out += TypeWalker::resolveType(_param.get(), m_parentClassName).build();
                    }
                    catch (...)
                    {
                        a_out += L"/* <error> */";
                    }

                    _isFirst = false;
                }
            }
        }

        return _count;
    }

    std::wstring enumMemberValue(IDiaSymbol* a_symbol) const
    {
        VARIANT v;
        VariantInit(&v);
        if (SUCCEEDED(a_symbol->get_value(&v)))
        {
            std::wstring _ret;
            if (m_config.showEnumHex)
            {
                wchar_t _buf[32];
                swprintf_s(_buf, L" = 0x%Xll", v.llVal);
                _ret = _buf;
            }
            else
            {
                switch (v.vt)
                {
                case VT_I4:  { wchar_t _buf[32]; swprintf_s(_buf, L" = %d", v.lVal);   _ret = _buf; break; }
                case VT_UI4: { wchar_t _buf[32]; swprintf_s(_buf, L" = %u", v.ulVal);  _ret = _buf; break; }
                case VT_I2:  { wchar_t _buf[32]; swprintf_s(_buf, L" = %d", v.iVal);   _ret = _buf; break; }
                case VT_UI2: { wchar_t _buf[32]; swprintf_s(_buf, L" = %u", v.uiVal);  _ret = _buf; break; }
                case VT_I1:  { wchar_t _buf[32]; swprintf_s(_buf, L" = %d", v.bVal);   _ret = _buf; break; }
                case VT_UI1: { wchar_t _buf[32]; swprintf_s(_buf, L" = %u", v.bVal);   _ret = _buf; break; }
                default: break;
                }
            }
            VariantClear(&v);
            return _ret;
        }
        return L"";
    }

    bool headerComment(std::wstring& a_out, const wchar_t* a_label, int a_nesting, bool a_hasContent)
    {
        if (a_hasContent) { a_out += L"\n"; }
        a_out += tab(a_nesting);
        a_out += L"///";
        a_out += a_label;
        a_out += L"\n";
        return true;
    }

    static const wchar_t* getVirtualName(IDiaSymbol* a_symbol)
    {
        BOOL _isVirt;
        return SUCCEEDED(a_symbol->get_virtual(&_isVirt)) && _isVirt ? L"virtual" : nullptr;
    }

    static const wchar_t* getStaticName(IDiaSymbol* a_symbol)
    {
        BOOL _isStatic;
        return SUCCEEDED(a_symbol->get_isStatic(&_isStatic)) && _isStatic ? L"static" : nullptr;
    }

    static ComPtr<IDiaSymbol> getTypeCom(IDiaSymbol* a_symbol)
    {
        ComPtr<IDiaSymbol> _type;
        if (SUCCEEDED(a_symbol->get_type(&_type))) return _type;
        return ComPtr<IDiaSymbol>();
    }

    static DWORD countChildren(IDiaSymbol* a_symbol, enum SymTagEnum a_tag)
    {
        DWORD _count = 0;
        ComPtr<IDiaEnumSymbols> _enum;
        if (SUCCEEDED(a_symbol->findChildren(a_tag, nullptr, nsNone, &_enum)) && _enum)
        {
            ComPtr<IDiaSymbol> _child;
            ULONG _celt = 0;
            while (SUCCEEDED(_enum->Next(1, &_child, &_celt)) && _celt == 1)
            {
                ++_count;
            }
        }
        return _count;
    }

    DumpConfig m_config;
    std::wstring m_parentClassName;
    std::vector<std::wstring> m_typeSources;
    IDiaSession* m_session = nullptr;
};