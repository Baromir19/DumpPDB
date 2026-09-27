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

        const wchar_t* bits = (a_type.bitness == PEBitness::Bits32) ? L"x86" : L"x64";
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
            // Estimate: last field offset + field size, rounded up to ptr alignment
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
        for (size_t vi = 0; vi < a_type.vftables.size(); ++vi)
        {
            const auto& vft = a_type.vftables[vi];
            wchar_t buf[256];
            if (vft.objectOffset == 0)
            {
                swprintf_s(buf, L"    /* vftable @ RVA 0x%llX (%zu slots) */\n",
                    static_cast<unsigned long long>(vft.vftableRva),
                    vft.vfuncs.size());
            }
            else
            {
                // Try to identify which base class this vftable belongs to
                std::wstring baseName;
                for (const auto& b : a_type.directBases)
                {
                    if (b.offset == static_cast<int32_t>(vft.objectOffset))
                    {
                        baseName = toWide(b.demangledName);
                        break;
                    }
                }
                if (baseName.empty())
                    swprintf_s(buf, L"    /* vftable @ RVA 0x%llX, offset +0x%X (%zu slots) */\n",
                        static_cast<unsigned long long>(vft.vftableRva),
                        vft.objectOffset, vft.vfuncs.size());
                else
                    swprintf_s(buf, L"    /* vftable @ RVA 0x%llX, offset +0x%X (%zu slots) [%s] */\n",
                        static_cast<unsigned long long>(vft.vftableRva),
                        vft.objectOffset, vft.vfuncs.size(), baseName.c_str());
            }
            out += buf;
        }

        // ── Virtual functions ─────────────────────────────────────────────────
        if (!a_type.vftables.empty() && !a_type.vftables[0].vfuncs.empty())
        {
            out += L"\n    /// VIRTUAL FUNCTIONS:\n";
            for (const auto& slot : a_type.vftables[0].vfuncs)
            {
                wchar_t buf[256];
                const std::wstring className = toWide(a_type.className);

                if (slot.isPureVirtual)
                {
                    swprintf_s(buf, L"    virtual void func_%u() = 0;\n", slot.slotIndex);
                    out += buf;
                    continue;
                }

                // Prefix: virtual keyword
                out += L"    virtual ";

                if (slot.isDestructor)
                {
                    swprintf_s(buf, L"~%s();", className.c_str());
                    out += buf;
                }
                else
                {
                    swprintf_s(buf, L"void func_%u();", slot.slotIndex);
                    out += buf;
                    if (slot.isOverride) out += L" override";
                }

                // Suffix comment with RVA and override/new annotation
                if (slot.isDestructor)
                {
                    swprintf_s(buf, L"  // [%u] dtor  0x%llX\n",
                        slot.slotIndex,
                        static_cast<unsigned long long>(slot.funcRva));
                }
                else if (slot.isNew)
                {
                    swprintf_s(buf, L"  // [%u] 0x%llX  (new)\n",
                        slot.slotIndex,
                        static_cast<unsigned long long>(slot.funcRva));
                }
                else if (slot.isOverride)
                {
                    swprintf_s(buf, L"  // [%u] 0x%llX\n",
                        slot.slotIndex,
                        static_cast<unsigned long long>(slot.funcRva));
                }
                else
                {
                    swprintf_s(buf, L"  // [%u] 0x%llX\n",
                        slot.slotIndex,
                        static_cast<unsigned long long>(slot.funcRva));
                }
                out += buf;
            }
        }

        // ── Fields ────────────────────────────────────────────────────────────
        if (!a_type.fields.empty())
        {
            out += L"\n    /// FIELDS:\n";
            for (const auto& f : a_type.fields)
            {
                wchar_t buf[320];

                if (f.isMIBaseVfptr)
                {
                    // MI base vftable pointer — show as vfptr with base name hint
                    const std::wstring hint = toWide(f.embeddedClassName);
                    if (hint.empty())
                        swprintf_s(buf, L"    void*    vfptr_%02X;           /* 0x%02X */\n",
                            f.offset, f.offset);
                    else
                        swprintf_s(buf, L"    void*    vfptr_%02X;           /* 0x%02X */  // %s\n",
                            f.offset, f.offset, hint.c_str());
                    out += buf;
                    continue;
                }

                if (f.kind == FieldKind::Padding)
                {
                    // Gap filler
                    swprintf_s(buf, L"    uint8_t  pad_0x%02X[%u];",
                        f.offset, f.size);
                    out += buf;
                    wchar_t padBuf[64];
                    swprintf_s(padBuf, L"  /* 0x%02X */  // padding\n", f.offset);
                    out += padBuf;
                    continue;
                }

                const std::wstring typeName = toWide(f.typeName());

                if (!f.embeddedClassName.empty())
                {
                    // Embedded object hint
                    if (f.isBitfield)
                    {
                        swprintf_s(buf, L"    %-8s fld_0x%02X : %u;",
                            typeName.c_str(), f.offset, f.bitSize);
                    }
                    else
                    {
                        swprintf_s(buf, L"    %-8s fld_0x%02X;",
                            typeName.c_str(), f.offset);
                    }
                    out += buf;
                    wchar_t cmt[160];
                    swprintf_s(cmt, L"  /* 0x%02X */  // probably: %s (RVA 0x%llX)\n",
                        f.offset,
                        toWide(f.embeddedClassName).c_str(),
                        static_cast<unsigned long long>(f.embeddedVftableRva));
                    out += cmt;
                }
                else if (f.isBitfield)
                {
                    swprintf_s(buf, L"    %-8s fld_0x%02X : %u;",
                        typeName.c_str(), f.offset, f.bitSize);
                    out += buf;
                    wchar_t cmt[64];
                    swprintf_s(cmt, L"  /* 0x%02X */\n", f.offset);
                    out += cmt;
                }
                else
                {
                    swprintf_s(buf, L"    %-8s fld_0x%02X;",
                        typeName.c_str(), f.offset);
                    out += buf;
                    wchar_t cmt[64];
                    swprintf_s(cmt, L"  /* 0x%02X */\n", f.offset);
                    out += cmt;
                }
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

        // ── Override / new detection for primary vftable ──────────────────────
        if (!rt.vftables.empty())
        {
            annotateOverrides(rt.vftables[0], a_reader);
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

        // ── Field recovery ────────────────────────────────────────────────────
        {
            FieldRecovery fr;
            uint32_t objectSize      = 0;
            uint32_t maxObservedOff  = 0;
            rt.fields = fr.recover(primary->vftableRva, a_pe, a_bitness,
                                   baseVftableRvas, baseVftableOffsets,
                                   objectSize, maxObservedOff);
            rt.objectSize        = objectSize;
            rt.maxObservedOffset = maxObservedOff;
        }

        // ── Annotate embedded class hints in fields ───────────────────────────
        annotateEmbeddedHints(rt.fields, rt.vftables, a_reader, a_bitness);

        // ── Gap filling ───────────────────────────────────────────────────────
        const uint32_t ptrSz = (a_bitness == PEBitness::Bits32) ? 4u : 8u;
        const uint32_t totalSize = rt.objectSize > 0
            ? rt.objectSize
            : (rt.maxObservedOffset > 0 ? align(rt.maxObservedOffset, ptrSz) : 0);

        if (totalSize > 0)
            fillGaps(rt.fields, totalSize, ptrSz);

        return rt;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Override / new annotation
    //
    // For each slot i in the primary vftable:
    //   - Collect the funcRva of slot i from each direct base's primary vftable.
    //   - If our slot RVA matches any base slot RVA → isOverride = true.
    //   - If slot index >= base vftable size → isNew = true.
    // ─────────────────────────────────────────────────────────────────────────

    static void annotateOverrides(VftableInfo& a_vft, const RttiReader& a_reader)
    {
        // Collect all base primary vftables (objectOffset == 0 for the base type)
        // by looking up the base class names in the reader.
        struct BaseVft
        {
            std::vector<uint64_t> slotRvas; // indexed by slot
        };
        std::vector<BaseVft> baseVfts;

        for (size_t bi = 1; bi < a_vft.bases.size(); ++bi)
        {
            const auto& base = a_vft.bases[bi];
            if (base.demangledName.empty()) continue;
            if (base.demangledName == a_vft.fullName) continue;

            // Find this base in the reader
            auto baseFound = a_reader.findExact(base.demangledName);
            for (const auto* bv : baseFound)
            {
                if (bv->objectOffset != 0) continue; // only primary vftable

                BaseVft bvft;
                bvft.slotRvas.reserve(bv->vfuncs.size());
                for (const auto& s : bv->vfuncs)
                    bvft.slotRvas.push_back(s.funcRva);
                baseVfts.push_back(std::move(bvft));
            }
        }

        if (baseVfts.empty())
        {
            // No base vftables found — mark all non-dtor as new
            for (auto& slot : a_vft.vfuncs)
            {
                slot.isNew      = !slot.isDestructor;
                slot.isOverride = false;
            }
            return;
        }

        // Determine max base vftable size
        size_t maxBaseSize = 0;
        for (const auto& bv : baseVfts)
            maxBaseSize = std::max(maxBaseSize, bv.slotRvas.size());

        for (auto& slot : a_vft.vfuncs)
        {
            if (slot.isDestructor)
            {
                slot.isNew      = false;
                slot.isOverride = false;
                continue;
            }

            if (slot.slotIndex >= maxBaseSize)
            {
                slot.isNew      = true;
                slot.isOverride = false;
                continue;
            }

            // Check if any base has the same RVA at this slot
            bool sameAsBase = false;
            for (const auto& bv : baseVfts)
            {
                if (slot.slotIndex < bv.slotRvas.size()
                    && bv.slotRvas[slot.slotIndex] == slot.funcRva
                    && slot.funcRva != 0)
                {
                    sameAsBase = true;
                    break;
                }
            }

            if (sameAsBase)
            {
                // Inherited but not overridden — still show, but neither new nor override
                slot.isNew      = false;
                slot.isOverride = false;
            }
            else
            {
                // Different RVA than base — it's overridden
                slot.isNew      = false;
                slot.isOverride = true;
            }
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
    // Gap filling
    //
    // Walk the sorted field list, insert Padding entries for every byte range
    // not covered by a known field. Stop at a_totalSize.
    // ─────────────────────────────────────────────────────────────────────────

    static void fillGaps(std::vector<FieldInfo>& a_fields,
                         uint32_t a_totalSize,
                         uint32_t /*ptrSz*/)
    {
        if (a_totalSize == 0) return;

        std::vector<FieldInfo> result;
        result.reserve(a_fields.size() * 2);

        uint32_t cursor = 0;

        for (const auto& f : a_fields)
        {
            if (f.offset > cursor)
            {
                // There's a gap [cursor, f.offset)
                FieldInfo pad;
                pad.offset    = cursor;
                pad.size      = f.offset - cursor;
                pad.kind      = FieldKind::Padding;
                pad.isBitfield = false;
                result.push_back(pad);
            }
            else if (f.offset < cursor)
            {
                // Overlapping field (can happen with bitfields) — skip
                continue;
            }

            result.push_back(f);
            cursor = f.offset + f.size;
        }

        // Trailing gap to totalSize
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
