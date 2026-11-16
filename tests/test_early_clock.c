#include "../src/early_clock.h"
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static const char *both="attackbothlight1",*right="attackrightlight1";
static void Observe(COPYDOG_EARLY_STATE *s,COPYDOG_EARLY_PROFILES *p,
    const char *name,int id,uint32_t first,uint32_t now)
{
    CopydogEarlyObserve(s,p,0x1111,7,6020000,3,name,id,first,now);
}
static uint32_t Episode(COPYDOG_EARLY_STATE *s,COPYDOG_EARLY_PROFILES *p,uint32_t now,int id,int lag)
{
    Observe(s,p,"idle",0,now,now);Observe(s,p,"idle",0,now,now+12);
    uint32_t start=now+20;
    Observe(s,p,both,0,now,start);Observe(s,p,both,0,now,start+12);
    for(unsigned elapsed=22;elapsed<(unsigned)lag+12;elapsed+=10)
        Observe(s,p,both,0,now,start+elapsed);
    Observe(s,p,both,id,start+(uint32_t)lag,start+(uint32_t)lag+12);
    return start+(uint32_t)lag+16;
}
int main(void)
{
    COPYDOG_EARLY_STATE s={0};COPYDOG_EARLY_PROFILES p={0};
    int id,lead;int lags[]={188,192,200};
    CHECK(CopydogEarlyNameKind(both)==2&&CopydogEarlyNameKind(right)==1);
    CHECK(!CopydogEarlyNameKind("AttackBothLight1"));
    CHECK(!CopydogEarlyNameKind("attackbothlight10"));
    CHECK(!CopydogEarlyNameKind("xattackbothlight1"));
    CHECK(!CopydogEarlyNameKind(0));
    CHECK(!CopydogEarlyValidId(both,23030000));
    CHECK(!CopydogEarlyValidId(right,25034000));
    CHECK(!CopydogEarlyValidId(both,34000));
    CHECK(CopydogEarlySeed(&p,6020310,3,both,25034000,lags,3));
    COPYDOG_EARLY_PROFILE *profile=CopydogEarlyFindProfile(&p,6020000,3,both,0);
    CHECK(profile&&profile->count==3&&profile->leadMs==192&&profile->ready);
    CHECK(!CopydogEarlyFindProfile(&p,6030000,3,both,0));
    CHECK(!CopydogEarlyFindProfile(&p,6020000,1,both,0));
    CHECK(!CopydogEarlyFindProfile(&p,6020000,3,right,0));
    CHECK(!CopydogEarlySeed(&p,6020000,3,both,23030000,lags,3));
    CHECK(!CopydogEarlySeed(&p,6020000,3,both,25034000,lags,6));

    /* Attaching to an attack already in progress must not establish an origin. */
    Observe(&s,&p,both,0,100,100);
    for(unsigned t=110;t<=320;t+=10)Observe(&s,&p,both,t>=300?25034000:0,300,t);
    CHECK(s.suppressed&&!s.paired&&profile->count==3);
    CHECK(!CopydogEarlyPlan(&s,&p,&id,&lead));

    Observe(&s,&p,"idle",0,321,321);Observe(&s,&p,"idle",0,321,333);
    CHECK(!s.active&&!s.suppressed);
    Observe(&s,&p,both,0,334,340);
    CHECK(!CopydogEarlyPlan(&s,&p,&id,&lead));
    Observe(&s,&p,both,0,334,351);
    CHECK(!CopydogEarlyPlan(&s,&p,&id,&lead));
    Observe(&s,&p,both,0,334,352);
    CHECK(s.started==340&&s.stable&&!s.candidateActive);
    CHECK(CopydogEarlyPlan(&s,&p,&id,&lead)&&id==25034000&&lead==192);
    CopydogEarlyMarkSpent(&s);
    CHECK(!CopydogEarlyPlan(&s,&p,&id,&lead));
    CHECK(CopydogEarlyMapping(&s,&p,&id,&lead)&&id==25034000);

    /* Brief name flashes do not reset the consumed episode or its first seen time. */
    Observe(&s,&p,"idle",0,334,357);
    CHECK(s.candidateActive&&s.spent&&!CopydogEarlyPlan(&s,&p,&id,&lead));
    Observe(&s,&p,both,0,334,362);
    CHECK(s.started==340&&s.spent&&!s.candidateActive);
    for(unsigned t=372;t<550;t+=10)Observe(&s,&p,both,0,334,t);
    Observe(&s,&p,both,25034000,540,552);
    CHECK(s.paired&&s.spent&&profile->count==4);
    CHECK(!CopydogEarlyPlan(&s,&p,&id,&lead));
    CHECK(CopydogEarlyMapping(&s,&p,&id,&lead)&&id==25034000);
    Observe(&s,&p,both,25034000,540,560);
    CHECK(profile->count==4);

    /* Full ID changes require three new mutually consistent samples. */
    memset(&s,0,sizeof s);memset(&p,0,sizeof p);
    uint32_t now=1000;
    now=Episode(&s,&p,now,25034000,180);
    profile=CopydogEarlyFindProfile(&p,6020000,3,both,0);
    CHECK(profile&&profile->count==1&&!profile->ready);
    now=Episode(&s,&p,now,25034000,190);
    CHECK(profile->count==2&&!profile->ready);
    now=Episode(&s,&p,now,25034000,200);
    CHECK(profile->count==3&&profile->ready&&profile->leadMs==190);
    now=Episode(&s,&p,now,26034000,181);
    CHECK(profile->count==1&&!profile->ready&&profile->expectedId==26034000);
    now=Episode(&s,&p,now,26034000,191);
    now=Episode(&s,&p,now,26034000,201);
    CHECK(profile->ready&&profile->count==3&&profile->leadMs==191);
    now=Episode(&s,&p,now,26034000,260);
    CHECK(profile->ready&&profile->count==4&&profile->leadMs==191);
    for(unsigned i=0;i<5;++i)now=Episode(&s,&p,now,26034000,190+(int)i);
    CHECK(profile->ready&&profile->count==5&&profile->leadMs==192);

    /* SC4 epoch1221 had a lone 386.577 ms ID lag among approximately305 ms lags. */
    const int baseline[]={304,305,306,305,304};
    CHECK(CopydogEarlySeed(&p,6020000,3,both,25034000,baseline,5));
    CopydogEarlyAddLag(profile,25034000,387);
    CHECK(profile->ready&&profile->leadMs==305);
    CopydogEarlyAddLag(profile,25034000,386);
    CHECK(profile->ready&&profile->leadMs==305);
    CopydogEarlyAddLag(profile,25034000,388);
    CHECK(profile->ready&&profile->leadMs==387);
    CopydogEarlyAddLag(profile,26034000,305);
    CHECK(!profile->ready&&profile->count==1&&profile->expectedId==26034000);
    CopydogEarlyAddLag(profile,26034000,306);
    CHECK(!profile->ready&&profile->count==2);
    CopydogEarlyAddLag(profile,26034000,304);
    CHECK(profile->ready&&profile->count==3&&profile->leadMs==305);
    const int noMajority[]={100,110,300,310,500};
    CHECK(!CopydogEarlySeed(&p,6020000,3,both,25034000,noMajority,5));
    CHECK(!profile->ready&&profile->count==5);
    const int edge32[]={300,316,332,500,600};
    CHECK(CopydogEarlySeed(&p,6020000,3,both,25034000,edge32,5));
    CHECK(profile->ready&&profile->leadMs==316);
    const int edge33[]={300,316,333,500,600};
    CHECK(!CopydogEarlySeed(&p,6020000,3,both,25034000,edge33,5));
    const int sparseOutlier[]={304,305,387};
    CHECK(!CopydogEarlySeed(&p,6020000,3,both,25034000,sparseOutlier,3));

    /* A weapon or identity change mid-attack is attachment, not a fresh attack. */
    CopydogEarlyObserve(&s,&p,0x1111,7,6030000,3,both,0,now,now);
    CHECK(s.suppressed&&s.weaponBase==6030000&&!s.paired&&!s.spent);
    CopydogEarlyObserve(&s,&p,0x1111,8,6030000,3,both,0,now,now+1);
    CHECK(s.networkId==8&&s.suppressed);
    CopydogEarlyObserve(&s,&p,0x2222,8,6030000,3,both,0,now,now+2);
    CHECK(s.actor==0x2222&&s.suppressed);
    CopydogEarlyObserve(&s,&p,0x2222,8,6030000,1,both,0,now,now+3);
    CHECK(s.mode==1&&s.suppressed);

    /* Reusing an existing C8 ID or pairing a future timestamp is not a lag sample. */
    memset(&s,0,sizeof s);memset(&p,0,sizeof p);
    Observe(&s,&p,"idle",25034000,100,100);
    Observe(&s,&p,both,25034000,100,110);Observe(&s,&p,both,25034000,100,122);
    for(unsigned t=132;t<350;t+=10)Observe(&s,&p,both,25034000,100,t);
    CHECK(!s.paired&&!CopydogEarlyFindProfile(&p,6020000,3,both,0));
    memset(&s,0,sizeof s);
    Observe(&s,&p,"idle",0,100,100);Observe(&s,&p,both,0,100,110);
    Observe(&s,&p,both,25034000,200,122);
    CHECK(!s.paired);
    Observe(&s,&p,both,25034000,100,132);
    CHECK(!s.paired);

    memset(&s,0,sizeof s);now=5000;
    now=Episode(&s,&p,now,25034000,49);CHECK(!s.paired);
    now=Episode(&s,&p,now,25034000,601);CHECK(!s.paired);
    now=Episode(&s,&p,now,25034000,50);CHECK(s.paired);
    now=Episode(&s,&p,now,25034000,600);CHECK(s.paired);
    profile=CopydogEarlyFindProfile(&p,6020000,3,both,0);
    CHECK(profile->count==2&&!profile->ready);

    /* Unsigned millisecond arithmetic remains correct across tick wrap. */
    memset(&s,0,sizeof s);memset(&p,0,sizeof p);
    now=UINT32_MAX-80;
    now=Episode(&s,&p,now,25034000,100);
    profile=CopydogEarlyFindProfile(&p,6020000,3,both,0);
    CHECK(now<200&&s.paired&&profile&&profile->lags[0]==100);
    Observe(&s,&p,both,25034000,now,now+COPYDOG_EARLY_GAP_MS+1);
    CHECK(s.suppressed&&!s.paired);

    /* Bounded storage evicts a whole old key, never aliases different weapons. */
    memset(&p,0,sizeof p);
    for(unsigned i=0;i<COPYDOG_EARLY_CAPACITY+1;++i)
        CHECK(CopydogEarlySeed(&p,6020000+(int)i*10000,3,both,25034000,lags,3));
    unsigned used=0;
    for(unsigned i=0;i<COPYDOG_EARLY_CAPACITY;++i)used+=(unsigned)p.entries[i].used;
    CHECK(used==COPYDOG_EARLY_CAPACITY);
    CHECK(!CopydogEarlyFindProfile(&p,6020000,3,both,0));
    CHECK(CopydogEarlyFindProfile(&p,6020000+32*10000,3,both,0));
    CopydogEarlyObserve(&s,&p,0,0,0,0,0,-1,0,0);
    CHECK(!s.initialized&&!s.active);
    CHECK(!CopydogEarlyPlan(&s,&p,0,&lead));
    printf("PASS %u early clock/key/lag/epoch assertions\n",checks);
    return 0;
}
