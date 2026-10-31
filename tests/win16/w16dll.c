/* w16dll.c - WIN16 fixture (SPEC.md 124.23, docs/plans/WIN16-ANY-PLAN.md
 * S18): a library with a counter in its own DGROUP. DLLUSE.EXE imports it
 * by name and by ordinal, and W16DLL2.DLL imports it too, so one counter
 * answering both says the library has one DGROUP, as on real-mode Windows.
 * Its exports and their ordinals are tests/win16/w16dll.wlk's.
 */
#include <windows.h>

static int counter;
static int started;

int FAR PASCAL LibMain(HINSTANCE inst, WORD dseg, WORD heap, LPSTR cmd)
{
    started = (inst != 0 && dseg != 0) ? 0x5A : 1;
    return 1;
}

int FAR PASCAL WEP(int type)
{
    return 1;
}

int FAR PASCAL DllBump(int by)
{
    counter += by;
    return counter;
}

int FAR PASCAL DllCount(void)
{
    return counter;
}

int FAR PASCAL DllStarted(void)
{
    return started;
}
