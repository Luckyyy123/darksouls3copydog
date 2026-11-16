#include "../src/roll_dispatch.h"
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)
#define BEGIN(t) CopydogRollQBegin(&q,1,3000300,3,"idle",0,100,(t))
#define OBS(n,a,h,c,t) CopydogRollQObserve(&q,1,3000300,3,(n),(a),(h),(c),(t))

static int add(COPYDOG_ROLL_Q *q,uint32_t t,unsigned lag,int id)
{
    if(!CopydogRollQBegin(q,1,3000300,3,"Idle",0,100,t))return 0;
    for(unsigned i=16;i<lag;i+=16)CopydogRollQObserve(q,1,3000300,3,"Idle",0,100,0,t+i);
    if(CopydogRollQObserve(q,1,3000300,3,"RollingLight",0,100,0,t+lag))return 0;
    for(unsigned i=12;i<117;i+=12)CopydogRollQObserve(q,1,3000300,3,"rollinglight",0,100,0,t+lag+i);
    if(CopydogRollQObserve(q,1,3000300,3,"rollinglight",id,100,0,t+lag+117))return 0;
    if(CopydogRollQObserve(q,1,3000300,3,"rollinglight",id,100,0,t+lag+128))return 0;
    return CopydogRollQObserve(q,1,3000300,3,"rollinglight",id,100,0,t+lag+129);
}

int main(void)
{
    COPYDOG_ROLL_Q q={0};int lo,hi;
    CHECK(CopydogRollQBounds(&q,&lo,&hi)==COPYDOG_ROLL_Q_CANDIDATE&&lo==0&&hi==32);
    CHECK(!OBS("RollingLight",27105,100,0,1)&&!q.count);
    CHECK(!CopydogRollQBegin(&q,1,3000300,3,"RollingLightSelftrans",0,100,100));
    CHECK(!CopydogRollQBegin(&q,1,3000300,3,"RollingLight",0,100,200));
    CHECK(!CopydogRollQBegin(&q,1,3000300,3,"Idle",27105,100,300));
    CHECK(!CopydogRollQBegin(&q,1,3000300,3,"Idle",38020,100,400));
    CHECK(!CopydogRollQBegin(&q,1,3000300,3,"",0,100,500));
    CHECK(!CopydogRollQBegin(&q,1,3000300,3,"Idle",-1,100,600));
    CHECK(!CopydogRollQBegin(&q,1,3000300,3,"Idle",0,0,700));
    CHECK(add(&q,1000,0,27100)&&q.values[0]==0);
    CHECK(add(&q,2000,13,27105)&&q.values[1]==13);
    CHECK(CopydogRollQBounds(&q,&lo,&hi)==COPYDOG_ROLL_Q_CANDIDATE&&lo==0&&hi==32);
    CHECK(add(&q,3000,22,27107)&&q.values[2]==22);
    CHECK(CopydogRollQBounds(&q,&lo,&hi)==COPYDOG_ROLL_Q_MEASURED&&lo==0&&hi==28);
    CHECK(!OBS("RollingLight",27107,100,0,3200)&&q.count==3);
    CHECK(!BEGIN(3000)&&q.count==3);
    CHECK(BEGIN(4000));CHECK(!OBS("RollingLightSelftrans",0,100,0,4012)&&!q.pending);
    CHECK(BEGIN(5000));CHECK(!OBS("Idle",27105,100,0,5012)&&!q.pending);
    CHECK(BEGIN(6000));CHECK(!OBS("RollingLight",0,100,0,6012)&&q.pending);
    CHECK(!OBS("Idle",0,100,0,6018)&&!q.pending);
    CHECK(BEGIN(7000));CHECK(!OBS("RollingLight",0,100,0,7012));CHECK(!OBS("RollingLight",10027105,100,0,7032)&&!q.pending);
    CHECK(BEGIN(8000));CHECK(!OBS("RollingLight",27110,100,0,8012)&&!q.pending);
    CHECK(BEGIN(9000));CHECK(!OBS("RollingLight",27104,100,0,9012));CHECK(!OBS("RollingLight",27104,99,0,9024)&&!q.pending);
    CHECK(BEGIN(10000));CHECK(!OBS("RollingLight",27104,100,0,10012));CHECK(!OBS("RollingLight",27104,100,1,10024)&&!q.pending);
    CHECK(BEGIN(11000));CHECK(!OBS("RollingLight",27104,100,0,11065)&&!q.pending);
    CHECK(BEGIN(12000));CHECK(!OBS("Idle",0,100,0,12064)&&q.pending);
    CHECK(!OBS("Idle",0,100,0,12128));CHECK(!OBS("Idle",0,100,0,12192));CHECK(!OBS("RollingLight",0,100,0,12201)&&!q.pending);
    CHECK(BEGIN(13000));CHECK(!OBS("RollingLight",0,100,0,13012));
    for(unsigned k=1;k<=6;++k)CHECK(!OBS("RollingLight",0,100,0,13012+64*k));
    CHECK(!OBS("RollingLight",27103,100,0,13401)&&!q.pending);
    CHECK(BEGIN(14000));CHECK(!OBS("RollingLight",27105,100,0,14012));CHECK(!OBS("RollingLight",0,100,0,14020)&&!q.pending);
    CHECK(BEGIN(15000));CHECK(!OBS("RollingLight",0,100,0,15012));CopydogRollQCancel(&q);
    CHECK(!OBS("RollingLight",27105,100,0,15024)&&q.count==3);
    CHECK(BEGIN(16000));CHECK(!CopydogRollQObserve(&q,2,3000300,3,"RollingLight",27100,100,0,16012)&&!q.pending&&!q.count);
    CHECK(add(&q,17000,200,27105)&&q.values[0]==200);
    CHECK(BEGIN(18000));CHECK(!CopydogRollQObserve(&q,1,3000310,3,"RollingLight",27100,100,0,18012)&&!q.count);
    CHECK(add(&q,19000,8,27101));CHECK(!CopydogRollQObserve(&q,1,3000300,2,"Idle",0,100,0,19200)&&!q.count);
    for(unsigned k=0;k<10;++k)CHECK(add(&q,20000+k*1000,k+4,27100+(int)(k%8)));
    CHECK(q.count==9&&CopydogRollQBounds(&q,&lo,&hi)==COPYDOG_ROLL_Q_MEASURED&&lo==0&&hi==19);
    CHECK(!CopydogRollQBind(&q,0,0,0)&&!q.count);
    CHECK(add(&q,UINT32_MAX-5,13,27105)&&q.values[0]==13);
    CHECK(add(&q,201,5,27105)&&q.values[1]==5);
    CHECK(!CopydogRollQBounds(&q,0,&hi));
    printf("roll dispatch: %u checks passed\n",checks);return 0;
}
