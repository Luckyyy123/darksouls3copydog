#include <windows.h>
static LONG registrations;
__declspec(dllexport) DWORD WINAPI BsRegisterDeliveredObserverV1(void*callback)
{
    (void)callback;InterlockedIncrement(&registrations);return ERROR_INVALID_FUNCTION;
}
__declspec(dllexport) DWORD WINAPI FixtureRegistrationCalls(void*unused)
{
    (void)unused;return (DWORD)InterlockedCompareExchange(&registrations,0,0);
}
