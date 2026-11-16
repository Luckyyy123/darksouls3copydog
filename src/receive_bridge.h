#pragma once
#include <windows.h>
#include <stdint.h>
typedef void (WINAPI *COPYDOG_RECEIVE_OBSERVER)(void*,uint32_t,const unsigned char*,uint32_t);
typedef DWORD (WINAPI *COPYDOG_REGISTER_OBSERVER)(COPYDOG_RECEIVE_OBSERVER);
DWORD CopydogFindReceiveBridge(HANDLE process,uintptr_t* address);
