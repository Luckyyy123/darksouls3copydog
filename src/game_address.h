#ifndef COPYDOG_GAME_ADDRESS_H
#define COPYDOG_GAME_ADDRESS_H
#include <stdint.h>
#define COPYDOG_PREFERRED_BASE UINT64_C(0x140000000)
extern uintptr_t CopydogGameBase,CopydogGameSize;
extern uintptr_t CdActionResume,CdSessionAddress,CdSendDone,CdSendContinue,CdWalkResume;
extern uintptr_t CdRollResume;
extern uintptr_t BulletResume,HitCall,HitResume;
static inline uintptr_t CopydogAddress(uintptr_t preferred){return CopydogGameBase+(preferred-COPYDOG_PREFERRED_BASE);}
void CopydogBindImage(uintptr_t base,uintptr_t size);
#endif
