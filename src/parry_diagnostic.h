#ifndef COPYDOG_PARRY_DIAGNOSTIC_H
#define COPYDOG_PARRY_DIAGNOSTIC_H

#ifndef COPYDOG_DIAGNOSTIC_DEFAULT
#define COPYDOG_DIAGNOSTIC_DEFAULT 1
#endif
enum {
    CD_DIAG_ENABLED,CD_DIAG_ATTACK,CD_DIAG_NATIVE,CD_DIAG_SELF,CD_DIAG_LOCAL,
    CD_DIAG_TOOL,CD_DIAG_RANGE,CD_DIAG_CONTACT,CD_DIAG_TIMING,CD_DIAG_CURRENT,
    CD_DIAG_TRANSACTION,CD_DIAG_NOT_HIT,CD_DIAG_ONCE,CD_DIAG_QUEUE
};
enum { CD_DIAG_HIT=1,CD_DIAG_UNREADABLE,CD_DIAG_DUPLICATE,CD_DIAG_DISABLED,
       CD_DIAG_QUEUE_FAILED,CD_DIAG_SUBMITTED,CD_DIAG_NOT_PARRYABLE,
       CD_DIAG_TIMING_WAIT,CD_DIAG_TIMING_MISSED,CD_DIAG_TIMING_UNKNOWN };
enum { CD_DIAG_TIMING_PERMISSION=0x100, CD_DIAG_TIMING_ONSET=0x200 };
#define CD_DIAG_FORCED_SOURCE (1u<<28)
typedef struct {
    uintptr_t self,actor;
    int handle,network,weapon,mode,animation,active,spent,readable,neutral,namedAttack;
    uint32_t started,neutralSince;
    CD_FRAME_CLOCK clock;
    CD_FRAME_CLOCK restart;
    CD_NATIVE_FRAME observed[CD_PRED_CLOCK_COUNT];
    unsigned observedMask;
    float beforeRewind;
    uint32_t restartStarted;
    unsigned restartPending,restartAdvances;
} DiagnosticParryEpisode;
#endif
