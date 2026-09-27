#pragma once

#include <Core/PE/PEImage.hpp>
#include <Core/RTTI/RttiTypes.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace DumpPDB
{

/// ============================================================================
/// RttiReader — dual-mode (x86 / x64) MSVC RTTI parser
///
/// x64 algorithm:
///   1. Scan .rdata for ColOnDisk with signature==1 and selfRva==&col.
///   2. Each COL stores RVAs (relative to imageBase) for TypeDescriptor and CHD.
///   3. Locate vftable: scan .rdata for 8-byte abs pointer == imageBase+colRva.
///      The vftable starts at offset+8 (right after that pointer).
///
/// x86 algorithm:
///   1. Scan .rdata for ColOnDisk with signature==0.
///      Validate by checking typeDescriptorAddr and classDescriptorAddr are
///      plausible absolute VAs (>= imageBase, within image).
///   2. Each COL stores 4-byte absolute VAs (imageBase+rva) for TD and CHD.
///   3. Locate vftable: scan .rdata for 4-byte abs pointer == absAddr(col).
///      The vftable starts right after that pointer.
///
/// In both cases the name is read from TypeDescriptor at a fixed byte offset
/// that differs between x86 (+8) and x64 (+16), as given by RttiLayout.
/// ============================================================================
class RttiReader
{
public:

    RttiReader() = default;

    /// Load and parse a PE file. Detects bitness automatically from the PE header.
    bool load(const std::wstring& a_path)
    {
        m_vftables.clear();

        if (!m_pe.load(a_path))
            return false;
        if (!m_pe.isValid())
            return false;

        m_bitness   = m_pe.is64Bit() ? PEBitness::Bits64 : PEBitness::Bits32;
        m_layout    = RttiLayout::forBitness(m_bitness);
        m_imageBase = m_pe.imageBase();

        findSections();
        if (!m_rdata || !m_text)
            return false;

        scanForCols();
        return true;
    }

    [[nodiscard]] const std::vector<VftableInfo>& vftables() const noexcept { return m_vftables; }
    [[nodiscard]] PEBitness bitness() const noexcept { return m_bitness; }
    [[nodiscard]] const PEImage& pe() const noexcept { return m_pe; }

    /// Find vftables by exact demangled name (case-insensitive).
    [[nodiscard]] std::vector<const VftableInfo*> findExact(const std::string& a_name) const
    {
        std::string lower = toLower(a_name);
        std::vector<const VftableInfo*> result;
        for (const auto& v : m_vftables)
        {
            if (toLower(v.fullName) == lower || toLower(v.className) == lower)
                result.push_back(&v);
        }
        return result;
    }

    /// Find vftables whose name contains a_substr (case-insensitive substring match).
    [[nodiscard]] std::vector<const VftableInfo*> findByName(const std::string& a_name) const
    {
        std::string lower = toLower(a_name);
        std::vector<const VftableInfo*> result;
        for (const auto& v : m_vftables)
        {
            if (toLower(v.fullName).find(lower) != std::string::npos
                || toLower(v.className).find(lower) != std::string::npos)
            {
                result.push_back(&v);
            }
        }
        return result;
    }

    /// Unique type names, alphabetically sorted (deduped — MI types appear once).
    [[nodiscard]] std::vector<std::string> allTypeNames() const
    {
        std::vector<std::string> names;
        for (const auto& v : m_vftables)
        {
            bool found = false;
            for (const auto& n : names)
                if (n == v.fullName) { found = true; break; }
            if (!found) names.push_back(v.fullName);
        }
        return names;
    }

private:

    // ─────────────────────────────────────────────────────────────────────────
    // Section discovery
    // ─────────────────────────────────────────────────────────────────────────

    void findSections()
    {
        m_rdata = nullptr;
        m_text  = nullptr;
        m_data  = nullptr;
        for (const auto& s : m_pe.sections())
        {
            if (s.name() == ".rdata") m_rdata = &s;
            else if (s.name() == ".text")  m_text  = &s;
            else if (s.name() == ".data")  m_data  = &s;
        }
        // Fallback for merged sections (e.g. some packed exes): pick first exec + first data
        if (!m_text)
            for (const auto& s : m_pe.sections())
                if (s.isExecutable()) { m_text = &s; break; }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Address helpers — unify x86 abs-VA vs x64 RVA into a common "rva" space
    // ─────────────────────────────────────────────────────────────────────────

    /// Convert a raw 32-bit value from the PE image to an RVA.
    /// x64: the value IS an RVA already.
    /// x86: the value is an absolute VA; subtract imageBase.
    [[nodiscard]] uint64_t toRva(uint32_t a_raw) const noexcept
    {
        if (m_bitness == PEBitness::Bits32)
        {
            // abs VA -> RVA (guard against underflow)
            if (static_cast<uint64_t>(a_raw) < m_imageBase) return 0;
            return static_cast<uint64_t>(a_raw) - m_imageBase;
        }
        return static_cast<uint64_t>(a_raw);
    }

    /// RVA -> file offset. Returns 0 on failure.
    [[nodiscard]] size_t rvaToOff(uint64_t a_rva) const noexcept
    {
        for (const auto& s : m_pe.sections())
        {
            if (a_rva >= s.virtualAddress()
                && a_rva < s.virtualAddress() + s.virtualSize())
            {
                return static_cast<size_t>(s.fileOffset() + (a_rva - s.virtualAddress()));
            }
        }
        return 0;
    }

    [[nodiscard]] bool rvaInSection(uint64_t a_rva, const PESection& a_sec) const noexcept
    {
        return a_rva >= a_sec.virtualAddress()
            && a_rva < a_sec.virtualAddress() + a_sec.virtualSize();
    }

    [[nodiscard]] bool rvaIsValid(uint64_t a_rva) const noexcept
    {
        for (const auto& s : m_pe.sections())
            if (a_rva >= s.virtualAddress() && a_rva < s.virtualAddress() + s.virtualSize())
                return true;
        return false;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Main COL scan
    // ─────────────────────────────────────────────────────────────────────────

    void scanForCols()
    {
        const BinaryView& view    = m_rdata->view();
        const uint32_t    rdataVa = m_rdata->virtualAddress();
        const size_t      colSize = m_layout.colSize;

        if (view.size() < colSize) return;

        const size_t count = view.size() - colSize;

        for (size_t i = 0; i <= count; i += 4)
        {
            uint32_t sig = 0;
            if (!view.readLE<uint32_t>(i, sig)) continue;
            if (sig != m_layout.colSignature) continue;

            uint32_t offset   = 0;
            uint32_t cdOffset = 0;
            uint32_t tdRaw    = 0;
            uint32_t chdRaw   = 0;

            if (!view.readLE<uint32_t>(i + 4,  offset))   continue;
            if (!view.readLE<uint32_t>(i + 8,  cdOffset)) continue;
            if (!view.readLE<uint32_t>(i + 12, tdRaw))    continue;
            if (!view.readLE<uint32_t>(i + 16, chdRaw))   continue;

            const uint64_t tdRva  = toRva(tdRaw);
            const uint64_t chdRva = toRva(chdRaw);

            // Validate pointers point somewhere real
            if (!rvaIsValid(tdRva) || !rvaIsValid(chdRva)) continue;

            // x64 extra validation: selfRva
            uint32_t selfRaw = 0;
            if (m_layout.colHasSelfRva)
            {
                if (!view.readLE<uint32_t>(i + 20, selfRaw)) continue;
                const uint32_t expectedRva = rdataVa + static_cast<uint32_t>(i);
                if (selfRaw != expectedRva) continue;
            }

            const uint64_t colRva = rdataVa + static_cast<uint64_t>(i);
            processCol(colRva, offset, tdRva, chdRva);
        }

        // Sort: fullName first, then objectOffset for MI
        std::sort(m_vftables.begin(), m_vftables.end(),
            [](const VftableInfo& a, const VftableInfo& b)
            {
                if (a.fullName != b.fullName) return a.fullName < b.fullName;
                return a.objectOffset < b.objectOffset;
            });
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Process one validated COL
    // ─────────────────────────────────────────────────────────────────────────

    void processCol(uint64_t a_colRva,
                    uint32_t a_objectOffset,
                    uint64_t a_tdRva,
                    uint64_t a_chdRva)
    {
        // 1. Read TypeDescriptor name
        const size_t tdOff = rvaToOff(a_tdRva);
        if (!tdOff) return;

        const BinaryView fv         = m_pe.view();
        const size_t     nameOffset = m_layout.typeDescNameOffset;

        if (tdOff + nameOffset >= fv.size()) return;

        std::string mangledName;
        for (size_t k = tdOff + nameOffset; k < fv.size(); ++k)
        {
            char c = static_cast<char>(fv[k]);
            if (c == '\0') break;
            mangledName.push_back(c);
            if (mangledName.size() > 512) break;
        }
        if (mangledName.empty()) return;

        // 2. Demangle
        std::string fullName = demangleMsvc(mangledName);
        if (fullName.empty()) return;

        std::string className = extractClassName(fullName);

        // 3. Find vftable in .rdata by scanning for pointer-to-COL
        uint64_t vftableRva = findVftableForCol(a_colRva);
        if (!vftableRva) return;

        // 4. Read vftable slots
        auto vfuncs = readVftableSlots(vftableRva);

        // 5. Read hierarchy
        InheritanceKind inheritance = InheritanceKind::None;
        auto bases = readHierarchy(a_chdRva, inheritance);

        // 6. Store
        VftableInfo info;
        info.vftableRva   = vftableRva;
        info.colRva       = a_colRva;
        info.objectOffset = a_objectOffset;
        info.mangledName  = mangledName;
        info.className    = className;
        info.fullName     = fullName;
        info.inheritance  = inheritance;
        info.bases        = std::move(bases);
        info.vfuncs       = std::move(vfuncs);

        m_vftables.push_back(std::move(info));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Locate vftable pointer
    //
    // The layout in .rdata immediately before a vftable is:
    //   x64: [8-byte abs VA of COL] [vftable slot 0] [slot 1] ...
    //   x86: [4-byte abs VA of COL] [vftable slot 0] [slot 1] ...
    //
    // We scan .rdata for the COL's absolute virtual address.
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] uint64_t findVftableForCol(uint64_t a_colRva) const
    {
        const uint64_t ptrSize = m_layout.ptrSize;
        const uint64_t colAbsVa = m_imageBase + a_colRva;

        const BinaryView& rv      = m_rdata->view();
        const uint32_t    rdataVa = m_rdata->virtualAddress();

        if (ptrSize == 8)
        {
            // x64: look for 8-byte pointer == colAbsVa
            const size_t sz = (rv.size() >= 8) ? rv.size() - 8 : 0;
            for (size_t j = 0; j <= sz; j += 8)
            {
                uint64_t candidate = 0;
                if (!rv.readLE<uint64_t>(j, candidate)) continue;
                if (candidate != colAbsVa) continue;
                // vftable starts at j + 8
                return rdataVa + static_cast<uint32_t>(j + 8);
            }
        }
        else
        {
            // x86: look for 4-byte pointer == colAbsVa (colAbsVa fits in 32 bits)
            const uint32_t colVa32 = static_cast<uint32_t>(colAbsVa);
            const size_t sz = (rv.size() >= 4) ? rv.size() - 4 : 0;
            for (size_t j = 0; j <= sz; j += 4)
            {
                uint32_t candidate = 0;
                if (!rv.readLE<uint32_t>(j, candidate)) continue;
                if (candidate != colVa32) continue;
                // vftable starts at j + 4
                return rdataVa + static_cast<uint32_t>(j + 4);
            }
        }
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // vftable slot reading
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] std::vector<VfuncSlot> readVftableSlots(uint64_t a_vftRva) const
    {
        std::vector<VfuncSlot> slots;
        if (!m_text) return slots;

        const uint64_t textStart = m_text->virtualAddress();
        const uint64_t textEnd   = textStart + m_text->virtualSize();
        const uint64_t ptrSize   = m_layout.ptrSize;

        for (uint32_t idx = 0; idx < 512; ++idx)
        {
            const uint64_t slotRva = a_vftRva + static_cast<uint64_t>(idx) * ptrSize;
            const size_t   slotOff = rvaToOff(slotRva);
            if (!slotOff) break;

            const BinaryView fv = m_pe.view();

            uint64_t funcVa = 0;
            if (ptrSize == 8)
            {
                if (!fv.readLE<uint64_t>(slotOff, funcVa)) break;
            }
            else
            {
                uint32_t v32 = 0;
                if (!fv.readLE<uint32_t>(slotOff, v32)) break;
                funcVa = static_cast<uint64_t>(v32);
            }

            if (funcVa == 0)
            {
                VfuncSlot s;
                s.slotIndex     = idx;
                s.funcRva       = 0;
                s.isPureVirtual = true;
                s.isDestructor  = false;
                slots.push_back(s);
                continue;
            }

            if (funcVa < m_imageBase) break;
            const uint64_t funcRva = funcVa - m_imageBase;
            if (funcRva < textStart || funcRva >= textEnd) break;

            VfuncSlot s;
            s.slotIndex     = idx;
            s.funcRva       = funcRva;
            s.isPureVirtual = false;
            s.isDestructor  = (idx == 0 || idx == 1);
            slots.push_back(s);
        }

        return slots;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // CHD/BCD hierarchy reading
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] std::vector<BaseClassInfo> readHierarchy(uint64_t a_chdRva,
                                                            InheritanceKind& a_kind) const
    {
        std::vector<BaseClassInfo> bases;
        a_kind = InheritanceKind::None;

        const size_t     chdOff = rvaToOff(a_chdRva);
        if (!chdOff) return bases;

        const BinaryView fv = m_pe.view();

        ChdOnDisk chd{};
        if (!fv.readLE<uint32_t>(chdOff,      chd.signature))         return bases;
        if (!fv.readLE<uint32_t>(chdOff + 4,  chd.attributes))        return bases;
        if (!fv.readLE<uint32_t>(chdOff + 8,  chd.numBaseClasses))    return bases;
        if (!fv.readLE<uint32_t>(chdOff + 12, chd.baseClassArrayAddr)) return bases;

        const bool isMultiple = (chd.attributes & 1) != 0;
        const bool isVirtual  = (chd.attributes & 2) != 0;
        if (isVirtual)       a_kind = InheritanceKind::Virtual;
        else if (isMultiple) a_kind = InheritanceKind::Multiple;
        else if (chd.numBaseClasses > 1) a_kind = InheritanceKind::Single;

        if (chd.numBaseClasses == 0 || chd.numBaseClasses > 256) return bases;

        const uint64_t arrayRva = toRva(chd.baseClassArrayAddr);
        const size_t   arrayOff = rvaToOff(arrayRva);
        if (!arrayOff) return bases;

        bases.reserve(chd.numBaseClasses);

        // x86 BCD array: 4-byte abs VA per entry
        // x64 BCD array: 4-byte RVA per entry
        for (uint32_t i = 0; i < chd.numBaseClasses; ++i)
        {
            uint32_t bcdRaw = 0;
            if (!fv.readLE<uint32_t>(arrayOff + i * 4, bcdRaw)) break;

            const uint64_t bcdRva = toRva(bcdRaw);
            const size_t   bcdOff = rvaToOff(bcdRva);
            if (!bcdOff) continue;

            BcdOnDisk bcd{};
            if (!fv.readLE<uint32_t>(bcdOff,      bcd.typeDescriptorAddr)) continue;
            if (!fv.readLE<uint32_t>(bcdOff + 4,  bcd.numContainedBases))  continue;
            if (!fv.readLE<int32_t> (bcdOff + 8,  bcd.pmd.mdisp))          continue;
            if (!fv.readLE<int32_t> (bcdOff + 12, bcd.pmd.pdisp))          continue;
            if (!fv.readLE<int32_t> (bcdOff + 16, bcd.pmd.vdisp))          continue;
            if (!fv.readLE<uint32_t>(bcdOff + 20, bcd.attributes))          continue;

            const uint64_t btdRva = toRva(bcd.typeDescriptorAddr);
            const size_t   btdOff = rvaToOff(btdRva);
            if (!btdOff) continue;

            if (btdOff + m_layout.typeDescNameOffset >= fv.size()) continue;

            std::string mangledBase;
            for (size_t k = btdOff + m_layout.typeDescNameOffset; k < fv.size(); ++k)
            {
                char c = static_cast<char>(fv[k]);
                if (c == '\0') break;
                mangledBase.push_back(c);
                if (mangledBase.size() > 512) break;
            }
            if (mangledBase.empty()) continue;

            BaseClassInfo bc;
            bc.mangledName   = mangledBase;
            bc.demangledName = demangleMsvc(mangledBase);
            bc.offset        = bcd.pmd.mdisp;
            bc.isVirtual     = (bcd.attributes & 1) != 0;
            bc.isDirect      = (i == 1); // index 0 = self, index 1 = first direct base

            bases.push_back(std::move(bc));
        }

        return bases;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // MSVC name demangling (no DbgHelp / UnDecorateSymbolName dependency)
    //
    // Handles:
    //   ".?AVFoo@@"          -> "Foo"          (class)
    //   ".?AUMyStruct@@"     -> "MyStruct"     (struct)
    //   ".?AVFoo@Bar@@"      -> "Bar::Foo"     (nested)
    //   ".?AVFoo@NS1@NS2@@"  -> "NS2::NS1::Foo"
    // ─────────────────────────────────────────────────────────────────────────

    static std::string demangleMsvc(const std::string& a_mangled)
    {
        // Must start with ".?A" followed by a tag char
        if (a_mangled.size() < 5) return {};
        if (a_mangled[0] != '.' || a_mangled[1] != '?' || a_mangled[2] != 'A')
            return {};

        const char tag = a_mangled[3];
        if (tag != 'V' && tag != 'U' && tag != 'W' && tag != 'T' && tag != 'A')
            return {};

        std::string inner = a_mangled.substr(4);

        // Strip trailing "@@"
        if (inner.size() >= 2 && inner.substr(inner.size() - 2) == "@@")
            inner = inner.substr(0, inner.size() - 2);
        if (inner.empty()) return {};

        // Split by '@' and reverse: [className, ns1, ns2] -> "ns2::ns1::className"
        std::vector<std::string> parts;
        std::string cur;
        for (char c : inner)
        {
            if (c == '@')
            {
                if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
            }
            else
            {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) parts.push_back(cur);
        if (parts.empty()) return {};

        std::string result;
        for (int i = static_cast<int>(parts.size()) - 1; i >= 0; --i)
        {
            if (!result.empty()) result += "::";
            result += parts[i];
        }
        return result;
    }

    static std::string extractClassName(const std::string& a_fullName)
    {
        const auto pos = a_fullName.rfind("::");
        return (pos == std::string::npos) ? a_fullName : a_fullName.substr(pos + 2);
    }

    static std::string toLower(const std::string& s)
    {
        std::string r = s;
        for (char& c : r) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        return r;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Members
    // ─────────────────────────────────────────────────────────────────────────

    PEImage             m_pe;
    PEBitness           m_bitness   = PEBitness::Bits64;
    RttiLayout          m_layout    = RttiLayout::x64();
    uint64_t            m_imageBase = 0;

    const PESection*    m_rdata     = nullptr;
    const PESection*    m_text      = nullptr;
    const PESection*    m_data      = nullptr;

    std::vector<VftableInfo> m_vftables;
};

} // namespace DumpPDB
