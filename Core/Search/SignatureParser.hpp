#pragma once

#include <Core/Search/Signature.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace DumpPDB
{
    /// Parses signature patterns from various textual formats.
    /// Each format has its own dedicated parse method, keeping the logic
    /// clean and testable instead of one monolithic parser.
    class SignatureParser
    {
    public:
        /// IDA style: "FF ?? 01 BD ?? CA" (bytes separated by spaces, ?? = wildcard)
        static SignaturePattern parseIda(const std::string& a_text)
        {
            return parseTokens(a_text, true);
        }

        /// Compact style: "FF??01BD??CA" (no separators)
        static SignaturePattern parseCompact(const std::string& a_text)
        {
            return parseTokens(a_text, false);
        }

        /// Hex style: "0xFF??01BD??CA" (0x prefix, no separators)
        static SignaturePattern parseHex(const std::string& a_text)
        {
            std::string cleaned = a_text;
            if (cleaned.size() >= 2 && cleaned[0] == '0' && (cleaned[1] == 'x' || cleaned[1] == 'X'))
            {
                cleaned = cleaned.substr(2);
            }
            return parseTokens(cleaned, false);
        }

        /// YARA style: "{ FF ?? 01 BD ?? CA }" (braces, optional spaces)
        static SignaturePattern parseYara(const std::string& a_text)
        {
            return parseTokens(a_text, true);
        }

        /// Auto-detect the format and parse.
        /// Returns empty pattern on failure.
        static SignaturePattern parse(const std::string& a_text)
        {
            if (a_text.empty())
            {
                return {};
            }

            // YARA: starts with '{'
            if (a_text[0] == '{')
            {
                return parseYara(a_text);
            }

            // Hex: starts with "0x"
            if (a_text.size() >= 2 && a_text[0] == '0' && (a_text[1] == 'x' || a_text[1] == 'X'))
            {
                return parseHex(a_text);
            }

            // IDA: contains spaces
            for (char c : a_text)
            {
                if (c == ' ' || c == '\t')
                {
                    return parseIda(a_text);
                }
            }

            // Compact: no separators
            return parseCompact(a_text);
        }

    private:
        static SignaturePattern parseTokens(const std::string& a_text, bool a_allowSeparators)
        {
            SignaturePattern out;

            std::string cleaned;
            cleaned.reserve(a_text.size());

            for (char c : a_text)
            {
                if (a_allowSeparators)
                {
                    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '-' || c == ':'
                        || c == '{' || c == '}')
                    {
                        continue;
                    }
                }
                cleaned += c;
            }

            // Must have an even number of hex digits.
            if (cleaned.size() % 2 != 0)
            {
                return {};
            }

            auto hexVal = [](char c) -> int
            {
                if (c >= '0' && c <= '9')
                {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f')
                {
                    return c - 'a' + 10;
                }
                if (c >= 'A' && c <= 'F')
                {
                    return c - 'A' + 10;
                }
                return -1;
            };

            for (size_t i = 0; i < cleaned.size(); i += 2)
            {
                char hi = cleaned[i];
                char lo = cleaned[i + 1];

                if (hi == '?' && lo == '?')
                {
                    out.bytes.push_back(0);
                    out.mask.push_back(false);
                    continue;
                }

                int hiVal = hexVal(hi);
                int loVal = hexVal(lo);
                if (hiVal < 0 || loVal < 0)
                {
                    return {};
                }

                out.bytes.push_back(static_cast<uint8_t>((hiVal << 4) | loVal));
                out.mask.push_back(true);
            }

            return out;
        }
    };
} // namespace DumpPDB