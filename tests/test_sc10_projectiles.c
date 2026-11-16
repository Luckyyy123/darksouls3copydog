#define CROSSBOW_FIXTURE_ENTRY UnusedCrossbowFixture
#include "test_crossbow_roll.c"
#include "../src/projectile_profiles.h"

typedef struct { char data[0x500]; } TestBolt;

static void setBolt(TestBolt *b,int id,float x,float y,float z,float vx,float vy,float vz)
{
    integer(b->data+0x4e4,id);integer(b->data+0x4ec,MyId+1);integer(b->data+0x4dc,1);
    real(b->data+0x10,x);real(b->data+0x14,y);real(b->data+0x18,z);
    real(b->data+0x30,vx);real(b->data+0x34,vy);real(b->data+0x38,vz);
}

int main(void)
{
    void *globals=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(globals==(void*)0x144770000);SCENE s={0};setup(&s);
    const int verified[]={500,800,805,808,810,815,818,820,825,828,830,835,838};
    for(unsigned i=0;i<_countof(verified);++i)CHECK(CopydogVerifiedBoltRadius(verified[i])==.02f);
    const int unsupported[]={-1,50,111,570,840,850,860,861,862,9999999};
    for(unsigned i=0;i<_countof(unsupported);++i)CHECK(CopydogVerifiedBoltRadius(unsupported[i])<0.f);

    TestBolt a={0},b={0};float yaw,closestYaw;
    CombatSetBulletInputPaused(0);
    CombatSamplePlayerMotion(s.me,RealNow());
    setBolt(&a,800,-4,.8f,2,30,0,0);CombatObserveBullet(a.data);
    CHECK(!CombatPeekBulletThreat(RealNow()));
    setBolt(&a,800,-4,3,0,30,0,0);CombatObserveBullet(a.data);
    CHECK(!CombatPeekBulletThreat(RealNow()));
    setBolt(&a,800,0,5,0,0,-30,0);CombatObserveBullet(a.data);
    CHECK(CombatTakeBulletThreatDirection(RealNow(),&yaw)&&isfinite(yaw));

    CombatClearBulletThreat();
    setBolt(&a,800,-3,.8f,0,30,0,0);CombatObserveBullet(a.data);
    CHECK(CombatPeekBulletThreat(RealNow()));
    setBolt(&a,800,-3,.8f,0,-30,0,0);CombatObserveBullet(a.data);
    CHECK(!CombatPeekBulletThreat(RealNow()));

    setBolt(&a,800,-3,.8f,0,30,0,0);CombatObserveBullet(a.data);
    CHECK(CombatTakeBulletThreatDirection(RealNow(),&closestYaw));
    for(unsigned i=0;i<3;++i){CombatObserveBullet(a.data);CHECK(!CombatPeekBulletThreat(RealNow()));}

    /* Same ID and shooter, different live objects and incoming directions.
       Single nearest result supplies a direction oracle independent of the
       implementation's policy for choosing left vs right. */
    setBolt(&b,800,0,.8f,-11,0,0,30);
    for(int order=0;order<2;++order){
        CombatClearBulletThreat();
        CombatObserveBullet(order?b.data:a.data);CombatObserveBullet(order?a.data:b.data);
        CHECK(CombatPeekBulletThreat(RealNow()));
        CHECK(!CombatTakeDueBulletDirection(RealNow(),0,&yaw));
        CHECK(CombatTakeDueBulletDirection(RealNow(),150,&yaw)&&fabsf(closestYaw-yaw)<1e-4f);
        CHECK(CombatPeekBulletThreat(RealNow()));
        CHECK(!CombatTakeDueBulletDirection(RealNow(),150,&yaw));
        CHECK(CombatTakeBulletThreatDirection(RealNow(),&yaw)&&fabsf(closestYaw-yaw)>0.2f);
        CHECK(!CombatPeekBulletThreat(RealNow()));
    }
    /* Six units/s of sustained sideways movement from independent poll
       samples: a previously centered arrow misses and an offset arrow hits. */
    CombatClearBulletThreat();
    uint32_t t=RealNow();
    real(s.pos+0x88,-.288f);CombatSamplePlayerMotion(s.me,t-48);
    real(s.pos+0x88,-.144f);CombatSamplePlayerMotion(s.me,t-24);
    real(s.pos+0x88,0.f);CombatSamplePlayerMotion(s.me,t);
    setBolt(&a,800,-10,.8f,0,30,0,0);CombatObserveBullet(a.data);
    CHECK(!CombatPeekBulletThreat(RealNow()));
    setBolt(&b,800,-10,.8f,2,30,0,0);CombatObserveBullet(b.data);
    CHECK(CombatTakeBulletThreatDirection(RealNow(),&yaw)&&isfinite(yaw));
    /* Cancelling a defense transaction discards EVERY pending observation,
       not the consumed object's identity. Only a new observation of a
       still-relevant unconsumed projectile may arm the next request. */
    CombatClearBulletThreat();
    TestBolt c={0};
    setBolt(&a,800,-2,.8f,0,30,0,0);
    setBolt(&b,800,-5,.8f,0,30,0,0);
    setBolt(&c,805,0,.8f,-9,0,0,30);
    CombatObserveBullet(a.data);CombatObserveBullet(b.data);CombatObserveBullet(c.data);
    CHECK(CombatTakeDueBulletDirection(RealNow(),100,&yaw));
    CHECK(CombatPeekBulletThreat(RealNow()));
    CombatDiscardPendingBullets();
    CHECK(!CombatPeekBulletThreat(RealNow()));
    CombatObserveBullet(a.data);
    CHECK(!CombatTakeBulletThreat(RealNow()));
    CombatObserveBullet(c.data);
    CHECK(CombatTakeBulletThreat(RealNow())&&!CombatPeekBulletThreat(RealNow()));
    VirtualFree(globals,0,MEM_RELEASE);
    printf("PASS %u SC10 projectile profile and observer assertions\n",checks);return 0;
}
