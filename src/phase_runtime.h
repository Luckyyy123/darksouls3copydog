#ifndef COPYDOG_PHASE_RUNTIME_H
#define COPYDOG_PHASE_RUNTIME_H
#include "early_clock.h"

typedef struct {
    uintptr_t actor;
    int networkId,weaponBase,mode,nameKind;
    uint32_t started,episodeStarted;
    unsigned initialized,spent;
} COPYDOG_PHASE_EPISODE;

static inline void CopydogPhaseEpisodeReset(COPYDOG_PHASE_EPISODE *episode)
{
    if(episode)memset(episode,0,sizeof *episode);
}

static inline int CopydogPhaseHeavyContinuation(int before,int after)
{
    static const char *pairs[][2]={
        {"attackrightheavy1start","attackrightheavy1end"},
        {"attackrightheavy2start","attackrightheavy2end"},
        {"attackbothheavy1start","attackbothheavy1end"},
        {"attackbothheavy2start","attackbothheavy2end"}};
    for(unsigned i=0;i<sizeof pairs/sizeof pairs[0];++i)
        if(before==CopydogEarlyNameKind(pairs[i][0])&&after==CopydogEarlyNameKind(pairs[i][1]))return 1;
    return 0;
}

/* The caller supplies an already-stable canonical name and its backdated onset.
   Transient names are handled by early_clock before reaching this function. */
static inline int CopydogPhaseEpisodeObserve(COPYDOG_PHASE_EPISODE *episode,
    uintptr_t actor,int networkId,int weaponBase,int mode,const char *stableName,uint32_t started)
{
    if(!episode)return 0;
    int kind=CopydogEarlyNameKind(stableName);
    weaponBase=CopydogEarlyBase(weaponBase);
    if(!actor||!weaponBase||mode<1||mode>3||!kind){CopydogPhaseEpisodeReset(episode);return 0;}
    int sameIdentity=episode->initialized&&episode->actor==actor&&episode->networkId==networkId&&
        episode->weaponBase==weaponBase&&episode->mode==mode;
    int sameEpoch=sameIdentity&&episode->nameKind==kind&&episode->started==started;
    int continuation=sameIdentity&&CopydogPhaseHeavyContinuation(episode->nameKind,kind)&&
        (int32_t)(started-episode->started)>=0;
    if(!sameEpoch&&!continuation){episode->spent=0;episode->episodeStarted=started;}
    episode->actor=actor;episode->networkId=networkId;episode->weaponBase=weaponBase;
    episode->mode=mode;episode->nameKind=kind;episode->started=started;episode->initialized=1;
    return 1;
}

static inline int CopydogPhaseEpisodeSpent(const COPYDOG_PHASE_EPISODE *episode)
{
    return episode&&episode->initialized&&episode->spent;
}

static inline void CopydogPhaseEpisodeMarkSpent(COPYDOG_PHASE_EPISODE *episode)
{
    if(episode&&episode->initialized)episode->spent=1;
}

enum { COPYDOG_PHASE_Q_SAMPLES=9, COPYDOG_PHASE_Q_NONE=0,
       COPYDOG_PHASE_Q_HISTORICAL=1, COPYDOG_PHASE_Q_MEASURED=2 };
typedef struct {
    uintptr_t self;
    int tool,mode,nameKind;
    int values[COPYDOG_PHASE_Q_SAMPLES];
    uint32_t requests[COPYDOG_PHASE_Q_SAMPLES];
    unsigned initialized,count,next,hasSeed;
    int seedMinMs,seedMaxMs;
} COPYDOG_PHASE_Q;

static inline int CopydogPhaseQParryNameKind(const char *name)
{
    if(!name)return 0;
    return !strcmp(name,"parryleftstart")?1:!strcmp(name,"parryrightstart")?2:0;
}

static inline void CopydogPhaseQReset(COPYDOG_PHASE_Q *q)
{
    if(q)memset(q,0,sizeof *q);
}

/* tool is exact, without weapon-category normalization. Changing self/tool/
   mode/name clears measured samples and historical seeds. Rebind before add. */
static inline int CopydogPhaseQBind(COPYDOG_PHASE_Q *q,uintptr_t self,int tool,int mode,const char *name)
{
    if(!q)return 0;
    int kind=CopydogPhaseQParryNameKind(name);
    if(!self||tool<=0||mode<1||mode>3||!kind){CopydogPhaseQReset(q);return 0;}
    if(!q->initialized||q->self!=self||q->tool!=tool||q->mode!=mode||q->nameKind!=kind){
        CopydogPhaseQReset(q);q->self=self;q->tool=tool;q->mode=mode;q->nameKind=kind;q->initialized=1;
    }
    return 1;
}

/* Historical bounds are not replicated measurements and never increase count. */
static inline int CopydogPhaseQSeed(COPYDOG_PHASE_Q *q,int minMs,int maxMs)
{
    if(!q||!q->initialized||minMs<0||maxMs<minMs||maxMs>200)return 0;
    q->hasSeed=1;q->seedMinMs=minMs;q->seedMaxMs=maxMs;return 1;
}

/* Call only for a newly observed matching own ParryStart name. Never pass the
   later C8 animation confirmation timestamp. Tick arithmetic supports wrap. */
static inline int CopydogPhaseQAdd(COPYDOG_PHASE_Q *q,uint32_t requestTick,uint32_t nameTick)
{
    if(!q||!q->initialized)return 0;
    uint32_t delay=nameTick-requestTick;
    if(delay>200)return 0;
    for(unsigned i=0;i<q->count;++i)if(q->requests[i]==requestTick)return 0;
    q->values[q->next]=(int)delay;q->requests[q->next]=requestTick;
    q->next=(q->next+1)%COPYDOG_PHASE_Q_SAMPLES;
    if(q->count<COPYDOG_PHASE_Q_SAMPLES)++q->count;
    return 1;
}

static inline int CopydogPhaseQBounds(const COPYDOG_PHASE_Q *q,int *minMs,int *maxMs,int *source)
{
    if(minMs)*minMs=0;
    if(maxMs)*maxMs=0;
    if(source)*source=COPYDOG_PHASE_Q_NONE;
    if(!q||!q->initialized||!minMs||!maxMs||!source)return 0;
    if(q->count>=3){
        int low=q->values[0],high=q->values[0];
        for(unsigned i=1;i<q->count;++i){
            if(q->values[i]<low)low=q->values[i];
            if(q->values[i]>high)high=q->values[i];
        }
        *minMs=low;*maxMs=high;*source=COPYDOG_PHASE_Q_MEASURED;return 1;
    }
    if(!q->hasSeed)return 0;
    *minMs=q->seedMinMs;*maxMs=q->seedMaxMs;*source=COPYDOG_PHASE_Q_HISTORICAL;return 1;
}
#endif
