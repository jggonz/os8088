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
 * Before the overrun it grows a 40 KB moveable block to 80 KB with
 * GlobalReAlloc and reads the whole of it back through __AHINCR: the 40 KB
 * written before the grow and the 40 KB after it, past 64 KB (WIN16-NEXT-PLAN
 * N1: a grown block's selector that reached only its first 64 KB). A wrong
 * byte is the check "grow".
 *
 * tests/win16ovr.py reads the file: the first line must say 0 wrong, the
 * second must be there and the third must not. In real mode (every kernel
 * but kern_mega) it says so on one line and overruns nothing.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <dos.h>

/* KERNEL.114, imported by ovrtest.wlk: its OFFSET is the value, the
 * selector step to the next 64 KB of a block */
extern char __far __AHINCR;
#pragma aux __AHINCR "*"

#define GROW_A 40960L
#define GROW_N 81920L
#define GROW_B(i) ((unsigned char)((i) * 5 + ((i) >> 8)))

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

/* the grow: a 40 KB block made 80 KB, every byte read back across 64 KB */
static void grow(void)
{
    HGLOBAL g, g2;
    unsigned char __huge *p;
    unsigned char __far *b;
    unsigned incr = (unsigned)(void __near *)&__AHINCR;
    long i;
    unsigned bad = 0;

    g = GlobalAlloc(GMEM_MOVEABLE, GROW_A);
    check(g != 0, "growalloc");
    if (!g)
        return;
    p = (unsigned char __huge *)GlobalLock(g);
    for (i = 0; i < GROW_A; i++)
        p[i] = GROW_B(i);
    GlobalUnlock(g);
    g2 = GlobalReAlloc(g, GROW_N, GMEM_MOVEABLE);
    check(g2 != 0, "growrealloc");
    if (!g2) {
        GlobalFree(g);
        return;
    }
    check(GlobalSize(g2) >= GROW_N, "growsize");
    p = (unsigned char __huge *)GlobalLock(g2);
    for (i = GROW_A; i < GROW_N; i++)
        p[i] = GROW_B(i);
    for (i = 0; i < GROW_N; i++) {
        b = (unsigned char __far *)MK_FP(FP_SEG(p) +
                (unsigned)(i >> 16) * incr, (unsigned)i);
        if (*b != GROW_B(i))
            bad++;
    }
    check(bad == 0, "grow");
    GlobalUnlock(g2);
    GlobalFree(g2);
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
    grow();

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
