#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace DumpPDB
{
    /// Non-owning read-only view over a contiguous range of bytes.
    /// This is the fundamental abstraction that all scanners operate on,
    /// so they can work over files, PE sections, memory ranges, etc.
    class BinaryView
    {
    public:
        BinaryView() = default;

        BinaryView(const uint8_t* a_data, size_t a_size)
            : m_data(a_data)
            , m_size(a_size)
        {
        }

        BinaryView(const std::vector<uint8_t>& a_data)
            : m_data(a_data.data())
            , m_size(a_data.size())
        {
        }

        [[nodiscard]] const uint8_t* data() const noexcept { return m_data; }
        [[nodiscard]] size_t size() const noexcept { return m_size; }
        [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

        [[nodiscard]] const uint8_t& operator[](size_t a_index) const
        {
            return m_data[a_index];
        }

        /// Create a sub-view starting at a_offset with a_length bytes.
        /// Returns an empty view if the range is out of bounds.
        [[nodiscard]] BinaryView subview(size_t a_offset, size_t a_length) const noexcept
        {
            if (a_offset > m_size || a_length > m_size - a_offset)
            {
                return {};
            }
            return {m_data + a_offset, a_length};
        }

        /// Create a sub-view starting at a_offset to the end of this view.
        [[nodiscard]] BinaryView subview(size_t a_offset) const noexcept
        {
            if (a_offset > m_size)
            {
                return {};
            }
            return {m_data + a_offset, m_size - a_offset};
        }

        /// Read a little-endian value of type T at a_offset.
        /// Returns false if the range is out of bounds.
        template <typename T>
        [[nodiscard]] bool readLE(size_t a_offset, T& a_out) const noexcept
        {
            if (a_offset + sizeof(T) > m_size)
            {
                return false;
            }
            a_out = 0;
            for (size_t i = 0; i < sizeof(T); ++i)
            {
                a_out |= static_cast<T>(m_data[a_offset + i]) << (8 * i);
            }
            return true;
        }

        /// Read a big-endian value of type T at offset given.
        /// Returns false if the value is out of bounds.
        template <typename T>
        [[nodiscard]] bool readBigEndian(size_t a_offset, T& a_out) const noexcept
        {
            if (a_offset + sizeof(T) > m_size)
            {
                return false;
            }
            a_out = 0;
            for (size_t i = 0; i < sizeof(T); ++i)
            {
                a_out = static_cast<T>((a_out << 8) | m_data[a_offset + i]);
            }
            return true;
        }

    private:
        const uint8_t* m_data = nullptr;
        size_t m_size = 0;
    };
} // namespace DumpPDB