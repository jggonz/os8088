/* ============================================================================
 * os8088 - apps/pagedesk/hosttest/pde_fl_boot.c
 *
 * PDENG's FlateDecode running for real (PAGEDESK-SPEC 20.13): compiled with the
 * TARGET toolchain into pde_boot.asm's boot-sector harness (hosttest/pde_fl_boot.py
 * builds it), it loads PDENG.MOD and a PDF into an emulated PC (QEMU), gives the
 * engine the flate claim and prints, for objects 10, 11 and 12 of the PDF, the
 * length and CRC of what the engine delivers at three read sizes, a nested
 * read of 10 inside 11 and the BIOS ticks a read took. fl_check's host build
 * prints the same CRCs; any difference is a place where 16-bit int, SmallerC's
 * code or pdmath.inc does not mean what the host's 32-bit build meant.
 * Obeys the C rules (no &local, no struct assignment, frames under 96 bytes).
 * ==========================================================================*/
#include "pdabi.h"
#include "pdmath.h"

unsigned pdb_cf;
unsigned pdb_call(unsigned verb, unsigned bx, unsigned cx, unsigned dx);
void pdb_putc(unsigned c);

#define SEG_XB    0x0800
#define SEG_CACHE 0x1000
#define SEG_ARENA 0x1400
#define SEG_TBL   0x1800
#define SEG_BUF   0x1C00
#define SEG_FLATE 0x7A00

static void ps(const char *s) { while (*s) pdb_putc((unsigned char)*s++); }
static void pu(unsigned v)
{
    static char b[6];
    int i = 0;
    if (v == 0) { pdb_putc('0'); return; }
    while (v) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i) pdb_putc((unsigned char)b[--i]);
}
static void px(unsigned v)
{
    int i, d;
    for (i = 12; i >= 0; i -= 4) { d = (v >> i) & 15; pdb_putc((unsigned)(d < 10 ? '0' + d : 'a' + d - 10)); }
}
static void xb_put(const void *p, unsigned n) { pd_fcopy_out(p, SEG_XB, 0, (pdw)n); }
static void xb_get(void *p, unsigned n) { pd_fcopy_in(SEG_XB, 0, p, (pdw)n); }
static unsigned call(unsigned v, unsigned bx, unsigned cx, unsigned dx) { return pdb_call(v, bx, cx, dx); }

static struct pdxb_open op;
static struct pdxb_strm sx;
static struct pdxb_init ini;
static unsigned char buf[512];
static unsigned ext[2];
static unsigned crc, nlo, nhi, rc;

static unsigned tick(void) { return pd_fpeekw(0x40, 0x6C); }
unsigned pdb_pit(void);                          /* PIT channel 0's count (pde_fl_boot.py adds it to the harness) */

static unsigned strm_open(unsigned obj)
{
    sx.objnum = (pdw)obj; sx.gen = 0; sx.page = 0; sx.index = 0; sx.declen.lo = 0; sx.declen.hi = 0; sx.filters = 0;
    xb_put(&sx, sizeof sx);
    return call(PDV_ENG_STRM_OPEN, 0, 0, 0);
}

/* read up to `limit` bytes (0 = to the end) in `chunk`-byte calls; crc/nlo/nhi accumulate; rc = the read's failure or 0 */
static void drain(unsigned chunk, unsigned limit)
{
    unsigned got, want;
    rc = 0;
    for (;;) {
        want = chunk;
        if (limit && limit - nlo < want) want = limit - nlo;
        if (!want) return;
        got = call(PDV_ENG_STRM_READ, SEG_BUF, 0, want);
        if (pdb_cf) { rc = got; return; }
        if (!got) return;
        pd_fcopy_in(SEG_BUF, 0, buf, (pdw)got);
        crc = pdm_crc16((pdw)crc, buf, (pdw)got);
        if ((nlo += got) < got) nhi++;
    }
}

static void line(const char *tag, unsigned obj, unsigned chunk, unsigned t)
{
    ps(tag); pdb_putc(' '); pu(obj); pdb_putc(' '); pu(chunk);
    ps(" rc="); pu(rc); ps(" bytes="); pu(nhi); pdb_putc(':'); pu(nlo); ps(" crc="); px(crc);
    if (t != 0xFFFF) { ps(" ticks="); pu(t); }
    pdb_putc('\n');
}

void pdb_main(void)
{
    static unsigned chunks[3];
    unsigned r, k, t0, obj, ra, na, ca;
    ini.xb_seg = SEG_XB; ini.cache_seg = SEG_CACHE; ini.cache_kb = 16;
    ini.arena_seg = SEG_ARENA; ini.arena_kb = 8; ini.tables_seg = SEG_TBL; ini.tables_kb = 11;
    ini.flags = 1;                                  /* PDXI_FLATE: the two words after the struct */
    ext[0] = SEG_FLATE; ext[1] = 86;
    xb_put(&ini, sizeof ini);
    pd_fcopy_out(ext, SEG_XB, (pdw)sizeof ini, 4);
    call(PDV_ENG_INIT, SEG_XB, 0, 0);
    if (pdb_cf) { ps("INIT failed\n"); return; }
    op.state = 0;
    op.name[0] = 'D'; op.name[1] = 'O'; op.name[2] = 'C'; op.name[3] = '.'; op.name[4] = 'P'; op.name[5] = 'D'; op.name[6] = 'F'; op.name[7] = 0;
    for (k = 0; k < 5000; k++) {
        xb_put(&op, sizeof op);
        r = call(PDV_ENG_OPEN, 0, 0, 0);
        xb_get(&op, sizeof op);
        if (!pdb_cf && r == PDE_BUSY) continue;
        break;
    }
    if (pdb_cf) { ps("OPEN failed "); pu(r); pdb_putc('\n'); return; }
    chunks[0] = 7; chunks[1] = 64; chunks[2] = 512;
    for (obj = 10; obj <= 12; obj++) {
        for (k = 0; k < 3; k++) {
            r = strm_open(obj);
            crc = 0xFFFF; nlo = 0; nhi = 0; rc = 0;
            if (pdb_cf) { rc = r; line("OPEN", obj, chunks[k], 0xFFFF); continue; }
            t0 = tick();
            drain(chunks[k], 0);
            r = tick() - t0;
            call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
            line("READ", obj, chunks[k], r);
        }
    }
    /* object 10 half-way, 11 inside it to the end, then the rest of 10 */
    strm_open(10);
    crc = 0xFFFF; nlo = 0; nhi = 0; drain(100, 700);
    ra = rc; na = nlo; ca = crc;
    r = strm_open(11);
    if (pdb_cf) { rc = r; crc = 0; nlo = 0; nhi = 0; line("NESTOPEN", 11, 0, 0xFFFF); }
    else {
        crc = 0xFFFF; nlo = 0; nhi = 0; drain(100, 0);
        line("NESTB", 11, 100, 0xFFFF);
        call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
    }
    crc = ca; nlo = na; nhi = 0; drain(100, 0);
    line("NESTA", 10, 100, 0xFFFF);
    call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
    /* throughput: objects 13 (Flate text), 14 (the same kind of text, unfiltered), 15 (a PNG-predicted image) */
    for (obj = 13; obj <= 15; obj++) {
        r = strm_open(obj);
        crc = 0xFFFF; nlo = 0; nhi = 0; rc = 0;
        if (pdb_cf) { rc = r; line("OPEN", obj, 512, 0xFFFF); continue; }
        t0 = tick(); ra = pdb_pit();
        drain(512, 0);
        na = pdb_pit(); r = tick() - t0;
        call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
        ps("TIME "); pu(obj); ps(" rc="); pu(rc); ps(" bytes="); pu(nhi); pdb_putc(':'); pu(nlo); ps(" crc="); px(crc);
        ps(" ticks="); pu(r); ps(" pit0="); pu(ra); ps(" pit1="); pu(na); pdb_putc('\n');
    }
    call(PDV_ENG_CLOSE, 0, 0, 0);
}
