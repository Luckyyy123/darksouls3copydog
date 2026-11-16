#ifndef COPYDOG_PROJECTILE_AREA_TRACK_H
#define COPYDOG_PROJECTILE_AREA_TRACK_H
#include <stddef.h>
#include <math.h>
#include "projectile_area_data.h"
#include "projectile_area_geometry.h"
#include "projectile_set.h"

enum CD_AREA_EVIDENCE { CD_AREA_NONE,CD_AREA_FOOT_PLANE,CD_AREA_FUSE,CD_AREA_OBSERVED,CD_AREA_AGE_UNKNOWN };
enum { CD_AREA_PAIR_MS=64 };
#define CD_AREA_POSITION_TOLERANCE 0.15
#define CD_AREA_FOOT_HORIZON_S 0.25
typedef struct {
    COPYDOG_PROJECTILE_KEY key;
    uintptr_t shooter;
    uint32_t born,seen,group,eventTick;
    uint64_t order;
    float pos[3],velocity[3],origin[3];
    unsigned present,retired,linked,spawnBound,stationary,spent,source,paired;
} CD_AREA_OBJECT;
typedef struct { COPYDOG_PROJECTILE_KEY key;uint32_t tick;uint64_t order; } CD_AREA_FENCE;
typedef struct {
    CD_AREA_OBJECT objects[COPYDOG_PROJECTILE_CAPACITY];
    CD_AREA_FENCE fences[2*COPYDOG_PROJECTILE_CAPACITY];
    uint32_t nextGroup;
} CD_AREA_HISTORY;

static inline int CopydogAreaSameKey(COPYDOG_PROJECTILE_KEY a,COPYDOG_PROJECTILE_KEY b)
{ return a.object==b.object&&a.owner==b.owner&&a.id==b.id; }

static inline CD_AREA_OBJECT *CopydogAreaFind(CD_AREA_HISTORY*h,COPYDOG_PROJECTILE_KEY key)
{
    for(unsigned i=0;i<COPYDOG_PROJECTILE_CAPACITY;i++)
        if(h->objects[i].present&&CopydogAreaSameKey(h->objects[i].key,key))return &h->objects[i];
    return NULL;
}
/* The callback's sequence resolves equal-millisecond observations. A terminal
   event fences earlier in-flight work without replacing the parent's last pose. */
static inline int CopydogAreaOrder(CD_AREA_HISTORY*h,COPYDOG_PROJECTILE_SET*threats,
    COPYDOG_PROJECTILE_KEY key,uint32_t now,uint64_t order,int terminal)
{
    CD_AREA_FENCE*fence=NULL,*available=NULL,*carrier=NULL;
    for(unsigned i=0;i<sizeof h->fences/sizeof h->fences[0];i++){
        CD_AREA_FENCE*f=&h->fences[i];
        if(f->key.object==key.object){fence=f;break;}
        if(!available&&(!f->key.object||(int32_t)(now-f->tick)>COPYDOG_PROJECTILE_TTL_MS))available=f;
        if(!available&&f->key.object){
            const CD_PROJECTILE_AREA*d=CopydogProjectileArea(f->key.id);
            if(d&&d->role==CD_AREA_FLIGHT&&(!carrier||(int32_t)(carrier->tick-f->tick)>0))carrier=f;
        }
    }
    if(fence){
        if(order<fence->order||(int32_t)(now-fence->tick)<0)return 0;
        if(terminal&&!CopydogAreaSameKey(fence->key,key))return 0;
    }else if(CopydogProjectileArea(key.id)){
        const CD_PROJECTILE_AREA*d=CopydogProjectileArea(key.id);
        fence=available?available:(d->role!=CD_AREA_FLIGHT?carrier:NULL);
        if(!fence)return 0;
    }
    if(!fence||fence->key.object!=key.object){
        for(unsigned i=0;i<COPYDOG_PROJECTILE_CAPACITY;i++){
            const CD_AREA_OBJECT*o=&h->objects[i];
            if(o->present&&o->key.object==key.object&&
               (order<o->order||(int32_t)(now-o->eventTick)<0))return 0;
        }
    }
    if(fence)*fence=(CD_AREA_FENCE){key,now,order};
    for(unsigned i=0;i<COPYDOG_PROJECTILE_CAPACITY;i++){
        CD_AREA_OBJECT*o=&h->objects[i];
        if(!o->present||o->key.object!=key.object)continue;
        int same=CopydogAreaSameKey(o->key,key);
        if(terminal&&!same)continue;
        o->eventTick=now;o->order=order;
        if(!same||terminal){
            o->retired=1;
            CopydogProjectileSetRetire(threats,o->key,now);
        }
    }
    return 1;
}
static inline double CopydogAreaDistance2(const float a[3],const float b[3])
{
    double x=(double)a[0]-b[0],y=(double)a[1]-b[1],z=(double)a[2]-b[2];
    return x*x+y*y+z*z;
}
/* Only an observed successor can be associated. A disappearing parent alone
   says nothing about impact with the map, a wall, another actor or expiry. */
static inline CD_AREA_OBJECT *CopydogAreaParent(CD_AREA_HISTORY*h,int child,uintptr_t shooter,
    int owner,const float p[3],uint32_t now)
{
    CD_AREA_OBJECT*match=NULL;
    for(unsigned i=0;i<COPYDOG_PROJECTILE_CAPACITY;i++){
        CD_AREA_OBJECT*o=&h->objects[i];uint32_t dt=now-o->seen;
        if(!o->present||o->linked||o->shooter!=shooter||o->key.owner!=owner||dt>CD_AREA_PAIR_MS)continue;
        const CD_PROJECTILE_AREA*data=CopydogProjectileArea(o->key.id);
        if(!data||data->child!=child||!o->paired||(data->role==CD_AREA_ANCHOR&&!o->stationary))continue;
        double dot=0,length2=0,delta[3],offset[3];
        for(unsigned k=0;k<3;k++){
            offset[k]=(double)p[k]-o->pos[k];delta[k]=(double)o->velocity[k]*dt*.001;
            dot+=offset[k]*delta[k];length2+=delta[k]*delta[k];
        }
        double u=length2>0?fmax(0,fmin(1,dot/length2)):0,dist2=0;
        for(unsigned k=0;k<3;k++){double d=offset[k]-u*delta[k];dist2+=d*d;}
        double limit=CD_AREA_POSITION_TOLERANCE+data->initialRadius;
        if(dist2>limit*limit)continue;
        if(match)return NULL;
        match=o;
    }
    return match;
}
static inline int CopydogAreaSpentAlive(const CD_AREA_OBJECT*o,uint32_t now)
{
    if(!o->spent||o->retired)return 0;
    const CD_PROJECTILE_AREA*d=CopydogProjectileArea(o->key.id);
    return d&&(double)(uint32_t)(now-o->born)<d->lifeMs;
}
static inline CD_AREA_OBJECT *CopydogAreaNew(CD_AREA_HISTORY*h,uint32_t now,int role)
{
    CD_AREA_OBJECT*oldest=NULL,*carrier=NULL;
    for(unsigned i=0;i<COPYDOG_PROJECTILE_CAPACITY;i++){
        CD_AREA_OBJECT*o=&h->objects[i];
        if(!o->present)return o;
        if(role!=CD_AREA_FLIGHT&&!CopydogAreaSpentAlive(o,now)){
            const CD_PROJECTILE_AREA*d=CopydogProjectileArea(o->key.id);
            if(d&&d->role==CD_AREA_FLIGHT&&(!carrier||(int32_t)(carrier->seen-o->seen)>0))carrier=o;
        }
        if((int32_t)(now-o->seen)>(int)COPYDOG_PROJECTILE_TTL_MS&&!CopydogAreaSpentAlive(o,now)&&
           (!oldest||(int32_t)(oldest->seen-o->seen)>0))oldest=o;
    }
    return oldest?oldest:carrier; /* An observed blast has priority over a speculative carrier. */
}
static inline void CopydogAreaShift(const float p[3],const float v[3],double seconds,float out[3])
{
    for(unsigned k=0;k<3;k++)out[k]=(float)(p[k]+(double)v[k]*seconds);
}
static inline int CopydogAreaActiveContact(const CD_PROJECTILE_AREA*d,const float center[3],
    const float player[3],const float v[3],double age,double horizon,double*eta)
{
    double life=d->lifeMs*.001,delay=d->expansionDelayMs*.001;
    if(age>=life||horizon<0)return 0;
    if(age<delay){
        double fixed=fmin(delay-age,horizon);
        if(CopydogAreaContact(center,player,v,d->initialRadius,d->initialRadius,0,age,life,fixed,eta))return 1;
        if(delay>=life||fixed>=horizon)return 0;
        float next[3];CopydogAreaShift(player,v,fixed,next);
        if(!CopydogAreaContact(center,next,v,d->initialRadius,d->maxRadius,d->spreadMs*.001,
                               0,life-delay,horizon-fixed,eta))return 0;
        *eta+=fixed;return 1;
    }
    return CopydogAreaContact(center,player,v,d->initialRadius,d->maxRadius,d->spreadMs*.001,
                              age-delay,life-delay,horizon,eta);
}

/* The caller serializes updates with the ordinary threat set. The floor
   estimate is conditional on a local level surface and a descending bolt. */
static inline int CopydogAreaObserve(CD_AREA_HISTORY*h,const CD_PROJECTILE_AREA*d,COPYDOG_PROJECTILE_KEY key,
    uintptr_t shooter,const float p[3],const float velocity[3],const float player[3],const float v[3],
    uint32_t now,uint64_t order,int haveMotion,float facing,double*eta,float*yaw)
{
    *eta=-1;
    CD_AREA_OBJECT*o=CopydogAreaFind(h,key);
    if(o&&(int32_t)(now-o->seen)<0)return CD_AREA_NONE;
    int fresh=!o||o->retired||o->shooter!=shooter||
        (now-o->seen>COPYDOG_PROJECTILE_TTL_MS&&!CopydogAreaSpentAlive(o,now));
    if(fresh){
        CD_AREA_OBJECT*parent=CopydogAreaParent(h,d->id,shooter,key.owner,p,now);
        int bound=parent!=NULL,mute=bound&&d->role==CD_AREA_DAMAGE&&parent->spent;
        if(!o)o=CopydogAreaNew(h,now,d->role);
        if(!o)return CD_AREA_NONE;
        uint32_t group=0;
        if(parent){
            parent->linked=1;
            if(d->role==CD_AREA_DAMAGE)group=parent->group;
        }
        if(!group){if(!++h->nextGroup)++h->nextGroup;group=h->nextGroup;}
        *o=(CD_AREA_OBJECT){.key=key,.shooter=shooter,.born=now,.seen=now,.present=1,
                            .spawnBound=bound,.stationary=1,.spent=mute,.group=group};
        memcpy(o->origin,p,sizeof o->origin);
    }
    uint32_t dt=now-o->seen;
    if(!fresh&&dt&&dt<=CD_AREA_PAIR_MS){
        float projected[3];CopydogAreaShift(o->pos,o->velocity,dt*.001,projected);
        /* The old position and direction must actually predict the new position.
           A reversed track or one cached position does not establish a birth time. */
        o->paired=CopydogAreaDistance2(projected,p)<=CD_AREA_POSITION_TOLERANCE*CD_AREA_POSITION_TOLERANCE;
    }else if(fresh||dt>CD_AREA_PAIR_MS)o->paired=0;
    double previousY=o->pos[1],previousVY=o->velocity[1];
    memcpy(o->pos,p,sizeof o->pos);memcpy(o->velocity,velocity,sizeof o->velocity);o->seen=now;o->eventTick=now;o->order=order;
    o->source=CD_AREA_NONE;
    if(o->spent)return CD_AREA_NONE;
    const CD_PROJECTILE_AREA*blast=CopydogProjectileArea(d->terminal);
    if(!blast||blast->role!=CD_AREA_DAMAGE)return CD_AREA_NONE;
    float center[3];memcpy(center,p,sizeof center);
    double age=(uint32_t)(now-o->born)*.001,start=0,blastAge=0;
    int source=CD_AREA_OBSERVED;
    if(d->role==CD_AREA_FLIGHT){
        if(!haveMotion||d->child!=d->terminal||fresh||!o->paired||!dt||dt>CD_AREA_PAIR_MS||velocity[1]>=0||previousVY>=0||
           p[1]>=previousY||p[1]<player[1]||fabs(v[1])*CD_AREA_FOOT_HORIZON_S>CD_AREA_POSITION_TOLERANCE)
            return CD_AREA_NONE;
        double measured=(p[1]-previousY)*1000./dt;
        if(fabs(measured-velocity[1])>fmax(1.,.5*fabs(velocity[1])))return CD_AREA_NONE;
        start=((double)player[1]-p[1])/velocity[1];
        if(start<0||start>CD_AREA_FOOT_HORIZON_S)return CD_AREA_NONE;
        CopydogAreaShift(p,velocity,start,center);source=CD_AREA_FOOT_PLANE;
    }else if(d->role==CD_AREA_ANCHOR){
        if(CopydogAreaDistance2(o->origin,p)>CD_AREA_POSITION_TOLERANCE*CD_AREA_POSITION_TOLERANCE)
            o->stationary=0;
        if(!o->spawnBound||!o->stationary||!age)return CD_AREA_NONE;
        double until=d->activationDelayMs*.001-age;
        start=fmax(0,until);blastAge=fmax(0,-until);source=CD_AREA_FUSE;
    }else blastAge=age;
    double horizon=COPYDOG_PROJECTILE_HORIZON_MS*.001;
    if(start>horizon)return CD_AREA_NONE;
    float future[3];CopydogAreaShift(player,v,start,future);
    double contact;
    if(d->role==CD_AREA_DAMAGE&&!o->spawnBound&&blast->initialRadius!=blast->maxRadius){
        /* A live child first seen in progress can already have reached full
           radius. Its greatest possible age is bounded only by its lifetime. */
        if(!CopydogAreaContact(center,future,v,blast->maxRadius,blast->maxRadius,0,blastAge,
                               blast->lifeMs*.001,horizon-start,&contact))return CD_AREA_NONE;
        double inside;
        source=CopydogAreaContact(center,future,v,blast->initialRadius,blast->initialRadius,0,blastAge,
                                  blast->lifeMs*.001,0,&inside)?CD_AREA_OBSERVED:CD_AREA_AGE_UNKNOWN;
    }else if(!CopydogAreaActiveContact(blast,center,future,v,blastAge,horizon-start,&contact))return CD_AREA_NONE;
    *eta=start+contact;*yaw=CopydogAreaAwayYaw(center,player,v,*eta,facing);
    o->source=source;return source;
}
#endif
