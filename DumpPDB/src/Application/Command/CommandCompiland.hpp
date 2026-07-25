#pragma once

#include <Core\PdbToolset.hpp>
#include <Application\Command\ICommand.hpp>
#include <Application\IO\ConsoleManager.hpp>

class CommandCompiland : public ICommand
{
public:
	CommandCompiland() : ICommand(0, COMMAND_EXECUTE) { m_names.push_back(L"-compilands"); };

	virtual const wchar_t* getArgHelp() const override { return L"[full]"; }
	virtual const wchar_t* getUsageHelp() const override { return L"print compilands"; }

	virtual bool execute(const std::wstring a_commandArgs[]) override
	{
		if (a_commandArgs == nullptr)
		{
			return false;
		}

		std::wstring text;
		auto fullInfo = wcscmp(a_commandArgs[0].c_str(), L"true"); /// ATTENTION

		if (!fullInfo)
		{
			text = PdbToolset::instance().dumpCompilandsEnv();
		}
		else
		{
			text = PdbToolset::instance().dumpCompilands();
		}

		if (text.empty()) return false;
		ConsoleManager::print(text.c_str());
		return true;
	}
};
