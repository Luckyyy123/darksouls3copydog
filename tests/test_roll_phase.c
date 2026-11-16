#include "../src/roll_phase.h"
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)

int main(void)
{
    COPYDOG_ROLL_PHASE_WINDOW w,shifted;
    /* Synthetic contact case plus the audited 27100 ArgE=0 resource duration.
       q bounds here exercise math; they are not measured rolling dispatch. */
    CHECK(CopydogRollPhaseWindow(300000,366667,0,433333,0,24,0,0,0,&w));
    CHECK(w.startMs==0&&w.endMs==276&&w.preferredMs==138&&!w.onsetOnly);
    CHECK(!CopydogRollPhaseDue(&w,137));
    CHECK(CopydogRollPhaseDue(&w,138)&&CopydogRollPhaseDue(&w,276));
    CHECK(!CopydogRollPhaseDue(&w,277)&&!CopydogRollPhaseDue(&w,UINT32_MAX));
    CHECK(!CopydogRollPhaseDue(0,138));
    CHECK(CopydogRollPhaseWindow(633333,700000,0,433333,0,24,0,0,0,&w));
    CHECK(w.startMs==267&&w.endMs==609&&w.preferredMs==438);
    CHECK(CopydogRollPhaseWindow(633333,700000,0,433333,20,44,0,0,0,&shifted));
    CHECK(shifted.startMs==247&&shifted.endMs==589);
    CHECK(CopydogRollPhaseWindow(633333,700000,0,433333,0,24,20,20,0,&shifted));
    CHECK(shifted.startMs==247&&shifted.endMs==589);
    CHECK(!CopydogRollPhaseWindow(600000,1400000,0,433333,0,24,0,0,0,&w));
    CHECK(!w.valid);
    CHECK(CopydogRollPhaseWindow(600000,1400000,0,433333,0,24,0,0,1,&w));
    CHECK(w.onsetOnly&&w.startMs==167&&w.endMs==576);
    CHECK(!CopydogRollPhaseWindow(10000,20000,100000,200000,0,24,0,0,1,&w));
    CHECK(!CopydogRollPhaseWindow(100,100,0,200000,0,24,0,0,0,&w));
    CHECK(!CopydogRollPhaseWindow(0,100,200,200,0,24,0,0,0,&w));
    CHECK(!CopydogRollPhaseWindow(0,100,0,200,-1,24,0,0,0,&w));
    CHECK(!CopydogRollPhaseWindow(0,100,0,200,25,24,0,0,0,&w));
    CHECK(!CopydogRollPhaseWindow(0,100,0,200,0,1001,0,0,0,&w));
    CHECK(!CopydogRollPhaseWindow(0,100,0,200,0,24,-1001,0,0,&w));
    CHECK(!CopydogRollPhaseWindow(0,100,0,200,0,24,1,0,0,&w));
    CHECK(!CopydogRollPhaseWindow(0,100,0,200,0,24,0,1001,0,&w));
    CHECK(!CopydogRollPhaseWindow(0,100,0,200,0,24,0,0,0,0));
    uint32_t elapsed=0;
    CHECK(CopydogRollNameElapsedFromId(100,210,&elapsed)&&elapsed==310);
    CHECK(!CopydogRollNameElapsedFromId(100,-1,&elapsed));
    CHECK(!CopydogRollNameElapsedFromId(100,1001,&elapsed));
    CHECK(!CopydogRollNameElapsedFromId(UINT32_MAX,1,&elapsed));
    CHECK(!CopydogRollNameElapsedFromId(100,210,0));
    for(int first=0;first<=2000000;first+=17000){
        for(int duration=1000;duration<=500000;duration+=31000){
            if(!CopydogRollPhaseWindow(first,first+duration,0,433333,2,22,-5,7,0,&w))continue;
            CHECK((int64_t)w.startMs*1000+2000-5000+433333>=first+duration);
            CHECK((int64_t)w.endMs*1000+22000+7000<=first);
            CHECK(w.preferredMs>=w.startMs&&w.preferredMs<=w.endMs);
            CHECK(!CopydogRollPhaseDue(&w,(uint32_t)w.endMs+1));
        }
    }
    printf("roll phase: %u checks passed\n",checks);
    return 0;
}
