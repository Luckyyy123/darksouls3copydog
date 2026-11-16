#ifndef COPYDOG_NATIVE_PARRY_CUE_H
#define COPYDOG_NATIVE_PARRY_CUE_H
#include "dynamic_parry.h"
#include "early_clock.h"

enum CD_PARRY_CUE_STAGE { CD_CUE_IDLE, CD_CUE_SAMPLE, CD_CUE_CONFIRMED };
#define CD_PARRY_TRACE_NATIVE_CUE 131072u
#define CD_PARRY_TRACE_BEFORE_VISIBLE 262144u
typedef struct {
    uintptr_t actor;
    int handle,weapon,mode,expected;
    uint32_t onset,tick;
    CD_NATIVE_FRAME last;
    enum CD_PARRY_CUE_STAGE stage;
    unsigned seen,blocked,spent,readable;
} CD_PARRY_CUE;

static inline void CopydogParryCueCancel(CD_PARRY_CUE*c)
{
    c->blocked=1;c->stage=CD_CUE_IDLE;c->readable=0;
}

/* The observation timestamp belongs to an advancing sample. Repeated polls
   must not renew its freshness or promote one published pose into two. */
static inline enum CD_PARRY_CUE_STAGE CopydogParryCueUpdate(CD_PARRY_CUE*c,
    uintptr_t actor,int handle,int weapon,int mode,int expected,uint32_t onset,
    const CD_NATIVE_FRAME*f,int retained,uint32_t now)
{
    if(!actor||!handle||weapon<=0||mode<1||mode>3||expected<=0){
        *c=(CD_PARRY_CUE){0};return CD_CUE_IDLE;
    }
    if(c->actor!=actor||c->handle!=handle||c->onset!=onset){
        *c=(CD_PARRY_CUE){.actor=actor,.handle=handle,.weapon=weapon,.mode=mode,
            .expected=expected,.onset=onset};
    }else if(c->weapon!=weapon||c->mode!=mode||c->expected!=expected){
        CopydogParryCueCancel(c);
    }
    c->readable=0;
    if(c->blocked)return CD_CUE_IDLE;
    if(!f)return CD_CUE_IDLE;
    if(f->animation!=expected||!f->clock||f->index<0||f->index>=CD_PRED_CLOCK_COUNT||
       !isfinite(f->previous)||!isfinite(f->current)||!isfinite(f->duration)||
       f->previous<0||f->current<f->previous||f->current>f->duration||f->duration<=0){
        if(c->seen)CopydogParryCueCancel(c);
        return CD_CUE_IDLE;
    }
    uint32_t age=now-c->tick;
    if(c->seen){
        if(f->clock!=c->last.clock||f->duration!=c->last.duration||
           f->current<c->last.current||f->previous<c->last.previous){
            CopydogParryCueCancel(c);return CD_CUE_IDLE;
        }
        if(f->current==c->last.current){
            if(age>CD_PRED_MAX_AGE_MS||f->previous!=c->last.previous)return CD_CUE_IDLE;
            c->last.index=f->index;c->readable=1;return c->stage;
        }
    }
    if(retained||f->current<=f->previous)return CD_CUE_IDLE;
    if(!c->seen){
        if(f->previous*1000.>(double)(now-onset)*CD_PRED_MAX_RATE+CD_PRED_MAX_AGE_MS)
            return CD_CUE_IDLE;
        c->stage=CD_CUE_SAMPLE;
    }else{
        double advance=((double)f->current-c->last.current)*1000.;
        if(!age||advance>CD_PRED_MAX_RATE*age||advance<CD_PRED_MIN_RATE*age||
           age>CD_PRED_MAX_AGE_MS)c->stage=CD_CUE_SAMPLE;
        else if(now-onset>=COPYDOG_EARLY_STABLE_MS)c->stage=CD_CUE_CONFIRMED;
    }
    c->last=*f;c->tick=now;c->seen=1;c->readable=1;return c->stage;
}
#endif
