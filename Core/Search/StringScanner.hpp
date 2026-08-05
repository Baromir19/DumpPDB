#pragma once

#include <Core/Binary/BinaryView.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace DumpPDB
{
    /// Bit flags for string encodings to search for.
    enum class StringEncoding : uint32_t
    {
        ASCII = 1 << 0,
        UTF8 = 1 << 1,
        UTF16LE = 1 << 2,
    };

    inline constexpr uint32_t operator|(StringEncoding a, StringEncoding b) noexcept
    {
        return static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
    }

    inline constexpr uint32_t operator&(StringEncoding a, uint32_t b) noexcept
    {
        return static_cast<uint32_t>(a) & b;
    }

    inline constexpr uint32_t operator&(uint32_t a, StringEncoding b) noexcept
    {
        return a & static_cast<uint32_t>(b);
    }

    /// A single string match found in a binary.
    struct StringMatch
    {
        uint64_t offset = 0;
        std::wstring text;
        StringEncoding encoding = StringEncoding::ASCII;
    };

    /// Searches for ASCII / UTF-8 / UTF-16LE strings in a BinaryView.
    /// Minimum length is measured in characters, not bytes.
    class StringScanner
    {
    public:
        explicit StringScanner(BinaryView a_view)
            : m_view(a_view)
        {
        }

        /// Search for strings. a_encodingFlags is a bitwise OR of StringEncoding values.
        [[nodiscard]] std::vector<StringMatch> find(uint32_t a_minLength,
            uint32_t a_encodingFlags) const
        {
            std::vector<StringMatch> matches;

            if (a_minLength == 0)
            {
                a_minLength = 1;
            }

            if (a_encodingFlags & StringEncoding::ASCII)
            {
                findAscii(a_minLength, matches);
            }
            if (a_encodingFlags & StringEncoding::UTF8)
            {
                findUtf8(a_minLength, matches);
            }
            if (a_encodingFlags & StringEncoding::UTF16LE)
            {
                findUtf16Le(a_minLength, matches);
            }

            return matches;
        }

    private:
        static bool isPrintableAscii(uint8_t b) noexcept
        {
            return b >= 0x20 && b <= 0x7E;
        }

        void findAscii(uint32_t a_minLength, std::vector<StringMatch>& a_matches) const
        {
            const size_t size = m_view.size();
            size_t i = 0;

            while (i < size)
            {
                if (isPrintableAscii(m_view[i]))
                {
                    const size_t start = i;
                    std::string run;
                    while (i < size && isPrintableAscii(m_view[i]))
                    {
                        run += static_cast<char>(m_view[i]);
                        ++i;
                    }
                    if (run.size() >= a_minLength)
                    {
                        StringMatch m;
                        m.offset = start;
                        m.text.assign(run.begin(), run.end());
                        m.encoding = StringEncoding::ASCII;
                        a_matches.push_back(std::move(m));
                    }
                }
                else
                {
                    ++i;
                }
            }
        }

        void findUtf16Le(uint32_t a_minLength, std::vector<StringMatch>& a_matches) const
        {
            const size_t size = m_view.size();
            size_t i = 0;

            while (i + 1 < size)
            {
                if (isPrintableAscii(m_view[i]) && m_view[i + 1] == 0x00)
                {
                    const size_t start = i;
                    std::wstring run;
                    while (i + 1 < size && isPrintableAscii(m_view[i]) && m_view[i + 1] == 0x00)
                    {
                        run += static_cast<wchar_t>(m_view[i]);
                        i += 2;
                    }
                    if (run.size() >= a_minLength)
                    {
                        StringMatch m;
                        m.offset = start;
                        m.text = std::move(run);
                        m.encoding = StringEncoding::UTF16LE;
                        a_matches.push_back(std::move(m));
                    }
                }
                else
                {
                    ++i;
                }
            }
        }

        void findUtf8(uint32_t a_minLength, std::vector<StringMatch>& a_matches) const
        {
            const size_t size = m_view.size();
            size_t i = 0;

            while (i < size)
            {
                if (isPrintableUtf8(i))
                {
                    const size_t start = i;
                    std::wstring run;
                    while (i < size && isPrintableUtf8(i))
                    {
                        const size_t len = utf8SeqLen(m_view[i]);
                        appendUtf8(run, i, len);
                        i += len;
                    }
                    // Count characters, not bytes.
                    if (run.size() >= a_minLength)
                    {
                        StringMatch m;
                        m.offset = start;
                        m.text = std::move(run);
                        m.encoding = StringEncoding::UTF8;
                        a_matches.push_back(std::move(m));
                    }
                }
                else
                {
                    ++i;
                }
            }
        }

        static size_t utf8SeqLen(uint8_t b) noexcept
        {
            if (b < 0x80)
            {
                return 1;
            }
            if ((b & 0xE0) == 0xC0)
            {
                return 2;
            }
            if ((b & 0xF0) == 0xE0)
            {
                return 3;
            }
            if ((b & 0xF8) == 0xF0)
            {
                return 4;
            }
            return 0; // invalid lead byte
        }

        static bool isContinuation(uint8_t b) noexcept
        {
            return (b & 0xC0) == 0x80;
        }

        bool isPrintableUtf8(size_t a_pos) const noexcept
        {
            const uint8_t b = m_view[a_pos];
            if (b < 0x80)
            {
                return isPrintableAscii(b);
            }

            const size_t len = utf8SeqLen(b);
            if (len == 0 || a_pos + len > m_view.size())
            {
                return false;
            }

            // Validate continuation bytes.
            for (size_t k = 1; k < len; ++k)
            {
                if (!isContinuation(m_view[a_pos + k]))
                {
                    return false;
                }
            }

            // Reject overlong encodings and surrogates.
            if (len == 2 && b < 0xC2)
            {
                return false;
            }
            if (len == 3 && b == 0xE0 && m_view[a_pos + 1] < 0xA0)
            {
                return false;
            }
            if (len == 3 && b == 0xED && m_view[a_pos + 1] >= 0xA0)
            {
                return false; // surrogate range
            }
            if (len == 4 && b == 0xF0 && m_view[a_pos + 1] < 0x90)
            {
                return false;
            }
            if (len == 4 && b == 0xF4 && m_view[a_pos + 1] >= 0x90)
            {
                return false;
            }
            if (len == 4 && b > 0xF4)
            {
                return false;
            }

            return true;
        }

        void appendUtf8(std::wstring& a_out, size_t a_pos, size_t a_len) const noexcept
        {
            uint32_t cp = 0;
            switch (a_len)
            {
            case 1:
                cp = m_view[a_pos];
                break;
            case 2:
                cp = (static_cast<uint32_t>(m_view[a_pos] & 0x1F) << 6)
                    | (m_view[a_pos + 1] & 0x3F);
                break;
            case 3:
                cp = (static_cast<uint32_t>(m_view[a_pos] & 0x0F) << 12)
                    | (static_cast<uint32_t>(m_view[a_pos + 1] & 0x3F) << 6)
                    | (m_view[a_pos + 2] & 0x3F);
                break;
            case 4:
                cp = (static_cast<uint32_t>(m_view[a_pos] & 0x07) << 18)
                    | (static_cast<uint32_t>(m_view[a_pos + 1] & 0x3F) << 12)
                    | (static_cast<uint32_t>(m_view[a_pos + 2] & 0x3F) << 6)
                    | (m_view[a_pos + 3] & 0x3F);
                break;
            default:
                a_out += L'?';
                return;
            }

            // Encode as UTF-16 (surrogate pair for code points > 0xFFFF).
            if (cp > 0xFFFF)
            {
                cp -= 0x10000;
                a_out += static_cast<wchar_t>(0xD800 + (cp >> 10));
                a_out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
            }
            else
            {
                a_out += static_cast<wchar_t>(cp);
            }
        }

        BinaryView m_view;
    };
} // namespace DumpPDB