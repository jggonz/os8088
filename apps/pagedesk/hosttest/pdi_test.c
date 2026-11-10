/* os8088 - hosttest/pdi_test.c - PDXI's host harness (PAGEDESK-SPEC 20.22.11).
 *
 *   pdi_test <file.jpg> <want_w> <want_h> <flags> <out.raw> [inline]
 *
 * Drives the REAL pdxi.c (with the C twins of the assembly) through the module's
 * own verbs: X_INIT, IMG_OPEN, IMG_ROW, IMG_CLOSE, with a mock PDENG that serves
 * the file as the CODED bytes of object 1. Prints "w h mode ncomp rowbytes scale"
 * and writes the rows to out.raw. The comparison with libjpeg is
 * tools/os88pdfx_dct.py's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PD_HOST 1
#include "../pdxi.c"

extern unsigned char pd_host_mem[];
static unsigned char *g_file; static long g_flen, g_fpos;
static unsigned g_chunk = 0;               /* force short reads: 0 = as asked */
#define XB 0x2000
#define SLOT 0x3000
#define DEST 0x1800

pdw pdi_engcall(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdi_engcf = 0;
    if (verb == PDV_ENG_STRM_OPEN) {
        struct pdxb_strm st;
        pd_fcopy_in(g_xb, 0, &st, sizeof st);
        if (!(st.accept & PDSA_CODEC)) { pdi_engcf = 1; return PDE_FILTER; }
        st.codec = PDCD_DCT; st.filters = 0;
        g_fpos = 0;
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

int main(int argc, char **argv)
{
    FILE *f; struct pdxb_init in; struct pdxb_img rq, out; unsigned r, y;
    if (argc < 6) { fprintf(stderr, "usage\n"); return 2; }
    f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END); g_flen = ftell(f); fseek(f, 0, SEEK_SET);
    g_file = malloc(g_flen + 1); fread(g_file, 1, g_flen, f); fclose(f);
    if (getenv("PDI_CHUNK")) g_chunk = atoi(getenv("PDI_CHUNK"));
    memset(&in, 0, sizeof in);
    in.xb_seg = XB; in.slot_seg = SLOT; in.slot_kb = getenv("PDI_KB") ? atoi(getenv("PDI_KB")) : 64;
    pd_fcopy_out(&in, XB, 0, sizeof in);
    r = pdi_verb(PDV_X_INIT, XB, 0, 0); if (r) { printf("INIT %u\n", r); return 1; }
    memset(&rq, 0, sizeof rq);
    rq.codec = PDCD_DCT; rq.objnum = 1;
    rq.want_w = atoi(argv[2]); rq.want_h = atoi(argv[3]); rq.flags = strtol(argv[4], 0, 0);
    if (argc > 6 && !strcmp(argv[6], "inline")) {          /* the coded bytes ARE in memory */
        memcpy(pd_host_mem + 0x5000 * 16, g_file, g_flen > 60000 ? 60000 : g_flen);
        rq.objnum = 0; rq.gen = 0; rq.iseg = 0x5000; rq.ioff = 0; rq.ilen = g_flen > 60000 ? 60000 : g_flen;
    }
    pd_fcopy_out(&rq, XB, 0, sizeof rq);
    r = pdi_verb(PDV_IMG_OPEN, XB, 0, 0);
    if (r == PDE_NEEDMEM) {            /* plat regrows the slot (PDI_REGROW=1) or the same call is made again */
        printf("NEEDMEM %u KiB\n", (unsigned)pdi_rdx);
        if (getenv("PDI_REGROW")) { in.slot_kb = pdi_rdx; pd_fcopy_out(&in, XB, 0, sizeof in); pdi_verb(PDV_X_INIT, XB, 0, 0); }
        pd_fcopy_out(&rq, XB, 0, sizeof rq);
        r = pdi_verb(PDV_IMG_OPEN, XB, 0, 0);
    }
    if (r) { printf("OPEN %u\n", r); return 1; }
    pd_fcopy_in(XB, 0, &out, sizeof out);
    printf("%u %u %u %u %u %u\n", out.out_w, out.out_h, out.out_mode, out.out_ncomp, out.rowbytes, out.scale);
    f = fopen(argv[5], "wb");
    for (y = 0; y < out.out_h; y++) {
        r = pdi_verb(PDV_IMG_ROW, DEST, 0, y);
        if (r) { printf("ROW %u -> %u\n", y, r); return 1; }
        fwrite(pd_host_mem + (unsigned long)DEST * 16, 1, out.rowbytes, f);
    }
    fclose(f);
    pdi_verb(PDV_IMG_CLOSE, 0, 0, 0);
    return 0;
}
