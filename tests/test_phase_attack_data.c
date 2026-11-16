#include "../src/phase_attack_data.h"
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)

static const COPYDOG_PHASE_ATTACK_PROFILE *Find(int animation)
{
    for(unsigned i=0;i<COPYDOG_PHASE_ATTACK_COUNT;++i)
        if(CopydogPhaseAttacks[i].animation==animation)return &CopydogPhaseAttacks[i];
    return 0;
}

int main(void)
{
    CHECK(COPYDOG_PHASE_ATTACK_COUNT>400);
    CHECK(!Find(34500)&&!Find(99999999));
    /* These full IDs remain forbidden by the game's/current parry rules.
       Their attacks must nevertheless remain available to auto-roll. */
    CHECK(Find(264034000));
    CHECK(Find(48034500));
    CHECK(Find(259034320));
    CHECK(Find(23030000)&&Find(25034000));
    CHECK(Find(23030320)&&Find(23030321)&&Find(23030340)&&Find(23030341));
    CHECK(Find(25034320)&&Find(25034321)&&Find(25034340)&&Find(25034341));
    CHECK(Find(23030500)&&Find(25034500)&&Find(142033500));
    const COPYDOG_PHASE_ATTACK_PROFILE *p=Find(142033500);
    CHECK(p->count==2);
    const COPYDOG_PHASE_ATTACK_INTERVAL *first=&CopydogPhaseAttackIntervals[p->firstInterval];
    const COPYDOG_PHASE_ATTACK_INTERVAL *second=first+1;
    CHECK(first->judgeId==750&&first->startUs==566667&&first->endUs==666667);
    CHECK(second->judgeId==751&&second->startUs==766667&&second->endUs==866667);
    p=Find(23030321);
    CHECK(p->count==1);
    first=&CopydogPhaseAttackIntervals[p->firstInterval];
    CHECK(first->startUs==300000&&first->endUs==400000);
    p=Find(264034500);
    CHECK(p&&p->count==1);
    first=&CopydogPhaseAttackIntervals[p->firstInterval];
    CHECK(first->startUs==633333&&first->endUs==766667);
    unsigned total=0;
    for(unsigned i=0;i<COPYDOG_PHASE_ATTACK_COUNT;++i){
        p=&CopydogPhaseAttacks[i];
        CHECK(p->animation>=100000&&p->count>0);
        CHECK(i==0||CopydogPhaseAttacks[i-1].animation<p->animation);
        CHECK(p->firstInterval==total);
        CHECK(p->firstInterval+p->count<=COPYDOG_PHASE_ATTACK_INTERVAL_COUNT);
        for(unsigned j=0;j<p->count;++j){
            const COPYDOG_PHASE_ATTACK_INTERVAL *iv=&CopydogPhaseAttackIntervals[p->firstInterval+j];
            CHECK(iv->startUs>=0&&iv->endUs>iv->startUs);
            CHECK(j==0||CopydogPhaseAttackIntervals[p->firstInterval+j-1].startUs<=iv->startUs);
        }
        total+=p->count;
    }
    CHECK(total==COPYDOG_PHASE_ATTACK_INTERVAL_COUNT);
    printf("raw phase attack data: %u checks passed\n",checks);
    return 0;
}
