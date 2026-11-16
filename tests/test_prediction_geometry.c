#include "../src/prediction.h"
#include "../src/prediction_limits.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static CD_PRED_CAPSULE ball(double x,double y,double z,double r)
{
    CD_PRED_CAPSULE p={{x,y,z},{x,y,z},r};return p;
}

static void crossing(double start,double finish,double radiusSum,double duration,
    double first,double last)
{
    CD_PRED_CAPSULE a=ball(start,0,0,radiusSum/2),A=ball(finish,0,0,radiusSum/2);
    CD_PRED_CAPSULE b=ball(0,0,0,radiusSum/2);
    double speed=(finish-start)/duration,hit;
    unsigned budget=CD_PRED_WORK_BUDGET;
    assert(fabs((-radiusSum-start)/speed-first)<1e-9);
    assert(fabs((radiusSum-start)/speed-last)<1e-9);
    assert(CopydogPredictionSweep(&a,&A,&b,&b,duration,&hit,&budget)==CD_PRED_CONTACT);
    /* The kernel reports the start of a bracket containing the first hit. */
    assert(hit<=first+CD_PRED_EPSILON/speed);
    assert(first-hit<=CD_PRED_TOLERANCE_MS+CD_PRED_EPSILON/speed);
}

static void moving_segments(void)
{
    /* The two centre lines cross in Y/Z. Their relative X speed is .04 m/ms. */
    const double duration=16.667,first=6.0417875,relativeSpeed=.04,radiusSum=.00001;
    double start=-(relativeSpeed*first+radiusSum);
    CD_PRED_CAPSULE a={{start,-.3,0},{start,.5,0},radiusSum/2},A=a;
    CD_PRED_CAPSULE b={{0,.1,-.3},{0,.1,.2},radiusSum/2},B=b;
    A.a[0]+=.024*duration;A.b[0]+=.024*duration;
    B.a[0]-=.016*duration;B.b[0]-=.016*duration;
    unsigned budget=CD_PRED_WORK_BUDGET;
    double hit;
    int status=CopydogPredictionSweep(&a,&A,&b,&B,duration,&hit,&budget);
    /* The 0.0005 ms overlap can fit completely between the last samples. */
    assert(status==CD_PRED_CONTACT||status==CD_PRED_POSSIBLE);
    assert(hit<=first+CD_PRED_EPSILON/relativeSpeed&&first-hit<=CD_PRED_TOLERANCE_MS);
}

static void grazing_budget(void)
{
    const double duration=16.667,first=6.0417875;
    CD_PRED_CAPSULE a=ball(-first,.2,0,.1),A=ball(duration-first,.2,0,.1);
    CD_PRED_CAPSULE b=ball(0,0,0,.1);
    unsigned budget=2;
    double hit;
    assert(CopydogPredictionSweep(&a,&A,&b,&b,duration,&hit,&budget)==CD_PRED_UNKNOWN);
    assert(budget==0&&hit==-1);
    budget=CD_PRED_WORK_BUDGET;
    int status=CopydogPredictionSweep(&a,&A,&b,&b,duration,&hit,&budget);
    assert(status==CD_PRED_CONTACT||status==CD_PRED_POSSIBLE);
    assert(hit>=0&&hit<=first+CD_PRED_TOLERANCE_MS);
}

int main(void)
{
    CD_PRED_CAPSULE a=ball(-2,0,0,.1),A=ball(2,0,0,.1),b=ball(0,0,0,.1);
    unsigned budget=32768;double t;
    int r=CopydogPredictionSweep(&a,&A,&b,&b,50,&t,&budget);
    assert(r==CD_PRED_CONTACT&&t>=22.4&&t<=22.6);
    b=ball(0,1,0,.1);budget=32768;
    assert(CopydogPredictionSweep(&a,&A,&b,&b,50,&t,&budget)==CD_PRED_CLEAR&&t==-1);
    b=ball(0,.2,0,.1);budget=32768;
    assert(CopydogPredictionSweep(&a,&A,&b,&b,50,&t,&budget)>0);
    b=ball(-2,0,0,.1);budget=32768;
    assert(CopydogPredictionSweep(&a,&A,&b,&b,50,&t,&budget)==CD_PRED_CONTACT&&t==0);
    budget=0;assert(CopydogPredictionSweep(&a,&A,&b,&b,50,&t,&budget)==CD_PRED_UNKNOWN);
    budget=32768;A.a[0]=NAN;
    assert(CopydogPredictionSweep(&a,&A,&b,&b,50,&t,&budget)==CD_PRED_UNKNOWN&&t==-1);
    crossing(-1.,1.,.5,16.667,4.16675,12.50025);
    crossing(-.6,1.,.02,16.667,6.0417875,6.4584625);
    /* A minimum step of .5 ms samples x=-.002 and x=.003, missing [.1,.3]. */
    crossing(-.002,.008,.001,1.,.1,.3);
    moving_segments();grazing_budget();
    puts("PASS continuous sweep: exact first-hit brackets, sub-step crossings, moving capsules, grazing, budget, invalid input");
}
