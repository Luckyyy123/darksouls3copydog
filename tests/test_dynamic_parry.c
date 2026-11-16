/* Historical solver comparison only: explicit offline legacy entry points.
   Current production scheduling is covered by test_sc23_parry/features.c. */
#define CROSSBOW_FIXTURE_ENTRY UnusedCrossbowFixture
#include "test_crossbow_roll.c"
static void nativeClock(char*clock,int animation,float phase)
{
    integer(clock+0x20,animation);real(clock+0x24,phase-.004f);real(clock+0x28,phase);
    real(clock+0x2c,2);integer(clock+0xc0,1);integer(clock+0xc4,0);
}
int main(void)
{
    double lo,hi,preferred;
    CHECK(CopydogDynamicParryWindow(400,430,133,267,40,60,&lo,&hi,&preferred));
    CHECK(lo==123&&hi==207&&preferred==165);
    for(int d=123;d<=207;d++)for(int q=40;q<=60;q++)
        CHECK(d+q+133<=400&&d+q+267>=430);
    CHECK(!CopydogDynamicParryWindow(0,200,100,150,0,20,&lo,&hi,&preferred));
    CHECK(!CopydogDynamicParryWindow(NAN,1,0,1,0,1,&lo,&hi,&preferred));
    void*g=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    void*h=VirtualAlloc((void*)0x144750000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(g==(void*)0x144770000&&h==(void*)0x144750000);
    SCENE s={0};setup(&s);char equipment[0x400]={0},root[32]={0},ownClock[0x100]={0},enemyClock[0x100]={0};
    pointer(addr2,root);pointer(root+16,equipment);pointer(s.me+XB,equipment);
    integer(equipment+0x2b8,1);integer(equipment+0x32c,11010000);
    pointer(s.mods+0x10,ownClock);pointer(s.ymods+0x10,enemyClock);
    CHECK(GetCurrentWeapon(0)==11010000);Config.KeyCondition=0;Config.ParryDistance=4;
    CHECK(TryLegacyFrameAutomaticParryForOffline(1,s.me,s.you,fakeNow)==CD_FRAME_SKIP);
    /* Request-to-native-origin is 50ms, learned from three actual clock pairs. */
    for(unsigned n=0;n<3;n++){
        unsigned request=10000+n*1000;
        Defense=(COPYDOG_DEFENSE){.self=(uintptr_t)s.me,.kind=DEF_PARRY,.tool=11010000,
            .phase=DEF_WAIT_ANIMATION,.submitted=request};DefenseHandMode=1;
        nativeClock(ownClock,42032100,.1f);ObserveDynamicParrySelf(s.me,request+150);
        nativeClock(ownClock,42032100,.14f);ObserveDynamicParrySelf(s.me,request+190);
    }
    CHECK(DynamicSelf.count==3&&abs(DynamicSelf.values[0]-50)<=1);
    Defense=(COPYDOG_DEFENSE){0};CombatFrameClaimed=0;
    /* Fist R1 has a 400..466ms resource window and no mapped attack shape. */
    integer(s.equipment+0x330,110000);integer(s.theirAnim+0xc8,42030000);
    nativeClock(enemyClock,42030000,.05f);fakeNow=14000;
    int before=calls;CHECK(TryLegacyFrameAutomaticParryForOffline(1,s.me,s.you,fakeNow)==CD_FRAME_WAIT&&calls==before);
    uint64_t plans=atomic_load(&CopydogDynamicParryCounters[DP_PLAN]);
    CHECK(TryLegacyFrameAutomaticParryForOffline(1,s.me,s.you,++fakeNow)==CD_FRAME_WAIT);
    CHECK(plans==atomic_load(&CopydogDynamicParryCounters[DP_PLAN]));
    fakeNow+=150;nativeClock(enemyClock,42030000,.2f);
    CHECK(TryLegacyFrameAutomaticParryForOffline(1,s.me,s.you,fakeNow)!=CD_FRAME_PRESS);
    CHECK(PendingDefenseTrace[14]==PARRY_CONTACT_UNCONFIRMED);
    CHECK(!CopydogParryContactAuthorizes((unsigned)PendingDefenseTrace[15]));
    CHECK(calls==before&&!ParryTrack[1].spent&&!DynamicTargets[1].submitted);
    CHECK(atomic_load(&CopydogDynamicParryCounters[DP_SUBMIT])==0);
    CHECK(TryLegacyFrameAutomaticParryForOffline(1,s.me,s.you,fakeNow)!=CD_FRAME_PRESS&&calls==before);
    Defense=(COPYDOG_DEFENSE){0};CombatFrameClaimed=0;ParryTrack[1].spent=0;
    nativeClock(enemyClock,42030000,.38f);fakeNow+=20;
    CHECK(TryLegacyFrameAutomaticParryForOffline(1,s.me,s.you,fakeNow)==CD_FRAME_SKIP&&calls==before);
    /* Same move, new native playback. First see it past the last press deadline. */
    DynamicTargets[1]=(ParryFrameTarget){0};fakeNow+=20;
    CHECK(TryLegacyFrameAutomaticParryForOffline(1,s.me,s.you,fakeNow)==CD_FRAME_SKIP&&calls==before);
    CHECK(atomic_load(&CopydogDynamicParryCounters[DP_LATE])>0);
    /* A missing remote clock cannot retrigger the rejected visible swing.
       Unknown moves also stay silent across successive local updates. */
    pointer(s.ymods+0x10,NULL);
    CHECK(TryLegacyFrameAutomaticParryForOffline(1,s.me,s.you,++fakeNow)==CD_FRAME_SKIP);
    integer(s.theirAnim+0xc8,25034000);
    CHECK(TryLegacyFrameAutomaticParryForOffline(1,s.me,s.you,++fakeNow)==CD_FRAME_SKIP);
    CHECK(TryLegacyFrameAutomaticParryForOffline(1,s.me,s.you,++fakeNow)==CD_FRAME_SKIP);
    nativeClock(ownClock,42032100,.152f);
    CHECK(TryLegacyFrameAutomaticParryForOffline(1,s.me,s.you,++fakeNow)==CD_FRAME_SKIP);
    /* Two nearby enemies reach different valid timing windows on the same
       tick. Neither resource band supplies spatial evidence. */
    char actor2[sizeof s.you],mods2[sizeof s.ymods],clock2[0x100]={0},pos2[sizeof s.ypos];
    memcpy(actor2,s.you,sizeof actor2);memcpy(mods2,s.ymods,sizeof mods2);memcpy(pos2,s.ypos,sizeof pos2);
    pointer(s.slots+2*0x38,actor2);pointer(actor2+XA,mods2);pointer(mods2+0x10,clock2);
    pointer(mods2+0x68,pos2);integer(actor2+8,MyId+2);integer(actor2+0x78,MyId+2);
    real(s.ypos+0x88,.8f);real(pos2+0x88,2.7f);integer(s.theirAnim+0xc8,42030000);
    pointer(s.ymods+0x10,enemyClock);nativeClock(enemyClock,42030000,.192f);nativeClock(clock2,42030000,.204f);
    Defense=(COPYDOG_DEFENSE){0};CombatFrameClaimed=0;memset(DynamicTargets,0,sizeof DynamicTargets);
    memset(ParryTrack,0,sizeof ParryTrack);before=calls;fakeNow+=100;
    CheckLegacyFrameTargetsForOffline(s.me,NULL,fakeNow);
    CHECK(calls==before&&!DynamicTargets[1].submitted&&!DynamicTargets[2].submitted);
    VirtualFree(g,0,MEM_RELEASE);VirtualFree(h,0,MEM_RELEASE);
    printf("PASS %u dynamic CPU window, native delay learning and production dispatch checks\n",checks);
}
