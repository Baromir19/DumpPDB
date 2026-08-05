#pragma once

#include <Core/Binary/BinaryView.hpp>
#include <Core/Search/Signature.hpp>
#include <Core/Search/SignatureParser.hpp>

#include <cstdint>
#include <vector>

namespace DumpPDB
{
    /// A single signature match: the file offset where the pattern was found.
    struct SignatureMatch
    {
        uint64_t offset = 0;
    };

    /// Searches for byte signatures (with wildcards) in a BinaryView.
    class SignatureScanner
    {
    public:
        explicit SignatureScanner(BinaryView a_view)
            : m_view(a_view)
        {
        }

        /// Search for a compiled signature pattern.
        [[nodiscard]] std::vector<SignatureMatch> find(const SignaturePattern& a_pattern) const
        {
            std::vector<SignatureMatch> matches;

            if (a_pattern.empty() || a_pattern.size() > m_view.size())
            {
                return matches;
            }

            const size_t patternSize = a_pattern.size();
            const size_t dataSize = m_view.size();

            for (size_t i = 0; i + patternSize <= dataSize; ++i)
            {
                bool found = true;
                for (size_t j = 0; j < patternSize; ++j)
                {
                    if (a_pattern.mask[j] && m_view[i + j] != a_pattern.bytes[j])
                    {
                        found = false;
                        break;
                    }
                }
                if (found)
                {
                    matches.push_back({i});
                }
            }

            return matches;
        }

        /// Search for a Signature object.
        [[nodiscard]] std::vector<SignatureMatch> find(const Signature& a_signature) const
        {
            return find(a_signature.pattern());
        }

        /// Search for a textual pattern (auto-detects format).
        /// Returns empty vector if the pattern is invalid.
        [[nodiscard]] std::vector<SignatureMatch> find(const std::string& a_patternText) const
        {
            auto pattern = SignatureParser::parse(a_patternText);
            if (pattern.empty())
            {
                return {};
            }
            return find(pattern);
        }

    private:
        BinaryView m_view;
    };
} // namespace DumpPDB