/* Repeated native swing with the same action name and visible ID.
   The scene supplies no pose palette, so only the resource roll can succeed. */
#define CROSSBOW_FIXTURE_ENTRY UnusedCrossbowRollFixture
#include "test_crossbow_roll.c"

enum { SwordAttack=23030000, UpdateMs=12 };
enum RepeatCase { Continuous, WaitForSpeed, HeldBeforeDue, MissingEdge, MissingBeforeFirstDue,
    ChangedTarget, ChangedHandle, ChangedWeapon, SameWeaponBaseBeforeFirstDue };
typedef struct {
    SCENE game;
    char alternateActor[sizeof(((SCENE*)0)->you)],clock[0x100];
    unsigned nextSlot;
} RepeatScene;

static void publish(RepeatScene*s,int animation,int previousMs,int currentMs)
{
    unsigned slot=s->nextSlot++%CD_PRED_CLOCK_COUNT;
    char*record=s->clock+CD_PRED_CLOCK_RECORDS+16*slot;
    integer(record,animation);real(record+4,previousMs*.001f);
    real(record+8,currentMs*.001f);real(record+12,2.f);
    integer(s->clock+CD_PRED_CLOCK_INDICES,(slot+1)%CD_PRED_CLOCK_COUNT);
    integer(s->clock+CD_PRED_CLOCK_INDICES+4,slot);
}

static void update(RepeatScene*s,unsigned tick,const wchar_t*name,int visible)
{
    SCENE*g=&s->game;fakeNow=tick;
    memcpy(g->theirNames+0x898,name,(wcslen(name)+1)*sizeof(*name));
    integer(g->theirAnim+0xc8,visible);
    unsigned age=tick-Defense.submitted;
    int inRoll=Defense.requests&&age>=24&&age<408;
    const wchar_t*own=inRoll?L"RollingLight":L"Idle";
    memcpy(g->ownNames+0x898,own,(wcslen(own)+1)*sizeof(*own));
    integer(g->ownAnim+0xc8,inRoll?27105:0);
    UpdateDefenseState(tick);CombatFrameClaimed=0;
    ObserveParryTargets(g->me,NULL,tick);
    ObserveEarlyTargets(g->me,tick);
    if(!CopydogDefenseBusy(&Defense))(void)TryPhaseAutomaticRoll(1,g->me,tick);
}

static int repeat_case(enum RepeatCase kind,int nativeStep)
{
    RepeatScene scene={0};SCENE*g=&scene.game;
    unsigned start=fakeNow+2000;
    setup(g);Defense=(COPYDOG_DEFENSE){0};
    ResetFrameWorld(g->me,start-60);calls=0;
    integer(g->equipment+0x330,16071010);pointer(g->ymods+0x10,scene.clock);
    for(unsigned t=start-48;t<start;t+=UpdateMs){
        publish(&scene,22100,0,12);update(&scene,t,L"Move",22100);
    }
    /* First swing includes the usual delayed visible ID. Keep its name held
       while the first roll confirms, ends, and the actor returns to idle. */
    int firstPending=kind==MissingBeforeFirstDue||kind==SameWeaponBaseBeforeFirstDue;
    int firstEnd=firstPending?228:900;
    int firstCount=firstPending?0:1;
    for(int elapsed=0;elapsed<=firstEnd;elapsed+=UpdateMs){
        publish(&scene,SwordAttack,elapsed,elapsed+UpdateMs);
        update(&scene,start+(unsigned)elapsed,L"AttackRightLight1",elapsed<144?22100:SwordAttack);
    }
    CHECK(calls==firstCount&&Defense.confirmations==(unsigned)firstCount&&Defense.phase==DEF_IDLE);
    CHECK(AutoRollCanSubmit(g->me)&&EarlyTrack[1].paired);
    int weaponAnimation;double contact;
    CHECK(CopydogPredictionQuery(1,(uintptr_t)g->you,(uintptr_t)g->me,fakeNow,
                                 &contact,&weaponAnimation)==CD_PRED_UNKNOWN);
    unsigned second=start+(unsigned)firstEnd+UpdateMs;
    int issuedAtPhase=-1;
    for(int elapsed=0;elapsed<=1440;elapsed+=UpdateMs){
        int phaseMs=(elapsed/UpdateMs+1)*nativeStep;
        if(kind==WaitForSpeed)phaseMs=192+(elapsed/UpdateMs)*nativeStep;
        int held=kind==HeldBeforeDue&&elapsed>=276&&elapsed<372;
        if(kind==HeldBeforeDue&&elapsed>=372)phaseMs-=96;
        if(kind==SameWeaponBaseBeforeFirstDue){
            if(!elapsed)integer(g->equipment+0x330,16071011);
            phaseMs+=firstEnd+UpdateMs;
        }
        if(kind==MissingBeforeFirstDue){
            if(elapsed<96)real(g->ypos+0x88,8.f);
            else {real(g->ypos+0x88,2.f);phaseMs-=96;}
        }
        if(elapsed==UpdateMs&&kind==ChangedTarget){
            memcpy(scene.alternateActor,g->you,sizeof g->you);
            integer(scene.alternateActor+8,MyId+2);integer(scene.alternateActor+0x78,MyId+2);
            pointer(g->slots+0x38,scene.alternateActor);
        }
        if(elapsed==UpdateMs&&kind==ChangedHandle)integer(g->you+8,MyId+2);
        if(elapsed==UpdateMs&&kind==ChangedWeapon)integer(g->equipment+0x330,2060010);
        if(held||((kind==MissingEdge||kind==MissingBeforeFirstDue)&&elapsed<96)){
            /* No publication bridges the rewind. The first valid record after
               the gap is already an advancing attack, not a new name onset. */
            integer(scene.clock+CD_PRED_CLOCK_INDICES+4,
                    (int)(scene.nextSlot%CD_PRED_CLOCK_COUNT));
        }else publish(&scene,SwordAttack,phaseMs-nativeStep,phaseMs);
        int before=calls;
        update(&scene,second+(unsigned)elapsed,L"AttackRightLight1",SwordAttack);
        if(!elapsed&&(kind==Continuous||kind==WaitForSpeed))CHECK(PhaseEpisode[1].episodeStarted==second);
        if(before!=calls){CHECK(issuedAtPhase<0);issuedAtPhase=phaseMs;}
        if(kind==WaitForSpeed&&elapsed<=2*UpdateMs)CHECK(calls==(elapsed==2*UpdateMs?2:1));
        if(kind==HeldBeforeDue&&elapsed>=276&&elapsed<=396)CHECK(calls==(elapsed==396?2:1));
    }
    if(kind==Continuous||kind==WaitForSpeed||kind==HeldBeforeDue){
        CHECK(calls==2&&Defense.confirmations==2&&Defense.phase==DEF_IDLE&&issuedAtPhase>=3*nativeStep);
        /* Raw TAE for this swing is [466.667,600) ms. Playback speed changes
           the attack time remaining. Own 433.333 ms iframes remain in wall time. */
        double rate=(double)nativeStep/UpdateMs;
        double first=(466.667-issuedAtPhase)/rate,last=(600.-issuedAtPhase)/rate;
        CHECK(first>=64.-UpdateMs&&last<=433.333+UpdateMs);
    }else CHECK(calls==firstCount&&issuedAtPhase<0);
    return 0;
}

int main(void)
{
    void*globals=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(globals==(void*)0x144770000);
    CHECK(!repeat_case(Continuous,12));
    CHECK(!repeat_case(Continuous,6));
    CHECK(!repeat_case(WaitForSpeed,24));
    CHECK(!repeat_case(HeldBeforeDue,12));
    CHECK(!repeat_case(MissingEdge,12));
    CHECK(!repeat_case(MissingBeforeFirstDue,12));
    CHECK(!repeat_case(ChangedTarget,12));
    CHECK(!repeat_case(ChangedHandle,12));
    CHECK(!repeat_case(ChangedWeapon,12));
    CHECK(!repeat_case(SameWeaponBaseBeforeFirstDue,12));
    VirtualFree(globals,0,MEM_RELEASE);
    printf("PASS %u same-ID repeat roll assertions\n",checks);
    return 0;
}
