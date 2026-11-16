#ifndef COPYDOG_RUNTIME_H
#define COPYDOG_RUNTIME_H
#include <windows.h>
#include <stdint.h>
#include <stdatomic.h>
typedef struct _CONFIG {
    int ParryAhead,KeyCondition,ForestallLatency;
    float ForestallDistance,ParryDistance,RollDistance,BackstabRad,BackstabDist;
    float BackEvadeRad,BackEvadeDist,RollAtkDist,RollAtkRad,AtkMult,Ass;
    short CopyActionKey;
    int BulletDodge,HitGuard;
    int ParryStartup;
} COPYDOG_CONFIG;
extern COPYDOG_CONFIG Config;
extern volatile LONG exitThread,CopydogEnabled;
extern _Atomic char CopyAction;
DWORD WINAPI PollingThreadProc(void*);
void ReleaseCopydogKeys(void);
int CopydogInputsReleased(void);
int ParseCopydogConfig(const wchar_t*,COPYDOG_CONFIG*);
int ValidCopydogConfig(const COPYDOG_CONFIG*);
__declspec(dllexport) DWORD WINAPI CopydogCheckProcess(HANDLE);
__declspec(dllexport) DWORD WINAPI CopydogStart(void*);
__declspec(dllexport) DWORD WINAPI CopydogStop(void*);
__declspec(dllexport) DWORD WINAPI CopydogPrepareUnload(void*);
__declspec(dllexport) DWORD WINAPI CopydogUnloadApiVersion(void*);
__declspec(dllexport) DWORD WINAPI CopydogHookActivity(void*);
__declspec(dllexport) DWORD WINAPI CopydogStatus(void*);
__declspec(dllexport) DWORD WINAPI CopydogCopyStatus(void*);
#endif
