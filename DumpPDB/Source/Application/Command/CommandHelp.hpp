#pragma once

#include "ICommand.hpp"

#include "..\..\Util\Types\Serialization\BoolSerializable.hpp"
#include "CommandType.hpp"

class Nulla : public BoolSerializable, CommandType
{
	unsigned int INTVALUE;
	static constexpr int CONSTEXPRINT = 8;
	const int CONSTVALUE = 8;
	BoolSerializable BOOLOBJ;
	BoolSerializable* BOOLPTR;
	BoolSerializable BOOLARRAY[10];
	bool BOOLARR[10][30][1]; /// <- TODO
	int (*tester)(char*, int); /// <- TODO
	// int&& newRef; /// <- TODO
	int bfield : 4;
	int bfield1 : 8;
	static inline auto ATOTYPE = 7;

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

	virtual float& ANON(bool a___, __NON b, aBC _d, LOL _c, TEST __formal, LOL) { float _a = (float)__a;  float b___ = 8; return _a; }

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