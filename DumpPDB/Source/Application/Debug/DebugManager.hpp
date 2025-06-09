#pragma once

#include "DebugTools.hpp"

class DebugManager
{
public:
	static inline void WaitDebugger() { while (!IsDebuggerPresent()) {}; }
};