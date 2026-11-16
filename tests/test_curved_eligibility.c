/* Historical solver comparison only: explicit offline legacy entry points.
   Current production scheduling is covered by test_sc23_parry/features.c. */
#define CROSSBOW_FIXTURE_ENTRY UnusedCrossbowFixture
#include "test_crossbow_roll.c"
int main(void)
{
    void*g=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(g==(void*)0x144770000);SCENE s={0};setup(&s);
    const int attacks[]={195034000,195034010,195034020};
    for(unsigned i=0;i<_countof(attacks);i++){
        CHECK(!Unparriable(attacks[i]));int first,last;GetAnimAtkTime(attacks[i],&first,&last);
        CHECK(first>0&&last>=first);
    }
    CHECK(Unparriable(195036400)&&Unparriable(195034001));
    integer(s.equipment+0x2b8,3);integer(s.equipment+0x330,6081010);
    int first,last,start,end;GetAnimAtkTime(attacks[0],&first,&last);
    CHECK(CopydogParryWindow(first,last,Config.ParryStartup,Config.ParryAhead,&start,&end));
    ParryTrack[1]=(COPYDOG_PARRY_TRACK){.self=(uintptr_t)s.me,.actor=(uintptr_t)s.you,.animation=attacks[0],
        .stable=1,.started=fakeNow-(unsigned)start};
    integer(s.theirAnim+0xc8,attacks[0]);Config.KeyCondition=0;Config.ParryDistance=4;
    atomic_store(&AutoRollEnabled,0);
    CheckAnimation(1);CheckLegacyFrameTargetsForOffline(s.me,NULL,fakeNow);
    CHECK(!SubmitDefense(ActionLeftParry,DEF_PARRY,s.you,attacks[0],20080000,fakeNow));
    CHECK(!SubmitDefense(ActionRightParry,DEF_PARRY,s.you,attacks[0],20080000,fakeNow));
    CHECK(!ParryTrack[1].spent&&!Defense.requests&&!calls);
    VirtualFree(g,0,MEM_RELEASE);
    printf("PASS %u Murakumo exact eligibility and retired legacy dispatch checks\n",checks);
}
