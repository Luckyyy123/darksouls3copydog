#include "../src/auto_trace.h"
#include <stddef.h>
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static struct {
    uint64_t before[4];
    COPYDOG_AUTO_TRACE trace;
    uint64_t after[4];
} storage;

static void FillRecord(uint64_t record[TRACE_WORDS], uint64_t id)
{
    for (unsigned word=0; word<TRACE_WORDS; ++word)
        record[word]=UINT64_C(0x8000000000000000) | (id<<16) | word;
    record[1]=TRACE_START;
    record[2]=id;
}

int main(void)
{
    COPYDOG_AUTO_TRACE *trace=&storage.trace;
    uint64_t record[TRACE_WORDS], saved[TRACE_WORDS];
    const uint64_t guard=UINT64_C(0x123456789abcdef0);

    CHECK(TRACE_CAPACITY==256 && TRACE_WORDS==32);
    CHECK(sizeof(COPYDOG_AUTO_TRACE)==65600);
    CHECK(offsetof(COPYDOG_AUTO_TRACE,entries)==64);
    CHECK(sizeof trace->header==64 && sizeof trace->entries[0]==256);
    CHECK((uintptr_t)&trace->header[4]%8==0);
    CHECK((uintptr_t)&trace->entries[0][0]%8==0);
    CHECK(TRACE_OBSERVE==1 && TRACE_PLAN==2 && TRACE_SUBMIT==3);
    CHECK(TRACE_RETURN==4 && TRACE_CONFIRM==5 && TRACE_CANCEL==6 && TRACE_START==7);

    for(unsigned i=0;i<4;++i)storage.before[i]=storage.after[i]=guard;
    FillRecord(record,1);
    CopydogTraceWrite(trace,record);
    CHECK(trace->header[4]==0 && trace->header[5]==0);
    for(unsigned i=0;i<TRACE_CAPACITY;++i)
        for(unsigned j=0;j<TRACE_WORDS;++j)CHECK(trace->entries[i][j]==0);

    CopydogTraceInit(trace);
    uint64_t firstSession=trace->header[4];
    CHECK(trace->header[0]==UINT64_C(0x4350445452434531));
    CHECK(trace->header[1]==1 && trace->header[2]==256 && trace->header[3]==256);
    CHECK(firstSession!=0 && trace->header[5]==0 && trace->header[6]==0 && trace->header[7]==0);

    for(uint64_t sequence=1;sequence<=TRACE_CAPACITY;++sequence){
        FillRecord(record,sequence);memcpy(saved,record,sizeof saved);
        CopydogTraceWrite(trace,record);
        const uint64_t *entry=trace->entries[sequence-1];
        CHECK(trace->header[4]==firstSession && trace->header[5]==sequence);
        CHECK(entry[0]==sequence && entry[31]==sequence);
        CHECK(memcmp(entry+1,record+1,30*sizeof(uint64_t))==0);
        CHECK(memcmp(record,saved,sizeof saved)==0);
    }
    CHECK(trace->entries[0][0]==1 && trace->entries[255][0]==256);

    FillRecord(record,257);CopydogTraceWrite(trace,record);
    CHECK(trace->header[5]==257 && trace->entries[0][0]==257 && trace->entries[0][31]==257);
    CHECK(trace->entries[0][2]==257);
    CHECK(trace->entries[1][0]==2 && trace->entries[255][0]==256);
    for(uint64_t sequence=258;sequence<=TRACE_CAPACITY*3+17;++sequence){
        FillRecord(record,sequence);CopydogTraceWrite(trace,record);
    }
    const uint64_t last=TRACE_CAPACITY*3+17;
    CHECK(trace->header[5]==last);
    for(uint64_t sequence=last-TRACE_CAPACITY+1;sequence<=last;++sequence){
        const uint64_t *entry=trace->entries[(sequence-1)%TRACE_CAPACITY];
        FillRecord(record,sequence);
        CHECK(entry[0]==sequence && entry[31]==sequence);
        CHECK(memcmp(entry+1,record+1,30*sizeof(uint64_t))==0);
    }
    for(unsigned i=0;i<4;++i)CHECK(storage.before[i]==guard && storage.after[i]==guard);

    Sleep(2);
    CopydogTraceInit(trace);
    CHECK(trace->header[4]!=0 && trace->header[4]!=firstSession);
    CHECK(trace->header[5]==0 && trace->header[6]==0 && trace->header[7]==0);
    for(unsigned i=0;i<TRACE_CAPACITY;++i)
        for(unsigned j=0;j<TRACE_WORDS;++j)CHECK(trace->entries[i][j]==0);
    FillRecord(record,9001);CopydogTraceWrite(trace,record);
    CHECK(trace->header[5]==1 && trace->entries[0][0]==1 && trace->entries[0][31]==1);
    CHECK(trace->entries[0][2]==9001);
    CHECK(trace->entries[1][0]==0);
    for(unsigned i=0;i<4;++i)CHECK(storage.before[i]==guard && storage.after[i]==guard);

    printf("PASS %u auto trace ABI/ring/session assertions; own-process memory only\n",checks);
    return 0;
}
