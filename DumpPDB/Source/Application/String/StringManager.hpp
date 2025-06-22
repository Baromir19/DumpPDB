#pragma once 

#include <windows.h>
#include <string>

class StringManager
{
public:
    static std::string convertWCharToChar(const wchar_t* a_string)
    {
        if (!a_string) return "";

        int _sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, a_string, -1, nullptr, 0, nullptr, nullptr);
        if (_sizeNeeded <= 0) return "";

        std::string _result(_sizeNeeded - 1, 0); // -1 to remove null terminator
        WideCharToMultiByte(CP_UTF8, 0, a_string, -1, &_result[0], _sizeNeeded, nullptr, nullptr);

        return _result;
    }

    static std::wstring convertCharToWChar(const char* a_string)
    {
        if (!a_string) return L"";

        int _sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, a_string, -1, nullptr, 0);
        if (_sizeNeeded <= 0) return L"";

        std::wstring _result(_sizeNeeded - 1, 0); // -1 to remove null terminator
        MultiByteToWideChar(CP_UTF8, 0, a_string, -1, &_result[0], _sizeNeeded);

        return _result;
    }

    template<typename T>
    static T convertWCharToInt(const wchar_t* a_string)
    {
        return static_cast<T>(wcstoll(a_string, nullptr, 10));
    }
};