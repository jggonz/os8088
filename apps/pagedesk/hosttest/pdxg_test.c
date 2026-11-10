/* os8088 - apps/pagedesk/hosttest/pdxg_test.c - PDXG's host harness (20.22.9).
 * #includes pdxg.c; supplies the object source from a .cases file written by
 * tools/os88pdfx_gfx.py, runs each case through the real verbs and prints the
 * colour: "name r g b" (or "name NONE"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../pdxg.c"

extern unsigned char pd_host_mem[];
#define MAXO 4096
static unsigned char *o_syn[MAXO]; static int o_syn_n[MAXO];
static unsigned char *o_str[MAXO]; static int o_str_n[MAXO];
static int s_cur, s_pos;

pdw pdxgh_fetch(pdw obj, pdw gen, char *buf, pdw cap)
{
    (void)gen;
    if (obj >= MAXO || !o_syn[obj]) return 0;
    int n = o_syn_n[obj] < (int)cap ? o_syn_n[obj] : (int)cap;
    memcpy(buf, o_syn[obj], n);
    return (pdw)n;
}
pdw pdxgh_sopen(pdw obj, pdw gen)
{
    (void)gen;
    if (obj >= MAXO || !o_str[obj]) return PDE_NOTFOUND;
    s_cur = obj; s_pos = 0; return PDE_OK;
}
pdw pdxgh_sread(pdb *buf, pdw cap)
{
    int left = o_str_n[s_cur] - s_pos, n = left < (int)cap ? left : (int)cap;
    if (n <= 0) return 0;
    memcpy(buf, o_str[s_cur] + s_pos, n); s_pos += n; return (pdw)n;
}
pdw pdx_engcall(pdw v, pdw b, pdw c, pdw d) { (void)v; (void)b; (void)c; (void)d; return 0; }

static unsigned char *unhex(const char *h, int *n)
{
    if (!h) h = "";
    int l = (int)strlen(h) / 2; unsigned char *p = malloc(l + 1);
    for (int i = 0; i < l; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); p[i] = (unsigned char)v; }
    p[l] = 0; *n = l; return p;
}
static void tofx(struct pd_fx *r, double v)
{
    long n = lround(v * 65536.0);
    r->hi = (pdi)(n >> 16); r->lo = (pdw)(n & 0xFFFF);
}
static double fxd(const struct pd_fx *f) { return (double)f->hi + f->lo / 65536.0; }
#define XBSEG 0x2000
#define TSEG  0x3000
#define RSEG  0x4000

int main(int argc, char **argv)
{
    static char line[1 << 16];
    FILE *f = argc > 1 ? fopen(argv[1], "r") : NULL;
    if (!f) { fprintf(stderr, "usage: pdxg_test cases\n"); return 2; }
    struct pdxb_init in; memset(&in, 0, sizeof in); in.slot_seg = 0x5000; in.slot_kb = 14;
    pd_fcopy_out(&in, XBSEG, 0, sizeof in);
    pdx_verb(PDV_X_INIT, XBSEG, 0, 0);
    while (fgets(line, sizeof line, f)) {
        char *t = strtok(line, " \n");
        if (!t) continue;
        if (t[0] == 'O' || t[0] == 'S') {
            int num = atoi(strtok(NULL, " \n")), n;
            unsigned char *p = unhex(strtok(NULL, " \n"), &n);
            if (t[0] == 'O') { o_syn[num] = p; o_syn_n[num] = n; } else { o_str[num] = p; o_str_n[num] = n; }
            continue;
        }
        if (t[0] == 'H') {      /* H name objnum: a shading (an object of the table) */
            char *nm = strtok(NULL, " \n"); int obj = atoi(strtok(NULL, " \n"));
            static pdb big[40000]; pdw state = 0, tot = 0; int first = 1;
            for (;;) {
                struct pdxb_gfx g; memset(&g, 0, sizeof g);
                g.objnum = (pdw)obj; g.rseg = RSEG; g.roff = 0; g.rcap = 30000; g.state = state;
                pd_fcopy_out(&g, XBSEG, 0, sizeof g);
                pdw r = pdx_verb(PDV_GFX_SHADE, XBSEG, 0, 0);
                pd_fcopy_in(XBSEG, 0, &g, sizeof g);
                if (r != PDE_OK && r != PDE_BUSY) { printf("SH %s FAILED %u\n", nm, r); break; }
                pd_fcopy_in(RSEG, 0, big, g.rlen);
                struct pdgx_shade *sh = (struct pdgx_shade *)big;
                if (first) {
                    printf("SH %s %d %d %.5f %.5f", nm, sh->type, sh->flags, fxd(&sh->t0), fxd(&sh->t1));
                    for (int i = 0; i < 6; i++) printf(" %.5f", fxd(&sh->coords[i]));
                    printf("\nR");
                    for (int i = 0; i < 256; i++) printf(" %d", sh->ramp[i * 2]);
                    printf("\nRC");
                    for (int i = 0; i < 256; i++) printf(" %d", sh->ramp[i * 2 + 1]);
                    printf("\nB %d", sh->bg_ok ? sh->bg_lum : -1); printf("\n"); first = 0;
                }
                for (int i = 0; i < g.ntri; i++) {
                    pdb *q = big + PDGX_SHBYTES + i * PDGX_TRI_BYTES;
                    printf("T");
                    for (int v = 0; v < 3; v++) {
                        struct pd_fx x, y;
                        x.lo = (pdw)(q[0] | (q[1] << 8)); x.hi = (pdi)(short)(q[2] | (q[3] << 8));
                        y.lo = (pdw)(q[4] | (q[5] << 8)); y.hi = (pdi)(short)(q[6] | (q[7] << 8));
                        printf(" %.4f %.4f %d %d", fxd(&x), fxd(&y), q[8], q[9]); q += 10;
                    }
                    printf("\n"); tot++;
                }
                if (r == PDE_OK) break;
                state = g.state;
            }
            printf("END %s %u\n", nm, tot);
            continue;
        }
        if (t[0] != 'C' && t[0] != 'R') continue;
        char *name = strtok(NULL, " \n"); int tn; unsigned char *tx = unhex(strtok(NULL, " \n"), &tn);
        int viares = t[0] == 'R';
        if (viares) {          /* R name hex(resource name): look it up in the page object (3), as the core does */
            unsigned char *rt = malloc(tn + 16); memcpy(rt, "ColorSpace", 11); memcpy(rt + 11, tx, tn); free(tx); tx = rt; tn += 11;
        }
        int nc = atoi(strtok(NULL, " \n")); double c[8] = {0};
        for (int i = 0; i < nc && i < 8; i++) c[i] = atof(strtok(NULL, " \n"));
        struct pdxb_gfx g; memset(&g, 0, sizeof g);
        pd_fcopy_out(tx, TSEG, 0, tn);
        g.tseg = TSEG; g.toff = 0; g.tlen = (pdw)tn; g.rseg = RSEG; g.roff = 0; g.rcap = 4096;
        if (viares) { g.objnum = (pdw)(3000 + atoi((char *)tx + 12)); g.flags = PDGXI_RES; }
        pd_fcopy_out(&g, XBSEG, 0, sizeof g);
        pdw r = pdx_verb(PDV_GFX_CS, XBSEG, 0, 0);
        if (r != PDE_OK) { printf("%s FAILED %u\n", name, r); continue; }
        struct pdgx_cs cs; pd_fcopy_in(RSEG, 0, &cs, sizeof cs);
        int rgb[3] = {0, 0, 0}, none = 0;
        if (cs.flags & PDGX_F_NONE) none = 1;
        else if (cs.flags & PDGX_F_TABLE) {
            int ix = cs.kind == PDGX_K_INDEXED ? (int)lround(c[0]) : (int)lround(c[0] * 255.0);
            if (ix < 0) ix = 0; if (ix > 255) ix = 255;
            for (int i = 0; i < 3; i++) rgb[i] = cs.rgb[ix * 3 + i];
        } else if (cs.flags & PDGX_F_RGBLIKE) {
            for (int i = 0; i < 3; i++) rgb[i] = (int)lround(c[i] * 255.0);
        } else if (cs.flags & PDGX_F_CMYKLIKE) {
            int a = (int)lround(c[0] * 255), b = (int)lround(c[1] * 255), cc = (int)lround(c[2] * 255), k = (int)lround(c[3] * 255);
            rgb[0] = (255 - a) * (255 - k) / 255; rgb[1] = (255 - b) * (255 - k) / 255; rgb[2] = (255 - cc) * (255 - k) / 255;
        } else {
            struct pdxb_gfx q; memset(&q, 0, sizeof q);
            q.cseg = RSEG; q.coff = 0;
            for (int i = 0; i < nc && i < 8; i++) tofx(&q.comp[i], c[i]);
            pd_fcopy_out(&q, XBSEG, 0, sizeof q);
            if (pdx_verb(PDV_GFX_COLOR, XBSEG, 0, 0) != PDE_OK) { printf("%s FAILED COLOR\n", name); continue; }
            pd_fcopy_in(XBSEG, 0, &q, sizeof q);
            if (q.rgb[3]) none = 1;
            for (int i = 0; i < 3; i++) rgb[i] = q.rgb[i];
        }
        if (none) printf("%s NONE\n", name); else printf("%s %d %d %d\n", name, rgb[0], rgb[1], rgb[2]);
        free(tx);
    }
    return 0;
}
