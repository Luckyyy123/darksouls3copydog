#ifndef COPYDOG_PREDICTION_H
#define COPYDOG_PREDICTION_H
#include <stdint.h>
enum { CD_PRED_UNKNOWN=-1, CD_PRED_CLEAR=0, CD_PRED_POSSIBLE=1, CD_PRED_CONTACT=2 };
typedef struct { double a[3],b[3],radius; } CD_PRED_CAPSULE;
typedef struct {
    uintptr_t clock;
    int index,animation;
    float previous,current,duration;
} CD_NATIVE_FRAME;
typedef struct {
    const CD_NATIVE_FRAME* frame;
    int weapon,mode;
    double rate,begin,end,horizon;
    unsigned* budget;
    uint32_t phaseTick;
} CD_PRED_PARRY_BAND;
/* Stable diagnostic values recorded in the parry trace. */
enum {
    CD_PRED_UNK_NONE=0, CD_PRED_UNK_OTHER=1, CD_PRED_UNK_POSE=2,
    CD_PRED_UNK_SHAPE=3, CD_PRED_UNK_HORIZON=4, CD_PRED_UNK_TURN_LIMIT=5,
    CD_PRED_UNK_BUDGET=6, CD_PRED_UNK_NARROW_PHASE=7
};
typedef struct { double contact,clearThrough;int complete,turning;unsigned unknownReason; } CD_PRED_PARRY_RESULT;
/* Endpoints move linearly over duration milliseconds. Work is bounded. */
int CopydogPredictionSweep(const CD_PRED_CAPSULE*,const CD_PRED_CAPSULE*,
    const CD_PRED_CAPSULE*,const CD_PRED_CAPSULE*,double,double*,unsigned*);
void CopydogPredictionObserve(unsigned,uintptr_t,uint32_t);
void CopydogPredictionObserveFrame(unsigned,uintptr_t,uint32_t,int);
void CopydogPredictionObserveAttackFrame(unsigned slot,uintptr_t actor,uint32_t now,int allowRetained,int expected,uint32_t episode);
int CopydogPredictionQuery(unsigned,uintptr_t,uintptr_t,uint32_t,double*,int*);
int CopydogPredictionForecast(unsigned,uintptr_t,uintptr_t,uint32_t,double,double*,int*);
int CopydogPredictionParryContact(unsigned,uintptr_t,uintptr_t,uint32_t,const CD_PRED_PARRY_BAND*,CD_PRED_PARRY_RESULT*);
int CopydogPredictionAttackParryContact(unsigned,uintptr_t,uintptr_t,uint32_t,const CD_PRED_PARRY_BAND*,CD_PRED_PARRY_RESULT*);
int CopydogPredictionHasAttackShape(int weapon,int animation,int mode);
int CopydogPredictionAttackMotionReady(unsigned slot,uintptr_t actor,const CD_NATIVE_FRAME*frame,int weapon,int mode,uint32_t now);
void CopydogPredictionConsume(unsigned);
void CopydogPredictionEpisode(unsigned,uintptr_t,uint32_t);
void CopydogPredictionResetAll(void);
int CopydogReadNativeClock(uintptr_t,int*,float*);
int CopydogReadNativeFrame(uintptr_t,CD_NATIVE_FRAME*);
int CopydogReadLatestNativeFrame(uintptr_t,CD_NATIVE_FRAME*,int*retained);
/* The newest published slot owns the attack clock during a blend. An empty
   queue can report its last entry as history only when retained is supplied. */
int CopydogReadPublishedNativeFrame(uintptr_t,CD_NATIVE_FRAME*,int*retained);
int CopydogReadAttackNativeFrame(uintptr_t,int expected,CD_NATIVE_FRAME*,int*retained);
uint32_t CopydogPredictionFrameOrigin(unsigned slot,uintptr_t actor,const CD_NATIVE_FRAME*,uint32_t observed,uint32_t now);
uint32_t CopydogPredictionAttackFrameOrigin(unsigned slot,uintptr_t actor,const CD_NATIVE_FRAME*,uint32_t observed,uint32_t now);
int CopydogPredictionBullet(uintptr_t,const float[3],const float[3],float,uint32_t,double*);
#endif
