/* big.c - WIN16 fixture (SPEC.md 124.20, docs/plans/WIN16-ANY-PLAN.md S15):
 * what Solitaire does not check. Built medium model (big.wcl), so its code is
 * FAR: sixteen functions, each in a code segment of its own (code_seg) and
 * each about 7 KB of straight-line arithmetic, are more than 100 KB of code
 * the loader places a segment at a time and the far calls between them reach.
 * Then a 128 KB block from GlobalAlloc is written and read back through a
 * __huge pointer, whose arithmetic crosses each 64 KB with __AHINCR.
 *
 * The answer goes into a MessageBox, which tests/win16big.py reads off the
 * program's stack and compares with the same sums computed in Python.
 */
#include <windows.h>
#include <stdio.h>
#include <dos.h>

/* one step: h = (h * 31 + k) ^ (h >> 3), in 16 bits */
#define S1(k)   h = (unsigned)((h * 31u + (unsigned)(k)) ^ (h >> 3));
#define S10(k)  S1(k) S1(k+1) S1(k+2) S1(k+3) S1(k+4) \
                S1(k+5) S1(k+6) S1(k+7) S1(k+8) S1(k+9)
#define S100(k) S10(k) S10(k+10) S10(k+20) S10(k+30) S10(k+40) \
                S10(k+50) S10(k+60) S10(k+70) S10(k+80) S10(k+90)
#define S500(k) S100(k) S100(k+100) S100(k+200) S100(k+300) S100(k+400)

#pragma code_seg("BIG00_TEXT", "CODE")
unsigned __far f0(unsigned h) { S500(0) return h; }
#pragma code_seg("BIG01_TEXT", "CODE")
unsigned __far f1(unsigned h) { S500(500) return h; }
#pragma code_seg("BIG02_TEXT", "CODE")
unsigned __far f2(unsigned h) { S500(1000) return h; }
#pragma code_seg("BIG03_TEXT", "CODE")
unsigned __far f3(unsigned h) { S500(1500) return h; }
#pragma code_seg("BIG04_TEXT", "CODE")
unsigned __far f4(unsigned h) { S500(2000) return h; }
#pragma code_seg("BIG05_TEXT", "CODE")
unsigned __far f5(unsigned h) { S500(2500) return h; }
#pragma code_seg("BIG06_TEXT", "CODE")
unsigned __far f6(unsigned h) { S500(3000) return h; }
#pragma code_seg("BIG07_TEXT", "CODE")
unsigned __far f7(unsigned h) { S500(3500) return h; }
#pragma code_seg("BIG08_TEXT", "CODE")
unsigned __far f8(unsigned h) { S500(4000) return h; }
#pragma code_seg("BIG09_TEXT", "CODE")
unsigned __far f9(unsigned h) { S500(4500) return h; }
#pragma code_seg("BIG10_TEXT", "CODE")
unsigned __far f10(unsigned h) { S500(5000) return h; }
#pragma code_seg("BIG11_TEXT", "CODE")
unsigned __far f11(unsigned h) { S500(5500) return h; }
#pragma code_seg("BIG12_TEXT", "CODE")
unsigned __far f12(unsigned h) { S500(6000) return h; }
#pragma code_seg("BIG13_TEXT", "CODE")
unsigned __far f13(unsigned h) { S500(6500) return h; }
#pragma code_seg("BIG14_TEXT", "CODE")
unsigned __far f14(unsigned h) { S500(7000) return h; }
#pragma code_seg("BIG15_TEXT", "CODE")
unsigned __far f15(unsigned h) { S500(7500) return h; }
#pragma code_seg("_TEXT", "CODE")

static unsigned (__far *fns[16])(unsigned) = {
    f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15
};

#define HUGE_N 131072L

/* KERNEL.114, imported by big.wlk: its OFFSET is the value, 1000h in real
 * mode, which a program adds to a segment to step 64 KB (MS C's own idiom) */
extern char __far __AHINCR;
#pragma aux __AHINCR "*"

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    char buf[120];
    unsigned h = 1, sum = 0, bad = 0;
    long i;
    int k;
    HGLOBAL g;
    unsigned char __huge *p;
    unsigned char __far *q;
    unsigned incr;

    for (k = 0; k < 16; k++)            /* forward, then back: every far */
        h = fns[k](h);                  /*   call and return twice */
    for (k = 15; k >= 0; k--)
        h = fns[k](h);

    g = GlobalAlloc(GMEM_FIXED, HUGE_N);
    if (g == 0) {
        sprintf(buf, "big: code %04X; GlobalAlloc(128K) failed", h);
    } else {
        p = (unsigned char __huge *)GlobalLock(g);
        for (i = 0; i < HUGE_N; i++)    /* the compiler's huge arithmetic */
            p[i] = (unsigned char)(i * 7 + (i >> 9));
        incr = (unsigned)(void __near *)&__AHINCR;
        for (i = 0; i < HUGE_N; i++) {  /* read back a 64 KB at a time by */
            q = (unsigned char __far *)MK_FP(FP_SEG(p) + /* __AHINCR */
                    (unsigned)(i >> 16) * incr, (unsigned)i);
            if (*q != (unsigned char)(i * 7 + (i >> 9)))
                bad++;
            sum = (unsigned)((sum * 31u + *q) ^ (sum >> 3));  /* S1's mix */
        }
        GlobalUnlock(g);
        GlobalFree(g);
        sprintf(buf, "big: code %04X\nhuge %ld bytes, sum %04X, %u wrong"
                "\n__AHINCR %04X", h, HUGE_N, sum, bad, incr);
    }
    MessageBox(NULL, buf, "WIN16 big", MB_OK);
    return 0;
}
