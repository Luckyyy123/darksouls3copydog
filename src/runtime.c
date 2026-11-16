#include "copydog.h"
#include "runtime.h"
#include "combat.h"
#include "control.h"
#include <wchar.h>
#include "build_id.h"
#include "receive_bridge.h"
#include "hook_lifetime.h"
__declspec(dllexport) const char CopydogBuildId[65]=COPYDOG_BUILD_ID;

volatile LONG exitThread=1;
__declspec(dllexport) volatile LONG CopydogEnabled=0;
static SRWLOCK ControlLock=SRWLOCK_INIT;
static HMODULE ThisModule;
static HANDLE WorkerHandle;
static DWORD WorkerId;
static volatile LONG LastFailure,Installed,UnloadRequested,UnloadPrepared;
extern char OnSendAttack[],OnWalk[];
extern char OnBulletObserve[],OnHitApply[],OnRollDirection[];
extern void*OnAction(void*,int*);
extern int OnGetCurrentMagic(int*,int);
extern int*OnGetCurrentGoods(int*,int*,int);
extern void ObserveReceivePacket(char*,int,char*,int);
typedef int (*RECEIVE)(void*,char*,int,char*,int);
static RECEIVE OriginalReceive;
static int SharedReceive;
static int HookEnabled(unsigned i){return i!=1||!SharedReceive;}
__declspec(dllexport) DWORD WINAPI CopydogReceiveMode(void*unused)
{(void)unused;return Installed?(SharedReceive?3u:1u):0u;}
__declspec(dllexport) DWORD WINAPI CopydogSharedReceiveCalls(void*unused)
{(void)unused;return 0;}
static int ReceiveHook(void*ctx,char*from,int id,char*buf,int capacity)
{
    COPYDOG_HOOK_SCOPE;
    int n=OriginalReceive(ctx,from,id,buf,capacity);
    if(CopydogEnabled&&n>0&&n<=capacity)ObserveReceivePacket(from,id,buf,n);
    return n;
}
typedef struct {
    uintptr_t site;
    unsigned count;
    const unsigned char*expected;
    void*handler;
    unsigned verifyCount;
} HOOK;
static const unsigned char SendBytes[]={0x4c,0x8b,0xcf,0x41,0xb8,0x14,0,0,0,0x48,0x8b,0xd3};
static const unsigned char ReceiveBytes[]={0x48,0x89,0x5c,0x24,8,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20};
static const unsigned char ActionBytes[]={0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xfa};
static const unsigned char WalkBytes[]={0x48,0x8b,0x44,0xce,8,0x48,0x89,0x47,8,0xe9,0x47,0xdc,0xff,0xff};
static const unsigned char MagicBytes[]={0x83,0xfa,0x0d,0x77,8,0x48,0x63,0xc2,0x8b,0x44,0xc1,0x18,0xc3};
static const unsigned char GoodsBytes[]={0x49,0x63,0xc0,0x8b,0x4c,0x81,0x58,0x48,0x8b,0xc2,0x89,0x0a,0xc3};
static const unsigned char BulletBytes[]={0x8b,0x97,0xd8,4,0,0,0x48,0x8b,0x8f,0xd0,4,0,0,0x44,0x8b,4,0x91};
static const unsigned char HitApplyBytes[]={0x49,0x8b,0x8e,0x90,0x1f,0,0,0x41,0xb1,1,0x4c,0x8d,0x45,0xb0,0x48,0x8b,0xd3,0x48,0x8b,0x89,0x98,0,0,0,0xe8,0xf7,1,0x0e,0};
static const unsigned char RollDirectionBytes[]={0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0x41,0x10,0x0f,0x29,0x74,0x24,0x20};
static HOOK Hooks[]={
    {0x14077e39e,sizeof SendBytes,SendBytes,OnSendAttack,0},
    {0x14078e700,sizeof ReceiveBytes,ReceiveBytes,ReceiveHook,0},
    {0x140d913e0,sizeof ActionBytes,ActionBytes,OnAction,0},
    {0x142725191,sizeof WalkBytes,WalkBytes,OnWalk,0},
    {0x1405908e0,sizeof MagicBytes,MagicBytes,OnGetCurrentMagic,0},
    {0x14058a6f6,sizeof GoodsBytes,GoodsBytes,OnGetCurrentGoods,0},
    {0x14097b1aa,sizeof BulletBytes,BulletBytes,OnBulletObserve,0},
    {0x1408df0fc,24,HitApplyBytes,OnHitApply,sizeof HitApplyBytes},
    {0x140d92bd0,sizeof RollDirectionBytes,RollDirectionBytes,OnRollDirection,0},
};
static DWORD PendingProtections[_countof(Hooks)];
static unsigned PendingProtectionMask;
static DWORD RestorePageProtections(void)
{
    for(unsigned i=_countof(Hooks);i;){
        --i;if(!(PendingProtectionMask&(1u<<i)))continue;
        DWORD ignored;
        if(!VirtualProtect((void*)CopydogAddress(Hooks[i].site),Hooks[i].count,
                           PendingProtections[i],&ignored))return GetLastError();
        PendingProtectionMask&=~(1u<<i);
    }
    return ERROR_SUCCESS;
}
static DWORD OpenHookPages(void)
{
    for(unsigned i=0;i<_countof(Hooks);++i)if(HookEnabled(i)){
        if(!VirtualProtect((void*)CopydogAddress(Hooks[i].site),Hooks[i].count,
                           PAGE_EXECUTE_READWRITE,&PendingProtections[i]))return GetLastError();
        PendingProtectionMask|=1u<<i;
    }
    return ERROR_SUCCESS;
}
/* No dynamically decoded/guessed instruction prefixes are accepted. */
static int SameBytes(const HOOK*h)
{
    unsigned char bytes[32];SIZE_T got;
    unsigned n=h->verifyCount?h->verifyCount:h->count;
    return n<=sizeof bytes&&ReadProcessMemory(GetCurrentProcess(),(void*)CopydogAddress(h->site),bytes,n,&got)&&got==n&&!memcmp(bytes,h->expected,n);
}
DWORD WINAPI CopydogCheckProcess(HANDLE process)
{
    IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;SIZE_T got;
    uintptr_t base=0;DWORD pid=GetProcessId(process);
    if(!pid)return GetLastError();
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snapshot==INVALID_HANDLE_VALUE)return GetLastError();
    MODULEENTRY32W entry={.dwSize=sizeof entry};
    if(Module32FirstW(snapshot,&entry))do{
        if(!_wcsicmp(entry.szModule,L"DarkSoulsIII.exe")){base=(uintptr_t)entry.modBaseAddr;break;}
    }while(Module32NextW(snapshot,&entry));
    CloseHandle(snapshot);if(!base)return ERROR_MOD_NOT_FOUND;
    if(!ReadProcessMemory(process,(void*)base,&dos,sizeof dos,&got)||got!=sizeof dos)return ERROR_PARTIAL_COPY;
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>0x100000)return ERROR_BAD_EXE_FORMAT;
    if(!ReadProcessMemory(process,(void*)(base+(uintptr_t)dos.e_lfanew),&nt,sizeof nt,&got)||got!=sizeof nt)return ERROR_PARTIAL_COPY;
    if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64)return ERROR_BAD_EXE_FORMAT;
    uintptr_t bridge=0;DWORD bridgeError=CopydogFindReceiveBridge(process,&bridge);
    if(bridgeError)return bridgeError;
    for(unsigned i=0;i<_countof(Hooks);++i){unsigned char bytes[32];
        if(i==1&&bridge)continue;
        unsigned n=Hooks[i].verifyCount?Hooks[i].verifyCount:Hooks[i].count;
        if(!ReadProcessMemory(process,(void*)(base+Hooks[i].site-COPYDOG_PREFERRED_BASE),bytes,n,&got)||got!=n)return ERROR_PARTIAL_COPY;
        if(memcmp(bytes,Hooks[i].expected,n))return ERROR_REVISION_MISMATCH;
    }
    return 0;
}
static void Jump14(unsigned char*p,uintptr_t target)
{
    p[0]=0xff;p[1]=0x25;memset(p+2,0,4);memcpy(p+6,&target,8);
}
static void PatchBytes(const HOOK*h,unsigned char*out)
{
    memset(out,0x90,h->count);
    if(h->count>=14)Jump14(out,(uintptr_t)h->handler);
    else {out[0]=0x48;out[1]=0xb8;memcpy(out+2,&h->handler,8);out[10]=0xff;out[11]=0xe0;}
}
static int InHook(uintptr_t ip)
{
    for(unsigned i=0;i<_countof(Hooks);++i)if(HookEnabled(i)&&ip>=CopydogAddress(Hooks[i].site)&&ip<CopydogAddress(Hooks[i].site)+Hooks[i].count)return 1;
    return 0;
}
typedef LONG (NTAPI*NT_NEXT_THREAD)(HANDLE,HANDLE,ACCESS_MASK,ULONG,ULONG,PHANDLE);
static NT_NEXT_THREAD NextThread;
typedef struct {HANDLE handles[1024];DWORD ids[1024];unsigned count,suspended;} PAUSED;
static void ResumeAll(PAUSED*p)
{
    while(p->suspended)ResumeThread(p->handles[--p->suspended]);
    while(p->count)CloseHandle(p->handles[--p->count]);
}
static int InOwnedCode(uintptr_t ip)
{
    uintptr_t base=(uintptr_t)ThisModule;
    const IMAGE_DOS_HEADER*dos=(const IMAGE_DOS_HEADER*)base;
    const IMAGE_NT_HEADERS64*nt=(const IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    uintptr_t trampoline=(uintptr_t)OriginalReceive;
    return (ip>=base&&ip-base<nt->OptionalHeader.SizeOfImage)||
           (trampoline&&ip>=trampoline&&ip-trampoline<64);
}
static int CollectThreads(PAUSED*p,int suspendNew)
{
    HANDLE cursor=0;DWORD self=GetCurrentThreadId();int success=0;
    for(;;){
        HANDLE next=0;
        LONG status=NextThread(GetCurrentProcess(),cursor,
            THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION|SYNCHRONIZE,0,0,&next);
        if(cursor)CloseHandle(cursor);
        cursor=next;
        if((DWORD)status==0x8000001au){success=1;break;}
        if(status<0||!cursor)break;
        if(WaitForSingleObject(cursor,0)==WAIT_OBJECT_0)continue;
        DWORD id=GetThreadId(cursor);if(!id)break;
        if(id==self)continue;
        unsigned k;for(k=0;k<p->count;++k)if(p->ids[k]==id)break;
        if(k<p->count)continue;
        if(p->count==_countof(p->handles))break;
        HANDLE owned=0;
        if(!DuplicateHandle(GetCurrentProcess(),cursor,GetCurrentProcess(),&owned,0,FALSE,DUPLICATE_SAME_ACCESS))break;
        p->ids[p->count]=id;p->handles[p->count++]=owned;
        if(suspendNew){
            if(SuspendThread(owned)==(DWORD)-1){
                if(WaitForSingleObject(owned,0)==WAIT_OBJECT_0){CloseHandle(owned);--p->count;continue;}
                break;
            }
            ++p->suspended;
        }
    }
    if(cursor)CloseHandle(cursor);
    return success;
}
static int PauseOthers(PAUSED*p,int finalDrain)
{
    p->count=p->suspended=0;
    if(!NextThread){
        FARPROC address=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtGetNextThread");
        if(!address)return 0;
        memcpy(&NextThread,&address,sizeof NextThread);
    }
    if(!CollectThreads(p,0))goto fail;
    while(p->suspended<p->count){
        HANDLE h=p->handles[p->suspended];
        if(SuspendThread(h)==(DWORD)-1){
            if(WaitForSingleObject(h,0)!=WAIT_OBJECT_0)goto fail;
            CloseHandle(h);--p->count;
            for(unsigned j=p->suspended;j<p->count;++j){p->handles[j]=p->handles[j+1];p->ids[j]=p->ids[j+1];}
            continue;
        }
        ++p->suspended;
    }
    /* Close the snapshot/creation gap without heap-allocating snapshot APIs
       while other threads may be suspended inside allocator/loader locks. */
    for(unsigned pass=0;pass<8;++pass){
        unsigned before=p->count;
        if(!CollectThreads(p,1))goto fail;
        if(p->count!=before)continue;
        for(unsigned k=0;k<p->count;++k){
            if(WaitForSingleObject(p->handles[k],0)==WAIT_OBJECT_0)continue;
            CONTEXT c={.ContextFlags=CONTEXT_CONTROL};
            if(!GetThreadContext(p->handles[k],&c)||InHook(c.Rip)||
               (finalDrain&&InOwnedCode(c.Rip)))goto fail;
        }
        if(finalDrain&&CopydogActiveHooks()!=0)goto fail;
        return 1;
    }
fail:
    ResumeAll(p);
    return 0;
}
static DWORD InstallHooks(void)
{
    uintptr_t bridge=0;DWORD bridgeError=CopydogFindReceiveBridge(GetCurrentProcess(),&bridge);
    if(bridgeError)return bridgeError;
    SharedReceive=bridge!=0;
    for(unsigned i=0;i<_countof(Hooks);++i)if(HookEnabled(i)&&!SameBytes(&Hooks[i]))return ERROR_REVISION_MISMATCH;
    unsigned char*trampoline=0;
    if(!SharedReceive){
    trampoline=VirtualAlloc(0,64,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!trampoline)return GetLastError();
    memcpy(trampoline,ReceiveBytes,sizeof ReceiveBytes);
    Jump14(trampoline+sizeof ReceiveBytes,CopydogAddress(0x14078e714));
    DWORD old;
    if(!VirtualProtect(trampoline,64,PAGE_EXECUTE_READ,&old)){DWORD e=GetLastError();VirtualFree(trampoline,0,MEM_RELEASE);return e;}
    FlushInstructionCache(GetCurrentProcess(),trampoline,64);
    OriginalReceive=(RECEIVE)trampoline;
    }
    /* V1 observers cannot be unregistered and pin their callback owner. Leave
       that optional observation unavailable, preserving the provider's hook. */
    PAUSED paused;
    if(!PauseOthers(&paused,0)){OriginalReceive=0;if(trampoline)VirtualFree(trampoline,0,MEM_RELEASE);return ERROR_BUSY;}
    DWORD status=ERROR_SUCCESS;
    for(unsigned i=0;i<_countof(Hooks);++i)if(HookEnabled(i)&&!SameBytes(&Hooks[i])){status=ERROR_REVISION_MISMATCH;goto done;}
    status=OpenHookPages();if(status)goto done;
    for(unsigned i=0;i<_countof(Hooks);++i){if(!HookEnabled(i))continue;unsigned char patch[32];PatchBytes(&Hooks[i],patch);memcpy((void*)CopydogAddress(Hooks[i].site),patch,Hooks[i].count);}
    FlushInstructionCache(GetCurrentProcess(),0,0);
    Installed=1;
done:
    {DWORD protectionStatus=RestorePageProtections();if(!status)status=protectionStatus;}
    ResumeAll(&paused);
    if(!Installed){OriginalReceive=0;if(trampoline)VirtualFree(trampoline,0,MEM_RELEASE);}
    return status;
}
static int SupportedHost(void)
{
    wchar_t path[32768];DWORD n=GetModuleFileNameW(0,path,_countof(path));
    if(!n||n>=_countof(path))return 0;
    wchar_t*name=wcsrchr(path,L'\\');name=name?name+1:path;
    if(_wcsicmp(name,L"DarkSoulsIII.exe"))return 0;
    uintptr_t base=(uintptr_t)GetModuleHandleW(0);
    
    IMAGE_DOS_HEADER*dos=(IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS64*nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64)return 0;
    if(!Installed)CopydogBindImage(base,nt->OptionalHeader.SizeOfImage);
    return 1;
}
/* This filter runs only inside the worker's native SEH scope. */
LONG CopydogWorkerFaultFilter(PEXCEPTION_POINTERS e,void* frame)
{
    (void)frame;
    if(!e || !e->ExceptionRecord || GetCurrentThreadId()!=WorkerId ||
       (e->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION &&
        e->ExceptionRecord->ExceptionCode!=EXCEPTION_IN_PAGE_ERROR))return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t ip=(uintptr_t)e->ExceptionRecord->ExceptionAddress,base=(uintptr_t)ThisModule;
    IMAGE_DOS_HEADER*dos=(IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS64*nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    if(ip<base || ip>=base+nt->OptionalHeader.SizeOfImage)return EXCEPTION_CONTINUE_SEARCH;
    LastFailure=ERROR_INVALID_ACCESS;CopydogEnabled=0;exitThread=1;
    return EXCEPTION_EXECUTE_HANDLER;
}
extern DWORD CopydogRunPollingGuarded(void* argument);
static DWORD WINAPI Worker(void*unused)
{
    (void)unused;WorkerId=GetCurrentThreadId();
    HANDLE cadenceTimer=CreateWaitableTimerExW(NULL,NULL,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_MODIFY_STATE|SYNCHRONIZE);
    (void)CopydogRunPollingGuarded(cadenceTimer);
    if(cadenceTimer)CloseHandle(cadenceTimer);
    CopyAction=0;ReleaseCopydogKeys();return 0;
}
DWORD WINAPI CopydogStart(void*configuration)
{
    DWORD result=0;
    AcquireSRWLockExclusive(&ControlLock);
    if(UnloadRequested||PendingProtectionMask){result=ERROR_BUSY;goto done;}
    if(!SupportedHost()){result=ERROR_REVISION_MISMATCH;goto done;}
    if(WorkerHandle){result=ERROR_ALREADY_INITIALIZED;goto done;}
    if(!Installed){
        COPYDOG_CONFIG candidate;SIZE_T n;
        if(!configuration||!ReadProcessMemory(GetCurrentProcess(),configuration,&candidate,sizeof candidate,&n)||n!=sizeof candidate||!ValidCopydogConfig(&candidate)){result=ERROR_INVALID_PARAMETER;goto done;}
        Config=candidate; /* The loader validates every field before this call. */
        result=InstallHooks();if(result)goto done;
    }else {
        for(unsigned i=0;i<_countof(Hooks);++i){unsigned char expected[32],actual[32];SIZE_T n;
            if(!HookEnabled(i))continue;
            PatchBytes(&Hooks[i],expected);
            if(!ReadProcessMemory(GetCurrentProcess(),(void*)CopydogAddress(Hooks[i].site),actual,Hooks[i].count,&n)||n!=Hooks[i].count||memcmp(expected,actual,Hooks[i].count)){result=ERROR_REVISION_MISMATCH;goto done;}
            unsigned tail=Hooks[i].verifyCount>Hooks[i].count?Hooks[i].verifyCount-Hooks[i].count:0;
            if(tail&&(!ReadProcessMemory(GetCurrentProcess(),(void*)(CopydogAddress(Hooks[i].site)+Hooks[i].count),actual,tail,&n)||n!=tail||memcmp(actual,Hooks[i].expected+Hooks[i].count,tail))){result=ERROR_REVISION_MISMATCH;goto done;}
        }
    }
    CombatClearBulletThreat();CopydogClearRollDirection();exitThread=0;LastFailure=0;CopydogEnabled=1;
    WorkerHandle=CreateThread(0,0,Worker,0,0,&WorkerId);
    if(!WorkerHandle){result=GetLastError();CopydogEnabled=0;exitThread=1;}
done:
    ReleaseSRWLockExclusive(&ControlLock);return result;
}
static DWORD StopWorker(void)
{
    DWORD result=0;
    CopydogEnabled=0;exitThread=1;CopyAction=0;CombatClearBulletThreat();CopydogClearRollDirection();
    if(WorkerHandle){
        DWORD r=WaitForSingleObject(WorkerHandle,10000);
        if(r==WAIT_OBJECT_0){CloseHandle(WorkerHandle);WorkerHandle=0;CopyAction=0;ReleaseCopydogKeys();}
        else result=r==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError();
    }
    if(!result){ReleaseCopydogKeys();if(!CopydogInputsReleased())result=ERROR_WRITE_FAULT;}
    return result;
}
DWORD WINAPI CopydogStop(void*unused)
{
    (void)unused;
    AcquireSRWLockExclusive(&ControlLock);
    DWORD result=StopWorker();
    ReleaseSRWLockExclusive(&ControlLock);return result;
}
static int OwnedPatch(const HOOK*h)
{
    unsigned char patch[32],actual[32];SIZE_T got=0;
    unsigned n=h->verifyCount?h->verifyCount:h->count;
    PatchBytes(h,patch);
    if(n>sizeof actual||!ReadProcessMemory(GetCurrentProcess(),(void*)CopydogAddress(h->site),
                                         actual,n,&got)||got!=n)return 0;
    return !memcmp(actual,patch,h->count)&&
           (n==h->count||!memcmp(actual+h->count,h->expected+h->count,n-h->count));
}
static DWORD RestoreHooks(ULONGLONG deadline)
{
    if(!Installed&&!PendingProtectionMask)return 0;
    PAUSED paused;
    while(!PauseOthers(&paused,0)){
        if(GetTickCount64()>=deadline)return ERROR_BUSY;
        Sleep(2);
    }
    DWORD result=RestorePageProtections();
    if(result)goto done;
    if(!Installed)goto done;
    /* Never overwrite a newer/third-party hook, including the untouched tail. */
    for(unsigned i=0;i<_countof(Hooks);++i)if(HookEnabled(i)&&!OwnedPatch(&Hooks[i])){
        result=ERROR_REVISION_MISMATCH;goto done;
    }
    result=OpenHookPages();if(result)goto done;
    for(unsigned i=0;i<_countof(Hooks);++i)if(HookEnabled(i))
        memcpy((void*)CopydogAddress(Hooks[i].site),Hooks[i].expected,Hooks[i].count);
    FlushInstructionCache(GetCurrentProcess(),0,0);
    Installed=0;
done:
    {DWORD protectionStatus=RestorePageProtections();if(!result)result=protectionStatus;}
    ResumeAll(&paused);
    return result;
}
DWORD WINAPI CopydogUnloadApiVersion(void*unused)
{
    (void)unused;return 1;
}
DWORD WINAPI CopydogHookActivity(void*unused)
{
    (void)unused;return (DWORD)CopydogActiveHooks();
}
DWORD WINAPI CopydogPrepareUnload(void*unused)
{
    (void)unused;DWORD result=0;
    ULONGLONG deadline=GetTickCount64()+14000;
    AcquireSRWLockExclusive(&ControlLock);
    UnloadRequested=1;
    if(UnloadPrepared)goto done;
    result=StopWorker();if(result)goto done;
    result=RestoreHooks(deadline);if(result)goto done;
    for(;;){
        if(GetTickCount64()>=deadline){result=ERROR_TIMEOUT;goto done;}
        if(CopydogActiveHooks()==0){
            PAUSED paused;
            if(PauseOthers(&paused,1)){
                /* Counts cover calls into native code; RIP checks cover the
                   small entry/epilogue intervals outside the count itself. */
                if(OriginalReceive&&!VirtualFree((void*)OriginalReceive,0,MEM_RELEASE))result=GetLastError();
                else {OriginalReceive=0;SharedReceive=0;UnloadPrepared=1;}
                ResumeAll(&paused);
                goto done;
            }
        }
        Sleep(2);
    }
done:
    ReleaseSRWLockExclusive(&ControlLock);return result;
}
DWORD WINAPI CopydogCopyStatus(void*unused){(void)unused;return CopydogEnabled&&CopyAction?1:0;}
DWORD WINAPI CopydogStatus(void*unused)
{
    (void)unused;
    return LastFailure?0x80000000u|(DWORD)LastFailure:(CopydogEnabled?1u:0u);
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*reserved)
{
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH){ThisModule=module;DisableThreadLibraryCalls(module);}
    return TRUE;
}
