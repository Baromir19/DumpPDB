#pragma once

#include <string>

#include <Core/DIA/TypeWalker.hpp>

template <typename T>
struct SettingTraits; // primary — only specialisations below are valid

// ---------------------------------------------------------------------------
// bool
// ---------------------------------------------------------------------------
template <>
struct SettingTraits<bool>
{
    static std::wstring toString(bool a_value)
    {
        return a_value ? L"true" : L"false";
    }

    static bool fromString(const std::wstring& a_str, bool& a_out)
    {
        if (a_str == L"1" || a_str == L"true" || a_str == L"True")
        {
            a_out = true;
            return true;
        }
        if (a_str == L"0" || a_str == L"false" || a_str == L"False")
        {
            a_out = false;
            return true;
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
// DWORD  (unsigned long on Windows)
// ---------------------------------------------------------------------------
template <>
struct SettingTraits<DWORD>
{
    static std::wstring toString(DWORD a_value)
    {
        return std::to_wstring(a_value);
    }

    static bool fromString(const std::wstring& a_str, DWORD& a_out)
    {
        try
        {
            a_out = static_cast<DWORD>(std::stoul(a_str, nullptr, 0)); // base=0 accepts "0x…"
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
};

// ---------------------------------------------------------------------------
// IntStyle  — numeric format, consistent with IniSerializer<DumpConfig>
// ---------------------------------------------------------------------------
template <>
struct SettingTraits<IntStyle>
{
    static std::wstring toString(IntStyle a_value)
    {
        return std::to_wstring(static_cast<long>(a_value));
    }

    static bool fromString(const std::wstring& a_str, IntStyle& a_out)
    {
        long rawValue = 0;
        try
        {
            rawValue = std::stol(a_str);
        }
        catch (...)
        {
            return false;
        }

        if (!isValidIntStyle(rawValue))
        {
            return false;
        }

        a_out = static_cast<IntStyle>(rawValue);
        return true;
    }
};
