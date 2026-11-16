#include "../src/prediction.h"
#include "../src/prediction_limits.h"
#include "../src/prediction_motion.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;

static void check(int condition,const char*name)
{
    if(!condition){fprintf(stderr,"FAIL turning sweep: %s\n",name);exit(EXIT_FAILURE);}
    ++checks;
}

static CD_PRED_TURN_POINT point(double rootX,double pivotX,double armX)
{
    CD_PRED_TURN_POINT p={0};
    p.root[0]=rootX;p.pivot[0]=pivotX;p.arm[0]=armX;p.axis[1]=1.;
    return p;
}

static CD_PRED_TURN_CAPSULE sphere(CD_PRED_TURN_POINT p,double radius)
{
    return (CD_PRED_TURN_CAPSULE){.a=p,.b=p,.radius=radius};
}

static int sweep(const CD_PRED_TURN_CAPSULE*a,double ageA,const CD_PRED_TURN_CAPSULE*b,
    double ageB,double duration,double*out)
{
    unsigned budget=CD_PRED_WORK_BUDGET;
    return CopydogPredictionTurningSweep(a,ageA,b,ageB,duration,out,&budget);
}

static void expect_first(const CD_PRED_TURN_CAPSULE*a,double ageA,const CD_PRED_TURN_CAPSULE*b,
    double ageB,double duration,double first,const char*name)
{
    double hit=-1.;
    int status=sweep(a,ageA,b,ageB,duration,&hit);
    if(status<=0||hit<0.||fabs(hit-first)>2.*CD_PRED_TOLERANCE_MS+1e-6){
        fprintf(stderr,"%s: status=%d time=%.9f expected=%.9f\n",name,status,hit,first);
        check(0,name);
    }
    ++checks;
}

static void independent_clocks(void)
{
    CD_PRED_TURN_POINT a=point(0,0,0),b=point(1.48,0,0);
    a.rootVelocity[0]=.04;b.rootVelocity[0]=.01;
    CD_PRED_TURN_CAPSULE attack=sphere(a,.1),body=sphere(b,.1);
    /* At absolute wall 1040 the centers are 1.6 and 1.8. Their previous
       positions refer to walls 1000 and 1008 respectively. */
    expect_first(&attack,20,&body,12,45,20,"independent pose clocks at 1020");
    expect_first(&attack,12,&body,4,45,28,"same contact seen eight ms earlier");
    expect_first(&body,12,&attack,20,45,20,"symmetric age alignment");
    /* Translation along the rotation axis gives zero horizontal radius. */
    a.arm[1]=1.;a.yawRate=.015;a.boneRate=-.01;b.root[1]=1.;
    attack=sphere(a,.1);body=sphere(b,.1);
    expect_first(&attack,20,&body,12,45,20,"zero lever arm with nonzero spin");
}

static void circular_arcs(void)
{
    const double pi=acos(-1.),duration=40.,speed=(pi/2.)/duration,phi=pi/4.;
    for(int sign=-1;sign<=1;sign+=2){
        for(int bone=0;bone<=1;bone++){
            CD_PRED_TURN_POINT a=point(0,bone?0:1,bone?1:0);
            if(bone)a.boneRate=sign*speed;
            else a.yawRate=sign*speed;
            CD_PRED_TURN_POINT b=point(cos(phi),0,0);b.root[2]=-sign*sin(phi);
            CD_PRED_TURN_CAPSULE attack=sphere(a,.01),body=sphere(b,.01);
            /* Point on the arc. The center chord is 0.292893 m away. */
            double first=(phi-2.*asin(.02/2.))/speed;
            expect_first(&attack,0,&body,0,duration,first,"arc-only hit");
            expect_first(&attack,6,&body,3,duration-6,first-6,"time-shifted arc-only hit");
            b.root[0]=.5;b.root[2]=-.5*sign;body=sphere(b,.01);
            double hit=-1.;
            check(sweep(&attack,0,&body,0,duration,&hit)==CD_PRED_CLEAR&&hit<0.,
                "chord-only crossing is not a modeled arc hit");
        }
    }
    CD_PRED_TURN_POINT a=point(0,0,1),b=point(1.,0,0);
    a.yawRate=speed;a.boneRate=-speed;b.root[1]=.2;
    CD_PRED_TURN_CAPSULE attack=sphere(a,.02),body=sphere(b,.02);
    double hit=-1.;
    check(sweep(&attack,0,&body,0,duration,&hit)==CD_PRED_CLEAR,
        "opposite root and bone spins around one axis cancel");
}

static void moving_segment_and_drifts(void)
{
    const double pi=acos(-1.),speed=pi/80.,phi=pi/4.;
    CD_PRED_TURN_POINT inner=point(0,.7,0),outer=point(0,1.3,0),target=point(cos(phi),0,0);
    target.root[2]=-sin(phi);inner.yawRate=outer.yawRate=speed;
    /* The deliberately short segment has no endpoint inside the target. */
    CD_PRED_TURN_CAPSULE blade={.a=inner,.b=outer,.radius=.01},body=sphere(target,.01);
    expect_first(&blade,0,&body,0,40,(phi-asin(.02))/speed,"moving segment interior");

    /* A rotating pivot that slides in the local X direction reaches this
       point precisely at u=20. Zero radii exercise a model tangent. */
    CD_PRED_TURN_POINT pointA=point(0,1,0);
    pointA.yawRate=speed;pointA.pivotVelocity[0]=.002;pointA.rootVelocity[1]=.001;
    target=point(1.04*cos(phi),0,0);target.root[2]=-1.04*sin(phi);
    target.root[1]=.020;
    blade=sphere(pointA,0);body=sphere(target,0);
    double hit=-1.;
    int status=sweep(&blade,0,&body,0,40,&hit);
    check(status>0&&hit>=0.&&fabs(hit-20.)<=2.*CD_PRED_TOLERANCE_MS+1e-6,
        "zero-radius contact with rotating local drift");
}

static void combined_axes(void)
{
    CD_PRED_TURN_POINT p=point(0,.7,.02);
    p.yawRate=.018;p.boneRate=.011;p.axis[0]=1.;p.axis[1]=0.;
    p.rootVelocity[0]=.001;p.rootVelocity[1]=.014;p.rootVelocity[2]=-.0015;
    p.pivot[1]=.1;p.pivot[2]=.1;p.pivotVelocity[0]=.003;p.pivotVelocity[2]=-.001;
    p.arm[1]=.35;p.arm[2]=.25;
    CD_PRED_TURN_CAPSULE attack=sphere(p,0);
    for(unsigned frame=1;frame<=7;frame++){
        double t=5.*frame;
        /* Separate coordinate oracle: explicit Rx of the local arm, then
           Ry of the complete pivot+arm. Y'(t) >= .014-.011*hypot(.35,.25)
           is positive, so the planted contact is unique. */
        double x=.7+.003*t+.02,y=.1+.35*cos(.011*t)-.25*sin(.011*t);
        double z=.1-.001*t+.35*sin(.011*t)+.25*cos(.011*t);
        CD_PRED_TURN_POINT target=point(0,0,0);
        target.root[0]=.001*t+x*cos(.018*t)+z*sin(.018*t);
        target.root[1]=.014*t+y;
        target.root[2]=-.0015*t-x*sin(.018*t)+z*cos(.018*t);
        CD_PRED_TURN_CAPSULE body=sphere(target,0);
        expect_first(&attack,0,&body,0,40,t,"noncommuting bone and root rotations");
    }
}

static void angle_wrap(void)
{
    const double pi=acos(-1.),degree=pi/180.,speed=2.*degree/40.,radiusSum=.002;
    for(int sign=-1;sign<=1;sign+=2){
        /* A speed model using the short 179 to -179 arc crosses 180;
           endpoints alone cannot decide this vs a reversed 358-degree turn. */
        CD_PRED_TURN_POINT p=point(0,cos(179.*degree),0);
        p.pivot[2]=-sign*sin(179.*degree);p.yawRate=sign*speed;
        CD_PRED_TURN_CAPSULE attack=sphere(p,radiusSum/2.),body=sphere(point(-1,0,0),radiusSum/2.);
        expect_first(&attack,0,&body,0,40,(degree-2.*asin(radiusSum/2.))/speed,
            "continuous short arc through signed pi");
        attack.a.yawRate=attack.b.yawRate=-p.yawRate;
        double hit;
        check(sweep(&attack,0,&body,0,40,&hit)==CD_PRED_CLEAR,
            "reversing the assumed future direction changes the result");
    }
}

/* Independent evaluation via a quaternion-derived 3x3 matrix. */
static void reference_position(const CD_PRED_TURN_POINT*p,double t,double out[3])
{
    double sine=sin(.5*p->boneRate*t),w=cos(.5*p->boneRate*t);
    double x=p->axis[0]*sine,y=p->axis[1]*sine,z=p->axis[2]*sine;
    const double matrix[3][3]={
        {1.-2.*(y*y+z*z),2.*(x*y-w*z),2.*(x*z+w*y)},
        {2.*(x*y+w*z),1.-2.*(x*x+z*z),2.*(y*z-w*x)},
        {2.*(x*z-w*y),2.*(y*z+w*x),1.-2.*(x*x+y*y)}
    };
    double local[3],c=cos(p->yawRate*t),s=sin(p->yawRate*t);
    for(unsigned i=0;i<3;i++){
        local[i]=p->pivot[i]+p->pivotVelocity[i]*t;
        for(unsigned j=0;j<3;j++)local[i]+=matrix[i][j]*p->arm[j];
    }
    out[0]=p->root[0]+p->rootVelocity[0]*t+c*local[0]+s*local[2];
    out[1]=p->root[1]+p->rootVelocity[1]*t+local[1];
    out[2]=p->root[2]+p->rootVelocity[2]*t-s*local[0]+c*local[2];
}

static void planted_contacts(void)
{
    for(unsigned n=1;n<=64;n++){
        double q=n*.43;
        double ageA=n%19,ageB=(n*5)%23,when=7.3+n%21;
        CD_PRED_TURN_POINT a=point(0,.7,.12),b=point(0,.3,-.22);
        a.axis[0]=cos(q);a.axis[1]=0.;a.axis[2]=sin(q);
        b.axis[0]=0.;b.axis[1]=sin(q*.6);b.axis[2]=cos(q*.6);
        a.yawRate=.018*sin(q);a.boneRate=.021*cos(q*.3);
        b.yawRate=-.011*cos(q);b.boneRate=.015*sin(q*.4);
        a.arm[2]=.36;b.arm[1]=.21;
        a.pivotVelocity[2]=.003;b.pivotVelocity[1]=-.002;
        a.rootVelocity[0]=.01*sin(q);b.rootVelocity[2]=.007*cos(q);
        /* Translate the second moving sphere to overlap at a known wall
           time. The first contact may legitimately precede that time. */
        double A[3],B[3];reference_position(&a,ageA+when,A);reference_position(&b,ageB+when,B);
        for(unsigned i=0;i<3;i++)b.root[i]=A[i]-B[i];
        CD_PRED_TURN_CAPSULE attack=sphere(a,.006),body=sphere(b,.006);
        double hit=-1.;
        check(sweep(&attack,ageA,&body,ageB,40.,&hit)>0&&hit>=0.&&hit<=when+1e-6,
            "planted contacts under independent orientation and translation");
        /* Vertical separation exceeds both possible arm sweeps and pivot drift. */
        body.a.root[1]+=3.;body.b.root[1]+=3.;
        check(sweep(&attack,ageA,&body,ageB,40.,&hit)==CD_PRED_CLEAR,
            "separated independently turning bodies");
    }
}

static void budget_and_alias(void)
{
    const double pi=acos(-1.);
    CD_PRED_TURN_POINT a=point(0,.5,0),b=point(-.5,0,0);
    a.yawRate=2.*pi/40.;
    CD_PRED_TURN_CAPSULE attack=sphere(a,.025),body=sphere(b,.025);
    double hit=-1.;
    int status=sweep(&attack,0,&body,0,40,&hit);
    /* Identical start/end orientations allow a full revolution here.
       Restrictions on a long arc may leave it unknown, never proven clear. */
    check(status!=CD_PRED_CLEAR,"one full turn must not become a stationary chord");
    unsigned noWork=0;
    status=CopydogPredictionTurningSweep(&attack,0,&body,0,40,&hit,&noWork);
    check(status==CD_PRED_UNKNOWN&&hit<0.,"exhausted work cannot exclude the turn");
    check(sweep(&attack,-1,&body,0,40,&hit)==CD_PRED_UNKNOWN,"negative wall age");
    CD_PRED_TURN_POINT fast=point(0,1.,0);fast.yawRate=2.;
    CD_PRED_TURN_CAPSULE ancient=sphere(fast,.1),distant=sphere(point(100.,0,0),.1);
    check(sweep(&ancient,DBL_MAX,&distant,0.,8.,&hit)==CD_PRED_UNKNOWN&&hit<0.,
        "finite but overflowing pose age cannot fabricate contact at zero");
    attack.a.boneRate=.01;attack.a.axis[1]=2.;
    check(sweep(&attack,0,&body,0,40,&hit)==CD_PRED_UNKNOWN,"non-unit driven rotation axis");
    attack.a.boneRate=0.;
    attack.a.root[2]=NAN;
    check(sweep(&attack,0,&body,0,40,&hit)==CD_PRED_UNKNOWN,"nonfinite observation");
}

int main(void)
{
    independent_clocks();circular_arcs();moving_segment_and_drifts();combined_axes();angle_wrap();
    planted_contacts();budget_and_alias();
    printf("PASS %u clock-aligned turning capsule and conservative uncertainty checks\n",checks);
    return 0;
}
