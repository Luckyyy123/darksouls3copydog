#ifndef COPYDOG_DEFENSE_H
#define COPYDOG_DEFENSE_H
#include <stdint.h>
enum { DEF_IDLE,DEF_WAIT_ANIMATION,DEF_ACTIVE,DEF_CANCELLED };
enum { DEF_ROLL=1,DEF_PARRY=2 };
enum { COPYDOG_ACTION_CONFIRM_MS=400 };
enum { DEF_OK,DEF_DAMAGE,DEF_TARGET_CHANGED,DEF_TIMEOUT,DEF_SELF_CHANGED,DEF_WRONG_ACTION };
typedef struct {
    uintptr_t self,target;
    int phase,kind,tool,hp,previousAnimation,targetAnimation,reason;
    int delayTool,delayKind,delayMs,haveDelay;
    uint32_t submitted,confirmed,changed;
    uint64_t requests,confirmations,cancellations;
    int candidateAnimation,haveCandidate,targetCandidate,haveTargetCandidate;
    uint32_t candidateSince,targetCandidateSince;
    int awaitingConfirmation;
} COPYDOG_DEFENSE;
int CopydogDefenseAnimation(int,int);
int CopydogDefenseBegin(COPYDOG_DEFENSE*,uintptr_t,uintptr_t,int,int,int,int,int,uint32_t);
void CopydogDefenseObserve(COPYDOG_DEFENSE*,uintptr_t,int,int,int,int,uint32_t);
void CopydogDefenseObserveAttributed(COPYDOG_DEFENSE*,uintptr_t,int,int,int,int,uint32_t,int);
int CopydogDefenseBusy(const COPYDOG_DEFENSE*);
int CopydogDefenseDue(int,int,int,int,int,int);
void CopydogDefenseCancel(COPYDOG_DEFENSE*,int,uint32_t);
#endif
