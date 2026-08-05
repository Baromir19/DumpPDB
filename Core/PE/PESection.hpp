#pragma once

#include <Core/Binary/BinarySection.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace DumpPDB
{
    /// A PE section (e.g. .text, .rdata, .data).
    /// Extends BinarySection with PE-specific attributes.
    class PESection : public BinarySection
    {
    public:
        PESection() = default;

        PESection(std::string a_name,
            BinaryView a_view,
            uint64_t a_fileOffset,
            uint32_t a_virtualAddress,
            uint32_t a_virtualSize,
            uint32_t a_characteristics)
            : BinarySection(std::move(a_name), a_view, a_fileOffset)
            , m_virtualAddress(a_virtualAddress)
            , m_virtualSize(a_virtualSize)
            , m_characteristics(a_characteristics)
        {
        }

        [[nodiscard]] uint32_t virtualAddress() const noexcept { return m_virtualAddress; }
        [[nodiscard]] uint32_t virtualSize() const noexcept { return m_virtualSize; }
        [[nodiscard]] uint32_t characteristics() const noexcept { return m_characteristics; }

        /// IMAGE_SCN_MEM_READ
        [[nodiscard]] bool isReadable() const noexcept
        {
            return (m_characteristics & 0x40000000u) != 0;
        }

        /// IMAGE_SCN_MEM_WRITE
        [[nodiscard]] bool isWritable() const noexcept
        {
            return (m_characteristics & 0x80000000u) != 0;
        }

        /// IMAGE_SCN_MEM_EXECUTE
        [[nodiscard]] bool isExecutable() const noexcept
        {
            return (m_characteristics & 0x20000000u) != 0;
        }

        /// IMAGE_SCN_CNT_INITIALIZED_DATA
        [[nodiscard]] bool hasInitializedData() const noexcept
        {
            return (m_characteristics & 0x00000040u) != 0;
        }

        /// IMAGE_SCN_CNT_UNINITIALIZED_DATA
        [[nodiscard]] bool hasUninitializedData() const noexcept
        {
            return (m_characteristics & 0x00000080u) != 0;
        }

        /// IMAGE_SCN_CNT_CODE
        [[nodiscard]] bool hasCode() const noexcept
        {
            return (m_characteristics & 0x00000020u) != 0;
        }

    private:
        uint32_t m_virtualAddress = 0;
        uint32_t m_virtualSize = 0;
        uint32_t m_characteristics = 0;
    };
} // namespace DumpPDB