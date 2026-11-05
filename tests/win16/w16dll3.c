/* w16dll3.c - WIN16 fixture (SPEC.md 124.25.22): a library found only by a
 * FULL PATH. The row win16dll puts it in a LIBS\ folder below DLLUSE.EXE's
 * own, where no plain name is looked for, so LoadLibrary("W16DLL3.DLL")
 * answers 2 and LoadLibrary("X:\...\LIBS\W16DLL3.DLL") loads it.
 */
#include <windows.h>

int FAR PASCAL LibMain(HINSTANCE inst, WORD dseg, WORD heap, LPSTR cmd)
{
    return 1;
}

int FAR PASCAL WEP(int type)
{
    return 1;
}

int FAR PASCAL Dll3Magic(void)
{
    return 0x3D3D;
}
