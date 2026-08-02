#pragma once

#include <Core/Config/IniFile.hpp>
#include <Core/Config/IniSerializer.hpp>
#include <Core/Config/SettingsRegistry.hpp>

#include <Core/Util/Container/Singleton.hpp>

class SaveManager : public Singleton<SaveManager>
{
    SET_SINGLETON_FRIEND(SaveManager)

    IniFile               m_ini;
    std::filesystem::path m_path;

    DumpConfig    m_dumpConfig;
    CommandConfig m_commandConfig;

    SaveManager() = default;

    void registerSettings()
    {
        auto& reg = SettingsRegistry::instance();

        reg.registerSetting(L"DumpConfig.ShowSize", m_dumpConfig.m_showSize);
        reg.registerSetting(L"DumpConfig.ShowOffset", m_dumpConfig.m_showOffset);
        reg.registerSetting(L"DumpConfig.ShowAccess", m_dumpConfig.m_showAccess);
        reg.registerSetting(L"DumpConfig.ShowInfoComment", m_dumpConfig.m_showInfoComment);
        reg.registerSetting(L"DumpConfig.ShowNonScoped", m_dumpConfig.m_showNonScoped);
        reg.registerSetting(L"DumpConfig.ShowEnumHex", m_dumpConfig.m_showEnumHex);
        reg.registerSetting(L"DumpConfig.ShowTypeSource", m_dumpConfig.m_showTypeSource);
        reg.registerSetting(L"DumpConfig.CurlyBraceNewline", m_dumpConfig.m_curlyBraceNewline);
        reg.registerSetting(L"DumpConfig.HideCompilerGenerated", m_dumpConfig.m_hideCompilerGenerated);
        reg.registerSetting(L"DumpConfig.BaseAccessType", m_dumpConfig.m_baseAccessType);
        reg.registerSetting(L"DumpConfig.IntStyle", m_dumpConfig.m_intStyle);

        reg.registerSetting(L"CommandConfig.UseClipboard", m_commandConfig.m_useClipboard);
    }

public:
    bool initialize(std::filesystem::path a_path = "config.ini")
    {
        static bool initState = false;
        if (initState) return true;

        m_path = std::move(a_path);
        m_ini.load(m_path);

        IniSerializer<DumpConfig>::load(m_ini, m_dumpConfig);
        IniSerializer<CommandConfig>::load(m_ini, m_commandConfig);

        registerSettings();

        initState = true;
        return true;
    }

    void save()
    {
        IniSerializer<DumpConfig>::save(m_ini, m_dumpConfig);
        IniSerializer<CommandConfig>::save(m_ini, m_commandConfig);
        m_ini.save(m_path);
    }

    DumpConfig& dumpConfig() { return m_dumpConfig; }
    CommandConfig& commandConfig() { return m_commandConfig; }
};