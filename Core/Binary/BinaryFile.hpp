#pragma once

#include <Core/Binary/BinaryView.hpp>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace DumpPDB
{
    /// Owns the raw bytes of a binary file and exposes a BinaryView over them.
    /// This class only reads the file — it knows nothing about PE structure,
    /// strings, signatures, or any other higher-level concept.
    class BinaryFile
    {
    public:
        BinaryFile() = default;

        /// Load a file from disk. Returns false on any error
        /// (file not found, read failure, etc.).
        bool load(const std::wstring& a_path)
        {
            m_data.clear();

            std::ifstream file(a_path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                return false;
            }

            const auto size = file.tellg();
            if (size < 0)
            {
                return false;
            }

            m_data.resize(static_cast<size_t>(size));
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(m_data.data()), static_cast<std::streamsize>(size));

            if (!file.good() && !file.eof())
            {
                m_data.clear();
                return false;
            }

            return true;
        }

        /// The raw file bytes.
        [[nodiscard]] const std::vector<uint8_t>& data() const noexcept { return m_data; }

        /// A non-owning view over the whole file.
        [[nodiscard]] BinaryView view() const noexcept { return {m_data}; }

        [[nodiscard]] bool empty() const noexcept { return m_data.empty(); }
        [[nodiscard]] size_t size() const noexcept { return m_data.size(); }

    private:
        std::vector<uint8_t> m_data;
    };
} // namespace DumpPDB