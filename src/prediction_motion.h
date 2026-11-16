#ifndef COPYDOG_PREDICTION_MOTION_H
#define COPYDOG_PREDICTION_MOTION_H
#include "prediction.h"

/* Distances use world units, time uses milliseconds. Pivot, arm and bone
   axis are expressed in the world-facing basis at the observation time. */
typedef struct {
    double root[3],rootVelocity[3],pivot[3],pivotVelocity[3],arm[3],axis[3];
    double yawRate,boneRate;
} CD_PRED_TURN_POINT;
typedef struct { CD_PRED_TURN_POINT a,b;double radius; } CD_PRED_TURN_CAPSULE;
int CopydogPredictionTurningSweep(const CD_PRED_TURN_CAPSULE*a,double ageA,
    const CD_PRED_TURN_CAPSULE*b,double ageB,double duration,double*out,unsigned*budget);
/* For CONTACT, out is the earliest bracket's lower bound and witness is an
   overlapping sample in that bracket. Only CONTACT gives witness meaning. */
int CopydogPredictionTurningSweepWitness(const CD_PRED_TURN_CAPSULE*a,double ageA,
    const CD_PRED_TURN_CAPSULE*b,double ageB,double duration,double*out,unsigned*budget,double*witness);
#endif
