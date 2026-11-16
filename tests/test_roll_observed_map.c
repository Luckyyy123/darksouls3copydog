#include <stdio.h>
#include "../src/roll_observed_map.h"

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)

int main(void)
{
    CHECK(CopydogRollObservedId(16070000,1,"attackrightlight1")==23030000);
    CHECK(CopydogRollObservedId(6020000,3,"attackbothlight1")==25034000);
    CHECK(CopydogRollObservedId(6380000,3,"attackbothdash")==264034500);
    CHECK(CopydogRollObservedId(20310000,3,"attackbothdash")==48034500);
    CHECK(CopydogRollObservedId(16070000,3,"attackbothleftdash")==142033500);
    CHECK(CopydogRollObservedId(16070000,1,"attackrightheavy1start")==23030320);
    CHECK(CopydogRollObservedId(16070000,1,"attackrightheavy1end")==23030321);
    CHECK(CopydogRollObservedId(16070000,1,"attackrightlightdash")==-1);
    CHECK(CopydogRollObservedId(16070000,3,"attackrightlight1")==-1);
    CHECK(CopydogRollObservedId(16071010,1,"attackrightlight1")==-1);
    CHECK(CopydogRollObservedId(16070000,1,"AttackRightLight1")==-1);
    CHECK(CopydogRollObservedId(16070000,1,"attackrightlight10")==-1);
    CHECK(CopydogRollObservedId(16070000,1,0)==-1);
    CHECK(CopydogRollObservedId(0,1,"attackrightlight1")==-1);
    for(unsigned i=0;i<COPYDOG_ROLL_OBSERVED_COUNT;++i){
        const COPYDOG_ROLL_OBSERVED_KEY *p=&CopydogRollObservedKeys[i];
        CHECK(CopydogRollObservedId(p->weaponBase,p->mode,p->name)==p->animation);
        for(unsigned j=0;j<i;++j){
            const COPYDOG_ROLL_OBSERVED_KEY *q=&CopydogRollObservedKeys[j];
            CHECK(p->weaponBase!=q->weaponBase||p->mode!=q->mode||strcmp(p->name,q->name));
        }
    }
    printf("roll observed lookup: %u checks passed\n",checks);
    return 0;
}
