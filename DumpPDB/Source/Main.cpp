#include <Windows.h>

#include "Application\Application.hpp"
#include "Application\Save\SaveManager.hpp"

#include "Test/Tests.hpp"

int wmain(int argc, wchar_t* argv[]) 
{
    // DebugManager::WaitDebugger();

    COMPILE_TEST

    Application::instance().initialize(argc, argv);

    // SaveManager::instance().save();

    return EXIT_SUCCESS;
}