#ifndef COPYDOG_COMBAT_H
#define COPYDOG_COMBAT_H
#include <stdint.h>
#include <windows.h>
typedef struct {
    uint64_t bullet_samples,bullet_threats,bullet_rolls,hit_checks,hit_skips;
} COPYDOG_COMBAT_COUNTERS;
enum { BD_CALLBACKS,BD_INPUT_PAUSED,BD_OWNER_REJECT,BD_INVALID,BD_UNKNOWN_PROFILE,
       BD_GEOMETRY,BD_FULL_BODY,BD_CONTACT,BD_PUBLISHED,BD_COUNT };
int CombatBulletApproaching(const float position[3],const float velocity[3],const float player[3]);
int CombatHitProtected(uint32_t flags58,uint32_t flags5c,int animation);
void CombatObserveBullet(void*);
void CombatSamplePlayerMotion(char*,uint32_t);
int CombatShouldSkipHit(void*);
typedef struct {
    uintptr_t object,self;
    int owner,id;
    uint32_t serial,input,generation,areaGroup,areaSource,seen,contact,lead;
    float yaw;
} CD_BULLET_TICKET;
int CombatSelectDueBullet(uint32_t now,unsigned lead,CD_BULLET_TICKET*out);
int CombatBulletTicketCurrent(const CD_BULLET_TICKET*,uint32_t now);
void CombatConfirmBullet(const CD_BULLET_TICKET*);
int CombatPeekBulletThreat(uint32_t);
int CombatTakeBulletThreat(uint32_t);
int CombatTakeBulletThreatDirection(uint32_t,float*);
int CombatTakeDueBulletDirection(uint32_t,unsigned,float*);
void CombatSetBulletInputPaused(int);
void CombatDiscardPendingBullets(void);
void CombatClearBulletThreat(void);
void CombatRecordBulletRoll(void);
void CombatReadCounters(COPYDOG_COMBAT_COUNTERS*);
__declspec(dllexport) DWORD WINAPI CopydogGetCombatCounters(void*);
#endif
