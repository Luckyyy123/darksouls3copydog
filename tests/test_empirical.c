#include "../src/parry_empirical.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    const int tools[] = {11010000, 20080000};
    const int weapons[] = {6020000, 16070000, 4070000};
    const int animations[] = {25034000, 23030000, 164034000};
    const double expected[2][3] = {{45.0, 50.0, 36.03}, {35.0, 53.96, 57.266}};
    int start, end, profile, oldStart, oldEnd, oldProfile;
    CHECK(CopydogEmpiricalProfileCount() == 6);
    CHECK(CopydogEmpiricalProfileAt(6) == NULL);
    CHECK(strstr(CopydogEmpiricalDispatchSource(0), "uncalibrated") != NULL);
    CHECK(strcmp(CopydogEmpiricalDispatchSource(1), "measured_local_request_to_animation") == 0);
    for (unsigned tool = 0; tool < 2; ++tool) {
        for (unsigned weapon = 0; weapon < 3; ++weapon) {
            CHECK(CopydogEmpiricalPlan(tools[tool], weapons[weapon], animations[weapon],
                                      0, 154, 0, 1.0f, &start, &end, &profile) == 1);
            const COPYDOG_EMPIRICAL_PROFILE *p = CopydogEmpiricalProfileAt((unsigned)profile);
            CHECK(p && p->toolBase == tools[tool] && p->targetWeaponBase == weapons[weapon]);
            CHECK(fabs(p->targetMs - expected[tool][weapon]) < 0.000001);
            CHECK(p->earlyMs >= 0 && p->earlyMs <= p->targetMs && p->targetMs <= p->lateMs);
            CHECK(p->successCount >= p->nonnegativeSuccessCount && p->nonnegativeSuccessCount > 0);
            CHECK(strstr(p->source, "archived-private-measurements/") != NULL);
            CHECK((strstr(p->source, "recommendations.json") != NULL) ==
                  (weapon == 0 || (tool == 0 && weapon == 1)));
            CHECK(start == (int)ceil(p->targetMs) && end >= start && end - start <= 16);
            CHECK(end <= (int)floor(p->lateMs));
            oldStart = start; oldEnd = end; oldProfile = profile;
            CHECK(CopydogEmpiricalPlan(tools[tool] + 310, weapons[weapon] + 310, animations[weapon],
                                      0, 999, 0, 1.0f, &start, &end, &profile) == 1);
            CHECK(start == oldStart && end == oldEnd && profile == oldProfile);
            CHECK(CopydogEmpiricalPlan(tools[tool], weapons[weapon], animations[weapon],
                                      1, 10, 5, 1.0f, &start, &end, &profile) == 1);
            CHECK(start == (int)ceil(p->targetMs - 15) && end <= (int)floor(p->lateMs - 15));
            CHECK(CopydogEmpiricalPlan(tools[tool], weapons[weapon], animations[weapon],
                                      1, (int)ceil(p->targetMs) + 1, 0, 1.0f, &start, &end, &profile) == 1);
            CHECK(start == 0 && end <= 16 && end <= (int)floor(p->lateMs - ceil(p->targetMs) - 1));
            CHECK(CopydogEmpiricalPlan(tools[tool], weapons[weapon], animations[weapon],
                                      1, (int)floor(p->lateMs), 0, 1.0f, &start, &end, &profile) == 1);
            CHECK(start == 0 && end == 0);
            CHECK(CopydogEmpiricalPlan(tools[tool], weapons[weapon], animations[weapon],
                                      1, (int)ceil(p->lateMs), 0, 1.0f, &start, &end, &profile) == -1);
            CHECK(start == -1 && end == -1 && profile == oldProfile);
            CHECK(CopydogEmpiricalPlan(tools[tool], weapons[weapon], animations[weapon],
                                      0, 0, (int)ceil(p->lateMs), 1.0f, &start, &end, &profile) == -1);
            CHECK(CopydogEmpiricalPlan(tools[tool], weapons[weapon], animations[weapon] + 10,
                                      0, 0, 0, 1.0f, &start, &end, &profile) == 0);
            CHECK(CopydogEmpiricalPlan(tools[tool], weapons[weapon], animations[weapon] + 1000,
                                      0, 0, 0, 1.0f, &start, &end, &profile) == 0);
        }
    }
    CHECK(CopydogEmpiricalPlan(20080310, 4070005, 164034000, 0, 0, 0, 1, &start, &end, &profile) == 1);
    const COPYDOG_EMPIRICAL_PROFILE *curved = CopydogEmpiricalProfileAt((unsigned)profile);
    CHECK(curved->successCount == 21 && curved->failedCount == 9 && curved->nonnegativeSuccessCount == 17);
    CHECK(strstr(curved->parryVariants, "48032110") && strstr(curved->parryVariants, "48032120"));
    CHECK(CopydogEmpiricalPlan(20080010, 6020310, 25034000, 0, 0, 0, 0.49f, &start, &end, &profile) == 1);
    CHECK(CopydogEmpiricalPlan(20080010, 6020310, 25034000, 0, 0, 0, 1.69f, &start, &end, &profile) == 1);
    CHECK(CopydogEmpiricalPlan(20080010, 6020310, 25034000, 0, 0, 0, 0.48f, &start, &end, &profile) == 0);
    CHECK(CopydogEmpiricalPlan(20080010, 6020310, 25034000, 0, 0, 0, 1.70f, &start, &end, &profile) == 0);
    CHECK(CopydogEmpiricalPlan(20080010, 6030310, 25034000, 0, 0, 0, 1, &start, &end, &profile) == 0);
    CHECK(CopydogEmpiricalPlan(20090010, 6020310, 25034000, 0, 0, 0, 1, &start, &end, &profile) == 0);
    CHECK(CopydogEmpiricalPlan(20080010, 6020310, 23030000, 0, 0, 0, 1, &start, &end, &profile) == 0);
    CHECK(CopydogEmpiricalPlan(20080010, 6020310, 25034000, 1, -1, 0, 1, &start, &end, &profile) == 0);
    CHECK(CopydogEmpiricalPlan(20080010, 6020310, 25034000, 0, -1, 0, 1, &start, &end, &profile) == 1);
    CHECK(CopydogEmpiricalPlan(20080010, 6020310, 25034000, 0, 0, -1, 1, &start, &end, &profile) == 0);
    CHECK(CopydogEmpiricalPlan(20080010, 6020310, 25034000, 0, 0, 0, NAN, &start, &end, &profile) == 0);
    CHECK(CopydogEmpiricalPlan(20080010, 6020310, 25034000, 0, 0, 0, INFINITY, &start, &end, &profile) == 0);
    CHECK(CopydogEmpiricalPlan(20080010, 6020310, 25034000, 0, 0, 0, 1, NULL, &end, &profile) == 0);
    printf("PASS %u empirical profile assertions\n", checks);
    return 0;
}
