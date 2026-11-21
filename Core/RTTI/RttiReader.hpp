#pragma once

#include <Core/PE/PEImage.hpp>
#include <Core/PE/PERelocations.hpp>
#include <Core/RTTI/MsvcDemangler.hpp>
#include <Core/RTTI/RttiTypes.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace DumpPDB
{

/// ============================================================================
/// RttiReader — dual-mode (x86 / x64) MSVC RTTI parser
///
/// Algorithm (identical for both bitnesses, only the field widths differ):
///
///   1. Scan every initialised, non-executable section for
///      RTTICompleteObjectLocator candidates and validate each one (signature,
///      pointers into the image, and — crucially — that the TypeDescriptor it
///      references really holds a ".?A..." name).
///   2. Make ONE pass over the same sections looking for pointers whose value
///      is the address of a validated COL. The slot after such a pointer is the
///      first entry of the vftable. (The previous implementation rescanned the
///      whole section for every COL, which is quadratic: ~12 s on a 28 MB exe.)
///   3. Read the vftable slots. When the image carries base relocations the
///      table ends exactly at the first slot that is not a relocated pointer
///      into code; otherwise the first non-code value ends it.
///   4. Read the class hierarchy (CHD / BCD) including displacements.
///   5. Demangle the names with MsvcDemangler (templates, anonymous namespaces,
///      function-local classes, ...).
///   6. Scan for ALL TypeDescriptors, so interface/base-only classes that have
///      no vftable of their own are still reported.
///   7. Recognise the `_purecall` thunk statistically and flag pure slots.
/// ============================================================================
class RttiReader
{
public:

    RttiReader() = default;

    /// Load and parse a PE file. Detects bitness automatically from the PE header.
    bool load(const std::wstring& a_path)
    {
        clear();

        if (!m_pe.load(a_path))
            return false;
        if (!m_pe.isValid())
            return false;

        m_bitness = m_pe.is64Bit() ? PEBitness::Bits64 : PEBitness::Bits32;
        m_layout = RttiLayout::forBitness(m_bitness);
        m_imageBase = m_pe.imageBase();
        m_relocs.load(m_pe);

        findSections();
        if (m_dataSections.empty() || m_codeSections.empty())
            return false;

        scanForCols();
        scanTypeDescriptors();
        detectPureCall();
        buildIndexes();
        return true;
    }

    [[nodiscard]] const std::vector<VftableInfo>& vftables() const noexcept { return m_vftables; }
    [[nodiscard]] PEBitness bitness() const noexcept { return m_bitness; }
    [[nodiscard]] const PEImage& pe() const noexcept { return m_pe; }
    [[nodiscard]] const PERelocations& relocations() const noexcept { return m_relocs; }
    [[nodiscard]] const RttiLayout& layout() const noexcept { return m_layout; }

    /// Every TypeDescriptor found in the image (with or without a vftable).
    [[nodiscard]] const std::vector<TypeDescriptorInfo>& typeDescriptors() const noexcept
    {
        return m_typeDescs;
    }

    /// RVA of the most widely shared pure-virtual stub (0 when none recognised).
    [[nodiscard]] uint64_t pureCallRva() const noexcept { return m_pureCallRva; }

    /// Every function recognised as a pure-virtual stub (sorted RVAs).
    [[nodiscard]] const std::vector<uint64_t>& pureStubs() const noexcept { return m_pureStubs; }

    /// RVA of type_info's own vftable (0 when unknown).
    [[nodiscard]] uint64_t typeInfoVftableRva() const noexcept { return m_typeInfoVftable; }

    /// Find vftables by exact demangled name (case-insensitive, ignoring spaces
    /// so "A<B<int>>" and "A<B<int> >" are the same query).
    [[nodiscard]] std::vector<const VftableInfo*> findExact(const std::string& a_name) const
    {
        std::vector<const VftableInfo*> result;
        const auto it = m_byName.find(normalize(a_name));
        if (it == m_byName.end())
            return result;

        for (size_t idx : it->second)
            result.push_back(&m_vftables[idx]);
        return result;
    }

    /// Find vftables whose name contains a_substr (case-insensitive substring match).
    [[nodiscard]] std::vector<const VftableInfo*> findByName(const std::string& a_name) const
    {
        const std::string needle = normalize(a_name);
        std::vector<const VftableInfo*> result;
        for (size_t i = 0; i < m_vftables.size(); ++i)
        {
            if (m_normFull[i].find(needle) != std::string::npos
                || m_normLeaf[i].find(needle) != std::string::npos)
            {
                result.push_back(&m_vftables[i]);
            }
        }
        return result;
    }

    /// The vftable that starts at a_rva, or nullptr.
    [[nodiscard]] const VftableInfo* findByVftableRva(uint64_t a_rva) const
    {
        const auto it = m_byVftableRva.find(a_rva);
        return it == m_byVftableRva.end() ? nullptr : &m_vftables[it->second];
    }

    /// All vftables of one class (identified by its TypeDescriptor), primary first.
    [[nodiscard]] std::vector<const VftableInfo*> vftablesOfType(uint64_t a_typeDescRva) const
    {
        std::vector<const VftableInfo*> result;
        const auto it = m_byTypeDesc.find(a_typeDescRva);
        if (it == m_byTypeDesc.end())
            return result;
        for (size_t idx : it->second)
            result.push_back(&m_vftables[idx]);
        return result;
    }

    /// Unique type names, alphabetically sorted (deduped — MI types appear once).
    [[nodiscard]] std::vector<std::string> allTypeNames() const
    {
        std::vector<std::string> names;
        names.reserve(m_vftables.size());
        for (const auto& v : m_vftables)
            names.push_back(v.fullName);

        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        return names;
    }

    /// Demangle with the default (PDB-compatible) style; falls back to the raw text.
    [[nodiscard]] static std::string demangleMsvc(const std::string& a_mangled)
    {
        const std::string out = MsvcDemangler::demangle(a_mangled);
        return out;
    }

    [[nodiscard]] static std::string extractClassName(const std::string& a_fullName)
    {
        return MsvcDemangler::leafName(a_fullName);
    }

private:

    void clear()
    {
        m_vftables.clear();
        m_typeDescs.clear();
        m_dataSections.clear();
        m_codeSections.clear();
        m_byName.clear();
        m_byVftableRva.clear();
        m_byTypeDesc.clear();
        m_normFull.clear();
        m_normLeaf.clear();
        m_pureCallRva = 0;
        m_typeInfoVftable = 0;
        m_pureStubs.clear();
        m_relocs = PERelocations();
        m_pe = PEImage();
    }

    // ─────────────────────────────────────────────────────────────────────
    // Section discovery
    // ─────────────────────────────────────────────────────────────────────

    void findSections()
    {
        for (const auto& s : m_pe.sections())
        {
            if (s.empty())
                continue;

            if (s.isExecutable() || s.hasCode())
            {
                m_codeSections.push_back(&s);
                continue;
            }

            // Everything readable and file-backed can hold RTTI: .rdata normally,
            // but merged/renamed sections (.data, .rodata, custom) do occur.
            if (s.isReadable() && s.hasInitializedData() && s.name() != ".rsrc"
                && s.name() != ".reloc")
            {
                m_dataSections.push_back(&s);
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Address helpers — unify x86 abs-VA vs x64 RVA into a common "rva" space
    // ─────────────────────────────────────────────────────────────────────

    /// Convert a raw 32-bit value stored in a COL/CHD/BCD to an RVA.
    /// x64: the value IS an RVA already.  x86: it is an absolute VA.
    [[nodiscard]] bool toRva(uint32_t a_raw, uint64_t& a_rva) const noexcept
    {
        if (m_bitness == PEBitness::Bits32)
        {
            if (static_cast<uint64_t>(a_raw) < m_imageBase)
                return false;
            a_rva = static_cast<uint64_t>(a_raw) - m_imageBase;
            return true;
        }
        a_rva = a_raw;
        return true;
    }

    [[nodiscard]] bool isDataRva(uint64_t a_rva) const noexcept
    {
        for (const auto* s : m_dataSections)
        {
            if (a_rva >= s->virtualAddress() && a_rva < s->virtualAddress() + s->size())
                return true;
        }
        return false;
    }

    [[nodiscard]] bool isCodeRva(uint64_t a_rva) const noexcept
    {
        for (const auto* s : m_codeSections)
        {
            if (a_rva >= s->virtualAddress() && a_rva < s->virtualAddress() + s->virtualSize())
                return true;
        }
        return false;
    }

    /// Mangled name stored in the TypeDescriptor at a_tdRva ("" when it is not one).
    [[nodiscard]] std::string readTypeName(uint64_t a_tdRva) const
    {
        const std::string name = m_pe.readCString(a_tdRva + m_layout.typeDescNameOffset, 1024);
        if (name.size() < 5 || name[0] != '.' || name[1] != '?' || name[2] != 'A')
            return {};
        return name;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Step 1: COL candidates
    // ─────────────────────────────────────────────────────────────────────

    struct ColCandidate
    {
        uint64_t colRva;
        uint32_t objectOffset;
        uint32_t cdOffset;
        uint64_t tdRva;
        uint64_t chdRva;
    };

    void scanForCols()
    {
        std::vector<ColCandidate> cols;

        const size_t colSize = m_layout.colSize;

        for (const auto* sec : m_dataSections)
        {
            const BinaryView view = sec->view();
            const uint32_t secVa = sec->virtualAddress();
            if (view.size() < colSize)
                continue;

            for (size_t i = 0; i + colSize <= view.size(); i += 4)
            {
                uint32_t sig = 0;
                if (!view.readLE<uint32_t>(i, sig) || sig != m_layout.colSignature)
                    continue;

                uint32_t offset = 0, cdOffset = 0, tdRaw = 0, chdRaw = 0;
                if (!view.readLE<uint32_t>(i + 4, offset) || !view.readLE<uint32_t>(i + 8, cdOffset)
                    || !view.readLE<uint32_t>(i + 12, tdRaw)
                    || !view.readLE<uint32_t>(i + 16, chdRaw))
                    continue;

                uint64_t tdRva = 0, chdRva = 0;
                if (!toRva(tdRaw, tdRva) || !toRva(chdRaw, chdRva))
                    continue;
                if (!isDataRva(tdRva) || !isDataRva(chdRva))
                    continue;

                const uint64_t colRva = static_cast<uint64_t>(secVa) + i;

                if (m_layout.colHasSelfRva)
                {
                    uint32_t selfRaw = 0;
                    if (!view.readLE<uint32_t>(i + 20, selfRaw) || selfRaw != colRva)
                        continue;
                }

                // The strongest filter: the TypeDescriptor must hold a real RTTI name.
                if (readTypeName(tdRva).empty())
                    continue;

                cols.push_back({colRva, offset, cdOffset, tdRva, chdRva});
            }
        }

        if (cols.empty())
            return;

        // ── Step 2: one pass locating the pointer-to-COL that precedes each vftable ──
        std::unordered_map<uint64_t, size_t> colByVa; // colVa -> index into cols
        colByVa.reserve(cols.size() * 2);
        for (size_t i = 0; i < cols.size(); ++i)
            colByVa.emplace(m_imageBase + cols[i].colRva, i);

        std::vector<uint64_t> vftableOfCol(cols.size(), 0);
        const size_t ptrSize = m_layout.ptrSize;

        for (const auto* sec : m_dataSections)
        {
            const BinaryView view = sec->view();
            const uint32_t secVa = sec->virtualAddress();
            if (view.size() < ptrSize)
                continue;

            for (size_t j = 0; j + ptrSize <= view.size(); j += ptrSize)
            {
                uint64_t value = 0;
                if (ptrSize == 8)
                {
                    if (!view.readLE<uint64_t>(j, value))
                        continue;
                }
                else
                {
                    uint32_t v32 = 0;
                    if (!view.readLE<uint32_t>(j, v32))
                        continue;
                    value = v32;
                }

                const auto it = colByVa.find(value);
                if (it == colByVa.end())
                    continue;

                const uint64_t slotRva = static_cast<uint64_t>(secVa) + j;
                // With relocations present the meta pointer must itself be relocated.
                if (!m_relocs.empty() && !m_relocs.contains(slotRva))
                    continue;

                if (vftableOfCol[it->second] == 0)
                    vftableOfCol[it->second] = slotRva + ptrSize;
            }
        }

        // ── Steps 3-5: build the per-vftable records ──
        m_vftables.reserve(cols.size());
        for (size_t i = 0; i < cols.size(); ++i)
        {
            if (vftableOfCol[i] == 0)
                continue;
            processCol(cols[i], vftableOfCol[i]);
        }

        std::sort(m_vftables.begin(),
            m_vftables.end(),
            [](const VftableInfo& a, const VftableInfo& b)
            {
                if (a.fullName != b.fullName)
                    return a.fullName < b.fullName;
                return a.objectOffset < b.objectOffset;
            });
    }

    void processCol(const ColCandidate& a_col, uint64_t a_vftableRva)
    {
        const std::string mangledName = readTypeName(a_col.tdRva);
        if (mangledName.empty())
            return;

        VftableInfo info;
        info.vftableRva = a_vftableRva;
        info.colRva = a_col.colRva;
        info.objectOffset = a_col.objectOffset;
        info.constructorDisplacement = a_col.cdOffset;
        info.typeDescriptorRva = a_col.tdRva;
        info.chdRva = a_col.chdRva;
        info.mangledName = mangledName;

        fillNames(info, mangledName);

        info.vfuncs = readVftableSlots(a_vftableRva);
        info.vftableSizeBytes = static_cast<uint32_t>(info.vfuncs.size() * m_layout.ptrSize);

        InheritanceKind inheritance = InheritanceKind::None;
        uint32_t chdAttrs = 0;
        info.bases = readHierarchy(a_col.chdRva, inheritance, chdAttrs);
        info.inheritance = inheritance;
        info.chdAttributes = chdAttrs;

        m_vftables.push_back(std::move(info));
    }

    void fillNames(VftableInfo& a_info, const std::string& a_mangled) const
    {
        const auto parsed = MsvcDemangler::parse(a_mangled);
        if (parsed.ok)
        {
            a_info.fullName = parsed.name;
            a_info.isTemplateInstance = parsed.isTemplate;
            a_info.isInAnonymousNamespace = parsed.isAnonymous;
            a_info.isLocalToFunction = parsed.isLocal;
            a_info.isStructTag = parsed.kind == MsvcDemangler::Kind::Struct;
            a_info.isUnionTag = parsed.kind == MsvcDemangler::Kind::Union;
        }
        else
        {
            // Keep the entry (it is still a real class) but make the failure visible.
            a_info.fullName = a_mangled;
            a_info.demangleFailed = true;
            a_info.isStructTag = a_mangled.size() > 3 && a_mangled[3] == 'U';
        }

        a_info.className = MsvcDemangler::leafName(a_info.fullName);
        a_info.leafName = a_info.className;
        a_info.scopeName = MsvcDemangler::scopeOf(a_info.fullName);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Step 3: vftable slot reading
    // ─────────────────────────────────────────────────────────────────────

    [[nodiscard]] std::vector<VfuncSlot> readVftableSlots(uint64_t a_vftRva) const
    {
        std::vector<VfuncSlot> slots;

        const uint64_t ptrSize = m_layout.ptrSize;
        const bool haveRelocs = !m_relocs.empty();

        for (uint32_t idx = 0; idx < 2048; ++idx)
        {
            const uint64_t slotRva = a_vftRva + static_cast<uint64_t>(idx) * ptrSize;

            uint64_t funcVa = 0;
            if (!m_pe.readPointer(slotRva, funcVa))
                break;

            // A relocated pointer is part of the table; anything else ends it.
            if (haveRelocs && !m_relocs.contains(slotRva))
                break;

            if (funcVa < m_imageBase)
                break;
            const uint64_t funcRva = funcVa - m_imageBase;
            if (!isCodeRva(funcRva))
                break;

            VfuncSlot s;
            s.slotIndex = idx;
            s.funcRva = funcRva;
            s.isPureVirtual = false;
            // Only slot 0 is the (scalar) deleting destructor. On a derived
            // class's primary vftable MSVC may also emit a vector-deleting
            // destructor in slot 1, but it is usually inherited from the base,
            // so it is caught by the plain override/inherit comparison instead.
            s.isDestructor = (idx == 0);
            s.isOverride = false;
            s.isNew = false;
            slots.push_back(std::move(s));
        }

        return slots;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Step 4: CHD/BCD hierarchy reading
    // ─────────────────────────────────────────────────────────────────────

    [[nodiscard]] std::vector<BaseClassInfo> readHierarchy(
        uint64_t a_chdRva, InheritanceKind& a_kind, uint32_t& a_attrs) const
    {
        std::vector<BaseClassInfo> bases;
        a_kind = InheritanceKind::None;
        a_attrs = 0;

        ChdOnDisk chd{};
        if (!m_pe.readU32(a_chdRva, chd.signature) || !m_pe.readU32(a_chdRva + 4, chd.attributes)
            || !m_pe.readU32(a_chdRva + 8, chd.numBaseClasses)
            || !m_pe.readU32(a_chdRva + 12, chd.baseClassArrayAddr))
            return bases;

        a_attrs = chd.attributes;

        const bool isMultiple = (chd.attributes & 1) != 0;
        const bool isVirtual = (chd.attributes & 2) != 0;
        if (isVirtual)
            a_kind = InheritanceKind::Virtual;
        else if (isMultiple)
            a_kind = InheritanceKind::Multiple;
        else if (chd.numBaseClasses > 1)
            a_kind = InheritanceKind::Single;

        if (chd.numBaseClasses == 0 || chd.numBaseClasses > 1024)
            return bases;

        uint64_t arrayRva = 0;
        if (!toRva(chd.baseClassArrayAddr, arrayRva))
            return bases;

        bases.reserve(chd.numBaseClasses);

        // The MSVC base-class array is a depth-first flattened list:
        //   [0]             = the class itself
        //   [1]             = first direct base, immediately followed by its own
        //                     contained bases, then the second direct base, etc.
        // numContainedBases counts the transitive bases of an entry (not
        // including itself), so the next *direct* base sits 1 + ncb entries on.
        uint32_t nextDirectIndex = 1;

        for (uint32_t i = 0; i < chd.numBaseClasses; ++i)
        {
            uint32_t bcdRaw = 0;
            if (!m_pe.readU32(arrayRva + static_cast<uint64_t>(i) * 4, bcdRaw))
                break;

            uint64_t bcdRva = 0;
            if (!toRva(bcdRaw, bcdRva))
                continue;

            BcdOnDisk bcd{};
            if (!m_pe.readU32(bcdRva, bcd.typeDescriptorAddr)
                || !m_pe.readU32(bcdRva + 4, bcd.numContainedBases)
                || !m_pe.readAtRva<int32_t>(bcdRva + 8, bcd.pmd.mdisp)
                || !m_pe.readAtRva<int32_t>(bcdRva + 12, bcd.pmd.pdisp)
                || !m_pe.readAtRva<int32_t>(bcdRva + 16, bcd.pmd.vdisp)
                || !m_pe.readU32(bcdRva + 20, bcd.attributes))
                continue;

            uint64_t btdRva = 0;
            if (!toRva(bcd.typeDescriptorAddr, btdRva))
                continue;

            const std::string mangledBase = readTypeName(btdRva);
            if (mangledBase.empty())
                continue;

            BaseClassInfo bc;
            bc.mangledName = mangledBase;
            bc.demangledName = MsvcDemangler::demangle(mangledBase);
            if (bc.demangledName.empty())
                bc.demangledName = mangledBase;
            bc.offset = bcd.pmd.mdisp;
            bc.pdisp = bcd.pmd.pdisp;
            bc.vdisp = bcd.pmd.vdisp;
            bc.attributes = bcd.attributes;
            bc.numContainedBases = bcd.numContainedBases;
            bc.typeDescriptorRva = btdRva;
            // pdisp == -1 means "no vbtable pointer" i.e. a plain (non-virtual) base.
            bc.isVirtual = bcd.pmd.pdisp != -1
                           || (bcd.attributes & BaseClassInfo::kVirtualBaseOfContObj) != 0;
            bc.isDirect = (i == nextDirectIndex);

            // Advance to the next direct base (skip this base's transitive bases).
            if (bc.isDirect)
                nextDirectIndex = i + 1 + bcd.numContainedBases;

            bases.push_back(std::move(bc));
        }

        return bases;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Step 6: every TypeDescriptor in the image
    // ─────────────────────────────────────────────────────────────────────

    void scanTypeDescriptors()
    {
        const size_t nameOff = m_layout.typeDescNameOffset;
        const bool haveRelocs = !m_relocs.empty();

        struct Cand
        {
            uint64_t rva;
            uint64_t vptr;
            std::string mangled;
        };
        std::vector<Cand> cands;

        auto scanSection = [&](const PESection* a_sec)
        {
            const BinaryView v = a_sec->view();
            const uint32_t secVa = a_sec->virtualAddress();
            if (v.size() < nameOff + 5)
                return;

            for (size_t i = nameOff; i + 5 <= v.size(); ++i)
            {
                if (v[i] != '.' || v[i + 1] != '?' || v[i + 2] != 'A')
                    continue;
                const uint8_t tag = v[i + 3];
                if (tag != 'V' && tag != 'U' && tag != 'T' && tag != 'W')
                    continue;

                const uint64_t tdRva = static_cast<uint64_t>(secVa) + i - nameOff;
                if (tdRva % 4 != 0)
                    continue;
                // TypeDescriptor.pVFTable is a relocated pointer.
                if (haveRelocs && !m_relocs.contains(tdRva))
                    continue;

                std::string name;
                for (size_t k = i; k < v.size() && name.size() < 1024; ++k)
                {
                    const char c = static_cast<char>(v[k]);
                    if (c == '\0')
                        break;
                    if (c < 0x20 || c > 0x7E)
                    {
                        name.clear();
                        break;
                    }
                    name.push_back(c);
                }
                if (name.size() < 5)
                    continue;

                uint64_t vptr = 0;
                if (!m_pe.readPointer(tdRva, vptr))
                    continue;

                cands.push_back({tdRva, vptr, std::move(name)});
            }
        };

        for (const auto* sec : m_dataSections)
            scanSection(sec);

        if (cands.empty())
            return;

        // All TypeDescriptors share one pVFTable value: type_info's vftable.
        std::unordered_map<uint64_t, size_t> vptrVotes;
        for (const auto& c : cands)
            ++vptrVotes[c.vptr];
        uint64_t bestVptr = 0;
        size_t bestVotes = 0;
        for (const auto& [vptr, votes] : vptrVotes)
        {
            if (votes > bestVotes)
            {
                bestVotes = votes;
                bestVptr = vptr;
            }
        }
        if (bestVptr >= m_imageBase)
            m_typeInfoVftable = bestVptr - m_imageBase;

        std::unordered_set<uint64_t> withVftable;
        for (const auto& v : m_vftables)
            withVftable.insert(v.typeDescriptorRva);

        std::unordered_map<uint64_t, uint32_t> baseUses;
        for (const auto& v : m_vftables)
            for (const auto& b : v.bases)
                if (b.typeDescriptorRva != v.typeDescriptorRva)
                    ++baseUses[b.typeDescriptorRva];

        m_typeDescs.reserve(cands.size());
        for (auto& c : cands)
        {
            if (c.vptr != bestVptr)
                continue;

            TypeDescriptorInfo t;
            t.rva = c.rva;
            t.mangledName = c.mangled;

            const auto parsed = MsvcDemangler::parse(c.mangled);
            if (parsed.ok)
            {
                t.fullName = parsed.name;
                t.isStruct = parsed.kind == MsvcDemangler::Kind::Struct;
                t.isUnion = parsed.kind == MsvcDemangler::Kind::Union;
                t.isEnum = parsed.kind == MsvcDemangler::Kind::Enum;
            }
            else
            {
                t.fullName = c.mangled;
                t.demangleFailed = true;
            }

            t.hasVftable = withVftable.count(c.rva) != 0;
            const auto bu = baseUses.find(c.rva);
            t.usedAsBaseCount = bu == baseUses.end() ? 0 : bu->second;
            m_typeDescs.push_back(std::move(t));
        }

        std::sort(m_typeDescs.begin(),
            m_typeDescs.end(),
            [](const TypeDescriptorInfo& a, const TypeDescriptorInfo& b)
            { return a.fullName < b.fullName; });
    }

    // ─────────────────────────────────────────────────────────────────────
    // Step 7: pure-virtual stubs
    //
    // The slot of a pure virtual function does not hold zero: it holds the
    // address of a stub that aborts. A stub is recognised by its *body*
    // (statistics alone cannot tell it from an inherited no-op method): the
    // function references a message string containing "pure virtual", found
    // through a relocated pointer inside the function — this covers the CRT's
    // `_purecall` (R6025 text) when its strings are present and engine stubs
    // asserting `false && "Pure virtual function call!"`.
    //
    // Limitation: a stripped CRT whose `_purecall` carries no message cannot be
    // identified without symbols (the usual "push 19h" test was tried and gives
    // false positives in ordinary functions). Such slots stay regular virtuals.
    // A game may have several stubs (one per signature), so every recognised
    // target is flagged, not just the most common one.
    // ─────────────────────────────────────────────────────────────────────

    static constexpr size_t kStubScanBytes = 160;

    [[nodiscard]] bool bodyLooksLikePureStub(uint64_t a_funcRva) const
    {
        const BinaryView code = m_pe.bytesAtRva(a_funcRva, kStubScanBytes);
        if (code.size() < 8)
            return false;

        // A relocated pointer to a "pure virtual ..." message string.
        bool found = false;
        m_relocs.forEachInRange(a_funcRva,
            a_funcRva + code.size(),
            [&](uint64_t a_relocRva)
            {
                if (found)
                    return;
                uint64_t va = 0;
                if (!m_pe.readPointer(a_relocRva, va) || va < m_imageBase)
                    return;
                if (!isDataRva(va - m_imageBase))
                    return;

                std::string text = m_pe.readCString(va - m_imageBase, 96);
                for (char& c : text)
                    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                if (text.find("pure virtual") != std::string::npos
                    || text.find("purevirtual") != std::string::npos)
                    found = true;
            });
        return found;
    }

    void detectPureCall()
    {
        if (m_vftables.empty())
            return;

        // distinct target -> number of vftables containing it
        std::unordered_map<uint64_t, uint32_t> useCount;
        for (const auto& v : m_vftables)
        {
            std::unordered_set<uint64_t> seen;
            for (const auto& s : v.vfuncs)
                if (seen.insert(s.funcRva).second)
                    ++useCount[s.funcRva];
        }

        std::unordered_set<uint64_t> stubs;
        uint32_t bestUses = 0;
        for (const auto& [target, uses] : useCount)
        {
            if (!bodyLooksLikePureStub(target))
                continue;
            stubs.insert(target);
            if (uses > bestUses)
            {
                bestUses = uses;
                m_pureCallRva = target;
            }
        }

        if (stubs.empty())
            return;

        for (auto& v : m_vftables)
            for (auto& s : v.vfuncs)
                if (stubs.count(s.funcRva))
                {
                    s.isPureVirtual = true;
                    s.isDestructor = false;
                }

        m_pureStubs.assign(stubs.begin(), stubs.end());
        std::sort(m_pureStubs.begin(), m_pureStubs.end());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Lookup indexes
    // ─────────────────────────────────────────────────────────────────────

    /// Lower-case and drop spaces: "A<B<int> >" == "a<b<int>>".
    [[nodiscard]] static std::string normalize(const std::string& a_name)
    {
        std::string r;
        r.reserve(a_name.size());
        for (char c : a_name)
        {
            if (c == ' ')
                continue;
            r.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
        }
        return r;
    }

    void buildIndexes()
    {
        m_normFull.reserve(m_vftables.size());
        m_normLeaf.reserve(m_vftables.size());

        for (size_t i = 0; i < m_vftables.size(); ++i)
        {
            const auto& v = m_vftables[i];
            m_normFull.push_back(normalize(v.fullName));
            m_normLeaf.push_back(normalize(v.className));

            auto add = [&](const std::string& key)
            {
                auto& vec = m_byName[key];
                if (vec.empty() || vec.back() != i)
                    vec.push_back(i);
            };
            add(m_normFull.back());
            add(m_normLeaf.back());

            m_byVftableRva.emplace(v.vftableRva, i);
            m_byTypeDesc[v.typeDescriptorRva].push_back(i);
        }

        // Primary (offset 0) vftable first inside each class.
        for (auto& [td, idxs] : m_byTypeDesc)
        {
            std::stable_sort(idxs.begin(),
                idxs.end(),
                [&](size_t a, size_t b)
                { return m_vftables[a].objectOffset < m_vftables[b].objectOffset; });
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Members
    // ─────────────────────────────────────────────────────────────────────

    PEImage m_pe;
    PERelocations m_relocs;
    PEBitness m_bitness = PEBitness::Bits64;
    RttiLayout m_layout = RttiLayout::x64();
    uint64_t m_imageBase = 0;

    std::vector<const PESection*> m_dataSections;
    std::vector<const PESection*> m_codeSections;

    std::vector<VftableInfo> m_vftables;
    std::vector<TypeDescriptorInfo> m_typeDescs;

    std::unordered_map<std::string, std::vector<size_t>> m_byName;
    std::unordered_map<uint64_t, size_t> m_byVftableRva;
    std::unordered_map<uint64_t, std::vector<size_t>> m_byTypeDesc;
    std::vector<std::string> m_normFull;
    std::vector<std::string> m_normLeaf;

    uint64_t m_pureCallRva = 0;
    uint64_t m_typeInfoVftable = 0;
    std::vector<uint64_t> m_pureStubs;
};

} // namespace DumpPDB
