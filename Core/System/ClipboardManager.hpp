#pragma once

#include <windows.h>
#include <string>

class ClipboardManager
{
public:

    static bool copy(const std::wstring& a_text)
    {
        if (OpenClipboard(nullptr) == FALSE)
        {
            return false;
        }

        EmptyClipboard();

        const SIZE_T _size = (a_text.size() + 1) * sizeof(wchar_t);

        HGLOBAL _memory = GlobalAlloc(GMEM_MOVEABLE, _size);
        if (_memory == nullptr)
        {
            CloseClipboard();
            return false;
        }

        void* _data = GlobalLock(_memory);
        if (_data == nullptr)
        {
            GlobalFree(_memory);
            CloseClipboard();
            return false;
        }

        memcpy(_data, a_text.c_str(), _size);
        GlobalUnlock(_memory);

        if (SetClipboardData(CF_UNICODETEXT, _memory) == nullptr)
        {
            GlobalFree(_memory);
            CloseClipboard();
            return false;
        }

        CloseClipboard();
        return true;
    }
};