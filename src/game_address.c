#include "game_address.h"
uintptr_t CopydogGameBase=COPYDOG_PREFERRED_BASE,CopydogGameSize=0x6400000;
uintptr_t CdActionResume=0x140d913ed,CdSessionAddress=0x144763f38;
uintptr_t CdSendDone=0x14077e3b7,CdSendContinue=0x14077e3aa,CdWalkResume=0x142722de6;
uintptr_t BulletResume=0x14097b1bb,HitCall=0x1408df114,HitResume=0x1408df119;
uintptr_t CdRollResume=0x140d92bdf;
void CopydogBindImage(uintptr_t base,uintptr_t size){
 CdRollResume=base+0xd92bdf;
 CopydogGameBase=base;CopydogGameSize=size;
 CdActionResume=CopydogAddress(0x140d913ed);CdSessionAddress=CopydogAddress(0x144763f38);
 CdSendDone=CopydogAddress(0x14077e3b7);CdSendContinue=CopydogAddress(0x14077e3aa);
 CdWalkResume=CopydogAddress(0x142722de6);BulletResume=CopydogAddress(0x14097b1bb);
 HitCall=CopydogAddress(0x1408df114);HitResume=CopydogAddress(0x1408df119);
}
