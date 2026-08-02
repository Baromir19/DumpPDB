#pragma once

#include <string>

class ISettingDescriptor
{
public:
    virtual ~ISettingDescriptor() = default;

    virtual const wchar_t* getName()                                    const = 0;
    virtual std::wstring   getValueAsString()                           const = 0;
    virtual bool           setValueFromString(const std::wstring& a_value)    = 0;
};
