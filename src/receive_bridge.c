#include "receive_bridge.h"
#include <tlhelp32.h>
#include <string.h>
#include <wchar.h>
#define BRIDGE_EXPORT "BsRegisterDeliveredObserverV1"
static int ReadImage(HANDLE process,uintptr_t base,DWORD size,DWORD rva,void*out,SIZE_T n)
{
    SIZE_T got=0;
    return rva<=size&&n<=size-rva&&ReadProcessMemory(process,(void*)(base+rva),out,n,&got)&&got==n;
}
static DWORD FindExport(HANDLE process,uintptr_t base,uintptr_t*address)
{
    IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;SIZE_T got;
    if(!ReadProcessMemory(process,(void*)base,&dos,sizeof dos,&got)||got!=sizeof dos)return ERROR_PARTIAL_COPY;
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>0x100000)return ERROR_BAD_EXE_FORMAT;
    if(!ReadProcessMemory(process,(void*)(base+(uintptr_t)dos.e_lfanew),&nt,sizeof nt,&got)||got!=sizeof nt)return ERROR_PARTIAL_COPY;
    if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return ERROR_BAD_EXE_FORMAT;
    const DWORD size=nt.OptionalHeader.SizeOfImage;
    if(nt.OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_EXPORT)return ERROR_SUCCESS;
    const IMAGE_DATA_DIRECTORY table=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!table.VirtualAddress||!table.Size)return ERROR_SUCCESS;
    IMAGE_EXPORT_DIRECTORY exports;
    if(!ReadImage(process,base,size,table.VirtualAddress,&exports,sizeof exports))return ERROR_PARTIAL_COPY;
    if(exports.NumberOfNames>65536||exports.NumberOfFunctions>65536)return ERROR_BAD_EXE_FORMAT;
    for(DWORD i=0;i<exports.NumberOfNames;++i){
        DWORD nameRva;char name[sizeof BRIDGE_EXPORT];
        if(exports.AddressOfNames>size||i>(size-exports.AddressOfNames)/sizeof(DWORD))return ERROR_BAD_EXE_FORMAT;
        if(!ReadImage(process,base,size,exports.AddressOfNames+i*sizeof(DWORD),&nameRva,sizeof nameRva))return ERROR_PARTIAL_COPY;
        if(!ReadImage(process,base,size,nameRva,name,sizeof name))continue;
        if(memcmp(name,BRIDGE_EXPORT,sizeof name))continue;
        WORD ordinal;DWORD functionRva;
        if(exports.AddressOfNameOrdinals>size||i>(size-exports.AddressOfNameOrdinals)/sizeof(WORD))return ERROR_BAD_EXE_FORMAT;
        if(!ReadImage(process,base,size,exports.AddressOfNameOrdinals+i*sizeof(WORD),&ordinal,sizeof ordinal)||ordinal>=exports.NumberOfFunctions)return ERROR_BAD_EXE_FORMAT;
        if(exports.AddressOfFunctions>size||ordinal>(size-exports.AddressOfFunctions)/sizeof(DWORD))return ERROR_BAD_EXE_FORMAT;
        if(!ReadImage(process,base,size,exports.AddressOfFunctions+ordinal*sizeof(DWORD),&functionRva,sizeof functionRva)||!functionRva||functionRva>=size)return ERROR_BAD_EXE_FORMAT;
        // 不接受转发导出，不解析任意现有跳转，也不覆盖第三方钩子。
        if(functionRva>=table.VirtualAddress&&functionRva-table.VirtualAddress<table.Size)return ERROR_BAD_EXE_FORMAT;
        *address=base+functionRva;return ERROR_SUCCESS;
    }
    return ERROR_SUCCESS;
}
DWORD CopydogFindReceiveBridge(HANDLE process,uintptr_t*address)
{
    *address=0;
    DWORD pid=GetProcessId(process);if(!pid)return GetLastError();
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snapshot==INVALID_HANDLE_VALUE)return GetLastError();
    MODULEENTRY32W module={.dwSize=sizeof module};DWORD result=ERROR_SUCCESS;
    if(!Module32FirstW(snapshot,&module)){result=GetLastError();goto done;}
    do {
        if(_wcsicmp(module.szModule,L"dinput8.dll")&&_wcsicmp(module.szModule,L"BlueSentinelRebuild.dll"))continue;
        result=FindExport(process,(uintptr_t)module.modBaseAddr,address);
        if(result||*address)break;
    }while(Module32NextW(snapshot,&module));
done:
    CloseHandle(snapshot);return result;
}
