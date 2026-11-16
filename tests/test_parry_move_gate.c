#define PREDICTION_DISPATCH_FIXTURE_ENTRY UnusedPredictionFixture
#include "test_prediction_dispatch.c"

enum TrackChange { NO_CHANGE, BEFORE_PLAN, AFTER_PLAN, OLD_IN_BATCH };
enum { WALK=300020100, RUN=300020200, ENEMY_CLIP=195034000, ENEMY_WEAPON=6080000 };
typedef struct {SCENE s;PredFixture own,enemy;char manager[0xc0];} MoveScene;

static int start(MoveScene*f,int move)
{
    memset(f,0,sizeof *f);SCENE*s=&f->s;setup(s);
    predSetup(&f->own,s->me,s->mods,s->ownNames,move);
    predSetup(&f->enemy,s->you,s->ymods,s->theirNames,ENEMY_CLIP);
    pointer(addr2,f->manager);pointer(f->manager+16,f->own.ownEquipment);
    integer(f->own.ownEquipment+0x2b8,1);integer(f->own.ownEquipment+0x32c,11010000);
    integer(s->equipment+0x2b8,3);integer(s->equipment+0x330,ENEMY_WEAPON);
    integer(s->theirAnim+0xc8,ENEMY_CLIP);
    const CD_PRED_SHAPE*shape=NULL;
    for(unsigned i=0;i<_countof(CdPredProfiles);i++){
        const CD_PRED_PROFILE*p=&CdPredProfiles[i];
        if(p->weapon==ENEMY_WEAPON&&p->animation==ENEMY_CLIP&&p->mode==3&&p->hand==1&&p->count){
            shape=&CdPredShapes[p->first];break;
        }
    }
    CHECK(shape);
    for(unsigned i=0;i<3;i++)real(s->ypos+0x80+4*i,CdPredBodies[0].a[i]-shape->a.p[i]);
    fakeNow+=1000;Config.KeyCondition=0;Config.ParryDistance=8.f;atomic_store(&AutoRollEnabled,0);
    Defense=(COPYDOG_DEFENSE){0};CombatFrameClaimed=0;GameCadence=(CD_GAME_CADENCE){0};
    DynamicSelf=(DynamicParrySelf){0};DynamicTargets[1]=(ParryFrameTarget){0};
    ParryTrack[1]=(COPYDOG_PARRY_TRACK){0};EarlyTrack[1]=(COPYDOG_EARLY_STATE){0};
    CopydogPredictionResetAll();SelfMoveClock=(ParryMoveClock){0};NoteLocalAction(NULL,-1,fakeNow);
    LocalAction moving={L"Move",move,0.f,LOCAL_AT_START,1};
    setLocalAction(s,&moving);CHECK(!ready&&ReadAutoRollState(s->me)==ROLL_SELF_OK);
    return 0;
}
static void change(MoveScene*f,int id,int drain)
{
    publish(&f->own,3,id,.023529f,.035294f);
    integer(f->own.clock+0xc4,drain?4:2);
}

static int dispatchCase(int move,enum TrackChange when,int other,int drained)
{
    MoveScene f;if(start(&f,move))return 1;
    SCENE*s=&f.s;
    size_t row=FirstDynamicWindow(CdDynamicTargets,_countof(CdDynamicTargets),ENEMY_WEAPON,ENEMY_CLIP);
    CHECK(row<_countof(CdDynamicTargets));
    ParryFrameTarget oracle={.self=(uintptr_t)s->me,.actor=(uintptr_t)s->you,.weapon=ENEMY_WEAPON,.handMode=3};
    FramePress press={0};int before=calls;
    enum CD_PARRY_FRAME_DECISION decision=CD_FRAME_SKIP;
    for(unsigned i=0;i<2;i++){
        unsigned phase=424+12*i,head=2*i;fakeNow+=12;
        if(when==OLD_IN_BATCH){
            publish(&f.own,head,other,.012f,.024f);
            publish(&f.own,head+1,move,(phase-12)/1000.f,phase/1000.f);integer(f.own.clock+0xc4,head);
        }else publish(&f.own,head,move,(phase-12)/1000.f,phase/1000.f);
        publish(&f.enemy,i,ENEMY_CLIP,(phase-12)/1000.f,phase/1000.f);
        CopydogPredictionObserve(0,oracle.self,fakeNow);CopydogPredictionObserve(1,oracle.actor,fakeNow);
        CD_NATIVE_FRAME remote;CHECK(CopydogReadNativeFrame(oracle.actor,&remote));
        CHECK(CopydogAdvanceFrame(&oracle.clock,&remote,fakeNow));
        if(i&&when==BEFORE_PLAN)change(&f,other,drained);
        ObserveDynamicParrySelf(s->me,fakeNow);
        decision=EvaluateParryFrame(1,s->me,s->you,fakeNow,&press);
        if(!i)CHECK(decision!=CD_FRAME_PRESS);
    }
    ParryActivation activation;CHECK(FrameSelfActivation(11010000,1,12.,&activation));
    FramePlan geometry=PlanParryFrame(1,&oracle,&activation,row,fakeNow,12.);
    CHECK(geometry.decision==CD_FRAME_PRESS&&CopydogParryContactAuthorizes(geometry.source));
    if(when==AFTER_PLAN)change(&f,other,drained);
    CD_NATIVE_FRAME latest;int retained,ordinary=when==NO_CHANGE||when==OLD_IN_BATCH;
    CHECK(CopydogReadPublishedNativeFrame(oracle.self,&latest,&retained));
    CHECK(GetActorAnim(s->me)==move&&latest.animation==(ordinary?move:other)&&retained==drained);
    if(when==BEFORE_PLAN)CHECK(decision!=CD_FRAME_PRESS);
    else{
        CHECK(decision==CD_FRAME_PRESS);
        CHECK(FramePressCurrent(&press,fakeNow)==ordinary);
        CHECK(DispatchParryFrame(&press)==ordinary);
    }
    CHECK(calls==before+ordinary&&DynamicTargets[1].submitted==ordinary);
    if(ordinary)CHECK(lastAction==ActionLeftParry);
    return 0;
}

static int retainedAndMissing(void)
{
    MoveScene f;if(start(&f,WALK))return 1;
    SCENE*s=&f.s;
    fakeNow=UINT32_MAX-16; /* both recovery and evidence expiry cross DWORD rollover */
    change(&f,23030450,1);
    CHECK(!FrameParrySelfReady(s->me));
    fakeNow+=CD_PRED_MAX_AGE_MS;CHECK(!FrameParrySelfReady(s->me));
    ++fakeNow;CHECK(FrameParrySelfReady(s->me));
    publish(&f.own,4,23030450,.023529f,.035294f);integer(f.own.clock+0xc4,5);
    CHECK(FrameParrySelfReady(s->me)); /* a copied, fully consumed record keeps its age */
    publish(&f.own,5,23030450,.105882f,.117647f);integer(f.own.clock+0xc4,6);
    CHECK(!FrameParrySelfReady(s->me)); /* fresh phase even with an empty queue */
    pointer(s->mods+0x10,NULL);CHECK(!FrameParrySelfReady(s->me));
    PredFixture newClock={0};pointer(s->mods+0x10,newClock.clock);
    publish(&newClock,6,WALK,.1f,.112f);integer(newClock.clock+0xc4,7);
    CHECK(FrameParrySelfReady(s->me));
    intptr_t manual[3]={ActionRightAttack1,0,0};
    OnAction(s->queue,(int*)manual);CHECK(!FrameParrySelfReady(s->me));
    fakeNow+=COPYDOG_ACTION_CONFIRM_MS;
    pointer(s->mods+0x10,NULL);
    LocalAction idle={L"Idle",0,1.f,LOCAL_AT_START,1};setLocalAction(s,&idle);
    CHECK(FrameParrySelfReady(s->me)); /* original neutral path can work without that clock */
    return 0;
}

int main(void)
{
    void*world=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    void*manager=VirtualAlloc((void*)0x144750000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(world&&manager);
    if(dispatchCase(WALK,NO_CHANGE,0,0)||dispatchCase(RUN,NO_CHANGE,0,0)||
       dispatchCase(WALK,OLD_IN_BATCH,23030450,0))return 1;
    const int protected[]={23030000,23030450,40000,50000,27105,5102,9510};
    for(unsigned i=0;i<_countof(protected);i++)
        for(int drain=0;drain<=1;drain++)
            if(dispatchCase(WALK,BEFORE_PLAN,protected[i],drain)||
               dispatchCase(RUN,AFTER_PLAN,protected[i],drain))return 1;
    if(retainedAndMissing())return 1;
    VirtualFree(manager,0,MEM_RELEASE);VirtualFree(world,0,MEM_RELEASE);
    printf("PASS %u busy locomotion, retained self-clock and actual CCD dispatch checks\n",checks);
    return 0;
}
