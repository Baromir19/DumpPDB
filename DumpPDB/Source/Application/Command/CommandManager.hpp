#pragma once

#include "..\..\Util\Container\Singleton.hpp"

#include "..\Console\ConsoleManager.hpp"

#include "ICommand.hpp"
#include "CommandType.hpp"
#include "CommandHelp.hpp"

class CommandManager : public Singleton<CommandManager>
{
    SET_SINGLETON_FRIEND(CommandManager)

protected:
	std::vector<ICommand*> m_commands;

public:
	bool initialize()
	{
		static bool initState = false;
		if (initState) return true;

		m_commands.push_back(new CommandType());
		m_commands.push_back(new CommandHelp());

		initState = true;
		return true;
	}

	__declspec(noreturn) void displayCommandsInfo() const
	{
		ConsoleManager::print(L"Command list:\n");

		for (const auto& _command : m_commands)
		{
			ConsoleManager::print(L"\t");

			auto _names = _command->getCommandNames();

			for (auto i = 0; i < _names.size(); ++i)
			{
				ConsoleManager::print(L"%s", _names[i]);

				if (i < _names.size() - 1) ConsoleManager::print(L", ");
			}

			ConsoleManager::print(L" %s : %s\n", _command->getArgHelp(), _command->getUsageHelp());
		}

		exit(EXIT_SUCCESS);
	}

	ICommand* getCommand(const wchar_t* a_commandString) const
	{
		for (const auto& _command : m_commands)
		{
			for (const auto& _name : _command->getCommandNames())
			{
				if (!wcscmp(_name, a_commandString))
				{
					return _command;
				}
			}
		}

		ConsoleManager::printError(L"No command defined as \"%s\"! \n", a_commandString);
		// displayCommandsInfo();
	}

	void executeCommand(ICommand* a_command)
	{
		a_command->execute();

		if (a_command->getType() == ICommand::COMMAND_HELP)
		{
			displayCommandsInfo();
		}
	}
};