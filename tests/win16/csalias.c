/* csalias.c - WIN16 fixture (SPEC.md 124.31.6, KERN-MEGA-PLAN 14.142): a
 * program that builds code in a global block and calls it, as Arcade's
 * ASTEROID does. A data selector is not code: AllocDStoCSAlias and
 * PrestoChangoSelector must hand back a REAL code selector over the same
 * bytes, so a far call through it runs (on kern_mega a far call through a
 * data selector is a #GP that closes WIN16).
 *
 * The code is  mov ax, 1234h / retf  (B8 34 12 CB). The checks:
 *   1. AllocDStoCSAlias(block) is a selector with the block's base, and a
 *      far call through it returns 1234h
 *   2. FreeSelector gives it back
 *   3. AllocSelector + PrestoChangoSelector(block, new) makes a code
 *      selector, and a far call through it returns 1234h
 *   4. PrestoChangoSelector(new, new) flips it back to DATA in place: the
 *      bytes read back through it
 *   5. (ASTEROID's) PrestoChangoSelector(blk, blk) flips the BLOCK's own
 *      selector to code in place: a far call through it returns 1234h, and a
 *      second one makes it data again
 *   6. PrestoChangoSelector onto a selector the program does not own (its own
 *      CS) is refused (0), and the program carries on running afterwards
 *
 * It writes CSALIAS.TXT beside itself: "csalias: 0 wrong" or the failed tags.
 * In real mode a selector is a segment and every alias is the segment itself.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <dos.h>

UINT WINAPI AllocCStoDSAlias(UINT);
UINT WINAPI AllocDStoCSAlias(UINT);

typedef unsigned (__far *FN)(void);

static char res[256];
static char path[128];
static int fails;
static const unsigned char code[4] = { 0xB8, 0x34, 0x12, 0xCB };

static void check(int ok, const char *tag)
{
    if (!ok) {
        fails++;
        strcat(res, " ");
        strcat(res, tag);
    }
}

static void line(const char *s)
{
    HFILE f = _lcreat(path, 0);
    if (f == HFILE_ERROR)
        return;
    _lwrite(f, s, strlen(s));
    _lwrite(f, "\r\n", 2);
    _lclose(f);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    HGLOBAL h;
    unsigned char __far *p;
    UINT blk, a, s, cs;
    FN f;
    static char buf[300];
    char *e;

    (void)prev; (void)cmd; (void)show;
    GetModuleFileName(inst, path, sizeof path - 16);
    e = strrchr(path, '\\');
    strcpy(e ? e + 1 : path, "CSALIAS.TXT");
    if (!(GetWinFlags() & WF_PMODE)) {
        line("csalias: real mode, nothing to check");
        return 0;
    }
    cs = FP_SEG((void (__far *)(void))WinMain);

    h = GlobalAlloc(GMEM_MOVEABLE, 64);
    check(h != 0, "alloc");
    if (!h)
        goto done;
    p = (unsigned char __far *)GlobalLock(h);
    check(p != 0, "lock");
    if (!p)
        goto done;
    blk = FP_SEG(p);
    _fmemcpy(p, code, sizeof code);

    a = AllocDStoCSAlias(blk);
    check(a != 0, "dscs");
    if (a) {
        check(GetSelectorBase(a) == GetSelectorBase(blk), "dscsbase");
        f = (FN)MK_FP(a, FP_OFF(p));
        check(f() == 0x1234, "dscscall");
        FreeSelector(a);
    }

    s = AllocSelector(blk);
    check(s != 0, "alloc2");
    if (s) {
        check(PrestoChangoSelector(blk, s) == s, "presto");
        f = (FN)MK_FP(s, FP_OFF(p));
        check(f() == 0x1234, "prestocall");
        PrestoChangoSelector(s, s);         /* code back to data, in place */
        check(*(unsigned char __far *)MK_FP(s, 0) == 0xB8, "back");
        FreeSelector(s);
    }

    /* ASTEROID's idiom: the block's OWN selector flipped in place, called,
     * and flipped back */
    if (PrestoChangoSelector(blk, blk) == blk) {
        f = (FN)MK_FP(blk, FP_OFF(p));
        check(f() == 0x1234, "inplacecall");
        PrestoChangoSelector(blk, blk);
        check(*(unsigned char __far *)MK_FP(blk, 0) == 0xB8, "inplaceback");
    } else
        check(0, "inplace");

    /* a selector the program does not own is not re-aimed or retyped */
    check(PrestoChangoSelector(blk, cs) == 0, "refuse");
    GlobalUnlock(h);
    GlobalFree(h);

done:
    if (fails)
        sprintf(buf, "csalias:%s (%d wrong)", res, fails);
    else
        strcpy(buf, "csalias: 0 wrong");
    line(buf);
    return 0;
}
