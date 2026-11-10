/* os8088 - hosttest/pdi_fuzz.c - PDXI's JPEG fuzz target (PAGEDESK-SPEC 20.22.11).
 *
 *   pdi_fuzz <iterations> <seed> <file.jpg>...
 *
 * Built with -fsanitize=address,undefined. Each iteration mutates a fixture (bit
 * flips, byte stores, a truncation, a marker or a length forged, a dimension or
 * a table count rewritten), then drives the module's own verbs: X_INIT (a random
 * workspace size, 0 to 64 KiB), IMG_OPEN with a random want_w/want_h and flags,
 * every row, IMG_CLOSE, and for a progressive file the warm re-OPEN. The mock
 * PDENG hands the coded bytes over in short random chunks. Guards: the host's
 * far-memory arena around the workspace and the destination row are filled with
 * a pattern that must be intact afterwards. The marker parser and the entropy
 * decoder are the targets; nothing may crash, loop, or write outside its region. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PD_HOST 1
#include "../pdxi.c"

extern unsigned char pd_host_mem[];
static unsigned char *g_file; static long g_flen, g_fpos;
static unsigned g_chunk;
#define XB 0x2000
#define SLOT 0x3000
#define DEST 0x1800
#define GUARD 512
static unsigned long g_rng = 88;
static unsigned rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return (unsigned)(g_rng >> 11); }

pdw pdi_engcall(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdi_engcf = 0;
    if (verb == PDV_ENG_STRM_OPEN) {
        struct pdxb_strm st;
        pd_fcopy_in(g_xb, 0, &st, sizeof st);
        st.codec = PDCD_DCT; g_fpos = 0;
        pd_fcopy_out(&st, g_xb, 0, sizeof st);
        return 0;
    }
    if (verb == PDV_ENG_STRM_READ) {
        long n = g_flen - g_fpos;
        if (n > dx) n = dx;
        if (g_chunk && n > (long)g_chunk) n = g_chunk;
        if (n > 0) memcpy(pd_host_mem + (unsigned long)bx * 16 + cx, g_file + g_fpos, n);
        g_fpos += n > 0 ? n : 0;
        return (pdw)(n > 0 ? n : 0);
    }
    if (verb == PDV_ENG_STRM_CLOSE) return 0;
    pdi_engcf = 1; return PDE_RANGE;
}

static void guards(int set)
{
    unsigned char *w = pd_host_mem + (unsigned long)SLOT * 16;
    unsigned char *d = pd_host_mem + (unsigned long)DEST * 16;
    unsigned long off[4] = { (unsigned long)(w - pd_host_mem) - GUARD, (unsigned long)(w - pd_host_mem) + 65536, (unsigned long)(d - pd_host_mem) + 1024, (unsigned long)(d - pd_host_mem) - GUARD };
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < GUARD; j++) {
            if (set) pd_host_mem[off[i] + j] = 0x5A;
            else if (pd_host_mem[off[i] + j] != 0x5A) { fprintf(stderr, "GUARD %d hit at +%d\n", i, j); abort(); }
        }
}

static void run(unsigned char *buf, long len, int kb)
{
    struct pdxb_init in; struct pdxb_img rq, out; unsigned r, y, pass;
    g_file = buf; g_flen = len;
    g_chunk = (rnd() & 3) == 0 ? 1 + rnd() % 700 : 0;
    memset(&in, 0, sizeof in);
    in.xb_seg = XB; in.slot_seg = SLOT; in.slot_kb = kb;
    pd_fcopy_out(&in, XB, 0, sizeof in);
    pdi_verb(PDV_X_INIT, XB, 0, 0);
    for (pass = 0; pass < 2; pass++) {
        guards(1);
        memset(&rq, 0, sizeof rq);
        rq.codec = PDCD_DCT; rq.objnum = 1 + (rnd() & 1);
        if (pass) rq.objnum = 1;
        rq.want_w = (rnd() & 1) ? rnd() % 700 : 0; rq.want_h = (rnd() & 1) ? rnd() % 500 : 0;
        rq.flags = rnd() & 7;
        if (pass) { rq.want_w = rq.want_h = 0; rq.flags = 0; }
        pd_fcopy_out(&rq, XB, 0, sizeof rq);
        r = pdi_verb(PDV_IMG_OPEN, XB, 0, 0);
        if (!r) {
            pd_fcopy_in(XB, 0, &out, sizeof out);
            if (out.rowbytes > 1024 || out.rowbytes == 0 || out.out_w == 0 || out.out_h == 0) { fprintf(stderr, "bad geometry %u %u %u\n", out.out_w, out.out_h, out.rowbytes); abort(); }
            for (y = 0; y < out.out_h; y++) {
                if (pdi_verb(PDV_IMG_ROW, DEST, 0, y)) break;
                if ((rnd() & 255) == 0) y += rnd() % 40;
            }
        }
        pdi_verb(PDV_IMG_CLOSE, 0, 0, 0);
        guards(0);
        if (r && rnd() & 1) break;
    }
}

int main(int argc, char **argv)
{
    long iters = atol(argv[1]); int a, i; long n, ok = 0;
    g_rng = (unsigned long)atol(argv[2]) * 2654435761UL + 88;
    for (a = 3; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); unsigned char *orig, *buf; long len;
        if (!f) { perror(argv[a]); return 2; }
        fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
        orig = malloc(len + 16); buf = malloc(len + 16);
        if (fread(orig, 1, len, f) != (size_t)len) return 2; fclose(f);
        for (n = 0; n < iters; n++) {
            long l2 = len; int muts = n == 0 ? 0 : 1 + rnd() % 8;
            memcpy(buf, orig, len);
            for (i = 0; i < muts; i++) {
                unsigned k = rnd() % 8, p = rnd() % l2;
                if (k < 3) buf[p] ^= 1 << (rnd() & 7);
                else if (k == 3) buf[p] = (unsigned char)rnd();
                else if (k == 4) l2 = p ? p : 1;                       /* truncate */
                else if (k == 5 && p + 3 < l2) { buf[p] = 0xFF; buf[p + 1] = (unsigned char)(rnd() & 1 ? 0xC4 + rnd() % 30 : 0xD0 + rnd() % 10); }
                else if (k == 6 && p + 1 < l2) { buf[p] = 0; buf[p + 1] = (unsigned char)rnd(); }   /* a length */
                else if (p < 400) buf[p] = (unsigned char)(rnd() % 256);  /* the header region */
            }
            run(buf, l2, (rnd() & 7) == 0 ? rnd() % 65 : 64);
            ok++;
        }
        free(orig); free(buf);
    }
    printf("pdi_fuzz: %ld runs, no sanitizer report, guards intact\n", ok);
    return 0;
}
