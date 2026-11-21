#pragma once

#include <Core/Binary/BinaryFile.hpp>
#include <Core/Binary/BinaryView.hpp>
#include <Core/PE/PESection.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace DumpPDB
{

/// Indices of the PE data directories (IMAGE_DIRECTORY_ENTRY_*).
enum PEDirectory : uint32_t
{
    PE_DIR_EXPORT = 0,
    PE_DIR_IMPORT = 1,
    PE_DIR_RESOURCE = 2,
    PE_DIR_EXCEPTION = 3,
    PE_DIR_SECURITY = 4, ///< Raw file offset (not an RVA)!
    PE_DIR_BASERELOC = 5,
    PE_DIR_DEBUG = 6,
    PE_DIR_ARCH = 7,
    PE_DIR_GLOBALPTR = 8,
    PE_DIR_TLS = 9,
    PE_DIR_LOAD_CONFIG = 10,
    PE_DIR_BOUND_IMPORT = 11,
    PE_DIR_IAT = 12,
    PE_DIR_DELAY_IMPORT = 13,
    PE_DIR_COM_DESCRIPTOR = 14,
};

struct PEDataDirectory
{
    uint32_t rva = 0; ///< RVA (file offset for PE_DIR_SECURITY).
    uint32_t size = 0;
};

/// One entry of the decoded "Rich" header (compiler/linker product ids).
struct PERichEntry
{
    uint16_t productId = 0;
    uint16_t buildNumber = 0;
    uint32_t count = 0;
};

/// Parses a PE/COFF image (exe/dll) and exposes its headers and sections.
/// Only reads the file — it does not load the image into memory.
///
/// All "rva" helpers use a single, consistent address space: for both PE32 and
/// PE32+ an RVA is an offset from the image base. A virtual address (VA) is
/// imageBase + rva.
class PEImage
{
public:

    PEImage() = default;

    /// Load and parse a PE file. Returns false if the file is not a valid PE.
    bool load(const std::wstring& a_path)
    {
        reset();

        if (!m_file.load(a_path))
        {
            return false;
        }

        return parse();
    }

    /// Load from an already-loaded BinaryFile.
    bool load(const BinaryFile& a_file)
    {
        reset();

        m_file = a_file;
        return parse();
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return m_isValid;
    }

    /// All PE sections in file order.
    [[nodiscard]] const std::vector<PESection>& sections() const noexcept
    {
        return m_sections;
    }

    /// Sections that are likely to contain meaningful string data.
    /// Excludes .reloc, .pdata, .debug, etc.
    [[nodiscard]] std::vector<const PESection*> stringSections() const noexcept
    {
        std::vector<const PESection*> out;
        for (const auto& s : m_sections)
        {
            if (s.isStringCandidate())
            {
                out.push_back(&s);
            }
        }
        return out;
    }

    /// Sections matching the given names (comma-separated, e.g. ".rdata,.data").
    /// Empty string means "all string-candidate sections".
    [[nodiscard]] std::vector<const PESection*> sectionsByName(
        const std::string& a_names) const noexcept
    {
        std::vector<const PESection*> out;

        if (a_names.empty())
        {
            return stringSections();
        }

        // Split by comma.
        std::vector<std::string> wanted;
        std::string cur;
        for (char c : a_names)
        {
            if (c == ',')
            {
                if (!cur.empty())
                {
                    wanted.push_back(cur);
                    cur.clear();
                }
            }
            else
            {
                cur += c;
            }
        }
        if (!cur.empty())
        {
            wanted.push_back(cur);
        }

        for (const auto& s : m_sections)
        {
            for (const auto& w : wanted)
            {
                if (s.name() == w)
                {
                    out.push_back(&s);
                    break;
                }
            }
        }

        return out;
    }

    /// The raw file view (whole file).
    [[nodiscard]] BinaryView view() const noexcept
    {
        return m_file.view();
    }

    /// Image base from the optional header.
    [[nodiscard]] uint64_t imageBase() const noexcept
    {
        return m_imageBase;
    }

    /// True if this is a 64-bit PE (PE32+).
    [[nodiscard]] bool is64Bit() const noexcept
    {
        return m_is64Bit;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Header fields
    // ─────────────────────────────────────────────────────────────────────

    [[nodiscard]] uint16_t machine() const noexcept { return m_machine; }
    [[nodiscard]] uint32_t timeDateStamp() const noexcept { return m_timeDateStamp; }
    [[nodiscard]] uint16_t fileCharacteristics() const noexcept { return m_fileCharacteristics; }
    [[nodiscard]] uint32_t entryPointRva() const noexcept { return m_entryPointRva; }
    [[nodiscard]] uint32_t sizeOfImage() const noexcept { return m_sizeOfImage; }
    [[nodiscard]] uint32_t sizeOfHeaders() const noexcept { return m_sizeOfHeaders; }
    [[nodiscard]] uint32_t checkSum() const noexcept { return m_checkSum; }
    [[nodiscard]] uint16_t subsystem() const noexcept { return m_subsystem; }
    [[nodiscard]] uint16_t dllCharacteristics() const noexcept { return m_dllCharacteristics; }
    [[nodiscard]] uint8_t linkerMajor() const noexcept { return m_linkerMajor; }
    [[nodiscard]] uint8_t linkerMinor() const noexcept { return m_linkerMinor; }
    [[nodiscard]] uint16_t osMajor() const noexcept { return m_osMajor; }
    [[nodiscard]] uint16_t osMinor() const noexcept { return m_osMinor; }
    [[nodiscard]] uint16_t subsystemMajor() const noexcept { return m_subsysMajor; }
    [[nodiscard]] uint16_t subsystemMinor() const noexcept { return m_subsysMinor; }
    [[nodiscard]] uint64_t stackReserve() const noexcept { return m_stackReserve; }
    [[nodiscard]] uint64_t heapReserve() const noexcept { return m_heapReserve; }
    [[nodiscard]] size_t peHeaderOffset() const noexcept { return m_peOffset; }
    [[nodiscard]] size_t fileSize() const noexcept { return m_file.size(); }

    [[nodiscard]] bool isDll() const noexcept { return (m_fileCharacteristics & 0x2000) != 0; }

    [[nodiscard]] const PEDataDirectory& directory(PEDirectory a_dir) const noexcept
    {
        static const PEDataDirectory kEmpty{};
        return a_dir < m_directories.size() ? m_directories[a_dir] : kEmpty;
    }

    [[nodiscard]] bool hasDirectory(PEDirectory a_dir) const noexcept
    {
        const auto& d = directory(a_dir);
        return d.rva != 0 && d.size != 0;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Address translation
    // ─────────────────────────────────────────────────────────────────────

    /// The section that owns a_rva (by virtual range), or nullptr.
    [[nodiscard]] const PESection* sectionForRva(uint64_t a_rva) const noexcept
    {
        for (const auto& s : m_sections)
        {
            const uint64_t span = std::max<uint64_t>(s.virtualSize(), s.size());
            if (a_rva >= s.virtualAddress() && a_rva < s.virtualAddress() + span)
            {
                return &s;
            }
        }
        return nullptr;
    }

    /// The section containing a_fileOffset (by raw range), or nullptr.
    [[nodiscard]] const PESection* sectionForOffset(uint64_t a_fileOffset) const noexcept
    {
        for (const auto& s : m_sections)
        {
            if (!s.empty() && a_fileOffset >= s.fileOffset()
                && a_fileOffset < s.fileOffset() + s.size())
            {
                return &s;
            }
        }
        return nullptr;
    }

    /// RVA -> file offset. Returns false when the RVA is outside every section
    /// or lies in the zero-initialised (not file-backed) tail of a section.
    [[nodiscard]] bool rvaToOffset(uint64_t a_rva, size_t& a_out) const noexcept
    {
        // Headers are mapped 1:1 below the first section.
        if (a_rva < m_firstSectionRva)
        {
            if (a_rva < m_file.size())
            {
                a_out = static_cast<size_t>(a_rva);
                return true;
            }
            return false;
        }

        const PESection* s = sectionForRva(a_rva);
        if (!s)
        {
            return false;
        }

        const uint64_t delta = a_rva - s->virtualAddress();
        if (delta >= s->size())
        {
            return false; // virtual-only (.bss-like) region
        }

        a_out = static_cast<size_t>(s->fileOffset() + delta);
        return true;
    }

    /// Legacy helper kept for the existing analyzers: returns 0 on failure.
    [[nodiscard]] size_t rvaToOff(uint64_t a_rva) const noexcept
    {
        size_t off = 0;
        return rvaToOffset(a_rva, off) ? off : 0;
    }

    /// file offset -> RVA. Returns false when the offset is in no section.
    [[nodiscard]] bool offsetToRva(uint64_t a_offset, uint64_t& a_out) const noexcept
    {
        const PESection* s = sectionForOffset(a_offset);
        if (!s)
        {
            return false;
        }
        a_out = s->virtualAddress() + (a_offset - s->fileOffset());
        return true;
    }

    /// True when the RVA lies inside a section's virtual range (file-backed or not).
    [[nodiscard]] bool isRvaMapped(uint64_t a_rva) const noexcept
    {
        return sectionForRva(a_rva) != nullptr;
    }

    /// VA -> RVA; false when the VA is below the image base or past its end.
    [[nodiscard]] bool vaToRva(uint64_t a_va, uint64_t& a_rva) const noexcept
    {
        if (a_va < m_imageBase)
        {
            return false;
        }
        a_rva = a_va - m_imageBase;
        return a_rva < m_sizeOfImage;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Typed reads at an RVA (bounds-checked against the owning section)
    // ─────────────────────────────────────────────────────────────────────

    template <typename T>
    [[nodiscard]] bool readAtRva(uint64_t a_rva, T& a_out) const noexcept
    {
        const BinaryView bytes = bytesAtRva(a_rva, sizeof(T));
        return bytes.size() >= sizeof(T) && bytes.readLE<T>(0, a_out);
    }

    [[nodiscard]] bool readU32(uint64_t a_rva, uint32_t& a_out) const noexcept
    {
        return readAtRva<uint32_t>(a_rva, a_out);
    }

    [[nodiscard]] bool readU16(uint64_t a_rva, uint16_t& a_out) const noexcept
    {
        return readAtRva<uint16_t>(a_rva, a_out);
    }

    [[nodiscard]] bool readU8(uint64_t a_rva, uint8_t& a_out) const noexcept
    {
        return readAtRva<uint8_t>(a_rva, a_out);
    }

    /// Native-width pointer (4 bytes on PE32, 8 on PE32+) at a_rva.
    [[nodiscard]] bool readPointer(uint64_t a_rva, uint64_t& a_out) const noexcept
    {
        if (m_is64Bit)
        {
            return readAtRva<uint64_t>(a_rva, a_out);
        }
        uint32_t v = 0;
        if (!readAtRva<uint32_t>(a_rva, v))
        {
            return false;
        }
        a_out = v;
        return true;
    }

    /// NUL-terminated ASCII string at a_rva (capped at a_maxLen). Empty on failure.
    [[nodiscard]] std::string readCString(uint64_t a_rva, size_t a_maxLen = 1024) const
    {
        std::string out;
        const BinaryView bytes = bytesAtRva(a_rva, a_maxLen);
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            const char c = static_cast<char>(bytes[i]);
            if (c == '\0')
            {
                break;
            }
            out.push_back(c);
        }
        return out;
    }

    /// Raw bytes at an RVA, clamped to the file-backed part of the owning
    /// section (so a read never leaks into the next section's data).
    [[nodiscard]] BinaryView bytesAtRva(uint64_t a_rva, size_t a_len) const noexcept
    {
        size_t off = 0;
        if (!rvaToOffset(a_rva, off))
        {
            return {};
        }

        size_t avail = m_file.size() - off;
        if (a_rva >= m_firstSectionRva)
        {
            if (const PESection* s = sectionForRva(a_rva))
            {
                avail = static_cast<size_t>(s->size() - (a_rva - s->virtualAddress()));
            }
        }
        return m_file.view().subview(off, std::min(a_len, avail));
    }

    // ─────────────────────────────────────────────────────────────────────
    // Convenience section lookups
    // ─────────────────────────────────────────────────────────────────────

    [[nodiscard]] const PESection* findSection(const std::string& a_name) const noexcept
    {
        for (const auto& s : m_sections)
        {
            if (s.name() == a_name)
            {
                return &s;
            }
        }
        return nullptr;
    }

    /// First executable section, preferring one literally named ".text".
    [[nodiscard]] const PESection* codeSection() const noexcept
    {
        if (const auto* t = findSection(".text"))
        {
            return t;
        }
        for (const auto& s : m_sections)
        {
            if (s.isExecutable() || s.hasCode())
            {
                return &s;
            }
        }
        return nullptr;
    }

    /// True when a_rva lies in any section flagged executable / code.
    [[nodiscard]] bool isCodeRva(uint64_t a_rva) const noexcept
    {
        const PESection* s = sectionForRva(a_rva);
        return s && (s->isExecutable() || s->hasCode());
    }

    /// Decoded Rich header (empty when the linker did not emit one).
    [[nodiscard]] const std::vector<PERichEntry>& richHeader() const noexcept
    {
        return m_rich;
    }

    /// Data appended after the last section (certificates, installers, ...).
    [[nodiscard]] uint64_t overlayOffset() const noexcept
    {
        return m_overlayOffset;
    }
    [[nodiscard]] uint64_t overlaySize() const noexcept
    {
        return m_overlaySize;
    }

private:

    void reset()
    {
        m_sections.clear();
        m_directories.clear();
        m_rich.clear();
        m_isValid = false;
        m_is64Bit = false;
        m_imageBase = 0;
        m_overlayOffset = 0;
        m_overlaySize = 0;
        m_firstSectionRva = 0;
    }

    bool parse()
    {
        const BinaryView view = m_file.view();
        if (view.size() < 0x40)
        {
            return false;
        }

        // DOS header: e_magic = 'MZ'
        if (view[0] != 'M' || view[1] != 'Z')
        {
            return false;
        }

        // e_lfanew at offset 0x3C
        uint32_t peOffset = 0;
        if (!view.readLE<uint32_t>(0x3C, peOffset))
        {
            return false;
        }

        if (static_cast<uint64_t>(peOffset) + 0x18 > view.size())
        {
            return false;
        }

        // PE signature "PE\0\0"
        if (view[peOffset] != 'P' || view[peOffset + 1] != 'E' || view[peOffset + 2] != 0
            || view[peOffset + 3] != 0)
        {
            return false;
        }
        m_peOffset = peOffset;

        // COFF header
        const size_t coff = static_cast<size_t>(peOffset) + 4;
        uint16_t numSections = 0;
        uint16_t sizeOfOptionalHeader = 0;
        if (!view.readLE<uint16_t>(coff, m_machine)
            || !view.readLE<uint16_t>(coff + 2, numSections)
            || !view.readLE<uint32_t>(coff + 4, m_timeDateStamp)
            || !view.readLE<uint16_t>(coff + 16, sizeOfOptionalHeader)
            || !view.readLE<uint16_t>(coff + 18, m_fileCharacteristics))
        {
            return false;
        }

        // Optional header
        const size_t opt = coff + 20;
        if (opt + sizeOfOptionalHeader > view.size())
        {
            return false;
        }

        uint16_t magic = 0;
        if (!view.readLE<uint16_t>(opt, magic))
        {
            return false;
        }

        m_linkerMajor = view[opt + 2];
        m_linkerMinor = view[opt + 3];
        (void)view.readLE<uint32_t>(opt + 16, m_entryPointRva);

        size_t dirOffset = 0;
        uint32_t numDirs = 0;

        if (magic == 0x10B) // PE32
        {
            m_is64Bit = false;
            uint32_t base = 0;
            if (!view.readLE<uint32_t>(opt + 28, base))
            {
                return false;
            }
            m_imageBase = base;

            uint32_t stackRes = 0;
            uint32_t heapRes = 0;
            (void)view.readLE<uint32_t>(opt + 72, stackRes);
            (void)view.readLE<uint32_t>(opt + 80, heapRes);
            m_stackReserve = stackRes;
            m_heapReserve = heapRes;

            (void)view.readLE<uint32_t>(opt + 92, numDirs);
            dirOffset = opt + 96;
        }
        else if (magic == 0x20B) // PE32+
        {
            m_is64Bit = true;
            uint64_t base = 0;
            if (!view.readLE<uint64_t>(opt + 24, base))
            {
                return false;
            }
            m_imageBase = base;

            (void)view.readLE<uint64_t>(opt + 72, m_stackReserve);
            (void)view.readLE<uint64_t>(opt + 88, m_heapReserve);

            (void)view.readLE<uint32_t>(opt + 108, numDirs);
            dirOffset = opt + 112;
        }
        else
        {
            return false;
        }

        // Fields shared by both layouts (same offsets in PE32 and PE32+).
        (void)view.readLE<uint16_t>(opt + 40, m_osMajor);
        (void)view.readLE<uint16_t>(opt + 42, m_osMinor);
        (void)view.readLE<uint16_t>(opt + 48, m_subsysMajor);
        (void)view.readLE<uint16_t>(opt + 50, m_subsysMinor);
        (void)view.readLE<uint32_t>(opt + 56, m_sizeOfImage);
        (void)view.readLE<uint32_t>(opt + 60, m_sizeOfHeaders);
        (void)view.readLE<uint32_t>(opt + 64, m_checkSum);
        (void)view.readLE<uint16_t>(opt + 68, m_subsystem);
        (void)view.readLE<uint16_t>(opt + 70, m_dllCharacteristics);

        // Data directories
        numDirs = std::min<uint32_t>(numDirs, 16);
        m_directories.assign(16, PEDataDirectory{});
        for (uint32_t i = 0; i < numDirs; ++i)
        {
            const size_t o = dirOffset + static_cast<size_t>(i) * 8;
            if (o + 8 > opt + sizeOfOptionalHeader)
            {
                break;
            }
            (void)view.readLE<uint32_t>(o, m_directories[i].rva);
            (void)view.readLE<uint32_t>(o + 4, m_directories[i].size);
        }

        // Section table
        const size_t sectionTable = opt + sizeOfOptionalHeader;
        const size_t sectionEntrySize = 40; // IMAGE_SECTION_HEADER

        if (sectionTable + static_cast<size_t>(numSections) * sectionEntrySize > view.size())
        {
            return false;
        }

        m_sections.reserve(numSections);

        uint64_t endOfRawData = m_sizeOfHeaders;
        m_firstSectionRva = 0xFFFFFFFFu;

        for (uint16_t i = 0; i < numSections; ++i)
        {
            const size_t off = sectionTable + static_cast<size_t>(i) * sectionEntrySize;

            // Name: 8 bytes, null-terminated
            char nameBuf[9] = {};
            for (int k = 0; k < 8; ++k)
            {
                nameBuf[k] = static_cast<char>(view[off + k]);
            }
            std::string name(nameBuf);

            uint32_t virtualSize = 0;
            uint32_t virtualAddress = 0;
            uint32_t sizeOfRawData = 0;
            uint32_t pointerToRawData = 0;
            uint32_t characteristics = 0;

            if (!view.readLE<uint32_t>(off + 8, virtualSize)
                || !view.readLE<uint32_t>(off + 12, virtualAddress)
                || !view.readLE<uint32_t>(off + 16, sizeOfRawData)
                || !view.readLE<uint32_t>(off + 20, pointerToRawData)
                || !view.readLE<uint32_t>(off + 36, characteristics))
            {
                return false;
            }

            // Sections without raw data (pure .bss) are still real sections.
            BinaryView sectionView;
            if (sizeOfRawData != 0 && pointerToRawData < view.size())
            {
                const size_t rawSize
                    = std::min<size_t>(sizeOfRawData, view.size() - pointerToRawData);
                sectionView = view.subview(pointerToRawData, rawSize);
                endOfRawData = std::max<uint64_t>(endOfRawData, pointerToRawData + rawSize);
            }

            m_firstSectionRva = std::min<uint64_t>(m_firstSectionRva, virtualAddress);

            m_sections.emplace_back(std::move(name),
                sectionView,
                pointerToRawData,
                virtualAddress,
                virtualSize,
                characteristics);
        }

        if (m_sections.empty())
        {
            m_firstSectionRva = 0;
        }

        if (m_sizeOfImage == 0 && !m_sections.empty())
        {
            const auto& last = m_sections.back();
            m_sizeOfImage = last.virtualAddress()
                            + std::max(last.virtualSize(), static_cast<uint32_t>(last.size()));
        }

        if (endOfRawData < view.size())
        {
            m_overlayOffset = endOfRawData;
            m_overlaySize = view.size() - endOfRawData;
        }

        parseRichHeader(view, peOffset);

        m_isValid = true;
        return true;
    }

    /// Decode the XOR-masked "Rich" header sitting between the DOS stub and the
    /// PE header. Silently leaves m_rich empty when it is absent or corrupt.
    void parseRichHeader(const BinaryView& a_view, size_t a_peOffset)
    {
        const size_t limit = std::min<size_t>(a_peOffset, a_view.size());
        if (limit < 0x60)
        {
            return;
        }

        size_t richPos = 0;
        bool found = false;
        for (size_t i = 0x40; i + 8 <= limit; i += 4)
        {
            uint32_t v = 0;
            if (a_view.readLE<uint32_t>(i, v) && v == 0x68636952u) // "Rich"
            {
                richPos = i;
                found = true;
                break;
            }
        }
        if (!found)
        {
            return;
        }

        uint32_t key = 0;
        if (!a_view.readLE<uint32_t>(richPos + 4, key))
        {
            return;
        }

        // Walk backwards until the XOR-ed "DanS" marker.
        size_t start = 0;
        bool haveStart = false;
        for (size_t i = richPos; i >= 0x44; i -= 4)
        {
            uint32_t v = 0;
            if (!a_view.readLE<uint32_t>(i - 4, v))
            {
                return;
            }
            if ((v ^ key) == 0x536E6144u) // "DanS"
            {
                start = i - 4;
                haveStart = true;
                break;
            }
        }
        if (!haveStart)
        {
            return;
        }

        // 3 padding dwords follow "DanS", then {compId, count} pairs.
        for (size_t p = start + 16; p + 8 <= richPos; p += 8)
        {
            uint32_t compId = 0;
            uint32_t count = 0;
            if (!a_view.readLE<uint32_t>(p, compId) || !a_view.readLE<uint32_t>(p + 4, count))
            {
                break;
            }
            compId ^= key;
            count ^= key;

            PERichEntry e;
            e.productId = static_cast<uint16_t>(compId >> 16);
            e.buildNumber = static_cast<uint16_t>(compId & 0xFFFF);
            e.count = count;
            m_rich.push_back(e);
        }
    }

    BinaryFile m_file;
    std::vector<PESection> m_sections;
    std::vector<PEDataDirectory> m_directories;
    std::vector<PERichEntry> m_rich;

    uint64_t m_imageBase = 0;
    uint64_t m_stackReserve = 0;
    uint64_t m_heapReserve = 0;
    uint64_t m_overlayOffset = 0;
    uint64_t m_overlaySize = 0;
    uint64_t m_firstSectionRva = 0;
    size_t m_peOffset = 0;

    uint32_t m_timeDateStamp = 0;
    uint32_t m_entryPointRva = 0;
    uint32_t m_sizeOfImage = 0;
    uint32_t m_sizeOfHeaders = 0;
    uint32_t m_checkSum = 0;

    uint16_t m_machine = 0;
    uint16_t m_fileCharacteristics = 0;
    uint16_t m_subsystem = 0;
    uint16_t m_dllCharacteristics = 0;
    uint16_t m_osMajor = 0;
    uint16_t m_osMinor = 0;
    uint16_t m_subsysMajor = 0;
    uint16_t m_subsysMinor = 0;
    uint8_t m_linkerMajor = 0;
    uint8_t m_linkerMinor = 0;

    bool m_is64Bit = false;
    bool m_isValid = false;
};
} // namespace DumpPDB
