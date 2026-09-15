#pragma once

#include <string>

class ISettingDescriptor
{
public:

    virtual ~ISettingDescriptor() = default;

    [[nodiscard]] virtual const wchar_t* getName() const = 0;
    [[nodiscard]] virtual std::wstring getValueAsString() const = 0;

    virtual bool setValueFromString(const std::wstring& a_value) = 0;
};
