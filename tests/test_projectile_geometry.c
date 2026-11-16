#include "../src/projectile_geometry.h"
#include "../src/combat.h"
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static const float zero[3]={0};

static void expect(const float p[3],const float v[3],const float playerV[3],double low,double high,
    double radius,double horizon,double expected)
{
    double t=11.0;
    CHECK(CopydogProjectileContact(p,v,zero,playerV,low,high,radius,horizon,&t)==(expected>=0.0));
    CHECK(expected<0.0?t==-1.0:fabs(expected-t)<1e-8);
}

/* Independent oracle: squared distance to a closed line segment is convex
   along a straight path. Numerically minimize it, then bisect the first hit.
   This does not use or decompose into ray/sphere or ray/cylinder tests. */
static double distance2(const float p[3],const float v[3],const float me[3],const float pv[3],
    double lo,double hi,double t)
{
    double d[3];for(unsigned i=0;i<3;i++)d[i]=(double)p[i]-me[i]+t*((double)v[i]-pv[i]);
    d[1]-=fmin(hi,fmax(lo,d[1]));
    return d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
}
static uint32_t seed=93047;
static float between(double lo,double hi)
{
    seed=1664525u*seed+1013904223u;
    return (float)(lo+(hi-lo)*(seed/4294967296.0));
}

int main(void)
{
    /* First contact, not closest approach, in a sphere centered at (0,0,0). */
    expect((float[]){4,0,0},(float[]){-10,0,0},zero,0,0,1,.5,.3);
    expect((float[]){4,3,0},(float[]){-10,0,0},zero,0,0,1,.5,-1);
    expect((float[]){4,0,2},(float[]){-10,0,0},zero,0,0,1,.5,-1);
    expect((float[]){0,4,0},(float[]){0,-10,0},zero,0,0,1,.5,.3);
    expect((float[]){4,0,0},(float[]){-10,0,0},(float[]){0,0,6},0,0,1,.5,-1);
    expect((float[]){4,0,4},(float[]){-10,0,0},(float[]){0,0,10},0,0,1,.5,.4-1./sqrt(200.));
    expect((float[]){3,0,0},(float[]){-10,0,0},(float[]){-10,0,0},0,0,1,.5,-1);
    expect((float[]){2,0,0},(float[]){10,0,0},zero,0,0,1,.5,-1);
    expect((float[]){4,0,1},(float[]){-8,0,0},zero,0,0,1,.5,.5);
    expect((float[]){4,0,nextafterf(1.f,2.f)},(float[]){-8,0,0},zero,0,0,1,.5,-1);
    expect((float[]){2,0,0},(float[]){-2,0,0},zero,0,0,1,.5,.5);
    expect((float[]){2,0,0},(float[]){-2,0,0},zero,0,0,1,nextafter(.5,0.),-1);
    expect((float[]){0,0,0},zero,zero,0,0,1,0,0);
    /* Slab, cap and tangent contacts. Integers avoid input quantization. */
    expect((float[]){2,2,0},(float[]){-4,0,0},zero,1,3,.5,.5,.375);
    expect((float[]){0,5,0},(float[]){0,-4,0},zero,1,3,.5,.5,.375);
    expect((float[]){0,-1,0},(float[]){0,4,0},zero,1,3,.5,.5,.375);
    expect((float[]){2,3.5,0},(float[]){-4,0,0},zero,1,3,.5,.5,.5);
    expect((float[]){0,2,0},(float[]){0,0,1},zero,1,3,.5,0,0);
    expect((float[]){0,3.6f,0},zero,zero,1,3,.5,.5,-1);
    expect((float[]){4,5,0},(float[]){-8,-8,0},zero,1,3,.5,.5,.4375);
    expect((float[]){4,2,0},(float[]){-4,0,0},(float[]){4,0,0},1,3,.5,.5,.4375);
    expect((float[]){FLT_MAX,2,0},(float[]){-FLT_MAX,0,0},zero,1,3,.5,.5,-1);
    expect(NULL,zero,zero,0,1,1,.5,-1);
    expect(zero,NULL,zero,0,1,1,.5,-1);
    expect(zero,zero,NULL,0,1,1,.5,-1);
    expect(zero,zero,zero,1,0,1,.5,-1);
    expect(zero,zero,zero,0,1,-1,.5,-1);
    expect(zero,zero,zero,0,1,0,.5,-1);
    expect(zero,zero,zero,0,1,1,-.1,-1);
    expect(zero,zero,zero,NAN,1,1,.5,-1);
    expect(zero,zero,zero,0,1,1,INFINITY,-1);
    expect((float[]){0,NAN,0},zero,zero,0,1,1,.5,-1);
    CHECK(!CopydogProjectileContact(zero,zero,zero,zero,0,0,1,.5,NULL));
    CHECK(!CombatBulletApproaching(zero,zero,zero));
    CHECK(CombatBulletApproaching((float[]){0,5,0},(float[]){0,-10,0},zero));
    CHECK(!CombatBulletApproaching((float[]){4,3,0},(float[]){-10,0,0},zero));

    unsigned hits=0;
    for(unsigned i=0;i<2500;i++){
        float p[3],v[3],me[3],pv[3];
        for(unsigned k=0;k<3;k++){
            me[k]=between(-80,80);p[k]=me[k]+between(-2.5,2.5);
            pv[k]=between(-4,4);v[k]=pv[k]+between(-10,10);
        }
        double height=between(0,2),radius=between(.1,1.3),horizon=between(.02,.5);
        double low=0,high=horizon;
        for(unsigned j=0;j<85;j++){
            double a=(2*low+high)/3,b=(low+2*high)/3;
            if(distance2(p,v,me,pv,0,height,a)<distance2(p,v,me,pv,0,height,b))high=b;else low=a;
        }
        double mid=(low+high)*.5,nearest=distance2(p,v,me,pv,0,height,mid);
        int expected=nearest<=radius*radius;
        double actual;
        CHECK(CopydogProjectileContact(p,v,me,pv,0,height,radius,horizon,&actual)==expected);
        if(!expected){CHECK(actual==-1.0);continue;}
        ++hits;low=0;high=mid;
        for(unsigned j=0;j<70;j++){
            double t=(low+high)*.5;
            if(distance2(p,v,me,pv,0,height,t)<=radius*radius)high=t;else low=t;
        }
        CHECK(fabs(actual-high)<1e-8);
    }
    CHECK(hits>100);
    printf("PASS %u projectile geometry checks, 2500 numerical differentials (%u hits)\n",checks,hits);
    return 0;
}
