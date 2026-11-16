#ifndef COPYDOG_PROJECTILE_SET_H
#define COPYDOG_PROJECTILE_SET_H
#include <math.h>
#include <stdint.h>
#include <string.h>

enum { COPYDOG_PROJECTILE_CAPACITY=32, COPYDOG_PROJECTILE_TTL_MS=128,
       COPYDOG_PROJECTILE_HORIZON_MS=500, COPYDOG_PROJECTILE_LATE_MS=12 };
typedef struct {uintptr_t object; int owner,id;} COPYDOG_PROJECTILE_KEY;
enum { CD_PROJ_EMPTY,CD_PROJ_ACTIVE,CD_PROJ_MISS,CD_PROJ_SPENT };
typedef struct {
    COPYDOG_PROJECTILE_KEY key;
    uint32_t seen,contact;
    float yaw;
    unsigned state;
    uint32_t serial;
} COPYDOG_PROJECTILE_ENTRY;
typedef struct { COPYDOG_PROJECTILE_ENTRY slots[COPYDOG_PROJECTILE_CAPACITY]; uint32_t nextSerial; } COPYDOG_PROJECTILE_SET;

static inline void CopydogProjectileSetClear(COPYDOG_PROJECTILE_SET *s)
{memset(s,0,sizeof *s);}

/* A callback may publish just after the poll took its timestamp. Such a
   sample is fresh but must not be consumed until the poll clock catches up. */
static inline int CopydogProjectileSampleAge(uint32_t now,uint32_t seen)
{
    uint32_t age=now-seen;
    if(age<=COPYDOG_PROJECTILE_TTL_MS)return (int)age;
    if(seen-now<=COPYDOG_PROJECTILE_TTL_MS)return -1;
    return COPYDOG_PROJECTILE_TTL_MS+1;
}

static inline int CopydogProjectileKeyBefore(COPYDOG_PROJECTILE_KEY a,COPYDOG_PROJECTILE_KEY b)
{
    if(a.owner!=b.owner)return a.owner<b.owner;
    if(a.id!=b.id)return a.id<b.id;
    return a.object<b.object;
}

/* All contacts compared here are within a bounded TTL + horizon; the signed
   tick difference handles the 32-bit timer wrapping through zero. */
static inline int CopydogProjectileBefore(const COPYDOG_PROJECTILE_ENTRY *a,const COPYDOG_PROJECTILE_ENTRY *b)
{
    int32_t dt=(int32_t)(a->contact-b->contact);
    return dt<0||(!dt&&CopydogProjectileKeyBefore(a->key,b->key));
}

static inline int CopydogProjectileSetUpdate(COPYDOG_PROJECTILE_SET *s,COPYDOG_PROJECTILE_KEY key,
    uint32_t now,int intersects,unsigned etaMs,float yaw)
{
    if(!s||!key.object||key.id<0||key.owner<0||
       (intersects&&(etaMs>COPYDOG_PROJECTILE_HORIZON_MS||!isfinite(yaw))))return 0;
    COPYDOG_PROJECTILE_ENTRY *slot=NULL,*freeSlot=NULL,*replaceable=NULL,*last=NULL;
    for(unsigned i=0;i<COPYDOG_PROJECTILE_CAPACITY;i++){
        COPYDOG_PROJECTILE_ENTRY *e=&s->slots[i];
        int age=e->state?CopydogProjectileSampleAge(now,e->seen):0;
        if(age>COPYDOG_PROJECTILE_TTL_MS)e->state=CD_PROJ_EMPTY;
        if(!e->state){if(!freeSlot)freeSlot=e;continue;}
        if(e->key.object==key.object){
            if(age<0)return 0;
            slot=e;break;
        }
        if(e->state==CD_PROJ_MISS||
           (e->state==CD_PROJ_ACTIVE&&(int32_t)(now-e->contact)>COPYDOG_PROJECTILE_LATE_MS)){
            if(!replaceable)replaceable=e;
        }else if(e->state==CD_PROJ_ACTIVE&&(!last||CopydogProjectileBefore(last,e)))last=e;
    }
    COPYDOG_PROJECTILE_ENTRY incoming={.key=key,.seen=now,.contact=now+etaMs,.yaw=yaw,.state=CD_PROJ_ACTIVE};
    if(slot){
        if(slot->key.owner==key.owner&&slot->key.id==key.id){
            incoming.serial=slot->serial;
            /* Keep a consumed object muted even if it turns away and then
               back. Only disappearance beyond TTL or a new identity rearms. */
            if(slot->state==CD_PROJ_SPENT){slot->seen=now;return 0;}
            if(!intersects){slot->seen=now;slot->state=CD_PROJ_MISS;return 0;}
        }else if(!intersects){*slot=incoming;slot->state=CD_PROJ_MISS;return 0;}
    }else{
        if(!intersects)return 0;
        slot=freeSlot?freeSlot:replaceable;
        if(!slot){
            if(!last||!CopydogProjectileBefore(&incoming,last))return 0;
            slot=last;
        }
    }
    if(!incoming.serial){if(!++s->nextSerial)++s->nextSerial;incoming.serial=s->nextSerial;}
    *slot=incoming;return 1;
}

static inline COPYDOG_PROJECTILE_ENTRY *CopydogProjectileSetSelect(COPYDOG_PROJECTILE_SET *s,uint32_t now,unsigned leadMs)
{
    if(!s)return 0;
    if(leadMs>COPYDOG_PROJECTILE_HORIZON_MS)leadMs=COPYDOG_PROJECTILE_HORIZON_MS;
    COPYDOG_PROJECTILE_ENTRY *best=NULL;
    for(unsigned i=0;i<COPYDOG_PROJECTILE_CAPACITY;i++){
        COPYDOG_PROJECTILE_ENTRY *e=&s->slots[i];
        if(!e->state)continue;
        int age=CopydogProjectileSampleAge(now,e->seen);
        if(age>COPYDOG_PROJECTILE_TTL_MS){e->state=CD_PROJ_EMPTY;continue;}
        if(age<0||e->state!=CD_PROJ_ACTIVE)continue;
        int32_t remaining=(int32_t)(e->contact-now);
        if(remaining < -COPYDOG_PROJECTILE_LATE_MS || remaining>(int)leadMs)continue;
        if(!best||CopydogProjectileBefore(e,best))best=e;
    }
    return best;
}
static inline int CopydogProjectileSetDue(COPYDOG_PROJECTILE_SET *s,uint32_t now,int consume,unsigned leadMs,float*yaw)
{
    COPYDOG_PROJECTILE_ENTRY*best=CopydogProjectileSetSelect(s,now,leadMs);
    if(!best)return 0;
    if(yaw)*yaw=best->yaw;
    if(consume)best->state=CD_PROJ_SPENT;
    return 1;
}

static inline int CopydogProjectileSetRead(COPYDOG_PROJECTILE_SET *s,uint32_t now,int consume,float *yaw)
{return CopydogProjectileSetDue(s,now,consume,COPYDOG_PROJECTILE_HORIZON_MS,yaw);}

/* A terminal state is stronger evidence than a miss: a new projectile with
   the same owner/type may immediately reuse this pool slot. A delayed retire
   from an older publication must not erase the newer object's observation. */
static inline int CopydogProjectileSetRetire(COPYDOG_PROJECTILE_SET *s,COPYDOG_PROJECTILE_KEY key,uint32_t now)
{
    if(!s||!key.object)return 0;
    for(unsigned i=0;i<COPYDOG_PROJECTILE_CAPACITY;i++){
        COPYDOG_PROJECTILE_ENTRY *e=&s->slots[i];
        if(e->state&&e->key.object==key.object&&e->key.owner==key.owner&&e->key.id==key.id&&
           (int32_t)(now-e->seen)>=0){e->state=CD_PROJ_EMPTY;return 1;}
    }
    return 0;
}
#endif
