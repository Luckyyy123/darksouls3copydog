#include "../src/native_parry_cue.h"
#include <stdio.h>

/* Test process only. The tokens stand in for the anonymised owner and native
   clock within one observed episode. No production actor pointers are saved. */
int main(void)
{
    CD_PARRY_CUE cue = {0};
    char command;
    while (scanf(" %c", &command) == 1) {
        if (command == 'R') {
            cue = (CD_PARRY_CUE){0};
            continue;
        }
        if (command == 'F') {
            unsigned owner, onset, now, clock;
            int handle, weapon, mode, expected, retained, index, id;
            double previous, current, duration;
            if (scanf("%u %d %d %d %d %u %u %d %u %d %d %lf %lf %lf",
                      &owner, &handle, &weapon, &mode, &expected, &onset, &now,
                      &retained, &clock, &index, &id, &previous, &current, &duration) != 14)
                return 2;
            CD_NATIVE_FRAME f = {.clock = clock, .index = index, .animation = id,
                .previous = (float)(previous / 1000.), .current = (float)(current / 1000.),
                .duration = (float)(duration / 1000.)};
            int stage = CopydogParryCueUpdate(&cue, owner, handle, weapon, mode, expected,
                                              onset, clock ? &f : NULL, retained, now);
            printf("%d\n", stage);
        } else if (command == 'W') {
            double c0, c1, next, lo, hi, midpoint;
            if (scanf("%lf %lf %lf", &c0, &c1, &next) != 3)
                return 2;
            /* User-specified synthetic activation, with zero dispatch delay. */
            int ok = CopydogDynamicParryWindow(c0, c1, 100., 140., 0., 0.,
                                               &lo, &hi, &midpoint);
            printf("%d\n", ok ? CopydogParryFrameChoiceBounds(lo, hi, next, 0) : CD_FRAME_SKIP);
        } else if (command == 'G') {
            /* A collinear displacement over [160,180] ms. Both capsules have
               zero-length axes. The combined radius is .4 and initial gap .1. */
            CD_PRED_CAPSULE a0 = {.a = {-.5, 0., 0.}, .b = {-.5, 0., 0.}, .radius = .2};
            CD_PRED_CAPSULE a1 = {.a = {-.3, 0., 0.}, .b = {-.3, 0., 0.}, .radius = .2};
            CD_PRED_CAPSULE b = {.radius = .2};
            unsigned budget = CD_PRED_WORK_BUDGET;
            double contact = -1.;
            int status = CopydogPredictionSweep(&a0, &a1, &b, &b, 20., &contact, &budget);
            printf("%d %.12f\n", status, 160. + contact);
        } else {
            return 2;
        }
    }
    return ferror(stdin) || ferror(stdout);
}
