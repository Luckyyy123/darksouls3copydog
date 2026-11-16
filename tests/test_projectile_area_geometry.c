#include "../src/projectile_area_geometry.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(v) do{++checks;if(!(v)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);exit(1);}}while(0)
static const float zero[3]={0};

static void check_contact(const float center[3],const float velocity[3],
    double r0,double r1,double growth,double age,double life,double horizon,double expected)
{
    double eta=123.0;
    int hit=CopydogAreaContact(center,zero,velocity,r0,r1,growth,age,life,horizon,&eta);
    if(hit!=(expected>=0.0)||(hit?fabs(eta-expected)>1.e-9:eta!=-1.0)){
        fprintf(stderr,"center=(%g,%g,%g), radius=(%g,%g), age=%g, life=%g, horizon=%.17g: "
            "hit=%d, t=%.17g expected=%.17g\n",center?center[0]:0.,center?center[1]:0.,center?center[2]:0.,
            r0,r1,age,life,horizon,hit,eta,expected);
        exit(1);
    }
    checks++;
}

/* Independent scalar oracle. Distance to a translating segment is convex;
   subtracting a linear-then-constant increasing radius preserves convexity.
   Find the minimum gap, then bisect the descending branch for the entry. */
static double gap(const float center[3],const float player[3],const float velocity[3],
    double r0,double r1,double grow,double age,double t)
{
    double dx=(double)center[0]-player[0]-velocity[0]*t;
    double dy=(double)center[1]-player[1]-velocity[1]*t;
    double dz=(double)center[2]-player[2]-velocity[2]*t;
    dy-=fmin(COPYDOG_PROJECTILE_BODY_HIGH_Y,fmax(COPYDOG_PROJECTILE_BODY_LOW_Y,dy));
    double radius=r0+(r1-r0)*(grow>0.0?fmin(1.0,(age+t)/grow):1.0);
    return hypot(hypot(dx,dy),dz)-radius-COPYDOG_PROJECTILE_BODY_RADIUS;
}

static uint32_t seed=2913065;
static float between(double a,double b)
{
    seed=1664525u*seed+1013904223u;
    return (float)(a+(b-a)*(seed/4294967296.0));
}

static void compare_with_oracle(void)
{
    unsigned hits=0,future=0;
    for(unsigned n=0;n<1800;n++){
        float center[3],player[3],velocity[3];
        for(unsigned i=0;i<3;i++){
            player[i]=between(-30,30);center[i]=player[i]+between(-2.5,2.5);
            velocity[i]=between(-8,8);
        }
        double r0=between(0.01,0.9),r1=r0+between(0.02,1.5),growth=between(0.01,0.45);
        double age=between(0,0.35),life=between(0.4,1.0),horizon=between(0.01,0.7);
        double end=fmin(horizon,nextafter(life-age,0.0)),lo=0.0,hi=end;
        for(unsigned i=0;i<74;i++){
            double a=(2.0*lo+hi)/3.0,b=(lo+2.0*hi)/3.0;
            if(gap(center,player,velocity,r0,r1,growth,age,a)<=gap(center,player,velocity,r0,r1,growth,age,b))
                hi=b;
            else lo=a;
        }
        double minimum=(lo+hi)*0.5;
        if(gap(center,player,velocity,r0,r1,growth,age,end)<
           gap(center,player,velocity,r0,r1,growth,age,minimum))minimum=end;
        int now=gap(center,player,velocity,r0,r1,growth,age,0.0)<=0.0;
        int expected=now||gap(center,player,velocity,r0,r1,growth,age,minimum)<=0.0;
        double actual=0.0;
        CHECK(CopydogAreaContact(center,player,velocity,r0,r1,growth,age,life,horizon,&actual)==expected);
        if(!expected){CHECK(actual==-1.0);continue;}
        hits++;
        if(now){CHECK(actual==0.0);continue;}
        future++;lo=0.0;hi=minimum;
        for(unsigned i=0;i<65;i++){
            double mid=0.5*(lo+hi);
            if(gap(center,player,velocity,r0,r1,growth,age,mid)<=0.0)hi=mid;
            else lo=mid;
        }
        CHECK(fabs(hi-actual)<1.e-8);
    }
    CHECK(hits>150&&future>60);
    printf("1800 numerical differentials, %u contacts (%u future)\n",hits,future);
}

int main(void)
{
    const float inner[3]={.75f,0,0},band[3]={1,0,0},far[3]={2,0,0},side[3]={2,1,0};
    const double edge=(hypot(1.0,0.4)-0.4-0.5)*0.1/0.85;
    check_contact(inner,zero,.5,1.35,.1,0,.25,0,0);
    check_contact(band,zero,.5,1.35,.1,0,.25,.2,edge);
    check_contact(band,zero,.5,1.35,.1,.0125,.25,.2,edge-.0125);
    check_contact(band,zero,.5,1.35,.1,0,.25,edge-1e-6,-1);
    check_contact(band,zero,.5,1.35,.1,0,.25,edge+1e-6,edge);
    check_contact(band,zero,.5,1.35,.1,.1,.25,0,0);
    check_contact(band,zero,.5,1.35,0,0,.25,0,0);
    check_contact(far,zero,.5,1.35,.1,0,.25,.5,-1);
    check_contact(side,(float[]){5,0,0},.6,2.6,.5,0,.75,.75,1.0/9.0);
    check_contact(side,(float[]){-6,0,0},.6,2.6,.5,0,.75,.75,-1);
    check_contact(side,(float[]){4,0,0},.6,1.6,.25,0,.75,.75,.125);
    check_contact((float[]){4,1,0},(float[]){-2,0,0},.6,10.6,2,0,2,1.5,1);
    check_contact((float[]){2.5f,1,0},(float[]){2,0,0},.1,.6,.1,0,1,1,.75);
    check_contact((float[]){0,3,0},(float[]){0,4,0},.6,.6,0,0,1,1,.2);
    check_contact((float[]){0,-1,0},(float[]){0,-2,0},.6,.6,0,0,1,1,.2);
    /* Fast transit hits and leaves between two clear endpoints. */
    check_contact((float[]){4,1,0},(float[]){80,0,0},.6,.6,0,0,1,.1,.0375);
    check_contact((float[]){4,1,1},(float[]){8,0,0},.6,.6,0,0,1,.5,.5);
    check_contact((float[]){4,1,nextafterf(1.f,2.f)},(float[]){8,0,0},.6,.6,0,0,1,.5,-1);
    /* Exactly at a forecast endpoint is due; exactly at death is not. */
    check_contact(side,(float[]){1,0,0},.6,.6,0,0,1,2,-1);
    check_contact(side,(float[]){1,0,0},.6,.6,0,.5,1.5,2,-1);
    check_contact(side,(float[]){1,0,0},.6,.6,0,0,2,1,1);
    check_contact(side,(float[]){1,0,0},.6,.6,0,0,2,nextafter(1.0,0.0),-1);
    check_contact((float[]){4,1,1},(float[]){8,0,0},.6,.6,0,0,.5,.6,-1);
    /* The expansion reaches each tangent radius only at the endpoint. The
       closed horizon admits it; the half-open lifetime excludes it even
       when an unguarded floating-point root rounds a few ULPs early. */
    for(unsigned d=1;d<=16;d++){
        float center[3]={(float)d,1,0};
        for(unsigned k=1;k<50;k+=3){
            double begin=k/100.0,final=(double)d-COPYDOG_PROJECTILE_BODY_RADIUS;
            check_contact(center,zero,begin,final,.125,0,.125,.125,-1);
            check_contact(center,zero,begin,final,.125,0,1,.125,.125);
        }
    }
    check_contact(inner,zero,.5,1.35,.1,nextafter(.25,0),.25,0,0);
    check_contact(inner,zero,.5,1.35,.1,.25,.25,0,-1);
    check_contact(inner,zero,.5,1.35,.1,0,0,1,-1);
    check_contact(side,zero,-.1,1,.1,0,.25,.1,-1);
    check_contact(side,zero,0,0,.1,0,.25,.1,-1);
    check_contact(side,zero,1,.5,.1,0,.25,.1,-1);
    check_contact(side,zero,.5,NAN,.1,0,.25,.1,-1);
    check_contact(side,zero,.5,1,-.1,0,.25,.1,-1);
    check_contact(side,zero,.5,1,.1,-.1,.25,.1,-1);
    check_contact(side,zero,.5,1,.1,0,INFINITY,.1,-1);
    check_contact(side,zero,.5,1,.1,0,.25,-.1,-1);
    check_contact((float[]){0,0,NAN},zero,.5,1,.1,0,.25,.1,-1);
    check_contact(side,NULL,.5,1,.1,0,.25,.1,-1);
    check_contact(NULL,zero,.5,1,.1,0,.25,.1,-1);
    double eta=99;
    CHECK(!CopydogAreaContact(side,NULL,zero,.5,1,.1,0,.25,.1,&eta)&&eta==-1.0);
    CHECK(!CopydogAreaContact(side,zero,zero,.5,1,.1,0,.25,.1,NULL));
    /* Horizontal game forward at yaw is (-sin(yaw), -cos(yaw)). */
    CHECK(-sin(CopydogAreaAwayYaw(band,zero,zero,0,.25f))<-.99);
    CHECK(-sin(CopydogAreaAwayYaw(zero,(float[]){-1,0,0},(float[]){2,0,0},1,.25f))>.99);
    CHECK(-cos(CopydogAreaAwayYaw(zero,zero,(float[]){0,0,-2},0,.25f))<-.99);
    CHECK(CopydogAreaAwayYaw(zero,zero,zero,0,.25f)==.25f);
    CHECK(CopydogAreaAwayYaw(zero,zero,zero,0,NAN)==0.f);
    CHECK(CopydogAreaAwayYaw(NULL,zero,zero,1,.25f)==.25f);
    CHECK(CopydogAreaAwayYaw(zero,zero,(float[]){NAN,0,0},1,.25f)==.25f);
    compare_with_oracle();
    printf("PASS %u projectile area geometry checks\n",checks);return 0;
}
