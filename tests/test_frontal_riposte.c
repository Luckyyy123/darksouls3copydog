static int TestNativeFrontal(void*,void*,void*,int,unsigned char,unsigned char,int);
#define COPYDOG_FRONTAL_THROW_CALL TestNativeFrontal
#define main UnusedLegacyFrontalMain
#include "test_v6_restore.c"
#undef main

static int nativeCalls,nativeResult=FRONTAL_THROW_ID,nativeType,nativeFlags[3];
static void*nativeManager,*nativeSelf,*nativeTarget;
static int TestNativeFrontal(void*manager,void*self,void*target,int type,unsigned char first,unsigned char second,int event)
{
    ++nativeCalls;nativeManager=manager;nativeSelf=self;nativeTarget=target;
    nativeType=type;nativeFlags[0]=first;nativeFlags[1]=second;nativeFlags[2]=event;
    return nativeResult;
}
/* Each helper call represents a new virtual worker poll. Same-poll rejection
   cases below deliberately call the production function without clearing claim. */
static int RipostePoll(char*self,unsigned now)
{
    CombatFrameClaimed=0;
    return TryAutomaticRiposte(self,now);
}
static int RejectQueueBeforeFacing(char*me,char*you,char*ownAnim,char*targetAnim,char*physics)
{
    int before=nativeCalls;
    Defense=(COPYDOG_DEFENSE){0};BackstabTransaction=(COPYDOG_BACKSTAB_TRANSACTION){0};
    memset(RiposteTrack,0,sizeof RiposteTrack);
    CopydogRiposteSubmitted(&RiposteTrack[1],(uintptr_t)me,(uintptr_t)you,0,25034000,5000);
    integer(ownAnim+0xc8,42032100);integer(targetAnim+0xc8,6000);
    real(physics+0x74,.375f);
    CombatFrameClaimed=1;
    CHECK(!TryAutomaticRiposte(me,5100));
    CHECK(!TryAutomaticRiposte(me,5112));
    CHECK(RiposteTrack[1].pending&&RiposteTrack[1].staggered);
    CHECK(nativeCalls==before&&calls==0&&readlpt(float,physics+0x74)==.375f);
    CHECK(RiposteTrack[1].criticalState==RIPOSTE_CRITICAL_NONE);
    CombatFrameClaimed=0;
    CopydogBackstabSubmitted(&BackstabTransaction,(uintptr_t)me,5124);
    CHECK(!TryAutomaticRiposte(me,5136));
    CHECK(nativeCalls==before&&calls==0&&readlpt(float,physics+0x74)==.375f);
    CHECK(BackstabTransaction.pending&&RiposteTrack[1].criticalState==RIPOSTE_CRITICAL_NONE);
    return 0;
}
int main(void)
{
    void*worldGlobals=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    void*throwGlobals=VirtualAlloc((void*)0x144750000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(worldGlobals==(void*)0x144770000&&throwGlobals==(void*)0x144750000);
    char world[0x100]={0},slots[6*0x38]={0},me[0x2100]={0},you[0x2100]={0},equipment[0x400]={0};
    pointer(me+XB,equipment);
    char modules[0x100]={0},targetModules[0x100]={0},state[0x229]={0},stats[0x100]={0},targetStats[0x100]={0};
    char ownAnim[0xd0]={0},targetAnim[0xd0]={0},physics[0x90]={0},targetPhysics[0x90]={0};
    char throwModule[0x40]={0},targetThrowModule[0x40]={0},ownHandle[0x70]={0},targetHandle[0x70]={0},manager[0x100]={0};
    pointer(addr1,world);pointer(world+0x80,me);pointer(world+0x40,slots);pointer(slots+0x38,you);
    pointer(me+XA,modules);pointer(you+XA,targetModules);pointer(modules,state);
    pointer(modules+0x18,stats);pointer(targetModules+0x18,targetStats);
    pointer(modules+0x80,ownAnim);pointer(targetModules+0x80,targetAnim);
    pointer(modules+0x68,physics);pointer(targetModules+0x68,targetPhysics);
    pointer(modules+0x88,throwModule);pointer(targetModules+0x88,targetThrowModule);
    pointer(throwModule+0x28,ownHandle);pointer(targetThrowModule+0x28,targetHandle);
    pointer(ownHandle,me);pointer(targetHandle,you);
    pointer((void*)CopydogAddress(0x14475A7A8),manager);
    integer(stats+0xd8,100);integer(stats+0xf0,100);integer(targetStats+0xd8,100);
    real(state+0x224,1);real(targetPhysics+0x88,1);
    integer(targetAnim+0xc8,6000);ready=1;

    CHECK(TryRequestFrontalRiposte(0,you)==FRONTAL_THROW_INVALID);
    CHECK(TryRequestFrontalRiposte(me,0)==FRONTAL_THROW_INVALID);
    CHECK(TryRequestFrontalRiposte(me,me)==FRONTAL_THROW_INVALID);
    CHECK(TryRequestFrontalRiposte(you,me)==FRONTAL_THROW_INVALID&&nativeCalls==0);
    integer(targetAnim+0xc8,25034000);
    CHECK(TryRequestFrontalRiposte(me,you)==FRONTAL_THROW_STALE);
    integer(targetAnim+0xc8,6000);integer(targetStats+0xd8,0);
    CHECK(TryRequestFrontalRiposte(me,you)==FRONTAL_THROW_DEAD);
    integer(targetStats+0xd8,100);integer(stats+0xd8,0);
    CHECK(TryRequestFrontalRiposte(me,you)==FRONTAL_THROW_DEAD);
    integer(stats+0xd8,100);real(state+0x224,0);state[0x228]=1;
    CHECK(TryRequestFrontalRiposte(me,you)==FRONTAL_THROW_NOT_READY);
    real(state+0x224,1);state[0x228]=0;pointer(throwModule+0x28,0);
    CHECK(TryRequestFrontalRiposte(me,you)==FRONTAL_THROW_NO_HANDLE);
    pointer(throwModule+0x28,ownHandle);pointer(targetThrowModule+0x28,ownHandle);
    CHECK(TryRequestFrontalRiposte(me,you)==FRONTAL_THROW_NO_HANDLE);
    pointer(targetThrowModule+0x28,targetHandle);pointer(targetHandle,me);
    CHECK(TryRequestFrontalRiposte(me,you)==FRONTAL_THROW_NO_HANDLE);
    pointer(targetHandle,you);pointer(ownHandle,you);
    CHECK(TryRequestFrontalRiposte(me,you)==FRONTAL_THROW_NO_HANDLE);
    pointer(ownHandle,me);pointer((void*)CopydogAddress(0x14475A7A8),0);
    CHECK(TryRequestFrontalRiposte(me,you)==FRONTAL_THROW_NO_HANDLE&&nativeCalls==0);
    pointer((void*)CopydogAddress(0x14475A7A8),manager);
    CHECK(TryRequestFrontalRiposte(me,you)==FRONTAL_THROW_ID&&nativeCalls==1);
    CHECK(nativeManager==manager&&nativeSelf==ownHandle&&nativeTarget==targetHandle&&nativeType==101);
    CHECK(nativeFlags[0]==0&&nativeFlags[1]==0&&nativeFlags[2]==-1);
    nativeResult=-1;
    CHECK(TryRequestFrontalRiposte(me,you)==-1&&nativeCalls==2);

    /* Exercise selection -> native request -> actual confirmation, without any ordinary R1. */
    nativeResult=FRONTAL_THROW_ID;Defense=(COPYDOG_DEFENSE){0};CopyAction=0;calls=0;
    unsigned now=1000;CopydogTraceInit(&CopydogAutoTrace);
    CopydogRiposteSubmitted(&RiposteTrack[1],(uintptr_t)me,(uintptr_t)you,0,25034000,now);
    integer(ownAnim+0xc8,42032100);
    CHECK(!RipostePoll(me,now+100));
    CHECK(RipostePoll(me,now+112)&&nativeCalls==3&&calls==0);
    CHECK(!RiposteTrack[1].spent&&RiposteTrack[1].criticalState==RIPOSTE_CRITICAL_WAIT);
    CHECK(RipostePoll(me,now+116)&&nativeCalls==3&&calls==0);
    integer(ownAnim+0xc8,23030201);integer(targetAnim+0xc8,70030);
    CHECK(RipostePoll(me,now+300)&&RiposteTrack[1].spent&&nativeCalls==3);
    CHECK(RiposteTrack[1].criticalState==RIPOSTE_CRITICAL_CONFIRMED);

    CopydogRiposteSubmitted(&RiposteTrack[1],(uintptr_t)me,(uintptr_t)you,0,25034000,2000);
    integer(ownAnim+0xc8,42032100);integer(targetAnim+0xc8,6000);nativeResult=-1;
    CHECK(!RipostePoll(me,2100));
    CHECK(RipostePoll(me,2112)&&nativeCalls==4&&!RiposteTrack[1].spent);
    CHECK(RiposteTrack[1].criticalState==RIPOSTE_CRITICAL_FAILED);
    CHECK(!RipostePoll(me,2124)&&nativeCalls==4&&calls==0);
    /* Damage during this request may still be a real partial parry. Observe
       actual own parry + fresh target stagger, but wait through hurt recovery. */
    nativeResult=FRONTAL_THROW_ID;
    integer(stats+0xd8,100);integer(ownAnim+0xc8,42032100);integer(targetAnim+0xc8,25034000);
    Defense=(COPYDOG_DEFENSE){.self=(uintptr_t)me,.target=(uintptr_t)you,
        .phase=DEF_ACTIVE,.kind=DEF_PARRY,.hp=100,.submitted=3000};
    CopydogRiposteSubmitted(&RiposteTrack[1],(uintptr_t)me,(uintptr_t)you,0,25034000,3000);
    CHECK(!RipostePoll(me,3100));
    RiposteTrack[2]=(COPYDOG_RIPOSTE_TRACK){.self=(uintptr_t)me,.actor=(uintptr_t)me+16,.pending=1,.staggered=1};
    integer(stats+0xd8,80);integer(targetAnim+0xc8,6000);UpdateDefenseState(3110);
    CHECK(Defense.phase==DEF_CANCELLED&&!RiposteTrack[1].suppressed);
    CHECK(RiposteTrack[2].suppressed&&!RiposteTrack[2].pending);
    CHECK(!RipostePoll(me,3110));
    CHECK(!RipostePoll(me,3122)&&RiposteTrack[1].pending&&RiposteTrack[1].staggered);
    CHECK(nativeCalls==4&&calls==0);
    integer(ownAnim+0xc8,0);UpdateDefenseState(3400);UpdateDefenseState(3412);
    CHECK(Defense.phase==DEF_IDLE);
    CHECK(RipostePoll(me,3412)&&nativeCalls==5&&!RiposteTrack[1].spent);
    integer(ownAnim+0xc8,23030202);integer(targetAnim+0xc8,70030);
    CHECK(RipostePoll(me,3500)&&RiposteTrack[1].spent&&calls==0);
    integer(ownAnim+0xc8,0);integer(targetAnim+0xc8,6000);nativeResult=COPYDOG_HORNET_FRONTAL_THROW_ID;
    for(unsigned i=0;i<4;i++){
        memset(equipment+COPYDOG_EQUIPPED_RINGS_OFFSET,0,16);
        integer(equipment+COPYDOG_EQUIPPED_RINGS_OFFSET+i*4,COPYDOG_HORNET_RING_ID);
        CHECK(CopydogSelectFrontalThrow(me)==COPYDOG_HORNET_FRONTAL_THROW_ID);
        CHECK(TryRequestFrontalRiposte(me,you)==COPYDOG_HORNET_FRONTAL_THROW_ID&&nativeType==COPYDOG_HORNET_FRONTAL_THROW_ID);
    }
    memset(equipment+COPYDOG_EQUIPPED_RINGS_OFFSET,0,16);
    CHECK(CopydogSelectFrontalThrow(me)==FRONTAL_THROW_ID);
    CHECK(!RejectQueueBeforeFacing(me,you,ownAnim,targetAnim,physics));
    pointer(me+XB,0);CHECK(TryRequestFrontalRiposte(me,you)==FRONTAL_THROW_EQUIPMENT_UNKNOWN);
    VirtualFree(worldGlobals,0,MEM_RELEASE);VirtualFree(throwGlobals,0,MEM_RELEASE);
    printf("PASS %u native frontal riposte assertions\n",checks);return 0;
}
