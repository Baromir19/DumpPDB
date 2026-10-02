#pragma once

#include <Core/RTTI/FieldRecovery.hpp>
#include <Core/RTTI/RttiReader.hpp>
#include <Core/RTTI/RttiTypes.hpp>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace DumpPDB
{

/// ============================================================================
/// TypeReconstructor
///
/// Combines RttiReader (vftable/hierarchy) + FieldRecovery (constructor trace)
/// into a ReconstructedType, then formats it as a C++ declaration.
///
/// Output example:
///
///   // [EXE x86] size: ~0x1F14 bytes | multiple inheritance
///   class ActorInstance : public ExposedObject
///   {
///       /* vftable @ RVA 0x1469D9C (4 slots) */
///       /* vftable @ RVA 0x1469D54, offset +0x30 (17 slots) [base: IListener] */
///
///       /// VIRTUAL FUNCTIONS:
///       virtual ~ActorInstance();           // [0] dtor
///       virtual void func_1() override;     // [1] 0x...
///       virtual void func_2();              // [2] 0x...  (new)
///
///       /// FIELDS:
///       void*    vfptr;              /* 0x00 */
///       void*    fld_0x04;           /* 0x04 */
///       float    fld_0x08;           /* 0x08 */
///       uint8_t  pad_0x0C[4];        /* 0x0C */  (gap)
///       int32_t  fld_0x10;           /* 0x10 */
///   };
/// ============================================================================
class TypeReconstructor
{
public:

    TypeReconstructor() = default;

    /// Reconstruct a single type by name using the reader's bitness.
    [[nodiscard]] ReconstructedType reconstruct(const std::string& a_name,
                                                 const RttiReader& a_reader)
    {
        auto found = a_reader.findExact(a_name);
        if (found.empty())
            found = a_reader.findByName(a_name);
        if (found.empty())
            return {};

        return buildFromVftables(found, a_reader, a_reader.pe(), a_reader.bitness());
    }

    /// Format a ReconstructedType as a wide C++ declaration string.
    [[nodiscard]] static std::wstring format(const ReconstructedType& a_type)
    {
        std::wstring out;

        if (!a_type.hasRtti)
        {
            out += L"// [EXE] Type not found or no RTTI.\n";
            return out;
        }

        const wchar_t* bits  = (a_type.bitness == PEBitness::Bits32) ? L"x86" : L"x64";
        const uint32_t ptrSz = (a_type.bitness == PEBitness::Bits32) ? 4u : 8u;

        // ── Header ────────────────────────────────────────────────────────────
        out += L"// [EXE ";
        out += bits;
        out += L"] ";

        if (a_type.objectSize > 0)
        {
            wchar_t buf[64];
            swprintf_s(buf, L"size: 0x%X (%u) bytes | ", a_type.objectSize, a_type.objectSize);
            out += buf;
        }
        else if (a_type.maxObservedOffset > 0)
        {
            const uint32_t est = align(a_type.maxObservedOffset, ptrSz);
            wchar_t buf[64];
            swprintf_s(buf, L"size: ~0x%X bytes (estimated) | ", est);
            out += buf;
        }
        else
        {
            out += L"size: unknown | ";
        }

        switch (a_type.inheritance)
        {
        case InheritanceKind::None:     out += L"no bases\n";             break;
        case InheritanceKind::Single:   out += L"single inheritance\n";   break;
        case InheritanceKind::Multiple: out += L"multiple inheritance\n"; break;
        case InheritanceKind::Virtual:  out += L"virtual inheritance\n";  break;
        }

        // ── Class declaration ─────────────────────────────────────────────────
        out += a_type.isStruct ? L"struct " : L"class ";
        out += toWide(a_type.fullName);

        bool firstBase = true;
        for (const auto& b : a_type.directBases)
        {
            if (b.demangledName == a_type.fullName) continue;
            out += firstBase ? L" : public " : L", public ";
            out += toWide(b.demangledName);
            firstBase = false;
        }
        out += L"\n{\npublic:\n";

        // ── Vftable comments ─────────────────────────────────────────────────
        for (const auto& vft : a_type.vftables)
        {
            wchar_t buf[256];
            if (vft.objectOffset == 0)
            {
                swprintf_s(buf, L"    /* vftable @ RVA 0x%llX (%zu slots) */\n",
                    static_cast<unsigned long long>(vft.vftableRva),
                    vft.vfuncs.size());
            }
            else
            {
                std::wstring baseName;
                for (const auto& b : a_type.directBases)
                    if (b.offset == static_cast<int32_t>(vft.objectOffset))
                    { baseName = toWide(b.demangledName); break; }

                if (baseName.empty())
                    swprintf_s(buf,
                        L"    /* vftable @ RVA 0x%llX, offset +0x%X (%zu slots) */\n",
                        static_cast<unsigned long long>(vft.vftableRva),
                        vft.objectOffset, vft.vfuncs.size());
                else
                    swprintf_s(buf,
                        L"    /* vftable @ RVA 0x%llX, offset +0x%X (%zu slots) [%s] */\n",
                        static_cast<unsigned long long>(vft.vftableRva),
                        vft.objectOffset, vft.vfuncs.size(), baseName.c_str());
            }
            out += buf;
        }

        // ── Virtual functions ─────────────────────────────────────────────────
        // Rules (evaluated per subobject vftable):
        //   isDestructor          → primary vftable only (a subobject's slot 0 is
        //                           an adjustor thunk of the same destructor)
        //   isNew                 → shown, no suffix
        //   isOverride            → shown, " override" suffix
        //   inherited (neither)   → SKIPPED (same address as base, not overridden)
        {
            bool anySlots = false;
            for (const auto& vft : a_type.vftables)
                if (!vft.vfuncs.empty()) { anySlots = true; break; }

            if (anySlots)
            {
                out += L"\n    /// VIRTUAL FUNCTIONS:\n";

                const bool multi = (a_type.vftables.size() > 1);

                for (const auto& vft : a_type.vftables)
                {
                    if (vft.vfuncs.empty()) continue;

                    const bool primary = (vft.objectOffset == 0);

                    if (multi)
                    {
                        wchar_t hbuf[256];
                        std::wstring baseName;
                        for (const auto& b : a_type.directBases)
                            if (b.offset == static_cast<int32_t>(vft.objectOffset))
                            { baseName = toWide(b.demangledName); break; }

                        if (primary)
                            swprintf_s(hbuf, L"    // --- primary vftable (+0x00) ---\n");
                        else if (!baseName.empty())
                            swprintf_s(hbuf, L"    // --- vftable (+0x%X, %s) ---\n",
                                vft.objectOffset, baseName.c_str());
                        else
                            swprintf_s(hbuf, L"    // --- vftable (+0x%X) ---\n",
                                vft.objectOffset);
                        out += hbuf;
                    }

                    for (const auto& slot : vft.vfuncs)
                    {
                        // Skip pure-inherited slots (not overridden, not new)
                        if (slot.isDestructor && !primary) continue;
                        if (!slot.isDestructor && !slot.isNew && !slot.isOverride
                            && !slot.isPureVirtual)
                            continue;

                        wchar_t buf[256];

                        if (slot.isPureVirtual)
                        {
                            swprintf_s(buf, L"    virtual void func_%u() = 0;  // [%u]\n",
                                slot.slotIndex, slot.slotIndex);
                            out += buf;
                            continue;
                        }

                        out += L"    virtual ";

                        if (slot.isDestructor)
                        {
                            swprintf_s(buf, L"~%s();  // [%u] 0x%llX\n",
                                toWide(a_type.className).c_str(),
                                slot.slotIndex,
                                static_cast<unsigned long long>(slot.funcRva));
                        }
                        else if (slot.isOverride)
                        {
                            swprintf_s(buf, L"void func_%u() override;  // [%u] 0x%llX\n",
                                slot.slotIndex, slot.slotIndex,
                                static_cast<unsigned long long>(slot.funcRva));
                        }
                        else // isNew
                        {
                            swprintf_s(buf, L"void func_%u();  // [%u] 0x%llX\n",
                                slot.slotIndex, slot.slotIndex,
                                static_cast<unsigned long long>(slot.funcRva));
                        }
                        out += buf;
                    }
                }
            }
        }

        // ── Fields ────────────────────────────────────────────────────────────
        if (!a_type.fields.empty())
        {
            out += L"\n    /// FIELDS:\n";

            // Show base region header if we trimmed inherited fields
            if (a_type.ownFieldsStart > 0)
            {
                wchar_t buf[128];
                swprintf_s(buf, L"    // ... base class data [0x00 - 0x%02X] ...\n",
                    a_type.ownFieldsStart - 1);
                out += buf;
            }

            for (const auto& f : a_type.fields)
            {
                wchar_t buf[320];

                if (f.isMIBaseVfptr)
                {
                    const std::wstring hint = toWide(f.embeddedClassName);
                    if (hint.empty())
                        swprintf_s(buf, L"    void*    vfptr_0x%02X;\n", f.offset);
                    else
                        swprintf_s(buf, L"    void*    vfptr_0x%02X;  // %s\n",
                            f.offset, hint.c_str());
                    out += buf;
                    continue;
                }

                if (f.kind == FieldKind::Padding)
                {
                    // Padding — choose Array or Expanded based on paddingStyle
                    const wchar_t* padType = L"int8_t ";
                    uint32_t elemSize = 1;
                    if      (f.size % 4 == 0) { padType = L"int32_t"; elemSize = 4; }
                    else if (f.size % 2 == 0) { padType = L"int16_t"; elemSize = 2; }

                    const uint32_t count = f.size / elemSize;

                    if (a_type.paddingStyle == PaddingStyle::Array)
                    {
                        if (count == 1)
                            swprintf_s(buf, L"    %s  pad_0x%02X;\n",   padType, f.offset);
                        else
                            swprintf_s(buf, L"    %s  pad_0x%02X[%u];\n", padType, f.offset, count);
                        out += buf;
                    }
                    else // Expanded: one declaration per element
                    {
                        for (uint32_t k = 0; k < count; ++k)
                        {
                            const uint32_t elemOff = f.offset + k * elemSize;
                            swprintf_s(buf, L"    %s  pad_0x%02X;\n", padType, elemOff);
                            out += buf;
                        }
                    }
                    continue;
                }

                const std::wstring typeName = toWide(f.typeName());

                // Build comment suffix
                std::wstring comment;
                if (!f.foreignVftableName.empty())
                {
                    // Embedded object with its own vftable (e.g. FadeIconMiniMapModifier)
                    comment = L"  // embedded: " + toWide(f.foreignVftableName);
                }
                else if (!f.ctorCallHint.empty() && f.ctorCallHint != "?")
                {
                    comment = L"  // ctor: " + toWide(f.ctorCallHint);
                }
                else if (!f.ctorCallHint.empty()) // "?"
                {
                    comment = L"  // ctor called here";
                }
                else if (!f.embeddedClassName.empty())
                {
                    comment = L"  // probably: " + toWide(f.embeddedClassName);
                }

                if (f.isBitfield)
                {
                    swprintf_s(buf, L"    %-8s fld_0x%02X : %u;%s\n",
                        typeName.c_str(), f.offset, f.bitSize, comment.c_str());
                }
                else
                {
                    swprintf_s(buf, L"    %-8s fld_0x%02X;%s\n",
                        typeName.c_str(), f.offset, comment.c_str());
                }
                out += buf;
            }
        }
        else
        {
            out += L"\n    // No fields recovered\n";
        }

        out += L"};\n";
        return out;
    }

private:

    // ─────────────────────────────────────────────────────────────────────────
    // Build
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] ReconstructedType buildFromVftables(
        const std::vector<const VftableInfo*>& a_vfts,
        const RttiReader& a_reader,
        const PEImage& a_pe,
        PEBitness a_bitness)
    {
        ReconstructedType rt;
        if (a_vfts.empty()) return rt;

        // Primary vftable = objectOffset == 0
        const VftableInfo* primary = nullptr;
        for (const auto* v : a_vfts)
            if (v->objectOffset == 0) { primary = v; break; }
        if (!primary) primary = a_vfts[0];

        rt.hasRtti     = true;
        rt.fullName    = primary->fullName;
        rt.className   = primary->className;
        rt.mangledName = primary->mangledName;
        rt.inheritance = primary->inheritance;
        rt.bitness     = a_bitness;
        rt.isStruct    = detectIsStruct(primary->mangledName);

        // Direct bases
        for (size_t i = 1; i < primary->bases.size(); ++i)
        {
            const auto& b = primary->bases[i];
            if (b.isDirect) rt.directBases.push_back(b);
        }
        if (rt.directBases.empty() && primary->bases.size() >= 2)
            rt.directBases.push_back(primary->bases[1]);

        // All vftables sorted by objectOffset
        rt.vftables.reserve(a_vfts.size());
        for (const auto* v : a_vfts) rt.vftables.push_back(*v);
        std::sort(rt.vftables.begin(), rt.vftables.end(),
            [](const VftableInfo& a, const VftableInfo& b)
            { return a.objectOffset < b.objectOffset; });

        // ── Override / new detection for every subobject vftable ──────────────
        for (auto& vft : rt.vftables)
        {
            annotateOverrides(vft, a_reader);
        }

        // ── Collect all MI base vftable RVAs for FieldRecovery ────────────────
        std::vector<uint64_t> baseVftableRvas;
        std::vector<uint32_t> baseVftableOffsets;
        for (const auto& vft : rt.vftables)
        {
            if (vft.objectOffset > 0)
            {
                baseVftableRvas.push_back(vft.vftableRva);
                baseVftableOffsets.push_back(vft.objectOffset);
            }
        }

        // ── Collect ALL known vftables from the reader for foreign-vftable detection ──
        std::vector<uint64_t> allVftRvas;
        std::vector<std::string> allVftNames;
        allVftRvas.reserve(a_reader.vftables().size());
        allVftNames.reserve(a_reader.vftables().size());
        for (const auto& vft : a_reader.vftables())
        {
            allVftRvas.push_back(vft.vftableRva);
            allVftNames.push_back(vft.fullName);
        }

        // ── Field recovery ────────────────────────────────────────────────────
        {
            FieldRecovery fr;
            uint32_t objectSize      = 0;
            uint32_t maxObservedOff  = 0;
            rt.fields = fr.recover(primary->vftableRva, a_pe, a_bitness,
                                   baseVftableRvas, baseVftableOffsets,
                                   allVftRvas, allVftNames,
                                   objectSize, maxObservedOff);
            rt.objectSize        = objectSize;
            rt.maxObservedOffset = maxObservedOff;
        }

        // ── Annotate embedded class hints in fields ───────────────────────────
        annotateEmbeddedHints(rt.fields, rt.vftables, a_reader, a_bitness);

        // ── Compute ownFieldsStart: skip fields belonging to base classes ──────
        // Strategy: find the end of the last direct base class's data region.
        // For each direct base at offset B, find its estimated size via its
        // own maxObservedOffset (reconstruct the base lightly). Then
        // ownFieldsStart = max(B + baseSize) over all direct bases.
        rt.ownFieldsStart = computeOwnFieldsStart(rt.directBases, rt.vftables,
                                                   a_reader, a_pe, a_bitness);

        // Strip fields that fall entirely within base class region
        if (rt.ownFieldsStart > 0)
        {
            std::vector<FieldInfo> ownFields;
            for (const auto& f : rt.fields)
            {
                if (f.offset + f.size > rt.ownFieldsStart)
                    ownFields.push_back(f);
            }
            rt.fields = std::move(ownFields);
        }

        // ── Gap filling ───────────────────────────────────────────────────────
        const uint32_t ptrSz = (a_bitness == PEBitness::Bits32) ? 4u : 8u;
        const uint32_t totalSize = rt.objectSize > 0
            ? rt.objectSize
            : (rt.maxObservedOffset > 0 ? align(rt.maxObservedOffset, ptrSz) : 0);

        if (totalSize > rt.ownFieldsStart)
            fillGaps(rt.fields, rt.ownFieldsStart, totalSize, ptrSz);

        return rt;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Override / new annotation
    //
    // Each vftable corresponds to one subobject (objectOffset). Its comparison
    // baseline is the primary vftable (objectOffset == 0) of the direct base
    // that occupies the SAME subobject offset:
    //   - the primary vftable (offset 0)      -> compared with the primary base
    //                                            (offset 0), e.g. ExposedObject
    //   - the subobject vftable at +0x30      -> compared with that interface's
    //                                            own vftable (AI::INavMeshRegisterable)
    //
    //   slot RVA unchanged vs base      -> inherited  (not printed)
    //   slot exists in base, RVA differs-> overridden (override)
    //   slot index >= base vftable size -> new
    // ─────────────────────────────────────────────────────────────────────────

    static void annotateOverrides(VftableInfo& a_vft, const RttiReader& a_reader)
    {
        // Find the direct base that owns this subobject offset.
        const BaseClassInfo* match = nullptr;
        for (const auto& b : a_vft.bases)
        {
            if (b.demangledName == a_vft.fullName) continue;
            if (!b.isDirect) continue;
            if (b.offset != static_cast<int32_t>(a_vft.objectOffset)) continue;
            match = &b;
            break;
        }

        // Fallback for the primary subobject: take the first direct base.
        if (!match && a_vft.objectOffset == 0)
        {
            for (const auto& b : a_vft.bases)
                if (b.isDirect && b.demangledName != a_vft.fullName)
                {
                    match = &b;
                    break;
                }
        }

        std::vector<uint64_t> baseSlotRvas;
        if (match && !match->demangledName.empty())
        {
            auto baseFound = a_reader.findExact(match->demangledName);
            for (const auto* bv : baseFound)
            {
                if (bv->objectOffset != 0) continue; // the base's own primary vftable
                for (const auto& s : bv->vfuncs)
                    baseSlotRvas.push_back(s.funcRva);
                break;
            }
        }

        const size_t baseSize = baseSlotRvas.size();

        for (auto& slot : a_vft.vfuncs)
        {
            slot.isNew      = false;
            slot.isOverride = false;

            if (slot.isDestructor || slot.isPureVirtual)
                continue;

            if (baseSlotRvas.empty() || slot.slotIndex >= baseSize)
            {
                slot.isNew = true;
                continue;
            }

            if (slot.funcRva != 0 && slot.funcRva == baseSlotRvas[slot.slotIndex])
                continue; // inherited, unchanged

            slot.isOverride = true; // re-implemented by the derived class
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Annotate fields with embedded class hints.
    //
    // For each pointer-sized field, check if the field value in the object
    // layout *could* be a vftable pointer by searching the known vftable list
    // for a vftable at that RVA. Also mark MI base vfptr fields explicitly.
    // ─────────────────────────────────────────────────────────────────────────

    static void annotateEmbeddedHints(
        std::vector<FieldInfo>&        a_fields,
        const std::vector<VftableInfo>& a_vftables,
        const RttiReader&               a_reader,
        PEBitness                       a_bitness)
    {
        // Build a map: objectOffset -> vftable name for MI bases
        std::unordered_map<uint32_t, std::string> miBaseNames;
        for (const auto& vft : a_vftables)
        {
            if (vft.objectOffset > 0)
                miBaseNames[vft.objectOffset] = vft.fullName;
        }

        for (auto& f : a_fields)
        {
            // Mark known MI base vfptr offsets
            auto it = miBaseNames.find(f.offset);
            if (it != miBaseNames.end() && f.kind == FieldKind::Pointer)
            {
                f.isMIBaseVfptr      = true;
                f.embeddedClassName  = it->second;
                continue;
            }

            // For pointer fields at other offsets: look for vftable matches
            // (a field might be an embedded object whose ctor writes its vfptr there)
            // We look for any vftable in the reader whose objectOffset == 0
            // and whose class was also seen as a base. This is a best-effort hint.
            if (f.kind != FieldKind::Pointer) continue;
            if (f.isMIBaseVfptr) continue;

            // Check if this offset matches any base's mdisp
            for (const auto& vft : a_vftables)
            {
                for (const auto& base : vft.bases)
                {
                    if (static_cast<uint32_t>(base.offset) == f.offset
                        && !base.demangledName.empty()
                        && base.demangledName != vft.fullName)
                    {
                        // Find the base's primary vftable RVA
                        auto baseVfts = a_reader.findExact(base.demangledName);
                        for (const auto* bv : baseVfts)
                        {
                            if (bv->objectOffset == 0)
                            {
                                f.embeddedClassName  = base.demangledName;
                                f.embeddedVftableRva = bv->vftableRva;
                                break;
                            }
                        }
                        if (!f.embeddedClassName.empty()) break;
                    }
                }
                if (!f.embeddedClassName.empty()) break;
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Compute ownFieldsStart
    //
    // Strategy (most reliable for MSVC MI):
    //   1. If there are MI vftables (objectOffset > 0): the first one's offset
    //      is the boundary — everything before it belongs to the primary base.
    //   2. Otherwise: run FieldRecovery on the direct base to find its extent.
    //   3. Fallback: ptrSize (just skip the primary vfptr).
    // ─────────────────────────────────────────────────────────────────────────

    static uint32_t computeOwnFieldsStart(
        const std::vector<BaseClassInfo>& a_bases,
        const std::vector<VftableInfo>&   a_vftables,
        const RttiReader& a_reader,
        const PEImage& a_pe,
        PEBitness a_bitness)
    {
        if (a_bases.empty()) return 0;

        const uint32_t ptrSz = (a_bitness == PEBitness::Bits32) ? 4u : 8u;

        // ── Multiple inheritance ──────────────────────────────────────────────
        // Every vftable with objectOffset > 0 marks a base subobject; a pure
        // interface occupies just its vfptr. The base region therefore ends at
        //     max(subobjectOffset) + ptrSize
        // e.g. ActorInstance: ICharacterProxyHitOverrider @ 0xB8 -> 0xBC.
        uint32_t maxEnd = 0;
        for (const auto& vft : a_vftables)
            if (vft.objectOffset > 0)
                maxEnd = std::max(maxEnd, vft.objectOffset + ptrSz);

        if (maxEnd > 0)
            return maxEnd;

        // ── Single inheritance ────────────────────────────────────────────────
        // Recover the primary base's extent from its own vftable/constructor.
        for (const auto& base : a_bases)
        {
            if (base.demangledName.empty() || base.offset < 0) continue;
            const uint32_t baseOffset = static_cast<uint32_t>(base.offset);

            auto baseVfts = a_reader.findExact(base.demangledName);
            const VftableInfo* bPrimary = nullptr;
            for (const auto* bv : baseVfts)
                if (bv->objectOffset == 0) { bPrimary = bv; break; }

            if (!bPrimary)
            {
                maxEnd = std::max(maxEnd, baseOffset + ptrSz);
                continue;
            }

            FieldRecovery fr;
            uint32_t baseObjSize = 0;
            uint32_t baseMaxOff  = 0;
            std::vector<uint64_t> emptyRvas;
            std::vector<uint32_t> emptyOffs;
            std::vector<std::string> emptyNames;
            (void)fr.recover(bPrimary->vftableRva, a_pe, a_bitness,
                             emptyRvas, emptyOffs, emptyRvas, emptyNames,
                             baseObjSize, baseMaxOff);

            uint32_t baseEnd = 0;
            if (baseObjSize > 0)
                baseEnd = baseOffset + baseObjSize;
            else if (baseMaxOff > 0)
                baseEnd = baseOffset + align(baseMaxOff, ptrSz);
            else
                baseEnd = baseOffset + ptrSz;

            maxEnd = std::max(maxEnd, baseEnd);
        }

        return maxEnd > 0 ? maxEnd : ptrSz;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Gap filling — fill [startOffset, totalSize) with padding entries
    // ─────────────────────────────────────────────────────────────────────────

    static void fillGaps(std::vector<FieldInfo>& a_fields,
                         uint32_t a_startOffset,
                         uint32_t a_totalSize,
                         uint32_t /*ptrSz*/)
    {
        if (a_totalSize <= a_startOffset) return;

        std::vector<FieldInfo> result;
        result.reserve(a_fields.size() * 2);

        uint32_t cursor = a_startOffset;

        for (const auto& f : a_fields)
        {
            if (f.offset < a_startOffset) continue; // skip base region

            if (f.offset > cursor)
            {
                FieldInfo pad;
                pad.offset    = cursor;
                pad.size      = f.offset - cursor;
                pad.kind      = FieldKind::Padding;
                pad.isBitfield = false;
                result.push_back(pad);
            }
            else if (f.offset < cursor)
            {
                continue; // overlapping
            }

            result.push_back(f);
            cursor = f.offset + f.size;
        }

        if (cursor < a_totalSize)
        {
            FieldInfo pad;
            pad.offset    = cursor;
            pad.size      = a_totalSize - cursor;
            pad.kind      = FieldKind::Padding;
            pad.isBitfield = false;
            result.push_back(pad);
        }

        a_fields = std::move(result);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Helpers
    // ─────────────────────────────────────────────────────────────────────────

    static uint32_t align(uint32_t v, uint32_t a) noexcept
    {
        return (v + a - 1) & ~(a - 1);
    }

    static bool detectIsStruct(const std::string& a_mangled)
    {
        return a_mangled.size() >= 4
            && a_mangled[0] == '.' && a_mangled[1] == '?'
            && a_mangled[2] == 'A' && a_mangled[3] == 'U';
    }

    static std::wstring toWide(const std::string& s)
    {
        return std::wstring(s.begin(), s.end());
    }
};

} // namespace DumpPDB
