#include "../src/phase_timing.h"
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)

static COPYDOG_PHASE_KEY Key(int weapon,int mode,const char *name,int animation)
{
    return (COPYDOG_PHASE_KEY){weapon,mode,animation,name};
}

int main(void)
{
    COPYDOG_PHASE_KEY heavy=Key(16070000,1,"attackrightheavy1end",23030321);
    COPYDOG_PHASE_KEY heavy2=Key(16070000,1,"attackrightheavy2end",23030341);
    COPYDOG_PHASE_KEY shield=Key(20080000,1,"parryleftstart",48032100);
    COPYDOG_PHASE_KEY caestus=Key(11010000,1,"parryleftstart",42032100);
    COPYDOG_PHASE_KEY forced=Key(3000000,3,"parryrightstart",27036900);
    COPYDOG_PHASE_PLAN plan,other;
    const COPYDOG_PHASE_TARGET *target=CopydogPhaseFindTarget(&heavy);
    CHECK(target&&target->observations>=3&&target->intervalCount==1);
    CHECK(CopydogPhaseFindSelf(&shield)&&CopydogPhaseFindSelf(&caestus)&&CopydogPhaseFindSelf(&forced));
    CHECK(CopydogPhaseSupportedName("attackrightheavy1end"));
    CHECK(CopydogPhaseSupportedName("attackbothheavy2end"));
    CHECK(!CopydogPhaseSupportedName("attackrightlight1"));
    CHECK(!CopydogPhaseSupportedName("attackbothlight1"));
    CHECK(!CopydogPhaseSupportedName("attackrightheavy1start"));
    CHECK(!CopydogPhaseSupportedName("AttackRightHeavy1End"));
    CHECK(!CopydogPhaseSupportedName("attackrightheavy1endx"));
    CHECK(!CopydogPhaseSupportedName(0));
    CHECK(CopydogPhaseTargetByName(16070000,1,heavy.name)==target);
    CHECK(!CopydogPhaseTargetByName(99990000,1,heavy.name));
    CHECK(CopydogPhaseSelfByName(20080000,1,shield.name)==CopydogPhaseFindSelf(&shield));
    CHECK(!CopydogPhaseSelfByName(20080000,3,shield.name));
    CHECK(!CopydogPhaseSelfByName(20080000,1,0));

    /* Native contact for this End is 300..366.667ms. No C8 lag is added. */
    CHECK(CopydogPhasePlan(&heavy,&shield,0,20,0,0,0,&plan));
    CHECK(plan.executable&&plan.quality==COPYDOG_PHASE_NATIVE_CANDIDATE&&plan.reason==COPYDOG_PHASE_OK);
    CHECK(plan.count==1&&plan.windows[0].startMs==34&&plan.windows[0].endMs==113);
    CHECK(plan.windows[0].preferredMs==73&&plan.windows[0].contactStartUs==300000);
    CHECK(plan.windows[0].contactEndUs==366667);
    CHECK(plan.windows[0].activeStartUs==166667&&plan.windows[0].activeEndUs==333333);
    CHECK(!CopydogPhaseDue(&plan,72,0));
    unsigned index=99;
    CHECK(CopydogPhaseDue(&plan,73,&index)&&index==0);
    CHECK(CopydogPhaseDue(&plan,113,0));
    CHECK(!CopydogPhaseDue(&plan,114,0));
    CHECK(!CopydogPhaseDue(&plan,UINT32_MAX,0));
    CHECK(!CopydogPhaseDue(0,73,0));

    CHECK(CopydogPhasePlan(&heavy2,&shield,0,20,0,0,0,&other));
    CHECK(other.count==1&&other.windows[0].startMs==0&&other.windows[0].endMs==80);
    CHECK(other.windows[0].contactStartUs==266667&&other.windows[0].contactEndUs==300000);
    CHECK(CopydogPhasePlan(&heavy,&caestus,0,20,0,0,0,&other));
    CHECK(other.windows[0].startMs==100&&other.windows[0].endMs==146);

    /* q is measured at the name boundary; an additional 20ms advances once. */
    CHECK(CopydogPhasePlan(&heavy,&shield,20,40,0,0,0,&other));
    CHECK(other.windows[0].startMs==plan.windows[0].startMs-20);
    CHECK(other.windows[0].endMs==plan.windows[0].endMs-20);
    CHECK(CopydogPhasePlan(&heavy,&shield,0,20,20,20,0,&other));
    CHECK(other.windows[0].startMs==14&&other.windows[0].endMs==93);
    CHECK(!CopydogPhasePlan(&heavy,&shield,0,200,0,0,0,&other));
    CHECK(other.reason==COPYDOG_PHASE_NO_COVERAGE&&!other.executable);
    CHECK(!CopydogPhasePlan(&heavy,&shield,0,20,900,900,0,&other));

    CHECK(CopydogPhasePlan(&heavy,&forced,0,20,0,0,0,&other));
    CHECK(other.count>0&&!other.executable&&other.quality==COPYDOG_PHASE_FORCED_UNCALIBRATED);
    CHECK(other.reason==COPYDOG_PHASE_NEEDS_FORCED_CALIBRATION);
    CHECK(!CopydogPhaseDue(&other,(uint32_t)other.windows[0].preferredMs,0));
    CHECK(CopydogPhasePlan(&heavy,&forced,0,20,100,100,1,&other));
    CHECK(other.executable&&other.quality==COPYDOG_PHASE_FORCED_CALIBRATED_CANDIDATE);
    CHECK(other.windows[0].startMs==67&&other.windows[0].endMs==180);

    /* Exact observed mappings and online-learned new weapons use the same TAE,
       but the static fallback must not pretend to have observations or seeds. */
    COPYDOG_PHASE_KEY unseen=heavy;unseen.weaponBase=99990000;
    const COPYDOG_PHASE_TARGET *native=CopydogPhaseFindTarget(&unseen);
    CHECK(native&&native->key.weaponBase==0&&native->observations==0&&native->lagCount==0);
    CHECK(CopydogPhasePlan(&unseen,&shield,0,20,0,0,0,&other));
    CHECK(other.count==plan.count&&other.windows[0].startMs==plan.windows[0].startMs);
    CHECK(other.windows[0].preferredMs==plan.windows[0].preferredMs);
    unseen.mode=3;
    CHECK(!CopydogPhaseFindTarget(&unseen));
    unseen=heavy;unseen.name="attackrightheavy2end";
    CHECK(!CopydogPhaseFindTarget(&unseen));
    unseen=heavy;unseen.animation=23030320;
    CHECK(!CopydogPhaseFindTarget(&unseen));
    unseen=heavy;unseen.weaponBase=0;
    CHECK(!CopydogPhaseFindTarget(&unseen));

    COPYDOG_PHASE_KEY twins=Key(16070000,3,"attackbothleftdash",142033500);
    target=CopydogPhaseFindTarget(&twins);
    CHECK(target&&target->intervalCount==1);
    CHECK(CopydogPhaseContacts[target->firstInterval].judgeId==750);
    CHECK(CopydogPhaseContacts[target->firstInterval].startUs==566667);
    CHECK(CopydogPhaseContacts[target->firstInterval].endUs==600000);
    CHECK(CopydogPhasePlan(&twins,&shield,0,20,0,0,0,&other));
    CHECK(other.count==1&&other.windows[0].contactEndUs==600000);
    CHECK(other.windows[0].startMs==267&&other.windows[0].endMs==380);
    CHECK(!CopydogPhaseDue(&other,780,0));
    COPYDOG_PHASE_KEY harald=Key(6380000,3,"attackbothdash",264034500);
    target=CopydogPhaseFindTarget(&harald);
    CHECK(target&&target->intervalCount==1);
    CHECK(CopydogPhaseContacts[target->firstInterval].originId==51034500);
    CHECK(CopydogPhasePlan(&harald,&shield,0,20,0,0,0,&other));
    CHECK(other.windows[0].contactStartUs==633333&&other.windows[0].contactEndUs==700000);
    CHECK(other.windows[0].startMs==367&&other.windows[0].endMs==446);

    COPYDOG_PHASE_KEY badSelf=shield;badSelf.mode=3;
    CHECK(!CopydogPhasePlan(&heavy,&badSelf,0,20,0,0,0,&other));
    CHECK(other.reason==COPYDOG_PHASE_UNKNOWN_SELF);
    badSelf=shield;badSelf.animation=42032100;
    CHECK(!CopydogPhaseFindSelf(&badSelf));
    badSelf=shield;badSelf.name="parryrightstart";
    CHECK(!CopydogPhaseFindSelf(&badSelf));
    CHECK(!CopydogPhasePlan(0,&shield,0,20,0,0,0,&other)&&other.reason==COPYDOG_PHASE_UNKNOWN_TARGET);
    CHECK(!CopydogPhasePlan(&heavy,0,0,20,0,0,0,&other)&&other.reason==COPYDOG_PHASE_UNKNOWN_SELF);
    CHECK(!CopydogPhasePlan(&heavy,&shield,0,20,0,0,0,0));
    CHECK(!CopydogPhasePlan(&heavy,&shield,-1,20,0,0,0,&other));
    CHECK(!CopydogPhasePlan(&heavy,&shield,20,0,0,0,0,&other));
    CHECK(!CopydogPhasePlan(&heavy,&shield,0,1001,0,0,0,&other));
    CHECK(!CopydogPhasePlan(&heavy,&shield,0,20,-1001,0,0,&other));
    CHECK(!CopydogPhasePlan(&heavy,&shield,0,20,0,1001,0,&other));
    CHECK(!CopydogPhasePlan(&heavy,&shield,0,20,1,0,0,&other));

    /* Audit every generated native interval and every returned coverage bound. */
    CHECK(COPYDOG_PHASE_NATIVE_TARGET_COUNT>200);
    unsigned onsetCases=0;
    for(unsigned i=0;i<COPYDOG_PHASE_NATIVE_TARGET_COUNT;++i){
        const COPYDOG_PHASE_TARGET *p=&CopydogPhaseNativeTargets[i];
        CHECK(p->key.weaponBase==0&&p->observations==0&&p->lagCount==0);
        CHECK(CopydogPhaseSupportedName(p->key.name));
        CHECK(p->intervalCount>0&&p->intervalCount<=COPYDOG_PHASE_MAX_WINDOWS);
        COPYDOG_PHASE_KEY k=p->key;k.weaponBase=99990000;if(!k.mode)k.mode=3;
        CHECK(CopydogPhaseFindTarget(&k)==p);
        for(unsigned j=0;j<p->intervalCount;++j){
            const COPYDOG_PHASE_INTERVAL *c=&CopydogPhaseContacts[p->firstInterval+j];
            CHECK(c->startUs>=0&&c->endUs>c->startUs);
        }
        if(!CopydogPhasePlan(&k,&shield,0,20,0,0,0,&other))continue;
        for(unsigned j=0;j<other.count;++j){
            COPYDOG_PHASE_WINDOW *w=&other.windows[j];
            CHECK(w->startMs>=0&&w->endMs>=w->startMs);
            CHECK(w->preferredMs>=w->startMs&&w->preferredMs<=w->endMs);
            int contactBound=w->onsetOnly?w->contactStartUs:w->contactEndUs;
            if(w->onsetOnly){++onsetCases;CHECK(other.quality&COPYDOG_PHASE_ONSET_ONLY);}
            CHECK((int64_t)w->startMs*1000+w->activeEndUs>=contactBound);
            CHECK((int64_t)(w->endMs+20)*1000+w->activeStartUs<=w->contactStartUs);
        }
    }
    CHECK(onsetCases>0);
    printf("phase timing: %u checks passed\n",checks);
    return 0;
}
