#pragma once

#include "ICommand.hpp"

#include "..\..\Util\Types\Serialization\IBaseSerializableBase.hpp"

class CommandSettings : public ICommand
{
public:
	CommandSettings() : ICommand(0, COMMAND_OPTIONS)
	{
		m_names.push_back(L"-settings");
		m_names.push_back(L"--s");
	};

	// virtual const wchar_t* getCommandName() const override { return L"-help"; }

	virtual const wchar_t* getArgHelp() const override { return L"[-h] <setting> <value>"; }
	virtual const wchar_t* getUsageHelp() const override { return L"set setting"; }

	virtual bool execute(const std::wstring a_commandArgs[] = nullptr) override 
	{ 
		if (a_commandArgs[0] == L"-h")
		{
			IBaseSerializableBase::displayInstancesInfo();
			return true;
		}

		auto _value = StringManager::convertWCharToInt<int>(a_commandArgs[1].c_str());
		
		IBaseSerializableBase::setInstance(
			StringManager::convertWCharToChar(a_commandArgs[0].c_str()).c_str(), 
			_value);

		return true; 
	}
};