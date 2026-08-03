#pragma once

#include <string>
#include <Core/Config/ISettingDescriptor.hpp>
#include <Core/Config/SettingTraits.hpp>

template <typename T>
class SettingDescriptor : public ISettingDescriptor
{
    std::wstring m_name;
    T& m_ref;

public:

    SettingDescriptor(std::wstring a_name, T& a_ref)
        : m_name(std::move(a_name))
        , m_ref(a_ref)
    {
    }

    [[nodiscard]] const wchar_t* getName() const override
    {
        return m_name.c_str();
    }

    [[nodiscard]] std::wstring getValueAsString() const override
    {
        return SettingTraits<T>::toString(m_ref);
    }

    bool setValueFromString(const std::wstring& a_value) override
    {
        T parsed{};
        if (!SettingTraits<T>::fromString(a_value, parsed))
            return false;

        m_ref = parsed;
        return true;
    }
};
