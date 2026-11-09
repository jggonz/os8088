/* hugega.c - WIN16 fixture (SPEC.md 124.31.3, 50.3.5; docs/plans/KERN-MEGA-PLAN.md
 * 14.169): THE LARGEST GlobalAlloc, held whole and right. On kern_mega WIN16
 * claims an arena past 64 KB through OSAPI_MEM_HUGE, which may BRIDGE the pool
 * above 1 MB and the conventional heap - two runs of memory under one run of
 * selectors - and WIN16's virtual paragraphs, SEG_SUB windows, SEG_TILE huge
 * pointers and linear-base cache all assume ONE run. So WIN16 asks with
 * HUGE_NOBRIDGE, and a block neither region holds alone is refused (0) rather
 * than given across the cut.
 *
 * This asks GlobalAlloc for 960 KB, then 32 KB less each time, until one is
 * granted; writes a pattern word every 256 bytes through a __huge pointer
 * (__AHINCR across every 64 KB, the tiles WIN16 made), reads it all back, and
 * says HUGEGA OK <KB> or HUGEGA BAD <KB> <offset> in HUGEGA.TXT
 * (tests/win16/restxt.h) and a MessageBox. The block stays held until the box
 * is closed, so the row can read the heap with it standing.
 */
#include <windows.h>
#include <stdio.h>
#include <dos.h>
#include "restxt.h"

static char res[80];

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    HGLOBAL g = 0;
    unsigned long kb, n, i, bad = 0xFFFFFFFFUL;
    unsigned __huge *p;

    (void)prev; (void)cmd; (void)show;
    for (kb = 960; kb >= 64; kb -= 32) {
        g = GlobalAlloc(GMEM_FIXED, kb * 1024UL);
        if (g != 0)
            break;
    }
    if (g == 0) {
        sprintf(res, "HUGEGA NONE");
    } else {
        p = (unsigned __huge *)GlobalLock(g);
        n = kb * 1024UL / 256;
        for (i = 0; i < n; i++)
            p[i * 128] = (unsigned)(i ^ (i >> 16) ^ 0x5A00);
        for (i = 0; i < n; i++)
            if (p[i * 128] != (unsigned)(i ^ (i >> 16) ^ 0x5A00)) {
                bad = i * 256;
                break;
            }
        if (bad == 0xFFFFFFFFUL)
            sprintf(res, "HUGEGA OK %lu", kb);
        else
            sprintf(res, "HUGEGA BAD %lu %lX", kb, bad);
    }
    restxt(inst, "HUGEGA.TXT", res);
    MessageBox(0, res, "HUGEGA", MB_OK);
    if (g != 0)
        GlobalFree(g);
    return 0;
}
