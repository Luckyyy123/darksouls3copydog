#ifndef COPYDOG_H
#define COPYDOG_H
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "game_address.h"
#include <stdio.h>
#include <ntdef.h>
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <psapi.h>
#define HIDWORD(x) ((uint32_t)(((uint64_t)(x))>>32))
#define LODWORD(x) ((uint32_t)(((uint64_t)(x))))
#define MAKEQWORD(h,l) (((uint64_t)(h))<<32|((uint64_t)(uint32_t)(l)))
#define Emit(addr,type,...) do{type data[]={__VA_ARGS__};DWORD OldProtect;if(memcmp(data,(void*)(addr),sizeof data)&&VirtualProtectEx((HANDLE)-1,(void*)(addr),sizeof data,PAGE_EXECUTE_READWRITE,&OldProtect))\
{memcpy((void*)(addr),data,sizeof data);VirtualProtectEx((HANDLE)-1,(void*)(addr),sizeof data,OldProtect,&OldProtect);}}while(0)
#define Fill(addr,type,value,count)do{DWORD OldProtect;if(VirtualProtectEx((HANDLE)-1,(void*)(addr),sizeof(type)*(count),PAGE_EXECUTE_READWRITE,&OldProtect))\
{for(int iter=0;iter<(count);++iter)((type*)addr)[iter]=(value);VirtualProtectEx((HANDLE)-1,(void*)(addr),sizeof(type)*(count),OldProtect,&OldProtect);}}while(0)
#define nop(addr,size) Fill(addr,unsigned char,0x90,size)
#define AX 0
#define CX 1
#define DX 2
#define BX 3
#define SP 4
#define BP 5
#define SI 6
#define DI 7
#define addr1 ((void*)CopydogAddress(0x14477FDB8))
#define addr2 ((void*)CopydogAddress(0x1447572B8))
#define addr3 ((void*)CopydogAddress(0x14475AC00))
#define addr4 ((void*)CopydogAddress(0x14477DBE0))
#define addr7 ((void*)CopydogAddress(0x14477FEC8))
#define XA 0x1f90
#define XB 0x1fA0
#define MyId 0x10068000
#define SteamIdHigh (0x1100001)
#define MAX_REMOTE_PLAYER 5
#ifdef __cplusplus
	#define call(f,...) (((intptr_t(*)(...))CopydogAddress((uintptr_t)(f)))(__VA_ARGS__))
#else
	#define call(f,...) (((intptr_t(*)())CopydogAddress((uintptr_t)(f)))(__VA_ARGS__))
#endif

#define dbgprint(format, ...) do{\
		static char ssss1[4096];\
        unsigned long _n_=_snprintf(ssss1,_countof(ssss1),format,##__VA_ARGS__);\
        OutputDebugStringA(ssss1);\
    }while(0)

#define VALIST_END INT_MIN
/* Each pointer read and final transfer is checked. This is not a lock on
   game-owned objects: callers must still tolerate lifetime changes. */
static void*_rwlp(int write,unsigned len,void*buf,void*lp_start,...)
{
    va_list ap;
    uintptr_t p=(uintptr_t)lp_start,next;
    int offset,ok=1;
    SIZE_T done=0;
    if(len&&!buf)return 0;
    va_start(ap,lp_start);
    while((offset=va_arg(ap,int))!=VALIST_END){
        if(!p||!ReadProcessMemory(GetCurrentProcess(),(void*)p,&next,sizeof next,&done)||done!=sizeof next){ok=0;break;}
        if(!next||(offset>=0?next>UINTPTR_MAX-(unsigned)offset:next<(uintptr_t)(-(int64_t)offset))){ok=0;break;}
        p=offset>=0?next+(unsigned)offset:next-(uintptr_t)(-(int64_t)offset);
    }
    va_end(ap);
    if(!ok||!p||p>UINTPTR_MAX-(len?len-1:0))goto fail;
    if(!len){
        unsigned char probe;
        if(!ReadProcessMemory(GetCurrentProcess(),(void*)p,&probe,1,&done)||done!=1)goto fail;
    }else if(write){
        if(!WriteProcessMemory(GetCurrentProcess(),(void*)p,buf,len,&done)||done!=len)goto fail;
    }else if(!ReadProcessMemory(GetCurrentProcess(),(void*)p,buf,len,&done)||done!=len)goto fail;
    return (void*)p;
fail:
    if(!write&&len&&buf)memset(buf,0,len);
    return 0;
}
#define getlp(type,lp,...) (type)(_rwlp(0,0,0,(void*)(uintptr_t)(lp),##__VA_ARGS__,VALIST_END))
#define readlp(len,data,lp,...) _rwlp(0,len,data,(void*)(uintptr_t)(lp),##__VA_ARGS__,VALIST_END)
#define readlpv(data,lp,...) _rwlp(0,sizeof(data),&(data),(void*)(uintptr_t)(lp),##__VA_ARGS__,VALIST_END)
#define readlpt(type,lp,...) ({type __cd_value;memset(&__cd_value,0,sizeof(type));_rwlp(0,sizeof(type),&__cd_value,(void*)(uintptr_t)(lp),##__VA_ARGS__,VALIST_END);__cd_value;})

#define writelp(len,buf,lp,...) _rwlp(1,len,buf,(void*)(uintptr_t)(lp),##__VA_ARGS__,VALIST_END)
#define writelpv(v,lp,...) _rwlp(1,sizeof(v),&(v),(void*)(uintptr_t)(lp),##__VA_ARGS__,VALIST_END)
#define writelpt(type,v,lp,...) ({type __cd_value=(v);void*__cd_address=_rwlp(1,sizeof(type),&__cd_value,(void*)(uintptr_t)(lp),##__VA_ARGS__,VALIST_END);__cd_address;})
static inline char*_player(int i)
{
    return 0<=i&&i<=MAX_REMOTE_PLAYER?readlpt(char*,addr1,0x40,0x38*i):0;
}
static inline int _actor_anim(char*p)
{
    int anim=-1;
    if(p&&!readlpv(anim,&p,XA,0x80,0xc8))anim=-1;
    return anim;
}
static inline float*_actor_vector(char*p,int kind)
{
    if(!p)return 0;
    switch(kind){
    case 0:return getlp(float*,&p,XA,0x68,0x70);
    case 1:return getlp(float*,&p,0x50,0x348,0x40,0x10);
    case 2:return getlp(float*,&p,XA,0x68,0x80);
    default:return getlp(float*,&p,XA,0x68,0xa8,0x40,0x70);
    }
}
#define Player0 readlpt(char*,addr1,0x80)
#define Player(i) _player(i)
#define PlayerCount readlpt(int,addr3,0xd3c)
#define GetActorAnim(p) _actor_anim((char*)(p))
#define GetActorDir(p) _actor_vector((char*)(p),0)
#define GetActorDir2(p) _actor_vector((char*)(p),1)
#define GetActorPos(p) _actor_vector((char*)(p),2)
#define GetActorPos2(p) _actor_vector((char*)(p),3)
static inline char*_net2char(void*p)
{
    void*world=readlpt(void*,addr1);
    return world&&p?((char*(*)(void*,void*))CopydogAddress(0x1408D9E70))(world,p):0;
}
#define net2char(p) _net2char((void*)(p))
#define GetActionAddress(p) readlpt(char*,(char*)(p)+XA,0x28,16,0x28,0xb8)
#define DoIdle(a) do{char*p=readlpt(char*,addr1,0x80,XA,0x28,16);if(p)call(0x140D91060,p+0x28,a);}while(0)
extern double(*__imp_fabs)(double),(*__imp_atan2)(double,double),(*__imp_fmod)(double,double),(*__imp_sin)(double),(*__imp_cos)(double),(*__imp_sqrt)(double);
typedef enum _ACTION_ID{
	ActionLeftParry=1889,ActionRightParry=1890,ActionBackstab=2897,ActionRollForward=3821,ActionJumpAttack=1076,ActionJumpAttack2=1024,ActionBackStep=3814,ActionQuickStep=3500,
	ActionTwoHandAttack1=1029,ActionTwoHandAttack2=1030,ActionTwoHandAttack3=1031,ActionTwoHandRollAttack=1851,ActionTwoHandRunAttack=1020,
	ActionTwoHandHeavyAttack1=1846,ActionTwoHandHeavyChargedAttack1=1847,ActionTwoHandHeavyAttack2=1848,ActionTwoHandHeavyChargedAttack2=1849,ActionLeftMagic=3146,ActionLeftMagic1=3147,
	ActionRightHeavyAttack1=1070,ActionRightHeavyChargedAttack1=1071,ActionRightHeavyAttack2=1074,ActionRightHeavyChargedAttack2=1075,ActionRightMagic=3148,ActionRightMagic1=3149,
	ActionRightAttack1=1085,ActionRightAttack2=1086,ActionRightAttack3=1087,ActionRightRunAttack=1088,ActionRightRollAttack=1090,ActionWalk=2475,ActionGesture=3552,ActionGesture1=3553,ActionIdle=3564,ActionIdle1=3744
}ACTION_ID,*PACTION_ID;

typedef enum _ANIMATION_ID{
	AnimTwoHandAttack1=34000,AnimTwoHandAttack2=34010,AnimTwoHandAttack3=34020,AnimTwoHandRunAttack=34500,AnimTwoHandRollAttack=34900,
	AnimTwoHandHeavyAttack1=34321,AnimTwoHandHeavyChargedAttack1=34320,AnimTwoHandHeavyAttack2=34341,AnimTwoHandHeavyChargedAttack2=34340,
	AnimLeftAttack1=32000,AnimLeftAttack2=32030,AnimLeftAttack3=32060,AnimTwoHandLeftAttack1=33000,AnimTwoHandLeftAttack2=33010,AnimTwoHandLeftAttack3=33020,
	AnimRightHeavyAttack1=30321,AnimRightHeavyChargedAttack1=30320,AnimRightHeavyAttack2=30341,AnimRightHeavyChargedAttack2=30340,
	AnimRightAttack1=30000,AnimRightAttack2=30010,AnimRightAttack3=30020,AnimRightRunAttack=30500,AnimRightRollAttack=30900,
}ANIMATION_ID,*PANIMATION_ID;
extern char*GetLockTarget(),IsStagger(int anim);
extern int ReadAutoRollState(char *me);
extern int ReadAutoRollStateDetail(char *me,int *busyMove);
extern int GetPlayerIndex(char*p),ReadyToAttack(),ReadyToRoll(),GetCurrentWeapon(int right),IsNonDmgAttack(int anim),Unparriable(int anim),IsDmgAttack(int anim),GetWeaponSkill(int id),
GetNextAttack(),GetNextHeavyAttack(),GetNextRunAttack(),GetNextRollAttack(),IsFastWeapon(int weapon),IsSlowAttack(int anim);
extern float GetAttackBulletSpeed(int id);
extern int TryDoAction(int action);
extern int ReadyToCritical(void);
enum { FRONTAL_THROW_ID=101,FRONTAL_THROW_INVALID=-10,FRONTAL_THROW_STALE=-11,
       FRONTAL_THROW_DEAD=-12,FRONTAL_THROW_NOT_READY=-13,FRONTAL_THROW_NO_HANDLE=-14,
       FRONTAL_THROW_EQUIPMENT_UNKNOWN=-15 };
extern int CopydogSelectFrontalThrow(char*);
extern int TryRequestFrontalRiposte(char*self,char*target);
extern void GetAnimAtkTime(int anim,int*mintime,int*maxtime),DoAction(int action),*doaction(void*,void*);;
#endif /* COPYDOG_H */
