#pragma once
#include <Windows.h>
#define COINIT_MULTITHREADED 0
inline HRESULT CoInitializeEx(void*,int){return 0;}
inline void CoUninitialize(){}
inline HRESULT CoCreateInstance(int,void*,int,int,void**){return 0;}
inline HRESULT NoRegCoCreate(const wchar_t*,int,int,void**){return 0;}
