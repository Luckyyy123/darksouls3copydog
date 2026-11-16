/* Offline behavior regression. The included fixtures publish actor state;
   the production observation, scheduling and dispatch functions decide input. */
#define DIAGNOSTIC_FIXTURE_ENTRY Sc23InheritedDiagnosticMain
#include "test_diagnostic_parry.c"

enum { Sc23Slow=26030000,Sc23Parry=48032100 };
static const LocalState sc23Walk={L"Move",300020100,300020100,0.f,1};
static const LocalState sc23Run={L"Move",300020200,300020200,0.f,1};
static const LocalState sc23Parry={L"ParryLeftStart",Sc23Parry,Sc23Parry,.2f,1};

static void sc23Init(DiagnosticScene*s)
{
    initScene(s);
    integer(s->game.equipment+0x2bc,0);
    integer(s->game.equipment+0x2c0,0);
    integer(s->game.equipment+0x32c,110000);
    integer(s->game.equipment+0x330,6070810);
    diagnosticKeys[VK_SHIFT]=1;
}

static int sc23NonMelee(DiagnosticScene*s)
{
    const struct {const wchar_t*name;int visible,native,weapon,mode;} cases[]={
        {L"RollForward",27105,27105,6070810,1},
        {L"AttackCrossbowRightStart",46033500,46033500,14050010,1},
        {L"AttackCrossbowRightFire",46032000,46032000,14050010,1},
        {L"AttackCrossbowRightReload",46033520,46033520,14050010,1},
        /* A stale attack C8 cannot override a currently published roll. */
        {L"RollForward",Sc23Slow,27105,6070810,1},
        /* A stale melee native record cannot authorize a crossbow name. */
        {L"AttackCrossbowRightStart",Sc23Slow,Sc23Slow,14050010,1},
        {L"QuickStep",38020,38020,6070810,1}
    };
    for(unsigned i=0;i<_countof(cases);i++){
        sc23Init(s);
        integer(s->game.equipment+0x2b8,cases[i].mode);
        integer(s->game.equipment+0x330,cases[i].weapon);
        for(int n=1;n<=100;n++)
            tick(s,sc23Walk,cases[i].name,cases[i].visible,cases[i].native,n,0);
        if(calls)printf("non-melee case %u native=%d visible=%d queued=%d\n",
                        i,cases[i].native,cases[i].visible,calls);
        CHECK(calls==0);
        CHECK(Defense.requests==0);
    }
    return 0;
}

static int sc23TimedMoving(DiagnosticScene*s,LocalState own)
{
    sc23Init(s);
    for(int n=1;n<=24;n++){
        tick(s,own,L"AttackRightLight1",Sc23Slow,Sc23Slow,n,0);
        CHECK(calls==0&&!DiagnosticEpisodes[1].spent);
    }
    CHECK(results(CD_DIAG_TIMING_WAIT)>0);
    int firstPhase=0;
    for(int n=25;n<=90;n++){
        int before=calls;
        tick(s,own,L"AttackRightLight1",Sc23Slow,Sc23Slow,n,0);
        if(calls>before&&!firstPhase)firstPhase=n*StepMs;
        CHECK(calls<=1);
    }
    CHECK(calls==1&&lastAction==ActionLeftParry&&DiagnosticEpisodes[1].spent);
    CHECK(firstPhase>288&&firstPhase<734);
    return 0;
}

static int sc23CancelledAttribution(DiagnosticScene*s)
{
    sc23Init(s);diagnosticKeys[VK_SHIFT]=0;
    CHECK(CopydogDefenseBegin(&Defense,(uintptr_t)s->game.me,(uintptr_t)s->game.you,
          Sc23Slow,0,100,DEF_PARRY,20080300,fakeNow));
    ActiveDefenseTrace[15]=CD_DIAG_FORCED_SOURCE|(UINT64_C(1)<<29);
    DiagnosticConfirmationAmbiguous=1;
    DiagnosticSubmittedOwn=Sc23Parry;
    CopydogDefenseCancel(&Defense,DEF_TARGET_CHANGED,fakeNow);
    for(int n=1;n<=4;n++)tick(s,sc23Parry,L"Move",MoveId,MoveId,n,0);
    CHECK(Defense.phase==DEF_CANCELLED&&Defense.awaitingConfirmation);
    for(int n=5;n<=24;n++)tick(s,sc23Walk,L"Move",MoveId,MoveId,n,0);
    CHECK(Defense.phase==DEF_CANCELLED&&Defense.awaitingConfirmation);
    for(int n=25;n<=44;n++)tick(s,sc23Walk,L"Move",MoveId,MoveId,n,0);
    CHECK(Defense.phase==DEF_IDLE&&!Defense.awaitingConfirmation);
    CHECK(!calls&&!Defense.confirmations);
    return 0;
}

static int sc23NativeLeadsVisible(DiagnosticScene*s,int contradiction)
{
    sc23Init(s);
    int n=1;
    for(;n<=70&&!calls;n++)
        tick(s,sc23Walk,L"AttackRightLight1",MoveId,Sc23Slow,n,0);
    CHECK(calls==1&&Defense.phase==DEF_WAIT_ANIMATION);
    for(int stop=n+6;n<stop;n++){
        tick(s,sc23Walk,L"AttackRightLight1",MoveId,Sc23Slow,n,0);
        CHECK(Defense.phase==DEF_WAIT_ANIMATION);
    }
    CHECK(Defense.cancellations==0);
    if(contradiction==5){
        /* A missing native clock with the old C8 still visible is unknown,
           not a witnessed replacement of the submitted attack episode. */
        pointer(s->game.ymods+0x10,NULL);
        for(int stop=n+6;n<stop;n++)
            tick(s,sc23Walk,L"AttackRightLight1",MoveId,Sc23Slow,n,0);
        CHECK(Defense.phase==DEF_WAIT_ANIMATION&&Defense.cancellations==0&&calls==1);
        pointer(s->game.ymods+0x10,s->remoteClock);
        tick(s,sc23Walk,L"AttackRightLight1",MoveId,Sc23Slow,n,0);
        CHECK(Defense.phase==DEF_WAIT_ANIMATION&&Defense.cancellations==0&&calls==1);
        return 0;
    }
    if(contradiction){
        diagnosticKeys[VK_SHIFT]=0;
        if(contradiction==2)integer(s->game.equipment+0x330,6020810);
        if(contradiction==3)integer(s->game.you+8,MyId+7);
        if(contradiction==4)integer(s->game.you+0x78,MyId+7);
        for(int k=1;k<=4;k++)
            tick(s,sc23Walk,contradiction==1?L"Move":L"AttackRightLight1",
                 MoveId,contradiction==1?MoveId:Sc23Slow,contradiction==1?k:n+k,0);
        CHECK(Defense.phase==DEF_CANCELLED&&Defense.reason==DEF_TARGET_CHANGED);
        for(int k=5;k<=50;k++)tick(s,sc23Walk,L"Move",MoveId,MoveId,k,0);
        CHECK(Defense.phase==DEF_IDLE&&calls==1);
    }
    return 0;
}

static DiagnosticScene*sc23FenceScene;
static unsigned sc23FenceTick;
static int sc23FenceReads,sc23FenceKind,sc23FenceFired;
static void sc23ChangeOnFinalRead(void)
{
    const uint64_t*r=lastGate();
    if(!r||r[2]!=fakeNow||!(r[12]&(UINT64_C(1)<<CD_DIAG_TIMING)))return;
    if(sc23FenceTick!=fakeNow){sc23FenceTick=fakeNow;sc23FenceReads=0;}
    if(++sc23FenceReads!=2)return;
    shiftReadFence=NULL;sc23FenceFired=1;
    if(sc23FenceKind==1)integer(sc23FenceScene->game.equipment+0x330,6020810);
    else if(sc23FenceKind==2)integer(sc23FenceScene->game.you+8,MyId+7);
    else if(sc23FenceKind==3)fakeNow+=200;
    else integer(sc23FenceScene->game.equipment+0x2b8,3);
}
static int sc23FinalReadFence(DiagnosticScene*s,int kind)
{
    sc23Init(s);sc23FenceScene=s;sc23FenceTick=0;
    sc23FenceReads=sc23FenceFired=0;sc23FenceKind=kind;
    /* The first 288 ms are guaranteed early; install the mutation only
       afterwards so it exercises final admission near a feasible slot. */
    int n=1;
    for(;n<=24;n++)tick(s,sc23Walk,L"AttackRightLight1",Sc23Slow,Sc23Slow,n,0);
    CHECK(!calls&&!DiagnosticEpisodes[1].spent);
    shiftReadFence=sc23ChangeOnFinalRead;
    for(;n<=70&&!sc23FenceFired;n++)
        tick(s,sc23Walk,L"AttackRightLight1",Sc23Slow,Sc23Slow,n,0);
    shiftReadFence=NULL;
    CHECK(sc23FenceFired&&!calls&&!DiagnosticEpisodes[1].spent);
    diagnosticKeys[VK_SHIFT]=0;
    for(int stop=n+12;n<stop;n++)tick(s,sc23Walk,L"AttackRightLight1",Sc23Slow,Sc23Slow,n,0);
    CHECK(!calls);
    return 0;
}

/* Retain the named-epoch regressions in the packaged, routinely run suite.
   These cases publish real fixture ring records and use the production loop. */
static int sc23NativeAfterNeutral(DiagnosticScene*s)
{
    sc23Init(s);
    for(int n=1;n<=24;n++)tick(s,sc23Walk,L"AttackRightLight1",Sc23Slow,Sc23Slow,n,0);
    CHECK(calls==0&&DiagnosticEpisodes[1].namedAttack);
    for(int n=1;n<=8;n++)tick(s,sc23Walk,L"Move",MoveId,MoveId,n,0);
    CHECK(!DiagnosticEpisodes[1].active);
    for(int n=1;n<=5;n++)tick(s,sc23Walk,L"Move",MoveId,Sc23Slow,n,0);
    CHECK(DiagnosticEpisodes[1].active&&DiagnosticEpisodes[1].readable);
    CHECK(!DiagnosticEpisodes[1].namedAttack&&calls==0);
    for(int n=6;n<=70;n++)tick(s,sc23Walk,L"AttackRightLight1",Sc23Slow,Sc23Slow,n,0);
    CHECK(calls==1&&DiagnosticEpisodes[1].spent);
    return 0;
}

static int sc23RetainedSpentPayload(DiagnosticScene*s)
{
    sc23Init(s);
    for(int n=1;n<=70&&!calls;n++)
        tick(s,sc23Walk,L"AttackRightLight1",Sc23Slow,Sc23Slow,n,0);
    CHECK(calls==1&&DiagnosticEpisodes[1].spent);
    unsigned oldSlot=(s->next-1)%CD_PRED_CLOCK_COUNT;
    for(int n=1;n<=5;n++)tick(s,sc23Walk,L"Move",Sc23Slow,MoveId,n,0);
    CHECK(!DiagnosticEpisodes[1].active);
    /* The old slot payload remains in memory; only head/tail are changed to
       read it as drained history. Subsequent ticks publish no target data. */
    unsigned tail=(oldSlot+1)%CD_PRED_CLOCK_COUNT;
    integer(s->remoteClock+CD_PRED_CLOCK_INDICES,(int)tail);
    integer(s->remoteClock+CD_PRED_CLOCK_INDICES+4,(int)tail);
    s->drainRemote=2;
    for(int n=1;n<=10;n++){
        tick(s,sc23Walk,L"AttackRightLight1",Sc23Slow,Sc23Slow,1,1);
        CHECK(calls==1&&DiagnosticEpisodes[1].spent);
    }
    return 0;
}

static int sc23UnseenRetainedPayload(DiagnosticScene*s)
{
    sc23Init(s);
    tick(s,sc23Walk,L"AttackRightLight1",Sc23Slow,Sc23Slow,1,1);
    CHECK(DiagnosticEpisodes[1].active&&DiagnosticEpisodes[1].readable);
    CHECK(DiagnosticEpisodes[1].clock.valid&&calls==0);
    uint32_t sampleTick=DiagnosticEpisodes[1].clock.tick;
    s->drainRemote=2;
    tick(s,sc23Walk,L"AttackRightLight1",Sc23Slow,Sc23Slow,1,1);
    CHECK(DiagnosticEpisodes[1].active&&DiagnosticEpisodes[1].readable);
    CHECK(DiagnosticEpisodes[1].clock.tick==sampleTick);
    s->drainRemote=0;
    for(int n=2;n<=70;n++)tick(s,sc23Walk,L"AttackRightLight1",Sc23Slow,Sc23Slow,n,0);
    CHECK(calls==1&&DiagnosticEpisodes[1].spent);
    return 0;
}

int main(void)
{
    void*world=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    void*equipment=VirtualAlloc((void*)0x144750000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(world==(void*)0x144770000&&equipment==(void*)0x144750000);
    DiagnosticScene s;
    int failed=0;
    failed|=sc23NonMelee(&s);
    failed|=sc23TimedMoving(&s,sc23Walk);
    failed|=sc23TimedMoving(&s,sc23Run);
    failed|=sc23CancelledAttribution(&s);
    failed|=sc23NativeLeadsVisible(&s,0);
    failed|=sc23NativeLeadsVisible(&s,1);
    failed|=sc23NativeLeadsVisible(&s,2);
    failed|=sc23NativeLeadsVisible(&s,3);
    failed|=sc23NativeLeadsVisible(&s,4);
    failed|=sc23NativeLeadsVisible(&s,5);
    failed|=sc23FinalReadFence(&s,1);
    failed|=sc23FinalReadFence(&s,2);
    failed|=sc23FinalReadFence(&s,3);
    failed|=sc23FinalReadFence(&s,4);
    failed|=sc23NativeAfterNeutral(&s);
    failed|=sc23RetainedSpentPayload(&s);
    failed|=sc23UnseenRetainedPayload(&s);
    VirtualFree(equipment,0,MEM_RELEASE);VirtualFree(world,0,MEM_RELEASE);
    CHECK(!failed);
    printf("PASS %u SC23 production-loop parry checks (offline)\n",checks);
    return 0;
}
