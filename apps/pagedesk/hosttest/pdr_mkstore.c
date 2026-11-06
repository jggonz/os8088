/* ============================================================================
 * os8088 - apps/pagedesk/hosttest/pdr_mkstore.c   an edit store and the file PDWRT saves from it
 *
 * The harness side of PAGEDESK-SPEC 20.12's proof: one store built through the
 * edit store's own layout (13.1.1) and the REAL writer (pdwrt.c over the real
 * pdeng.c, as hosttest/pdw_real.c does), so the same marks exist twice - as the
 * records PDREND draws from, and as the annotations PDWRT wrote, which mutool
 * renders independently. A scenario names the marks:
 *
 *   pdr_mkstore <source.pdf> <scenario> <outdir>
 *     writes <outdir>/store.bin (the store segment, 0x8000 bytes: records at 0, text heap at 0x3000,
 *     points at 0x5000), <outdir>/store.cnt (the record count) and <outdir>/R.PDF (the incremental save)
 *
 *   all     every kind on page 0 of MARKUP.PDF
 *   replace ANNOTS256.PDF page 0: one source annotation deleted, one replaced and moved
 *   hostile records no writer would write (bad offsets, huge counts, inverted rects): store only, no PDF
 * ==========================================================================*/
#include "../pdwrt.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned pde_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx);
extern unsigned pde_fail;
extern int pdh_load(const char *path);
extern int pdh_cluster;

#define SEG_XB    0x2000
#define SEG_CACHE 0x3000
#define SEG_ARENA 0x4000
#define SEG_TBL   0x5000
#define SEG_WBUF  0x7000
#define SEG_EDS   0x8000
static int fails;

pdw pdw_engcall(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdw r = (pdw)pde_verb(verb, bx, cx, dx);
    pdw_engcf = pde_fail != 0;
    return r;
}
static int eng_open(const char *file)
{
    struct pdxb_init in; struct pdxb_open op; unsigned r; int n = 0;
    if (pdh_load(file)) return -1;
    memset(&in, 0, sizeof in);
    in.xb_seg = SEG_XB; in.cache_seg = SEG_CACHE; in.cache_kb = 16;
    in.arena_seg = SEG_ARENA; in.arena_kb = 8; in.tables_seg = SEG_TBL; in.tables_kb = 11;
    pd_fcopy_out(&in, SEG_XB, 0, sizeof in);
    pde_verb(PDV_ENG_INIT, SEG_XB, 0, 0);
    if (pde_fail) return (int)pde_fail;
    memset(&op, 0, sizeof op); strcpy(op.name, "DOC.PDF");
    for (;;) {
        pd_fcopy_out(&op, SEG_XB, 0, sizeof op);
        r = pde_verb(PDV_ENG_OPEN, 0, 0, 0);
        pd_fcopy_in(SEG_XB, 0, &op, sizeof op);
        if (!pde_fail && r == PDE_BUSY) { if (++n > 5000) return 999; continue; }
        return pde_fail ? (int)r : 0;
    }
}
static void wrt_init(void)
{
    struct pdxb_init in; memset(&in, 0, sizeof in);
    in.xb_seg = SEG_XB; in.eng_seg = 0xE000; in.wbuf_seg = SEG_WBUF; in.wbuf_kb = 12;
    pd_fcopy_out(&in, SEG_XB, 0, sizeof in);
    pdw_verb(PDV_WRT_INIT, SEG_XB, 0, 0);
}
/* the edit store */
static pdw nE, tx, px;
static struct pd_edit E;
static void es_reset(void) { nE = 0; tx = 0x3000; px = 0x5000; pd_ffill(SEG_EDS, 0, 0, 0x8000); }
static void es_new(pdb kind, pdw page, int x0, int y0, int x1, int y1)
{
    memset(&E, 0, sizeof E); E.kind = kind; E.page = page; E.id = (pdw)(nE + 1);
    E.rect.x0 = (pd_pt)(x0 * 16); E.rect.y0 = (pd_pt)(y0 * 16); E.rect.x1 = (pd_pt)(x1 * 16); E.rect.y1 = (pd_pt)(y1 * 16);
}
static void es_text(const char *t) { pdw n = (pdw)strlen(t); pd_fcopy_out(t, SEG_EDS, tx, n); E.text_off = tx; E.text_len = n; tx = (pdw)(tx + n); }
/* points in whole pt; 0x7FFF as an x is the pen-up separator */
static void es_pts(const int *p, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        pd_pt x = p[2 * i] == 0x7FFF ? (pd_pt)PD_PENUP : (pd_pt)(p[2 * i] * 16);
        pd_fpokew(SEG_EDS, (pdw)(px + 4 * i), (pdw)x);
        pd_fpokew(SEG_EDS, (pdw)(px + 4 * i + 2), (pdw)(pd_pt)(p[2 * i + 1] * 16));
    }
    E.pts_off = px; E.pts_n = (pdw)n; px = (pdw)(px + 4 * n);
}
static void es_add(void) { pd_fcopy_out(&E, SEG_EDS, (pdw)(nE * sizeof E), sizeof E); nE++; }

static pdw save(const char *dst)
{
    struct pdxb_save sv; pdw r; int g = 0;
    memset(&sv, 0, sizeof sv);
    strcpy(sv.target, dst); strcpy(sv.source, "DOC.PDF"); strcpy(sv.date, "D:20261105190000"); strcpy(sv.author, "annotr");
    sv.mode = PDSV_INCREMENTAL; sv.edits_seg = SEG_EDS; sv.edits_count = nE;
    pd_fcopy_out(&sv, SEG_XB, 0, sizeof sv);
    do { r = (pdw)pdw_verb(PDV_WRT_SAVE, SEG_XB, 0, 0); pd_fcopy_in(SEG_XB, 0, &sv, sizeof sv); } while (r == PDE_BUSY && ++g < 20000);
    if (r) printf("save %s -> %u detail %u\n", dst, r, sv.err_detail);
    return r;
}
static void cp(const char *a, const char *b) { char c[512]; snprintf(c, sizeof c, "cp '%s' '%s'", a, b); system(c); }
static int annot_obj(pdw page, pdw idx, pdw *gen)
{
    struct pdxb_annot a;
    pde_verb(PDV_ENG_ANNOTS, page, idx, 0);
    if (pde_fail) return 0;
    pd_fcopy_in(SEG_XB, 0, &a, sizeof a);
    *gen = a.gen; return a.objnum;
}
static void dump(const char *dir)
{
    char p[512]; FILE *f; static unsigned char buf[0x8000]; int i;
    for (i = 0; i < 0x8000; i++) buf[i] = pd_fpeekb(SEG_EDS, (pdw)i);
    snprintf(p, sizeof p, "%s/store.bin", dir); f = fopen(p, "wb"); fwrite(buf, 1, 0x8000, f); fclose(f);
    snprintf(p, sizeof p, "%s/store.cnt", dir); f = fopen(p, "w"); fprintf(f, "%d\n", (int)nE); fclose(f);
}

int main(int argc, char **argv)
{
    char p[512]; pdw gen = 0, g2 = 0, r; int o1, o2;
    if (argc < 4) { fprintf(stderr, "usage: pdr_mkstore <source.pdf> <scenario> <outdir>\n"); return 2; }
    pdh_cluster = 1; pdw_host_clu = 512;
    snprintf(p, sizeof p, "%s/R.PDF", argv[3]); remove(p); remove(PDW_TEMPNAME);
    cp(argv[1], "DOC.PDF");
    if (eng_open("DOC.PDF")) { fprintf(stderr, "PDENG cannot open %s\n", argv[1]); return 2; }
    wrt_init(); es_reset();
    if (!strcmp(argv[2], "all")) {
        static const int hl1[8] = { 54, 748, 330, 748, 54, 722, 330, 722 };            /* UL UR LL LR */
        static const int hl2[16] = { 54, 716, 375, 716, 54, 703, 375, 703, 54, 372, 200, 372, 54, 356, 200, 356 };
        static const int ink1[20] = { 80, 300, 120, 380, 160, 320, 200, 390, 240, 300, 0x7FFF, 0, 100, 200, 300, 200, 0x7FFF, 0, 150, 250 };
        static const int sig[8] = { 80, 110, 110, 130, 140, 108, 170, 128 };
        static const int ln1[4] = { 400, 520, 500, 600 };
        static const int ln2[4] = { 400, 500, 520, 500 };
        es_new(PDA_HIGHLIGHT, 0, 54, 722, 330, 748); E.colour = PDC_YELLOW; es_pts((const int *)hl1, 4); es_add();
        es_new(PDA_HIGHLIGHT, 0, 54, 703, 375, 716); E.colour = PDC_LGREEN; es_pts((const int *)hl2, 8); es_add();
        es_new(PDA_NOTE, 0, 470, 740, 490, 758); E.colour = PDC_YELLOW; es_text("check this"); es_add();
        es_new(PDA_TEXT, 0, 152, 660, 528, 684); E.colour = PDC_BLUE; E.size = 12; es_text("J. Smith"); es_add();
        es_new(PDA_TEXT, 0, 408, 200, 554, 346); E.colour = PDC_RED; E.size = 10; E.align = 1; E.style = (pdb)((1 << 4) | 1);
        es_text("Cable seated and baud rate matches\nHeads clean; power is stable. Extra words to wrap"); es_add();
        es_new(PDA_INK, 0, 80, 200, 300, 390); E.colour = PDC_RED; E.size = 5; es_pts(ink1, 10); es_add();
        es_new(PDA_SIGN, 0, 80, 108, 170, 130); E.colour = PDC_BLACK; E.size = 3; es_pts(sig, 4); es_add();
        es_new(PDA_LINE, 0, 400, 520, 500, 600); E.colour = PDC_BLUE; E.style = 2; E.align = 2; es_pts(ln1, 2); es_add();
        es_new(PDA_LINE, 0, 400, 500, 520, 500); E.colour = PDC_GREEN; E.style = 1; E.align = 3; es_pts(ln2, 2); es_add();
        es_new(PDA_RECT, 0, 60, 470, 120, 510); E.colour = PDC_RED; E.style = 2; es_add();
        es_new(PDA_ELLIPSE, 0, 130, 470, 220, 520); E.colour = PDC_GREEN; E.style = 2; es_add();
        es_new(PDA_AREA, 0, 250, 470, 380, 520); E.colour = PDC_BLUE; E.style = 1; E.align = 1; es_add();
        es_new(PDA_AREA, 0, 250, 420, 380, 460); E.colour = PDC_MAGENTA; E.style = 1; es_add();
        es_new(PDA_NOTE, 0, 470, 740, 490, 758); E.irt = 1; E.state = PDS_COMPLETED; es_add();     /* a review reply: never drawn */
    } else if (!strcmp(argv[2], "replace")) {
        o1 = annot_obj(0, 1, &gen);         /* annotation 1 of ANNOTS256: a Highlight - deleted */
        o2 = annot_obj(0, 0, &g2);          /* annotation 0: a Text - replaced by a note moved to the page's bottom */
        if (!o1 || !o2) { fprintf(stderr, "ANNOTS256 not as expected\n"); return 2; }
        es_new(PDA_HIGHLIGHT, 0, 0, 0, 0, 0); E.src_num = (pdw)o1; E.src_gen = gen; E.flags = PDEF_DELETED; es_add();
        es_new(PDA_NOTE, 0, 300, 40, 320, 58); E.colour = PDC_LGREEN; E.src_num = (pdw)o2; E.src_gen = g2; E.id = 1; es_text("moved"); es_add();
    } else if (!strcmp(argv[2], "hostile")) {
        int k;
        for (k = 1; k <= 9; k++) {
            es_new((pdb)k, 0, 100, 100, 100 + k * 30, 140); E.colour = (pdb)(k + 5); E.size = 200; E.style = 0xFF; E.align = 3;
            E.text_off = 0xFFF0; E.text_len = 5000; E.pts_off = 0xFFFC; E.pts_n = 60000; es_add();
        }
        es_new(PDA_RECT, 0, 300, 300, 100, 100); es_add();                                     /* inverted */
        es_new(PDA_TEXT, 0, 10, 10, 11, 11); E.size = 255; es_text("w"); es_add();             /* thinner than its padding */
        es_new(PDA_NOTE, 0, 0, 0, 0, 0); es_add();                                             /* a zero rect */
        es_new(PDA_ELLIPSE, 0, -2000, -2000, 2000, 2000); E.style = 255; es_add();             /* the page-sized frame */
        es_new(99, 0, 0, 0, 10, 10); es_add();                                                  /* no such kind */
        es_new(PDA_TEXT, 0, 50, 200, 350, 290); E.size = 3; E.align = 2; es_text("a very long line with many many words and  double  spaces\n\n\r\n\r and breaks"); es_add();
        dump(argv[3]);
        return 0;
    } else { fprintf(stderr, "unknown scenario %s\n", argv[2]); return 2; }
    dump(argv[3]);
    snprintf(p, sizeof p, "%s/R.PDF", argv[3]);
    r = save(p);
    if (r != PDE_OK) { printf("FAIL: save answers %u\n", r); fails++; }
    printf("%s: store of %d records, %s\n", fails ? "FAIL" : "ok", (int)nE, p);
    return fails;
}
