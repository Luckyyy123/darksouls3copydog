#ifndef COPYDOG_PARRY_EMPIRICAL_H
#define COPYDOG_PARRY_EMPIRICAL_H

typedef struct {
    int toolBase, targetWeaponBase, animation;
    double targetMs, earlyMs, lateMs;
    float minDistanceSquared, maxDistanceSquared;
    unsigned successCount, failedCount, nonnegativeSuccessCount;
    const char *source;
    const char *parryVariants;
} COPYDOG_EMPIRICAL_PROFILE;

unsigned CopydogEmpiricalProfileCount(void);
const COPYDOG_EMPIRICAL_PROFILE *CopydogEmpiricalProfileAt(unsigned index);
const char *CopydogEmpiricalDispatchSource(int haveDispatch);

/* 0: no applicable data/invalid input; 1: request window; -1: measured
   corridor cannot be reached. Min/max are observations, not guaranteed edges.
   haveDispatch=0 uses an explicitly uncalibrated zero-delay startup seed. */
int CopydogEmpiricalPlan(int tool, int targetWeapon, int animation,
    int haveDispatch, int dispatchMs, int ahead, float distanceSquared,
    int *start, int *end, int *profileIndex);
/* clockLeadMs is measured state-name to animation-ID lead, in [0,600].
   It shifts both raw target and late edge before reachability/clipping. */
int CopydogEmpiricalPlanShifted(int tool, int targetWeapon, int animation,
    int haveDispatch, int dispatchMs, int ahead, float distanceSquared, int clockLeadMs,
    int *start, int *end, int *profileIndex);

#endif
