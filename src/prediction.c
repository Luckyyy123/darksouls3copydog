#include "prediction.h"
#include "prediction_limits.h"
#include "prediction_motion.h"
#include <windows.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include <stdatomic.h>
typedef struct { int bone;float a[3],b[3],radius; } CD_PRED_BODY;
typedef struct { int bone;float p[3]; } Point;
typedef struct { float begin,end,radius;Point a,b; } CD_PRED_SHAPE;
typedef struct { int weapon,animation,mode,hand;unsigned first,count; } CD_PRED_PROFILE;
#include "prediction_data.h"
typedef struct { uintptr_t skeleton,local;uint32_t localCount,localCapacity;uintptr_t model;uint32_t count,capacity; } PoseHeader;
typedef struct { int animation;float previous,current,duration; } ClockRecord;
_Static_assert(sizeof(PoseHeader)==40,"Native pose header layout");
_Static_assert(sizeof(ClockRecord)==16,"Native animation ring record layout");
typedef struct {
    uintptr_t actor,skeleton,clockOwner,equipment,poseOwner,model;int handle,actorHandle,weapon[2],gearSlot[2],mode,clockSlot;uint32_t stamp,phaseStamp;
    ClockRecord clock;float pose[CD_PRED_MAX_BONES][12],position[3],yaw;
} Snapshot;
typedef struct { Snapshot now,before;double rate,consumedEnd,pendingEnd;int valid,paired,motionPaired,history,expected;uintptr_t checkedSkeleton,episodeActor;uint32_t episode; } Track;
static Track Tracks[CD_PRED_TRACK_COUNT];
static Track AttackTracks[CD_PRED_TRACK_COUNT];
static SRWLOCK PredictionLock=SRWLOCK_INIT;
__declspec(dllexport) _Atomic uint64_t CopydogPredictionSamples,CopydogPredictionQueries,
    CopydogPredictionContacts,CopydogPredictionUnknown,CopydogPredictionClear;
static int read_memory(uintptr_t p,void*out,size_t size)
{
    SIZE_T n=0;
    if(!p||!out||p>UINTPTR_MAX-size||!ReadProcessMemory(GetCurrentProcess(),(void*)p,out,size,&n)||n!=size){
        if(out)memset(out,0,size);
        return 0;
    }
    return 1;
}
static uintptr_t pointer(uintptr_t p){uintptr_t q;return read_memory(p,&q,sizeof q)?q:0;}
/* The producer publishes tail after writing, the consumer advances head.
   A batch may contain several clips during blending. */
static int read_clock_batch(uintptr_t clock,int visible,int newestOnly,int indices[2],ClockRecord*out,int*slot,int*retained)
{
    ClockRecord records[CD_PRED_CLOCK_COUNT];
    if(!read_memory(clock+CD_PRED_CLOCK_INDICES,indices,2*sizeof *indices))return 0;
    int tail=indices[0],head=indices[1],newest=-1;
    if(retained)*retained=0;
    if((unsigned)head>=CD_PRED_CLOCK_COUNT||(unsigned)tail>=CD_PRED_CLOCK_COUNT||(head==tail&&!retained)||
       !read_memory(clock+CD_PRED_CLOCK_RECORDS,records,sizeof records))return 0;
    /* The consumer only advances head to tail. The last published slot may
       be used as history; it does not establish a new pose or game update. */
    if(head==tail){*retained=1;head=(tail+CD_PRED_CLOCK_COUNT-1)%CD_PRED_CLOCK_COUNT;}
    for(int i=tail;i!=head;){
        i=(i+CD_PRED_CLOCK_COUNT-1)%CD_PRED_CLOCK_COUNT;
        const ClockRecord*r=&records[i];
        if(r->animation<0||!isfinite(r->previous)||!isfinite(r->current)||!isfinite(r->duration)||
           r->previous<0||r->current<r->previous||r->duration<=0||r->duration>600||r->current>r->duration+.1){
            if(newestOnly)return 0;
            continue;
        }
        if(newest<0)newest=i;
        if(newestOnly||r->animation==visible){newest=i;break;}
    }
    if(newest<0)return 0;
    *out=records[newest];*slot=newest;return 1;
}
static int clock_still_current(uintptr_t clock,const int indices[2],int slot,const ClockRecord*record)
{
    int after[2];ClockRecord check;
    return read_memory(clock+CD_PRED_CLOCK_RECORDS+slot*sizeof check,&check,sizeof check)&&
        !memcmp(&check,record,sizeof check)&&read_memory(clock+CD_PRED_CLOCK_INDICES,after,sizeof after)&&
        !memcmp(indices,after,sizeof after);
}
static int read_native_frame(uintptr_t actor,CD_NATIVE_FRAME*out,int*retained,int newestOnly)
{
    if(retained)*retained=0;
    if(!out)return 0;
    *out=(CD_NATIVE_FRAME){.index=-1,.animation=-1};
    if(!actor)return 0;
    uintptr_t modules=pointer(actor+CD_PRED_ACTOR_MODULES),clock=pointer(modules+CD_PRED_MODULE_TIMEACT);
    int indices[2],slot,visible=-1,historical=0;ClockRecord r;
    if(!newestOnly&&modules){
        uintptr_t action=pointer(modules+0x80);
        if(action&&!read_memory(action+0xc8,&visible,sizeof visible))visible=-1;
    }
    if(!modules||!clock||!read_clock_batch(clock,visible,newestOnly,indices,&r,&slot,retained?&historical:NULL)||
       !clock_still_current(clock,indices,slot,&r)||
       pointer(modules+CD_PRED_MODULE_TIMEACT)!=clock||pointer(actor+CD_PRED_ACTOR_MODULES)!=modules)return 0;
    if(retained)*retained=historical;
    *out=(CD_NATIVE_FRAME){clock,slot,r.animation,r.previous,r.current,r.duration};return 1;
}
int CopydogReadLatestNativeFrame(uintptr_t actor,CD_NATIVE_FRAME*out,int*retained)
{return read_native_frame(actor,out,retained,0);}
int CopydogReadPublishedNativeFrame(uintptr_t actor,CD_NATIVE_FRAME*out,int*retained)
{return read_native_frame(actor,out,retained,1);}
int CopydogReadAttackNativeFrame(uintptr_t actor,int expected,CD_NATIVE_FRAME*out,int*retained)
{
    if(expected>=0&&CopydogReadPublishedNativeFrame(actor,out,retained)&&out->animation==expected)return 1;
    if(out)*out=(CD_NATIVE_FRAME){.index=-1,.animation=-1};
    if(retained)*retained=0;
    return 0;
}
int CopydogReadNativeFrame(uintptr_t actor,CD_NATIVE_FRAME*out)
{return CopydogReadLatestNativeFrame(actor,out,NULL);}
int CopydogReadNativeClock(uintptr_t actor,int*animation,float*phase)
{
    CD_NATIVE_FRAME frame;
    if(animation)*animation=-1;
    if(phase)*phase=-1;
    if(!animation||!phase||!CopydogReadNativeFrame(actor,&frame))return 0;
    *animation=frame.animation;*phase=frame.current;return 1;
}
void CopydogPredictionEpisode(unsigned slot,uintptr_t actor,uint32_t episode)
{
    if(slot>=CD_PRED_TRACK_COUNT)return;
    AcquireSRWLockExclusive(&PredictionLock);
    Track*t=&Tracks[slot];
    if(t->episodeActor!=actor||t->episode!=episode){
        t->episodeActor=actor;t->episode=episode;t->consumedEnd=t->pendingEnd=0;
        t->valid=t->paired=t->history=0;
    }
    ReleaseSRWLockExclusive(&PredictionLock);
}
void CopydogPredictionResetAll(void)
{
    AcquireSRWLockExclusive(&PredictionLock);
    memset(Tracks,0,sizeof Tracks);
    memset(AttackTracks,0,sizeof AttackTracks);
    ReleaseSRWLockExclusive(&PredictionLock);
}
static int skeleton_matches(uintptr_t skeleton)
{
    uintptr_t bones=pointer(skeleton+0x28);unsigned n=0;
    if(!read_memory(skeleton+0x30,&n,4)||n!=CD_PRED_EXPECTED_BONES||!bones)return 0;
    for(unsigned i=0;i<n;i++){
        char name[64];size_t length=strlen(CdPredBoneNames[i])+1;
        if(length>sizeof name||!read_memory(pointer(bones+i*16),name,length)||
           memcmp(name,CdPredBoneNames[i],length))return 0;
    }
    return 1;
}
static int read_gear(uintptr_t equipment,int*mode,int slots[2],int weapons[2])
{
    return equipment&&read_memory(equipment+0x2b8,mode,sizeof *mode)&&*mode>=1&&*mode<=3&&
        read_memory(equipment+0x2bc,slots,2*sizeof *slots)&&(unsigned)slots[0]<3&&(unsigned)slots[1]<3&&
        read_memory(equipment+0x32c+slots[0]*8,&weapons[0],sizeof weapons[0])&&
        read_memory(equipment+0x330+slots[1]*8,&weapons[1],sizeof weapons[1]);
}
static int attack_binding_current(const Snapshot*s)
{
    int actorHandle,handle,mode,slots[2],weapons[2];PoseHeader h;
    uintptr_t equipment=pointer(s->actor+CD_PRED_ACTOR_EQUIPMENT);
    uintptr_t modules=pointer(s->actor+CD_PRED_ACTOR_MODULES),behavior=modules?pointer(modules+CD_PRED_MODULE_BEHAVIOR):0;
    if(!behavior||pointer(behavior+CD_PRED_BEHAVIOR_POSE)!=s->poseOwner||
       !read_memory(s->poseOwner+CD_PRED_POSE_HEADER,&h,sizeof h)||!h.skeleton||h.skeleton!=s->skeleton||
       !h.model||h.model!=s->model||h.count!=CD_PRED_EXPECTED_BONES||h.localCount!=h.count||
       h.count>(h.capacity&0x3fffffff)||h.count>(h.localCapacity&0x3fffffff))return 0;
    return equipment==s->equipment&&read_memory(s->actor+8,&actorHandle,sizeof actorHandle)&&actorHandle==s->actorHandle&&
        read_memory(s->actor+0x78,&handle,sizeof handle)&&handle==s->handle&&
        read_gear(equipment,&mode,slots,weapons)&&mode==s->mode&&
        !memcmp(slots,s->gearSlot,sizeof slots)&&!memcmp(weapons,s->weapon,sizeof weapons);
}
static int snapshot(uintptr_t actor,Snapshot*out,int allowRetained,int expected,int*wasRetained)
{
    memset(out,0,sizeof *out);out->actor=actor;
    uintptr_t modules=pointer(actor+CD_PRED_ACTOR_MODULES),behavior=pointer(modules+CD_PRED_MODULE_BEHAVIOR);
    uintptr_t owner=pointer(behavior+CD_PRED_BEHAVIOR_POSE),timeact=pointer(modules+CD_PRED_MODULE_TIMEACT);
    uintptr_t physics=pointer(modules+CD_PRED_MODULE_PHYSICS),equipment=pointer(actor+CD_PRED_ACTOR_EQUIPMENT);
    uintptr_t action=pointer(modules+0x80);
    out->equipment=equipment;
    PoseHeader h,check;int indices[2],visible,visibleAfter,retained=0;float transform[7];
    if(!actor||!modules||!behavior||!owner||!timeact||!physics||!equipment||
       !read_memory(owner+CD_PRED_POSE_HEADER,&h,sizeof h)||h.count!=CD_PRED_EXPECTED_BONES||
       (expected>=0&&!h.skeleton)||
       h.localCount!=h.count||h.count>(h.capacity&0x3fffffff)||h.count>(h.localCapacity&0x3fffffff)||
       !action||!read_memory(action+0xc8,&visible,sizeof visible)||
       !read_clock_batch(timeact,visible,expected>=0,indices,&out->clock,&out->clockSlot,allowRetained?&retained:NULL)||
       out->clock.animation!=(expected>=0?expected:visible)||
       !read_memory(physics+0x70,transform,sizeof transform)||
       !read_gear(equipment,&out->mode,out->gearSlot,out->weapon)||
       (expected>=0&&!read_memory(actor+8,&out->actorHandle,sizeof out->actorHandle))||
       !read_memory(actor+0x78,&out->handle,4)||
       !read_memory(h.model,out->pose,h.count*sizeof out->pose[0]))return 0;
    for(unsigned i=0;i<7;i++)if(!isfinite(transform[i]))return 0;
    out->yaw=transform[1];memcpy(out->position,transform+4,sizeof out->position);out->skeleton=h.skeleton;
    out->poseOwner=owner;out->model=h.model;
    for(unsigned i=0;i<h.count;i++){
        float*p=out->pose[i];double q=0;
        for(unsigned k=0;k<12;k++)if(!isfinite(p[k])||fabs(p[k])>1e6)return 0;
        for(unsigned k=4;k<8;k++)q+=(double)p[k]*p[k];
        if(q<.9||q>1.1)return 0;
        for(unsigned k=8;k<11;k++)if(fabs(p[k])>100)return 0;
    }
    if(expected>=0&&(pointer(modules+CD_PRED_MODULE_BEHAVIOR)!=behavior||pointer(modules+CD_PRED_MODULE_PHYSICS)!=physics||
       !attack_binding_current(out)))return 0;
    if(!read_memory(owner+CD_PRED_POSE_HEADER,&check,sizeof check)||memcmp(&h,&check,sizeof h)||
       !clock_still_current(timeact,indices,out->clockSlot,&out->clock)||
       !read_memory(action+0xc8,&visibleAfter,sizeof visibleAfter)||visibleAfter!=visible||
       pointer(modules+0x80)!=action||pointer(modules+CD_PRED_MODULE_TIMEACT)!=timeact||
       pointer(actor+CD_PRED_ACTOR_MODULES)!=modules||
       pointer(behavior+CD_PRED_BEHAVIOR_POSE)!=owner)return 0;
    out->clockOwner=timeact;
    *wasRetained=retained;
    return 1;
}
static void observe(Track*t,uintptr_t actor,uint32_t now,int allowRetained,int expected)
{
    Snapshot s;int retained=0;
    if(!actor||!snapshot(actor,&s,allowRetained,expected,&retained)){
        t->valid=0;
        if(expected>=0||!actor||actor!=t->now.actor||now-t->now.stamp>CD_PRED_MAX_GAP_MS)t->history=t->paired=t->motionPaired=0;
        return;
    }
    s.stamp=s.phaseStamp=now;
    if(t->now.actor!=s.actor||t->now.handle!=s.handle||t->now.clockOwner!=s.clockOwner||
       t->now.clock.animation!=s.clock.animation||t->now.clock.duration!=s.clock.duration||
       t->now.mode!=s.mode||memcmp(t->now.weapon,s.weapon,sizeof s.weapon)||s.clock.current<t->now.clock.current||
       now-t->now.stamp>CD_PRED_MAX_GAP_MS)t->consumedEnd=t->pendingEnd=0;
    if(t->checkedSkeleton!=s.skeleton){
        if(!skeleton_matches(s.skeleton)){
            t->valid=t->paired=0;
            if(expected>=0)t->history=t->motionPaired=0;
            return;
        }
        t->checkedSkeleton=s.skeleton;
    }
    int same=t->history&&t->now.actor==s.actor&&t->now.handle==s.handle&&t->now.skeleton==s.skeleton&&
        t->now.clockOwner==s.clockOwner&&
        t->now.clock.animation==s.clock.animation&&t->now.clock.duration==s.clock.duration&&t->now.mode==s.mode&&
        !memcmp(t->now.weapon,s.weapon,sizeof s.weapon);
    if(expected>=0&&(s.actorHandle!=t->now.actorHandle||s.equipment!=t->now.equipment||s.poseOwner!=t->now.poseOwner||
       memcmp(s.gearSlot,t->now.gearSlot,sizeof s.gearSlot)))same=0;
    uint32_t dt=now-t->now.stamp;
    if(expected>=0&&retained){
        /* A consumed slot can corroborate a live sample already seen for this
           episode. Advancing poses, clocks or sample counters would re-arm it. */
        if(!same||s.model!=t->now.model||s.clockSlot!=t->now.clockSlot||memcmp(&s.clock,&t->now.clock,sizeof s.clock)||
           now-t->now.phaseStamp>CD_PRED_MAX_AGE_MS)t->valid=t->history=t->paired=t->motionPaired=0;
        return;
    }
    if(expected>=0&&same&&s.clock.previous<t->now.clock.previous)same=0;
    int heldPhase=0;
    if(same&&!memcmp(&s.clock,&t->now.clock,sizeof s.clock)){
        int moved=s.yaw!=t->now.yaw||memcmp(s.position,t->now.position,sizeof s.position)||
            memcmp(s.pose,t->now.pose,CD_PRED_EXPECTED_BONES*sizeof s.pose[0]);
        if(!allowRetained||!moved){
            t->now.clockSlot=s.clockSlot;
            t->valid=dt<=CD_PRED_MAX_AGE_MS;return;
        }
        s.phaseStamp=t->now.phaseStamp;
        heldPhase=expected>=0&&t->paired&&now-s.phaseStamp<=CD_PRED_MAX_AGE_MS;
    }
    double phase=same?1000.0*(s.clock.current-t->now.clock.current):0;
    uint32_t phaseDt=now-t->now.phaseStamp;
    t->motionPaired=same&&dt>0&&dt<=CD_PRED_MAX_GAP_MS&&phase>=0;
    t->paired=t->motionPaired&&((phase>0&&phaseDt>0&&phaseDt<=CD_PRED_MAX_GAP_MS)||heldPhase);
    if(!heldPhase)t->rate=t->paired?phase/phaseDt:0;
    if(t->rate<CD_PRED_MIN_RATE||t->rate>CD_PRED_MAX_RATE)t->paired=0;
    t->before=t->now;t->now=s;t->valid=t->history=1;
    atomic_fetch_add(&CopydogPredictionSamples,1);
}
void CopydogPredictionObserveFrame(unsigned slot,uintptr_t actor,uint32_t now,int allowRetained)
{
    if(slot>=CD_PRED_TRACK_COUNT)return;
    AcquireSRWLockExclusive(&PredictionLock);
    observe(&Tracks[slot],actor,now,allowRetained,-1);
    ReleaseSRWLockExclusive(&PredictionLock);
}
void CopydogPredictionObserveAttackFrame(unsigned slot,uintptr_t actor,uint32_t now,int allowRetained,int expected,uint32_t episode)
{
    if(!slot||slot>=CD_PRED_TRACK_COUNT)return;
    AcquireSRWLockExclusive(&PredictionLock);
    Track*t=&AttackTracks[slot];
    if(!actor||expected<0){
        if(t->episodeActor)memset(t,0,sizeof *t);
    }else{
        if(t->episodeActor!=actor||t->episode!=episode||t->expected!=expected){
            memset(t,0,sizeof *t);
            t->episodeActor=actor;t->episode=episode;t->expected=expected;
        }
        observe(t,actor,now,allowRetained,expected);
    }
    ReleaseSRWLockExclusive(&PredictionLock);
}
void CopydogPredictionObserve(unsigned slot,uintptr_t actor,uint32_t now)
{CopydogPredictionObserveFrame(slot,actor,now,0);}
static int matches_frame(const Snapshot*s,const CD_NATIVE_FRAME*f)
{
    return f&&s->clockOwner==f->clock&&s->clockSlot==f->index&&s->clock.animation==f->animation&&
        s->clock.current==f->current&&s->clock.previous==f->previous&&s->clock.duration==f->duration;
}
static uint32_t frame_origin(const Track*tracks,unsigned slot,uintptr_t actor,const CD_NATIVE_FRAME*f,uint32_t observed,uint32_t now)
{
    if(slot>=CD_PRED_TRACK_COUNT)return observed;
    const Track*t=&tracks[slot];
    if(t->history&&t->now.actor==actor&&matches_frame(&t->now,f)){
        uint32_t age=now-t->now.phaseStamp;
        if(age<INT32_MAX&&age>now-observed)return t->now.phaseStamp;
    }
    return observed;
}
uint32_t CopydogPredictionFrameOrigin(unsigned slot,uintptr_t actor,const CD_NATIVE_FRAME*f,uint32_t observed,uint32_t now)
{
    AcquireSRWLockShared(&PredictionLock);
    uint32_t origin=frame_origin(Tracks,slot,actor,f,observed,now);
    ReleaseSRWLockShared(&PredictionLock);
    return origin;
}
uint32_t CopydogPredictionAttackFrameOrigin(unsigned slot,uintptr_t actor,const CD_NATIVE_FRAME*f,uint32_t observed,uint32_t now)
{
    AcquireSRWLockShared(&PredictionLock);
    uint32_t origin=frame_origin(AttackTracks,slot,actor,f,observed,now);
    ReleaseSRWLockShared(&PredictionLock);
    return origin;
}
static int transformed_point(const Snapshot*s,Point p,const float root[3],double out[3])
{
    if(p.bone<0||p.bone>=CD_PRED_EXPECTED_BONES)return 0;
    const float*m=s->pose[p.bone];double v[3],u[3]={m[4],m[5],m[6]};
    for(unsigned i=0;i<3;i++)v[i]=(double)p.p[i]*m[8+i];
    double uv[3]={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
    double uu[3]={u[1]*uv[2]-u[2]*uv[1],u[2]*uv[0]-u[0]*uv[2],u[0]*uv[1]-u[1]*uv[0]};
    for(unsigned i=0;i<3;i++)v[i]+=2*(m[7]*uv[i]+uu[i])+m[i];
    double c=cos(s->yaw),sn=sin(s->yaw);
    out[0]=root[0]+c*v[0]+sn*v[2];out[1]=root[1]+v[1];out[2]=root[2]-sn*v[0]+c*v[2];
    return 1;
}
static int point(const Snapshot*s,Point p,double out[3])
{return transformed_point(s,p,s->position,out);}
static double scale(const Snapshot*s,int bone)
{
    const float*p=s->pose[bone];return fmax(fabs(p[8]),fmax(fabs(p[9]),fabs(p[10])));
}
static int moving_capsule(Track*t,Point a,Point b,double radius,double lo,double hi,
    CD_PRED_CAPSULE*out,CD_PRED_CAPSULE*end)
{
    double p[3],q[3];
    if(!point(&t->now,a,out->a)||!point(&t->now,b,out->b)||!point(&t->before,a,p)||!point(&t->before,b,q))return 0;
    double dt=t->now.stamp-t->before.stamp,speedA=0,speedB=0;
    for(unsigned i=0;i<3;i++){
        double va=(out->a[i]-p[i])/dt,vb=(out->b[i]-q[i])/dt;
        speedA+=va*va;speedB+=vb*vb;
        end->a[i]=out->a[i]+hi*va;end->b[i]=out->b[i]+hi*vb;
        out->a[i]+=lo*va;out->b[i]+=lo*vb;
    }
    if(fmax(speedA,speedB)>pow(CD_PRED_MAX_ENDPOINT_SPEED/1000.,2))return 0;
    out->radius=end->radius=radius*fmax(fmax(scale(&t->now,a.bone),scale(&t->now,b.bone)),
        fmax(scale(&t->before,a.bone),scale(&t->before,b.bone)));
    return 1;
}
typedef struct { double dt,c,s,rate,velocity[3]; } TurnBasis;
static double norm3(const double v[3]){return sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);}
static void facing(const TurnBasis*b,const double local[3],double world[3])
{
    world[0]=b->c*local[0]+b->s*local[2];world[1]=local[1];world[2]=-b->s*local[0]+b->c*local[2];
}
static void cross3(const double a[3],const double b[3],double out[3])
{
    out[0]=a[1]*b[2]-a[2]*b[1];out[1]=a[2]*b[0]-a[0]*b[2];out[2]=a[0]*b[1]-a[1]*b[0];
}
static int turn_basis(const Track*t,TurnBasis*b)
{
    b->dt=(uint32_t)(t->now.stamp-t->before.stamp);
    if(b->dt<1.||b->dt>CD_PRED_MAX_GAP_MS)return 0;
    double angle=remainder((double)t->now.yaw-t->before.yaw,2.*M_PI);
    if(M_PI-fabs(angle)<CD_PRED_TURN_SIGN_TOLERANCE)return 0;
    b->rate=angle/b->dt;b->c=cos(t->now.yaw);b->s=sin(t->now.yaw);
    for(unsigned k=0;k<3;k++)b->velocity[k]=((double)t->now.position[k]-t->before.position[k])/b->dt;
    return norm3(b->velocity)<=CD_PRED_MAX_ENDPOINT_SPEED/1000.;
}
static void unit_quat(const float*raw,double q[4])
{
    double n=0;for(unsigned k=0;k<4;k++)n+=(double)raw[k]*raw[k];
    n=sqrt(n);for(unsigned k=0;k<4;k++)q[k]=raw[k]/n;
}
static int bind_turn_point(const Track*t,const TurnBasis*b,Point p,CD_PRED_TURN_POINT*out)
{
    if(p.bone<0||p.bone>=CD_PRED_EXPECTED_BONES)return 0;
    const float*m=t->now.pose[p.bone],*old=t->before.pose[p.bone];
    *out=(CD_PRED_TURN_POINT){.yawRate=b->rate};
    double pivot[3],velocity[3],offset[3];
    for(unsigned k=0;k<3;k++){
        if(fabs(m[8+k]-old[8+k])>CD_PRED_TURN_SCALE_TOLERANCE*fmax(1.,fabs(m[8+k])))return 0;
        out->root[k]=t->now.position[k];out->rootVelocity[k]=b->velocity[k];
        pivot[k]=m[k];velocity[k]=((double)m[k]-old[k])/b->dt;offset[k]=p.p[k]*(double)m[8+k];
    }
    facing(b,pivot,out->pivot);facing(b,velocity,out->pivotVelocity);
    double arm=norm3(offset);
    if(arm!=0.){
        double q[4],v[3],twice[3],local[3];
        unit_quat(m+4,q);cross3(q,offset,v);cross3(q,v,twice);
        for(unsigned k=0;k<3;k++)local[k]=offset[k]+2.*(q[3]*v[k]+twice[k]);
        facing(b,local,out->arm);
        if(memcmp(m+4,old+4,4*sizeof(float))){
            double prior[4],xyz[3],w=0;unit_quat(old+4,prior);cross3(prior,q,xyz);
            for(unsigned k=0;k<4;k++)w+=q[k]*prior[k];
            for(unsigned k=0;k<3;k++)xyz[k]+=prior[3]*q[k]-q[3]*prior[k];
            /* q and -q describe the same pose. The sign at a half turn
               cannot be recovered from the two samples alone. */
            if(fabs(w)<CD_PRED_TURN_SIGN_TOLERANCE)return 0;
            double n=norm3(xyz);
            if(n>1e-12){
                for(unsigned k=0;k<3;k++)xyz[k]*=(w<0.?-1.:1.)/n;
                facing(b,xyz,out->axis);out->boneRate=2.*atan2(n,fabs(w))/b->dt;
            }
        }
    }
    double upper=norm3(b->velocity)+norm3(velocity)+fabs(b->rate)*(norm3(pivot)+arm)+out->boneRate*arm;
    return upper<=CD_PRED_MAX_ENDPOINT_SPEED/1000.;
}
static int bind_turn_capsule(const Track*t,const TurnBasis*b,Point a,Point end,double radius,CD_PRED_TURN_CAPSULE*out)
{
    if(!bind_turn_point(t,b,a,&out->a)||!bind_turn_point(t,b,end,&out->b))return 0;
    out->radius=radius*fmax(fmax(scale(&t->now,a.bone),scale(&t->now,end.bone)),
        fmax(scale(&t->before,a.bone),scale(&t->before,end.bone)));return 1;
}
static double turn_rate(const CD_PRED_TURN_CAPSULE*c)
{return fabs(c->a.yawRate)+fmax(c->a.boneRate,c->b.boneRate);}
static double trusted_turn_end(double rate,double poseAge)
{
    return rate>0.?fmax(CD_PRED_HORIZON_MS,CD_PRED_LONG_TURN_LIMIT_RAD/rate)-poseAge:INFINITY;
}

typedef struct {double low[3],high[3];} TurnBox;
/* An untrusted future bone axis can point anywhere, so each arm stays inside
   the full sphere about its pivot. Root yaw can turn that pivot freely in XZ.
   The radius of a linearly translating pivot is convex in time. Thus the two
   endpoint bounds enclose the whole root/pivot trajectory on each axis. */
static TurnBox turn_box(const CD_PRED_TURN_CAPSULE*c,double begin,double end)
{
    TurnBox box={{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
    const CD_PRED_TURN_POINT*points[2]={&c->a,&c->b};
    for(unsigned i=0;i<2;i++){
        const CD_PRED_TURN_POINT*p=points[i];
        double reach=c->radius+norm3(p->arm);
        for(unsigned edge=0;edge<2;edge++){
            double t=edge?end:begin,pivot[3];
            for(unsigned k=0;k<3;k++)pivot[k]=p->pivot[k]+t*p->pivotVelocity[k];
            double horizontal=hypot(pivot[0],pivot[2])+reach;
            for(unsigned k=0;k<3;k++){
                double center=p->root[k]+t*p->rootVelocity[k]+(k==1?pivot[1]:0.);
                double extent=k==1?reach:horizontal;
                box.low[k]=fmin(box.low[k],center-extent);
                box.high[k]=fmax(box.high[k],center+extent);
            }
        }
    }
    return box;
}
static int separated_for_any_turn(const CD_PRED_TURN_CAPSULE*a,double ageA,
    const CD_PRED_TURN_CAPSULE*b,double ageB,double begin,double end)
{
    TurnBox first=turn_box(a,ageA+begin,ageA+end),second=turn_box(b,ageB+begin,ageB+end);
    for(unsigned k=0;k<3;k++)
        if(first.high[k]+CD_PRED_EPSILON<second.low[k]||second.high[k]+CD_PRED_EPSILON<first.low[k])return 1;
    return 0;
}
typedef struct {double time;unsigned reason;} UnresolvedContact;
static void unresolved_from(UnresolvedContact*u,double time,unsigned reason)
{
    if(time<u->time){u->time=time;u->reason=reason;}
}
static const CD_PRED_PROFILE*profile(int weapon,int animation,int mode,int hand)
{
    size_t lo=0,hi=sizeof CdPredProfiles/sizeof CdPredProfiles[0];
    while(lo<hi){
        size_t mid=lo+(hi-lo)/2;const CD_PRED_PROFILE*p=&CdPredProfiles[mid];
        int cmp=weapon!=p->weapon?(weapon<p->weapon?-1:1):animation!=p->animation?(animation<p->animation?-1:1):
            mode!=p->mode?(mode<p->mode?-1:1):hand!=p->hand?(hand<p->hand?-1:1):0;
        if(cmp<0)hi=mid;else if(cmp>0)lo=mid+1;else return p;
    }
    return 0;
}
static int attack_hand(int animation,int mode)
{
    int move=animation%1000000;
    if(move>=40000)return mode==2?0:1;
    return move>=32000&&move<34000?0:1;
}
int CopydogPredictionHasAttackShape(int weapon,int animation,int mode)
{
    if(weapon<=0||animation<0||mode<1||mode>3)return 0;
    const CD_PRED_PROFILE*p=profile(weapon/10000*10000,animation,mode,attack_hand(animation,mode));
    return p&&p->count;
}
static int current_attack_pair(const Track*t,uintptr_t actor,const CD_NATIVE_FRAME*frame,uint32_t now)
{
    CD_NATIVE_FRAME current;int retained;
    return actor&&frame&&t->valid&&t->paired&&t->episodeActor==actor&&t->now.actor==actor&&
        now-t->now.stamp<=CD_PRED_MAX_AGE_MS&&now-t->now.phaseStamp<=CD_PRED_MAX_AGE_MS&&
        matches_frame(&t->now,frame)&&attack_binding_current(&t->now)&&
        CopydogReadAttackNativeFrame(actor,t->expected,&current,&retained)&&matches_frame(&t->now,&current);
}
static int blade_point_moved(const Track*t,Point p)
{
    const float origin[3]={0};double before[3],current[3];
    if(!transformed_point(&t->before,p,origin,before)||!transformed_point(&t->now,p,origin,current))return 0;
    for(unsigned i=0;i<3;i++){
        /* The transforms are floats. Ignore displacement within their
           roundoff, and exclude common root translation from both poses. */
        double tolerance=8.*FLT_EPSILON*fmax(1.,fmax(fabs(before[i]),fabs(current[i])));
        if(fabs(current[i]-before[i])>tolerance)return 1;
    }
    return 0;
}
static int attack_shape_ready(const Track*t,const CD_PRED_SHAPE*s)
{
    double phase=1000.*t->now.clock.current;
    if(phase>=s->end)return 0;
    return phase>=s->begin||blade_point_moved(t,s->a)||blade_point_moved(t,s->b);
}
int CopydogPredictionAttackMotionReady(unsigned slot,uintptr_t actor,const CD_NATIVE_FRAME*frame,int weapon,int mode,uint32_t now)
{
    if(!slot||slot>=CD_PRED_TRACK_COUNT||!frame||weapon<=0||mode<1||mode>3)return 0;
    int hand=attack_hand(frame->animation,mode),base=weapon/10000*10000,ready=0;
    const CD_PRED_PROFILE*p=profile(base,frame->animation,mode,hand);
    if(!p)return 0;
    AcquireSRWLockShared(&PredictionLock);
    Track*t=&AttackTracks[slot];TurnBasis basis;
    if(t->now.mode==mode&&t->now.weapon[hand]/10000*10000==base&&
       current_attack_pair(t,actor,frame,now)&&turn_basis(t,&basis)){
        for(unsigned i=0;i<p->count;i++){
            const CD_PRED_SHAPE*s=&CdPredShapes[p->first+i];CD_PRED_TURN_CAPSULE capsule;
            if(attack_shape_ready(t,s)&&bind_turn_capsule(t,&basis,s->a,s->b,s->radius,&capsule)){ready=1;break;}
        }
    }
    ReleaseSRWLockShared(&PredictionLock);
    return ready;
}
/* Eligibility and shape times come from separately rounded resource tables.
   Only a contiguous run of tested shapes can justify discarding this period. */
static double covered_phase(const CD_PRED_SHAPE *shapes,unsigned count,double start,
    double limit,unsigned *budget,int *budgetExhausted)
{
    double covered=start;
    *budgetExhausted=0;
    while(covered<limit){
        double before=covered;
        for(unsigned i=0;i<count;i++){
            if(!*budget){*budgetExhausted=1;return covered;}
            --*budget;
            if(shapes[i].begin<=covered&&shapes[i].end>covered){
                covered=shapes[i].end;
                if(covered>=limit)return limit;
            }
        }
        if(covered==before)break;
    }
    return covered;
}
void CopydogPredictionConsume(unsigned slot){if(slot<CD_PRED_TRACK_COUNT)Tracks[slot].consumedEnd=Tracks[slot].pendingEnd;}
static int prediction_query(Track*sources,unsigned slot,uintptr_t actor,uintptr_t self,uint32_t now,
    double horizon,int consumeAware,double*contactMs,int*animation,const CD_PRED_PARRY_BAND*band,CD_PRED_PARRY_RESULT*detail)
{
    if(contactMs)*contactMs=-1;
    if(animation)*animation=-1;
    if(!contactMs||!animation||slot==0||slot>=CD_PRED_TRACK_COUNT||!isfinite(horizon)||horizon<=0||horizon>CD_PRED_MAX_FORECAST_MS)return CD_PRED_UNKNOWN;
    atomic_fetch_add(&CopydogPredictionQueries,1);
    Track*a=&sources[slot],*b=&Tracks[0];
    int result=CD_PRED_UNKNOWN;
    unsigned unknownReason=CD_PRED_UNK_POSE;
    if(!actor||!self||a->now.actor!=actor||b->now.actor!=self||!a->valid||!b->valid||
       !(band?a->motionPaired:a->paired)||!b->motionPaired||now-a->now.stamp>CD_PRED_MAX_AGE_MS||
       now-b->now.stamp>CD_PRED_MAX_AGE_MS)goto done;
    double age=now-a->now.stamp,bage=now-b->now.stamp;
    const CD_NATIVE_FRAME*frame=band?band->frame:NULL;
    if(frame&&(!matches_frame(&a->now,frame)||band->mode!=a->now.mode||
       now-band->phaseTick>CD_PRED_MAX_AGE_MS))goto done;
    if(sources==AttackTracks&&!current_attack_pair(a,actor,frame,now))goto done;
    double rate=band?band->rate:a->rate;
    double phaseAge=band?(double)(uint32_t)(now-band->phaseTick):age;
    double phase=1000.*a->now.clock.current+phaseAge*rate;
    int hand=attack_hand(a->now.clock.animation,a->now.mode);
    int weapon=a->now.weapon[hand]/10000*10000;
    unknownReason=CD_PRED_UNK_SHAPE;
    if(band&&band->weapon!=weapon)goto done;
    const CD_PRED_PROFILE*p=profile(weapon,a->now.clock.animation,a->now.mode,hand);
    if(!p)goto done;
    *animation=p->animation;result=CD_PRED_CLEAR;
    double bandStart=band?band->begin:0,bandEnd=band?band->end:INFINITY;
    TurnBasis at,bt;
    if(band&&(!turn_basis(a,&at)||!turn_basis(b,&bt))){unknownReason=CD_PRED_UNK_OTHER;goto unknown;}
    enum { BODY_COUNT=sizeof CdPredBodies/sizeof CdPredBodies[0] };
    CD_PRED_TURN_CAPSULE bodyMotion[BODY_COUNT];
    unsigned char built[BODY_COUNT]={0};int tested=0,beyondHorizon=0;
    unsigned localBudget=CD_PRED_WORK_BUDGET;double best=horizon+1;
    double firstWitness=INFINITY,firstPossible=INFINITY;
    UnresolvedContact unresolved={INFINITY,CD_PRED_UNK_NONE};
    double earliest=fmax(0.,(bandStart-phase)/rate);
    unsigned*work=band?band->budget:&localBudget;
    for(unsigned k=0;k<p->count;k++){
        const CD_PRED_SHAPE*s=&CdPredShapes[p->first+k];
        if(consumeAware&&s->end<=a->consumedEnd)continue;
        double begin=fmax(s->begin,bandStart),end=fmin(s->end,bandEnd);
        double lo=fmax(0,(begin-phase)/rate);
        double hi=fmin(horizon,(end-phase)/rate);
        if(lo>=hi){
            if(band&&begin<end&&lo>=horizon)beyondHorizon=1;
            continue;
        }
        if(band&&firstWitness<=lo)continue;
        if(sources==AttackTracks&&!attack_shape_ready(a,s)){unresolved_from(&unresolved,lo,CD_PRED_UNK_SHAPE);continue;}
        hi=nextafter(hi,lo);
        CD_PRED_CAPSULE a0,a1;CD_PRED_TURN_CAPSULE attack;
        if(band?!bind_turn_capsule(a,&at,s->a,s->b,s->radius,&attack):
           !moving_capsule(a,s->a,s->b,s->radius,age+lo,age+hi,&a0,&a1)){
            if(band){unresolved_from(&unresolved,lo,CD_PRED_UNK_OTHER);continue;}
            unknownReason=CD_PRED_UNK_OTHER;goto unknown;
        }
        tested=1;
        for(unsigned j=0;j<sizeof CdPredBodies/sizeof CdPredBodies[0];j++){
            if(band&&firstWitness<=lo)break;
            const CD_PRED_BODY*body=&CdPredBodies[j];CD_PRED_CAPSULE b0,b1;
            Point ba={body->bone,{body->a[0],body->a[1],body->a[2]}},bb={body->bone,{body->b[0],body->b[1],body->b[2]}};
            double hit,witness=-1.;int r;
            if(band){
                if(!built[j]){
                    if(!bind_turn_capsule(b,&bt,ba,bb,body->radius,&bodyMotion[j])){
                        unresolved_from(&unresolved,lo,CD_PRED_UNK_OTHER);continue;
                    }
                    built[j]=1;
                }
                double ar=turn_rate(&attack),br=turn_rate(&bodyMotion[j]);
                if(ar||br)detail->turning=1;
                if(*work<3){unknownReason=CD_PRED_UNK_BUDGET;goto unknown;}
                --*work;
                double end=hi,trust=fmin(trusted_turn_end(ar,age),trusted_turn_end(br,bage));
                if(trust<hi){
                    double suffix=fmax(lo,trust);
                    if(!separated_for_any_turn(&attack,age,&bodyMotion[j],bage,suffix,hi))
                        unresolved_from(&unresolved,suffix,CD_PRED_UNK_TURN_LIMIT);
                    if(trust<=lo)continue;
                    end=trust;
                }
                r=CopydogPredictionTurningSweepWitness(&attack,age+lo,&bodyMotion[j],bage+lo,end-lo,&hit,work,&witness);
            }else{
                if(!moving_capsule(b,ba,bb,body->radius,bage+lo,bage+hi,&b0,&b1)){unknownReason=CD_PRED_UNK_OTHER;goto unknown;}
                r=CopydogPredictionSweep(&a0,&a1,&b0,&b1,hi-lo,&hit,work);
            }
            if(r==CD_PRED_UNKNOWN){
                unknownReason=*work?CD_PRED_UNK_NARROW_PHASE:CD_PRED_UNK_BUDGET;
                if(band&&*work){unresolved_from(&unresolved,lo,unknownReason);continue;}
                goto unknown;
            }
            if(band){
                if(r==CD_PRED_CONTACT){
                    best=fmin(best,lo+hit);
                    firstWitness=fmin(firstWitness,lo+witness);
                    if(firstWitness<=earliest){result=r;goto result_ready;}
                }else if(r==CD_PRED_POSSIBLE){
                    firstPossible=fmin(firstPossible,lo+hit);
                }
            }else if(r>0&&lo+hit<best){
                best=lo+hit;result=r;if(consumeAware)a->pendingEnd=s->end;
            }
        }
    }
    if(isfinite(firstWitness)&&firstWitness<=firstPossible)result=CD_PRED_CONTACT;
    else if(isfinite(firstPossible)){best=fmin(best,firstPossible);result=CD_PRED_POSSIBLE;}
    if(isfinite(unresolved.time)&&(result<=0||unresolved.time<(result==CD_PRED_CONTACT?firstWitness:best))){
        unknownReason=unresolved.reason;goto unknown;
    }
result_ready:
    if(band&&!tested){
        unknownReason=beyondHorizon?CD_PRED_UNK_HORIZON:CD_PRED_UNK_SHAPE;goto unknown;
    }
    if(detail&&result==CD_PRED_CLEAR){
        double start=fmax(phase,bandStart);
        double trusted=fmin(horizon,CD_PRED_HORIZON_MS-fmax(age,bage));
        double clipEnd=1000.*a->now.clock.duration,trustedEnd=phase+trusted*rate;
        double limit=fmin(bandEnd,fmin(clipEnd,trustedEnd));
        if(start<limit){
            int exhausted;
            double end=covered_phase(CdPredShapes+p->first,p->count,start,limit,work,&exhausted);
            if(end>start){
                detail->clearThrough=(end-phase)/rate;
                detail->complete=end>=bandEnd;
            }else{
                unknownReason=exhausted?CD_PRED_UNK_BUDGET:CD_PRED_UNK_SHAPE;goto unknown;
            }
        }else{
            unknownReason=start<bandEnd&&start<clipEnd&&trustedEnd<=start?CD_PRED_UNK_HORIZON:CD_PRED_UNK_SHAPE;
            goto unknown;
        }
    }
    if(result>0)*contactMs=best;
    else if(!frame&&phase+horizon*a->rate>a->now.clock.duration*1000)result=CD_PRED_UNKNOWN;
    goto done;
unknown:
    result=CD_PRED_UNKNOWN;
done:
    if(detail)detail->unknownReason=result==CD_PRED_UNKNOWN?unknownReason:CD_PRED_UNK_NONE;
    if(detail&&result!=CD_PRED_CLEAR){detail->clearThrough=0;detail->complete=0;}
    if(result==CD_PRED_UNKNOWN)atomic_fetch_add(&CopydogPredictionUnknown,1);
    else if(result==CD_PRED_CLEAR)atomic_fetch_add(&CopydogPredictionClear,1);
    else atomic_fetch_add(&CopydogPredictionContacts,1);
    return result;
}
int CopydogPredictionQuery(unsigned slot,uintptr_t actor,uintptr_t self,uint32_t now,double*out,int*animation)
{return prediction_query(Tracks,slot,actor,self,now,CD_PRED_HORIZON_MS,1,out,animation,NULL,NULL);}
int CopydogPredictionForecast(unsigned slot,uintptr_t actor,uintptr_t self,uint32_t now,double horizon,double*out,int*animation)
{return prediction_query(Tracks,slot,actor,self,now,horizon,0,out,animation,NULL,NULL);}
static int parry_contact(Track*sources,unsigned slot,uintptr_t actor,uintptr_t self,uint32_t now,
    const CD_PRED_PARRY_BAND*band,CD_PRED_PARRY_RESULT*out)
{
    int animation;
    if(!out)return CD_PRED_UNKNOWN;
    *out=(CD_PRED_PARRY_RESULT){.contact=-1,.unknownReason=CD_PRED_UNK_OTHER};
    if(band&&!band->frame){out->unknownReason=CD_PRED_UNK_POSE;return CD_PRED_UNKNOWN;}
    if(!band||!band->budget||!isfinite(band->rate)||
       band->rate<CD_PRED_MIN_RATE||band->rate>CD_PRED_MAX_RATE||
       !isfinite(band->begin)||!isfinite(band->end)||band->begin<0||band->begin>=band->end)return CD_PRED_UNKNOWN;
    return prediction_query(sources,slot,actor,self,now,band->horizon,0,&out->contact,&animation,band,out);
}
int CopydogPredictionParryContact(unsigned slot,uintptr_t actor,uintptr_t self,uint32_t now,
    const CD_PRED_PARRY_BAND*band,CD_PRED_PARRY_RESULT*out)
{
    AcquireSRWLockShared(&PredictionLock);
    int result=parry_contact(Tracks,slot,actor,self,now,band,out);
    ReleaseSRWLockShared(&PredictionLock);
    return result;
}
int CopydogPredictionAttackParryContact(unsigned slot,uintptr_t actor,uintptr_t self,uint32_t now,
    const CD_PRED_PARRY_BAND*band,CD_PRED_PARRY_RESULT*out)
{
    AcquireSRWLockShared(&PredictionLock);
    int result=parry_contact(AttackTracks,slot,actor,self,now,band,out);
    ReleaseSRWLockShared(&PredictionLock);
    return result;
}
int CopydogPredictionBullet(uintptr_t self,const float position[3],const float velocity[3],float radius,uint32_t now,double*out)
{
    if(!out)return CD_PRED_UNKNOWN;
    *out=-1;
    if(!position||!velocity||!isfinite(radius)||radius<0)return CD_PRED_UNKNOWN;
    Track body;
    AcquireSRWLockShared(&PredictionLock);
    body=Tracks[0];
    ReleaseSRWLockShared(&PredictionLock);
    if(!self||body.now.actor!=self||!body.valid||!body.paired||now-body.now.stamp>CD_PRED_MAX_AGE_MS)return CD_PRED_UNKNOWN;
    CD_PRED_CAPSULE a0,a1;
    for(unsigned i=0;i<3;i++){
        if(!isfinite(position[i])||!isfinite(velocity[i]))return CD_PRED_UNKNOWN;
        a0.a[i]=a0.b[i]=position[i];
        a1.a[i]=a1.b[i]=position[i]+velocity[i]*(CD_PRED_HORIZON_MS/1000.);
    }
    a0.radius=a1.radius=radius;
    double age=now-body.now.stamp,best=CD_PRED_HORIZON_MS+1;
    int result=CD_PRED_CLEAR;unsigned budget=CD_PRED_WORK_BUDGET;
    for(unsigned j=0;j<sizeof CdPredBodies/sizeof CdPredBodies[0];j++){
        const CD_PRED_BODY*b=&CdPredBodies[j];CD_PRED_CAPSULE b0,b1;
        Point ba={b->bone,{b->a[0],b->a[1],b->a[2]}},bb={b->bone,{b->b[0],b->b[1],b->b[2]}};
        if(!moving_capsule(&body,ba,bb,b->radius,age,age+CD_PRED_HORIZON_MS,&b0,&b1))return CD_PRED_UNKNOWN;
        double hit;int r=CopydogPredictionSweep(&a0,&a1,&b0,&b1,CD_PRED_HORIZON_MS,&hit,&budget);
        if(r==CD_PRED_UNKNOWN)return r;
        if(r>0&&hit<best){best=hit;result=r;}
    }
    if(result>0)*out=best;
    return result;
}
