#include "control.h"
#include <math.h>
#include "parry_animations.h"
int CopydogIsParryRecovery(int animation){
 int a=animation<0?-1:animation%100000;
 /* Failed/empty parry variants must not enter automatic heavy punishment. */
 return CopydogKnownParryAnimation(animation)||a==32100||a==32110||a==32120||a==36100||a==36110||a==36120||a==36900||a==37900;
}
int CopydogCriticalMovementException(int animation,unsigned char flags58,unsigned char flags5c,float progress,unsigned char recovery){
 if(animation<0||(flags58&2)||flags5c)return 0;
 int a=animation%100000;
 if((20100<=a&&a<20120)||(20200<=a&&a<20220)||
    (23100<=a&&a<23120)||(23200<=a&&a<23220))return 1;
 /* Only the late recovery state is exempt, never the invulnerable roll itself. */
 return 27100<=a&&a<27160&&isfinite(progress)&&progress>=1.f&&recovery==0;
}
static int RiposteStagger(int animation){return animation==6000||animation==6010;}
void CopydogRiposteInvalidate(COPYDOG_RIPOSTE_TRACK*t,uintptr_t self,int own,uint32_t now){
 uintptr_t actor=t->actor;
 int state=t->criticalState,failure=t->criticalFailure;
 uint32_t requested=t->criticalRequested,confirmed=t->criticalConfirmed;
 if(state==RIPOSTE_CRITICAL_WAIT){state=RIPOSTE_CRITICAL_FAILED;failure=RIPOSTE_FAIL_INVALIDATED;}
 *t=(COPYDOG_RIPOSTE_TRACK){.self=self,.actor=actor,.previousSelf=own<0?-1:own%100000,
     .previousTarget=-1,.suppressed=1,.suppressClearSince=now,
     .suppressClearing=own>=0&&!CopydogIsParryRecovery(own),
     .criticalState=state,.criticalFailure=failure,.criticalRequested=requested,.criticalConfirmed=confirmed};
}
void CopydogRiposteSubmitted(COPYDOG_RIPOSTE_TRACK*t,uintptr_t self,uintptr_t actor,int own,int target,uint32_t now){
 int ownLow=own<0?-1:own%100000,targetLow=target<0?-1:target%100000;
 int suppressed=t->self==self&&t->suppressed,suppressClearing=t->suppressClearing;
 uint32_t suppressClearSince=t->suppressClearSince;
 *t=(COPYDOG_RIPOSTE_TRACK){.self=self,.actor=actor,.previousSelf=ownLow,.previousTarget=targetLow,
     .suppressed=suppressed,.suppressClearing=suppressClearing,.suppressClearSince=suppressClearSince};
 if(!self||!actor||own<0||target<0||CopydogKnownParryAnimation(own)||
    ownLow==30201||ownLow==30202||RiposteStagger(targetLow))return;
 /* A queued request witnesses this attack, not a successful parry. */
 t->suppressed=t->suppressClearing=0;
 t->requested=now;t->haveRequest=1;t->lastAttack=now;t->haveAttack=1;
}
int CopydogRiposteRequestCritical(COPYDOG_RIPOSTE_TRACK*t,uint32_t now){
 if(!t->pending||!t->staggered||t->spent||t->suppressed||t->criticalState!=RIPOSTE_CRITICAL_NONE)return 0;
 t->criticalState=RIPOSTE_CRITICAL_WAIT;t->criticalRequested=now;
 return 1;
}
int CopydogObserveRiposte(COPYDOG_RIPOSTE_TRACK*t,uintptr_t self,uintptr_t actor,int own,int target,int attacking,uint32_t now){
 if(t->self!=self||t->actor!=actor){
  int suppressed=t->self==self&&t->suppressed,suppressClearing=t->suppressClearing;
  uint32_t suppressClearSince=t->suppressClearSince;
  *t=(COPYDOG_RIPOSTE_TRACK){.self=self,.actor=actor,.previousSelf=-1,.previousTarget=-1,
      .suppressed=suppressed,.suppressClearing=suppressClearing,.suppressClearSince=suppressClearSince};
 }
 if(!self)return 0;
 int ownLow=own<0?-1:own%100000,targetLow=target<0?-1:target%100000;
 int parry=CopydogKnownParryAnimation(own);
 /* Failed local parries stay blocked even when the remote slot changes. */
 if(t->suppressed){
  if(own<0||CopydogIsParryRecovery(own))t->suppressClearing=0;
  else if(!t->suppressClearing){t->suppressClearing=1;t->suppressClearSince=now;}
  else if((uint32_t)(now-t->suppressClearSince)>=COPYDOG_ANIMATION_STABLE_MS)
   t->suppressed=t->suppressClearing=0;
  t->previousSelf=ownLow;t->previousTarget=targetLow;return 0;
 }
 if(!actor)return 0;
 if(ownLow==30201||ownLow==30202){
  if(t->criticalState!=RIPOSTE_CRITICAL_CONFIRMED)t->criticalConfirmed=now;
  t->criticalState=RIPOSTE_CRITICAL_CONFIRMED;t->criticalFailure=RIPOSTE_FAIL_NONE;
  t->pending=t->haveRequest=t->staggerCandidate=t->staggered=0;t->spent=1;
  t->parryCandidate=t->parryLatched=0;t->previousSelf=ownLow;t->previousTarget=targetLow;return 0;
 }
 if(t->criticalState==RIPOSTE_CRITICAL_WAIT){
  if(ownLow==30000||ownLow==30010||ownLow==30020||ownLow==34000||ownLow==34010||ownLow==34020){
   t->criticalState=RIPOSTE_CRITICAL_FAILED;t->criticalFailure=RIPOSTE_FAIL_NORMAL_ATTACK;t->pending=0;
  }else if((uint32_t)(now-t->criticalRequested)>=600){
   t->criticalState=RIPOSTE_CRITICAL_FAILED;t->criticalFailure=RIPOSTE_FAIL_TIMEOUT;t->pending=0;
  }
 }
 if(t->haveRequest&&(uint32_t)(now-t->requested)>750){
  t->haveRequest=0;
  if(!t->pending&&!t->parryCandidate)t->staggerCandidate=0;
 }
 if(attacking){t->lastAttack=now;t->haveAttack=1;}
 if(parry){
  if(!t->parryCandidate){
   t->parryCandidate=1;t->parrySince=now;
   if(t->criticalState!=RIPOSTE_CRITICAL_WAIT){
    t->criticalState=RIPOSTE_CRITICAL_NONE;t->criticalFailure=RIPOSTE_FAIL_NONE;
    t->criticalRequested=t->criticalConfirmed=0;
   }
   if(!t->haveRequest)t->staggerCandidate=0;
  }
  t->parryLastSeen=now;
  if(!t->parryLatched&&(uint32_t)(now-t->parrySince)>=COPYDOG_ANIMATION_STABLE_MS){
   t->parryLatched=1;
   if(!t->pending&&t->haveAttack&&(uint32_t)(now-t->lastAttack)<=750){
    t->armed=t->parrySince;t->pending=1;t->staggered=0;t->spent=0;
   }
   t->haveRequest=0;
  }
 }else if((uint32_t)(now-t->parryLastSeen)>=COPYDOG_ANIMATION_STABLE_MS){
  t->parryCandidate=t->parryLatched=0;
 }
 /* Keep the new stagger edge even before the local parry has stabilized. */
 if(RiposteStagger(targetLow)){
  if(!t->staggerCandidate&&!RiposteStagger(t->previousTarget)&&
     (t->pending||((t->haveRequest||t->parryCandidate)&&t->haveAttack&&(uint32_t)(now-t->lastAttack)<=750))){
   t->staggerCandidate=1;t->staggerSince=now;
  }
 }else {
  t->staggerCandidate=0;
  if(t->staggered)t->pending=0;
  t->staggered=0;
 }
 if(t->pending){
  if((uint32_t)(now-t->armed)>2000)t->pending=0;
  else if(t->staggerCandidate&&(uint32_t)(now-t->staggerSince)>=COPYDOG_ANIMATION_STABLE_MS)t->staggered=1;
 }
 t->previousSelf=ownLow;t->previousTarget=targetLow;
 return t->pending&&t->staggered&&!t->spent&&t->criticalState==RIPOSTE_CRITICAL_NONE;
}
int CopydogBackstabLocked(COPYDOG_BACKSTAB_TRANSACTION*t,uintptr_t self,int animation,uint32_t now){
 if(t->self!=self)*t=(COPYDOG_BACKSTAB_TRANSACTION){.self=self};
 if(animation>=0&&animation%100000==30450){
  t->lastBusy=now;t->sawBusy=1;return 1;
 }
 /* Minimum spacing survives geometry flicker; critical exit gets a short grace period. */
 if(t->pending&&(uint32_t)(now-t->submitted)<1200)return 1;
 if(t->sawBusy&&(uint32_t)(now-t->lastBusy)<250)return 1;
 t->pending=0;t->sawBusy=0;return 0;
}
void CopydogBackstabSubmitted(COPYDOG_BACKSTAB_TRANSACTION*t,uintptr_t self,uint32_t now){
 t->self=self;t->submitted=now;t->pending=1;
}
void CopydogTrackActor(COPYDOG_ROLL_GATE*g,unsigned slot,uintptr_t actor,int animation,int outside){
 (void)outside;
 if(slot>=COPYDOG_TRACKED_ACTORS)return;
 COPYDOG_ACTOR_GATE*t=&g->actors[slot];
 if(t->actor!=actor)*t=(COPYDOG_ACTOR_GATE){.actor=actor};
 t->animation=animation;
}
int CopydogAllowBackstab(COPYDOG_ROLL_GATE*g,unsigned slot,int eligible){
 if(slot>=COPYDOG_TRACKED_ACTORS||!g->actors[slot].actor)return 0;
 COPYDOG_ACTOR_GATE*t=&g->actors[slot];
 if(!eligible){t->backstabSpent=0;return 0;}
 if(t->backstabSpent)return 0;
 t->backstabSpent=1;return 1;
}
int CopydogCopyKey(COPYDOG_COPY_KEY*k,int down,int shift,int focused,int enabled){
 int rising=down&&!k->held;k->held=down;
 if(!rising||!focused)return enabled;
 return shift?!enabled:0;
}
int CopydogAwayYaw(const float me[3],const float threat[3],float*yaw){
 if(!me||!threat||!yaw)return 0;
 for(unsigned i=0;i<3;++i)if(!isfinite(me[i])||!isfinite(threat[i]))return 0;
 double dx=(double)threat[0]-me[0],dz=(double)threat[2]-me[2];
 if(dx*dx+dz*dz<1e-8)return 0;
 /* Original source faces a target with atan2(dx,dz)-pi.
    Add pi for the opposite yaw; Y remains the height axis. */
 *yaw=(float)atan2(dx,dz);return 1;
}

int CopydogRelativeRollDegrees(float desired,float actual,float*out){
 if(!out||!isfinite(desired)||!isfinite(actual))return 0;
 double angle=remainder((double)desired-actual,6.2831853071795864769);
 *out=(float)(angle*57.295780181884766);return 1;
}

int CopydogToggleKey(COPYDOG_COPY_KEY*k,int down,int focused,int enabled){
 int rising=down&&!k->held;k->held=down;
 return rising&&focused?!enabled:enabled;
}
