#pragma once

#include "..\Util\Container\Singleton.hpp"

#include "DIA\DiaManager.hpp"
#include "Console\ConsoleManager.hpp"
#include "Debug\DebugManager.hpp"

class Application : public Singleton<Application>
{
    SET_SINGLETON_FRIEND(Application)

protected:

public:
    bool initialize(const std::wstring& a_pdbPath)
    {
        static bool initState = false;
        if (initState) return true;

        if (!DiaManager::instance().initialize(a_pdbPath))
        {
            ConsoleManager::printError(L"DIA is not initializated!");
        }

        initState = true;
        return true;
    }
};