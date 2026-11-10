/* os8088 - apps/pagedesk/hosttest/pdxgr_test.c - the shading RASTERISER (pdrxgr.c) against the
 * module's output, in one translation unit. Reads a .cases file (tools/os88pdfx_gfx.py --shade),
 * compiles each shading with the real PDXG verbs, paints it into a 64x48 cell through the
 * core's real xg_shade_rows / xg_tri_rows with a lum sink, prints the cell. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../pdxg.c"

#define CW 64
#define CH 48
static int img[CH][CW];
static pdb xg_xc[4096];
static void xg_emit(pdi y, pdi xa, pdi xb, pdw lum, pdw c4)
{
    (void)c4;
    if (y < 0 || y >= CH) return;
    if (xa < 0) xa = 0;
    if (xb > CW - 1) xb = CW - 1;
    for (pdi x = xa; x <= xb; x++) img[y][x] = (int)lum;
}
static void xg_load(pdw off, void *dst, pdw n) { memcpy(dst, xg_xc + off, n); }
#include "../pdrxgr.c"

extern unsigned char pd_host_mem[];
static unsigned char *o_syn[256]; static int o_syn_n[256];
static unsigned char *o_str[256]; static int o_str_n[256];
static int s_cur, s_pos;
pdw pdxgh_fetch(pdw obj, pdw gen, char *buf, pdw cap)
{ (void)gen; if (obj >= 256 || !o_syn[obj]) return 0; int n = o_syn_n[obj] < (int)cap ? o_syn_n[obj] : (int)cap; memcpy(buf, o_syn[obj], n); return (pdw)n; }
pdw pdxgh_sopen(pdw obj, pdw gen) { (void)gen; if (obj >= 256 || !o_str[obj]) return PDE_NOTFOUND; s_cur = obj; s_pos = 0; return PDE_OK; }
pdw pdxgh_sread(pdb *buf, pdw cap) { int left = o_str_n[s_cur] - s_pos, n = left < (int)cap ? left : (int)cap; if (n <= 0) return 0; memcpy(buf, o_str[s_cur] + s_pos, n); s_pos += n; return (pdw)n; }
pdw pdx_engcall(pdw v, pdw b, pdw c, pdw d) { (void)v; (void)b; (void)c; (void)d; return 0; }
static unsigned char *unhex(const char *h, int *n)
{
    if (!h) h = ""; int l = (int)strlen(h) / 2; unsigned char *p = malloc(l + 1); for (int i = 0; i < l; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); p[i] = (unsigned char)v; } p[l] = 0; *n = l; return p; }
static void tofx(struct pd_fx *r, double v) { long n = lround(v * 65536.0); r->hi = (pdi)(n >> 16); r->lo = (pdw)(n & 0xFFFF); }
#define XBSEG 0x2000
#define RSEG  0x4000

int main(int argc, char **argv)
{
    static char line[1 << 16];
    FILE *f = argc > 1 ? fopen(argv[1], "r") : NULL;
    if (!f) return 2;
    struct pdxb_init in; memset(&in, 0, sizeof in);
    pd_fcopy_out(&in, XBSEG, 0, sizeof in);
    pdx_verb(PDV_X_INIT, XBSEG, 0, 0);
    while (fgets(line, sizeof line, f)) {
        char *t = strtok(line, " \n");
        if (!t) continue;
        if (t[0] == 'O' || t[0] == 'S') {
            int num = atoi(strtok(NULL, " \n")), n; unsigned char *p = unhex(strtok(NULL, " \n"), &n);
            if (t[0] == 'O') { o_syn[num] = p; o_syn_n[num] = n; } else { o_str[num] = p; o_str_n[num] = n; }
            continue;
        }
        if (t[0] != 'H') continue;
        char *nm = strtok(NULL, " \n"); int obj = atoi(strtok(NULL, " \n"));
        static pdb big[40000]; pdw state = 0;
        for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) img[y][x] = 255;
        struct pd_fx m[6];
        tofx(&m[0], 1); tofx(&m[1], 0); tofx(&m[2], 0); tofx(&m[3], -1); tofx(&m[4], 0); tofx(&m[5], CH);
        for (;;) {
            struct pdxb_gfx g; memset(&g, 0, sizeof g);
            g.objnum = (pdw)obj; g.rseg = RSEG; g.roff = 0; g.rcap = 30000; g.state = state;
            pd_fcopy_out(&g, XBSEG, 0, sizeof g);
            pdw r = pdx_verb(PDV_GFX_SHADE, XBSEG, 0, 0);
            pd_fcopy_in(XBSEG, 0, &g, sizeof g);
            if (r != PDE_OK && r != PDE_BUSY) { fprintf(stderr, "%s FAILED %u\n", nm, r); break; }
            pd_fcopy_in(RSEG, 0, big, g.rlen);
            struct pdgx_shade *sh = (struct pdgx_shade *)big;
            xg_tcx0 = 0; xg_tcx1 = CW - 1;
            if (sh->type == 2 || sh->type == 3) {
                memcpy(xg_xc + 16, big, PDGX_SHBYTES);
                xg_shade_rows(16, m, 0, CW - 1, 0, CH - 1);
            }
            for (int i = 0; i < g.ntri; i++) {
                pdb *q = big + PDGX_SHBYTES + i * PDGX_TRI_BYTES;
                for (int v = 0; v < 3; v++) {
                    struct pd_fx x, y;
                    x.lo = (pdw)(q[0] | (q[1] << 8)); x.hi = (pdi)(short)(q[2] | (q[3] << 8));
                    y.lo = (pdw)(q[4] | (q[5] << 8)); y.hi = (pdi)(short)(q[6] | (q[7] << 8));
                    double xs = x.hi + x.lo / 65536.0, ys = y.hi + y.lo / 65536.0;
                    xg_tq[2 * v] = (pdi)lround(4 * xs); xg_tq[2 * v + 1] = (pdi)lround(4 * (CH - ys));
                    xg_tl[2 * v] = q[8]; xg_tl[2 * v + 1] = q[9]; q += 10;
                }
                xg_tri_rows(0, CH - 1);
            }
            if (r == PDE_OK) break;
            state = g.state;
        }
        printf("IMG %s\n", nm);
        for (int y = 0; y < CH; y++) { for (int x = 0; x < CW; x++) printf("%d ", img[y][x]); printf("\n"); }
    }
    return 0;
}
