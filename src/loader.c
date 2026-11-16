#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "runtime.h"
#include "combat.h"
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
static DWORD QueryModule(DWORD pid,const wchar_t*name,HMODULE*module,wchar_t*path)
{
    *module=0;
    HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(s==INVALID_HANDLE_VALUE)return GetLastError();
    MODULEENTRY32W e={.dwSize=sizeof e};DWORD result=ERROR_SUCCESS;
    if(!Module32FirstW(s,&e)){result=GetLastError();if(result==ERROR_NO_MORE_FILES)result=ERROR_SUCCESS;}
    else {
        do if(!_wcsicmp(e.szModule,name)){
            if(*module){result=ERROR_DUP_NAME;break;}
            *module=e.hModule;if(path)wcscpy(path,e.szExePath);
        }while(Module32NextW(s,&e));
        if(!result&&GetLastError()!=ERROR_NO_MORE_FILES)result=GetLastError();
    }
    CloseHandle(s);return result;
}
static HMODULE FindModule(DWORD pid,const wchar_t*name,wchar_t*path)
{
    HMODULE found=0;return QueryModule(pid,name,&found,path)==ERROR_SUCCESS?found:0;
}
static DWORD FindGame(void)
{
    HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);DWORD pid=0;
    if(s==INVALID_HANDLE_VALUE)return 0;
    PROCESSENTRY32W e={.dwSize=sizeof e};
    if(Process32FirstW(s,&e))do if(!_wcsicmp(e.szExeFile,L"DarkSoulsIII.exe")){if(pid){pid=0;break;}pid=e.th32ProcessID;}while(Process32NextW(s,&e));
    CloseHandle(s);return pid;
}
static DWORD CallRemote(HANDLE process,uintptr_t entry,void*parameter,int*completed,DWORD*exitCode)
{
    *completed=1;*exitCode=0;
    HANDLE t=CreateRemoteThread(process,0,0,(LPTHREAD_START_ROUTINE)entry,parameter,0,0);
    if(!t)return GetLastError();
    *completed=0;
    DWORD result=0,wait=WaitForSingleObject(t,15000);
    if(wait!=WAIT_OBJECT_0)result=wait==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError();
    else {*completed=1;if(!GetExitCodeThread(t,exitCode))result=GetLastError();}
    CloseHandle(t);return result;
}
static int ReadExact(HANDLE p,uintptr_t address,void*data,SIZE_T size)
{
    SIZE_T got;return ReadProcessMemory(p,(void*)address,data,size,&got)&&got==size;
}
static int ImageRange(DWORD size,DWORD rva,SIZE_T count)
{return rva<=size&&count<=size-rva;}
static DWORD ResidentExport(HANDLE process,HMODULE module,const char*wanted,uintptr_t*address)
{
    *address=0;uintptr_t base=(uintptr_t)module;IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    if(!ReadExact(process,base,&dos,sizeof dos))return ERROR_PARTIAL_COPY;
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<(LONG)sizeof dos||dos.e_lfanew>0x100000||base>UINTPTR_MAX-(uintptr_t)dos.e_lfanew)return ERROR_BAD_EXE_FORMAT;
    if(!ReadExact(process,base+(uintptr_t)dos.e_lfanew,&nt,sizeof nt))return ERROR_PARTIAL_COPY;
    if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return ERROR_BAD_EXE_FORMAT;
    DWORD size=nt.OptionalHeader.SizeOfImage;
    if(!ImageRange(size,(DWORD)dos.e_lfanew,sizeof nt)||base>UINTPTR_MAX-size)return ERROR_BAD_EXE_FORMAT;
    if(nt.OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_EXPORT)return ERROR_PROC_NOT_FOUND;
    IMAGE_DATA_DIRECTORY dir=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];IMAGE_EXPORT_DIRECTORY exports;
    if(!dir.VirtualAddress)return ERROR_PROC_NOT_FOUND;
    if(!ImageRange(size,dir.VirtualAddress,dir.Size)||dir.Size<sizeof exports||!ReadExact(process,base+dir.VirtualAddress,&exports,sizeof exports))return ERROR_BAD_EXE_FORMAT;
    if(exports.NumberOfNames>4096||exports.NumberOfFunctions>4096||
       !ImageRange(size,exports.AddressOfNames,(SIZE_T)exports.NumberOfNames*sizeof(DWORD))||
       !ImageRange(size,exports.AddressOfNameOrdinals,(SIZE_T)exports.NumberOfNames*sizeof(WORD))||
       !ImageRange(size,exports.AddressOfFunctions,(SIZE_T)exports.NumberOfFunctions*sizeof(DWORD)))return ERROR_BAD_EXE_FORMAT;
    for(DWORD i=0;i<exports.NumberOfNames;++i){
        DWORD nameRva;WORD ordinal;DWORD functionRva;char name[128];
        if(!ReadExact(process,base+exports.AddressOfNames+i*sizeof(DWORD),&nameRva,sizeof nameRva))return ERROR_PARTIAL_COPY;
        if(nameRva>=size)return ERROR_BAD_EXE_FORMAT;
        SIZE_T count=size-nameRva;if(count>sizeof name)count=sizeof name;
        if(!ReadExact(process,base+nameRva,name,count))return ERROR_PARTIAL_COPY;
        if(!memchr(name,0,count))return ERROR_BAD_EXE_FORMAT;
        if(strcmp(name,wanted))continue;
        if(!ReadExact(process,base+exports.AddressOfNameOrdinals+i*sizeof(WORD),&ordinal,sizeof ordinal)||
           ordinal>=exports.NumberOfFunctions||!ReadExact(process,base+exports.AddressOfFunctions+ordinal*sizeof(DWORD),&functionRva,sizeof functionRva))return ERROR_BAD_EXE_FORMAT;
        if(!functionRva||!ImageRange(size,functionRva,1)||
           (functionRva>=dir.VirtualAddress&&functionRva-dir.VirtualAddress<dir.Size))return ERROR_BAD_EXE_FORMAT;
        MEMORY_BASIC_INFORMATION memory;
        if(VirtualQueryEx(process,(void*)(base+functionRva),&memory,sizeof memory)!=sizeof memory||
           memory.AllocationBase!=(void*)base||memory.State!=MEM_COMMIT||(memory.Protect&PAGE_GUARD)||
           !(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return ERROR_BAD_EXE_FORMAT;
        *address=base+functionRva;return ERROR_SUCCESS;
    }
    return ERROR_PROC_NOT_FOUND;
}
static DWORD RemoteSystemFunction(DWORD pid,uintptr_t function,uintptr_t*remote)
{
    HMODULE owner=0;wchar_t path[MAX_PATH];*remote=0;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)function,&owner))return GetLastError();
    DWORD n=GetModuleFileNameW(owner,path,_countof(path));if(!n)return GetLastError();
    if(n>=_countof(path))return ERROR_INSUFFICIENT_BUFFER;
    wchar_t*name=wcsrchr(path,L'\\');name=name?name+1:path;
    HMODULE target=0;DWORD result=QueryModule(pid,name,&target,0);
    if(result)return result;
    if(!target)return ERROR_MOD_NOT_FOUND;
    *remote=(uintptr_t)target+function-(uintptr_t)owner;return ERROR_SUCCESS;
}
typedef DWORD (*REMOTE_CALL)(HANDLE,uintptr_t,void*,int*,DWORD*);
typedef DWORD (*MODULE_QUERY)(DWORD,const wchar_t*,HMODULE*,wchar_t*);
static DWORD UnloadResident(HANDLE process,DWORD pid,HMODULE remote,uintptr_t freeLibrary,REMOTE_CALL invoke,MODULE_QUERY query)
{
    uintptr_t version=0,prepare=0;DWORD result=ResidentExport(process,remote,"CopydogUnloadApiVersion",&version);
    if(!result)result=ResidentExport(process,remote,"CopydogPrepareUnload",&prepare);
    if(result){puts("Resident copydog does not expose a valid unload API. Exit the game once to replace this legacy build. No FreeLibrary was attempted.");return result;}
    int completed;DWORD code;
    result=invoke(process,version,0,&completed,&code);
    if(result||!completed){puts("Unload API version call did not complete safely. No FreeLibrary was attempted.");return result?result:ERROR_TIMEOUT;}
    if(code!=1){printf("Unsupported resident unload API version %lu. Exit the game once; no FreeLibrary was attempted.\n",code);return ERROR_REVISION_MISMATCH;}
    result=invoke(process,prepare,0,&completed,&code);
    if(result||!completed||code){puts("Resident DLL did not confirm safe unload preparation. It remains loaded; no FreeLibrary was attempted.");return result?result:!completed?ERROR_TIMEOUT:code;}
    // Exactly one reference is released. A retained module is never forced out.
    result=invoke(process,freeLibrary,remote,&completed,&code);
    if(result||!completed){puts("FreeLibrary completion is unknown. Do not repeat it to force the module out.");return result?result:ERROR_TIMEOUT;}
    if(!code){puts("FreeLibrary returned failure. The module was not confirmed unloaded.");return ERROR_DLL_INIT_FAILED;}
    HMODULE remaining=0;result=query(pid,L"copydog.dll",&remaining,0);
    if(result){puts("Module enumeration failed; unload cannot be confirmed.");return result;}
    if(remaining){puts("copydog.dll remains resident (pinned or additional references). Unload failed; no additional FreeLibrary calls were made. Exit the game once.");return ERROR_BUSY;}
    DWORD state;if(!GetExitCodeProcess(process,&state))return GetLastError();
    if(state!=STILL_ACTIVE){puts("The game process ended; a live unload was not verified.");return ERROR_PROCESS_ABORTED;}
    puts("Unload confirmed: copydog.dll is absent from the live game process.");return ERROR_SUCCESS;
}
static DWORD LockLifecycle(DWORD pid,HANDLE*mutex)
{
    wchar_t name[64];swprintf(name,_countof(name),L"Local\\CopydogLifecycle_%lu",pid);
    *mutex=CreateMutexW(0,FALSE,name);if(!*mutex)return GetLastError();
    DWORD wait=WaitForSingleObject(*mutex,15000);
    if(wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED)return ERROR_SUCCESS;
    DWORD error=wait==WAIT_TIMEOUT?ERROR_BUSY:GetLastError();CloseHandle(*mutex);*mutex=0;return error;
}
/* Compare actual export RVAs and a per-build identity before using local RVAs
   against a resident image. A matching pathname alone is insufficient. */
static int SameResidentBuild(HANDLE p,HMODULE remote,HMODULE local)
{
    uintptr_t base=(uintptr_t)remote;IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    IMAGE_DOS_HEADER*ld=(IMAGE_DOS_HEADER*)local;
    IMAGE_NT_HEADERS64*ln=(IMAGE_NT_HEADERS64*)((char*)local+ld->e_lfanew);
    if(!ReadExact(p,base,&dos,sizeof dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>0x100000||!ReadExact(p,base+(uintptr_t)dos.e_lfanew,&nt,sizeof nt))return 0;
    if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.FileHeader.TimeDateStamp!=ln->FileHeader.TimeDateStamp||nt.OptionalHeader.SizeOfImage!=ln->OptionalHeader.SizeOfImage)return 0;
    DWORD imageSize=nt.OptionalHeader.SizeOfImage;
    IMAGE_DATA_DIRECTORY dir=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];IMAGE_EXPORT_DIRECTORY e;
    if(!dir.VirtualAddress||dir.VirtualAddress>imageSize-sizeof e||!ReadExact(p,base+dir.VirtualAddress,&e,sizeof e)||e.NumberOfNames>256||e.NumberOfFunctions>256||!e.NumberOfNames)return 0;
    DWORD names[256],functions[256];WORD ordinals[256];
    if(e.AddressOfNames>imageSize-e.NumberOfNames*4||e.AddressOfFunctions>imageSize-e.NumberOfFunctions*4||e.AddressOfNameOrdinals>imageSize-e.NumberOfNames*2)return 0;
    if(!ReadExact(p,base+e.AddressOfNames,names,e.NumberOfNames*4)||!ReadExact(p,base+e.AddressOfFunctions,functions,e.NumberOfFunctions*4)||!ReadExact(p,base+e.AddressOfNameOrdinals,ordinals,e.NumberOfNames*2))return 0;
    const char*required[]={"CopydogStart","CopydogStop","CopydogStatus","CopydogCheckProcess","CopydogBuildId","CopydogGetCombatCounters"};unsigned found=0;
    for(unsigned i=0;i<e.NumberOfNames;++i){
        char name[128];if(names[i]>imageSize-sizeof name||!ReadExact(p,base+names[i],name,sizeof name)||!memchr(name,0,sizeof name)||ordinals[i]>=e.NumberOfFunctions)return 0;
        for(unsigned k=0;k<sizeof required/sizeof*required;++k)if(!strcmp(name,required[k])){
            uintptr_t rva=(uintptr_t)GetProcAddress(local,required[k])-(uintptr_t)local;
            if(rva>=imageSize||functions[ordinals[i]]!=rva)return 0;
            if(k==4){char identity[65];const char*expected=(const char*)GetProcAddress(local,"CopydogBuildId");if(rva>imageSize-sizeof identity||!expected||!ReadExact(p,base+rva,identity,sizeof identity)||memcmp(identity,expected,sizeof identity))return 0;}
            found|=1u<<k;
        }
    }
    return found==63;
}
int wmain(int argc,wchar_t**argv)
{
    if(argc!=2||(_wcsicmp(argv[1],L"preflight")&&_wcsicmp(argv[1],L"inject")&&_wcsicmp(argv[1],L"stop")&&_wcsicmp(argv[1],L"unload")&&_wcsicmp(argv[1],L"status"))){puts("Usage: copydog-loader.exe preflight|inject|stop|unload|status");return 2;}
    int inject=!_wcsicmp(argv[1],L"inject"),preflight=!_wcsicmp(argv[1],L"preflight"),unload=!_wcsicmp(argv[1],L"unload");
    wchar_t dllPath[32768],configPath[32768],gamePath[MAX_PATH],existingPath[MAX_PATH];
    DWORD n=GetModuleFileNameW(0,dllPath,_countof(dllPath));
    if(!n||n>=_countof(dllPath)-32)return 2;
    wchar_t*name=wcsrchr(dllPath,L'\\');if(!name)return 2;
    wcscpy(name+1,L"config.txt");wcscpy(configPath,dllPath);wcscpy(name+1,L"copydog.dll");
    DWORD pid=FindGame();if(!pid){puts("Start exactly one DarkSoulsIII.exe first. No process was modified.");return 3;}
    if(!FindModule(pid,L"DarkSoulsIII.exe",gamePath)){puts("DarkSoulsIII.exe was not found in the selected process. No process was modified.");return 4;}
    HMODULE local=0;HANDLE lifecycle=0,process=0;DWORD result=0;void*remoteMemory=0;int keepMemory=0;
    // Status also executes remote exports, so it shares the lifecycle lock.
    if(!preflight){result=LockLifecycle(pid,&lifecycle);if(result){puts("Another loader lifecycle operation is busy.");goto done;}}
    DWORD access=PROCESS_QUERY_INFORMATION|PROCESS_VM_READ;
    if(!preflight)access|=PROCESS_CREATE_THREAD|PROCESS_VM_OPERATION|PROCESS_VM_WRITE;
    process=OpenProcess(access,FALSE,pid);
    if(!process){result=GetLastError();printf("OpenProcess failed: %lu\n",result);goto done;}
    HMODULE remote=0;result=QueryModule(pid,L"copydog.dll",&remote,existingPath);if(result)goto done;
    if(unload){
        if(!remote){puts("copydog.dll is not loaded; no unload operation was needed.");goto done;}
        uintptr_t freeLibrary=0;result=RemoteSystemFunction(pid,(uintptr_t)FreeLibrary,&freeLibrary);
        if(!result)result=UnloadResident(process,pid,remote,freeLibrary,CallRemote,QueryModule);
        goto done;
    }
    local=LoadLibraryW(dllPath);if(!local){result=GetLastError();printf("Cannot load local copydog.dll: %lu\n",result);goto done;}
    const char*build=(const char*)GetProcAddress(local,"CopydogBuildId");
    if(build)printf("Test build: %.64s\n",build);
    if(remote&&_wcsicmp(existingPath,dllPath)){puts("Another copydog.dll is loaded from a different path. Run unload first; legacy pinned builds require a game restart.");result=ERROR_ALREADY_EXISTS;goto done;}
    if(remote&&!SameResidentBuild(process,remote,local)){puts("Resident and local DLL builds differ. Run unload first; legacy pinned builds require a game restart.");result=ERROR_REVISION_MISMATCH;goto done;}
    typedef DWORD (WINAPI*CHECK)(HANDLE);CHECK check;FARPROC checkProc=GetProcAddress(local,"CopydogCheckProcess");memcpy(&check,&checkProc,sizeof check);
    if(!check){result=ERROR_PROC_NOT_FOUND;goto done;}
    if(preflight){
        if(remote){puts("This build is already resident. Preflight performed no remote execution; unload before original-byte checks.");goto done;}
        result=check(process);printf("Read-only preflight: %s (code %lu). No hooks installed.\n",result?"FAIL":"PASS",result);goto done;
    }
    COPYDOG_CONFIG config;
    if(inject){
        wchar_t text[4096];FILE*f=_wfopen(configPath,L"r");
        if(!f){puts("config.txt missing.");result=ERROR_FILE_NOT_FOUND;goto done;}
        size_t used=0;wint_t c;while((c=fgetwc(f))!=WEOF&&used<_countof(text)-1)text[used++]=(wchar_t)c;
        int tooLong=c!=WEOF;fclose(f);text[used]=0;
        if(tooLong||!ParseCopydogConfig(text,&config)){puts("Invalid config.txt; no settings were applied.");result=ERROR_INVALID_DATA;goto done;}
    }
    if(!remote){
        if(!inject){puts("copydog.dll is not loaded; no operation performed.");preflight=1;goto done;}
        result=check(process);if(result){puts("Original-byte preflight failed; existing hooks were not overwritten.");goto done;}
        HMODULE owner=0;wchar_t ownerPath[MAX_PATH];
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)LoadLibraryW,&owner)||!GetModuleFileNameW(owner,ownerPath,_countof(ownerPath))){result=GetLastError();goto done;}
        wchar_t*ownerName=wcsrchr(ownerPath,L'\\');ownerName=ownerName?ownerName+1:ownerPath;
        HMODULE remoteOwner=FindModule(pid,ownerName,0);if(!remoteOwner){result=ERROR_MOD_NOT_FOUND;goto done;}
        SIZE_T bytes=(wcslen(dllPath)+1)*sizeof(wchar_t),written;
        remoteMemory=VirtualAllocEx(process,0,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if(!remoteMemory||!WriteProcessMemory(process,remoteMemory,dllPath,bytes,&written)||written!=bytes){result=GetLastError();goto done;}
        uintptr_t load=(uintptr_t)remoteOwner+(uintptr_t)LoadLibraryW-(uintptr_t)owner;
        int completed;DWORD threadExit;
        result=CallRemote(process,load,remoteMemory,&completed,&threadExit);
        if(!completed)keepMemory=1;
        if(result)goto done;
        remote=FindModule(pid,L"copydog.dll",existingPath);
        if(!remote||_wcsicmp(existingPath,dllPath)||!SameResidentBuild(process,remote,local)){result=ERROR_DLL_INIT_FAILED;goto done;}
        VirtualFreeEx(process,remoteMemory,0,MEM_RELEASE);remoteMemory=0;
    }
    SIZE_T written;
    if(inject){remoteMemory=VirtualAllocEx(process,0,sizeof config,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!remoteMemory||!WriteProcessMemory(process,remoteMemory,&config,sizeof config,&written)||written!=sizeof config){result=GetLastError();goto done;}}
    const char*entry=inject?"CopydogStart":(!_wcsicmp(argv[1],L"stop")?"CopydogStop":"CopydogStatus");
    FARPROC proc=GetProcAddress(local,entry);if(!proc){result=ERROR_PROC_NOT_FOUND;goto done;}
    int completed;DWORD threadExit;
    result=CallRemote(process,(uintptr_t)remote+(uintptr_t)proc-(uintptr_t)local,remoteMemory,&completed,&threadExit);
    if(!completed)keepMemory=1;
    if(result)goto done;
    result=threadExit;
    if(!strcmp(entry,"CopydogStatus")){
        printf("Status: 0=stopped, 1=running, high bit=worker fault. Value=0x%08lX\n",result);
        if(result<2){
            result=0;
            COPYDOG_COMBAT_COUNTERS counters;
            remoteMemory=VirtualAllocEx(process,0,sizeof counters,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
            if(!remoteMemory){result=GetLastError();goto done;}
            FARPROC stats=GetProcAddress(local,"CopydogGetCombatCounters");if(!stats){result=ERROR_PROC_NOT_FOUND;goto done;}
            result=CallRemote(process,(uintptr_t)remote+(uintptr_t)stats-(uintptr_t)local,remoteMemory,&completed,&threadExit);
            if(!completed)keepMemory=1;
            if(result)goto done;
            if(threadExit){result=threadExit;goto done;}
            if(!ReadExact(process,(uintptr_t)remoteMemory,&counters,sizeof counters)){result=ERROR_PARTIAL_COPY;goto done;}
            printf("Bullet: samples=%llu threats=%llu roll_requests=%llu; Hit: checks=%llu skips=%llu\n",
                (unsigned long long)counters.bullet_samples,(unsigned long long)counters.bullet_threats,
                (unsigned long long)counters.bullet_rolls,(unsigned long long)counters.hit_checks,(unsigned long long)counters.hit_skips);
        }
        if(result==0){
        FARPROC parryStats=GetProcAddress(local,"CopydogGetParryCounters");
        if(!parryStats){result=ERROR_PROC_NOT_FOUND;goto done;}
        if(remoteMemory){VirtualFreeEx(process,remoteMemory,0,MEM_RELEASE);remoteMemory=0;}
        uint64_t parry[9];
        remoteMemory=VirtualAllocEx(process,0,sizeof parry,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if(!remoteMemory){result=GetLastError();goto done;}
        result=CallRemote(process,(uintptr_t)remote+(uintptr_t)parryStats-(uintptr_t)local,remoteMemory,&completed,&threadExit);
        if(!completed)keepMemory=1;
        if(result)goto done;
        if(threadExit){result=threadExit;goto done;}
        if(!ReadExact(process,(uintptr_t)remoteMemory,parry,sizeof parry)){result=ERROR_PARTIAL_COPY;goto done;}
        printf("Parry: checks=%llu known=%llu unknown=%llu requests=%llu; anim=%llu elapsed=%llu window=%lld..%lld reason=%llu\n",
            (unsigned long long)parry[0],(unsigned long long)parry[1],(unsigned long long)parry[2],
            (unsigned long long)parry[3],(unsigned long long)parry[4],(unsigned long long)parry[5],
            (long long)parry[6],(long long)parry[7],(unsigned long long)parry[8]);
        puts("Parry reasons: 1=roll priority 2=unknown 3=unparriable 4=key 5=state/distance 6=roll cooldown 7=early 8=late 9=spent 10=requested 11=queue unavailable 12=Shift/F7 roll pause. Requests are not confirmed parries.");
        FARPROC pipeline=GetProcAddress(local,"CopydogGetParryPipeline");
        if(!pipeline){result=ERROR_PROC_NOT_FOUND;goto done;}
        if(remoteMemory){VirtualFreeEx(process,remoteMemory,0,MEM_RELEASE);remoteMemory=0;}
        uint64_t gate[20];remoteMemory=VirtualAllocEx(process,0,sizeof gate,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if(!remoteMemory){result=GetLastError();goto done;}
        result=CallRemote(process,(uintptr_t)remote+(uintptr_t)pipeline-(uintptr_t)local,remoteMemory,&completed,&threadExit);
        if(!completed)keepMemory=1;
        if(result)goto done;
        if(threadExit){result=threadExit;goto done;}
        if(!ReadExact(process,(uintptr_t)remoteMemory,gate,sizeof gate)){result=ERROR_PARTIAL_COPY;goto done;}
        printf("Pipeline: loops=%llu self=%llu poll_not_ready=%llu copy=%llu targets=%llu type_filtered=%llu entries=%llu\n",
            (unsigned long long)gate[0],(unsigned long long)gate[1],(unsigned long long)gate[2],(unsigned long long)gate[3],(unsigned long long)gate[4],(unsigned long long)gate[5],(unsigned long long)gate[6]);
        printf("Gate: missing=%llu bad_anim=%llu not_ready=%llu bad_vector=%llu prior_action=%llu range=%llu space=%llu\n",
            (unsigned long long)gate[7],(unsigned long long)gate[8],(unsigned long long)gate[9],(unsigned long long)gate[10],(unsigned long long)gate[11],(unsigned long long)gate[12],(unsigned long long)gate[13]);
        printf("Observed: self_anim=%llu target_anim=%llu; current hand_mode=%llu parry_key=%llu key_down=%llu self_anim=%lld\n",
            (unsigned long long)gate[14],(unsigned long long)gate[15],(unsigned long long)gate[16],(unsigned long long)gate[19],(unsigned long long)gate[17],(long long)gate[18]);
        FARPROC rollPause=GetProcAddress(local,"CopydogRollPauseStatus");
        if(!rollPause){result=ERROR_PROC_NOT_FOUND;goto done;}
        result=CallRemote(process,(uintptr_t)remote+(uintptr_t)rollPause-(uintptr_t)local,0,&completed,&threadExit);
        if(result)goto done;
        DWORD paused=threadExit;
        FARPROC rollEnabled=GetProcAddress(local,"CopydogRollEnabledStatus");
        if(!rollEnabled){result=ERROR_PROC_NOT_FOUND;goto done;}
        result=CallRemote(process,(uintptr_t)remote+(uintptr_t)rollEnabled-(uintptr_t)local,0,&completed,&threadExit);
        if(result)goto done;
        printf("Auto roll: %s (F7 toggles; default OFF; Shift temporarily pauses).\n",
            !threadExit?"OFF":paused?"PAUSED":"ON");
        FARPROC copyStatus=GetProcAddress(local,"CopydogCopyStatus");
        if(!copyStatus){result=ERROR_PROC_NOT_FOUND;goto done;}
        result=CallRemote(process,(uintptr_t)remote+(uintptr_t)copyStatus-(uintptr_t)local,0,&completed,&threadExit);
        if(result)goto done;
        printf("Copy mode: %s (Shift+Y toggles; Y turns off).\n",threadExit?"ON":"OFF");
        FARPROC calibration=GetProcAddress(local,"CopydogGetParryCalibration");
        if(calibration){
            if(remoteMemory){VirtualFreeEx(process,remoteMemory,0,MEM_RELEASE);remoteMemory=0;}
            uint64_t values[10];remoteMemory=VirtualAllocEx(process,0,sizeof values,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
            if(!remoteMemory){result=GetLastError();goto done;}
            result=CallRemote(process,(uintptr_t)remote+(uintptr_t)calibration-(uintptr_t)local,remoteMemory,&completed,&threadExit);
            if(!completed)keepMemory=1;
            if(result)goto done;
            if(threadExit){result=threadExit;goto done;}
            if(!ReadExact(process,(uintptr_t)remoteMemory,values,sizeof values)){result=ERROR_PARTIAL_COPY;goto done;}
            unsigned sourceCode=(unsigned)values[0]&7u;
            const char*source=sourceCode==0?"resource fallback":sourceCode==1?"empirical seed (D unknown)":sourceCode==2?"empirical measured D":sourceCode==3?"empirical historical SC3 D seed":"unknown source";
            printf("SC4 calibration: clock=%s; source=%s; profile=",(values[0]&8u)?"early action name":"visible animation ID",source);
            if(values[1]==UINT64_MAX)printf("none");else printf("%llu",(unsigned long long)values[1]);
            printf("; target_weapon_raw=%llu target_animation=%llu tool=%llu; ",
                (unsigned long long)values[2],(unsigned long long)values[3],(unsigned long long)values[4]);
            if(values[5])printf("dispatch_D=%llu ms\n",(unsigned long long)values[6]);
            else puts("dispatch_D=UNKNOWN (not a measured zero)");
            printf("SC4 request plan: result=%lld (0=no override, 1=valid, -1=unreachable); window=%lld..%lld ms (-1=unavailable).\n",
                (long long)(int64_t)values[7],(long long)(int64_t)values[8],(long long)(int64_t)values[9]);
            puts("Calibration/request windows are diagnostics, not confirmed successful parries.");
        }
        FARPROC receiveMode=GetProcAddress(local,"CopydogReceiveMode");
        if(receiveMode){
            result=CallRemote(process,(uintptr_t)remote+(uintptr_t)receiveMode-(uintptr_t)local,0,&completed,&threadExit);
            if(result)goto done;
            printf("Receive mode: %lu (0=not installed, 1=direct hook, 2=shared observer, 3=V1 observer skipped for unload).\n",threadExit);
            if(threadExit==3)puts("V1 observer skipped: packet-based latency estimate and item/magic synchronization unavailable; memory-based action recognition is unaffected.");
        }
        }
    }
done:
    if(remoteMemory&&!keepMemory)VirtualFreeEx(process,remoteMemory,0,MEM_RELEASE);
    if(process)CloseHandle(process);
    if(local)FreeLibrary(local);
    if(lifecycle){ReleaseMutex(lifecycle);CloseHandle(lifecycle);}
    if(result)printf("Operation returned Windows code %lu (0x%08lX).\n",result,result);
    else if(!preflight&&!unload)puts(inject?"Injection/start completed. Use 03_status.bat for diagnostics; stop.bat safely unloads this build.":"Operation completed.");
    return result?1:0;
}
