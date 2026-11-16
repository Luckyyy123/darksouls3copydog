/* Synthetic game-shaped allocations in this test process; no live game writes. */
#include "../src/prediction.c"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
typedef struct {
    unsigned char actor[0x2100],modules[0x100],behavior[0x40],owner[0x80],clock[0x100],physics[0xa0],
        equipment[0x400],action[0xe0],skeleton[0x60],bones[110*16];
    float pose[110][12];
} Fixture;
static void ptr(void*p,const void*q){uintptr_t v=(uintptr_t)q;memcpy(p,&v,8);}
static void integer(void*p,int v){memcpy(p,&v,4);}
static void phase(Fixture*f,int animation,float v)
{
    ClockRecord c={animation,v-.01f,v,2};memcpy(f->clock+0x20,&c,sizeof c);
    integer(f->clock+0xc0,1);integer(f->clock+0xc4,0);integer(f->action+0xc8,animation);
}
static void init(Fixture*f,int animation,int handle)
{
    memset(f,0,sizeof *f);ptr(f->actor+0x1f90,f->modules);ptr(f->actor+0x1fa0,f->equipment);
    ptr(f->modules+0x28,f->behavior);ptr(f->modules+0x10,f->clock);ptr(f->modules+0x68,f->physics);
    ptr(f->modules+0x80,f->action);ptr(f->behavior+0x28,f->owner);
    PoseHeader h={(uintptr_t)f->skeleton,(uintptr_t)f->pose,110,110,(uintptr_t)f->pose,110,110};
    memcpy(f->owner+0x38,&h,sizeof h);ptr(f->skeleton+0x28,f->bones);integer(f->skeleton+0x30,110);
    for(unsigned i=0;i<110;i++){ptr(f->bones+i*16,CdPredBoneNames[i]);f->pose[i][7]=1;f->pose[i][8]=f->pose[i][9]=f->pose[i][10]=1;}
    integer(f->equipment+0x2b8,1);integer(f->equipment+0x330,1000000);integer(f->actor+8,handle+100);integer(f->actor+0x78,handle);
    phase(f,animation,.3f);
}
static void different_clocks(Fixture*self,Fixture*enemy)
{
    CopydogPredictionResetAll();init(self,10000000,10);init(enemy,20030000,11);
    const CD_PRED_PROFILE*p=profile(1000000,20030000,1,1);assert(p);
    const CD_PRED_SHAPE*s=&CdPredShapes[p->first];
    float xyz[7]={0};
    for(unsigned i=0;i<3;i++)xyz[i+4]=CdPredBodies[0].a[i]-s->a.p[i];
    memcpy(enemy->physics+0x70,xyz,sizeof xyz);
    uint32_t base=UINT32_MAX-15u,now=base+20u;
    phase(self,10000000,.32f);CopydogPredictionObserveFrame(0,(uintptr_t)self->actor,base-2u,1);
    phase(enemy,20030000,.37f);CopydogPredictionObserveFrame(1,(uintptr_t)enemy->actor,base-10u,1);
    phase(enemy,20030000,.38f);CopydogPredictionObserveFrame(1,(uintptr_t)enemy->actor,base,1);
    phase(self,10000000,.33f);CopydogPredictionObserveFrame(0,(uintptr_t)self->actor,base+8u,1);
    CD_NATIVE_FRAME f;assert(CopydogReadNativeFrame((uintptr_t)enemy->actor,&f));
    uint32_t origin=CopydogPredictionFrameOrigin(1,(uintptr_t)enemy->actor,&f,base+12u,now);
    assert(origin==base);
    unsigned budget=32768;CD_PRED_PARRY_RESULT result;
    CD_PRED_PARRY_BAND band={&f,1000000,1,1.,420.,431.,50.,&budget,origin};
    assert(CopydogPredictionParryContact(1,(uintptr_t)enemy->actor,(uintptr_t)self->actor,now,&band,&result)==CD_PRED_CONTACT);
    assert(fabs(result.contact-20.)<.01&&result.unknownReason==CD_PRED_UNK_NONE);
    band.phaseTick=CopydogPredictionFrameOrigin(1,(uintptr_t)enemy->actor,&f,base-6u,now);
    assert(band.phaseTick==base-6u);
    assert(CopydogPredictionParryContact(1,(uintptr_t)enemy->actor,(uintptr_t)self->actor,now,&band,&result)==CD_PRED_CONTACT);
    assert(fabs(result.contact-14.)<.01);
    CD_NATIVE_FRAME different=f;different.duration+=1.f;
    assert(CopydogPredictionFrameOrigin(1,(uintptr_t)enemy->actor,&different,base+12u,now)==base+12u);
    /* A defender can move without changing animation phase. A pose-only
       update must not redate that animation or discard the spatial velocity. */
    float moved=2.f;memcpy(self->physics+0x80,&moved,sizeof moved);
    CopydogPredictionObserveFrame(0,(uintptr_t)self->actor,base+28u,1);
    assert(Tracks[0].motionPaired&&Tracks[0].now.phaseStamp==base+8u&&Tracks[0].now.stamp==base+28u);
    band.phaseTick=base-40u;
    assert(CopydogPredictionParryContact(1,(uintptr_t)enemy->actor,(uintptr_t)self->actor,base+28u,&band,&result)==CD_PRED_UNKNOWN);
    assert(result.unknownReason==CD_PRED_UNK_POSE);
    /* Replacement with the same current phase but different previous phase
       is a new native record, including when the body moved in this tick. */
    float revisedPrevious=.3795f,enemyMoved=.02f;
    memcpy(enemy->clock+0x24,&revisedPrevious,sizeof revisedPrevious);
    memcpy(enemy->physics+0x80,&enemyMoved,sizeof enemyMoved);
    CopydogPredictionObserveFrame(1,(uintptr_t)enemy->actor,base+29u,1);
    assert(CopydogReadNativeFrame((uintptr_t)enemy->actor,&f));
    assert(CopydogPredictionFrameOrigin(1,(uintptr_t)enemy->actor,&f,base+32u,base+32u)==base+29u);
}
static void rotating_attachment(Fixture*enemy)
{
    CopydogPredictionResetAll();init(enemy,20030000,11);
    const double rad=M_PI/180.;
    for(unsigned i=0;i<2;i++){
        double yaw=(i?-179.:179.)*rad,bone=.4+i*.2;
        float y=(float)yaw;memcpy(enemy->physics+0x74,&y,sizeof y);
        float*m=enemy->pose[103];double sign=i?-1.:1.;
        m[0]=.5f;m[6]=(float)(sign*sin(bone*.5));m[7]=(float)(sign*cos(bone*.5));
        phase(enemy,20030000,(float)(.30+i*.01));
        CopydogPredictionObserveFrame(1,(uintptr_t)enemy->actor,1000+10*i,1);
    }
    TurnBasis basis;CD_PRED_TURN_CAPSULE blade={0},marker={0};
    assert(turn_basis(&Tracks[1],&basis)&&fabs(basis.rate-.2*rad)<1e-6);
    Point attachment={103,{1,0,0}};
    assert(bind_turn_point(&Tracks[1],&basis,attachment,&blade.a)&&fabs(blade.a.boneRate-.02)<1e-6);
    blade.b=blade.a;marker.radius=.00001;
    double yaw=-178.*rad,bone=.7,x=.5+cos(bone);
    marker.a.root[0]=cos(yaw)*x;marker.a.root[1]=sin(bone);marker.a.root[2]=-sin(yaw)*x;
    marker.b=marker.a;unsigned budget=32768;double hit=-1;
    assert(CopydogPredictionTurningSweep(&blade,5.,&marker,0.,1.,&hit,&budget)==CD_PRED_CONTACT&&hit==0.);
    Tracks[1].before.yaw=(float)(Tracks[1].now.yaw-M_PI);
    assert(!turn_basis(&Tracks[1],&basis));
}
static void resource_coverage(Fixture*self,Fixture*enemy)
{
    CD_PRED_SHAPE shapes[]={{.begin=420,.end=430},{.begin=400,.end=410},{.begin=410,.end=420}};
    unsigned work=128;int exhausted;
    assert(covered_phase(shapes,3,400,430,&work,&exhausted)==430&&!exhausted);
    shapes[2].begin=411;
    assert(covered_phase(shapes,3,400,430,&work,&exhausted)==410&&!exhausted);
    assert(covered_phase(shapes,3,410.5,430,&work,&exhausted)==410.5&&!exhausted);
    work=1;assert(covered_phase(shapes,3,400,430,&work,&exhausted)==400&&exhausted);
    work=1;assert(covered_phase(shapes,1,400,430,&work,&exhausted)==400&&!work&&!exhausted);

    /* A real eligibility row starts one float ULP before its shape.
       Test through the production query with a distant defender. */
    CopydogPredictionResetAll();init(self,10000000,10);init(enemy,23034340,11);
    integer(enemy->equipment+0x2b8,3);integer(enemy->equipment+0x330,2000000);
    float separation=10.f;memcpy(enemy->physics+0x80,&separation,sizeof separation);
    for(unsigned i=0;i<2;i++){
        phase(self,10000000,.3f+.012f*i);phase(enemy,23034340,1.112f+.012f*i);
        CopydogPredictionObserve(0,(uintptr_t)self->actor,1000+12*i);
        CopydogPredictionObserve(1,(uintptr_t)enemy->actor,1000+12*i);
    }
    CD_NATIVE_FRAME f;assert(CopydogReadNativeFrame((uintptr_t)enemy->actor,&f));
    work=32768;CD_PRED_PARRY_RESULT out;
    CD_PRED_PARRY_BAND band={&f,2000000,3,1.,1133.3333f,1145.,50.,&work,1012};
    assert(CopydogPredictionParryContact(1,(uintptr_t)enemy->actor,(uintptr_t)self->actor,1012,&band,&out)==CD_PRED_UNKNOWN);
    assert(!out.complete&&out.clearThrough==0.&&out.unknownReason==CD_PRED_UNK_SHAPE);
    const CD_PRED_PROFILE*p=profile(2000000,23034340,3,1);assert(p);
    band.begin=CdPredShapes[p->first].begin;
    assert(CopydogPredictionParryContact(1,(uintptr_t)enemy->actor,(uintptr_t)self->actor,1012,&band,&out)==CD_PRED_CLEAR);
    assert(out.complete&&out.clearThrough>20.&&out.unknownReason==CD_PRED_UNK_NONE);

    uintptr_t actor=(uintptr_t)enemy->actor,defender=(uintptr_t)self->actor;
    band.begin=1200.;band.end=1210.;
    assert(CopydogPredictionParryContact(1,actor,defender,1012,&band,&out)==CD_PRED_UNKNOWN);
    assert(out.unknownReason==CD_PRED_UNK_HORIZON);
    /* The long query can see the band, but its clear span cannot be trusted. */
    band.horizon=150.;
    assert(CopydogPredictionParryContact(1,actor,defender,1012,&band,&out)==CD_PRED_UNKNOWN);
    assert(out.unknownReason==CD_PRED_UNK_HORIZON&&!out.complete&&out.clearThrough==0.);
    /* The half-open span collapses when its endpoint is only one ULP away. */
    band.horizon=nextafter(band.begin-1000.*f.current,INFINITY);
    assert(CopydogPredictionParryContact(1,actor,defender,1012,&band,&out)==CD_PRED_UNKNOWN);
    assert(out.unknownReason==CD_PRED_UNK_NARROW_PHASE&&work>0);
    band.horizon=150.;work=2;
    assert(CopydogPredictionParryContact(1,actor,defender,1012,&band,&out)==CD_PRED_UNKNOWN);
    assert(out.unknownReason==CD_PRED_UNK_BUDGET&&work==2);
    band.budget=NULL;
    assert(CopydogPredictionParryContact(1,actor,defender,1012,&band,&out)==CD_PRED_UNKNOWN);
    assert(out.unknownReason==CD_PRED_UNK_OTHER);
    band.budget=&work;work=32768;
    float yaw=.4f;memcpy(enemy->physics+0x74,&yaw,sizeof yaw);
    phase(self,10000000,.324f);phase(enemy,23034340,1.136f);
    CopydogPredictionObserve(0,defender,1024);CopydogPredictionObserve(1,actor,1024);
    assert(CopydogReadNativeFrame(actor,&f));band.phaseTick=1024;
    assert(CopydogPredictionParryContact(1,actor,defender,1024,&band,&out)==CD_PRED_UNKNOWN);
    /* Even allowing every rotation this distant pair stays apart. The band
       still starts beyond the range in which CLEAR can be reported. */
    assert(out.turning&&out.unknownReason==CD_PRED_UNK_HORIZON&&!out.complete);
}
enum { moveAnim=10000000,attackAnim=20030000,otherAttackAnim=20030001 };
static void publish_pair(Fixture*f,ClockRecord older,ClockRecord newer)
{
    memcpy(f->clock+CD_PRED_CLOCK_RECORDS+9*sizeof older,&older,sizeof older);
    memcpy(f->clock+CD_PRED_CLOCK_RECORDS,&newer,sizeof newer);
    integer(f->clock+CD_PRED_CLOCK_INDICES,1);integer(f->clock+CD_PRED_CLOCK_INDICES+4,9);
}
static void published_attack_clock(Fixture*enemy)
{
    init(enemy,moveAnim,11);uintptr_t actor=(uintptr_t)enemy->actor;
    ClockRecord move={moveAnim,.4f,.41f,2},attack={attackAnim,.3f,.312f,2};
    ClockRecord other={otherAttackAnim,.05f,.06f,2},newer=attack;
    CD_NATIVE_FRAME frame;int retained=7;
    publish_pair(enemy,move,attack);
    assert(CopydogReadLatestNativeFrame(actor,&frame,&retained)&&frame.index==9&&frame.animation==moveAnim&&!retained);
    assert(CopydogReadPublishedNativeFrame(actor,&frame,&retained)&&frame.index==0&&frame.animation==attackAnim&&!retained);
    assert(CopydogReadAttackNativeFrame(actor,attackAnim,&frame,NULL)&&frame.index==0&&frame.current==attack.current);
    assert(!CopydogReadAttackNativeFrame(actor,30000,&frame,&retained)&&frame.index==-1&&!retained);
    newer.current=.325f;publish_pair(enemy,attack,newer);
    assert(CopydogReadAttackNativeFrame(actor,attackAnim,&frame,NULL)&&frame.index==0&&frame.current==newer.current);
    publish_pair(enemy,attack,other);
    assert(CopydogReadPublishedNativeFrame(actor,&frame,NULL)&&frame.animation==otherAttackAnim);
    assert(!CopydogReadAttackNativeFrame(actor,attackAnim,&frame,&retained)&&frame.animation==-1&&!retained);
    publish_pair(enemy,attack,move);
    assert(!CopydogReadAttackNativeFrame(actor,attackAnim,&frame,NULL));
    newer.current=NAN;publish_pair(enemy,attack,newer);
    assert(!CopydogReadPublishedNativeFrame(actor,&frame,&retained)&&frame.index==-1&&!retained);
    publish_pair(enemy,move,attack);
    integer(enemy->clock+CD_PRED_CLOCK_INDICES+4,1);
    assert(!CopydogReadPublishedNativeFrame(actor,&frame,NULL));
    assert(CopydogReadAttackNativeFrame(actor,attackAnim,&frame,&retained)&&retained==1&&frame.index==0);
    assert(!CopydogReadAttackNativeFrame(actor,otherAttackAnim,&frame,&retained)&&!retained&&frame.index==-1);
    integer(enemy->clock+CD_PRED_CLOCK_INDICES+4,CD_PRED_CLOCK_COUNT);
    assert(!CopydogReadPublishedNativeFrame(actor,&frame,&retained)&&!retained&&frame.index==-1);
}
static void hidden_attack_poses(Fixture*self,Fixture*enemy)
{
    CopydogPredictionResetAll();init(self,moveAnim,10);init(enemy,moveAnim,11);
    uintptr_t actor=(uintptr_t)enemy->actor,defender=(uintptr_t)self->actor;
    const CD_PRED_PROFILE*p=profile(1000000,attackAnim,1,1);assert(p);
    assert(CopydogPredictionHasAttackShape(1000005,attackAnim,1));
    assert(!CopydogPredictionHasAttackShape(1000000,30000,1)&&!CopydogPredictionHasAttackShape(1000000,attackAnim,0));
    float xyz[7]={0};const CD_PRED_SHAPE*s=&CdPredShapes[p->first];
    for(unsigned i=0;i<3;i++)xyz[i+4]=CdPredBodies[0].a[i]-s->a.p[i];
    memcpy(enemy->physics+0x70,xyz,sizeof xyz);
    ClockRecord move={moveAnim,.4f,.41f,2},attack={attackAnim,.288f,.3f,2};
    CD_NATIVE_FRAME frame;CD_PRED_PARRY_RESULT result;unsigned budget=32768;
    CD_PRED_PARRY_BAND band={&frame,1000000,1,1.,420.,431.,150.,&budget,1000};
    Track*t=&AttackTracks[1];
    for(unsigned i=0;i<2;i++){
        uint32_t now=1000+12*i;
        phase(self,moveAnim,.3f+.012f*i);CopydogPredictionObserveFrame(0,defender,now,1);
        if(i){
            attack.previous=.3f;attack.current=.312f;move.previous=.41f;move.current=.422f;
            enemy->pose[s->a.bone][6]=sinf(.005f);enemy->pose[s->a.bone][7]=cosf(.005f);
        }
        publish_pair(enemy,move,attack);
        CopydogPredictionObserveFrame(1,actor,now,1);
        CopydogPredictionObserveAttackFrame(1,actor,now,1,attackAnim,7);
        assert(CopydogReadAttackNativeFrame(actor,attackAnim,&frame,NULL));
        assert(t->valid&&t->now.clock.animation==attackAnim&&Tracks[1].now.clock.animation==moveAnim);
        band.phaseTick=now;
        if(!i){
            assert(!CopydogPredictionAttackMotionReady(1,actor,&frame,1000000,1,now));
            assert(CopydogPredictionAttackParryContact(1,actor,defender,now,&band,&result)==CD_PRED_UNKNOWN);
        }
    }
    assert(t->paired&&CopydogPredictionAttackFrameOrigin(1,actor,&frame,1014,1014)==1012);
    assert(CopydogPredictionFrameOrigin(1,actor,&frame,1014,1014)==1014);
    assert(CopydogPredictionParryContact(1,actor,defender,1012,&band,&result)==CD_PRED_UNKNOWN);
    assert(CopydogPredictionAttackMotionReady(1,actor,&frame,1000000,1,1012));
    assert(CopydogPredictionAttackParryContact(1,actor,defender,1012,&band,&result)==CD_PRED_CONTACT);
    assert(result.turning&&fabs(result.contact-108.)<.01&&result.unknownReason==CD_PRED_UNK_NONE);
    integer(enemy->actor+8,1234);
    assert(!CopydogPredictionAttackMotionReady(1,actor,&frame,1000000,1,1012));
    assert(CopydogPredictionAttackParryContact(1,actor,defender,1012,&band,&result)==CD_PRED_UNKNOWN);
    integer(enemy->actor+8,111);
    integer(enemy->equipment+0x330,2000000);assert(!CopydogPredictionAttackMotionReady(1,actor,&frame,1000000,1,1012));
    integer(enemy->equipment+0x330,1000000);
    ptr(enemy->owner+CD_PRED_POSE_HEADER,NULL);assert(!CopydogPredictionAttackMotionReady(1,actor,&frame,1000000,1,1012));
    ptr(enemy->owner+CD_PRED_POSE_HEADER,enemy->skeleton);
    assert(!CopydogPredictionAttackMotionReady(1,actor,&frame,1000000,1,1045));

    uint64_t samples=atomic_load(&CopydogPredictionSamples);
    integer(enemy->clock+CD_PRED_CLOCK_INDICES+4,1);
    enemy->pose[103][0]+=.1f;
    CopydogPredictionObserveAttackFrame(1,actor,1015,1,attackAnim,7);
    assert(t->paired&&t->now.stamp==1012&&t->now.phaseStamp==1012&&atomic_load(&CopydogPredictionSamples)==samples);
    CopydogPredictionObserveAttackFrame(1,actor,1016,1,attackAnim,8);
    assert(!t->history&&!t->valid&&CopydogPredictionAttackFrameOrigin(1,actor,&frame,1016,1016)==1016);
    assert(!CopydogPredictionAttackMotionReady(1,actor,&frame,1000000,1,1016));

    publish_pair(enemy,move,attack);CopydogPredictionObserveAttackFrame(1,actor,1017,1,attackAnim,8);
    assert(t->valid&&!t->paired);
    attack.previous=.312f;attack.current=.324f;publish_pair(enemy,move,attack);
    CopydogPredictionObserveAttackFrame(1,actor,1029,1,attackAnim,8);assert(t->paired);
    assert(CopydogReadAttackNativeFrame(actor,attackAnim,&frame,NULL));band.phaseTick=1029;
    publish_pair(enemy,attack,move);
    assert(CopydogPredictionAttackParryContact(1,actor,defender,1029,&band,&result)==CD_PRED_UNKNOWN);
    CopydogPredictionObserveAttackFrame(1,actor,1030,1,attackAnim,8);
    assert(!t->history&&!t->valid);
    publish_pair(enemy,move,attack);CopydogPredictionObserveAttackFrame(1,actor,1031,1,attackAnim,8);
    assert(t->valid&&!t->paired);
    integer(enemy->clock+CD_PRED_CLOCK_INDICES+4,1);
    CopydogPredictionObserveAttackFrame(1,actor,1064,1,attackAnim,8);
    assert(!t->valid&&!t->history);
    publish_pair(enemy,move,attack);CopydogPredictionObserveAttackFrame(1,actor,1070,1,attackAnim,8);
    attack.previous=.324f;attack.current=.336f;publish_pair(enemy,move,attack);
    CopydogPredictionObserveAttackFrame(1,actor,1082,1,attackAnim,8);assert(t->paired);
    attack.previous=.21f;attack.current=.22f;publish_pair(enemy,move,attack);
    CopydogPredictionObserveAttackFrame(1,actor,1094,1,attackAnim,8);assert(t->valid&&!t->paired);
    int newHandle=23;integer(enemy->actor+0x78,newHandle);
    attack.previous=.22f;attack.current=.232f;publish_pair(enemy,move,attack);
    CopydogPredictionObserveAttackFrame(1,actor,1106,1,attackAnim,8);
    assert(t->valid&&!t->paired&&t->now.handle==newHandle);
    CopydogPredictionObserveAttackFrame(1,0,1107,1,-1,8);assert(!t->history&&!t->episodeActor);
    integer(enemy->clock+CD_PRED_CLOCK_INDICES+4,1);
    CopydogPredictionObserveAttackFrame(1,actor,1108,1,attackAnim,9);assert(!t->valid);
    publish_pair(enemy,move,attack);CopydogPredictionObserveAttackFrame(1,actor,1109,1,attackAnim,9);assert(t->valid);
    CopydogPredictionResetAll();assert(!t->history&&!t->episodeActor&&!Tracks[0].history);
}
static void stationary_and_active_shapes(Fixture*self,Fixture*enemy)
{
    CopydogPredictionResetAll();init(self,moveAnim,10);init(enemy,moveAnim,11);
    uintptr_t actor=(uintptr_t)enemy->actor,defender=(uintptr_t)self->actor;
    const CD_PRED_PROFILE*p=profile(1000000,attackAnim,1,1);assert(p&&p->count==1);
    const CD_PRED_SHAPE*s=&CdPredShapes[p->first];
    float transform[7]={0};
    for(unsigned k=0;k<3;k++)transform[k+4]=CdPredBodies[0].a[k]-s->a.p[k];
    CD_NATIVE_FRAME frame;CD_PRED_PARRY_RESULT result;unsigned budget=CD_PRED_WORK_BUDGET;
    CD_PRED_PARRY_BAND band={&frame,1000000,1,1.,420.,431.,150.,&budget,0};
    const float phases[]={.300f,.312f,.324f,.336f};
    for(unsigned i=0;i<sizeof phases/sizeof phases[0];i++){
        uint32_t now=1000+12*i;band.phaseTick=now;
        phase(self,moveAnim,phases[i]);CopydogPredictionObserveFrame(0,defender,now,1);
        phase(enemy,attackAnim,phases[i]);integer(enemy->action+0xc8,moveAnim);
        transform[4]+=.001f;memcpy(enemy->physics+0x70,transform,sizeof transform);
        /* An unrelated limb moves while the blade only translates with root. */
        enemy->pose[10][0]+=.002f;
        CopydogPredictionObserveAttackFrame(1,actor,now,1,attackAnim,40);
        assert(CopydogReadAttackNativeFrame(actor,attackAnim,&frame,NULL));
        if(1000.*frame.current<s->begin){
            assert(!CopydogPredictionAttackMotionReady(1,actor,&frame,1000000,1,now));
            assert(CopydogPredictionAttackParryContact(1,actor,defender,now,&band,&result)==CD_PRED_UNKNOWN);
            assert(result.unknownReason==(i?CD_PRED_UNK_SHAPE:CD_PRED_UNK_POSE));
        }else{
            assert(1000.*frame.current<s->end);
            assert(CopydogPredictionAttackMotionReady(1,actor,&frame,1000000,1,now));
            assert(CopydogPredictionAttackParryContact(1,actor,defender,now,&band,&result)==CD_PRED_CONTACT);
        }
    }
}
int main(void)
{
    Fixture *self=calloc(1,sizeof *self),*enemy=calloc(1,sizeof *enemy);assert(self&&enemy);
    init(self,10000000,10);init(enemy,20030000,11);
    ClockRecord batch[]={{20030000,.18f,.2f,2},{20030000,.2f,.22f,2},{99000,.01f,.02f,1}};
    memcpy(enemy->clock+0x20+9*sizeof(ClockRecord),&batch[0],sizeof(ClockRecord));
    memcpy(enemy->clock+0x20,batch+1,2*sizeof(ClockRecord));
    integer(enemy->clock+0xc4,9);integer(enemy->clock+0xc0,2);
    CD_NATIVE_FRAME f;
    assert(CopydogReadNativeFrame((uintptr_t)enemy->actor,&f)&&f.index==0&&f.animation==20030000);
    integer(enemy->action+0xc8,-1);
    assert(CopydogReadNativeFrame((uintptr_t)enemy->actor,&f)&&f.index==1&&f.animation==99000);
    integer(enemy->clock+0xc4,2);
    assert(!CopydogReadNativeFrame((uintptr_t)enemy->actor,&f)&&f.index==-1);
    phase(enemy,20030000,.3f);
    const CD_PRED_PROFILE*p=profile(1000000,20030000,1,1);assert(p);
    const CD_PRED_SHAPE*s=&CdPredShapes[p->first];
    float xyz[7]={0};
    for(unsigned i=0;i<3;i++)xyz[i+4]=CdPredBodies[0].a[i]-s->a.p[i];
    memcpy(enemy->physics+0x70,xyz,sizeof xyz);
    CopydogPredictionObserve(0,(uintptr_t)self->actor,1000);CopydogPredictionObserve(1,(uintptr_t)enemy->actor,1000);
    double hit;int animation;
    assert(CopydogPredictionQuery(1,(uintptr_t)enemy->actor,(uintptr_t)self->actor,1000,&hit,&animation)==CD_PRED_UNKNOWN);
    integer(enemy->clock+0xc4,1);CopydogPredictionObserve(1,(uintptr_t)enemy->actor,1004);
    assert(!Tracks[1].valid&&Tracks[1].history);
    phase(self,10000000,.312f);phase(enemy,20030000,.312f);
    CopydogPredictionObserve(0,(uintptr_t)self->actor,1012);CopydogPredictionObserve(1,(uintptr_t)enemy->actor,1012);
    assert(CopydogPredictionQuery(1,(uintptr_t)enemy->actor,(uintptr_t)self->actor,1012,&hit,&animation)>0);
    assert(hit>=0&&hit<=50&&animation==20030000);
    float bolt[3]={-2,CdPredBodies[0].a[1],CdPredBodies[0].a[2]},velocity[3]={80,0,0};
    assert(CopydogPredictionBullet((uintptr_t)self->actor,bolt,velocity,.02f,1012,&hit)>0&&hit>=0&&hit<=50);
    assert(CopydogPredictionBullet((uintptr_t)self->actor,bolt,velocity,-1,1012,&hit)==CD_PRED_UNKNOWN&&hit==-1);
    CopydogPredictionConsume(1);
    assert(CopydogPredictionQuery(1,(uintptr_t)enemy->actor,(uintptr_t)self->actor,1012,&hit,&animation)==CD_PRED_CLEAR);
    assert(CopydogPredictionQuery(1,(uintptr_t)enemy->actor,(uintptr_t)self->actor,1100,&hit,&animation)==CD_PRED_UNKNOWN);
    assert(CopydogPredictionQuery(1,(uintptr_t)self->actor,(uintptr_t)enemy->actor,1012,&hit,&animation)==CD_PRED_UNKNOWN);
    phase(enemy,20030000,.1f);CopydogPredictionObserve(1,(uintptr_t)enemy->actor,1024);
    assert(CopydogPredictionQuery(1,(uintptr_t)enemy->actor,(uintptr_t)self->actor,1024,&hit,&animation)==CD_PRED_UNKNOWN);
    integer(enemy->clock+0xc4,10);CopydogPredictionObserve(1,(uintptr_t)enemy->actor,1036);
    assert(!Tracks[1].valid);
    different_clocks(self,enemy);rotating_attachment(enemy);resource_coverage(self,enemy);
    published_attack_clock(enemy);hidden_attack_poses(self,enemy);stationary_and_active_shapes(self,enemy);
    free(self);free(enemy);puts("PASS runtime prediction: native pose, skewed clocks, moving defender, rotating attachment, mixed clips, hidden attack sweep, cancellation and reset");
    return 0;
}
