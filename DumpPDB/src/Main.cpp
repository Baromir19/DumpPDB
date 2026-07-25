#include <Windows.h>

#include <Application\Application.hpp>
#include <Application\Save\SaveManager.hpp>
#include <Application\IO\ConsoleManager.hpp>

#include <Util\Error\DumpError.hpp>

#include <Test/Tests.hpp>

int wmain(int argc, wchar_t* argv[]) 
{
    // DebugManager::WaitDebugger();

    COMPILE_TEST

    try
    {
        Application::instance().initialize(argc, argv);
    }
    catch (const DumpError& a_error)
    {
        ConsoleManager::print(L"%s\n", a_error.wideMessage().c_str());
        return EXIT_FAILURE;
    }
    catch (const std::exception& a_error)
    {
        ConsoleManager::print(L"Unexpected error: %S\n", a_error.what());
        return EXIT_FAILURE;
    }

    // SaveManager::instance().save();

    return EXIT_SUCCESS;
}
