/* dgroup.c - WIN16 fixture (SPEC.md 124.9.2, WIN16-NEXT-PLAN): the program's
 * own DGROUP is a block of the global heap, as Visual Basic 3.0's runtime
 * (VBRUN300.100's first call is GlobalHandle(SS)) needs it to be.
 *
 * GlobalHandle(SS) answers a handle, and its segment is SS; GlobalSize says
 * the DGROUP holds at least its stack; GlobalLock answers SS:0 and
 * GlobalUnlock 0; GlobalReAlloc grows it (VBRUN300 asks 8298h bytes), a
 * byte is written and read back at an offset past the stack inside the
 * grown size, and the locals, the stack and the DS data are intact after it;
 * a size past 64 KB is refused with 0.
 *
 * Every check that fails is named in the message box, which
 * tests/win16dgroup.py reads; "DGROUP OK" when none does.
 */
#include <windows.h>
#include <dos.h>
#include <string.h>

extern unsigned getss(void);
#pragma aux getss = "mov ax,ss" value [ax];

static char res[200];
static int keepme = 0x1234;

static void check(int ok, const char *tag)
{
    if (!ok) {
        strcat(res, " ");
        strcat(res, tag);
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    unsigned ss = getss();
    int local = 0x5A5A;
    DWORD h, sz;
    HGLOBAL g2;
    BYTE far *p;
    BYTE saved;

    (void)inst; (void)prev; (void)cmd; (void)show;
    h = GlobalHandle(ss);
    check(LOWORD(h) != 0, "handle");
    check(HIWORD(h) == ss, "seg");

    sz = GlobalSize((HGLOBAL)LOWORD(h));
    check(sz >= 0x2000L, "size");

    p = (BYTE far *)GlobalLock((HGLOBAL)LOWORD(h));
    check(FP_SEG(p) == ss && FP_OFF(p) == 0, "lock");
    check(GlobalUnlock((HGLOBAL)LOWORD(h)) == 0, "unlock");

    g2 = GlobalReAlloc((HGLOBAL)LOWORD(h), 0x8298L, 0);
    check(g2 != 0, "grow");
    check(getss() == ss, "ss");
    check(local == 0x5A5A && keepme == 0x1234, "intact");

    p = (BYTE far *)MK_FP(ss, 0x8000);
    saved = *p;
    *p = 0xA5;
    check(*p == 0xA5, "past");
    *p = saved;
    check(local == 0x5A5A, "stack");

    check(GlobalReAlloc((HGLOBAL)LOWORD(h), 0x20000L, 0) == 0, "toobig");
    check(GlobalSize((HGLOBAL)LOWORD(h)) >= 0x8298L, "sizeafter");

    MessageBox(NULL, res[0] ? res : "DGROUP OK", "DGROUP", MB_OK);
    return 0;
}
