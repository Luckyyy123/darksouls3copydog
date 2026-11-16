#include <windows.h>
#include <mmsystem.h>
static DWORD fakeNow=10000;
static int shiftHeld,sleepCalls;
static DWORD WINAPI TestNow(void){return fakeNow;}
static SHORT WINAPI TestKey(int key){return key==VK_SHIFT&&shiftHeld?(SHORT)0x8000:0;}
static void WINAPI TestSleep(DWORD ms){if(ms==192)++sleepCalls;fakeNow+=ms;}
#define timeGetTime TestNow
#define GetAsyncKeyState TestKey
#define Sleep TestSleep
#define main UnusedLegacyPollMain
#include "test_v6_restore.c"
#undef main

int main(void)
{
    void*globals=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(globals==(void*)0x144770000);
    char world[0x100]={0},slots[6*0x38]={0},me[0x2100]={0},you[0x2100]={0};
    char modules[0x100]={0},targetModules[0x100]={0},selfAnim[0xd0]={0},targetAnim[0xd0]={0};
    char pos[0x90]={0},targetPos[0x90]={0},hp[0x100]={0},targetHP[0x100]={0};
    char state[0x229]={0},targetState[0x229]={0},mod[0x20]={0},node[0x30]={0},sub[0xc0]={0},queue[0x20]={0};
    pointer(addr1,world);pointer(world+0x80,me);pointer(world+0x40,slots);pointer(slots+0x38,you);
    pointer(me+XA,modules);pointer(you+XA,targetModules);pointer(modules,state);pointer(targetModules,targetState);
    pointer(modules+0x18,hp);pointer(targetModules+0x18,targetHP);integer(hp+0xd8,100);integer(targetHP+0xd8,100);
    pointer(modules+0x80,selfAnim);pointer(targetModules+0x80,targetAnim);integer(targetAnim+0xc8,40002);
    pointer(modules+0x68,pos);pointer(targetModules+0x68,targetPos);real(targetPos+0x88,2.0f);
    pointer(modules+0x28,mod);pointer(mod+0x10,node);pointer(node+0x28,sub);pointer(sub+0xb8,queue);
    Config.BackstabDist=0;Config.RollAtkDist=0;Config.ForestallDistance=0;Config.RollDistance=4;Config.KeyCondition='V';
    CopydogParryObserve(&ParryTrack[1],(uintptr_t)me,(uintptr_t)you,40002,NAN,fakeNow-20);
    CopydogParryObserve(&ParryTrack[1],(uintptr_t)me,(uintptr_t)you,40002,NAN,fakeNow);
    CHECK(ParryTrack[1].stable&&IsNonDmgAttack(40002));

    atomic_store(&AutoRollEnabled,1);shiftHeld=0;CheckAnimation(1);
    CHECK(calls==0&&sleepCalls==0&&!CombatFrameClaimed);
    atomic_store(&AutoRollEnabled,0);fakeNow+=200;CheckAnimation(1);
    CHECK(calls==1&&sleepCalls==0&&CombatFrameClaimed);
    FinishHeavyInput(me,fakeNow+191);CHECK(calls==1);
    fakeNow+=192;FinishHeavyInput(me,fakeNow);CHECK(calls==2);
    CHECK(lastAction==GetNextHeavyAttack()-1);
    atomic_store(&AutoRollEnabled,1);shiftHeld=1;CombatFrameClaimed=0;fakeNow+=200;CheckAnimation(1);
    CHECK(calls==3&&sleepCalls==0&&CombatFrameClaimed);
    fakeNow+=192;FinishHeavyInput(me,fakeNow);CHECK(calls==4);
    CHECK(lastAction==GetNextHeavyAttack()-1);
    shiftHeld=0;CombatFrameClaimed=0;fakeNow+=200;CheckAnimation(1);
    CHECK(calls==4&&sleepCalls==0&&!CombatFrameClaimed);
    VirtualFree(globals,0,MEM_RELEASE);
    printf("PASS %u roll polling priority assertions; mocked actions and clock\n",checks);return 0;
}
