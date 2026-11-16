#include "../src/projectile_set.h"
#include <stdio.h>

static unsigned checks;
#define CHECK(v) do{++checks;if(!(v)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);return 1;}}while(0)
static COPYDOG_PROJECTILE_KEY key(unsigned n){return (COPYDOG_PROJECTILE_KEY){n,1,800};}

int main(void)
{
    COPYDOG_PROJECTILE_SET s={0};
    float yaw=123;
    CHECK(!CopydogProjectileSetRead(&s,100,0,&yaw)&&yaw==123);
    for(int reverse=0;reverse<=1;reverse++){
        CopydogProjectileSetClear(&s);
        for(int j=0;j<2;j++){
            int near=(j==reverse);
            CHECK(CopydogProjectileSetUpdate(&s,key(near?1:2),100,1,near?25:230,near?-1.f:1.f));
        }
        CHECK(!CopydogProjectileSetDue(&s,100,1,24,&yaw));
        CHECK(CopydogProjectileSetDue(&s,100,0,25,&yaw)&&yaw==-1.f);
        CHECK(CopydogProjectileSetDue(&s,100,1,25,&yaw)&&yaw==-1.f);
        CHECK(CopydogProjectileSetRead(&s,100,0,&yaw)&&yaw==1.f);
        CHECK(!CopydogProjectileSetUpdate(&s,key(2),101,0,0,0));
        CHECK(!CopydogProjectileSetRead(&s,101,0,&yaw));
    }
    /* Reversing the order of simultaneous equal-ETA observations also
       preserves the key-based tie break, even when they have opposite yaws. */
    for(int reverse=0;reverse<=1;reverse++){
        CopydogProjectileSetClear(&s);
        for(int j=0;j<2;j++){
            unsigned k=(j==reverse)?1:2;
            CHECK(CopydogProjectileSetUpdate(&s,key(k),200,1,40,(float)k));
        }
        CHECK(CopydogProjectileSetRead(&s,200,1,&yaw)&&yaw==1.f);
        CHECK(CopydogProjectileSetRead(&s,200,1,&yaw)&&yaw==2.f);
    }
    /* Per-object removal, not global: shot 1 turns away, shot 2 remains. */
    CopydogProjectileSetClear(&s);
    CHECK(CopydogProjectileSetUpdate(&s,key(1),100,1,100,11));
    CHECK(CopydogProjectileSetUpdate(&s,key(2),140,1,25,22));
    CHECK(CopydogProjectileSetRead(&s,140,0,&yaw)&&yaw==22.f);
    CHECK(!CopydogProjectileSetUpdate(&s,key(2),150,0,0,0));
    CHECK(!CopydogProjectileSetUpdate(&s,key(2),149,1,30,22));
    CHECK(CopydogProjectileSetRead(&s,150,1,&yaw)&&yaw==11.f);
    CHECK(!CopydogProjectileSetUpdate(&s,key(1),155,1,36,11));
    CHECK(!CopydogProjectileSetUpdate(&s,key(1),175,0,0,0));
    CHECK(!CopydogProjectileSetUpdate(&s,key(1),250,1,50,11));
    CHECK(!CopydogProjectileSetRead(&s,251,1,&yaw));
    CHECK(CopydogProjectileSetUpdate(&s,key(1),379,1,50,11));
    CHECK(CopydogProjectileSetRead(&s,380,1,&yaw)&&yaw==11);
    COPYDOG_PROJECTILE_KEY reused=key(1);reused.id=810;
    CHECK(CopydogProjectileSetUpdate(&s,reused,381,1,30,33));
    reused.owner=2;
    CHECK(!CopydogProjectileSetUpdate(&s,reused,382,0,0,0));
    CHECK(!CopydogProjectileSetRead(&s,382,0,&yaw));
    CHECK(CopydogProjectileSetUpdate(&s,reused,383,1,200,44));
    CHECK(CopydogProjectileSetRead(&s,383,1,&yaw)&&yaw==44);
    CHECK(!CopydogProjectileSetRetire(&s,key(1),383));
    CHECK(!CopydogProjectileSetRetire(&s,reused,382));
    CHECK(!CopydogProjectileSetRead(&s,383,0,&yaw));
    CHECK(CopydogProjectileSetRetire(&s,reused,384));
    CHECK(CopydogProjectileSetUpdate(&s,reused,384,1,100,55));
    CHECK(CopydogProjectileSetRead(&s,384,1,&yaw)&&yaw==55);

    /* Grace belongs to estimated first contact, not the lifetime of an
       observation. They have separate boundaries. A new estimate can replace
       an old one; merely polling the stale one never changes its deadlines. */
    CopydogProjectileSetClear(&s);
    CHECK(CopydogProjectileSetUpdate(&s,key(1),1000,1,10,1));
    CHECK(CopydogProjectileSetDue(&s,1022,0,0,&yaw));
    CHECK(!CopydogProjectileSetDue(&s,1023,1,500,&yaw));
    CHECK(CopydogProjectileSetUpdate(&s,key(1),1030,1,250,1));
    CHECK(CopydogProjectileSetRead(&s,1158,0,&yaw));
    CHECK(!CopydogProjectileSetRead(&s,1159,0,&yaw));
    CopydogProjectileSetClear(&s);
    CHECK(CopydogProjectileSetUpdate(&s,key(1),UINT32_MAX-10u,1,25,-1));
    CHECK(CopydogProjectileSetUpdate(&s,key(2),UINT32_MAX-9u,1,49,1));
    CHECK(!CopydogProjectileSetRead(&s,UINT32_MAX-11u,1,&yaw));
    CHECK(!CopydogProjectileSetDue(&s,10,1,3,&yaw));
    CHECK(CopydogProjectileSetDue(&s,10,1,4,&yaw)&&yaw==-1);
    CHECK(CopydogProjectileSetRead(&s,20,1,&yaw)&&yaw==1);

    /* Saturation retains the earliest contacts. Consumed entries are
       protected so a flood of lower-priority updates doesn't create repeats. */
    CopydogProjectileSetClear(&s);
    for(unsigned i=0;i<COPYDOG_PROJECTILE_CAPACITY;i++)
        CHECK(CopydogProjectileSetUpdate(&s,key(i+1),100,1,100+i,(float)i));
    CHECK(!CopydogProjectileSetUpdate(&s,key(100),100,1,480,100));
    CHECK(CopydogProjectileSetUpdate(&s,key(200),100,1,10,-7));
    CHECK(CopydogProjectileSetDue(&s,100,1,10,&yaw)&&yaw==-7);
    for(unsigned i=0;i<COPYDOG_PROJECTILE_CAPACITY-1;i++)
        CHECK(CopydogProjectileSetRead(&s,100,1,&yaw)&&yaw==(float)i);
    CHECK(!CopydogProjectileSetRead(&s,100,0,&yaw));
    CHECK(!CopydogProjectileSetUpdate(&s,key(200),170,1,1,-7));
    CopydogProjectileSetClear(&s);
    CHECK(!CopydogProjectileSetRead(&s,170,1,&yaw));
    CHECK(!CopydogProjectileSetUpdate(&s,key(0),170,1,1,1));
    CHECK(!CopydogProjectileSetUpdate(&s,key(3),170,1,501,1));
    CHECK(!CopydogProjectileSetUpdate(&s,key(3),170,1,10,NAN));
    printf("PASS %u projectile set checks\n",checks);
    return 0;
}
