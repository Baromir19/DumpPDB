#pragma once

#include <cassert>

#ifdef _DEBUG
	#define DEBUG_ASSERT(expression) assert(expression)
#else
	#define DEBUG_ASSERT(expression) if (!(expression)) ConsoleManager::printError(L ## #expression)
#endif