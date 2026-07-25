#pragma once

#include <Core\PdbToolset.hpp>
#include <Application\Command\ICommand.hpp>
#include <Application\IO\ConsoleManager.hpp>
#include <Application\IO\ClipboardManager.hpp>

class CommandType : public ICommand
{
private:
	bool m_useClipboard = false;

public:
	CommandType(bool a_useClipboard) 
		: ICommand(1, COMMAND_EXECUTE) 
	{ 
		m_useClipboard = a_useClipboard;
		m_names.push_back(L"-type"); 
	}

	// virtual const wchar_t* getCommandName() const override { return L"-type"; }

	virtual const wchar_t* getArgHelp() const override { return L"<typename>"; }
	virtual const wchar_t* getUsageHelp() const override { return L"print type info"; }

	virtual bool execute(const std::wstring a_commandArgs[]) override
	{ 
		if (a_commandArgs == nullptr)
		{
			return false;
		}

		auto text = PdbToolset::instance().dumpTypeByName(a_commandArgs[0].c_str(), false);
		if (text.empty()) return false;

		if (m_useClipboard && !ClipboardManager::copy(text))
		{
			ConsoleManager::printError(
				L"Failed to copy result to clipboard.\n\n"
			);
		}

		ConsoleManager::print(text.c_str());
		return true;
	}
};
