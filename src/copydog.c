#include "copydog.h"
#include "runtime.h"
#include "combat.h"
#include "control.h"
#include "parry.h"
#include "parry_empirical.h"
#include "parry_animations.h"
#include "roll_catch.h"
#include "roll_actionable.h"
#include "defense.h"
#include "auto_trace.h"
#include "early_clock.h"
#include "early_clock_seed.h"
#include "phase_timing.h"
#include "phase_runtime.h"
#include "phase_attack_data.h"
#include "roll_phase.h"
#include "roll_clock.h"
#include "roll_dispatch.h"
#include "crossbow_roll_data.h"
#include "prediction.h"
#include "prediction_limits.h"
#include "dynamic_parry.h"
#include "native_parry_cue.h"
#include "dynamic_parry_data.h"
#include "parry_diagnostic.h"
__declspec(dllexport) _Atomic int CopydogDiagnosticParryEnabled=COPYDOG_DIAGNOSTIC_DEFAULT;
static DiagnosticParryEpisode DiagnosticEpisodes[MAX_REMOTE_PLAYER+1];
__declspec(dllexport) COPYDOG_CONFIG Config={256,0,128,2,2.5,3.5,.8,1.5,.85,2,3,1.2,1,1,'Y',1,1,133};
__declspec(dllexport) _Atomic int Latency[MAX_REMOTE_PLAYER],LastAttackTime[MAX_REMOTE_PLAYER];
__declspec(dllexport) _Atomic(char*) prevLockTarget;
__declspec(dllexport) _Atomic char CopyAction;
__declspec(dllexport) COPYDOG_AUTO_TRACE CopydogAutoTrace;
_Atomic float TargetWalkSpeed;
_Atomic int TargetCurrentMagic,TargetCurrentGoods;
static int ReadVec(const float*p,float out[3]){return p&&readlp(3*sizeof(float),out,p)&&isfinite(out[0])&&isfinite(out[1])&&isfinite(out[2]);}
static void SetFacing(char*p,float a){float*q=GetActorDir(p);if(q)writelpt(float,a,q+1);}
static int CombatFrameClaimed;
static COPYDOG_BACKSTAB_TRANSACTION BackstabTransaction;
static COPYDOG_RIPOSTE_TRACK RiposteTrack[MAX_REMOTE_PLAYER+1];
static void FrameAction(int action){CombatFrameClaimed=1;DoAction(action);}
static int AutomaticActionQueueBusy(char*,unsigned);
enum { HEAVY_RELEASE_MS=192 };
static struct { char* self;int action;uint32_t due; } HeavyRelease;
static void FinishHeavyInput(char*me,uint32_t now)
{
    if(!HeavyRelease.self)return;
    if(me!=HeavyRelease.self){HeavyRelease.self=NULL;return;}
    if((int32_t)(now-HeavyRelease.due)>=0){
        int action=HeavyRelease.action;
        HeavyRelease.self=NULL;FrameAction(action);
    }
}
static COPYDOG_ROLL_CATCH RollCatch[MAX_REMOTE_PLAYER];
static uintptr_t RollCatchSelf;
static unsigned RollCatchLast;
static int RollCatchHasAttack;
static int RollCatchPlay(char *context)
{
    static const unsigned char expected[]={0x40,0x53,0x48,0x83,0xec,0x40};
    unsigned char actual[sizeof expected];
    if(!readlp(sizeof actual,actual,CopydogAddress(RC_PLAY_ANIMATION_ADDRESS))||
       memcmp(actual,expected,sizeof expected))return 0;
    call(RC_PLAY_ANIMATION_ADDRESS,context,RC_ATTACK_NAME);
    return 1;
}
#ifndef COPYDOG_ROLL_CATCH_PLAY
#define COPYDOG_ROLL_CATCH_PLAY(context) RollCatchPlay(context)
#endif
static int RollCatchName(char *actor,char name[RC_NAME_CAP])
{
    wchar_t raw[RC_NAME_CAP];
    return readlp(sizeof raw,raw,&actor,XA,0x28,0x898)&&RcNormalize(raw,name);
}
/* A ranged action is not an incoming bolt. The action name can change before
   the visible ID, including when the crossbow is in the off hand. */
static int CrossbowAnimationOnly(char *actor,int animation)
{
    if(CopydogCrossbowRangedId(animation))return 1;
    char name[RC_NAME_CAP];
    if(!actor||!RollCatchName(actor,name))return 0;
    const char prefix[]="attackcrossbow";
    if(strncmp(name,prefix,sizeof prefix-1)!=0)return 0;
    const char *phase=name+sizeof prefix-1;
    if(!strncmp(phase,"left",4)||!strncmp(phase,"both",4))phase+=4;
    else if(!strncmp(phase,"right",5))phase+=5;
    return !strcmp(phase,"start")||!strcmp(phase,"loop")||!strcmp(phase,"fire")||!strcmp(phase,"reload");
}
static int RollCatchTarget(char *me,char *actor,float *distance)
{
    float a[3],b[3];
    if(!actor||actor==me||readlpt(short,actor+XA,0x18,0xec)<=0||
       !(readlpt(unsigned char,actor+0x74)>4||readlpt(unsigned char,me+0x74)>4)||
       !ReadVec(GetActorPos(me),a)||!ReadVec(GetActorPos(actor),b))return 0;
    float dx=b[0]-a[0],dz=b[2]-a[2];
    *distance=dx*dx+dz*dz;
    return *distance<=RC_RANGE_METRES*RC_RANGE_METRES;
}
static void ObserveRollCatch(char *me,unsigned now)
{
    if(RollCatchSelf!=(uintptr_t)me||CopyAction){
        memset(RollCatch,0,sizeof RollCatch);RollCatchSelf=(uintptr_t)me;
        RollCatchHasAttack=0;
    }
    if(CopyAction)return;
    char self[RC_NAME_CAP];
    int ready=readlpt(short,me+XA,0x18,0xd8)>0&&RollCatchName(me,self)&&RcSelfReady(self);
    for(int i=0;i<MAX_REMOTE_PLAYER;++i){
        char *actor=Player(i+1),name[RC_NAME_CAP];float distance;
        if(!actor||!RollCatchName(actor,name)){
            RcObserve(&RollCatch[i],0,0,0,0,now);continue;
        }
        int eligible=ready&&RollCatchTarget(me,actor,&distance);
        RcObserve(&RollCatch[i],(uintptr_t)actor,readlpt(int,actor+0x78),name,eligible,now);
    }
}
static void TryRollCatch(char *me,unsigned now)
{
    if(CopyAction||CombatFrameClaimed||RollCatchSelf!=(uintptr_t)me||Player0!=me||
       (RollCatchHasAttack&&now-RollCatchLast<RC_RECOVERY_MS))return;
    if(AutomaticActionQueueBusy(me,now))return;
    char self[RC_NAME_CAP];
    if(readlpt(short,me+XA,0x18,0xd8)<=0||!RollCatchName(me,self)||
       !RcSelfReady(self)||!ReadyToAttack())return;
    int best=-1;float nearest=RC_RANGE_METRES*RC_RANGE_METRES+1;
    for(int i=0;i<MAX_REMOTE_PLAYER;++i){
        COPYDOG_ROLL_CATCH *track=&RollCatch[i];
        if(!RcDue(track,now))continue;
        char *actor=Player(i+1),name[RC_NAME_CAP];float distance;
        if((uintptr_t)actor!=track->actor||!actor||readlpt(int,actor+0x78)!=track->network_id||
           !RollCatchName(actor,name)||strcmp(name,track->name)||!RollCatchTarget(me,actor,&distance)){
            track->pending=0;continue;
        }
        if(distance<nearest){nearest=distance;best=i;}
    }
    if(best<0)return;
    char *pre=readlpt(char*,&me,XA,0x28,0x10);
    if(!pre||!getlp(char*,pre+0x28))return;
    if(!COPYDOG_ROLL_CATCH_PLAY(pre+0x28))return;
    RollCatch[best].pending=0;RollCatchLast=now;RollCatchHasAttack=1;CombatFrameClaimed=1;
}
static unsigned prevparrytime;
static COPYDOG_ROLL_Q RollQ;
__declspec(dllexport) _Atomic int AutoRollEnabled; /* Default OFF: first F7 press enables. */
static COPYDOG_COPY_KEY RollKey;
static int AutoRollPaused(void){return !atomic_load(&AutoRollEnabled)||GetAsyncKeyState(VK_SHIFT)<0;}
__declspec(dllexport) DWORD WINAPI CopydogRollEnabledStatus(void*unused){(void)unused;return (DWORD)atomic_load(&AutoRollEnabled);}
static void UpdateRollKey(void){
    static char *directionSelf;
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    int before=atomic_load(&AutoRollEnabled);
    int after=CopydogToggleKey(&RollKey,GetAsyncKeyState(VK_F7)<0,pid==GetCurrentProcessId(),before);
    if(after!=before){atomic_store(&AutoRollEnabled,after);CopydogRollQCancel(&RollQ);CopydogClearRollDirection();}
    char *current=Player0;
    int paused=AutoRollPaused()||GetAsyncKeyState(VK_SPACE)<0||CopyAction||!current;
    CombatSetBulletInputPaused(paused);
    if(!paused&&Config.BulletDodge)CombatSamplePlayerMotion(current,timeGetTime());
    if(paused||current!=directionSelf)CopydogClearRollDirection();
    directionSelf=current;
}
__declspec(dllexport) DWORD WINAPI CopydogRollPauseStatus(void*unused){(void)unused;return AutoRollPaused()?1:0;}
static int AutoRollAction(int action){return action==ActionRollForward||action==ActionQuickStep;}
__declspec(dllexport) COPYDOG_DEFENSE Defense;
/* OnAction may run on a different thread. Never read the polling thread's
   mutable transaction or call its history-based ReadyToRoll there. */
static _Atomic int MirrorDefenseBusy;
/* Local game commands can be forwarded before their animation is published.
   Keep only the priority reservation. No input history is retained. */
typedef struct {char *self;uint32_t tick;unsigned sequence;int before;} LocalActionRequest;
static LocalActionRequest LocalRequest;
static SRWLOCK LocalRequestLock=SRWLOCK_INIT;
static int IsLocalActionRequest(int action)
{
    switch(action){
    case ActionLeftParry: case ActionRightParry: case ActionBackstab:
    case ActionRollForward: case ActionQuickStep: case ActionBackStep:
    case ActionJumpAttack: case ActionJumpAttack2:
    case ActionTwoHandAttack1: case ActionTwoHandAttack2: case ActionTwoHandAttack3:
    case ActionTwoHandRollAttack: case ActionTwoHandRunAttack:
    case ActionTwoHandHeavyAttack1: case ActionTwoHandHeavyChargedAttack1:
    case ActionTwoHandHeavyAttack2: case ActionTwoHandHeavyChargedAttack2:
    case ActionRightHeavyAttack1: case ActionRightHeavyChargedAttack1:
    case ActionRightHeavyAttack2: case ActionRightHeavyChargedAttack2:
    case ActionRightAttack1: case ActionRightAttack2: case ActionRightAttack3:
    case ActionRightRunAttack: case ActionRightRollAttack:
    case ActionLeftMagic: case ActionLeftMagic1: case ActionRightMagic: case ActionRightMagic1:
    case ActionGesture: case ActionGesture1:
        return 1;
    default:return 0;
    }
}
static void NoteLocalAction(char *self,int before,uint32_t now)
{
    AcquireSRWLockExclusive(&LocalRequestLock);
    LocalRequest=(LocalActionRequest){self,now,LocalRequest.sequence+1,before};
    ReleaseSRWLockExclusive(&LocalRequestLock);
}
static LocalActionRequest ReadLocalRequest(void)
{
    AcquireSRWLockShared(&LocalRequestLock);
    LocalActionRequest request=LocalRequest;
    ReleaseSRWLockShared(&LocalRequestLock);
    return request;
}
static int LocalActionPending(char *me,uint32_t now)
{
    LocalActionRequest request=ReadLocalRequest();
    return request.self==me&&me&&(int32_t)(now-request.tick)<COPYDOG_ACTION_CONFIRM_MS;
}
static void ObserveLocalAction(char *me,int own,uint32_t now)
{
    LocalActionRequest request=ReadLocalRequest();
    if(!request.self||request.self!=me)return;
    if((int32_t)(now-request.tick)<COPYDOG_ACTION_CONFIRM_MS&&
       (own<0||own==request.before||ReadAutoRollState(me)!=ROLL_SELF_ACTION))return;
    AcquireSRWLockExclusive(&LocalRequestLock);
    if(LocalRequest.sequence==request.sequence)LocalRequest.self=NULL;
    ReleaseSRWLockExclusive(&LocalRequestLock);
}
static int AutoRollCanSubmit(char *me)
{
    return me&&CopydogEnabled&&!exitThread&&!CopyAction&&!AutoRollPaused()&&
        GetAsyncKeyState(VK_SPACE)>=0&&!AutomaticActionQueueBusy(me,timeGetTime())&&
        ReadAutoRollState(me)==ROLL_SELF_OK&&!LocalActionPending(me,timeGetTime());
}
/* Poll-thread queue ownership is shared by defense, critical and roll-catch
   producers. Character locomotion alone does not own this queue. */
static int AutomaticActionQueueBusy(char*me,unsigned now)
{
    if(!me||CombatFrameClaimed||CopydogDefenseBusy(&Defense)||
       CopydogBackstabLocked(&BackstabTransaction,(uintptr_t)me,GetActorAnim(me),now))return 1;
    for(unsigned i=0;i<=MAX_REMOTE_PLAYER;++i)
        if(RiposteTrack[i].criticalState==RIPOSTE_CRITICAL_WAIT)return 1;
    return 0;
}
/* Polling-thread only. An unchanged consumed animation slot has no timestamp.
   Compare consecutive observations to recognize publications between two polls. */
typedef struct {
    char *self;int handle,hold;
    CD_NATIVE_FRAME last;
    uint32_t changed;
} ParryMoveClock;
static ParryMoveClock SelfMoveClock;
static ParryMoveClock DiagnosticHitClock;
static int DiagnosticSubmittedOwn,DiagnosticConfirmationAmbiguous,DiagnosticLeftBaseline;
static DiagnosticParryEpisode DiagnosticPending;
static unsigned DiagnosticPendingSlot;
static CD_NATIVE_FRAME DiagnosticNativeBaseline;
static int DiagnosticNativeBaselineValid;
static int ObserveParryMoveClock(char *me,uint32_t now)
{
    ParryMoveClock *s=&SelfMoveClock;
    int handle;
    if(!me||!readlpv(handle,me+8)){*s=(ParryMoveClock){0};return 1;}
    if(s->self!=me||s->handle!=handle)*s=(ParryMoveClock){.self=me,.handle=handle};
    CD_NATIVE_FRAME f;int retained;
    if(!CopydogReadPublishedNativeFrame((uintptr_t)me,&f,&retained))return 1;
    int id=f.animation%100000;
    if(id<4000||(19000<=id&&id<27000))s->hold=0;
    else if(s->last.clock!=f.clock||s->last.animation!=f.animation||s->last.current!=f.current){
        /* The first protected sample may have just drained. Give it the same
           bounded hold as a changed phase; repeated reads never extend it. */
        s->hold=1;s->changed=now;
    }
    s->last=f;
    if(s->hold&&now-s->changed>CD_PRED_MAX_AGE_MS)s->hold=0;
    return s->hold;
}
static int FrameParrySelfReady(char *me)
{
    int busyMove;
    if(ReadAutoRollStateDetail(me,&busyMove)!=ROLL_SELF_OK)return 0;
    unsigned now=timeGetTime();
    return !LocalActionPending(me,now)&&(!busyMove||!ObserveParryMoveClock(me,now));
}
static unsigned DefenseHandMode,ParryDelayHandMode;
static uint64_t PendingDefenseTrace[TRACE_WORDS],ActiveDefenseTrace[TRACE_WORDS];
typedef struct {
    uint64_t trace[TRACE_WORDS];
    unsigned slot,tick,cueOnset;
    size_t bandIndex;
    double deadline;
    CD_NATIVE_FRAME frame;
    unsigned diagnostic;
    unsigned *diagnosticAttempted;
    int diagnosticMode,diagnosticRaw;
} FramePress;
static CD_PARRY_CUE NativeCues[MAX_REMOTE_PLAYER+1];
typedef struct {
    uintptr_t actor;
    int handle,network,lastAnimation,nameKind;
    CD_NATIVE_FRAME prior;
    unsigned havePublished,lastPublished,nameReleased,poseObserved;
} NativeCueWatch;
static NativeCueWatch CueWatch[MAX_REMOTE_PLAYER+1];
static unsigned ActiveCueSlot,ActiveCueOnset;
static char ActiveRollName[RC_NAME_CAP];
static int FramePressCurrent(const FramePress*,unsigned);
static void ObserveDiagnosticEpisode(unsigned,char*,char*,unsigned);
static int DiagnosticPressCurrent(const FramePress*,char*,char*,unsigned);
static int DiagnosticPendingTargetStatus(char*,unsigned);
static int ActiveNativeCueStatus(char*,unsigned);
static int ReadRemoteSlot(unsigned i,char**actor)
{
    return i&&i<=MAX_REMOTE_PLAYER&&readlpv(*actor,addr1,0x40,0x38*i)!=NULL;
}
static int RemoteAttackWeapon(char*actor);
static unsigned RemoteHandMode(char*actor);
static void PhaseSubmitted(char*,char*,int,int,unsigned,const char*);
static void TraceDefense(unsigned event,unsigned now,int action)
{
    uint64_t record[TRACE_WORDS];memcpy(record,ActiveDefenseTrace,sizeof record);
    record[1]=event;record[2]=now;record[3]=Defense.self;record[4]=Defense.target;
    record[5]=(uint64_t)(int64_t)Defense.targetAnimation;
    /* A load can already expose the replacement's animation when the old
       actor's input is cancelled. Its last observation is unavailable here. */
    int own=(event==TRACE_CANCEL&&Defense.reason==DEF_SELF_CHANGED)?-1:GetActorAnim(Player0);
    record[6]=(uint64_t)(int64_t)own;record[7]=Defense.tool;
    record[8]=DefenseHandMode;record[19]=Defense.phase;record[20]=Defense.kind;
    if(record[10])record[11]=(uint32_t)(now-(unsigned)record[10]);
    record[21]=Defense.requests;record[22]=Defense.confirmations;
    record[23]=Defense.submitted;record[24]=Defense.confirmed;record[25]=action;
    if(event==TRACE_CANCEL)record[14]=Defense.reason;
    if(event==TRACE_CONFIRM){
        record[17]=Defense.haveDelay;record[18]=Defense.delayMs;
        record[29]=(uint32_t)(Defense.confirmed-Defense.submitted);
    }
    CopydogTraceWrite(&CopydogAutoTrace,record);
}
static int PartialParryContext(char*me,int hp,unsigned now)
{
    return me&&hp>0&&Defense.self==(uintptr_t)me&&Defense.target&&Defense.kind==DEF_PARRY&&
        (Defense.phase==DEF_WAIT_ANIMATION||Defense.phase==DEF_ACTIVE||
         (Defense.phase==DEF_CANCELLED&&Defense.reason==DEF_DAMAGE))&&
        (uint32_t)(now-Defense.submitted)<=3000;
}
static uintptr_t FramePlayer;
static int ReadFramePlayer(char**me)
{
    uintptr_t world,check;char*actor=NULL;
    if(!readlpv(world,addr1)||(world&&!readlpv(actor,world+0x80))||
       !readlpv(check,addr1)||world!=check)return 0;
    *me=actor;return 1;
}
static char* CurrentDefensePlayer(void)
{
    char*me;
    return ReadFramePlayer(&me)&&(!FramePlayer||FramePlayer==(uintptr_t)me)?me:NULL;
}
static void UpdateDefenseState(unsigned now)
{
    char*me=CurrentDefensePlayer();int hp;
    /* The world observer owns player changes. A second, failed or changed read
       inside a submit must not unlock an input which is still in the queue. */
    if(!me||
       (Defense.phase==DEF_CANCELLED&&Defense.reason==DEF_SELF_CHANGED&&Defense.self!=(uintptr_t)me)||
       !readlpv(hp,me+XA,0x18,0xd8))return;
    uint64_t before=Defense.confirmations,cancelled=Defense.cancellations;
    int own=GetActorAnim(me);
    int forced=(ActiveDefenseTrace[15]&CD_DIAG_FORCED_SOURCE)!=0;
    if(forced&&own>=0&&own!=DiagnosticSubmittedOwn)DiagnosticLeftBaseline=1;
    int allowConfirmation=!(forced&&Defense.awaitingConfirmation&&
        (DiagnosticConfirmationAmbiguous||
         (DiagnosticNativeBaselineValid&&CopydogKnownParryAnimation(DiagnosticNativeBaseline.animation))||
         (!DiagnosticLeftBaseline&&CopydogKnownParryAnimation(own)&&own==DiagnosticSubmittedOwn)));
    ObserveLocalAction(me,own,timeGetTime());
    int preservePartial=PartialParryContext(me,hp,now);
    if(preservePartial&&Defense.hp>0&&hp<Defense.hp){
        for(unsigned i=0;i<=MAX_REMOTE_PLAYER;++i)
            if(RiposteTrack[i].self!=(uintptr_t)me||RiposteTrack[i].actor!=Defense.target)
                CopydogRiposteInvalidate(&RiposteTrack[i],(uintptr_t)me,own,now);
    }
    int invalidateRiposte=Defense.self!=(uintptr_t)me||hp<=0||
        (Defense.hp>0&&hp<Defense.hp&&!preservePartial);
    int target=Defense.target?GetActorAnim((char*)Defense.target):-1;
    if(Defense.phase==DEF_WAIT_ANIMATION&&forced){
        int status=DiagnosticPendingTargetStatus((char*)Defense.target,now);
        if(status<0)CopydogDefenseCancel(&Defense,DEF_TARGET_CHANGED,now);
        else target=Defense.targetAnimation; /* Unknown native evidence retains the existing timeout. */
    }else if(Defense.phase==DEF_WAIT_ANIMATION&&(ActiveDefenseTrace[15]&CD_PARRY_TRACE_NATIVE_CUE)&&
       target!=6000&&target!=6010&&!CopydogKnownParryAnimation(own)){
        int status=ActiveNativeCueStatus((char*)Defense.target,now);
        if(status<0)CopydogDefenseCancel(&Defense,DEF_TARGET_CHANGED,now);
        else target=Defense.targetAnimation; /* unknown evidence keeps the existing timeout */
    }else if(Defense.phase==DEF_WAIT_ANIMATION&&Defense.kind==DEF_PARRY&&(target==6000||target==6010)){
        target=Defense.targetAnimation;
    }else if(Defense.phase==DEF_WAIT_ANIMATION&&Defense.kind==DEF_ROLL&&(ActiveDefenseTrace[15]&8u)&&
             target!=Defense.targetAnimation&&ActiveRollName[0]){
        char*actor=(char*)Defense.target;char name[RC_NAME_CAP];
        if(RollCatchName(actor,name)&&!strcmp(name,ActiveRollName)&&
           RemoteAttackWeapon(actor)==(int)ActiveDefenseTrace[27]&&RemoteHandMode(actor)==ActiveDefenseTrace[9])
            target=Defense.targetAnimation;
    }
    /* The old readiness readers maintain timing history for other callers.
       Ending a confirmed roll on this sample also requires name and ID agreement. */
    int released=ReadyToAttack()&&ReadyToRoll();
    if(Defense.phase==DEF_ACTIVE)
        released=Defense.kind==DEF_ROLL&&own>=0&&!CopydogDefenseAnimation(DEF_ROLL,own)&&
                 ReadAutoRollState(me)==ROLL_SELF_OK&&GetActorAnim(me)==own;
    CopydogDefenseObserveAttributed(&Defense,(uintptr_t)me,own,hp,target,released,now,allowConfirmation);
    atomic_store(&MirrorDefenseBusy,CopydogDefenseBusy(&Defense));
    if(Defense.confirmations>before){
        if(Defense.kind==DEF_PARRY){prevparrytime=Defense.confirmed;ParryDelayHandMode=DefenseHandMode;}
        TraceDefense(TRACE_CONFIRM,now,0);
    }
    if(Defense.cancellations>cancelled)TraceDefense(TRACE_CANCEL,now,0);
    if(invalidateRiposte||(Defense.phase==DEF_CANCELLED&&!preservePartial)){
        for(unsigned i=0;i<=MAX_REMOTE_PLAYER;++i)
            CopydogRiposteInvalidate(&RiposteTrack[i],(uintptr_t)me,own,now);
        CopydogClearRollDirection();
        CombatDiscardPendingBullets();
    }
}
/* Same selected hand as GetCurrentWeapon(1), retaining the actual raw item ID.
   A single equipment snapshot avoids combining a new mode with an old slot. */
static int RollEquipmentRaw(int *mode)
{
    unsigned loadout[35],*equip=readlpt(unsigned*,addr2,16);
    *mode=0;
    if(!equip||!readlp(sizeof loadout,loadout,equip+174)||loadout[0]<1||loadout[0]>3)return 0;
    unsigned right=loadout[0]!=2,slot=loadout[1+right];
    if(slot>=3)return 0;
    unsigned raw=loadout[29+slot*2+right];
    if(!raw||raw>INT_MAX)return 0;
    *mode=(int)loadout[0];return (int)raw;
}
static void ObserveRollQ(char *me,unsigned now)
{
    if(!RollQ.self)return;
    int mode=0,raw=me?RollEquipmentRaw(&mode):0;
    if(!CopydogRollQBind(&RollQ,(uintptr_t)me,raw,mode)||!RollQ.pending)return;
    if(Defense.self!=(uintptr_t)me||Defense.kind!=DEF_ROLL||Defense.submitted!=RollQ.request||
       Defense.phase==DEF_CANCELLED||AutoRollPaused()||CopyAction||GetAsyncKeyState(VK_SPACE)<0){
        CopydogRollQCancel(&RollQ);return;
    }
    char name[RC_NAME_CAP]={0};(void)RollCatchName(me,name);
    CopydogRollQObserve(&RollQ,(uintptr_t)me,raw,mode,name,GetActorAnim(me),
        readlpt(int,me+XA,0x18,0xd8),0,now);
}
static int DispatchDefense(int action,int kind,char*target,int animation,int tool,unsigned now,const float *rollYaw,const FramePress*press,const CD_BULLET_TICKET*bullet)
{
    if(kind==DEF_PARRY&&(!press||press->trace[4]!=(uintptr_t)target||
       (int)press->trace[5]!=animation||(int)press->trace[7]!=tool))return 0;
    UpdateDefenseState(now);
    char*me=Player0;
    int requestOwn=me?GetActorAnim(me):-1;
    int requestTarget=kind==DEF_PARRY&&target?GetActorAnim(target):-1;
    int requestHp=me?readlpt(int,me+XA,0x18,0xd8):0;
    int normalRoll=kind==DEF_ROLL&&action==ActionRollForward,rollMode=0;
    int rollRaw=normalRoll?RollEquipmentRaw(&rollMode):0;
    char requestName[RC_NAME_CAP]={0};
    if(kind==DEF_PARRY||normalRoll)(void)RollCatchName(me,requestName);
    if(kind==DEF_ROLL&&!AutoRollAction(action))return 0;
    char rollName[RC_NAME_CAP]={0};
    if(normalRoll&&target&&(PendingDefenseTrace[15]&8u))(void)RollCatchName(target,rollName);
    if(!me||CurrentDefensePlayer()!=me)return 0;
    if(press){
        if(press->diagnostic){
            if(!DiagnosticPressCurrent(press,me,target,timeGetTime()))return 0;
        }else if(!FramePressCurrent(press,timeGetTime()))return 0;
        requestTarget=GetActorAnim(target);
        if(requestTarget<0||(!press->diagnostic&&!(press->trace[15]&CD_PARRY_TRACE_NATIVE_CUE)&&requestTarget!=animation))return 0;
        now=timeGetTime();
        if(!press->diagnostic&&(double)(uint32_t)(now-press->tick)>press->deadline)return 0;
    }
    if(bullet&&!CombatBulletTicketCurrent(bullet,timeGetTime()))return 0;
    if(kind==DEF_ROLL&&(!AutoRollCanSubmit(me)||GetActorAnim(me)!=requestOwn))return 0;
    int diagnostic=press&&press->diagnostic;
    if(CopydogDefenseBusy(&Defense))return 0;
    CD_NATIVE_FRAME baseline={0};
    int baselineRetained=0;
    int baselineValid=diagnostic&&CopydogReadPublishedNativeFrame((uintptr_t)me,&baseline,&baselineRetained);
    if(baselineValid&&baselineRetained&&baseline.animation!=requestOwn)baselineValid=0;
    /* Finish external-state admission before reserving this worker-owned
       transaction. Revalidating against Busy after Begin would reject itself. */
    if(diagnostic&&!DiagnosticPressCurrent(press,me,target,timeGetTime()))return 0;
    if(diagnostic)now=timeGetTime();
    int ambiguous=diagnostic&&(!baselineValid||CopydogKnownParryAnimation(baseline.animation));
    if(!CopydogDefenseBegin(&Defense,(uintptr_t)me,(uintptr_t)target,animation,
        diagnostic?0:requestOwn,requestHp,kind,tool,now))return 0;
    Defense.previousAnimation=requestOwn;
    atomic_store(&MirrorDefenseBusy,1);
    if(rollYaw)CopydogArmRollDirection(*rollYaw,now);
    if(diagnostic){
        DiagnosticEpisodes[press->slot].spent=1;
        if(press->diagnosticAttempted)*press->diagnosticAttempted=1;
        DiagnosticSubmittedOwn=requestOwn;DiagnosticConfirmationAmbiguous=ambiguous;
        DiagnosticLeftBaseline=0;
        DiagnosticPending=DiagnosticEpisodes[press->slot];DiagnosticPendingSlot=press->slot;
        DiagnosticNativeBaseline=baseline;DiagnosticNativeBaselineValid=baselineValid;
    }
    CombatFrameClaimed=1;
    int queued=TryDoAction(action);
    CopydogRollQCancel(&RollQ);
    DefenseHandMode=diagnostic?(unsigned)press->diagnosticMode:readlpt(unsigned,addr2,16,0x2b8);
    memset(ActiveDefenseTrace,0,sizeof ActiveDefenseTrace);
    int pendingRoll=(PendingDefenseTrace[15]&32u)!=0;
    if(PendingDefenseTrace[4]==(uintptr_t)target&&(int)PendingDefenseTrace[5]==animation&&
       ((kind==DEF_PARRY&&!pendingRoll)||(normalRoll&&pendingRoll&&(unsigned)PendingDefenseTrace[2]==now)))
        memcpy(ActiveDefenseTrace,PendingDefenseTrace,sizeof ActiveDefenseTrace);
    if(ambiguous)ActiveDefenseTrace[15]|=UINT64_C(1)<<29;
    ActiveRollName[0]=0;
    if(normalRoll&&(ActiveDefenseTrace[15]&8u))memcpy(ActiveRollName,rollName,sizeof rollName);
    if(press&&(press->trace[15]&CD_PARRY_TRACE_NATIVE_CUE)){
        ActiveCueSlot=press->slot;ActiveCueOnset=press->cueOnset;
        ActiveDefenseTrace[15]&=~(uint64_t)CD_PARRY_TRACE_BEFORE_VISIBLE;
        if(requestTarget!=animation)ActiveDefenseTrace[15]|=CD_PARRY_TRACE_BEFORE_VISIBLE;
    }
    if(kind==DEF_PARRY&&Defense.haveDelay&&Defense.delayKind==DEF_PARRY&&
       (Defense.delayTool!=tool||ParryDelayHandMode!=DefenseHandMode))Defense.haveDelay=0;
    TraceDefense(TRACE_SUBMIT,now,action);
    if(!queued){
        if(rollYaw)CopydogClearRollDirection();
        CopydogDefenseCancel(&Defense,DEF_TIMEOUT,now);TraceDefense(TRACE_CANCEL,now,action);return 0;
    }
    if(normalRoll&&!AutoRollPaused()&&!CopyAction&&GetAsyncKeyState(VK_SPACE)>=0)
        CopydogRollQBegin(&RollQ,(uintptr_t)me,rollRaw,rollMode,requestName,requestOwn,requestHp,now);
    if(kind==DEF_PARRY&&target){
        int slot=GetPlayerIndex(target);
        if(slot>=1&&slot<=MAX_REMOTE_PLAYER)
            CopydogRiposteSubmitted(&RiposteTrack[slot],(uintptr_t)me,(uintptr_t)target,requestOwn,requestTarget,now);
    }
    TraceDefense(TRACE_RETURN,timeGetTime(),action);
    PhaseSubmitted(me,target,kind,tool,now,requestName);
    CombatFrameClaimed=1;return 1;
}
static inline int SubmitDefense(int action,int kind,char*target,int animation,int tool,unsigned now)
{return DispatchDefense(action,kind,target,animation,tool,now,NULL,NULL,NULL);}
static int SubmitDirectedRoll(int action,char*target,int animation,int tool,unsigned now,float yaw)
{return DispatchDefense(action,DEF_ROLL,target,animation,tool,now,&yaw,NULL,NULL);}
__declspec(dllexport) DWORD WINAPI CopydogGetDefenseState(void*out)
{
    /* Diagnostic snapshot is sampled, not a synchronization contract. */
    uint64_t values[]={Defense.phase,Defense.kind,Defense.reason,Defense.tool,
        Defense.requests,Defense.confirmations,Defense.cancellations,
        Defense.haveDelay,Defense.delayKind,Defense.delayTool,Defense.delayMs,
        Defense.submitted,Defense.confirmed};
    return out&&writelp(sizeof values,values,out)?ERROR_SUCCESS:ERROR_INVALID_PARAMETER;
}
enum { WEAPON_FAMILY_STRIDE=10000, MURKY_HAND_SCYTHE_BASE_ID=1150000, ORIGINAL_QUICKSTEP_SKILL=100 };
static int SupportsAutomaticQuickstep(int weaponId)
{
    /* Infusion and reinforcement occupy the lower four decimal digits. */
    return weaponId>0&&
        (weaponId/WEAPON_FAMILY_STRIDE*WEAPON_FAMILY_STRIDE==MURKY_HAND_SCYTHE_BASE_ID||
         GetWeaponSkill(weaponId/100*100)==ORIGINAL_QUICKSTEP_SKILL);
}
/* Exact item-use phases supplied from local observation; do not admit all 50xxx actions. */
enum {
    ESTUS_DRINK_START=50110, ESTUS_DRINK_LOOP=50111, ESTUS_DRINK_END=50112,
    ESTUS_ALT_DRINK_START=50150, ESTUS_ALT_DRINK_LOOP=50151, ESTUS_ALT_DRINK_END=50152
};
static int IsBackstabTargetAnimation(int animation)
{
    return (0<=animation&&animation<4000)||
        (6000<=animation&&animation<17000)||
        (19000<=animation&&animation<50000)||
        (80000<=animation&&animation<90000)||
        animation==ESTUS_DRINK_START||animation==ESTUS_DRINK_LOOP||animation==ESTUS_DRINK_END||
        animation==ESTUS_ALT_DRINK_START||animation==ESTUS_ALT_DRINK_LOOP||animation==ESTUS_ALT_DRINK_END;
}
static COPYDOG_ROLL_GATE RollGate;
static COPYDOG_PARRY_TRACK ParryTrack[MAX_REMOTE_PLAYER+1];
static COPYDOG_EARLY_STATE EarlyTrack[MAX_REMOTE_PLAYER+1];
static COPYDOG_EARLY_PROFILES EarlyProfiles;
static uintptr_t EarlySelf;
static COPYDOG_PHASE_EPISODE PhaseEpisode[MAX_REMOTE_PLAYER+1];
static COPYDOG_PHASE_Q PhaseQ;
static struct { unsigned submitted,first;int waiting,candidate;char before[RC_NAME_CAP]; } PhaseQRequest;
static unsigned PhaseRollSegment[MAX_REMOTE_PLAYER+1];

static void BindPhaseQ(char*me,int tool,int mode)
{
    const char*name=mode==3?"parryrightstart":"parryleftstart";
    CopydogPhaseQBind(&PhaseQ,(uintptr_t)me,tool,mode,name);
    /* SC5 request -> ParryStart observations, not request -> visible C8.
       Historical range 2..22 ms (caestus 8..19); allow 6 ms sampling error. */
    if(!PhaseQ.hasSeed&&((tool==20080000&&mode==1)||(tool==11010300&&mode==1)||(tool==3000300&&mode==3)))
        CopydogPhaseQSeed(&PhaseQ,0,28);
}
static void PhaseSubmitted(char*me,char*target,int kind,int tool,unsigned now,const char*before)
{
    int slot=GetPlayerIndex(target);
    if(slot>=1&&slot<=MAX_REMOTE_PLAYER){
        if(kind==DEF_PARRY)CopydogPhaseEpisodeMarkSpent(&PhaseEpisode[slot]);
    }
    if(kind!=DEF_PARRY)return;
    BindPhaseQ(me,tool,(int)DefenseHandMode);
    memset(&PhaseQRequest,0,sizeof PhaseQRequest);
    PhaseQRequest.submitted=now;PhaseQRequest.waiting=1;
    if(before&&before[0])strcpy(PhaseQRequest.before,before);
    else PhaseQRequest.waiting=0;
}
static void ObservePhaseQ(char*me,unsigned now)
{
    if(!PhaseQRequest.waiting)return;
    char name[RC_NAME_CAP];
    if(PhaseQ.self!=(uintptr_t)me||(uint32_t)(now-PhaseQRequest.submitted)>200||
       GetCurrentWeapon(PhaseQ.mode==3)!=PhaseQ.tool){PhaseQRequest.waiting=0;return;}
    if(!RollCatchName(me,name))return;
    int matches=CopydogPhaseQParryNameKind(name)==PhaseQ.nameKind&&strcmp(name,PhaseQRequest.before);
    if(!matches){PhaseQRequest.candidate=0;return;}
    if(!PhaseQRequest.candidate){PhaseQRequest.first=now;PhaseQRequest.candidate=1;}
    else if((uint32_t)(now-PhaseQRequest.first)>=COPYDOG_ANIMATION_STABLE_MS){
        CopydogPhaseQAdd(&PhaseQ,PhaseQRequest.submitted,PhaseQRequest.first);PhaseQRequest.waiting=0;
    }
}
static void SeedPhaseMappings(void)
{
    for(unsigned i=0;i<COPYDOG_PHASE_TARGET_COUNT;++i){
        const COPYDOG_PHASE_TARGET*p=&CopydogPhaseTargets[i];
        if(p->lagCount>=3)CopydogEarlySeed(&EarlyProfiles,p->key.weaponBase,p->key.mode,
            p->key.name,p->key.animation,p->lagMs,p->lagCount);
    }
}
static _Atomic uint64_t ParryChecks,ParryKnown,ParryUnknown;
__declspec(dllexport) _Atomic uint64_t ParryRequests;
__declspec(dllexport) COPYDOG_PARRY_COUNTERS ParryLast;
static SRWLOCK ParryLock=SRWLOCK_INIT;
static uint64_t ParryCalibration[10]={0,UINT64_MAX,0,0,0,0,0,0,UINT64_MAX,UINT64_MAX};
__declspec(dllexport) DWORD WINAPI CopydogGetParryCalibration(void*out)
{
    uint64_t values[10];
    AcquireSRWLockShared(&ParryLock);memcpy(values,ParryCalibration,sizeof values);ReleaseSRWLockShared(&ParryLock);
    return out&&writelp(sizeof values,values,out)?ERROR_SUCCESS:ERROR_INVALID_PARAMETER;
}
static int RemoteAttackWeapon(char*actor)
{
    char*equip=actor?readlpt(char*,actor+XB):0;
    unsigned mode=0,slot=0,weapon=0;
    if(!equip||!readlpv(mode,equip+0x2b8)||mode<1||mode>3)return 0;
    unsigned right=mode!=2;
    if(!readlpv(slot,equip+0x2bc+right*4)||slot>=3)return 0;
    if(!readlpv(weapon,equip+0x32c+slot*8+right*4))return 0;
    return weapon<=INT_MAX?(int)weapon:0;
}
static unsigned RemoteHandMode(char*actor)
{
    char*equip=actor?readlpt(char*,actor+XB):0;
    return equip?readlpt(unsigned,equip+0x2b8):0;
}
static COPYDOG_ROLL_NATIVE_CLOCK NativeParryClock[MAX_REMOTE_PLAYER+1];
__declspec(dllexport) _Atomic uint64_t CopydogNativeParryRearms;
static void ObserveParryClock(unsigned i,char*me,char*target,int anim,unsigned now)
{
    if(i>MAX_REMOTE_PLAYER)return;
    ObserveDiagnosticEpisode(i,me,target,now);
    COPYDOG_PARRY_TRACK*t=&ParryTrack[i];
    int candidate=t->candidateAnimation,active=t->candidateActive,stable=t->stable,old=t->animation;
    uintptr_t actor=t->actor;
    CopydogParryObserve(t,(uintptr_t)me,(uintptr_t)target,anim,NAN,now);
    CD_NATIVE_FRAME frame;
    const CD_NATIVE_FRAME*sample=NULL;
    COPYDOG_ROLL_BINDING key={.self=(uintptr_t)me,.actor=(uintptr_t)target};
    int retained=0;
    if(target&&anim>=0&&anim%100000>=30000&&anim%100000<40000&&
       CopydogReadPublishedNativeFrame((uintptr_t)target,&frame,&retained)&&frame.animation==anim&&
       readlpv(key.handle,target+8)&&readlpv(key.network,target+0x78)){
        key.weapon=RemoteAttackWeapon(target);key.mode=RemoteHandMode(target);sample=&frame;
    }
    if(CopydogRollObserveNative(&NativeParryClock[i],key,sample,retained,t->stable&&!t->candidateActive,&EarlyTrack[i],now)){
        t->spent=0;t->started=now;EarlyTrack[i].spent=0;
        CopydogPhaseEpisodeReset(&PhaseEpisode[i]);PhaseRollSegment[i]=0;
        if(NativeParryClock[i].repeat==ROLL_REPEAT_TRACK){
            /* Preserve the stable name key so the next name poll cannot restore
               the first swing's episode after the native rewind. */
            COPYDOG_EARLY_STATE*e=&EarlyTrack[i];
            CopydogPhaseEpisodeObserve(&PhaseEpisode[i],e->actor,e->networkId,e->weaponBase,e->mode,e->name,e->started);
            PhaseEpisode[i].episodeStarted=now;
        }
        CopydogPredictionEpisode(i,(uintptr_t)target,now);
        atomic_fetch_add(&CopydogNativeParryRearms,1);
    }
    if(anim<0||anim%100000<30000||anim%100000>=50000)return;
    if((t->candidateActive&&(!active||candidate!=anim||actor!=(uintptr_t)target))||
       (!t->candidateActive&&t->stable&&(!stable||old!=anim))){
        uint64_t record[TRACE_WORDS]={0};
        record[1]=TRACE_OBSERVE;record[2]=now;record[3]=(uintptr_t)me;record[4]=(uintptr_t)target;
        record[5]=anim;record[6]=(uint64_t)(int64_t)GetActorAnim(me);
        record[8]=readlpt(unsigned,addr2,16,0x2b8);record[9]=RemoteHandMode(target);
        record[10]=t->candidateActive?t->candidateStarted:t->started;
        record[11]=(uint32_t)(now-(unsigned)record[10]);
        record[14]=t->candidateActive?PARRY_UNSTABLE:PARRY_NONE;
        record[27]=RemoteAttackWeapon(target);record[28]=readlpt(unsigned,target+0x78);
        CopydogTraceWrite(&CopydogAutoTrace,record);
    }
}
static uint64_t LastTraceDecision[MAX_REMOTE_PLAYER+1][7];
static void TracePlanBounds(unsigned i,COPYDOG_PARRY_TRACK*t,unsigned now,const uint64_t *model,int equiptype,float distance,int reason,uint64_t bounds)
{
    uint64_t *record=PendingDefenseTrace;
    memset(record,0,sizeof PendingDefenseTrace);
    record[1]=TRACE_PLAN;record[2]=now;record[3]=t->self;record[4]=t->actor;
    record[5]=model[3];record[6]=(uint64_t)(int64_t)GetActorAnim(Player0);
    record[7]=model[4];record[8]=equiptype;record[9]=RemoteHandMode((char*)t->actor);
    record[10]=t->started;record[11]=(uint32_t)(now-t->started);
    record[12]=model[8];record[13]=model[9];record[14]=reason;record[15]=model[0];
    record[16]=model[1];record[17]=model[5];record[18]=model[6];
    record[19]=Defense.phase;record[20]=Defense.kind;record[21]=Defense.requests;
    record[22]=Defense.confirmations;record[26]=(uint64_t)(sqrtf(distance)*1000.f);
    record[27]=model[2];record[28]=readlpt(unsigned,(char*)t->actor+0x78);
    record[30]=bounds;
    if(model[0]&1024u){
        record[29]=model[10];record[25]=model[11];record[24]=model[12];record[23]=model[13];
    }
    uint64_t key[]={t->actor,t->started,(uint64_t)reason,model[8],model[9],model[0],bounds};
    if((model[0]&1024u)||memcmp(key,LastTraceDecision[i],sizeof key)){
        CopydogTraceWrite(&CopydogAutoTrace,record);memcpy(LastTraceDecision[i],key,sizeof key);
    }
}
enum { PIPE_LOOPS,PIPE_SELF,PIPE_POLL_BLOCKED,PIPE_COPY,PIPE_TARGETS,PIPE_TYPE,
    PIPE_ENTRY,PIPE_MISSING,PIPE_BAD_ANIM,PIPE_NOT_READY,PIPE_BAD_VECTOR,
    PIPE_PRIOR_ACTION,PIPE_RANGE,PIPE_SPACE,PIPE_SELF_ANIM,PIPE_TARGET_ANIM,PIPE_COUNT };
static _Atomic uint64_t ParryPipeline[PIPE_COUNT];
#define PIPE_INC(n) ((void)atomic_fetch_add(&ParryPipeline[n],1))
__declspec(dllexport) DWORD WINAPI CopydogGetParryPipeline(void*out)
{
    uint64_t values[PIPE_COUNT+4];
    for(unsigned i=0;i<PIPE_COUNT;++i)values[i]=atomic_load(&ParryPipeline[i]);
    values[PIPE_COUNT]=(uint64_t)readlpt(unsigned char,addr2,16,0x2b8);
    values[PIPE_COUNT+1]=Config.KeyCondition==0||GetAsyncKeyState(Config.KeyCondition)<0||
        ((Config.KeyCondition==VK_NUMLOCK||Config.KeyCondition==VK_CAPITAL||Config.KeyCondition==VK_SCROLL)&&(GetKeyState(Config.KeyCondition)&1));
    values[PIPE_COUNT+2]=(uint64_t)(int64_t)GetActorAnim(Player0);
    values[PIPE_COUNT+3]=(uint64_t)Config.KeyCondition;
    return out&&writelp(sizeof values,values,out)?ERROR_SUCCESS:ERROR_INVALID_PARAMETER;
}
static void ParrySnapshot(int anim,unsigned elapsed,int first,int last,int start,int end,int reason)
{
    atomic_fetch_add(&ParryChecks,1);
    if(first>0&&last>=first)atomic_fetch_add(&ParryKnown,1);else atomic_fetch_add(&ParryUnknown,1);
    AcquireSRWLockExclusive(&ParryLock);
    ParryLast.animation=(uint64_t)anim;ParryLast.elapsed=elapsed;
    ParryLast.start=start<0?UINT64_MAX:(uint64_t)start;ParryLast.end=end<0?UINT64_MAX:(uint64_t)end;
    ParryLast.reason=reason;
    ReleaseSRWLockExclusive(&ParryLock);
}
__declspec(dllexport) DWORD WINAPI CopydogGetParryCounters(void*out)
{
    COPYDOG_PARRY_COUNTERS value;
    AcquireSRWLockShared(&ParryLock);value=ParryLast;ReleaseSRWLockShared(&ParryLock);
    value.checks=atomic_load(&ParryChecks);value.known=atomic_load(&ParryKnown);
    value.unknown=atomic_load(&ParryUnknown);value.requests=atomic_load(&ParryRequests);
    return out&&writelp(sizeof value,&value,out)?ERROR_SUCCESS:ERROR_INVALID_PARAMETER;
}
static int PhaseParryKeyDown(void)
{
    return Config.KeyCondition==0||GetAsyncKeyState(Config.KeyCondition)<0||
        ((Config.KeyCondition==VK_NUMLOCK||Config.KeyCondition==VK_CAPITAL||Config.KeyCondition==VK_SCROLL)&&(GetKeyState(Config.KeyCondition)&1));
}
typedef struct {
    uintptr_t self;
    int tool,mode,animation,candidateAnimation,values[9];
    unsigned count,next;
    uint32_t requests[9],observedRequest,firstTick;
    float firstPhase;
    double rates[9];
} DynamicParrySelf;
static DynamicParrySelf DynamicSelf;
static CD_GAME_CADENCE GameCadence;
static int NewCombatStamp,FrameStampReady;
static void ResetFrameWorld(char*me,unsigned now);
static void FrameStampUnavailable(void)
{
    /* The next coherent sample reestablishes cadence; the last confirmed world
       identity remains available for comparison across this failed read. */
    GameCadence.running=0;GameCadence.wallStep=0;FrameStampReady=0;NewCombatStamp=0;
}
static int ReadCombatStamp(uintptr_t*owner,uint32_t*value)
{
    uintptr_t check;
    return readlpv(*owner,addr2)&&*owner&&readlpv(*value,*owner+CD_GAME_PLAY_MS_OFFSET)&&
        readlpv(check,addr2)&&check==*owner;
}
static void ObserveParryGameFrame(char*me,unsigned now)
{
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;uintptr_t owner=0;uint32_t value=0;
    NewCombatStamp=0;
    if(FramePlayer!=(uintptr_t)me){
        if(FramePlayer)ResetFrameWorld(me,now);
        else if(me&&Defense.phase==DEF_CANCELLED&&Defense.reason==DEF_SELF_CHANGED&&!Defense.self)
            Defense.self=(uintptr_t)me; /* finish the old input's normal cancellation cooldown */
        FramePlayer=(uintptr_t)me;
        GameCadence=(CD_GAME_CADENCE){0};FrameStampReady=0;
    }
    if(!me)return;
    if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);
    if(!frequency.QuadPart||!QueryPerformanceCounter(&counter)||!ReadCombatStamp(&owner,&value)){
        FrameStampUnavailable();return;
    }
    int reloaded=GameCadence.owner&&(owner!=GameCadence.owner||value<GameCadence.value);
    if(reloaded)ResetFrameWorld(me,now);
    double wall=1000.*(double)counter.QuadPart/frequency.QuadPart;
    NewCombatStamp=!FrameStampReady||owner!=GameCadence.owner||value!=GameCadence.value;
    if(!FrameStampReady||reloaded)
        GameCadence=(CD_GAME_CADENCE){.owner=owner,.value=value,.tick=wall};
    else CopydogObserveCadence(&GameCadence,owner,value,wall);
    FrameStampReady=1;
}
typedef struct {
    uintptr_t self,actor;
    uintptr_t scheduledOwner;
    uint32_t scheduledValue;
    int handle,weapon,handMode,tool,mode,submitted,armed,ownsModel,verifiedFrame;
    unsigned cueOnset;
    int usingCue;
    CD_FRAME_CLOCK clock;
    enum CD_PARRY_FRAME_DECISION lastDecision;
} ParryFrameTarget;
static ParryFrameTarget DynamicTargets[MAX_REMOTE_PLAYER+1];
enum { DP_NATIVE_OBSERVE,DP_DELAY_SAMPLE,DP_FALLBACK,DP_PLAN,DP_GEOMETRY,DP_EARLY,DP_LATE,DP_SUBMIT,DP_COUNT };
__declspec(dllexport) _Atomic uint64_t CopydogDynamicParryCounters[DP_COUNT];

static size_t FirstDynamicWindow(const CD_DYNAMIC_WINDOW*rows,size_t count,int weapon,int animation)
{
    size_t lo=0,hi=count;
    while(lo<hi){
        size_t mid=lo+(hi-lo)/2;
        if(rows[mid].weapon<weapon||(rows[mid].weapon==weapon&&rows[mid].animation<animation))lo=mid+1;
        else hi=mid;
    }
    return lo;
}

static void ObserveDynamicParrySelf(char*me,unsigned now)
{
    if(!GameCadence.running||NewCombatStamp)(void)ObserveParryMoveClock(me,now);
    int mode=me?readlpt(int,me+XB,0x2b8):0,tool=me?GetCurrentWeapon(mode==3):0;
    if(mode<1||mode>3||tool<=0){DynamicSelf=(DynamicParrySelf){0};return;}
    if(DynamicSelf.self!=(uintptr_t)me||DynamicSelf.tool!=tool||DynamicSelf.mode!=mode)
        DynamicSelf=(DynamicParrySelf){.self=(uintptr_t)me,.tool=tool,.mode=mode};
    if(Defense.self!=(uintptr_t)me||Defense.kind!=DEF_PARRY||Defense.tool!=tool||DefenseHandMode!=(unsigned)mode||
       (Defense.phase!=DEF_WAIT_ANIMATION&&Defense.phase!=DEF_ACTIVE))return;
    if((ActiveDefenseTrace[15]&CD_DIAG_FORCED_SOURCE)&&
       (DiagnosticConfirmationAmbiguous||!DiagnosticNativeBaselineValid||
        CopydogKnownParryAnimation(DiagnosticNativeBaseline.animation)))return;
    CD_NATIVE_FRAME local;int retained;
    if(!CopydogReadLatestNativeFrame((uintptr_t)me,&local,GameCadence.running?&retained:NULL)||
       (GameCadence.running&&retained&&GetActorAnim(me)!=local.animation)||
       !CopydogKnownParryAnimation(local.animation))return;
    int animation=local.animation;float phase=local.current;
    int base=CopydogEarlyBase(tool);
    size_t row=FirstDynamicWindow(CdDynamicTools,_countof(CdDynamicTools),base,animation);
    if(row==_countof(CdDynamicTools)||CdDynamicTools[row].weapon!=base||CdDynamicTools[row].animation!=animation)return;
    if(DynamicSelf.observedRequest!=Defense.submitted||DynamicSelf.candidateAnimation!=animation){
        DynamicSelf.observedRequest=Defense.submitted;DynamicSelf.firstTick=now;
        DynamicSelf.firstPhase=phase;DynamicSelf.candidateAnimation=animation;return;
    }
    unsigned dt=now-DynamicSelf.firstTick;
    if(dt<32||dt>200||phase<=DynamicSelf.firstPhase)return;
    if(DynamicSelf.animation==animation)
        for(unsigned n=0;n<DynamicSelf.count;n++)if(DynamicSelf.requests[n]==Defense.submitted)return;
    double rate=(phase-DynamicSelf.firstPhase)*1000./dt;
    if(rate<.25||rate>4)return;
    double delay=(double)(now-Defense.submitted)-phase*1000./rate;
    if(delay < -CD_FRAME_DELAY_PAD_MS||delay>400)return;
    if(DynamicSelf.animation!=animation){
        DynamicSelf.count=DynamicSelf.next=0;DynamicSelf.animation=animation;
    }
    unsigned at=DynamicSelf.next++%_countof(DynamicSelf.values);
    DynamicSelf.values[at]=(int)lround(fmax(0,delay));DynamicSelf.requests[at]=Defense.submitted;
    DynamicSelf.rates[at]=rate;
    if(DynamicSelf.count<_countof(DynamicSelf.values))DynamicSelf.count++;
    atomic_fetch_add(&CopydogDynamicParryCounters[DP_DELAY_SAMPLE],1);
}

typedef struct { double open,close;int qmin,qmax,seed; } ParryActivation;
static int FrameSelfActivation(int tool,int mode,double step,ParryActivation*out)
{
    int base=CopydogEarlyBase(tool),animation=DynamicSelf.animation;
    if(!animation){
        const COPYDOG_PHASE_SELF*known=CopydogPhaseSelfByName(base,mode,mode==3?"parryrightstart":"parryleftstart");
        if(known)animation=known->key.animation;
        else if(mode!=1)return 0;
    }
    size_t n=FirstDynamicWindow(CdDynamicTools,_countof(CdDynamicTools),base,animation);
    double first=-1,last=INFINITY;
    for(;n<_countof(CdDynamicTools)&&CdDynamicTools[n].weapon==base;n++){
        const CD_DYNAMIC_WINDOW*w=&CdDynamicTools[n];
        if(animation&&w->animation!=animation)break;
        if(!animation&&w->animation%1000000!=32100)continue;
        first=fmax(first,w->begin);last=fmin(last,w->end);
    }
    if(first<0||first>=last)return 0;
    /* Before three local observations use an explicitly labelled scheduling prior.
       Full RTT and human reaction time are not observations of local dispatch. */
    *out=(ParryActivation){.open=first,.close=last,.seed=DynamicSelf.count<3};
    if(out->seed){
        out->qmax=(int)ceil(CD_FRAME_BOOTSTRAP_STEPS*step);
        out->open+=out->qmax;
    }else{
        out->open=0;out->close=INFINITY;out->qmin=INT_MAX;
        for(unsigned i=0;i<DynamicSelf.count;i++){
            int q=DynamicSelf.values[i];
            int lower=q>CD_FRAME_DELAY_PAD_MS?q-CD_FRAME_DELAY_PAD_MS:0;
            int upper=q+CD_FRAME_DELAY_PAD_MS;
            if(lower<out->qmin)out->qmin=lower;
            if(upper>out->qmax)out->qmax=upper;
            out->open=fmax(out->open,upper+first/DynamicSelf.rates[i]);
            out->close=fmin(out->close,lower+last/DynamicSelf.rates[i]);
        }
    }
    return 1;
}

static int ReadFrameAttackRaw(char*actor,int mode,int animation,int*raw)
{
    char*equipment=readlpt(char*,actor+XB);
    int move=animation%1000000;
    unsigned right=mode!=2&&!(mode==1&&move>=32000&&move<34000),slot,item;
    *raw=0;
    if(!equipment||mode<1||mode>3||!readlpv(slot,equipment+0x2bc+4*right)||slot>=3||
       !readlpv(item,equipment+0x32c+slot*8+4*right)||item>INT_MAX)return 0;
    *raw=(int)item;return 1;
}
static int FrameAttackRaw(char*actor,int mode,int animation)
{
    int raw;ReadFrameAttackRaw(actor,mode,animation,&raw);return raw;
}
static int FrameAttackWeapon(char*actor,int mode,int animation)
{
    return CopydogEarlyBase(FrameAttackRaw(actor,mode,animation));
}

static int EligibleAttackTrack(int raw,int animation,int mode)
{
    if(raw<=0||animation<0||Unparriable(animation))return 0;
    int weapon=CopydogEarlyBase(raw);
    size_t n=FirstDynamicWindow(CdDynamicTargets,_countof(CdDynamicTargets),weapon,animation);
    return n<_countof(CdDynamicTargets)&&CdDynamicTargets[n].weapon==weapon&&
        CdDynamicTargets[n].animation==animation&&CopydogPredictionHasAttackShape(weapon,animation,mode);
}

static void TraceNativeCue(unsigned slot,unsigned now)
{
    const CD_PARRY_CUE*c=&NativeCues[slot];
    uint64_t record[TRACE_WORDS]={0};
    record[1]=TRACE_NATIVE_CUE;record[2]=now;record[3]=EarlySelf;record[4]=c->actor;
    record[5]=c->expected;record[6]=(uint64_t)(int64_t)GetActorAnim((char*)EarlySelf);
    record[16]=(uint64_t)(int64_t)GetActorAnim((char*)c->actor);
    record[9]=c->mode;record[10]=c->onset;record[11]=now-c->onset;
    record[14]=c->blocked?3u:(unsigned)c->stage;record[15]=CD_PARRY_TRACE_NATIVE_CUE;
    record[27]=c->weapon;record[28]=CueWatch[slot].network;
    CopydogTraceWrite(&CopydogAutoTrace,record);
}
static int RejectNativeCue(unsigned slot,unsigned now)
{
    CD_PARRY_CUE*c=&NativeCues[slot];
    if(!c->blocked){CopydogParryCueCancel(c);TraceNativeCue(slot,now);}
    if(CueWatch[slot].poseObserved){
        CopydogPredictionObserveAttackFrame(slot,0,now,0,-1,0);CueWatch[slot].poseObserved=0;
    }
    return -1;
}

/* A published attack can precede both the visible action and its name. The
   first observed native edge or corroborated name onset starts a pose track. */
static void ObserveNativeParryCue(unsigned slot,char*target,const char*name,unsigned now,int enabled)
{
    CD_PARRY_CUE*c=&NativeCues[slot];NativeCueWatch*w=&CueWatch[slot];
    enum CD_PARRY_CUE_STAGE previousStage=c->stage;
    unsigned wasBlocked=c->blocked;
    int handle=0,network=0;
    if(target&&(!readlpv(handle,target+8)||!readlpv(network,target+0x78))){
        c->readable=0;return;
    }
    if(!target||!handle||w->actor!=(uintptr_t)target||w->handle!=handle||w->network!=network){
        if(w->actor)CopydogPredictionObserveAttackFrame(slot,0,now,0,-1,0);
        *c=(CD_PARRY_CUE){0};*w=(NativeCueWatch){.actor=(uintptr_t)target,.handle=handle,.network=network};
    }
    if(!target||!handle)return;
    int kind=CopydogEarlyNameKind(name),mode=0,retained=0;
    int haveMode=readlpv(mode,&target,XB,0x2b8)!=NULL;
    CD_NATIVE_FRAME f;
    int have=CopydogReadPublishedNativeFrame((uintptr_t)target,&f,&retained);
    int edge=have&&!retained&&w->havePublished&&now-w->lastPublished<=CD_PRED_MAX_GAP_MS&&
        w->lastAnimation!=f.animation;
    if(have&&!retained&&(!w->havePublished||!CopydogSameFrame(&f,&w->prior))){
        w->lastAnimation=f.animation;w->prior=f;w->havePublished=1;w->lastPublished=now;
    }
    COPYDOG_EARLY_STATE*e=&EarlyTrack[slot];
    unsigned nameOnset=e->candidateActive?e->candidateStarted:e->started;
    int named=kind&&e->initialized&&e->actor==(uintptr_t)target&&
        (e->candidateActive||!e->suppressed)&&!strcmp(name,e->candidateActive?e->candidateName:e->name);
    int newName=named&&c->onset!=nameOnset&&now-nameOnset<=CD_PRED_MAX_AGE_MS;
    int different=have&&(c->expected!=f.animation||!c->actor);
    int raw=0;
    int haveRaw=have&&ReadFrameAttackRaw(target,mode,f.animation,&raw);
    if(c->actor&&name&&w->nameKind&&kind!=w->nameKind)w->nameReleased=1;
    int reenter=c->actor&&c->blocked&&have&&f.animation==c->expected;
    int freshReentry=!reenter||(f.previous==0.f&&edge&&(!w->nameKind||w->nameReleased||newName));
    int start=freshReentry&&enabled&&have&&!retained&&name&&
        (different||c->blocked||!c->seen)&&(edge||newName)&&(!kind||CopydogEarlyValidId(name,f.animation))&&
        EligibleAttackTrack(raw,f.animation,mode);
    if(start){
        unsigned onset=named&&now-nameOnset<=CD_PRED_MAX_AGE_MS?nameOnset:now;
        if(c->actor&&c->onset==onset)onset=now;
        CopydogPredictionObserveAttackFrame(slot,0,now,0,-1,0);
        *c=(CD_PARRY_CUE){0};w->nameKind=kind;w->nameReleased=0;
        CopydogParryCueUpdate(c,(uintptr_t)target,handle,raw,mode,f.animation,onset,&f,0,now);
    }else if(c->actor){
        if((name&&w->nameKind&&w->nameKind!=kind)||(kind&&!CopydogEarlyValidId(name,c->expected))||
           (have&&f.animation!=c->expected)||
           (haveMode&&mode!=c->mode)||(haveRaw&&f.animation==c->expected&&raw!=c->weapon))
            CopydogParryCueCancel(c);
        if(!w->nameKind&&kind&&CopydogEarlyValidId(name,c->expected))w->nameKind=kind;
        if(!name||!have||!raw||!enabled){c->readable=0;}
        else CopydogParryCueUpdate(c,c->actor,handle,c->weapon,c->mode,c->expected,c->onset,&f,retained,now);
    }
    if(!c->actor)return;
    if((start&&c->seen)||c->stage!=previousStage||(!wasBlocked&&c->blocked)){
        TraceNativeCue(slot,now);
    }
    if(c->blocked||!enabled||!c->readable){
        if((c->blocked||!enabled)&&w->poseObserved){
            CopydogPredictionObserveAttackFrame(slot,0,now,0,-1,0);w->poseObserved=0;
        }
        return;
    }
    /* Bind the first pose immediately even if a second poll shares this physics stamp. */
    if(start||(!retained&&c->tick==now)||NewCombatStamp){
        CopydogPredictionObserveAttackFrame(slot,c->actor,now,GameCadence.running,c->expected,c->onset);
        w->poseObserved=1;
    }
}

/* Unknown reads withhold a new command. A witnessed contradiction retires the
   epoch immediately, including when the command's last read first sees it. */
static int ReadLiveParryCue(unsigned i,char*actor,int expected,unsigned onset,unsigned now,
    CD_NATIVE_FRAME*latest,int*retained)
{
    if(!actor||!i||i>MAX_REMOTE_PLAYER)return -1;
    CD_PARRY_CUE*c=&NativeCues[i];NativeCueWatch*w=&CueWatch[i];
    if(c->actor!=(uintptr_t)actor||c->expected!=expected||c->onset!=onset||c->blocked)return -1;
    char*current=NULL,name[RC_NAME_CAP];int handle,network,mode,raw;
    if(!ReadRemoteSlot(i,&current))return 0;
    if(current!=actor)return RejectNativeCue(i,now);
    if(!readlpv(handle,actor+8)||!readlpv(network,actor+0x78))return 0;
    if(handle!=c->handle||network!=w->network)return RejectNativeCue(i,now);
    if(!readlpv(mode,&actor,XB,0x2b8))return 0;
    if(mode!=c->mode)return RejectNativeCue(i,now);
    if(!ReadFrameAttackRaw(actor,mode,c->expected,&raw)||!RollCatchName(actor,name))return 0;
    int kind=CopydogEarlyNameKind(name);
    if(raw!=c->weapon||(w->nameKind&&kind!=w->nameKind)||(kind&&!CopydogEarlyValidId(name,c->expected)))
        return RejectNativeCue(i,now);
    if(!CopydogReadPublishedNativeFrame((uintptr_t)actor,latest,retained))return 0;
    if(latest->clock!=c->last.clock||latest->animation!=c->expected||latest->duration!=c->last.duration||
       latest->current<c->last.current||latest->previous<c->last.previous)return RejectNativeCue(i,now);
    return (!retained||!*retained||now-c->tick<=CD_PRED_MAX_AGE_MS)?1:0;
}
static int ActiveNativeCueStatus(char*actor,unsigned now)
{
    CD_NATIVE_FRAME latest;int retained;
    return ReadLiveParryCue(ActiveCueSlot,actor,Defense.targetAnimation,ActiveCueOnset,now,&latest,&retained);
}

typedef struct {
    enum CD_PARRY_FRAME_DECISION decision;
    int reason;
    unsigned source;
    double lo,hi;
    size_t bandIndex;
} FramePlan;

static FramePlan PlanParryBands(unsigned slot,const ParryFrameTarget*t,const ParryActivation*own,
    size_t first,size_t end,unsigned now,double nextStep)
{
    const CD_NATIVE_FRAME*f=&t->clock.last;
    FramePlan chosen={.decision=CD_FRAME_SKIP,.reason=PARRY_LATE};
    unsigned budget=CD_PRED_WORK_BUDGET;
    /* Query only the attack shapes inside each eligible half-open native band.
       A clear prefix cannot rule out a later part of a multi-hit attack. */
    uint32_t phaseTick=t->usingCue?CopydogPredictionAttackFrameOrigin(slot,t->actor,f,t->clock.tick,now):
        CopydogPredictionFrameOrigin(slot,t->actor,f,t->clock.tick,now);
    double age=(uint32_t)(now-phaseTick);
    if(age>CD_PRED_MAX_AGE_MS)return (FramePlan){.decision=CD_FRAME_SKIP,.reason=PARRY_UNSTABLE};
    double horizon=fmin(CD_PRED_MAX_FORECAST_MS,fmax(CD_PRED_HORIZON_MS,own->close+nextStep));
    for(size_t n=first;n<end;n++){
        const CD_DYNAMIC_WINDOW*w=&CdDynamicTargets[n];
        if(w->weapon!=t->weapon||w->animation!=f->animation)break;
        double c0=(w->begin-1000.*f->current)/t->clock.rate-age;
        double c1=(w->end-1000.*f->current)/t->clock.rate-age;
        if(c1<=0||w->begin>=w->end)continue;
        FramePlan p={.decision=CD_FRAME_WAIT,.reason=PARRY_EARLY,.bandIndex=n};
        int openContactEnd=1;
        if(c0<horizon){
            CD_PRED_PARRY_BAND band={f,t->weapon,t->handMode,t->clock.rate,w->begin,w->end,horizon,&budget,phaseTick};
            CD_PRED_PARRY_RESULT geom;
            int g=t->usingCue?CopydogPredictionAttackParryContact(slot,t->actor,t->self,now,&band,&geom):
                CopydogPredictionParryContact(slot,t->actor,t->self,now,&band,&geom);
            if(geom.turning)p.source|=65536u;
            if(g==CD_PRED_CLEAR&&geom.complete){
                if(chosen.decision==CD_FRAME_SKIP)chosen=(FramePlan){.decision=CD_FRAME_SKIP,.reason=PARRY_NO_CONTACT,
                    .source=CD_PARRY_COMPLETE_CLEAR|p.source,.bandIndex=n};
                continue;
            }
            if(g==CD_PRED_CLEAR){
                p.source|=CD_PARRY_PARTIAL_CLEAR;
                c0=fmax(c0,geom.clearThrough);
            }else if(g==CD_PRED_CONTACT){
                double pad=fmax(CD_PRED_TOLERANCE_MS,nextStep*.5);
                /* Intersect the closed observation with the half-open resource.
                   On equality the resource still excludes its last instant. */
                openContactEnd=c1<=geom.contact+pad;
                c0=fmax(c0,geom.contact-pad);c1=fmin(c1,geom.contact+pad);
                if(c0>c1||(c0==c1&&openContactEnd))continue;
                p.source|=CD_PARRY_CONTACT_PROOF;
            }else if(g==CD_PRED_POSSIBLE)p.source|=CD_PARRY_POSSIBLE_CONTACT;
            else p.source|=CD_PARRY_UNKNOWN_GEOMETRY|(geom.unknownReason<<CD_PARRY_UNKNOWN_REASON_SHIFT);
        }
        c0=fmax(0,c0);
        double preferred;
        if(!CopydogDynamicParryWindow(c0,c1,own->open,own->close,0,0,&p.lo,&p.hi,&preferred)){
            p.reason=PARRY_DISPATCH_UNREACHABLE;
            p.decision=c1>own->open?CD_FRAME_WAIT:CD_FRAME_SKIP;
        }else{
            p.decision=CopydogParryFrameChoiceBounds(p.lo,p.hi,nextStep,openContactEnd);
            p.reason=p.decision==CD_FRAME_PRESS?PARRY_NONE:p.decision==CD_FRAME_WAIT?PARRY_EARLY:PARRY_LATE;
        }
        if(p.decision==CD_FRAME_PRESS){
            if(CopydogParryContactAuthorizes(p.source))return p;
            /* Time alone can keep sampling alive. Continue checking subsequent
               eligible bands, which can carry their own contact at this tick. */
            p.reason=PARRY_CONTACT_UNCONFIRMED;
            p.decision=nextStep<=p.hi?CD_FRAME_WAIT:CD_FRAME_SKIP;
        }
        if(chosen.decision!=CD_FRAME_WAIT)chosen=p;
    }
    return chosen;
}

static FramePlan PlanParryFrame(unsigned slot,const ParryFrameTarget*t,const ParryActivation*own,
    size_t first,unsigned now,double nextStep)
{
    return PlanParryBands(slot,t,own,first,_countof(CdDynamicTargets),now,nextStep);
}
static enum CD_PARRY_FRAME_DECISION EvaluateParryFrame(unsigned i,char*me,char*target,unsigned now,FramePress*press)
{
    if(press)memset(press,0,sizeof *press);
    if(i>MAX_REMOTE_PLAYER||!me||!target)return CD_FRAME_SKIP;
    ParryFrameTarget*t=&DynamicTargets[i];
    if(!PhaseParryKeyDown()||CopyAction){t->armed=0;return CD_FRAME_SKIP;}
    if(CombatFrameClaimed||CopydogDefenseBusy(&Defense))return CD_FRAME_SKIP;
    int mode=readlpt(int,me+XB,0x2b8),tool=GetCurrentWeapon(mode==3);
    int targetMode=(int)RemoteHandMode(target),handle=readlpt(int,target+8);
    if(tool<=0||mode<1||mode>3)return CD_FRAME_SKIP;
    CD_NATIVE_FRAME f;
    int engine=GameCadence.running,retained=0;
    int visible=GetActorAnim(target);
    CD_PARRY_CUE*c=&NativeCues[i];
    int have=handle&&CopydogReadPublishedNativeFrame((uintptr_t)target,&f,engine?&retained:NULL);
    int usingCue=c->actor==(uintptr_t)target&&c->handle==handle&&
        (c->expected==visible||(have&&f.animation==c->expected));
    if(usingCue&&(c->blocked||c->spent||!c->readable||c->stage!=CD_CUE_CONFIRMED||
       now-c->tick>CD_PRED_MAX_AGE_MS||!have||!CopydogSameFrame(&f,&c->last)))return CD_FRAME_SKIP;
    if(!have||(!usingCue&&f.animation!=visible)){
        if(t->self!=(uintptr_t)me||t->actor!=(uintptr_t)target||t->handle!=handle)
            *t=(ParryFrameTarget){.self=(uintptr_t)me,.actor=(uintptr_t)target,.handle=handle};
        if(engine&&(t->scheduledOwner!=GameCadence.owner||t->scheduledValue!=GameCadence.value)){
            t->scheduledOwner=GameCadence.owner;t->scheduledValue=GameCadence.value;
            atomic_fetch_add(&CopydogDynamicParryCounters[DP_FALLBACK],1);
        }
        return CD_FRAME_SKIP;
    }
    int weapon=FrameAttackWeapon(target,targetMode,f.animation);
    int sameActor=t->self==(uintptr_t)me&&t->actor==(uintptr_t)target&&t->handle==handle;
    if(!sameActor||(usingCue&&t->usingCue&&t->cueOnset!=c->onset)||t->clock.last.clock!=f.clock||
       t->clock.last.animation!=f.animation||
       (f.current<=CD_NATIVE_REPEAT_START_MAX_S&&f.current+CD_NATIVE_REPEAT_REWIND_MIN_S<t->clock.last.current)){
        *t=(ParryFrameTarget){.self=(uintptr_t)me,.actor=(uintptr_t)target,.handle=handle};
    }
    int refresh=!t->armed||usingCue!=t->usingCue||t->tool!=tool||t->mode!=mode||t->weapon!=weapon||t->handMode!=targetMode;
    t->usingCue=usingCue;t->cueOnset=usingCue?c->onset:0;
    t->armed=1;t->tool=tool;t->mode=mode;t->weapon=weapon;t->handMode=targetMode;
    int progressed=t->clock.valid&&f.current>t->clock.last.current&&now-t->clock.tick<=CD_FRAME_CLOCK_RESET_MS;
    int changed=CopydogAdvanceFrame(&t->clock,&f,now);
    if(!retained||progressed)t->verifiedFrame=1;
    if(!t->verifiedFrame)return CD_FRAME_SKIP;
    int already=engine?t->scheduledOwner==GameCadence.owner&&t->scheduledValue==GameCadence.value:!changed;
    if(already&&!refresh)
        return t->lastDecision==CD_FRAME_WAIT?CD_FRAME_WAIT:CD_FRAME_SKIP;
    t->scheduledOwner=engine?GameCadence.owner:0;t->scheduledValue=GameCadence.value;
    atomic_fetch_add(&CopydogDynamicParryCounters[DP_NATIVE_OBSERVE],1);
    t->lastDecision=CD_FRAME_SKIP;
    if(t->clock.step<=0||f.current<=f.previous||t->submitted||
       now-t->clock.tick>CD_PRED_MAX_AGE_MS)return CD_FRAME_SKIP;
    double nextStep=engine?GameCadence.wallStep:t->clock.step;
    if(!usingCue&&ParryTrack[i].spent&&ParryTrack[i].animation==f.animation)return CD_FRAME_SKIP;
    if(Unparriable(f.animation)){t->ownsModel=1;return CD_FRAME_SKIP;}
    size_t first=FirstDynamicWindow(CdDynamicTargets,_countof(CdDynamicTargets),weapon,f.animation);
    ParryActivation own;
    if(first==_countof(CdDynamicTargets)||CdDynamicTargets[first].weapon!=weapon||
       CdDynamicTargets[first].animation!=f.animation||DynamicSelf.self!=(uintptr_t)me||
       DynamicSelf.tool!=tool||DynamicSelf.mode!=mode||!FrameSelfActivation(tool,mode,nextStep,&own)){
        t->ownsModel=0;
        atomic_fetch_add(&CopydogDynamicParryCounters[DP_FALLBACK],1);return CD_FRAME_SKIP;
    }
    t->ownsModel=1;
    float a[3],b[3];
    if(!ReadVec(GetActorPos(me),a)||!ReadVec(GetActorPos(target),b))return CD_FRAME_SKIP;
    float dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2],distance=dx*dx+dy*dy+dz*dz;
    FramePlan plan={.decision=CD_FRAME_SKIP,.reason=PARRY_STATE};
    if(isfinite(distance)&&distance<Config.ParryDistance*Config.ParryDistance&&
       FrameParrySelfReady(me)&&GetAsyncKeyState(VK_SPACE)>=0)
        plan=PlanParryFrame(i?i:CD_PRED_LOCK_SLOT,t,&own,first,now,nextStep);
    if(usingCue&&visible!=f.animation&&plan.decision==CD_FRAME_PRESS&&
       !CopydogPredictionAttackMotionReady(i,t->actor,&f,t->weapon,t->handMode,now)){
        plan.decision=CD_FRAME_WAIT;plan.reason=PARRY_UNSTABLE;
    }
    if(plan.decision==CD_FRAME_PRESS&&(double)(uint32_t)(timeGetTime()-now)>plan.hi){
        plan.decision=CD_FRAME_WAIT;plan.reason=PARRY_UNSTABLE;
    }
    unsigned source=(usingCue?CD_PARRY_TRACE_NATIVE_CUE:0u)|
        (usingCue&&visible!=f.animation?CD_PARRY_TRACE_BEFORE_VISIBLE:0u)|1024u|512u|16u|plan.source|(own.seed?2048u:0u)|(engine?16384u:0u)|(retained?32768u:0u);
    COPYDOG_PARRY_TRACK clock={.self=(uintptr_t)me,.actor=(uintptr_t)target,.animation=f.animation,
        .stable=1,.started=now-(unsigned)(f.current*1000./t->clock.rate)};
    uint64_t model[]={source,DynamicSelf.count,(uint64_t)weapon,(uint64_t)f.animation,(uint64_t)tool,
        !own.seed,(uint64_t)((own.qmin+own.qmax)/2),0,(uint64_t)(int64_t)lround(plan.lo),
        (uint64_t)(int64_t)lround(plan.hi),(uint64_t)lround(nextStep*1000.),plan.decision,
        (unsigned)f.index,t->clock.skipped};
    AcquireSRWLockExclusive(&ParryLock);memcpy(ParryCalibration,model,sizeof ParryCalibration);ReleaseSRWLockExclusive(&ParryLock);
    ParrySnapshot(f.animation,now-clock.started,(int)CdDynamicTargets[first].begin,(int)CdDynamicTargets[first].end,
                  (int)lround(plan.lo),(int)lround(plan.hi),plan.reason);
    TracePlanBounds(i,&clock,now,model,mode,distance,plan.reason,((uint64_t)(unsigned)own.qmin<<32)|(unsigned)own.qmax);
    atomic_fetch_add(&CopydogDynamicParryCounters[DP_PLAN],1);
    if(CopydogParryContactAuthorizes(plan.source))atomic_fetch_add(&CopydogDynamicParryCounters[DP_GEOMETRY],1);
    if(plan.decision==CD_FRAME_WAIT)atomic_fetch_add(&CopydogDynamicParryCounters[DP_EARLY],1);
    if(plan.reason==PARRY_LATE)atomic_fetch_add(&CopydogDynamicParryCounters[DP_LATE],1);
    if(plan.decision==CD_FRAME_PRESS){
        press->slot=i;press->tick=now;press->deadline=plan.hi;press->bandIndex=plan.bandIndex;
        press->cueOnset=t->cueOnset;press->frame=f;
        memcpy(press->trace,PendingDefenseTrace,sizeof press->trace);
    }
    t->lastDecision=plan.decision;
    return plan.decision;
}
static int FramePressStateCurrent(const FramePress*p,unsigned now)
{
    if(!p||p->slot>MAX_REMOTE_PLAYER||!(p->trace[15]&1024u)||p->trace[1]!=TRACE_PLAN||
       !CopydogParryContactAuthorizes((unsigned)p->trace[15])||p->trace[25]!=CD_FRAME_PRESS||(double)(uint32_t)(now-p->tick)>p->deadline)return 0;
    unsigned i=p->slot;ParryFrameTarget*t=&DynamicTargets[i];
    char*target=(char*)t->actor,*me=(char*)t->self;
    if(t->lastDecision!=CD_FRAME_PRESS||!t->clock.valid||now-t->clock.tick>CD_PRED_MAX_AGE_MS||t->submitted||CombatFrameClaimed||
       CopydogDefenseBusy(&Defense)||!me||!target||Player0!=me||p->trace[3]!=t->self||
       p->trace[4]!=t->actor||(int)p->trace[5]!=p->frame.animation||!CopydogSameFrame(&p->frame,&t->clock.last)||
       readlpt(int,me+XB,0x2b8)!=t->mode||GetCurrentWeapon(t->mode==3)!=t->tool||
       !PhaseParryKeyDown()||CopyAction||!FrameParrySelfReady(me)||GetAsyncKeyState(VK_SPACE)<0)return 0;
    int visible=GetActorAnim(target),engine=(p->trace[15]&16384u)!=0,retained=0;
    if(visible<0)return 0;
    uintptr_t owner;uint32_t value;
    if(engine&&(!ReadCombatStamp(&owner,&value)||owner!=t->scheduledOwner||value!=t->scheduledValue))return 0;
    CD_NATIVE_FRAME latest;
    if(p->trace[15]&CD_PARRY_TRACE_NATIVE_CUE){
        CD_PARRY_CUE*c=&NativeCues[i];
        if(!t->usingCue||c->spent||!c->readable||c->stage!=CD_CUE_CONFIRMED||now-c->tick>CD_PRED_MAX_AGE_MS||
           !CopydogSameFrame(&c->last,&p->frame)||
           ReadLiveParryCue(i,target,p->frame.animation,p->cueOnset,now,&latest,engine?&retained:NULL)!=1)return 0;
        if(visible!=p->frame.animation&&
           !CopydogPredictionAttackMotionReady(i,t->actor,&p->frame,t->weapon,t->handMode,now))return 0;
    }else if((i?Player(i):GetLockTarget())!=target||visible!=p->frame.animation||
             readlpt(int,target+8)!=t->handle||RemoteHandMode(target)!=(unsigned)t->handMode||
             FrameAttackWeapon(target,t->handMode,p->frame.animation)!=t->weapon||
             !CopydogReadAttackNativeFrame(t->actor,p->frame.animation,&latest,engine?&retained:NULL))return 0;
    return CopydogSameFrame(&latest,&p->frame)&&(double)(uint32_t)(timeGetTime()-p->tick)<=p->deadline;
}
static int FramePressCurrent(const FramePress*p,unsigned now)
{
    if(!FramePressStateCurrent(p,now)||p->bandIndex>=_countof(CdDynamicTargets))return 0;
    const ParryFrameTarget*t=&DynamicTargets[p->slot];
    double step=GameCadence.running?GameCadence.wallStep:t->clock.step;
    ParryActivation own;
    if(!FrameSelfActivation(t->tool,t->mode,step,&own))return 0;
    /* The current poses must still predict a contact in the same attack band.
       The previous observation alone cannot authorize input after a reset. */
    unsigned slot=p->slot?p->slot:CD_PRED_LOCK_SLOT;
    FramePlan check=PlanParryBands(slot,t,&own,p->bandIndex,p->bandIndex+1,now,step);
    if(check.decision!=CD_FRAME_PRESS||!CopydogParryContactAuthorizes(check.source))return 0;
    unsigned finished=timeGetTime();
    return (double)(uint32_t)(finished-now)<=check.hi&&FramePressStateCurrent(p,finished);
}
static int DispatchParryFrame(FramePress*p)
{
    if(!p||p->slot>MAX_REMOTE_PLAYER)return 0;
    unsigned i=p->slot;ParryFrameTarget*t=&DynamicTargets[i];
    char*target=(char*)t->actor;
    if(FramePressStateCurrent(p,timeGetTime())){
        unsigned at=timeGetTime();
        memcpy(PendingDefenseTrace,p->trace,sizeof p->trace);
        if(DispatchDefense(t->mode==3?ActionRightParry:ActionLeftParry,DEF_PARRY,target,
                           p->frame.animation,t->tool,at,NULL,p,NULL)){
            t->submitted=1;ParryTrack[i].spent=1;EarlyTrack[i].spent=1;
            if(t->usingCue)NativeCues[i].spent=1;
            CopydogPhaseEpisodeMarkSpent(&PhaseEpisode[i]);atomic_fetch_add(&ParryRequests,1);
            atomic_fetch_add(&CopydogDynamicParryCounters[DP_SUBMIT],1);return 1;
        }
    }
    p->trace[2]=timeGetTime();p->trace[14]=PARRY_UNSTABLE;p->trace[25]=CD_FRAME_WAIT;
    CopydogTraceWrite(&CopydogAutoTrace,p->trace);t->lastDecision=CD_FRAME_WAIT;return 0;
}
static __attribute__((unused)) enum CD_PARRY_FRAME_DECISION TryLegacyFrameAutomaticParryForOffline(unsigned i,char*me,char*target,unsigned now)
{
    FramePress press;
    enum CD_PARRY_FRAME_DECISION choice=EvaluateParryFrame(i,me,target,now,&press);
    return choice==CD_FRAME_PRESS&&!DispatchParryFrame(&press)?CD_FRAME_WAIT:choice;
}
#include "parry_diagnostic.inc"
static enum CD_PARRY_FRAME_DECISION TryFrameAutomaticParry(unsigned i,char*me,char*target,unsigned now)
{
    if(!atomic_load(&CopydogDiagnosticParryEnabled))return CD_FRAME_SKIP;
    return TryDiagnosticParry(i,me,target,now);
}

static int TryPhaseAutomaticRoll(unsigned i,char*me,unsigned now)
{
    COPYDOG_EARLY_STATE*e=&EarlyTrack[i];unsigned clockSource;
    if(AutoRollPaused()||CopyAction||CombatFrameClaimed||CopydogDefenseBusy(&Defense))return 0;
    char*target=Player(i);
    double predictedContact;int predictedAnimation;
    int prediction=CopydogPredictionQuery(i,(uintptr_t)target,(uintptr_t)me,now,&predictedContact,&predictedAnimation);
    if(prediction>=CD_PRED_CLEAR){
        /* Known geometry owns this decision; clear must not trigger the legacy
           distance-only timer. Unknown coverage retains the previous path. */
        if(prediction==CD_PRED_CLEAR||!AutoRollCanSubmit(me)||CrossbowAnimationOnly(target,predictedAnimation))return 1;
        float selfPos[3],targetPos[3],away,distance;
        if(!target||readlpt(int,target+XA,0x18,0xd8)<=0||
           !ReadVec(GetActorPos(me),selfPos)||!ReadVec(GetActorPos(target),targetPos))return 1;
        float dx=selfPos[0]-targetPos[0],dy=selfPos[1]-targetPos[1],dz=selfPos[2]-targetPos[2];
        distance=dx*dx+dy*dy+dz*dz;
        if(distance>=Config.RollDistance*Config.RollDistance||!CopydogAwayYaw(selfPos,targetPos,&away))return 1;
        int tool=GetCurrentWeapon(1);
        COPYDOG_PARRY_TRACK predictedClock={.self=(uintptr_t)me,.actor=(uintptr_t)target,
            .animation=predictedAnimation,.stable=1,.started=now};
        int predictedMode=0;(void)RollEquipmentRaw(&predictedMode);
        uint64_t model[]={32u|256u,(uint64_t)prediction,(uint64_t)RemoteAttackWeapon(target),
            (uint64_t)predictedAnimation,(uint64_t)tool,0,0,0,0,50};
        TracePlanBounds(i,&predictedClock,now,model,predictedMode,distance,PARRY_ROLL_PRIORITY,
            (uint64_t)(unsigned)ceil(predictedContact*1000.0));
        if(SubmitDirectedRoll(ActionRollForward,target,predictedAnimation,tool,now,away))CopydogPredictionConsume(i);
        return 1;
    }
    COPYDOG_ROLL_NATIVE_CLOCK*native=&NativeParryClock[i];
    int repeat=native->repeat!=ROLL_REPEAT_NONE&&e->initialized&&e->started==native->nameStarted;
    unsigned elapsed=now-e->started;
    int animation;
    if(repeat){
        /* Both the authored attack interval and elapsed phase are converted
           to wall time. A known repeat cannot fall back to the first name edge. */
        if(native->repeat!=ROLL_REPEAT_TRACK||!native->valid||native->advances<2||
           now!=native->tick||native->key.self!=(uintptr_t)me||
           native->key.actor!=(uintptr_t)target||native->key.actor!=e->actor||
           native->key.network!=e->networkId||native->key.mode!=(unsigned)e->mode||
           CopydogEarlyBase(native->key.weapon)!=e->weaponBase||!e->active||!e->stable||
           e->candidateActive||e->suppressed||!CopydogEarlyValidId(e->name,native->frame.animation))return 1;
        double age=1000.*native->frame.current/native->rate;
        if(!(age>=0.&&age<=UINT_MAX))return 1;
        elapsed=(unsigned)age;animation=native->frame.animation;clockSource=ROLL_CLOCK_NATIVE_REPEAT;
    }else animation=CopydogRollClockId(e,&EarlyProfiles,&ParryTrack[i],(uintptr_t)me,(uintptr_t)target,&clockSource);
    if(animation<0)return 0;
    const COPYDOG_PHASE_ATTACK_PROFILE*profile=0;
    for(unsigned n=0;n<COPYDOG_PHASE_ATTACK_COUNT;++n)if(CopydogPhaseAttacks[n].animation==animation){profile=&CopydogPhaseAttacks[n];break;}
    if(!profile)return 0;
    float a[3],b[3];
    if(!ReadVec(GetActorPos(me),a)||!ReadVec(GetActorPos(target),b))return 0;
    float dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2],distance=dx*dx+dy*dy+dz*dz;
    if(distance>=Config.RollDistance*Config.RollDistance||GetAsyncKeyState(VK_SPACE)<0)return 0;
    int mode,raw=RollEquipmentRaw(&mode),qmin,qmax;
    CopydogRollQBind(&RollQ,(uintptr_t)me,raw,mode);
    int qsource=CopydogRollQBounds(&RollQ,&qmin,&qmax);
    for(unsigned n=0;n<profile->count&&n<32;++n){
        if(PhaseRollSegment[i]&(1u<<n))continue;
        const COPYDOG_PHASE_ATTACK_INTERVAL*h=&CopydogPhaseAttackIntervals[profile->firstInterval+n];
        COPYDOG_ROLL_PHASE_WINDOW w;
        int first=h->startUs,last=h->endUs;
        if(repeat&&!CopydogRollScaleInterval(&first,&last,native->rate))continue;
        if(!CopydogRollPhaseWindow(first,last,0,433333,qmin,qmax,0,32,1,&w))continue;
        if(elapsed>(unsigned)w.endMs)continue;
        if(!CopydogRollPhaseDue(&w,elapsed)||!AutoRollCanSubmit(me))return 1;
        if(CrossbowAnimationOnly(target,animation))return 1;
        float away;if(!CopydogAwayYaw(a,b,&away))return 1;
        COPYDOG_PARRY_TRACK clock={.self=(uintptr_t)me,.actor=e->actor,.animation=animation,.stable=1,.started=now-elapsed};
        int tool=GetCurrentWeapon(1);
        unsigned source=56u|(qsource==COPYDOG_ROLL_Q_MEASURED?128u:0u);
        uint64_t model[]={source,clockSource,(uint64_t)RemoteAttackWeapon(target),(uint64_t)animation,(uint64_t)tool,
            0,0,0,(uint64_t)w.preferredMs,(uint64_t)w.endMs};
        TracePlanBounds(i,&clock,now,model,mode,distance,PARRY_ROLL_PRIORITY,((uint64_t)(unsigned)qmin<<32)|(unsigned)qmax);
        if(SubmitDirectedRoll(ActionRollForward,target,animation,tool,now,away))PhaseRollSegment[i]|=1u<<n;
        return 1;
    }
    /* Known expired phases never fall through to the legacy C8 roll timer. */
    return 1;
}
static void ObserveEarlyTargets(char*me,unsigned now)
{
    ObserveDynamicParrySelf(me,now);
    int predictionActive=(!AutoRollPaused()||PhaseParryKeyDown())&&!CopyAction;
    static int precedingActive;
    if(!GameCadence.running||NewCombatStamp||predictionActive!=precedingActive){
        CopydogPredictionObserveFrame(0,predictionActive?(uintptr_t)me:0,now,GameCadence.running);
        for(unsigned slot=1;slot<=MAX_REMOTE_PLAYER;slot++)
            CopydogPredictionObserveFrame(slot,predictionActive?(uintptr_t)Player(slot):0,now,GameCadence.running);
    }
    precedingActive=predictionActive;
    ObservePhaseQ(me,now);
    if(EarlySelf!=(uintptr_t)me){
        memset(NativeCues,0,sizeof NativeCues);memset(CueWatch,0,sizeof CueWatch);
        memset(EarlyTrack,0,sizeof EarlyTrack);
        memset(PhaseEpisode,0,sizeof PhaseEpisode);
        memset(PhaseRollSegment,0,sizeof PhaseRollSegment);EarlySelf=(uintptr_t)me;
    }
    for(unsigned i=1;i<=MAX_REMOTE_PLAYER;++i){
        char*target=NULL,name[RC_NAME_CAP];COPYDOG_PARRY_TRACK*t=&ParryTrack[i];
        if(!ReadRemoteSlot(i,&target)){NativeCues[i].readable=0;continue;}
        if(target&&!RollCatchName(target,name)){
            ObserveNativeParryCue(i,target,NULL,now,predictionActive);continue;
        }
        if(!target){
            CopydogEarlyObserve(&EarlyTrack[i],&EarlyProfiles,0,0,0,0,0,-1,0,now);
            ObserveNativeParryCue(i,target,NULL,now,0);continue;
        }
        int weapon=RemoteAttackWeapon(target)/10000*10000,mode=(int)RemoteHandMode(target);
        int stableId=t->stable&&!t->candidateActive?t->animation:-1;
        CopydogEarlyObserve(&EarlyTrack[i],&EarlyProfiles,(uintptr_t)target,readlpt(int,target+0x78),
            weapon,mode,name,stableId,t->started,now);
        COPYDOG_EARLY_STATE*e=&EarlyTrack[i];
        if(e->stable&&!e->candidateActive){
            unsigned previous=PhaseEpisode[i].started;uintptr_t actor=PhaseEpisode[i].actor;
            CopydogPhaseEpisodeObserve(&PhaseEpisode[i],e->actor,e->networkId,weapon,mode,e->name,e->started);
            if(previous!=e->started||actor!=e->actor){
                PhaseRollSegment[i]=0;
                CopydogPredictionEpisode(i,e->actor,PhaseEpisode[i].episodeStarted);
            }
            if(CopydogEarlyNameKind(e->name)>2&&CopydogPhaseEpisodeSpent(&PhaseEpisode[i])){
                e->spent=1;
                if(t->stable&&CopydogEarlyValidId(e->name,t->animation))t->spent=1;
            }
        }
        ObserveNativeParryCue(i,target,name,now,predictionActive);
    }
}
static COPYDOG_COPY_KEY CopyKey;
static void UpdateCopyKey(void)
{
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    int before=CopyAction;
    int after=CopydogCopyKey(&CopyKey,Config.CopyActionKey&&GetAsyncKeyState(Config.CopyActionKey)<0,
        GetAsyncKeyState(VK_SHIFT)<0,pid==GetCurrentProcessId(),before);
    if(!CopydogEnabled||exitThread)after=0;
    if(after!=before){
        CopyAction=(char)after;
        if(!after){TargetWalkSpeed=0;TargetCurrentMagic=0;TargetCurrentGoods=0;ReleaseCopydogKeys();}
    }
}
double rdiff(double a,double b)
{
	double d=__imp_fmod(__imp_fabs(a-b),M_PI*2);
	return __imp_fabs(d<M_PI?d:d-M_PI*2);
}
/* Physical attacks with phase data have their own per-segment latch. Legacy
   IDs have one opportunity per observed swing, not one per recovery cycle. */
static struct {uintptr_t self,actor;unsigned started;int animation;} LegacyRollUsed[MAX_REMOTE_PLAYER+1];
static int LegacyPassivePose(char *actor,int animation)
{
    if((animation>=4000&&animation<19000)||IsNonDmgAttack(animation))return 1;
    char name[RC_NAME_CAP];
    if(!RollCatchName(actor,name))return 0;
    return RcSelfReady(name)||!strncmp(name,"damage",6)||!strncmp(name,"adddamage",9)||
        !strncmp(name,"guarddamage",11)||!strncmp(name,"death",5)||!strcmp(name,"throwdef")||
        !strcmp(name,"fallstart")||!strcmp(name,"land")||!strcmp(name,"landlow");
}
static void TryLegacyRoll(int i,char *pt,int anim,int own,int mode,unsigned tc,
    int ahead,int okattack,float distance,const float *pos,const float *targetPos)
{
    COPYDOG_PARRY_TRACK *t=&ParryTrack[i];
    if(distance>=Config.RollDistance*Config.RollDistance){PIPE_INC(PIPE_RANGE);return;}
    if(GetAsyncKeyState(VK_SPACE)<0){PIPE_INC(PIPE_SPACE);return;}
    if(LegacyRollUsed[i].actor==t->actor&&LegacyRollUsed[i].self==t->self&&
       LegacyRollUsed[i].started==t->started&&LegacyRollUsed[i].animation==anim)return;
    int first,last;GetAnimAtkTime(anim,&first,&last);
    int unblockable=Unparriable(anim);
    unsigned age=tc-t->started;
    if(first<=0&&!unblockable){ParrySnapshot(anim,age,first,last,-1,-1,PARRY_UNKNOWN);return;}
    /* With no resource window, accept a fresh stable ID only. Never wake up
       after hitstun and dodge an unchanging, seconds-old unknown pose. */
    if(first<0&&(age>COPYDOG_ANIMATION_STABLE_MS+COPYDOG_EARLY_GAP_MS||LegacyPassivePose(pt,anim)))return;
    if(tc-prevparrytime<=1024||CrossbowAnimationOnly(pt,anim))return;
    int latency=i>0?Latency[i-1]:0;if(latency>0)ahead+=latency;
    int weapon=GetCurrentWeapon(0)/10000*10000,skill=GetWeaponSkill(weapon);
    int lead=weapon==1020000||skill==91||skill==80||(skill/100==6&&mode==1)?768:512;
    int tool=GetCurrentWeapon(1);
    int dispatch=Defense.haveDelay&&Defense.delayKind==DEF_ROLL&&Defense.delayTool==tool?Defense.delayMs:0;
    if(first>=0&&!CopydogDefenseDue(first,last,(int)age,lead,ahead,dispatch))return;
    if(okattack&&!unblockable&&!IsStagger(own)&&
       !(((first-lead-ahead<0&&latency>0)||distance>Config.ParryDistance*Config.ParryDistance||!PhaseParryKeyDown())&&
         ((first<0&&IsDmgAttack(anim))||(int64_t)age<=last-256)))return;
    if(AutoRollPaused()){ParrySnapshot(anim,age,first,last,-1,-1,PARRY_SHIFT_PAUSE);return;}
    float yaw;if(!CopydogAwayYaw(pos,targetPos,&yaw))return;
    int action=SupportsAutomaticQuickstep(tool)&&readlpt(int,addr1,0x80,XA,0x18,0xe4)>0?ActionQuickStep:ActionRollForward;
    if(SubmitDirectedRoll(action,pt,anim,tool,tc,yaw)){
        LegacyRollUsed[i].self=t->self;LegacyRollUsed[i].actor=t->actor;
        LegacyRollUsed[i].started=t->started;LegacyRollUsed[i].animation=anim;
        ParrySnapshot(anim,age,first,last,-1,-1,PARRY_ROLL_PRIORITY);
    }
}
static int prevanim[MAX_REMOTE_PLAYER+1];
static unsigned prevanimtime[MAX_REMOTE_PLAYER+1];
void CheckAnimation(uintptr_t param)
{
    UpdateDefenseState(timeGetTime());
    if(AutomaticActionQueueBusy(Player0,timeGetTime()))return;
    PIPE_INC(PIPE_ENTRY);
	int i,anim,lanim,myanim,mylanim,tdiff,ahead,action;
	char*pt,*me=Player0,*st,okattack;
	if(param<=MAX_REMOTE_PLAYER)i=(int)param,pt=Player(i),ahead=Config.ParryAhead;else i=0,pt=(char*)param,ahead=0;
	if(pt&&me){
		unsigned tc=timeGetTime();
		anim=GetActorAnim(pt);if(anim<0){PIPE_INC(PIPE_BAD_ANIM);return;}lanim=anim%100000;myanim=GetActorAnim(me);if(myanim<0){PIPE_INC(PIPE_BAD_ANIM);return;}mylanim=myanim%100000;
        atomic_store(&ParryPipeline[PIPE_SELF_ANIM],(uint64_t)myanim);atomic_store(&ParryPipeline[PIPE_TARGET_ANIM],(uint64_t)anim);
		readlpv(st,pt+XA,0);
        CopydogTrackActor(&RollGate,(unsigned)i,(uintptr_t)pt,anim,0);
        ObserveParryClock((unsigned)i,me,pt,anim,tc);
        /* Short pose/zero flashes must not restart the attack clock or replay actions. */
        if(!ParryTrack[i].stable||ParryTrack[i].candidateActive||ParryTrack[i].animation!=anim){
            ParrySnapshot(anim,0,-1,-1,-1,-1,PARRY_UNSTABLE);return;
        }
        if(anim!=prevanim[i]||tc-prevanimtime[i]>1536)prevanimtime[i]=tc;
        tdiff=(int)(tc-prevanimtime[i]);
		int readyToRoll=ReadyToRoll();
		{
			float*pos=GetActorPos(me),*tpos=GetActorPos(pt),*tdir=GetActorDir(pt),*dir=GetActorDir(me),d[3],dist,vdist,hdist,fa,da;char equiptype=readlpt(char,addr2,16,0x2b8);
			float posv[3],tposv[3],tdirv[3],dirv[3];
            if(!ReadVec(pos,posv)||!ReadVec(tpos,tposv)||!ReadVec(tdir,tdirv)||!ReadVec(dir,dirv)){PIPE_INC(PIPE_BAD_VECTOR);return;}
            pos=posv;tpos=tposv;tdir=tdirv;dir=dirv;
            d[0]=tpos[0]-pos[0];d[1]=tpos[1]-pos[1];d[2]=tpos[2]-pos[2];
			dist=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];vdist=d[0]*d[0]+d[2]*d[2];hdist=__imp_fabs(d[1]);
			fa=__imp_atan2(d[0],d[2])-M_PI;da=rdiff(fa,tdir[1]);

			readlpv(st,pt+XA,0);
			okattack=ReadyToAttack();
			int backstabGeometry=vdist<Config.BackstabDist*Config.BackstabDist&&hdist<1&&da<Config.BackstabRad&&
			IsBackstabTargetAnimation(lanim)&&st&&(st[0x58]&2)==0&&((st[0x5c]&0x11)==0||lanim<27000||lanim>28000);
            int backstabEligible=backstabGeometry&&readlpt(int,pt+XA,0x18,0xd8)>0&&ReadyToCritical();
            if(!backstabGeometry)CopydogAllowBackstab(&RollGate,(unsigned)i,0);
            if(backstabEligible){
                if(CopydogBackstabLocked(&BackstabTransaction,(uintptr_t)me,myanim,tc))goto animation_done;
                if(!CopydogAllowBackstab(&RollGate,(unsigned)i,1))goto animation_done;
                PIPE_INC(PIPE_PRIOR_ACTION);
				SetFacing(me,fa);
                CombatFrameClaimed=1;
                if(TryDoAction(ActionBackstab)){
                    CombatFrameClaimed=1;
                    CopydogBackstabSubmitted(&BackstabTransaction,(uintptr_t)me,tc);
                }else CopydogAllowBackstab(&RollGate,(unsigned)i,0);
                goto animation_done;
            }
            if(atomic_load(&CopydogDiagnosticParryEnabled)){
                if(!AutoRollPaused()&&AutoRollCanSubmit(me))
                    TryLegacyRoll(i,pt,anim,myanim,equiptype,tc,ahead,okattack,dist,pos,tpos);
                goto animation_done;
            }
            if(!readyToRoll){
                PIPE_INC(PIPE_NOT_READY);
                if(!AutoRollPaused()&&AutoRollCanSubmit(me))TryLegacyRoll(i,pt,anim,myanim,equiptype,tc,ahead,okattack,dist,pos,tpos);
                goto animation_done;
            }
            if(dist<16&&(lanim==30100||lanim==34100||lanim==42050||lanim==40050)&&tdiff<768&&okattack){
                PIPE_INC(PIPE_PRIOR_ACTION);
				SetFacing(me,fa);
				FrameAction(GetNextAttack());
			}else if(dist<16&&IsNonDmgAttack(anim)&&!CopydogIsParryRecovery(anim)&&tdiff<1536&&okattack&&AutoRollPaused()){
                PIPE_INC(PIPE_PRIOR_ACTION);
				action=GetNextHeavyAttack();
				SetFacing(me,fa);
                if(!HeavyRelease.self){
                    FrameAction(action);
                    HeavyRelease.self=me;HeavyRelease.action=action-1;HeavyRelease.due=tc+HEAVY_RELEASE_MS;
                }
			}else if(da<Config.RollAtkRad&&dist<Config.RollAtkDist*Config.RollAtkDist&&((27100<=myanim&&myanim<27200)||(38020<=mylanim&&mylanim<=38070))&&30000<=lanim&&lanim<50000&&okattack){
                PIPE_INC(PIPE_PRIOR_ACTION);
				SetFacing(me,fa);
				FrameAction(GetNextRollAttack());
			}else if(dist<Config.ForestallDistance*Config.ForestallDistance&&tdiff<256&&(i==0||Latency[i-1]<Config.ForestallLatency)&&okattack&&IsFastWeapon(GetCurrentWeapon(1))&&IsSlowAttack(anim)){
                PIPE_INC(PIPE_PRIOR_ACTION);
				SetFacing(me,fa);
				FrameAction(27100<=myanim&&myanim<27200?GetNextRollAttack():GetNextAttack());
			}else TryLegacyRoll(i,pt,anim,myanim,equiptype,tc,ahead,okattack,dist,pos,tpos);
		}
animation_done:
		prevanim[i]=anim;
	}else PIPE_INC(PIPE_MISSING);
}

static void CheckOtherActions(unsigned slot,char*me,char*target,unsigned now,enum CD_PARRY_FRAME_DECISION choice)
{
    if(choice==CD_FRAME_PRESS||choice==CD_FRAME_WAIT)return;
    if(slot&&TryPhaseAutomaticRoll(slot,me,now))return;
    CheckAnimation(slot?slot:(uintptr_t)target);
}
static void CheckFrameTargets(char*me,char*npc,unsigned now)
{
    char*targets[MAX_REMOTE_PLAYER+1]={npc};
    unsigned count=npc?1u:0u;
    for(unsigned i=1;i<=MAX_REMOTE_PLAYER;i++){
        char*actor=Player(i);
        if(!actor){DynamicTargets[i]=(ParryFrameTarget){0};continue;}
        targets[i]=actor;count++;
    }
    if(!npc)DynamicTargets[0]=(ParryFrameTarget){0};
    if(!count)return;
    for(unsigned i=0;i<=MAX_REMOTE_PLAYER;i++){
        if(!targets[i])continue;
        enum CD_PARRY_FRAME_DECISION choice=TryFrameAutomaticParry(i,me,targets[i],now);
        CheckOtherActions(i,me,targets[i],timeGetTime(),choice);
    }
}

/* Explicit historical replay entry; the production worker never calls it. */
static __attribute__((unused)) void CheckLegacyFrameTargetsForOffline(char*me,char*npc,unsigned now)
{
    char*targets[MAX_REMOTE_PLAYER+1]={npc};
    unsigned count=npc?1u:0u,single=0;
    for(unsigned i=1;i<=MAX_REMOTE_PLAYER;i++){
        char*actor=Player(i);
        if(!actor){DynamicTargets[i]=(ParryFrameTarget){0};continue;}
        if(!atomic_load(&CopydogDiagnosticParryEnabled)&&readlpt(unsigned char,actor+0x74)<=4&&readlpt(unsigned char,me+0x74)<=4){PIPE_INC(PIPE_TYPE);continue;}
        targets[i]=actor;single=i;count++;
    }
    if(!npc)DynamicTargets[0]=(ParryFrameTarget){0};
    if(!count)return;
    if(count==1){
        enum CD_PARRY_FRAME_DECISION choice=TryLegacyFrameAutomaticParryForOffline(single,me,targets[single],now);
        CheckOtherActions(single,me,targets[single],now,choice);return;
    }
    FramePress presses[MAX_REMOTE_PLAYER+1];
    enum CD_PARRY_FRAME_DECISION choices[MAX_REMOTE_PLAYER+1]={0};
    for(unsigned i=0;i<=MAX_REMOTE_PLAYER;i++)
        if(targets[i])choices[i]=EvaluateParryFrame(i,me,targets[i],now,&presses[i]);
    /* All deadlines share the same wall-time origin, including the locked NPC. */
    for(;;){
        unsigned best=MAX_REMOTE_PLAYER+1;
        for(unsigned i=0;i<=MAX_REMOTE_PLAYER;i++)
            if(choices[i]==CD_FRAME_PRESS&&(best>MAX_REMOTE_PLAYER||presses[i].deadline<presses[best].deadline))
                best=i;
        if(best>MAX_REMOTE_PLAYER)break;
        if(DispatchParryFrame(&presses[best]))return;
        choices[best]=CD_FRAME_WAIT;
    }
    for(unsigned i=0;i<=MAX_REMOTE_PLAYER;i++)
        if(targets[i])CheckOtherActions(i,me,targets[i],timeGetTime(),choices[i]);
}

static unsigned char SyntheticKeys[256];
int SendKey(unsigned key,unsigned flags)
{
	DWORD pid;INPUT Input={0};
	if(key<3){
		Input.type=INPUT_MOUSE;
		Input.mi.dwFlags=flags;
		Input.mi.dx=Input.mi.dy=Input.mi.time=Input.mi.dwExtraInfo=0;
	}else{
		Input.type=INPUT_KEYBOARD;
		Input.ki.wVk=key;
		Input.ki.wScan=MapVirtualKeyW(key,MAPVK_VK_TO_VSC);
		Input.ki.dwFlags=flags==16?0:flags;
		Input.ki.time=Input.ki.dwExtraInfo=0;
	}
	short r=GetAsyncKeyState(key);
    if(key>=256)return r;
    if(flags==KEYEVENTF_KEYUP&&!SyntheticKeys[key])return r;
	if((r>=0||flags!=16)&&(flags==MOUSEEVENTF_LEFTUP||flags==KEYEVENTF_KEYUP||(GetWindowThreadProcessId(GetForegroundWindow(),&pid)&&pid==GetCurrentProcessId()))){if(SendInput(1,&Input,sizeof(INPUT))==1)SyntheticKeys[key]=flags==KEYEVENTF_KEYUP?0:1;}
	return r;
}

#include "hook_lifetime.h"
void*OnAction(void*rcx,int*rdx)
{
    COPYDOG_HOOK_SCOPE;
    if(!CopydogEnabled)return doaction(rcx,rdx);
	uintptr_t retaddr=(uintptr_t)__builtin_return_address(0);char*me=Player0;
    if(!rdx)return 0;
    int localAction;
    if((!CopyAction||atomic_load(&CopydogDiagnosticParryEnabled))&&me&&readlp(sizeof localAction,&localAction,rdx)&&IsLocalActionRequest(localAction)&&
       rcx&&rcx==GetActionAddress(me))NoteLocalAction(me,GetActorAnim(me),timeGetTime());
	if(!atomic_load(&CopydogDiagnosticParryEnabled)&&CopyAction&&prevLockTarget&&readlpt(short,prevLockTarget+XA,0x18,0xec)>0&&me&&readlpt(short,me+XA,0x18,0xd8)>0){
		char*aa=GetActionAddress(me);
        if(!aa)return doaction(rcx,rdx);
		if(GetActionAddress(prevLockTarget)==rcx&&*rdx!=ActionGesture&&*rdx!=ActionGesture1){
			intptr_t data[3];
			if(!readlp(sizeof data,data,rdx))return doaction(rcx,rdx);
            if(!AutoRollAction((int)data[0])||(!AutoRollPaused()&&GetAsyncKeyState(VK_SPACE)>=0&&
               !atomic_load(&MirrorDefenseBusy)&&ReadAutoRollState(me)==0))doaction(aa,data);
		}else if(CopydogGameBase<retaddr&&retaddr-CopydogGameBase<CopydogGameSize&&rcx==aa&&(*rdx==1070||*rdx==1071||*rdx==1074||*rdx==1075||*rdx==1170||*rdx==1405||*rdx==1406||*rdx==1587||*rdx==1655||*rdx==1846||*rdx==1847||*rdx==1848||
		*rdx==1849||*rdx==2422||*rdx==3014||*rdx==3015||*rdx==3016||*rdx==3017||*rdx==3072||*rdx==3146||*rdx==3147||*rdx==3148||*rdx==3149||*rdx==3776||*rdx==3777||*rdx==3778||*rdx==3779))return 0;
		
	}
	return doaction(rcx,rdx);
}

int OnGetCurrentMagic(int*p,int i)
{
    COPYDOG_HOOK_SCOPE;
	if(CopydogEnabled&&p==readlpt(int*,addr2,16,0x470)&&CopyAction&&prevLockTarget&&readlpt(short,prevLockTarget+XA,0x18,0xec)>0&&TargetCurrentMagic>0)return TargetCurrentMagic;
	if(i<0||i>13||!p)return -1;
	return readlpt(int,p+i*2+6);
}

int*OnGetCurrentGoods(int*p,int*r,int i)
{
    COPYDOG_HOOK_SCOPE;
    if(!r)return 0;
    int*equip=readlpt(int*,addr2,16);
    int value=(CopydogEnabled&&equip&&p==equip+341&&CopyAction&&prevLockTarget&&readlpt(short,prevLockTarget+XA,0x18,0xec)>0&&TargetCurrentGoods>0)?(0x40000000|TargetCurrentGoods):(p&&i>=0&&i<=9?readlpt(int,p+i+22):-1);
    writelpt(int,value,r);
	return r;
}

extern char OnSendAttack[],OnWalk[];

#ifndef COPYDOG_OFFLINE_TEST
asm(".globl OnSendAttack\nOnSendAttack:pushfq;lock incl CopydogHookCalls(%rip);popfq;movq $176,32(%rsp);movq %rdi,%r9;movl $20,%r8d;movq %rbx,%rdx;movq CdSessionAddress(%rip),%rcx;movq (%rcx),%rcx;call OnSendPacket;testl %eax,%eax;jne 1f;movl $1,%eax;pushfq;lock decl CopydogHookCalls(%rip);popfq;jmp *CdSendDone(%rip);"
"1:movq $176,32(%rsp);movq %rdi,%r9;movl $20,%r8d;movq %rbx,%rdx;movq CdSessionAddress(%rip),%rcx;movq (%rcx),%rcx;pushfq;lock decl CopydogHookCalls(%rip);popfq;jmp *CdSendContinue(%rip);");
asm(".globl OnWalk\nOnWalk:pushfq;lock incl CopydogHookCalls(%rip);popfq;movq 8(%rsi,%rcx,8),%rax;cmpl $46,%ecx;jne 1f;cmpl $0,CopydogEnabled(%rip);je 1f;cmpb $0,CopyAction(%rip);je 1f;cmpl $0x3F800000,%eax;jne 1f;movl TargetWalkSpeed(%rip),%eax;1:movq %rax,8(%rdi);pushfq;lock decl CopydogHookCalls(%rip);popfq;jmp *CdWalkResume(%rip);");

#endif

DWORD PollingThreadId;
static int RollPauseObserved;
static int TryBulletAutoRoll(unsigned tc)
{
    CD_BULLET_TICKET selected;
    char *me=Player0;
    RollPauseObserved=!CopydogEnabled||exitThread||!me||AutoRollPaused()||GetAsyncKeyState(VK_SPACE)<0||CopyAction;
    CombatSetBulletInputPaused(RollPauseObserved);
    if(RollPauseObserved){CopydogClearRollDirection();return 0;}
    if(!CombatPeekBulletThreat(tc)||!AutoRollCanSubmit(me))return 0;
    int mode=0,raw=RollEquipmentRaw(&mode),lo,hi;
    CopydogRollQBounds(RollQ.self==(uintptr_t)me&&RollQ.tool==raw&&RollQ.mode==mode?&RollQ:NULL,&lo,&hi);
    /* Center contact between latest candidate iframe onset and earliest end:
       queue q, name/resource shift [0,32] ms, light-roll duration 433 ms.
       A threat seen inside that lead is due immediately; don't wait again. */
    unsigned lead=(unsigned)(hi+32+lo+433)/2;
    if(!CombatSelectDueBullet(tc,lead,&selected))return 0;
    if(!DispatchDefense(ActionRollForward,DEF_ROLL,NULL,-1,GetCurrentWeapon(1),tc,&selected.yaw,NULL,&selected))return 0;
    CombatConfirmBullet(&selected);CombatRecordBulletRoll();
    if(selected.areaSource){
        /* This poll-thread event is readable by an already running ring recorder. */
        uint64_t record[TRACE_WORDS]={0};
        record[1]=TRACE_AREA_ROLL;record[2]=tc;record[3]=selected.self;
        record[12]=(uint64_t)(int64_t)(int32_t)(selected.contact-tc);
        record[15]=selected.areaSource;record[16]=selected.id;
        record[21]=Defense.requests;record[23]=Defense.submitted;record[28]=(uint32_t)selected.owner;
        CopydogTraceWrite(&CopydogAutoTrace,record);
    }
    return 1;
}
static void ObserveParryTargets(char*me,char*locked,unsigned tc)
{
    /* Observation does not submit actions or update the legacy roll clock. */
    for(unsigned i=1;i<=MAX_REMOTE_PLAYER;++i){
        char*pt=Player(i);
        if(!pt){ParryTrack[i]=(COPYDOG_PARRY_TRACK){0};continue;}
        PIPE_INC(PIPE_TARGETS);
        int anim=GetActorAnim(pt);
        if(anim>=0)ObserveParryClock(i,me,pt,anim,tc);
    }
    /* The early polling pass only refreshes remote slots, before lock resolution. */
    if(!locked)return;
    if(GetPlayerIndex(locked)<0){
        int anim=GetActorAnim(locked);
        if(anim>=0)ObserveParryClock(0,me,locked,anim,tc);
    }else ParryTrack[0]=(COPYDOG_PARRY_TRACK){0};
}
enum { TRACE_RIPOSTE_CANDIDATE=8,TRACE_RIPOSTE_BLOCKED,TRACE_RIPOSTE_REQUEST,
       TRACE_RIPOSTE_CONFIRMED,TRACE_RIPOSTE_FAILED };
static struct {
    uintptr_t actor;
    unsigned armed,resultRequest;
    int candidateLogged,blockReason,resultState;
} RiposteTraceState[MAX_REMOTE_PLAYER+1];
static void TraceRiposte(unsigned slot,unsigned event,int reason,unsigned tc)
{
    if(slot<1||slot>MAX_REMOTE_PLAYER)return;
    COPYDOG_RIPOSTE_TRACK*t=&RiposteTrack[slot];
    uint64_t record[TRACE_WORDS]={0};
    char*me=Player0,*target=Player(slot);
    record[1]=event;record[2]=tc;record[3]=(uintptr_t)me;record[4]=t->actor;
    record[5]=(uint64_t)(int64_t)(target?GetActorAnim(target):-1);
    record[6]=(uint64_t)(int64_t)(me?GetActorAnim(me):-1);
    record[7]=GetCurrentWeapon(1);record[8]=readlpt(unsigned,addr2,16,0x2b8);
    record[10]=t->armed;record[11]=(unsigned)(tc-t->armed);record[14]=reason;
    record[15]=32;record[18]=t->criticalState;record[19]=Defense.phase;record[20]=DEF_PARRY;
    record[23]=t->criticalRequested;record[24]=t->criticalConfirmed;record[25]=(uint64_t)(int64_t)CopydogSelectFrontalThrow(me);
    float pos[3],targetPos[3];
    if(me&&target&&ReadVec(GetActorPos(me),pos)&&ReadVec(GetActorPos(target),targetPos)){
        float dx=targetPos[0]-pos[0],dz=targetPos[2]-pos[2];
        record[26]=(uint64_t)(__imp_sqrt(dx*dx+dz*dz)*1000);
    }
    CopydogTraceWrite(&CopydogAutoTrace,record);
}
static void TraceRiposteReady(unsigned slot,unsigned tc)
{
    COPYDOG_RIPOSTE_TRACK*t=&RiposteTrack[slot];
    if(RiposteTraceState[slot].actor!=t->actor||RiposteTraceState[slot].armed!=t->armed){
        RiposteTraceState[slot].actor=t->actor;RiposteTraceState[slot].armed=t->armed;
        RiposteTraceState[slot].candidateLogged=0;RiposteTraceState[slot].blockReason=-1;
    }
    if(!RiposteTraceState[slot].candidateLogged){
        TraceRiposte(slot,TRACE_RIPOSTE_CANDIDATE,0,tc);RiposteTraceState[slot].candidateLogged=1;
    }
}
static void TraceRiposteBlocked(unsigned slot,int reason,unsigned tc)
{
    if(RiposteTraceState[slot].blockReason==reason)return;
    TraceRiposte(slot,TRACE_RIPOSTE_BLOCKED,reason,tc);RiposteTraceState[slot].blockReason=reason;
}
static void TraceRiposteResult(unsigned slot,unsigned tc)
{
    COPYDOG_RIPOSTE_TRACK*t=&RiposteTrack[slot];
    if(!t->criticalRequested||(t->criticalState!=RIPOSTE_CRITICAL_CONFIRMED&&t->criticalState!=RIPOSTE_CRITICAL_FAILED))return;
    if(RiposteTraceState[slot].resultRequest==t->criticalRequested&&RiposteTraceState[slot].resultState==t->criticalState)return;
    TraceRiposte(slot,t->criticalState==RIPOSTE_CRITICAL_CONFIRMED?TRACE_RIPOSTE_CONFIRMED:TRACE_RIPOSTE_FAILED,t->criticalFailure,tc);
    RiposteTraceState[slot].resultRequest=t->criticalRequested;RiposteTraceState[slot].resultState=t->criticalState;
}
static int TryAutomaticRiposte(char*me,unsigned tc)
{
    int own=GetActorAnim(me),ready[MAX_REMOTE_PLAYER+1]={0},waiting=0;
    int partial=PartialParryContext(me,readlpt(int,me+XA,0x18,0xd8),tc);
    if(CopyAction||own<0||(Defense.phase==DEF_CANCELLED&&!partial)){
        for(unsigned i=0;i<=MAX_REMOTE_PLAYER;++i){
            CopydogRiposteInvalidate(&RiposteTrack[i],(uintptr_t)me,own,tc);
            if(i)TraceRiposteResult(i,tc);
        }
        return 0;
    }
    for(unsigned i=1;i<=MAX_REMOTE_PLAYER;++i){
        char*target=Player(i);
        if(Defense.phase==DEF_CANCELLED&&partial&&
           ((uintptr_t)target!=Defense.target||RiposteTrack[i].actor!=Defense.target)){
            CopydogRiposteInvalidate(&RiposteTrack[i],(uintptr_t)me,own,tc);continue;
        }
        if(RiposteTrack[i].criticalState==RIPOSTE_CRITICAL_WAIT&&RiposteTrack[i].actor!=(uintptr_t)target){
            CopydogRiposteInvalidate(&RiposteTrack[i],(uintptr_t)me,own,tc);TraceRiposteResult(i,tc);
        }
        if(!target){CopydogObserveRiposte(&RiposteTrack[i],(uintptr_t)me,0,own,-1,0,tc);continue;}
        int animation=GetActorAnim(target);
        ready[i]=CopydogObserveRiposte(&RiposteTrack[i],(uintptr_t)me,(uintptr_t)target,
                                      own,animation,animation>=0&&IsDmgAttack(animation),tc);
        TraceRiposteResult(i,tc);
        if(ready[i])TraceRiposteReady(i,tc);
        if(RiposteTrack[i].criticalState==RIPOSTE_CRITICAL_WAIT)waiting=1;
    }
    if(Defense.phase==DEF_WAIT_ANIMATION||Defense.phase==DEF_CANCELLED){
        for(unsigned i=1;i<=MAX_REMOTE_PLAYER;++i)if(ready[i])TraceRiposteBlocked(i,1,tc);
        return 0;
    }
    /* Do not interrupt an ongoing frontal critical with any automatic action. */
    if(own%100000==30201||own%100000==30202)return 1;
    if(waiting)return 1;
    if(!ReadyToCritical()){
        for(unsigned i=1;i<=MAX_REMOTE_PLAYER;++i)if(ready[i])TraceRiposteBlocked(i,2,tc);
        return 0;
    }
    float pos[3];if(!ReadVec(GetActorPos(me),pos))return 0;
    unsigned best=0;float nearest=1.8f*1.8f,bestYaw=0;
    for(unsigned i=1;i<=MAX_REMOTE_PLAYER;++i){
        if(!ready[i])continue;
        char*target=Player(i);float targetPos[3],targetDir[3];
        if((uintptr_t)target!=RiposteTrack[i].actor||!ReadVec(GetActorPos(target),targetPos)||
           !ReadVec(GetActorDir(target),targetDir))continue;
        float dx=targetPos[0]-pos[0],dz=targetPos[2]-pos[2],distance=dx*dx+dz*dz;
        float yaw=(float)(__imp_atan2(dx,dz)-M_PI);
        if(distance>=nearest||__imp_fabs(targetPos[1]-pos[1])>=1||rdiff(yaw,targetDir[1])<M_PI-1.0){
            TraceRiposteBlocked(i,3,tc);continue;
        }
        best=i;nearest=distance;bestYaw=yaw;
    }
    if(!best)return 0;
    char*target=Player(best);int animation=GetActorAnim(target);
    if((uintptr_t)target!=RiposteTrack[best].actor||!(animation==6000||animation==6010))return 0;
    if(AutomaticActionQueueBusy(me,tc))return 0;
    SetFacing(me,bestYaw);
    /* A native throw request is not yet a confirmed paired critical. Never queue ordinary R1. */
    if(!CopydogRiposteRequestCritical(&RiposteTrack[best],tc))return 0;
    int result=TryRequestFrontalRiposte(me,target);
    TraceRiposte(best,TRACE_RIPOSTE_REQUEST,result<0?-result:0,tc);
    if(result<0){
        RiposteTrack[best].criticalState=RIPOSTE_CRITICAL_FAILED;
        RiposteTrack[best].criticalFailure=RIPOSTE_FAIL_NATIVE_REJECTED;
        RiposteTrack[best].pending=0;TraceRiposteResult(best,tc);
    }
    CombatFrameClaimed=1;
    return 1;
}
static void ResetFrameWorld(char*me,unsigned now)
{
    memset(DiagnosticEpisodes,0,sizeof DiagnosticEpisodes);
    DiagnosticHitClock=(ParryMoveClock){0};
    DiagnosticSubmittedOwn=DiagnosticConfirmationAmbiguous=DiagnosticLeftBaseline=0;
    DiagnosticPending=(DiagnosticParryEpisode){0};DiagnosticPendingSlot=0;
    DiagnosticNativeBaseline=(CD_NATIVE_FRAME){0};DiagnosticNativeBaselineValid=0;
    SelfMoveClock=(ParryMoveClock){0};
    NoteLocalAction(NULL,-1,now);
    /* An input submitted before a load may still complete. Retire its observations
       and leave the normal cancelled-transaction guard in charge of any next input. */
    if(CopydogDefenseBusy(&Defense)){
        if(Defense.phase!=DEF_CANCELLED){
            CopydogDefenseCancel(&Defense,DEF_SELF_CHANGED,now);
            TraceDefense(TRACE_CANCEL,now,0);
        }
        Defense.self=(uintptr_t)me;Defense.target=0;Defense.reason=DEF_SELF_CHANGED;
        Defense.haveCandidate=Defense.haveTargetCandidate=0;
    }
    atomic_store(&MirrorDefenseBusy,CopydogDefenseBusy(&Defense));
    Defense.haveDelay=0;
    DefenseHandMode=ParryDelayHandMode=0;
    DynamicSelf=(DynamicParrySelf){0};memset(DynamicTargets,0,sizeof DynamicTargets);
    HeavyRelease.self=NULL;
    prevLockTarget=NULL;
    memset(prevanim,0xff,sizeof prevanim);memset(prevanimtime,0,sizeof prevanimtime);
    memset(NativeParryClock,0,sizeof NativeParryClock);
    memset(NativeCues,0,sizeof NativeCues);memset(CueWatch,0,sizeof CueWatch);
    ActiveCueSlot=ActiveCueOnset=0;ActiveRollName[0]=0;
    memset(EarlyTrack,0,sizeof EarlyTrack);memset(&EarlyProfiles,0,sizeof EarlyProfiles);EarlySelf=0;
    memset(PhaseEpisode,0,sizeof PhaseEpisode);
    memset(PhaseRollSegment,0,sizeof PhaseRollSegment);memset(&PhaseQ,0,sizeof PhaseQ);
    memset(LegacyRollUsed,0,sizeof LegacyRollUsed);
    memset(&PhaseQRequest,0,sizeof PhaseQRequest);
    memset(&RollQ,0,sizeof RollQ);
    SeedPhaseMappings();
    (void)CopydogEarlySeed(&EarlyProfiles,SC4_EARLY_WEAPON,SC4_EARLY_MODE,SC4_EARLY_NAME,
        SC4_EARLY_ANIMATION,Sc4EarlyLagsMs,(unsigned)_countof(Sc4EarlyLagsMs));
    memset(LastTraceDecision,0xff,sizeof LastTraceDecision);
    memset(PendingDefenseTrace,0,sizeof PendingDefenseTrace);
    memset(ActiveDefenseTrace,0,sizeof ActiveDefenseTrace);
    for(unsigned i=0;i<_countof(Latency);++i)Latency[i]=0;
    BackstabTransaction=(COPYDOG_BACKSTAB_TRANSACTION){0};
    memset(RiposteTrack,0,sizeof RiposteTrack);
    memset(RollCatch,0,sizeof RollCatch);RollCatchSelf=0;RollCatchHasAttack=0;CombatFrameClaimed=0;
    RollGate=(COPYDOG_ROLL_GATE){0};memset(ParryTrack,0,sizeof ParryTrack);CopydogClearRollDirection();
    CopydogPredictionResetAll();
    CombatClearBulletThreat();
}
/* One poll owns action arbitration; submitted defenses and criticals drain
   before another automatic action may claim the queue. */
static int PollCombatActions(char*me,char*LockTarget,unsigned tc)
{
            ObserveParryTargets(me,LockTarget,tc);
            if(TryAutomaticRiposte(me,tc))return 1;
            if(Defense.phase==DEF_WAIT_ANIMATION||Defense.phase==DEF_CANCELLED)return 1;
            ObserveRollCatch(me,tc);
            if(CopydogDefenseBusy(&Defense))return 1;
            if(CopydogBackstabLocked(&BackstabTransaction,(uintptr_t)me,GetActorAnim(me),tc))return 1;
            if(TryBulletAutoRoll(tc))return 1;
			
			if(LockTarget){if(LockTarget!=prevLockTarget){TargetCurrentMagic=0;TargetCurrentGoods=0;}prevLockTarget=LockTarget;}else LockTarget=prevLockTarget;
            if(!LockTarget)ParryTrack[0]=(COPYDOG_PARRY_TRACK){0};
			if(CopyAction&&readlpt(short,me+XA,0x18,0xd8)>0){
                PIPE_INC(PIPE_COPY);
				if(prevLockTarget&&readlpt(short,prevLockTarget+XA,0x18,0xec)>0){
					int myanim=GetActorAnim(me),anim=GetActorAnim(prevLockTarget),lanim=anim%100000,llhanim=(lanim%1000)/100;float*tdir=GetActorDir(prevLockTarget),*dir=GetActorDir(me),*dir2=GetActorDir2(me);char*tequip,*equip;
					if((20000<=lanim&&lanim<21000)||(23000<=lanim&&lanim<24000)||(27000<=lanim&&lanim<28000)){
						int llanim=anim%10;
						TargetWalkSpeed=llhanim;
						if(llanim==0){SendKey('W',16);SendKey('A',KEYEVENTF_KEYUP);SendKey('S',KEYEVENTF_KEYUP);SendKey('D',KEYEVENTF_KEYUP);}
						else if(llanim==1){SendKey('W',KEYEVENTF_KEYUP);SendKey('A',KEYEVENTF_KEYUP);SendKey('S',16);SendKey('D',KEYEVENTF_KEYUP);}
						else if(llanim==2){SendKey('W',KEYEVENTF_KEYUP);SendKey('A',KEYEVENTF_KEYUP);SendKey('S',KEYEVENTF_KEYUP);SendKey('D',16);}
						else if(llanim==3){SendKey('W',KEYEVENTF_KEYUP);SendKey('A',16);SendKey('S',KEYEVENTF_KEYUP);SendKey('D',KEYEVENTF_KEYUP);}
					}else{
						TargetWalkSpeed=0;
						SendKey('W',KEYEVENTF_KEYUP);SendKey('A',KEYEVENTF_KEYUP);SendKey('S',KEYEVENTF_KEYUP);SendKey('D',KEYEVENTF_KEYUP);
					}
					if((60000<=anim&&anim<=68001)||(80000<=anim&&anim<=91230)){
						if(myanim!=anim)DoIdle(anim);
					}else if(((60000<=myanim&&myanim<=68001)||(80000<=myanim&&myanim<=91230))&&myanim!=60390)DoIdle(60390);
					if(tdir&&dir&&dir2&&!GetLockTarget()){float a=readlpt(float,tdir+1)+(float)M_PI;writelpt(float,a,dir+1);writelpt(float,a,dir2+1);}
					if(readlpv(equip,addr2,16)&&equip&&readlpv(tequip,prevLockTarget+XB)&&tequip){char data[12];if(readlp(sizeof data,data,tequip+0x2b8))writelp(sizeof data,data,equip+0x2b8);}
				}
			}else{
                char*npc=LockTarget&&GetPlayerIndex(LockTarget)<0&&readlpt(short,LockTarget+XA,0x18,0xec)>0?LockTarget:NULL;
                CopydogPredictionObserveFrame(CD_PRED_LOCK_SLOT,npc&&PhaseParryKeyDown()?(uintptr_t)npc:0,tc,GameCadence.running);
                CheckFrameTargets(me,npc,timeGetTime());
			}
            TryRollCatch(me,timeGetTime());

    return CombatFrameClaimed;
}

DWORD WINAPI PollingThreadProc(void*p)
{
    HANDLE cadenceTimer=(HANDLE)p;
    GameCadence=(CD_GAME_CADENCE){0};FramePlayer=0;FrameStampReady=0;NewCombatStamp=0;
    Defense=(COPYDOG_DEFENSE){0};RollPauseObserved=0;
    ResetFrameWorld(NULL,timeGetTime());
    CopydogTraceInit(&CopydogAutoTrace);
    uint64_t startTrace[TRACE_WORDS]={0};startTrace[1]=TRACE_START;startTrace[2]=timeGetTime();
    CopydogTraceWrite(&CopydogAutoTrace,startTrace);
    CopyKey.held=Config.CopyActionKey&&GetAsyncKeyState(Config.CopyActionKey)<0;
    RollKey.held=GetAsyncKeyState(VK_F7)<0;
	char*me;
	while(!exitThread){
        LARGE_INTEGER due={.QuadPart=GameCadence.running?-20000:-40000};
        if(!cadenceTimer||!SetWaitableTimer(cadenceTimer,&due,0,NULL,NULL,FALSE)||
           WaitForSingleObject(cadenceTimer,8)==WAIT_FAILED)Sleep(4);
        UpdateRollKey();UpdateCopyKey();if(exitThread||!CopydogEnabled)break;PIPE_INC(PIPE_LOOPS);
        if(!ReadFramePlayer(&me)){FrameStampUnavailable();continue;}
		if(me){
            PIPE_INC(PIPE_SELF);
            unsigned tc=timeGetTime();
            ObserveParryGameFrame(me,tc);
            UpdateDefenseState(tc);
            ObserveRollQ(me,tc);
            CombatFrameClaimed=0;
            FinishHeavyInput(me,tc);
            ObserveParryTargets(me,0,tc);
            ObserveEarlyTargets(me,tc);
            (void)PollCombatActions(me,GetLockTarget(),tc);

		}else{
            ObserveParryGameFrame(NULL,timeGetTime());
            /* No sample means no network advance, never a synthetic 256ms lead. */
			for(unsigned i=0;i<_countof(Latency);++i)Latency[i]=0;
		}
	}
	ReleaseCopydogKeys();
	return 0;
}

int OnSendPacket(void*rcx,char*to,int id,char*buf,int len)
{
    (void)rcx;
    if(!CopydogEnabled)return 1;
	char*you=net2char(to),*me=Player0;int*bufint=(int*)buf,i=GetPlayerIndex(you)-1;float*bufl=(float*)buf;
    if(id==20&&i>=0&&i<MAX_REMOTE_PLAYER&&me&&buf&&len>=176){
        /* Native execution handshake passes through without forcing a throw or scaling it. */
        if(bufint[17]%1000==610)return 1;
		if(bufint[0]>0||bufint[1]>0||bufint[2]>0||bufint[3]>0||bufint[4]>0){
			bufl[0]*=Config.AtkMult;bufl[1]*=Config.AtkMult;bufl[2]*=Config.AtkMult;bufl[3]*=Config.AtkMult;bufl[4]*=Config.AtkMult;
			LastAttackTime[i]=timeGetTime();
		}
	}
	return 1;
}

void ObserveReceivePacket(char*from,int id,char*buf,int len)
{
    if(!CopydogEnabled||!buf||len<=0)return;
	char*you=net2char(from),*me=Player0;int*bufint=(int*)buf,i=GetPlayerIndex(you)-1;
    if(id==19&&len>=12&&i>=0&&i<MAX_REMOTE_PLAYER&&you==prevLockTarget){
        TargetCurrentMagic=bufint[1];if(bufint[2]>0)TargetCurrentGoods=bufint[2];
    }else if(id==28&&len>=12&&i>=0&&i<MAX_REMOTE_PLAYER&&you&&me){
        if(bufint[1]==readlpt(int,you+0x78)&&bufint[2]==readlpt(int,me+0x78)){
            unsigned tc=timeGetTime();if(tc-(unsigned)LastAttackTime[i]<2048)Latency[i]=(tc-(unsigned)LastAttackTime[i])/2;
        }
    }
}

int CheckBullet(char*bullet,int rvpass)
{
    CombatObserveBullet(bullet);
	return rvpass;
}


void ReleaseCopydogKeys(void){for(unsigned i=3;i<256;++i)if(SyntheticKeys[i])SendKey(i,KEYEVENTF_KEYUP);}
int CopydogInputsReleased(void){for(unsigned i=3;i<256;++i)if(SyntheticKeys[i])return 0;return 1;}
