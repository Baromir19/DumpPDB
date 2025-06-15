#pragma once

#include <vector>

class ICommand
{
public:
	enum Type
	{
		COMMAND_HELP = 0x0,
		COMMAND_OPTIONS = 0x1,

		COMMAND_EXECUTE = 0x10
	};

	static constexpr unsigned int s_executableMask = 0xF0;

protected:
	const int m_minArgCount;
	const Type m_commandType;

	std::vector<const wchar_t*> m_names;

public:
	ICommand(int a_argumentCount, Type a_type) : m_minArgCount(a_argumentCount), m_commandType(a_type) {};

	const std::vector<const wchar_t*>& getCommandNames() { return m_names; }

	virtual const wchar_t* getArgHelp() const = 0;
	virtual const wchar_t* getUsageHelp() const = 0;

	virtual bool execute(const std::wstring a_commandArgs[]) = 0;

	int getArgCount() const { return m_minArgCount; }

	Type getType() const { return m_commandType; }
};