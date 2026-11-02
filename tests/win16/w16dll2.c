/* w16dll2.c - WIN16 fixture (SPEC.md 124.23, S18): a library that imports
 * W16DLL.DLL. DLLUSE.EXE loads it with LoadLibrary, calls Dll2Bump - which
 * raises W16DLL's counter by 100 - and frees it, and its WEP raises the
 * counter by 1000, which is how the program sees that WEP ran.
 */
#include <windows.h>

int FAR PASCAL DllBump(int by);

int FAR PASCAL LibMain(HINSTANCE inst, WORD dseg, WORD heap, LPSTR cmd)
{
    return 1;
}

int FAR PASCAL WEP(int type)
{
    if (type == 0)
        DllBump(1000);
    return 1;
}

int FAR PASCAL Dll2Bump(void)
{
    return DllBump(100);
}
