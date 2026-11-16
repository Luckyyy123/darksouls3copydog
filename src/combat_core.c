#include "combat.h"
#include "projectile_geometry.h"
#include <math.h>
#include <float.h>

/* Ray versus a sphere over a closed time interval. Cross-product distance
   avoids subtracting two large nearly equal squared ranges at tangency. */
static double first_sphere(double x,double y,double z,double vx,double vy,double vz,
    double radius,double duration)
{
    double radius2=radius*radius,c=x*x+y*y+z*z-radius2;
    if(c<=0.0)return 0.0;
    double a=vx*vx+vy*vy+vz*vz,b=x*vx+y*vy+z*vz;
    if(a<=0.0||b>=0.0)return -1.0;
    double cx=y*vz-z*vy,cy=z*vx-x*vz,cz=x*vy-y*vx;
    double miss2=(cx*cx+cy*cy+cz*cz)/a,margin=radius2-miss2;
    if(margin < -16.0*DBL_EPSILON*radius2)return -1.0;
    double root=c/(-b+sqrt(a)*sqrt(fmax(0.0,margin)));
    return root<=duration?root:-1.0;
}

int CopydogProjectileContact(const float p[3],const float v[3],const float me[3],
    const float playerV[3],double lowY,double highY,double radius,double horizon,double *out)
{
    if(!out)return 0;
    *out=-1.0;
    if(!p||!v||!me||!playerV||!isfinite(lowY)||!isfinite(highY)||!isfinite(radius)||
       !isfinite(horizon)||radius<=0.0||lowY>highY||horizon<0.0||
       fabs(lowY)>FLT_MAX||fabs(highY)>FLT_MAX||radius>FLT_MAX||horizon>FLT_MAX)return 0;
    double r[3],w[3];
    for(unsigned i=0;i<3;i++){
        if(!isfinite(p[i])||!isfinite(v[i])||!isfinite(me[i])||!isfinite(playerV[i]))return 0;
        r[i]=(double)p[i]-me[i];w[i]=(double)v[i]-playerV[i];
    }
    double best=first_sphere(r[0],r[1]-lowY,r[2],w[0],w[1],w[2],radius,horizon);
    if(highY!=lowY&&best!=0.0){
        double top=first_sphere(r[0],r[1]-highY,r[2],w[0],w[1],w[2],radius,horizon);
        if(top>=0.0&&(best<0.0||top<best))best=top;
        /* Middle cylinder: its radial disk is valid only while Y lies
           between the cap centers. Start the disk test at the slab entry. */
        double t0=0.0,t1=best>=0.0?best:horizon;
        if(w[1]!=0.0){
            double a=(lowY-r[1])/w[1],b=(highY-r[1])/w[1];
            t0=fmax(0.0,fmin(a,b));t1=fmin(t1,fmax(a,b));
        }else if(r[1]<lowY||r[1]>highY)t1=-1.0;
        if(t0<=t1){
            double disk=first_sphere(r[0]+w[0]*t0,0.0,r[2]+w[2]*t0,
                w[0],0.0,w[2],radius,t1-t0);
            if(disk>=0.0&&(best<0.0||t0+disk<best))best=t0+disk;
        }
    }
    *out=best;
    return best>=0.0;
}

int CombatBulletApproaching(const float p[3],const float v[3],const float me[3])
{
    static const float still[3]={0,0,0};
    double t;
    return v&&(v[0]!=0.0f||v[1]!=0.0f||v[2]!=0.0f)&&
        CopydogProjectileContact(p,v,me,still,COPYDOG_PROJECTILE_BODY_LOW_Y,
            COPYDOG_PROJECTILE_BODY_HIGH_Y,COPYDOG_PROJECTILE_BODY_RADIUS,COPYDOG_PROJECTILE_HORIZON,&t);
}
int CombatHitProtected(uint32_t flags58,uint32_t flags5c,int anim)
{
    /* Preserve the original inclusive upper endpoint and exact masks.
       These are source conditions, not a claim of invulnerability semantics. */
    return (flags58&0x2)!=0||((flags5c&0x1)!=0&&27100<=anim&&anim<=27200);
}
