/* fpx.c - WIN16 fixture (SPEC.md 124.5.3, docs/plans/WIN16-PLAN.md S9): a
 * floating-point EXCEPTION, the way a C program meets one. The runtime gives
 * WIN87EM a handler (__FPMATH 3); an unmasked 1/0 calls it, and it raises
 * SIGFPE, whose handler here longjmps out - so the emulator, and the
 * coprocessor's interrupt, are both seen reaching the program's own code.
 * Then the same division masked, and two values to six places past what a
 * double rounds differently: 1/3 and sqrt(2).
 *
 * "Masked" is the coprocessor's answer only. Watcom's runtime looks for an
 * 8087 with a bare fninit/fnstcw that carries no OS fixup, which nothing can
 * trap on an 8086, so with no coprocessor it finds none and its _control87
 * does nothing: WIN87EM's own 1332h stands, zero divide unmasked, and the
 * second 1/0 raises SIGFPE too. It did on Windows' terms as well; the
 * second setjmp is what makes that a line rather than a loop.
 */
#include <windows.h>
#include <float.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>

static jmp_buf jb;
static volatile int caught;
static double one = 1.0, zero = 0.0, three = 3.0, two = 2.0;

static void fpe(int s)
{
    (void)s;
    caught++;
    _fpreset();
    longjmp(jb, 1);
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    char buf[160];
    volatile double r = 0.0;
    volatile int n;                         /* kept across the longjmp */

    signal(SIGFPE, fpe);
    _control87(0, EM_ZERODIVIDE);           /* 1/0 unmasked */
    if (setjmp(jb) == 0) {
        r = one / zero;
        r = r + one;
    }
    n = sprintf(buf, "1/0 unmasked: %s\n", caught ? "SIGFPE" : "no signal");
    signal(SIGFPE, fpe);
    _control87(MCW_EM, MCW_EM);             /* ...and every exception masked */
    if (setjmp(jb) == 0) {
        r = one / zero;
        n += sprintf(buf + n, "1/0 masked: %s\n", r > 1e308 ? "infinity" : "finite");
    } else
        n += sprintf(buf + n, "1/0 masked: SIGFPE (no 8087 to mask)\n");
    r = one / three;
    n += sprintf(buf + n, "1/3 = %.15f\n", r);
    r = sqrt(two);
    sprintf(buf + n, "sqrt(2) = %.15f", r);
    MessageBox(NULL, buf, "WIN16 fpx", MB_OK);
    return 0;
}
