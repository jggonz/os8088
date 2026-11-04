/* dosenv.c - WIN16 fixture (SPEC.md 124.25.21, WIN16-NEXT `vbxlib`):
 * GetDOSEnvironment answers a pointer whose OFFSET is 0 and whose selector
 * is readable, as Windows does, to the Windows session's environment.
 * Microsoft Mozart's SPUSH.VBX (a Microsoft C start-up) takes a nonzero
 * offset for a segment and loads it into ES, which faults; MSGBLAST.VBX
 * takes a nonzero offset for "no environment".
 *
 * 1. the answer's offset is 0 and its selector is not;
 * 2. the start-up's own reading, `or ax,ax / jz / mov dx,ax / mov es,dx`,
 *    loads a selector that exists (a bad one would fault right there);
 * 3. the list ends at a double NUL within 2 KB, the word after it is 1;
 * 4. the variables: windir= the Windows directory (GetWindowsDirectory's
 *    answer), TEMP= the same, PATH= naming it, each found by the C
 *    runtime's own getenv() (Open Watcom's reads GetDOSEnvironment);
 * 5. after the count, the program's full path: GetModuleFileName's answer,
 *    ending in DOSENV.EXE;
 * 6. the PSP's word 2Ch (GetCurrentPDB) is the environment's selector;
 * 7. a second call answers the same block.
 *
 * Every check that fails is named in the message box, which
 * tests/win16dosenv.py reads; "DOSENV OK" when none does.
 */
#include <windows.h>
#include <dos.h>
#include <stdlib.h>
#include <string.h>

extern unsigned loades(unsigned long p);
#pragma aux loades = \
    "or ax,ax" "jz L1" "mov dx,ax" "L1:" "mov es,dx" "mov ax,es" \
    parm [dx ax] value [ax] modify [dx es];

static char res[200];
static char win[144];
static char me[144];
static int fails;

static void check(int ok, const char *tag)
{
    if (!ok) {
        fails++;
        strcat(res, " ");
        strcat(res, tag);
    }
}

/* the value of one variable, read by the C runtime: equal to s (or, with
 * sub set, containing it) */
static int var(const char *name, const char *s, int sub)
{
    char *v = getenv(name);

    if (v == NULL)
        return 0;
    return sub ? strstr(v, s) != NULL : stricmp(v, s) == 0;
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    LPSTR e, e2;
    unsigned sel, n, k;
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
    if (n < 2048) {
        check(*(unsigned far *)(p + n + 2) == 1, "count");
        GetModuleFileName(inst, me, sizeof me);
        for (k = 0; k < sizeof me - 1 && p[n + 4 + k] != 0; k++)
            ;
        check(k == strlen(me) && _fmemcmp(p + n + 4, me, k) == 0, "path");
        check(k > 10 && _fstricmp(p + n + 4 + k - 10, "DOSENV.EXE") == 0,
              "pathname");
    }
    GetWindowsDirectory(win, sizeof win);
    check(var("windir", win, 0), "windir");
    check(var("TEMP", win, 0), "temp");
    check(var("PATH", win, 1), "pathvar");
    check(*(unsigned far *)MK_FP(GetCurrentPDB(), 0x2C) == sel, "psp");
    e2 = GetDOSEnvironment();
    check(e2 == e, "same");

    MessageBox(NULL, res[0] ? res : "DOSENV OK", "DOSENV", MB_OK);
    return 0;
}
