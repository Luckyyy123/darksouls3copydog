#include <windows.h>
static int shiftDown;
static SHORT WINAPI BurstKey(int key) { return key == VK_SHIFT && shiftDown ? (SHORT)0x8000 : 0; }
#define GetAsyncKeyState BurstKey
#define CROSSBOW_FIXTURE_ENTRY UnusedBurstCrossbowFixture
#include "test_crossbow_roll.c"
/* The callback and input loop share the fixture's millisecond clock. Link
   the other production units normally; combat.c is already present here. */
#include "../src/combat.c"

typedef struct { char data[0x500]; } BurstObject;
enum { HOST = 0x12340000, HOSTILE = 0x12340007 };

static void sample(BurstObject *obj, int id, int owner, int state, float x, float y, float vx, float vy)
{
    integer(obj->data + 0x4e4, id);
    integer(obj->data + 0x4ec, owner);
    integer(obj->data + 0x4dc, state);
    real(obj->data + 0x10, x); real(obj->data + 0x14, y); real(obj->data + 0x18, 0.f);
    real(obj->data + 0x30, vx); real(obj->data + 0x34, vy); real(obj->data + 0x38, 0.f);
    CombatObserveBullet(obj->data);
}

static void reset(SCENE *s)
{
    fakeNow += 2000;
    CombatClearBulletThreat();
    CombatSetBulletInputPaused(0);
    CombatFrameClaimed = 0;
    Defense = (COPYDOG_DEFENSE){0};
    Config.BulletDodge = 1;
    atomic_store(&AutoRollEnabled, 1);
    shiftDown = 0;
    for (unsigned axis = 0; axis < 3; ++axis) real(s->pos + 0x80 + 4 * axis, 0.f);
    UpdateDefenseState(fakeNow);
    CombatSamplePlayerMotion(s->me, fakeNow - 24);
    CombatSamplePlayerMotion(s->me, fakeNow);
}

static int landingCases(SCENE *s)
{
    BurstObject b = {0};
    const int parent[] = {860, 865, 880};
    for (unsigned i = 0; i < _countof(parent); ++i) {
        reset(s);
        /* A .9 lateral offset clears the narrow bolt; its ground child can reach.
           At the second observation the foot-plane crossing is 18ms away. */
        sample(&b, parent[i], HOSTILE, 1, .9f, .90f, 0.f, -30.f);
        CHECK(!CombatPeekBulletThreat(fakeNow));
        fakeNow += 12;
        sample(&b, parent[i], HOSTILE, 1, .9f, .54f, 0.f, -30.f);
        float yaw;
        CHECK(CombatTakeDueBulletDirection(fakeNow, 100, &yaw) && isfinite(yaw));
        CHECK(-sinf(yaw) < -.99f);
        uint64_t packed = atomic_load(&CopydogLastAreaThreat);
        CHECK((packed >> 32) == (unsigned)parent[i] && ((packed >> 16) & 65535u) == CD_AREA_FOOT_PLANE);
        CHECK(!CombatPeekBulletThreat(fakeNow));
    }
    for (int scenario = 0; scenario < 4; ++scenario) {
        reset(s);
        int id = scenario == 2 ? 800 : 860;
        float vy = scenario == 0 ? 0.f : -30.f;
        sample(&b, id, HOSTILE, 1, .9f, .90f, 0.f, vy);
        fakeNow += scenario == 3 ? 80 : 12;
        float y = scenario == 0 ? .90f : scenario == 1 ? 1.20f : .54f;
        sample(&b, id, HOSTILE, 1, scenario == 0 ? 1.26f : .9f, y,
               scenario == 0 ? 30.f : 0.f, vy);
        CHECK(!CombatPeekBulletThreat(fakeNow));
    }
    for (int climbing = 0; climbing <= 1; ++climbing) {
        reset(s);
        CombatClearBulletThreat();
        CombatSamplePlayerMotion(s->me, fakeNow - 24);
        if (climbing) {
            real(s->pos + 0x84, .15f);
            CombatSamplePlayerMotion(s->me, fakeNow);
        }
        sample(&b, 860, HOSTILE, 1, .9f, .90f, 0.f, -30.f);
        fakeNow += 12;
        sample(&b, 860, HOSTILE, 1, .9f, .54f, 0.f, -30.f);
        CHECK(!CombatPeekBulletThreat(fakeNow));
    }
    return 0;
}

static int observedBursts(SCENE *s)
{
    BurstObject b = {0};
    reset(s);
    sample(&b, 861, HOSTILE, 1, 2.f, 0.f, 0.f, 0.f);
    CHECK(!CombatPeekBulletThreat(fakeNow));
    ++fakeNow;
    sample(&b, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(CombatTakeDueBulletDirection(fakeNow, 0, NULL));
    ++fakeNow;
    sample(&b, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(!CombatPeekBulletThreat(fakeNow));
    fakeNow += 144; /* Ordinary 128ms slot TTL elapsed; this area lives up to 250ms. */
    sample(&b, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(!CombatPeekBulletThreat(fakeNow));
    /* A terminal record permits an immediately reused slot with identical owner/ID. */
    sample(&b, 861, HOSTILE, 4, .75f, 0.f, 0.f, 0.f);
    ++fakeNow;
    sample(&b, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(CombatTakeBulletThreat(fakeNow));
    const int inert[] = {862, 867, 800, 9999999};
    for (unsigned i = 0; i < _countof(inert); ++i) {
        reset(s);
        sample(&b, inert[i], HOSTILE, 1, .15f, 0.f, 0.f, 0.f);
        CHECK(!CombatPeekBulletThreat(fakeNow));
    }
    reset(s);
    sample(&b, 704, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(CombatTakeDueBulletDirection(fakeNow, 0, NULL));
    reset(s);
    sample(&b, 861, HOSTILE, 1, 1.25f, 0.f, 0.f, 0.f);
    CD_BULLET_TICKET unknown;
    CHECK(CombatSelectDueBullet(fakeNow, 100, &unknown) && unknown.areaSource == CD_AREA_AGE_UNKNOWN);
    return 0;
}

static uint32_t plant(BurstObject *parent, BurstObject *anchor, int parentId, int anchorId, float x)
{
    sample(parent, parentId, HOSTILE, 1, x, .72f, 0.f, -30.f);
    fakeNow += 12;
    sample(parent, parentId, HOSTILE, 1, x, .36f, 0.f, -30.f);
    fakeNow += 12;
    sample(anchor, anchorId, HOSTILE, 1, x, 0.f, 0.f, 0.f);
    return fakeNow;
}

static int timedAnchors(SCENE *s, int parentId, int anchorId, int retireFirst)
{
    BurstObject parentA = {0}, parentB = {0}, a = {0}, b = {0};
    reset(s);
    const uint32_t atA = plant(&parentA, &a, parentId, anchorId, -.95f);
    uint32_t atB = 0;
    int sawA = 0, sawB = 0;
    CHECK(!CombatPeekBulletThreat(fakeNow));
    for (unsigned i = 1; i <= 165; ++i) {
        fakeNow += 12;
        if (i == 25) atB = plant(&parentB, &b, parentId, anchorId, .95f);
        unsigned ageA = fakeNow - atA;
        if (retireFirst && i == 60)
            sample(&a, anchorId, HOSTILE, 4, -.95f, 0.f, 0.f, 0.f);
        if ((!retireFirst || i < 60) && ageA < 1490)
            sample(&a, anchorId, HOSTILE, 1, -.95f, 0.f, 0.f, 0.f);
        if (atB) sample(&b, anchorId, HOSTILE, 1, .95f, 0.f, 0.f, 0.f);

        if (ageA < 1100) {
            CHECK(!CombatTakeDueBulletDirection(fakeNow, 180, NULL));
        } else if (ageA < 1490) {
            int due = CombatTakeDueBulletDirection(fakeNow, 100, NULL);
            if (due) {
                CHECK(!retireFirst && !sawA && ageA >= 1380);
                sawA = 1;
            }
        } else if (atB) {
            unsigned ageB = fakeNow - atB;
            int due = CombatTakeDueBulletDirection(fakeNow, 100, NULL);
            if (due) {
                CHECK(!sawB && ageB >= 1380 && ageB <= 1512);
                sawB = 1;
            }
        }
    }
    CHECK(sawA == !retireFirst && sawB);
    return 0;
}

static int incompleteFuse(SCENE *s)
{
    enum { NO_PARENT, DISTANT_PARENT, AMBIGUOUS, PAUSED, REUSED, ONE_OBSERVATION, CONTRARY_MOTION };
    for (int scenario = NO_PARENT; scenario <= CONTRARY_MOTION; ++scenario) {
        BurstObject p = {0}, q = {0}, a = {0};
        reset(s);
        if (scenario != NO_PARENT) {
            float x = scenario == DISTANT_PARENT ? 5.f : .75f;
            if (scenario != ONE_OBSERVATION)
                sample(&p, 706, HOSTILE, 1, x, scenario == CONTRARY_MOTION ? .18f : .72f, 0.f, -30.f);
            if (scenario == AMBIGUOUS) sample(&q, 706, HOSTILE, 1, x, .72f, 0.f, -30.f);
            fakeNow += 12;
            sample(&p, 706, HOSTILE, 1, x, .36f, 0.f, -30.f);
            if (scenario == AMBIGUOUS) sample(&q, 706, HOSTILE, 1, x, .36f, 0.f, -30.f);
        }
        fakeNow += 12;
        sample(&a, 705, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
        for (unsigned i = 1; i <= 140; ++i) {
            fakeNow += 12;
            if (i == 45 && scenario == PAUSED) CombatSetBulletInputPaused(1);
            if (i == 48 && scenario == PAUSED) CombatSetBulletInputPaused(0);
            if (i == 45 && scenario == REUSED) {
                sample(&a, 705, HOSTILE, 4, .75f, 0.f, 0.f, 0.f);
                continue;
            }
            sample(&a, 705, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
            if (CombatTakeDueBulletDirection(fakeNow, 100, NULL)) {
                fprintf(stderr, "unexpected fuse for incomplete chain scenario %d, age %u ms\n",
                        scenario, i * 12);
                return 1;
            }
            ++checks;
        }
    }
    return 0;
}

static int inputAndOwner(SCENE *s)
{
    BurstObject b = {0};
    reset(s);
    sample(&b, 861, HOST, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(!CombatPeekBulletThreat(fakeNow));
    sample(&b, 861, 53250, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(!CombatPeekBulletThreat(fakeNow));
    int before = calls;
    atomic_store(&AutoRollEnabled, 0);
    UpdateRollKey();
    sample(&b, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(!TryBulletAutoRoll(fakeNow) && calls == before);
    atomic_store(&AutoRollEnabled, 1);
    shiftDown = 1;
    UpdateRollKey();
    sample(&b, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(!TryBulletAutoRoll(fakeNow) && calls == before);
    shiftDown = 0;
    UpdateRollKey();
    CHECK(!CombatPeekBulletThreat(fakeNow));
    sample(&b, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    uint64_t accepted = atomic_load(&CopydogAreaDiagnostics[3]);
    CD_BULLET_TICKET candidate;
    CHECK(CombatSelectDueBullet(fakeNow, 100, &candidate) && candidate.areaSource == CD_AREA_OBSERVED);
    CHECK(atomic_load(&CopydogAreaDiagnostics[3]) == accepted);
    CHECK(TryBulletAutoRoll(fakeNow) && calls == before + 1 && lastAction == ActionRollForward);
    CHECK(atomic_load(&CopydogAreaDiagnostics[3]) == accepted + 1);
    reset(s);
    sample(&b, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(CombatSelectDueBullet(fakeNow, 100, &candidate));
    pointer(s->sub + 0xb8, NULL);
    CHECK(!TryBulletAutoRoll(fakeNow) && calls == before + 1);
    CHECK(atomic_load(&CopydogAreaDiagnostics[3]) == accepted + 1);
    pointer(s->sub + 0xb8, s->queue);
    reset(s);
    sample(&b, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(CombatSelectDueBullet(fakeNow, 100, &candidate));
    Defense = (COPYDOG_DEFENSE){0}; /* Nested owner binding invalidates the selected ticket. */
    CHECK(!TryBulletAutoRoll(fakeNow) && calls == before + 1);
    CHECK(atomic_load(&CopydogAreaDiagnostics[3]) == accepted + 1);
    return 0;
}

static int lifetimeOrderAndReuse(SCENE *s)
{
    BurstObject b = {0};
    reset(s);
    uint32_t origin = fakeNow;
    fakeNow = origin + 100;
    sample(&b, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(CombatPeekBulletThreat(fakeNow));
    fakeNow = origin + 120;
    sample(&b, 861, HOSTILE, 4, .75f, 0.f, 0.f, 0.f);
    CHECK(!CombatPeekBulletThreat(fakeNow));
    fakeNow = origin + 110; /* Earlier callback reaches the shared publication state late. */
    sample(&b, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    fakeNow = origin + 120;
    CHECK(!CombatPeekBulletThreat(fakeNow));

    reset(s);
    sample(&b, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(CombatTakeBulletThreat(fakeNow));
    fakeNow += 4;
    sample(&b, 800, HOSTILE, 1, 5.f, .8f, 30.f, 0.f);
    CHECK(!CombatPeekBulletThreat(fakeNow));
    fakeNow += 4;
    sample(&b, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    CHECK(CombatTakeBulletThreat(fakeNow));
    CHECK(!CombatTakeBulletThreat(fakeNow));
    return 0;
}

static int crowdedCarriers(SCENE *s)
{
    BurstObject parents[32] = {0}, burst = {0};
    reset(s);
    for (unsigned frame = 0; frame < 2; ++frame) {
        fakeNow += 12;
        for (unsigned i = 0; i < _countof(parents); ++i) {
            sample(&parents[i], 706, HOSTILE, 1, 5.f + (float)i, .9f - .36f * frame, 0.f, -30.f);
        }
    }
    CHECK(!CombatPeekBulletThreat(fakeNow));
    sample(&burst, 861, HOSTILE, 1, .75f, 0.f, 0.f, 0.f);
    CD_BULLET_TICKET selected;
    CHECK(CombatSelectDueBullet(fakeNow, 0, &selected) && selected.object == (uintptr_t)burst.data &&
          selected.id == 861 && selected.areaSource == CD_AREA_OBSERVED);

    const int crossing[] = {706, 736, 800};
    for (unsigned i = 0; i < _countof(crossing); ++i) {
        reset(s);
        sample(&parents[0], crossing[i], HOSTILE, 1, -1.2f, .8f, 30.f, 0.f);
        fakeNow += 12;
        sample(&parents[0], crossing[i], HOSTILE, 1, -.84f, .8f, 30.f, 0.f);
        /* Same centerline and speed; only the ordinary 800 bolt has direct damage. */
        CHECK(CombatPeekBulletThreat(fakeNow) == (crossing[i] == 800));
    }
    return 0;
}

int main(void)
{
    void *globals = VirtualAlloc((void *)0x144770000, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(globals == (void *)0x144770000);
    SCENE s = {0};
    setup(&s);
    integer(s.me + 8, HOST); integer(s.you + 8, HOSTILE);
    integer(s.me + 0x78, 53249); integer(s.you + 0x78, 53250);
    if (landingCases(&s) || observedBursts(&s)) return 1;
    const int chains[][2] = {{706, 705}, {716, 715}, {726, 725}, {736, 735}};
    for (unsigned i = 0; i < _countof(chains); ++i)
        if (timedAnchors(&s, chains[i][0], chains[i][1], i == 1)) return 1;
    if (inputAndOwner(&s) || incompleteFuse(&s) || lifetimeOrderAndReuse(&s) || crowdedCarriers(&s)) return 1;
    VirtualFree(globals, 0, MEM_RELEASE);
    printf("PASS %u game-projectile ground-area, delayed child and input checks\n", checks);
    return 0;
}
