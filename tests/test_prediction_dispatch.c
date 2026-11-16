/* Historical solver comparison only: explicit offline legacy entry points.
   Current production scheduling is covered by test_sc23_parry/features.c. */
#define CROSSBOW_FIXTURE_ENTRY UnusedCrossbowFixture
#include "test_crossbow_roll.c"
typedef struct { int bone;float a[3],b[3],radius; } CD_PRED_BODY;
typedef struct { int bone;float p[3]; } PredPoint;
typedef struct { float begin,end,radius;PredPoint a,b; } CD_PRED_SHAPE;
typedef struct { int weapon,animation,mode,hand;unsigned first,count; } CD_PRED_PROFILE;
#include "../src/prediction_data.h"
typedef struct {
    char owner[0x80],clock[0x100],skeleton[0x60],bones[110*16],ownEquipment[0x400];
    float transforms[110][12];
} PredFixture;
static void predClock(PredFixture*f,int animation,float phase)
{
    integer(f->clock+0x20,animation);real(f->clock+0x24,phase-.01f);real(f->clock+0x28,phase);
    real(f->clock+0x2c,2);integer(f->clock+0xc0,1);integer(f->clock+0xc4,0);
}
static void predSetup(PredFixture*f,char*actor,char*mods,char*behavior,int animation)
{
    pointer(mods+0x10,f->clock);pointer(behavior+0x28,f->owner);
    pointer(f->owner+0x38,f->skeleton);pointer(f->owner+0x40,f->transforms);
    integer(f->owner+0x48,110);integer(f->owner+0x4c,110);pointer(f->owner+0x50,f->transforms);
    integer(f->owner+0x58,110);integer(f->owner+0x5c,110);pointer(f->skeleton+0x28,f->bones);
    integer(f->skeleton+0x30,110);
    for(unsigned i=0;i<110;i++){
        pointer(f->bones+i*16,(void*)CdPredBoneNames[i]);f->transforms[i][7]=1;
        f->transforms[i][8]=f->transforms[i][9]=f->transforms[i][10]=1;
    }
    if(!readlpt(char*,actor+XB)){
        pointer(actor+XB,f->ownEquipment);integer(f->ownEquipment+0x2b8,1);
    }
    predClock(f,animation,.3f);
}
static void publish(PredFixture*f,unsigned serial,int animation,float previous,float current)
{
    unsigned head=serial%CD_PRED_CLOCK_COUNT;
    char*entry=f->clock+0x20+16*head;
    integer(entry,animation);real(entry+4,previous);real(entry+8,current);real(entry+12,2);
    integer(f->clock+0xc4,(int)head);integer(f->clock+0xc0,(int)((head+1)%CD_PRED_CLOCK_COUNT));
}

typedef struct {
    char actor[0x2100],modules[0x100],behavior[0xa00],animation[0xd0],physics[0x90];
    PredFixture pose;
    ParryFrameTarget target;
} BenchEnemy;

enum { BENCH_SAMPLES=1000, BENCH_WARMUP=32 };
static int compareTicks(const void*a,const void*b)
{
    LONGLONG left=*(const LONGLONG*)a,right=*(const LONGLONG*)b;
    return (left>right)-(left<right);
}
static void printTiming(const char*label,LONGLONG*data,double usPerTick)
{
    qsort(data,BENCH_SAMPLES,sizeof *data,compareTicks);
    printf(" %s=%.2f/%.2f/%.2f",label,data[BENCH_SAMPLES*50/100-1]*usPerTick,
        data[BENCH_SAMPLES*95/100-1]*usPerTick,data[BENCH_SAMPLES*99/100-1]*usPerTick);
}
static int benchmarkParry(SCENE*s,PredFixture*own,const CD_PRED_SHAPE*shape,
    const ParryActivation*activation,size_t row,int clip,int turning,int separated)
{
    static BenchEnemy enemies[5];
    LONGLONG poseTicks[BENCH_SAMPLES],planTicks[BENCH_SAMPLES],totalTicks[BENCH_SAMPLES];
    LARGE_INTEGER frequency;
    CHECK(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0);
    for(unsigned i=0;i<_countof(enemies);i++){
        BenchEnemy*e=&enemies[i];
        integer(e->actor+0x78,MyId+1+i);integer(e->actor+8,MyId+1+i);
        pointer(e->actor+XA,e->modules);pointer(e->modules+0x28,e->behavior);
        pointer(e->modules+0x68,e->physics);pointer(e->modules+0x80,e->animation);
        integer(e->animation+0xc8,clip);
        predSetup(&e->pose,e->actor,e->modules,e->behavior,clip);
        integer(e->pose.ownEquipment+0x2b8,3);integer(e->pose.ownEquipment+0x330,6080000);
        for(unsigned k=0;k<3;k++)real(e->physics+0x80+k*4,CdPredBodies[0].a[k]-shape->a.p[k]);
        e->target=(ParryFrameTarget){.self=(uintptr_t)s->me,.actor=(uintptr_t)e->actor,
            .weapon=6080000,.handMode=3};
        CopydogPredictionEpisode(i+1,(uintptr_t)e->actor,fakeNow);
    }
    puts("BENCH offline synthetic, distinct actors/poses/tracks, 110 bones each, warmed skeleton names; QPC us P50/P95/P99");
    puts("BENCH observe + cached plan only; fixture publication, clock preparation and input excluded");
    printf("BENCH path=%s scene=%s\n",turning?"turning":"linear",separated?"clear_all_pairs":"contact_at_band_start");
    for(unsigned n=1;n<=_countof(enemies);n+=2){
        unsigned geometricPlans=0;
        for(unsigned i=0;i<BENCH_WARMUP+BENCH_SAMPLES;i++){
            LARGE_INTEGER start={0},sampled,finished;
            /* Make a genuine 12 ms pair each run. Only its second capture is timed. */
            fakeNow+=32;
            for(unsigned k=0;k<2;k++){
                unsigned ms=(separated?612+i%6:466+4*(i%9))+12*k,stamp=fakeNow-12+12*k;
                publish(own,2*i+k,0,(ms-12)/1000.f,ms/1000.f);
                for(unsigned j=0;j<n;j++){
                    BenchEnemy*e=&enemies[j];CD_NATIVE_FRAME f;
                    /* Small distinct velocities keep all current attack shapes in contact. */
                    float x=CdPredBodies[0].a[0]-shape->a.p[0]+((float)ms-500)*.0001f*(j+1);
                    if(separated)x+=3.f;
                    real(e->physics+0x80,x);
                    real(e->physics+0x74,turning?((float)ms-500)*.001f*(j+1):0.f);
                    publish(&e->pose,2*i+k,clip,(ms-12)/1000.f,ms/1000.f);
                    if(!k)e->target.clock=(CD_FRAME_CLOCK){0};
                    if(!CopydogReadNativeFrame((uintptr_t)e->actor,&f)||
                       !CopydogAdvanceFrame(&e->target.clock,&f,stamp)){
                        fprintf(stderr,"BENCH failed to prepare actor %u sample %u\n",j+1,i);
                        return 1;
                    }
                }
                if(k)QueryPerformanceCounter(&start);
                CopydogPredictionObserveFrame(0,(uintptr_t)s->me,stamp,1);
                for(unsigned j=0;j<n;j++)
                    CopydogPredictionObserveFrame(j+1,(uintptr_t)enemies[j].actor,stamp,1);
            }
            QueryPerformanceCounter(&sampled);
            for(unsigned j=0;j<n;j++){
                FramePlan p=PlanParryFrame(j+1,&enemies[j].target,activation,row,fakeNow,12.);
                if(p.source&(separated?4096u:256u))++geometricPlans;
            }
            QueryPerformanceCounter(&finished);
            if(i>=BENCH_WARMUP){
                unsigned ix=i-BENCH_WARMUP;
                poseTicks[ix]=sampled.QuadPart-start.QuadPart;
                planTicks[ix]=finished.QuadPart-sampled.QuadPart;
                totalTicks[ix]=finished.QuadPart-start.QuadPart;
            }
        }
        CHECK(geometricPlans==n*(BENCH_SAMPLES+BENCH_WARMUP));
        printf("BENCH attackers=%u observed_actors=%u samples=%d",n,n+1,BENCH_SAMPLES);
        double usPerTick=1000000./frequency.QuadPart;
        printTiming("observe_us",poseTicks,usPerTick);
        printTiming("cached_plan_us",planTicks,usPerTick);
        printTiming("combined_us",totalTicks,usPerTick);
        putchar('\n');
    }
    return 0;
}
static int checkTwoContacts(SCENE*s,PredFixture*own,PredFixture*first,const CD_PRED_SHAPE*shape,int clip)
{
    BenchEnemy second={0};
    memcpy(second.actor,s->you,sizeof s->you);memcpy(second.modules,s->ymods,sizeof s->ymods);
    memcpy(second.behavior,s->theirNames,sizeof s->theirNames);
    memcpy(second.physics,s->ypos,sizeof s->ypos);memcpy(second.animation,s->theirAnim,sizeof s->theirAnim);
    pointer(second.actor+XA,second.modules);pointer(second.modules+0x28,second.behavior);
    pointer(second.modules+0x68,second.physics);pointer(second.modules+0x80,second.animation);
    integer(second.actor+8,MyId+2);integer(second.actor+0x78,MyId+2);
    pointer(s->slots+2*0x38,second.actor);
    predSetup(&second.pose,second.actor,second.modules,second.behavior,clip);
    for(unsigned k=0;k<3;k++){
        float pos=CdPredBodies[0].a[k]-shape->a.p[k];
        real(s->ypos+0x80+k*4,pos);real(second.physics+0x80+k*4,pos);
    }
    real(s->ypos+0x74,0);real(second.physics+0x74,0);
    /* The second body is farther away, but its independently observed blade
       reaches the same defender. Its attack phase is twelve ms further in. */
    float x=readlpt(float,s->ypos+0x80),offset=x>=0.f?1.f:-1.f;
    real(second.physics+0x80,x+offset);
    second.pose.transforms[shape->a.bone][0]=-offset;
    second.pose.transforms[shape->b.bone][0]=-offset;
    char*actors[]={s->you,second.actor};
    PredFixture*poses[]={first,&second.pose};
    ParryFrameTarget oracle[2]={0};
    size_t row=FirstDynamicWindow(CdDynamicTargets,_countof(CdDynamicTargets),6080000,clip);
    CopydogPredictionResetAll();GameCadence=(CD_GAME_CADENCE){0};DynamicSelf=(DynamicParrySelf){0};
    memset(DynamicTargets,0,sizeof DynamicTargets);memset(ParryTrack,0,sizeof ParryTrack);
    Defense=(COPYDOG_DEFENSE){0};CombatFrameClaimed=0;fakeNow+=1000;
    ObserveDynamicParrySelf(s->me,fakeNow);
    int before=calls;
    for(unsigned step=0;step<2;step++){
        unsigned phase=424+12*step;fakeNow+=12;
        publish(own,step,0,(phase-12)/1000.f,phase/1000.f);
        CopydogPredictionObserve(0,(uintptr_t)s->me,fakeNow);
        for(unsigned j=0;j<2;j++){
            unsigned ms=phase+12*j;
            publish(poses[j],step,clip,(ms-12)/1000.f,ms/1000.f);
            CopydogPredictionObserve(j+1,(uintptr_t)actors[j],fakeNow);
            CD_NATIVE_FRAME f;CHECK(CopydogReadNativeFrame((uintptr_t)actors[j],&f));
            oracle[j].actor=(uintptr_t)actors[j];oracle[j].self=(uintptr_t)s->me;
            oracle[j].weapon=6080000;oracle[j].handMode=3;
            CHECK(CopydogAdvanceFrame(&oracle[j].clock,&f,fakeNow));
        }
        if(!step){CheckLegacyFrameTargetsForOffline(s->me,NULL,fakeNow);CHECK(calls==before);}
    }
    ParryActivation activation;CHECK(FrameSelfActivation(11010000,1,12,&activation));
    FramePlan plans[2];
    for(unsigned j=0;j<2;j++){
        plans[j]=PlanParryFrame(j+1,&oracle[j],&activation,row,fakeNow,12);
        CHECK(plans[j].decision==CD_FRAME_PRESS&&CopydogParryContactAuthorizes(plans[j].source));
    }
    CHECK(plans[1].hi<plans[0].hi);
    CheckLegacyFrameTargetsForOffline(s->me,NULL,fakeNow);
    CHECK(calls==before+1&&Defense.target==(uintptr_t)second.actor&&lastAction==ActionLeftParry);
    CHECK(!DynamicTargets[1].submitted&&DynamicTargets[2].submitted&&
          CopydogParryContactAuthorizes((unsigned)ActiveDefenseTrace[15]));
    pointer(s->slots+2*0x38,NULL);CopydogPredictionResetAll();return 0;
}

static int checkGrazingUncertainty(SCENE*s,PredFixture*own,PredFixture*enemy,
    const CD_PRED_SHAPE*shape,int clip)
{
    const int weapon=6080000;
    const CD_PRED_BODY*body=&CdPredBodies[0];
    size_t row=FirstDynamicWindow(CdDynamicTargets,_countof(CdDynamicTargets),weapon,clip);
    CHECK(row<_countof(CdDynamicTargets));
    const CD_DYNAMIC_WINDOW*window=&CdDynamicTargets[row];
    CHECK(window->weapon==weapon&&window->animation==clip);
    double crossing=window->begin+.3*(window->end-window->begin);
    float tangentY=(float)((double)body->a[1]-shape->a.p[1]+body->radius+shape->radius);
    float previousX[_countof(own->transforms)];
    for(unsigned i=0;i<_countof(own->transforms);i++)previousX[i]=own->transforms[i][0];
    /* Isolate the one grazing limb. Other body shapes would produce a separate contact. */
    for(unsigned i=1;i<_countof(CdPredBodies);i++)
        if(CdPredBodies[i].bone!=body->bone)own->transforms[CdPredBodies[i].bone][0]=40.f;
    CopydogPredictionResetAll();real(s->ypos+0x74,0.f);fakeNow+=100;
    ParryFrameTarget target={.self=(uintptr_t)s->me,.actor=(uintptr_t)s->you,.weapon=weapon,.handMode=3};
    CD_NATIVE_FRAME frame;
    for(unsigned i=0;i<2;i++){
        unsigned phase=436+12*i;fakeNow+=12;
        real(s->ypos+0x80,(float)((double)body->a[0]-shape->a.p[0]+(phase-crossing)*.0625));
        real(s->ypos+0x84,tangentY);
        real(s->ypos+0x88,(float)((double)body->a[2]-shape->a.p[2]));
        publish(own,i,0,(phase-12)/1000.f,phase/1000.f);
        publish(enemy,i,clip,(phase-12)/1000.f,phase/1000.f);
        CopydogPredictionObserve(0,target.self,fakeNow);CopydogPredictionObserve(1,target.actor,fakeNow);
        CHECK(CopydogReadNativeFrame(target.actor,&frame));
        CHECK(CopydogAdvanceFrame(&target.clock,&frame,fakeNow));
    }
    unsigned budget=CD_PRED_WORK_BUDGET;
    const ParryActivation activation={.open=157.,.close=267.};
    CD_PRED_PARRY_BAND band={&frame,weapon,3,target.clock.rate,window->begin,window->end,
        activation.close+12.,&budget,target.clock.tick};
    CD_PRED_PARRY_RESULT result;
    CHECK(CopydogPredictionParryContact(1,target.actor,target.self,fakeNow,&band,&result)==CD_PRED_POSSIBLE);
    FramePlan plan=PlanParryFrame(1,&target,&activation,row,fakeNow,12.);
    CHECK((plan.source&CD_PARRY_POSSIBLE_CONTACT)&&!CopydogParryContactAuthorizes(plan.source));
    CHECK(CopydogParryFrameChoiceBounds(plan.lo,plan.hi,12.,1)==CD_FRAME_PRESS);
    CHECK(plan.decision==CD_FRAME_WAIT&&plan.reason==PARRY_CONTACT_UNCONFIRMED);
    for(unsigned i=0;i<_countof(own->transforms);i++)own->transforms[i][0]=previousX[i];
    CopydogPredictionResetAll();return 0;
}

typedef struct {
    const wchar_t*name;
    int animation;
    float progress;
    enum { LOCAL_AT_START,LOCAL_AFTER_PLAN,LOCAL_REQUEST_ONLY } stage;
    int allowed;
} LocalAction;

static void setLocalAction(SCENE*s,const LocalAction*action)
{
    memcpy(s->ownNames+0x898,action->name,(wcslen(action->name)+1)*sizeof *action->name);
    integer(s->ownAnim+0xc8,action->animation);
    real(s->state+0x224,action->progress);
    s->state[0x228]=!wcscmp(action->name,L"Move")&&action->progress==0.f;
    /* The inherited fixture uses a switch for these two queries. Supply its actual
       production result at each transition, while time and the actor stay fixed. */
    ready=DataReadyToRoll()&&DataReadyToAttack();
}

static int checkLocalActionGate(SCENE*s,PredFixture*own,PredFixture*enemy,
    const CD_PRED_SHAPE*shape,int clip)
{
    const LocalAction idle={L"Idle",0,1.f,LOCAL_AT_START,1};
    const LocalAction actions[]={
        {L"Idle",0,1.f,LOCAL_AT_START,1},{L"Move",300020100,1.f,LOCAL_AT_START,1},
        {L"Move",300020100,0.f,LOCAL_AT_START,1},{L"Move",300020200,0.f,LOCAL_AT_START,1},
        {L"Idle",0,1.f,LOCAL_REQUEST_ONLY,1},
        {L"ThrowAtk",0,1.f,LOCAL_AT_START,0},{L"AttackRightLight1",23030000,1.f,LOCAL_AT_START,0},
        {L"ThrowAtk",0,1.f,LOCAL_AFTER_PLAN,0},{L"RollingLight",27105,.2f,LOCAL_AFTER_PLAN,0},
        {L"AttackRightLight1",23030000,1.f,LOCAL_AFTER_PLAN,0},{L"Drink",0,1.f,LOCAL_AFTER_PLAN,0}
    };
    const int weapon=6080000;
    const size_t row=FirstDynamicWindow(CdDynamicTargets,_countof(CdDynamicTargets),weapon,clip);
    CHECK(row<_countof(CdDynamicTargets));
    for(unsigned axis=0;axis<3;axis++)
        real(s->ypos+0x80+axis*4,CdPredBodies[0].a[axis]-shape->a.p[axis]);
    real(s->ypos+0x74,0);
    for(unsigned n=0;n<_countof(actions);n++){
        const LocalAction*action=&actions[n];
        const LocalAction*initial=action->stage==LOCAL_AT_START?action:&idle;
        int before=calls;
        fakeNow+=1000;CopydogPredictionResetAll();
        Defense=(COPYDOG_DEFENSE){0};CombatFrameClaimed=0;GameCadence=(CD_GAME_CADENCE){0};
        DynamicSelf=(DynamicParrySelf){0};DynamicTargets[1]=(ParryFrameTarget){0};
        ParryTrack[1]=(COPYDOG_PARRY_TRACK){0};EarlyTrack[1]=(COPYDOG_EARLY_STATE){0};
        setLocalAction(s,initial);
        CHECK(ready||(!wcscmp(initial->name,L"Move")&&initial->progress==0.f));
        ObserveDynamicParrySelf(s->me,fakeNow);
        ParryFrameTarget target={.actor=(uintptr_t)s->you,.self=(uintptr_t)s->me,.weapon=weapon,.handMode=3};
        FramePress press={0};
        enum CD_PARRY_FRAME_DECISION decision=CD_FRAME_SKIP;
        for(unsigned tick=0;tick<2;tick++){
            unsigned phase=424+12*tick;fakeNow+=12;
            publish(own,tick,initial->animation,(phase-12)/1000.f,phase/1000.f);
            publish(enemy,tick,clip,(phase-12)/1000.f,phase/1000.f);
            CopydogPredictionObserve(0,target.self,fakeNow);
            CopydogPredictionObserve(1,target.actor,fakeNow);
            CD_NATIVE_FRAME local,remote;
            CHECK(CopydogReadNativeFrame(target.self,&local)&&local.animation==initial->animation);
            CHECK(CopydogReadNativeFrame(target.actor,&remote)&&remote.animation==clip);
            CHECK(CopydogAdvanceFrame(&target.clock,&remote,fakeNow));
            decision=EvaluateParryFrame(1,s->me,s->you,fakeNow,&press);
            if(!tick)CHECK(decision!=CD_FRAME_PRESS);
        }
        ParryActivation activation;CHECK(FrameSelfActivation(11010000,1,12.,&activation));
        FramePlan timing=PlanParryFrame(1,&target,&activation,row,fakeNow,12.);
        CHECK(timing.decision==CD_FRAME_PRESS&&CopydogParryContactAuthorizes(timing.source));
        if(action->stage==LOCAL_AT_START&&!action->allowed){
            CHECK(decision!=CD_FRAME_PRESS&&calls==before&&!DynamicTargets[1].submitted);
            continue;
        }
        CHECK(decision==CD_FRAME_PRESS&&CopydogParryContactAuthorizes((unsigned)press.trace[15]));
        if(action->stage==LOCAL_REQUEST_ONLY){
            /* Only the simulated player's action is forwarded in this case. The
               input callback is a recorder and does not implement an engine queue. */
            CD_NATIVE_FRAME prior,latest;char name[RC_NAME_CAP];
            CHECK(GetActionAddress(s->me)==s->queue&&CopydogReadNativeFrame(target.self,&prior));
            intptr_t input[3]={ActionRightAttack1,0,0};
            OnAction(s->queue,(int*)input);
            CHECK(calls==before+1&&lastAction==ActionRightAttack1);
            CHECK(RollCatchName(s->me,name)&&!strcmp(name,"idle")&&GetActorAnim(s->me)==0);
            CHECK(CopydogReadNativeFrame(target.self,&latest)&&CopydogSameFrame(&prior,&latest));
            CHECK(!FramePressCurrent(&press,fakeNow)&&!DispatchParryFrame(&press));
            CHECK(!Defense.requests&&!DynamicTargets[1].submitted&&calls==before+1);
            continue;
        }
        /* A local transition can precede publication of another pose or remote tick. */
        if(action->stage==LOCAL_AFTER_PLAN)setLocalAction(s,action);
        CHECK(ready||(!wcscmp(action->name,L"Move")&&action->progress==0.f));
        CHECK(FramePressCurrent(&press,fakeNow)==action->allowed);
        CHECK(DispatchParryFrame(&press)==action->allowed);
        CHECK(calls==before+action->allowed&&DynamicTargets[1].submitted==action->allowed);
        if(action->allowed)CHECK(lastAction==ActionLeftParry);
    }
    setLocalAction(s,&idle);
    CopydogPredictionResetAll();Defense=(COPYDOG_DEFENSE){0};CombatFrameClaimed=0;return 0;
}

static int exerciseParry(SCENE*s,PredFixture*own,PredFixture*enemy)
{
    void*block=VirtualAlloc((void*)0x144750000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(block==(void*)0x144750000);
    char root[0xc0]={0};pointer(addr2,root);pointer(root+16,own->ownEquipment);
    integer(own->ownEquipment+0x2b8,1);integer(own->ownEquipment+0x32c,11010000);
    integer(s->equipment+0x2b8,3);integer(s->equipment+0x330,6080000);
    const int clip=195034000;
    integer(s->theirAnim+0xc8,clip);
    const CD_PRED_PROFILE*p=NULL;
    for(unsigned i=0;i<_countof(CdPredProfiles);i++)
        if(CdPredProfiles[i].weapon==6080000&&CdPredProfiles[i].animation==clip&&
           CdPredProfiles[i].mode==3&&CdPredProfiles[i].hand==1)p=&CdPredProfiles[i];
    CHECK(p);const CD_PRED_SHAPE*shape=&CdPredShapes[p->first];
    for(unsigned i=0;i<3;i++)real(s->ypos+0x80+i*4,CdPredBodies[0].a[i]-shape->a.p[i]);
    Config.KeyCondition=0;Config.ParryDistance=8;atomic_store(&AutoRollEnabled,0);
    if(checkLocalActionGate(s,own,enemy,shape,clip))return 1;
    for(unsigned run=0;run<6;run++){
        const unsigned dt[]={33,12,25,12,17,7};
        fakeNow+=1000;
        Defense=(COPYDOG_DEFENSE){0};CombatFrameClaimed=0;
        DynamicSelf=(DynamicParrySelf){0};DynamicTargets[1]=(ParryFrameTarget){0};GameCadence=(CD_GAME_CADENCE){0};
        if(run==3)CopydogObserveCadence(&GameCadence,(uintptr_t)root,150,fakeNow);
        ParryTrack[1]=(COPYDOG_PARRY_TRACK){0};EarlyTrack[1]=(COPYDOG_EARLY_STATE){0};
        CopydogPredictionEpisode(1,(uintptr_t)s->you,fakeNow);
        ObserveDynamicParrySelf(s->me,fakeNow);
        uint64_t hits=atomic_load(&CopydogDynamicParryCounters[DP_GEOMETRY]);
        int before=calls;unsigned phase=150,contactWhileWaiting=0,deniedInWindow=0;
        for(unsigned i=0;phase<670;i++){
            unsigned delta=run==0?33:run==1?12:dt[i%_countof(dt)],prev=phase;
            phase+=delta;fakeNow+=delta;
            if(run==4)real(s->ypos+0x74,(float)phase*.0006f);
            publish(own,i,0,prev/1000.f,phase/1000.f);
            publish(enemy,i,clip,prev/1000.f,phase/1000.f);
            if(run==3){
                integer(root+CD_GAME_PLAY_MS_OFFSET,(int)phase);
                CopydogObserveCadence(&GameCadence,(uintptr_t)root,phase,fakeNow);
                integer(own->clock+0xc4,(int)((i+1)%CD_PRED_CLOCK_COUNT));
                integer(enemy->clock+0xc4,(int)((i+1)%CD_PRED_CLOCK_COUNT));
            }
            if(run!=5||!contactWhileWaiting){
                CopydogPredictionObserveFrame(0,(uintptr_t)s->me,fakeNow,run==3);
                CopydogPredictionObserveFrame(1,(uintptr_t)s->you,fakeNow,run==3);
            }
            enum CD_PARRY_FRAME_DECISION d=TryLegacyFrameAutomaticParryForOffline(1,s->me,s->you,fakeNow);
            if(d==CD_FRAME_PRESS){
                CHECK(phase<634&&phase+267>=633);
                CHECK(CopydogParryContactAuthorizes((unsigned)ActiveDefenseTrace[15]));
            }
            if(run==5&&d==CD_FRAME_WAIT&&!contactWhileWaiting&&
               CopydogParryContactAuthorizes((unsigned)PendingDefenseTrace[15])){
                contactWhileWaiting=1;
                CopydogPredictionResetAll();
            }else if(run==5&&contactWhileWaiting&&PendingDefenseTrace[14]==PARRY_CONTACT_UNCONFIRMED&&
                     (PendingDefenseTrace[15]&CD_PARRY_UNKNOWN_GEOMETRY))deniedInWindow++;
            (void)TryLegacyFrameAutomaticParryForOffline(1,s->me,s->you,fakeNow);
        }
        if(run==5){
            CHECK(contactWhileWaiting&&deniedInWindow&&calls==before&&!DynamicTargets[1].submitted);
            continue;
        }
        CHECK(calls==before+1&&lastAction==ActionLeftParry&&DynamicTargets[1].submitted);
        if(run==4)CHECK(ActiveDefenseTrace[15]&65536u);
        CHECK(atomic_load(&CopydogDynamicParryCounters[DP_GEOMETRY])>hits);
    }
    if(checkTwoContacts(s,own,enemy,shape,clip)||checkGrazingUncertainty(s,own,enemy,shape,clip))return 1;
    GameCadence=(CD_GAME_CADENCE){0};
    /* A short fully observed clear window can veto, an extrapolated clear
       window hundreds of milliseconds away remains open to later movement. */
    for(unsigned i=0;i<3;i++)real(s->ypos+0x80+i*4,i==0?4.f:0.f);
    const ParryActivation ownWindow={.open=133.,.close=267.};
    size_t row=FirstDynamicWindow(CdDynamicTargets,_countof(CdDynamicTargets),6080000,clip);
    ParryFrameTarget t={.self=(uintptr_t)s->me,.actor=(uintptr_t)s->you,.weapon=6080000,.handMode=3};
    fakeNow+=1000;
    const unsigned phases[]={380,392,584,596,620,632};
    for(unsigned i=0;i<_countof(phases);i++){
        unsigned phase=phases[i];fakeNow+=i?phase-phases[i-1]:12;
        publish(own,i,0,(phase-12)/1000.f,phase/1000.f);publish(enemy,i,clip,(phase-12)/1000.f,phase/1000.f);
        CopydogPredictionObserve(0,(uintptr_t)s->me,fakeNow);CopydogPredictionObserve(1,(uintptr_t)s->you,fakeNow);
        CD_NATIVE_FRAME frame;CHECK(CopydogReadNativeFrame((uintptr_t)s->you,&frame));
        CopydogAdvanceFrame(&t.clock,&frame,fakeNow);
        if(i==1){FramePlan plan=PlanParryFrame(1,&t,&ownWindow,row,fakeNow,t.clock.step);
            CHECK(!(plan.source&CD_PARRY_COMPLETE_CLEAR)&&plan.decision!=CD_FRAME_PRESS);}
        if(i==3){
            const ParryActivation shortWindow={.open=30.,.close=90.};
            FramePlan plan=PlanParryFrame(1,&t,&shortWindow,row,fakeNow,t.clock.step);
            CHECK((plan.source&CD_PARRY_PARTIAL_CLEAR)&&plan.reason==PARRY_CONTACT_UNCONFIRMED);
            CHECK(CopydogParryFrameChoiceBounds(plan.lo,plan.hi,t.clock.step,1)==CD_FRAME_PRESS);
            CHECK(plan.decision!=CD_FRAME_PRESS);
        }
        if(i==5){FramePlan plan=PlanParryFrame(1,&t,&ownWindow,row,fakeNow,t.clock.step);
            CHECK((plan.source&CD_PARRY_COMPLETE_CLEAR)&&plan.decision==CD_FRAME_SKIP&&plan.reason==PARRY_NO_CONTACT);}
    }
    /* A geometric contact band clipped at the resource's open endpoint
       can fit an equally wide tool interval at this instant only. */
    CopydogPredictionResetAll();t.clock=(CD_FRAME_CLOCK){0};
    real(s->ypos+0x74,0.f);
    for(unsigned i=0;i<3;i++)real(s->ypos+0x80+i*4,CdPredBodies[0].a[i]-shape->a.p[i]);
    for(unsigned i=0;i<2;i++){
        fakeNow+=12;unsigned ms=600+12*i;
        publish(own,i,0,(ms-12)/1000.f,ms/1000.f);publish(enemy,i,clip,(ms-12)/1000.f,ms/1000.f);
        CopydogPredictionObserve(0,(uintptr_t)s->me,fakeNow);CopydogPredictionObserve(1,(uintptr_t)s->you,fakeNow);
        CD_NATIVE_FRAME f;CHECK(CopydogReadNativeFrame((uintptr_t)s->you,&f));
        CHECK(CopydogAdvanceFrame(&t.clock,&f,fakeNow));
    }
    double current=1000.*t.clock.last.current;
    ParryActivation exact={.open=(CdDynamicTargets[row].begin-current)/t.clock.rate,
        .close=(CdDynamicTargets[row].end-current)/t.clock.rate};
    FramePlan clipped=PlanParryFrame(1,&t,&exact,row,fakeNow,80.);
    CHECK((clipped.source&256u)&&clipped.lo==0.&&clipped.hi==0.&&clipped.decision==CD_FRAME_PRESS);
    const char*bench=getenv("COPYDOG_BENCH");
    if(bench&&strcmp(bench,"1")==0){
        if(benchmarkParry(s,own,shape,&ownWindow,row,clip,0,0)||
           benchmarkParry(s,own,shape,&ownWindow,row,clip,1,0)||
           benchmarkParry(s,own,shape,&ownWindow,row,clip,1,1))return 1;
    }
    VirtualFree(block,0,MEM_RELEASE);return 0;
}
#ifndef PREDICTION_DISPATCH_FIXTURE_ENTRY
#define PREDICTION_DISPATCH_FIXTURE_ENTRY main
#endif
int PREDICTION_DISPATCH_FIXTURE_ENTRY(void)
{
    void*globals=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(globals==(void*)0x144770000);SCENE s={0};setup(&s);
    PredFixture own={0},enemy={0};predSetup(&own,s.me,s.mods,s.ownNames,0);
    predSetup(&enemy,s.you,s.ymods,s.theirNames,20030000);
    integer(s.theirAnim+0xc8,20030000);integer(s.equipment+0x330,1000000);
    const CD_PRED_PROFILE*p=0;
    for(unsigned i=0;i<_countof(CdPredProfiles);i++)if(CdPredProfiles[i].weapon==1000000&&CdPredProfiles[i].animation==20030000&&CdPredProfiles[i].mode==1)p=&CdPredProfiles[i];
    CHECK(p);const CD_PRED_SHAPE*shape=&CdPredShapes[p->first];
    for(unsigned i=0;i<3;i++)real(s.ypos+0x80+i*4,CdPredBodies[0].a[i]-shape->a.p[i]);
    ObserveEarlyTargets(s.me,fakeNow);fakeNow+=12;predClock(&own,0,.312f);predClock(&enemy,20030000,.312f);
    ObserveEarlyTargets(s.me,fakeNow);
    int before=calls;
    CHECK(TryPhaseAutomaticRoll(1,s.me,fakeNow));
    CHECK(calls==before+1&&lastAction==ActionRollForward&&Defense.requests==1);
    Defense=(COPYDOG_DEFENSE){0};CombatFrameClaimed=0;
    CHECK(TryPhaseAutomaticRoll(1,s.me,fakeNow)&&calls==before+1);
    /* New name episode with the same visible ID and a missed native rewind. */
    fakeNow+=500;
    CopydogPredictionEpisode(1,(uintptr_t)s.you,fakeNow+1);
    fakeNow+=12;predClock(&own,0,.324f);predClock(&enemy,20030000,.324f);
    CopydogPredictionObserve(0,(uintptr_t)s.me,fakeNow);CopydogPredictionObserve(1,(uintptr_t)s.you,fakeNow);
    fakeNow+=12;predClock(&own,0,.336f);predClock(&enemy,20030000,.336f);
    CopydogPredictionObserve(0,(uintptr_t)s.me,fakeNow);CopydogPredictionObserve(1,(uintptr_t)s.you,fakeNow);
    CHECK(TryPhaseAutomaticRoll(1,s.me,fakeNow)&&calls==before+2&&Defense.requests==1);
    Defense=(COPYDOG_DEFENSE){0};CombatFrameClaimed=0;
    CHECK(TryPhaseAutomaticRoll(1,s.me,fakeNow)&&calls==before+2);
    /* A genuine native cycle must also rearm a spent same-ID parry track. */
    ParryTrack[1]=(COPYDOG_PARRY_TRACK){.self=(uintptr_t)s.me,.actor=(uintptr_t)s.you,.animation=20030000,.stable=1,.spent=1};
    predClock(&enemy,20030000,.7f);ObserveParryClock(1,s.me,s.you,20030000,fakeNow+12);
    CHECK(ParryTrack[1].spent);
    predClock(&enemy,20030000,.05f);ObserveParryClock(1,s.me,s.you,20030000,fakeNow+24);
    CHECK(!ParryTrack[1].spent&&ParryTrack[1].started==fakeNow+24);
    CHECK(!Unparriable(38030000)&&!Unparriable(38034000));
    if(exerciseParry(&s,&own,&enemy))return 1;
    VirtualFree(globals,0,MEM_RELEASE);
    printf("PASS %u native pose, swept geometry, roll and variable-cadence parry dispatch checks including drained post-physics batches\n",checks);
    return 0;
}
