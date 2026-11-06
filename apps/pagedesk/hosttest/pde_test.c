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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pdmath.h"

unsigned pde_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx);
extern unsigned pde_fail;
extern int pdh_load(const char *path);
extern int pdh_cluster;
extern unsigned long pdh_reads, pdh_failat, pdh_budget, pdh_bytes;

#define SEG_XB    0x2000
#define SEG_CACHE 0x3000
#define SEG_ARENA 0x4000
#define SEG_TBL   0x5000
#define SEG_BUF   0x6000

static int cf;
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
        if (cf && r == PDE_BUSY) { if (++n > 5000) { fprintf(stderr, "OPEN never ends\n"); exit(96); } continue; }
        return cf ? (int)r : 0;
    }
}

static void fnv(unsigned *h, unsigned char c) { *h = (*h ^ c) * 16777619u; }

static void dump_page_content(int pg)
{
    struct pdxb_strm sx;
    struct pdxb_token tkn;
    unsigned idx, r, got, crc, total, ntok, ops, nops;
    unsigned char buf[4096];
    for (idx = 0;; idx++) {
        memset(&sx, 0, sizeof sx);
        sx.page = (pdw)pg; sx.index = (pdw)idx;
        xb_put(&sx, sizeof sx);
        r = call(PDV_ENG_STRM_OPEN, 0, 0, 0);
        if (cf) { if (r != PDE_RANGE) printf("CONTENT %d %u rc=%u\n", pg, idx, r); return; }
        xb_get(&sx, sizeof sx);
        crc = 0xFFFF; total = 0;
        for (;;) {
            got = call(PDV_ENG_STRM_READ, SEG_BUF, 0, 4096);
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
        ntok = 0; ops = 2166136261u; nops = 0;
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
        printf("CONTENT %d %u obj=%u bytes=%u crc=%04x flt=%u declen=%u tokens=%u ops=%u opshash=%08x\n",
               pg, idx, sx.objnum, total, crc, sx.filters, sx.declen.lo, ntok, nops, ops);
    }
}

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

int main(int argc, char **argv)
{
    int i, rc;
    const char *cmd, *file;
    if (argc < 3) { fprintf(stderr, "usage: pde_test dump|open|objs FILE [-c N] [-k KB] [-f N] [-b N]\n"); return 2; }
    cmd = argv[1]; file = argv[2];
    for (i = 3; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "-c")) pdh_cluster = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-k")) cache_kb = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-f")) pdh_failat = (unsigned long)atol(argv[i + 1]);
        else if (!strcmp(argv[i], "-b")) pdh_budget = (unsigned long)atol(argv[i + 1]);
    }
    if (pdh_load(file)) { fprintf(stderr, "cannot read %s\n", file); return 2; }
    if ((rc = do_init())) { printf("INIT rc=%d\n", rc); return 1; }
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
    if (!strcmp(cmd, "dump")) dump();
    else if (!strcmp(cmd, "objs")) objs();
    call(PDV_ENG_CLOSE, 0, 0, 0);
    return 0;
}
