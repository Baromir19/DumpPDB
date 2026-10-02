#pragma once

#include <Core/RTTI/FieldRecovery.hpp>
#include <Core/RTTI/RttiReader.hpp>
#include <Core/RTTI/RttiTypes.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
                std::wstring baseName = toWide(vft.subobjectBaseName);

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
                        std::wstring baseName = toWide(vft.subobjectBaseName);

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

                        const std::wstring ownerId = sanitizeIdentifier(
                            slot.isOverride && !slot.ownerName.empty()
                                ? slot.ownerName : a_type.fullName);

                        if (slot.isDestructor)
                        {
                            swprintf_s(buf, L"~%s();  // [%u] 0x%llX\n",
                                toWide(a_type.className).c_str(),
                                slot.slotIndex,
                                static_cast<unsigned long long>(slot.funcRva));
                        }
                        else if (slot.isOverride)
                        {
                            swprintf_s(buf, L"void func_%u_%s() override;  // [%u] 0x%llX\n",
                                slot.slotIndex, ownerId.c_str(), slot.slotIndex,
                                static_cast<unsigned long long>(slot.funcRva));
                        }
                        else // isNew
                        {
                            swprintf_s(buf, L"void func_%u_%s();  // [%u] 0x%llX\n",
                                slot.slotIndex, ownerId.c_str(), slot.slotIndex,
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
        const uint32_t observedEnd = rt.maxObservedOffset > 0
            ? align(rt.maxObservedOffset, ptrSz)
            : 0;

        // An allocation size that does not even cover the last observed field
        // write cannot be the real object size (e.g. a stray
        // `push imm32 ; call <helper>` found in the constructor body). Discard
        // it and fall back to the observed extent so gap filling still runs.
        if (rt.objectSize > 0 && rt.objectSize < observedEnd)
            rt.objectSize = 0;

        const uint32_t totalSize = rt.objectSize > 0 ? rt.objectSize : observedEnd;

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
    //
    // Every processed slot records its "owner": the derived class name for new
    // slots and destructors, the direct base name for overrides. The formatter
    // renders func_<slot>_<Owner> from this (sanitised) name.
    // ─────────────────────────────────────────────────────────────────────────

    /// Resolve the base class that owns the subobject vftable at
    /// a_vft.objectOffset.
    ///
    /// A *direct* base whose offset matches always wins. When no direct base
    /// owns the subobject (the interface was inherited through an intermediate
    /// class, e.g. TrolleyCarInstance -> VehicleInstance -> AI::INavMeshRegisterable),
    /// fall back to the most-derived transitive base at that offset. The COL
    /// base array is a depth-first list ordered most-derived first, so the first
    /// matching entry is the closest owner.
    [[nodiscard]] static const BaseClassInfo* findSubobjectBase(const VftableInfo& a_vft)
    {
        const BaseClassInfo* fallback = nullptr;

        for (const auto& b : a_vft.bases)
        {
            if (b.demangledName.empty()) continue;
            if (b.demangledName == a_vft.fullName) continue; // the class itself
            if (b.offset != static_cast<int32_t>(a_vft.objectOffset)) continue;

            if (b.isDirect) return &b;
            if (!fallback) fallback = &b; // depth-first => most derived first
        }

        // Primary subobject: any direct base (its offset is 0 by construction).
        if (!fallback && a_vft.objectOffset == 0)
        {
            for (const auto& b : a_vft.bases)
                if (b.isDirect && b.demangledName != a_vft.fullName)
                {
                    fallback = &b;
                    break;
                }
        }
        return fallback;
    }

    static void annotateOverrides(VftableInfo& a_vft, const RttiReader& a_reader)
    {
        // Find the base that owns this subobject — direct if possible, else the
        // most-derived transitive base (see findSubobjectBase).
        const BaseClassInfo* match = findSubobjectBase(a_vft);
        a_vft.subobjectBaseName = match ? match->demangledName : std::string();

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
            slot.ownerName  = a_vft.fullName; // default owner: the derived class

            if (slot.isDestructor || slot.isPureVirtual)
                continue;

            if (baseSlotRvas.empty() || slot.slotIndex >= baseSize)
            {
                slot.isNew = true;
                continue;
            }

            if (slot.funcRva != 0 && slot.funcRva == baseSlotRvas[slot.slotIndex])
                continue; // inherited, unchanged

            // Re-implemented by the derived class — but the slot signature
            // still belongs to the base interface that declared it, so the
            // owner name points at the direct base for the formatter.
            slot.isOverride = true;
            if (match && !match->demangledName.empty())
                slot.ownerName = match->demangledName;
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

        // Diagnostics (DUMPPDB_DEBUG=1): dump the ownFieldsStart decision.
        if (FieldRecoveryDbgOn())
        {
            std::fprintf(stderr, "[TR] computeOwnFieldsStart for %zu base(s):\n",
                a_bases.size());
            for (const auto& b : a_bases)
                std::fprintf(stderr, "[TR]   base '%s' off=0x%X direct=%d virt=%d\n",
                    b.demangledName.c_str(), static_cast<unsigned>(b.offset),
                    b.isDirect ? 1 : 0, b.isVirtual ? 1 : 0);
        }

        // ── Multiple inheritance ──────────────────────────────────────────────
        // Every vftable with objectOffset > 0 marks a base subobject, but the
        // extent of a base is NOT just its vfptr: MSVC packs the derived
        // class's own members after the primary base's full data, while
        // secondary (interface) subobjects may sit *inside* the derived
        // region. The RTTI COL offset alone therefore cannot serve as the
        // own-fields boundary. Use the primary (offset-0) base's full data
        // size, recovered recursively from *its* direct bases:
        //     ownStart >= primaryBaseSize (== 0xB0 for VehicleInstance).
        // Secondary interface vfptrs inside the derived region are reported
        // as explicit vfptr fields rather than hidden base data.
        // e.g. ActorInstance: ICharacterProxyHitOverrider @ 0xB8 -> 0xBC.
        std::unordered_set<std::string> visited;
        const uint32_t primaryEnd = baseDataSize(a_bases, a_reader, a_pe,
            a_bitness, visited, 0);
        if (FieldRecoveryDbgOn())
            std::fprintf(stderr, "[TR]   baseDataSize -> 0x%X\n", primaryEnd);
        if (primaryEnd > 0)
            return primaryEnd;

        // Recursive recovery failed: fall back to the largest secondary
        // subobject offset + vfptr, as before.
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

    /// Recursively estimate the full data extent of the primary (offset-0)
    /// base chain:
    ///     extent(base) = max( extent(primaryBaseOfBase),
    ///                        max over direct secondary bases (base.offset + ptrSize) )
    /// with the recovered constructor field extent of the base itself as a
    /// lower bound. Returns 0 when nothing could be recovered (caller falls
    /// back to the old subobject-offset heuristic).
    ///
    /// a_visited guards against cyclic hierarchies; a_depth caps recursion.
    static uint32_t baseDataSize(const std::vector<BaseClassInfo>& a_bases,
        const RttiReader& a_reader,
        const PEImage& a_pe,
        PEBitness a_bitness,
        std::unordered_set<std::string>& a_visited,
        int a_depth)
    {
        if (a_depth > 8) return 0;

        const uint32_t ptrSz = (a_bitness == PEBitness::Bits32) ? 4u : 8u;
        uint32_t maxEnd = 0;

        for (const auto& base : a_bases)
        {
            if (base.demangledName.empty() || base.offset < 0 || !base.isDirect)
                continue;

            const uint32_t baseOffset = static_cast<uint32_t>(base.offset);

            if (base.isVirtual)
            {
                // Virtual bases live at the tail of the object; only their
                // vbptr slot inside the derived region matters here.
                maxEnd = std::max(maxEnd, baseOffset + ptrSz);
                continue;
            }

            if (baseOffset == 0)
            {
                // Primary base: its own extent determines the boundary.
                // Recurse through ITS direct bases + ctor data so that a long
                // primary chain accumulates correctly.
                if (a_visited.count(base.demangledName)) continue;
                a_visited.insert(base.demangledName);

                uint32_t inner = 0;
                auto baseVfts = a_reader.findExact(base.demangledName);
                const VftableInfo* bPrimary = nullptr;
                for (const auto* bv : baseVfts)
                    if (bv->objectOffset == 0) { bPrimary = bv; break; }

                if (bPrimary)
                {
                    // 1) Recursive extent from the base's own hierarchy.
                    inner = std::max(inner, baseDataSize(bPrimary->bases,
                        a_reader, a_pe, a_bitness, a_visited, a_depth + 1));

                    // 2) Collect secondary vftable RVAs/offsets for this base
                    //    so FieldRecovery can correctly identify its most-derived
                    //    constructor (the one that installs secondary vfptrs).
                    std::vector<uint64_t> baseSecRvas;
                    std::vector<uint32_t> baseSecOffs;
                    for (const auto* bv : baseVfts)
                    {
                        if (bv->objectOffset > 0)
                        {
                            baseSecRvas.push_back(bv->vftableRva);
                            baseSecOffs.push_back(bv->objectOffset);
                        }
                    }

                    // 3) All known vftable RVAs/names for foreign-vftable detection.
                    std::vector<uint64_t> allVftRvas;
                    std::vector<std::string> allVftNames;
                    allVftRvas.reserve(a_reader.vftables().size());
                    allVftNames.reserve(a_reader.vftables().size());
                    for (const auto& vft : a_reader.vftables())
                    {
                        allVftRvas.push_back(vft.vftableRva);
                        allVftNames.push_back(vft.fullName);
                    }

                    // 4) Lower bound from the base's own recovered fields.
                    FieldRecovery fr;
                    uint32_t baseObjSize = 0;
                    uint32_t baseMaxOff  = 0;
                    auto bFields = fr.recover(bPrimary->vftableRva, a_pe, a_bitness,
                        baseSecRvas, baseSecOffs, allVftRvas, allVftNames,
                        baseObjSize, baseMaxOff);
                    uint32_t ownMax = 0;
                    for (const auto& f : bFields)
                        ownMax = std::max(ownMax, f.offset + f.size);
                    if (baseObjSize > 0 && baseObjSize < 0x1000000)
                        ownMax = std::max(ownMax, baseObjSize);
                    inner = std::max(inner, ownMax);

                    if (FieldRecoveryDbgOn())
                        std::fprintf(stderr,
                            "[TR]   depth=%d base='%s' off=0x%X inner=0x%X "
                            "baseFields=%zu ownMax=0x%X baseObjSize=0x%X\n",
                            a_depth, base.demangledName.c_str(), baseOffset,
                            inner, bFields.size(), ownMax, baseObjSize);
                }

                a_visited.erase(base.demangledName);
                maxEnd = std::max(maxEnd, baseOffset + inner);
            }
            else
            {
                // Secondary non-virtual base: at minimum its vfptr/subobject
                // slot at the recorded offset.
                maxEnd = std::max(maxEnd, baseOffset + ptrSz);
            }
        }

        return maxEnd;
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

    /// Diagnostics switch (mirrors FieldRecovery::dbgOn): DUMPPDB_DEBUG=1.
    [[nodiscard]] static bool FieldRecoveryDbgOn()
    {
        static const bool on = std::getenv("DUMPPDB_DEBUG") != nullptr;
        return on;
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

    /// Make a demangled "NS::Class<T>" name a valid C++ identifier fragment:
    /// alphanumerics and '_' are kept, everything else becomes '_'.
    /// e.g. "AI::INavMeshRegisterable" -> "AI_INavMeshRegisterable".
    static std::wstring sanitizeIdentifier(const std::string& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (char c : s)
        {
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                || (c >= '0' && c <= '9') || c == '_';
            out.push_back(ok ? static_cast<wchar_t>(c) : L'_');
        }
        if (out.empty()) out = L"Unknown";
        return out;
    }
};

} // namespace DumpPDB
