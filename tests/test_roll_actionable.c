#include <windows.h>
static unsigned char keys[256];
static SHORT WINAPI TestKeys(int key){return (unsigned)key<256&&keys[key]?(SHORT)0x8000:0;}
#define GetAsyncKeyState TestKeys
#define CROSSBOW_FIXTURE_ENTRY UnusedCrossbowFixture
#include "test_crossbow_roll.c"

static void own(SCENE *s,const wchar_t *name,int id,float progress,unsigned busy)
{
    memcpy(s->ownNames+0x898,name,(wcslen(name)+1)*sizeof(wchar_t));
    integer(s->ownAnim+0xc8,id);real(s->state+0x224,progress);s->state[0x228]=(char)busy;
}
static int rejected(SCENE *s)
{
    unsigned before=calls;uint64_t requests=Defense.requests;
    fakeNow+=40;
    if(SubmitDefense(ActionRollForward,DEF_ROLL,s->you,23030000,16071010,fakeNow))return 0;
    if(SubmitDefense(ActionQuickStep,DEF_ROLL,s->you,23030000,16071010,fakeNow))return 0;
    return before==(unsigned)calls&&requests==Defense.requests&&!CopydogDefenseBusy(&Defense)&&!RollQ.pending;
}

int main(void)
{
    void* globals=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(globals==(void*)0x144770000);SCENE s={0};setup(&s);
    /* SC8's real ReadyToRoll, not the fixture's ready=1, accepts these sampled
       poses. Capture 20261112-200701 local actor, times 6633.39, 879853.57,
       1526684.93 and 1527661.95 ms. No automatic roll was inferred from them. */
    struct {const wchar_t *name;int id;float progress;int oldAllowed;} cases[]={
        {L"DamageLarge",5102,0.f,0},{L"ThrowAtk",0,1.f,1},
        {L"FallStart",0,1.f,1},{L"Land",4200,.35f,1},
        {L"ThrowAtk",23030202,1.f,1},{L"RollingLight",27105,1.f,1},
        {L"AttackRightLight1",23030000,.1f,1},{L"UnknownAnimation",0,1.f,1}
    };
    for(unsigned i=0;i<_countof(cases);++i){
        own(&s,cases[i].name,cases[i].id,cases[i].progress,0);
        CHECK(DataReadyToRoll()==cases[i].oldAllowed);CHECK(rejected(&s));
    }
    own(&s,L"DamageLarge",5102,.2f,0);
    CHECK(ReadAutoRollState(s.me)==ROLL_SELF_RECOVERY&&!AutoRollCanSubmit(s.me));
    own(&s,L"DamageLarge",5102,.2f,1);CHECK(ReadAutoRollState(s.me)!=ROLL_SELF_OK);
    own(&s,L"DamageLarge",5102,0.f,0);CHECK(ReadAutoRollState(s.me)!=ROLL_SELF_OK);
    own(&s,L"ThrowDef",6000,1.f,0);CHECK(ReadAutoRollState(s.me)!=ROLL_SELF_OK);
    own(&s,L"Idle",0,1.f,1);CHECK(rejected(&s));
    own(&s,L"Move",300020100,0.f,1);
    CHECK(!DataReadyToRoll()&&!DataReadyToAttack());
    CHECK(ReadAutoRollState(s.me)==ROLL_SELF_OK&&!FrameParrySelfReady(s.me)); /* no native clock in this fixture */
    own(&s,L"Move",300020200,0.f,1);
    CHECK(!DataReadyToRoll()&&!DataReadyToAttack()&&ReadAutoRollState(s.me)==ROLL_SELF_OK);
    own(&s,L"AttackRightLight1",300020100,0.f,1);CHECK(!FrameParrySelfReady(s.me));
    own(&s,L"ItemDrinkEstus",0,1.f,0);CHECK(!FrameParrySelfReady(s.me));
    own(&s,L"DamageSmall",300020100,0.f,1);CHECK(!FrameParrySelfReady(s.me));
    own(&s,L"Idle",0,1.f,0);
    integer(s.hp+0xf0,0);CHECK(rejected(&s));integer(s.hp+0xf0,100);
    integer(s.hp+0xd8,0);CHECK(rejected(&s));integer(s.hp+0xd8,100);
    pointer(s.mods,0);CHECK(rejected(&s));pointer(s.mods,s.state);
    pointer(s.mods+0x28,0);CHECK(rejected(&s));pointer(s.mods+0x28,s.ownNames);
    s.state[0x58]=2;CHECK(rejected(&s));s.state[0x58]=0;
    s.state[0x5c]=1;CHECK(rejected(&s));s.state[0x5c]=0;
    atomic_store(&AutoRollEnabled,0);CHECK(rejected(&s));atomic_store(&AutoRollEnabled,1);
    keys[VK_SHIFT]=1;CHECK(rejected(&s));keys[VK_SHIFT]=0;
    keys[VK_SPACE]=1;CHECK(rejected(&s));keys[VK_SPACE]=0;
    CopyAction=1;CHECK(rejected(&s));CopyAction=0;
    /* A fall is not an incoming attack: the old table marked this observed
       FallStart ID unparriable but provided no contact window. */
    integer(s.theirAnim+0xc8,4050);
    memcpy(s.theirNames+0x898,L"FallStart",sizeof L"FallStart");
    for(unsigned i=0;i<12;++i){CheckAnimation(1);fakeNow+=4;}
    CHECK(!calls&&!Defense.requests);

    /* Legacy table has no entry for this native Type0 attack; the resource
       itself is not an idle/fall pose (resolved_animation_events.csv:76850).
       Keep a single stale observation across a two-second self lock. */
    const int legacy=144038300;int first,last;
    GetAnimAtkTime(legacy,&first,&last);CHECK(first<0&&last<0&&Unparriable(legacy));
    own(&s,L"DamageMiddle",5102,1.f,1);integer(s.theirAnim+0xc8,legacy);
    memcpy(s.theirNames+0x898,L"SkillAttack",sizeof L"SkillAttack");
    for(unsigned i=0;i<500;++i){CheckAnimation(1);fakeNow+=4;}
    CHECK(!calls&&!Defense.requests);
    own(&s,L"Move",300020100,0.f,1);ready=0;
    for(unsigned i=0;i<4;++i){CheckAnimation(1);fakeNow+=4;}
    CHECK(!calls&&!Defense.requests);
    integer(s.theirAnim+0xc8,22100);
    for(unsigned i=0;i<4;++i){CheckAnimation(1);fakeNow+=4;}
    integer(s.theirAnim+0xc8,legacy);
    for(unsigned i=0;i<3;++i){CheckAnimation(1);fakeNow+=4;}
    CHECK(!calls&&!Defense.requests);
    CheckAnimation(1);CHECK(calls==1&&Defense.requests==1&&lastAction==ActionRollForward);
    /* Simulate recovery within the freshness threshold. Unchanged target
       episode must not issue a duplicate after becoming free again. */
    Defense.phase=DEF_IDLE;CombatFrameClaimed=0;fakeNow+=36;CheckAnimation(1);
    CHECK(calls==1&&Defense.requests==1);
    ready=1;

    /* A named sword R1 remains live while the player is hit. Recovery within
       the real window is usable immediately; recovery after it is not. */
    integer(s.equipment+0x330,16071010);
    memcpy(s.theirNames+0x898,L"AttackRightLight1",sizeof L"AttackRightLight1");
    integer(s.theirAnim+0xc8,22100);
    EarlyTrack[1]=(COPYDOG_EARLY_STATE){.actor=(uintptr_t)s.you,.networkId=MyId+1,
        .weaponBase=16070000,.mode=1,.started=fakeNow,.initialized=1,.active=1,.stable=1,.initialAnim=22100};
    strcpy(EarlyTrack[1].name,"attackrightlight1");
    ParryTrack[1]=(COPYDOG_PARRY_TRACK){.self=(uintptr_t)s.me,.actor=(uintptr_t)s.you,
        .stable=1,.animation=22100,.started=fakeNow-20};
    const COPYDOG_PHASE_ATTACK_INTERVAL *hit=0;
    for(unsigned j=0;j<COPYDOG_PHASE_ATTACK_COUNT;++j)if(CopydogPhaseAttacks[j].animation==23030000)
        hit=&CopydogPhaseAttackIntervals[CopydogPhaseAttacks[j].firstInterval];
    COPYDOG_ROLL_PHASE_WINDOW w;CHECK(hit&&CopydogRollPhaseWindow(hit->startUs,hit->endUs,0,433333,0,32,0,32,1,&w));
    int initial=calls;
    fakeNow=EarlyTrack[1].started+w.preferredMs;
    own(&s,L"DamageLarge",5102,1.f,1);(void)TryPhaseAutomaticRoll(1,s.me,fakeNow);
    CHECK(calls==initial&&!PhaseRollSegment[1]&&!CopydogDefenseBusy(&Defense));
    fakeNow=EarlyTrack[1].started+w.endMs+1;
    own(&s,L"Move",300020100,0.f,1);(void)TryPhaseAutomaticRoll(1,s.me,fakeNow);
    CHECK(calls==initial&&!PhaseRollSegment[1]);
    EarlyTrack[1].started=fakeNow+100;fakeNow=EarlyTrack[1].started+w.preferredMs;
    own(&s,L"FallStart",0,1.f,0);(void)TryPhaseAutomaticRoll(1,s.me,fakeNow);
    CHECK(calls==initial&&!PhaseRollSegment[1]);
    own(&s,L"Move",300020100,0.f,1);fakeNow+=4;
    CHECK(TryPhaseAutomaticRoll(1,s.me,fakeNow)&&calls==initial+1&&lastAction==ActionRollForward);
    (void)TryPhaseAutomaticRoll(1,s.me,fakeNow+4);CHECK(calls==initial+1&&CopydogDefenseBusy(&Defense));

    Defense=(COPYDOG_DEFENSE){0};CombatFrameClaimed=0;fakeNow+=100;
    own(&s,L"DamageLarge",5102,.2f,0);int recoveryCalls=calls;
    CHECK(!SubmitDefense(ActionRollForward,DEF_ROLL,s.you,23030000,16071010,fakeNow)&&calls==recoveryCalls);
    own(&s,L"Move",5102,1.f,0);
    CHECK(SubmitDefense(ActionRollForward,DEF_ROLL,s.you,23030000,16071010,fakeNow)&&calls==recoveryCalls+1);
    Defense=(COPYDOG_DEFENSE){0};CombatFrameClaimed=0;
    /* Mirror input must not force a second action while locked. The original
       input from either player must always be forwarded once in these cases. */
    char yNode[0x30]={0},ySub[0xc0]={0},yQueue[0x20]={0};
    pointer(s.theirNames+0x10,yNode);pointer(yNode+0x28,ySub);pointer(ySub+0xb8,yQueue);
    integer(s.yhp+0xec,1);prevLockTarget=s.you;CopyAction=1;
    intptr_t mirrored[3]={ActionQuickStep,0,0};int before=calls;
    atomic_store(&MirrorDefenseBusy,0);
    own(&s,L"ThrowAtk",0,1.f,0);OnAction(yQueue,(int*)mirrored);CHECK(calls==before+1);
    OnAction(s.queue,(int*)mirrored);CHECK(calls==before+2);
    own(&s,L"Idle",0,1.f,0);OnAction(yQueue,(int*)mirrored);CHECK(calls==before+4);
    atomic_store(&MirrorDefenseBusy,1);OnAction(yQueue,(int*)mirrored);CHECK(calls==before+5);
    /* The local game request is forwarded immediately. An automatic action
       yields while the previously published animation still looks idle. */
    CopyAction=0;CombatFrameClaimed=0;
    intptr_t manual[3]={ActionRightAttack1,0,0},walk[3]={ActionWalk,0,0};
    OnAction(yQueue,(int*)manual);OnAction(s.queue,(int*)walk);
    CHECK(AutoRollCanSubmit(s.me));
    OnAction(s.queue,(int*)manual);CHECK(lastAction==ActionRightAttack1&&!AutoRollCanSubmit(s.me));
    CHECK(!AutoRollCanSubmit(s.me)&&!LocalActionPending(s.you,fakeNow));
    own(&s,L"AttackRightLight1",23030000,.2f,1);fakeNow+=12;UpdateDefenseState(fakeNow);
    CHECK(!LocalActionPending(s.me,fakeNow)&&!AutoRollCanSubmit(s.me));
    own(&s,L"Idle",0,1.f,0);fakeNow+=12;CHECK(AutoRollCanSubmit(s.me));
    OnAction(s.queue,(int*)manual);fakeNow+=COPYDOG_ACTION_CONFIRM_MS-1;
    CHECK(!AutoRollCanSubmit(s.me));++fakeNow;CHECK(AutoRollCanSubmit(s.me));
    VirtualFree(globals,0,MEM_RELEASE);
    printf("PASS %u automatic-roll eligibility integration assertions\n",checks);return 0;
}
