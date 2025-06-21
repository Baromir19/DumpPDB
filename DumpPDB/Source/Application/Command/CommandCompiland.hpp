#pragma once

#include "ICommand.hpp"

#include "..\DIA\DiaManager.hpp"

class CommandCompiland : public ICommand
{
public:
	CommandCompiland() : ICommand(0, COMMAND_EXECUTE) { m_names.push_back(L"-compilands"); };

	virtual const wchar_t* getArgHelp() const override { return L"[full]"; }
	virtual const wchar_t* getUsageHelp() const override { return L"print compilands"; }

	virtual bool execute(const std::wstring a_commandArgs[]) override
	{
		auto _fullInfo = wcscmp(a_commandArgs[0].c_str(), L"true"); /// ATTENTION

		if (!_fullInfo)
		{
			return DiaManager::instance().displayCompilandsEnv();
		}
		
		return DiaManager::instance().displayCompilands();
	}
};