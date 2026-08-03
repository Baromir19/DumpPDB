#pragma once

#include <Windows.h>

#include <Core/Util/Debug/DebugTools.hpp>

class DebugManager
{
public:

    static inline void WaitDebugger()
    {
        while (!IsDebuggerPresent())
        {
        };
    }
};