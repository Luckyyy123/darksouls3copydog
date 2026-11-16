#include "parry_empirical.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "parry_empirical_profiles.h"

unsigned CopydogEmpiricalProfileCount(void)
{
    return (unsigned)(sizeof EmpiricalProfiles / sizeof *EmpiricalProfiles);
}

const COPYDOG_EMPIRICAL_PROFILE *CopydogEmpiricalProfileAt(unsigned index)
{
    return index < CopydogEmpiricalProfileCount() ? &EmpiricalProfiles[index] : NULL;
}

const char *CopydogEmpiricalDispatchSource(int haveDispatch)
{
    return haveDispatch ? "measured_local_request_to_animation"
                        : "uncalibrated_dispatch_zero_startup_seed";
}

int CopydogEmpiricalPlan(int tool, int targetWeapon, int animation,
    int haveDispatch, int dispatchMs, int ahead, float distanceSquared,
    int *start, int *end, int *profileIndex)
{
    return CopydogEmpiricalPlanShifted(tool, targetWeapon, animation,
        haveDispatch, dispatchMs, ahead, distanceSquared, 0, start, end, profileIndex);
}

int CopydogEmpiricalPlanShifted(int tool, int targetWeapon, int animation,
    int haveDispatch, int dispatchMs, int ahead, float distanceSquared, int clockLeadMs,
    int *start, int *end, int *profileIndex)
{
    if (start) *start = -1;
    if (end) *end = -1;
    if (profileIndex) *profileIndex = -1;
    if (!start || !end || !profileIndex || tool <= 0 || targetWeapon <= 0 ||
        animation < 0 || ahead < 0 || (haveDispatch && dispatchMs < 0) ||
        clockLeadMs < 0 || clockLeadMs > 600 ||
        !isfinite(distanceSquared) || distanceSquared < 0) return 0;

    tool = tool / 10000 * 10000;
    targetWeapon = targetWeapon / 10000 * 10000;
    for (unsigned i = 0; i < CopydogEmpiricalProfileCount(); ++i) {
        const COPYDOG_EMPIRICAL_PROFILE *p = &EmpiricalProfiles[i];
        if (p->toolBase != tool || p->targetWeaponBase != targetWeapon ||
            p->animation != animation) continue;
        if (distanceSquared < p->minDistanceSquared ||
            distanceSquared > p->maxDistanceSquared) return 0;
        *profileIndex = (int)i;
        int64_t shift = (int64_t)clockLeadMs - (haveDispatch ? (int64_t)dispatchMs : 0) - (int64_t)ahead;
        double late = p->lateMs + (double)shift;
        if (late < 0.0) return -1;
        double request = p->targetMs + (double)shift;
        if (request < 0.0) request = 0.0;
        int first = (int)ceil(request);
        int last = (int)floor(late);
        if (first > last) return -1;
        if (last > first + 16) last = first + 16;
        *start = first;
        *end = last;
        return 1;
    }
    return 0;
}
