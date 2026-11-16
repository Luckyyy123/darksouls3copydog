#ifndef COPYDOG_ROLL_DISPATCH_H
#define COPYDOG_ROLL_DISPATCH_H
#include <stdint.h>
#include <string.h>

enum {
    COPYDOG_ROLL_Q_CANDIDATE=1,COPYDOG_ROLL_Q_MEASURED=2,COPYDOG_ROLL_Q_CAPACITY=9,
    COPYDOG_ROLL_Q_MAX_GAP=64,COPYDOG_ROLL_Q_STABLE=12,COPYDOG_ROLL_Q_MAX_NAME=200,
    COPYDOG_ROLL_Q_MAX_CONFIRM=400,COPYDOG_ROLL_Q_MARGIN=6
};
typedef struct {
    uintptr_t self;
    int tool,mode,values[COPYDOG_ROLL_Q_CAPACITY];
    unsigned count,next,haveRequest,pending,atName,atId;
    uint32_t request,lastSeen,nameAt,idAt;
    int lastHp,visibleId;
} COPYDOG_ROLL_Q;

static inline unsigned char CopydogRollQLower(unsigned char c)
{return c>='A'&&c<='Z'?(unsigned char)(c+('a'-'A')):c;}

static inline int CopydogRollQNamePrefix(const char *s,const char *lower,int exact)
{
    if(!s)return 0;
    while(*lower){if(CopydogRollQLower((unsigned char)*s++)!=(unsigned char)*lower++)return 0;}
    return !exact||!*s;
}

static inline int CopydogRollQInRoll(int animation)
{
    if(animation<0)return 0;
    int id=animation%100000;
    return (id>=27100&&id<27200)||(id>=38020&&id<=38070);
}

static inline void CopydogRollQCancel(COPYDOG_ROLL_Q *q)
{if(q){q->pending=0;q->atName=0;q->atId=0;}}

static inline int CopydogRollQBind(COPYDOG_ROLL_Q *q,uintptr_t self,int tool,int mode)
{
    if(!q)return 0;
    if(!self||tool<=0||mode<1||mode>3){memset(q,0,sizeof *q);return 0;}
    if(q->self!=self||q->tool!=tool||q->mode!=mode){
        memset(q,0,sizeof *q);q->self=self;q->tool=tool;q->mode=mode;
    }
    return 1;
}

/* Call ONLY after the local dispatch call returned for the ordinary forward-roll
   request. beforeName/C8/HP are captured before submission; no manual roll,
   quickstep, action-call failure or pre-existing roll may open this gate. */
static inline int CopydogRollQBegin(COPYDOG_ROLL_Q *q,uintptr_t self,int tool,int mode,
    const char *beforeName,int beforeC8,int hp,uint32_t requestTick)
{
    if(!CopydogRollQBind(q,self,tool,mode))return 0;
    if(q->haveRequest&&(int32_t)(requestTick-q->request)<=0)return 0;
    CopydogRollQCancel(q);q->haveRequest=1;q->request=requestTick;
    if(!beforeName||!*beforeName||CopydogRollQNamePrefix(beforeName,"rolling",0)||
       beforeC8<0||CopydogRollQInRoll(beforeC8)||hp<=0)return 0;
    q->pending=1;q->lastSeen=requestTick;q->lastHp=hp;return 1;
}

/* q ends at first valid RollingLight name, not C8 arrival. Stable exact C8 is
   only corroboration. Unrelated names after onset and missing observations
   discard that request. Caller may explicitly cancel on conflicting input. */
static inline int CopydogRollQObserve(COPYDOG_ROLL_Q *q,uintptr_t self,int tool,int mode,
    const char *ownName,int ownC8,int hp,int cancelled,uint32_t now)
{
    if(!CopydogRollQBind(q,self,tool,mode)||!q->pending)return 0;
    uint32_t age=now-q->request;
    if(cancelled||!ownName||!*ownName||ownC8<0||hp<=0||hp<q->lastHp||
       now-q->lastSeen>COPYDOG_ROLL_Q_MAX_GAP||age>COPYDOG_ROLL_Q_MAX_CONFIRM||
       (!q->atName&&age>COPYDOG_ROLL_Q_MAX_NAME)){
        CopydogRollQCancel(q);return 0;
    }
    q->lastSeen=now;q->lastHp=hp;
    if(!CopydogRollQNamePrefix(ownName,"rollinglight",1)){
        if(q->atName||CopydogRollQNamePrefix(ownName,"rolling",0)||CopydogRollQInRoll(ownC8))CopydogRollQCancel(q);
        return 0;
    }
    if(!q->atName){q->nameAt=now;q->atName=1;}
    /* Full IDs only: do not accept an unrelated weapon-prefixed lookalike,
       another roll class or a quickstep with the same high-level name. */
    if(ownC8<27100||ownC8>27107){
        if(q->atId||CopydogRollQInRoll(ownC8))CopydogRollQCancel(q);
        return 0;
    }
    if(!q->atId||ownC8!=q->visibleId){q->idAt=now;q->visibleId=ownC8;q->atId=1;return 0;}
    if(now-q->nameAt<COPYDOG_ROLL_Q_STABLE||now-q->idAt<COPYDOG_ROLL_Q_STABLE)return 0;
    q->values[q->next]=(int)(q->nameAt-q->request);q->next=(q->next+1)%COPYDOG_ROLL_Q_CAPACITY;
    if(q->count<COPYDOG_ROLL_Q_CAPACITY)++q->count;
    CopydogRollQCancel(q);return 1;
}

static inline int CopydogRollQBounds(const COPYDOG_ROLL_Q *q,int *minMs,int *maxMs)
{
    if(!minMs||!maxMs)return 0;
    *minMs=0;*maxMs=32;
    if(!q||q->count<3)return COPYDOG_ROLL_Q_CANDIDATE;
    int lo=q->values[0],hi=q->values[0];
    for(unsigned i=1;i<q->count;++i){if(q->values[i]<lo)lo=q->values[i];if(q->values[i]>hi)hi=q->values[i];}
    *minMs=lo>COPYDOG_ROLL_Q_MARGIN?lo-COPYDOG_ROLL_Q_MARGIN:0;
    *maxMs=hi+COPYDOG_ROLL_Q_MARGIN;return COPYDOG_ROLL_Q_MEASURED;
}
#endif
