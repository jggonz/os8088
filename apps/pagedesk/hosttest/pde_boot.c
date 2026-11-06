/* ============================================================================
 * os8088 - apps/pagedesk/hosttest/pde_boot.c
 *
 * PDENG running for real: this file is compiled with the TARGET toolchain
 * (smlrcc + tools/cc8086.py, the same gates as the module) into a boot-sector
 * harness, hosttest/pde_boot.asm, which loads PDENG.MOD and a PDF into an
 * emulated 8086-class PC (QEMU) and drives the verbs through the exchange
 * block exactly as the primary will. It prints the SAME lines as
 * `pde_test dump` (pde_test.c) over the serial port, so hosttest/pde_boot.py
 * can diff the two: any difference is a place where 16-bit int, SmallerC's
 * code generation or the hand-written pdmath.inc does not mean what the host's
 * 32-bit build meant.
 * The harness obeys the C rules too (no &local, no struct assignment, frames
 * under 96 bytes, no long): its own output goes through pf(), a fixed-arity
 * printf of %u %d %x %04x %l (a pd_u32 *) and %s.
 * ==========================================================================*/
#include "pdabi.h"
#include "pdmath.h"

unsigned pdb_cf;                        /* CF of the last door call (set by pde_boot.asm) */
unsigned pdb_call(unsigned verb, unsigned bx, unsigned cx, unsigned dx);
void pdb_putc(unsigned c);
unsigned pdb_reads(void);               /* READ_SEQ calls the kernel stub has served */

#define SEG_XB    0x0800
#define SEG_CACHE 0x1000
#define SEG_ARENA 0x1400
#define SEG_TBL   0x1800
#define SEG_BUF   0x1C00

static void ps(const char *s) { while (*s) pdb_putc((unsigned char)*s++); }
static void pu(unsigned v)
{
    static char b[6];
    int i = 0;
    if (v == 0) { pdb_putc('0'); return; }
    while (v) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i) pdb_putc((unsigned char)b[--i]);
}
static void pdec(int v) { if (v < 0) { pdb_putc('-'); pu((unsigned)(-v)); } else pu((unsigned)v); }
static void phex(unsigned v, int width)
{
    int i, d;
    for (i = (width - 1) * 4; i >= 0; i -= 4) { d = (v >> i) & 15; pdb_putc((unsigned)(d < 10 ? '0' + d : 'a' + d - 10)); }
}
static void p32(const struct pd_u32 *a)
{
    static char b[12];
    pdm_u32_todec(b, a, 0);
    ps(b);
}
/* fixed arity: unused arguments are 0. %u %d %x (no padding) %4 (=%04x) %l (pd_u32 *) %s */
static void pf(const char *f, unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5,
               unsigned a6, unsigned a7, unsigned a8, unsigned a9)
{
    static unsigned arg[10];
    int n = 0;
    arg[0] = a0; arg[1] = a1; arg[2] = a2; arg[3] = a3; arg[4] = a4;
    arg[5] = a5; arg[6] = a6; arg[7] = a7; arg[8] = a8; arg[9] = a9;
    while (*f) {
        if (*f != '%') { pdb_putc((unsigned char)*f++); continue; }
        f++;
        switch (*f++) {
        case 'u': pu(arg[n++]); break;
        case 'd': pdec((int)arg[n++]); break;
        case 'x': phex(arg[n++], 4); break;
        case '4': phex(arg[n++], 4); break;
        case 'l': p32((const struct pd_u32 *)arg[n++]); break;
        case 's': ps((const char *)arg[n++]); break;
        case '%': pdb_putc('%'); break;
        }
    }
}

static void xb_put(const void *p, unsigned n) { pd_fcopy_out(p, SEG_XB, 0, (pdw)n); }
static void xb_get(void *p, unsigned n) { pd_fcopy_in(SEG_XB, 0, p, (pdw)n); }
static unsigned call(unsigned v, unsigned bx, unsigned cx, unsigned dx) { return pdb_call(v, bx, cx, dx); }

static struct pdxb_open op;
static struct pdxb_strm sx;
static struct pdxb_token tkn;
static struct pdxb_pageinfo pi;
static struct pdxb_trailer tr;
static struct pdxb_fingerprint fp;
static struct pdxb_init ini;
static unsigned char buf[512];
static struct pd_u32 total, ntok, nops, u1;

static void content(int pg)
{
    unsigned idx, r, got, crc, ops, k;
    for (idx = 0;; idx++) {
        sx.objnum = 0; sx.gen = 0; sx.page = (pdw)pg; sx.index = (pdw)idx; sx.declen.lo = 0; sx.declen.hi = 0; sx.filters = 0;
        xb_put(&sx, sizeof sx);
        r = call(PDV_ENG_STRM_OPEN, 0, 0, 0);
        if (pdb_cf) { if (r != PDE_RANGE) pf("CONTENT %d %u rc=%u\n", pg, idx, r, 0, 0, 0, 0, 0, 0, 0); return; }
        xb_get(&sx, sizeof sx);
        crc = 0xFFFF; pdm_u32_set(&total, 0, 0);
        for (;;) {
            got = call(PDV_ENG_STRM_READ, SEG_BUF, 0, 512);
            if (pdb_cf) { pf("CONTENT %d %u readrc=%u\n", pg, idx, got, 0, 0, 0, 0, 0, 0, 0); break; }
            if (!got) break;
            pd_fcopy_in(SEG_BUF, 0, buf, (pdw)got);
            crc = pdm_crc16((pdw)crc, buf, (pdw)got);
            pdm_u32_add16(&total, (pdw)got);
        }
        call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
        sx.objnum = 0; sx.gen = 0; sx.page = (pdw)pg; sx.index = (pdw)idx;
        xb_put(&sx, sizeof sx);
        call(PDV_ENG_STRM_OPEN, 0, 0, 0);
        pdm_u32_set(&ntok, 0, 0); pdm_u32_set(&nops, 0, 0); ops = 0;
        for (;;) {
            r = call(PDV_ENG_TOKEN, 0, 0, 0);
            xb_get(&tkn, sizeof tkn);
            if (pdb_cf && r != PDE_RANGE) { pf("TOKEN %d %u rc=%u\n", pg, idx, r, 0, 0, 0, 0, 0, 0, 0); break; }
            if (tkn.kind == PDT_EOF) break;
            pdm_u32_add16(&ntok, 1);
            if (tkn.kind == PDT_OP) {
                pdm_u32_add16(&nops, 1);
                for (k = 0; k < tkn.len; k++) ops = ((ops << 5) - ops + (unsigned char)tkn.text[k]) & 0xFFFF;
                ops = ((ops << 5) - ops + ' ') & 0xFFFF;
            }
        }
        call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
        pf("CONTENT %d %u obj=%u bytes=%l crc=%4 flt=%u declen=%u tokens=%l ops=%l opshash=%4\n", (unsigned)pg, idx, 0,
           (unsigned)&total, crc, sx.filters, sx.declen.lo, (unsigned)&ntok, (unsigned)&nops, ops);
    }
}

static struct pdxb_objref rf;
static struct pdxb_objinfo oi;
static struct pdxb_dictget dg;
static struct pdxb_outline ol;
static struct pdxb_annot an;
static unsigned ostk_n[32], ostk_g[32];

static void show_dg(unsigned r)
{
    pdm_u32_mul16(&u1, dg.num.lo, 3125);
    pdm_u32_shr(&u1, &u1, 11);
    pf("GET rc=%u kind=%u num=%d.", r, dg.kind, (unsigned)dg.num.hi, 0, 0, 0, 0, 0, 0, 0);
    { /* %05u of the fraction */
        unsigned f = u1.lo, d = 10000;
        while (d) { pdb_putc('0' + f / d); f %= d; d /= 10; }
    }
    pf(" ref=%u,%u len=%u count=%u text=%s\n", dg.refnum, dg.refgen, dg.len, dg.count, (unsigned)dg.text, 0, 0, 0, 0, 0);
}

static void extras(void)
{
    unsigned n, r, last, p, i;
    int sp = 0, depth = 0;
    last = tr.size < 120 ? tr.size : 120;
    for (n = 1; n < last; n++) {
        rf.objnum = (pdw)n; rf.gen = 0;
        xb_put(&rf, sizeof rf);
        r = call(PDV_ENG_OBJ_FIND, 0, 0, 0);
        if (pdb_cf) pf("OBJ %u rc=%u\n", n, r, 0, 0, 0, 0, 0, 0, 0, 0);
        else {
            xb_get(&oi, sizeof oi);
            pf("OBJ %u kind=%u off=%l len=%l\n", n, oi.kind, (unsigned)&oi.offset, (unsigned)&oi.length, 0, 0, 0, 0, 0, 0);
        }
        for (i = 0; i < sizeof dg; i++) ((unsigned char *)&dg)[i] = 0;
        dg.objnum = (pdw)n;
        xb_put(&dg, sizeof dg);
        r = call(PDV_ENG_DICT_GET, 0, 0, 0);
        xb_get(&dg, sizeof dg);
        show_dg(pdb_cf ? r : 0);
    }
    for (i = 0; i < sizeof ol; i++) ((unsigned char *)&ol)[i] = 0;
    for (;;) {
        xb_put(&ol, sizeof ol);
        r = call(PDV_ENG_OUTLINE, 0, 0, 0);
        if (pdb_cf) { pf("OUTLINE rc=%u\n", r, 0, 0, 0, 0, 0, 0, 0, 0, 0); break; }
        xb_get(&ol, sizeof ol);
        pf("OUTLINE depth=%d obj=%u title=%s count=%u page=%u\n", (unsigned)depth, ol.objnum, (unsigned)ol.title, ol.count, ol.dest_page, 0, 0, 0, 0, 0);
        if (ol.first_num) {
            if (sp >= 31) break;
            ostk_n[sp] = ol.next_num; ostk_g[sp] = ol.next_gen; sp++;
            depth++;
            ol.objnum = ol.first_num; ol.gen = ol.first_gen;
        } else if (ol.next_num) {
            ol.objnum = ol.next_num; ol.gen = ol.next_gen;
        } else {
            n = 0;
            while (sp) {
                sp--; depth--;
                if (ostk_n[sp]) { ol.objnum = ostk_n[sp]; ol.gen = ostk_g[sp]; n = 1; break; }
            }
            if (!n) break;
        }
    }
    for (p = 0; p < op.npages; p++) {
        for (i = 0;; i++) {
            r = call(PDV_ENG_ANNOTS, p, i, 0);
            if (pdb_cf) { if (r != PDE_RANGE) pf("ANNOT %u rc=%u\n", i, r, 0, 0, 0, 0, 0, 0, 0, 0); break; }
            xb_get(&an, sizeof an);
            pf("ANNOT %u obj=%u sub=%s rect=%d,%d,%d,", i, an.objnum, (unsigned)an.subtype, an.rect.x0, an.rect.y0, an.rect.x1, 0, 0, 0, 0);
            pf("%d nm=%s f=%u ap=%u irt=%u state=%u ", an.rect.y1, (unsigned)an.nm, an.flags, an.has_ap, an.irt_num, an.state, 0, 0, 0, 0);
            pf("clen=%u contents=%s\n", an.contents_len, (unsigned)an.contents, 0, 0, 0, 0, 0, 0, 0, 0);
        }
    }
}

void pdb_main(void)
{
    unsigned r, p, n;
    ini.xb_seg = SEG_XB; ini.cache_seg = SEG_CACHE; ini.cache_kb = 16;
    ini.arena_seg = SEG_ARENA; ini.arena_kb = 8; ini.tables_seg = SEG_TBL; ini.tables_kb = 11;
    xb_put(&ini, sizeof ini);
    r = call(PDV_ENG_INIT, SEG_XB, 0, 0);
    if (pdb_cf) { pf("INIT rc=%u\n", r, 0, 0, 0, 0, 0, 0, 0, 0, 0); return; }
    op.state = 0;
    op.name[0] = 'D'; op.name[1] = 'O'; op.name[2] = 'C'; op.name[3] = '.'; op.name[4] = 'P'; op.name[5] = 'D'; op.name[6] = 'F'; op.name[7] = 0;
    for (n = 0; n < 5000; n++) {
        xb_put(&op, sizeof op);
        r = call(PDV_ENG_OPEN, 0, 0, 0);
        xb_get(&op, sizeof op);
        if (pdb_cf && r == PDE_BUSY) continue;
        break;
    }
    if (pdb_cf) {
        pf("OPEN rc=%u err_obj=%u detail=%u reads=%u\n", r, op.err_obj, op.err_detail, pdb_reads(), 0, 0, 0, 0, 0, 0);
        return;
    }
    pf("OPEN ok pages=%u nrev=%u ver=%u flags=%u size=%l reads=%u\n", op.npages, op.nrev, op.version, op.flags,
       (unsigned)&op.size, pdb_reads(), 0, 0, 0, 0);
    call(PDV_ENG_TRAILER, 0, 0, 0); xb_get(&tr, sizeof tr);
    pf("TRAILER size=%u root=%u,%u info=%u,%u startxref=%l id0len=%u ver=%u\n", tr.size, tr.root_num, tr.root_gen,
       tr.info_num, tr.info_gen, (unsigned)&tr.startxref, tr.id0_len, tr.version, 0, 0);
    call(PDV_ENG_FINGERPRINT, 0, 0, 0); xb_get(&fp, sizeof fp);
    pf("FINGERPRINT size=%l head=%4 tail=%4\n", (unsigned)&fp.size, fp.crc_head, fp.crc_tail, 0, 0, 0, 0, 0, 0, 0);
    for (p = 0; p < op.npages; p++) {
        r = call(PDV_ENG_PAGEINFO, p, 0, 0);
        if (pdb_cf) { pf("PAGE %d rc=%u\n", p, r, 0, 0, 0, 0, 0, 0, 0, 0); continue; }
        xb_get(&pi, sizeof pi);
        pf("PAGE %d obj=%u,%u media=", p, pi.objnum, pi.gen, 0, 0, 0, 0, 0, 0, 0);
        pf("%d,%d,%d,%d crop=", pi.media.x0, pi.media.y0, pi.media.x1, pi.media.y1, 0, 0, 0, 0, 0, 0);
        pf("%d,%d,%d,%d ", pi.crop.x0, pi.crop.y0, pi.crop.x1, pi.crop.y1, 0, 0, 0, 0, 0, 0);
        pf("rot=%u annots=%u flags=%u\n", pi.rotate, pi.nannots, pi.flags, 0, 0, 0, 0, 0, 0, 0);
        content((int)p);
    }
    extras();
    call(PDV_ENG_CLOSE, 0, 0, 0);
}
