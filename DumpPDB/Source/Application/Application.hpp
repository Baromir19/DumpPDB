#pragma once

#include <Core\PdbToolset.hpp>

#include <Application\Command\CommandManager.hpp>
#include <Application\Debug\DebugManager.hpp>
#include <Application\Console\ConsoleManager.hpp>

#include <Util\Container\Singleton.hpp>

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

            if (!PdbToolset::instance().initialize(_path))
            {
                ConsoleManager::printError(L"DIA is not initialized!\n");
            }
        }

        try
        {/*
            __try
            {*/
                // while (!IsDebuggerPresent()) {}
                CommandManager::instance().executeCommand(_cmd);
            /* }
            __except (
                [](EXCEPTION_POINTERS* p)
                {
                    printf(
                        "SEH exception: 0x%08X\n",
                        p->ExceptionRecord->ExceptionCode
                    );

                    return EXCEPTION_EXECUTE_HANDLER;
                }(GetExceptionInformation())
                    )
            {
            }*/
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
