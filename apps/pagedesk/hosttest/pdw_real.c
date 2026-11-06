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
#include <unistd.h>

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
#define SEG_MAP   0x9800
static pdw g_mapn, g_extract, g_lastmode, g_big, g_lastdetail;
static int fails;
#define CK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

extern char pdh_name[14];
pdw pdw_engcall(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdw r;
    if (verb == PDV_ENG_OPEN) {                          /* the verify opens PDSAVE.$$$ (14.2 step 4) */
        struct pdxb_open op; pd_fcopy_in(SEG_XB, 0, &op, sizeof op);
        if (op.state == 0 && strcmp(op.name, pdh_name) && !pdh_load(op.name)) strcpy(pdh_name, op.name);
    }
    r = (pdw)pde_verb(verb, bx, cx, dx);
    pdw_engcf = pde_fail != 0;
    if (getenv("PDW_ENGLOG") && (pde_fail || getenv("PDW_ENGALL"))) fprintf(stderr, "     PDENG verb %02X bx %u -> %u\n", verb, bx, (unsigned)r);
    return r;
}
static int eng_open(const char *file)
{
    struct pdxb_init in; struct pdxb_open op; unsigned r; int n = 0;
    if (pdh_load(file)) return -1;
    strcpy(pdh_name, "DOC.PDF");
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
    in.xb_seg = SEG_XB; in.eng_seg = 0xE000; in.wbuf_seg = SEG_WBUF; in.wbuf_kb = (g_extract || g_big) ? 40 : 18;
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

static void map_set(const unsigned *m, int n)
{
    int i; pd_ffill(SEG_MAP, 0, 0, 1024);
    for (i = 0; i < n; i++) pd_fpokew(SEG_MAP, (pdw)(2 * i), (pdw)m[i]);
    g_mapn = (pdw)n;
}
static pdw save(const char *dst)
{
    struct pdxb_save sv; pdw r; int g = 0;
    memset(&sv, 0, sizeof sv);
    sv.pages_seg = g_mapn ? SEG_MAP : 0; sv.pages_count = g_mapn;
    strcpy(sv.target, dst); strcpy(sv.source, "DOC.PDF"); strcpy(sv.date, "D:20261105190000"); strcpy(sv.author, "Verifier");
    sv.mode = g_extract ? PDSV_EXTRACT : PDSV_INCREMENTAL; sv.edits_seg = SEG_EDS; sv.edits_count = nE;
    pd_fcopy_out(&sv, SEG_XB, 0, sizeof sv);
    do { r = (pdw)pdw_verb(g_extract ? PDV_WRT_EXTRACT : PDV_WRT_SAVE, SEG_XB, 0, 0); pd_fcopy_in(SEG_XB, 0, &sv, sizeof sv); } while (r == PDE_BUSY && ++g < 20000);
    g_lastmode = sv.mode; g_lastdetail = sv.err_detail;
    if (r) printf("     save %s -> %u detail %u (phase %u item %u)\n", dst, r, sv.err_detail, sv_lastph, sv_lasti);
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
static void ins_slot(int k, const char *name, pdw clus, pdw vol1)
{
    pd_ffill(SEG_MAP, (pdw)(PDM_NAMES + 14 * k), 0, 14);
    pd_fcopy_out(name, SEG_MAP, (pdw)(PDM_NAMES + 14 * k), (pdw)(strlen(name) + 1));
    pd_fpokew(SEG_MAP, (pdw)(1002 + 4 * k), clus); pd_fpokew(SEG_MAP, (pdw)(1004 + 4 * k), vol1);
}
static void cp(const char *a, const char *b) { char c[256]; snprintf(c, sizeof c, "cp '%s' '%s'", a, b); system(c); }

int main(int argc, char **argv)
{
    static struct pdxb_annot an[32]; int n, i, k; pdw r, noteobj = 0, hlobj = 0, hlgen = 0, notegen = 0;
    pdh_cluster = 1; pdw_host_clu = 512;
    remove("R1.PDF"); remove("R2.PDF"); remove("P1.PDF"); remove("X1.PDF"); remove("N1.PDF"); remove("P2.PDF"); remove("X2.PDF"); remove("I1.PDF"); remove("I2.PDF"); remove(PDW_TEMPNAME);    /* a re-run starts clean */
    cp(argc > 1 ? argv[1] : "../../apps/pagedesk/samples/MARKUP.PDF", "DOC.PDF");
    if (system("python3 ../../apps/pagedesk/hosttest/pdw_nest.py NEST.PDF")) { printf("FAIL: pdw_nest.py\n"); return 2; }
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
    /* ---- 20.11: page operations on PAGES.PDF (8 pages, a flat tree whose root carries
       /MediaBox and /Resources) against the real engine ---- */
    {   unsigned map[12]; int np; struct pdxb_pageinfo pi[12]; int j;
        unsigned srcobj[8];
        cp(argc > 2 ? argv[2] : "../../apps/pagedesk/samples/PAGES.PDF", "DOC.PDF");
        CK(eng_open("DOC.PDF") == 0, "PDENG opens PAGES.PDF");
        for (j = 0; j < 8; j++) {
            pde_verb(PDV_ENG_PAGEINFO, (pdw)j, 0, 0); pd_fcopy_in(SEG_XB, 0, &pi[j], sizeof pi[j]); srcobj[j] = pi[j].objnum;
        }
        /* reversed-ish order, one rotated, two deleted, one duplicated (itself rotated), a blank */
        map[0] = PDM_MK(PDM_SRC, 0, 7); map[1] = PDM_MK(PDM_SRC, 0, 6); map[2] = PDM_MK(PDM_SRC, 1, 5);
        map[3] = PDM_MK(PDM_SRC, 0, 1); map[4] = PDM_MK(PDM_SRC, 2, 1); map[5] = PDM_MK(PDM_BLANK, 0, 0);
        map[6] = PDM_MK(PDM_SRC, 0, 2); map[7] = PDM_MK(PDM_SRC, 3, 4);
        map_set(map, 8); np = 8;
        wrt_init(); es_reset();
        es_new(PDA_NOTE, 5, 300, 300, 0, 0); E.colour = PDC_YELLOW; es_text("a note on a page that moved"); es_add();
        es_new(PDA_NOTE, 3, 300, 300, 0, 0); E.colour = PDC_RED; es_text("a note on a deleted page"); es_add();
        r = save("P1.PDF");
        CK(r == PDE_OK, "page operations save answers %u", r);
        CK((g_lastmode & PDW_OUT_REOPEN) != 0, "the writer says PDENG was left on the output (re-open)");
        cp("P1.PDF", "DOC.PDF");
        CK(eng_open("DOC.PDF") == 0, "PDENG opens the result");
        r = (pdw)pde_verb(PDV_ENG_PAGEINFO, (pdw)np, 0, 0);
        CK(pde_fail != 0, "exactly %d pages", np);
        {   static struct pdxb_pageinfo q; static const int want[8] = { 7, 6, 5, 1, 1, 0, 2, 4 };
            static const unsigned wrq[8] = { 0, 0, 1, 0, 2, 0, 0, 3 };
            int bad = 0;
            for (j = 0; j < np; j++) {
                pde_verb(PDV_ENG_PAGEINFO, (pdw)j, 0, 0); pd_fcopy_in(SEG_XB, 0, &q, sizeof q);
                if (j == 5) { if (q.objnum == srcobj[0] || (q.flags & PDP_NOCONTENT) == 0) bad++; }     /* a new, empty page */
                else if (j == 4) { if (q.objnum == srcobj[1]) bad++; }                                    /* the duplicate is a NEW object */
                else if (q.objnum != srcobj[want[j]]) bad++;
                if (q.rotate != (pdw)((pi[want[j]].rotate + 90 * wrq[j]) % 360)) { bad++; printf("     page %d rotate %u want %u\n", j, q.rotate, (pi[want[j]].rotate + 90 * wrq[j]) % 360); }
                if (q.media.x1 != pi[want[j]].media.x1 || q.media.y1 != pi[want[j]].media.y1) { bad++; printf("     page %d media %d x %d want %d x %d\n", j, q.media.x1, q.media.y1, pi[want[j]].media.x1, pi[want[j]].media.y1); }
            }
            CK(bad == 0, "order, rotation and boxes of the 8 pages (%d wrong)", bad);
        }
        {   static struct pdxb_annot an[8]; int n5 = annots(2, an, 8), n1 = annots(3, an, 8);
            CK(n5 == 1 && n1 == 0, "the note went with its page (page 3 of 8 has %d, the duplicate of page 1 has %d)", n5, n1); }
        printf("PDW-PAGES P1.PDF 8 root-flat\n");
        /* a second save over the result: the nine-page order is the source's own again (identity) -> byte copy plus */
        map_set(0, 0);
    }
    /* ---- Extract: pages 5, 1, a blank and 3 (rotated) of XTGUIDE.PDF into a fresh document ---- */
    {   unsigned map[4]; int j; static struct pdxb_pageinfo q;
        cp(argc > 3 ? argv[3] : "../../apps/pagedesk/samples/XTGUIDE.PDF", "DOC.PDF");
        CK(eng_open("DOC.PDF") == 0, "PDENG opens XTGUIDE.PDF");
        map[0] = PDM_MK(PDM_SRC, 0, 4); map[1] = PDM_MK(PDM_SRC, 0, 0); map[2] = PDM_MK(PDM_BLANK, 0, 0); map[3] = PDM_MK(PDM_SRC, 1, 2);
        map_set(map, 4); g_extract = 1; wrt_init(); es_reset();
        r = save("X1.PDF");
        CK(r == PDE_OK, "Extract answers %u", r);
        g_extract = 0; map_set(0, 0);
        cp("X1.PDF", "DOC.PDF");
        CK(eng_open("DOC.PDF") == 0, "PDENG opens the extract");
        pde_verb(PDV_ENG_PAGEINFO, 4, 0, 0);
        CK(pde_fail != 0, "four pages");
        for (j = 0; j < 4; j++) { pde_verb(PDV_ENG_PAGEINFO, (pdw)j, 0, 0); pd_fcopy_in(SEG_XB, 0, &q, sizeof q);
            printf("     page %d: obj %u rotate %u media %d x %d\n", j, q.objnum, q.rotate, q.media.x1 / 16, q.media.y1 / 16); }
        printf("PDW-EXTRACT X1.PDF XTGUIDE.PDF 5,1,blank,3r90\n");
    }
    /* ---- what the glass did: rotate page 1, a blank after it, page 2 deleted ---- */
    {   unsigned map[8]; int j;
        cp(argc > 2 ? argv[2] : "../../apps/pagedesk/samples/PAGES.PDF", "DOC.PDF");
        CK(eng_open("DOC.PDF") == 0, "PDENG opens PAGES.PDF again");
        map[0] = PDM_MK(PDM_SRC, 1, 0); map[1] = PDM_MK(PDM_BLANK, 0, 0);
        for (j = 2; j < 8; j++) map[j] = PDM_MK(PDM_SRC, 0, j);
        map_set(map, 8); wrt_init(); es_reset();
        r = save("P2.PDF");
        CK(r == PDE_OK, "rotate + blank + delete answers %u", r);
        map_set(0, 0);
    }
    /* ---- a NESTED page tree whose boxes, resources and rotation are inherited ---- */
    {   unsigned map[6]; int j; static struct pdxb_pageinfo q, p0[4]; static struct pdxb_annot an[8];
        cp("NEST.PDF", "DOC.PDF");
        CK(eng_open("DOC.PDF") == 0, "PDENG opens the nested tree");
        for (j = 0; j < 4; j++) { pde_verb(PDV_ENG_PAGEINFO, (pdw)j, 0, 0); pd_fcopy_in(SEG_XB, 0, &p0[j], sizeof p0[j]); }
        map[0] = PDM_MK(PDM_SRC, 0, 3); map[1] = PDM_MK(PDM_SRC, 0, 0); map[2] = PDM_MK(PDM_SRC, 0, 1);
        map[3] = PDM_MK(PDM_SRC, 0, 1); map[4] = PDM_MK(PDM_SRC, 1, 2);
        map_set(map, 5);
        wrt_init(); es_reset();
        es_new(PDA_NOTE, 1, 100, 100, 0, 0); E.colour = PDC_CYAN; es_text("a new note on B, which is also duplicated"); es_add();
        r = save("N1.PDF");
        CK(r == PDE_OK, "nested tree: page operations answer %u", r);
        cp("N1.PDF", "DOC.PDF");
        CK(eng_open("DOC.PDF") == 0, "PDENG opens the nested result");
        for (j = 0; j < 5; j++) {
            static const int want[5] = { 3, 0, 1, 1, 2 };
            pde_verb(PDV_ENG_PAGEINFO, (pdw)j, 0, 0); pd_fcopy_in(SEG_XB, 0, &q, sizeof q);
            CK(q.media.x1 == p0[want[j]].media.x1 && q.media.y1 == p0[want[j]].media.y1
               && q.rotate == (pdw)((p0[want[j]].rotate + (j == 4 ? 90 : 0)) % 360),
               "page %d: media %d x %d rotate %u (source page %d: %d x %d rotate %u)", j + 1, q.media.x1 / 16, q.media.y1 / 16, q.rotate,
               want[j] + 1, p0[want[j]].media.x1 / 16, p0[want[j]].media.y1 / 16, p0[want[j]].rotate);
        }
        CK(annots(2, an, 8) == 3 && annots(3, an, 8) == 2, "B has its note, its popup and the new note; the duplicate has the note and the popup (copies)");
        printf("PDW-NEST N1.PDF\n");
        /* Extract two of them: D and B (with its note and popup, /P and /Parent retargeted) */
        cp("NEST.PDF", "DOC.PDF");
        CK(eng_open("DOC.PDF") == 0, "PDENG opens the nested tree again");
        map[0] = PDM_MK(PDM_SRC, 0, 3); map[1] = PDM_MK(PDM_SRC, 0, 1);
        map_set(map, 2); g_extract = 1; wrt_init(); es_reset();
        r = save("X2.PDF");
        CK(r == PDE_OK, "nested tree: Extract answers %u", r);
        g_extract = 0; map_set(0, 0);
        printf("PDW-NEST X2.PDF\n");
    }

    /* ---- Insert (PAGEDESK-SPEC 20.16): pages of two other PDFs whose object numbers and font names
       COLLIDE with PAGES.PDF's, one of them in another folder; the refusals; the rollbacks ---- */
    {   unsigned map[12]; static struct pdxb_pageinfo q; int j; char cwd[200];
        static const int hasf[1] = { 0 };
        (void)hasf;
        if (system("python3 ../../apps/pagedesk/hosttest/pdw_foreign.py FOREIGN.PDF && mkdir -p FD && cp FOREIGN.PDF FD/FOREIGN2.PDF")) { printf("FAIL: pdw_foreign.py\n"); return 2; }
        if (!getcwd(cwd, sizeof cwd)) return 2;
        cp(argc > 2 ? argv[2] : "../../apps/pagedesk/samples/PAGES.PDF", "DOC.PDF");
        cp("DOC.PDF", "DOCKEEP.PDF"); cp("FOREIGN.PDF", "FOREIGNKEEP.PDF");
        CK(eng_open("DOC.PDF") == 0, "PDENG opens PAGES.PDF for Insert");
        /* S0, F(0):1, F(0):3, S1, F(1):2 turned, F(0):0, S2..S7 */
        map[0] = PDM_MK(PDM_SRC, 0, 0); map[1] = PDM_MK(PDM_FOREIGN, 0, PDM_FORE(0, 1)); map[2] = PDM_MK(PDM_FOREIGN, 0, PDM_FORE(0, 3));
        map[3] = PDM_MK(PDM_SRC, 0, 1); map[4] = PDM_MK(PDM_FOREIGN, 1, PDM_FORE(1, 2)); map[5] = PDM_MK(PDM_FOREIGN, 0, PDM_FORE(0, 0));
        for (j = 6; j < 12; j++) map[j] = PDM_MK(PDM_SRC, 0, j - 4);
        map_set(map, 12);
        ins_slot(0, "FOREIGN.PDF", 0, 1); ins_slot(1, "FOREIGN2.PDF", 1, 1);
        snprintf(pdw_host_fdir[1], 128, "%s/FD", cwd); snprintf(pdw_host_tgtdir, 128, "%s", cwd);
        g_big = 1; wrt_init(); es_reset();
        r = save("I1.PDF");
        CK(r == PDE_OK, "Insert: three pages of FOREIGN.PDF and one of a file in another folder answer %u", r);
        CK((g_lastmode & PDW_OUT_REOPEN) != 0, "the writer says PDENG was left away from the source (re-open)");
        CK(pdw_host_swaps > 0, "the foreign file in the other folder was read from it (%u folder switches)", (unsigned)pdw_host_swaps);
        CK(system("cmp -s DOC.PDF DOCKEEP.PDF && cmp -s FOREIGN.PDF FOREIGNKEEP.PDF && cmp -s FD/FOREIGN2.PDF FOREIGNKEEP.PDF") == 0, "the source and both foreign files are byte for byte as they were");
        cp("I1.PDF", "DOC.PDF");
        CK(eng_open("DOC.PDF") == 0, "PDENG opens the result");
        pde_verb(PDV_ENG_PAGEINFO, 12, 0, 0);
        CK(pde_fail != 0, "exactly 12 pages");
        for (j = 0; j < 12; j++) { pde_verb(PDV_ENG_PAGEINFO, (pdw)j, 0, 0); pd_fcopy_in(SEG_XB, 0, &q, sizeof q);
            printf("     page %d: obj %u rotate %u media %d x %d\n", j, q.objnum, q.rotate, q.media.x1 / 16, q.media.y1 / 16); }
        printf("PDW-INSERT I1.PDF PAGES.PDF FOREIGN.PDF\n");
        /* the refusals and rollbacks: each leaves no PDSAVE.$$$ and PDENG to be re-opened */
        cp("DOCKEEP.PDF", "DOC.PDF");
        CK(eng_open("DOC.PDF") == 0, "PDENG opens PAGES.PDF again");
        map[0] = PDM_MK(PDM_SRC, 0, 0); map[1] = PDM_MK(PDM_FOREIGN, 0, PDM_FORE(0, 9)); for (j = 2; j < 9; j++) map[j] = PDM_MK(PDM_SRC, 0, j - 1);
        map_set(map, 9); ins_slot(0, "FOREIGN.PDF", 0, 1); wrt_init();
        r = save("I2.PDF");
        CK(r == PDE_RANGE && g_lastdetail == 17, "a page past the end of the inserted file is refused (%u, detail %u)", r, g_lastdetail);
        CK(access(PDW_TEMPNAME, F_OK) != 0 && access("I2.PDF", F_OK) != 0, "...and nothing is left behind");
        CK((g_lastmode & PDW_OUT_REOPEN) != 0, "...the primary is told to re-open the source");
        ins_slot(0, "NOSUCH.PDF", 0, 1); map[1] = PDM_MK(PDM_FOREIGN, 0, PDM_FORE(0, 0)); map_set(map, 9); ins_slot(0, "NOSUCH.PDF", 0, 1);
        CK(eng_open("DOC.PDF") == 0, "PDENG opens PAGES.PDF"); wrt_init();
        r = save("I2.PDF");
        CK(r != PDE_OK && access(PDW_TEMPNAME, F_OK) != 0 && access("I2.PDF", F_OK) != 0, "a missing foreign file is refused with %u and writes nothing", r);
        cp("../../apps/pagedesk/samples/ACROFORM.PDF", "FORM.PDF");
        CK(eng_open("DOC.PDF") == 0, "PDENG opens PAGES.PDF"); map_set(map, 9); ins_slot(0, "FORM.PDF", 0, 1); wrt_init();
        r = save("I2.PDF");
        CK(r == PDE_LIMIT && g_lastdetail == 18 && access(PDW_TEMPNAME, F_OK) != 0, "a page with form fields is refused (%u, detail %u)", r, g_lastdetail);
        CK(eng_open("DOC.PDF") == 0, "PDENG opens PAGES.PDF"); map_set(map, 9); ins_slot(0, "FOREIGN.PDF", 0, 1); wrt_init();
        pdw_host_budget = 6000;
        r = save("I2.PDF");
        pdw_host_budget = -1;
        CK(r == PDE_DISKFULL && access(PDW_TEMPNAME, F_OK) != 0, "a full disk in the middle of an insert answers %u and removes the temp file", r);
        /* Esc: abort after a few slices */
        {   struct pdxb_save sv; int g = 0;
            CK(eng_open("DOC.PDF") == 0, "PDENG opens PAGES.PDF"); wrt_init();
            memset(&sv, 0, sizeof sv); sv.pages_seg = SEG_MAP; sv.pages_count = 9; strcpy(sv.target, "I2.PDF"); strcpy(sv.source, "DOC.PDF");
            sv.mode = PDSV_INCREMENTAL; sv.edits_seg = SEG_EDS; sv.edits_count = 0;
            pd_fcopy_out(&sv, SEG_XB, 0, sizeof sv);
            do { r = (pdw)pdw_verb(PDV_WRT_SAVE, SEG_XB, 0, 0); g++; } while (r == PDE_BUSY && g < 6);
            CK(r == PDE_BUSY, "an insert is still running after %d slices", g);
            pdw_verb(PDV_WRT_ABORT, SEG_XB, 0, 0);
            CK(access(PDW_TEMPNAME, F_OK) != 0 && access("I2.PDF", F_OK) != 0, "Esc removes the temp file and writes no target");
        }
        g_big = 0; map_set(0, 0);
    }
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails;
}
