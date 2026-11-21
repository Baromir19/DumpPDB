#pragma once

#include <cstdint>
#include <string>

/// Wide string -> UTF-8. Handles UTF-16 surrogate pairs (Windows wchar_t) as
/// well as 32-bit wchar_t. Invalid lone surrogates become U+FFFD.
inline std::string wideToUtf8(const std::wstring& a_wide)
{
    std::string out;
    out.reserve(a_wide.size());

    for (size_t i = 0; i < a_wide.size(); ++i)
    {
        uint32_t cp = static_cast<uint32_t>(a_wide[i]);

        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDFFF)
        {
            if (cp < 0xDC00 && i + 1 < a_wide.size()
                && static_cast<uint32_t>(a_wide[i + 1]) >= 0xDC00
                && static_cast<uint32_t>(a_wide[i + 1]) <= 0xDFFF)
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(a_wide[i + 1]) - 0xDC00);
                ++i;
            }
            else
            {
                cp = 0xFFFD;
            }
        }

        if (cp < 0x80)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}
