#ifndef COPYDOG_PROJECTILE_MOTION_H
#define COPYDOG_PROJECTILE_MOTION_H
#include <math.h>
#include <stdint.h>
#include <string.h>

/* Positions are sampled by the normal polling loop, not by projectile count.
   The time and speed limits are estimator safeguards, not native game stats. */
typedef struct { uint32_t at;float p[3]; } COPYDOG_MOTION_POINT;
typedef struct { uintptr_t self;unsigned count,next;COPYDOG_MOTION_POINT points[8]; } COPYDOG_PLAYER_MOTION;

static inline void CopydogMotionSample(COPYDOG_PLAYER_MOTION *m,uintptr_t self,const float *p,uint32_t now)
{
    if(!self||!p||!isfinite(p[0])||!isfinite(p[1])||!isfinite(p[2])){
        memset(m,0,sizeof *m);return;
    }
    if(m->self!=self){memset(m,0,sizeof *m);m->self=self;}
    if(m->count){
        const COPYDOG_MOTION_POINT *last=&m->points[(m->next+7)%8];
        uint32_t dt=now-last->at;if(dt<8)return;
        double ds=0.0;for(unsigned k=0;k<3;k++){double d=(double)p[k]-last->p[k];ds+=d*d;}
        double limit=0.05+0.025*dt;
        if(dt>64||ds>limit*limit){m->next=0;m->count=0;}
    }
    COPYDOG_MOTION_POINT *slot=&m->points[m->next];
    slot->at=now;memcpy(slot->p,p,sizeof slot->p);
    m->next=(m->next+1)%8;if(m->count<8)m->count++;
}

static inline int CopydogMotionVelocity(const COPYDOG_PLAYER_MOTION *m,uintptr_t self,
    const float *p,uint32_t now,float v[3])
{
    memset(v,0,3*sizeof *v);
    if(self!=m->self||!p||m->count<2)return 0;
    const COPYDOG_MOTION_POINT *last=&m->points[(m->next+7)%8],*first=0;
    uint32_t age=now-last->at;
    if(age>48)return 0;
    double residual=0.0;
    for(unsigned k=0;k<3;k++){double d=(double)p[k]-last->p[k];residual+=d*d;}
    double tolerance=0.12+0.025*age;
    if(!isfinite(residual)||residual>tolerance*tolerance)return 0;
    for(unsigned i=1;i<m->count;i++){
        const COPYDOG_MOTION_POINT *point=&m->points[(m->next+7-i)%8];
        uint32_t dt=last->at-point->at;if(dt>48)break;
        if(dt>=16)first=point;
    }
    if(!first)return 0;
    double ds=0.0,scale=1000.0/(last->at-first->at);
    for(unsigned k=0;k<3;k++){v[k]=(float)((last->p[k]-first->p[k])*scale);ds+=(double)v[k]*v[k];}
    if(!isfinite(ds)||ds>25.*25.){memset(v,0,3*sizeof *v);return 0;}
    return 1;
}

/* Escape sideways from a horizontal flight line instead of rolling along it.
   Preserve the player's side/movement when possible. No terrain guarantee. */
static inline float CopydogProjectileEscapeYaw(const float p[3],const float v[3],const float me[3],
    const float playerV[3],double t,float facing)
{
    double x=(double)me[0]-p[0],z=(double)me[2]-p[2],vx=v[0],vz=v[2];
    double speed=hypot(vx,vz),sx,sz;
    if(speed>0.01){
        sx=-vz/speed;sz=vx/speed;
        double side=(x+playerV[0]*t)*sx+(z+playerV[2]*t)*sz;
        if(fabs(side)<0.02)side=(double)playerV[0]*sx+(double)playerV[2]*sz;
        if(side<0){sx=-sx;sz=-sz;}
    }else{
        sx=x+((double)playerV[0]-vx)*t;sz=z+((double)playerV[2]-vz)*t;
        if(hypot(sx,sz)<0.02){sx=playerV[0];sz=playerV[2];}
        if(hypot(sx,sz)<0.02)return isfinite(facing)?facing:0.f;
    }
    /* Engine forward on the horizontal plane is (-sin(yaw),-cos(yaw)). */
    return (float)atan2(-sx,-sz);
}
#endif
