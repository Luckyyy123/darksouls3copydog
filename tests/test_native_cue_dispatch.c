/* Historical solver comparison only: explicit offline legacy entry points.
   Current production scheduling is covered by test_sc23_parry/features.c. */
#define PREDICTION_DISPATCH_FIXTURE_ENTRY UnusedPredictionDispatchFixture
#include "test_prediction_dispatch.c"

enum { WALK = 10019001, SWING = 164034000, OTHER = 164034010, WEAPON = 4070000 };
enum Change { UNCHANGED, NATIVE_MOVE, NATIVE_REWIND, OTHER_ATTACK, CHANGED_NAME, MISSING_CLOCK, LOST_GEOMETRY };

typedef struct {
    SCENE scene;
    PredFixture defender, attacker;
    char manager[0xc0];
    const CD_PRED_SHAPE *shape;
    unsigned serial, gameTime;
} CueScene;

static void enemyName(CueScene *f, const wchar_t *name)
{
    memcpy(f->scene.theirNames + 0x898, name, (wcslen(name) + 1) * sizeof *name);
}

static void tick(CueScene *f, int native, unsigned phase, int visible, const wchar_t *name,
                 int movingBlade)
{
    SCENE *s = &f->scene;
    fakeNow += 12;
    f->gameTime += 12;
    ++f->serial;
    integer(f->manager + CD_GAME_PLAY_MS_OFFSET, (int)f->gameTime);
    CopydogObserveCadence(&GameCadence, (uintptr_t)f->manager, f->gameTime, fakeNow);
    NewCombatStamp = 1;
    CombatFrameClaimed = 0;
    integer(s->theirAnim + 0xc8, visible);
    enemyName(f, name);
    /* Model-space motion of the bound blade bone is independent of the
       defender's small root displacement. Both actors publish a new pose. */
    f->attacker.transforms[f->shape->a.bone][0] = movingBlade ? phase * .0001f : 0.f;
    real(s->pos + 0x80, .00001f * f->serial);
    publish(&f->defender, f->serial, 0, (f->gameTime - 12) / 1000.f, f->gameTime / 1000.f);
    publish(&f->attacker, f->serial, native, (phase - 12) / 1000.f, phase / 1000.f);
    ObserveParryTargets(s->me, NULL, fakeNow);
    ObserveEarlyTargets(s->me, fakeNow);
}

static int startScene(CueScene *f)
{
    memset(f, 0, sizeof *f);
    SCENE *s = &f->scene;
    setup(s);
    fakeNow += 1000;
    Defense = (COPYDOG_DEFENSE){0};
    DynamicSelf = (DynamicParrySelf){0};
    memset(DynamicTargets, 0, sizeof DynamicTargets);
    memset(ParryTrack, 0, sizeof ParryTrack);
    EarlySelf = 0;
    ActiveCueSlot = 0;
    GameCadence = (CD_GAME_CADENCE){0};
    CopydogPredictionResetAll();
    Config.KeyCondition = 0;
    Config.ParryDistance = 8.f;
    atomic_store(&AutoRollEnabled, 0);
    predSetup(&f->defender, s->me, s->mods, s->ownNames, 0);
    predSetup(&f->attacker, s->you, s->ymods, s->theirNames, WALK);
    pointer(addr2, f->manager);
    pointer(f->manager + 16, f->defender.ownEquipment);
    integer(f->defender.ownEquipment + 0x2b8, 1);
    integer(f->defender.ownEquipment + 0x32c, 11010300);
    integer(s->equipment + 0x2b8, 3);
    integer(s->equipment + 0x330, WEAPON + 5);
    for (unsigned i = 0; i < _countof(CdPredProfiles); ++i) {
        const CD_PRED_PROFILE *profile = &CdPredProfiles[i];
        if (profile->animation == SWING && profile->weapon == WEAPON &&
            profile->mode == 3 && profile->hand == 1 && profile->count) {
            f->shape = &CdPredShapes[profile->first];
            break;
        }
    }
    CHECK(f->shape && f->shape->a.bone >= 0 && f->shape->a.bone < 110);
    for (unsigned i = 0; i < 3; ++i)
        real(s->ypos + 0x80 + i * 4, CdPredBodies[0].a[i] - f->shape->a.p[i]);
    tick(f, WALK, 48, WALK, L"Move", 0);
    tick(f, WALK, 60, WALK, L"Move", 0);
    CHECK(GameCadence.running && !NativeCues[1].seen);
    return 0;
}

/* Leave the planned attack in the pending ring behind a newer native entry.
   A visible-biased search of the queue could still return the obsolete hit. */
static void changeBeforeDispatch(CueScene *f, enum Change change)
{
    if (change == CHANGED_NAME) {
        enemyName(f, L"AttackBothHeavy1Start");
    } else if (change == MISSING_CLOCK) {
        pointer(f->scene.ymods + 0x10, NULL);
    } else {
        unsigned before = f->serial % CD_PRED_CLOCK_COUNT;
        int next = change == NATIVE_MOVE ? WALK : change == NATIVE_REWIND ? SWING : OTHER;
        publish(&f->attacker, f->serial + 1, next, 0.f, .012f);
        integer(f->attacker.clock + 0xc4, (int)before);
    }
}

static int runDispatch(CueScene *f, int nameLate, enum Change change, int cancelAfterPress)
{
    if (startScene(f)) return 1;
    SCENE *s = &f->scene;
    int before = calls;
    unsigned onset = 0, planned = 0;
    FramePress press = {0};
    for (unsigned phase = 12; phase < 360; phase += 12) {
        const wchar_t *name = nameLate && phase < 60 ? L"Move" : L"AttackBothLight1";
        tick(f, SWING, phase, WALK, name, 1);
        if (phase == 12) {
            CHECK(NativeCues[1].stage == CD_CUE_SAMPLE && NativeCues[1].expected == SWING);
            onset = NativeCues[1].onset;
        }
        if (phase >= 24) {
            CHECK(NativeCues[1].stage == CD_CUE_CONFIRMED && NativeCues[1].onset == onset);
        }
        if (EvaluateParryFrame(1, s->me, s->you, fakeNow, &press) == CD_FRAME_PRESS) {
            planned = phase;
            break;
        }
    }
    CHECK(planned && calls == before && GetActorAnim(s->you) == WALK);
    /* Bit 256 is a CCD contact from the pair of real production pose tracks.
       Resource-only timing cannot satisfy this fixture's positive assertion. */
    CHECK((press.trace[15] & (CD_PARRY_TRACE_NATIVE_CUE | 256u | 16384u)) ==
          (CD_PARRY_TRACE_NATIVE_CUE | 256u | 16384u));
    const unsigned absent[]={0,CD_PARRY_COMPLETE_CLEAR,CD_PARRY_POSSIBLE_CONTACT,
                             CD_PARRY_PARTIAL_CLEAR,CD_PARRY_UNKNOWN_GEOMETRY};
    for(unsigned i=0;i<_countof(absent);i++){
        FramePress invalid=press;
        invalid.trace[15]=(press.trace[15]&~(uint64_t)CD_PARRY_CONTACT_PROOF)|absent[i];
        CHECK(!FramePressCurrent(&invalid,fakeNow));
        CHECK(!DispatchDefense(ActionLeftParry,DEF_PARRY,s->you,SWING,DynamicTargets[1].tool,
                               fakeNow,NULL,&invalid,NULL)&&calls==before);
        if(absent[i]){
            invalid.trace[15]|=CD_PARRY_CONTACT_PROOF;
            CHECK(!FramePressCurrent(&invalid,fakeNow));
        }
    }
    if(change==LOST_GEOMETRY){
        /* Visible ID caught up; the early-id motion guard can no longer
           account for rejection. The native identity, clock and timing
           remain current while the independently captured pose is gone. */
        integer(s->theirAnim+0xc8,SWING);
        CHECK(FramePressCurrent(&press,fakeNow));
        CopydogPredictionResetAll();
        CHECK(!FramePressCurrent(&press,fakeNow));
        CHECK(!DispatchParryFrame(&press)&&calls==before&&!DynamicTargets[1].submitted);
        return 0;
    }
    if (change != UNCHANGED) {
        changeBeforeDispatch(f, change);
        CHECK(!DispatchParryFrame(&press) && calls == before && !DynamicTargets[1].submitted);
        if (change != MISSING_CLOCK) {
            CHECK(NativeCues[1].blocked);
            /* A later observation of the old frame cannot revive the rejected input. */
            CHECK(!DispatchParryFrame(&press) && calls == before);
        }
        return 0;
    }
    CHECK(DispatchParryFrame(&press) && calls == before + 1 && lastAction == ActionLeftParry);
    CHECK(NativeCues[1].spent && DynamicTargets[1].submitted && ParryTrack[1].spent);
    CHECK(ActiveDefenseTrace[15] & CD_PARRY_TRACE_BEFORE_VISIBLE);
    UpdateDefenseState(fakeNow + 1);
    CHECK(Defense.phase == DEF_WAIT_ANIMATION);
    if (cancelAfterPress) {
        changeBeforeDispatch(f, NATIVE_MOVE);
        UpdateDefenseState(fakeNow + 2);
        CHECK(Defense.phase == DEF_CANCELLED && calls == before + 1);
        return 0;
    }
    /* Remove the outer busy guard so a duplicate would actually be observable
       when the visible ID catches up to this already consumed swing. */
    Defense = (COPYDOG_DEFENSE){0};
    for (unsigned phase = planned + 12; phase <= 432; phase += 12) {
        tick(f, SWING, phase, SWING, L"AttackBothLight1", 1);
        CheckLegacyFrameTargetsForOffline(s->me, NULL, fakeNow);
    }
    CHECK(calls == before + 1 && NativeCues[1].spent);
    return 0;
}

static int runWithoutMotion(CueScene *f, int cancel)
{
    if (startScene(f)) return 1;
    SCENE *s = &f->scene;
    int before = calls;
    for (unsigned phase = 12; phase <= 300; phase += 12) {
        int native = cancel && phase >= 48 ? WALK : SWING;
        tick(f, native, phase, WALK, L"AttackBothLight1", 0);
        if (phase == 24) CHECK(NativeCues[1].stage == CD_CUE_CONFIRMED);
        if (cancel && phase >= 48) CHECK(NativeCues[1].blocked);
        CheckLegacyFrameTargetsForOffline(s->me, NULL, fakeNow);
    }
    CHECK(calls == before && !DynamicTargets[1].submitted);
    return 0;
}

/* Stdin contains only the selected native record, visible ID, gear, and
   recorded or carried-forward action name. Bone owners and transforms are
   intentionally absent in this mode. The production reader gets no expected
   attack ID. A single-slot projection cannot reconstruct producer cursors. */
static int replayObserved(CueScene *f)
{
    char command;
    int initialized = 0;
    while (scanf(" %c", &command) == 1) {
        if (command == 'B') {
            int left, right, mode, ownLeft, ownRight, ownMode;
            if (scanf("%d %d %d %d %d %d", &left, &right, &mode,
                      &ownLeft, &ownRight, &ownMode) != 6) return 2;
            memset(f, 0, sizeof *f);
            SCENE *s = &f->scene;
            setup(s);
            Defense = (COPYDOG_DEFENSE){0};
            DynamicSelf = (DynamicParrySelf){0};
            memset(ParryTrack, 0, sizeof ParryTrack);
            EarlySelf = 0;
            GameCadence = (CD_GAME_CADENCE){0};
            CopydogPredictionResetAll();
            Config.KeyCondition = 0;
            atomic_store(&AutoRollEnabled, 0);
            pointer(addr2, f->manager);
            pointer(f->manager + 16, f->defender.ownEquipment);
            pointer(s->me + XB, f->defender.ownEquipment);
            integer(f->defender.ownEquipment + 0x2b8, ownMode);
            integer(f->defender.ownEquipment + 0x32c, ownLeft);
            integer(f->defender.ownEquipment + 0x330, ownRight);
            integer(s->equipment + 0x2b8, mode);
            integer(s->equipment + 0x2bc, 0);
            integer(s->equipment + 0x32c, left);
            integer(s->equipment + 0x330, right);
            initialized = 1;
        } else if (command == 'O' && initialized) {
            int relative, visible, index, native;
            double previous, current, duration;
            char name[64];
            if (scanf("%d %63s %d %d %d %lf %lf %lf", &relative, name, &visible, &index,
                      &native, &previous, &current, &duration) != 8) return 2;
            if (relative < -300000 || relative > 300000) return 2;
            SCENE *s = &f->scene;
            fakeNow = (DWORD)(1000000 + relative);
            integer(s->theirAnim + 0xc8, visible);
            wchar_t wide[64];
            size_t length = strlen(name);
            for (size_t i = 0; i <= length; ++i) wide[i] = (unsigned char)name[i];
            enemyName(f, wide);
            if (index < 0) {
                pointer(s->ymods + 0x10, NULL);
            } else {
                if (index >= CD_PRED_CLOCK_COUNT) return 2;
                pointer(s->ymods + 0x10, f->attacker.clock);
                char *rec = f->attacker.clock + 0x20 + 16 * index;
                integer(rec, native); real(rec + 4, (float)(previous / 1000.));
                real(rec + 8, (float)(current / 1000.)); real(rec + 12, (float)(duration / 1000.));
                integer(f->attacker.clock + 0xc4, index);
                integer(f->attacker.clock + 0xc0, (index + 1) % CD_PRED_CLOCK_COUNT);
            }
            NewCombatStamp = 0;
            ObserveParryTargets(s->me, NULL, fakeNow);
            ObserveEarlyTargets(s->me, fakeNow);
            CD_PARRY_CUE *c = &NativeCues[1];
            int ready = CopydogPredictionAttackMotionReady(1, (uintptr_t)s->you,
                                                          &c->last, c->weapon, c->mode, fakeNow);
            printf("%d %d %d %d\n", c->readable ? (int)c->stage : 0, c->expected, ready, calls);
        } else return 2;
    }
    return ferror(stdin) || ferror(stdout);
}

int main(int argc, char **argv)
{
    void *world = VirtualAlloc((void *)0x144770000, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    void *manager = VirtualAlloc((void *)0x144750000, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(world == (void *)0x144770000 && manager == (void *)0x144750000);
    CueScene f;
    if (argc == 2 && strcmp(argv[1], "--observed") == 0) {
        int result = replayObserved(&f);
        VirtualFree(manager, 0, MEM_RELEASE);
        VirtualFree(world, 0, MEM_RELEASE);
        return result;
    }
    if (argc != 1) return 2;
    if (runWithoutMotion(&f, 0) || runWithoutMotion(&f, 1)) return 1;
    if (runDispatch(&f, 0, UNCHANGED, 0) || runDispatch(&f, 1, UNCHANGED, 0) ||
        runDispatch(&f, 0, UNCHANGED, 1)) return 1;
    for (enum Change change = NATIVE_MOVE; change <= LOST_GEOMETRY; ++change)
        if (runDispatch(&f, 0, change, 0)) return 1;
    VirtualFree(manager, 0, MEM_RELEASE);
    VirtualFree(world, 0, MEM_RELEASE);
    printf("PASS %u production early-native pose, cancellation and dispatch checks\n", checks);
    return 0;
}
