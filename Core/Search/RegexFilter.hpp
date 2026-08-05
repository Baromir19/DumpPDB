#pragma once

#include <Core/Search/StringScanner.hpp>

#include <regex>
#include <string>
#include <vector>

namespace DumpPDB
{
    /// Filters string matches by a regular expression.
    /// The regex is applied to the match text (wide string).
    class RegexFilter
    {
    public:
        RegexFilter() = default;

        /// Compile a regex. Returns false if the pattern is invalid.
        bool compile(const std::wstring& a_pattern)
        {
            try
            {
                m_regex = std::wregex(a_pattern, std::regex::ECMAScript);
                m_hasRegex = true;
                return true;
            }
            catch (const std::regex_error&)
            {
                m_hasRegex = false;
                return false;
            }
        }

        /// Compile a regex from a narrow string (converted to wide).
        bool compile(const std::string& a_pattern)
        {
            std::wstring wide(a_pattern.begin(), a_pattern.end());
            return compile(wide);
        }

        [[nodiscard]] bool hasRegex() const noexcept { return m_hasRegex; }

        /// Filter matches. If no regex is compiled, returns all matches unchanged.
        [[nodiscard]] std::vector<StringMatch> filter(
            const std::vector<StringMatch>& a_matches) const
        {
            if (!m_hasRegex)
            {
                return a_matches;
            }

            std::vector<StringMatch> out;
            out.reserve(a_matches.size());

            for (const auto& m : a_matches)
            {
                if (std::regex_search(m.text, m_regex))
                {
                    out.push_back(m);
                }
            }

            return out;
        }

    private:
        std::wregex m_regex;
        bool m_hasRegex = false;
    };
} // namespace DumpPDB