#ifndef COPYDOG_DYNAMIC_PARRY_H
#define COPYDOG_DYNAMIC_PARRY_H
#include <math.h>
#include "prediction.h"
#include "prediction_limits.h"

enum CD_PARRY_FRAME_DECISION { CD_FRAME_SKIP, CD_FRAME_WAIT, CD_FRAME_PRESS };
enum {
    CD_PARRY_CONTACT_PROOF=256u, CD_PARRY_COMPLETE_CLEAR=4096u, CD_PARRY_POSSIBLE_CONTACT=8192u,
    CD_PARRY_PARTIAL_CLEAR=524288u, CD_PARRY_UNKNOWN_GEOMETRY=1048576u,
    CD_PARRY_UNKNOWN_REASON_SHIFT=21u
};
/* A contact belongs to the selected band and this native observation. Other
   trace bits describe the clock, origin and motion; they cannot establish contact. */
static inline int CopydogParryContactAuthorizes(unsigned source)
{
    const unsigned geometry=CD_PARRY_CONTACT_PROOF|CD_PARRY_COMPLETE_CLEAR|CD_PARRY_POSSIBLE_CONTACT|
                            CD_PARRY_PARTIAL_CLEAR|CD_PARRY_UNKNOWN_GEOMETRY;
    return (source&geometry)==CD_PARRY_CONTACT_PROOF;
}
enum { CD_FRAME_MAX_INTERVAL_MS=100, CD_FRAME_CLOCK_RESET_MS=250,
       CD_FRAME_BOOTSTRAP_STEPS=2, CD_FRAME_DELAY_PAD_MS=8 };
#define CD_GAME_PLAY_MS_OFFSET 0xA4u
#define CD_GAME_PLAY_MS_LIMIT UINT32_C(3599999000)
typedef struct {
    uintptr_t owner;
    uint32_t value;
    double tick,wallStep;
    int running;
} CD_GAME_CADENCE;
/* GameFlowInGame_MoveMap_PostPhysics_1 publishes cumulative play milliseconds.
   Time jumps, saturation, and owner changes cannot identify adjacent updates. */
static inline void CopydogObserveCadence(CD_GAME_CADENCE*c,uintptr_t owner,uint32_t value,double now)
{
    double elapsed=now-c->tick;
    if(!owner||owner!=c->owner||value<c->value||value>=CD_GAME_PLAY_MS_LIMIT||
       value-c->value>CD_FRAME_CLOCK_RESET_MS||!isfinite(elapsed)||elapsed<0||elapsed>CD_FRAME_CLOCK_RESET_MS){
        *c=(CD_GAME_CADENCE){.owner=owner,.value=value,.tick=now};
    }else if(value!=c->value){
        c->value=value;c->tick=now;c->wallStep=elapsed;
        c->running=elapsed>=1.&&elapsed<=CD_FRAME_MAX_INTERVAL_MS;
    }
}
typedef struct {
    CD_NATIVE_FRAME last;
    uint32_t tick;
    double rate,step;
    unsigned valid,skipped;
} CD_FRAME_CLOCK;

static inline int CopydogSameFrame(const CD_NATIVE_FRAME*a,const CD_NATIVE_FRAME*b)
{
    return a->clock==b->clock&&a->index==b->index&&a->animation==b->animation&&
        a->current==b->current&&a->previous==b->previous&&a->duration==b->duration;
}

/* A duplicate poll must not move the wall-time origin of a native sample. */
static inline int CopydogAdvanceFrame(CD_FRAME_CLOCK*c,const CD_NATIVE_FRAME*f,uint32_t now)
{
    if(c->valid&&c->last.clock==f->clock&&c->last.animation==f->animation&&
       c->last.previous==f->previous&&c->last.current==f->current&&c->last.duration==f->duration){
        c->last.index=f->index;return 0;
    }
    double nativeStep=1000.*((double)f->current-f->previous);
    uint32_t dt=now-c->tick;
    int continuous=c->valid&&c->last.clock==f->clock&&c->last.animation==f->animation&&
        f->duration==c->last.duration&&f->current>c->last.current&&dt&&dt<=CD_FRAME_CLOCK_RESET_MS;
    double rate=1,step=nativeStep;
    c->skipped=0;
    if(continuous){
        unsigned advance=(unsigned)(f->index-c->last.index+CD_PRED_CLOCK_COUNT)%CD_PRED_CLOCK_COUNT;
        double elapsed=1000.*((double)f->current-c->last.current);
        /* Only the modulo advance is observable. Extra full laps are unknown. */
        if(!advance)advance=CD_PRED_CLOCK_COUNT;
        c->skipped=advance-1;
        double measured=elapsed/dt;
        rate=c->rate;
        if(measured>=CD_PRED_MIN_RATE&&measured<=CD_PRED_MAX_RATE)
            rate=.5*(c->rate+measured);
        step=nativeStep/rate;
    }
    c->last=*f;c->tick=now;c->rate=rate;c->valid=1;
    c->step=step>=1&&step<=CD_FRAME_MAX_INTERVAL_MS?step:0;
    return 1;
}
/* Contact and activation bounds are relative to the current observation.
   Request delay d must cover the contact band for every q in [q0,q1]. */
static inline int CopydogDynamicParryWindow(double c0,double c1,double s0,double s1,
    double q0,double q1,double*outLo,double*outHi,double*preferred)
{
    if(!outLo||!outHi||!preferred)return 0;
    *outLo=*outHi=*preferred=0;
    if(!isfinite(c0)||!isfinite(c1)||!isfinite(s0)||!isfinite(s1)||!isfinite(q0)||!isfinite(q1)||
       c0>c1||s0<0||s0>=s1||q0<0||q0>q1)return 0;
    double lo=c1-q0-s1,hi=c0-q1-s0;
    if(lo>hi)return 0;
    *outLo=lo;*outHi=hi;*preferred=(lo+hi)*.5;return 1;
}

/* The active interval ends before s1. Select the available observation with
   the larger worst-case margin, counting both extremes of dispatch latency. */
static inline enum CD_PARRY_FRAME_DECISION CopydogParryFrameChoiceBounds(double lo,double hi,
    double next,int includeLower)
{
    if(!isfinite(lo)||!isfinite(hi)||lo>hi||(lo==hi&&!includeLower)||hi<0)return CD_FRAME_SKIP;
    if(lo>0||(lo==0&&!includeLower)||!isfinite(next)||next<=0)return CD_FRAME_WAIT;
    if(next>hi)return CD_FRAME_PRESS;
    double nowMargin=fmin(-lo,hi),nextMargin=fmin(next-lo,hi-next);
    return nowMargin>=nextMargin?CD_FRAME_PRESS:CD_FRAME_WAIT;
}
static inline enum CD_PARRY_FRAME_DECISION CopydogParryFrameChoice(double lo,double hi,double next)
{return CopydogParryFrameChoiceBounds(lo,hi,next,0);}
#endif
