#include <Windows.h>

#include "Application\Application.hpp"

int wmain(int argc, wchar_t* argv[]) 
{
    // DebugManager::WaitDebugger();

    Application::instance().initialize(argc, argv);

    return EXIT_SUCCESS;
}