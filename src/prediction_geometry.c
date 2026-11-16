#include "prediction.h"
#include "prediction_limits.h"
#include "prediction_motion.h"
#include <math.h>
#include <stddef.h>
static double dot(const double*a,const double*b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static double clamp(double x){return fmin(1.0,fmax(0.0,x));}
static double distance(const CD_PRED_CAPSULE*a,const CD_PRED_CAPSULE*b)
{
    double u[3],v[3],r[3];
    for(unsigned i=0;i<3;i++){u[i]=a->b[i]-a->a[i];v[i]=b->b[i]-b->a[i];r[i]=a->a[i]-b->a[i];}
    double A=dot(u,u),E=dot(v,v),F=dot(v,r),s=0,t=0;
    if(!isfinite(A)||!isfinite(E)||!isfinite(F))return NAN;
    if(A<=1e-12){if(E>1e-12)t=clamp(F/E);}
    else {
        double C=dot(u,r);if(!isfinite(C))return NAN;
        if(E<=1e-12)s=clamp(-C/A);
        else {
            double B=dot(u,v),den=A*E-B*B;
            if(!isfinite(den))return NAN;
            if(den>1e-12*A*E)s=clamp((B*F-C*E)/den);
            t=(B*s+F)/E;
            if(t<0){t=0;s=clamp(-C/A);}else if(t>1){t=1;s=clamp((B-C)/A);}
        }
    }
    double q[3];for(unsigned i=0;i<3;i++)q[i]=r[i]+s*u[i]-t*v[i];
    double q2=dot(q,q);return isfinite(q2)?sqrt(q2)-a->radius-b->radius:NAN;
}
static CD_PRED_CAPSULE lerp(const CD_PRED_CAPSULE*a,const CD_PRED_CAPSULE*b,double u)
{
    CD_PRED_CAPSULE c;
    for(unsigned i=0;i<3;i++){c.a[i]=a->a[i]+u*(b->a[i]-a->a[i]);c.b[i]=a->b[i]+u*(b->b[i]-a->b[i]);}
    c.radius=a->radius+u*(b->radius-a->radius);return c;
}
typedef struct { const CD_PRED_CAPSULE *a,*A,*b,*B;double duration; } LinearSweep;
typedef struct { const void*path;double (*sample)(const void*,double);double speed;unsigned*budget;double*witness; } Search;
static double linear_clearance(const void*path,double t)
{
    const LinearSweep*s=path;
    CD_PRED_CAPSULE a=lerp(s->a,s->A,t/s->duration),b=lerp(s->b,s->B,t/s->duration);
    return distance(&a,&b);
}
static double clearance(Search*s,double t)
{
    --*s->budget;return s->sample(s->path,t);
}
static int search(Search*s,double l,double r,double dl,double dr,double*out,unsigned depth)
{
    if(!isfinite(dl)||!isfinite(dr))return CD_PRED_UNKNOWN;
    if(dl<=CD_PRED_EPSILON){*out=l;if(s->witness)*s->witness=l;return CD_PRED_CONTACT;}
    if(fmin(fmin(dl,dr),(dl+dr-s->speed*(r-l))*.5)>CD_PRED_EPSILON)return CD_PRED_CLEAR;
    if(!*s->budget)return CD_PRED_UNKNOWN;
    double m=(l+r)*.5,dm=clearance(s,m);
    if(!isfinite(dm))return CD_PRED_UNKNOWN;
    if(r-l<=CD_PRED_TOLERANCE_MS||depth>=16){
        *out=l;
        if(dm>CD_PRED_EPSILON&&dr>CD_PRED_EPSILON)return CD_PRED_POSSIBLE;
        if(s->witness)*s->witness=dm<=CD_PRED_EPSILON?m:r;
        return CD_PRED_CONTACT;
    }
    double left=-1,right=-1;
    int a=search(s,l,m,dl,dm,&left,depth+1);
    if(a==CD_PRED_CONTACT||a==CD_PRED_UNKNOWN){*out=left;return a;}
    int b=search(s,m,r,dm,dr,&right,depth+1);
    if(a==CD_PRED_POSSIBLE){*out=left;return b==CD_PRED_UNKNOWN?CD_PRED_UNKNOWN:a;}
    *out=right;return b;
}
static int linear_sweep(const CD_PRED_CAPSULE*a,const CD_PRED_CAPSULE*A,
    const CD_PRED_CAPSULE*b,const CD_PRED_CAPSULE*B,double duration,double*out,unsigned*budget,double*witness)
{
    if(!out)return CD_PRED_UNKNOWN;
    *out=-1;
    if(!a||!A||!b||!B||!budget||!isfinite(duration)||duration<=0||duration>CD_PRED_MAX_FORECAST_MS||*budget<2)return CD_PRED_UNKNOWN;
    const CD_PRED_CAPSULE*caps[]={a,A,b,B};
    for(unsigned k=0;k<4;k++){
        if(!isfinite(caps[k]->radius)||caps[k]->radius<0)return CD_PRED_UNKNOWN;
        for(unsigned i=0;i<3;i++)if(!isfinite(caps[k]->a[i])||!isfinite(caps[k]->b[i]))return CD_PRED_UNKNOWN;
    }
    double radius=fmax(a->radius,A->radius)+fmax(b->radius,B->radius)+CD_PRED_EPSILON;
    for(unsigned i=0;i<3;i++){
        double amin=fmin(fmin(a->a[i],a->b[i]),fmin(A->a[i],A->b[i]));
        double amax=fmax(fmax(a->a[i],a->b[i]),fmax(A->a[i],A->b[i]));
        double bmin=fmin(fmin(b->a[i],b->b[i]),fmin(B->a[i],B->b[i]));
        double bmax=fmax(fmax(b->a[i],b->b[i]),fmax(B->a[i],B->b[i]));
        if(amax+radius<bmin||bmax+radius<amin)return CD_PRED_CLEAR;
    }
    double speeds[4]={0};
    for(unsigned i=0;i<3;i++){
        speeds[0]+=pow(A->a[i]-a->a[i],2);speeds[1]+=pow(A->b[i]-a->b[i],2);
        speeds[2]+=pow(B->a[i]-b->a[i],2);speeds[3]+=pow(B->b[i]-b->b[i],2);
    }
    LinearSweep path={a,A,b,B,duration};
    Search s={&path,linear_clearance,(sqrt(fmax(speeds[0],speeds[1]))+sqrt(fmax(speeds[2],speeds[3]))+
        fabs(A->radius-a->radius)+fabs(B->radius-b->radius))/duration,budget,witness};
    double dl=clearance(&s,0),dr=clearance(&s,duration);
    return search(&s,0,duration,dl,dr,out,0);
}
int CopydogPredictionSweep(const CD_PRED_CAPSULE*a,const CD_PRED_CAPSULE*A,
    const CD_PRED_CAPSULE*b,const CD_PRED_CAPSULE*B,double duration,double*out,unsigned*budget)
{return linear_sweep(a,A,b,B,duration,out,budget,NULL);}

static void turn_position(const CD_PRED_TURN_POINT*p,double t,double out[3])
{
    double arm[3]={p->arm[0],p->arm[1],p->arm[2]};
    if(p->boneRate!=0.){
        double angle=p->boneRate*t,c=cos(angle),s=sin(angle),along=dot(p->axis,p->arm);
        for(unsigned i=0;i<3;i++){
            unsigned j=(i+1)%3,k=(i+2)%3;
            arm[i]=p->arm[i]*c+(p->axis[j]*p->arm[k]-p->axis[k]*p->arm[j])*s+
                p->axis[i]*along*(1.-c);
        }
    }
    for(unsigned i=0;i<3;i++)out[i]=p->pivot[i]+p->pivotVelocity[i]*t+arm[i];
    if(p->yawRate!=0.){
        double x=out[0],z=out[2],angle=p->yawRate*t,c=cos(angle),s=sin(angle);
        out[0]=c*x+s*z;out[2]=c*z-s*x;
    }
    for(unsigned i=0;i<3;i++)out[i]+=p->root[i]+p->rootVelocity[i]*t;
}
typedef struct { const CD_PRED_TURN_CAPSULE*a,*b;double ageA,ageB; } TurningSweep;
static void turn_capsule(const CD_PRED_TURN_CAPSULE*c,double t,CD_PRED_CAPSULE*out)
{
    turn_position(&c->a,t,out->a);turn_position(&c->b,t,out->b);out->radius=c->radius;
}
static double turning_clearance(const void*path,double t)
{
    const TurningSweep*s=path;CD_PRED_CAPSULE a,b;
    turn_capsule(s->a,s->ageA+t,&a);turn_capsule(s->b,s->ageB+t,&b);
    return distance(&a,&b);
}
static int turn_bounds(const CD_PRED_TURN_POINT*p,double begin,double end,double*speed,double*accel)
{
    double u0[3],u1[3];
    const double*arrays[]={p->root,p->rootVelocity,p->pivot,p->pivotVelocity,p->arm,p->axis};
    for(unsigned j=0;j<sizeof arrays/sizeof arrays[0];j++)
        for(unsigned k=0;k<3;k++)if(!isfinite(arrays[j][k]))return 0;
    if(!isfinite(p->yawRate)||!isfinite(p->boneRate)||
       (p->boneRate!=0.&&fabs(dot(p->axis,p->axis)-1.)>1e-8))return 0;
    for(unsigned k=0;k<3;k++){
        u0[k]=p->pivot[k]+p->pivotVelocity[k]*begin;
        u1[k]=p->pivot[k]+p->pivotVelocity[k]*end;
    }
    double arm=sqrt(dot(p->arm,p->arm)),y=fabs(p->yawRate),w=fabs(p->boneRate);
    double reach=sqrt(fmax(dot(u0,u0),dot(u1,u1)))+arm;
    double localSpeed=sqrt(dot(p->pivotVelocity,p->pivotVelocity))+w*arm;
    *speed=sqrt(dot(p->rootVelocity,p->rootVelocity))+localSpeed+y*reach;
    *accel=y*y*reach+2.*y*localSpeed+w*w*arm;
    return isfinite(*speed)&&isfinite(*accel);
}
int CopydogPredictionTurningSweepWitness(const CD_PRED_TURN_CAPSULE*a,double ageA,
    const CD_PRED_TURN_CAPSULE*b,double ageB,double duration,double*out,unsigned*budget,double*witness)
{
    if(witness)*witness=-1.;
    if(!out)return CD_PRED_UNKNOWN;
    *out=-1;
    if(!a||!b||!budget||*budget<2||!isfinite(duration)||duration<=0||duration>CD_PRED_MAX_FORECAST_MS||
       !isfinite(ageA)||ageA<0||ageA>CD_PRED_MAX_FORECAST_MS+CD_PRED_MAX_AGE_MS||
       !isfinite(ageB)||ageB<0||ageB>CD_PRED_MAX_FORECAST_MS+CD_PRED_MAX_AGE_MS||
       !isfinite(a->radius)||a->radius<0||!isfinite(b->radius)||b->radius<0)return CD_PRED_UNKNOWN;
    const CD_PRED_TURN_POINT*ends[]={&a->a,&a->b,&b->a,&b->b};
    double speed[4],accel[4];
    for(unsigned j=0;j<4;j++){
        double begin=j<2?ageA:ageB;
        if(!turn_bounds(ends[j],begin,begin+duration,&speed[j],&accel[j]))return CD_PRED_UNKNOWN;
    }
    CD_PRED_CAPSULE a0,a1,b0,b1;
    turn_capsule(a,ageA,&a0);turn_capsule(a,ageA+duration,&a1);
    turn_capsule(b,ageB,&b0);turn_capsule(b,ageB+duration,&b1);
    const CD_PRED_CAPSULE*caps[]={&a0,&a1,&b0,&b1};
    for(unsigned j=0;j<4;j++)for(unsigned k=0;k<3;k++)
        if(!isfinite(caps[j]->a[k])||!isfinite(caps[j]->b[k]))return CD_PRED_UNKNOWN;
    double acceleration=fmax(accel[0],accel[1])+fmax(accel[2],accel[3]);
    if(acceleration==0.)return linear_sweep(&a0,&a1,&b0,&b1,duration,out,budget,witness);
    /* For a C2 endpoint, deviation from its same-time chord is at most
       max|p''| h²/8. Padding is used solely to prove an interval clear. */
    double padding=a->radius+b->radius+acceleration*duration*duration/8.+CD_PRED_EPSILON;
    for(unsigned k=0;k<3;k++){
        double amin=fmin(fmin(a0.a[k],a0.b[k]),fmin(a1.a[k],a1.b[k]));
        double amax=fmax(fmax(a0.a[k],a0.b[k]),fmax(a1.a[k],a1.b[k]));
        double bmin=fmin(fmin(b0.a[k],b0.b[k]),fmin(b1.a[k],b1.b[k]));
        double bmax=fmax(fmax(b0.a[k],b0.b[k]),fmax(b1.a[k],b1.b[k]));
        if(amax+padding<bmin||bmax+padding<amin)return CD_PRED_CLEAR;
    }
    TurningSweep path={a,b,ageA,ageB};
    Search s={&path,turning_clearance,fmax(speed[0],speed[1])+fmax(speed[2],speed[3]),budget,witness};
    if(!isfinite(s.speed))return CD_PRED_UNKNOWN;
    *budget-=2;
    return search(&s,0,duration,distance(&a0,&b0),distance(&a1,&b1),out,0);
}
int CopydogPredictionTurningSweep(const CD_PRED_TURN_CAPSULE*a,double ageA,
    const CD_PRED_TURN_CAPSULE*b,double ageB,double duration,double*out,unsigned*budget)
{return CopydogPredictionTurningSweepWitness(a,ageA,b,ageB,duration,out,budget,NULL);}
