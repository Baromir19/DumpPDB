#include <Windows.h>

BOOL APIENTRY DllMain(HMODULE, DWORD a_reason, LPVOID)
{
    switch (a_reason)
    {
    case DLL_PROCESS_ATTACH:
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}