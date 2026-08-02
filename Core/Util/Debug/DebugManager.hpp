#pragma once

#include <Core/Util/Debug/DebugTools.hpp>

class DebugManager
{
public:
	static inline void WaitDebugger() { while (!IsDebuggerPresent()) {}; }
};