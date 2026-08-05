#pragma once

#include <Core/Binary/BinaryView.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace DumpPDB
{
    /// A named, contiguous range of bytes within a binary.
    /// Used to represent PE sections, or any other logical chunk of a file.
    class BinarySection
    {
    public:
        BinarySection() = default;

        BinarySection(std::string a_name, BinaryView a_view, uint64_t a_fileOffset = 0)
            : m_name(std::move(a_name))
            , m_view(a_view)
            , m_fileOffset(a_fileOffset)
        {
        }

        [[nodiscard]] const std::string& name() const noexcept { return m_name; }
        [[nodiscard]] BinaryView view() const noexcept { return m_view; }
        [[nodiscard]] uint64_t fileOffset() const noexcept { return m_fileOffset; }
        [[nodiscard]] size_t size() const noexcept { return m_view.size(); }
        [[nodiscard]] bool empty() const noexcept { return m_view.empty(); }

        /// True if this section is likely to contain meaningful string data.
        /// .reloc, .pdata, .debug, etc. are excluded.
        [[nodiscard]] bool isStringCandidate() const noexcept
        {
            if (m_name.empty())
            {
                return false;
            }

            // Sections that never contain meaningful strings.
            static const char* const kExcluded[] = {
                ".reloc", ".pdata", ".xdata", ".debug", ".rsrc", ".tls", ".gfids", ".voltbl",
            };

            for (const char* ex : kExcluded)
            {
                if (m_name == ex)
                {
                    return false;
                }
            }

            return true;
        }

    private:
        std::string m_name;
        BinaryView m_view;
        uint64_t m_fileOffset = 0;
    };
} // namespace DumpPDB