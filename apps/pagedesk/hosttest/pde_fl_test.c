/* os8088 - apps/pagedesk/hosttest/pde_fl_test.c
 * PDENG's FlateDecode on the host (PAGEDESK-SPEC 20.13): the verbs exactly as
 * the primary will speak them, with the Enhanced tier's flate claim handed in
 * through pdxb_init (flags = PDXI_FLATE and the two words after the struct).
 *
 *   pde_fl_test strm FILE OBJ OUT [-r CHUNK] [-w KB] [-c SECTORS] [-k CACHEKB]
 *       STRM_OPEN OBJ, STRM_READ it CHUNK bytes at a time into OUT. Prints
 *       "STRM rc=<open rc> read=<rc> bytes=<n>". -w 0 withholds the claim.
 *   pde_fl_test nest FILE OBJA OBJB OUTA OUTB [-r CHUNK] [-w KB]
 *       A is opened and read half-way, B is opened inside it and read to the
 *       end, closed, then A is read to the end: two decoders, one engine.
 *   pde_fl_test tok FILE OBJ     STRM_OPEN, then TOKEN to the end: kinds counted.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pdmath.h"

unsigned pde_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx);
extern unsigned pde_fail;
extern int pdh_load(const char *path);
extern int pdh_cluster;
extern unsigned long pdh_reads, pdh_bytes;

#define SEG_XB    0x2000
#define SEG_CACHE 0x3000
#define SEG_ARENA 0x4000
#define SEG_TBL   0x5000
#define SEG_BUF   0x6000
#define SEG_FLATE 0x7000

static int cf;
static unsigned cache_kb = 16, flate_kb = 86;
static unsigned call(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
{
    unsigned r = pde_verb(verb, bx, cx, dx);
    cf = pde_fail != 0;
    return r;
}
static void xb_put(const void *p, unsigned n) { pd_fcopy_out(p, SEG_XB, 0, (pdw)n); }
static void xb_get(void *p, unsigned n) { pd_fcopy_in(SEG_XB, 0, p, (pdw)n); }

static int do_init(void)
{
    struct pdxb_init in;
    pdw ext[2];
    memset(&in, 0, sizeof in);
    in.xb_seg = SEG_XB; in.cache_seg = SEG_CACHE; in.cache_kb = (pdw)cache_kb;
    in.arena_seg = SEG_ARENA; in.arena_kb = 8; in.tables_seg = SEG_TBL; in.tables_kb = 11;
    if (flate_kb) {
        in.flags = 1;                               /* PDXI_FLATE */
        ext[0] = SEG_FLATE; ext[1] = (pdw)flate_kb;
        pd_fcopy_out(ext, SEG_XB, (pdw)sizeof in, 4);
    }
    xb_put(&in, sizeof in);
    call(PDV_ENG_INIT, SEG_XB, 0, 0);
    return cf ? (int)pde_fail : 0;
}

static int do_open(void)
{
    struct pdxb_open op;
    unsigned r; int n = 0;
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

static unsigned strm_open(unsigned num)
{
    struct pdxb_strm sx;
    memset(&sx, 0, sizeof sx);
    sx.objnum = (pdw)num; sx.gen = 0;
    xb_put(&sx, sizeof sx);
    return cf = 0, call(PDV_ENG_STRM_OPEN, 0, 0, 0);
}

/* read up to `limit` bytes (0 = all) in `chunk`-byte calls to fp; returns the read rc */
static unsigned long total;
static unsigned read_some(FILE *fp, unsigned chunk, unsigned long limit)
{
    static unsigned char buf[65536];
    unsigned got;
    while (!limit || total < limit) {
        unsigned want = chunk;
        if (limit && limit - total < want) want = (unsigned)(limit - total);
        got = call(PDV_ENG_STRM_READ, SEG_BUF, 0, want);
        if (cf) return got;
        if (!got) return 0;
        pd_fcopy_in(SEG_BUF, 0, buf, (pdw)got);
        if (fp) fwrite(buf, 1, got, fp);
        total += got;
    }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned chunk = 37, r, rr;
    int i, rc;
    FILE *fa = 0, *fb = 0;
    if (argc < 4) { fprintf(stderr, "usage: pde_fl_test strm|nest|tok FILE OBJ ...\n"); return 2; }
    for (i = 4; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "-r")) chunk = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-w")) flate_kb = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-c")) pdh_cluster = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-k")) cache_kb = (unsigned)atoi(argv[i + 1]);
    }
    if (pdh_load(argv[2])) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
    if ((rc = do_init())) { printf("INIT rc=%d\n", rc); return 1; }
    if ((rc = do_open())) { printf("OPEN rc=%d\n", rc); return 1; }
    if (!strcmp(argv[1], "strm") && argc >= 5) {
        fa = fopen(argv[4], "wb");
        r = strm_open((unsigned)atoi(argv[3]));
        if (cf) { printf("STRM rc=%u\n", r); return 0; }
        total = 0;
        rr = read_some(fa, chunk, 0);
        printf("STRM rc=0 read=%u bytes=%lu\n", cf ? rr : 0, total);
        call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
        fclose(fa);
    } else if (!strcmp(argv[1], "nest") && argc >= 7) {
        unsigned long ta, tb;
        fa = fopen(argv[5], "wb"); fb = fopen(argv[6], "wb");
        r = strm_open((unsigned)atoi(argv[3]));
        if (cf) { printf("NEST A rc=%u\n", r); return 0; }
        total = 0; rr = read_some(fa, chunk, 5000);
        r = strm_open((unsigned)atoi(argv[4]));
        if (cf) { printf("NEST B rc=%u\n", r); return 0; }
        ta = total; total = 0;
        rr = read_some(fb, chunk, 0);
        tb = total;
        call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
        total = ta;
        rr = read_some(fa, chunk, 0);
        printf("NEST rc=0 read=%u A=%lu B=%lu\n", cf ? rr : 0, total, tb);
        call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
        fclose(fa); fclose(fb);
    } else if (!strcmp(argv[1], "stack")) {
        /* STRM_OPEN each OBJ without closing: a third Flate stream open at once is PDE_LIMIT */
        for (i = 3; i < argc && argv[i][0] != '-'; i++) {
            r = strm_open((unsigned)atoi(argv[i]));
            printf("OPEN %s rc=%u\n", argv[i], cf ? r : 0);
        }
    } else if (!strcmp(argv[1], "tok")) {
        struct pdxb_token tkn;
        unsigned n = 0;
        r = strm_open((unsigned)atoi(argv[3]));
        if (cf) { printf("TOK rc=%u\n", r); return 0; }
        for (;;) {
            r = call(PDV_ENG_TOKEN, 0, 0, 0);
            xb_get(&tkn, sizeof tkn);
            if (cf && r != PDE_RANGE) { printf("TOK rc=%u after %u\n", r, n); return 0; }
            if (tkn.kind == PDT_EOF) break;
            n++;
        }
        printf("TOK rc=0 tokens=%u\n", n);
    }
    return 0;
}
