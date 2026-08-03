#pragma once

#include <windows.h>
#include <string>

class ClipboardManager
{
public:

    static bool copy(const std::wstring& a_text)
    {
        if (!OpenClipboard(nullptr))
            return false;

        EmptyClipboard();

        const SIZE_T _size = (a_text.size() + 1) * sizeof(wchar_t);

        HGLOBAL _memory = GlobalAlloc(GMEM_MOVEABLE, _size);
        if (!_memory)
        {
            CloseClipboard();
            return false;
        }

        void* _data = GlobalLock(_memory);
        if (!_data)
        {
            GlobalFree(_memory);
            CloseClipboard();
            return false;
        }

        memcpy(_data, a_text.c_str(), _size);
        GlobalUnlock(_memory);

        if (!SetClipboardData(CF_UNICODETEXT, _memory))
        {
            GlobalFree(_memory);
            CloseClipboard();
            return false;
        }

        CloseClipboard();
        return true;
    }
};