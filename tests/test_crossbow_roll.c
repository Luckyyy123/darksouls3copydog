#include <windows.h>
#include <mmsystem.h>
static DWORD fakeNow=10000;
static DWORD RealNow(void){return timeGetTime();}
static DWORD WINAPI TestNow(void){return fakeNow;}
#define timeGetTime TestNow
#define main UnusedLegacyFixture
#include "test_v6_restore.c"
#undef main

typedef struct {
    char world[0x100],slots[6*0x38],me[0x2100],you[0x2100];
    char mods[0x100],ymods[0x100],hp[0x100],yhp[0x100],ownAnim[0xd0],theirAnim[0xd0];
    char pos[0x90],ypos[0x90],state[0x229],ystate[0x229],equipment[0x400];
    char ownNames[0xa00],theirNames[0xa00],node[0x30],sub[0xc0],queue[0x20];
} SCENE;

static void setup(SCENE *s)
{
    pointer(addr1,s->world);pointer(s->world+0x80,s->me);pointer(s->world+0x40,s->slots);
    pointer(s->slots+0x38,s->you);integer(s->you+0x78,MyId+1);s->me[0x74]=5;
    integer(s->me+8,MyId);integer(s->you+8,MyId+1);
    pointer(s->me+XA,s->mods);pointer(s->you+XA,s->ymods);pointer(s->mods,s->state);pointer(s->ymods,s->ystate);
    pointer(s->mods+0x18,s->hp);pointer(s->ymods+0x18,s->yhp);integer(s->hp+0xd8,100);integer(s->yhp+0xd8,100);
    integer(s->hp+0xf0,100);real(s->state+0x224,1.f);
    pointer(s->mods+0x80,s->ownAnim);pointer(s->ymods+0x80,s->theirAnim);
    pointer(s->mods+0x68,s->pos);pointer(s->ymods+0x68,s->ypos);real(s->ypos+0x88,2.0f);
    pointer(s->mods+0x28,s->ownNames);pointer(s->ymods+0x28,s->theirNames);
    pointer(s->ownNames+0x10,s->node);pointer(s->node+0x28,s->sub);pointer(s->sub+0xb8,s->queue);
    memcpy(s->ownNames+0x898,L"Idle",sizeof L"Idle");
    pointer(s->you+XB,s->equipment);integer(s->equipment+0x2b8,1);
    integer(s->equipment+0x2bc,1);integer(s->equipment+0x334,14050010);integer(s->equipment+0x330,110000);
    Config.BackstabDist=Config.RollAtkDist=Config.ForestallDistance=0.f;
    Config.RollDistance=4.f;Config.KeyCondition=VK_SHIFT;Config.BulletDodge=1;
    atomic_store(&AutoRollEnabled,1);CopydogTraceInit(&CopydogAutoTrace);
}

typedef struct { unsigned ms;const wchar_t *name;int animation; } STEP;
static void sceneTick(SCENE *s,const STEP *step)
{
    memcpy(s->theirNames+0x898,step->name,(wcslen(step->name)+1)*sizeof(wchar_t));
    integer(s->theirAnim+0xc8,step->animation);
    unsigned age=fakeNow-Defense.submitted;
    integer(s->ownAnim+0xc8,Defense.kind==DEF_ROLL&&Defense.requests&&age>=24&&age<400?27105:0);
    UpdateDefenseState(fakeNow);CombatFrameClaimed=0;
    ObserveParryTargets(s->me,0,fakeNow);ObserveEarlyTargets(s->me,fakeNow);
    if(!CopydogDefenseBusy(&Defense))CheckFrameTargets(s->me,NULL,fakeNow);
}
static unsigned replay(SCENE *s,const STEP *steps,unsigned n,unsigned duration)
{
    unsigned before=Defense.requests,phase=0;
    for(unsigned ms=0;ms<duration;ms+=4){
        while(phase+1<n&&steps[phase+1].ms<=ms)++phase;
        sceneTick(s,&steps[phase]);fakeNow+=4;
    }
    return (unsigned)Defense.requests-before;
}
#ifndef CROSSBOW_FIXTURE_ENTRY
#define CROSSBOW_FIXTURE_ENTRY main
#endif
int CROSSBOW_FIXTURE_ENTRY(void)
{
    void *globals=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(globals==(void*)0x144770000);
    SCENE s={0};setup(&s);
    int first,last;GetAnimAtkTime(46033530,&first,&last);
    CHECK(first<0&&last<0&&Unparriable(46033530));
    /* Real left Arbalest/empty right hand: name and C8 transitions from local
       capture-20261112-200701/action_names.jsonl:5055-5070. No projectile in
       this fixture. Only the stationary tail of the hold is lengthened. */
    const STEP hold[]={{0,L"AttackCrossbowLeftStart",27115},{180,L"AttackCrossbowLeftStart",46033500},
        {251,L"Move",46033500},{275,L"AttackCrossbowLeftLoop",46033500},
        {590,L"AttackCrossbowLeftLoop",46033530},{1028,L"AttackCrossbowLeftLoop",46033540},
        {1259,L"AttackCrossbowLeftLoop",46033530}};
    unsigned held=replay(&s,hold,_countof(hold),5000);
    const STEP noBolt[]={{0,L"AttackCrossbowLeftFire",46033530},{102,L"AttackCrossbowLeftFire",46032000},
        {822,L"AttackCrossbowLeftReload",46032000},{996,L"AttackCrossbowLeftReload",46033520},
        {2455,L"Idle",46033520},{2520,L"Idle",22100}};
    unsigned fired=replay(&s,noBolt,_countof(noBolt),3200);
    /* New crossbow name, old dangerous C8: prove name protection rather
       than just the a46 family filter. The old ID is a synthetic stress case. */
    const STEP stale[]={{0,L"AttackCrossbowLeftStart",4050}};
    unsigned staleCount=replay(&s,stale,_countof(stale),1000);
    const STEP rest[]={{0,L"Idle",22100}};
    (void)replay(&s,rest,1,1000);
    /* Leave the crossbow selected on the left and attack with the right sword. */
    integer(s.equipment+0x330,16071010);
    (void)replay(&s,rest,1,200);
    const STEP sword[]={{0,L"AttackRightLight1",22100},{220,L"AttackRightLight1",23030000}};
    unsigned melee=replay(&s,sword,_countof(sword),480);
    CHECK(melee==1&&Defense.kind==DEF_ROLL&&lastAction==ActionRollForward);
    (void)replay(&s,rest,1,1000);
    /* DS3 Standard Bolt (404000, behavior 101100300 -> bullet 800).
       Velocity is controlled by the fixture, not a captured flight trace. */
    char projectile[0x500]={0};const int id=800;float speed=GetAttackBulletSpeed(id);
    CHECK(speed==30.f);integer(projectile+0x4e4,id);integer(projectile+0x4ec,MyId+1);integer(projectile+0x4dc,1);
    real(projectile+0x10,-3.f);
    CombatObserveBullet(projectile);CHECK(!CombatTakeBulletThreat(RealNow()));
    real(projectile+0x30,-speed);CombatObserveBullet(projectile);CHECK(!CombatTakeBulletThreat(RealNow()));
    real(projectile+0x30,speed);CombatObserveBullet(projectile);fakeNow=RealNow();
    unsigned before=Defense.requests;int submitted=TryBulletAutoRoll(fakeNow);
    unsigned projectileRolls=(unsigned)Defense.requests-before;
    printf("5s hold=%u fire/reload without projectile=%u stale-name handoff=%u sword R1=%u incoming projectile=%u\n",
        held,fired,staleCount,melee,projectileRolls);
    CHECK(held==0);CHECK(fired==0);CHECK(staleCount==0);CHECK(melee==1);
    CHECK(submitted&&projectileRolls==1&&lastAction==ActionRollForward);
    CHECK(!TryBulletAutoRoll(fakeNow));
    VirtualFree(globals,0,MEM_RELEASE);
    printf("PASS %u crossbow roll checks (offline)\n",checks);return 0;
}
