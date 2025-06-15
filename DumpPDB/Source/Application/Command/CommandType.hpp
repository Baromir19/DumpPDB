#pragma once

#include "ICommand.hpp"

#include "..\DIA\DiaManager.hpp"

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

		return DiaManager::instance().displayType(a_commandArgs[0].c_str());
	}
};