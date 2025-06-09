#pragma once

#include "ICommand.hpp"

class CommandHelp : public ICommand
{
public:
	CommandHelp() : ICommand(1, COMMAND_HELP) 
	{ 
		m_names.push_back(L"-help");
		m_names.push_back(L"--h");
	};

	// virtual const wchar_t* getCommandName() const override { return L"-help"; }

	virtual const wchar_t* getArgHelp() const override { return L""; }
	virtual const wchar_t* getUsageHelp() const override { return L"Show this table"; }

	virtual bool execute() override { return true; }
};