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
    bool m_showSize        = true;
    bool m_showOffset      = true;
    bool m_showAccess      = true;
    bool m_showInfoComment = false;
    bool m_showNonScoped   = true;
    bool m_showEnumHex     = false;
    bool m_showTypeSource  = false;
    bool m_curlyBraceNewline = true;
    bool m_hideCompilerGenerated = true; // hide __local_vftable_ctor_closure, etc.
    DWORD m_baseAccessType = 0; // override access type
    IntStyle m_intStyle = IntStyle::Cstdint; // __int32 vs int32_t
};

/// Produces formatted C++ declaration strings from DIA symbols.

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
        std::wstring ret;

        // Get class name relative to current scope
        std::wstring className = TypeWalker::getName(a_symbol, m_scope, m_config.m_showNonScoped);

        ret += tab(a_nestingLevel);
        ret += sizeComment(a_symbol);

        ret += tab(a_nestingLevel);
        ret += modPrefix(a_symbol);
        ret += udtKeyword(a_symbol);
        ret += className;

        ret += classInheritance(a_symbol);
        ret += scopeBegin(a_nestingLevel);

        // Push this class onto the scope stack
        m_scope.push(className);
        ret += dumpMembers(a_symbol, a_nestingLevel + 1);
        m_scope.pop();

        ret += scopeEnd(a_nestingLevel);
        ret += typeSources();

        return ret;
    }

    /// Dump an anonymous UDT (union/struct) as an inline block, without a name.
    /// Used when a data member's type is itself an anonymous union/struct
    /// (e.g. compiler-generated $HASH types wrapping bitfields).
    /// Emits "struct { ... };" or "union { ... };" with no variable name.
    std::wstring dumpAnonymousUDT(IDiaSymbol* a_udtSymbol, int a_nestingLevel)
    {
        std::wstring ret;

        ret += tab(a_nestingLevel);
        ret += modPrefix(a_udtSymbol);
        ret += udtKeyword(a_udtSymbol); // "struct " / "union " — no name follows

        ret += scopeBegin(a_nestingLevel);
        ret += dumpMembers(a_udtSymbol, a_nestingLevel + 1);
        ret += scopeEnd(a_nestingLevel);

        return ret;
    }

    std::wstring dumpEnum(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        std::wstring ret;

        ret += tab(a_nestingLevel);
        ret += sizeComment(a_symbol);

        ret += tab(a_nestingLevel);
        ret += modPrefix(a_symbol);
        ret += L"enum";

        // Filter synthetic names like <unnamed-tag> or $HASH names
        std::wstring enum_symbolsName = TypeWalker::getName(a_symbol, m_scope, m_config.m_showNonScoped);
        if (!TypeWalker::isSyntheticName(enum_symbolsName))
        {
            ret += L" ";
            ret += enum_symbolsName;
        }

        ret += baseTypeInheritance(a_symbol);
        ret += scopeBegin(a_nestingLevel);

        ComPtr<IDiaEnumSymbols> enum_symbolsMembers;
        if (SUCCEEDED(a_symbol->findChildren(SymTagData, nullptr, nsNone, &enum_symbolsMembers)))
        {
            ComPtr<IDiaSymbol> member;
            ULONG celt = 0;
            while (SUCCEEDED(enum_symbolsMembers->Next(1, &member, &celt)) && celt == 1)
            {
                ret += tab(a_nestingLevel + 1);
                ret += TypeWalker::getName(member.get(), m_scope);
                ret += constantValueSuffix(member.get());
                ret += L",\n";
            }
        }

        ret += scopeEnd(a_nestingLevel);
        return ret;
    }

    std::wstring dumpTypedef(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        std::wstring ret;
        ret += tab(a_nestingLevel);
        ret += modPrefix(a_symbol);
        ret += L"typedef ";

        // Get the typedef name
        std::wstring typedefName = TypeWalker::getName(a_symbol, m_scope);

        // Resolve the underlying type (the type this typedef aliases)
        // We need to get the type of the typedef symbol, not the typedef itself
        std::wstring typeText;
        try
        {
            ComPtr<IDiaSymbol> underlyingType;
            if (SUCCEEDED(a_symbol->get_type(&underlyingType)) && underlyingType)
            {
                // Build the underlying type's full declaration
                TypeBuilder builder = TypeWalker::resolveType(
                    underlyingType.get(), 
                    m_scope, 
                    true, 
                    m_config.m_intStyle
                );
                // Set the typedef name as the "variable name" in the declaration
                builder.name(typedefName);
                typeText = builder.build();
            }
            else
            {
                // Fallback: just use the typedef name itself
                typeText = typedefName;
            }
        }
        catch (...)
        {
            typeText = L"/* <error resolving type> */";
        }
        ret += typeText;
        ret += L";\n";
        return ret;
    }

    std::wstring dumpFriend(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        std::wstring ret;
        ret += tab(a_nestingLevel);
        ret += modPrefix(a_symbol);
        ret += L"friend ";

        std::wstring typeText;
        try
        {
            typeText = TypeWalker::resolveType(
                a_symbol, 
                m_scope, 
                true, 
                m_config.m_intStyle
            ).build();
        }
        catch (...)
        {
            typeText = L"/* <error resolving type> */";
        }
        ret += typeText;
        ret += L";\n";
        return ret;
    }

    std::wstring dumpFunction(IDiaSymbol* a_symbol, int a_nestingLevel = 0)
    {
        std::wstring ret;
        ret += tab(a_nestingLevel);

        // Virtual/static qualifiers
        const wchar_t* names[] = { getVirtualName(a_symbol), getStaticName(a_symbol) };
        for (auto name : names) { if (name) { ret += name; ret += L" "; } }

        auto functionType = getTypeCom(a_symbol); // SymTagFunctionType

        DWORD argCount = 0;
        if (functionType)
        {
            argCount = countChildren(functionType.get(), SymTagFunctionArgType);
        }

        // Return type
        std::wstring funcName = TypeWalker::getName(a_symbol, m_scope);
        BOOL isCtor = FALSE;
        a_symbol->get_constructor(&isCtor);
        bool isDtor = !funcName.empty() && funcName[0] == L'~';

        if (!isCtor && !m_scope.empty() && funcName == TypeWalker::leafName(m_scope.top()))
        {
            isCtor = TRUE;
        }

        // printf("%s\n", isCtor ? "true" : "false");

        // Return type — skipped for constructors/destructors
        if (!isCtor && !isDtor && functionType)
        {
            ComPtr<IDiaSymbol> retType;
            if (SUCCEEDED(functionType->get_type(&retType)) && retType)
            {
                std::wstring retTypeStr;
                try
                {
                    retTypeStr = TypeWalker::resolveType(
                        retType.get(), 
                        m_scope,
                        m_config.m_showNonScoped, 
                        m_config.m_intStyle
                    ).build();
                }
                catch (...)
                {
                    retTypeStr = L"/* <error> */";
                }
                ret += retTypeStr;
                ret += L" ";
            }
        }

        ret += funcName;
        ret += L"(";

        // Named parameters (searched on SymTagFunction itself)
        auto namedArgCount = dumpFunctionArgsToString(a_symbol, ret);

        // If named arg count doesn't match the actual function type arg count,
        // fall back to the function type's args (which may have unnamed params).
        // This fixes constructors/copy-constructors where params are on FunctionType
        // but not directly on the Function symbol.
        if (functionType && namedArgCount != (int)argCount)
        {
            if (namedArgCount > 0) { ret += L", "; }
            ret += TypeWalker::getFuncArgsString(
                functionType.get(), 
                m_scope, 
                m_config.m_showNonScoped,
                m_config.m_intStyle
            );
        }

        ret += L")";

        // Const qualifier on function
        if (functionType)
        {
            BOOL isConst = FALSE;
            if (SUCCEEDED(functionType->get_constType(&isConst)) && isConst)
            {
                ret += L" const";
            }


            BOOL isVolatile = FALSE;
            if (SUCCEEDED(functionType->get_volatileType(&isVolatile)) && isVolatile)
            {
                ret += L" volatile";
            }
        }

        // NOTE: noexcept support
        {
            IDiaSymbol4* symbol4 = nullptr;
            if (SUCCEEDED(a_symbol->QueryInterface(__uuidof(IDiaSymbol4), (void**)&symbol4)) && symbol4)
            {
                BOOL isNoExcept = FALSE;
                if (SUCCEEDED(symbol4->get_noexcept(&isNoExcept)) && isNoExcept)
                {
                    ret += L" noexcept";
                }
                symbol4->Release();
            }
        }

        BOOL isVirtual = FALSE;
        a_symbol->get_virtual(&isVirtual);
        if (isVirtual)
        {
            BOOL isIntro = TRUE;  // TRUE = new, FALSE = old one
            a_symbol->get_intro(&isIntro);

            BOOL isSealed = FALSE;
            a_symbol->get_sealed(&isSealed);

            if (!isIntro) { ret += L" override"; }
            if (isSealed) { ret += L" final"; }

            // pure virtual
            BOOL isPure = FALSE;
            a_symbol->get_pure(&isPure);
            if (isPure) { ret += L" = 0"; }
        }

        ret += L";";

        registerTypeSource(a_symbol);

        return ret;
    }

    /// Check if a function symbol is compiler-generated (starts with __).
    static bool isCompilerGenerated(IDiaSymbol* a_symbol)
    {
        BOOL isGen = FALSE;
        if (SUCCEEDED(a_symbol->get_compilerGenerated(&isGen)) && isGen)
            return true;

        /*
        BSTR bstrName = nullptr;
        if (SUCCEEDED(a_symbol->get_name(&bstrName)) && bstrName)
        {
            std::wstring name(bstrName);
            SysFreeString(bstrName);
            return name.size() >= 2 && name[0] == L'_' && name[1] == L'_';
        }*/
        return false;
    }

    /// Emit access specifier label if access has changed.
    /// Returns the new lastAccess value.
    DWORD emitAccessLabel(std::wstring& aout, IDiaSymbol* a_symbol, DWORD alastAccess, int a_nestingLevel) const
    {
        if (!m_config.m_showAccess) return alastAccess;

        DWORD access = 0;
        if (SUCCEEDED(a_symbol->get_access(&access)) && access != alastAccess)
        {
            alastAccess = access;
            aout += tab(a_nestingLevel - 1);
            const wchar_t* accessName = nullptr;
            if (m_config.m_baseAccessType) { access = m_config.m_baseAccessType; }
            switch (access)
            {
            case CV_private:   accessName = L"private"; break;
            case CV_protected: accessName = L"protected"; break;
            case CV_public:    accessName = L"public"; break;
            case 0:            accessName = L"public"; break; // undefined !!!
            }
            if (accessName)
            {
                aout += accessName;
                aout += L":\n";
            }
        }
        return alastAccess;
    }

    std::wstring dumpMembers(IDiaSymbol* a_symbol, int a_nestingLevel)
    {
        std::wstring ret;

        ComPtr<IDiaEnumSymbols> children;
        if (FAILED(a_symbol->findChildren(SymTagNull, nullptr, nsNone, &children)))
            return ret;

        std::vector<ComPtr<IDiaSymbol>> childContainers[7];

        ComPtr<IDiaSymbol> child;
        ULONG celt = 0;
        while (SUCCEEDED(children->Next(1, &child, &celt)) && celt == 1)
        {
            DWORD symTag = 0;
            child->get_symTag(&symTag);

            try
            {
                switch (symTag)
                {
                case SymTagData:        childContainers[0].push_back(child); break;
                case SymTagFunction:    childContainers[1].push_back(child); break;
                case SymTagUDT:         childContainers[2].push_back(child); break;
                case SymTagEnum:        childContainers[3].push_back(child); break;
                case SymTagTypedef:     childContainers[4].push_back(child); break;
                case SymTagFriend:      childContainers[6].push_back(child); break;
                default:
                    // SymTagVTable, etc. - skip
                    break;
                }
            }
            catch (...)
            {
                ret += tab(a_nestingLevel) + L"/* <error processing child symbol> */\n";
            }
        }

        bool hasContent = false;
        DWORD lastAccess = (DWORD)-1; // sentinel value - no previous access

        // Friends
        if (!childContainers[6].empty() && m_config.m_showInfoComment)
        {
            hasContent = headerComment(ret, L" FRIENDS:", a_nestingLevel, hasContent);
        }
        for (auto& _friend : childContainers[6])
        {
            lastAccess = emitAccessLabel(ret, _friend.get(), lastAccess, a_nestingLevel);
            ret += dumpFriend(_friend.get(), a_nestingLevel);
        }

        // Enums
        if (!childContainers[3].empty() && m_config.m_showInfoComment)
        {
            hasContent = headerComment(ret, L" ENUMS:", a_nestingLevel, hasContent);
        }
        for (auto& enum_symbols : childContainers[3])
        {
            lastAccess = emitAccessLabel(ret, enum_symbols.get(), lastAccess, a_nestingLevel);
            ret += dumpEnum(enum_symbols.get(), a_nestingLevel);
        }

        // Typedefs
        if (!childContainers[4].empty() && m_config.m_showInfoComment)
        {
            hasContent = headerComment(ret, L" TYPEDEFS:", a_nestingLevel, hasContent);
        }
        for (auto& _typedef : childContainers[4])
        {
            lastAccess = emitAccessLabel(ret, _typedef.get(), lastAccess, a_nestingLevel);
            ret += dumpTypedef(_typedef.get(), a_nestingLevel);
        }

        // Nested classes
        if (!childContainers[2].empty() && m_config.m_showInfoComment)
        {
            hasContent = headerComment(ret, L" CLASSES:", a_nestingLevel, hasContent);
        }
        for (auto& _class : childContainers[2])
        {
            lastAccess = emitAccessLabel(ret, _class.get(), lastAccess, a_nestingLevel);
            ret += dumpClass(_class.get(), a_nestingLevel);
        }

        // Virtual functions (sorted by vtable offset, filter compiler-generated)
        std::vector<ComPtr<IDiaSymbol>> vfuncs;
        for (auto& func : childContainers[1])
        {
            BOOL isVirtual = FALSE;
            if (SUCCEEDED(func->get_virtual(&isVirtual)) && isVirtual)
            {
                if (m_config.m_hideCompilerGenerated && isCompilerGenerated(func.get())) { continue; }
                // lastAccess = emitAccessLabel(ret, func.get(), lastAccess, a_nestingLevel);
                vfuncs.push_back(func);
            }
        }

        std::sort(vfuncs.begin(), vfuncs.end(),
            [](const ComPtr<IDiaSymbol>& a, const ComPtr<IDiaSymbol>& b)
            {
                DWORD offsetA = 0, offsetB = 0;
                a->get_virtualBaseOffset(&offsetA);
                b->get_virtualBaseOffset(&offsetB);
                return offsetA < offsetB;
            });

        if (!vfuncs.empty() && m_config.m_showInfoComment)
        {
            hasContent = headerComment(ret, L" VIRTUALS:", a_nestingLevel, hasContent);
        }
        for (auto& vfunc : vfuncs)
        {
            lastAccess = emitAccessLabel(ret, vfunc.get(), lastAccess, a_nestingLevel);
            ret += dumpFunction(vfunc.get(), a_nestingLevel);
            if (m_config.m_showOffset)
            {
                DWORD offset = 0xFFFFFFFC;
                if (SUCCEEDED(vfunc->get_virtualBaseOffset(&offset)) && offset != 0xFFFFFFFC)
                {
                    wchar_t buf[32];
                    swprintf_s(buf, L" // 0x%X", offset);
                    ret += buf;
                }
            }
            ret += L"\n";
        }

        // Fields (SymTagData, DataIsMember)
        std::vector<ComPtr<IDiaSymbol>> fields;
        for (auto& field : childContainers[0])
        {
            DWORD _kind = 0;
            if (SUCCEEDED(field->get_dataKind(&_kind)) && _kind == DataIsMember)
                fields.push_back(field);
        }

        if (!fields.empty() && m_config.m_showInfoComment)
            hasContent = headerComment(ret, L" FIELDS:", a_nestingLevel, hasContent);

        struct FieldGroup {
            std::vector<ComPtr<IDiaSymbol>> fields;
            LONG beginOffset;
            LONG endOffset;
        };

        struct FieldBranch {
            std::vector<FieldGroup> groups; // 1 элемент — не union, 2+ — union
        };

        std::vector<FieldBranch> branches;
        {
            auto isBitfieldOffset = [](ComPtr<IDiaSymbol>& f) -> bool {
                DWORD bitPos = 0;
                ULONGLONG bitWidth = 0;
                if (SUCCEEDED(f->get_bitPosition(&bitPos)) &&
                    SUCCEEDED(f->get_length(&bitWidth)) && bitWidth < 64) {
                    return bitPos > 0;
                }
                return false;
                };

            auto fieldEnd = [](ComPtr<IDiaSymbol>& f) -> LONG {
                LONG off = 0;
                ULONGLONG length = 0;
                f->get_offset(&off);
                f->get_length(&length);
                return off + (LONG)length;
                };

            FieldBranch curBranch;
            LONG branchMaxEnd = LONG_MIN;
            bool isCurrentBranch = false;
            bool startsNewBranch = true;

            for (size_t i = 0; i < fields.size(); ++i) {
                auto& field = fields[i];
                LONG off = 0;
                field->get_offset(&off);

                ULONGLONG length = 0;

                if (isBitfieldOffset(field))
                {
                    field->get_length(&length);
                }
                else
                {
                    ComPtr<IDiaSymbol> type;
                    field->get_type(&type);

                    type->get_length(&length);
                }

                if (startsNewBranch && !curBranch.groups.empty())
                {
                    branches.push_back(curBranch);
                    curBranch = {};
                }

                if (!startsNewBranch)
                {
                    auto nextIdx = i + 1;

                    startsNewBranch = true;

                    for (size_t j = nextIdx; j < fields.size(); ++j)
                    {
                        LONG futureOff = 0;

                        auto& currentField = fields[j];

                        currentField->get_offset(&futureOff);

                        if (off == futureOff) // end. Do not touch j
                        {
                            auto lastGroupIdx = j - 1;
                            auto& lastGroupField = fields[lastGroupIdx];

                            FieldGroup group;

                            LONG offsetGroupEnd = 0;
                            lastGroupField->get_offset(&offsetGroupEnd);

                            ULONGLONG lengthGroupEnd = 0;

                            if (isBitfieldOffset(lastGroupField))
                            {
                                field->get_length(&length);
                            }
                            else
                            {
                                ComPtr<IDiaSymbol> type;
                                lastGroupField->get_type(&type);

                                type->get_length(&lengthGroupEnd);
                            }

                            group.beginOffset = off;
                            group.endOffset = offsetGroupEnd + lengthGroupEnd;

                            for (size_t k = i; k < j; ++k)
                            {
                                group.fields.push_back(fields[k]);
                            }

                            curBranch.groups.push_back(group);
                            startsNewBranch = false;
                            i = lastGroupIdx; // because of increment
                            break;
                        }
                    }

                    if (!startsNewBranch)
                    {
                        continue;
                    }

                    LONG maxEndOffset = LONG_MIN;

                    for (const auto& groups : curBranch.groups)
                    {
                        maxEndOffset = maxEndOffset > groups.endOffset ? maxEndOffset : groups.endOffset;
                    }

                    FieldGroup group;
                    group.beginOffset = off;
                    group.endOffset = maxEndOffset;
                    if (nextIdx >= fields.size())
                    {
                        group.fields.push_back(fields[i]); // only one
                        curBranch.groups.push_back(group);
                    }
                    else
                    {
                        for (size_t j = nextIdx; j < fields.size(); ++j)
                        {
                            LONG futureOff = 0;
                            auto& currentField = fields[j];

                            currentField->get_offset(&futureOff);

                            if (futureOff >= maxEndOffset)
                            {
                                auto lastGroupIdx = j - 1;

                                for (size_t k = i; k < j; ++k)
                                {
                                    group.fields.push_back(fields[k]);
                                }

                                curBranch.groups.push_back(group);
                                i = lastGroupIdx;
                                break;
                            }
                        }
                    }

                    continue;
                }

                startsNewBranch = true;

                // main logic

                auto nextIdx = i + 1;

                for (size_t j = nextIdx; j < fields.size(); ++j)
                {
                    LONG futureOff = 0;

                    auto& currentField = fields[j];

                    currentField->get_offset(&futureOff);

                    if (off == futureOff) // end. Do not touch j
                    {
                        auto lastGroupIdx = j - 1;
                        auto& lastGroupField = fields[lastGroupIdx];

                        FieldGroup group;

                        LONG offsetGroupEnd = 0;
                        lastGroupField->get_offset(&offsetGroupEnd);

                        ULONGLONG lengthGroupEnd = 0;

                        if (isBitfieldOffset(lastGroupField))
                        {
                            field->get_length(&length);
                        }
                        else
                        {
                            ComPtr<IDiaSymbol> type;
                            lastGroupField->get_type(&type);

                            type->get_length(&lengthGroupEnd);
                        }

                        group.beginOffset = off;
                        group.endOffset = offsetGroupEnd + lengthGroupEnd;

                        for (size_t k = i; k < j; ++k)
                        {
                            group.fields.push_back(fields[k]);
                        }

                        curBranch.groups.push_back(group);
                        startsNewBranch = false;
                        i = lastGroupIdx;
                        break;
                    }
                }

                if (!startsNewBranch)
                {
                    continue;
                }

                FieldGroup group;
                group.fields.push_back(field);
                group.beginOffset = off;
                group.endOffset = off + length;

                curBranch.groups.push_back(group);
            }

            if (!curBranch.groups.empty()) 
            { 
                branches.push_back(std::move(curBranch)); 
            }
        }

        for (const auto& branch : branches)
        {
            printf(" - branch\n");
            for (const auto& group : branch.groups)
            {
                printf("  - group (begin: %d, end: %d)\n", group.beginOffset, group.endOffset);
                for (const auto& field : group.fields)
                {
                    LONG off = 0;
                    field->get_offset(&off);
                    printf("   - field: %d\n", off);
                }
            }
        }

        // Helper lambda: emit one field (handles anonymous UDT inline blocks).
        auto emitField = [&](const ComPtr<IDiaSymbol>& field, int _level)
        {
            lastAccess = emitAccessLabel(ret, field.get(), lastAccess, _level);

            // Check if this field's type is itself an anonymous union/struct ($HASH names).
            ComPtr<IDiaSymbol> fieldType;
            bool isAnonBlock = false;
            if (SUCCEEDED(field->get_type(&fieldType)) && fieldType)
            {
                DWORD fieldTypeTag = SymTagNull;
                fieldType->get_symTag((DWORD*)&fieldTypeTag);
                if (fieldTypeTag == SymTagUDT && TypeWalker::isAnonymousUDT(fieldType.get()))
                    isAnonBlock = true;
            }

            if (isAnonBlock)
            {
                // dumpAnonymousUDT already emits closing "};\n"
                ret += dumpAnonymousUDT(fieldType.get(), _level);
                return;
            }

            ret += tab(_level);
            try { ret += TypeWalker::resolveType(field.get(), m_scope, true, m_config.m_intStyle).build(); }
            catch (...) { ret += L"/* <error resolving field type> */"; }
            ret += L";";

            if (m_config.m_showOffset)
            {
                LONG offset = 0xFFFFFFFC;
                if (SUCCEEDED(field->get_offset(&offset)) && offset != 0xFFFFFFFC)
                {
                    wchar_t buf[32];
                    swprintf_s(buf, L"// 0x%X", offset);
                    size_t padNeeded = ret.length() < 60 ? 60 - ret.length() : 1;
                    ret.append(padNeeded, L' ');
                    ret += buf;
                }
            }
            ret += L"\n";
        };

        if (branches.empty())
        {
            
        }
        else
        {
            for (const auto& branch : branches)
            {
                bool isUnion = false;

                if (branch.groups.size() > 1)
                {
                    isUnion = true;
                }

                if (isUnion)
                {
                    ret += tab(a_nestingLevel);
                    ret += L"union\n";
                    ret += tab(a_nestingLevel);
                    ret += L"{\n";
                }

                for (const auto& group : branch.groups)
                {
                    bool isStruct = false;

                    if (group.fields.size() > 1)
                    {
                        isStruct = true;
                    }

                    if (isStruct)
                    {
                        ret += tab(a_nestingLevel + isUnion);
                        ret += L"struct\n";
                        ret += tab(a_nestingLevel + isUnion);
                        ret += L"{\n";
                    }

                    for (auto& field : group.fields)
                    {
                        emitField(field, a_nestingLevel + isUnion + isStruct);
                    }

                    if (isStruct)
                    {
                        ret += tab(a_nestingLevel + isUnion);
                        ret += L"};\n";
                    }
                }

                if (isUnion)
                {
                    ret += tab(a_nestingLevel);
                    ret += L"};\n";
                }
            }
        }

        // Non-virtual functions (filter compiler-generated like __local_vftable_ctor_closure)
        bool firstFunc = true;
        for (auto& func : childContainers[1])
        {
            BOOL isVirtual = TRUE;
            if (FAILED(func->get_virtual(&isVirtual)) || isVirtual) { continue; }

            // Skip compiler-generated functions (e.g. __local_vftable_ctor_closure)
            if (m_config.m_hideCompilerGenerated && isCompilerGenerated(func.get())) { continue; }

            if (firstFunc && m_config.m_showInfoComment)
            {
                hasContent = headerComment(ret, L" FUNCS:", a_nestingLevel, hasContent);
                firstFunc = false;
            }

            lastAccess = emitAccessLabel(ret, func.get(), lastAccess, a_nestingLevel);
            ret += dumpFunction(func.get(), a_nestingLevel);
            ret += L"\n";
        }

        // Static/const data members
        bool firstStatic = true;
        for (auto& field : childContainers[0])
        {
            DWORD kind = 0;
            if (FAILED(field->get_dataKind(&kind)) || kind == DataIsMember) { continue; }

            if (firstStatic && m_config.m_showInfoComment)
            {
                hasContent = headerComment(ret, L" OTHER MEMBERS:", a_nestingLevel, hasContent);
                firstStatic = false;
            }

            lastAccess = emitAccessLabel(ret, field.get(), lastAccess, a_nestingLevel);

            ret += tab(a_nestingLevel);

            switch (kind)
            {
            case DataIsStaticMember: ret += L"static "; break;
            case DataIsConstant: ret += L"constexpr "; break;
            default: break;
            }

            try
            {
                ret += TypeWalker::resolveType(
                    field.get(), 
                    m_scope, 
                    true,
                    m_config.m_intStyle
                ).build();
            }
            catch (...)
            {
                ret += L"/* <error> */";
            }

            // ret += constantValueSuffix(field.get());

            ret += L";\n";
        }

        return ret;
    }

    /// Register source file info for a symbol (stores for later output).
    void registerTypeSource(IDiaSymbol* a_symbol)
    {
        if (!m_config.m_showTypeSource) return;

        ComPtr<IDiaEnumLineNumbers> enum_symbolsLines;
        ComPtr<IDiaSourceFile> sourceFile;
        ComPtr<IDiaLineNumber> lineNumber;

        DWORD addressSection = 0;
        DWORD addressOffset = 0;

        if (SUCCEEDED(a_symbol->get_addressSection(&addressSection)) &&
            SUCCEEDED(a_symbol->get_addressOffset(&addressOffset)))
        {
            if (m_session && SUCCEEDED(m_session->findLinesByAddr(
                addressSection, addressOffset, 1, &enum_symbolsLines)) && enum_symbolsLines)
            {
                ULONG celt = 0;
                if (SUCCEEDED(enum_symbolsLines->Next(1, &lineNumber, &celt)) && celt == 1)
                {
                    if (SUCCEEDED(lineNumber->get_sourceFile(&sourceFile)) && sourceFile)
                    {
                        BSTR _filename;
                        if (SUCCEEDED(sourceFile->get_fileName(&_filename)))
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

        std::wstring ret;
        for (const auto& _src : m_typeSources)
        {
            ret += L"// ";
            ret += _src;
            ret += L"\n";
        }
        m_typeSources.clear();
        return ret;
    }

    void processType(IDiaSymbol* a_symbol, std::wstring& aoutput)
    {
        DWORD symTag = 0;
        if (SUCCEEDED(a_symbol->get_symTag(&symTag)))
        {
            switch (symTag)
            {
            case SymTagTypedef: aoutput += dumpTypedef(a_symbol); break;
            case SymTagUDT:    aoutput += dumpClass(a_symbol); break;
            case SymTagEnum:   aoutput += dumpEnum(a_symbol); break;
            case SymTagData:
            {
                std::wstring typeText;
                try { typeText = TypeWalker::resolveType(a_symbol, m_scope, true, m_config.m_intStyle).build(); }
                catch (...) { typeText = L"/* <error> */"; }
                aoutput += typeText;
                break;
            }
            case SymTagFunction:
                aoutput += dumpFunction(a_symbol);
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
        if (!m_config.m_showSize) return L"";

        ULONGLONG len;
        if (SUCCEEDED(a_symbol->get_length(&len)))
        {
            wchar_t buf[64];
            swprintf_s(buf, L"// size: %llu byte\n", len);
            return buf;
        }
        return L"";
    }

    std::wstring modPrefix(IDiaSymbol* a_symbol) const
    {
        std::wstring ret;
        BOOL isConst = FALSE;
        BOOL isVol = FALSE;
        if (SUCCEEDED(a_symbol->get_constType(&isConst)) && isConst) ret += L"const ";
        if (SUCCEEDED(a_symbol->get_volatileType(&isVol)) && isVol) ret += L"volatile ";
        return ret;
    }

    std::wstring udtKeyword(IDiaSymbol* a_symbol) const
    {
        auto name = TypeWalker::getUDTKindName(a_symbol);
        if (name)
        {
            std::wstring ret = name;
            ret += L" ";
            return ret;
        }

        // Check for anonymous union/struct
        if (TypeWalker::isAnonymousUDT(a_symbol))
        {
            DWORD udtKind = 0;
            a_symbol->get_udtKind(&udtKind);
            switch (udtKind)
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
        std::wstring ret;
        ComPtr<IDiaEnumSymbols> baseEnum;

        bool _isBegin = true;
        if (SUCCEEDED(a_symbol->findChildren(SymTagBaseClass, nullptr, nsNone, &baseEnum)))
        {
            ComPtr<IDiaSymbol> baseSymbol;
            ULONG celt = 0;
            while (SUCCEEDED(baseEnum->Next(1, &baseSymbol, &celt)) && celt == 1)
            {
                ret += _isBegin ? L" : " : L", ";
                _isBegin = false;

                auto access = TypeWalker::getAccessName(baseSymbol.get(), m_config.m_baseAccessType);
                if (access) { ret += access; ret += L" "; }

                BOOL isVirtualBase = FALSE;
                baseSymbol->get_virtualBaseClass(&isVirtualBase); // get_indirectVirtualBaseClass
                if (isVirtualBase) ret += L"virtual ";

                ret += TypeWalker::getName(baseSymbol.get(), m_scope);
            }
        }
        return ret;
    }

    std::wstring baseTypeInheritance(IDiaSymbol* a_symbol) const
    {
        // if (!m_config.m_showInfoComment) return L"";

        auto base = TypeWalker::getBaseTypeName(a_symbol, m_config.m_intStyle);
        if (base)
        {
            std::wstring ret = L" : ";
            ret += base;
            return ret;
        }
        return L"";
    }

    std::wstring scopeBegin(int a_nestingLevel)
    {
        std::wstring ret;
        if (m_config.m_curlyBraceNewline)
        {
            ret += L"\n";
            ret += tab(a_nestingLevel);
        }
        else
        {
            ret += L" ";
        }
        ret += L"{\n";
        return ret;
    }

    std::wstring scopeEnd(int a_nestingLevel)
    {
        std::wstring ret = tab(a_nestingLevel);
        ret += L"};\n";
        return ret;
    }

    int dumpFunctionArgsToString(IDiaSymbol* a_symbol, std::wstring& aout)
    {
        int count = 0;
        bool isFirst = true;

        ComPtr<IDiaEnumSymbols> enum_symbolsParams;
        if (SUCCEEDED(a_symbol->findChildren(SymTagData, nullptr, nsNone, &enum_symbolsParams)))
        {
            ComPtr<IDiaSymbol> param;
            ULONG fetched = 0;
            while (SUCCEEDED(enum_symbolsParams->Next(1, &param, &fetched)) && fetched == 1)
            {
                DWORD _kind = 0;
                if (SUCCEEDED(param->get_dataKind(&_kind)) && _kind == DataIsParam)
                {
                    ++count;
                    if (!isFirst) { aout += L", "; }

                    try
                    {
                        aout += TypeWalker::resolveType(param.get(), m_scope, true, m_config.m_intStyle).build();
                    }
                    catch (...)
                    {
                        aout += L"/* <error> */";
                    }

                    isFirst = false;
                }
            }
        }

        return count;
    }

    std::wstring constantValueSuffix(IDiaSymbol* a_symbol) const
    {
        VARIANT v;
        VariantInit(&v);
        if (SUCCEEDED(a_symbol->get_value(&v)))
        {
            std::wstring ret;
            if (m_config.m_showEnumHex)
            {
                wchar_t buf[32];
                swprintf_s(buf, L" = 0x%llX", v.llVal); 
                ret = buf;
            }
            else
            {
                switch (v.vt)
                {
                case VT_I4:  { wchar_t buf[32]; swprintf_s(buf, L" = %d", v.lVal);   ret = buf; break; }
                case VT_UI4: { wchar_t buf[32]; swprintf_s(buf, L" = %u", v.ulVal);  ret = buf; break; }
                case VT_I2:  { wchar_t buf[32]; swprintf_s(buf, L" = %d", v.iVal);   ret = buf; break; }
                case VT_UI2: { wchar_t buf[32]; swprintf_s(buf, L" = %u", v.uiVal);  ret = buf; break; }
                case VT_I1:  { wchar_t buf[32]; swprintf_s(buf, L" = %d", (int)v.cVal);   ret = buf; break; }
                case VT_UI1: { wchar_t buf[32]; swprintf_s(buf, L" = %u", v.bVal);   ret = buf; break; }
                case VT_R4:  { wchar_t buf[32]; swprintf_s(buf, L" = %ff", v.fltVal); ret = buf; break; }
                case VT_R8:  { wchar_t buf[32]; swprintf_s(buf, L" = %f", v.dblVal); ret = buf; break; }
                case VT_BSTR: if (v.bstrVal) { ret = L" = L\""; ret += v.bstrVal; ret += L"\""; } break;
                default: /*printf("VALUE: Undefined type : %d", v.vt);*/ break;
                }
            }
            VariantClear(&v);
            return ret;
        }
        return L"";
    }

    bool headerComment(std::wstring& aout, const wchar_t* a_label, int a_nesting, bool ahasContent)
    {
        if (ahasContent) { aout += L"\n"; }
        aout += tab(a_nesting);
        aout += L"///";
        aout += a_label;
        aout += L"\n";
        return true;
    }

    static const wchar_t* getVirtualName(IDiaSymbol* a_symbol)
    {
        BOOL isVirt;
        return SUCCEEDED(a_symbol->get_virtual(&isVirt)) && isVirt ? L"virtual" : nullptr;
    }

    static const wchar_t* getStaticName(IDiaSymbol* a_symbol)
    {
        BOOL isStatic;
        return SUCCEEDED(a_symbol->get_isStatic(&isStatic)) && isStatic ? L"static" : nullptr;
    }

    static ComPtr<IDiaSymbol> getTypeCom(IDiaSymbol* a_symbol)
    {
        ComPtr<IDiaSymbol> type;
        if (SUCCEEDED(a_symbol->get_type(&type))) return type;
        return ComPtr<IDiaSymbol>();
    }

    static DWORD countChildren(IDiaSymbol* a_symbol, enum SymTagEnum a_tag)
    {
        DWORD count = 0;
        ComPtr<IDiaEnumSymbols> enum_symbols;
        if (SUCCEEDED(a_symbol->findChildren(a_tag, nullptr, nsNone, &enum_symbols)) && enum_symbols)
        {
            ComPtr<IDiaSymbol> child;
            ULONG celt = 0;
            while (SUCCEEDED(enum_symbols->Next(1, &child, &celt)) && celt == 1)
            {
                ++count;
            }
        }
        return count;
    }

    DumpConfig m_config;
    ScopeContext m_scope;
    std::vector<std::wstring> m_typeSources;
    IDiaSession* m_session = nullptr;
};