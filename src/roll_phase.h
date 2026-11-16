#ifndef COPYDOG_ROLL_PHASE_H
#define COPYDOG_ROLL_PHASE_H
#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    int valid,onsetOnly,startMs,endMs,preferredMs;
    int contactFirstUs,contactLastUs,iframeFirstUs,iframeLastUs;
} COPYDOG_ROLL_PHASE_WINDOW;

/* Both resource intervals must be aligned to their observed action-name clocks.
   q is request -> RollStart name, NOT request -> visible roll ID. Bounds on
   name/resource alignment are separate; an unmeasured alignment is not zero by
   definition. This pure helper does not assert native iframe activation. */
static inline int CopydogRollPhaseWindow(int contactFirstUs,int contactLastUs,
    int iframeFirstUs,int iframeLastUs,int qMinMs,int qMaxMs,
    int rollShiftMinMs,int rollShiftMaxMs,int allowOnsetOnly,COPYDOG_ROLL_PHASE_WINDOW *out)
{
    if(!out)return 0;
    memset(out,0,sizeof *out);
    if(contactFirstUs<0||contactLastUs<=contactFirstUs||iframeFirstUs<0||iframeLastUs<=iframeFirstUs||
       qMinMs<0||qMaxMs<qMinMs||qMaxMs>1000||rollShiftMinMs < -1000||
       rollShiftMaxMs<rollShiftMinMs||rollShiftMaxMs>1000)return 0;
    int64_t first=(int64_t)contactLastUs-iframeLastUs-1000LL*(qMinMs+rollShiftMinMs);
    int64_t last=(int64_t)contactFirstUs-iframeFirstUs-1000LL*(qMaxMs+rollShiftMaxMs);
    int onsetOnly=0;
    if(first>last&&allowOnsetOnly){
        first=(int64_t)contactFirstUs-iframeLastUs-1000LL*(qMinMs+rollShiftMinMs);
        onsetOnly=1;
    }
    if(first<0)first=0;
    if(last<first)return 0;
    first=(first+999)/1000;last/=1000;
    if(last<first||last>INT_MAX)return 0;
    out->valid=1;out->onsetOnly=onsetOnly;
    out->startMs=(int)first;out->endMs=(int)last;
    out->preferredMs=(int)(first+(last-first)/2);
    out->contactFirstUs=contactFirstUs;out->contactLastUs=contactLastUs;
    out->iframeFirstUs=iframeFirstUs;out->iframeLastUs=iframeLastUs;
    return 1;
}

/* No fixed 256ms reserve and no grace period that replays expired attacks. */
static inline int CopydogRollPhaseDue(const COPYDOG_ROLL_PHASE_WINDOW *window,uint32_t elapsedNameMs)
{
    return window&&window->valid&&elapsedNameMs>=(uint32_t)window->preferredMs&&
        elapsedNameMs<=(uint32_t)window->endMs;
}

/* Only use a lag learned for the exact current actor/weapon/mode/name/full ID.
   Converting late-ID elapsed time to the earlier name clock adds the lag once. */
static inline int CopydogRollNameElapsedFromId(uint32_t elapsedIdMs,int observedNameToIdMs,uint32_t *elapsedNameMs)
{
    if(!elapsedNameMs||observedNameToIdMs<0||observedNameToIdMs>1000||
       elapsedIdMs>UINT32_MAX-(uint32_t)observedNameToIdMs)return 0;
    *elapsedNameMs=elapsedIdMs+(uint32_t)observedNameToIdMs;
    return 1;
}
#endif
