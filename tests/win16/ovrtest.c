/* ovrtest.c - WIN16 fixture (SPEC.md 124.31.3, docs/plans/WIN16-ANY-PLAN.md
 * S24): a global block's selector on kern_mega is its HANDLE descriptor - it
 * reaches the block's own length, GlobalReAlloc re-aims it in place so the
 * program's selector follows the block, and a write one byte past the block
 * FAULTS, as it did in Windows' standard mode.
 *
 * It writes OVRTEST.TXT beside itself, closing the file after each line so
 * every line is on the disk before the next step:
 *
 *   ovrtest: <the checks that failed, or "0 wrong">   the selector checks
 *   ovrtest: armed <selector> limit <limit>            just before the overrun
 *   ovrtest: SURVIVED the overrun                      only when it did not fault
 *
 * tests/win16ovr.py reads the file: the first line must say 0 wrong, the
 * second must be there and the third must not. In real mode (every kernel
 * but kern_mega) it says so on one line and overruns nothing.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <dos.h>

static char res[256];
static char path[128];
static int fails;

static void check(int ok, const char *tag)
{
    if (!ok) {
        fails++;
        strcat(res, " ");
        strcat(res, tag);
    }
}

static void line(const char *s, int fresh)
{
    HFILE f = fresh ? _lcreat(path, 0) : _lopen(path, OF_WRITE);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0L, 2);
    _lwrite(f, s, strlen(s));
    _lwrite(f, "\r\n", 2);
    _lclose(f);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    HGLOBAL h, h2, pad;
    unsigned char __far *p;
    unsigned char __far *q;
    UINT sel, i;
    DWORD size, lim;
    char *s;

    (void)prev; (void)cmd; (void)show;
    GetModuleFileName(inst, path, sizeof path - 16);
    s = strrchr(path, '\\');
    strcpy(s ? s + 1 : path, "OVRTEST.TXT");
    if (!(GetWinFlags() & WF_PMODE)) {
        line("ovrtest: real mode, nothing to check", 1);
        return 0;
    }

    /* a moveable block of 100 bytes: its selector reaches what GlobalSize
     * answers, and no further */
    h = GlobalAlloc(GMEM_MOVEABLE, 100);
    check(h != 0, "alloc");
    if (!h)
        goto done;
    p = (unsigned char __far *)GlobalLock(h);
    check(p != 0, "lock");
    if (!p)
        goto done;
    sel = FP_SEG(p);
    size = GlobalSize(h);
    check(size >= 100, "size");
    check(GetSelectorLimit(sel) == size - 1, "limit");
    for (i = 0; i < 100; i++)
        p[i] = (unsigned char)(i * 7 + 3);
    GlobalUnlock(h);

    /* a block after it, so a grow cannot stay where it is: the block moves,
     * and the program's selector must follow it, with the new length */
    pad = GlobalAlloc(GMEM_FIXED, 64);
    h2 = GlobalReAlloc(h, 6000, GMEM_MOVEABLE);
    check(h2 != 0, "realloc");
    if (!h2)
        goto done;
    q = (unsigned char __far *)GlobalLock(h2);
    check(q != 0, "lock2");
    if (!q)
        goto done;
    check(FP_SEG(q) == sel, "samesel");
    size = GlobalSize(h2);
    check(size >= 6000, "size2");
    lim = GetSelectorLimit(FP_SEG(q));
    check(lim == size - 1, "limit2");
    for (i = 0; i < 100; i++)
        if (q[i] != (unsigned char)(i * 7 + 3))
            break;
    check(i == 100, "kept");
    q[5999] = 0x5A;
    check(q[5999] == 0x5A, "tail");
    if (pad)
        GlobalFree(pad);

done:
    if (fails)
        sprintf(res + strlen(res), " (%d wrong)", fails);
    else
        strcpy(res, " 0 wrong");
    {
        static char buf[300];
        sprintf(buf, "ovrtest:%s", res);
        line(buf, 1);
        if (!fails) {
            sprintf(buf, "ovrtest: armed %04X limit %04lX", FP_SEG(q), lim);
            line(buf, 0);
            q[(UINT)size] = 1;              /* one byte past the block */
            line("ovrtest: SURVIVED the overrun", 0);
        }
    }
    return 0;
}
