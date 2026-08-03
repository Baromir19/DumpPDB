#pragma once

#include <memory>

#include <Core/Util/Container/Singleton.hpp>

#include <Core/System/ConsoleManager.hpp>
#include <Core/Config/IniSerializer.hpp>

#include <CLI/Application/Command/ICommand.hpp>
#include <CLI/Application/Command/Commands/CommandType.hpp>
#include <CLI/Application/Command/Commands/CommandCompiland.hpp>
#include <CLI/Application/Command/Commands/CommandSource.hpp>
#include <CLI/Application/Command/Commands/CommandSettings.hpp>
#include <CLI/Application/Command/Commands/CommandHelp.hpp>

class CommandManager : public Singleton<CommandManager>
{
    SET_SINGLETON_FRIEND(CommandManager)

protected:

    std::vector<std::unique_ptr<ICommand>> m_commands;

public:

    bool initialize(const CommandConfig& a_config, const DumpConfig& a_dumpConfig)
    {
        static bool initState = false;
        if (initState)
        {
            return true;
        }

        m_commands.push_back(std::make_unique<CommandType>(a_config.m_useClipboard, a_dumpConfig));
        m_commands.push_back(std::make_unique<CommandHelp>());
        m_commands.push_back(std::make_unique<CommandCompiland>());
        m_commands.push_back(std::make_unique<CommandSource>());
        m_commands.push_back(std::make_unique<CommandSettings>());

        initState = true;
        return true;
    }

    [[noreturn]]
    void displayCommandsInfo() const
    {
        ConsoleManager::print(L"Usage: DumpPDB.exe <commandname> <filename>\n");
        ConsoleManager::print(L"Command list:\n");

        /// TODO: get cmd size before ":"

        for (const auto& command : m_commands)
        {
            ConsoleManager::print(L"  ");

            const auto& names = command->getCommandNames();

            for (auto i = 0; i < names.size(); ++i)
            {
                ConsoleManager::print(L"%s", names[i]);

                if (i < names.size() - 1)
                {
                    ConsoleManager::print(L", ");
                }
            }

            ConsoleManager::setCursor(20);
            ConsoleManager::print(L" %s", command->getArgHelp());

            ConsoleManager::setCursor(44);
            ConsoleManager::print(L" : %s\n", command->getUsageHelp());
        }

        exit(EXIT_SUCCESS);
    }

    ICommand* getCommand(const wchar_t* a_commandString) const
    {
        for (const auto& command : m_commands)
        {
            for (const auto& name : command->getCommandNames())
            {
                if (wcscmp(name, a_commandString) == 0)
                {
                    return command.get();
                }
            }
        }

        ConsoleManager::printError(L"No command defined as \"%s\"! \n", a_commandString);
        return nullptr;
        // displayCommandsInfo();
    }

    ICommand* getCommand(const wchar_t* a_commandString, int a_userMessageSize) const
    {
        auto* ret = getCommand(a_commandString);

        auto count = ret->getArgCount();
        auto type = ret->getType();

        count += static_cast<unsigned int>(type) & ICommand::s_executableMask
                     ? 1
                     : 0; // is it need name of .pdb?

        count += 2; // executable path + command name

        if (a_userMessageSize - count >= 0)
        {
            return ret;
        }

        ConsoleManager::printError(
            L"Command size (%u) is less than minimum (%u) \n", a_userMessageSize, count);
        return nullptr;
    }

    void executeCommand(ICommand* a_command) const
    {
        a_command->execute(ConsoleManager::instance().getCommandArguments());

        if (a_command->getType() == ICommand::Type::COMMAND_HELP) /// ATTENTION
        {
            displayCommandsInfo();
        }
    }
};