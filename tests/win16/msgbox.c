/* msgbox.c - WIN16 fixture (SPEC.md 124, docs/plans/WIN16-PLAN.md S1/S2):
 * the smallest C program - one message box, then exit. S2's loader is done
 * when this reaches its WinMain on os8088 and the box appears.
 *
 * Built by `make win16fixtures` with Open Watcom (-0 -bt=windows), 8086 code.
 */
#include <windows.h>

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    MessageBox(NULL, "Hello from a Win16 program built by Open Watcom.",
               "WIN16 msgbox", MB_OK | MB_ICONINFORMATION);
    return 0;
}
