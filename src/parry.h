#ifndef COPYDOG_PARRY_H
#define COPYDOG_PARRY_H
#include <stdint.h>
typedef struct {
    uintptr_t self,actor;
    int animation,spent,haveProgress,stable,candidateAnimation,candidateActive;
    float progress;
    uint32_t started,candidateStarted;
} COPYDOG_PARRY_TRACK;
typedef struct {
    uint64_t checks,known,unknown,requests;
    uint64_t animation,elapsed,start,end,reason;
} COPYDOG_PARRY_COUNTERS;
enum { PARRY_NONE,PARRY_ROLL_PRIORITY,PARRY_UNKNOWN,PARRY_UNPARRIABLE,
    PARRY_KEY,PARRY_STATE,PARRY_COOLDOWN,PARRY_EARLY,PARRY_LATE,
    PARRY_SPENT,PARRY_QUEUED,PARRY_QUEUE_MISSING,PARRY_SHIFT_PAUSE,
    PARRY_UNSTABLE,PARRY_DISPATCH_UNREACHABLE,PARRY_NO_CONTACT,PARRY_CONTACT_UNCONFIRMED };
void CopydogParryObserve(COPYDOG_PARRY_TRACK*,uintptr_t,uintptr_t,int,float,uint32_t);
int CopydogParryWindow(int,int,int,int,int*,int*);
int CopydogParryCalibratedWindow(int,int,int,int,int,int*,int*);
/* Shift from a later animation-ID clock to an earlier observed state clock.
   Apply the measured lead before clipping negative request times. */
int CopydogParryWindowShifted(int,int,int,int,int,int*,int*);
int CopydogParryCalibratedWindowShifted(int,int,int,int,int,int,int*,int*);
int CopydogParryPhase(const COPYDOG_PARRY_TRACK*,uint32_t,int,int);
#endif
