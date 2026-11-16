#include "runtime.h"
#include <wchar.h>
#include <wctype.h>
#include <stdlib.h>
#include <math.h>
#include <stddef.h>
#include <errno.h>
int ValidCopydogConfig(const COPYDOG_CONFIG*c)
{
    if(!c||c->ParryAhead<0||c->ParryAhead>4096||c->KeyCondition<0||c->KeyCondition>255||
       c->ForestallLatency<0||c->ForestallLatency>10000||c->CopyActionKey<0||c->CopyActionKey>255||
       c->ParryStartup<0||c->ParryStartup>1000||
       (c->BulletDodge!=0&&c->BulletDodge!=1)||(c->HitGuard!=0&&c->HitGuard!=1))return 0;
    const float values[]={c->ForestallDistance,c->ParryDistance,c->RollDistance,c->BackstabDist,c->BackEvadeDist,c->RollAtkDist,c->AtkMult};
    for(unsigned i=0;i<sizeof values/sizeof*values;++i)if(!isfinite(values[i])||values[i]<0||values[i]>100)return 0;
    const float angles[]={c->BackstabRad,c->BackEvadeRad,c->RollAtkRad};
    for(unsigned i=0;i<sizeof angles/sizeof*angles;++i)if(!isfinite(angles[i])||angles[i]<0||angles[i]>6.284f)return 0;
    return isfinite(c->Ass)&&c->Ass>=0&&c->Ass<=1;
}
/* Parse into a temporary object; invalid/unknown/duplicate keys commit nothing. */
int ParseCopydogConfig(const wchar_t*text,COPYDOG_CONFIG*out)
{
    static const struct {const wchar_t*name;size_t offset;int kind;double max;} keys[]={
        {L"parrystartup",offsetof(COPYDOG_CONFIG,ParryStartup),0,1000},
        {L"parryahead",offsetof(COPYDOG_CONFIG,ParryAhead),0,4096},
        {L"parrykey",offsetof(COPYDOG_CONFIG,KeyCondition),0,255},
        {L"fstlatency",offsetof(COPYDOG_CONFIG,ForestallLatency),0,10000},
        {L"fstdist",offsetof(COPYDOG_CONFIG,ForestallDistance),1,100},
        {L"parrydist",offsetof(COPYDOG_CONFIG,ParryDistance),1,100},
        {L"rolldist",offsetof(COPYDOG_CONFIG,RollDistance),1,100},
        {L"backstabrad",offsetof(COPYDOG_CONFIG,BackstabRad),1,6.284},
        {L"backstabdist",offsetof(COPYDOG_CONFIG,BackstabDist),1,100},
        {L"backevaderad",offsetof(COPYDOG_CONFIG,BackEvadeRad),1,6.284},
        {L"backevadedist",offsetof(COPYDOG_CONFIG,BackEvadeDist),1,100},
        {L"rollatkdist",offsetof(COPYDOG_CONFIG,RollAtkDist),1,100},
        {L"rollatkrad",offsetof(COPYDOG_CONFIG,RollAtkRad),1,6.284},
        {L"atkmult",offsetof(COPYDOG_CONFIG,AtkMult),1,100},
        {L"ass",offsetof(COPYDOG_CONFIG,Ass),1,1},
        {L"copyactionkey",offsetof(COPYDOG_CONFIG,CopyActionKey),2,255},
        {L"bulletdodge",offsetof(COPYDOG_CONFIG,BulletDodge),0,1},
        {L"hitguard",offsetof(COPYDOG_CONFIG,HitGuard),0,1},
    };
    COPYDOG_CONFIG tmp={256,0,128,2,2.5,3.5,.8,1.5,.85,2,3,1.2,1,1,'Y',1,1,133};
    unsigned seen=0;
    if(!text||!out)return 0;
    while(*text){
        while(iswspace(*text))++text;
        if(!*text)break;
        const wchar_t*name=text;while(iswalnum(*text)||*text==L'_')++text;
        size_t n=(size_t)(text-name);
        if(!n||*text++!=L'=')return 0;
        wchar_t*end;errno=0;double v=wcstod(text,&end);
        if(end==text||errno||!isfinite(v)||(*end&&!iswspace(*end)))return 0;
        unsigned i;for(i=0;i<sizeof keys/sizeof*keys;++i)
            if(wcslen(keys[i].name)==n&&!wcsncmp(name,keys[i].name,n))break;
        if(i==sizeof keys/sizeof*keys||(seen&(1u<<i))||v<0||v>keys[i].max)return 0;
        void*field=(char*)&tmp+keys[i].offset;
        if(keys[i].kind==1)*(float*)field=(float)v;
        else {if(v!=floor(v))return 0;if(keys[i].kind==2)*(short*)field=(short)v;else *(int*)field=(int)v;}
        seen|=1u<<i;text=end;
    }
    if(!ValidCopydogConfig(&tmp))return 0;
    *out=tmp;return 1;
}
