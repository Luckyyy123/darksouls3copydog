#ifndef COPYDOG_CONTROL_H
#define COPYDOG_CONTROL_H
#include <stdint.h>
#define COPYDOG_TRACKED_ACTORS 6
typedef struct {uintptr_t actor;int animation,backstabSpent;} COPYDOG_ACTOR_GATE;
typedef struct {COPYDOG_ACTOR_GATE actors[COPYDOG_TRACKED_ACTORS];} COPYDOG_ROLL_GATE;
typedef struct {int held;} COPYDOG_COPY_KEY;
int CopydogCriticalMovementException(int,unsigned char,unsigned char,float,unsigned char);
int CopydogIsParryRecovery(int);
enum { RIPOSTE_CRITICAL_NONE,RIPOSTE_CRITICAL_WAIT,RIPOSTE_CRITICAL_CONFIRMED,RIPOSTE_CRITICAL_FAILED };
enum { RIPOSTE_FAIL_NONE,RIPOSTE_FAIL_TIMEOUT,RIPOSTE_FAIL_NORMAL_ATTACK,RIPOSTE_FAIL_INVALIDATED,RIPOSTE_FAIL_NATIVE_REJECTED };
typedef struct {
 uintptr_t self,actor;
 uint32_t lastAttack,armed;
 int haveAttack,previousSelf,previousTarget,pending,staggered,spent;
 uint32_t parrySince,parryLastSeen,staggerSince;
 int parryCandidate,parryLatched,staggerCandidate;
 uint32_t requested;
 int haveRequest;
 uint32_t suppressClearSince;
 int suppressed,suppressClearing;
 uint32_t criticalRequested,criticalConfirmed;
 int criticalState,criticalFailure;
} COPYDOG_RIPOSTE_TRACK;
void CopydogRiposteInvalidate(COPYDOG_RIPOSTE_TRACK*,uintptr_t,int,uint32_t);
void CopydogRiposteSubmitted(COPYDOG_RIPOSTE_TRACK*,uintptr_t,uintptr_t,int,int,uint32_t);
int CopydogObserveRiposte(COPYDOG_RIPOSTE_TRACK*,uintptr_t,uintptr_t,int,int,int,uint32_t);
int CopydogRiposteRequestCritical(COPYDOG_RIPOSTE_TRACK*,uint32_t);
typedef struct {uintptr_t self;uint32_t submitted,lastBusy;int pending,sawBusy;} COPYDOG_BACKSTAB_TRANSACTION;
int CopydogBackstabLocked(COPYDOG_BACKSTAB_TRANSACTION*,uintptr_t,int,uint32_t);
void CopydogBackstabSubmitted(COPYDOG_BACKSTAB_TRANSACTION*,uintptr_t,uint32_t);
void CopydogTrackActor(COPYDOG_ROLL_GATE*,unsigned,uintptr_t,int,int);
int CopydogAllowBackstab(COPYDOG_ROLL_GATE*,unsigned,int);
int CopydogToggleKey(COPYDOG_COPY_KEY*,int,int,int);
int CopydogCopyKey(COPYDOG_COPY_KEY*,int,int,int,int);
void CopydogArmRollDirection(float,uint32_t);
void CopydogClearRollDirection(void);
float CopydogRollingAngleAt(void*,float,void*,uint32_t);
float CopydogOverrideRollingAngle(void*,float,void*);
int CopydogRelativeRollDegrees(float,float,float*);
int CopydogAwayYaw(const float[3],const float[3],float*);
#endif
