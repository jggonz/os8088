/* ============================================================================
 * os8088 - apps/pagedesk/hosttest/pdw_real.c   PDWRT against the REAL PDENG
 *
 * verify-wrt's integration check (PAGEDESK-SPEC 20.5.12): PDWRT's incremental
 * save driven through pdw_verb() against PDENG's own host build (pdeng.c +
 * hosttest/pde_host.c), not pdw_test.c's stub - twice in a chain (a save over
 * the first save's output: a replaced note, a deleted highlight, new ink), and
 * each output re-opened by PDENG, which must list the newest revisions.
 * `make pdwrt-real` builds and runs it; it SKIPs while pdeng.c is not on the
 * branch. argv[1] is the source PDF (default: samples/MARKUP.PDF). Outputs
 * R1.PDF and R2.PDF land in the current directory.
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
#define CK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

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
static void es_reset(void) { nE = 0; tx = 0x3000; px = 0x5000; pd_ffill(SEG_EDS, 0, 0, 0x3000); }
static struct pd_edit *es_new(pdb kind, pdw page, int x0, int y0, int x1, int y1)
{
    memset(&E, 0, sizeof E); E.kind = kind; E.page = page; E.id = (pdw)(nE + 1);
    E.rect.x0 = (pd_pt)(x0 * 16); E.rect.y0 = (pd_pt)(y0 * 16); E.rect.x1 = (pd_pt)(x1 * 16); E.rect.y1 = (pd_pt)(y1 * 16);
    return &E;
}
static void es_text(const char *t) { pdw n = (pdw)strlen(t); pd_fcopy_out(t, SEG_EDS, tx, n); E.text_off = tx; E.text_len = n; tx = (pdw)(tx + n); }
static void es_pts(const int *p, int n) { int i; for (i = 0; i < 2 * n; i++) pd_fpokew(SEG_EDS, (pdw)(px + 2 * i), (pdw)(pd_pt)(p[i] * 16)); E.pts_off = px; E.pts_n = (pdw)n; px = (pdw)(px + 4 * n); }
static void es_add(void) { pd_fcopy_out(&E, SEG_EDS, (pdw)(nE * sizeof E), sizeof E); nE++; }

static pdw save(const char *dst)
{
    struct pdxb_save sv; pdw r; int g = 0;
    memset(&sv, 0, sizeof sv);
    strcpy(sv.target, dst); strcpy(sv.source, "DOC.PDF"); strcpy(sv.date, "D:20261105190000"); strcpy(sv.author, "Verifier");
    sv.mode = PDSV_INCREMENTAL; sv.edits_seg = SEG_EDS; sv.edits_count = nE;
    pd_fcopy_out(&sv, SEG_XB, 0, sizeof sv);
    do { r = (pdw)pdw_verb(PDV_WRT_SAVE, SEG_XB, 0, 0); pd_fcopy_in(SEG_XB, 0, &sv, sizeof sv); } while (r == PDE_BUSY && ++g < 20000);
    if (r) printf("     save %s -> %u detail %u\n", dst, r, sv.err_detail);
    return r;
}
static int annots(pdw page, struct pdxb_annot *out, int max)
{
    int i; pdw r;
    for (i = 0; i < max; i++) {
        r = (pdw)pde_verb(PDV_ENG_ANNOTS, page, (pdw)i, 0);
        if (pde_fail) break;
        (void)r; pd_fcopy_in(SEG_XB, 0, &out[i], sizeof out[i]);
    }
    return i;
}
static void cp(const char *a, const char *b) { char c[256]; snprintf(c, sizeof c, "cp '%s' '%s'", a, b); system(c); }

int main(int argc, char **argv)
{
    static struct pdxb_annot an[32]; int n, i, k; pdw r, noteobj = 0, hlobj = 0, hlgen = 0, notegen = 0;
    pdh_cluster = 1; pdw_host_clu = 512;
    remove("R1.PDF"); remove("R2.PDF"); remove(PDW_TEMPNAME);    /* a re-run starts clean */
    cp(argc > 1 ? argv[1] : "../../apps/pagedesk/samples/MARKUP.PDF", "DOC.PDF");
    /* round A: four new marks on a real sample */
    CK(eng_open("DOC.PDF") == 0, "PDENG opens %s", argv[1]);
    wrt_init(); es_reset();
    es_new(PDA_NOTE, 0, 400, 700, 0, 0); E.colour = PDC_YELLOW; es_text("A note from the integration run"); es_add();
    { int q[8] = { 72, 720, 300, 720, 72, 700, 300, 700 }; es_new(PDA_HIGHLIGHT, 1, 0, 0, 0, 0); E.colour = PDC_YELLOW; es_pts(q, 4); es_add(); }
    es_new(PDA_TEXT, 0, 72, 100, 300, 160); E.colour = PDC_BLUE; E.size = 12; es_text("Tested on two IBM XTs."); es_add();
    es_new(PDA_NOTE, 0, 400, 700, 0, 0); E.irt = 1; E.state = PDS_COMPLETED; es_add();
    r = save("R1.PDF");
    CK(r == PDE_OK, "round A: incremental save over the real PDENG answers %u", r);
    /* round B: PDENG on R1, replace the note, delete the highlight, add ink */
    cp("R1.PDF", "DOC.PDF");
    CK(eng_open("DOC.PDF") == 0, "PDENG opens R1");
    n = annots(0, an, 32);
    for (i = 0; i < n; i++) if (!strncmp(an[i].subtype, "Text", 4) && !an[i].irt_num) { noteobj = an[i].objnum; notegen = an[i].gen; }
    k = annots(1, an, 32);
    for (i = 0; i < k; i++) if (!strncmp(an[i].subtype, "Highlight", 9)) { hlobj = an[i].objnum; hlgen = an[i].gen; }
    CK(n == 3 && noteobj && hlobj, "PDENG lists round A's marks: %d on page 1, note obj %u, highlight obj %u", n, noteobj, hlobj);
    wrt_init(); es_reset();
    es_new(PDA_NOTE, 0, 420, 650, 0, 0); E.colour = PDC_LGREEN; E.src_num = noteobj; E.src_gen = notegen; E.id = 1; es_text("A note from the integration run (revised)"); es_add();
    es_new(PDA_HIGHLIGHT, 1, 0, 0, 0, 0); E.src_num = hlobj; E.src_gen = hlgen; E.flags = PDEF_DELETED; es_add();
    { int s[8] = { 100, 300, 200, 400, 300, 300, 400, 400 }; es_new(PDA_INK, 1, 0, 0, 0, 0); E.colour = PDC_RED; E.size = 4; es_pts(s, 4); es_add(); }
    r = save("R2.PDF");
    CK(r == PDE_OK, "round B (a save over the first save's output) answers %u", r);
    /* PDENG re-opens R2: the newest revision wins */
    cp("R2.PDF", "DOC.PDF");
    CK(eng_open("DOC.PDF") == 0, "PDENG opens R2");
    n = annots(0, an, 32);
    for (i = 0, k = 0; i < n; i++) if (an[i].objnum == noteobj) { k = 1; CK(strstr(an[i].contents, "(revised)") != NULL, "note %u reads '%s' (newest wins)", noteobj, an[i].contents); }
    CK(k, "the revised note is still on page 1");
    n = annots(1, an, 32);
    for (i = 0, k = 0; i < n; i++) { if (an[i].objnum == hlobj) k |= 1; if (!strncmp(an[i].subtype, "Ink", 3)) k |= 2; }
    CK(k == 2, "page 2: highlight gone, ink present (%d annots, flags %d)", n, k);
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails;
}
