#pragma once

#include "ICommand.hpp"

#include "..\..\Util\Types\Serialization\BoolSerializable.hpp"
#include "CommandType.hpp"

class Nulla : public BoolSerializable, CommandType
{
	unsigned int a;

	friend class BoolSerializable;

	enum TEST : unsigned __int8
	{
		bb = 1,
		aa = 0,
		cc = -1,
	};

	class ABS : BoolSerializable
	{
		int a5;

		class AAA
		{

		};
	};

	struct aBC
	{

	};

	union LOL
	{
		int a;
		bool b;
	};

	typedef float __NON;

	__NON __a;

	virtual float& ANON() { float _a = (float)__a; return _a; }

	virtual const wchar_t* const RETT() { return L""; }
};

class CommandHelp : public ICommand
{
public:
	CommandHelp() : ICommand(0, COMMAND_HELP) 
	{ 
		m_names.push_back(L"-help");
		m_names.push_back(L"--h");
	};

	Nulla a;

	// virtual const wchar_t* getCommandName() const override { return L"-help"; }

	virtual const wchar_t* getArgHelp() const override { return L""; }
	virtual const wchar_t* getUsageHelp() const override { return L"print this table"; }

	virtual bool execute(const std::wstring a_commandArgs[] = nullptr) override { return true; }
};