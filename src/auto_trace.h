#ifndef COPYDOG_AUTO_TRACE_H
#define COPYDOG_AUTO_TRACE_H
#include <windows.h>
#include <stdint.h>
#include <string.h>
enum { TRACE_OBSERVE=1,TRACE_PLAN,TRACE_SUBMIT,TRACE_RETURN,TRACE_CONFIRM,TRACE_CANCEL,TRACE_START,TRACE_NATIVE_CUE=13,TRACE_AREA_ROLL=14,
       TRACE_DIAGNOSTIC_GATE=15,TRACE_DIAGNOSTIC_FORCED=16 };
enum { TRACE_CAPACITY=256,TRACE_WORDS=32 };
typedef struct { uint64_t header[8],entries[TRACE_CAPACITY][TRACE_WORDS]; } COPYDOG_AUTO_TRACE;
_Static_assert(sizeof(COPYDOG_AUTO_TRACE)==64+256*256,"Trace ABI size");
static inline void CopydogTraceInit(COPYDOG_AUTO_TRACE*t)
{
    LARGE_INTEGER clock;QueryPerformanceCounter(&clock);
    InterlockedExchange64((volatile LONG64*)&t->header[4],0);
    MemoryBarrier();
    memset(t->entries,0,sizeof t->entries);
    t->header[0]=UINT64_C(0x4350445452434531);t->header[1]=1;
    t->header[2]=TRACE_CAPACITY;t->header[3]=TRACE_WORDS*sizeof(uint64_t);
    t->header[5]=t->header[6]=t->header[7]=0;
    MemoryBarrier();
    InterlockedExchange64((volatile LONG64*)&t->header[4],clock.QuadPart?clock.QuadPart:1);
}
/* Single worker writer; readers only use ReadProcessMemory and validate stamps. */
static inline void CopydogTraceWrite(COPYDOG_AUTO_TRACE*t,const uint64_t record[TRACE_WORDS])
{
    if(!t->header[4])return;
    uint64_t sequence=t->header[5]+1;
    uint64_t *entry=t->entries[(sequence-1)%TRACE_CAPACITY];
    InterlockedExchange64((volatile LONG64*)&entry[0],0);
    entry[31]=0;MemoryBarrier();
    memcpy(entry+1,record+1,30*sizeof(uint64_t));
    entry[31]=sequence;MemoryBarrier();
    InterlockedExchange64((volatile LONG64*)&entry[0],(LONG64)sequence);
    InterlockedExchange64((volatile LONG64*)&t->header[5],(LONG64)sequence);
}
#endif
