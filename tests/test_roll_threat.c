#include <windows.h>
static SHORT threatKeys[256];
static SHORT WINAPI ThreatKeys(int key){return (unsigned)key<256?threatKeys[key]:0;}
#define GetAsyncKeyState ThreatKeys
#define CROSSBOW_FIXTURE_ENTRY UnusedCrossbowFixture
#include "test_crossbow_roll.c"

static void sampleBolt(char *bullet)
{
    int id=-1;
    for(unsigned i=0;i<_countof(AttackBulletSpeed);++i)
        if(AttackBulletSpeed[i].speed>0){id=AttackBulletSpeed[i].id;break;}
    integer(bullet+0x4e4,id);integer(bullet+0x4ec,MyId+1);integer(bullet+0x4dc,1);
    real(bullet+0x10,-3.f);real(bullet+0x30,10.f);
    CombatObserveBullet(bullet);
}

/* The publisher runs on the real clock. Non-consuming probes recover its
   exact timestamp without sleeping or forcing a system timer resolution. */
static unsigned stampBetween(unsigned before,unsigned after)
{
    for(unsigned i=0;i<=after-before&&i<256;++i)
        if(CombatPeekBulletThreat(before+i))return before+i;
    return after;
}
static unsigned publish(char *bullet)
{
    /* Each independent trial reuses the buffer for a new shot, not a second
       callback for a bolt whose one dodge was already consumed. */
    integer(bullet+0x4dc,4);
    CombatObserveBullet(bullet);
    unsigned before=RealNow();sampleBolt(bullet);return stampBetween(before,RealNow());
}
int main(void)
{
    void *globals=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(globals==(void*)0x144770000);
    SCENE s={0};setup(&s);char bullet[0x500]={0};
    UpdateRollKey();CombatClearBulletThreat();
    unsigned start=publish(bullet);
    CHECK(!CombatPeekBulletThreat(start-1)&&CombatPeekBulletThreat(start));
    CHECK(CombatPeekBulletThreat(start+128)&&!CombatPeekBulletThreat(start+129));
    CHECK(!CombatTakeBulletThreat(start+130));

    /* The same sample survives a temporary action lock, not a timeout. */
    start=publish(bullet);Defense.phase=DEF_ACTIVE;Defense.self=(uintptr_t)s.me;
    fakeNow=start+35;
    CHECK(!TryBulletAutoRoll(fakeNow)&&!calls&&CombatPeekBulletThreat(start+36));
    Defense.phase=DEF_IDLE;integer(s.hp+0xf0,0);fakeNow=start+80;
    CHECK(!TryBulletAutoRoll(fakeNow)&&!calls&&CombatPeekBulletThreat(fakeNow));
    integer(s.hp+0xf0,100);fakeNow=start+128;
    CHECK(TryBulletAutoRoll(fakeNow)&&calls==1&&Defense.requests==1);
    CHECK(!TryBulletAutoRoll(fakeNow)&&!CombatTakeBulletThreat(fakeNow));
    start=publish(bullet);Defense.phase=DEF_ACTIVE;
    CHECK(!TryBulletAutoRoll(start+30));
    Defense.phase=DEF_IDLE;
    CHECK(!TryBulletAutoRoll(start+129)&&calls==1&&!CombatTakeBulletThreat(start+129));

    /* The control edge is polled even on frames that do not evaluate threats. */
    const int keys[]={VK_SHIFT,VK_SPACE};
    for(unsigned i=0;i<_countof(keys);++i){
        start=publish(bullet);CHECK(CombatPeekBulletThreat(start));
        threatKeys[keys[i]]=(SHORT)0x8000;UpdateRollKey();
        sampleBolt(bullet);CHECK(!CombatTakeBulletThreat(RealNow()));
        threatKeys[keys[i]]=0;UpdateRollKey();
        CHECK(!TryBulletAutoRoll(RealNow())&&calls==1);
    }
    start=publish(bullet);CHECK(CombatPeekBulletThreat(start));
    CopyAction=1;UpdateRollKey();sampleBolt(bullet);CopyAction=0;UpdateRollKey();
    CHECK(!CombatTakeBulletThreat(RealNow()));
    start=publish(bullet);CHECK(CombatPeekBulletThreat(start));
    atomic_store(&AutoRollEnabled,0);UpdateRollKey();sampleBolt(bullet);
    atomic_store(&AutoRollEnabled,1);UpdateRollKey();
    CHECK(!CombatTakeBulletThreat(RealNow()));
    start=publish(bullet);pointer(s.world+0x80,s.you);
    CHECK(!CombatTakeBulletThreat(start)&&calls==1);
    pointer(s.world+0x80,s.me);start=publish(bullet);
    pointer(s.world+0x80,0);UpdateRollKey();pointer(s.world+0x80,s.me);UpdateRollKey();
    CHECK(!CombatTakeBulletThreat(start)&&calls==1);
    CHECK(CombatPeekBulletThreat(publish(bullet)));
    VirtualFree(globals,0,MEM_RELEASE);
    printf("PASS %u bullet lifetime, ownership and manual override checks\n",checks);return 0;
}
