#pragma once

#include <Core\PdbToolset.hpp>
#include <Application\Command\ICommand.hpp>
#include <Application\IO\ConsoleManager.hpp>

class CommandSource : public ICommand
{
public:
	CommandSource() : ICommand(0, COMMAND_EXECUTE) { m_names.push_back(L"-sources"); };

	// virtual const wchar_t* getCommandName() const override { return L"-type"; }

	virtual const wchar_t* getArgHelp() const override { return L""; }
	virtual const wchar_t* getUsageHelp() const override { return L"print file sources"; }

	virtual bool execute(const std::wstring a_commandArgs[]) override
	{
		auto text = PdbToolset::instance().dumpSourceFiles();
		if (text.empty()) return false;
		ConsoleManager::print(text.c_str());
		return true;
	}
};
