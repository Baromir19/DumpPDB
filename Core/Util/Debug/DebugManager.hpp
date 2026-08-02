#pragma once

#include <Core\Debug\DebugTools.hpp>

class DebugManager
{
public:
	static inline void WaitDebugger() { while (!IsDebuggerPresent()) {}; }
};