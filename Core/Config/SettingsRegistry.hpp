#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <string>

#include <Core/Util/Container/Singleton.hpp>
#include <Core/Config/SettingDescriptor.hpp>

class SettingsRegistry : public Singleton<SettingsRegistry>
{
    SET_SINGLETON_FRIEND(SettingsRegistry)

    std::vector<std::unique_ptr<ISettingDescriptor>>      m_settings;
    std::unordered_map<std::wstring, ISettingDescriptor*> m_byName;

    SettingsRegistry() = default;

public:
    template<typename T>
    void registerSetting(const std::wstring& a_name, T& a_ref)
    {
        auto descriptor = std::make_unique<SettingDescriptor<T>>(a_name, a_ref);
        m_byName[a_name] = descriptor.get();
        m_settings.push_back(std::move(descriptor));
    }

    ISettingDescriptor* find(const std::wstring& a_name) const
    {
        const auto it = m_byName.find(a_name);
        return it != m_byName.end() ? it->second : nullptr;
    }

    const std::vector<std::unique_ptr<ISettingDescriptor>>& all() const
    {
        return m_settings;
    }
};
