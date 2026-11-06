/* os8088 - apps/pagedesk/hosttest/pde_srch.c
 * PDENG's SEARCH (PAGEDESK-SPEC 20.10.1) over a synthetic glyph stream.
 *
 *   pde_srch DOC.PDF PIECE NEEDLE TEXT [SKIP]
 *
 * TEXT is laid out like a page: every byte is one glyph advancing 6 pt on a
 * line, '/' starts the next line (a baseline 12 pt lower), '|' is a 4 pt gap
 * with no glyph (a TJ kern that stands for a word space), '~' a 0.4 pt kern
 * (no gap). PIECE is how many records go in one SEARCH call (1..74) - the
 * hit list must not depend on it, which is what the matcher carrying its state
 * across pieces means. SKIP is the first run searched (Find next).
 * Prints "H first last" for each hit, in order, with runs counting glyphs.
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
    struct pdxb_search sr;
    static struct pdtm_glyph g[4096];
    int n = 0, i, k, piece, at, skip = 0, x = 0, y = 800 * 16, run = 0;
    const char *t;
    unsigned r;

    if (argc < 5) { fprintf(stderr, "usage\n"); return 2; }
    if (pdh_load(argv[1])) { fprintf(stderr, "no file\n"); return 2; }
    piece = atoi(argv[2]);
    if (argc > 5) skip = atoi(argv[5]);
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
    for (t = argv[4]; *t && n < 4096; t++) {
        if (*t == '/') { y -= 12 * 16; x = 0; continue; }
        if (*t == '|') { x += 4 * 16; continue; }
        if (*t == '~') { x += 6; continue; }
        memset(&g[n], 0, sizeof g[n]);
        g[n].code = (unsigned char)*t;
        g[n].x0 = (pd_pt)x; g[n].x1 = (pd_pt)(x + 6 * 16);
        g[n].y0 = (pd_pt)(y - 3 * 16); g[n].y1 = (pd_pt)(y + 9 * 16);
        g[n].run = (pdw)run++;
        x += 6 * 16;
        n++;
    }
    at = 0;
    {
        int state = 0, glyph = skip;
        while (at < n) {
            int cnt = n - at < piece ? n - at : piece;
            pd_fcopy_out(&g[at], SEG_XB, 128, (pdw)(cnt * 12));
            for (;;) {
                memset(&sr, 0, sizeof sr);
                strncpy(sr.needle, argv[3], 63);
                sr.state = (pdw)state; sr.glyph = (pdw)glyph; sr.found = (pdw)cnt;
                pd_fcopy_out(&sr, SEG_XB, 0, sizeof sr);
                r = call(PDV_ENG_SEARCH, 0, 0, 0);
                if (cf) { printf("SEARCH rc=%u\n", r); return 1; }
                pd_fcopy_in(SEG_XB, 0, &sr, sizeof sr);
                state = 1;
                if (!sr.found) break;
                printf("H %u %u\n", sr.glyph, sr.glyph_end);
                glyph = sr.glyph_end + 1;
            }
            at += cnt;
        }
    }
    (void)i; (void)k;
    return 0;
}
