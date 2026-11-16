#include "../src/projectile_motion.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int closef_(float a,float b){return fabs((double)a-b)<1e-5;}
static void no_speed(const COPYDOG_PLAYER_MOTION *m,uintptr_t who,const float *p,uint32_t t)
{
    float v[3]={99,88,77};
    CHECK(!CopydogMotionVelocity(m,who,p,t,v));
    CHECK(v[0]==0&&v[1]==0&&v[2]==0);
}
static void sample(COPYDOG_PLAYER_MOTION *m,uintptr_t who,float x,float y,float z,uint32_t t)
{float p[]={x,y,z};CopydogMotionSample(m,who,p,t);}

int main(void)
{
    const float origin[3]={0};
    COPYDOG_PLAYER_MOTION m={0};
    float vel[3];
    no_speed(&m,3,origin,10);
    sample(&m,3,0,0,0,100);
    sample(&m,3,0,0,0,107);CHECK(m.count==1);
    sample(&m,3,.08f,.016f,-.024f,108);
    no_speed(&m,3,(float[]){.08f,.016f,-.024f},108);
    sample(&m,3,.16f,.032f,-.048f,116);
    CHECK(CopydogMotionVelocity(&m,3,(float[]){.16f,.032f,-.048f},116,vel));
    CHECK(closef_(vel[0],10)&&closef_(vel[1],2)&&closef_(vel[2],-3));
    CHECK(CopydogMotionVelocity(&m,3,(float[]){.64f,.128f,-.192f},164,vel));
    no_speed(&m,3,(float[]){.65f,.13f,-.195f},165);
    no_speed(&m,3,(float[]){30,0,0},116);
    no_speed(&m,3,(float[]){NAN,0,0},116);
    no_speed(&m,4,(float[]){.16f,.032f,-.048f},116);
    sample(&m,3,.82f,0,0,183);CHECK(m.count==1);
    no_speed(&m,3,(float[]){.82f,0,0},183);
    sample(&m,3,.98f,0,0,199);
    CHECK(CopydogMotionVelocity(&m,3,(float[]){.98f,0,0},199,vel)&&closef_(vel[0],10));
    sample(&m,3,20,0,0,215);CHECK(m.count==1);
    no_speed(&m,3,(float[]){20,0,0},215);
    sample(&m,4,20,0,0,231);CHECK(m.self==4&&m.count==1);
    sample(&m,4,20,0,0,247);
    CHECK(CopydogMotionVelocity(&m,4,(float[]){20,0,0},247,vel)&&vel[0]==0);
    sample(&m,4,40,INFINITY,0,263);CHECK(!m.self&&!m.count);
    sample(&m,4,.08f,0,0,UINT32_MAX-15);sample(&m,4,.16f,0,0,UINT32_MAX-7);
    sample(&m,4,.24f,0,0,0);
    CHECK(CopydogMotionVelocity(&m,4,(float[]){.24f,0,0},0,vel)&&closef_(vel[0],10));
    CopydogMotionSample(&m,4,NULL,8);CHECK(!m.self&&!m.count);
    sample(&m,5,0,0,0,20);sample(&m,5,.405f,0,0,36);
    no_speed(&m,5,(float[]){.405f,0,0},36);
    sample(&m,0,0,0,0,40);CHECK(!m.self&&!m.count);
    for(unsigned j=0;j<20;j++)sample(&m,7,.04f*j,0,0,100+j*8);
    CHECK(m.count==8);
    CHECK(CopydogMotionVelocity(&m,7,(float[]){.76f,0,0},252,vel)&&closef_(vel[0],5));

    const float p[]={-4,1,0},along[]={20,0,0};
    float yaw=CopydogProjectileEscapeYaw(p,along,(float[]){0,0,.3f},origin,.2,0);
    CHECK(isfinite(yaw)&&-cos(yaw)>.99);
    yaw=CopydogProjectileEscapeYaw(p,along,(float[]){0,0,-.3f},origin,.2,0);
    CHECK(isfinite(yaw)&&-cos(yaw)<-.99);
    yaw=CopydogProjectileEscapeYaw(p,along,origin,(float[]){0,0,-2},.2,0);
    CHECK(isfinite(yaw)&&-cos(yaw)<-.99);
    yaw=CopydogProjectileEscapeYaw((float[]){0,5,0},(float[]){0,-20,0},origin,origin,.2,.75f);
    CHECK(isfinite(yaw)&&closef_(yaw,.75f));
    yaw=CopydogProjectileEscapeYaw((float[]){0,5,0},(float[]){0,-20,0},origin,origin,.2,NAN);
    CHECK(isfinite(yaw)&&yaw==0);
    yaw=CopydogProjectileEscapeYaw((float[]){0,5,0},(float[]){0,-20,0},
        (float[]){.2f,0,0},origin,.2,0);
    CHECK(isfinite(yaw)&&-sin(yaw)>.99);
    yaw=CopydogProjectileEscapeYaw((float[]){0,5,0},(float[]){0,-20,0},
        origin,(float[]){-2,0,0},.2,0);
    CHECK(isfinite(yaw)&&-sin(yaw)<-.99);
    printf("PASS %u projectile motion and direction checks\n",checks);
    return 0;
}
