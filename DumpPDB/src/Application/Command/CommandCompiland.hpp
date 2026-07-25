#pragma once

#include <Core\PdbToolset.hpp>
#include <Application\Command\ICommand.hpp>
#include <Application\Console\ConsoleManager.hpp>

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

		std::wstring _text;
		auto _fullInfo = wcscmp(a_commandArgs[0].c_str(), L"true"); /// ATTENTION

		if (!_fullInfo)
		{
			_text = PdbToolset::instance().dumpCompilandsEnv();
		}
		else
		{
			_text = PdbToolset::instance().dumpCompilands();
		}

		if (_text.empty()) return false;
		ConsoleManager::print(_text.c_str());
		return true;
	}
};
