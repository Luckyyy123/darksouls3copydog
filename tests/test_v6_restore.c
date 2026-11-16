#ifndef GetAsyncKeyState
#include <windows.h>
static SHORT WINAPI V6TestAsyncKey(int key){(void)key;return 0;}
#define GetAsyncKeyState V6TestAsyncKey
#endif
static int TestSendInputAllowed;
static UINT WINAPI TestSendInput(UINT count,LPINPUT inputs,int bytes)
{(void)inputs;(void)bytes;return TestSendInputAllowed?count:0;}
#define SendInput TestSendInput
#define COPYDOG_OFFLINE_TEST
#define COPYDOG_DIAGNOSTIC_DEFAULT 0
#include "../src/copydog.h"
static char*opponent;
static char*MockNetChar(void*p){(void)p;return opponent;}
#undef net2char
#define net2char(p) MockNetChar((void*)(p))
#include "../src/copydog.c"
#define ReadyToRoll DataReadyToRoll
#define ReadyToAttack DataReadyToAttack
#include "../src/gamedata.c"
#undef ReadyToRoll
#undef ReadyToAttack
static int ready=1;
int ReadyToRoll(void){return ready;}
int ReadyToAttack(void){return ready;}
volatile LONG CopydogEnabled=1,exitThread=0;
static int calls,lastAction;
void*doaction(void*a,void*b){(void)a;lastAction=*(int*)b;++calls;return 0;}
static unsigned checks;
#define CHECK(x) do{if(!(x)){printf("FAIL line%d %s\n",__LINE__,#x);return 1;}++checks;}while(0)
static void pointer(void*p,void*v){memcpy(p,&v,sizeof v);}
static void integer(void*p,int v){memcpy(p,&v,4);}
static void real(void*p,float v){memcpy(p,&v,4);}
int main(void){
 atomic_store(&AutoRollEnabled,1);
 void*globals=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(globals==(void*)0x144770000);
 char world[0x100]={0},slots[6*0x38]={0},me[0x2100]={0},you[0x2100]={0},modules[0x100]={0},ymod[0x100]={0};
 char physics[0x90]={0},yphysics[0x90]={0},state[0x229]={0},ystate[0x229]={0},anim[0xd0]={0},yanim[0xd0]={0};
 char a0[0xa00]={0},a1[0x30]={0},a2[0xc0]={0},action[0x10]={0};
 pointer(addr1,world);pointer(world+0x80,me);pointer(world+0x40,slots);pointer(slots+0x38,you);opponent=you;
 pointer(me+XA,modules);pointer(you+XA,ymod);pointer(modules+0x68,physics);pointer(ymod+0x68,yphysics);
 pointer(modules,state);pointer(ymod,ystate);pointer(modules+0x80,anim);pointer(ymod+0x80,yanim);
 pointer(modules+0x28,a0);pointer(a0+0x10,a1);pointer(a1+0x28,a2);pointer(a2+0xb8,action);
 real(yphysics+0x88,1.f);real(yphysics+0x74,-(float)M_PI);
 real(state+0x224,1);real(ystate+0x224,.1f);
 CheckAnimation(1);CHECK(calls==1&&lastAction==ActionBackstab);
 for(unsigned i=0;i<5;++i){int prior=calls;Sleep(35);CheckAnimation(1);CHECK(calls==prior+1&&lastAction==ActionBackstab);}
 /* No rearm or geometry change is required; the existing32ms throttle remains. */
 int prior=calls;CheckAnimation(1);CHECK(calls==prior);
 real(yphysics+0x88,3);CheckAnimation(1);Sleep(35);real(yphysics+0x88,1);CheckAnimation(1);CHECK(calls==prior+1&&lastAction==ActionBackstab);
 /* Separate remote slots can retry while the original geometry stays eligible. */
 char second[0x2100];memcpy(second,you,sizeof second);pointer(slots+0x70,second);
 prior=calls;Sleep(35);CheckAnimation(2);CHECK(calls==prior+1&&lastAction==ActionBackstab);
 Sleep(35);CheckAnimation(1);CHECK(calls==prior+2&&lastAction==ActionBackstab);pointer(slots+0x70,0);
 /* Missing native queue must not latch out the next valid attempt. */
 pointer(a2+0xb8,0);prior=calls;Sleep(35);CheckAnimation(1);CHECK(calls==prior);
 pointer(a2+0xb8,action);CheckAnimation(1);CHECK(calls==prior+1&&lastAction==ActionBackstab);
 /* Exercise the actual CheckAnimation path for every observed drinking phase. */
 real(yphysics+0x88,3);CheckAnimation(1);real(yphysics+0x88,1);
 const int drinks[]={50110,50111,50112,50150,50151,50152};
 for(unsigned k=0;k<sizeof drinks/sizeof drinks[0];++k){
  int baseline=calls;
  integer(yanim+0xc8,drinks[k]);Sleep(35);CheckAnimation(1);
  CHECK(calls==baseline+1&&lastAction==ActionBackstab);
  for(unsigned repeat=0;repeat<10;++repeat)CheckAnimation(1);
  CHECK(calls==baseline+1);
  /* Same phase outside rear range remains ineligible. */
  real(yphysics+0x88,3);Sleep(35);CheckAnimation(1);CHECK(calls==baseline+1);
  real(yphysics+0x88,1);
 }
 const int rejected[]={-1,50000,50109,50113,50149,50153,50200,79999,90000};
 for(unsigned k=0;k<sizeof rejected/sizeof rejected[0];++k)CHECK(!IsBackstabTargetAnimation(rejected[k]));
 integer(yanim+0xc8,150110);Sleep(35);int prefixed=calls;CheckAnimation(1);
 CHECK(calls==prefixed+1&&lastAction==ActionBackstab);
 integer(yanim+0xc8,0);
 /* Proximity to our back must not cause a roll when backstab is ineligible. */
 real(yphysics+0x88,-1);real(yphysics+0x74,-(float)M_PI);integer(ystate+0x58,2);int before=calls;
 for(unsigned i=0;i<50;++i){CheckAnimation(1);}CHECK(calls==before);
 /* Original unknown-timing/unparriable path must roll, with no added 250ms/window gate. */
 integer(yanim+0xc8,4050);real(yphysics+0x88,-2);real(physics+0x74,0);Config.KeyCondition=0;
 int minTime,maxTime;GetAnimAtkTime(4050,&minTime,&maxTime);CHECK(minTime<0&&Unparriable(4050));
 Sleep(35);CheckAnimation(1);CHECK(calls==before+1&&lastAction==ActionRollForward);
 pointer(a0+8,me);integer(a0+0x9dc,13);uint64_t variableCount=32;memcpy(a0+0x9f0,&variableCount,8);
 struct {int id,padding;void*graph;void*excluded;} accessor={13,0,a2,0};
 float relative=CopydogRollingAngleAt(a0,7,&accessor,timeGetTime());CHECK(fabsf(fabsf(relative)-180)<.01f);
 CHECK(readlpt(float,physics+0x74)==0);
 Sleep(35);CheckAnimation(1);CHECK(calls==before+2&&lastAction==ActionRollForward);
 ready=0;Sleep(35);CheckAnimation(1);CHECK(calls==before+2);ready=1;
 real(yphysics+0x88,-5);CheckAnimation(1);CHECK(calls==before+2);real(yphysics+0x88,-2);
 before=calls;
 /* Former forced-execution selector now leaves the native packet intact. */
 int packet[44]={0},saved[44];packet[17]=610;packet[0]=0x3f800000;memcpy(saved,packet,sizeof packet);Config.Ass=1;Config.AtkMult=3;
 CHECK(OnSendPacket(0,0,20,(char*)packet,sizeof packet)==1);CHECK(!memcmp(packet,saved,sizeof packet));CHECK(calls==before);
 /* Delivered observers retain magic/goods and round-trip timing semantics. */
 int delivered[3]={0,1234,5678};prevLockTarget=you;TargetCurrentMagic=0;TargetCurrentGoods=0;
 ObserveReceivePacket(0,19,(char*)delivered,11);CHECK(TargetCurrentMagic==0&&TargetCurrentGoods==0);
 ObserveReceivePacket(0,19,(char*)delivered,12);CHECK(TargetCurrentMagic==1234&&TargetCurrentGoods==5678);
 CHECK(delivered[1]==1234&&delivered[2]==5678);
 integer(me+0x78,MyId);integer(you+0x78,MyId+1);delivered[1]=MyId+1;delivered[2]=MyId;
 Latency[0]=777;LastAttackTime[0]=timeGetTime()-200;
 ObserveReceivePacket(0,28,(char*)delivered,12);CHECK(Latency[0]>=100&&Latency[0]<200);
 CopydogEnabled=0;delivered[1]=999;ObserveReceivePacket(0,19,(char*)delivered,12);CHECK(TargetCurrentMagic==1234);
 VirtualFree(globals,0,MEM_RELEASE);printf("PASS %u source integration assertions: original behavior and delivered magic/goods/latency retained\n",checks);return 0;
}


