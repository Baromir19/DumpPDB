#pragma once

#include <Core/PdbToolset.hpp>

#include <Core/Config/SaveManager.hpp>
#include <Core/Util/Debug/DebugManager.hpp>
#include <Core/System/ConsoleManager.hpp>
#include <Core/Util/Container/Singleton.hpp>

#include <CLI/Application/Command/CommandManager.hpp>

class Application : public Singleton<Application>
{
    SET_SINGLETON_FRIEND(Application)

public:

    bool initialize(int a_argc, wchar_t* a_argv[])
    {
        static bool initState = false;
        if (initState)
            return true;

        if (!SaveManager::instance().initialize())
        {
            ConsoleManager::printError(L"Save system is not initialized!\n");
        }

        if (!ConsoleManager::instance().initialize(a_argc, a_argv))
        {
            ConsoleManager::printError(L"Console is not initialized!\n");
        }

        if (!CommandManager::instance().initialize(
                SaveManager::instance().commandConfig(), SaveManager::instance().dumpConfig()))
        {
            ConsoleManager::printError(L"Commands is not initialized!\n");
        }

        const auto& cmdString = ConsoleManager::instance().getCommand().c_str();
        const auto& cmd = CommandManager::instance().getCommand(cmdString, a_argc);

        if (cmd->getType() & cmd->s_executableMask && ConsoleManager::instance().verifyPDBFormat())
        {
            const auto& path = ConsoleManager::instance().getPath();

            if (!PdbToolset::instance().initialize(path))
            {
                ConsoleManager::printError(L"DIA is not initialized!\n");
            }
        }

        try
        {
            CommandManager::instance().executeCommand(cmd);
        }
        catch (const std::exception& e)
        {
            printf("Exception: %s\n", e.what());
        }
        catch (...)
        {
            printf("Unknown C++ exception\n");
        }

        initState = true;
        return true;
    }
};
