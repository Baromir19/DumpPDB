#include <Windows.h>

// NOLINTBEGIN(modernize-use-trailing-return-type, modernize-avoid-c-arrays)

BOOL APIENTRY DllMain(HMODULE a_module, DWORD a_reason, LPVOID a_reserved)
{
    switch (a_reason)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_PROCESS_DETACH:
    default:
        break;
    }

    return TRUE;
}

// NOLINTEND(modernize-use-trailing-return-type, modernize-avoid-c-arrays)