#pragma once

#include "..\..\Util\Container\Singleton.hpp"

#include "..\Console\ConsoleManager.hpp"

#include "ICommand.hpp"

#include "CommandType.hpp"
#include "CommandHelp.hpp"
#include "CommandCompiland.hpp"
#include "CommandSource.hpp"
#include "CommandSettings.hpp"

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
		m_commands.push_back(new CommandCompiland());
		m_commands.push_back(new CommandSource());
		m_commands.push_back(new CommandSettings());

		initState = true;
		return true;
	}

	__declspec(noreturn) void displayCommandsInfo() const
	{
		ConsoleManager::print(L"Usage: DumpPDB.exe <commandname> <filename>\n");
		ConsoleManager::print(L"Command list:\n");

		/// TODO: get cmd size before ":"

		for (const auto& _command : m_commands)
		{
			ConsoleManager::print(L"  ");

			const auto& _names = _command->getCommandNames();

			for (auto i = 0; i < _names.size(); ++i)
			{
				ConsoleManager::print(L"%s", _names[i]);

				if (i < _names.size() - 1) ConsoleManager::print(L", ");
			}

			ConsoleManager::setCursor(20);
			ConsoleManager::print(L" %s", _command->getArgHelp());

			ConsoleManager::setCursor(44);
			ConsoleManager::print(L" : %s\n", _command->getUsageHelp());
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

	ICommand* getCommand(const wchar_t* a_commandString, int a_userMessageSize) const
	{
		auto _ret = getCommand(a_commandString);

		auto _count = _ret->getArgCount();
		auto _type = _ret->getType();

		_count += _type & _ret->s_executableMask ? 1 : 0; // is it need name of .pdb?

		_count += 2; // executable path + command name

		if (a_userMessageSize - _count >= 0) { return _ret; }

		ConsoleManager::printError(L"Command size (%u) is less than minimum (%u) \n", a_userMessageSize, _count);
	}

	void executeCommand(ICommand* a_command)
	{
		a_command->execute(ConsoleManager::instance().getCommandArguments());

		if (a_command->getType() == ICommand::COMMAND_HELP) /// ATTENTION
		{
			displayCommandsInfo();
		}
	}
};