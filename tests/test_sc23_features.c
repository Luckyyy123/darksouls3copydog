/* The foreground/key adapters belong to this offline fixture only. */
#include <windows.h>
#include <stdint.h>
static HWND WINAPI FeatureForeground(void){return (HWND)(uintptr_t)1;}
static DWORD WINAPI FeatureForegroundPid(HWND window,LPDWORD pid)
{(void)window;*pid=GetCurrentProcessId();return 1;}
#define GetForegroundWindow FeatureForeground
#define GetWindowThreadProcessId FeatureForegroundPid
static int featureRollCatchNativeCalls;
static int FeatureRollCatchNative(char*context)
{if(!context)return 0;++featureRollCatchNativeCalls;return 1;}
#define COPYDOG_ROLL_CATCH_PLAY(context) FeatureRollCatchNative(context)
#define DIAGNOSTIC_FIXTURE_ENTRY Sc23FeaturesInheritedDiagnosticMain
#include "test_diagnostic_parry.c"

static const LocalState featureIdle={L"Idle",0,0,1.f,0};
static void featureInit(DiagnosticScene*s)
{
    initScene(s);ready=1;
    Config.BulletDodge=0;Config.BackstabDist=0;Config.RollDistance=4;
    Config.BackstabRad=.8f;
    integer(s->game.equipment+0x2bc,0);integer(s->game.equipment+0x2c0,0);
    integer(s->game.equipment+0x32c,110000);integer(s->game.equipment+0x330,16071010);
    integer(s->game.ownAnim+0xc8,0);real(s->game.state+0x224,1.f);s->game.state[0x228]=0;
    memcpy(s->game.ownNames+0x898,L"Idle",sizeof L"Idle");
    diagnosticKeys[VK_SHIFT]=0;atomic_store(&AutoRollEnabled,1);
    RollKey=(COPYDOG_COPY_KEY){0};
    featureRollCatchNativeCalls=0;
}

/* Publish memory without selecting an action. PollCombatActions below is the
   production worker dispatcher; this fixture does not duplicate its ordering. */
static void featureTick(DiagnosticScene*s,LocalState own,const wchar_t*name,
                        int visible,int native,int phase)
{
    SCENE*g=&s->game;fakeNow+=StepMs;
    memcpy(g->ownNames+0x898,own.name,(wcslen(own.name)+1)*sizeof *own.name);
    memcpy(g->theirNames+0x898,name,(wcslen(name)+1)*sizeof *name);
    integer(g->ownAnim+0xc8,own.visible);real(g->state+0x224,own.progress);g->state[0x228]=(char)own.busy;
    integer(g->theirAnim+0xc8,visible);
    if(s->localAnimation!=own.native){s->localAnimation=own.native;s->localPhase=0;}
    clockRecord(s->localClock,s->next,own.native,(int)++s->localPhase,0);
    clockRecord(s->remoteClock,s->next,native,phase,0);++s->next;
    UpdateDefenseState(fakeNow);ObserveRollQ(g->me,fakeNow);
    CombatFrameClaimed=0;
    ObserveParryTargets(g->me,NULL,fakeNow);ObserveEarlyTargets(g->me,fakeNow);
    PollCombatActions(g->me,NULL,fakeNow);
}

static int featureRollReachable(DiagnosticScene*s)
{
    featureInit(s);
    for(int n=1;n<=60&&!calls;n++)
        featureTick(s,featureIdle,L"AttackRightLight1",EnemyR1,EnemyR1,n);
    CHECK(atomic_load(&CopydogDiagnosticParryEnabled)==1);
    CHECK(calls==1&&lastAction==ActionRollForward&&Defense.kind==DEF_ROLL);
    return 0;
}

static int featureShiftPause(DiagnosticScene*s)
{
    featureInit(s);diagnosticKeys[VK_SHIFT]=1;
    /* This attack has no current diagnostic parry model. Shift must not let
       its automatic-roll fallback bypass the player's pause reservation. */
    for(int n=1;n<=80;n++)
        featureTick(s,featureIdle,L"SkillAttack",144038300,144038300,n);
    CHECK(AutoRollPaused()&&calls==0);
    featureInit(s);diagnosticKeys[VK_SHIFT]=1;
    for(int n=1;n<=12;n++)featureTick(s,featureIdle,L"Move",MoveId,MoveId,n);
    CHECK(AutoRollPaused()&&calls==0);
    diagnosticKeys[VK_SHIFT]=0;
    for(int n=1;n<=60&&!calls;n++)
        featureTick(s,featureIdle,L"AttackRightLight1",EnemyR1,EnemyR1,n);
    CHECK(!AutoRollPaused()&&calls==1&&lastAction==ActionRollForward);
    return 0;
}

static int featureF7Toggle(DiagnosticScene*s)
{
    featureInit(s);atomic_store(&AutoRollEnabled,0);
    diagnosticKeys[VK_F7]=1;UpdateRollKey();
    CHECK(atomic_load(&AutoRollEnabled)==1);
    UpdateRollKey();CHECK(atomic_load(&AutoRollEnabled)==1);
    diagnosticKeys[VK_F7]=0;UpdateRollKey();CHECK(atomic_load(&AutoRollEnabled)==1);
    diagnosticKeys[VK_SHIFT]=1;CHECK(AutoRollPaused());
    diagnosticKeys[VK_SHIFT]=0;CHECK(!AutoRollPaused());
    diagnosticKeys[VK_F7]=1;UpdateRollKey();CHECK(atomic_load(&AutoRollEnabled)==0);
    diagnosticKeys[VK_F7]=0;UpdateRollKey();
    for(int n=1;n<=60;n++)featureTick(s,featureIdle,L"AttackRightLight1",EnemyR1,EnemyR1,n);
    CHECK(calls==0);
    diagnosticKeys[VK_F7]=1;UpdateRollKey();CHECK(atomic_load(&AutoRollEnabled)==1);
    diagnosticKeys[VK_F7]=0;UpdateRollKey();
    for(int n=1;n<=6;n++)featureTick(s,featureIdle,L"Move",MoveId,MoveId,n);
    for(int n=1;n<=60&&!calls;n++)
        featureTick(s,featureIdle,L"AttackRightLight1",EnemyR1,EnemyR1,n);
    CHECK(calls==1&&lastAction==ActionRollForward);
    return 0;
}

static void featureRear(DiagnosticScene*s)
{
    Config.BackstabDist=2.5f;
    real(s->game.ypos+0x88,1.f);real(s->game.ypos+0x74,-(float)M_PI);
}
static int featureBackstabReachable(DiagnosticScene*s,int shift,int keepPending)
{
    featureInit(s);featureRear(s);diagnosticKeys[VK_SHIFT]=(unsigned char)shift;
    for(int n=1;n<=12&&!calls;n++)featureTick(s,featureIdle,L"ItemDrinkEstus",50110,50110,n);
    CHECK(calls==1&&lastAction==ActionBackstab&&BackstabTransaction.pending);
    if(keepPending){
        diagnosticKeys[VK_SHIFT]=(unsigned char)(keepPending==1);
        integer(s->game.equipment+0x330,6070810);
        int first=keepPending==1?38:1,last=keepPending==1?44:80;
        for(int n=first;n<=last;n++)
            featureTick(s,featureIdle,L"AttackRightLight1",26030000,26030000,n);
        CHECK(calls==1&&BackstabTransaction.pending&&!Defense.requests);
    }
    return 0;
}

static int featurePendingRoll(DiagnosticScene*s)
{
    featureInit(s);
    CHECK(SubmitDefense(ActionRollForward,DEF_ROLL,s->game.you,EnemyR1,16071010,fakeNow));
    CHECK(calls==1&&Defense.kind==DEF_ROLL&&Defense.awaitingConfirmation);
    uint32_t submitted=Defense.submitted;uint64_t requests=Defense.requests;
    diagnosticKeys[VK_SHIFT]=1;integer(s->game.equipment+0x330,6070810);
    for(int n=38;n<=44;n++)
        featureTick(s,featureIdle,L"AttackRightLight1",26030000,26030000,n);
    CHECK(calls==1&&Defense.kind==DEF_ROLL&&Defense.submitted==submitted&&Defense.requests==requests);
    CHECK(!DiagnosticEpisodes[1].spent);
    return 0;
}

static int featureOneFrameClaim(DiagnosticScene*s)
{
    featureInit(s);featureRear(s);diagnosticKeys[VK_SHIFT]=1;
    integer(s->game.equipment+0x330,6070810);
    /* The synthetic old drink C8 is backstab eligible while the native attack
       and its melee name are at a diagnostic parry opportunity. */
    featureTick(s,featureIdle,L"AttackRightLight1",50110,26030000,38);
    CHECK(calls==1&&lastAction==ActionLeftParry&&CombatFrameClaimed);
    CHECK(!BackstabTransaction.pending);
    diagnosticKeys[VK_SHIFT]=0;
    CHECK(!SubmitDefense(ActionRollForward,DEF_ROLL,s->game.you,26030000,6070810,fakeNow));
    CheckAnimation(1);
    CHECK(calls==1&&Defense.kind==DEF_PARRY);
    return 0;
}

static int featureNoExtraPunishment(DiagnosticScene*s)
{
    const int targets[]={40002,23030100};
    for(unsigned i=0;i<_countof(targets);i++){
        featureInit(s);diagnosticKeys[VK_SHIFT]=1;
        for(int n=1;n<=60;n++)
            featureTick(s,featureIdle,L"AttackOther",targets[i],targets[i],n);
        CHECK(calls==0&&!HeavyRelease.self);
    }
    return 0;
}

static void featureArmRollCatch(DiagnosticScene*s)
{
    integer(s->game.yhp+0xec,1);
    memcpy(s->game.theirNames+0x898,L"RollingLight",sizeof L"RollingLight");
    RollCatchSelf=(uintptr_t)s->game.me;RollCatchHasAttack=0;
    RollCatch[0]=(COPYDOG_ROLL_CATCH){.actor=(uintptr_t)s->game.you,
        .network_id=MyId+1,.due=fakeNow,.pending=1};
    strcpy(RollCatch[0].name,"rollinglight");
}
static int featureRejectedQueueOwnsFrame(DiagnosticScene*s)
{
    featureInit(s);diagnosticKeys[VK_SHIFT]=1;
    integer(s->game.equipment+0x330,6070810);fakeNow+=40;
    CHECK(TryDoAction(ActionLeftParry));
    int primingCalls=calls;
    /* The real queue's same-action 32 ms throttle rejects this feasible
       automatic attempt. Admission succeeded, so this poll remains claimed. */
    featureTick(s,featureIdle,L"AttackRightLight1",26030000,26030000,38);
    CHECK(calls==primingCalls&&Defense.phase==DEF_CANCELLED&&CombatFrameClaimed);
    CHECK(DiagnosticEpisodes[1].spent&&results(CD_DIAG_QUEUE_FAILED)>0);
    featureArmRollCatch(s);
    TryRollCatch(s->game.me,fakeNow);
    CHECK(featureRollCatchNativeCalls==0&&RollCatch[0].pending);
    diagnosticKeys[VK_SHIFT]=0;
    CHECK(!SubmitDefense(ActionRollForward,DEF_ROLL,s->game.you,26030000,6070810,fakeNow));
    CHECK(calls==primingCalls);
    /* Even without the per-poll claim, the cancelled, unconfirmed defense
       still owns its submitted reservation until the actual drain guard ends. */
    CombatFrameClaimed=0;TryRollCatch(s->game.me,fakeNow);
    CHECK(featureRollCatchNativeCalls==0&&RollCatch[0].pending);
    fakeNow+=400;UpdateDefenseState(fakeNow);
    fakeNow+=12;UpdateDefenseState(fakeNow);
    CHECK(Defense.phase==DEF_IDLE);
    CombatFrameClaimed=0;featureArmRollCatch(s);
    TryRollCatch(s->game.me,fakeNow);
    CHECK(featureRollCatchNativeCalls==1&&!RollCatch[0].pending&&CombatFrameClaimed);
    CHECK(calls==primingCalls);
    return 0;
}

int main(void)
{
    (void)RollCatchPlay;
    void*world=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    void*equipment=VirtualAlloc((void*)0x144750000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(world==(void*)0x144770000&&equipment==(void*)0x144750000);
    DiagnosticScene s;int failed=0;
    failed|=featureRollReachable(&s);
    failed|=featureShiftPause(&s);
    failed|=featureF7Toggle(&s);
    failed|=featureBackstabReachable(&s,0,0);
    failed|=featureBackstabReachable(&s,1,0);
    failed|=featureBackstabReachable(&s,0,1);
    failed|=featureBackstabReachable(&s,0,2);
    failed|=featurePendingRoll(&s);
    failed|=featureOneFrameClaim(&s);
    failed|=featureNoExtraPunishment(&s);
    failed|=featureRejectedQueueOwnsFrame(&s);
    VirtualFree(equipment,0,MEM_RELEASE);VirtualFree(world,0,MEM_RELEASE);
    CHECK(!failed);
    printf("PASS %u SC23 restored-feature checks (offline)\n",checks);
    return 0;
}
