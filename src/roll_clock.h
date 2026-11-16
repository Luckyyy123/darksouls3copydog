#ifndef COPYDOG_ROLL_CLOCK_H
#define COPYDOG_ROLL_CLOCK_H
#include "early_clock.h"
#include "parry.h"
#include "roll_observed_map.h"
#include "prediction.h"
#include "prediction_limits.h"
#include <math.h>
#include <limits.h>

enum { ROLL_CLOCK_NONE, ROLL_CLOCK_MAPPING, ROLL_CLOCK_ARCHIVE, ROLL_CLOCK_CURRENT_ID, ROLL_CLOCK_NATIVE_REPEAT };
enum { ROLL_REPEAT_NONE, ROLL_REPEAT_TRACK, ROLL_REPEAT_BLOCKED };
typedef struct { uintptr_t self,actor;int handle,network,weapon;unsigned mode; } COPYDOG_ROLL_BINDING;
typedef struct {
    COPYDOG_ROLL_BINDING key;
    CD_NATIVE_FRAME frame;
    uint32_t tick,previousTick,nameStarted;
    float previousPhase;
    double rate;
    unsigned valid,hasName,repeat,advances;
} COPYDOG_ROLL_NATIVE_CLOCK;

/* A rewind can identify a new swing with no new name edge. Only fresh,
   consecutive native movements measure the speed of that new swing. */
static inline int CopydogRollObserveNative(COPYDOG_ROLL_NATIVE_CLOCK*c,COPYDOG_ROLL_BINDING key,
    const CD_NATIVE_FRAME*f,int retained,int canRearm,const COPYDOG_EARLY_STATE*e,uint32_t now)
{
    if(e->initialized&&e->stable&&!e->candidateActive&&e->started!=c->nameStarted)
        c->hasName=c->repeat=0;
    int heldName=c->hasName&&e->initialized&&e->started==c->nameStarted&&
        e->active&&e->stable&&!e->suppressed&&!e->candidateActive;
    uint32_t dt=now-c->tick;
    if(!f||!key.self||!key.actor||!key.handle||key.weapon<=0||key.mode<1||key.mode>3){
        if(heldName&&!c->repeat&&dt>CD_PRED_MAX_AGE_MS)c->repeat=ROLL_REPEAT_BLOCKED;
        c->valid=c->advances=0;return 0;
    }
    int same=key.self==c->key.self&&key.actor==c->key.actor&&key.handle==c->key.handle&&key.network==c->key.network&&
        key.weapon==c->key.weapon&&key.mode==c->key.mode&&f->clock==c->frame.clock&&
        f->animation==c->frame.animation&&f->duration==c->frame.duration;
    if(!same&&(heldName||c->repeat))c->repeat=ROLL_REPEAT_BLOCKED;
    int duplicate=same&&f->previous==c->frame.previous&&f->current==c->frame.current;
    if(retained||duplicate){
        if(!duplicate||dt>CD_PRED_MAX_AGE_MS){
            if(heldName&&!c->repeat)c->repeat=ROLL_REPEAT_BLOCKED;
            c->valid=c->advances=0;
        }
        return 0;
    }
    int named=e->initialized&&e->actor==key.actor&&e->networkId==key.network&&e->mode==(int)key.mode&&
        e->weaponBase==CopydogEarlyBase(key.weapon)&&e->stable&&e->active&&!e->suppressed&&
        !e->candidateActive&&CopydogEarlyValidId(e->name,f->animation);
    int adjacent=same&&c->valid&&dt&&dt<=CD_PRED_MAX_GAP_MS;
    int rewound=same&&f->current+CD_NATIVE_REPEAT_REWIND_MIN_S<c->frame.current;
    int restart=rewound&&canRearm&&adjacent&&f->current<=CD_NATIVE_REPEAT_START_MAX_S;
    if(rewound){
        c->advances=0;
        if(heldName)c->repeat=restart&&named?ROLL_REPEAT_TRACK:ROLL_REPEAT_BLOCKED;
    }else if(c->repeat==ROLL_REPEAT_TRACK){
        double step=1000.*((double)f->current-c->frame.current);
        double rate=adjacent?step/dt:0.;
        if(fabs((double)f->previous-c->frame.current)<=1e-6&&
           rate>=CD_PRED_MIN_RATE&&rate<=CD_PRED_MAX_RATE){
            if(c->advances){
                c->rate=1000.*((double)f->current-c->previousPhase)/(uint32_t)(now-c->previousTick);
                c->advances=2;
            }else c->advances=1;
            c->previousTick=c->tick;c->previousPhase=c->frame.current;
        }else c->advances=0;
    }
    if(!c->repeat){c->hasName=(unsigned)named;c->nameStarted=e->started;}
    c->key=key;c->frame=*f;c->tick=now;c->valid=1;
    return restart;
}

/* TAE coordinates scale with the attack. Queue latency and defender iframes
   stay in wall time. Widen rounding at either end of the contact band. */
static inline int CopydogRollScaleInterval(int*first,int*last,double rate)
{
    double start=floor(*first/rate),end=ceil(*last/rate);
    if(!(start>=0.&&start<end&&end<=INT_MAX))return 0;
    *first=(int)start;*last=(int)end;return 1;
}

/* The ID selects a resource, never the time origin. If this swing already
   supplied an ID, prefer that over any prediction from previous swings. */
static inline int CopydogRollClockId(const COPYDOG_EARLY_STATE *e,COPYDOG_EARLY_PROFILES *profiles,
    const COPYDOG_PARRY_TRACK *track,uintptr_t self,uintptr_t actor,unsigned *source)
{
    *source=ROLL_CLOCK_NONE;
    if(!e->initialized||!e->active||!e->stable||e->candidateActive||e->suppressed||
       !actor||actor!=e->actor)return -1;
    if(e->paired){
        if(track->actor!=actor||track->self!=self||!track->stable||track->candidateActive||
           track->animation!=e->pairedId||track->animation==e->initialAnim||(int32_t)(track->started-e->started)<0||
           !CopydogEarlyValidId(e->name,track->animation))return -1;
        *source=ROLL_CLOCK_CURRENT_ID;
        return track->animation;
    }
    int id,lag;
    if(CopydogEarlyMapping(e,profiles,&id,&lag)){*source=ROLL_CLOCK_MAPPING;return id;}
    id=CopydogRollObservedId(e->weaponBase,e->mode,e->name);
    if(id<0)return -1;
    COPYDOG_EARLY_PROFILE *p=CopydogEarlyFindProfile(profiles,e->weaponBase,e->mode,e->name,0);
    if(p&&p->count&&p->expectedId!=id)return -1;
    *source=ROLL_CLOCK_ARCHIVE;
    return id;
}
#endif
