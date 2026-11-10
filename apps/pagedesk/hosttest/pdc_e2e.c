/* os8088 - apps/pagedesk/hosttest/pdc_e2e.c
 * Encryption, end to end on the host (PAGEDESK-SPEC 20.22.5): the REAL PDENG and
 * the REAL PDXK (both compiled -DPD_HOST, the crypto blocks being pdctwin.c's)
 * speak to each other exactly as the primary will: OPEN answers PDE_PASSWORD,
 * PDXK's KEY_AUTH (calling PDENG's CRYPTINFO through the vector) finds the key,
 * SETKEY, OPEN resumes. Then everything decrypted is printed as lines that
 * tests/pdcrypt/pdc_e2e.py compares with the SAME driver run over the file
 * qpdf --decrypt wrote: every stream's decoded CRC, every page's content
 * streams, every string a dictionary key gives, every annotation.
 *
 *   pdc_e2e dump FILE [PASSWORD|-]    open (with the password), print everything
 *   pdc_e2e auth FILE PASSWORD        only the authentication: prints rc, auth, the key in hex
 *   pdc_e2e cost FILE                 PDV_KEY_COST for the empty password and a typical one
 *   pdc_e2e cryptop MODE ...          PDV_ENG_CRYPT round trips (see main)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pdmath.h"

unsigned pde_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx);
unsigned pdk_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx);
extern unsigned pde_fail;
extern pdw pdk_cf, pdk_engcf, pdk_engvec[2];
extern int pdh_load(const char *path);
extern int pdh_cluster;
extern unsigned long pdh_reads;

#define SEG_XB    0x2000
#define SEG_CACHE 0x3000
#define SEG_ARENA 0x4000
#define SEG_TBL   0x5000
#define SEG_BUF   0x6000
#define SEG_FLATE 0x8000

static int cf;
static unsigned call(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
{
    unsigned r = pde_verb(verb, bx, cx, dx);
    cf = pde_fail != 0;
    return r;
}
static unsigned kcall(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
{
    unsigned r = pdk_verb(verb, bx, cx, dx);
    cf = pdk_cf != 0;
    return r;
}
/* PDXK reaches PDENG through here (pdxk.asm's _pdk_engcall on the target) */
pdw pdk_engcall(pdw verb, pdw bx, pdw cx, pdw dx)
{
    unsigned r = pde_verb(verb, bx, cx, dx);
    pdk_engcf = pde_fail != 0;
    return (pdw)r;
}
static void xb_put(const void *p, unsigned n) { pd_fcopy_out(p, SEG_XB, 0, (pdw)n); }
static void xb_get(void *p, unsigned n) { pd_fcopy_in(SEG_XB, 0, p, (pdw)n); }

static int do_init(void)
{
    struct pdxb_init in;
    memset(&in, 0, sizeof in);
    in.xb_seg = SEG_XB; in.cache_seg = SEG_CACHE; in.cache_kb = 16;
    in.arena_seg = SEG_ARENA; in.arena_kb = 8; in.tables_seg = SEG_TBL; in.tables_kb = 11;
    in.flags = PDXI_FLATE; in.flate_seg = SEG_FLATE; in.flate_kb = 86;
    xb_put(&in, sizeof in);
    call(PDV_ENG_INIT, SEG_XB, 0, 0);
    if (cf) return (int)pde_fail;
    in.cpu = 0;
    xb_put(&in, sizeof in);
    kcall(PDV_X_INIT, SEG_XB, 0, 0);
    return 0;
}

static struct pdxb_open op;
static int open_loop(void)
{
    unsigned r;
    int n = 0;
    for (;;) {
        xb_put(&op, sizeof op);
        r = call(PDV_ENG_OPEN, 0, 0, 0);
        xb_get(&op, sizeof op);
        if (!cf && r == PDE_BUSY) { if (++n > 5000) { fprintf(stderr, "OPEN never ends\n"); exit(96); } continue; }
        return cf ? (int)r : 0;
    }
}

static struct pdxb_crypt cr;
static unsigned long slices;
/* KEY_AUTH over the XB until it stops being busy. Returns the PDE code. */
static int do_auth(const char *pw)
{
    unsigned r;
    memset(&cr, 0, sizeof cr);
    cr.pw_len = (pdw)(strcmp(pw, "-") ? strlen(pw) : 0);
    memcpy(cr.pw, strcmp(pw, "-") ? pw : "", cr.pw_len);
    slices = 0;
    for (;;) {
        xb_put(&cr, sizeof cr);
        r = kcall(PDV_KEY_AUTH, 0, 0, 0);
        xb_get(&cr, sizeof cr);
        slices++;
        if (!cf && r == PDE_BUSY) { if (slices > 200000) { fprintf(stderr, "AUTH never ends\n"); exit(95); } continue; }
        return cf ? (int)r : 0;
    }
}

static void hex(const unsigned char *p, unsigned n) { while (n--) printf("%02x", *p++); }

static void do_setkey(void)
{
    xb_put(&cr, sizeof cr);
    call(PDV_ENG_SETKEY, 0, 0, 0);
    if (cf) { printf("SETKEY rc=%u\n", pde_fail); exit(2); }
}

static unsigned crc_of(unsigned char *buf, unsigned n, unsigned crc) { return pdm_crc16((pdw)crc, buf, (pdw)n); }

static void dump_stream(unsigned obj, unsigned gen, int page, int idx)
{
    struct pdxb_strm sx;
    unsigned r, got, crc = 0xFFFF, total = 0;
    unsigned char buf[512];
    memset(&sx, 0, sizeof sx);
    sx.objnum = (pdw)obj; sx.gen = (pdw)gen; sx.page = (pdw)page; sx.index = (pdw)idx;
    xb_put(&sx, sizeof sx);
    r = call(PDV_ENG_STRM_OPEN, 0, 0, 0);
    if (cf) { if (obj == 0 && r != PDE_RANGE) printf("CONTENT %d %d rc=%u\n", page, idx, r); return; }
    xb_get(&sx, sizeof sx);
    for (;;) {
        got = call(PDV_ENG_STRM_READ, SEG_BUF, 0, 512);
        if (cf) { printf("STREAM %u readrc=%u\n", obj, got); break; }
        if (!got) break;
        pd_fcopy_in(SEG_BUF, 0, buf, (pdw)got);
        crc = crc_of(buf, got, crc);
        total += got;
    }
    call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
    if (obj) printf("STREAM %u bytes=%u crc=%04x flt=%u\n", obj, total, crc, sx.filters & ~PDFL_CRYPT);
    else printf("CONTENT %d %d bytes=%u crc=%04x\n", page, idx, total, crc);
}

static void dump_strings(unsigned obj)
{
    static const char *keys[] = { "Title", "Author", "Subject", "Keywords", "Creator", "Producer", "Contents", "T", "NM", "Subj", "TU", "CreationDate", "M" };
    struct pdxb_dictget g;
    unsigned k, r;
    for (k = 0; k < sizeof keys / sizeof *keys; k++) {
        memset(&g, 0, sizeof g);
        g.objnum = (pdw)obj; g.gen = 0;
        strcpy(g.key[0], keys[k]);
        xb_put(&g, sizeof g);
        r = call(PDV_ENG_DICT_GET, 0, 0, 0);
        xb_get(&g, sizeof g);
        if (cf || g.kind != PDK_STR) continue;
        printf("STR %u /%s len=%u ", obj, keys[k], g.len);
        hex((unsigned char *)g.text, g.len < 127 ? g.len : 127);
        printf("\n");
        (void)r;
    }
}

static void dump_outline(void)
{
    struct pdxb_outline ol;
    unsigned r, stack_n[32], stack_g[32];
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

static void dump_all(void)
{
    struct pdxb_trailer tr;
    struct pdxb_pageinfo pi;
    struct pdxb_annot an;
    unsigned p, a, o, r, maxo;
    call(PDV_ENG_TRAILER, 0, 0, 0);
    xb_get(&tr, sizeof tr);
    printf("TRAILER root=%u info=%u\n", tr.root_num, tr.info_num);
    printf("OPEN pages=%u flags=%04x\n", op.npages, op.flags & ~PDO_ENCRYPTED);
    for (p = 0; p < op.npages; p++) {
        memset(&pi, 0, sizeof pi);
        pi.objnum = (pdw)p;                      /* BX carries the page; see below */
        r = call(PDV_ENG_PAGEINFO, p, 0, 0);
        xb_get(&pi, sizeof pi);
        printf("PAGE %u rc=%u obj=%u\n", p, cf ? r : 0, pi.objnum);
        for (a = 0;; a++) {
            r = call(PDV_ENG_ANNOTS, p, a, 0);
            if (cf) break;
            xb_get(&an, sizeof an);
            printf("ANNOT %u %u obj=%u %s nm=%s len=%u ", p, a, an.objnum, an.subtype, an.nm, an.contents_len);
            hex((unsigned char *)an.contents, an.contents_len < 160 ? an.contents_len : 160);
            printf("\n");
        }
        for (a = 0; a < 16; a++) dump_stream(0, 0, (int)p, (int)a);
    }
    if (op.flags & PDO_OUTLINE) dump_outline();
    maxo = tr.size < 400 ? tr.size : 400;
    for (o = 1; o < maxo; o++) {
        dump_stream(o, 0, 0, 0);
        dump_strings(o);
    }
    dump_strings(tr.info_num);
}

int main(int argc, char **argv)
{
    int rc;
    if (argc < 3) { fprintf(stderr, "usage: pdc_e2e dump|auth|cost FILE [PASSWORD|-]\n"); return 2; }
    if (pdh_load(argv[2])) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
    if (do_init()) { printf("INIT failed\n"); return 2; }
    memset(&op, 0, sizeof op);
    strcpy(op.name, "DOC.PDF");
    rc = open_loop();
    if (rc == PDE_PASSWORD) {
        const char *pw = argc > 3 ? argv[3] : "-";
        if (!strcmp(argv[1], "cost")) {
            memset(&cr, 0, sizeof cr);
            call(PDV_ENG_CRYPTINFO, 0, 0, 0); xb_get(&cr, sizeof cr);
            cr.pw_len = 0; xb_put(&cr, sizeof cr);
            printf("COST r=%u empty=%u s\n", cr.r, kcall(PDV_KEY_COST, 0, 0, 0));
            cr.pw_len = 8; xb_put(&cr, sizeof cr);
            printf("COST r=%u 8chars=%u s\n", cr.r, kcall(PDV_KEY_COST, 0, 0, 0));
            return 0;
        }
        rc = do_auth(pw);
        if (rc) { printf("AUTH rc=%d\n", rc); return 3; }
        printf("AUTH ok auth=%u keylen=%u stm=%u str=%u r=%u slices=%lu key=", cr.auth, cr.key_len, cr.stm, cr.str, cr.r, slices);
        hex(cr.key, cr.key_len);
        printf("\n");
        if (!strcmp(argv[1], "auth")) return 0;
        do_setkey();
        rc = open_loop();
    } else if (!strcmp(argv[1], "auth")) { printf("NOT ENCRYPTED rc=%d\n", rc); return 0; }
    if (rc) { printf("OPEN rc=%d\n", rc); return 4; }
    dump_all();
    call(PDV_ENG_CLOSE, 0, 0, 0);
    return 0;
}
