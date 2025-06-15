#pragma once

#include "..\Util\Container\Singleton.hpp"

#include "DIA\DiaManager.hpp"
#include "Console\ConsoleManager.hpp"
#include "Debug\DebugManager.hpp"

#include "Command\CommandManager.hpp"

class Application : public Singleton<Application>
{
    SET_SINGLETON_FRIEND(Application)

protected:

public:
    bool initialize(int a_argc, wchar_t* a_argv[])
    {
        static bool initState = false;
        if (initState) return true;
        
        if (!ConsoleManager::instance().initialize(a_argc, a_argv))
        {
            ConsoleManager::printError(L"Console is not initialized!\n");
        }

        if (!CommandManager::instance().initialize())
        {
            ConsoleManager::printError(L"Commands is not initialized!\n");
        }

        const auto& _cmdString = ConsoleManager::instance().getCommand().c_str();
        const auto& _cmd = CommandManager::instance().getCommand(_cmdString, a_argc);

        if (_cmd->getType() & _cmd->s_executableMask 
            && ConsoleManager::instance().verifyPDBFormat())
        {
            const auto& _path = ConsoleManager::instance().getPath();

            if (!DiaManager::instance().initialize(_path))
            {
                ConsoleManager::printError(L"DIA is not initialized!\n");
            }
        }

        CommandManager::instance().executeCommand(_cmd);

        initState = true;
        return true;
    }
};