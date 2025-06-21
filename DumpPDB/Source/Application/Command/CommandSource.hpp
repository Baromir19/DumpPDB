#pragma once

#include "ICommand.hpp"

#include "..\DIA\DiaManager.hpp"

class CommandSource : public ICommand
{
public:
	CommandSource() : ICommand(0, COMMAND_EXECUTE) { m_names.push_back(L"-sources"); };

	// virtual const wchar_t* getCommandName() const override { return L"-type"; }

	virtual const wchar_t* getArgHelp() const override { return L""; }
	virtual const wchar_t* getUsageHelp() const override { return L"print file sources"; }

	virtual bool execute(const std::wstring a_commandArgs[]) override
	{
		return DiaManager::instance().displaySourceFiles();
	}
};