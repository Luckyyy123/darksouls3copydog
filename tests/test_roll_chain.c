#include <windows.h>
static int held[256];
static SHORT WINAPI ChainKey(int key)
{
    return (unsigned)key < 256 && held[key] ? (SHORT)0x8000 : 0;
}
#define GetAsyncKeyState ChainKey
#define CROSSBOW_FIXTURE_ENTRY UnusedRollChainCrossbowFixture
#include "test_crossbow_roll.c"

/* One remote sword, real action names and the shipped 23030000 damage window.
   The world, input, clock and native action queue are local test doubles. */
static void pose(SCENE *s,int self,const wchar_t *name,int animation,float progress,int busy)
{
    char *names=self?s->ownNames:s->theirNames,*anim=self?s->ownAnim:s->theirAnim;
    memcpy(names+0x898,name,(wcslen(name)+1)*sizeof(wchar_t));
    integer(anim+0xc8,animation);
    if(self){real(s->state+0x224,progress);s->state[0x228]=(char)busy;}
}

static void startScene(SCENE *s)
{
    fakeNow+=3000;
    memset(s,0,sizeof *s);setup(s);
    memset(held,0,sizeof held);
    Defense=(COPYDOG_DEFENSE){0};
    ResetFrameWorld(s->me,fakeNow);
    FramePlayer=(uintptr_t)s->me;GameCadence=(CD_GAME_CADENCE){0};
    CombatFrameClaimed=0;CopyAction=0;ready=1;
    integer(s->equipment+0x330,16071010);
    pose(s,1,L"Idle",0,1.f,0);
    pose(s,0,L"Idle",22100,1.f,0);
}

/* Same ordering of defense observation, target observation and melee dispatch
   as the polling loop. Native queue calls are counted by the existing fixture. */
static void pollScene(SCENE *s,unsigned at)
{
    fakeNow=at;
    UpdateDefenseState(at);ObserveRollQ(s->me,at);
    CombatFrameClaimed=0;
    ObserveParryTargets(s->me,NULL,at);ObserveEarlyTargets(s->me,at);
    if(!CopydogDefenseBusy(&Defense))CheckFrameTargets(s->me,NULL,at);
}

static int swordWindow(COPYDOG_ROLL_PHASE_WINDOW *window)
{
    for(unsigned i=0;i<COPYDOG_PHASE_ATTACK_COUNT;i++){
        const COPYDOG_PHASE_ATTACK_PROFILE *p=&CopydogPhaseAttacks[i];
        if(p->animation!=23030000)continue;
        const COPYDOG_PHASE_ATTACK_INTERVAL *h=&CopydogPhaseAttackIntervals[p->firstInterval];
        return p->count==1&&CopydogRollPhaseWindow(h->startUs,h->endUs,0,433333,0,32,0,32,1,window);
    }
    return 0;
}

enum { FREE, ATTACKING, DRINKING, STAGGERED, SPACE, ROLLING, DISABLED,
       ROLL_NAME_ONLY, ROLL_ID_ONLY, BUSY_IDLE, PARTIAL_DAMAGE };
static void playerState(SCENE *s,int state)
{
    held[VK_SPACE]=state==SPACE;
    atomic_store(&AutoRollEnabled,state!=DISABLED);
    switch(state){
    case ATTACKING:pose(s,1,L"AttackRightLight1",23030000,1.f,0);return;
    case DRINKING:pose(s,1,L"ItemDrinkEstus",50000,1.f,0);return;
    case STAGGERED:pose(s,1,L"DamageMiddle",5102,.3f,1);return;
    case ROLLING:pose(s,1,L"RollingMedium",27105,1.f,0);return;
    case ROLL_NAME_ONLY:pose(s,1,L"RollingMedium",0,1.f,0);return;
    case ROLL_ID_ONLY:pose(s,1,L"Idle",27105,1.f,0);return;
    case BUSY_IDLE:pose(s,1,L"Idle",0,.2f,1);return;
    case PARTIAL_DAMAGE:pose(s,1,L"DamageLarge",5202,.2f,0);return;
    default:pose(s,1,L"Idle",0,1.f,0);return;
    }
}

static int sameSwing(SCENE *s,const COPYDOG_ROLL_PHASE_WINDOW *w)
{
    startScene(s);
    unsigned begin=fakeNow+64,first=0;int before=calls;
    for(unsigned t=begin-64;t<=begin+(unsigned)w->endMs+300;t+=4){
        if(t>=begin)pose(s,0,L"AttackRightLight1",t-begin<100?22100:23030000,1.f,0);
        if(first)playerState(s,t-first>=8&&t-first<48?ROLLING:FREE);
        pollScene(s,t);
        if(!first&&calls>before)first=t;
    }
    CHECK(first>=begin+(unsigned)w->preferredMs&&first<=begin+(unsigned)w->endMs);
    CHECK(lastAction==ActionRollForward&&Defense.requests==1&&calls==before+1);
    CHECK(!CopydogDefenseBusy(&Defense));
    return 0;
}

/* Start the second real name epoch while the first roll is still active.
   Every transition runs through both target observers, including a stable
   intervening Idle. The early name prevails before the visible attack ID. */
static int followUp(SCENE *s,const COPYDOG_ROLL_PHASE_WINDOW *w,int state,
                    unsigned margin,unsigned frameMs,int *didRoll)
{
    startScene(s);
    unsigned first=fakeNow+64,second=first+600,release=second+(unsigned)w->endMs-margin;
    unsigned requested=0;
    int before=calls;
    for(unsigned t=first-64;t<release;t+=4){
        if(t>=first&&t<first+480)pose(s,0,L"AttackRightLight1",t-first<100?22100:23030000,1.f,0);
        else if(t>=second)pose(s,0,L"AttackRightLight1",t-second<100?22100:23030000,1.f,0);
        else pose(s,0,L"Idle",22100,1.f,0);
        if(requested&&t-requested>=8)playerState(s,ROLLING);
        pollScene(s,t);
        if(!requested&&calls>before)requested=t;
    }
    CHECK(requested&&calls==before+1&&Defense.confirmations==1);
    CHECK(Defense.phase==DEF_ACTIVE&&EarlyTrack[1].started==second);
    CHECK(!PhaseRollSegment[1]);
    playerState(s,state);
    if(state==FREE)CHECK(ReadAutoRollState(s->me)==0);
    pollScene(s,release);
    for(unsigned t=release+frameMs;t<=second+(unsigned)w->endMs+90;t+=frameMs)pollScene(s,t);
    *didRoll=calls-before-1;
    CHECK(*didRoll>=0&&*didRoll<=1);
    CHECK(Defense.requests==(unsigned)(calls-before));
    return 0;
}

static int unconfirmedOldRequest(SCENE *s,const COPYDOG_ROLL_PHASE_WINDOW *w,int cancel)
{
    startScene(s);
    unsigned begin=fakeNow+64;
    int before=calls;
    for(unsigned t=begin-64;t<=begin+(unsigned)w->endMs+8;t+=4){
        if(t>=begin)pose(s,0,L"AttackRightLight1",t-begin<100?22100:23030000,1.f,0);
        if(t==begin+148){
            fakeNow=t;CombatFrameClaimed=0;
            CHECK(SubmitDefense(ActionRollForward,DEF_ROLL,NULL,-1,0,t));
        }
        if(cancel&&t==begin+180)integer(s->hp+0xd8,80);
        pollScene(s,t);
    }
    CHECK(calls==before+1&&Defense.requests==1&&!Defense.confirmations);
    CHECK(Defense.phase==(cancel?DEF_CANCELLED:DEF_WAIT_ANIMATION));
    return 0;
}

/* At 85 Hz a millisecond clock alternates 11 and 12 ms intervals.
   34 updates span 400 ms, putting this recovery inside the 402 ms deadline. */
static int followUpAt85(SCENE *s,const COPYDOG_ROLL_PHASE_WINDOW *w,int sc19)
{
    startScene(s);
    enum { firstFrame=6,secondFrame=57,releaseFrame=secondFrame+34 };
    const unsigned base=fakeNow,second=base+secondFrame*1000u/85u;
    int before=calls;unsigned previous=base;
    for(unsigned n=0;n<140;++n){
        unsigned now=base+n*1000u/85u;
        if(n&&now-previous!=11&&now-previous!=12)return 1;
        previous=now;
        if(n>=secondFrame||(n>=firstFrame&&n<firstFrame+40)){
            unsigned begin=n>=secondFrame?second:base+firstFrame*1000u/85u;
            pose(s,0,L"AttackRightLight1",now-begin<100?22100:23030000,1.f,0);
        }else pose(s,0,L"Idle",22100,1.f,0);
        int stillRolling=Defense.requests&&now-Defense.submitted>=8&&
                         (n<releaseFrame||(Defense.requests==2&&now-Defense.submitted<400));
        playerState(s,stillRolling?ROLLING:FREE);
        if(n==releaseFrame)CHECK(now-second+2==(unsigned)w->endMs&&Defense.confirmations==1);
        pollScene(s,now);
    }
    CHECK(calls==before+(sc19?1:2)&&Defense.confirmations==(unsigned)(sc19?1:2));
    return 0;
}

static int otherDefenseKeepsRecoveryGuard(SCENE *s,int kind)
{
    startScene(s);
    unsigned request=fakeNow;
    UpdateDefenseState(request);
    if(kind==DEF_ROLL){
        CHECK(SubmitDefense(ActionQuickStep,DEF_ROLL,NULL,-1,0,request));
        pose(s,1,L"QuickStep",38020,1.f,0);
    }else{
        CHECK(CopydogDefenseBegin(&Defense,(uintptr_t)s->me,0,-1,0,100,DEF_PARRY,0,request));
        pose(s,1,L"ParryLeftStart",48032100,1.f,0);
    }
    fakeNow=request+8;UpdateDefenseState(fakeNow);
    fakeNow=request+24;UpdateDefenseState(fakeNow);
    CHECK(Defense.phase==DEF_ACTIVE&&Defense.confirmations==1);
    playerState(s,FREE);
    fakeNow=request+60;UpdateDefenseState(fakeNow);
    CHECK(ReadAutoRollState(s->me)==0&&CopydogDefenseBusy(&Defense));
    fakeNow+=12;UpdateDefenseState(fakeNow);
    CHECK(!CopydogDefenseBusy(&Defense));
    return 0;
}

static int newProjectileAfterRecovery(SCENE *s,int previousKind,int sc19)
{
    char bolt[0x500]={0};
    startScene(s);
    CombatClearBulletThreat();CombatSetBulletInputPaused(0);
    /* The archived projectile callback timestamps on the system clock. Place
       the preceding confirmed defense on the same clock before publishing. */
    unsigned anchor=RealNow();
    unsigned previous=anchor-(previousKind==DEF_ROLL?100u:450u);
    fakeNow=previous;
    UpdateDefenseState(previous);
    if(previousKind==DEF_ROLL)
        CHECK(SubmitDefense(ActionRollForward,DEF_ROLL,NULL,-1,0,previous));
    else{
        CHECK(CopydogDefenseBegin(&Defense,(uintptr_t)s->me,0,-1,0,100,DEF_PARRY,0,previous));
        CHECK(TryDoAction(ActionLeftParry));
    }
    if(previousKind==DEF_ROLL)playerState(s,ROLLING);
    else pose(s,1,L"ParryLeftStart",48032100,1.f,0);
    fakeNow=previous+8;UpdateDefenseState(fakeNow);
    fakeNow+=16;UpdateDefenseState(fakeNow);
    CHECK(Defense.phase==DEF_ACTIVE&&Defense.confirmations==1);
    playerState(s,FREE);
    fakeNow=anchor-40;UpdateDefenseState(fakeNow);
    fakeNow=anchor-24;UpdateDefenseState(fakeNow);
    CHECK(!CopydogDefenseBusy(&Defense));
    integer(bolt+0x4e4,800);integer(bolt+0x4ec,MyId+1);integer(bolt+0x4dc,1);
    real(bolt+0x10,-1.8f);real(bolt+0x30,30.f);
    CombatFrameClaimed=0;CombatObserveBullet(bolt);fakeNow=RealNow();
    CHECK(fakeNow-Defense.confirmed<(previousKind==DEF_ROLL?128u:1024u));
    CHECK(CombatPeekBulletThreat(fakeNow));
    int before=calls;
    int accepted=TryBulletAutoRoll(fakeNow);
    CHECK(accepted==!sc19&&calls==before+accepted);
    if(accepted){
        CHECK(lastAction==ActionRollForward);
        unsigned shot=fakeNow;
        playerState(s,ROLLING);
        fakeNow=shot+8;UpdateDefenseState(fakeNow);
        fakeNow=shot+24;UpdateDefenseState(fakeNow);
        playerState(s,FREE);
        fakeNow=shot+48;UpdateDefenseState(fakeNow);
        fakeNow=shot+64;UpdateDefenseState(fakeNow);CombatFrameClaimed=0;
        CHECK(!CopydogDefenseBusy(&Defense));
        CombatObserveBullet(bolt);
        CHECK(!TryBulletAutoRoll(fakeNow)&&calls==before+1);
    }
    printf("New projectile after confirmed %s, second roll %d\n",previousKind==DEF_ROLL?"roll":"parry",accepted);
    return 0;
}

int main(int argc,char **argv)
{
    int sc19=argc==2&&!strcmp(argv[1],"--sc19-probe");
    void *globals=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(globals==(void*)0x144770000);
    SCENE scene;COPYDOG_ROLL_PHASE_WINDOW window;
    CHECK(swordWindow(&window)&&window.endMs>70);
    CHECK(!sameSwing(&scene,&window));
    const unsigned cadence[]={4,8,12,16,24,32,33,40};
    for(unsigned i=0;i<_countof(cadence);i++){
        unsigned frame=cadence[i];
        int second;
        CHECK(!followUp(&scene,&window,FREE,64,frame,&second)&&second==1);
        CHECK(!followUp(&scene,&window,FREE,2,frame,&second));
        CHECK(second==!sc19);
        printf("Recovered 2 ms before deadline, next update %u ms, second roll %d\n",frame,second);
    }
    for(int state=ATTACKING;state<=PARTIAL_DAMAGE;++state){
        int second;
        CHECK(!followUp(&scene,&window,state,64,12,&second));
        CHECK(second==(sc19&&state==PARTIAL_DAMAGE));
    }
    CHECK(!followUpAt85(&scene,&window,sc19));
    CHECK(!unconfirmedOldRequest(&scene,&window,0));
    CHECK(!unconfirmedOldRequest(&scene,&window,1));
    CHECK(!otherDefenseKeepsRecoveryGuard(&scene,DEF_ROLL));
    CHECK(!otherDefenseKeepsRecoveryGuard(&scene,DEF_PARRY));
    CHECK(!newProjectileAfterRecovery(&scene,DEF_ROLL,sc19));
    CHECK(!newProjectileAfterRecovery(&scene,DEF_PARRY,sc19));
    VirtualFree(globals,0,MEM_RELEASE);
    printf("PASS %u roll chain checks; sword window preferred=%d end=%d ms\n",checks,window.preferredMs,window.endMs);
    return 0;
}
