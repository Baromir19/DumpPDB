#pragma once

#include <Core/Config/SettingsRegistry.hpp>
#include <Core/Config/SaveManager.hpp>
#include <Core/System/ConsoleManager.hpp>

#include <CLI/Application/Command/ICommand.hpp>

class CommandSettings : public ICommand
{
public:

    CommandSettings()
        : ICommand(1, Type::COMMAND_OPTIONS)
    {
        m_names.push_back(L"-settings");
        m_names.push_back(L"--s");
    }

    // virtual const wchar_t* getCommandName() const override { return L"-help"; }

    [[nodiscard]]
    const wchar_t* getArgHelp() const override
    {
        return L"[-h] <setting> <value>";
    }

    [[nodiscard]]
    const wchar_t* getUsageHelp() const override
    {
        return L"set setting";
    }

    virtual bool execute(const std::wstring* a_commandArgs = nullptr) override
    {
        if (a_commandArgs[0] == L"-h")
        {
            ConsoleManager::print(L"Settings list:\n");
            for (const auto& setting : SettingsRegistry::instance().all())
            {
                ConsoleManager::print(
                    L"  %s = %s\n", setting->getName(), setting->getValueAsString().c_str());
            }
            return true;
        }

        if (ConsoleManager::instance().getCommandArguments()->size() < 2)
        {
            ConsoleManager::printError(L"Usage: -settings <setting> <value>\n");
            return false;
        }

        auto* setting = SettingsRegistry::instance().find(a_commandArgs[0]);
        if (!setting)
        {
            ConsoleManager::printError(L"Unknown setting: %s\n", a_commandArgs[0].c_str());
            return false;
        }

        if (!setting->setValueFromString(a_commandArgs[1]))
        {
            ConsoleManager::printError(L"Invalid value \"%s\" for setting %s\n",
                a_commandArgs[1].c_str(),
                a_commandArgs[0].c_str());
            return false;
        }

        SaveManager::instance().save();

        ConsoleManager::print(
            L"%s = %s\n", setting->getName(), setting->getValueAsString().c_str());
        return true;
    }
};