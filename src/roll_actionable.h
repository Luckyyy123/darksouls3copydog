#ifndef COPYDOG_ROLL_ACTIONABLE_H
#define COPYDOG_ROLL_ACTIONABLE_H
#include <math.h>
#include "roll_catch.h"

enum { ROLL_SELF_OK, ROLL_SELF_UNREADABLE, ROLL_SELF_CHANGED, ROLL_SELF_NO_HP,
       ROLL_SELF_NO_STAMINA, ROLL_SELF_FLAGS, ROLL_SELF_ACTION, ROLL_SELF_ANIMATION,
       ROLL_SELF_RECOVERY };

/* Auto-dispatch cannot assume the manual input queue will enforce cancels.
   A name changing before its visible animation ID is normal, not sufficient
   evidence that an attack, paired critical or roll can be interrupted. */
static inline int CopydogRollActionable(const char *name,int animation,int hp,int stamina,
    unsigned flags58,unsigned flags5c,float progress,unsigned busy)
{
    if(!name||!*name||animation<0||!isfinite(progress)||progress<0.f||busy>1)return ROLL_SELF_UNREADABLE;
    if(hp<=0)return ROLL_SELF_NO_HP;
    if(stamina<=0)return ROLL_SELF_NO_STAMINA;
    if((flags58&2u)||flags5c)return ROLL_SELF_FLAGS;
    int local=animation%100000;
    int ordinaryDamage=(5000<=local&&local<=5003)||(5100<=local&&local<=5103)||
                       (5200<=local&&local<=5203);
    int damageName=!strncmp(name,"damage",6)||!strncmp(name,"adddamage",9)||!strncmp(name,"guarddamage",11);
    if(ordinaryDamage&&damageName)return ROLL_SELF_RECOVERY;
    if(!RcSelfReady(name))return ROLL_SELF_ACTION;
    int move=!strcmp(name,"move");
    if((animation>=5000&&animation<=5003)||(animation>=5100&&animation<=5103)||
       (animation>=5200&&animation<=5203)){
        /* Move with a late damage ID occurs in the archive. Do not extend
           this exception to falling, staggered-for-critical or death IDs. */
        return (move||!strcmp(name,"idle"))&&progress>=1.f&&!busy?ROLL_SELF_OK:ROLL_SELF_RECOVERY;
    }
    if(!(local<4000||(19000<=local&&local<27000)))return ROLL_SELF_ANIMATION;
    /* Actual walk/run cycles can be moving with zero progress and busy=1;
       the same tuple on just an idle ID is not proof of an actionable walk. */
    if(move&&((20100<=local&&local<20120)||(20200<=local&&local<20220)||
              (23100<=local&&local<23120)||(23200<=local&&local<23220)))return ROLL_SELF_OK;
    return !busy?ROLL_SELF_OK:ROLL_SELF_RECOVERY;
}
#endif
