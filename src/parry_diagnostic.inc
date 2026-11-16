/* Included after the normal solver. Geometry/readiness remain diagnostic;
   permission classification and temporal scheduling govern queue attempts. */
static int DiagnosticEligible(int weapon,int animation)
{
    weapon=CopydogEarlyBase(weapon);
    size_t n=FirstDynamicWindow(CdDynamicEligibility,_countof(CdDynamicEligibility),weapon,animation);
    return n<_countof(CdDynamicEligibility)&&CdDynamicEligibility[n].weapon==weapon&&
        CdDynamicEligibility[n].animation==animation;
}
static int DiagnosticMeleeName(const char*name)
{
    return name&&!strncmp(name,"attack",6)&&!strstr(name,"bow")&&!strstr(name,"shoot")&&
        !strstr(name,"reload")&&!strstr(name,"throw")&&!strstr(name,"magic");
}
static int DiagnosticNeutralName(const char*name)
{
    return name&&(!strncmp(name,"move",4)||!strncmp(name,"idle",4)||
        !strncmp(name,"walk",4)||!strncmp(name,"run",3)||!strncmp(name,"turn",4));
}
static int DiagnosticAttackId(int animation)
{
    int id=animation<0?-1:animation%100000;
    return id>=30000&&id<50000&&!CopydogKnownParryAnimation(animation);
}
static int DiagnosticNameKindById(int animation)
{
    for(int kind=1;*CopydogEarlyCanonicalName(kind);++kind)
        if(CopydogEarlyValidId(CopydogEarlyCanonicalName(kind),animation))return kind;
    return 0;
}
static void ObserveDiagnosticEpisode(unsigned slot,char*me,char*target,unsigned now)
{
    if(slot>MAX_REMOTE_PLAYER)return;
    DiagnosticParryEpisode*e=&DiagnosticEpisodes[slot];
    int handle,network;
    if(!me||!target||me==target||!readlpv(handle,target+8)||!readlpv(network,target+0x78)){
        e->readable=0;return;
    }
    int newActor=e->self!=(uintptr_t)me||e->actor!=(uintptr_t)target||
                 e->handle!=handle||e->network!=network;
    if(newActor)*e=(DiagnosticParryEpisode){.self=(uintptr_t)me,.actor=(uintptr_t)target,
        .handle=handle,.network=network,.animation=-1};
    CD_NATIVE_FRAME frame;int retained=0;
    int have=CopydogReadPublishedNativeFrame((uintptr_t)target,&frame,&retained);
    int visible=GetActorAnim(target),animation=have?frame.animation:visible;
    if(animation<0){e->readable=0;return;}
    /* Fix B: a known old payload cannot introduce a new episode merely because
       the producer/consumer indices or another channel were replayed.  Fresh or
       previously unseen retained publications remain eligible. */
    if(have&&retained&&(!e->active||animation!=e->animation)&&
       (unsigned)frame.index<CD_PRED_CLOCK_COUNT&&
       (e->observedMask&(1u<<frame.index))&&CopydogSameFrame(&e->observed[frame.index],&frame)){
        e->readable=0;return;
    }
    if(have&&retained&&!DiagnosticAttackId(visible)&&
       (!e->clock.valid||frame.clock!=e->clock.last.clock||
        frame.animation!=e->clock.last.animation||frame.current<=e->clock.last.current)){
        e->readable=0;
        (void)CopydogAdvanceFrame(&e->clock,&frame,now);
        return;
    }
    e->readable=1;
    int weapon=RemoteAttackWeapon(target),mode=(int)RemoteHandMode(target);
    char name[RC_NAME_CAP];
    int named=RollCatchName(target,name),melee=named&&DiagnosticMeleeName(name);
    int attack=DiagnosticAttackId(animation)&&DiagnosticEligible(weapon,animation);
    if(attack&&(!named||(!melee&&(!DiagnosticNeutralName(name)||e->namedAttack||!have||retained||
                                                 frame.current>CD_NATIVE_REPEAT_START_MAX_S)))){
        e->readable=0;e->active=0;e->restartPending=0;return;
    }
    if(!attack){
        e->readable=0;
        e->restartPending=0;
        int nativeNeutral=have&&!retained&&named&&DiagnosticNeutralName(name)&&
            !DiagnosticAttackId(frame.animation);
        if(nativeNeutral||!DiagnosticAttackId(visible)||!DiagnosticEligible(weapon,visible)){
            if(!e->neutral){e->neutral=1;e->neutralSince=now;}
            else if(now-e->neutralSince>=COPYDOG_ANIMATION_STABLE_MS){
                e->active=0;
                /* Fix A: confirmed native neutral over 12 ms clears namedAttack and
                   updates neutral clock/animation so the next native-before-visible
                   attack is not rejected due to a previous episode's melee name. */
                if(nativeNeutral){
                    e->namedAttack=0;e->animation=animation;
                    (void)CopydogAdvanceFrame(&e->clock,&frame,now);
                }
            }
        }
        return;
    }
    e->neutral=0;
    int confirmedRestart=0;
    if(e->restartPending&&have&&frame.animation==e->animation){
        if(frame.current>=e->beforeRewind)e->restartPending=0;
        else{
            CD_FRAME_CLOCK*c=&e->restart;
            if(CopydogSameFrame(&c->last,&frame)){e->readable=0;return;}
            unsigned elapsed=now-c->tick;
            double rate=elapsed?1000.*((double)frame.current-c->last.current)/elapsed:0.;
            int continuous=frame.clock==c->last.clock&&elapsed&&elapsed<=CD_PRED_MAX_GAP_MS&&
                fabs((double)frame.previous-c->last.current)<=1e-6&&
                rate>=CD_PRED_MIN_RATE&&rate<=CD_PRED_MAX_RATE;
            if(!continuous){e->restartPending=0;e->readable=0;return;}
            (void)CopydogAdvanceFrame(c,&frame,now);
            if(++e->restartAdvances<2){e->readable=0;return;}
            confirmedRestart=1;e->restartPending=0;
        }
    }else if(e->restartPending)e->restartPending=0;
    int rewind=have&&e->clock.valid&&frame.clock==e->clock.last.clock&&
        frame.animation==e->clock.last.animation&&frame.current<=CD_NATIVE_REPEAT_START_MAX_S&&
        frame.current+CD_NATIVE_REPEAT_REWIND_MIN_S<e->clock.last.current;
    if(rewind&&!confirmedRestart){
        e->restartPending=1;e->restartAdvances=0;e->beforeRewind=e->clock.last.current;
        e->restartStarted=now;e->restart=(CD_FRAME_CLOCK){0};
        (void)CopydogAdvanceFrame(&e->restart,&frame,now);
        e->readable=0;return;
    }
    if(!e->active||animation!=e->animation||confirmedRestart){
        int continuation=!confirmedRestart&&
            ((e->animation==animation&&e->clock.valid&&have&&frame.clock==e->clock.last.clock)||
             (e->active&&e->animation/100000==animation/100000&&
              CopydogPhaseHeavyContinuation(DiagnosticNameKindById(e->animation),DiagnosticNameKindById(animation))));
        if(!continuation){e->spent=0;e->namedAttack=0;e->started=confirmedRestart?e->restartStarted:now;}
        e->active=1;e->clock=confirmedRestart?e->restart:(CD_FRAME_CLOCK){0};
    }
    e->animation=animation;e->weapon=weapon;e->mode=mode;
    if(melee)e->namedAttack=1;
    if(have&&(unsigned)frame.index<CD_PRED_CLOCK_COUNT){
        e->observed[frame.index]=frame;e->observedMask|=1u<<frame.index;
    }
    if(have&&CopydogAdvanceFrame(&e->clock,&frame,now))
        atomic_fetch_add(&CopydogDynamicParryCounters[DP_NATIVE_OBSERVE],1);
}
static int DiagnosticDamageId(int animation)
{
    int id=animation<0?-1:animation%100000;
    return (id>=5000&&id<=5003)||(id>=5100&&id<=5103)||
           (id>=5200&&id<=5203)||(id>=5351&&id<=5352)||
           (id>=5501&&id<=5502)||id==5600||id==6000||id==6010;
}
static int ReadDiagnosticTool(char*me,int*mode,int*raw)
{
    char*equipment=NULL,*again=NULL;
    unsigned loadout[35];
    *mode=*raw=0;
    if(!me||!readlpv(equipment,me+XB)||!equipment||
       !readlp(sizeof loadout,loadout,equipment+0x2b8)||
       !readlpv(again,me+XB)||again!=equipment||loadout[0]<1||loadout[0]>3)return 0;
    unsigned right=loadout[0]==3,slot=loadout[1+right];
    if(slot>=3)return 0;
    unsigned item=loadout[29+slot*2+right];
    if(!item||item>INT_MAX)return 0;
    *mode=(int)loadout[0];*raw=(int)item;return 1;
}
/* Return 0 for a readable non-hit state, 1 for hit recovery, 2 for unknown.
   A drained, unchanged old hit slot does not keep a recovered actor locked. */
static int DiagnosticSelfHit(char*me,unsigned now,int*nativeId)
{
    if(nativeId)*nativeId=0;
    char name[RC_NAME_CAP],again[RC_NAME_CAP];
    int own=me?GetActorAnim(me):-1,hp,handle;
    if(!me||Player0!=me||own<0||!readlpv(hp,me+XA,0x18,0xd8)||hp<=0||
       !readlpv(handle,me+8)||!RollCatchName(me,name)||!RollCatchName(me,again)||
       strcmp(name,again)||GetActorAnim(me)!=own)return 2;
    if(DiagnosticDamageId(own)||!strncmp(name,"damage",6)||
       !strncmp(name,"adddamage",9)||!strncmp(name,"guarddamage",11))return 1;
    ParryMoveClock*s=&DiagnosticHitClock;
    if(s->self!=me||s->handle!=handle)*s=(ParryMoveClock){.self=me,.handle=handle};
    CD_NATIVE_FRAME f;int retained=0;
    if(!CopydogReadPublishedNativeFrame((uintptr_t)me,&f,&retained))return 2;
    if(nativeId)*nativeId=f.animation;
    int sameClock=s->last.clock==f.clock;
    if(!DiagnosticDamageId(f.animation))s->hold=0;
    else if(s->last.clock!=f.clock||s->last.animation!=f.animation||s->last.current!=f.current){
        s->hold=!retained||sameClock?1:2;s->changed=now;
    }
    s->last=f;
    if(DiagnosticDamageId(f.animation)&&!retained)return 1;
    if(s->hold&&now-s->changed>CD_PRED_MAX_AGE_MS)s->hold=0;
    return s->hold==2?2:s->hold?1:0;
}
/* These bands describe a static permission opportunity, not a damage impact.
   Long opportunities use only their onset as an explicitly tagged candidate. */
static FramePlan DiagnosticTimingPlan(const DiagnosticParryEpisode*e,int tool,int mode,unsigned now,unsigned*basis)
{
    FramePlan chosen={.decision=CD_FRAME_SKIP,.reason=PARRY_LATE};
    *basis=CD_DIAG_TIMING_PERMISSION;
    double step=GameCadence.running?GameCadence.wallStep:e->clock.step;
    ParryActivation own;
    if(!e->clock.valid||now-e->clock.tick>CD_PRED_MAX_AGE_MS||!isfinite(step)||step<=0||
       DynamicSelf.self!=e->self||DynamicSelf.mode!=mode||DynamicSelf.tool!=tool||
       !FrameSelfActivation(tool,mode,step,&own)||own.open>=own.close||
       !isfinite(e->clock.rate)||e->clock.rate<=0)
        return (FramePlan){.decision=CD_FRAME_WAIT,.reason=PARRY_UNSTABLE};
    int weapon=CopydogEarlyBase(e->weapon);
    size_t first=FirstDynamicWindow(CdDynamicEligibility,_countof(CdDynamicEligibility),weapon,e->animation);
    for(size_t n=first;n<_countof(CdDynamicEligibility);++n){
        const CD_DYNAMIC_WINDOW*w=&CdDynamicEligibility[n];
        if(w->weapon!=weapon||w->animation!=e->animation)break;
        double age=(uint32_t)(now-e->clock.tick);
        double c0=(w->begin-1000.*e->clock.last.current)/e->clock.rate-age;
        double c1=(w->end-1000.*e->clock.last.current)/e->clock.rate-age;
        if(c1<=0)continue;
        c0=fmax(0,c0);
        FramePlan p={.decision=CD_FRAME_WAIT,.reason=PARRY_EARLY,.bandIndex=n};
        double preferred;unsigned kind=CD_DIAG_TIMING_PERMISSION;
        if(!CopydogDynamicParryWindow(c0,c1,own.open,own.close,0,0,&p.lo,&p.hi,&preferred)){
            kind=CD_DIAG_TIMING_ONSET;
            if(!CopydogDynamicParryWindow(c0,c0,own.open,own.close,0,0,&p.lo,&p.hi,&preferred))continue;
        }
        p.decision=CopydogParryFrameChoiceBounds(p.lo,p.hi,step,kind==CD_DIAG_TIMING_PERMISSION);
        p.reason=p.decision==CD_FRAME_PRESS?PARRY_NONE:p.decision==CD_FRAME_WAIT?PARRY_EARLY:PARRY_LATE;
        if(p.decision==CD_FRAME_PRESS){*basis=kind;return p;}
        if(p.decision==CD_FRAME_WAIT&&
           (chosen.decision!=CD_FRAME_WAIT||p.hi<chosen.hi)){chosen=p;*basis=kind;}
    }
    return chosen;
}
static int DiagnosticPendingTargetStatus(char*target,unsigned now)
{
    DiagnosticParryEpisode*p=&DiagnosticPending;
    int handle,network;
    if(!target||p->actor!=(uintptr_t)target||p->self!=Defense.self||
       (DiagnosticPendingSlot?Player(DiagnosticPendingSlot):GetLockTarget())!=target)return -1;
    if(!readlpv(handle,target+8)||!readlpv(network,target+0x78))return 0;
    if(handle!=p->handle||network!=p->network)return -1;
    int weapon=RemoteAttackWeapon(target),mode=(int)RemoteHandMode(target);
    if(weapon<=0||mode<1||mode>3)return 0;
    if(weapon!=p->weapon||mode!=p->mode)return -1;
    char name[RC_NAME_CAP];
    if(RollCatchName(target,name)){
        if(DiagnosticMeleeName(name))p->namedAttack=1;
        else if(!DiagnosticNeutralName(name)||p->namedAttack)return -1;
    }
    CD_NATIVE_FRAME f;int retained;
    if(!CopydogReadPublishedNativeFrame((uintptr_t)target,&f,&retained))return 0;
    if(f.animation==6000||f.animation==6010)return 0;
    if(f.animation!=p->animation||f.clock!=p->clock.last.clock||
       f.current+CD_NATIVE_REPEAT_REWIND_MIN_S<p->clock.last.current)return -1;
    (void)CopydogAdvanceFrame(&p->clock,&f,now);
    return p->clock.valid&&now-p->clock.tick<=CD_PRED_MAX_AGE_MS?1:0;
}
static int DiagnosticPressCurrent(const FramePress*p,char*me,char*target,unsigned now)
{
    if(!p||!p->diagnostic||p->slot>MAX_REMOTE_PLAYER||
       !atomic_load(&CopydogDiagnosticParryEnabled)||!CopydogEnabled||exitThread||
       GetAsyncKeyState(VK_SHIFT)>=0||CombatFrameClaimed||CurrentDefensePlayer()!=me)return 0;
    if(AutomaticActionQueueBusy(me,now))return 0;
    DiagnosticParryEpisode*e=&DiagnosticEpisodes[p->slot];
    int handle,hp,network,mode,raw;
    if(!ReadDiagnosticTool(me,&mode,&raw)||mode!=p->diagnosticMode||raw!=p->diagnosticRaw)return 0;
    if(!e->active||!e->readable||e->self!=(uintptr_t)me||e->actor!=(uintptr_t)target||
       e->started!=p->cueOnset||e->animation!=p->frame.animation||
       (p->slot?Player(p->slot):GetLockTarget())!=target||
       !readlpv(handle,target+8)||handle!=e->handle||
       !readlpv(network,target+0x78)||network!=e->network||
       !readlpv(hp,target+XA,0x18,0xd8)||hp<=0)return 0;
    DiagnosticParryEpisode admission=*e;
    CD_NATIVE_FRAME latest;int retained=0;
    if(CopydogReadPublishedNativeFrame((uintptr_t)target,&latest,&retained)){
        if(latest.animation!=e->animation||
           (e->clock.valid&&(latest.clock!=e->clock.last.clock||
            (latest.current<=CD_NATIVE_REPEAT_START_MAX_S&&
             latest.current+CD_NATIVE_REPEAT_REWIND_MIN_S<e->clock.last.current))))return 0;
        (void)CopydogAdvanceFrame(&admission.clock,&latest,timeGetTime());
    }else if(GetActorAnim(target)!=e->animation)return 0;
    if(RemoteAttackWeapon(target)!=e->weapon||RemoteHandMode(target)!=(unsigned)e->mode)return 0;
    char name[RC_NAME_CAP];
    if(!RollCatchName(target,name)||(!DiagnosticMeleeName(name)&&
       (!DiagnosticNeutralName(name)||e->namedAttack)))return 0;
    if(!DiagnosticEligible(e->weapon,e->animation))return 0;
    now=timeGetTime();
    unsigned basis;
    FramePlan temporal=DiagnosticTimingPlan(&admission,raw/100*100,mode,now,&basis);
    if(temporal.decision!=CD_FRAME_PRESS)return 0;
    return DiagnosticSelfHit(me,now,NULL)==0;
}
static void DiagnosticGate(uint64_t*r,unsigned stage,int pass)
{
    uint64_t bit=UINT64_C(1)<<stage;
    r[17]|=bit;r[pass?12:13]|=bit;
}
/*
 * Production route: CheckFrameTargets -> TryFrameAutomaticParry ->
 * TryDiagnosticParry -> DiagnosticPressCurrent -> DispatchDefense/native queue.
 *
 * Geometry and contact are not evaluated here; CD_DIAG_TOOL and CD_DIAG_CONTACT
 * are intentionally absent from r[17] (evaluated-gate mask) to signal that
 * geometry is unevaluated on this path.  Classification and temporal scheduling
 * are the sole decision criteria.
 */
static enum CD_PARRY_FRAME_DECISION TryDiagnosticParry(unsigned slot,char*me,char*target,unsigned now)
{
    if(slot>MAX_REMOTE_PLAYER||!me||!target)return CD_FRAME_SKIP;
    ObserveDiagnosticEpisode(slot,me,target,now);
    DiagnosticParryEpisode*e=&DiagnosticEpisodes[slot];
    if(!e->active)return CD_FRAME_SKIP;
    uint64_t r[TRACE_WORDS]={0};
    r[1]=TRACE_DIAGNOSTIC_GATE;r[2]=now;r[3]=(uintptr_t)me;r[4]=(uintptr_t)target;
    r[5]=e->animation;r[6]=(uint64_t)(int64_t)GetActorAnim(me);
    int mode=0,raw=0;
    int toolReadable=ReadDiagnosticTool(me,&mode,&raw);
    int tool=raw/100*100;
    r[7]=tool;r[8]=mode;r[9]=e->mode;r[10]=e->started;r[11]=now-e->started;
    /* r[16]=3: no prior solver decision on this path */
    r[14]=PARRY_UNSTABLE;r[16]=3;r[18]=ReadAutoRollState(me);
    r[19]=Defense.phase;r[20]=DEF_PARRY;r[21]=Defense.requests;r[22]=Defense.confirmations;
    r[23]=e->started;r[25]=mode==3?ActionRightParry:ActionLeftParry;
    r[27]=e->weapon;r[28]=(uint32_t)e->network;
    int enabled=CopydogEnabled&&!exitThread&&GetAsyncKeyState(VK_SHIFT)<0&&
        atomic_load(&CopydogDiagnosticParryEnabled);
    DiagnosticGate(r,CD_DIAG_ENABLED,enabled);
    DiagnosticGate(r,CD_DIAG_ATTACK,e->readable&&DiagnosticEligible(e->weapon,e->animation));
    DiagnosticGate(r,CD_DIAG_NATIVE,e->clock.valid&&now-e->clock.tick<=CD_PRED_MAX_AGE_MS);
    DiagnosticGate(r,CD_DIAG_SELF,r[18]==ROLL_SELF_OK);
    DiagnosticGate(r,CD_DIAG_LOCAL,!LocalActionPending(me,now)&&!CopyAction&&GetAsyncKeyState(VK_SPACE)>=0);
    DiagnosticGate(r,CD_DIAG_TRANSACTION,!CopydogDefenseBusy(&Defense)&&!CombatFrameClaimed);
    DiagnosticGate(r,CD_DIAG_ONCE,!e->spent);
    float a[3],b[3];
    if(ReadVec(GetActorPos(me),a)&&ReadVec(GetActorPos(target),b)){
        double dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2];
        double distance=sqrt(dx*dx+dy*dy+dz*dz);
        if(isfinite(distance)&&distance<=INT_MAX/1000.){
            r[26]=(uint64_t)lround(distance*1000.);
            DiagnosticGate(r,CD_DIAG_RANGE,distance<Config.ParryDistance);
        }
    }
    /* CD_DIAG_TOOL and CD_DIAG_CONTACT are not gated; geometry is unevaluated. */
    unsigned basis;
    FramePlan temporal=DiagnosticTimingPlan(e,tool,mode,now,&basis);
    atomic_fetch_add(&CopydogDynamicParryCounters[DP_PLAN],1);
    if(temporal.reason==PARRY_EARLY)atomic_fetch_add(&CopydogDynamicParryCounters[DP_EARLY],1);
    if(temporal.decision==CD_FRAME_SKIP)atomic_fetch_add(&CopydogDynamicParryCounters[DP_LATE],1);
    r[14]=temporal.reason;
    DiagnosticGate(r,CD_DIAG_TIMING,temporal.decision==CD_FRAME_PRESS);
    r[29]=basis|(unsigned)(temporal.decision+1);
    r[30]=(uint32_t)(int32_t)llround(temporal.lo*1000.)|
        ((uint64_t)(uint32_t)(int32_t)llround(temporal.hi*1000.)<<32);
    int native=0,hit=DiagnosticSelfHit(me,now,&native);
    r[24]=(uint32_t)native;
    DiagnosticGate(r,CD_DIAG_NOT_HIT,hit==0);
    FramePress press={.slot=slot,.tick=now,.cueOnset=e->started,.diagnostic=1,
        .diagnosticMode=mode,.diagnosticRaw=raw};
    press.frame=e->clock.last;press.frame.animation=e->animation;
    memcpy(press.trace,r,sizeof r);
    int current=enabled&&toolReadable&&temporal.decision==CD_FRAME_PRESS&&DiagnosticPressCurrent(&press,me,target,now);
    DiagnosticGate(r,CD_DIAG_CURRENT,current);
    CopydogTraceWrite(&CopydogAutoTrace,r);
    int outcome=!enabled?CD_DIAG_DISABLED:hit==1?CD_DIAG_HIT:hit==2?CD_DIAG_UNREADABLE:
        e->spent?CD_DIAG_DUPLICATE:!DiagnosticEligible(e->weapon,e->animation)?CD_DIAG_NOT_PARRYABLE:
        temporal.reason==PARRY_UNSTABLE?CD_DIAG_TIMING_UNKNOWN:
        temporal.decision==CD_FRAME_WAIT?CD_DIAG_TIMING_WAIT:
        temporal.decision==CD_FRAME_SKIP?CD_DIAG_TIMING_MISSED:!current?CD_DIAG_UNREADABLE:0;
    if(!outcome){
        unsigned attempted=0;
        press.diagnosticAttempted=&attempted;
        press.trace[1]=TRACE_PLAN;press.trace[2]=now;press.trace[15]|=CD_DIAG_FORCED_SOURCE;
        memcpy(PendingDefenseTrace,press.trace,sizeof press.trace);
        int accepted=DispatchDefense((int)r[25],DEF_PARRY,target,e->animation,tool,now,NULL,&press,NULL);
        if(attempted)DiagnosticGate(r,CD_DIAG_QUEUE,accepted);
        int finalHit=accepted?0:DiagnosticSelfHit(me,timeGetTime(),NULL);
        outcome=accepted?CD_DIAG_SUBMITTED:attempted?CD_DIAG_QUEUE_FAILED:
            finalHit==1?CD_DIAG_HIT:CD_DIAG_UNREADABLE;
        if(accepted){
            atomic_fetch_add(&ParryRequests,1);
            atomic_fetch_add(&CopydogDynamicParryCounters[DP_SUBMIT],1);
        }
    }
    r[1]=TRACE_DIAGNOSTIC_FORCED;r[15]|=CD_DIAG_FORCED_SOURCE;r[16]=outcome;
    r[21]=Defense.requests;r[22]=Defense.confirmations;
    CopydogTraceWrite(&CopydogAutoTrace,r);
    return outcome==CD_DIAG_SUBMITTED?CD_FRAME_PRESS:
        (outcome==CD_DIAG_DISABLED||outcome==CD_DIAG_NOT_PARRYABLE||
         outcome==CD_DIAG_TIMING_MISSED||outcome==CD_DIAG_DUPLICATE)?CD_FRAME_SKIP:CD_FRAME_WAIT;
}
