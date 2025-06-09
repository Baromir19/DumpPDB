#include <Windows.h>

#include "Application\Application.hpp"

int wmain(int argc, wchar_t* argv[]) 
{
    // DebugManager::WaitDebugger();

    ConsoleManager::instance().initialize(argc, argv);
    ConsoleManager::instance().printArguments();
    const auto& _path = ConsoleManager::instance().getPath();
    ConsoleManager::instance().print(L"%s \n", _path.c_str());

    Application::instance().initialize(_path);

    return EXIT_SUCCESS;
}