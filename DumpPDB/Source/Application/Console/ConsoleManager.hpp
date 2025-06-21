#pragma once

#include <vector>
#include <string>
#include <cstdarg>

#include "..\..\Util\Container\Singleton.hpp"

#include "..\Command\ICommand.hpp"

class ConsoleManager : public Singleton<ConsoleManager>
{
	SET_SINGLETON_FRIEND(ConsoleManager)

protected:
	std::vector<std::wstring> m_arguments;
	std::wstring m_path;
	std::wstring m_command;

	static constexpr wchar_t s_pdbFormat[] = L".pdb";

	static constexpr int s_minArgPathSize = 3; // Program path (.exe) + call type + .pdb path
	static constexpr int s_minArgCmdSize = 2;

	static constexpr size_t s_bufferSize = 0x2000;
	static inline wchar_t s_lineBuffer[s_bufferSize];
	static inline int s_bufferPointer = 0;

public:
	bool initialize(int a_argc, wchar_t* a_argv[])
	{
		static bool initState = false;
		if (initState) return true;

		// verifyArgumentsNumber(a_argc);

		m_arguments.reserve(a_argc);  

		for (int i = 0; i < a_argc; ++i) 
		{
			m_arguments.emplace_back(a_argv[i]); 
		}

		// verifyFormat(m_arguments.back());

		initState = true;
		return true;
	}

	void printArguments() const // DBG:
	{
		print(L"Args count: %u \nArgs: ", m_arguments.size());

		for (auto i = 0; i < m_arguments.size(); ++i)
		{ 
			print(L"%s", m_arguments[i].c_str());
			(i < m_arguments.size() - 1) ? print(L", ") : print(L";");
		}

		print(L"\n");
	}

	static inline void print(const wchar_t* a_format, va_list a_args) { vwprintf(a_format, a_args); }

	static inline void print(const wchar_t* a_format, ...)
	{
		va_list _args;
		va_start(_args, a_format);
		print(a_format, _args);
		va_end(_args);
	}

	static __declspec(noreturn) void printError(const wchar_t* a_format, ...)
	{
		print(L"Error: ");

		va_list _args;
		va_start(_args, a_format);
		print(a_format, _args);
		va_end(_args);

		exit(EXIT_FAILURE);
	}

	const std::wstring& getPath()
	{
		if (!m_path.empty()) { return m_path; }

		if (!m_arguments.empty() && verifyArgumentsNumber(m_arguments.size(), s_minArgPathSize))
		{ 
			m_path =  m_arguments.back();

			if (m_path.size() < 2 || m_path[1] != L':')
			{
				WCHAR _currentDir[MAX_PATH];
				auto _currentDirLen = GetCurrentDirectoryW(MAX_PATH, (LPWSTR)_currentDir);

				if (_currentDirLen == 0 || _currentDirLen >= MAX_PATH)
				{
					printError(L"Failed to get current directory (path size: \"%u\")! \n", _currentDirLen);
				}

				if (_currentDir[_currentDirLen - 1] != L'\\') 
				{
					m_path = std::wstring(_currentDir) + L'\\' + m_path;
				}
				else 
				{
					m_path = std::wstring(_currentDir) + m_path;
				}
				
				if (GetFileAttributesW(m_path.c_str()) == INVALID_FILE_ATTRIBUTES)
				{
					printError(L"Path \"%s\" does not exist! \n", m_path.c_str());
				}
			}

			return m_path;
		}
		else
		{
			printError(L"No arguments! \n");
		}
	}

	const std::wstring& getCommand()
	{
		if (!m_command.empty()) { return m_command; }

		if (verifyArgumentsNumber(m_arguments.size(), s_minArgCmdSize))
		{
			m_command = m_arguments[1];
		}

		return m_command;
	}

	const std::wstring* getCommandArguments()
	{
		if (m_arguments.size() > s_cmdArgsOffset)
		{
			return &m_arguments[s_cmdArgsOffset]; // .exe + command name
		}

		return nullptr;
	}

	/// Line Tools

	static void printLine()
	{
		print(s_lineBuffer);
		s_lineBuffer[0] = L'\0';
		s_bufferPointer = 0;
	}

	static void appendToLine(const wchar_t* a_format, va_list a_args)
	{
		if (s_bufferPointer >= s_bufferSize) { return; }

		int _written = vswprintf(s_lineBuffer + s_bufferPointer,
			s_bufferSize - s_bufferPointer,
			a_format,
			a_args);

		if (_written > 0) { s_bufferPointer += _written; }
	}

	static void appendToLine(const wchar_t* a_format, ...)
	{
		va_list _args;
		va_start(_args, a_format);
		appendToLine(a_format, _args);
		va_end(_args);
	}

protected:
	static inline bool verifyArgumentsNumber(int a_argc, int a_minimum)
	{
		if (a_argc < a_minimum)
		{
			printError(L"Argument count (%u) is less than the minimum (%u)!", a_argc, s_minArgPathSize);
		}

		return true;
	}

	static inline bool verifyFormat(const std::wstring& a_path)
	{
		auto _size = a_path.size();

		if (_size >= 4 && wcscmp(&a_path.end()[-4], s_pdbFormat) == 0)
		{
			return true;
		}

		auto _begin = a_path.c_str();

		for (auto i = a_path.size(); i > 0; --i)
		{
			if (_begin[i] == L'.') { printError(L"Extension \"%s\" must be \".pdb\"! \n", &(_begin[i])); }
		}

		printError(L"No extension for \"%s\" (must be \".pdb\")!\n ", a_path.c_str());
	}

public:
	inline bool verifyPDBFormat() const
	{
		return verifyFormat(m_arguments.back());
	}

	static constexpr int s_executableOffset = 0;
	static constexpr int s_cmdOffset = 1;
	static constexpr int s_cmdArgsOffset = 2;
};