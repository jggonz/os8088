/* os8088 - apps/pagedesk/hosttest/pde_fuzz.c
 * PDENG's hostile-input pass (PAGEDESK-SPEC 20.2.1, PD-28): mutate real PDFs
 * and drive every verb over each one, on ONE engine that is re-INITed between
 * cases, the way the primary will. Built with -fsanitize=address,undefined by
 * `make pdefuzz`, so an out-of-bounds near access is a crash; the far claims
 * live inside pdhost.c's one big array, which ASan cannot police, so every
 * claim has a CANARY after its end and every destination buffer one before
 * and after, and a case that moves a canary fails.
 *
 *   pde_fuzz [-n MUTATIONS] [-s SEED] [-o DIR] FILE...
 *
 * Per file: a truncation at every 997th byte, then MUTATIONS random cases
 * (bit flips, delimiter bytes, /Length +/-1, 0, huge, negative, real and
 * indirect, xref entry offsets and startxref shifted, region splices, a
 * nesting bomb). Every 64th case, and after the last, the PRISTINE file is
 * opened again and must give the same page count as it did first: the engine
 * is reusable after whatever came before. A case that runs longer than
 * PDF_WATCHDOG seconds, or makes more READ_SEQ calls than pdh_budget, is a
 * hang. One mutated case in eight also has one READ_SEQ fail (an I/O error
 * at a random point of a random verb). The case being run is always written to DIR/pde_fuzz_case.pdf first,
 * so a crash leaves its input behind. Exit 0 = every case clean.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "../pdmath.h"

unsigned pde_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx);
extern unsigned pde_fail;
extern void pdh_set(unsigned char *buf, unsigned long n);
extern int pdh_cluster;
extern unsigned long pdh_reads, pdh_failat, pdh_budget;
extern unsigned char pd_host_mem[];

#define SEG_XB    0x2000
#define SEG_CACHE 0x3000
#define SEG_ARENA 0x4000
#define SEG_TBL   0x5000
#define SEG_BUF   0x6000
#define CACHE_KB  16
#define CANARY    0xA5
#define CAN_N     64
#define PDF_WATCHDOG 20

static int cf;
static unsigned long ncase, nopen_ok, nfail_codes[128], ntok, nreads_tot;
static const char *outdir = ".";
static char casepath[1024];
static const char *curfile;
static unsigned long curcase;

static unsigned call(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
{
    unsigned r = pde_verb(verb, bx, cx, dx);
    cf = pde_fail != 0;
    if (cf && r < 128) nfail_codes[r]++;
    return r;
}
static void xb_put(const void *p, unsigned n) { pd_fcopy_out(p, SEG_XB, 0, (pdw)n); }
static void xb_get(void *p, unsigned n) { pd_fcopy_in(SEG_XB, 0, p, (pdw)n); }

static unsigned long rng_state = 1;
static unsigned rnd(unsigned n)
{
    rng_state = rng_state * 6364136223846793005UL + 1442695040888963407UL;
    return n ? (unsigned)((rng_state >> 33) % n) : 0;
}

static void on_alarm(int s)
{
    (void)s;
    fprintf(stderr, "HANG: %s case %lu ran over %d s; input in %s\n", curfile, curcase, PDF_WATCHDOG, casepath);
    _exit(95);
}

/* ---- canaries ------------------------------------------------------------- */
static unsigned char *lin(unsigned seg, unsigned off) { return pd_host_mem + ((unsigned long)seg << 4) + off; }
static void can_set(unsigned seg, unsigned off) { memset(lin(seg, off), CANARY, CAN_N); }
static int can_ok(unsigned seg, unsigned off, const char *what)
{
    unsigned i;
    unsigned char *p = lin(seg, off);
    for (i = 0; i < CAN_N; i++)
        if (p[i] != CANARY) {
            fprintf(stderr, "CANARY %s moved (byte %u = %02x): %s case %lu; input in %s\n",
                    what, i, p[i], curfile, curcase, casepath);
            return 0;
        }
    return 1;
}
static void cans_set(void)
{
    can_set(SEG_XB, PD_XB_BYTES);
    can_set(SEG_CACHE, CACHE_KB * 1024);
    can_set(SEG_ARENA, 8192);
    can_set(SEG_TBL, 11264);
    can_set(SEG_BUF, 0);                        /* below a STRM_READ / RAW_COPY at 64 */
    can_set(SEG_BUF, 64 + 1024);                /* ...and above it                     */
    can_set(SEG_BUF, 0xFFF0 - CAN_N);           /* below a destination at the top      */
}
static int cans_ok(void)
{
    return can_ok(SEG_XB, PD_XB_BYTES, "after XB") & can_ok(SEG_CACHE, CACHE_KB * 1024, "after cache") &
           can_ok(SEG_ARENA, 8192, "after arena") & can_ok(SEG_TBL, 11264, "after tables") &
           can_ok(SEG_BUF, 0, "below dest") & can_ok(SEG_BUF, 64 + 1024, "above dest") &
           can_ok(SEG_BUF, 0xFFF0 - CAN_N, "below top dest");
}

/* ---- the verbs ------------------------------------------------------------ */
static int do_init(unsigned cluster)
{
    struct pdxb_init in;
    memset(&in, 0, sizeof in);
    in.xb_seg = SEG_XB; in.cache_seg = SEG_CACHE; in.cache_kb = CACHE_KB;
    in.arena_seg = SEG_ARENA; in.arena_kb = 8; in.tables_seg = SEG_TBL; in.tables_kb = 11;
    pdh_cluster = (int)cluster;
    xb_put(&in, sizeof in);
    call(PDV_ENG_INIT, SEG_XB, 0, 0);
    return cf ? (int)pde_fail : 0;
}

static struct pdxb_open op;
static int do_open(void)
{
    unsigned r, n = 0;
    memset(&op, 0, sizeof op);
    strcpy(op.name, "DOC.PDF");
    for (;;) {
        xb_put(&op, sizeof op);
        r = call(PDV_ENG_OPEN, 0, 0, 0);
        xb_get(&op, sizeof op);
        if (cf && r == PDE_BUSY) {
            /* 8 revisions + 1024 pages / 16 nodes a slice + slack */
            if (++n > 400) { fprintf(stderr, "OPEN never ends: %s case %lu\n", curfile, curcase); exit(96); }
            continue;
        }
        return cf ? (int)r : 0;
    }
}

static void stream_ops(unsigned page, unsigned idx, unsigned objnum, unsigned gen)
{
    struct pdxb_strm sx;
    struct pdxb_token tk;
    unsigned r, k;
    memset(&sx, 0, sizeof sx);
    sx.objnum = (pdw)objnum; sx.gen = (pdw)gen; sx.page = (pdw)page; sx.index = (pdw)idx;
    xb_put(&sx, sizeof sx);
    call(PDV_ENG_STRM_OPEN, 0, 0, 0);
    if (cf) return;
    for (k = 0; k < 20000; k++) {               /* a 1 MiB stream is ~ 4k tokens of 255 */
        r = call(PDV_ENG_TOKEN, 0, 0, 0);
        xb_get(&tk, sizeof tk);
        ntok++;
        if (tk.len > 255 || tk.text[tk.len > 255 ? 255 : tk.len] != 0) {
            fprintf(stderr, "TOKEN len %u not terminated: %s case %lu\n", tk.len, curfile, curcase); exit(94);
        }
        if (cf && r != PDE_RANGE && r != PDE_DEPTH) break;
        if (tk.kind == PDT_EOF) break;
        if (tk.kind == PDT_OP && tk.len == 2 && tk.text[0] == 'I' && tk.text[1] == 'D') {
            call(PDV_ENG_STRM_READ, SEG_BUF, 64, 200);      /* an inline image's bytes */
        }
        if ((k & 63) == 63) {                   /* another verb between two tokens */
            struct pdxb_dictget g;
            memset(&g, 0, sizeof g);
            strcpy(g.key[0], "Root");
            xb_put(&g, sizeof g);
            call(PDV_ENG_DICT_GET, 0, 0, 0);
        }
    }
    call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
    /* the same stream through STRM_READ: into the middle, then at the very top
     * of the segment (which must be refused or stop short, never wrap) */
    xb_put(&sx, sizeof sx);
    call(PDV_ENG_STRM_OPEN, 0, 0, 0);
    if (cf) return;
    for (k = 0; k < 1200; k++) {
        r = call(PDV_ENG_STRM_READ, SEG_BUF, 64, 1024);
        if (cf || r == 0) break;
        if (r > 1024) { fprintf(stderr, "STRM_READ gave %u > 1024: %s case %lu\n", r, curfile, curcase); exit(94); }
        if (k == 3) call(PDV_ENG_STRM_READ, SEG_BUF, 0xFFF0, 0x40);
    }
    call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
}

static void outline_walk(void)
{
    struct pdxb_outline ol;
    unsigned stk_n[40], stk_g[40], sp = 0, items = 0;
    memset(&ol, 0, sizeof ol);
    xb_put(&ol, sizeof ol);
    call(PDV_ENG_OUTLINE, 0, 0, 0);
    if (cf) return;
    for (;;) {
        xb_get(&ol, sizeof ol);
        if (memchr(ol.title, 0, sizeof ol.title) == NULL) { fprintf(stderr, "OUTLINE title unterminated: %s case %lu\n", curfile, curcase); exit(94); }
        if (++items > 60) return;               /* an outline cycle is the shell's to catch */
        if (ol.next_num && sp < 40) { stk_n[sp] = ol.next_num; stk_g[sp] = ol.next_gen; sp++; }
        if (ol.first_num && sp < 40) { stk_n[sp] = ol.first_num; stk_g[sp] = ol.first_gen; sp++; }
        if (!sp) return;
        sp--;
        memset(&ol, 0, sizeof ol);
        ol.objnum = (pdw)stk_n[sp]; ol.gen = (pdw)stk_g[sp];
        xb_put(&ol, sizeof ol);
        call(PDV_ENG_OUTLINE, 0, 0, 0);
        if (cf) return;
    }
}

static void objs_walk(unsigned size)
{
    struct pdxb_objref rf;
    struct pdxb_objinfo oi;
    struct pdxb_rawcopy rw;
    struct pdxb_dictget g;
    unsigned n, k, top;
    top = size < 48 ? size : 48;
    for (n = 1; n < top; n++) {
        rf.objnum = (pdw)n; rf.gen = 0;
        xb_put(&rf, sizeof rf);
        call(PDV_ENG_OBJ_FIND, 0, 0, 0);
        if (cf) continue;
        xb_get(&oi, sizeof oi);
        memset(&rw, 0, sizeof rw);
        rw.objnum = (pdw)n; rw.gen = 0; rw.dseg = SEG_BUF; rw.doff = 64; rw.dmax = 1024;
        for (k = 0; k < 400; k++) {
            xb_put(&rw, sizeof rw);
            call(PDV_ENG_RAW_COPY, 0, 0, 0);
            if (cf) break;
            xb_get(&rw, sizeof rw);
            if (rw.got > 1024) { fprintf(stderr, "RAW_COPY got %u: %s case %lu\n", rw.got, curfile, curcase); exit(94); }
            if (rw.got == 0) break;
        }
        /* a destination at the top of the segment: refuse, or fit, never wrap */
        memset(&rw, 0, sizeof rw);
        rw.objnum = (pdw)n; rw.gen = 0; rw.dseg = SEG_BUF; rw.doff = 0xFFF0; rw.dmax = 0x400;
        xb_put(&rw, sizeof rw);
        call(PDV_ENG_RAW_COPY, 0, 0, 0);
        if (n < 12) {
            memset(&g, 0, sizeof g);
            g.objnum = (pdw)n;
            switch (rnd(4)) {
            case 0: strcpy(g.key[0], "Type"); break;
            case 1: strcpy(g.key[0], "Kids"); strcpy(g.key[1], "[0]"); strcpy(g.key[2], "MediaBox"); break;
            case 2: strcpy(g.key[0], "Widths"); strcpy(g.key[1], "[409600017]"); break;
            default: strcpy(g.key[0], "Resources"); strcpy(g.key[1], "Font"); break;
            }
            xb_put(&g, sizeof g);
            call(PDV_ENG_DICT_GET, 0, 0, 0);
        }
    }
}

/* Open the buffer and drive every verb. Returns the OPEN code. */
static int run_case(unsigned char *buf, unsigned long n, unsigned cluster, unsigned fault)
{
    struct pdxb_pageinfo pi;
    struct pdxb_trailer tr;
    struct pdxb_reslookup rl;
    struct pdxb_annot an;
    unsigned p, i, np, r;
    int rc;
    FILE *f;
    ncase++;
    f = fopen(casepath, "wb");
    if (f) { fwrite(buf, 1, n, f); fclose(f); }
    pdh_set(buf, n);
    pdh_failat = fault ? pdh_reads + fault : 0;      /* the fault-th READ_SEQ of the case fails */
    pdh_budget = pdh_reads + 2000000UL;
    alarm(PDF_WATCHDOG);
    cans_set();
    do_init(cluster);
    rc = do_open();
    if (rc == 0) {
        nopen_ok++;
        call(PDV_ENG_TRAILER, 0, 0, 0); xb_get(&tr, sizeof tr);
        call(PDV_ENG_FINGERPRINT, 0, 0, 0);
        np = op.npages < 6 ? op.npages : 6;
        for (p = 0; p < np; p++) {
            call(PDV_ENG_PAGEINFO, p, 0, 0); xb_get(&pi, sizeof pi);
            for (i = 0; i < 3; i++) stream_ops(p, i, 0, 0);
            for (i = 0; i < 4; i++) {
                r = call(PDV_ENG_ANNOTS, p, i, 0);
                if (cf) break;
                xb_get(&an, sizeof an);
                if (!memchr(an.contents, 0, sizeof an.contents) || !memchr(an.nm, 0, sizeof an.nm) ||
                    !memchr(an.subtype, 0, sizeof an.subtype)) {
                    fprintf(stderr, "ANNOTS unterminated text: %s case %lu\n", curfile, curcase); exit(94);
                }
                if (an.objnum) stream_ops(0, 0, an.objnum, 0);     /* an /AP may be a stream: whatever it is, no crash */
            }
            memset(&rl, 0, sizeof rl);
            rl.page = (pdw)p; strcpy(rl.category, "Font"); strcpy(rl.name, "F1");
            xb_put(&rl, sizeof rl);
            r = call(PDV_ENG_RES_LOOKUP, 0, 0, 0);
            memset(&rl, 0, sizeof rl);
            rl.page = (pdw)p; strcpy(rl.category, "XObject"); strcpy(rl.name, "Im1");
            xb_put(&rl, sizeof rl);
            r = call(PDV_ENG_RES_LOOKUP, 0, 0, 0);
            if (!cf) {
                xb_get(&rl, sizeof rl);
                if (rl.objnum) {
                    stream_ops(0, 0, rl.objnum, rl.gen);
                    memset(&rl, 0, sizeof rl);
                    rl.page = 0xFFFF; rl.resobj = 0; strcpy(rl.category, "Font"); strcpy(rl.name, "F1");
                    xb_put(&rl, sizeof rl);
                    call(PDV_ENG_RES_LOOKUP, 0, 0, 0);
                }
            }
        }
        call(PDV_ENG_PAGEINFO, op.npages, 0, 0);            /* past the end */
        /* the stream stack: push until refused, then pop them all */
        for (i = 0; i < 9; i++) {
            struct pdxb_strm sx;
            memset(&sx, 0, sizeof sx);
            xb_put(&sx, sizeof sx);
            call(PDV_ENG_STRM_OPEN, 0, 0, 0);
            if (cf) break;
        }
        for (i = 0; i < 9; i++) call(PDV_ENG_STRM_CLOSE, 0, 0, 0);
        outline_walk();
        objs_walk(tr.size);
        call(PDV_ENG_ARENA_REL, 0x7FFF, 0, 0);               /* above the mark: refused */
    } else {
        /* every object verb must refuse cleanly with no document */
        call(PDV_ENG_PAGEINFO, 0, 0, 0);
        call(PDV_ENG_TOKEN, 0, 0, 0);
        call(PDV_ENG_STRM_READ, SEG_BUF, 64, 100);
    }
    call(PDV_ENG_CLOSE, 0, 0, 0);
    r = call(PDV_ENG_ARENA_MARK, 0, 0, 0);
    if (cf || r != 0) { fprintf(stderr, "ARENA_MARK %u after CLOSE: %s case %lu\n", r, curfile, curcase); exit(93); }
    alarm(0);
    if (!cans_ok()) exit(92);
    return rc;
}

/* ---- mutations ------------------------------------------------------------ */
static unsigned char *mbuf;
static unsigned long mcap;

static long find_from(const unsigned char *b, unsigned long n, unsigned long from, const char *s)
{
    unsigned long k, m = strlen(s);
    for (k = from; k + m <= n; k++) if (!memcmp(b + k, s, m)) return (long)k;
    return -1;
}
/* the k-th occurrence of s (wrapping), or -1 */
static long find_nth(const unsigned char *b, unsigned long n, const char *s, unsigned k)
{
    long at, first = find_from(b, n, 0, s);
    if (first < 0) return -1;
    at = first;
    while (k--) {
        at = find_from(b, n, (unsigned long)at + 1, s);
        if (at < 0) at = first;
    }
    return at;
}
/* replace [at, at+len) with text; returns the new length */
static unsigned long splice(unsigned long n, unsigned long at, unsigned long len, const char *text)
{
    unsigned long t = strlen(text);
    if (at > n) at = n;
    if (len > n - at) len = n - at;
    if (n - len + t > mcap) return n;
    memmove(mbuf + at + t, mbuf + at + len, n - at - len);
    memcpy(mbuf + at, text, t);
    return n - len + t;
}

static unsigned long mutate(const unsigned char *src, unsigned long n)
{
    unsigned kind = rnd(10), k, cnt;
    long at;
    unsigned long e, v;
    char num[32];
    static const char delims[] = "()<>[]{}/%\\\r\n 0123456789.-+#RTobjnf";
    memcpy(mbuf, src, n);
    switch (kind) {
    case 0: case 1:                             /* bit flips */
        cnt = 1 + rnd(8);
        for (k = 0; k < cnt; k++) mbuf[rnd((unsigned)n)] ^= (unsigned char)(1u << rnd(8));
        break;
    case 2:                                     /* syntax bytes */
        cnt = 1 + rnd(6);
        for (k = 0; k < cnt; k++) mbuf[rnd((unsigned)n)] = (unsigned char)delims[rnd(sizeof delims - 1)];
        break;
    case 3: case 4:                             /* a /Length */
        at = find_nth(mbuf, n, "/Length", rnd(8));
        if (at < 0) { mbuf[rnd((unsigned)n)] ^= 0x20; break; }
        e = (unsigned long)at + 7;
        while (e < n && (mbuf[e] == ' ' || mbuf[e] == '\n' || mbuf[e] == '\r')) e++;
        v = 0; k = 0;
        while (e + k < n && mbuf[e + k] >= '0' && mbuf[e + k] <= '9') { v = v * 10 + (mbuf[e + k] - '0'); k++; }
        switch (rnd(9)) {
        case 0: snprintf(num, sizeof num, "%lu", v + 1); break;
        case 1: snprintf(num, sizeof num, "%lu", v ? v - 1 : 0); break;
        case 2: strcpy(num, "0"); break;
        case 3: strcpy(num, "4294967295"); break;
        case 4: strcpy(num, "2147483647"); break;
        case 5: strcpy(num, "-1"); break;
        case 6: snprintf(num, sizeof num, "%lu.5", v); break;
        case 7: snprintf(num, sizeof num, "%u 0 R", 1 + rnd(12)); break;
        default: snprintf(num, sizeof num, "%lu", v + 1 + rnd(4000)); break;
        }
        n = splice(n, e, k, num);
        break;
    case 5: case 6:                             /* an xref entry's offset */
        at = find_nth(mbuf, n, " 00000 n", rnd(40));
        if (at < 10) { mbuf[rnd((unsigned)n)] ^= 0x01; break; }
        e = (unsigned long)at - 10;
        v = strtoul((const char *)mbuf + e, NULL, 10);
        switch (rnd(5)) {
        case 0: v = v + 1 + rnd(40); break;
        case 1: v = v > 40 ? v - 1 - rnd(40) : 0; break;
        case 2: v = n - 1 - rnd(30); break;
        case 3: v = 9999999999UL; break;
        default: v = rnd((unsigned)n); break;
        }
        snprintf(num, sizeof num, "%010lu", v);
        memcpy(mbuf + e, num, 10);
        if (rnd(4) == 0) mbuf[e + 17] = 'f';
        break;
    case 7:                                     /* startxref, or a subsection header */
        if (rnd(2)) {
            at = find_nth(mbuf, n, "startxref", 0);
            if (at < 0) break;
            e = (unsigned long)at + 9;
            while (e < n && (mbuf[e] == '\n' || mbuf[e] == '\r' || mbuf[e] == ' ')) e++;
            k = 0;
            while (e + k < n && mbuf[e + k] >= '0' && mbuf[e + k] <= '9') k++;
            v = strtoul((const char *)mbuf + e, NULL, 10);
            snprintf(num, sizeof num, "%lu", rnd(2) ? v + rnd(30) - 15 : (unsigned long)rnd((unsigned)n));
            n = splice(n, e, k, num);
        } else {
            at = find_nth(mbuf, n, "xref", rnd(3));
            if (at < 0) break;
            e = (unsigned long)at + 4;
            while (e < n && (mbuf[e] == '\n' || mbuf[e] == '\r')) e++;
            k = 0;
            while (e + k < n && mbuf[e + k] != '\n' && mbuf[e + k] != '\r' && k < 24) k++;
            switch (rnd(4)) {
            case 0: snprintf(num, sizeof num, "0 %u", 1 + rnd(70000)); break;
            case 1: snprintf(num, sizeof num, "%u 1", rnd(65536)); break;
            case 2: strcpy(num, "65535 2"); break;
            default: snprintf(num, sizeof num, "%u %u", rnd(30), rnd(30)); break;
            }
            n = splice(n, e, k, num);
        }
        break;
    case 8:                                     /* copy a region over another */
        {
            unsigned long a = rnd((unsigned)n), b = rnd((unsigned)n), l = 1 + rnd(300);
            if (a + l > n) l = n - a;
            if (b + l > n) l = n - b;
            memmove(mbuf + b, mbuf + a, l);
        }
        break;
    default:                                    /* nesting, or a long run */
        {
            static const char *bombs[] = { "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[",
                "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<",
                "((((((((((((((((((((((((((((((((((((((((", "<", "(", "%", "/Kids [", "1 0 R 1 0 R",
                "/Prev 9 ", "/Encrypt 1 0 R", "stream\r", "endstream", "/Filter [/AHx /RL /AHx /RL]",
                "/Filter [/RL /RL /RL /AHx]", "/Parent 1 0 R" };
            at = (long)rnd((unsigned)n);
            n = splice(n, (unsigned long)at, rnd(4), bombs[rnd(sizeof bombs / sizeof bombs[0])]);
        }
        break;
    }
    return n;
}

/* After a mutation that moved bytes, make the NEWEST xref table point at the
 * objects again - every "N G obj" found at a line start, the last one of each
 * number - and startxref at that table, so the case reaches the parser's
 * deeper layers instead of stopping at PDE_XREF. Half the moved cases get it. */
static unsigned long fixxref(unsigned long n)
{
    static unsigned long hdr[65536];
    unsigned long k, j, p, num, gen, first, count, xoff, sx;
    long at;
    char d[16];
    memset(hdr, 0, sizeof hdr);
    for (k = 0; k + 5 < n; k++) {
        if (k && mbuf[k - 1] != '\n' && mbuf[k - 1] != '\r') continue;
        p = k; num = 0; gen = 0;
        if (mbuf[p] < '0' || mbuf[p] > '9') continue;
        while (p < n && mbuf[p] >= '0' && mbuf[p] <= '9' && num < 70000) num = num * 10 + (mbuf[p++] - '0');
        if (p >= n || mbuf[p] != ' ') continue;
        p++;
        if (p >= n || mbuf[p] < '0' || mbuf[p] > '9') continue;
        while (p < n && mbuf[p] >= '0' && mbuf[p] <= '9' && gen < 70000) gen = gen * 10 + (mbuf[p++] - '0');
        if (p + 4 > n || memcmp(mbuf + p, " obj", 4) || num >= 65536) continue;
        hdr[num] = k + 1;                       /* +1: 0 = none */
    }
    at = -1;
    for (k = 0; k + 9 <= n; k++) if (!memcmp(mbuf + k, "startxref", 9)) at = (long)k;
    if (at < 0) return n;
    sx = (unsigned long)at;
    xoff = 0;
    for (k = 0; k + 4 <= sx; k++)
        if (!memcmp(mbuf + k, "xref", 4) && (k == 0 || mbuf[k - 1] == '\n' || mbuf[k - 1] == '\r')) xoff = k;
    if (!xoff) return n;
    p = xoff + 4;
    for (;;) {
        while (p < n && (mbuf[p] == ' ' || mbuf[p] == '\n' || mbuf[p] == '\r')) p++;
        if (p >= n || mbuf[p] < '0' || mbuf[p] > '9') break;
        first = strtoul((const char *)mbuf + p, NULL, 10);
        while (p < n && mbuf[p] >= '0' && mbuf[p] <= '9') p++;
        while (p < n && mbuf[p] == ' ') p++;
        count = strtoul((const char *)mbuf + p, NULL, 10);
        while (p < n && mbuf[p] >= '0' && mbuf[p] <= '9') p++;
        while (p < n && (mbuf[p] == ' ' || mbuf[p] == '\n' || mbuf[p] == '\r')) p++;
        for (j = 0; j < count && p + 20 <= n && j < 70000; j++, p += 20) {
            num = first + j;
            if (num < 65536 && mbuf[p + 17] == 'n' && hdr[num]) {
                snprintf(d, sizeof d, "%010lu", hdr[num] - 1);
                memcpy(mbuf + p, d, 10);
            }
        }
    }
    /* startxref's number */
    k = sx + 9;
    while (k < n && (mbuf[k] == ' ' || mbuf[k] == '\n' || mbuf[k] == '\r')) k++;
    j = 0;
    while (k + j < n && mbuf[k + j] >= '0' && mbuf[k + j] <= '9') j++;
    snprintf(d, sizeof d, "%lu", xoff);
    return splice(n, k, j, d);
}

static unsigned char *load(const char *path, unsigned long *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = (unsigned long)ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(*n ? *n : 1);
    if (*n && fread(b, 1, *n, f) != *n) { fclose(f); free(b); return NULL; }
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    unsigned long iters = 200, seed = 1, n, m, cut, before;
    unsigned long nfiles = 0, ntrunc = 0, nmut = 0, nreuse = 0;
    unsigned char *src, *pristine;
    int a, k, base_rc;
    static const unsigned clusters[4] = { 1, 2, 4, 8 };
    unsigned base_pages;
    signal(SIGALRM, on_alarm);
    for (a = 1; a < argc && argv[a][0] == '-'; a += 2) {
        if (a + 1 >= argc) break;
        if (!strcmp(argv[a], "-n")) iters = strtoul(argv[a + 1], NULL, 10);
        else if (!strcmp(argv[a], "-s")) seed = strtoul(argv[a + 1], NULL, 10);
        else if (!strcmp(argv[a], "-o")) outdir = argv[a + 1];
    }
    snprintf(casepath, sizeof casepath, "%s/pde_fuzz_case.pdf", outdir);
    rng_state = seed * 2654435761UL + 1;
    for (; a < argc; a++) {
        src = load(argv[a], &n);
        if (!src) { fprintf(stderr, "cannot read %s\n", argv[a]); return 2; }
        curfile = argv[a];
        nfiles++;
        mcap = n + 8192;
        mbuf = malloc(mcap);
        pristine = malloc(n ? n : 1);
        memcpy(pristine, src, n);
        /* the baseline */
        curcase = 0;
        memcpy(mbuf, src, n);
        base_rc = run_case(mbuf, n, 1, 0);
        base_pages = base_rc ? 0 : op.npages;
        /* truncations: every 997th byte */
        for (cut = 997; cut < n; cut += 997) {
            curcase++;
            memcpy(mbuf, src, cut);
            run_case(mbuf, cut, clusters[cut / 997 % 4], 0);
            ntrunc++;
        }
        for (m = 0; m < iters; m++) {
            curcase++;
            before = mutate(src, n);
            if (before != n || rnd(2)) before = fixxref(before);
            run_case(mbuf, before, clusters[rnd(4)], rnd(8) == 0 ? 1 + rnd(40) : 0);
            nmut++;
            if ((m & 63) == 63 || m + 1 == iters) {
                curcase++;
                memcpy(mbuf, pristine, n);
                k = run_case(mbuf, n, 1, 0);
                nreuse++;
                if (k != base_rc || (k == 0 && op.npages != base_pages)) {
                    fprintf(stderr, "REUSE: %s opened rc=%d pages=%u after case %lu, first time rc=%d pages=%u\n",
                            curfile, k, op.npages, curcase, base_rc, base_pages);
                    return 91;
                }
            }
        }
        if (memcmp(pristine, src, n)) { fprintf(stderr, "SOURCE CHANGED: %s\n", curfile); return 90; }
        free(src); free(pristine); free(mbuf);
    }
    nreads_tot = pdh_reads;
    printf("pde_fuzz: %lu files, %lu cases (%lu truncations, %lu mutations, %lu reuse checks), %lu opened, "
           "%lu tokens, %lu READ_SEQ calls - no crash, no hang, no canary moved\n",
           nfiles, ncase, ntrunc, nmut, nreuse, nopen_ok, ntok, nreads_tot);
    printf("pde_fuzz: failure codes seen:");
    for (k = 0; k < 128; k++) if (nfail_codes[k]) printf(" %d:%lu", k, nfail_codes[k]);
    printf("\n");
    return 0;
}
