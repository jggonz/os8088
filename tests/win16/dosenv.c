/* dosenv.c - WIN16 fixture (SPEC.md 124.25.21, WIN16-NEXT `vbxlib`):
 * GetDOSEnvironment answers a pointer whose OFFSET is 0 and whose selector
 * is readable, as Windows does. Microsoft Mozart's SPUSH.VBX (a Microsoft C
 * start-up) takes a nonzero offset for a segment and loads it into ES, which
 * faults; MSGBLAST.VBX takes a nonzero offset for "no environment".
 *
 * 1. the answer's offset is 0 and its selector is not;
 * 2. the start-up's own reading, `or ax,ax / jz / mov dx,ax / mov es,dx`,
 *    loads a selector that exists (a bad one would fault right there);
 * 3. the list ends at a double NUL within 2 KB, the word after it is 1;
 * 4. a second call answers the same block.
 *
 * Every check that fails is named in the message box, which
 * tests/win16dosenv.py reads; "DOSENV OK" when none does.
 */
#include <windows.h>
#include <dos.h>
#include <string.h>

extern unsigned loades(unsigned long p);
#pragma aux loades = \
    "or ax,ax" "jz L1" "mov dx,ax" "L1:" "mov es,dx" "mov ax,es" \
    parm [dx ax] value [ax] modify [dx es];

static char res[200];
static int fails;

static void check(int ok, const char *tag)
{
    if (!ok) {
        fails++;
        strcat(res, " ");
        strcat(res, tag);
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    LPSTR e, e2;
    unsigned sel, n;
    char far *p;

    e = GetDOSEnvironment();
    check(e != NULL, "null");
    check(FP_OFF(e) == 0, "off0");
    sel = FP_SEG(e);
    check(sel != 0, "sel");
    check(loades((unsigned long)e) == sel, "es");
    p = (char far *)e;
    for (n = 0; n < 2048; n++)
        if (p[n] == 0 && p[n + 1] == 0)
            break;
    check(n < 2048, "double nul");
    if (n < 2048)
        check(*(unsigned far *)(p + n + 2) == 1, "count");
    e2 = GetDOSEnvironment();
    check(e2 == e, "same");

    MessageBox(NULL, res[0] ? res : "DOSENV OK", "DOSENV", MB_OK);
    return 0;
}
