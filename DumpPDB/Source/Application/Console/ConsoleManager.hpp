#pragma once

#include <vector>
#include <string>
#include <cstdarg>

#include "..\..\Util\Container\Singleton.hpp"

class ConsoleManager : public Singleton<ConsoleManager>
{
	SET_SINGLETON_FRIEND(ConsoleManager)

protected:
	std::vector<std::wstring> m_arguments;
	std::wstring m_path;

	static constexpr wchar_t s_pdbFormat[] = L".pdb";

	static constexpr int s_minArgSize = 3; // Program path (.exe) + call type + .pdb path

public:
	bool initialize(int a_argc, wchar_t* a_argv[])
	{
		static bool initState = false;
		if (initState) return true;

		verifyArgumentsNumber(a_argc);

		m_arguments.reserve(a_argc);  

		for (int i = 0; i < a_argc; ++i) 
		{
			m_arguments.emplace_back(a_argv[i]); 
		}

		verifyFormat(m_arguments.back());

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

		if (!m_arguments.empty() && verifyArgumentsNumber(m_arguments.size()))
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

protected:
	static inline bool verifyArgumentsNumber(int a_argc)
	{
		if (a_argc < s_minArgSize)
		{
			printError(L"Argument count (%u) is less than the minimum (%u)!", a_argc, s_minArgSize);
		}

		return true;
	}

	static inline bool verifyFormat(const std::wstring& a_path)
	{
		auto _size = a_path.size();

		if (_size >= 4 && *(__int64*)&(a_path.end()[-4]) == *(__int64*)s_pdbFormat)
		{
			return true;
		}
		else
		{
			_size >= 4
				? printError(L"Unknown format (%s)!\n", &(a_path.end()[-4]))
				: printError(L"Wrong name \"%s\" size (%u < 4)\n", a_path.c_str(), _size);
		}
	}
};