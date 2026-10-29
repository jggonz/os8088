/* fpu.c - WIN16 fixture (SPEC.md 124, docs/plans/WIN16-PLAN.md S9): double
 * arithmetic the way Open Watcom compiles it for Windows (-fpi: inline x87
 * with the loader's fix-ups), so S9's emulator has a C-built program beside
 * Calculator's MS C one. 2.01 - 2 is Calculator's own oracle; this program
 * prints it to six places, where Calculator 3.0 and 3.1 both show 0.00.
 */
#include <windows.h>
#include <stdio.h>

static double a = 2.01, b = 2.0;

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    char buf[80];
    double c = a - b, d = a * b, e = a / b;

    sprintf(buf, "2.01-2 = %.6f\n2.01*2 = %.6f\n2.01/2 = %.6f", c, d, e);
    MessageBox(NULL, buf, "WIN16 fpu", MB_OK);
    return 0;
}
