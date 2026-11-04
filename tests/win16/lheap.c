/* lheap.c - WIN16 fixture (SPEC.md 124.25.19, WIN16-NEXT `lheap`): a local
 * heap grows when it is full, as Windows' did. Visual Basic 3.0's runtime
 * makes a small heap of its own (4 KB) in its DGROUP and allocates far more
 * there.
 *
 * 1. The program's DGROUP: LocalInit makes a heap of 1 KB (over part of what
 *    the load gave), then 100 blocks of 100 bytes are LocalAlloc'ed: each
 *    answer is nonzero and distinct, each block is filled with its own byte
 *    and read back after the last allocation. A moveable block is made
 *    larger by LocalReAlloc and keeps what it held; LocalCompact answers.
 * 2. A GlobalAlloc'ed, locked block of 2 KB gets a heap of 496 bytes by
 *    LocalInit(seg, 16, 512), and 60 blocks of 100 bytes are made in it by a
 *    LocalAlloc with DS set to that segment (first into the block's own tail,
 *    then, past 2 KB, by growing the block). Distinct, filled, read back; the
 *    block is then larger than it was.
 * 3. The same in a block whose neighbour is in use, so it cannot grow in
 *    place: on kern_mega (GetWinFlags WF_PMODE) it MOVES, its selector
 *    following it, and all 60 blocks must be had and keep their data at the
 *    same selector; in real mode nothing moves, so the heap stops short and
 *    what it did hand out must be intact.
 * 4. 70 window classes are registered, past the class block's first 64
 *    records (SPEC.md 124.25.18.1): every atom nonzero and distinct, and the
 *    first and the last found by GetClassInfo by name and by atom.
 *
 * Every check that fails is named in the message box, which
 * tests/win16lheap.py reads; "LHEAP OK" when none does.
 */
#include <windows.h>
#include <dos.h>
#include <string.h>

extern unsigned getds(void);
#pragma aux getds = "mov ax,ds" value [ax];
extern void setds(unsigned s);
#pragma aux setds = "mov ds,ax" parm [ax];

static char res[200];
static unsigned ptrs[100];
static ATOM atoms[70];
static char cname[8] = "LHC00";
static WNDCLASS wc, wq;

static void check(int ok, const char *tag)
{
    if (!ok) {
        strcat(res, " ");
        strcat(res, tag);
    }
}

/* LocalAlloc(LMEM_FIXED, n) with DS = seg; locals only while DS is moved */
static unsigned lalloc_in(unsigned seg, unsigned n)
{
    unsigned save = getds();
    unsigned r;

    setds(seg);
    r = (unsigned)LocalAlloc(LMEM_FIXED, n);
    setds(save);
    return r;
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    unsigned i, j, n, start, r, seg;
    int pm;
    HGLOBAL g2;
    HLOCAL h, h2;
    char *q;
    char far *f;
    HGLOBAL g;
    DWORD size0, size1;
    int ok;

    (void)prev; (void)cmd; (void)show;

    /* 1. the program's DGROUP, a heap of 1 KB */
    start = *(unsigned *)6;
    check(start != 0, "pLocalHeap");
    check(LocalInit(0, start, start + 1024) != 0, "init");
    ok = 1;
    for (i = 0; i < 100; i++) {
        r = (unsigned)LocalAlloc(LMEM_FIXED, 100);
        if (r == 0) { ok = 0; break; }
        for (j = 0; j < i; j++)
            if (ptrs[j] == r) ok = 0;
        ptrs[i] = r;
        memset((char *)r, (int)(i + 1), 100);
    }
    check(ok, "alloc");
    for (i = 0; ok && i < 100; i++) {
        q = (char *)ptrs[i];
        for (j = 0; j < 100; j++)
            if (q[j] != (char)(i + 1)) { ok = 0; break; }
    }
    check(ok, "data");

    h = LocalAlloc(LMEM_MOVEABLE, 50);
    check(h != 0, "mov");
    if (h) {
        q = (char *)LocalLock(h);
        memset(q, 0x77, 50);
        LocalUnlock(h);
        h2 = LocalReAlloc(h, 3000, LMEM_MOVEABLE);
        check(h2 != 0, "realloc");
        if (h2) {
            q = (char *)LocalLock(h2);
            ok = 1;
            for (j = 0; j < 50; j++)
                if (q[j] != 0x77) ok = 0;
            check(ok, "kept");
            q[2999] = 1;
            LocalUnlock(h2);
            check(LocalSize(h2) >= 3000, "lsize");
        }
    }
    check(LocalCompact(0) != 0, "compact");

    /* 2. a locked global block with a heap of its own */
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 2048L);
    check(g != 0, "galloc");
    if (g) {
        f = (char far *)GlobalLock(g);
        seg = FP_SEG(f);
        size0 = GlobalSize(g);
        check(LocalInit(seg, 16, 512) != 0, "init2");
        ok = 1;
        for (i = 0; i < 60; i++) {
            r = lalloc_in(seg, 100);
            if (r == 0) { ok = 0; break; }
            for (j = 0; j < i; j++)
                if (ptrs[j] == r) ok = 0;
            ptrs[i] = r;
            _fmemset(MK_FP(seg, r), (int)(i + 1), 100);
        }
        check(ok, "alloc2");
        for (i = 0; ok && i < 60; i++) {
            f = (char far *)MK_FP(seg, ptrs[i]);
            for (j = 0; j < 100; j++)
                if (f[j] != (char)(i + 1)) { ok = 0; break; }
        }
        check(ok, "data2");
        size1 = GlobalSize(g);
        check(size1 > size0, "grew");
        GlobalUnlock(g);
        GlobalFree(g);
    }

    /* 3. a block that must move to grow (kern_mega) */
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 2048L);
    g2 = GlobalAlloc(GMEM_MOVEABLE, 2048L);     /* its neighbour, in use */
    check(g != 0 && g2 != 0, "galloc3");
    if (g && g2) {
        pm = (GetWinFlags() & WF_PMODE) != 0;
        f = (char far *)GlobalLock(g);
        seg = FP_SEG(f);
        check(LocalInit(seg, 16, 2000) != 0, "init3");
        for (n = 0; n < 60; n++) {
            r = lalloc_in(seg, 100);
            if (r == 0)
                break;
            ptrs[n] = r;
            _fmemset(MK_FP(seg, r), (int)(n + 1), 100);
        }
        check(!pm || n == 60, "move");
        ok = 1;
        for (i = 0; i < n; i++) {
            f = (char far *)MK_FP(seg, ptrs[i]);
            for (j = 0; j < 100; j++)
                if (f[j] != (char)(i + 1)) { ok = 0; break; }
        }
        check(ok, "data3");
        f = (char far *)GlobalLock(g);
        check(FP_SEG(f) == seg, "sel3");
        GlobalUnlock(g);
        GlobalUnlock(g);
    }
    if (g)
        GlobalFree(g);
    if (g2)
        GlobalFree(g2);

    /* 4. classes past the class block's first 64 */
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = inst;
    wc.lpszClassName = cname;
    ok = 1;
    for (i = 0; i < 70; i++) {
        cname[3] = (char)('0' + i / 10);
        cname[4] = (char)('0' + i % 10);
        wc.style = i;
        atoms[i] = RegisterClass(&wc);
        if (atoms[i] == 0) { ok = 0; break; }
        for (j = 0; j < i; j++)
            if (atoms[j] == atoms[i]) ok = 0;
    }
    check(ok, "classes");
    if (ok) {
        check(GetClassInfo(inst, "LHC00", &wq) && wq.style == 0, "cls0");
        check(GetClassInfo(inst, "LHC69", &wq) && wq.style == 69, "cls69");
        check(GetClassInfo(inst, (LPCSTR)MAKELP(0, atoms[69]), &wq)
              && wq.style == 69, "atom69");
    }

    MessageBox(NULL, res[0] ? res : "LHEAP OK", "LHEAP", MB_OK);
    return 0;
}
