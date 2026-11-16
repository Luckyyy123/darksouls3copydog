#include "defense.h"
#include "parry_animations.h"
int CopydogDefenseDue(int first,int last,int elapsed,int lead,int network,int dispatch)
{
    if(first<=0||last<first||elapsed<0||lead<0||network<0||dispatch<0)return 0;
    int64_t begin=(int64_t)first-lead-network-dispatch;
    if(begin<0)begin=0;
    /* Preserve the existing 256ms roll reserve; never replay an expired attack. */
    int64_t end=(int64_t)last-256;
    if(end<begin)end=begin;
    return elapsed>=begin&&(int64_t)elapsed<=end+32;
}
int CopydogDefenseAnimation(int kind,int animation)
{
    int a=animation<0?-1:animation%100000;
    if(kind==DEF_PARRY)return CopydogKnownParryAnimation(animation);
    return kind==DEF_ROLL&&((27100<=a&&a<27200)||(38020<=a&&a<=38070));
}
int CopydogDefenseBusy(const COPYDOG_DEFENSE*t){return t->phase!=DEF_IDLE;}
void CopydogDefenseCancel(COPYDOG_DEFENSE*t,int reason,uint32_t now)
{
    if(t->phase==DEF_IDLE||t->phase==DEF_CANCELLED)return;
    t->phase=DEF_CANCELLED;t->reason=reason;t->changed=now;
    t->haveCandidate=t->haveTargetCandidate=0;++t->cancellations;
}
int CopydogDefenseBegin(COPYDOG_DEFENSE*t,uintptr_t self,uintptr_t target,int targetAnimation,
                       int own,int hp,int kind,int tool,uint32_t now)
{
    if(!self||hp<=0||CopydogDefenseBusy(t)||(kind!=DEF_ROLL&&kind!=DEF_PARRY)||CopydogDefenseAnimation(kind,own))return 0;
    t->self=self;t->target=target;t->targetAnimation=targetAnimation;t->previousAnimation=own;
    t->hp=hp;t->kind=kind;t->tool=tool;t->submitted=now;t->changed=now;
    t->haveCandidate=t->haveTargetCandidate=0;
    t->awaitingConfirmation=1;
    t->phase=DEF_WAIT_ANIMATION;t->reason=DEF_OK;++t->requests;return 1;
}
static int DefenseAnimationEnded(COPYDOG_DEFENSE*t,int own,uint32_t now)
{
    if(own<0||CopydogDefenseAnimation(t->kind,own)){t->haveCandidate=0;return 0;}
    if(!t->haveCandidate){t->haveCandidate=1;t->candidateSince=now;return 0;}
    return (uint32_t)(now-t->candidateSince)>=COPYDOG_ANIMATION_STABLE_MS;
}
void CopydogDefenseObserveAttributed(COPYDOG_DEFENSE*t,uintptr_t self,int own,int hp,int targetAnimation,int ready,uint32_t now,int allowConfirmation)
{
    if(!self||self!=t->self){
        *t=(COPYDOG_DEFENSE){.self=self,.hp=hp,.previousAnimation=own,.reason=DEF_SELF_CHANGED};return;
    }
    int damaged=hp<=0||(t->hp>0&&hp<t->hp);
    t->hp=hp;
    if(damaged&&(t->phase==DEF_WAIT_ANIMATION||t->phase==DEF_ACTIVE)){
        CopydogDefenseCancel(t,DEF_DAMAGE,now);t->previousAnimation=own;return;
    }
    if(t->phase==DEF_WAIT_ANIMATION){
        if(allowConfirmation&&CopydogDefenseAnimation(t->kind,own)){
            if(!t->haveCandidate||t->candidateAnimation!=own){
                t->candidateAnimation=own;t->candidateSince=now;t->haveCandidate=1;
            }
            int delay=(int)(t->candidateSince-t->submitted);
            if((uint32_t)(now-t->candidateSince)<COPYDOG_ANIMATION_STABLE_MS){
                t->previousAnimation=own;return;
            }
            if(delay<=COPYDOG_ACTION_CONFIRM_MS){
                t->delayMs=t->haveDelay&&t->delayKind==t->kind&&t->delayTool==t->tool?
                    (t->delayMs*3+delay)/4:delay;
                t->delayKind=t->kind;t->delayTool=t->tool;t->haveDelay=1;
                t->confirmed=t->candidateSince;t->phase=DEF_ACTIVE;t->changed=now;
                t->awaitingConfirmation=0;
                t->haveCandidate=t->haveTargetCandidate=0;++t->confirmations;
            }else CopydogDefenseCancel(t,DEF_TIMEOUT,now);
        }else {
            t->haveCandidate=0;
            if(t->target&&targetAnimation!=t->targetAnimation){
                if(!t->haveTargetCandidate||t->targetCandidate!=targetAnimation){
                    t->haveTargetCandidate=1;t->targetCandidate=targetAnimation;t->targetCandidateSince=now;
                }else if((uint32_t)(now-t->targetCandidateSince)>=COPYDOG_ANIMATION_STABLE_MS)
                    CopydogDefenseCancel(t,DEF_TARGET_CHANGED,now);
            }else t->haveTargetCandidate=0;
            if(t->phase==DEF_WAIT_ANIMATION&&(uint32_t)(now-t->submitted)>COPYDOG_ACTION_CONFIRM_MS)
                CopydogDefenseCancel(t,DEF_TIMEOUT,now);
        }
    }else if(t->phase==DEF_ACTIVE){
        int confirmedId=t->candidateAnimation%100000;
        int finishedRoll=ready&&t->kind==DEF_ROLL&&confirmedId>=27100&&confirmedId<27200&&
                         own>=0&&!CopydogDefenseAnimation(DEF_ROLL,own);
        if(finishedRoll||DefenseAnimationEnded(t,own,now)){
            t->phase=DEF_IDLE;t->changed=now;t->haveCandidate=0;
        }
        if(t->phase==DEF_ACTIVE&&(uint32_t)(now-t->confirmed)>3000)CopydogDefenseCancel(t,DEF_TIMEOUT,now);
    }else if(t->phase==DEF_CANCELLED){
        /* A cancelled, unconfirmed request can still reach the native queue. */
        int ended=DefenseAnimationEnded(t,own,now);
        if(ended&&(!t->awaitingConfirmation||(uint32_t)(now-t->submitted)>=COPYDOG_ACTION_CONFIRM_MS)){
            t->phase=DEF_IDLE;t->changed=now;t->haveCandidate=0;t->awaitingConfirmation=0;
        }
    }
    t->previousAnimation=own;
}
void CopydogDefenseObserve(COPYDOG_DEFENSE*t,uintptr_t self,int own,int hp,int targetAnimation,int ready,uint32_t now)
{
    CopydogDefenseObserveAttributed(t,self,own,hp,targetAnimation,ready,now,1);
}
