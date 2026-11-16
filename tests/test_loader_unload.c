#define wmain CopydogLoaderMainForTest
#include "../src/loader.c"
#undef wmain

static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uintptr_t entries[8];
static void*parameters[8];
static unsigned calls,queries;
static DWORD versionResult,prepareResult,freeResult,callError,queryError;
static int errorCall,incompleteCall;
static HMODULE residentAfter;
static DWORD MockCall(HANDLE p,uintptr_t entry,void*parameter,int*completed,DWORD*code)
{
    (void)p;unsigned index=calls++;entries[index]=entry;parameters[index]=parameter;
    *completed=(int)index!=incompleteCall;
    *code=index==0?versionResult:index==1?prepareResult:freeResult;
    return (int)index==errorCall?callError:ERROR_SUCCESS;
}
static DWORD MockQuery(DWORD pid,const wchar_t*name,HMODULE*module,wchar_t*path)
{(void)pid;(void)name;(void)path;++queries;*module=residentAfter;return queryError;}
static void Reset(void)
{
    memset(entries,0,sizeof entries);memset(parameters,0,sizeof parameters);
    calls=queries=0;versionResult=freeResult=1;prepareResult=queryError=0;
    errorCall=incompleteCall=-1;callError=ERROR_TIMEOUT;residentAfter=0;
}
int main(void)
{
    unsigned char*image=VirtualAlloc(0,0x3000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(image!=0);
    IMAGE_DOS_HEADER*dos=(IMAGE_DOS_HEADER*)image;dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x100;
    IMAGE_NT_HEADERS64*nt=(IMAGE_NT_HEADERS64*)(image+0x100);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfImage=0x3000;nt->OptionalHeader.NumberOfRvaAndSizes=IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]=(IMAGE_DATA_DIRECTORY){0x300,0x400};
    IMAGE_EXPORT_DIRECTORY*exports=(IMAGE_EXPORT_DIRECTORY*)(image+0x300);
    exports->NumberOfNames=exports->NumberOfFunctions=2;exports->AddressOfFunctions=0x400;
    exports->AddressOfNames=0x420;exports->AddressOfNameOrdinals=0x440;
    DWORD*functions=(DWORD*)(image+0x400);functions[0]=0x1000;functions[1]=0x1100;
    DWORD*names=(DWORD*)(image+0x420);names[0]=0x500;names[1]=0x540;
    WORD*ordinals=(WORD*)(image+0x440);ordinals[0]=0;ordinals[1]=1;
    strcpy((char*)image+0x500,"CopydogUnloadApiVersion");strcpy((char*)image+0x540,"CopydogPrepareUnload");
    DWORD protection;CHECK(VirtualProtect(image+0x1000,0x1000,PAGE_EXECUTE_READ,&protection));
    HANDLE process=GetCurrentProcess();HMODULE module=(HMODULE)image;uintptr_t address;
    CHECK(ResidentExport(process,module,"CopydogUnloadApiVersion",&address)==0&&address==(uintptr_t)image+0x1000);
    CHECK(ResidentExport(process,module,"CopydogPrepareUnload",&address)==0&&address==(uintptr_t)image+0x1100);
    CHECK(ResidentExport(process,module,"Absent",&address)==ERROR_PROC_NOT_FOUND);
    exports->NumberOfNames=4097;CHECK(ResidentExport(process,module,"Absent",&address)==ERROR_BAD_EXE_FORMAT);exports->NumberOfNames=2;
    ordinals[0]=2;CHECK(ResidentExport(process,module,"CopydogUnloadApiVersion",&address)==ERROR_BAD_EXE_FORMAT);ordinals[0]=0;
    functions[0]=0x350;CHECK(ResidentExport(process,module,"CopydogUnloadApiVersion",&address)==ERROR_BAD_EXE_FORMAT);
    functions[0]=0x800;CHECK(ResidentExport(process,module,"CopydogUnloadApiVersion",&address)==ERROR_BAD_EXE_FORMAT);functions[0]=0x1000;
    exports->AddressOfNames=0x2fff;CHECK(ResidentExport(process,module,"Absent",&address)==ERROR_BAD_EXE_FORMAT);exports->AddressOfNames=0x420;
    Reset();exports->NumberOfNames=1;
    CHECK(UnloadResident(process,1,module,0x12345678,MockCall,MockQuery)==ERROR_PROC_NOT_FOUND&&calls==0&&queries==0);exports->NumberOfNames=2;
    Reset();versionResult=2;
    CHECK(UnloadResident(process,1,module,0x12345678,MockCall,MockQuery)==ERROR_REVISION_MISMATCH&&calls==1);
    Reset();errorCall=0;
    CHECK(UnloadResident(process,1,module,0x12345678,MockCall,MockQuery)==ERROR_TIMEOUT&&calls==1);
    Reset();incompleteCall=0;
    CHECK(UnloadResident(process,1,module,0x12345678,MockCall,MockQuery)==ERROR_TIMEOUT&&calls==1);
    Reset();prepareResult=ERROR_BUSY;
    CHECK(UnloadResident(process,1,module,0x12345678,MockCall,MockQuery)==ERROR_BUSY&&calls==2&&queries==0);
    Reset();errorCall=1;
    CHECK(UnloadResident(process,1,module,0x12345678,MockCall,MockQuery)==ERROR_TIMEOUT&&calls==2);
    Reset();incompleteCall=1;
    CHECK(UnloadResident(process,1,module,0x12345678,MockCall,MockQuery)==ERROR_TIMEOUT&&calls==2);
    Reset();freeResult=0;
    CHECK(UnloadResident(process,1,module,0x12345678,MockCall,MockQuery)==ERROR_DLL_INIT_FAILED&&calls==3&&queries==0);
    Reset();errorCall=2;
    CHECK(UnloadResident(process,1,module,0x12345678,MockCall,MockQuery)==ERROR_TIMEOUT&&calls==3);
    Reset();incompleteCall=2;
    CHECK(UnloadResident(process,1,module,0x12345678,MockCall,MockQuery)==ERROR_TIMEOUT&&calls==3);
    Reset();queryError=ERROR_ACCESS_DENIED;
    CHECK(UnloadResident(process,1,module,0x12345678,MockCall,MockQuery)==ERROR_ACCESS_DENIED&&calls==3&&queries==1);
    Reset();residentAfter=module;
    CHECK(UnloadResident(process,1,module,0x12345678,MockCall,MockQuery)==ERROR_BUSY&&calls==3&&queries==1);
    Reset();CHECK(UnloadResident(process,1,module,0x12345678,MockCall,MockQuery)==0&&calls==3&&queries==1);
    CHECK(entries[0]==(uintptr_t)image+0x1000&&entries[1]==(uintptr_t)image+0x1100&&entries[2]==0x12345678);
    CHECK(parameters[0]==0&&parameters[1]==0&&parameters[2]==module);
    HMODULE self=0;CHECK(QueryModule(GetCurrentProcessId(),L"module-definitely-not-present.dll",&self,0)==0&&self==0);
    CHECK(RemoteSystemFunction(GetCurrentProcessId(),(uintptr_t)FreeLibrary,&address)==0&&address==(uintptr_t)FreeLibrary);
    HANDLE mutex=0;CHECK(LockLifecycle(GetCurrentProcessId(),&mutex)==0&&mutex!=0);ReleaseMutex(mutex);CloseHandle(mutex);
    CHECK(VirtualFree(image,0,MEM_RELEASE));
    printf("PASS %u loader unload assertions; synthetic image and mocked remote calls only\n",checks);return 0;
}
