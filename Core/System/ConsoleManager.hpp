#pragma once

#include <Windows.h>

#include <vector>
#include <string>
#include <cstdarg>

#include <Core/Util/Container/Singleton.hpp>
#include <Core/Util/Error/DumpError.hpp>

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
        if (initState)
            return true;

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

    static inline void print(const wchar_t* a_format, va_list a_args)
    {
        vfwprintf(stdout, a_format, a_args);
    }

    static inline void print(const wchar_t* a_format, ...)
    {
        va_list args;
        va_start(args, a_format);
        print(a_format, args);
        va_end(args);
    }

    [[noreturn]]
    static void printError(const wchar_t* a_format, ...)
    {
        wchar_t buffer[0x2000];
        buffer[0] = L'\0';

        va_list args;
        va_start(args, a_format);
        vswprintf(buffer, 0x2000, a_format, args);
        va_end(args);

        std::wstring msg = L"Error: ";
        msg += buffer;

        throw DumpError(msg);
    }

    static bool setCursorNoDiscard(int a_pos, int a_repeatTime = 10, bool a_tabulation = true)
    {
        while (!setCursor(a_pos, a_tabulation) && a_repeatTime--)
        {
            a_pos += a_pos / 1.5;
        }

        return true;
    }

    static bool setCursor(int a_pos, bool a_tabulation = true)
    {
        if (!a_tabulation)
        {
            return true;
        }

        int ret = false;

        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        SHORT currentX = csbi.dwCursorPosition.X;

        COORD newPos = csbi.dwCursorPosition;

        const SHORT padTo = std::max(a_pos, currentX + 1);
        newPos.X = padTo;

        if (padTo == a_pos)
        {
            ret = true;
        }

        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), newPos);

        return ret;
    }

    const std::wstring& getPath()
    {
        if (!m_path.empty())
        {
            return m_path;
        }

        if (!m_arguments.empty() && verifyArgumentsNumber(m_arguments.size(), s_minArgPathSize))
        {
            m_path = m_arguments.back();

            if (m_path.size() < 2 || m_path[1] != L':')
            {
                WCHAR currentDir[MAX_PATH];
                auto currentDirLen = GetCurrentDirectoryW(MAX_PATH, (LPWSTR)currentDir);

                if (currentDirLen == 0 || currentDirLen >= MAX_PATH)
                {
                    printError(
                        L"Failed to get current directory (path size: \"%u\")! \n", currentDirLen);
                }

                if (currentDir[currentDirLen - 1] != L'\\')
                {
                    m_path = std::wstring(currentDir) + L'\\' + m_path;
                }
                else
                {
                    m_path = std::wstring(currentDir) + m_path;
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
            return m_path;
        }
    }

    const std::wstring& getCommand()
    {
        if (!m_command.empty())
        {
            return m_command;
        }

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

    int getCommandArgumentCount() const
    {
        const int total = static_cast<int>(m_arguments.size());
        return total > s_cmdArgsOffset ? total - s_cmdArgsOffset : 0;
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
        if (s_bufferPointer >= s_bufferSize)
        {
            return;
        }

        int written = vswprintf(
            s_lineBuffer + s_bufferPointer, s_bufferSize - s_bufferPointer, a_format, a_args);

        if (written > 0)
        {
            s_bufferPointer += written;
        }
    }

    static void appendToLine(const wchar_t* a_format, ...)
    {
        va_list args;
        va_start(args, a_format);
        appendToLine(a_format, args);
        va_end(args);
    }

protected:

    static inline bool verifyArgumentsNumber(int a_argc, int a_minimum)
    {
        if (a_argc < a_minimum)
        {
            printError(
                L"Argument count (%u) is less than the minimum (%u)!", a_argc, s_minArgPathSize);
        }

        return true;
    }

    static inline bool verifyFormat(const std::wstring& a_path)
    {
        auto size = a_path.size();

        if (size >= 4 && wcscmp(&a_path.end()[-4], s_pdbFormat) == 0)
        {
            return true;
        }

        auto begin = a_path.c_str();

        for (auto i = a_path.size(); i > 0; --i)
        {
            if (begin[i] == L'.')
            {
                printError(L"Extension \"%s\" must be \".pdb\"! \n", &(begin[i]));
            }
        }

        printError(L"No extension for \"%s\" (must be \".pdb\")!\n ", a_path.c_str());

        return false;
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