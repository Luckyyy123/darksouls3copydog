#ifndef COPYDOG_PHASE_TIMING_H
#define COPYDOG_PHASE_TIMING_H
#include <limits.h>
#include <stdint.h>
#include <string.h>

/* All intervals are on the action-name clock, not the later C8 ID clock.
   TAE/name alignment remains a candidate model; it is not a measured win rate. */
enum { COPYDOG_PHASE_MAX_WINDOWS=8, COPYDOG_PHASE_MAX_LAGS=5 };
typedef struct {
    int weaponBase,mode,animation;
    const char *name;
} COPYDOG_PHASE_KEY;
typedef struct {
    int startUs,endUs,judgeId,originId;
} COPYDOG_PHASE_INTERVAL;
typedef struct {
    COPYDOG_PHASE_KEY key;
    unsigned firstInterval,intervalCount,observations,lagCount;
    int lagMs[COPYDOG_PHASE_MAX_LAGS];
} COPYDOG_PHASE_TARGET;
typedef struct {
    COPYDOG_PHASE_KEY key;
    unsigned firstInterval,intervalCount,forcedCandidate;
} COPYDOG_PHASE_SELF;
enum {
    COPYDOG_PHASE_NATIVE_CANDIDATE=1,
    COPYDOG_PHASE_FORCED_UNCALIBRATED=2,
    COPYDOG_PHASE_FORCED_CALIBRATED_CANDIDATE=3,
    COPYDOG_PHASE_ONSET_ONLY=0x100
};
enum {
    COPYDOG_PHASE_OK=0,COPYDOG_PHASE_UNKNOWN_TARGET=1,COPYDOG_PHASE_UNKNOWN_SELF=2,
    COPYDOG_PHASE_INVALID_INPUT=3,COPYDOG_PHASE_NO_COVERAGE=4,
    COPYDOG_PHASE_NEEDS_FORCED_CALIBRATION=5
};
typedef struct {
    unsigned targetSegment,selfSegment,onsetOnly;
    int contactStartUs,contactEndUs,activeStartUs,activeEndUs;
    int startMs,endMs,preferredMs;
} COPYDOG_PHASE_WINDOW;
typedef struct {
    unsigned count,quality,executable;
    int reason;
    COPYDOG_PHASE_WINDOW windows[COPYDOG_PHASE_MAX_WINDOWS];
} COPYDOG_PHASE_PLAN;

#include "phase_timing_data.h"

static inline int CopydogPhaseSupportedName(const char *name)
{
    if(!name)return 0;
    return !strcmp(name,"attackrightheavy1end")||!strcmp(name,"attackrightheavy2end")||
        !strcmp(name,"attackbothheavy1end")||!strcmp(name,"attackbothheavy2end")||
        !strcmp(name,"attackrightlightdash")||!strcmp(name,"attackbothdash")||
        !strcmp(name,"attackbothleftdash");
}
static inline int CopydogPhaseKeyEqual(const COPYDOG_PHASE_KEY *a,const COPYDOG_PHASE_KEY *b)
{
    return a&&b&&a->name&&b->name&&a->weaponBase==b->weaponBase&&a->mode==b->mode&&
        a->animation==b->animation&&!strcmp(a->name,b->name);
}
static inline const COPYDOG_PHASE_TARGET *CopydogPhaseFindTarget(const COPYDOG_PHASE_KEY *key)
{
    if(!key||key->weaponBase<=0||key->mode<1||key->mode>3||!CopydogPhaseSupportedName(key->name))return 0;
    for(unsigned i=0;i<COPYDOG_PHASE_TARGET_COUNT;++i)
        if(CopydogPhaseKeyEqual(key,&CopydogPhaseTargets[i].key))return &CopydogPhaseTargets[i];
    /* Native fallback carries no learned weapon association. The caller must
       supply an ID learned for this exact weapon/mode/name, not a guessed ID. */
    for(unsigned i=0;i<COPYDOG_PHASE_NATIVE_TARGET_COUNT;++i){
        const COPYDOG_PHASE_TARGET *p=&CopydogPhaseNativeTargets[i];
        int modeMatches=p->key.mode==1?key->mode==1:key->mode==2||key->mode==3;
        if(modeMatches&&key->animation==p->key.animation&&!strcmp(key->name,p->key.name))return p;
    }
    return 0;
}
static inline const COPYDOG_PHASE_SELF *CopydogPhaseFindSelf(const COPYDOG_PHASE_KEY *key)
{
    if(!key)return 0;
    for(unsigned i=0;i<COPYDOG_PHASE_SELF_COUNT;++i)
        if(CopydogPhaseKeyEqual(key,&CopydogPhaseSelves[i].key))return &CopydogPhaseSelves[i];
    return 0;
}
/* Ambiguous mappings must wait for an exact ID; never select the first match. */
static inline const COPYDOG_PHASE_TARGET *CopydogPhaseTargetByName(int weaponBase,int mode,const char *name)
{
    const COPYDOG_PHASE_TARGET *found=0;
    if(!CopydogPhaseSupportedName(name))return 0;
    for(unsigned i=0;i<COPYDOG_PHASE_TARGET_COUNT;++i){
        const COPYDOG_PHASE_TARGET *p=&CopydogPhaseTargets[i];
        if(p->key.weaponBase!=weaponBase||p->key.mode!=mode||strcmp(p->key.name,name))continue;
        if(found)return 0;
        found=p;
    }
    return found;
}
static inline const COPYDOG_PHASE_SELF *CopydogPhaseSelfByName(int weaponBase,int mode,const char *name)
{
    const COPYDOG_PHASE_SELF *found=0;
    if(!name)return 0;
    for(unsigned i=0;i<COPYDOG_PHASE_SELF_COUNT;++i){
        const COPYDOG_PHASE_SELF *p=&CopydogPhaseSelves[i];
        if(p->key.weaponBase!=weaponBase||p->key.mode!=mode||strcmp(p->key.name,name))continue;
        if(found)return 0;
        found=p;
    }
    return found;
}
/* q = request -> own ParryStart name, NOT request -> own C8 ID.
   shift = independently calibrated own-name -> TAE-active alignment correction.
   Cover the whole candidate contact interval for every q/shift in their bounds:
     request >= contactEnd - activeEnd - qMin - shiftMin
     request <= contactStart - activeStart - qMax - shiftMax.
   No name->C8 lag or R1 empirical offset belongs in this formula. */
static inline int CopydogPhasePlan(const COPYDOG_PHASE_KEY *targetKey,const COPYDOG_PHASE_KEY *selfKey,
    int qMinMs,int qMaxMs,int selfShiftMinMs,int selfShiftMaxMs,int forcedShiftCalibrated,
    COPYDOG_PHASE_PLAN *out)
{
    if(!out)return 0;
    memset(out,0,sizeof *out);out->reason=COPYDOG_PHASE_INVALID_INPUT;
    if(qMinMs<0||qMaxMs<qMinMs||qMaxMs>1000||selfShiftMinMs < -1000||
       selfShiftMaxMs<selfShiftMinMs||selfShiftMaxMs>1000)return 0;
    const COPYDOG_PHASE_TARGET *target=CopydogPhaseFindTarget(targetKey);
    if(!target){out->reason=COPYDOG_PHASE_UNKNOWN_TARGET;return 0;}
    const COPYDOG_PHASE_SELF *self=CopydogPhaseFindSelf(selfKey);
    if(!self){out->reason=COPYDOG_PHASE_UNKNOWN_SELF;return 0;}
    out->quality=self->forcedCandidate?(forcedShiftCalibrated?
        COPYDOG_PHASE_FORCED_CALIBRATED_CANDIDATE:COPYDOG_PHASE_FORCED_UNCALIBRATED):
        COPYDOG_PHASE_NATIVE_CANDIDATE;
    out->reason=COPYDOG_PHASE_NO_COVERAGE;
    for(unsigned a=0;a<target->intervalCount;++a){
        const COPYDOG_PHASE_INTERVAL *hit=&CopydogPhaseContacts[target->firstInterval+a];
        for(unsigned b=0;b<self->intervalCount;++b){
            const COPYDOG_PHASE_INTERVAL *parry=&CopydogPhaseActives[self->firstInterval+b];
            int64_t lo=(int64_t)hit->endUs-parry->endUs-1000LL*(qMinMs+selfShiftMinMs);
            int64_t hi=(int64_t)hit->startUs-parry->startUs-1000LL*(qMaxMs+selfShiftMaxMs);
            unsigned onsetOnly=0;
            /* A long contact span may exceed the parry's effective duration.
               Then target only the earliest candidate contact, explicitly:
               no promise of covering later parts or changed-distance contact. */
            if(hi<lo){
                onsetOnly=1;
                lo=(int64_t)hit->startUs-parry->endUs-1000LL*(qMinMs+selfShiftMinMs);
            }
            if(lo<0)lo=0;
            if(hi<lo)continue;
            lo=(lo+999)/1000;hi/=1000;
            if(hi<lo||hi>INT_MAX||out->count>=COPYDOG_PHASE_MAX_WINDOWS)continue;
            COPYDOG_PHASE_WINDOW *w=&out->windows[out->count++];
            w->targetSegment=a;w->selfSegment=b;w->onsetOnly=onsetOnly;
            if(onsetOnly)out->quality|=COPYDOG_PHASE_ONSET_ONLY;
            w->contactStartUs=hit->startUs;w->contactEndUs=hit->endUs;
            w->activeStartUs=parry->startUs;w->activeEndUs=parry->endUs;
            w->startMs=(int)lo;w->endMs=(int)hi;w->preferredMs=(int)(lo+(hi-lo)/2);
        }
    }
    if(!out->count)return 0;
    out->executable=!self->forcedCandidate||forcedShiftCalibrated;
    out->reason=out->executable?COPYDOG_PHASE_OK:COPYDOG_PHASE_NEEDS_FORCED_CALIBRATION;
    return 1;
}
/* Return a due window without turning an elapsed window into a late retry. */
static inline int CopydogPhaseDue(const COPYDOG_PHASE_PLAN *plan,uint32_t elapsedMs,unsigned *index)
{
    if(!plan||!plan->executable)return 0;
    for(unsigned i=0;i<plan->count;++i){
        const COPYDOG_PHASE_WINDOW *w=&plan->windows[i];
        if(elapsedMs>=(uint32_t)w->preferredMs&&elapsedMs<=(uint32_t)w->endMs){
            if(index)*index=i;
            return 1;
        }
    }
    return 0;
}
#endif
