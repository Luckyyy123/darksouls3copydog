#ifndef COPYDOG_HOOK_LIFETIME_H
#define COPYDOG_HOOK_LIFETIME_H
#include <windows.h>
extern volatile LONG CopydogHookCalls;
LONG CopydogActiveHooks(void);
#ifdef COPYDOG_OFFLINE_TEST
#define COPYDOG_HOOK_SCOPE ((void)0)
#else
static inline void CopydogHookScopeLeave(int* token)
{
    (void)token;
    InterlockedDecrement(&CopydogHookCalls);
}
/* Keep the count held while native calls can return into this DLL. */
#define COPYDOG_HOOK_SCOPE \
    int copydogHookScope __attribute__((cleanup(CopydogHookScopeLeave))) = \
        (int)InterlockedIncrement(&CopydogHookCalls)
#endif
#endif
