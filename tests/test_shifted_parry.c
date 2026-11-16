#include "../src/parry.h"
#include "../src/parry_empirical.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    int start, end, profile, oldStart, oldEnd, oldProfile;
    CHECK(CopydogParryWindow(100,180,200,0,&start,&end)==1);
    CHECK(start==0&&end==32);
    CHECK(CopydogParryWindowShifted(100,180,200,0,150,&start,&end)==1);
    CHECK(start==50&&end==162);
    CHECK(CopydogParryWindowShifted(100,180,200,0,50,&start,&end)==1);
    CHECK(start==0&&end==62);
    CHECK(CopydogParryCalibratedWindow(11010300,25034000,500,333,0,&start,&end)==1);
    CHECK(start==0&&end==32);
    CHECK(CopydogParryCalibratedWindowShifted(11010300,25034000,500,333,0,100,&start,&end)==1);
    CHECK(start==74&&end==106);

    CHECK(CopydogEmpiricalPlan(11010300,6020310,25034000,1,115,1,1.0f,&start,&end,&profile)==-1);
    CHECK(start==-1&&end==-1&&profile>=0);
    CHECK(CopydogEmpiricalPlanShifted(11010300,6020310,25034000,1,115,1,1.0f,305,&start,&end,&profile)==1);
    CHECK(start==234&&end==250);
    for(int lead=303;lead<=309;++lead){
        CHECK(CopydogEmpiricalPlanShifted(11010300,6020310,25034000,1,115,1,1.0f,lead,&start,&end,&profile)==1);
        CHECK(start==lead-71&&end==start+16);
        CHECK(end<=(int)floor(107.534+lead-115-1));
    }
    CHECK(CopydogEmpiricalPlanShifted(11010300,6020310,25034000,1,405,1,1.0f,305,&start,&end,&profile)==1);
    CHECK(start==0&&end==6);
    CHECK(CopydogEmpiricalPlanShifted(11010300,6020310,25034000,1,412,1,1.0f,305,&start,&end,&profile)==-1);

    for(int lead=-1;lead<=601;lead+=602){
        CHECK(CopydogParryWindowShifted(100,200,10,0,lead,&start,&end)==0);
        CHECK(CopydogParryCalibratedWindowShifted(11010000,25034000,500,333,0,lead,&start,&end)==0);
        CHECK(CopydogEmpiricalPlanShifted(11010000,6020000,25034000,0,0,0,1,lead,&start,&end,&profile)==0);
        CHECK(start==-1&&end==-1&&profile==-1);
    }
    CHECK(CopydogParryWindowShifted(INT_MAX,INT_MAX,0,0,600,&start,&end)==0);
    CHECK(CopydogParryCalibratedWindowShifted(11010000,25034000,INT_MAX,0,0,600,&start,&end)==0);
    CHECK(CopydogEmpiricalPlanShifted(11010000,6020000,25034000,1,INT_MAX,INT_MAX,1,600,&start,&end,&profile)==-1);
    CHECK(CopydogEmpiricalPlanShifted(11010000,6020000,25034000,0,INT_MIN,0,1,600,&start,&end,&profile)==1);
    CHECK(start==645&&end==661);
    CHECK(CopydogParryWindowShifted(100,200,10,0,600,NULL,&end)==0);
    CHECK(CopydogParryCalibratedWindowShifted(11010000,25034000,500,333,0,600,&start,NULL)==0);
    CHECK(CopydogEmpiricalPlanShifted(11010000,6020000,25034000,0,0,0,NAN,10,&start,&end,&profile)==0);
    CHECK(CopydogEmpiricalPlanShifted(11010000,6020000,25034000,0,0,0,1,10,NULL,&end,&profile)==0);
    CHECK(CopydogEmpiricalPlanShifted(11010000,6030000,25034000,0,0,0,1,100,&start,&end,&profile)==0);

    /* Every original API preserves its zero-shift result, including clipped windows. */
    for(int first=1;first<=1001;first+=100){
        for(int startup=0;startup<=1000;startup+=100){
            int old=CopydogParryWindow(first,first+90,startup,7,&oldStart,&oldEnd);
            CHECK(CopydogParryWindowShifted(first,first+90,startup,7,0,&start,&end)==old);
            CHECK(start==oldStart&&end==oldEnd);
            old=CopydogParryCalibratedWindow(20080000,25034000,first,startup,7,&oldStart,&oldEnd);
            CHECK(CopydogParryCalibratedWindowShifted(20080000,25034000,first,startup,7,0,&start,&end)==old);
            CHECK(start==oldStart&&end==oldEnd);
        }
    }
    for(unsigned i=0;i<CopydogEmpiricalProfileCount();++i){
        const COPYDOG_EMPIRICAL_PROFILE *p=CopydogEmpiricalProfileAt(i);
        for(int delay=0;delay<=400;delay+=20){
            int old=CopydogEmpiricalPlan(p->toolBase,p->targetWeaponBase,p->animation,1,delay,1,1,&oldStart,&oldEnd,&oldProfile);
            CHECK(CopydogEmpiricalPlanShifted(p->toolBase,p->targetWeaponBase,p->animation,1,delay,1,1,0,&start,&end,&profile)==old);
            CHECK(start==oldStart&&end==oldEnd&&profile==oldProfile);
        }
    }
    printf("PASS %u shifted parry window assertions\n",checks);
    return 0;
}
