#pragma once

#include <Core/Config/IniFile.hpp>
#include <Core/DIA/SymbolDumper.hpp>

template <typename T>
struct IniSerializer;

template <>
struct IniSerializer<DumpConfig>
{
    static constexpr const char* kSection = "DumpConfig";

    static void save(IniFile& a_ini, const DumpConfig& a_cfg)
    {
        a_ini.set(kSection, "ShowSize", a_cfg.m_showSize);
        a_ini.set(kSection, "ShowOffset", a_cfg.m_showOffset);
        a_ini.set(kSection, "ShowAccess", a_cfg.m_showAccess);
        a_ini.set(kSection, "ShowInfoComment", a_cfg.m_showInfoComment);
        a_ini.set(kSection, "ShowNonScoped", a_cfg.m_showNonScoped);
        a_ini.set(kSection, "ShowEnumHex", a_cfg.m_showEnumHex);
        a_ini.set(kSection, "ShowTypeSource", a_cfg.m_showTypeSource);
        a_ini.set(kSection, "CurlyBraceNewline", a_cfg.m_curlyBraceNewline);
        a_ini.set(kSection, "HideCompilerGenerated", a_cfg.m_hideCompilerGenerated);
        a_ini.set(kSection, "TemplateParams", a_cfg.m_templateParams);
        a_ini.set(kSection, "BaseAccessType", static_cast<unsigned long>(a_cfg.m_baseAccessType));
        a_ini.set(kSection, "IntStyle", static_cast<long>(a_cfg.m_intStyle));
    }

    static void load(const IniFile& a_ini, DumpConfig& a_cfg)
    {
        a_cfg.m_showSize = a_ini.getBool(kSection, "ShowSize", a_cfg.m_showSize);
        a_cfg.m_showOffset = a_ini.getBool(kSection, "ShowOffset", a_cfg.m_showOffset);
        a_cfg.m_showAccess = a_ini.getBool(kSection, "ShowAccess", a_cfg.m_showAccess);
        a_cfg.m_showInfoComment
            = a_ini.getBool(kSection, "ShowInfoComment", a_cfg.m_showInfoComment);
        a_cfg.m_showNonScoped = a_ini.getBool(kSection, "ShowNonScoped", a_cfg.m_showNonScoped);
        a_cfg.m_showEnumHex = a_ini.getBool(kSection, "ShowEnumHex", a_cfg.m_showEnumHex);
        a_cfg.m_showTypeSource = a_ini.getBool(kSection, "ShowTypeSource", a_cfg.m_showTypeSource);
        a_cfg.m_curlyBraceNewline
            = a_ini.getBool(kSection, "CurlyBraceNewline", a_cfg.m_curlyBraceNewline);
        a_cfg.m_hideCompilerGenerated
            = a_ini.getBool(kSection, "HideCompilerGenerated", a_cfg.m_hideCompilerGenerated);
        a_cfg.m_templateParams = a_ini.getBool(kSection, "TemplateParams", a_cfg.m_templateParams);
        a_cfg.m_baseAccessType = a_ini.getUlong(kSection, "BaseAccessType", a_cfg.m_baseAccessType);

        const long rawStyle
            = a_ini.getLong(kSection, "IntStyle", static_cast<long>(a_cfg.m_intStyle));
        a_cfg.m_intStyle
            = isValidIntStyle(rawStyle) ? static_cast<IntStyle>(rawStyle) : IntStyle::Cstdint;
    }
};

struct CommandConfig
{
    bool m_useClipboard = true;
};

template <>
struct IniSerializer<CommandConfig>
{
    static constexpr const char* kSection = "CommandConfig";

    static void save(IniFile& a_ini, const CommandConfig& a_cfg)
    {
        a_ini.set(kSection, "UseClipboard", a_cfg.m_useClipboard);
    }

    static void load(const IniFile& a_ini, CommandConfig& a_cfg)
    {
        a_cfg.m_useClipboard = a_ini.getBool(kSection, "UseClipboard", a_cfg.m_useClipboard);
    }
};