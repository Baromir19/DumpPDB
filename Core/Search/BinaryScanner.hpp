#pragma once

#include <Core/Binary/BinaryFile.hpp>
#include <Core/Binary/BinaryView.hpp>
#include <Core/PE/PEImage.hpp>
#include <Core/Search/RegexFilter.hpp>
#include <Core/Search/Signature.hpp>
#include <Core/Search/SignatureParser.hpp>
#include <Core/Search/SignatureScanner.hpp>
#include <Core/Search/StringScanner.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace DumpPDB
{
    /// High-level facade for searching strings and signatures in binary files.
    /// For PE files, searches are scoped to string-candidate sections
    /// (.text, .rdata, .data, etc.) rather than the whole file.
    class BinaryScanner
    {
    public:
        BinaryScanner() = default;

        /// Load a file. Returns false on load failure.
        bool load(const std::wstring& a_path)
        {
            m_pe = PEImage();
            m_file = BinaryFile();

            if (!m_file.load(a_path))
            {
                return false;
            }

            // Try to parse as PE; if it's not a PE, fall back to raw file scanning.
            m_pe.load(m_file);
            return true;
        }

        /// Search for strings in the file.
        /// a_encodingFlags is a bitwise OR of StringEncoding values.
        /// a_sectionNames is a comma-separated list of PE section names to search
        /// (e.g. ".rdata,.data"). Empty string means "all string-candidate sections"
        /// for PE files, or the whole file for non-PE files.
        /// a_regexPattern is an optional regex to filter results (empty = no filter).
        [[nodiscard]] std::vector<StringMatch> findStrings(uint32_t a_minLength,
            uint32_t a_encodingFlags,
            const std::string& a_sectionNames = "",
            const std::string& a_regexPattern = "") const
        {
            std::vector<StringMatch> all;

            if (m_pe.isValid())
            {
                // PE: search only in the selected sections.
                auto sections = m_pe.sectionsByName(a_sectionNames);
                for (const auto* section : sections)
                {
                    if (!section || section->empty())
                    {
                        continue;
                    }

                    StringScanner scanner(section->view());
                    auto matches = scanner.find(a_minLength, a_encodingFlags);

                    // Adjust offsets to be file-relative.
                    for (auto& m : matches)
                    {
                        m.offset += section->fileOffset();
                    }

                    all.insert(all.end(), matches.begin(), matches.end());
                }
            }
            else
            {
                // Non-PE: search the whole file.
                StringScanner scanner(m_file.view());
                all = scanner.find(a_minLength, a_encodingFlags);
            }

            // Apply regex filter if provided.
            if (!a_regexPattern.empty())
            {
                RegexFilter filter;
                if (filter.compile(a_regexPattern))
                {
                    all = filter.filter(all);
                }
            }

            return all;
        }

        /// Search for a byte signature in the file.
        /// a_sectionNames is a comma-separated list of PE section names to search
        /// (empty = all string-candidate sections for PE, whole file for non-PE).
        [[nodiscard]] std::vector<SignatureMatch> findSignatures(
            const std::string& a_pattern,
            const std::string& a_sectionNames = "") const
        {
            auto pattern = SignatureParser::parse(a_pattern);
            if (pattern.empty())
            {
                return {};
            }

            return findSignatures(pattern, a_sectionNames);
        }

        /// Search for a compiled signature pattern.
        [[nodiscard]] std::vector<SignatureMatch> findSignatures(
            const SignaturePattern& a_pattern,
            const std::string& a_sectionNames = "") const
        {
            std::vector<SignatureMatch> all;

            if (m_pe.isValid())
            {
                auto sections = m_pe.sectionsByName(a_sectionNames);
                for (const auto* section : sections)
                {
                    if (!section || section->empty())
                    {
                        continue;
                    }

                    SignatureScanner scanner(section->view());
                    auto matches = scanner.find(a_pattern);

                    for (auto& m : matches)
                    {
                        m.offset += section->fileOffset();
                    }

                    all.insert(all.end(), matches.begin(), matches.end());
                }
            }
            else
            {
                SignatureScanner scanner(m_file.view());
                all = scanner.find(a_pattern);
            }

            return all;
        }

        /// Search for a Signature object.
        [[nodiscard]] std::vector<SignatureMatch> findSignatures(
            const Signature& a_signature,
            const std::string& a_sectionNames = "") const
        {
            return findSignatures(a_signature.pattern(), a_sectionNames);
        }

        /// True if the loaded file is a valid PE image.
        [[nodiscard]] bool isPE() const noexcept { return m_pe.isValid(); }

        /// The parsed PE image (valid only if isPE()).
        [[nodiscard]] const PEImage& pe() const noexcept { return m_pe; }

        /// The raw file.
        [[nodiscard]] const BinaryFile& file() const noexcept { return m_file; }

    private:
        BinaryFile m_file;
        PEImage m_pe;
    };
} // namespace DumpPDB