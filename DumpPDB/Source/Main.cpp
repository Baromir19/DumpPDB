#include <Windows.h>

#include "Application\Application.hpp"
#include "Application\Save\SaveManager.hpp"

int wmain(int argc, wchar_t* argv[]) 
{
    // DebugManager::WaitDebugger();

    Application::instance().initialize(argc, argv);

    // SaveManager::instance().save();

    return EXIT_SUCCESS;
}