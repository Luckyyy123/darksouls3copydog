#include "../src/dynamic_parry.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks, failures;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        if (failures < 10) fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

/* Independent oracle: try each integer request delay and contact directly
   against the half-open active window. Contact uncertainty includes both ends. */
static int covers(double request, int first, int last, int open, int close,
                  int delay_min, int delay_max)
{
    for (int delay = delay_min; delay <= delay_max; ++delay)
        for (int contact = first; contact <= last; ++contact)
            if (contact < request + delay + open || contact >= request + delay + close)
                return 0;
    return 1;
}

static double worst_margin(double request, double c0, double c1, double open,
                           double close, double qmin, double qmax)
{
    return fmin(c0 - (request + qmax + open), request + qmin + close - c1);
}

static void test_bounds(void)
{
    const int open = 95, close = 163, qmin = 14, qmax = 41;
    for (int first = 207; first <= 262; first += 5) {
        for (int width = 0; width <= 64; width += 4) {
            int last = first + width;
            double lo, hi, middle;
            int bounded = CopydogDynamicParryWindow(first, last, open, close, qmin, qmax,
                                                    &lo, &hi, &middle);
            for (int request = -15; request <= 170; ++request) {
                int valid = covers(request, first, last, open, close, qmin, qmax);
                CHECK(valid == (bounded && request > lo && request <= hi));
                if (!bounded) continue;
                for (int next = 5; next <= 35; next += 5) {
                    int valid_later = covers(request + next, first, last, open, close, qmin, qmax);
                    double now_margin = worst_margin(request, first, last, open, close, qmin, qmax);
                    double later_margin = worst_margin(request + next, first, last, open, close, qmin, qmax);
                    int press = valid && (!valid_later || now_margin >= later_margin);
                    CHECK((CopydogParryFrameChoice(lo - request, hi - request, next) == CD_FRAME_PRESS) == press);
                }
            }
        }
    }
    CHECK(CopydogParryFrameChoice(0, 9, 8) != CD_FRAME_PRESS);
    CHECK(CopydogParryFrameChoice(-9, 0, 8) == CD_FRAME_PRESS);
    CHECK(CopydogParryFrameChoice(-8, 8, 16) == CD_FRAME_PRESS);
    CHECK(CopydogParryFrameChoice(2, 40, 5) == CD_FRAME_WAIT);
    CHECK(CopydogParryFrameChoice(-40, -1, 5) == CD_FRAME_SKIP);
    CHECK(CopydogParryFrameChoice(NAN, 5, 10) != CD_FRAME_PRESS);
    /* Equal-width half-open resource windows can exactly coincide. A closed
       uncertainty interval that includes the same final instant cannot. */
    double lo,hi,mid;
    CHECK(CopydogDynamicParryWindow(100,150,100,150,0,0,&lo,&hi,&mid));
    CHECK(lo==0. && hi==0.);
    CHECK(CopydogParryFrameChoiceBounds(lo,hi,8.,1)==CD_FRAME_PRESS);
    CHECK(CopydogParryFrameChoiceBounds(lo,hi,8.,0)==CD_FRAME_SKIP);
    CHECK(CopydogParryFrameChoiceBounds(0.,3.,12.,1)==CD_FRAME_PRESS);
    CHECK(CopydogParryFrameChoiceBounds(0.,3.,12.,0)==CD_FRAME_WAIT);
}

static CD_NATIVE_FRAME frame(unsigned index, double previous_ms, double current_ms)
{
    return (CD_NATIVE_FRAME){.clock=1, .index=(int)(index % CD_PRED_CLOCK_COUNT),
        .animation=42030000, .previous=(float)(previous_ms / 1000.),
        .current=(float)(current_ms / 1000.), .duration=10.f};
}

static void test_clock(void)
{
    CD_GAME_CADENCE game={0};
    CopydogObserveCadence(&game,1,1000,1000.);
    CopydogObserveCadence(&game,1,1006,1006.94444);
    CHECK(game.running&&fabs(game.wallStep-6.94444)<.0001);
    CopydogObserveCadence(&game,1,1006,1009.);
    CHECK(game.running&&fabs(game.tick-1006.94444)<.0001);
    CopydogObserveCadence(&game,1,900,1010.);
    CHECK(!game.running);
    CopydogObserveCadence(&game,2,910,1020.);CHECK(!game.running);
    CopydogObserveCadence(&game,2,100000,1030.);CHECK(!game.running);
    CopydogObserveCadence(&game,2,CD_GAME_PLAY_MS_LIMIT,1040.);CHECK(!game.running);
    CopydogObserveCadence(&game,2,CD_GAME_PLAY_MS_LIMIT,1050.);CHECK(!game.running);
    /* A fixed 16.7ms margin loses the entire feasible next opportunity. */
    CHECK(CopydogParryFrameChoice(130.-140.,130.-100.,40.)==CD_FRAME_PRESS);
    CHECK(CopydogParryFrameChoice(145.-140.,145.-100.,50.)!=CD_FRAME_PRESS);
    CHECK(CopydogParryFrameChoice(139.-140.,139.-100.,50.)==CD_FRAME_PRESS);
    CHECK(CopydogParryFrameChoice(140.-140.,140.-100.,20.)==CD_FRAME_WAIT);
    CD_FRAME_CLOCK clock = {0};
    CD_NATIVE_FRAME a = frame(8, 84, 100), b = frame(9, 100, 116);
    uint32_t start = UINT32_MAX - 8u;
    CHECK(CopydogAdvanceFrame(&clock, &a, start));
    CHECK(!CopydogAdvanceFrame(&clock, &a, start + 4u));
    CHECK(clock.tick == start);
    CD_NATIVE_FRAME samePose=a;samePose.index=7;
    CHECK(!CopydogAdvanceFrame(&clock,&samePose,start+6u)&&clock.tick==start&&clock.last.index==7);
    CHECK(CopydogAdvanceFrame(&clock, &b, start + 16u));
    CHECK(fabs(clock.step - 16.) < 0.1 && fabs(clock.rate - 1.) < 0.01);
    CD_FRAME_CLOCK replaced=clock;
    CD_NATIVE_FRAME changed=b;changed.previous+=.004f;
    CHECK(CopydogAdvanceFrame(&replaced,&changed,start+20u)&&replaced.skipped==0&&replaced.tick==start+20u);
    CD_NATIVE_FRAME skipped = frame(12, 148, 164);
    CHECK(CopydogAdvanceFrame(&clock, &skipped, start + 64u));
    CHECK(clock.skipped == 2 && fabs(clock.step - 16.) < 0.1);
    /* Repeated reads of a frozen frame cannot move the sampling origin. */
    CHECK(!CopydogAdvanceFrame(&clock, &skipped, start + 1200u));
    CHECK(clock.tick == start + 64u);
    CD_NATIVE_FRAME resumed = frame(13, 164, 180);
    CHECK(CopydogAdvanceFrame(&clock, &resumed, start + 1216u));
    CHECK(fabs(clock.step - 16.) < 0.1);
    for (int speed_case = 0; speed_case < 2; ++speed_case) {
        double speed = speed_case ? 2. : .5;
        clock = (CD_FRAME_CLOCK){0};
        for (unsigned i = 0; i < 20; ++i) {
            CD_NATIVE_FRAME f = frame(i, i*20.*speed, (i+1)*20.*speed);
            CHECK(CopydogAdvanceFrame(&clock, &f, i*20u));
        }
        CHECK(fabs(clock.rate - speed) < .02);
        CHECK(fabs(clock.step - 20.) < .3);
    }
    clock = (CD_FRAME_CLOCK){0};
    a = frame(1, 80, 100);
    b = frame(3, 190, 195);
    CHECK(CopydogAdvanceFrame(&clock, &a, 100));
    CHECK(CopydogAdvanceFrame(&clock, &b, 195));
    /* 90+5 ms passed across two updates. The final short step alone cannot
       change the observed ring displacement of two into nineteen updates. */
    CHECK((clock.skipped + 1u) % CD_PRED_CLOCK_COUNT == 2u);
}

/* Fist-sized interval here is a synthetic fixture. The optimum is checked
   against every actual opportunity, independent of the production choice. */
static void test_cadence(int hz)
{
    const double c0 = 552., c1 = 574., open = 133., close = 267., qmin = 20., qmax = 45.;
    double step = 1000./hz, lo, hi, mid;
    CHECK(CopydogDynamicParryWindow(c0,c1,open,close,qmin,qmax,&lo,&hi,&mid));
    CD_FRAME_CLOCK clock = {0};
    double pressed = -1., best = -1.;
    unsigned duplicates = 0;
    for (unsigned i = 1; i*step <= 650.; ++i) {
        double now = i*step;
        CD_NATIVE_FRAME s = frame(i, now-step, now);
        uint32_t tick = (uint32_t)lround(now);
        CHECK(CopydogAdvanceFrame(&clock, &s, tick));
        if (i>1) CHECK(fabs(clock.step-step) < 1.01);
        double margin = worst_margin(now,c0,c1,open,close,qmin,qmax);
        if (covers(now,552,574,133,267,20,45)) best=fmax(best,margin);
        if (pressed<0 && CopydogParryFrameChoice(lo-now,hi-now,clock.step)==CD_FRAME_PRESS)
            pressed=now;
        if (!CopydogAdvanceFrame(&clock, &s, tick+1u)) ++duplicates;
        CHECK(!CopydogAdvanceFrame(&clock, &s, tick+2u) && clock.tick == tick);
    }
    CHECK(pressed>=0 && covers(pressed,552,574,133,267,20,45));
    CHECK(best-worst_margin(pressed,c0,c1,open,close,qmin,qmax)<1.01);
    CHECK(duplicates>15);
    printf("%3d Hz: press %.2f ms, optimum %.2f ms, margin %.2f ms\n",
           hz, pressed, mid, worst_margin(pressed,c0,c1,open,close,qmin,qmax));
}

static void test_rate_change(void)
{
    CD_FRAME_CLOCK clock={0};
    double previous=0., now=0.;
    for(unsigned i=1;i<=40;i++){
        double step=i<=15 ? 1000./85 : 25.;
        previous=now;now+=step;
        CD_NATIVE_FRAME s=frame(i,previous,now);
        CHECK(CopydogAdvanceFrame(&clock,&s,(uint32_t)lround(now)));
        CHECK(fabs(clock.step-step)<1.01);
    }
    double lo=-1., hi=22.;
    /* Next observed time matters even when the contact and tool are identical. */
    CHECK(CopydogParryFrameChoice(lo,hi,1000./85)==CD_FRAME_WAIT);
    CHECK(CopydogParryFrameChoice(lo,hi,25.)==CD_FRAME_PRESS);
}

int main(void)
{
    const int rates[]={30,40,60,85,144};
    test_bounds();test_clock();test_rate_change();
    for(unsigned i=0;i<sizeof rates/sizeof rates[0];++i)test_cadence(rates[i]);
    printf("%s %u frame formula checks, %u failed\n",failures?"FAIL":"PASS",checks,failures);
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
