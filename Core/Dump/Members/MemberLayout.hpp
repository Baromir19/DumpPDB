#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <dia2.h>

#include <Core/DIA/TypeWalker.hpp>
#include <Core/Dump/Constants/ConstantRenderer.hpp>
#include <Core/Dump/DumpContext.hpp>
#include <Core/Dump/Format/DumpFormatter.hpp>
#include <Core/Dump/Functions/FunctionRenderer.hpp>
#include <Core/Dump/IDumpCoordinator.hpp>
#include <Core/Dump/Types/ClassRenderer.hpp>
#include <Core/Dump/Types/EnumRenderer.hpp>
#include <Core/Dump/Types/TypedefRenderer.hpp>
#include <Core/Util/Com/ComPtr.hpp>

/// Traverses a UDT's children and lays out fields, nested types, functions
/// and static members, injecting union/struct grouping for overlapping offsets.
class MemberLayout
{
public:

    MemberLayout(DumpContext& a_ctx,
        DumpFormatter& a_fmt,
        ConstantRenderer& a_constants,
        ClassRenderer& a_classes,
        EnumRenderer& a_enums,
        TypedefRenderer& a_typedefs,
        FunctionRenderer& a_functions,
        IDumpCoordinator& a_host)
        : m_ctx(a_ctx)
        , m_fmt(a_fmt)
        , m_constants(a_constants)
        , m_classes(a_classes)
        , m_enums(a_enums)
        , m_typedefs(a_typedefs)
        , m_functions(a_functions)
        , m_host(a_host)
    {
    }

    std::wstring dumpMembers(IDiaSymbol* a_symbol, int a_nestingLevel)
    {
        std::wstring ret;

        ComPtr<IDiaEnumSymbols> children;
        if (FAILED(a_symbol->findChildren(SymTagNull, nullptr, nsNone, &children)))
            return ret;

        // Buckets: [0]=Data, [1]=Function, [2]=UDT, [3]=Enum, [4]=Typedef, [5]=unused, [6]=Friend
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
                case SymTagData:
                    childContainers[0].push_back(child);
                    break;
                case SymTagFunction:
                    childContainers[1].push_back(child);
                    break;
                case SymTagUDT:
                    childContainers[2].push_back(child);
                    break;
                case SymTagEnum:
                    childContainers[3].push_back(child);
                    break;
                case SymTagTypedef:
                    childContainers[4].push_back(child);
                    break;
                case SymTagFriend:
                    childContainers[6].push_back(child);
                    break;
                default:
                    // SymTagVTable, etc. — skip
                    break;
                }
            }
            catch (...)
            {
                ret += m_fmt.tab(a_nestingLevel) + L"/* <error processing child symbol> */\n";
            }
        }

        bool hasContent = false;
        DWORD lastAccess = (DWORD)-1; // sentinel — no previous access

        // ── Friends ────────────────────────────────────────────────────────────
        if (!childContainers[6].empty() && m_ctx.config().m_showInfoComment)
            hasContent = m_fmt.headerComment(ret, L" FRIENDS:", a_nestingLevel, hasContent);

        for (auto& _friend : childContainers[6])
        {
            lastAccess = m_fmt.emitAccessLabel(ret, _friend.get(), lastAccess, a_nestingLevel);
            ret += m_functions.dumpFriend(_friend.get(), a_nestingLevel);
        }

        // ── Enums ──────────────────────────────────────────────────────────────
        if (!childContainers[3].empty() && m_ctx.config().m_showInfoComment)
            hasContent = m_fmt.headerComment(ret, L" ENUMS:", a_nestingLevel, hasContent);

        for (auto& enumSym : childContainers[3])
        {
            lastAccess = m_fmt.emitAccessLabel(ret, enumSym.get(), lastAccess, a_nestingLevel);
            ret += m_enums.dumpEnum(enumSym.get(), a_nestingLevel);
        }

        // ── Typedefs ───────────────────────────────────────────────────────────
        if (!childContainers[4].empty() && m_ctx.config().m_showInfoComment)
            hasContent = m_fmt.headerComment(ret, L" TYPEDEFS:", a_nestingLevel, hasContent);

        for (auto& typedefSym : childContainers[4])
        {
            lastAccess
                = m_fmt.emitAccessLabel(ret, typedefSym.get(), lastAccess, a_nestingLevel);
            ret += m_typedefs.dumpTypedef(typedefSym.get(), a_nestingLevel);
        }

        // ── Nested classes ─────────────────────────────────────────────────────
        if (!childContainers[2].empty() && m_ctx.config().m_showInfoComment)
            hasContent = m_fmt.headerComment(ret, L" CLASSES:", a_nestingLevel, hasContent);

        for (auto& classSym : childContainers[2])
        {
            lastAccess = m_fmt.emitAccessLabel(ret, classSym.get(), lastAccess, a_nestingLevel);
            ret += m_classes.dumpClass(classSym.get(), a_nestingLevel);
        }

        // ── Virtual functions (sorted by vtable offset) ────────────────────────
        std::vector<ComPtr<IDiaSymbol>> vfuncs;
        for (auto& func : childContainers[1])
        {
            BOOL isVirtual = FALSE;
            if (SUCCEEDED(func->get_virtual(&isVirtual)) && isVirtual)
            {
                if (m_ctx.config().m_hideCompilerGenerated
                    && FunctionRenderer::isCompilerGenerated(func.get()))
                {
                    continue;
                }
                vfuncs.push_back(func);
            }
        }

        std::sort(vfuncs.begin(),
            vfuncs.end(),
            [](const ComPtr<IDiaSymbol>& a, const ComPtr<IDiaSymbol>& b)
            {
                DWORD offsetA = 0, offsetB = 0;
                a->get_virtualBaseOffset(&offsetA);
                b->get_virtualBaseOffset(&offsetB);
                return offsetA < offsetB;
            });

        if (!vfuncs.empty() && m_ctx.config().m_showInfoComment)
            hasContent = m_fmt.headerComment(ret, L" VIRTUALS:", a_nestingLevel, hasContent);

        for (auto& vfunc : vfuncs)
        {
            lastAccess = m_fmt.emitAccessLabel(ret, vfunc.get(), lastAccess, a_nestingLevel);
            ret += m_functions.dumpFunction(vfunc.get(), a_nestingLevel);
            if (m_ctx.config().m_showOffset)
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

        // ── Fields (SymTagData, DataIsMember) ──────────────────────────────────
        std::vector<ComPtr<IDiaSymbol>> fields;
        for (auto& field : childContainers[0])
        {
            DWORD kind = 0;
            if (SUCCEEDED(field->get_dataKind(&kind)) && kind == DataIsMember)
                fields.push_back(field);
        }

        if (!fields.empty() && m_ctx.config().m_showInfoComment)
            hasContent = m_fmt.headerComment(ret, L" FIELDS:", a_nestingLevel, hasContent);

        // Helper: byte-end offset of a field (using its type size, not bit width)
        auto getFieldByteEnd = [](ComPtr<IDiaSymbol>& f) -> LONG
        {
            LONG off = 0;
            f->get_offset(&off);

            ComPtr<IDiaSymbol> type;
            if (SUCCEEDED(f->get_type(&type)) && type)
            {
                ULONGLONG typeSize = 0;
                if (SUCCEEDED(type->get_length(&typeSize)))
                    return off + static_cast<LONG>(typeSize);
            }

            ULONGLONG length = 0;
            f->get_length(&length);
            return off + static_cast<LONG>(length);
        };

        // Returns the first index j > startIdx where fields[j] has the same byte
        // offset as fields[startIdx] but bitPosition==0 (i.e. union overlap).
        auto findOverlapEnd = [&](size_t startIdx) -> size_t
        {
            if (startIdx + 1 >= fields.size())
                return fields.size();

            LONG off = 0;
            fields[startIdx]->get_offset(&off);

            for (size_t j = startIdx + 1; j < fields.size(); ++j)
            {
                LONG futureOff = 0;
                fields[j]->get_offset(&futureOff);

                DWORD bitPos = 0;
                fields[j]->get_bitPosition(&bitPos);

                if (off == futureOff && bitPos == 0)
                    return j;
            }
            return fields.size();
        };

        struct FieldGroup
        {
            std::vector<ComPtr<IDiaSymbol>> fields;
            LONG beginOffset = 0L;
            LONG endOffset = 0L;
        };

        struct FieldBranch
        {
            std::vector<FieldGroup> groups;
        };

        auto makeGroup = [&](size_t i, size_t j) -> FieldGroup
        {
            FieldGroup group;
            group.beginOffset = 0;
            if (i < fields.size())
                fields[i]->get_offset(&group.beginOffset);
            group.endOffset = getFieldByteEnd(fields[j - 1]);
            for (size_t k = i; k < j; ++k)
                group.fields.push_back(fields[k]);
            return group;
        };

        std::vector<FieldBranch> branches;
        {
            FieldBranch curBranch;
            bool startsNewBranch = true;

            for (size_t i = 0; i < fields.size(); ++i)
            {
                auto& field = fields[i];
                LONG off = 0;
                field->get_offset(&off);

                if (startsNewBranch && !curBranch.groups.empty())
                {
                    branches.push_back(curBranch);
                    curBranch = {};
                }

                if (!startsNewBranch)
                {
                    auto nextIdx = i + 1;
                    startsNewBranch = true;

                    size_t overlapEnd = findOverlapEnd(i);
                    if (overlapEnd != fields.size())
                    {
                        auto group = makeGroup(i, overlapEnd);
                        curBranch.groups.push_back(group);
                        startsNewBranch = false;
                        i = overlapEnd - 1;
                        continue;
                    }

                    LONG maxEndOffset = LONG_MIN;
                    for (const auto& g : curBranch.groups)
                        maxEndOffset = maxEndOffset > g.endOffset ? maxEndOffset : g.endOffset;

                    FieldGroup group;
                    group.beginOffset = off;
                    group.endOffset = maxEndOffset;

                    bool foundBoundary = false;
                    for (size_t j = nextIdx; j < fields.size(); ++j)
                    {
                        LONG futureOff = 0;
                        fields[j]->get_offset(&futureOff);
                        if (futureOff >= maxEndOffset)
                        {
                            for (size_t k = i; k < j; ++k)
                                group.fields.push_back(fields[k]);
                            curBranch.groups.push_back(group);
                            i = j - 1;
                            foundBoundary = true;
                            break;
                        }
                    }

                    if (!foundBoundary)
                    {
                        for (size_t k = i; k < fields.size(); ++k)
                            group.fields.push_back(fields[k]);
                        i = fields.size() - 1;
                        if (!group.fields.empty())
                            curBranch.groups.push_back(group);
                    }

                    continue;
                }

                startsNewBranch = true;

                size_t overlapEnd = findOverlapEnd(i);
                if (overlapEnd != fields.size())
                {
                    auto group = makeGroup(i, overlapEnd);
                    curBranch.groups.push_back(group);
                    startsNewBranch = false;
                    i = overlapEnd - 1;
                    continue;
                }

                FieldGroup group;
                group.fields.push_back(field);
                group.beginOffset = off;
                group.endOffset = getFieldByteEnd(field);
                curBranch.groups.push_back(group);
            }

            if (!curBranch.groups.empty())
                branches.push_back(std::move(curBranch));
        }

        // Lambda: emit one field (handles anonymous UDT inline blocks)
        auto emitField = [&](const ComPtr<IDiaSymbol>& field, int level)
        {
            lastAccess = m_fmt.emitAccessLabel(ret, field.get(), lastAccess, level);

            // Check if field's type is an anonymous union/struct ($HASH names)
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
                ret += m_classes.dumpAnonymousUDT(fieldType.get(), level);
                return;
            }

            ret += m_fmt.tab(level);
            try
            {
                ret += TypeWalker::resolveType(
                    field.get(), m_ctx.scope(), true, m_ctx.config().m_intStyle)
                           .build();
            }
            catch (...)
            {
                ret += L"/* <error resolving field type> */";
            }
            ret += L";";

            if (m_ctx.config().m_showOffset)
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

        for (const auto& branch : branches)
        {
            bool isUnion = branch.groups.size() > 1;

            if (isUnion)
            {
                ret += m_fmt.tab(a_nestingLevel);
                ret += L"union\n";
                ret += m_fmt.tab(a_nestingLevel);
                ret += L"{\n";
            }

            for (const auto& group : branch.groups)
            {
                bool isStruct = group.fields.size() > 1;

                if (isStruct)
                {
                    ret += m_fmt.tab(a_nestingLevel + (int)isUnion);
                    ret += L"struct\n";
                    ret += m_fmt.tab(a_nestingLevel + (int)isUnion);
                    ret += L"{\n";
                }

                for (auto& field : group.fields)
                    emitField(field, a_nestingLevel + (int)isUnion + (int)isStruct);

                if (isStruct)
                {
                    ret += m_fmt.tab(a_nestingLevel + (int)isUnion);
                    ret += L"};\n";
                }
            }

            if (isUnion)
            {
                ret += m_fmt.tab(a_nestingLevel);
                ret += L"};\n";
            }
        }

        // ── Non-virtual functions ──────────────────────────────────────────────
        bool firstFunc = true;
        for (auto& func : childContainers[1])
        {
            BOOL isVirtual = TRUE;
            if (FAILED(func->get_virtual(&isVirtual)) || isVirtual)
                continue;

            if (m_ctx.config().m_hideCompilerGenerated
                && FunctionRenderer::isCompilerGenerated(func.get()))
            {
                continue;
            }

            if (firstFunc && m_ctx.config().m_showInfoComment)
            {
                hasContent = m_fmt.headerComment(ret, L" FUNCS:", a_nestingLevel, hasContent);
                firstFunc = false;
            }

            lastAccess = m_fmt.emitAccessLabel(ret, func.get(), lastAccess, a_nestingLevel);
            ret += m_functions.dumpFunction(func.get(), a_nestingLevel);
            ret += L"\n";
        }

        // ── Static/const data members ─────────────────────────────────────────
        bool firstStatic = true;
        for (auto& field : childContainers[0])
        {
            DWORD kind = 0;
            if (FAILED(field->get_dataKind(&kind)) || kind == DataIsMember)
                continue;

            if (firstStatic && m_ctx.config().m_showInfoComment)
            {
                hasContent
                    = m_fmt.headerComment(ret, L" OTHER MEMBERS:", a_nestingLevel, hasContent);
                firstStatic = false;
            }

            lastAccess = m_fmt.emitAccessLabel(ret, field.get(), lastAccess, a_nestingLevel);
            ret += m_fmt.tab(a_nestingLevel);

            switch (kind)
            {
            case DataIsStaticMember:
                ret += L"static ";
                break;
            case DataIsConstant:
                ret += L"constexpr ";
                break;
            default:
                break;
            }

            try
            {
                ret += TypeWalker::resolveType(
                    field.get(), m_ctx.scope(), true, m_ctx.config().m_intStyle)
                           .build();
            }
            catch (...)
            {
                ret += L"/* <error> */";
            }

            if (m_constants.canHaveValue(field.get()))
                ret += m_constants.constantValueSuffix(field.get());

            ret += L";\n";
        }

        return ret;
    }

private:

    DumpContext& m_ctx;
    DumpFormatter& m_fmt;
    ConstantRenderer& m_constants;
    ClassRenderer& m_classes;
    EnumRenderer& m_enums;
    TypedefRenderer& m_typedefs;
    FunctionRenderer& m_functions;
    IDumpCoordinator& m_host;
};
