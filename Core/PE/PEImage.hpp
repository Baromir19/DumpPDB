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
    /// Parses a PE/COFF image (exe/dll) and exposes its sections.
    /// Only reads the file — it does not load the image into memory.
    class PEImage
    {
    public:
        PEImage() = default;

        /// Load and parse a PE file. Returns false if the file is not a valid PE.
        bool load(const std::wstring& a_path)
        {
            m_sections.clear();
            m_isValid = false;

            if (!m_file.load(a_path))
            {
                return false;
            }

            return parse();
        }

        /// Load from an already-loaded BinaryFile.
        bool load(const BinaryFile& a_file)
        {
            m_sections.clear();
            m_isValid = false;

            m_file = a_file;
            return parse();
        }

        [[nodiscard]] bool isValid() const noexcept { return m_isValid; }

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
        [[nodiscard]] BinaryView view() const noexcept { return m_file.view(); }

        /// Image base from the optional header.
        [[nodiscard]] uint64_t imageBase() const noexcept { return m_imageBase; }

        /// True if this is a 64-bit PE (PE32+).
        [[nodiscard]] bool is64Bit() const noexcept { return m_is64Bit; }

    private:
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

            if (peOffset + 0x18 > view.size())
            {
                return false;
            }

            // PE signature "PE\0\0"
            if (view[peOffset] != 'P' || view[peOffset + 1] != 'E'
                || view[peOffset + 2] != 0 || view[peOffset + 3] != 0)
            {
                return false;
            }

            // COFF header
            const size_t coff = peOffset + 4;
            uint16_t numSections = 0;
            uint16_t sizeOfOptionalHeader = 0;
            if (!view.readLE<uint16_t>(coff + 2, numSections))
            {
                return false;
            }
            if (!view.readLE<uint16_t>(coff + 16, sizeOfOptionalHeader))
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

            if (magic == 0x10B) // PE32
            {
                m_is64Bit = false;
                uint32_t base = 0;
                if (!view.readLE<uint32_t>(opt + 28, base))
                {
                    return false;
                }
                m_imageBase = base;
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
            }
            else
            {
                return false;
            }

            // Section table
            const size_t sectionTable = opt + sizeOfOptionalHeader;
            const size_t sectionEntrySize = 40; // IMAGE_SECTION_HEADER

            if (sectionTable + static_cast<size_t>(numSections) * sectionEntrySize > view.size())
            {
                return false;
            }

            m_sections.reserve(numSections);

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

                if (!view.readLE<uint32_t>(off + 8, virtualSize))
                {
                    return false;
                }
                if (!view.readLE<uint32_t>(off + 12, virtualAddress))
                {
                    return false;
                }
                if (!view.readLE<uint32_t>(off + 16, sizeOfRawData))
                {
                    return false;
                }
                if (!view.readLE<uint32_t>(off + 20, pointerToRawData))
                {
                    return false;
                }
                if (!view.readLE<uint32_t>(off + 36, characteristics))
                {
                    return false;
                }

                // Clamp the section view to the actual file bounds.
                if (pointerToRawData >= view.size())
                {
                    continue;
                }
                const size_t rawSize
                    = std::min<size_t>(sizeOfRawData, view.size() - pointerToRawData);
                BinaryView sectionView = view.subview(pointerToRawData, rawSize);

                m_sections.emplace_back(std::move(name),
                    sectionView,
                    pointerToRawData,
                    virtualAddress,
                    virtualSize,
                    characteristics);
            }

            m_isValid = true;
            return true;
        }

        BinaryFile m_file;
        std::vector<PESection> m_sections;
        uint64_t m_imageBase = 0;
        bool m_is64Bit = false;
        bool m_isValid = false;
    };
} // namespace DumpPDB