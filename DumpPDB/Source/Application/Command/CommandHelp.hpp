#pragma once

#include "ICommand.hpp"

#include "..\..\Util\Types\Serialization\BoolSerializable.hpp"

class CommandHelp : public ICommand
{
public:
	CommandHelp() : ICommand(0, COMMAND_HELP) 
	{ 
		m_names.push_back(L"-help");
		m_names.push_back(L"--h");
	};

	// virtual const wchar_t* getCommandName() const override { return L"-help"; }

	virtual const wchar_t* getArgHelp() const override { return L""; }
	virtual const wchar_t* getUsageHelp() const override { return L"print this table"; }

	virtual bool execute(const std::wstring a_commandArgs[] = nullptr) override { return true; }
};