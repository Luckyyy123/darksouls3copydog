/* Deterministic post-physics reads and a self-owned process fixture. */
#include <windows.h>
static LONGLONG wallMs=1000;
static BOOL WINAPI FrameFrequency(LARGE_INTEGER*out){out->QuadPart=1000;return TRUE;}
static BOOL WINAPI FrameCounter(LARGE_INTEGER*out){out->QuadPart=wallMs;return TRUE;}
#define QueryPerformanceFrequency FrameFrequency
#define QueryPerformanceCounter FrameCounter
#define CROSSBOW_FIXTURE_ENTRY UnusedFrameStateFixture
#include "test_crossbow_roll.c"

static void observe(char*me,unsigned deltaMs)
{
    fakeNow+=deltaMs;wallMs+=deltaMs;
    ObserveParryGameFrame(me,fakeNow);
}
static void markEpisode(char*me,char*target)
{
    DynamicSelf=(DynamicParrySelf){.self=(uintptr_t)me,.tool=11010000,.mode=1,.count=3};
    DynamicTargets[1]=(ParryFrameTarget){.actor=(uintptr_t)target,.submitted=1,.verifiedFrame=1};
    ParryTrack[1].spent=EarlyTrack[1].spent=1;
    prevLockTarget=target;
}
static int episodeCleared(void)
{
    return !DynamicSelf.count&&!DynamicTargets[1].submitted&&!DynamicTargets[1].verifiedFrame&&
        !ParryTrack[1].spent&&!EarlyTrack[1].spent&&!prevLockTarget;
}
int main(void)
{
    void*world=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    void*manager=VirtualAlloc((void*)0x144750000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(world==(void*)0x144770000&&manager==(void*)0x144750000);
    SCENE scene={0};setup(&scene);
    char original[0xc0]={0},replacement[0xc0]={0};
    pointer(addr2,original);integer(original+CD_GAME_PLAY_MS_OFFSET,1000);
    observe(scene.me,0);
    integer(original+CD_GAME_PLAY_MS_OFFSET,1016);observe(scene.me,16);
    CHECK(GameCadence.running&&GameCadence.wallStep==16);
    markEpisode(scene.me,scene.you);
    char*player=NULL;
    pointer(addr1,(void*)1);
    CHECK(!ReadFramePlayer(&player));
    FrameStampUnavailable();CHECK(!GameCadence.running&&DynamicSelf.count==3);
    pointer(addr1,scene.world);CHECK(ReadFramePlayer(&player)&&player==scene.me);
    /* A failed read breaks the cadence pair, never the identified loading epoch. */
    pointer(addr2,NULL);observe(scene.me,2);
    CHECK(!GameCadence.running&&DynamicSelf.count==3&&DynamicTargets[1].submitted);
    pointer(addr2,original);integer(original+CD_GAME_PLAY_MS_OFFSET,1018);observe(scene.me,2);
    CHECK(!GameCadence.running&&DynamicSelf.count==3);
    observe(scene.me,500);
    CHECK(DynamicSelf.count==3&&ParryTrack[1].spent);

    /* Across a bad read the same manager/actor/handle/animation can return after loading. */
    Defense=(COPYDOG_DEFENSE){.self=(uintptr_t)scene.me,.target=(uintptr_t)scene.you,
        .kind=DEF_PARRY,.phase=DEF_WAIT_ANIMATION,.requests=7,.awaitingConfirmation=1,
        .submitted=fakeNow,.hp=100,.haveDelay=1};
    pointer(addr1,(void*)1);UpdateDefenseState(fakeNow);
    CHECK(Defense.phase==DEF_WAIT_ANIMATION&&Defense.requests==7&&Defense.cancellations==0);
    pointer(addr1,scene.world);pointer(scene.mods+0x18,(void*)1);UpdateDefenseState(fakeNow);
    CHECK(Defense.phase==DEF_WAIT_ANIMATION&&Defense.requests==7&&Defense.cancellations==0);
    pointer(scene.mods+0x18,scene.hp);
    pointer(addr2,NULL);observe(scene.me,2);
    pointer(addr2,original);integer(original+CD_GAME_PLAY_MS_OFFSET,50);observe(scene.me,1);
    CHECK(episodeCleared()&&!GameCadence.running);
    CHECK(Defense.phase==DEF_CANCELLED&&Defense.requests==7&&Defense.cancellations==1&&
          Defense.awaitingConfirmation&&Defense.target==0&&!Defense.haveDelay&&atomic_load(&MirrorDefenseBusy));
    UpdateDefenseState(fakeNow);UpdateDefenseState(fakeNow+30);
    CHECK(Defense.phase==DEF_CANCELLED&&Defense.confirmations==0&&calls==0);
    UpdateDefenseState(fakeNow+405);CHECK(!CopydogDefenseBusy(&Defense));
    fakeNow+=405;
    markEpisode(scene.me,scene.you);observe(scene.me,4);
    CHECK(DynamicSelf.count==3&&Defense.cancellations==1);

    /* A changed manager invalidates the old world even at the identical clock value. */
    integer(replacement+CD_GAME_PLAY_MS_OFFSET,50);pointer(addr2,replacement);observe(scene.me,3);
    CHECK(episodeCleared());
    markEpisode(scene.me,scene.you);integer(replacement+CD_GAME_PLAY_MS_OFFSET,(int)CD_GAME_PLAY_MS_LIMIT);
    observe(scene.me,2);observe(scene.me,1200);
    CHECK(!GameCadence.running&&DynamicSelf.count==3);

    /* Player disappearance is a single boundary. Keep an unconfirmed queued input
       guarded when the player later reappears with the same memory identity. */
    Defense=(COPYDOG_DEFENSE){.self=(uintptr_t)scene.me,.kind=DEF_PARRY,.phase=DEF_WAIT_ANIMATION,
        .requests=1,.hp=100,.awaitingConfirmation=1,.submitted=fakeNow};
    pointer(scene.world+0x80,NULL);CHECK(ReadFramePlayer(&player)&&!player);
    observe(player,2);CHECK(episodeCleared()&&Defense.phase==DEF_CANCELLED&&!Defense.self);
    observe(NULL,3);CHECK(Defense.cancellations==1&&atomic_load(&MirrorDefenseBusy));
    pointer(scene.world+0x80,scene.me);CHECK(ReadFramePlayer(&player)&&player==scene.me);
    observe(player,3);CHECK(Defense.self==(uintptr_t)scene.me&&Defense.phase==DEF_CANCELLED);
    UpdateDefenseState(fakeNow);UpdateDefenseState(fakeNow+32);CHECK(Defense.phase==DEF_CANCELLED);
    UpdateDefenseState(fakeNow+405);CHECK(Defense.phase==DEF_IDLE&&calls==0);

    /* A different valid actor can appear between the poll and a nested defense
       update. Its parry must never confirm the old request. */
    fakeNow+=405;
    markEpisode(scene.me,scene.you);
    Defense=(COPYDOG_DEFENSE){.self=(uintptr_t)scene.me,.kind=DEF_PARRY,.phase=DEF_WAIT_ANIMATION,
        .requests=2,.hp=100,.awaitingConfirmation=1,.submitted=fakeNow};
    pointer(scene.world+0x80,scene.you);integer(scene.theirAnim+0xc8,48032100);
    UpdateDefenseState(fakeNow);UpdateDefenseState(fakeNow+16);
    CHECK(Defense.phase==DEF_WAIT_ANIMATION&&Defense.self==(uintptr_t)scene.me&&Defense.confirmations==0);
    observe(scene.you,16);
    CHECK(episodeCleared()&&Defense.phase==DEF_CANCELLED&&Defense.self==(uintptr_t)scene.you&&Defense.requests==2);
    const uint64_t*cancel=CopydogAutoTrace.entries[(CopydogAutoTrace.header[5]-1)%TRACE_CAPACITY];
    CHECK(cancel[1]==TRACE_CANCEL&&cancel[3]==(uintptr_t)scene.me&&(int64_t)cancel[6]==-1&&cancel[14]==DEF_SELF_CHANGED);
    UpdateDefenseState(fakeNow);UpdateDefenseState(fakeNow+420);
    CHECK(Defense.phase==DEF_CANCELLED&&Defense.confirmations==0&&!Defense.haveDelay);
    integer(scene.theirAnim+0xc8,0);UpdateDefenseState(fakeNow+421);UpdateDefenseState(fakeNow+450);
    CHECK(Defense.phase==DEF_IDLE&&Defense.cancellations==1&&calls==0);
    /* The final dispatch can see the replacement after selection for the old actor. */
    pointer(scene.world+0x80,scene.me);
    CHECK(!SubmitDefense(ActionLeftParry,DEF_PARRY,scene.you,23030000,11010000,fakeNow+451)&&calls==0);
    VirtualFree(manager,0,MEM_RELEASE);VirtualFree(world,0,MEM_RELEASE);
    printf("PASS %u world epoch, transient read, load calibration and input cancellation checks\n",checks);
}
