/* pmnp.c - WIN16 fixture (SPEC.md 124.26.12, KERN-MEGA-PLAN 14.143, O8): a
 * PROTECTED-MODE-ONLY program (pmnp.wlk: PROTMODE) whose code lives in six
 * DISCARDABLE segments that are named by their NUMBER - a far-pointer table
 * in its data (segment relocations) and direct far calls between segments.
 * Windows' standard mode discarded such code and loaded it again at the
 * not-present fault; kern_mega's WIN16 does the same (a stable selector, a
 * #NP routed to WIN16, w16_npent). Until O8 it kept all of it.
 *
 * Each of g0..g5 is about 9 KB: g(h) = h * 3 + n + 1 in the case that runs,
 * and a branch that never does (h = BEEFh) holds the straight-line
 * arithmetic that makes the segment big. Then, in order:
 *
 *   1  far call     every segment, twice around (loads and discards);
 *   2  data read    the first four code bytes of every segment through a far
 *                   pointer (a segment register load of a selector that may
 *                   be not present), against the bytes read first of all;
 *   3  mov es       a bare segment register load of the selector, then a read;
 *   4  API argument lstrcpy of a segment's bytes into a buffer, compared with
 *                   the first read: WIN16's own handler faults on the
 *                   pointer, and the segment is loaded on its own stack;
 *   5  a callback   a window procedure that is in a discardable segment: the
 *                   program SendMessage()s to it after the heap has been
 *                   filled, so the callee WIN16 enters is not present;
 *   6  LockSegment  of a selector that may be discarded, then a read;
 *   7  pressure     GlobalAlloc until it refuses, so everything not in use is
 *                   discarded for room; then the blocks are freed and steps
 *                   1-4 run again.
 *
 * It writes PMNP.TXT beside itself, "pmnp: 0 wrong" or the failed tags and
 * the number of far calls made, and then a MessageBox with the same text
 * (the OK the row reads). In real mode (every kernel but kern_mega) there is
 * nothing to check and it says so.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <dos.h>

#define S1(k)   h = (unsigned)((h * 31u + (unsigned)(k)) ^ (h >> 3));
#define S10(k)  S1(k) S1(k+1) S1(k+2) S1(k+3) S1(k+4) \
                S1(k+5) S1(k+6) S1(k+7) S1(k+8) S1(k+9)
#define S100(k) S10(k) S10(k+10) S10(k+20) S10(k+30) S10(k+40) \
                S10(k+50) S10(k+60) S10(k+70) S10(k+80) S10(k+90)
#define S500(k) S100(k) S100(k+100) S100(k+200) S100(k+300) S100(k+400)

#define SEGFN(n, name) \
unsigned __far name(unsigned h) \
{ if (h == 0xBEEF) { S500(n * 500) } return h * 3 + n + 1; }

#pragma code_seg("NP0_TEXT", "NPCODE")
SEGFN(0, g0)
#pragma code_seg("NP1_TEXT", "NPCODE")
SEGFN(1, g1)
#pragma code_seg("NP2_TEXT", "NPCODE")
SEGFN(2, g2)
#pragma code_seg("NP3_TEXT", "NPCODE")
SEGFN(3, g3)
#pragma code_seg("NP4_TEXT", "NPCODE")
SEGFN(4, g4)
/* g5's segment also holds a window procedure, step 5's callee */
#pragma code_seg("NP5_TEXT", "NPCODE")
unsigned __far g5(unsigned h)
{ if (h == 0xBEEF) { S500(2500) } return h * 3 + 5 + 1; }
long FAR PASCAL _export CbProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_USER + 7)
        return (long)wp * 5 + 3;
    return DefWindowProc(w, m, wp, lp);
}
#pragma code_seg("_TEXT", "CODE")

#define N 6
static unsigned (__far *fns[N])(unsigned) = { g0, g1, g2, g3, g4, g5 };
static unsigned long first[N];
static char res[200];
static char path[128];
static int fails;
static unsigned calls;

#pragma aux touch_es = "mov es, ax" parm [ax] modify [es]
void touch_es(unsigned);

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

static void calls_(const char *tag)
{
    int i, r;
    for (r = 0; r < 2; r++)
        for (i = 0; i < N; i++) {
            calls++;
            check(fns[i]((unsigned)i + 7) == (unsigned)(i + 7) * 3 + i + 1, tag);
        }
}

static void reads_(const char *tag)
{
    int i;
    for (i = 0; i < N; i++)
        check(*(unsigned long __far *)fns[i] == first[i], tag);
}

static void es_(const char *tag)
{
    int i;
    unsigned char b;
    for (i = 0; i < N; i++) {
        touch_es(FP_SEG(fns[i]));
        b = *(unsigned char __far *)MK_FP(FP_SEG(fns[i]), FP_OFF(fns[i]));
        check(b == (unsigned char)first[i], tag);
    }
}

static void api_(const char *tag)
{
    int i;
    static char buf[64];
    for (i = 0; i < N; i++) {
        buf[0] = buf[1] = 0;
        lstrcpy(buf, (LPSTR)fns[i]);
        check((unsigned char)buf[0] == (unsigned char)first[i], tag);
    }
}

static HGLOBAL hb[400];
static int nb;

/* GlobalAlloc until it refuses: everything discardable and not in use has
 * gone for room (the code with it) */
static void fill(void)
{
    for (nb = 0; nb < 400; nb++) {
        hb[nb] = GlobalAlloc(GMEM_FIXED, 4000);
        if (!hb[nb])
            break;
    }
}

/* ...and all but KEEP of the blocks given back: the room a load takes */
static void drain(int keep)
{
    while (nb > keep)
        GlobalFree(hb[--nb]);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    int i, most;
    HWND w;
    WNDCLASS wc;
    char *e;
    static char buf[300];

    (void)prev; (void)cmd; (void)show;
    GetModuleFileName(inst, path, sizeof path - 16);
    e = strrchr(path, '\\');
    strcpy(e ? e + 1 : path, "PMNP.TXT");
    if (!(GetWinFlags() & WF_PMODE)) {
        line("pmnp: real mode, nothing to check");
        return 0;
    }
    for (i = 0; i < N; i++)             /* read first of all: the bytes the */
        first[i] = *(unsigned long __far *)fns[i];  /* reloads must restore */
    calls_("call");                     /* 1: loaded, all of them */

    fill();                             /* the heap full, all code gone... */
    most = nb;
    check(nb > 0, "alloc");
    drain(nb - 6);                      /* ...and 24 KB back: two segments at
                                         * a time, each load discards one:
                                         * the segment a register holds is
                                         * in use, so one segment of room
                                         * would be too little (the program
                                         * then ends: Not enough memory for
                                         * its code) */
    calls_("call2");
    reads_("read2");                    /* 2: still tight */
    drain(0);

    fill(); drain(0);                   /* all code gone, all the room back */
    es_("es");                          /* 3 */
    fill(); drain(0);
    api_("api");                        /* 4 */
    fill(); drain(0);
    i = (int)LockSegment(FP_SEG(fns[1]));   /* 6 */
    check(i == (int)FP_SEG(fns[1]), "lock");
    check(*(unsigned long __far *)fns[1] == first[1], "lockread");
    UnlockSegment(FP_SEG(fns[1]));

    /* 5: a window class whose procedure is in segment 7, gone when the
     * message arrives: WIN16 touches the callee before it enters it */
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = (WNDPROC)CbProc;
    wc.hInstance = inst;
    wc.lpszClassName = "PMNP";
    check(RegisterClass(&wc) != 0, "class");
    w = CreateWindow("PMNP", "x", WS_POPUP, 0, 0, 10, 10, NULL, NULL, inst, NULL);
    check(w != 0, "window");
    fill(); drain(0);
    if (w) {
        check(SendMessage(w, WM_USER + 7, 11, 0L) == 58L, "callback");
        DestroyWindow(w);
    }
    fill(); drain(0);
    calls_("call3");                    /* and once more, from nothing */
    reads_("read3");
    es_("es3");
    api_("api3");

    if (fails)
        sprintf(buf, "pmnp:%s (%d wrong, %u calls)", res, fails, calls);
    else
        sprintf(buf, "pmnp: 0 wrong, %u calls, %d blocks", calls, most);
    line(buf);
    MessageBox(NULL, buf, "PMNP", MB_OK);
    return 0;
}
