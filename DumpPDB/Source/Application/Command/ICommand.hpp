#pragma once

#include <vector>

class ICommand
{
public:
	enum Type
	{
		COMMAND_HELP = 0,
		COMMAND_EXECUTE
	};

protected:
	const int m_argCount;
	const Type m_commandType;

	std::vector<const wchar_t*> m_names;

public:
	ICommand(int a_argumentCount, Type a_type) : m_argCount(a_argumentCount), m_commandType(a_type) {};

	const std::vector<const wchar_t*>& getCommandNames() { return m_names; }

	virtual const wchar_t* getArgHelp() const = 0;
	virtual const wchar_t* getUsageHelp() const = 0;

	virtual bool execute() = 0;

	Type getType() const { return m_commandType; }
};