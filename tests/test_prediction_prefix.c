/* Reuse the synthetic actor, native clock and live resource rows exercised by
   the runtime suite. Both observed entities exist only in this test process. */
#define main prediction_runtime_suite
#include "test_prediction_runtime.c"
#undef main
#include "../src/dynamic_parry.h"

enum Layout { ROOT_TURN, FAR_SPIN, NEAR_SPIN, CHANGED_AXIS, SPIN_AT_FIRST_TIME, FAR_STILL, ALL_DISTANT };

static void capture_poses(Fixture*self,Fixture*enemy,enum Layout layout)
{
    CopydogPredictionResetAll();
    init(self,moveAnim,10);
    init(enemy,attackAnim,11);
    for(unsigned i=0;i<2;i++){
        if(layout==ROOT_TURN){
            float yaw=i?0.f:(float)(-12.*(M_PI/2.)/225.);
            memcpy(enemy->physics+0x74,&yaw,sizeof yaw);
        }else{
            for(unsigned j=0;j<sizeof CdPredBodies/sizeof CdPredBodies[0];j++)
                self->pose[CdPredBodies[j].bone][1]=3.f;
            /* The first body rotates about its long axis. A second, distinct
               body slides into the blade during the band under test. */
            float*spin=self->pose[CdPredBodies[0].bone];
            spin[0]=layout==NEAR_SPIN||layout==SPIN_AT_FIRST_TIME?.55f:0.f;
            spin[1]=layout==NEAR_SPIN||layout==SPIN_AT_FIRST_TIME?.38f:1.8f;
            if(layout==CHANGED_AXIS){spin[0]=.637f;spin[1]=.16f;}
            double angle=!i&&layout!=FAR_STILL?-.6:0.;
            spin[5]=(float)sin(angle*.5);spin[7]=(float)cos(angle*.5);
            float*moving=self->pose[CdPredBodies[1].bone];
            moving[0]=(layout==SPIN_AT_FIRST_TIME?1.2f:1.5f)+(i?0.f:.048f);
            moving[1]=layout==ALL_DISTANT?3.f:0.f;
        }
        phase(self,moveAnim,.3f+.012f*i);
        phase(enemy,attackAnim,.188f+.012f*i);
        CopydogPredictionObserveFrame(0,(uintptr_t)self->actor,1000+12*i,1);
        CopydogPredictionObserveFrame(1,(uintptr_t)enemy->actor,1000+12*i,1);
    }
}

static int band_result(Fixture*self,Fixture*enemy,double begin,double end,
                       CD_PRED_PARRY_RESULT*out,unsigned work)
{
    CD_NATIVE_FRAME frame;
    assert(CopydogReadNativeFrame((uintptr_t)enemy->actor,&frame));
    CD_PRED_PARRY_BAND band={&frame,1000000,1,1.,begin,end,250.,&work,1012};
    return CopydogPredictionParryContact(1,(uintptr_t)enemy->actor,(uintptr_t)self->actor,1012,&band,out);
}

static void prefix_and_independent_limbs(void)
{
    Fixture self,enemy;
    CD_PRED_PARRY_RESULT out;
    capture_poses(&self,&enemy,ROOT_TURN);
    /* At the current phase 200ms the shape is active from +220 to +231.
       The body's first overlap precedes the root turn's 225ms trust cutoff. */
    assert(band_result(&self,&enemy,420.,431.,&out,32768)==CD_PRED_CONTACT);
    assert(out.turning&&fabs(out.contact-220.)<.02&&!out.complete);
    double forecast=out.contact,low,high,preferred;
    assert(band_result(&self,&enemy,420.,423.,&out,32768)==CD_PRED_CONTACT);
    assert(fabs(out.contact-forecast)<1.e-6);
    /* The captured iron shield's cold activation bounds at a 12ms cadence. */
    assert(CopydogDynamicParryWindow(forecast-6.,forecast+6.,166.6667+24.,333.3333,
           0.,0.,&low,&high,&preferred));
    assert(CopydogParryFrameChoice(low,high,12.)==CD_FRAME_PRESS);
    assert(band_result(&self,&enemy,427.,431.,&out,32768)==CD_PRED_UNKNOWN);
    assert(out.unknownReason==CD_PRED_UNK_TURN_LIMIT&&out.contact<0.&&!out.complete);

    capture_poses(&self,&enemy,FAR_STILL);
    assert(band_result(&self,&enemy,420.,431.,&out,32768)==CD_PRED_CONTACT);
    forecast=out.contact;assert(forecast>221.&&forecast<231.);
    capture_poses(&self,&enemy,FAR_SPIN);
    assert(band_result(&self,&enemy,420.,431.,&out,32768)==CD_PRED_CONTACT);
    assert(out.turning&&fabs(out.contact-forecast)<1.e-5);

    /* Now the unknown leading limb can reach the blade before the later
       witnessed body hit. A valid witness on that body alone is insufficient. */
    capture_poses(&self,&enemy,NEAR_SPIN);
    assert(band_result(&self,&enemy,420.,431.,&out,32768)==CD_PRED_UNKNOWN);
    assert(out.unknownReason==CD_PRED_UNK_TURN_LIMIT&&out.contact<0.&&!out.complete);
    /* The observed Y-axis spin moves these endpoints by at most 5.1mm.
       Beyond its trusted span, a different axis can turn the 64mm arm
       towards the weapon. A box restricted to the last axis is insufficient. */
    capture_poses(&self,&enemy,CHANGED_AXIS);
    assert(band_result(&self,&enemy,420.,431.,&out,32768)==CD_PRED_UNKNOWN);
    assert(out.unknownReason==CD_PRED_UNK_TURN_LIMIT&&out.contact<0.);
    capture_poses(&self,&enemy,SPIN_AT_FIRST_TIME);
    assert(band_result(&self,&enemy,420.,431.,&out,32768)==CD_PRED_CONTACT);
    assert(fabs(out.contact-220.)<.02);

    capture_poses(&self,&enemy,ALL_DISTANT);
    assert(band_result(&self,&enemy,420.,431.,&out,32768)==CD_PRED_UNKNOWN);
    assert(out.unknownReason==CD_PRED_UNK_HORIZON&&!out.complete&&out.clearThrough==0.);
    assert(band_result(&self,&enemy,420.,431.,&out,2)==CD_PRED_UNKNOWN);
    assert(out.unknownReason==CD_PRED_UNK_BUDGET&&out.contact<0.);
}

/* The returned lower time may precede actual overlap at a subdivision leaf.
   Ordering an unresolved suffix must use the overlapping sample. */
static void witness_times(void)
{
    CD_PRED_TURN_POINT tip={.rootVelocity={.001,0,0}};
    CD_PRED_TURN_POINT target={.root={.020013,0,0}};
    CD_PRED_TURN_CAPSULE a={.a=tip,.b=tip,.radius=.01},b={.a=target,.b=target,.radius=.01};
    double lower=-1.,witness=-1.;unsigned work=32768;
    assert(CopydogPredictionTurningSweepWitness(&a,0.,&b,0.,.04,&lower,&work,&witness)==CD_PRED_CONTACT);
    assert(lower==0.&&witness>=.013-1.e-7&&witness<=.04&&witness>lower);
    target.root[0]=0.;b.a=b.b=target;work=32768;
    assert(CopydogPredictionTurningSweepWitness(&a,0.,&b,0.,.04,&lower,&work,&witness)==CD_PRED_CONTACT);
    assert(lower==0.&&witness==0.);
}

static void arbitrary_axis_envelope(void)
{
    CD_PRED_TURN_POINT p={
        .root={.3,-.2,1.},.rootVelocity={-.001,.007,.002},
        .pivot={-.1,.2,.25},.pivotVelocity={.01,-.003,.002},
        .arm={.25,-.35,.11},.axis={.6,0.,.8},.yawRate=.019,.boneRate=-.033
    };
    CD_PRED_TURN_CAPSULE c={.a=p,.b=p,.radius=.12};
    c.b.arm[0]=-.32;c.b.pivot[2]=-.11;
    for(unsigned changed=0;changed<2;changed++){
        double axis[3]={.6,0.,.8};
        if(changed){axis[0]=0.;axis[1]=1.;axis[2]=0.;}
        TurnBox box=turn_box(&c,29.,241.);
        for(unsigned i=0;i<=32;i++)for(unsigned j=0;j<2;j++){
            double t=29.+212.*i/32.,u=j?.83:-1.24,v=j?-1.17:.71;
            const CD_PRED_TURN_POINT*m=j?&c.b:&c.a;
            /* The second pass uses a bone axis different from the observed
               axis. Root yaw also ranges independently of the last yaw rate. */
            double d=0.,local[3],cross[3];
            for(unsigned k=0;k<3;k++)d+=axis[k]*m->arm[k];
            cross3(axis,m->arm,cross);
            for(unsigned k=0;k<3;k++)local[k]=m->pivot[k]+m->pivotVelocity[k]*t+
                m->arm[k]*cos(u)+cross[k]*sin(u)+axis[k]*d*(1.-cos(u));
            double pos[3]={cos(v)*local[0]+sin(v)*local[2],local[1],-sin(v)*local[0]+cos(v)*local[2]};
            for(unsigned k=0;k<3;k++){
                pos[k]+=m->root[k]+m->rootVelocity[k]*t;
                assert(box.low[k]<=pos[k]-c.radius+1.e-9&&box.high[k]>=pos[k]+c.radius-1.e-9);
            }
        }
    }
}

int main(void)
{
    prefix_and_independent_limbs();
    witness_times();
    arbitrary_axis_envelope();
    puts("PASS trusted rotation prefixes, independent limbs, first-contact witnesses and arbitrary-axis bounds");
    return 0;
}

