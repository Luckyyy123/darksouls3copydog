#include "../src/parry.h"
#include "../src/control.h"
#include <math.h>
#include <stdio.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    COPYDOG_PARRY_TRACK p={0};
    CopydogParryObserve(&p,1,2,25034000,NAN,1000);
    for(int i=0;i<20;++i)CopydogParryObserve(&p,1,2,25034000,NAN,1000);
    CHECK(!p.stable);
    CHECK(CopydogParryPhase(&p,1000,0,100)==PARRY_UNSTABLE);
    CopydogParryObserve(&p,1,2,25034000,NAN,1012);
    CHECK(p.stable&&p.started==1000);
    CHECK(CopydogParryPhase(&p,1044,40,60)==PARRY_NONE);
    p.spent=1;
    CopydogParryObserve(&p,1,2,0,NAN,1050);
    CHECK(p.candidateActive&&p.spent);
    CopydogParryObserve(&p,1,2,25034000,NAN,1056);
    CHECK(!p.candidateActive&&p.spent&&p.started==1000);
    CopydogParryObserve(&p,1,2,25034010,NAN,1100);
    CopydogParryObserve(&p,1,2,25034010,NAN,1112);
    CHECK(p.animation==25034010&&!p.spent&&p.started==1100);
    p.spent=1;
    CopydogParryObserve(&p,1,2,0,NAN,1500);
    CopydogParryObserve(&p,1,2,0,NAN,1512);
    CopydogParryObserve(&p,1,2,25034010,NAN,1600);
    CopydogParryObserve(&p,1,2,25034010,NAN,1612);
    CHECK(!p.spent&&p.started==1600);
    CopydogParryObserve(&p,1,3,25034000,NAN,0xfffffff8u);
    CopydogParryObserve(&p,1,3,25034000,NAN,4);
    CHECK(p.stable&&p.started==0xfffffff8u&&p.actor==3);
    CHECK(CopydogParryPhase(&p,0x30,40,60)==PARRY_NONE);
    CopydogParryObserve(&p,1,3,0,NAN,100);
    CopydogParryObserve(&p,1,3,0,NAN,112);
    CopydogParryObserve(&p,1,3,25034000,NAN,200);
    CopydogParryObserve(&p,1,3,0,NAN,206);
    CopydogParryObserve(&p,1,3,25034000,NAN,300);
    CopydogParryObserve(&p,1,3,25034000,NAN,312);
    CHECK(p.started==300);

    COPYDOG_RIPOSTE_TRACK r={0};
    CHECK(!CopydogObserveRiposte(&r,1,2,0,25034000,1,1000));
    CHECK(!CopydogObserveRiposte(&r,1,2,42036900,25034000,1,1010));
    CHECK(!CopydogObserveRiposte(&r,1,2,42036900,25034000,1,1022));
    CHECK(r.pending);
    CHECK(!CopydogObserveRiposte(&r,1,2,42036900,6000,0,1050));
    CHECK(!CopydogObserveRiposte(&r,1,2,42036900,6000,0,1050));
    CHECK(CopydogObserveRiposte(&r,1,2,42036900,6000,0,1062));
    CHECK(!CopydogObserveRiposte(&r,1,2,23030201,70010,0,1100));
    CHECK(!r.pending&&r.spent);
    r=(COPYDOG_RIPOSTE_TRACK){0};
    CopydogObserveRiposte(&r,1,2,0,25034000,1,2000);
    CopydogObserveRiposte(&r,1,2,42032100,25034000,1,2010);
    CopydogObserveRiposte(&r,1,2,42032100,25034000,1,2022);
    CopydogObserveRiposte(&r,1,2,42032100,6000,0,2050);
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,25034000,1,2056));
    CHECK(!r.staggered);
    r=(COPYDOG_RIPOSTE_TRACK){0};
    CopydogObserveRiposte(&r,1,2,0,25034000,1,3000);
    CopydogObserveRiposte(&r,1,2,9932100,25034000,1,3010);
    CopydogObserveRiposte(&r,1,2,9932100,6000,0,3050);
    CHECK(!r.pending);

    /* The request precedes the late attack ID; WAIT still observes both actors. */
    CopydogRiposteSubmitted(&r,1,2,0,0,4000);
    CHECK(r.haveRequest&&r.haveAttack&&!r.pending&&!r.staggered);
    CHECK(!CopydogObserveRiposte(&r,1,2,0,25034000,1,4070));
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,4100));
    CHECK(r.staggerCandidate&&!r.pending);
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,4106));
    CHECK(CopydogObserveRiposte(&r,1,2,42032100,6000,0,4112));
    r.spent=1;
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,4124));

    /* A new stagger may precede even the first observed local parry animation. */
    CopydogRiposteSubmitted(&r,1,2,0,0,5000);
    CHECK(!CopydogObserveRiposte(&r,1,2,0,6000,0,5090));
    CHECK(r.staggerCandidate&&!r.pending);
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,5100));
    CHECK(r.staggerSince==5090);
    CHECK(CopydogObserveRiposte(&r,1,2,42032100,6000,0,5112));

    /* Manual parries also retain a stagger first seen with the local animation. */
    r=(COPYDOG_RIPOSTE_TRACK){0};
    CHECK(!CopydogObserveRiposte(&r,1,2,0,25034000,1,6000));
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,6100));
    CHECK(CopydogObserveRiposte(&r,1,2,42032100,6000,0,6112));

    /* A request cannot convert an already staggered enemy into fresh evidence. */
    CopydogRiposteSubmitted(&r,1,2,0,6000,7000);
    CHECK(!r.haveRequest&&!r.haveAttack&&!r.pending);
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,7050));
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,7062));
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6010,0,7074));
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6010,0,7086));
    CHECK(!r.staggerCandidate);

    r=(COPYDOG_RIPOSTE_TRACK){0};
    CopydogObserveRiposte(&r,1,2,0,25034000,1,8000);
    CopydogObserveRiposte(&r,1,2,0,6000,0,8050);
    CopydogObserveRiposte(&r,1,2,42032100,6000,0,8100);
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,8112));
    CHECK(r.pending&&!r.staggerCandidate);

    CopydogRiposteSubmitted(&r,1,2,0,0,9000);
    CHECK(!CopydogObserveRiposte(&r,1,2,9932100,6000,0,9100));
    CHECK(!CopydogObserveRiposte(&r,1,2,9932100,6000,0,9120));
    CHECK(!r.pending);
    CopydogRiposteSubmitted(&r,1,2,42032100,0,9200);
    CHECK(!r.haveRequest&&!r.haveAttack);
    CopydogRiposteSubmitted(&r,1,2,-1,0,9300);
    CHECK(!r.haveRequest&&!r.haveAttack);

    CopydogRiposteSubmitted(&r,1,2,0,0,10000);
    CopydogObserveRiposte(&r,1,2,42032100,6000,0,10100);
    CHECK(!CopydogObserveRiposte(&r,1,3,42032100,6000,0,10112));
    CHECK(!r.haveRequest&&!r.haveAttack&&!r.pending);
    CopydogRiposteSubmitted(&r,1,2,0,0,10200);
    CopydogObserveRiposte(&r,1,2,42032100,6000,0,10300);
    CHECK(!CopydogObserveRiposte(&r,4,2,42032100,6000,0,10312));
    CHECK(!r.haveRequest&&!r.haveAttack&&!r.pending);

    CopydogRiposteSubmitted(&r,1,2,0,0,11000);
    CopydogObserveRiposte(&r,1,2,42032100,6000,0,11800);
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,11812));
    CHECK(!r.haveRequest&&!r.pending);

    CopydogRiposteSubmitted(&r,1,2,0,0,12000);
    CopydogObserveRiposte(&r,1,2,42032100,6000,0,12100);
    CopydogObserveRiposte(&r,1,2,42032100,25034000,1,12106);
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,12112));
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,12118));
    CHECK(CopydogObserveRiposte(&r,1,2,42032100,6000,0,12124));
    CHECK(!CopydogObserveRiposte(&r,1,2,23030201,6000,0,12130));
    CHECK(!CopydogObserveRiposte(&r,1,2,23030202,6000,0,12142));
    CHECK(!r.pending&&r.spent&&!r.staggerCandidate);
    CopydogObserveRiposte(&r,1,2,42032100,6000,0,12154);
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,12166));

    /* HP loss must block the same failed parry, not just erase its candidates. */
    CopydogRiposteSubmitted(&r,1,2,0,0,13000);
    CopydogObserveRiposte(&r,1,2,42032100,6000,0,13100);
    CopydogRiposteInvalidate(&r,1,42032100,13106);
    CHECK(r.suppressed&&!r.pending&&!r.haveAttack&&!r.haveRequest);
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,25034000,1,13112));
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,25034000,1,13124));
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,13136));
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6000,0,13148));
    CHECK(r.suppressed&&!r.pending&&!r.haveAttack);

    /* An empty/new remote slot must inherit the local failure suppression. */
    r=(COPYDOG_RIPOSTE_TRACK){0};
    CopydogRiposteInvalidate(&r,1,42032100,14000);
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,25034000,1,14012));
    CHECK(r.suppressed&&r.actor==2&&!r.haveAttack);
    CHECK(!CopydogObserveRiposte(&r,1,3,42032100,25034000,1,14024));
    CHECK(r.suppressed&&r.actor==3&&!r.haveAttack);
    CHECK(!CopydogObserveRiposte(&r,1,0,42032100,-1,0,14036));
    CHECK(r.suppressed&&r.actor==0);
    CHECK(!CopydogObserveRiposte(&r,1,4,-1,25034000,1,14048));
    CHECK(r.suppressed&&!r.suppressClearing);
    CHECK(!CopydogObserveRiposte(&r,1,4,9932100,25034000,1,14060));
    CHECK(r.suppressed&&!r.suppressClearing);

    /* Leaving the parry must be stable; a short idle flicker does not release it. */
    CopydogObserveRiposte(&r,1,4,0,25034000,1,14100);
    CHECK(r.suppressed&&r.suppressClearing);
    CopydogObserveRiposte(&r,1,4,0,25034000,1,14111);
    CHECK(r.suppressed);
    CopydogObserveRiposte(&r,1,4,42032100,25034000,1,14112);
    CHECK(r.suppressed&&!r.suppressClearing);
    CopydogObserveRiposte(&r,1,0,0,-1,0,14120);
    CHECK(r.suppressed);
    CHECK(!CopydogObserveRiposte(&r,1,4,0,25034000,1,14132));
    CHECK(!r.suppressed&&!r.haveAttack&&!r.pending);
    CopydogObserveRiposte(&r,1,4,0,25034000,1,14140);
    CopydogObserveRiposte(&r,1,4,42032100,6000,0,14150);
    CHECK(CopydogObserveRiposte(&r,1,4,42032100,6000,0,14162));

    /* Only a valid newly submitted request can bypass the old suppression. */
    CopydogRiposteInvalidate(&r,1,42032100,15000);
    CopydogRiposteSubmitted(&r,1,2,42032100,0,15010);
    CHECK(r.suppressed&&!r.haveRequest);
    CopydogRiposteSubmitted(&r,1,3,0,6000,15020);
    CHECK(r.suppressed&&!r.haveRequest);
    CopydogRiposteSubmitted(&r,1,3,-1,0,15030);
    CHECK(r.suppressed&&!r.haveRequest);
    CopydogRiposteSubmitted(&r,1,3,0,0,15040);
    CHECK(!r.suppressed&&r.haveRequest&&!r.pending);
    CopydogObserveRiposte(&r,1,3,42032100,6000,0,15140);
    CHECK(CopydogObserveRiposte(&r,1,3,42032100,6000,0,15152));

    /* Persistent death/damage invalidation cannot age into a new candidate. */
    for(unsigned now=16000;now<=16060;now+=12){
        CopydogRiposteInvalidate(&r,1,0,now);
        CHECK(!CopydogObserveRiposte(&r,1,2,0,25034000,1,now));
        CHECK(r.suppressed&&!r.haveAttack&&!r.pending);
    }
    CopydogRiposteInvalidate(&r,1,0,0xfffffff8u);
    CHECK(!CopydogObserveRiposte(&r,1,0,0,-1,0,4));
    CHECK(!r.suppressed&&!r.haveAttack&&!r.pending);

    CopydogRiposteSubmitted(&r,1,2,0,0,0xffffffe0u);
    CopydogObserveRiposte(&r,1,2,0,6010,0,0xfffffff0u);
    CopydogObserveRiposte(&r,1,2,42032100,6010,0,0xfffffff8u);
    CHECK(CopydogObserveRiposte(&r,1,2,42032100,6010,0,4));

    CHECK(CopydogRiposteRequestCritical(&r,8));
    CHECK(r.criticalState==RIPOSTE_CRITICAL_WAIT&&!r.spent);
    CHECK(!CopydogRiposteRequestCritical(&r,9));
    CHECK(!CopydogObserveRiposte(&r,1,2,42032100,6010,0,20));
    CHECK(!r.spent&&r.criticalState==RIPOSTE_CRITICAL_WAIT);
    CHECK(!CopydogObserveRiposte(&r,1,2,23030201,70030,0,100));
    CHECK(r.spent&&r.criticalState==RIPOSTE_CRITICAL_CONFIRMED&&r.criticalConfirmed==100);

    CopydogRiposteSubmitted(&r,1,2,0,25034000,17000);
    CopydogObserveRiposte(&r,1,2,42032100,6000,0,17100);
    CHECK(CopydogObserveRiposte(&r,1,2,42032100,6000,0,17112));
    CHECK(CopydogRiposteRequestCritical(&r,17120));
    CHECK(!CopydogObserveRiposte(&r,1,2,27034000,6000,0,17300));
    CHECK(!r.spent&&r.criticalState==RIPOSTE_CRITICAL_FAILED&&r.criticalFailure==RIPOSTE_FAIL_NORMAL_ATTACK);
    CHECK(!CopydogRiposteRequestCritical(&r,17312));

    CopydogRiposteSubmitted(&r,1,2,0,25034000,18000);
    CopydogObserveRiposte(&r,1,2,42032100,6000,0,18100);
    CopydogObserveRiposte(&r,1,2,42032100,6000,0,18112);
    CHECK(CopydogRiposteRequestCritical(&r,18120));
    CHECK(!CopydogObserveRiposte(&r,1,2,0,6000,0,18719));
    CHECK(r.criticalState==RIPOSTE_CRITICAL_WAIT&&!r.spent);
    CHECK(!CopydogObserveRiposte(&r,1,2,0,6000,0,18720));
    CHECK(r.criticalState==RIPOSTE_CRITICAL_FAILED&&r.criticalFailure==RIPOSTE_FAIL_TIMEOUT&&!r.spent);
    CHECK(!CopydogRiposteRequestCritical(&r,18730));

    CopydogRiposteSubmitted(&r,1,2,0,25034000,19000);
    CopydogObserveRiposte(&r,1,2,42032100,6000,0,19100);
    CopydogObserveRiposte(&r,1,2,42032100,6000,0,19112);
    CHECK(CopydogRiposteRequestCritical(&r,19120));
    CopydogRiposteInvalidate(&r,1,42032100,19130);
    CHECK(r.criticalState==RIPOSTE_CRITICAL_FAILED&&r.criticalFailure==RIPOSTE_FAIL_INVALIDATED&&!r.spent);
    CHECK(!CopydogObserveRiposte(&r,1,2,23030202,70030,0,19140));
    CHECK(!r.spent&&r.suppressed);
    CHECK(CopydogIsParryRecovery(150036901));
    CHECK(CopydogIsParryRecovery(48036900));
    printf("PASS %u observed clock and riposte assertions\n",checks);
    return 0;
}
