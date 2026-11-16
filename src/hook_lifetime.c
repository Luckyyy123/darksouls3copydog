#include "hook_lifetime.h"
volatile LONG CopydogHookCalls;
LONG CopydogActiveHooks(void)
{
    return InterlockedCompareExchange(&CopydogHookCalls,0,0);
}
