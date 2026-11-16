#include "../src/hook_lifetime.h"
#include <stdio.h>
static HANDLE entered,releaseEvent;
static int ReturnBranch(int value)
{
    COPYDOG_HOOK_SCOPE;
    if(CopydogActiveHooks()!=1)return -1;
    if(value)return 17;
    return 23;
}
static DWORD WINAPI NativeWait(void* unused)
{
    (void)unused;
    COPYDOG_HOOK_SCOPE;
    SetEvent(entered);
    return WaitForSingleObject(releaseEvent,5000);
}
#define CHECK(x) do{if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}++checks;}while(0)
int main(void)
{
    unsigned checks=0;
    CHECK(CopydogActiveHooks()==0);
    CHECK(ReturnBranch(0)==23);CHECK(CopydogActiveHooks()==0);
    CHECK(ReturnBranch(1)==17);CHECK(CopydogActiveHooks()==0);
    entered=CreateEventW(0,TRUE,FALSE,0);releaseEvent=CreateEventW(0,TRUE,FALSE,0);
    CHECK(entered&&releaseEvent);
    HANDLE worker=CreateThread(0,0,NativeWait,0,0,0);CHECK(worker);
    CHECK(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);
    CHECK(CopydogActiveHooks()==1);
    CHECK(SetEvent(releaseEvent));CHECK(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
    CHECK(CopydogActiveHooks()==0);
    CloseHandle(worker);CloseHandle(entered);CloseHandle(releaseEvent);
    printf("PASS %u hook lifetime checks; native wait retains activity, all C returns drain\n",checks);
    return 0;
}
