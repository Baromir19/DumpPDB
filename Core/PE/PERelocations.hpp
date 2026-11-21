#pragma once

#include <Core/PE/PEImage.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace DumpPDB
{

/// The base-relocation table of a PE image (.reloc).
///
/// Every location that holds an absolute pointer into the image is listed here
/// (HIGHLOW on PE32, DIR64 on PE32+). That makes the table a free, exact
/// cross-reference index: no disassembly is needed to know *where* the code and
/// data keep pointers — only the pointer values have to be read.
///
/// Typical uses:
///   * vftable slots are relocated pointers, so a vftable ends at the first slot
///     that is not in the table;
///   * every code location relocated to a string / global / vftable address is a
///     reference to that object.
class PERelocations
{
public:

    PERelocations() = default;

    /// Parse the table of a_pe. Returns true when at least one entry was found.
    bool load(const PEImage& a_pe)
    {
        m_rvas.clear();
        m_ptrSize = a_pe.is64Bit() ? 8u : 4u;

        const PEDataDirectory& dir = a_pe.directory(PE_DIR_BASERELOC);
        if (dir.rva == 0 || dir.size == 0)
        {
            return false;
        }

        size_t off = 0;
        if (!a_pe.rvaToOffset(dir.rva, off))
        {
            return false;
        }

        const BinaryView file = a_pe.view();
        size_t pos = off;
        const size_t end = std::min<size_t>(off + dir.size, file.size());

        m_rvas.reserve(dir.size / 2);

        while (pos + 8 <= end)
        {
            uint32_t pageRva = 0;
            uint32_t blockSize = 0;
            if (!file.readLE<uint32_t>(pos, pageRva) || !file.readLE<uint32_t>(pos + 4, blockSize))
            {
                break;
            }
            if (blockSize < 8 || pos + blockSize > end)
            {
                break;
            }

            const size_t entries = (blockSize - 8) / 2;
            for (size_t i = 0; i < entries; ++i)
            {
                uint16_t e = 0;
                if (!file.readLE<uint16_t>(pos + 8 + i * 2, e))
                {
                    break;
                }

                const uint32_t type = e >> 12;
                if (type == 3 /*HIGHLOW*/ || type == 10 /*DIR64*/)
                {
                    m_rvas.push_back(pageRva + (e & 0x0FFF));
                }
            }

            pos += blockSize;
        }

        std::sort(m_rvas.begin(), m_rvas.end());
        m_rvas.erase(std::unique(m_rvas.begin(), m_rvas.end()), m_rvas.end());
        return !m_rvas.empty();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return m_rvas.empty();
    }

    [[nodiscard]] size_t size() const noexcept
    {
        return m_rvas.size();
    }

    /// All relocated locations (RVAs), sorted ascending.
    [[nodiscard]] const std::vector<uint32_t>& rvas() const noexcept
    {
        return m_rvas;
    }

    /// True when a pointer is stored at exactly a_rva.
    [[nodiscard]] bool contains(uint64_t a_rva) const noexcept
    {
        return std::binary_search(m_rvas.begin(), m_rvas.end(), static_cast<uint32_t>(a_rva))
               && a_rva <= 0xFFFFFFFFull;
    }

    /// Calls a_fn(rva) for every relocated location in [a_begin, a_end).
    template <typename Fn>
    void forEachInRange(uint64_t a_begin, uint64_t a_end, Fn&& a_fn) const
    {
        auto it = std::lower_bound(m_rvas.begin(), m_rvas.end(), static_cast<uint32_t>(a_begin));
        for (; it != m_rvas.end() && *it < a_end; ++it)
        {
            a_fn(static_cast<uint64_t>(*it));
        }
    }

    [[nodiscard]] uint32_t pointerSize() const noexcept
    {
        return m_ptrSize;
    }

private:

    std::vector<uint32_t> m_rvas;
    uint32_t m_ptrSize = 4;
};

} // namespace DumpPDB
