#pragma once

#include <Core\PdbToolset.hpp>
#include <Application\Command\ICommand.hpp>
#include <Application\Console\ConsoleManager.hpp>

class CommandType : public ICommand
{
public:
	CommandType() : ICommand(1, COMMAND_EXECUTE) { m_names.push_back(L"-type"); };

	// virtual const wchar_t* getCommandName() const override { return L"-type"; }

	virtual const wchar_t* getArgHelp() const override { return L"<typename>"; }
	virtual const wchar_t* getUsageHelp() const override { return L"print type info"; }

	virtual bool execute(const std::wstring a_commandArgs[]) override
	{ 
		if (a_commandArgs == nullptr)
		{
			return false;
		}

		auto _text = PdbToolset::instance().dumpTypeByName(a_commandArgs[0].c_str(), false);
		if (_text.empty()) return false;
		ConsoleManager::print(_text.c_str());
		return true;
	}
};
