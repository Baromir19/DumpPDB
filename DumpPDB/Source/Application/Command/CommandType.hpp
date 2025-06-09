#pragma once

#include "ICommand.hpp"

class CommandType : public ICommand
{
public:
	CommandType() : ICommand(1, COMMAND_EXECUTE) { m_names.push_back(L"-type"); };

	// virtual const wchar_t* getCommandName() const override { return L"-type"; }

	virtual const wchar_t* getArgHelp() const override { return L"[Type]"; }
	virtual const wchar_t* getUsageHelp() const override { return L"none"; }

	virtual bool execute() override { return true; }
};