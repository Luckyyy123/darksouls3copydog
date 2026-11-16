#define CROSSBOW_FIXTURE_ENTRY UnusedCrossbowFixture
#include "test_crossbow_roll.c"
#include "../src/projectile_param_data.h"
int main(void)
{
    void*g=VirtualAlloc((void*)0x144770000,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(g==(void*)0x144770000);SCENE s={0};setup(&s);char bolt[0x500]={0};
    integer(s.me+8,0x12340000);integer(s.you+8,0x12340007);
    integer(s.me+0x78,53249);integer(s.you+0x78,53250);
    integer(bolt+0x4e4,800);integer(bolt+0x4ec,0x12340007);integer(bolt+0x4dc,1);
    real(bolt+0x10,-1);real(bolt+0x14,.8f);real(bolt+0x30,30);
    CombatSetBulletInputPaused(0);CombatObserveBullet(bolt);
    CHECK(CombatTakeBulletThreat(RealNow()));
    integer(bolt+0x4dc,4);CombatObserveBullet(bolt);integer(bolt+0x4dc,1);
    integer(bolt+0x4ec,53250);CombatObserveBullet(bolt);CHECK(!CombatPeekBulletThreat(RealNow()));
    integer(bolt+0x4ec,0x12340000);CombatObserveBullet(bolt);CHECK(!CombatPeekBulletThreat(RealNow()));
    integer(bolt+0x4ec,0x12340007);
    int spell=-1;
    for(unsigned i=0;i<_countof(CopydogBulletBounds);i++){
        int id=CopydogBulletBounds[i].id;
        if(GetAttackBulletSpeed(id)<=0&&CopydogBulletBounds[i].radius<1){spell=id;break;}
    }
    CHECK(spell>=0);integer(bolt+0x4e4,spell);real(bolt+0x10,-.5f);CombatObserveBullet(bolt);
    CHECK(CombatTakeBulletThreat(RealNow()));
    VirtualFree(g,0,MEM_RELEASE);
    printf("PASS %u native FieldInsHandle / nonfixed slot / spell-observer checks\n",checks);
}
