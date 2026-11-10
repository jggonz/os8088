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
        if (t[0] == 'T') {      /* T name resname pageobj: a pattern resource - its parameters, then its tiles */
            char *nm = strtok(NULL, " \n"); char *rn = strtok(NULL, " \n"); int po = atoi(strtok(NULL, " \n"));
            struct pdxb_gfx g; memset(&g, 0, sizeof g);
            pd_fcopy_out(rn, TSEG, 0, strlen(rn) + 1);
            g.objnum = (pdw)po; g.flags = PDGXI_RES | PDGXI_CAT_PAT; g.tseg = TSEG; g.toff = 0; g.tlen = (pdw)(strlen(rn) + 1);
            g.rseg = RSEG; g.roff = 0; g.rcap = 256;
            pd_fcopy_out(&g, XBSEG, 0, sizeof g);
            pdw r = pdx_verb(PDV_GFX_PAT, XBSEG, 0, 0);
            if (r != PDE_OK) { printf("PAT %s FAILED %u\n", nm, r); continue; }
            struct pdgx_pat pe; pd_fcopy_in(RSEG, 0, &pe, sizeof pe);
            printf("PAT %s type %d paint %d obj %u gen %u flags %d bbox %.3f %.3f %.3f %.3f step %.3f %.3f\n", nm, pe.ptype, pe.paint, pe.objnum, pe.gen, pe.flags,
                   fxd(&pe.bbox[0]), fxd(&pe.bbox[1]), fxd(&pe.bbox[2]), fxd(&pe.bbox[3]), fxd(&pe.xstep), fxd(&pe.ystep));
            memset(&g, 0, sizeof g);
            g.cseg = RSEG; g.coff = 0; g.flags = PDGXI_TILES;
            tofx(&g.ctm.a, 4); tofx(&g.ctm.d, -4); tofx(&g.ctm.f, 4 * 48);
            g.clip[0] = 0; g.clip[1] = 0; g.clip[2] = 63; g.clip[3] = 47;
            pd_fcopy_out(&g, XBSEG, 0, sizeof g);
            r = pdx_verb(PDV_GFX_PAT, XBSEG, 0, 0);
            pd_fcopy_in(XBSEG, 0, &g, sizeof g);
            printf("TILES %s r=%u ni %u nj %u i0 %d j0 %d step %.3f %.3f %.3f %.3f ctm %.3f %.3f %.3f %.3f %.3f %.3f\n", nm, r, g.ncomp, g.kind,
                   (int)g.comp[4].hi, (int)g.comp[5].hi, fxd(&g.comp[0]), fxd(&g.comp[1]), fxd(&g.comp[2]), fxd(&g.comp[3]),
                   fxd(&g.ctm.a), fxd(&g.ctm.b), fxd(&g.ctm.c), fxd(&g.ctm.d), fxd(&g.ctm.e), fxd(&g.ctm.f));
            continue;
        }
        if (t[0] == 'H' || t[0] == 'J') {      /* H name objnum [steps]; J name resname pageobj [steps]: a shading in a 64 x 48 cell */
            char *nm = strtok(NULL, " \n"); int obj = 0; char *rn = NULL; int po = 0;
            if (t[0] == 'H') obj = atoi(strtok(NULL, " \n")); else { rn = strtok(NULL, " \n"); po = atoi(strtok(NULL, " \n")); }
            char *sp = strtok(NULL, " \n");
            static pdb big[2048]; pdw state = 0, tot = 0, calls = 0;
            for (;;) {
                struct pdxb_gfx g; memset(&g, 0, sizeof g);
                g.objnum = (pdw)obj; g.rseg = RSEG; g.roff = 0; g.rcap = 760; g.state = state;
                if (rn) { pd_fcopy_out(rn, TSEG, 0, strlen(rn) + 1); g.objnum = (pdw)po; g.flags = PDGXI_RES | PDGXI_CAT_SH; g.tseg = TSEG; g.toff = 0; g.tlen = (pdw)(strlen(rn) + 1); }
                g.steps = sp ? (pdw)atoi(sp) : 0;
                tofx(&g.ctm.a, 4); tofx(&g.ctm.d, -4); tofx(&g.ctm.f, 4 * 48);
                g.clip[0] = 0; g.clip[1] = 0; g.clip[2] = 63; g.clip[3] = 47;
                pd_fcopy_out(&g, XBSEG, 0, sizeof g);
                pdw r = pdx_verb(PDV_GFX_SHADE, XBSEG, 0, 0);
                pd_fcopy_in(XBSEG, 0, &g, sizeof g);
                if (r != PDE_OK && r != PDE_BUSY) { printf("SH %s FAILED %u\n", nm, r); break; }
                pd_fcopy_in(RSEG, 0, big, g.rlen);
                pdb *q = big;
                for (int i = 0; i < g.ntri; i++) {
                    int np = q[4];
                    printf("P %d %d %d %d %d %d", q[0], q[1], q[2], q[3], q[5], np);
                    for (int k = 0; k < np; k++) printf(" %d %d", (short)(q[6 + 4 * k] | (q[7 + 4 * k] << 8)), (short)(q[8 + 4 * k] | (q[9 + 4 * k] << 8)));
                    printf("\n"); tot++;
                    q += 6 + 4 * np;
                }
                calls++;
                if (r == PDE_OK) break;
                state = g.state;
            }
            printf("END %s %u calls %u\n", nm, tot, calls);
            continue;
        }
        if (t[0] != 'C' && t[0] != 'R') continue;
        char *name = strtok(NULL, " \n"); int tn; unsigned char *tx = unhex(strtok(NULL, " \n"), &tn);
        int viares = t[0] == 'R';
        if (viares) {          /* R name hex(resource name): look it up in the page object (3), as the core does */
            unsigned char *rt = malloc(tn + 2); memcpy(rt, tx, tn); rt[tn] = 0; free(tx); tx = rt; tn += 1;
        }
        int nc = atoi(strtok(NULL, " \n")); double c[8] = {0};
        for (int i = 0; i < nc && i < 8; i++) c[i] = atof(strtok(NULL, " \n"));
        struct pdxb_gfx g; memset(&g, 0, sizeof g);
        pd_fcopy_out(tx, TSEG, 0, tn);
        g.tseg = TSEG; g.toff = 0; g.tlen = (pdw)tn; g.rseg = RSEG; g.roff = 0; g.rcap = 4096;
        if (viares) { g.objnum = (pdw)(3000 + atoi((char *)tx + 1)); g.flags = PDGXI_RES | PDGXI_CAT_CS; }
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
            q.cseg = RSEG; q.coff = 0; q.ncomp = (pdw)nc;
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
