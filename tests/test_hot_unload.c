#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {int ParryAhead,KeyCondition,ForestallLatency;float ForestallDistance,ParryDistance,RollDistance,BackstabRad,BackstabDist,BackEvadeRad,BackEvadeDist,RollAtkDist,RollAtkRad,AtkMult,Ass;short CopyActionKey;int BulletDodge,HitGuard,ParryStartup;} CONFIG;
typedef DWORD(WINAPI*ENTRY)(void*);
static unsigned count;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d (%s), Windows=%lu\n",__LINE__,#x,GetLastError());return 1;}++count;}while(0)
static const unsigned char b0[]={0x4c,0x8b,0xcf,0x41,0xb8,0x14,0,0,0,0x48,0x8b,0xd3};
static const unsigned char b1[]={0x48,0x89,0x5c,0x24,8,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20};
static const unsigned char b2[]={0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xfa};
static const unsigned char b3[]={0x48,0x8b,0x44,0xce,8,0x48,0x89,0x47,8,0xe9,0x47,0xdc,0xff,0xff};
static const unsigned char b4[]={0x83,0xfa,0x0d,0x77,8,0x48,0x63,0xc2,0x8b,0x44,0xc1,0x18,0xc3};
static const unsigned char b5[]={0x49,0x63,0xc0,0x8b,0x4c,0x81,0x58,0x48,0x8b,0xc2,0x89,0x0a,0xc3};
static const unsigned char b6[]={0x8b,0x97,0xd8,4,0,0,0x48,0x8b,0x8f,0xd0,4,0,0,0x44,0x8b,4,0x91};
static const unsigned char b7[]={0x49,0x8b,0x8e,0x90,0x1f,0,0,0x41,0xb1,1,0x4c,0x8d,0x45,0xb0,0x48,0x8b,0xd3,0x48,0x8b,0x89,0x98,0,0,0};
static const unsigned char b8[]={0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0x41,0x10,0x0f,0x29,0x74,0x24,0x20};
static const unsigned char nativeCall[]={0xe8,0xf7,1,0x0e,0};
static struct {uintptr_t site;const unsigned char*bytes;size_t size;} hooks[]={
 {0x14077e39e,b0,sizeof b0},{0x14078e700,b1,sizeof b1},{0x140d913e0,b2,sizeof b2},{0x142725191,b3,sizeof b3},{0x1405908e0,b4,sizeof b4},{0x14058a6f6,b5,sizeof b5},{0x14097b1aa,b6,sizeof b6},{0x1408df0fc,b7,sizeof b7},{0x140d92bd0,b8,sizeof b8}};
static HANDLE nativeEntered,nativeRelease;
static DWORD nativeTimeout=5000;
static uintptr_t receiveEntry;
static volatile LONG nativeResult;
typedef int (*RECEIVE_TEST)(void*,char*,int,char*,int);
static int NativeReceive(void*context,char*from,int id,char*buffer,int capacity)
{
    (void)context;(void)from;(void)id;(void)buffer;(void)capacity;
    SetEvent(nativeEntered);
    return WaitForSingleObject(nativeRelease,nativeTimeout)==WAIT_OBJECT_0?0:-1;
}
static DWORD WINAPI InvokeReceive(void*unused)
{
    (void)unused;
    InterlockedExchange(&nativeResult,((RECEIVE_TEST)receiveEntry)(0,0,0,0,0));
    return 0;
}
static DWORD WINAPI ReleaseNative(void*unused)
{
    (void)unused;Sleep(200);SetEvent(nativeRelease);return 0;
}
static void JumpTo(void*destination,uintptr_t target)
{
    unsigned char code[14]={0xff,0x25,0,0,0,0};
    memcpy(code+6,&target,8);memcpy(destination,code,sizeof code);
}
int wmain(int argc,wchar_t**argv)
{
    CHECK(argc>=2);
    puts("SELF-OWNED MOCK HOST: no access to the actual game process.");
    uintptr_t imageBase=(uintptr_t)GetModuleHandleW(0);
    DWORD old;
    for(unsigned i=0;i<9;++i){
        hooks[i].site+=imageBase-0x140000000;
        uintptr_t page=hooks[i].site&~(uintptr_t)0xffff;int fresh=1;
        for(unsigned j=0;j<i;++j)if((hooks[j].site&~(uintptr_t)0xffff)==page)fresh=0;
        if(fresh)CHECK(VirtualAlloc((void*)page,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)==(void*)page);
        CHECK(VirtualProtect((void*)page,0x10000,PAGE_READWRITE,&old));
        memcpy((void*)hooks[i].site,hooks[i].bytes,hooks[i].size);
        if(i==7)memcpy((void*)(imageBase+0x8df114),nativeCall,sizeof nativeCall);
        if(i==1)JumpTo((void*)(hooks[i].site+hooks[i].size),(uintptr_t)&NativeReceive);
        CHECK(VirtualProtect((void*)page,0x10000,PAGE_EXECUTE_READ,&old));
    }
    HMODULE bridge=0;int shared=argc>2;
    if(shared){
        bridge=LoadLibraryW(argv[2]);CHECK(bridge);
        CHECK(VirtualProtect((void*)hooks[1].site,hooks[1].size,PAGE_READWRITE,&old));
        memset((void*)hooks[1].site,0xcc,hooks[1].size);
        CHECK(VirtualProtect((void*)hooks[1].site,hooks[1].size,old,&old));
    }
    unsigned cycles=shared?2:3;
    for(unsigned cycle=0;cycle<cycles;++cycle){
        int timeoutCase=!shared&&cycle==2;
        HMODULE dll=LoadLibraryW(argv[1]);CHECK(dll);
        ENTRY start=(ENTRY)(uintptr_t)GetProcAddress(dll,"CopydogStart");
        ENTRY stop=(ENTRY)(uintptr_t)GetProcAddress(dll,"CopydogStop");
        ENTRY prepare=(ENTRY)(uintptr_t)GetProcAddress(dll,"CopydogPrepareUnload");
        ENTRY version=(ENTRY)(uintptr_t)GetProcAddress(dll,"CopydogUnloadApiVersion");
        ENTRY mode=(ENTRY)(uintptr_t)GetProcAddress(dll,"CopydogReceiveMode");
        ENTRY activity=(ENTRY)(uintptr_t)GetProcAddress(dll,"CopydogHookActivity");
        CHECK(start&&stop&&prepare&&version&&mode&&activity);CHECK(version(0)==1);
        CONFIG config={256,0,128,2,2.5,3.5,.8,1.5,.85,2,3,1.2,1,1,'Y',1,1,133};
        CHECK(start(&config)==0);CHECK(mode(0)==(shared?3u:1u));CHECK(activity(0)==0);
        unsigned char patch[32];memcpy(patch,(void*)hooks[0].site,hooks[0].size);
        CHECK(memcmp(patch,hooks[0].bytes,hooks[0].size));
        CHECK(stop(0)==0);CHECK(!memcmp(patch,(void*)hooks[0].site,hooks[0].size));
        if(cycle==0){
            CHECK(VirtualProtect((void*)hooks[0].site,hooks[0].size,PAGE_READWRITE,&old));
            *(unsigned char*)hooks[0].site^=1;
            CHECK(VirtualProtect((void*)hooks[0].site,hooks[0].size,old,&old));
            CHECK(prepare(0)==ERROR_REVISION_MISMATCH);
            CHECK(start(&config)==ERROR_BUSY);
            CHECK(memcmp(patch,(void*)hooks[0].site,hooks[0].size));
            CHECK(VirtualProtect((void*)hooks[0].site,hooks[0].size,PAGE_READWRITE,&old));
            memcpy((void*)hooks[0].site,patch,hooks[0].size);
            CHECK(VirtualProtect((void*)hooks[0].site,hooks[0].size,old,&old));
        }
        HANDLE caller=0,releaser=0;
        if(!shared){
            nativeEntered=CreateEventW(0,TRUE,FALSE,0);nativeRelease=CreateEventW(0,TRUE,FALSE,0);
            CHECK(nativeEntered&&nativeRelease);receiveEntry=hooks[1].site;
            nativeTimeout=timeoutCase?INFINITE:5000;
            caller=CreateThread(0,0,InvokeReceive,0,0,0);CHECK(caller);
            CHECK(WaitForSingleObject(nativeEntered,5000)==WAIT_OBJECT_0);CHECK(activity(0)==1);
            if(!timeoutCase){releaser=CreateThread(0,0,ReleaseNative,0,0,0);CHECK(releaser);}
        }
        ULONGLONG started=GetTickCount64();
        DWORD prepared=prepare(0);
        if(timeoutCase){
            CHECK(prepared==ERROR_TIMEOUT);CHECK(activity(0)==1);CHECK(start(&config)==ERROR_BUSY);
            HMODULE resident=0;
            CHECK(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)dll,&resident));
            CHECK(resident==dll);CHECK(SetEvent(nativeRelease));
            CHECK(WaitForSingleObject(caller,5000)==WAIT_OBJECT_0);
            prepared=prepare(0);
        }
        if(prepared)fprintf(stderr,"Prepare failed: code=%lu, activity=%lu, elapsed=%llu\n",prepared,activity(0),(unsigned long long)(GetTickCount64()-started));
        CHECK(prepared==0);
        if(!shared){
            CHECK(GetTickCount64()-started>=100);CHECK(activity(0)==0);
            CHECK(WaitForSingleObject(caller,5000)==WAIT_OBJECT_0);
            if(releaser)CHECK(WaitForSingleObject(releaser,5000)==WAIT_OBJECT_0);
            CHECK(nativeResult==0);
            CloseHandle(caller);if(releaser)CloseHandle(releaser);CloseHandle(nativeEntered);CloseHandle(nativeRelease);
        }
        CHECK(prepare(0)==0);CHECK(start(&config)==ERROR_BUSY);
        for(unsigned i=0;i<9;++i){
            if(shared&&i==1){CHECK(*(unsigned char*)hooks[i].site==0xcc);continue;}
            CHECK(!memcmp((void*)hooks[i].site,hooks[i].bytes,hooks[i].size));
            MEMORY_BASIC_INFORMATION info;CHECK(VirtualQuery((void*)hooks[i].site,&info,sizeof info)==sizeof info);
            CHECK(info.Protect==PAGE_EXECUTE_READ);
        }
        CHECK(FreeLibrary(dll));
        HMODULE remaining=0;
        CHECK(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)dll,&remaining));
    }
    if(shared){
        ENTRY calls=(ENTRY)(uintptr_t)GetProcAddress(bridge,"FixtureRegistrationCalls");
        CHECK(calls&&calls(0)==0);CHECK(FreeLibrary(bridge));
    }
    printf("PASS %u hot unload checks; shared_v1=%d; %u LoadLibrary/Prepare/FreeLibrary cycles in mock host\n",count,shared,cycles);
    return 0;
}
