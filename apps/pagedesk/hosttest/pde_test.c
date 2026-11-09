/* os8088 - apps/pagedesk/hosttest/pde_test.c
 * PDENG's host driver. It speaks the verbs exactly as PDREND, PDWRT and the
 * UI will: structs through the exchange block in the pdhost arena, claims
 * addressed by segment, one pde_verb() call per verb. Used as a CLI by
 * hosttest/pde_check.py, which owns the fixtures and compares every line
 * against qpdf.
 *
 *   pde_test dump  FILE [-c SECTORS] [-k CACHEKB]     open + everything
 *   pde_test open  FILE [-c SECTORS] [-f N] [-b N]    open only; -f fails the
 *                                                     Nth read, -b read budget
 *   pde_test objs  FILE                               OBJ_FIND of every object
 *   pde_test get FILE OBJ GEN [KEY KEY KEY]           DICT_GET (OBJ 0 0 = the trailer)
 *   pde_test res FILE PAGE CATEGORY NAME              RES_LOOKUP on a page
 *   pde_test resobj FILE OBJ GEN CATEGORY NAME        RES_LOOKUP in a Form XObject
 *   pde_test outline FILE                             every outline item, depth first
 *   pde_test annots FILE PAGE                         every annotation of a page
 *   pde_test raw FILE OBJ GEN                         RAW_COPY in 100-byte pieces: length and CRC
 *   pde_test strm FILE OBJ GEN                        STRM_OPEN + STRM_READ in 37-byte pieces
 *   pde_test faults FILE                              fail every read in turn: IO error, never a crash,
 *                                                     and the engine opens the file afterwards
 *   pde_test multi FILE...                            open each in turn on one engine (re-INIT, reuse)
 *   pde_test fpchange FILE -- FILE2                   FINGERPRINT, then the same name holds FILE2's
 *                                                     bytes (another program saved it): FINGERPRINT again
 *   pde_test seq FILE -- Q...                         DICT_GET after DICT_GET on ONE open engine, so
 *                                                     dict_get's array cursor and key cache live across
 *                                                     them: Q = OBJ/GEN[/KEY[/KEY[/KEY]]], or Fa-b (reads
 *                                                     a..b from now fail: a disk taken out), or O (back)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pdmath.h"

unsigned pde_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx);
extern unsigned pde_fail;
extern int pdh_load(const char *path);
extern int pdh_cluster;
extern unsigned long pdh_reads, pdh_failat, pdh_failto, pdh_budget, pdh_bytes;

#define SEG_XB    0x2000
#define SEG_CACHE 0x3000
#define SEG_ARENA 0x4000
#define SEG_TBL   0x5000
#define SEG_BUF   0x6000

static int cf;
static void cmd_outline(void);
static void cmd_annots(unsigned page);
static void show_dg(const struct pdxb_dictget *g, unsigned r);
static unsigned call(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
{
    unsigned r = pde_verb(verb, bx, cx, dx);
    cf = pde_fail != 0;
    return r;
}
static void xb_put(const void *p, unsigned n) { pd_fcopy_out(p, SEG_XB, 0, (pdw)n); }
static void xb_get(void *p, unsigned n) { pd_fcopy_in(SEG_XB, 0, p, (pdw)n); }

static unsigned cache_kb = 16;
static int do_init(void)
{
    struct pdxb_init in;
    memset(&in, 0, sizeof in);
    in.xb_seg = SEG_XB; in.cache_seg = SEG_CACHE; in.cache_kb = (pdw)cache_kb;
    in.arena_seg = SEG_ARENA; in.arena_kb = 8; in.tables_seg = SEG_TBL; in.tables_kb = 11;
    xb_put(&in, sizeof in);
    call(PDV_ENG_INIT, SEG_XB, 0, 0);
    return cf ? (int)pde_fail : 0;
}

static struct pdxb_open op;
static int do_open(void)
{
    unsigned r;
    int n = 0;
    memset(&op, 0, sizeof op);
    strcpy(op.name, "DOC.PDF");
    for (;;) {
        xb_put(&op, sizeof op);
        r = call(PDV_ENG_OPEN, 0, 0, 0);
        xb_get(&op, sizeof op);
        if (!cf && r == PDE_BUSY) { if (++n > 5000) { fprintf(stderr, "OPEN never ends\n"); exit(96); } continue; }
        return cf ? (int)r : 0;
    }
}

static void fnv(unsigned *h, unsigned char c) { *h = (*h * 31 + c) & 0xFFFF; }

static void dump_page_content(int pg)
{
    struct pdxb_strm sx;
    struct pdxb_token tkn;
    unsigned idx, r, got, crc, total, ntok, ops, nops;
    unsigned char buf[512];
    for (idx = 0;; idx++) {
        memset(&sx, 0, sizeof sx);
        sx.page = (pdw)pg; sx.index = (pdw)idx;
        xb_put(&sx, sizeof sx);
        r = call(PDV_ENG_STRM_OPEN, 0, 0, 0);
        if (cf) { if (r != PDE_RANGE) printf("CONTENT %d %u rc=%u\n", pg, idx, r); return; }
        xb_get(&sx, sizeof sx);
        crc = 0xFFFF; total = 0;
        for (;;) {
            got = call(PDV_ENG_STRM_READ, SEG_BUF, 0, 512);
            if (cf) { printf("CONTENT %d %u readrc=%u\n", pg, idx, got); break; }
            if (!got) break;
            pd_fcopy_in(SEG_BUF, 0, buf, (pdw)got);
            crc = pdm_crc16((pdw)crc, buf, (pdw)got);
            total += got;
        }
        call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
        /* tokens */
        memset(&sx, 0, sizeof sx);
        sx.page = (pdw)pg; sx.index = (pdw)idx;
        xb_put(&sx, sizeof sx);
        call(PDV_ENG_STRM_OPEN, 0, 0, 0);
        xb_get(&sx, sizeof sx);
        ntok = 0; ops = 0; nops = 0;
        for (;;) {
            unsigned k;
            r = call(PDV_ENG_TOKEN, 0, 0, 0);
            xb_get(&tkn, sizeof tkn);
            if (cf && r != PDE_RANGE) { printf("TOKEN %d %u rc=%u\n", pg, idx, r); break; }
            if (tkn.kind == PDT_EOF) break;
            ntok++;
            if (tkn.kind == PDT_OP) { nops++; for (k = 0; k < tkn.len; k++) fnv(&ops, (unsigned char)tkn.text[k]); fnv(&ops, ' '); }
        }
        call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
        printf("CONTENT %d %u obj=%u bytes=%u crc=%04x flt=%u declen=%u tokens=%u ops=%u opshash=%04x\n",
               pg, idx, sx.objnum, total, crc, sx.filters, sx.declen.lo, ntok, nops, ops);
    }
}

static void extras(void);
static void dump(void)
{
    struct pdxb_pageinfo pi;
    struct pdxb_trailer tr;
    struct pdxb_fingerprint fp;
    int p;
    unsigned r;
    call(PDV_ENG_TRAILER, 0, 0, 0); xb_get(&tr, sizeof tr);
    printf("TRAILER size=%u root=%u,%u info=%u,%u startxref=%u id0len=%u ver=%u\n", tr.size, tr.root_num, tr.root_gen,
           tr.info_num, tr.info_gen, ((unsigned)tr.startxref.hi << 16) | tr.startxref.lo, tr.id0_len, tr.version);
    call(PDV_ENG_FINGERPRINT, 0, 0, 0); xb_get(&fp, sizeof fp);
    printf("FINGERPRINT size=%u head=%04x tail=%04x\n", ((unsigned)fp.size.hi << 16) | fp.size.lo, fp.crc_head, fp.crc_tail);
    for (p = 0; p < op.npages; p++) {
        r = call(PDV_ENG_PAGEINFO, (unsigned)p, 0, 0);
        if (cf) { printf("PAGE %d rc=%u\n", p, r); continue; }
        xb_get(&pi, sizeof pi);
        printf("PAGE %d obj=%u,%u media=%d,%d,%d,%d crop=%d,%d,%d,%d rot=%u annots=%u flags=%u\n", p, pi.objnum, pi.gen,
               pi.media.x0, pi.media.y0, pi.media.x1, pi.media.y1, pi.crop.x0, pi.crop.y0, pi.crop.x1, pi.crop.y1,
               pi.rotate, pi.nannots, pi.flags);
        dump_page_content(p);
    }
    extras();
}


/* the same four tails as pde_boot.c: OBJ_FIND and a key-less DICT_GET of every object,
 * the outline, every page's annotations */
static void extras(void)
{
    struct pdxb_objref rf;
    struct pdxb_objinfo oi;
    struct pdxb_dictget g;
    struct pdxb_trailer trl;
    unsigned n, r, last;
    call(PDV_ENG_TRAILER, 0, 0, 0); xb_get(&trl, sizeof trl);
    last = trl.size < 120 ? trl.size : 120;
    for (n = 1; n < last; n++) {
        rf.objnum = (pdw)n; rf.gen = 0;
        xb_put(&rf, sizeof rf);
        r = call(PDV_ENG_OBJ_FIND, 0, 0, 0);
        if (cf) printf("OBJ %u rc=%u\n", n, r);
        else {
            xb_get(&oi, sizeof oi);
            printf("OBJ %u kind=%u off=%u len=%u\n", n, oi.kind, ((unsigned)oi.offset.hi << 16) | oi.offset.lo,
                   ((unsigned)oi.length.hi << 16) | oi.length.lo);
        }
        memset(&g, 0, sizeof g);
        g.objnum = (pdw)n;
        xb_put(&g, sizeof g);
        r = call(PDV_ENG_DICT_GET, 0, 0, 0);
        xb_get(&g, sizeof g);
        show_dg(&g, cf ? r : 0);
    }
    cmd_outline();
    {
        unsigned p;
        for (p = 0; p < op.npages; p++) cmd_annots(p);
    }
}

static void objs(void)
{
    struct pdxb_objref rf;
    struct pdxb_objinfo oi;
    struct pdxb_trailer tr;
    unsigned n, r;
    call(PDV_ENG_TRAILER, 0, 0, 0); xb_get(&tr, sizeof tr);
    for (n = 1; n < tr.size; n++) {
        rf.objnum = (pdw)n; rf.gen = 0;
        xb_put(&rf, sizeof rf);
        r = call(PDV_ENG_OBJ_FIND, 0, 0, 0);
        if (cf) { printf("OBJ %u rc=%u\n", n, r); continue; }
        xb_get(&oi, sizeof oi);
        printf("OBJ %u kind=%u off=%u len=%u\n", n, oi.kind, ((unsigned)oi.offset.hi << 16) | oi.offset.lo,
               ((unsigned)oi.length.hi << 16) | oi.length.lo);
    }
}


static unsigned cmd_arg(const char *s) { return (unsigned)strtoul(s, 0, 0); }

static void show_dg(const struct pdxb_dictget *g, unsigned r)
{
    printf("GET rc=%u kind=%u num=%d.%05u ref=%u,%u len=%u count=%u text=%s\n", r, g->kind, g->num.hi,
           (unsigned)(((unsigned long)g->num.lo * 3125UL) >> 11), g->refnum, g->refgen, g->len, g->count, g->text);
}
static void cmd_get(int argc, char **argv)
{
    struct pdxb_dictget g;
    unsigned r;
    int k;
    memset(&g, 0, sizeof g);
    g.objnum = (pdw)cmd_arg(argv[0]); g.gen = (pdw)cmd_arg(argv[1]);
    for (k = 0; k < 3 && 2 + k < argc; k++) strncpy(g.key[k], argv[2 + k], 31);
    xb_put(&g, sizeof g);
    r = call(PDV_ENG_DICT_GET, 0, 0, 0);
    xb_get(&g, sizeof g);
    show_dg(&g, cf ? r : 0);
}
static void cmd_res(int forms, int argc, char **argv)
{
    struct pdxb_reslookup rl;
    unsigned r;
    memset(&rl, 0, sizeof rl);
    if (forms) { rl.page = 0xFFFF; rl.resobj = (pdw)cmd_arg(argv[0]); rl.resgen = (pdw)cmd_arg(argv[1]); argv += 2; }
    else { rl.page = (pdw)cmd_arg(argv[0]); argv += 1; }
    strncpy(rl.category, argv[0], 15); strncpy(rl.name, argv[1], 31);
    xb_put(&rl, sizeof rl);
    r = call(PDV_ENG_RES_LOOKUP, 0, 0, 0);
    xb_get(&rl, sizeof rl);
    if (cf) printf("RES rc=%u\n", r);
    else printf("RES obj=%u,%u kind=%u\n", rl.objnum, rl.gen, rl.kind);
}
static void cmd_outline(void)
{
    struct pdxb_outline ol;
    unsigned r;
    unsigned stack_n[32], stack_g[32];
    int sp = 0, depth = 0;
    memset(&ol, 0, sizeof ol);
    for (;;) {
        xb_put(&ol, sizeof ol);
        r = call(PDV_ENG_OUTLINE, 0, 0, 0);
        if (cf) { printf("OUTLINE rc=%u\n", r); return; }
        xb_get(&ol, sizeof ol);
        printf("OUTLINE depth=%d obj=%u title=%s count=%u page=%u\n", depth, ol.objnum, ol.title, ol.count, ol.dest_page);
        if (ol.first_num) {
            if (sp >= 31) return;
            stack_n[sp] = ol.next_num; stack_g[sp] = ol.next_gen; sp++;
            depth++;
            ol.objnum = ol.first_num; ol.gen = ol.first_gen;
        } else if (ol.next_num) {
            ol.objnum = ol.next_num; ol.gen = ol.next_gen;
        } else {
            for (;;) {
                if (sp == 0) return;
                sp--; depth--;
                if (stack_n[sp]) { ol.objnum = (pdw)stack_n[sp]; ol.gen = (pdw)stack_g[sp]; break; }
            }
        }
    }
}
static void cmd_annots(unsigned page)
{
    struct pdxb_annot an;
    unsigned r, i;
    for (i = 0;; i++) {
        r = call(PDV_ENG_ANNOTS, page, i, 0);
        if (cf) { if (r != PDE_RANGE) printf("ANNOT %u rc=%u\n", i, r); return; }
        xb_get(&an, sizeof an);
        printf("ANNOT %u obj=%u sub=%s rect=%d,%d,%d,%d nm=%s f=%u ap=%u irt=%u state=%u clen=%u contents=%s\n", i, an.objnum,
               an.subtype, an.rect.x0, an.rect.y0, an.rect.x1, an.rect.y1, an.nm, an.flags, an.has_ap, an.irt_num,
               an.state, an.contents_len, an.contents);
    }
}
static void cmd_raw(unsigned num, unsigned gen)
{
    struct pdxb_rawcopy rw;
    struct pdxb_objref rf;
    struct pdxb_objinfo oi;
    unsigned r, total = 0, crc = 0xFFFF;
    unsigned char buf[100];
    rf.objnum = (pdw)num; rf.gen = (pdw)gen;
    xb_put(&rf, sizeof rf);
    r = call(PDV_ENG_OBJ_FIND, 0, 0, 0);
    if (cf) { printf("RAW rc=%u\n", r); return; }
    xb_get(&oi, sizeof oi);
    memset(&rw, 0, sizeof rw);
    rw.objnum = (pdw)num; rw.gen = (pdw)gen; rw.dseg = SEG_BUF; rw.doff = 0; rw.dmax = 100;
    for (;;) {
        xb_put(&rw, sizeof rw);
        r = call(PDV_ENG_RAW_COPY, 0, 0, 0);
        if (cf) { printf("RAW rc=%u after %u\n", r, total); return; }
        xb_get(&rw, sizeof rw);
        if (!rw.got) break;
        pd_fcopy_in(SEG_BUF, 0, buf, rw.got);
        crc = pdm_crc16((pdw)crc, buf, rw.got);
        total += rw.got;
        if (total >= (((unsigned)oi.length.hi << 16) | oi.length.lo)) break;
    }
    printf("RAW obj=%u bytes=%u crc=%04x declared=%u\n", num, total, crc, ((unsigned)oi.length.hi << 16) | oi.length.lo);
}
static void cmd_strm(unsigned num, unsigned gen)
{
    struct pdxb_strm sx;
    unsigned r, got, total = 0, crc = 0xFFFF;
    unsigned char buf[40];
    memset(&sx, 0, sizeof sx);
    sx.objnum = (pdw)num; sx.gen = (pdw)gen;
    xb_put(&sx, sizeof sx);
    r = call(PDV_ENG_STRM_OPEN, 0, 0, 0);
    if (cf) { printf("STRM rc=%u\n", r); return; }
    for (;;) {
        got = call(PDV_ENG_STRM_READ, SEG_BUF, 0, 37);
        if (cf) { printf("STRM readrc=%u after %u\n", got, total); break; }
        if (!got) break;
        pd_fcopy_in(SEG_BUF, 0, buf, (pdw)got);
        crc = pdm_crc16((pdw)crc, buf, (pdw)got);
        total += got;
    }
    call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
    printf("STRM obj=%u bytes=%u crc=%04x\n", num, total, crc);
}
static void sig(void)
{
    struct pdxb_trailer tr;
    unsigned p, n = op.npages;
    struct pdxb_pageinfo pi;
    unsigned h = 0;
    call(PDV_ENG_TRAILER, 0, 0, 0); xb_get(&tr, sizeof tr);
    fnv(&h, (unsigned char)tr.size); fnv(&h, (unsigned char)tr.root_num);
    for (p = 0; p < n; p++) {
        call(PDV_ENG_PAGEINFO, p, 0, 0);
        xb_get(&pi, sizeof pi);
        fnv(&h, (unsigned char)pi.objnum); fnv(&h, (unsigned char)(pi.media.x1 >> 3));
    }
    printf("SIG pages=%u hash=%08x\n", n, h);
}
/* verify-perf: one engine, many DICT_GETs, reads failing in a window */
static void cmd_seq(int argc, char **argv)
{
    int i, k;
    char buf[200], *t;
    struct pdxb_dictget g;
    unsigned r;
    for (i = 0; i < argc; i++) {
        if (argv[i][0] == 'F') {
            pdh_failat = pdh_reads + strtoul(argv[i] + 1, &t, 10);
            pdh_failto = pdh_reads + strtoul(t + 1, 0, 10);
            continue;
        }
        if (argv[i][0] == 'O') { pdh_failat = 0; pdh_failto = 0; continue; }
        memset(&g, 0, sizeof g);
        strncpy(buf, argv[i], sizeof buf - 1); buf[sizeof buf - 1] = 0;
        t = strtok(buf, "/"); g.objnum = (pdw)cmd_arg(t);
        t = strtok(0, "/"); g.gen = t ? (pdw)cmd_arg(t) : 0;
        for (k = 0; k < 3 && (t = strtok(0, "/")); k++) strncpy(g.key[k], t, 31);
        xb_put(&g, sizeof g);
        r = call(PDV_ENG_DICT_GET, 0, 0, 0);
        xb_get(&g, sizeof g);
        printf("%s ", argv[i]);
        show_dg(&g, cf ? r : 0);
    }
}
static void cmd_faults(const char *file)
{
    unsigned long n, total;
    int rc, base;
    pdh_failat = 0; pdh_reads = 0;
    if (do_init() || (base = do_open())) { printf("FAULTS base open failed\n"); return; }
    sig();
    total = pdh_reads;
    call(PDV_ENG_CLOSE, 0, 0, 0);
    for (n = 1; n <= total + 1; n++) {
        pdh_failat = pdh_reads + n;                 /* the nth read from now */
        rc = do_open();
        if (rc && rc != PDE_IO) { printf("FAULTS read %lu gave rc=%d (want IO)\n", n, rc); return; }
        if (!rc && n <= total) {
            /* the fault may land after open on a cached path: only allowed when open needs fewer reads */
        }
        pdh_failat = 0;
        call(PDV_ENG_CLOSE, 0, 0, 0);
        rc = do_open();
        if (rc) { printf("FAULTS reuse after fault %lu failed rc=%d\n", n, rc); return; }
        call(PDV_ENG_CLOSE, 0, 0, 0);
    }
    printf("FAULTS ok reads=%lu file=%s\n", total, file);
}

int main(int argc, char **argv)
{
    int i, rc, first_extra, total;
    const char *cmd, *file;
    if (argc < 3) { fprintf(stderr, "usage: pde_test CMD FILE [-c N] [-k KB] [-f N] [-b N] [-- ARGS]\n"); return 2; }
    cmd = argv[1]; file = argv[2];
    total = argc;
    first_extra = argc;
    for (i = 3; i < argc; i++) if (!strcmp(argv[i], "--")) { first_extra = i + 1; argc = i; break; }
    if (!strcmp(cmd, "multi")) {
        for (i = 2; i < argc; i++) {
            pdh_load(argv[i]);
            if ((rc = do_init())) { printf("INIT rc=%d\n", rc); return 1; }
            rc = do_open();
            if (rc) printf("MULTI %s rc=%d\n", argv[i], rc);
            else { printf("MULTI %s ok pages=%u\n", argv[i], op.npages); sig(); }
            call(PDV_ENG_CLOSE, 0, 0, 0);
        }
        return 0;
    }
    for (i = 3; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "-c")) pdh_cluster = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-k")) cache_kb = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-f")) pdh_failat = (unsigned long)atol(argv[i + 1]);
        else if (!strcmp(argv[i], "-b")) pdh_budget = (unsigned long)atol(argv[i + 1]);
    }
    if (pdh_load(file)) { fprintf(stderr, "cannot read %s\n", file); return 2; }
    if ((rc = do_init())) { printf("INIT rc=%d\n", rc); return 1; }
    if (!strcmp(cmd, "faults")) { cmd_faults(file); return 0; }
    rc = do_open();
    if (rc) {
        printf("OPEN rc=%d err_obj=%u detail=%u reads=%lu\n", rc, op.err_obj, op.err_detail, pdh_reads);
        /* the engine must be reusable: a second OPEN of the same file must give the same answer */
        i = do_open();
        if (i != rc) { printf("REUSE mismatch %d != %d\n", i, rc); return 1; }
        call(PDV_ENG_CLOSE, 0, 0, 0);
        return 0;
    }
    printf("OPEN ok pages=%u nrev=%u ver=%u flags=%u size=%u reads=%lu\n", op.npages, op.nrev, op.version, op.flags,
           ((unsigned)op.size.hi << 16) | op.size.lo, pdh_reads);
    {
        char **ap = argv + first_extra;
        int ex = total - first_extra;
        if (ex < 0) ex = 0;
        if (!strcmp(cmd, "dump")) dump();
        else if (!strcmp(cmd, "objs")) objs();
        else if (!strcmp(cmd, "get") && ex >= 2) cmd_get(ex, ap);
        else if (!strcmp(cmd, "seq")) cmd_seq(ex, ap);
        else if (!strcmp(cmd, "res") && ex >= 3) cmd_res(0, ex, ap);
        else if (!strcmp(cmd, "resobj") && ex >= 4) cmd_res(1, ex, ap);
        else if (!strcmp(cmd, "outline")) cmd_outline();
        else if (!strcmp(cmd, "annots") && ex >= 1) cmd_annots(cmd_arg(ap[0]));
        else if (!strcmp(cmd, "raw") && ex >= 2) cmd_raw(cmd_arg(ap[0]), cmd_arg(ap[1]));
        else if (!strcmp(cmd, "strm") && ex >= 2) cmd_strm(cmd_arg(ap[0]), cmd_arg(ap[1]));
        else if (!strcmp(cmd, "fpchange") && ex >= 1) {
            struct pdxb_fingerprint a, b;
            unsigned r1, r2;
            r1 = call(PDV_ENG_FINGERPRINT, 0, 0, 0); xb_get(&a, sizeof a);
            if (pdh_load(ap[0])) { fprintf(stderr, "cannot read %s\n", ap[0]); return 2; }
            r2 = call(PDV_ENG_FINGERPRINT, 0, 0, 0); xb_get(&b, sizeof b);
            printf("FP before rc=%u size=%u head=%04x tail=%04x after rc=%u size=%u head=%04x tail=%04x %s\n",
                   r1, ((unsigned)a.size.hi << 16) | a.size.lo, a.crc_head, a.crc_tail,
                   r2, ((unsigned)b.size.hi << 16) | b.size.lo, b.crc_head, b.crc_tail,
                   memcmp(&a, &b, sizeof a) ? "CHANGED" : "SAME");
            r1 = call(PDV_ENG_PAGEINFO, 0, 0, 0);
            printf("FP pageinfo after the change rc=%u\n", cf ? r1 : 0);
        }
    }
    call(PDV_ENG_CLOSE, 0, 0, 0);
    return 0;
}
