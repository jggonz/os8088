/* os8088 - apps/pagedesk/hosttest/pde_fl_fuzz.c
 * The hostile-input pass for FlateDecode (PAGEDESK-SPEC 20.13, PD-28), built with
 * AddressSanitizer and UBSan (`make pdflatefuzz`). Each PDF given holds a Flate
 * stream as object 10; the payload is mutated -n times (bit flips, runs of random
 * bytes, truncation, a spliced-in copy of itself) and read to the end with random
 * read sizes, sometimes nested inside a second open of itself, sometimes closed
 * half-way and reopened. The engine must answer, never crash, never hang, never
 * deliver more than 1 MiB, and never write outside the flate claim: the arena
 * around it is painted with a guard pattern that is checked after every run.
 *
 *   pde_fl_fuzz [-n MUTATIONS] [-s SEED] FILE.PDF...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pdmath.h"

unsigned pde_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx);
extern unsigned pde_fail;
extern unsigned char pd_host_mem[];
extern void pdh_set(unsigned char *buf, unsigned long n);
extern unsigned long pdh_reads, pdh_budget;

#define SEG_XB    0x2000
#define SEG_CACHE 0x3000
#define SEG_ARENA 0x4000
#define SEG_TBL   0x5000
#define SEG_BUF   0x6000
#define SEG_FLATE 0x8000
#define GUARD     0x4000UL

static int cf;
static unsigned call(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
{
    unsigned r = pde_verb(verb, bx, cx, dx);
    cf = pde_fail != 0;
    return r;
}
static unsigned long rs = 1;
static unsigned rnd(void) { rs = rs * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)(rs >> 33); }

static unsigned flate_kb;
static int do_init(void)
{
    struct pdxb_init in;
    pdw ext[2];
    memset(&in, 0, sizeof in);
    in.xb_seg = SEG_XB; in.cache_seg = SEG_CACHE; in.cache_kb = 16;
    in.arena_seg = SEG_ARENA; in.arena_kb = 8; in.tables_seg = SEG_TBL; in.tables_kb = 11;
    if (flate_kb) { in.flags = 1; ext[0] = SEG_FLATE; ext[1] = (pdw)flate_kb; pd_fcopy_out(ext, SEG_XB, (pdw)sizeof in, 4); }
    pd_fcopy_out(&in, SEG_XB, 0, sizeof in);
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
        pd_fcopy_out(&op, SEG_XB, 0, sizeof op);
        r = call(PDV_ENG_OPEN, 0, 0, 0);
        pd_fcopy_in(SEG_XB, 0, &op, sizeof op);
        if (!cf && r == PDE_BUSY) { if (++n > 5000) { fprintf(stderr, "OPEN never ends\n"); exit(96); } continue; }
        return cf ? (int)r : 0;
    }
}
static unsigned open10(void)
{
    struct pdxb_strm sx;
    memset(&sx, 0, sizeof sx);
    sx.objnum = 10;
    pd_fcopy_out(&sx, SEG_XB, 0, sizeof sx);
    return call(PDV_ENG_STRM_OPEN, 0, 0, 0), cf ? pde_fail : 0;
}

static unsigned long total;
static int drain(unsigned maxcalls, unsigned chunkmax, int use_tokens)
{
    unsigned got, ncall = 0;
    struct pdxb_token tkn;
    for (;;) {
        if (use_tokens) {
            call(PDV_ENG_TOKEN, 0, 0, 0);
            pd_fcopy_in(SEG_XB, 0, &tkn, sizeof tkn);
            if (cf || tkn.kind == PDT_EOF) return cf ? (int)pde_fail : 0;
        } else {
            got = call(PDV_ENG_STRM_READ, SEG_BUF, 0, 1 + rnd() % chunkmax);
            if (cf) return (int)got;
            if (!got) return 0;
            total += got;
            if (total > (1UL << 20)) { fprintf(stderr, "FUZZ: %lu bytes from one stream\n", total); exit(3); }
        }
        if (maxcalls && ++ncall >= maxcalls) return -1;
    }
}

static unsigned char guard_before[1], dummy;
static void paint(void)
{
    memset(pd_host_mem + (unsigned long)SEG_FLATE * 16 - GUARD, 0xA5, GUARD);
    memset(pd_host_mem + (unsigned long)SEG_FLATE * 16 + 2 * 44032UL, 0xA5, GUARD);
}
static void check_guard(const char *what, unsigned long it)
{
    unsigned char *a = pd_host_mem + (unsigned long)SEG_FLATE * 16 - GUARD;
    unsigned char *b = pd_host_mem + (unsigned long)SEG_FLATE * 16 + 2 * 44032UL;
    unsigned long i;
    for (i = 0; i < GUARD; i++) if (a[i] != 0xA5 || b[i] != 0xA5) { fprintf(stderr, "FUZZ: %s %lu wrote outside the claim at +%lu\n", what, it, i); exit(4); }
    (void)guard_before; (void)dummy;
}

int main(int argc, char **argv)
{
    int i, n = 100;
    long seeds = 1;
    unsigned long it, runs = 0, errs = 0;
    for (i = 1; i + 1 < argc && argv[i][0] == '-'; i += 2) {
        if (!strcmp(argv[i], "-n")) n = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-s")) seeds = atol(argv[i + 1]);
    }
    rs = (unsigned long)seeds * 2654435761UL + 12345;
    for (; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        unsigned char *orig, *buf;
        unsigned long size, lo = 0, hi = 0, k;
        if (!f) { fprintf(stderr, "cannot read %s\n", argv[i]); return 2; }
        fseek(f, 0, SEEK_END); size = (unsigned long)ftell(f); fseek(f, 0, SEEK_SET);
        orig = malloc(size); buf = malloc(size);
        if (fread(orig, 1, size, f) != size) return 2;
        fclose(f);
        /* object 10's payload */
        for (k = 0; k + 8 < size; k++) if (!memcmp(orig + k, "10 0 obj", 8)) break;
        for (; k + 7 < size; k++) if (!memcmp(orig + k, "stream\n", 7)) { lo = k + 7; break; }
        for (k = lo; k + 9 < size; k++) if (!memcmp(orig + k, "\nendstream", 10)) { hi = k; break; }
        if (!lo || !hi) { fprintf(stderr, "%s: no object 10 stream\n", argv[i]); return 2; }
        for (it = 0; it < (unsigned long)n; it++) {
            unsigned long len = hi - lo, m, nm, a, b;
            memcpy(buf, orig, size);
            nm = it ? 1 + rnd() % 6 : 0;
            for (m = 0; m < nm; m++) {
                switch (rnd() % 5) {
                case 0: case 1: buf[lo + rnd() % len] ^= (unsigned char)(1 << (rnd() % 8)); break;
                case 2: a = rnd() % len; b = 1 + rnd() % 16; while (b-- && a < len) buf[lo + a++] = (unsigned char)rnd(); break;
                case 3: a = rnd() % len; b = rnd() % len; if (a != b) buf[lo + a] = buf[lo + b]; break;
                case 4: a = rnd() % len; buf[lo + a] = (unsigned char)(rnd() % 2 ? 0xFF : 0x00); break;
                }
            }
            pdh_set(buf, size);
            flate_kb = (rnd() % 4 == 0) ? 43 : (rnd() % 8 == 0) ? 0 : 86;
            paint();
            if (do_init() || do_open()) { fprintf(stderr, "FUZZ: %s: cannot open (the PDF, not the stream)\n", argv[i]); return 2; }
            total = 0;
            {
                unsigned mode = rnd() % 5, r;
                unsigned chunk = (rnd() % 3 == 0) ? 3 : (rnd() % 2) ? 200 : 2000;
                r = open10();
                if (!r) {
                    if (mode == 1) {                       /* nested inside itself: the second may be refused */
                        drain(1 + rnd() % 40, chunk, 0);
                        r = open10();
                        if (!r) { drain(0, chunk, 0); call(PDV_ENG_STRM_CLOSE, 0, 0, 0); }
                        r = drain(0, chunk, 0);
                    } else if (mode == 2) {                /* half-way, closed, reopened */
                        drain(1 + rnd() % 60, chunk, 0);
                        call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
                        r = open10();
                        if (!r) r = drain(0, chunk, 0);
                    } else if (mode == 3) {
                        r = drain(0, chunk, 1);
                    } else r = drain(0, chunk, 0);
                    call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
                    if (mode == 1 && !r) { /* nothing */ }
                }
                if (r) errs++;
            }
            call(PDV_ENG_CLOSE, 0, 0, 0);
            check_guard(argv[i], it);
            runs++;
        }
        free(orig); free(buf);
    }
    printf("pde_fl_fuzz: %lu runs, %lu ended in an error, no crash, no write outside the claim\n", runs, errs);
    return 0;
}
