#ifndef COPYDOG_ROLL_CATCH_H
#define COPYDOG_ROLL_CATCH_H
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>

enum { RC_NAME_CAP=80, RC_DELAY_MS=30, RC_CIRCLE_MS=110,
       RC_RECOVERY_MS=50, RC_MAX_LATE_MS=150 };
#define RC_RANGE_METRES 3.0f
#define RC_PLAY_ANIMATION_ADDRESS UINT64_C(0x140D90FC0)
#define RC_ATTACK_NAME L"W_AttackBothLight1"
typedef struct {
    uintptr_t actor;
    int network_id;
    char name[RC_NAME_CAP];
    uint32_t due;
    int pending;
} COPYDOG_ROLL_CATCH;

static inline int RcNormalize(const wchar_t *raw,char out[RC_NAME_CAP])
{
    size_t n=0,start=0;
    while(n<RC_NAME_CAP&&raw[n])++n;
    if(n==RC_NAME_CAP)return 0;
    while(start<n&&raw[start]<=127&&isspace((unsigned char)raw[start]))++start;
    while(n>start&&raw[n-1]<=127&&isspace((unsigned char)raw[n-1]))--n;
    if(n-start>=2&&(raw[start]=='W'||raw[start]=='w')&&raw[start+1]=='_')start+=2;
    size_t j=0;
    for(;start<n;++start){
        if(raw[start]>127)return 0;
        out[j++]=(char)tolower((unsigned char)raw[start]);
    }
    out[j]=0;return j!=0;
}
static inline int RcSelfReady(const char *name)
{
    static const char *const ready[]={"idle","move","runstopfront","runstopleft",
        "runstopright","runstopback","dashstop","guardstart","guardon","guardend",
        "dash180","quickturnright180","quickturnleft180"};
    for(unsigned i=0;i<sizeof ready/sizeof *ready;++i)if(!strcmp(name,ready[i]))return 1;
    return 0;
}
static inline unsigned RcDelay(const char *name)
{
    if(!strcmp(name,"circlestepstart"))return RC_CIRCLE_MS;
    return strstr(name,"rolling")||strstr(name,"backstep")||strstr(name,"dash180")?RC_DELAY_MS:0;
}
static inline void RcObserve(COPYDOG_ROLL_CATCH *track,uintptr_t actor,int id,
                             const char *name,int eligible,uint32_t now)
{
    if(!actor||!name||!*name){memset(track,0,sizeof *track);return;}
    if(track->actor!=actor||track->network_id!=id){
        memset(track,0,sizeof *track);track->actor=actor;track->network_id=id;
    }
    if(strcmp(track->name,name)){
        size_t n=strlen(name);
        track->pending=0;
        if(n>=RC_NAME_CAP){track->name[0]=0;return;}
        memcpy(track->name,name,n+1);
        unsigned delay=RcDelay(name);
        if(delay&&eligible){track->due=now+delay;track->pending=1;}
    }
    if(!eligible)track->pending=0;
}
static inline int RcDue(COPYDOG_ROLL_CATCH *track,uint32_t now)
{
    int32_t late=(int32_t)(now-track->due);
    if(track->pending&&late>RC_MAX_LATE_MS)track->pending=0;
    return track->pending&&late>=0;
}
#endif
