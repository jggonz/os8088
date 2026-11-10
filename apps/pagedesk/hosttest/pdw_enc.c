/* ============================================================================
 * os8088 - apps/pagedesk/hosttest/pdw_enc.c    PDWRT on xref-stream, object-stream
 *                                              and encrypted sources (20.22.4.6)
 *
 * PDWRT is driven through pdw_verb() as the primary drives it, over a STAND-IN
 * for the PDENG verbs this round's engine agents own and that have not landed:
 *   PDRC_PLAIN reads, PDV_ENG_TRAILER (encrypt_num, tflags, id0_hi), OPEN's
 *   PDE_PASSWORD, CRYPTINFO and SETKEY (the writer enciphers itself, with crypt's
 * shared primitives: pdctwin.c on the host).
 * The reads come from hosttest/pdw_encprep.py (qpdf decodes the file); the
 * cipher is OpenSSL's (AES) and RC4 written here; the stand-in checks the key
 * the writer holds against qpdf's, so a wrong key fails the job.
 *
 *   pdw_enc SRC.PDF PASSWORD PLAINCONTROL.PDF-or-"-"
 * writes S1.PDF (a save of every kind of mark over SRC.PDF), S2.PDF (a save
 * over S1: a replaced note, a deleted highlight, new ink) and prints the line
 * `PDW-ENC S1.PDF S2.PDF` for hosttest/pdw_encverify.py.
 * ==========================================================================*/
#include "../pdwrt.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SEG_XB   0x2000
#define SEG_WBUF 0x7000
#define SEG_EDS  0x8000
#define SEG_MAP  0x9800
static int fails;
#define CK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the stand-in PDENG ------------------------------------------------------ */
static char g_db[64] = "DB";
static char g_pw[64] = "";
static char g_fpw[64] = "";      /* the foreign file's */
static char G_lastname[14];
static int E_enc, E_keyed, E_stm, E_str, E_v, E_r, E_opens, E_setkeys, E_badkeys;
static unsigned char E_key[32]; static int E_keylen;
static long E_len; static unsigned char *E_buf;
static unsigned E_size, E_root, E_rg, E_info, E_ig, E_ver, E_en, E_eg, E_tfl, E_stmx; static long E_sx; static unsigned char E_id0[32]; static int E_id0n;
static int E_pg[1100][12]; static int E_npg;
static pdw g_rcmodes[4];
static int hv(int c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }
static int load_db(const char *name)
{
    char cmd[400], line[600], k[40]; FILE *f; int a, b, c, d, e, g;
    snprintf(cmd, sizeof cmd, "python3 %s '%s' %s '%s'", getenv("PDW_PREP") ? getenv("PDW_PREP") : "../../apps/pagedesk/hosttest/pdw_encprep.py", name, g_db, !strcmp(name, "FOREIGN.PDF") ? g_fpw : g_pw);
    if (system(cmd)) return -1;
    { FILE *s = fopen(name, "rb"); if (!s) return -1; fseek(s, 0, SEEK_END); E_len = ftell(s); fseek(s, 0, SEEK_SET); free(E_buf); E_buf = malloc((size_t)E_len + 1); fread(E_buf, 1, (size_t)E_len, s); fclose(s); }
    snprintf(cmd, sizeof cmd, "%s/trailer.txt", g_db); f = fopen(cmd, "r"); if (!f) return -1;
    E_id0n = 0; E_en = E_eg = E_tfl = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%39s", k) != 1) continue;
        if (!strcmp(k, "size")) E_size = (unsigned)atoi(line + 5);
        else if (!strcmp(k, "root")) sscanf(line + 5, "%u %u", &E_root, &E_rg);
        else if (!strcmp(k, "info")) sscanf(line + 5, "%u %u", &E_info, &E_ig);
        else if (!strcmp(k, "startxref")) E_sx = atol(line + 10);
        else if (!strcmp(k, "version")) E_ver = (unsigned)atoi(line + 8);
        else if (!strcmp(k, "id0")) { char *p = line + 4; while (*p == ' ') p++; for (E_id0n = 0; hv(p[0]) >= 0 && p[0] != '\n' && p[0] && p[1] && p[1] != '\n' && E_id0n < 32; E_id0n++, p += 2) E_id0[E_id0n] = (unsigned char)(hv(p[0]) * 16 + hv(p[1])); }
        else if (!strcmp(k, "encrypt")) sscanf(line + 8, "%u %u", &E_en, &E_eg);
        else if (!strcmp(k, "tflags")) E_tfl = (unsigned)atoi(line + 7);
        else if (!strcmp(k, "xrefstm")) E_stmx = (unsigned)atoi(line + 8);
    }
    fclose(f);
    snprintf(cmd, sizeof cmd, "%s/pages.txt", g_db); f = fopen(cmd, "r"); E_npg = 0;
    while (f && fgets(line, sizeof line, f) && E_npg < 1100) { sscanf(line, "%d %d %d %d %d %d %d %d %d %d %d", &a, &b, &c, &d, &e, &g, &E_pg[E_npg][6], &E_pg[E_npg][7], &E_pg[E_npg][8], &E_pg[E_npg][9], &E_pg[E_npg][10]); E_pg[E_npg][0] = a; E_pg[E_npg][1] = b; E_pg[E_npg][2] = c; E_pg[E_npg][3] = d; E_pg[E_npg][4] = e; E_pg[E_npg][5] = g; E_npg++; }
    if (f) fclose(f);
    snprintf(cmd, sizeof cmd, "%s/enc.txt", g_db); f = fopen(cmd, "r"); E_enc = 0; E_keyed = 0; E_keylen = 0;
    while (f && fgets(line, sizeof line, f)) {
        if (sscanf(line, "%39s", k) != 1) continue;
        if (!strcmp(k, "encrypted")) E_enc = atoi(line + 10);
        else if (!strcmp(k, "key")) { char *p = line + 4; for (E_keylen = 0; p[0] > ' ' && p[1] > ' '; E_keylen++, p += 2) E_key[E_keylen] = (unsigned char)(hv(p[0]) * 16 + hv(p[1])); }
        else if (!strcmp(k, "stm")) E_stm = atoi(line + 4);
        else if (!strcmp(k, "str")) E_str = atoi(line + 4);
        else if (!strcmp(k, "v")) E_v = atoi(line + 2);
        else if (!strcmp(k, "r")) E_r = atoi(line + 2);
    }
    if (f) fclose(f);
    strcpy(G_lastname, name);
    return 0;
}
pdw pdw_engcall(pdw verb, pdw bx, pdw cx, pdw dx)
{
    (void)bx; (void)cx; (void)dx;
    pdw_engcf = 0;
    switch (verb) {
    case PDV_ENG_CLOSE: G_lastname[0] = 0; return 0;
    case PDV_ENG_OPEN: {
        struct pdxb_open op; pd_fcopy_in(g_xb, 0, &op, sizeof op);
        if (strcmp(op.name, G_lastname)) { if (load_db(op.name)) { pdw_engcf = 1; return PDE_IO; } E_opens++; }
        op.size.lo = (pdw)(E_len & 0xFFFF); op.size.hi = (pdw)(E_len >> 16); op.state = 1;
        op.flags = (pdw)((E_enc ? PDO_ENCRYPTED : 0)); op.npages = (pdw)E_npg;
        pd_fcopy_out(&op, g_xb, 0, sizeof op);
        if (E_enc && !E_keyed) { pdw_engcf = 1; return PDE_PASSWORD; }
        return 0; }
    case PDV_ENG_CRYPTINFO: {
        struct pdxb_crypt c; memset(&c, 0, sizeof c);
        if (!E_enc) { pdw_engcf = 1; return PDE_RANGE; }
        c.v = (pdw)E_v; c.r = (pdw)E_r;
        if (E_keyed) { c.stm = (pdw)E_stm; c.str = (pdw)E_str; c.encmeta = 1; memcpy(c.key, E_key, (size_t)E_keylen); c.key_len = (pdw)E_keylen; c.auth = PDCA_USER; }
        memcpy(c.id0, E_id0, 32); c.id0_len = (pdw)E_id0n;
        pd_fcopy_out(&c, g_xb, 0, sizeof c); return 0; }
    case PDV_ENG_SETKEY: {
        struct pdxb_crypt c; pd_fcopy_in(g_xb, 0, &c, sizeof c); E_setkeys++;
        if (c.key_len != (pdw)E_keylen || memcmp(c.key, E_key, (size_t)E_keylen) || c.stm != (pdw)E_stm || c.str != (pdw)E_str) { E_badkeys++; pdw_engcf = 1; return PDE_PASSWORD; }
        E_keyed = 1; return 0; }
    case PDV_ENG_TRAILER: {
        struct pdxb_trailer t; memset(&t, 0, sizeof t);
        t.size = (pdw)E_size; t.root_num = (pdw)E_root; t.root_gen = (pdw)E_rg; t.info_num = (pdw)E_info; t.info_gen = (pdw)E_ig;
        t.startxref.lo = (pdw)(E_sx & 0xFFFF); t.startxref.hi = (pdw)(E_sx >> 16);
        memcpy(t.id0, E_id0, 16); memcpy(t.id0_hi, E_id0 + 16, 16); t.id0_len = (pdw)E_id0n; t.version = (pdw)E_ver;
        t.encrypt_num = (pdw)E_en; t.encrypt_gen = (pdw)E_eg; t.tflags = (pdw)(E_tfl | (E_stmx ? PDTR_XREFSTM : 0));
        pd_fcopy_out(&t, g_xb, 0, sizeof t); return 0; }
    case PDV_ENG_FINGERPRINT: {
        struct pdxb_fingerprint f; f.size.lo = (pdw)(E_len & 0xFFFF); f.size.hi = (pdw)(E_len >> 16);
        f.crc_head = pdm_crc16(0xFFFF, E_buf, 1024 < E_len ? 1024 : (pdw)E_len);
        f.crc_tail = pdm_crc16(0xFFFF, E_buf + (E_len > 1024 ? E_len - 1024 : 0), 1024 < E_len ? 1024 : (pdw)E_len);
        pd_fcopy_out(&f, g_xb, 0, sizeof f); return 0; }
    case PDV_ENG_PAGEINFO: {
        struct pdxb_pageinfo pi; if ((int)bx >= E_npg) { pdw_engcf = 1; return PDE_RANGE; }
        memset(&pi, 0, sizeof pi); pi.objnum = (pdw)E_pg[bx][0]; pi.gen = (pdw)E_pg[bx][1];
        pi.media.x0 = (pd_pt)(E_pg[bx][2] * 16); pi.media.y0 = (pd_pt)(E_pg[bx][3] * 16); pi.media.x1 = (pd_pt)(E_pg[bx][4] * 16); pi.media.y1 = (pd_pt)(E_pg[bx][5] * 16);
        pi.crop.x0 = (pd_pt)(E_pg[bx][6] * 16); pi.crop.y0 = (pd_pt)(E_pg[bx][7] * 16); pi.crop.x1 = (pd_pt)(E_pg[bx][8] * 16); pi.crop.y1 = (pd_pt)(E_pg[bx][9] * 16);
        pi.rotate = (pdw)E_pg[bx][10];
        pd_fcopy_out(&pi, g_xb, 0, sizeof pi); return 0; }
    case PDV_ENG_RAW_COPY: {
        struct pdxb_rawcopy rc; char fn[100]; FILE *f; long len, done, take; unsigned char *b;
        pd_fcopy_in(g_xb, 0, &rc, sizeof rc);
        if (rc.mode < 4) g_rcmodes[rc.mode]++;
        if (E_enc && !E_keyed && rc.mode == PDRC_PLAIN) { pdw_engcf = 1; return PDE_PASSWORD; }
        snprintf(fn, sizeof fn, "%s/o%u.txt", g_db, rc.objnum); f = fopen(fn, "rb");
        if (!f) { pdw_engcf = 1; return PDE_XREF; }
        fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET); b = malloc((size_t)len + 1); fread(b, 1, (size_t)len, f); fclose(f);
        done = ((long)rc.done.hi << 16) | rc.done.lo; take = len - done; if (take < 0) take = 0; if (take > rc.dmax) take = rc.dmax;
        memcpy(pd_host_mem + (((unsigned long)rc.dseg << 4) + rc.doff), b + done, (size_t)take); free(b);
        if (done > len) { pdw_engcf = 1; return PDE_RANGE; }
        rc.got = (pdw)take; done += take; rc.done.lo = (pdw)(done & 0xFFFF); rc.done.hi = (pdw)(done >> 16);
        pd_fcopy_out(&rc, g_xb, 0, sizeof rc); return 0; }
    }
    pdw_engcf = 1; return PDE_RANGE;
}
/* what the primary does to open a file: OPEN, and on PDE_PASSWORD the key the
 * password gives (here: qpdf's) through SETKEY, then OPEN again */
static int eng_open(const char *name)
{
    struct pdxb_open op; struct pdxb_init in; pdw r; pdw_host_clu = 512;
    (void)in; g_xb = SEG_XB;
    memset(&op, 0, sizeof op); strcpy(op.name, name); pd_fcopy_out(&op, SEG_XB, 0, sizeof op);
    G_lastname[0] = 0;
    r = pdw_engcall(PDV_ENG_OPEN, 0, 0, 0);
    if (pdw_engcf && r == PDE_PASSWORD) {
        struct pdxb_crypt c; memset(&c, 0, sizeof c);
        c.key_len = (pdw)E_keylen; memcpy(c.key, E_key, (size_t)E_keylen); c.stm = (pdw)E_stm; c.str = (pdw)E_str; c.auth = PDCA_USER;
        pd_fcopy_out(&c, SEG_XB, 0, sizeof c); pdw_engcall(PDV_ENG_SETKEY, 0, 0, 0);
        pd_fcopy_out(&op, SEG_XB, 0, sizeof op);
        r = pdw_engcall(PDV_ENG_OPEN, 0, 0, 0);
    }
    return pdw_engcf ? (int)r : 0;
}

/* ---- the edit store and the job ------------------------------------------------ */
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
static void wrt_init(void)
{
    struct pdxb_init in; memset(&in, 0, sizeof in);
    in.xb_seg = SEG_XB; in.eng_seg = 0xE000; in.wbuf_seg = SEG_WBUF; in.wbuf_kb = 40;
    pd_fcopy_out(&in, SEG_XB, 0, sizeof in);
    pdw_verb(PDV_WRT_INIT, SEG_XB, 0, 0);
}
/* the job as the primary runs it */
static struct pdxb_save SVB;
static pdw job(pdw verb, pdw mode, const char *src, const char *dst, pdw map_n)
{
    pdw r; int g = 0;
    memset(&SVB, 0, sizeof SVB);
    strcpy(SVB.target, dst); strcpy(SVB.source, src); strcpy(SVB.date, "D:20261110090000"); strcpy(SVB.author, "Reviewer \xE9");
    SVB.mode = mode; SVB.edits_seg = SEG_EDS; SVB.edits_count = nE; SVB.pages_seg = map_n ? SEG_MAP : 0; SVB.pages_count = map_n;
    pd_fcopy_out(&SVB, SEG_XB, 0, sizeof SVB);
    for (;;) {
        r = (pdw)pdw_verb(verb, SEG_XB, 0, 0); pd_fcopy_in(SEG_XB, 0, &SVB, sizeof SVB);
        if (r == PDE_BUSY && ++g < 20000) continue;
        break;
    }
    if (r) printf("     job %s -> %u detail %u (phase %u item %u)\n", dst, r, SVB.err_detail, sv_lastph, sv_lasti);
    return r;
}
static pdw save(const char *src, const char *dst) { return job(PDV_WRT_SAVE, PDSV_INCREMENTAL, src, dst, 0); }
static void map_set(const unsigned *m, int n)
{
    int i; pd_ffill(SEG_MAP, 0, 0, 1024);
    for (i = 0; i < n; i++) pd_fpokew(SEG_MAP, (pdw)(2 * i), (pdw)m[i]);
}
static void ins_slot(int k, const char *name)
{
    pd_ffill(SEG_MAP, (pdw)(PDM_NAMES + 14 * k), 0, 14);
    pd_fcopy_out(name, SEG_MAP, (pdw)(PDM_NAMES + 14 * k), (pdw)(strlen(name) + 1));
    pd_fpokew(SEG_MAP, (pdw)(1002 + 4 * k), 0); pd_fpokew(SEG_MAP, (pdw)(1004 + 4 * k), 1);
}

int main(int argc, char **argv)
{
    pdw r; unsigned size0; int setk0 = 0; const char *src = argc > 1 ? argv[1] : "SRC.PDF";
    if (argc > 2) snprintf(g_pw, sizeof g_pw, "%s", argv[2]);
    remove("S1.PDF"); remove("S2.PDF"); remove(PDW_TEMPNAME);
    wrt_init();
    CK(eng_open(src) == 0, "the stand-in opens %s%s", src, E_enc ? " (encrypted, key from qpdf)" : "");
    wrt_init(); es_reset(); size0 = E_size;
    es_new(PDA_NOTE, 0, 400, 700, 420, 718); E.colour = PDC_YELLOW; es_text("Caf\xE9 \x80uro note"); es_add();
    es_new(PDA_NOTE, 0, 300, 700, 320, 718); E.colour = PDC_BLUE; es_text("plain ASCII note (with parens) and \\ backslash"); es_add();
    { int q[8] = { 72, 720, 300, 720, 72, 700, 300, 700 }; es_new(PDA_HIGHLIGHT, 0, 0, 0, 0, 0); E.colour = PDC_YELLOW; es_pts(q, 4); es_add(); }
    es_new(PDA_TEXT, 0, 72, 100, 300, 160); E.colour = PDC_BLUE; E.size = 12; es_text("Tested on two IBM XTs."); es_add();
    es_new(PDA_AREA, 0, 300, 560, 520, 640); E.colour = 4; es_text("an area"); es_add();
    { int s[8] = { 100, 300, 200, 400, 300, 300, 400, 400 }; es_new(PDA_INK, 1, 0, 0, 0, 0); E.colour = PDC_RED; E.size = 4; es_pts(s, 4); es_add(); }
    { int s[4] = { 330, 130, 370, 170 }; es_new(PDA_SIGN, 1, 0, 0, 0, 0); E.colour = 0; E.size = 4; es_pts(s, 2); es_add(); }
    es_new(PDA_RECT, 1, 400, 40, 480, 100); E.colour = 6; E.style = 3; es_add();
    es_new(PDA_NOTE, 0, 400, 700, 420, 718); E.irt = 1; E.state = PDS_COMPLETED; es_add();
    r = save(src, "S1.PDF");
    CK(r == PDE_OK, "save over %s answers %u", src, r);
    CK(g_rcmodes[PDRC_PLAIN] > 0, "the writer read the source with PDRC_PLAIN (%u calls)", g_rcmodes[PDRC_PLAIN]);
    if (E_enc) {
        CK(pdw_host_ncr > 0 && E_badkeys == 0, "%u strings and streams enciphered by the writer; every SETKEY key and method was qpdf's (%d bad)", pdw_host_ncr, E_badkeys);
        CK(E_setkeys >= 2, "the verify re-opened the encrypted output with the held key (%d SETKEYs)", E_setkeys);
    }
    if (r) return 1;
    /* a second save, over the first: a replaced note (it keeps /NM and /T), the highlight deleted, ink added */
    if (eng_open("S1.PDF") != 0) { CK(0, "the stand-in opens S1.PDF"); return 1; }
    { /* the first note's object: the first object past the source's /Size */
      FILE *f; char fn[100]; unsigned n0 = 0, hl = 0, k; char buf[4096];
      for (k = size0; k < size0 + 40; k++) {
          snprintf(fn, sizeof fn, "%s/o%u.txt", g_db, k); f = fopen(fn, "rb"); if (!f) continue;
          { size_t m = fread(buf, 1, sizeof buf - 1, f); buf[m] = 0; }
          fclose(f);
          if (strstr(buf, "/Subtype /Text") && !strstr(buf, "/IRT") && !n0) n0 = k;
          if (strstr(buf, "/Subtype /Highlight")) hl = k;
      }
      CK(n0 && hl, "S1's note is object %u, its highlight %u (as read back by qpdf)", n0, hl);
      wrt_init(); es_reset();
      es_new(PDA_NOTE, 0, 400, 650, 0, 0); E.colour = PDC_LGREEN; E.src_num = (pdw)n0; E.src_gen = 0; E.id = 1; es_text("the note again (revised)"); es_add();
      es_new(PDA_HIGHLIGHT, 0, 0, 0, 0, 0); E.src_num = (pdw)hl; E.src_gen = 0; E.flags = PDEF_DELETED; es_add();
      { int s[8] = { 100, 300, 200, 400, 300, 300, 400, 400 }; es_new(PDA_INK, 1, 0, 0, 0, 0); E.colour = PDC_RED; E.size = 4; es_pts(s, 4); es_add(); }
    }
    r = save("S1.PDF", "S2.PDF");
    CK(r == PDE_OK, "a save over the first save's output answers %u", r);
    /* Extract: the two pages, the second first, from the source (a plain output from an encrypted source) */
    remove("X1.PDF");
    if (eng_open(src) != 0) { CK(0, "the stand-in opens %s", src); return 1; }
    { unsigned m[2] = { PDM_MK(PDM_SRC, 0, 1), PDM_MK(PDM_SRC, 0, 0) }; map_set(m, 2); }
    wrt_init(); es_reset();
    r = job(PDV_WRT_EXTRACT, PDSV_EXTRACT, src, "X1.PDF", 2);
    CK(r == PDE_OK, "Extract from %s answers %u", src, r);
    /* Insert: pages 1 and 3 of FOREIGN.PDF after the source's first page, then its second */
    if (argc > 3) {
        snprintf(g_fpw, sizeof g_fpw, "%s", argc > 4 ? argv[4] : "");
        remove("I1.PDF");
        { unsigned m[4] = { PDM_MK(PDM_SRC, 0, 0), PDM_MK(PDM_FOREIGN, 0, PDM_FORE(0, 1)), PDM_MK(PDM_FOREIGN, 0, PDM_FORE(0, 3)), PDM_MK(PDM_SRC, 0, 1) }; map_set(m, 4); }
        ins_slot(0, "FOREIGN.PDF");
        { /* plat's pre-flight (20.22.4.5): the foreign file's key and methods, then the document again */
          struct pdxb_wkeys wk; int setk;
          memset(&wk, 0, sizeof wk);
          if (strstr(argv[3], "enc")) {
              load_db("FOREIGN.PDF"); setk = E_setkeys;
              wk.n = 1; wk.k[0].slot = 0; wk.k[0].key_len = (pdw)E_keylen; memcpy(wk.k[0].key, E_key, (size_t)E_keylen);
              wk.k[0].stm = (pdw)E_stm; wk.k[0].str = (pdw)E_str; wk.k[0].encmeta = 1; (void)setk;
          }
          G_lastname[0] = 0;
          if (eng_open(src) != 0) { CK(0, "the stand-in opens %s", src); return 1; }
          wrt_init(); es_reset(); setk0 = E_setkeys;
          pd_fcopy_out(&wk, SEG_XB, 0, sizeof wk);
          CK(pdw_verb(PDV_WRT_KEYS, SEG_XB, 0, 0) == PDE_OK, "PDV_WRT_KEYS takes %u foreign key(s)", (unsigned)wk.n);
        }
        es_new(PDA_NOTE, 0, 400, 700, 420, 718); E.colour = PDC_YELLOW; es_text("a note beside an inserted page"); es_add();
        r = job(PDV_WRT_SAVE, PDSV_INCREMENTAL, src, "I1.PDF", 4);
        CK(r == PDE_OK, "Insert (target %s, foreign %s) answers %u", E_enc ? "encrypted" : "plain", argv[3], r);
        if (strstr(argv[3], "enc")) CK(E_setkeys > setk0, "the encrypted foreign file was opened with the key plat's pre-flight handed over (%d SETKEYs)", E_setkeys - setk0);
    }
    { unsigned a, b, rep = 0; for (a = 0; a < pdw_host_nivs; a++) for (b = a + 1; b < pdw_host_nivs; b++) if (!memcmp(pdw_host_ivs[a], pdw_host_ivs[b], 16)) rep++;
      CK(rep == 0 || !E_enc, "no IV repeated in %u (%u repeats)", pdw_host_nivs, rep); }
    if (getenv("PDW_SABOTAGE") && E_enc) {           /* a wrong key held by the writer must fail the verify, not pass it */
        struct pdxb_save sv; pdw rr; int g = 0;
        if (eng_open(src) != 0) return 1;
        es_reset(); es_new(PDA_NOTE, 0, 10, 10, 20, 20); E.colour = PDC_RED; es_text("sabotage"); es_add();
        wrt_init();
        memset(&sv, 0, sizeof sv); strcpy(sv.target, "BAD.PDF"); strcpy(sv.source, src); strcpy(sv.date, "D:20261110090000"); sv.mode = PDSV_INCREMENTAL; sv.edits_seg = SEG_EDS; sv.edits_count = nE;
        pd_fcopy_out(&sv, SEG_XB, 0, sizeof sv);
        do { rr = (pdw)pdw_verb(PDV_WRT_SAVE, SEG_XB, 0, 0); if (g_cop.key_len && !g++) g_cop.key[0] ^= 1; } while (rr == PDE_BUSY && g < 20000);
        CK(!E_enc || rr == PDE_PASSWORD, "a held key that is not the file's makes the verify fail (answer %u)", rr);
        CK(access(PDW_TEMPNAME, F_OK) != 0 && access("BAD.PDF", F_OK) != 0, "...and removes the temp file");
    }
    printf("PDW-ENC S1.PDF S2.PDF\n");
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails;
}
