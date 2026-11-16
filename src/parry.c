#include "parry.h"
#include <math.h>
#include <limits.h>
#include "parry_calibration.h"
#include "parry_dispatch_calibration.h"
#include "parry_animations.h"
int CopydogParryCalibratedWindow(int tool,int animation,int first,int startup,int ahead,int*start,int*end)
{
    return CopydogParryCalibratedWindowShifted(tool,animation,first,startup,ahead,0,start,end);
}
int CopydogParryCalibratedWindowShifted(int tool,int animation,int first,int startup,int ahead,int clockLeadMs,int*start,int*end)
{
    if(!start||!end||tool<=0||animation<0||first<=0||startup<0||startup>1000||ahead<0||ahead>14096||
       clockLeadMs<0||clockLeadMs>600)return 0;
    tool=tool/10000*10000;
    int lead=tool==11010000?PARRY_FIST_LEAD:tool==20080000?PARRY_SHIELD_LEAD:0;
    if(!lead)return 0;
    int family=animation/100000,category=(animation%100000)/1000,bias=PARRY_CONTACT_BIAS;
    if(category<30||category>=40)return 0;
    for(unsigned i=0;i<sizeof ParryContactProfiles/sizeof*ParryContactProfiles;++i)
        if(ParryContactProfiles[i].family==family&&ParryContactProfiles[i].category==category){
            bias=ParryContactProfiles[i].bias;break;
        }
    /* Predict observed contact using the native attack table, then subtract
       the tool's empirical lead. Preserve the user's adjustment around the
       captured startup setting. Do not mix remote latency into a local-clock fit. */
    int dispatch=tool==20080000?PARRY_SHIELD_DISPATCH:0;
    int64_t request=(int64_t)first-bias-lead-dispatch-(startup-PARRY_CAPTURE_STARTUP)-ahead+clockLeadMs;
    if(request<0)request=0;
    if(request>INT_MAX-32)return 0;
    *start=(int)request;*end=(int)request+32;
    return 1;
}
void CopydogParryObserve(COPYDOG_PARRY_TRACK*t,uintptr_t self,uintptr_t actor,int anim,float progress,uint32_t now)
{
    if(!t)return;
    int valid=isfinite(progress)&&progress>=0&&progress<=1;
    /* +0x224 is a readiness-related value, not a verified monotonic animation clock.
       Its rewind must not reset an unchanged animation or spend latch. */
    if(t->self!=self||t->actor!=actor){
        *t=(COPYDOG_PARRY_TRACK){.self=self,.actor=actor,.animation=-1};
    }
    if(!self||!actor||anim<0){t->candidateActive=0;return;}
    if(t->stable&&t->animation==anim){
        t->candidateActive=0;
    }else if(!t->candidateActive||t->candidateAnimation!=anim){
        t->candidateAnimation=anim;t->candidateStarted=now;t->candidateActive=1;
    }else if((uint32_t)(now-t->candidateStarted)>=COPYDOG_ANIMATION_STABLE_MS){
        t->animation=anim;t->started=t->candidateStarted;t->stable=1;
        t->candidateActive=0;t->spent=0;
    }
    t->haveProgress=valid;t->progress=progress;
}
int CopydogParryWindow(int first,int last,int startup,int ahead,int*start,int*end)
{
    return CopydogParryWindowShifted(first,last,startup,ahead,0,start,end);
}
int CopydogParryWindowShifted(int first,int last,int startup,int ahead,int clockLeadMs,int*start,int*end)
{
    if(!start||!end||first<=0||last<first||startup<0||startup>1000||ahead<0||ahead>14096||
       clockLeadMs<0||clockLeadMs>600)return 0;
    int64_t low=(int64_t)first-startup-ahead+clockLeadMs,high=(int64_t)last-startup-ahead+clockLeadMs;
    if(low<0)low=0;
    if(high<low)high=low;
    /* One 32ms polling allowance, never an unbounded late retry. */
    if(high>INT_MAX-32)return 0;
    *start=(int)low;*end=(int)high+32;return 1;
}
int CopydogParryPhase(const COPYDOG_PARRY_TRACK*t,uint32_t now,int start,int end)
{
    if(!t||!t->self||!t->actor||start<0||end<start)return PARRY_STATE;
    if(!t->stable||t->candidateActive)return PARRY_UNSTABLE;
    if(t->spent)return PARRY_SPENT;
    uint32_t elapsed=now-t->started;
    return elapsed<(uint32_t)start?PARRY_EARLY:elapsed>(uint32_t)end?PARRY_LATE:PARRY_NONE;
}
