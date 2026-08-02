#include <Windows.h>

#include <Core/Config/SaveManager.hpp>
#include <Core/System/ConsoleManager.hpp>

#include <CLI/Application/Application.hpp>

#include <Core/Util/Error/DumpError.hpp>

int wmain(int argc, wchar_t* argv[]) 
{
    // DebugManager::WaitDebugger();

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
