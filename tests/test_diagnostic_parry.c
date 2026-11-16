/* Real poll/dispatch entry points with an in-process fixture and a recording
   action queue. A request here is a local call, not an animation confirmation. */
#include <windows.h>
static unsigned char diagnosticKeys[256];
static void (*shiftReadFence)(void);
static SHORT WINAPI DiagnosticAsyncKey(int key)
{
    if(key==VK_SHIFT&&shiftReadFence)shiftReadFence();
    return (unsigned)key<256&&diagnosticKeys[key]?(SHORT)0x8000:0;
}
#define GetAsyncKeyState DiagnosticAsyncKey
#define CROSSBOW_FIXTURE_ENTRY UnusedDiagnosticCrossbowFixture
#include "test_crossbow_roll.c"

enum { MoveId=300020100, EnemyR1=23030000, EnemyR2=23030010, StepMs=12 };
typedef struct {
    SCENE game;
    char gearRoot[0xc0],ownGear[0x400],localClock[0x100],remoteClock[0x100];
    unsigned next;
    int freezeLocal,drainRemote;
    int localAnimation;
    unsigned localPhase;
} DiagnosticScene;
typedef struct {const wchar_t*name;int visible,native;float progress;unsigned busy;} LocalState;
static const LocalState moving={L"Move",MoveId,MoveId,0.f,1};
static const LocalState drinking={L"Drink",50110,50110,.1f,1};
static void clockRecord(char *clock,unsigned n,int animation,int phase,int drained)
{
    unsigned slot=n%CD_PRED_CLOCK_COUNT,tail=(slot+1)%CD_PRED_CLOCK_COUNT;
    char *r=clock+CD_PRED_CLOCK_RECORDS+slot*16;
    integer(r,animation);real(r+4,(phase-1)*StepMs*.001f);real(r+8,phase*StepMs*.001f);real(r+12,4.f);
    integer(clock+CD_PRED_CLOCK_INDICES,(int)tail);integer(clock+CD_PRED_CLOCK_INDICES+4,(int)(drained?tail:slot));
}
static void tick(DiagnosticScene*s,LocalState own,const wchar_t*name,int visible,int native,int phase,int drained)
{
    SCENE*g=&s->game;fakeNow+=StepMs;
    memcpy(g->ownNames+0x898,own.name,(wcslen(own.name)+1)*sizeof *own.name);
    memcpy(g->theirNames+0x898,name,(wcslen(name)+1)*sizeof *name);
    integer(g->ownAnim+0xc8,own.visible);real(g->state+0x224,own.progress);g->state[0x228]=(char)own.busy;
    integer(g->theirAnim+0xc8,visible);
    if(!s->freezeLocal){
        if(s->localAnimation!=own.native){s->localAnimation=own.native;s->localPhase=0;}
        clockRecord(s->localClock,s->next,own.native,(int)++s->localPhase,drained);
    }
    if(s->drainRemote!=2)clockRecord(s->remoteClock,s->next,native,phase,s->drainRemote==1);
    ++s->next;
    CombatFrameClaimed=0;UpdateDefenseState(fakeNow);
    ObserveParryTargets(g->me,NULL,fakeNow);ObserveEarlyTargets(g->me,fakeNow);
    CheckFrameTargets(g->me,NULL,fakeNow);
}
static void initScene(DiagnosticScene*s)
{
    memset(s,0,sizeof *s);SCENE*g=&s->game;
    setup(g);pointer(addr2,s->gearRoot);pointer(s->gearRoot+16,s->ownGear);pointer(g->me+XB,s->ownGear);
    pointer(g->mods+0x10,s->localClock);pointer(g->ymods+0x10,s->remoteClock);
    integer(s->ownGear+0x2b8,1);integer(s->ownGear+0x32c,20080310);integer(s->ownGear+0x330,16070310);
    integer(g->equipment+0x330,16070310);integer(g->me+0x78,MyId);
    fakeNow+=2000;Defense=(COPYDOG_DEFENSE){0};GameCadence=(CD_GAME_CADENCE){0};CopyAction=0;
    ResetFrameWorld(g->me,fakeNow);CopydogTraceInit(&CopydogAutoTrace);
    Config.KeyCondition=VK_SHIFT;Config.ParryDistance=.5f;Config.RollDistance=0.f;
    CopydogEnabled=1;atomic_store(&AutoRollEnabled,0);atomic_store(&CopydogDiagnosticParryEnabled,1);
    shiftReadFence=NULL;memset(diagnosticKeys,0,sizeof diagnosticKeys);calls=0;
    for(int n=1;n<=5;n++)tick(s,moving,L"Move",MoveId,MoveId,n,0);
}
static unsigned results(int verdict)
{
    unsigned count=0;uint64_t end=CopydogAutoTrace.header[5],start=end>TRACE_CAPACITY?end-TRACE_CAPACITY:0;
    for(uint64_t i=start;i<end;i++){
        uint64_t*r=CopydogAutoTrace.entries[i%TRACE_CAPACITY];
        if(r[1]==TRACE_DIAGNOSTIC_FORCED&&(int)r[16]==verdict)++count;
    }
    return count;
}
static const uint64_t*lastGate(void)
{
    uint64_t end=CopydogAutoTrace.header[5],start=end>TRACE_CAPACITY?end-TRACE_CAPACITY:0;
    while(end>start){
        const uint64_t*r=CopydogAutoTrace.entries[(--end)%TRACE_CAPACITY];
        if(r[1]==TRACE_DIAGNOSTIC_GATE)return r;
    }
    return NULL;
}
static int forceOnce(DiagnosticScene*s)
{
    initScene(s);diagnosticKeys[VK_SHIFT]=1;
    /* Simultaneous normal vetoes: the defender is using an item, the range
       is 2 m vs 0.5 m, and the synthetic actors provide no CCD palettes. */
    int submittedResult=0;
    for(int n=1;n<=80;n++){
        if(n==24)diagnosticKeys[VK_SHIFT]=0;
        if(n==35)diagnosticKeys[VK_SHIFT]=1;
        tick(s,drinking,L"AttackRightLight1",n<5?MoveId:EnemyR1,EnemyR1,n,0);
        if(results(CD_DIAG_SUBMITTED))submittedResult=1;
    }
    CHECK(calls==1&&lastAction==ActionLeftParry&&submittedResult);
    const uint64_t*r=lastGate();
    CHECK(r&&r[2]==fakeNow&&(r[13]&(UINT64_C(1)<<CD_DIAG_RANGE))&&
          (r[13]&(UINT64_C(1)<<CD_DIAG_SELF)));
    /* No eligible attack bands remain at 960 ms. Their zero-initialized
       bounds cannot represent an evaluated press opportunity at [0,0]. */
    CHECK(!(r[12]&(UINT64_C(1)<<CD_DIAG_TIMING)));
    for(int n=1;n<=36;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==2&&lastAction==ActionLeftParry);
    for(int n=1;n<=6;n++)tick(s,moving,L"Move",MoveId,MoveId,n,0);
    for(int n=1;n<=20;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==3);
    return 0;
}
static int hitThenRecover(DiagnosticScene*s,LocalState hit,int retained,int cancel)
{
    initScene(s);diagnosticKeys[VK_SHIFT]=1;
    for(int n=1;n<=6;n++)tick(s,hit,L"AttackRightLight1",EnemyR1,EnemyR1,n,retained);
    CHECK(calls==0&&(results(CD_DIAG_HIT)||results(CD_DIAG_UNREADABLE)));
    for(int n=7;n<=24;n++)
        tick(s,moving,cancel?L"Move":L"AttackRightLight1",cancel?MoveId:EnemyR1,cancel?MoveId:EnemyR1,n,0);
    CHECK(calls==(cancel?0:1));
    return 0;
}
static int unreadableAndDisabled(DiagnosticScene*s)
{
    initScene(s);Config.KeyCondition=0;
    for(int n=1;n<=8;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==0);
    diagnosticKeys[VK_SHIFT]=1;pointer(s->game.mods+0x80,NULL);
    for(int n=9;n<=16;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==0&&results(CD_DIAG_UNREADABLE)>0);
    pointer(s->game.mods+0x80,s->game.ownAnim);
    for(int n=17;n<=28;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==1);
    return 0;
}
static int changedBinding(DiagnosticScene*s)
{
    initScene(s);diagnosticKeys[VK_SHIFT]=1;CopyAction=1;
    integer(s->ownGear+0x2b8,3);
    /* Mode 3 needs an actual right-parry timing profile. The former sword
       fixture only exercised unconditional submission without such a model. */
    integer(s->ownGear+0x330,3000000);
    for(int n=1;n<=36;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==1&&lastAction==ActionRightParry);
    /* A raw equipment change in the middle of an uninterrupted clip is
       not a second attack. A later, observed new clip can be attempted. */
    integer(s->game.equipment+0x330,16070311);
    for(int n=37;n<=64;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==1);
    for(int n=1;n<=6;n++)tick(s,moving,L"Move",MoveId,MoveId,n,0);
    for(int n=1;n<=36;n++)tick(s,moving,L"AttackRightLight2",EnemyR2,EnemyR2,n,0);
    CHECK(calls==2&&lastAction==ActionRightParry);
    return 0;
}
static int queueReject(DiagnosticScene*s)
{
    initScene(s);diagnosticKeys[VK_SHIFT]=1;
    /* A real previous request on the fixture queue starts the production
       32 ms throttle. The diagnostic attempt returns false even with a queue. */
    int n=1;
    for(;n<=20&&!results(CD_DIAG_QUEUE_FAILED);++n){
        int before=calls;
        (void)TryDoAction(ActionRightParry);
        (void)TryDoAction(ActionLeftParry);
        int manual=calls-before;
        tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
        CHECK(calls==before+manual);
    }
    CHECK(results(CD_DIAG_QUEUE_FAILED)>0);
    int previous=calls;
    for(;n<=80;++n)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==previous);
    return 0;
}
static int confirmEdge(DiagnosticScene*s,int already)
{
    const LocalState parry={L"ParryLeftStart",48032100,48032100,.2f,1};
    initScene(s);diagnosticKeys[VK_SHIFT]=1;int n=1;
    for(;n<=36&&!calls;n++)tick(s,already?parry:moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==1&&Defense.confirmations==0);
    for(int stop=n+6;n<stop;n++)tick(s,parry,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(Defense.confirmations==(unsigned)!already);
    CHECK(DynamicSelf.count==(unsigned)!already);
    return 0;
}
static int heavyContinuation(DiagnosticScene*s)
{
    initScene(s);diagnosticKeys[VK_SHIFT]=1;
    for(int n=1;n<=10;n++)tick(s,moving,L"AttackRightHeavy1Start",23030320,23030320,n,0);
    for(int n=1;n<=16;n++)tick(s,moving,L"AttackRightHeavy1End",23030321,23030321,n,0);
    CHECK(calls==1);
    return 0;
}
static int oneBadRewind(DiagnosticScene*s)
{
    initScene(s);diagnosticKeys[VK_SHIFT]=1;
    for(int n=1;n<=20;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==1);
    /* One late record replays 12 ms amid the original steadily progressing
       swing (240, 12, 264, ... ms). The old timeline immediately resumes. */
    tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,1,0);
    for(int n=22;n<=32;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==1);
    return 0;
}
static int historicalTarget(DiagnosticScene*s)
{
    initScene(s);diagnosticKeys[VK_SHIFT]=1;s->drainRemote=1;
    /* Both the target name and its visible action have returned to Move.
       An unaged, drained attack record cannot introduce an attack episode. */
    tick(s,moving,L"Move",MoveId,EnemyR1,18,0);
    s->drainRemote=2;
    for(int n=2;n<=8;n++)tick(s,moving,L"Move",MoveId,EnemyR1,18,0);
    CHECK(!calls);
    s->drainRemote=0;
    for(int n=1;n<=36;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==1);
    return 0;
}
static int heldNativeHit(DiagnosticScene*s)
{
    const LocalState nativeHit={L"Move",MoveId,5502,0.f,1};
    initScene(s);diagnosticKeys[VK_SHIFT]=1;
    tick(s,nativeHit,L"AttackRightLight1",EnemyR1,EnemyR1,1,0);
    s->freezeLocal=1;
    for(int n=2;n<=8;n++)tick(s,nativeHit,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(!calls);
    s->freezeLocal=0;
    for(int n=9;n<=36;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==1);
    return 0;
}
static int firstRetainedHit(DiagnosticScene*s)
{
    const LocalState nativeHit={L"Move",MoveId,5502,0.f,1};
    initScene(s);diagnosticKeys[VK_SHIFT]=1;
    tick(s,nativeHit,L"AttackRightLight1",EnemyR1,EnemyR1,1,1);
    CHECK(!calls&&results(CD_DIAG_UNREADABLE)>0&&!results(CD_DIAG_HIT));
    for(int n=2;n<=36;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==1);
    return 0;
}
static int unreadableLocalHand(DiagnosticScene*s)
{
    initScene(s);diagnosticKeys[VK_SHIFT]=1;
    pointer(s->game.me+XB,NULL);
    for(int n=1;n<=8;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(!calls&&results(CD_DIAG_TIMING_UNKNOWN)>0);
    pointer(s->game.me+XB,s->ownGear);integer(s->ownGear+0x2b8,3);
    integer(s->ownGear+0x330,3000000);
    for(int n=9;n<=36;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==1&&lastAction==ActionRightParry);
    return 0;
}
static int overlappingUnconfirmed(DiagnosticScene*s)
{
    const LocalState parry={L"ParryLeftStart",48032100,48032100,.2f,1};
    initScene(s);diagnosticKeys[VK_SHIFT]=1;
    for(int n=1;n<=24&&!calls;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==1&&!Defense.confirmations);
    unsigned request=Defense.submitted;
    /* The second attack's feasible timing cannot steal a submitted queue
       reservation. A veto does not consume its own diagnostic episode. */
    for(int n=1;n<=12;n++)tick(s,moving,L"AttackRightLight2",EnemyR2,EnemyR2,n,0);
    CHECK(calls==1&&Defense.submitted==request&&CopydogDefenseBusy(&Defense));
    CHECK(!DiagnosticEpisodes[1].spent);
    /* The cancelled old command may still execute. Its real parry animation
       keeps the reservation until it ends and the 400 ms guard has elapsed. */
    for(int n=13;n<=18;n++)tick(s,parry,L"AttackRightLight2",EnemyR2,EnemyR2,n,0);
    CHECK(calls==1&&CopydogDefenseBusy(&Defense)&&!Defense.confirmations);
    for(int n=19;n<=38;n++)tick(s,moving,L"AttackRightLight2",EnemyR2,EnemyR2,n,0);
    CHECK(calls==1&&Defense.phase==DEF_IDLE&&!Defense.awaitingConfirmation);
    CHECK(!DiagnosticEpisodes[1].spent);
    CHECK(!DynamicSelf.count);
    for(int n=1;n<=6;n++)tick(s,moving,L"Move",MoveId,MoveId,n,0);
    for(int n=1;n<=36&&calls<2;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==2&&Defense.requests==2&&DiagnosticEpisodes[1].spent);
    return 0;
}
static int retainedNativeBaseline(DiagnosticScene*s,int consistent)
{
    const LocalState parry={L"ParryLeftStart",48032100,48032100,.2f,1};
    const LocalState differentNativeMove={L"Move",MoveId,300020200,0.f,1};
    initScene(s);diagnosticKeys[VK_SHIFT]=1;int n=1;
    /* A drained queue still exposes a readable published sample. Matching
       native/visible movement is a usable non-parry baseline; disagreement
       cannot prove that the later parry belongs to this request. */
    for(;n<=36&&!calls;n++)tick(s,consistent?moving:differentNativeMove,
                              L"AttackRightLight1",EnemyR1,EnemyR1,n,1);
    CHECK(calls==1&&DiagnosticConfirmationAmbiguous==!consistent);
    CHECK(!!(ActiveDefenseTrace[15]&(UINT64_C(1)<<29))==!consistent);
    for(int end=n+6;n<end;n++)tick(s,parry,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==1&&Defense.confirmations==(unsigned)consistent&&DynamicSelf.count==(unsigned)consistent);
    return 0;
}
static DiagnosticScene *fenceScene;
static int postPlanChecks,hitAtFence;
static void hitOnFinalRead(void)
{
    const uint64_t*r=lastGate();
    if(!r||r[2]!=fakeNow||!(r[12]&(UINT64_C(1)<<CD_DIAG_TIMING))||++postPlanChecks!=2)return;
    shiftReadFence=NULL;hitAtFence=1;
    integer(fenceScene->game.ownAnim+0xc8,5502);
}
static int lateSelfHit(DiagnosticScene*s)
{
    const LocalState parry={L"ParryLeftStart",48032100,48032100,.2f,1};
    initScene(s);diagnosticKeys[VK_SHIFT]=1;
    fenceScene=s;postPlanChecks=hitAtFence=0;shiftReadFence=hitOnFinalRead;
    int n=1;
    for(;n<=36&&!hitAtFence;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(hitAtFence&&!calls&&results(CD_DIAG_HIT)>0&&!results(CD_DIAG_QUEUE_FAILED));
    for(;n<=48&&!calls;n++)tick(s,moving,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(calls==1);
    for(int end=n+6;n<end;n++)tick(s,parry,L"AttackRightLight1",EnemyR1,EnemyR1,n,0);
    CHECK(Defense.confirmations==1);
    return 0;
}
#ifndef DIAGNOSTIC_FIXTURE_ENTRY
#define DIAGNOSTIC_FIXTURE_ENTRY main
#endif
int DIAGNOSTIC_FIXTURE_ENTRY(void)
{
    void*world=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    void*equipment=VirtualAlloc((void*)0x144750000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(world==(void*)0x144770000&&equipment==(void*)0x144750000);
    DiagnosticScene scene;
    CHECK(!forceOnce(&scene));
    CHECK(!hitThenRecover(&scene,(LocalState){L"DamageMiddle",5102,5102,0.f,1},0,0));
    CHECK(!hitThenRecover(&scene,(LocalState){L"Move",MoveId,5102,0.f,1},0,0));
    CHECK(!hitThenRecover(&scene,(LocalState){L"Move",MoveId,5102,0.f,1},1,0));
    CHECK(!hitThenRecover(&scene,(LocalState){L"Move",MoveId,5351,0.f,1},0,0));
    CHECK(!hitThenRecover(&scene,(LocalState){L"Move",MoveId,5502,0.f,1},0,0));
    CHECK(!hitThenRecover(&scene,(LocalState){L"Move",MoveId,5600,0.f,1},0,0));
    CHECK(!hitThenRecover(&scene,(LocalState){L"DamageSmall",5002,5002,0.f,1},0,1));
    CHECK(!unreadableAndDisabled(&scene));
    CHECK(!changedBinding(&scene));
    CHECK(!queueReject(&scene));
    CHECK(!confirmEdge(&scene,0));
    CHECK(!confirmEdge(&scene,1));
    CHECK(!heavyContinuation(&scene));
    CHECK(!oneBadRewind(&scene));
    CHECK(!historicalTarget(&scene));
    CHECK(!heldNativeHit(&scene));
    CHECK(!firstRetainedHit(&scene));
    CHECK(!unreadableLocalHand(&scene));
    CHECK(!overlappingUnconfirmed(&scene));
    CHECK(!retainedNativeBaseline(&scene,1));
    CHECK(!retainedNativeBaseline(&scene,0));
    CHECK(!lateSelfHit(&scene));
    VirtualFree(equipment,0,MEM_RELEASE);VirtualFree(world,0,MEM_RELEASE);
    printf("PASS %u forced diagnostic parry integration checks\n",checks);
    return 0;
}
