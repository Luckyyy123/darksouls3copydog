#include "copydog.h"
#include "runtime.h"
#include "combat.h"
#include "control.h"
#include "projectile_geometry.h"
#include "projectile_motion.h"
#include "projectile_profiles.h"
#include "projectile_param_data.h"
#include "projectile_set.h"
#include "projectile_area_track.h"
#include "prediction.h"
#include "prediction_limits.h"
static _Atomic uint32_t ThreatGeneration=1;
/* Odd epochs mute publication; changing epoch also invalidates a sample
   that was being read while manual control took over. */
static _Atomic uint32_t BulletInputEpoch;
static SRWLOCK ThreatLock=SRWLOCK_INIT;
static COPYDOG_PROJECTILE_SET Threats;
static CD_AREA_HISTORY AreaHistory;
static _Atomic uint64_t BulletObservationOrder;
__declspec(dllexport) _Atomic uint64_t CopydogAreaDiagnostics[4],CopydogLastAreaThreat;
static COPYDOG_PLAYER_MOTION Motion;
static char *ThreatSelf;
#ifdef COPYDOG_COMBAT_TEST
extern void CombatTestBeforePublish(void);
#endif
static _Atomic uint64_t BulletSamples,BulletThreats,BulletRolls,HitChecks,HitSkips;
__declspec(dllexport) _Atomic uint64_t CopydogBulletDiagnostics[BD_COUNT];
__declspec(dllexport) _Atomic int CopydogBulletLastId,CopydogBulletLastOwner;
#define BULLET_DIAG(n) ((void)atomic_fetch_add_explicit(&CopydogBulletDiagnostics[n],1,memory_order_relaxed))
/* FieldInsHandle at +8 is the projectile owner key. +78 is a different ID. */
static char* ResolveBulletShooter(char*me,int owner,int*slot)
{
    int own;
    if(!me||!readlpv(own,me+8)||owner==own||owner==0||owner==-1)return 0;
    char*found=0;*slot=0;
    for(int i=1;i<=MAX_REMOTE_PLAYER;i++){
        char*actor=Player(i);int handle;
        if(!actor||actor==me||!readlpv(handle,actor+8)||handle!=owner)continue;
        if(found)return 0;
        found=actor;*slot=i;
    }
    return found;
}
static void BindThreatSelf(char *me)
{
    if(ThreatSelf==me)return;
    CopydogProjectileSetClear(&Threats);memset(&AreaHistory,0,sizeof AreaHistory);memset(&Motion,0,sizeof Motion);ThreatSelf=me;
}
void CombatSamplePlayerMotion(char *me,uint32_t now)
{
    float pos[3],*p=me?GetActorPos(me):0;
    int ok=p&&readlp(sizeof pos,pos,p);
    AcquireSRWLockExclusive(&ThreatLock);
    if(me==Player0){
        BindThreatSelf(me);
        CopydogMotionSample(&Motion,(uintptr_t)me,ok?pos:0,now);
    }
    ReleaseSRWLockExclusive(&ThreatLock);
}
void CombatObserveBullet(void*raw)
{
    BULLET_DIAG(BD_CALLBACKS);
    if(!CopydogEnabled||!Config.BulletDodge||exitThread)return;
    uint32_t generation=atomic_load_explicit(&ThreatGeneration,memory_order_acquire);
    uint32_t input=atomic_load_explicit(&BulletInputEpoch,memory_order_acquire);
    if(input&1u){BULLET_DIAG(BD_INPUT_PAUSED);return;}
    atomic_fetch_add_explicit(&BulletSamples,1,memory_order_relaxed);
    char*bullet=raw,*me=Player0,*shooter;
    int id,handle,state,shooterSlot=0;float position[3],velocity[3],player[3],playerV[3];
    uint64_t order=atomic_fetch_add_explicit(&BulletObservationOrder,1,memory_order_relaxed)+1;
    uint32_t at=timeGetTime();
    if(!bullet||!me||!readlpv(id,bullet+0x4e4)||!readlpv(handle,bullet+0x4ec)||
       !readlpv(state,bullet+0x4dc)){BULLET_DIAG(BD_INVALID);return;}
    atomic_store(&CopydogBulletLastId,id);atomic_store(&CopydogBulletLastOwner,handle);
    COPYDOG_PROJECTILE_KEY key={(uintptr_t)bullet,handle,id};
    int terminal=state<0||state>=4;
    AcquireSRWLockExclusive(&ThreatLock);
    int inOrder=me==Player0&&generation==atomic_load(&ThreatGeneration)&&input==atomic_load(&BulletInputEpoch);
    if(inOrder){
        BindThreatSelf(me);
        inOrder=CopydogAreaOrder(&AreaHistory,&Threats,key,at,order,terminal);
        if(inOrder&&terminal)CopydogProjectileSetRetire(&Threats,key,at);
    }
    ReleaseSRWLockExclusive(&ThreatLock);
    if(!inOrder||terminal)return;
    shooter=ResolveBulletShooter(me,handle,&shooterSlot);
    unsigned char ownType,targetType;
    if(!shooter||!readlpv(ownType,me+0x74)||!readlpv(targetType,shooter+0x74)||!(ownType>4||targetType>4)){
        BULLET_DIAG(BD_OWNER_REJECT);return;
    }
    float radius=CopydogProjectileBoundRadius(id);
    if(radius<0){
        BULLET_DIAG(BD_UNKNOWN_PROFILE);
        if(GetAttackBulletSpeed(id)<=0)return;
    }
    float*mePosition=GetActorPos(me);
    if(!mePosition||!readlp(sizeof position,position,bullet+0x10)||
       !readlp(sizeof velocity,velocity,bullet+0x30)||!readlp(sizeof player,player,mePosition))return;
    for(unsigned k=0;k<3;k++)if(!isfinite(position[k])||!isfinite(velocity[k])||!isfinite(player[k]))return;
    AcquireSRWLockShared(&ThreatLock);
    int haveMotion=CopydogMotionVelocity(&Motion,(uintptr_t)me,player,at,playerV);
    ReleaseSRWLockShared(&ThreatLock);
    double speed2=(double)velocity[0]*velocity[0]+(double)velocity[1]*velocity[1]+(double)velocity[2]*velocity[2];
    const CD_PROJECTILE_AREA*area=CopydogProjectileArea(id);
    int flying=!area||(area->role==CD_AREA_FLIGHT&&area->directDamage);
    float yaw=0,facing=0;
    if(area||hypot((double)velocity[0],velocity[2])<=0.01){
        float*dir=GetActorDir(me);if(dir)(void)readlpv(facing,dir+1);
    }
    BULLET_DIAG(BD_GEOMETRY);
    double t=-1,combined=radius>=0?COPYDOG_PROJECTILE_BODY_RADIUS+radius:1.0;
    int contact=0;
    if(flying&&speed2>1e-7){
        contact=CopydogProjectileContact(position,velocity,player,playerV,
            COPYDOG_PROJECTILE_BODY_LOW_Y,COPYDOG_PROJECTILE_BODY_HIGH_Y,combined,
            CopydogVerifiedBoltRadius(id)>=0||radius<0?COPYDOG_PROJECTILE_HORIZON:CD_PRED_HORIZON_MS/1000.,&t);
        double bodyContact;
        int bodyPrediction=CopydogPredictionBullet((uintptr_t)me,position,velocity,radius,at,&bodyContact);
        if(bodyPrediction>=CD_PRED_CLEAR){
            BULLET_DIAG(BD_FULL_BODY);contact=bodyPrediction>0;t=contact?bodyContact/1000.:-1;
        }
        if(contact)yaw=CopydogProjectileEscapeYaw(position,velocity,player,playerV,t,facing);
    }
#ifdef COPYDOG_COMBAT_TEST
    if(contact)CombatTestBeforePublish();
#endif
    AcquireSRWLockExclusive(&ThreatLock);
    if(CopydogEnabled&&!exitThread&&me==Player0&&Player(shooterSlot)==shooter&&readlpt(int,shooter+8)==handle&&
       input==atomic_load(&BulletInputEpoch)&&generation==atomic_load(&ThreatGeneration)&&
       CopydogAreaOrder(&AreaHistory,&Threats,key,at,order,0)){
        BindThreatSelf(me);
        int source=CD_AREA_NONE;
        if(area){
            double areaTime=-1;float areaYaw=0;
            source=CopydogAreaObserve(&AreaHistory,area,key,(uintptr_t)shooter,position,velocity,player,playerV,
                                      at,order,haveMotion,facing,&areaTime,&areaYaw);
            if(source&&(!contact||areaTime<=t)){contact=1;t=areaTime;yaw=areaYaw;}
            else source=CD_AREA_NONE;
            CD_AREA_OBJECT*o=CopydogAreaFind(&AreaHistory,key);
            if(o)o->source=source;
        }
        if(contact)BULLET_DIAG(BD_CONTACT);
        int active=CopydogProjectileSetUpdate(&Threats,key,at,contact,contact?(unsigned)ceil(t*1000.):0,yaw);
        if(active){
            atomic_fetch_add_explicit(&BulletThreats,1,memory_order_relaxed);BULLET_DIAG(BD_PUBLISHED);
            if(source){
                atomic_fetch_add_explicit(&CopydogAreaDiagnostics[source==CD_AREA_AGE_UNKNOWN?2:source-1],1,memory_order_relaxed);
                uint64_t detail=((uint64_t)(uint32_t)id<<32)|((uint32_t)source<<16)|(unsigned)ceil(t*1000.);
                atomic_store_explicit(&CopydogLastAreaThreat,detail,memory_order_relaxed);
            }
        }
    }
    ReleaseSRWLockExclusive(&ThreatLock);
}
int CombatSelectDueBullet(uint32_t now,unsigned lead,CD_BULLET_TICKET*out)
{
    if(!out)return 0;
    *out=(CD_BULLET_TICKET){0};
    AcquireSRWLockExclusive(&ThreatLock);
    BindThreatSelf(Player0);
    uint32_t input=atomic_load(&BulletInputEpoch);
    if(ThreatSelf&&CopydogEnabled&&Config.BulletDodge&&!exitThread&&!(input&1u)){
        for(;;){
            COPYDOG_PROJECTILE_ENTRY*e=CopydogProjectileSetSelect(&Threats,now,lead);
            if(!e)break;
            CD_AREA_OBJECT*o=CopydogAreaFind(&AreaHistory,e->key);
            if(o){
                int slot=0;
                if(o->spent||o->retired||ResolveBulletShooter(ThreatSelf,e->key.owner,&slot)!=(char*)o->shooter){
                    e->state=CD_PROJ_MISS;continue;
                }
            }
            *out=(CD_BULLET_TICKET){.object=e->key.object,.self=(uintptr_t)ThreatSelf,.owner=e->key.owner,
                .id=e->key.id,.serial=e->serial,.seen=e->seen,.contact=e->contact,.lead=lead,.input=input,
                .generation=atomic_load(&ThreatGeneration),.areaGroup=o?o->group:0,.areaSource=o?o->source:0,.yaw=e->yaw};
            break;
        }
    }
    ReleaseSRWLockExclusive(&ThreatLock);return out->object!=0;
}
int CombatBulletTicketCurrent(const CD_BULLET_TICKET*old,uint32_t now)
{
    CD_BULLET_TICKET latest;
    return old&&CombatSelectDueBullet(now,old->lead,&latest)&&latest.object==old->object&&
        latest.serial==old->serial&&latest.owner==old->owner&&latest.id==old->id&&latest.self==old->self&&
        latest.input==old->input&&latest.generation==old->generation&&latest.seen==old->seen&&
        latest.contact==old->contact&&latest.yaw==old->yaw;
}
void CombatConfirmBullet(const CD_BULLET_TICKET*ticket)
{
    AcquireSRWLockExclusive(&ThreatLock);
    if(ticket&&ticket->object&&ticket->self==(uintptr_t)ThreatSelf&&ticket->input==atomic_load(&BulletInputEpoch)&&
       ticket->generation==atomic_load(&ThreatGeneration)){
        for(unsigned i=0;i<COPYDOG_PROJECTILE_CAPACITY;i++){
            COPYDOG_PROJECTILE_ENTRY*e=&Threats.slots[i];
            if(e->state&&e->serial==ticket->serial&&e->key.object==ticket->object&&
               e->key.owner==ticket->owner&&e->key.id==ticket->id)e->state=CD_PROJ_SPENT;
            CD_AREA_OBJECT*o=&AreaHistory.objects[i];
            if(ticket->areaGroup&&o->present&&o->group==ticket->areaGroup)o->spent=1;
        }
        if(ticket->areaSource)atomic_fetch_add_explicit(&CopydogAreaDiagnostics[3],1,memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&ThreatLock);
}
static int CombatReadBulletThreat(uint32_t now,float*yaw,int consume,unsigned lead)
{
    CD_BULLET_TICKET selected;
    if(!CombatSelectDueBullet(now,lead,&selected))return 0;
    if(yaw)*yaw=selected.yaw;
    if(consume)CombatConfirmBullet(&selected);
    return 1;
}
int CombatPeekBulletThreat(uint32_t now){return CombatReadBulletThreat(now,0,0,COPYDOG_PROJECTILE_HORIZON_MS);}
int CombatTakeBulletThreatDirection(uint32_t now,float*yaw){return CombatReadBulletThreat(now,yaw,1,COPYDOG_PROJECTILE_HORIZON_MS);}
int CombatTakeDueBulletDirection(uint32_t now,unsigned lead,float*yaw){return CombatReadBulletThreat(now,yaw,1,lead);}
int CombatTakeBulletThreat(uint32_t now){return CombatTakeBulletThreatDirection(now,0);}
void CombatSetBulletInputPaused(int paused)
{
    unsigned want=paused!=0;
    if((atomic_load_explicit(&BulletInputEpoch,memory_order_acquire)&1u)==want)return;
    AcquireSRWLockExclusive(&ThreatLock);
    if((atomic_load_explicit(&BulletInputEpoch,memory_order_acquire)&1u)!=want){
        atomic_fetch_add_explicit(&BulletInputEpoch,1,memory_order_acq_rel);
        CopydogProjectileSetClear(&Threats);memset(&AreaHistory,0,sizeof AreaHistory);memset(&Motion,0,sizeof Motion);
    }
    ReleaseSRWLockExclusive(&ThreatLock);
}
void CombatDiscardPendingBullets(void)
{
    AcquireSRWLockExclusive(&ThreatLock);
    atomic_fetch_add(&BulletInputEpoch,2u);
    memset(&AreaHistory,0,sizeof AreaHistory);
    for(unsigned i=0;i<COPYDOG_PROJECTILE_CAPACITY;i++){
        if(Threats.slots[i].state==CD_PROJ_ACTIVE)Threats.slots[i].state=CD_PROJ_MISS;
    }
    ReleaseSRWLockExclusive(&ThreatLock);
}
void CombatClearBulletThreat(void)
{
    AcquireSRWLockExclusive(&ThreatLock);
    atomic_fetch_add_explicit(&ThreatGeneration,1,memory_order_acq_rel);
    CopydogProjectileSetClear(&Threats);memset(&AreaHistory,0,sizeof AreaHistory);memset(&Motion,0,sizeof Motion);ThreatSelf=0;
    ReleaseSRWLockExclusive(&ThreatLock);
}
void CombatRecordBulletRoll(void){atomic_fetch_add_explicit(&BulletRolls,1,memory_order_relaxed);}
int CombatShouldSkipHit(void*raw)
{
    if(!CopydogEnabled||!Config.HitGuard||exitThread)return 0;
    uint32_t generation=atomic_load_explicit(&ThreatGeneration,memory_order_acquire);
    atomic_fetch_add_explicit(&HitChecks,1,memory_order_relaxed);
    char*owner,*state,*me=Player0;uint64_t flags;
    if(!raw||!me||!readlpv(owner,(char*)raw+8)||owner!=me||
       !readlpv(state,owner+XA,0)||!state||!readlpv(flags,state+0x58))return 0;
    int protected=CombatHitProtected((uint32_t)flags,(uint32_t)(flags>>32),GetActorAnim(owner));
    if(protected&&CopydogEnabled&&!exitThread&&generation==atomic_load_explicit(&ThreatGeneration,memory_order_acquire)){atomic_fetch_add_explicit(&HitSkips,1,memory_order_relaxed);return 1;}
    return 0;
}
void CombatReadCounters(COPYDOG_COMBAT_COUNTERS*out)
{
    if(!out)return;
    out->bullet_samples=atomic_load_explicit(&BulletSamples,memory_order_relaxed);
    out->bullet_threats=atomic_load_explicit(&BulletThreats,memory_order_relaxed);
    out->bullet_rolls=atomic_load_explicit(&BulletRolls,memory_order_relaxed);
    out->hit_checks=atomic_load_explicit(&HitChecks,memory_order_relaxed);
    out->hit_skips=atomic_load_explicit(&HitSkips,memory_order_relaxed);
}
DWORD WINAPI CopydogGetCombatCounters(void*destination)
{
    COPYDOG_COMBAT_COUNTERS c;CombatReadCounters(&c);SIZE_T written;
    return destination&&WriteProcessMemory(GetCurrentProcess(),destination,&c,sizeof c,&written)&&written==sizeof c?0:ERROR_INVALID_PARAMETER;
}
