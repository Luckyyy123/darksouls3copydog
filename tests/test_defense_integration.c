/* Historical solver comparison only: explicit offline legacy entry points.
   Current production scheduling is covered by test_sc23_parry/features.c. */
#include <windows.h>
static unsigned char IntegrationKeys[256];
static SHORT WINAPI IntegrationAsyncKey(int key)
{return key>=0&&key<256&&IntegrationKeys[key]?(SHORT)0x8000:0;}
#define GetAsyncKeyState IntegrationAsyncKey
#define main UnusedLegacyFixtureMain
#include "test_v6_restore.c"
#undef main
int main(void)
{
    void*globals=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    void*equipmentGlobals=VirtualAlloc((void*)0x144750000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(globals==(void*)0x144770000&&equipmentGlobals==(void*)0x144750000);
    char world[0x100]={0},me[0x2100]={0},you[0x2100]={0},modules[0x100]={0},ymod[0x100]={0};
    char ownAnim[0xd0]={0},targetAnim[0xd0]={0},stats[0x100]={0},targetEquipment[0x400]={0};
    char a0[0xa00]={0},a1[0x30]={0},a2[0xc0]={0},action[0x20]={0},equipmentRoot[0x20]={0},ownState[0x229]={0};
    unsigned equipment[209]={0};
    pointer(addr1,world);pointer(world+0x80,me);pointer(me+XA,modules);pointer(you+XA,ymod);
    pointer(modules+0x80,ownAnim);pointer(ymod+0x80,targetAnim);pointer(modules+0x18,stats);
    integer(stats+0xd8,100);integer(stats+0xf0,100);integer(targetAnim+0xc8,25034000);
    pointer(modules,ownState);real(ownState+0x224,1.f);memcpy(a0+0x898,L"Idle",sizeof L"Idle");
    pointer(modules+0x28,a0);pointer(a0+16,a1);pointer(a1+0x28,a2);pointer(a2+0xb8,action);
    pointer(addr2,equipmentRoot);pointer(equipmentRoot+16,equipment);equipment[174]=1;equipment[203]=20080010;
    pointer(you+XB,targetEquipment);integer(targetEquipment+0x2b8,3);integer(targetEquipment+0x330,6020310);
    Config.ParryStartup=333;Config.ParryAhead=1;Config.ParryDistance=3;
    unsigned now=timeGetTime();prevparrytime=0;
    CopydogTraceInit(&CopydogAutoTrace);
    UpdateDefenseState(now);
    CopydogParryObserve(&ParryTrack[1],(uintptr_t)me,(uintptr_t)you,25034000,NAN,now);
    CopydogParryObserve(&ParryTrack[1],(uintptr_t)me,(uintptr_t)you,25034000,NAN,now+12);
    CHECK(!SubmitDefense(ActionLeftParry,DEF_PARRY,you,25034000,20080000,now+12));
    CHECK(!SubmitDefense(ActionLeftParry,DEF_PARRY,you,25034000,20080000,now+44));
    CHECK(!Defense.requests&&!calls&&!prevparrytime);
    /* A recorded request exercises confirmation and damage independently of input dispatch. */
    DefenseHandMode=1;
    CHECK(CopydogDefenseBegin(&Defense,(uintptr_t)me,(uintptr_t)you,25034000,0,100,DEF_PARRY,20080000,now+44));
    CHECK(!CopydogDefenseBegin(&Defense,(uintptr_t)me,(uintptr_t)you,25034000,0,100,DEF_PARRY,20080000,now+45));
    CHECK(Defense.phase==DEF_WAIT_ANIMATION&&Defense.confirmations==0);
    integer(ownAnim+0xc8,48032100);UpdateDefenseState(now+64);UpdateDefenseState(now+76);
    CHECK(Defense.phase==DEF_ACTIVE&&Defense.confirmations==1&&prevparrytime==now+64);
    CHECK(Defense.delayMs==20&&Defense.delayKind==DEF_PARRY);
    uint64_t *confirmedTrace=CopydogAutoTrace.entries[(CopydogAutoTrace.header[5]-1)%TRACE_CAPACITY];
    CHECK(confirmedTrace[1]==TRACE_CONFIRM&&confirmedTrace[17]==1&&confirmedTrace[18]==20&&confirmedTrace[29]==20);
    integer(stats+0xd8,80);UpdateDefenseState(now+80);
    CHECK(Defense.phase==DEF_CANCELLED&&Defense.reason==DEF_DAMAGE);
    integer(ownAnim+0xc8,0);UpdateDefenseState(now+480);
    UpdateDefenseState(now+492);
    CHECK(!CopydogDefenseBusy(&Defense));
    integer(targetAnim+0xc8,25034010);
    CombatFrameClaimed=0;atomic_store(&AutoRollEnabled,1);
    CHECK(SubmitDefense(ActionRollForward,DEF_ROLL,you,25034010,6020000,now+500));
    CHECK(calls==1&&Defense.phase==DEF_WAIT_ANIMATION);
    integer(ownAnim+0xc8,27100);UpdateDefenseState(now+520);UpdateDefenseState(now+532);
    CHECK(Defense.phase==DEF_ACTIVE&&Defense.confirmations==2&&Defense.confirmed==now+520);
    integer(ownAnim+0xc8,0);UpdateDefenseState(now+550);UpdateDefenseState(now+562);
    CHECK(CopydogDefenseBegin(&Defense,(uintptr_t)me,(uintptr_t)you,25034010,0,80,DEF_PARRY,20080000,now+600));
    integer(ownAnim+0xc8,48036900);UpdateDefenseState(now+710);UpdateDefenseState(now+722);
    CHECK(Defense.phase==DEF_ACTIVE&&Defense.confirmations==3);
    CHECK(calls==1);
    uint64_t diagnostics[13];CHECK(CopydogGetDefenseState(diagnostics)==0&&diagnostics[4]==3&&diagnostics[5]==3);
    ObserveParryTargets(me,you,now+1000);
    ObserveParryTargets(me,0,now+1004);
    ObserveParryTargets(me,you,now+1012);
    CHECK(ParryTrack[0].stable&&ParryTrack[0].started==now+1000);
    equipment[203]=11010310;integer(targetEquipment+0x330,4070005);
    CopydogParryObserve(&ParryTrack[1],(uintptr_t)me,(uintptr_t)you,164034000,NAN,now+1100);
    CopydogParryObserve(&ParryTrack[1],(uintptr_t)me,(uintptr_t)you,164034000,NAN,now+1112);
    Defense.haveDelay=1;Defense.delayKind=DEF_PARRY;Defense.delayTool=GetCurrentWeapon(0);
    Defense.delayMs=80;ParryDelayHandMode=1;
    int start,end,profile;
    CHECK(CopydogEmpiricalPlanShifted(11010300,4070005,164034000,1,80,1,1.,0,&start,&end,&profile)>0);
    CHECK(start==0&&end==2&&end<(int)COPYDOG_ANIMATION_STABLE_MS);
    CHECK(!SubmitDefense(ActionRightParry,DEF_PARRY,you,164034000,11010300,now+1112)&&calls==1);
    equipment[174]=2;equipment[203]=20080010;
    CHECK(CopydogParryWindowShifted(600,700,333+20,0,0,&start,&end)&&start==247&&end==379);
    uint64_t traceBeforeReset=CopydogAutoTrace.header[5];
    unsigned parryBeforeReset=prevparrytime;
    Defense.confirmations=3;Defense.cancellations=2;Defense.self=(uintptr_t)you;
    UpdateDefenseState(now+1200);
    CHECK(Defense.confirmations==0&&Defense.cancellations==0);
    CHECK(CopydogAutoTrace.header[5]==traceBeforeReset&&prevparrytime==parryBeforeReset);
    /* Replay the measured early-name clock: request before the delayed target ID. */
    char slots[6*0x38]={0},ownPhysics[0x90]={0},otherPhysics[0x90]={0},targetNames[0xa00]={0};
    pointer(world+0x40,slots);pointer(slots+0x38,you);
    pointer(modules+0x68,ownPhysics);pointer(ymod+0x68,otherPhysics);real(otherPhysics+0x88,1);
    pointer(ymod+0x28,targetNames);integer(you+0x78,53249);
    equipment[174]=1;equipment[203]=11010310;integer(targetEquipment+0x330,6020310);
    Config.KeyCondition=0;Config.RollDistance=4;CombatFrameClaimed=0;
    integer(ownAnim+0xc8,0);integer(targetAnim+0xc8,22100);integer(stats+0xd8,100);
    Defense=(COPYDOG_DEFENSE){.self=(uintptr_t)me,.hp=100};
    me[0x74]=5;atomic_store(&AutoRollEnabled,0);
    CHECK(CopydogEarlySeed(&EarlyProfiles,SC4_EARLY_WEAPON,SC4_EARLY_MODE,SC4_EARLY_NAME,
        SC4_EARLY_ANIMATION,Sc4EarlyLagsMs,(unsigned)_countof(Sc4EarlyLagsMs)));
    ParryTrack[1]=(COPYDOG_PARRY_TRACK){.self=(uintptr_t)me,.actor=(uintptr_t)you,.stable=1,.animation=22100,.started=now+1900};
    memcpy(targetNames+0x898,L"Idle",sizeof L"Idle");ObserveEarlyTargets(me,now+1980);
    memcpy(targetNames+0x898,L"AttackBothLight1",sizeof L"AttackBothLight1");
    ObserveEarlyTargets(me,now+2000);ObserveEarlyTargets(me,now+2012);
    for(unsigned tick=2028;tick<=2232;tick+=16)ObserveEarlyTargets(me,now+tick);
    int learnedId,learnedLead;CHECK(CopydogEarlyPlan(&EarlyTrack[1],&EarlyProfiles,&learnedId,&learnedLead));
    CHECK(learnedId==25034000&&learnedLead==305);
    CheckLegacyFrameTargetsForOffline(me,NULL,now+2233);CheckLegacyFrameTargetsForOffline(me,NULL,now+2234);
    CHECK(Defense.phase==DEF_IDLE&&!EarlyTrack[1].spent&&calls==1);
    /* A known name and later visible ID without a native clock cannot authorize an input. */
    memset(&EarlyProfiles,0,sizeof EarlyProfiles);
    integer(targetAnim+0xc8,25034000);
    ObserveParryTargets(me,0,now+2305);ObserveParryTargets(me,0,now+2317);
    ObserveEarlyTargets(me,now+2317);CheckLegacyFrameTargetsForOffline(me,NULL,now+2317);CheckAnimation(1);
    CHECK(Defense.phase==DEF_IDLE&&!ParryTrack[1].spent&&calls==1);
    /* Idle damage clears riposte evidence without creating a stale defense lock. */
    Defense.phase=DEF_IDLE;Defense.hp=100;
    uint64_t cancelledBefore=Defense.cancellations,traceBeforeDamage=CopydogAutoTrace.header[5];
    RiposteTrack[1]=(COPYDOG_RIPOSTE_TRACK){.self=(uintptr_t)me,.actor=(uintptr_t)you,
        .haveAttack=1,.pending=1,.staggered=1,.armed=now+2361};
    integer(stats+0xd8,99);UpdateDefenseState(now+2400);
    CHECK(Defense.phase==DEF_IDLE&&Defense.cancellations==cancelledBefore);
    CHECK(CopydogAutoTrace.header[5]==traceBeforeDamage&&!RiposteTrack[1].pending);
    CHECK(!TryAutomaticRiposte(me,now+2401));
    CHECK(!TryAutomaticRiposte(me,now+2413));
    CHECK(!RiposteTrack[1].pending);
    integer(targetAnim+0xc8,6000);
    CHECK(!TryAutomaticRiposte(me,now+2420));
    CHECK(!TryAutomaticRiposte(me,now+2432)&&calls==1&&!RiposteTrack[1].pending);
    SyntheticKeys['W']=1;ReleaseCopydogKeys();CHECK(!CopydogInputsReleased());
    TestSendInputAllowed=1;ReleaseCopydogKeys();CHECK(CopydogInputsReleased());
    {
        /* SC6 glue runs on the simulated actor memory, without a game process. */
        unsigned phaseStart=now+5000;
        int baseline=calls;
        memset(EarlyTrack,0,sizeof EarlyTrack);memset(&EarlyProfiles,0,sizeof EarlyProfiles);
        memset(PhaseEpisode,0,sizeof PhaseEpisode);
        memset(PhaseRollSegment,0,sizeof PhaseRollSegment);CopydogPhaseQReset(&PhaseQ);
        memset(&PhaseQRequest,0,sizeof PhaseQRequest);
        CombatFrameClaimed=0;CopyAction=0;ready=1;Config.KeyCondition=0;
        equipment[174]=1;equipment[203]=20080010;equipment[204]=3000310;
        integer(stats+0xd8,100);integer(ownAnim+0xc8,0);
        integer(targetEquipment+0x2b8,1);integer(targetEquipment+0x330,16071010);
        integer(targetAnim+0xc8,23030320);
        memcpy(a0+0x898,L"Idle",sizeof L"Idle");
        memcpy(targetNames+0x898,L"AttackRightHeavy1End",sizeof L"AttackRightHeavy1End");
        Defense=(COPYDOG_DEFENSE){.self=(uintptr_t)me,.hp=100,.haveDelay=1,
            .delayKind=DEF_PARRY,.delayTool=20080000,.delayMs=199};
        ParryDelayHandMode=1;
        EarlyTrack[1]=(COPYDOG_EARLY_STATE){.actor=(uintptr_t)you,.networkId=53249,
            .weaponBase=16070000,.mode=1,.started=phaseStart,.lastSeen=phaseStart,
            .initialized=1,.active=1,.stable=1,.initialAnim=23030320};
        strcpy(EarlyTrack[1].name,"attackrightheavy1end");
        CheckLegacyFrameTargetsForOffline(me,NULL,phaseStart+70);CHECK(calls==baseline);
        SeedPhaseMappings();
        CHECK(CopydogEarlyMapping(&EarlyTrack[1],&EarlyProfiles,&learnedId,&learnedLead));
        CHECK(learnedId==23030321&&learnedLead>=190&&learnedLead<=220);
        EarlyTrack[1].mode=3;
        CheckLegacyFrameTargetsForOffline(me,NULL,phaseStart+70);CHECK(calls==baseline);
        EarlyTrack[1].mode=1;
        CopydogPhaseEpisodeObserve(&PhaseEpisode[1],(uintptr_t)you,53249,16070000,1,EarlyTrack[1].name,phaseStart);
        COPYDOG_PHASE_KEY targetKey={16070000,1,23030321,"attackrightheavy1end"};
        COPYDOG_PHASE_KEY shieldKey={20080000,1,48032100,"parryleftstart"};
        COPYDOG_PHASE_PLAN nativePlan;
        CHECK(CopydogPhasePlan(&targetKey,&shieldKey,0,28,0,0,0,&nativePlan)&&nativePlan.executable);
        unsigned due=(unsigned)nativePlan.windows[0].preferredMs;
        CHECK(due<(unsigned)learnedLead);
        unsigned window=0;
        CHECK(!CopydogPhaseDue(&nativePlan,due-1,&window)&&CopydogPhaseDue(&nativePlan,due,&window));
        CheckLegacyFrameTargetsForOffline(me,NULL,phaseStart+due-1);CheckLegacyFrameTargetsForOffline(me,NULL,phaseStart+due);
        CHECK(calls==baseline&&Defense.phase==DEF_IDLE&&!CopydogPhaseEpisodeSpent(&PhaseEpisode[1]));
        CHECK(GetActorAnim(you)==23030320&&GetActorAnim(me)==0);

        /* A previously submitted heavy Start owns the following End as well. */
        phaseStart+=1000;CombatFrameClaimed=0;
        Defense=(COPYDOG_DEFENSE){.self=(uintptr_t)me,.hp=100};
        CopydogPhaseEpisodeReset(&PhaseEpisode[1]);
        CopydogPhaseEpisodeObserve(&PhaseEpisode[1],(uintptr_t)you,53249,16070000,1,"attackrightheavy1start",phaseStart);
        memcpy(targetNames+0x898,L"AttackRightHeavy1Start",sizeof L"AttackRightHeavy1Start");
        CHECK(!SubmitDefense(ActionLeftParry,DEF_PARRY,you,23030320,20080000,phaseStart));
        CHECK(calls==baseline&&!CopydogPhaseEpisodeSpent(&PhaseEpisode[1]));
        CopydogPhaseEpisodeMarkSpent(&PhaseEpisode[1]);
        EarlyTrack[1].started=phaseStart+120;EarlyTrack[1].spent=0;
        CopydogPhaseEpisodeObserve(&PhaseEpisode[1],(uintptr_t)you,53249,16070000,1,"attackrightheavy1end",phaseStart+120);
        CheckLegacyFrameTargetsForOffline(me,NULL,phaseStart+120+due);
        CHECK(CopydogPhaseEpisodeSpent(&PhaseEpisode[1])&&calls==baseline);
        CHECK(!Unparriable(264034500)&&Unparriable(264034000));

        /* Forced mode uses configured lead + q, never the 199ms visible-ID D. */
        phaseStart+=1000;CombatFrameClaimed=0;
        equipment[174]=3;Config.ParryStartup=133;
        Defense=(COPYDOG_DEFENSE){.self=(uintptr_t)me,.hp=100,.haveDelay=1,
            .delayKind=DEF_PARRY,.delayTool=3000300,.delayMs=199};
        ParryDelayHandMode=3;CopydogPhaseQReset(&PhaseQ);
        CopydogPhaseEpisodeReset(&PhaseEpisode[1]);EarlyTrack[1].started=phaseStart;EarlyTrack[1].spent=0;
        CopydogPhaseEpisodeObserve(&PhaseEpisode[1],(uintptr_t)you,53249,16070000,1,"attackrightheavy1end",phaseStart);
        memcpy(targetNames+0x898,L"AttackRightHeavy1End",sizeof L"AttackRightHeavy1End");
        CHECK(!SubmitDefense(ActionRightParry,DEF_PARRY,you,23030321,3000300,phaseStart+186));
        CheckLegacyFrameTargetsForOffline(me,NULL,phaseStart+186);
        CHECK(calls==baseline&&Defense.delayMs==199);
        /* Name-to-input Q is a distinct measurement from the visible-ID D. */
        DefenseHandMode=3;
        PhaseSubmitted(me,you,DEF_PARRY,3000300,phaseStart+186,"idle");
        memcpy(a0+0x898,L"ParryRightStart",sizeof L"ParryRightStart");
        ObservePhaseQ(me,phaseStart+199);ObservePhaseQ(me,phaseStart+211);
        CHECK(PhaseQ.count==1&&PhaseQ.values[0]==13&&GetActorAnim(me)==0&&Defense.delayMs==199);

        /* Actual roll glue respects F7-disabled/Shift pause and never late-replays. */
        phaseStart+=1000;CombatFrameClaimed=0;equipment[174]=1;
        memcpy(a0+0x898,L"Idle",sizeof L"Idle");
        Defense=(COPYDOG_DEFENSE){.self=(uintptr_t)me,.hp=100};
        PhaseRollSegment[1]=0;EarlyTrack[1].started=phaseStart;EarlyTrack[1].spent=0;
        const COPYDOG_PHASE_ATTACK_PROFILE *attackProfile=0;
        for(unsigned n=0;n<COPYDOG_PHASE_ATTACK_COUNT;++n)
            if(CopydogPhaseAttacks[n].animation==23030321){attackProfile=&CopydogPhaseAttacks[n];break;}
        CHECK(attackProfile&&attackProfile->count==1);
        const COPYDOG_PHASE_ATTACK_INTERVAL *hit=&CopydogPhaseAttackIntervals[attackProfile->firstInterval];
        COPYDOG_ROLL_PHASE_WINDOW rollPlan;
        CHECK(CopydogRollPhaseWindow(hit->startUs,hit->endUs,0,433333,0,32,0,32,1,&rollPlan));
        atomic_store(&AutoRollEnabled,0);
        CHECK(!TryPhaseAutomaticRoll(1,me,phaseStart+(unsigned)rollPlan.preferredMs)&&calls==baseline);
        atomic_store(&AutoRollEnabled,1);IntegrationKeys[VK_SHIFT]=1;
        CHECK(!TryPhaseAutomaticRoll(1,me,phaseStart+(unsigned)rollPlan.preferredMs)&&calls==baseline);
        IntegrationKeys[VK_SHIFT]=0;
        CHECK(TryPhaseAutomaticRoll(1,me,phaseStart+(unsigned)rollPlan.preferredMs-1)&&calls==baseline);
        Sleep(35); /* The shared native action queue debounces identical rolls on wall time. */
        CHECK(TryPhaseAutomaticRoll(1,me,phaseStart+(unsigned)rollPlan.preferredMs)&&calls==baseline+1);
        CHECK(Defense.kind==DEF_ROLL&&lastAction==ActionRollForward&&(PhaseRollSegment[1]&1));
        CHECK(ActiveDefenseTrace[15]==56&&GetActorAnim(you)==23030320);
        Defense.phase=DEF_IDLE;CombatFrameClaimed=0;
        CHECK(TryPhaseAutomaticRoll(1,me,phaseStart+(unsigned)rollPlan.preferredMs)&&calls==baseline+1);
        PhaseRollSegment[1]=0;EarlyTrack[1].started=phaseStart+1000;
        CHECK(TryPhaseAutomaticRoll(1,me,phaseStart+1000+(unsigned)rollPlan.endMs+1)&&calls==baseline+1);
        atomic_store(&AutoRollEnabled,0);

        /* An unknown tool and a late visible ID do not re-enable legacy submission. */
        phaseStart+=2000;CombatFrameClaimed=0;equipment[203]=20090010;
        Defense=(COPYDOG_DEFENSE){.self=(uintptr_t)me,.hp=100};
        CopydogPhaseEpisodeReset(&PhaseEpisode[1]);
        EarlyTrack[1].started=phaseStart;EarlyTrack[1].spent=0;
        CHECK(!CopydogPhaseSelfByName(20090000,1,"parryleftstart"));
        CHECK(CopydogPhaseQBind(&PhaseQ,(uintptr_t)me,20090000,1,"parryleftstart"));
        CHECK(CopydogPhaseQSeed(&PhaseQ,2,22));
        CheckLegacyFrameTargetsForOffline(me,NULL,phaseStart+70);CHECK(calls==baseline+1);
        CHECK(CopydogPhaseQAdd(&PhaseQ,100,111)&&CopydogPhaseQAdd(&PhaseQ,200,213)&&CopydogPhaseQAdd(&PhaseQ,300,315));
        CheckLegacyFrameTargetsForOffline(me,NULL,phaseStart+70);CHECK(calls==baseline+1);
        integer(targetAnim+0xc8,23030321);
        ParryTrack[1]=(COPYDOG_PARRY_TRACK){.self=(uintptr_t)me,.actor=(uintptr_t)you,
            .stable=1,.animation=23030321,.started=phaseStart};
        Config.ParryStartup=133;
        CheckLegacyFrameTargetsForOffline(me,NULL,phaseStart+267);CheckAnimation(1);
        CHECK(calls==baseline+1&&Defense.phase==DEF_IDLE);

        /* An R1 paired to C8 must not turn the independent roll scheduler into a parry sender. */
        phaseStart+=1000;CombatFrameClaimed=0;equipment[203]=20080010;
        Defense=(COPYDOG_DEFENSE){.self=(uintptr_t)me,.hp=100};
        CopydogPhaseEpisodeReset(&PhaseEpisode[1]);
        PhaseRollSegment[1]=0;Config.ParryStartup=333;Config.ParryAhead=1;Latency[0]=128;
        integer(targetEquipment+0x2b8,3);integer(targetEquipment+0x330,6020310);
        integer(targetAnim+0xc8,25034000);integer(ownAnim+0xc8,0);
        memcpy(a0+0x898,L"Idle",sizeof L"Idle");
        memcpy(targetNames+0x898,L"AttackBothLight1",sizeof L"AttackBothLight1");
        CHECK(CopydogEarlySeed(&EarlyProfiles,SC4_EARLY_WEAPON,SC4_EARLY_MODE,SC4_EARLY_NAME,
            SC4_EARLY_ANIMATION,Sc4EarlyLagsMs,(unsigned)_countof(Sc4EarlyLagsMs)));
        EarlyTrack[1]=(COPYDOG_EARLY_STATE){.actor=(uintptr_t)you,.networkId=53249,
            .weaponBase=6020000,.mode=3,.started=phaseStart-305,.lastSeen=phaseStart,
            .initialized=1,.active=1,.stable=1,.paired=1,.pairedId=25034000,.initialAnim=22100};
        strcpy(EarlyTrack[1].name,"attackbothlight1");
        ParryTrack[1]=(COPYDOG_PARRY_TRACK){.self=(uintptr_t)me,.actor=(uintptr_t)you,
            .stable=1,.animation=25034000,.started=phaseStart};
        atomic_store(&AutoRollEnabled,1);
        (void)TryPhaseAutomaticRoll(1,me,phaseStart+12);
        (void)TryPhaseAutomaticRoll(1,me,phaseStart+44);
        CHECK(calls==baseline+1||(calls==baseline+2&&Defense.kind==DEF_ROLL&&lastAction==ActionRollForward));
        CHECK(!ParryTrack[1].spent);
        atomic_store(&AutoRollEnabled,0);

        /* Fresh session: straight-sword R1 is known to roll only. No visible ID yet. */
        memset(&EarlyProfiles,0,sizeof EarlyProfiles);phaseStart+=2000;
        Config.KeyCondition=VK_SHIFT;IntegrationKeys[VK_SHIFT]=0;
        CombatFrameClaimed=0;atomic_store(&AutoRollEnabled,1);PhaseRollSegment[1]=0;
        Defense=(COPYDOG_DEFENSE){.self=(uintptr_t)me,.hp=100};
        memset(&RollQ,0,sizeof RollQ);
        integer(targetEquipment+0x2b8,1);integer(targetEquipment+0x330,16071010);
        integer(targetAnim+0xc8,22100);
        memset(&ParryTrack[1],0,sizeof ParryTrack[1]);memset(&EarlyTrack[1],0,sizeof EarlyTrack[1]);
        memcpy(targetNames+0x898,L"Idle",sizeof L"Idle");
        for(unsigned step=0;step<=20;step+=4){
            unsigned tick=phaseStart-24+step;
            ObserveParryTargets(me,0,tick);ObserveEarlyTargets(me,tick);
        }
        memcpy(targetNames+0x898,L"AttackRightLight1",sizeof L"AttackRightLight1");
        for(unsigned step=0;step<=12;step+=4){
            ObserveParryTargets(me,0,phaseStart+step);ObserveEarlyTargets(me,phaseStart+step);
        }
        CHECK(EarlyTrack[1].stable&&!EarlyTrack[1].paired&&EarlyTrack[1].started==phaseStart);
        CHECK(ParryTrack[1].stable&&ParryTrack[1].animation==22100);
        CHECK(!CopydogEarlyMapping(&EarlyTrack[1],&EarlyProfiles,&learnedId,&learnedLead));
        int sc7Calls=calls;
        CHECK(TryPhaseAutomaticRoll(1,me,phaseStart+12)&&calls==sc7Calls);
        unsigned hitidx=0;
        while(hitidx<COPYDOG_PHASE_ATTACK_COUNT&&CopydogPhaseAttacks[hitidx].animation!=23030000)++hitidx;
        CHECK(hitidx<COPYDOG_PHASE_ATTACK_COUNT);
        const COPYDOG_PHASE_ATTACK_INTERVAL*r1=&CopydogPhaseAttackIntervals[CopydogPhaseAttacks[hitidx].firstInterval];
        CHECK(CopydogRollPhaseWindow(r1->startUs,r1->endUs,0,433333,0,32,0,32,1,&rollPlan));
        unsigned rollRequest=phaseStart+(unsigned)rollPlan.preferredMs;
        CHECK(TryPhaseAutomaticRoll(1,me,rollRequest-1)&&calls==sc7Calls&&!RollQ.pending);
        Sleep(35);
        CHECK(TryPhaseAutomaticRoll(1,me,rollRequest)&&calls==sc7Calls+1);
        CHECK(lastAction==ActionRollForward&&Defense.kind==DEF_ROLL&&Defense.phase==DEF_WAIT_ANIMATION);
        CHECK(RollQ.pending&&RollQ.request==rollRequest&&!RollQ.count);
        CHECK(ActiveDefenseTrace[16]==ROLL_CLOCK_ARCHIVE&&ActiveDefenseTrace[10]==phaseStart&&GetActorAnim(you)==22100);
        /* Submit through the real scheduler. The later visible ID validates the name
           edge without shifting its timestamp or admitting an extra roll. */
        memcpy(a0+0x898,L"RollingLight",sizeof L"RollingLight");
        for(unsigned delta=13;delta<130;delta+=12){
            UpdateDefenseState(rollRequest+delta);ObserveRollQ(me,rollRequest+delta);
        }
        CHECK(RollQ.pending&&RollQ.atName&&RollQ.nameAt==rollRequest+13&&!RollQ.count);
        CHECK(Defense.phase==DEF_WAIT_ANIMATION&&Defense.confirmations==0);
        integer(ownAnim+0xc8,27105);
        UpdateDefenseState(rollRequest+130);ObserveRollQ(me,rollRequest+130);
        UpdateDefenseState(rollRequest+141);ObserveRollQ(me,rollRequest+141);
        CHECK(RollQ.pending&&RollQ.atId&&RollQ.idAt-RollQ.nameAt==117&&!RollQ.count);
        CHECK(Defense.phase==DEF_WAIT_ANIMATION&&Defense.confirmations==0);
        UpdateDefenseState(rollRequest+142);ObserveRollQ(me,rollRequest+142);
        CHECK(RollQ.count==1&&RollQ.values[0]==13&&!RollQ.pending);
        CHECK(Defense.phase==DEF_ACTIVE&&Defense.confirmations==1&&Defense.confirmed==rollRequest+130);
        CHECK(Defense.delayKind==DEF_ROLL&&Defense.delayMs==130&&calls==sc7Calls+1);
        memcpy(a0+0x898,L"Idle",sizeof L"Idle");integer(ownAnim+0xc8,0);
        UpdateDefenseState(rollRequest+160);ObserveRollQ(me,rollRequest+160);
        UpdateDefenseState(rollRequest+172);ObserveRollQ(me,rollRequest+172);
        CHECK(Defense.phase==DEF_IDLE&&RollQ.count==1);
        /* Cold pair on a weapon never seen in the archive. The first real pair
           selects a full ID, but elapsed stays relative to the earlier name. */
        phaseStart+=2000;memset(&EarlyTrack[1],0,sizeof EarlyTrack[1]);
        integer(targetEquipment+0x330,8800010);integer(targetAnim+0xc8,23030000);
        COPYDOG_EARLY_STATE *epoch=&EarlyTrack[1];
        for(unsigned j=0;j<=12;j+=4)CopydogEarlyObserve(epoch,&EarlyProfiles,(uintptr_t)you,53249,8800000,1,"idle",22100,phaseStart-40,phaseStart-32+j);
        for(unsigned j=0;j<=220;j+=4)CopydogEarlyObserve(epoch,&EarlyProfiles,(uintptr_t)you,53249,8800000,1,"attackrightlight1",
            j>=212?23030000:22100,j>=212?phaseStart+200:phaseStart-40,phaseStart+j);
        CHECK(epoch->paired&&epoch->pairedId==23030000&&epoch->started==phaseStart);
        CHECK(!CopydogEarlyMapping(epoch,&EarlyProfiles,&learnedId,&learnedLead));
        ParryTrack[1].animation=23030000;ParryTrack[1].started=phaseStart+200;
        unsigned clockSource=0;
        CHECK(CopydogRollClockId(epoch,&EarlyProfiles,&ParryTrack[1],(uintptr_t)me,(uintptr_t)you,&clockSource)==23030000&&clockSource==ROLL_CLOCK_CURRENT_ID);
        Defense.phase=DEF_IDLE;CombatFrameClaimed=0;PhaseRollSegment[1]=0;
        Sleep(35);
        unsigned dueTick=(unsigned)rollPlan.preferredMs>220?(unsigned)rollPlan.preferredMs:220;
        CHECK(dueTick<=(unsigned)rollPlan.endMs&&TryPhaseAutomaticRoll(1,me,phaseStart+dueTick)&&calls==sc7Calls+2);
        CHECK(ActiveDefenseTrace[10]==phaseStart&&ActiveDefenseTrace[16]==ROLL_CLOCK_CURRENT_ID);
        Defense.phase=DEF_IDLE;CombatFrameClaimed=0;PhaseRollSegment[1]=0;
        CHECK(TryPhaseAutomaticRoll(1,me,phaseStart+rollPlan.endMs+1)&&calls==sc7Calls+2);
        ParryTrack[1].started=phaseStart-1;
        CHECK(CopydogRollClockId(epoch,&EarlyProfiles,&ParryTrack[1],(uintptr_t)me,(uintptr_t)you,&clockSource)<0);
        ParryTrack[1].started=phaseStart+200;ParryTrack[1].animation=20030000;
        CHECK(CopydogRollClockId(epoch,&EarlyProfiles,&ParryTrack[1],(uintptr_t)me,(uintptr_t)you,&clockSource)<0);
        epoch->paired=0;epoch->weaponBase=16070000;
        COPYDOG_EARLY_PROFILE *conflict=CopydogEarlyFindProfile(&EarlyProfiles,16070000,1,epoch->name,1);
        CopydogEarlyAddLag(conflict,20030000,200);
        CHECK(CopydogRollClockId(epoch,&EarlyProfiles,&ParryTrack[1],(uintptr_t)me,(uintptr_t)you,&clockSource)<0);
        atomic_store(&AutoRollEnabled,0);
    }
    VirtualFree(globals,0,MEM_RELEASE);VirtualFree(equipmentGlobals,0,MEM_RELEASE);
    printf("PASS %u defense integration assertions\n",checks);return 0;
}
