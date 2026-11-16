#ifndef COPYDOG_PROJECTILE_AREA_GEOMETRY_H
#define COPYDOG_PROJECTILE_AREA_GEOMETRY_H

#include <float.h>
#include <math.h>
#include "projectile_geometry.h"
#include "projectile_motion.h"

/* On one interval, q + s*u is the separation from the nearest capsule-axis
   point, and r + s*dr is the combined radius, for 0 <= s <= 1. */
static inline double CdAreaPieceFirst(const double q[3],const double u[3],double startRadius,double endRadius)
{
    double size=endRadius;
    for(unsigned i=0;i<3;i++)size=fmax(size,fmax(fabs(q[i]),fabs(u[i])));
    int exponent;
    (void)frexp(size,&exponent);
    double scale=ldexp(1.0,-exponent);
    double x=q[0]*scale,y=q[1]*scale,z=q[2]*scale;
    double vx=u[0]*scale,vy=u[1]*scale,vz=u[2]*scale;
    double r=startRadius*scale,r1=endRadius*scale,dr=r1-r;
    double dist=hypot(hypot(x,y),z);
    if(dist<=r)return 0.0;
    double ex=x+vx,ey=y+vy,ez=z+vz,endDist=hypot(hypot(ex,ey),ez);
    /* The gap is convex on this piece. A nonpositive slope at the end
       makes the endpoint a minimum. Testing its actual radius keeps a
       boundary-only touch exactly at that endpoint despite root rounding. */
    if(endDist>=r1&&ex*vx+ey*vy+ez*vz<=endDist*dr){
        if(endDist-r1>8.0*DBL_EPSILON*endDist)return -1.0;
        long double endX=(long double)x+vx,endY=(long double)y+vy,endZ=(long double)z+vz;
        long double gap2=endX*endX+endY*endY+endZ*endZ-(long double)r1*r1;
        if(gap2>=0.0L)return gap2==0.0L?1.0:-1.0;
    }
    double speed=hypot(hypot(vx,vy),vz);
    double a=(speed-dr)*(speed+dr),b=x*vx+y*vy+z*vz-r*dr;
    if(a>=0.0&&b>=0.0)return -1.0;
    double c=(dist-r)*(dist+r);
    /* For a*s^2 + 2*b*s + c=0, b^2-a*c also equals
       |r*u-dr*q|^2 - |q cross u|^2. This keeps narrow grazes resolvable
       when the starting distance along the line is large. */
    double dx=r*vx-dr*x,dy=r*vy-dr*y,dz=r*vz-dr*z;
    double cx=y*vz-z*vy,cy=z*vx-x*vz,cz=x*vy-y*vx;
    double radial=dx*dx+dy*dy+dz*dz,perp=cx*cx+cy*cy+cz*cz;
    double discriminant=radial-perp;
    if(discriminant < -16.0*DBL_EPSILON*(radial+perp))return -1.0;
    double root=sqrt(fmax(0.0,discriminant));
    /* c>0. Select the earliest positive root without subtracting nearly
       equal quantities; this also covers the linear case a==0, b<0. */
    double first=b<0.0?c/(-b+root):(-b-root)/a;
    if(first>=0.0&&first<=1.0)return first;
    return endDist<r1?1.0:-1.0;
}

/* The native parameters supply endpoints and a growth duration. Linear
   expansion between them is the model used here. Both shapes are in world
   units; the center is stationary and the upright player capsule translates
   at the supplied velocity. A caller supplying a predicted activation
   point must also place the player at that activation time. */
static inline int CopydogAreaContact(const float center[3],const float player[3],
    const float playerV[3],double rStart,double rEnd,double growSeconds,double ageSeconds,
    double lifeSeconds,double horizonSeconds,double *eta)
{
    if(!eta)return 0;
    *eta=-1.0;
    if(!center||!player||!playerV||!isfinite(rStart)||!isfinite(rEnd)||
       !isfinite(growSeconds)||!isfinite(ageSeconds)||!isfinite(lifeSeconds)||!isfinite(horizonSeconds)||
       rStart<0.0||rEnd<=0.0||rEnd<rStart||rEnd>FLT_MAX||
       growSeconds<0.0||growSeconds>FLT_MAX||ageSeconds<0.0||ageSeconds>=lifeSeconds||
       lifeSeconds>FLT_MAX||horizonSeconds<0.0||horizonSeconds>FLT_MAX)return 0;
    double origin[3],velocity[3];
    for(unsigned i=0;i<3;i++){
        if(!isfinite(center[i])||!isfinite(player[i])||!isfinite(playerV[i]))return 0;
        origin[i]=(double)center[i]-player[i];
        velocity[i]=-(double)playerV[i];
    }
    double remaining=lifeSeconds-ageSeconds,untilFull=growSeconds-ageSeconds;
    double end=fmin(remaining,horizonSeconds),cuts[5]={0.0,end};
    unsigned count=2;
    /* Split only where the nearest axis region or the radius slope changes.
       The closed forecast endpoint and open lifetime endpoint are handled
       separately below, including a zero-horizon current overlap. */
    double candidate[3]={untilFull,0.0,0.0};
    if(velocity[1]!=0.0){
        candidate[1]=(COPYDOG_PROJECTILE_BODY_LOW_Y-origin[1])/velocity[1];
        candidate[2]=(COPYDOG_PROJECTILE_BODY_HIGH_Y-origin[1])/velocity[1];
    }
    for(unsigned i=0;i<3;i++){
        double t=candidate[i];
        if(t<=0.0||t>=end)continue;
        unsigned j=1;
        while(cuts[j]<t)j++;
        if(cuts[j]==t)continue;
        for(unsigned k=count;k>j;k--)cuts[k]=cuts[k-1];
        cuts[j]=t;count++;
    }
    for(unsigned i=1;i<count;i++){
        double start=cuts[i-1],stop=cuts[i],dt=stop-start;
        double yMid=origin[1]+velocity[1]*(start+0.5*dt);
        double q[3],u[3];
        for(unsigned k=0;k<3;k++){q[k]=origin[k]+velocity[k]*start;u[k]=velocity[k]*dt;}
        if(yMid<COPYDOG_PROJECTILE_BODY_LOW_Y)q[1]-=COPYDOG_PROJECTILE_BODY_LOW_Y;
        else if(yMid>COPYDOG_PROJECTILE_BODY_HIGH_Y)q[1]-=COPYDOG_PROJECTILE_BODY_HIGH_Y;
        else {q[1]=0.0;u[1]=0.0;}
        double r0=start<untilFull?rStart+(rEnd-rStart)*((ageSeconds+start)/growSeconds):rEnd;
        double r1=stop<untilFull?rStart+(rEnd-rStart)*((ageSeconds+stop)/growSeconds):rEnd;
        double fraction=CdAreaPieceFirst(q,u,COPYDOG_PROJECTILE_BODY_RADIUS+r0,COPYDOG_PROJECTILE_BODY_RADIUS+r1);
        if(fraction<0.0)continue;
        double first=fraction==1.0?stop:start+fraction*dt;
        if(first<remaining){*eta=first;return 1;}
    }
    return 0;
}

static inline float CopydogAreaAwayYaw(const float center[3],const float player[3],
    const float playerV[3],double eta,float facing)
{
    const float stationary[3]={0.0f,0.0f,0.0f};
    if(!isfinite(facing))facing=0.0f;
    if(!center||!player||!playerV||!isfinite(eta)||eta<0.0||eta>FLT_MAX)return facing;
    for(unsigned i=0;i<3;i++)
        if(!isfinite(center[i])||!isfinite(player[i])||!isfinite(playerV[i]))return facing;
    return CopydogProjectileEscapeYaw(center,stationary,player,playerV,eta,facing);
}

#endif
