#pragma once

#include <Application\Debug\DebugTools.hpp>

class DebugManager
{
public:
	static inline void WaitDebugger() { while (!IsDebuggerPresent()) {}; }
};