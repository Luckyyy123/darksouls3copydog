#ifndef COPYDOG_EARLY_CLOCK_H
#define COPYDOG_EARLY_CLOCK_H
#include <stdint.h>
#include <string.h>

enum {
    COPYDOG_EARLY_CAPACITY=128, COPYDOG_EARLY_SAMPLES=5, COPYDOG_EARLY_NAME=32,
    COPYDOG_EARLY_STABLE_MS=12, COPYDOG_EARLY_GAP_MS=64,
    COPYDOG_EARLY_MIN_LAG_MS=50, COPYDOG_EARLY_MAX_LAG_MS=600,
    COPYDOG_EARLY_MAX_SPREAD_MS=32
};
typedef struct {
    int used,weaponBase,mode,expectedId;
    char name[COPYDOG_EARLY_NAME];
    int lags[COPYDOG_EARLY_SAMPLES];
    unsigned count,next,ready;
    int leadMs;
} COPYDOG_EARLY_PROFILE;
typedef struct {
    COPYDOG_EARLY_PROFILE entries[COPYDOG_EARLY_CAPACITY];
    unsigned nextEvict;
} COPYDOG_EARLY_PROFILES;
typedef struct {
    uintptr_t actor;
    int networkId,weaponBase,mode,initialAnim;
    char name[COPYDOG_EARLY_NAME],candidateName[COPYDOG_EARLY_NAME];
    uint32_t started,lastSeen,candidateStarted;
    int candidateInitialAnim,pairedId;
    unsigned initialized,active,stable,spent,paired,candidateActive,suppressed;
} COPYDOG_EARLY_STATE;

static inline int CopydogEarlyNameKind(const char *name)
{
    if(!name)return 0;
    static const char *names[]={"attackrightlight1","attackbothlight1",
        "attackrightheavy1start","attackrightheavy1end","attackrightheavy2start","attackrightheavy2end",
        "attackbothheavy1start","attackbothheavy1end","attackbothheavy2start","attackbothheavy2end",
        "attackrightlightdash","attackbothdash","attackbothleftdash"};
    for(unsigned i=0;i<sizeof names/sizeof names[0];++i)if(!strcmp(name,names[i]))return (int)i+1;
    return 0;
}
static inline const char *CopydogEarlyCanonicalName(int kind)
{
    static const char *names[]={"","attackrightlight1","attackbothlight1",
        "attackrightheavy1start","attackrightheavy1end","attackrightheavy2start","attackrightheavy2end",
        "attackbothheavy1start","attackbothheavy1end","attackbothheavy2start","attackbothheavy2end",
        "attackrightlightdash","attackbothdash","attackbothleftdash"};
    return kind>=0&&(unsigned)kind<sizeof names/sizeof names[0]?names[kind]:"";
}
static inline int CopydogEarlyBase(int weapon)
{
    return weapon>0?weapon/10000*10000:0;
}
static inline int CopydogEarlyValidId(const char *name,int id)
{
    int kind=CopydogEarlyNameKind(name);
    static const int suffix[]={-1,30000,34000,30320,30321,30340,30341,34320,34321,34340,34341,30500,34500,33500};
    return id>=100000&&kind>0&&(unsigned)kind<sizeof suffix/sizeof suffix[0]&&id%100000==suffix[kind];
}
static inline COPYDOG_EARLY_PROFILE *CopydogEarlyFindProfile(COPYDOG_EARLY_PROFILES *profiles,
    int weaponBase,int mode,const char *name,int create)
{
    if(!profiles||!(weaponBase=CopydogEarlyBase(weaponBase))||mode<1||mode>3||
       !CopydogEarlyNameKind(name))return 0;
    COPYDOG_EARLY_PROFILE *freeEntry=0;
    for(unsigned i=0;i<COPYDOG_EARLY_CAPACITY;++i){
        COPYDOG_EARLY_PROFILE *p=&profiles->entries[i];
        if(p->used&&p->weaponBase==weaponBase&&p->mode==mode&&!strcmp(p->name,name))return p;
        if(!p->used&&!freeEntry)freeEntry=p;
    }
    if(!create)return 0;
    COPYDOG_EARLY_PROFILE *p=freeEntry?freeEntry:&profiles->entries[profiles->nextEvict++%COPYDOG_EARLY_CAPACITY];
    memset(p,0,sizeof *p);p->used=1;p->weaponBase=weaponBase;p->mode=mode;
    strcpy(p->name,name);return p;
}
static inline void CopydogEarlyRefreshProfile(COPYDOG_EARLY_PROFILE *p)
{
    p->ready=0;p->leadMs=0;
    if(p->count<3||p->count>COPYDOG_EARLY_SAMPLES)return;
    int ordered[COPYDOG_EARLY_SAMPLES];
    for(unsigned i=0;i<p->count;++i){
        ordered[i]=p->lags[i];
        if(ordered[i]<COPYDOG_EARLY_MIN_LAG_MS||ordered[i]>COPYDOG_EARLY_MAX_LAG_MS)return;
        for(unsigned j=i;j>0&&ordered[j]<ordered[j-1];--j){
            int swap=ordered[j];ordered[j]=ordered[j-1];ordered[j-1]=swap;
        }
    }
    unsigned bestStart=0,bestCount=0;
    int bestSpread=COPYDOG_EARLY_MAX_SPREAD_MS+1;
    /* One delayed ID must not disable a consistent majority for five attacks. */
    for(unsigned first=0;first<p->count;++first){
        for(unsigned last=first;last<p->count;++last){
            int spread=ordered[last]-ordered[first];
            if(spread>COPYDOG_EARLY_MAX_SPREAD_MS)break;
            unsigned count=last-first+1;
            if(count>bestCount||(count==bestCount&&spread<bestSpread)){
                bestStart=first;bestCount=count;bestSpread=spread;
            }
        }
    }
    if(bestCount<3||bestCount*2<=p->count)return;
    p->leadMs=(ordered[bestStart+(bestCount-1)/2]+ordered[bestStart+bestCount/2])/2;
    p->ready=1;
}
static inline void CopydogEarlyAddLag(COPYDOG_EARLY_PROFILE *p,int expectedId,int lag)
{
    if(p->expectedId!=expectedId){
        memset(p->lags,0,sizeof p->lags);p->count=p->next=p->ready=0;p->leadMs=0;
        p->expectedId=expectedId;
    }
    p->lags[p->next]=lag;p->next=(p->next+1)%COPYDOG_EARLY_SAMPLES;
    if(p->count<COPYDOG_EARLY_SAMPLES)++p->count;
    CopydogEarlyRefreshProfile(p);
}
/* Seed values must be distinct audited observations, never replicated medians. */
static inline int CopydogEarlySeed(COPYDOG_EARLY_PROFILES *profiles,int weaponBase,int mode,
    const char *name,int expectedId,const int *lags,unsigned count)
{
    if(!lags||count<1||count>COPYDOG_EARLY_SAMPLES||!CopydogEarlyValidId(name,expectedId))return 0;
    for(unsigned i=0;i<count;++i)
        if(lags[i]<COPYDOG_EARLY_MIN_LAG_MS||lags[i]>COPYDOG_EARLY_MAX_LAG_MS)return 0;
    COPYDOG_EARLY_PROFILE *p=CopydogEarlyFindProfile(profiles,weaponBase,mode,name,1);
    if(!p)return 0;
    p->expectedId=expectedId;p->count=p->next=p->ready=0;p->leadMs=0;memset(p->lags,0,sizeof p->lags);
    for(unsigned i=0;i<count;++i)CopydogEarlyAddLag(p,expectedId,lags[i]);
    return p->ready!=0;
}
static inline void CopydogEarlyAttach(COPYDOG_EARLY_STATE *state,uintptr_t actor,int networkId,
    int weaponBase,int mode,const char *name,int observedStableId,uint32_t now)
{
    memset(state,0,sizeof *state);
    state->actor=actor;state->networkId=networkId;state->weaponBase=weaponBase;state->mode=mode;
    state->initialAnim=observedStableId;state->started=state->lastSeen=now;state->initialized=1;
    strcpy(state->name,CopydogEarlyCanonicalName(CopydogEarlyNameKind(name)));
    state->active=state->name[0]!=0;
    /* An attack already running at attachment has no observed start. */
    state->suppressed=state->active;state->stable=!state->active;
}
static inline void CopydogEarlyObserve(COPYDOG_EARLY_STATE *state,COPYDOG_EARLY_PROFILES *profiles,
    uintptr_t actor,int networkId,int weaponBase,int mode,const char *name,
    int observedStableId,uint32_t idFirstSeenTick,uint32_t now)
{
    if(!state)return;
    weaponBase=CopydogEarlyBase(weaponBase);
    if(!actor||!weaponBase||mode<1||mode>3){memset(state,0,sizeof *state);return;}
    const char *canonical=CopydogEarlyCanonicalName(CopydogEarlyNameKind(name));
    if(!state->initialized||state->actor!=actor||state->networkId!=networkId||
       state->weaponBase!=weaponBase||state->mode!=mode||
       (uint32_t)(now-state->lastSeen)>COPYDOG_EARLY_GAP_MS){
        CopydogEarlyAttach(state,actor,networkId,weaponBase,mode,canonical,observedStableId,now);return;
    }
    state->lastSeen=now;
    if(strcmp(state->name,canonical)){
        if(!state->candidateActive||strcmp(state->candidateName,canonical)){
            state->candidateActive=1;strcpy(state->candidateName,canonical);
            state->candidateStarted=now;state->candidateInitialAnim=observedStableId;return;
        }
        if((uint32_t)(now-state->candidateStarted)<COPYDOG_EARLY_STABLE_MS)return;
        strcpy(state->name,canonical);state->started=state->candidateStarted;
        state->initialAnim=state->candidateInitialAnim;state->active=canonical[0]!=0;
        state->spent=state->paired=state->suppressed=state->candidateActive=0;state->stable=1;
    }else{
        state->candidateActive=0;
        state->stable=(uint32_t)(now-state->started)>=COPYDOG_EARLY_STABLE_MS;
    }
    if(!state->active||!state->stable||state->suppressed||state->paired||state->initialAnim<0||
       observedStableId==state->initialAnim||!CopydogEarlyValidId(state->name,observedStableId))return;
    uint32_t lag=idFirstSeenTick-state->started;
    if(lag>COPYDOG_EARLY_MAX_LAG_MS||lag<COPYDOG_EARLY_MIN_LAG_MS||
       lag>(uint32_t)(now-state->started))return;
    COPYDOG_EARLY_PROFILE *p=CopydogEarlyFindProfile(profiles,weaponBase,mode,state->name,1);
    if(!p)return;
    CopydogEarlyAddLag(p,observedStableId,(int)lag);state->pairedId=observedStableId;state->paired=1;
}
static inline int CopydogEarlyMapping(const COPYDOG_EARLY_STATE *state,COPYDOG_EARLY_PROFILES *profiles,
    int *expectedId,int *leadMs)
{
    if(expectedId)*expectedId=-1;
    if(leadMs)*leadMs=0;
    if(!state||!expectedId||!leadMs||!state->initialized||!state->active)return 0;
    COPYDOG_EARLY_PROFILE *p=CopydogEarlyFindProfile(profiles,state->weaponBase,state->mode,state->name,0);
    if(!p||!p->ready||!CopydogEarlyValidId(state->name,p->expectedId))return 0;
    *expectedId=p->expectedId;*leadMs=p->leadMs;return 1;
}
static inline int CopydogEarlyPlan(const COPYDOG_EARLY_STATE *state,COPYDOG_EARLY_PROFILES *profiles,
    int *expectedId,int *leadMs)
{
    if(expectedId)*expectedId=-1;
    if(leadMs)*leadMs=0;
    if(!state||!state->stable||state->candidateActive||state->suppressed||state->spent)return 0;
    return CopydogEarlyMapping(state,profiles,expectedId,leadMs);
}
static inline void CopydogEarlyMarkSpent(COPYDOG_EARLY_STATE *state)
{
    if(state)state->spent=1;
}
#endif
