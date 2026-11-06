/* ============================================================================
 * os8088 - apps/pagedesk/hosttest/pdw_test.c      PDWRT's host harness
 *
 * #includes ../pdwrt.c (one translation unit, so the statics are reachable)
 * and drives it ONLY through the door, pdw_verb(), the way the primary will:
 * PDV_WRT_INIT, then NEWDOC / SAVE slices until the answer is not PDE_BUSY.
 * A stub PDENG answers TRAILER, FINGERPRINT, PAGEINFO and RAW_COPY from a real
 * (small) classic-xref reader, so a second incremental save over the first
 * save's output exercises "newest revision wins" for real.
 *
 * It writes its outputs into $PDW_OUT (default build/pdw) and prints one
 * `PDW-FILE <name>` line per output; hosttest/pdw_verify.py runs qpdf and
 * mutool over them. Exit status is the number of failed checks.
 * ==========================================================================*/
#include "../pdwrt.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- the arena's segments ------------------------------------------------ */
#define SEG_XB   0x4000u
#define SEG_WBUF 0x5000u
#define SEG_EDS  0x6000u
#define SEG_PGS  0x7000u
#define SEG_CONT 0x7100u
static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static char g_out[256];

/* ---- the stub PDENG --------------------------------------------------------
   a classic xref reader over one file held in memory */
static unsigned char *E_buf; static long E_len;
static long E_off[8192]; static int E_gen[8192]; static int E_have[8192];
static unsigned E_size, E_root, E_info, E_ver; static long E_sx; static unsigned char E_id0[16]; static int E_id0n;
static int E_calls[256];

static long find_last(const char *needle)
{
    long i, n = (long)strlen(needle);
    for (i = E_len - n; i >= 0; i--) if (!memcmp(E_buf + i, needle, (size_t)n)) return i;
    return -1;
}
static int hexv(int c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }
static void eng_load(const char *path)
{
    FILE *f = fopen(path, "rb"); long x, prev; int first = 1; char *p;
    free(E_buf);
    memset(E_have, 0, sizeof E_have);
    fseek(f, 0, SEEK_END); E_len = ftell(f); fseek(f, 0, SEEK_SET);
    E_buf = malloc((size_t)E_len + 1); fread(E_buf, 1, (size_t)E_len, f); fclose(f); E_buf[E_len] = 0;
    x = find_last("startxref"); E_sx = atol((char *)E_buf + x + 9);
    E_ver = 13; if (!memcmp(E_buf, "%PDF-1.", 7)) E_ver = 10u + (unsigned)(E_buf[7] - '0');
    prev = E_sx;
    while (prev >= 0 && first >= 0) {
        char *s = (char *)E_buf + prev, *t; long a, b, k; int i;
        if (strncmp(s, "xref", 4)) { printf("stub: no xref at %ld\n", prev); exit(2); }
        s += 5;
        for (;;) {
            if (!strncmp(s, "trailer", 7)) break;
            a = strtol(s, &t, 10); b = strtol(t, &t, 10); s = t; while (*s == '\n' || *s == '\r' || *s == ' ') s++;
            for (k = 0; k < b; k++, s += 20) {
                long id = a + k;
                if (s[17] == 'n' && !E_have[id]) { E_off[id] = atol(s); E_gen[id] = atoi(s + 11); E_have[id] = 1; }
            }
        }
        p = s;
        if (first) {
            char *q;
            q = strstr(p, "/Size"); E_size = (unsigned)atoi(q + 5);
            q = strstr(p, "/Root"); E_root = (unsigned)atoi(q + 5);
            q = strstr(p, "/Info"); E_info = q && q < strstr(p, "startxref") ? (unsigned)atoi(q + 5) : 0;
            q = strstr(p, "/ID"); E_id0n = 0;
            if (q) { q = strchr(q, '<') + 1; for (i = 0; i < 16 && q[i * 2] != '>'; i++) { E_id0[i] = (unsigned char)(hexv(q[i * 2]) * 16 + hexv(q[i * 2 + 1])); E_id0n++; } }
            first = 0;
        }
        { char *q = strstr(p, "/Prev"), *e = strstr(p, "startxref"); prev = (q && q < e) ? atol(q + 5) : -1; }
        if (prev < 0) first = -1;
    }
    /* /Version in the catalog */
    { char *c = (char *)E_buf + E_off[E_root], *e = strstr(c, "endobj"), *v = strstr(c, "/Version");
      if (v && v < e) E_ver = 10u + (unsigned)(v[10] - '0'); }
}
static int obj_len(unsigned n)
{
    char *s = (char *)E_buf + E_off[n], *e = strstr(s, "endobj");
    return (int)(e - s) + 7;                    /* "endobj\n" */
}
static int obj_kids(unsigned *kids)
{
    char *s = (char *)E_buf + E_off[E_root], *e = strstr(s, "endobj"), *p; unsigned pages; char *k; int n = 0;
    p = strstr(s, "/Pages"); (void)e; pages = (unsigned)atoi(p + 6);
    s = (char *)E_buf + E_off[pages]; k = strstr(s, "/Kids"); k = strchr(k, '[') + 1;
    while (*k != ']') { while (*k == ' ') k++; if (*k == ']') break; kids[n++] = (unsigned)atoi(k); while (*k != 'R') k++; k++; }
    return n;
}
pdw pdw_engcall(pdw verb, pdw bx, pdw cx, pdw dx)
{
    (void)cx; (void)dx;
    E_calls[verb & 255]++;
    pdw_engcf = 0;
    switch (verb) {
    case PDV_ENG_TRAILER: {
        struct pdxb_trailer t; memset(&t, 0, sizeof t);
        t.size = (pdw)E_size; t.root_num = (pdw)E_root; t.info_num = (pdw)E_info;
        t.startxref.lo = (pdw)(E_sx & 0xFFFF); t.startxref.hi = (pdw)(E_sx >> 16);
        memcpy(t.id0, E_id0, 16); t.id0_len = (pdw)E_id0n; t.version = (pdw)E_ver;
        pd_fcopy_out(&t, g_xb, 0, sizeof t); return 0; }
    case PDV_ENG_FINGERPRINT: {
        struct pdxb_fingerprint f; f.size.lo = (pdw)(E_len & 0xFFFF); f.size.hi = (pdw)(E_len >> 16);
        f.crc_head = pdm_crc16(0xFFFF, E_buf, 1024 < E_len ? 1024 : (pdw)E_len);
        f.crc_tail = pdm_crc16(0xFFFF, E_buf + (E_len > 1024 ? E_len - 1024 : 0), 1024 < E_len ? 1024 : (pdw)E_len);
        pd_fcopy_out(&f, g_xb, 0, sizeof f); return 0; }
    case PDV_ENG_PAGEINFO: {
        unsigned kids[1100]; int n = obj_kids(kids); struct pdxb_pageinfo pi;
        if ((int)bx >= n) { pdw_engcf = 1; return PDE_RANGE; }
        memset(&pi, 0, sizeof pi); pi.objnum = (pdw)kids[bx]; pi.gen = (pdw)E_gen[kids[bx]];
        pd_fcopy_out(&pi, g_xb, 0, sizeof pi); return 0; }
    case PDV_ENG_RAW_COPY: {
        struct pdxb_rawcopy rc; long len, done, take;
        pd_fcopy_in(g_xb, 0, &rc, sizeof rc);
        if (!rc.objnum || rc.objnum >= 8192 || !E_have[rc.objnum]) { pdw_engcf = 1; return PDE_XREF; }
        len = obj_len(rc.objnum); done = ((long)rc.done.hi << 16) | rc.done.lo;
        take = len - done; if (take < 0) take = 0; if (take > rc.dmax) take = rc.dmax;
        memcpy(pd_host_mem + (((unsigned long)rc.dseg << 4) + rc.doff), E_buf + E_off[rc.objnum] + done, (size_t)take);
        rc.got = (pdw)take; pd_fcopy_out(&rc, g_xb, 0, sizeof rc); return 0; }
    }
    pdw_engcf = 1; return PDE_RANGE;
}

/* ---- the edit store ----------------------------------------------------- */
static pdw ES_n, ES_tx, ES_px;
static void es_reset(void) { ES_n = 0; ES_tx = 0x3000; ES_px = 0x4000; pd_ffill(SEG_EDS, 0, 0, 0x3000); }
#define PT(v) ((pd_pt)((v) * 16))
static struct pd_edit *es_new(pdb kind, pdw page, double x0, double y0, double x1, double y1)
{
    static struct pd_edit e;
    memset(&e, 0, sizeof e); e.kind = kind; e.page = page;
    e.rect.x0 = PT(x0); e.rect.y0 = PT(y0); e.rect.x1 = PT(x1); e.rect.y1 = PT(y1);
    e.id = (pdw)(ES_n + 1);
    return &e;
}
static void es_text(struct pd_edit *e, const char *t, int n)
{
    if (n < 0) n = (int)strlen(t);
    pd_fcopy_out(t, SEG_EDS, ES_tx, (pdw)n); e->text_off = ES_tx; e->text_len = (pdw)n; ES_tx = (pdw)(ES_tx + n);
}
static void es_pts(struct pd_edit *e, const pd_pt *p, int n)
{
    pd_fcopy_out(p, SEG_EDS, ES_px, (pdw)(n * 4)); e->pts_off = ES_px; e->pts_n = (pdw)n; ES_px = (pdw)(ES_px + n * 4);
}
static pdw es_add(struct pd_edit *e)
{
    pd_fcopy_out(e, SEG_EDS, (pdw)(ES_n * sizeof *e), sizeof *e);
    return ES_n++;
}

/* ---- driving a job through the door ------------------------------------- */
static struct pdxb_save SV;
static int g_slices;
static int g_maxio;                     /* most disk transfers one slice made */
static pdw call_job(int verb)
{
    pdw r; int guard = 0; pdw c0, r0;
    pd_fcopy_out(&SV, SEG_XB, 0, sizeof SV);
    g_maxio = 0;
    for (;;) {
        c0 = pdw_host_calls; r0 = pdw_host_reads;
        r = pdw_verb((unsigned)verb, SEG_XB, 0, 0);
        { int io = (int)(pdw_host_calls - c0) + (int)(pdw_host_reads - r0); if (io > g_maxio) g_maxio = io; }
        pd_fcopy_in(SEG_XB, 0, &SV, sizeof SV);
        g_slices++;
        if (r != PDE_BUSY) break;
        if (++guard > 20000) { printf("job runaway\n"); exit(3); }
    }
    return r;
}
static char T_SRC[14];
static void sv_setup(const char *target, pdw mode)
{
    memset(&SV, 0, sizeof SV);
    strcpy(SV.source, T_SRC); strcpy(SV.date, "D:20261105120000"); strcpy(SV.author, "Reviewer");
    strncpy(SV.target, target, 13); SV.mode = mode;
    SV.edits_seg = SEG_EDS; SV.edits_count = ES_n;
}
static void init_module(pdw wkb)
{
    struct pdxb_init in; memset(&in, 0, sizeof in);
    in.xb_seg = SEG_XB; in.wbuf_seg = SEG_WBUF; in.wbuf_kb = wkb;
    pd_fcopy_out(&in, SEG_XB, 0, sizeof in);
    CHECK(pdw_verb(PDV_WRT_INIT, SEG_XB, 0, 0) == PDE_OK, "init");
}
static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static char *path(const char *n) { return (char *)n; }   /* main() chdir()s into the output directory: names are 8.3-sized */

/* ---- scenes ---------------------------------------------------------------- */
static const char *PAGE1 =
    "BT /Helv 14 Tf 72 700 Td (The quick brown fox jumps over the lazy dog) Tj "
    "0 -20 Td (and keeps running across the second line of text.) Tj ET\n"
    "0.5 g 72 120 468 1 re f\n";
static char PAGE2[24000] = "BT /TiRo 20 Tf 72 600 Td (Page two, set in Times) Tj ET\n";
static void fill_page2(void)       /* 20 KB of small text lines: several slices of contents */
{
    int y; char *p = PAGE2 + strlen(PAGE2);
    for (y = 0; y < 560; y += 2) p += sprintf(p, "BT /Cour 4 Tf 40 %d Td (line %d of the long page two contents stream) Tj ET\n", 20 + y, y);
}

static void stage_pages(void)
{
    struct pdw_newpage np[2]; memset(np, 0, sizeof np);
    np[0].w = PT(612); np[0].h = PT(792); np[0].cseg = SEG_CONT; np[0].clen = (pdw)strlen(PAGE1);
    np[1].w = PT(792); np[1].h = PT(612); np[1].rotate = 90; np[1].cseg = (pdw)(SEG_CONT + 0x20); np[1].clen = (pdw)strlen(PAGE2);
    pd_fcopy_out(PAGE1, SEG_CONT, 0, (pdw)strlen(PAGE1));
    if (strlen(PAGE2) < 1000) fill_page2();
    np[1].clen = (pdw)strlen(PAGE2);
    pd_fcopy_out(PAGE2, (pdw)(SEG_CONT + 0x20), 0, (pdw)strlen(PAGE2));
    pd_fcopy_out(np, SEG_PGS, 0, sizeof np);
    SV.pages_seg = SEG_PGS; SV.pages_count = 2;
}
static void spiral(pd_pt *p, int n, double cx, double cy, double r0, double r1, int *used)
{
    int i; double a, r;
    for (i = 0; i < n; i++) {
        a = i * 0.12; r = r0 + (r1 - r0) * i / (n - 1);
        p[i * 2] = PT(cx + r * __builtin_cos(a)); p[i * 2 + 1] = PT(cy + r * __builtin_sin(a));
    }
    *used = n;
}

/* every annotation kind, on a fresh two-page document */
static void scene_all_kinds(void)
{
    static pd_pt pt[1100]; struct pd_edit *e; int n; pdw note1;
    es_reset();
    /* page 0 */
    e = es_new(PDA_NOTE, 0, 400, 700, 420, 718); e->colour = 14; es_text(e, "Caf\xE9 \x80uro \x97 \x93quoted\x94 note", -1); note1 = es_add(e);
    {   pd_pt q[16];                           /* highlight: two lines, UL UR LL LR each */
        q[0] = PT(107); q[1] = PT(715); q[2] = PT(190); q[3] = PT(715); q[4] = PT(107); q[5] = PT(697); q[6] = PT(190); q[7] = PT(697);
        q[8] = PT(72);  q[9] = PT(695); q[10] = PT(150); q[11] = PT(695); q[12] = PT(72); q[13] = PT(677); q[14] = PT(150); q[15] = PT(677);
        e = es_new(PDA_HIGHLIGHT, 0, 0, 0, 0, 0); e->colour = 14; es_pts(e, q, 8); es_add(e);
    }
    e = es_new(PDA_AREA, 0, 300, 560, 520, 640); e->colour = 4; e->style = 1; e->align = 1; es_text(e, "Area mark", -1); es_add(e);
    e = es_new(PDA_TEXT, 0, 72, 380, 300, 520); e->colour = 1; e->style = 0; e->size = 12; e->align = 0;
    es_text(e, "This is a long text box wrapped in Helvetica twelve. It has several sentences so the wrapping has "
               "to break lines at spaces, and a_very_long_unbreakable_word_that_does_not_fit_on_one_line_at_all, "
               "plus a hard\nline break and \xE9\xE8\xE0 accents. Parens (like these) and a backslash \\ are escaped.", -1); es_add(e);
    e = es_new(PDA_TEXT, 0, 320, 380, 540, 440); e->colour = 5; e->style = (pdb)(0x10 | 1); e->size = 14; e->align = 1; es_text(e, "Times bold, centred", -1); es_add(e);
    e = es_new(PDA_TEXT, 0, 320, 320, 540, 370); e->colour = 2; e->style = 0x20; e->size = 10; e->align = 2; es_text(e, "Courier 10 right aligned\nsecond line", -1); es_add(e);
    spiral(pt, 500, 160, 250, 4, 70, &n);       /* a 500-point ink stroke */
    {   static pd_pt ink[1100]; int i;
        for (i = 0; i < 500; i++) { ink[i * 2] = pt[i * 2]; ink[i * 2 + 1] = pt[i * 2 + 1]; }
        ink[1000] = PDW_PENUP; ink[1001] = PDW_PENUP;      /* pen up, then a second stroke and a dot */
        ink[1002] = PT(250); ink[1003] = PT(200); ink[1004] = PT(300); ink[1005] = PT(260); ink[1006] = PT(350); ink[1007] = PT(200);
        ink[1008] = PDW_PENUP; ink[1009] = PDW_PENUP; ink[1010] = PT(380); ink[1011] = PT(250);
        e = es_new(PDA_INK, 0, 0, 0, 0, 0); e->colour = 9; e->size = 5; es_pts(e, ink, 506); es_add(e);
    }
    {   pd_pt sg[8]; sg[0] = PT(330); sg[1] = PT(120); sg[2] = PT(360); sg[3] = PT(160); sg[4] = PDW_PENUP; sg[5] = PDW_PENUP; sg[6] = PT(380); sg[7] = PT(125);
        sg[0] = PT(330); sg[1] = PT(130); sg[2] = PT(370); sg[3] = PT(170);
        e = es_new(PDA_SIGN, 0, 0, 0, 0, 0); e->colour = 0; e->size = 4; es_pts(e, sg, 4); es_add(e);
    }
    {   pd_pt l[4]; l[0] = PT(80); l[1] = PT(80); l[2] = PT(200); l[3] = PT(60);
        e = es_new(PDA_LINE, 0, 0, 0, 0, 0); e->colour = 4; e->style = 2; e->align = 1; es_pts(e, l, 2); es_add(e);
        l[0] = PT(220); l[1] = PT(70); l[2] = PT(280); l[3] = PT(70);
        e = es_new(PDA_LINE, 0, 0, 0, 0, 0); e->colour = 1; e->style = 1; e->align = 3; es_pts(e, l, 2); es_add(e);
        l[0] = PT(300); l[1] = PT(40); l[2] = PT(300); l[3] = PT(100);
        e = es_new(PDA_LINE, 0, 0, 0, 0, 0); e->colour = 2; e->style = 1; e->align = 2; es_pts(e, l, 2); es_add(e);
    }
    e = es_new(PDA_RECT, 0, 400, 40, 480, 100); e->colour = 6; e->style = 3; es_add(e);
    e = es_new(PDA_ELLIPSE, 0, 490, 40, 580, 100); e->colour = 3; e->style = 3; es_add(e);
    /* the review state: a reply that resolves the note */
    e = es_new(PDA_NOTE, 0, 400, 700, 420, 718); e->irt = ES_n ? (pdw)(note1 + 1) : 0; e->state = PDS_COMPLETED; es_add(e);
    /* page 1 */
    e = es_new(PDA_TEXT, 1, 72, 400, 300, 440); e->colour = 0; e->style = 0x10; e->size = 18; es_text(e, "Fresh page two, text box", -1); es_add(e);
    e = es_new(PDA_NOTE, 1, 100, 300, 120, 318); e->colour = 10; es_text(e, "A note on page two", -1); es_add(e);
}

static void write_file(const char *p, const void *d, long n) { FILE *f = fopen(p, "wb"); fwrite(d, 1, (size_t)n, f); fclose(f); }
static unsigned char *slurp(const char *p, long *n)
{
    FILE *f = fopen(p, "rb"); unsigned char *b;
    if (!f) { *n = -1; return NULL; }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET); b = malloc((size_t)*n + 1); fread(b, 1, (size_t)*n, f); fclose(f); return b;
}

/* ---- the hand-written source ---------------------------------------------- */
static long SRC_FILLER;                 /* bytes of filler stream before the xref (0 = none) */
static void build_source(const char *p)
{
    static const char *objs[] = {
        NULL,
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 7 0 R /Resources << /Font << /F1 8 0 R >> >> /Annots [ 9 0 R ] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 7 0 R /Resources << /Font << /F1 8 0 R >> >> /Annots 10 0 R >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 7 0 R /Resources << /Font << /F1 8 0 R >> >> >>",
        "<< /Producer (hand written) >>",
        NULL,
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Type /Annot /Subtype /Text /Rect [100 600 120 618] /Contents (Existing note) /P 3 0 R /F 4 >>",
        "[ 11 0 R 12 0 R ]",
        "<< /Type /Annot /Subtype /Text /Rect [100 500 120 518] /Contents (to be deleted) /P 4 0 R /F 4 >>",
        "<< /Type /Annot /Subtype /Square /Rect [200 200 300 260] /Contents (keeper) /P 4 0 R /F 4 /C [1 0 0] >>" };
    const char *content = "BT /F1 18 Tf 72 700 Td (Page text line one) Tj 0 -30 Td (Second line of text) Tj ET\n";
    FILE *f = fopen(p, "wb"); long off[16]; int i;
    fputs("%PDF-1.3\n%\xE2\xE3\xCF\xD3\n", f);
    for (i = 1; i <= 12; i++) {
        off[i] = ftell(f);
        fprintf(f, "%d 0 obj\n", i);
        if (i == 7) fprintf(f, "<< /Length %d >>\nstream\n%s\nendstream\n", (int)strlen(content), content);
        else fprintf(f, "%s\n", objs[i]);
        fputs("endobj\n", f);
    }
    if (SRC_FILLER) {
        long k;
        off[13] = ftell(f);
        fprintf(f, "13 0 obj\n<< /Length %ld >>\nstream\n", SRC_FILLER);
        for (k = 0; k < SRC_FILLER; k++) fputc((int)('A' + k % 26), f);
        fputs("\nendstream\nendobj\n", f);
    }
    off[0] = ftell(f);
    fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", SRC_FILLER ? 14 : 13);
    for (i = 1; i <= (SRC_FILLER ? 13 : 12); i++) fprintf(f, "%010ld 00000 n \n", off[i]);
    fprintf(f, "trailer\n<< /Size %d /Root 1 0 R /Info 6 0 R /ID [<0123456789ABCDEF0123456789ABCDEF><0123456789ABCDEF0123456789ABCDEF>] >>\nstartxref\n%ld\n%%%%EOF\n", SRC_FILLER ? 14 : 13, off[0]);
    fclose(f);
}

static void report(const char *name) { printf("PDW-FILE %s\n", name); }

/* ============================ the tests ============================== */
static void t_fresh(pdw clu, const char *name)
{
    pdw r; int viol0;
    pdw_host_clu = clu; pdw_host_viol = 0; viol0 = 0;
    init_module(12);
    scene_all_kinds();
    sv_setup(path(name), PDSV_FRESH);
    stage_pages();
    g_slices = 0;
    r = call_job(PDV_WRT_NEWDOC);
    CHECK(r == PDE_OK, "%s: newdoc answered %u (detail %u)", name, r, SV.err_detail);
    CHECK(exists(path(name)), "%s: output exists", name);
    CHECK(!exists(PDW_TEMPNAME), "%s: temp file gone", name);
    CHECK(pdw_host_viol == viol0, "%s: %u appends after a short chunk (cluster rule)", name, pdw_host_viol);
    CHECK(g_slices > 3, "%s: job was resumable (%d slices)", name, g_slices);
    CHECK(g_maxio <= 1, "%s: %d transfers in one slice (15.2: one)", name, g_maxio);
    report(name);
    printf("  %s: %d slices, %u objects in the table, %lu bytes\n", name, g_slices, g_tn, (unsigned long)(((unsigned long)SV.written.hi << 16) | SV.written.lo));
}

static pdw T_CLU = 512;
static int t_inc(const char *src, const char *dst, int round)
{
    pdw r; long n0, n1; unsigned char *a, *b; int i;
    struct pd_edit *e; static pd_pt pt[1100]; int used;
    init_module(12);
    pdw_host_clu = T_CLU; pdw_host_viol = 0;
    eng_load(src);
    strncpy(T_SRC, src, 13);
    es_reset();
    if (round == 1) {
        e = es_new(PDA_NOTE, 0, 300, 500, 320, 518); e->colour = 14; es_text(e, "Check this figure", -1); es_add(e);
        {   pd_pt q[8]; q[0] = PT(72); q[1] = PT(720); q[2] = PT(260); q[3] = PT(720); q[4] = PT(72); q[5] = PT(700); q[6] = PT(260); q[7] = PT(700);
            e = es_new(PDA_HIGHLIGHT, 1, 0, 0, 0, 0); e->colour = 10; es_pts(e, q, 4); es_add(e); }
        spiral(pt, 120, 300, 300, 3, 60, &used);
        e = es_new(PDA_INK, 2, 0, 0, 0, 0); e->colour = 4; e->size = 4; es_pts(e, pt, used); es_add(e);
        e = es_new(PDA_NOTE, 0, 350, 650, 370, 668); e->colour = 14; e->src_num = 9; es_text(e, "Existing note (moved)", -1); es_add(e);
        e = es_new(PDA_NOTE, 1, 100, 500, 120, 518); e->src_num = 11; e->flags = PDEF_DELETED; es_add(e);
        e = es_new(PDA_NOTE, 0, 300, 500, 320, 518); e->irt = 1; e->state = PDS_COMPLETED; es_add(e);
    } else {
        e = es_new(PDA_NOTE, 0, 300, 500, 320, 518); e->colour = 14; e->src_num = g_onum[0]; es_text(e, "Check this figure (revised)", -1); es_add(e);
        e = es_new(PDA_INK, 2, 0, 0, 0, 0); e->src_num = g_onum[2]; e->flags = PDEF_DELETED; es_add(e);
        e = es_new(PDA_AREA, 2, 100, 100, 200, 160); e->colour = 2; es_text(e, "second round", -1); es_add(e);
    }
    sv_setup(path(dst), PDSV_INCREMENTAL);
    a = slurp(src, &n0);
    g_slices = 0;
    r = call_job(PDV_WRT_SAVE);
    CHECK(r == PDE_OK, "%s: save answered %u (detail %u)", dst, r, SV.err_detail);
    b = slurp(path(dst), &n1);
    CHECK(b && n1 > n0, "%s: grew from %ld to %ld", dst, n0, n1);
    CHECK(b && !memcmp(a, b, (size_t)n0), "%s: the source's %ld bytes are the prefix, unchanged", dst, n0);
    CHECK(pdw_host_viol == 0, "%s: cluster rule", dst);
    CHECK(g_maxio <= 1, "%s: %d transfers in one slice (15.2: one)", dst, g_maxio);
    CHECK(!exists(PDW_TEMPNAME), "%s: no temp left", dst);
    for (i = 0; i < 1 && b; i++) CHECK(b[n0] == '\n', "new line after the source");
    free(a); free(b);
    report(dst);
    printf("  %s: %ld -> %ld bytes, %d slices, onum[0]=%u\n", dst, n0, n1, g_slices, g_onum[0]);
    return 0;
}

static void t_failures(void)
{
    pdw r; long n; unsigned char *s;
    /* disk full during a fresh save */
    pdw_host_clu = 512; init_module(12);
    scene_all_kinds(); sv_setup(path("full.pdf"), PDSV_FRESH); stage_pages();
    pdw_host_budget = 6000;
    r = call_job(PDV_WRT_NEWDOC);
    CHECK(r == PDE_DISKFULL, "disk full answered %u", r);
    CHECK(!exists(PDW_TEMPNAME), "disk full: temp deleted");
    CHECK(!exists(path("full.pdf")), "disk full: no target");
    pdw_host_budget = -1;
    /* write protect */
    pdw_host_wprot = 1;
    sv_setup(path("wp.pdf"), PDSV_FRESH); stage_pages();
    r = call_job(PDV_WRT_NEWDOC);
    CHECK(r == PDE_WPROT, "write protect answered %u", r);
    CHECK(!exists(PDW_TEMPNAME) && !exists(path("wp.pdf")), "write protect: nothing left");
    pdw_host_wprot = 0;
    /* abort in the middle */
    sv_setup(path("ab.pdf"), PDSV_FRESH); stage_pages();
    pd_fcopy_out(&SV, SEG_XB, 0, sizeof SV);
    r = pdw_verb(PDV_WRT_NEWDOC, SEG_XB, 0, 0);
    CHECK(r == PDE_BUSY, "first slice busy (%u)", r);
    CHECK(pdw_verb(PDV_WRT_ABORT, SEG_XB, 0, 0) == PDE_OK, "abort");
    CHECK(!exists(PDW_TEMPNAME), "abort: temp deleted");
    /* target exists: ask, then replace */
    write_file(path("exists.pdf"), "OLD", 3);
    sv_setup(path("exists.pdf"), PDSV_FRESH); stage_pages();
    r = call_job(PDV_WRT_NEWDOC);
    CHECK(r == PDE_EXISTS && SV.state == PDW_ST_CONFIRM, "exists: answered %u state %x", r, SV.state);
    CHECK(exists(PDW_TEMPNAME), "exists: the finished temp file waits");
    s = slurp(path("exists.pdf"), &n); CHECK(n == 3 && !memcmp(s, "OLD", 3), "exists: target untouched until confirmed"); free(s);
    SV.state = PDW_ST_REPLACE;
    r = call_job(PDV_WRT_NEWDOC);
    CHECK(r == PDE_OK && SV.state == PDW_ST_DONE, "replace: answered %u", r);
    s = slurp(path("exists.pdf"), &n); CHECK(n > 100 && !memcmp(s, "%PDF-1.5", 8), "replace: new file in place"); free(s);
    CHECK(!exists(PDW_TEMPNAME), "replace: temp renamed");
    report("exists.pdf");
    /* disk full during an incremental save leaves the source alone */
    {   long n0, n1; unsigned char *a, *b;
        init_module(12); eng_load(path("src.pdf")); strcpy(T_SRC, "src.pdf");
        es_reset(); { struct pd_edit *e = es_new(PDA_NOTE, 0, 300, 500, 320, 518); es_text(e, "x", 1); es_add(e); }
        sv_setup(path("incfull.pdf"), PDSV_INCREMENTAL); a = slurp(path("src.pdf"), &n0);
        pdw_host_budget = 2000;
        r = call_job(PDV_WRT_SAVE);
        CHECK(r == PDE_DISKFULL, "incremental disk full answered %u", r);
        CHECK(!exists(PDW_TEMPNAME) && !exists(path("incfull.pdf")), "incremental disk full: nothing left");
        b = slurp(path("src.pdf"), &n1); CHECK(n0 == n1 && !memcmp(a, b, (size_t)n0), "source intact"); free(a); free(b);
        pdw_host_budget = -1;
    }
    /* the self-check refuses a damaged file: no rename, temp deleted */
    pdw_host_clu = 512; init_module(12);
    scene_all_kinds(); sv_setup(path("tamper.pdf"), PDSV_FRESH); stage_pages();
    pdw_host_tamper = 1;
    r = call_job(PDV_WRT_NEWDOC);
    pdw_host_tamper = 0;
    CHECK(r == PDE_TRUNC, "a damaged tail answered %u", r);
    CHECK(!exists(PDW_TEMPNAME) && !exists(path("tamper.pdf")), "damaged: temp deleted, nothing renamed");
    /* too small a writer buffer is refused with the KB wanted */
    pdw_host_clu = 8192; init_module(8);
    es_reset(); sv_setup(path("tiny.pdf"), PDSV_FRESH); stage_pages();
    r = call_job(PDV_WRT_NEWDOC);
    CHECK(r == PDE_NOMEM && SV.err_detail > 8, "small buffer refused (%u, wants %u KB)", r, SV.err_detail);
    pdw_host_clu = 512;
}

static void t_limits(void)
{
    pdw r; int i;
    pdw_host_clu = 512; init_module(12);
    es_reset();
    for (i = 0; i < 250; i++) {
        struct pd_edit *e = es_new(PDA_NOTE, (pdw)(i & 1), 100 + (i % 20) * 20, 100 + (i / 20) * 40, 0, 0);
        char t[40]; snprintf(t, sizeof t, "note number %d", i); e->colour = (pdb)(i & 15); es_text(e, t, -1); es_add(e);
    }
    sv_setup(path("many.pdf"), PDSV_FRESH); stage_pages();
    r = call_job(PDV_WRT_NEWDOC);
    printf("  many.pdf: 250 notes -> %u (detail %u), table %u of %u\n", r, SV.err_detail, g_tn, g_tmax);
    CHECK(r == PDE_OK, "250 notes in a 12 KB buffer");
    report("many.pdf");
    init_module(8); sv_setup(path("many8.pdf"), PDSV_FRESH); stage_pages();
    r = call_job(PDV_WRT_NEWDOC);
    printf("  250 notes in an 8 KB buffer -> %u (table %u)\n", r, g_tmax);
    CHECK(r == PDE_LIMIT || r == PDE_OK, "8 KB buffer answers cleanly");
    CHECK(!exists(PDW_TEMPNAME), "limit: no temp left");
}

int main(void)
{
    const char *o = getenv("PDW_OUT");
    pdw clu;
    snprintf(g_out, sizeof g_out, "%s", o ? o : "build/pdw");
    { char cmd[300]; snprintf(cmd, sizeof cmd, "mkdir -p '%s'", g_out); system(cmd); }
    if (chdir(g_out)) { perror("chdir"); return 9; }
    system("rm -f -- *.pdf PDSAVE.*");   /* (no slash-star in a string: tests/unit/t_swallow.py) */
    for (clu = 512; clu <= 4096; clu *= 2) {
        char nm[32]; snprintf(nm, sizeof nm, "fresh%u.pdf", clu);
        t_fresh(clu, nm);
    }
    build_source(path("src.pdf")); report("src.pdf");
    t_inc(path("src.pdf"), "inc1.pdf", 1);
    t_inc(path("inc1.pdf"), "inc2.pdf", 2);
    /* a source past 64 KiB, not a cluster multiple: 32-bit offsets and the copy loop */
    SRC_FILLER = 150001;
    for (clu = 512; clu <= 4096; clu *= 8) {
        char s[32], d[32]; snprintf(s, sizeof s, "big%u.pdf", clu); snprintf(d, sizeof d, "bigi%u.pdf", clu);
        build_source(s); report(s); T_CLU = clu; t_inc(s, d, 1);
    }
    SRC_FILLER = 0; T_CLU = 512;
    t_failures();
    t_limits();
    printf("%s: %d failed check(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
