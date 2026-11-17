/* os8088 - apps/pagedesk/hosttest/pde_link.c
 * PDENG's links (PAGEDESK-SPEC 20.10.2): for every page, PDP_HASLINKS and
 * every /Link annotation's destination, action name and URI.
 *   pde_link DOC.PDF   ->   "P 3 haslinks=1 nannots=9" / "L 3 0 rect=... page+1=4 nm=GoTo uri="
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pdmath.h"

unsigned pde_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx);
extern unsigned pde_fail;
extern int pdh_load(const char *path);
#define SEG_XB    0x2000
#define SEG_CACHE 0x3000
#define SEG_ARENA 0x4000
#define SEG_TBL   0x5000
static int cf;
static unsigned call(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
{
    unsigned r = pde_verb(verb, bx, cx, dx);
    cf = pde_fail != 0;
    return r;
}
int main(int argc, char **argv)
{
    struct pdxb_init in;
    struct pdxb_open op;
    struct pdxb_pageinfo pi;
    struct pdxb_annot an;
    unsigned r, p, k;

    if (argc < 2 || pdh_load(argv[1])) return 2;
    memset(&in, 0, sizeof in);
    in.xb_seg = SEG_XB; in.cache_seg = SEG_CACHE; in.cache_kb = 16;
    in.arena_seg = SEG_ARENA; in.arena_kb = 8; in.tables_seg = SEG_TBL; in.tables_kb = 11;
    pd_fcopy_out(&in, SEG_XB, 0, sizeof in);
    call(PDV_ENG_INIT, SEG_XB, 0, 0);
    memset(&op, 0, sizeof op);
    strcpy(op.name, "DOC.PDF");
    for (;;) {
        pd_fcopy_out(&op, SEG_XB, 0, sizeof op);
        r = call(PDV_ENG_OPEN, 0, 0, 0);
        pd_fcopy_in(SEG_XB, 0, &op, sizeof op);
        if (!cf && r == PDE_BUSY) continue;
        break;
    }
    if (cf) { printf("OPEN %u\n", r); return 1; }
    for (p = 0; p < op.npages; p++) {
        r = call(PDV_ENG_PAGEINFO, p, 0, 0);
        if (cf) { printf("PAGEINFO %u rc=%u\n", p, r); continue; }
        pd_fcopy_in(SEG_XB, 0, &pi, sizeof pi);
        printf("P %u haslinks=%d nannots=%u\n", p, (pi.flags & PDP_HASLINKS) != 0, pi.nannots);
        for (k = 0; k < pi.nannots; k++) {
            r = call(PDV_ENG_ANNOTS, p, k, 0);
            if (cf) { printf("ANNOTS %u %u rc=%u\n", p, k, r); continue; }
            pd_fcopy_in(SEG_XB, 0, &an, sizeof an);
            if (strcmp(an.subtype, "Link")) continue;
            printf("L %u %u rect=%d,%d,%d,%d page+1=%u nm=%s uri=%.*s\n", p, k, an.rect.x0, an.rect.y0,
                   an.rect.x1, an.rect.y1, an.state, an.nm, (int)(an.contents_len < 159 ? an.contents_len : 159), an.contents);
        }
    }
    return 0;
}
