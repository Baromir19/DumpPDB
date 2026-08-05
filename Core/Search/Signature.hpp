#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace DumpPDB
{
    /// Endianness for multi-byte signature values.
    enum class Endian
    {
        Little,
        Big,
    };

    /// A compiled byte signature pattern with wildcard support.
    /// bytes[i] is the expected byte at position i; mask[i] is false for wildcards.
    struct SignaturePattern
    {
        std::vector<uint8_t> bytes;
        std::vector<bool> mask;

        [[nodiscard]] bool empty() const noexcept { return bytes.empty(); }
        [[nodiscard]] size_t size() const noexcept { return bytes.size(); }
    };

    /// A signature object that can be constructed from various textual forms
    /// or from a raw integer value with explicit endianness.
    class Signature
    {
    public:
        Signature() = default;

        /// Parse from a textual pattern. Supports:
        ///   "FF ?? 01 BD ?? CA"   (IDA style, spaces)
        ///   "FF??01BD??CA"        (compact)
        ///   "0xFF??01BD??CA"      (0x-prefixed)
        ///   "{ FF ?? 01 BD }"     (YARA-style braces)
        /// Returns false on parse error.
        bool parse(const std::string& a_text)
        {
            m_pattern = parseText(a_text);
            return !m_pattern.empty();
        }

        /// Build from an integer value with explicit endianness.
        /// e.g. Signature(0x12345678, Endian::Little) -> bytes 78 56 34 12
        template <typename T>
        static Signature fromValue(T a_value, Endian a_endian)
        {
            Signature sig;
            sig.m_pattern.bytes.resize(sizeof(T));
            sig.m_pattern.mask.assign(sizeof(T), true);

            for (size_t i = 0; i < sizeof(T); ++i)
            {
                if (a_endian == Endian::Little)
                {
                    sig.m_pattern.bytes[i] = static_cast<uint8_t>((a_value >> (8 * i)) & 0xFF);
                }
                else
                {
                    sig.m_pattern.bytes[sizeof(T) - 1 - i]
                        = static_cast<uint8_t>((a_value >> (8 * i)) & 0xFF);
                }
            }

            return sig;
        }

        [[nodiscard]] const SignaturePattern& pattern() const noexcept { return m_pattern; }
        [[nodiscard]] bool empty() const noexcept { return m_pattern.empty(); }
        [[nodiscard]] size_t size() const noexcept { return m_pattern.size(); }

    private:
        static SignaturePattern parseText(const std::string& a_text)
        {
            SignaturePattern out;

            std::string cleaned;
            cleaned.reserve(a_text.size());

            // Strip leading "0x" if present.
            size_t start = 0;
            if (a_text.size() >= 2 && a_text[0] == '0' && (a_text[1] == 'x' || a_text[1] == 'X'))
            {
                start = 2;
            }

            // Remove whitespace, braces, and separators.
            for (size_t i = start; i < a_text.size(); ++i)
            {
                char c = a_text[i];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '-' || c == ':'
                    || c == '{' || c == '}')
                {
                    continue;
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

        SignaturePattern m_pattern;
    };
} // namespace DumpPDB