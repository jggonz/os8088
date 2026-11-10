/* os8088 - apps/pagedesk/hosttest/pdxj_test.c - PDXJ's host harness.
 *   pdxj_test file.j2k out.raw want_w want_h [flags] [ncomp]
 * Opens the file through the inline path (the codestream sits in the host
 * arena), prints "scale out_w out_h mode ncomp rowbytes" or "REFUSED reason",
 * and writes every row to out.raw. hosttest/pdxj_cmp.py drives it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PD_HOST 1
#include "../pdxj.c"
#include "../pdxjtw.c"

#define SEG_XB  0x2000
#define SEG_WS  0x3000
#define SEG_SRC 0x5000
#define SEG_ROW 0x4F00

int main(int argc, char **argv)
{
    FILE *f; long n; struct pdxb_img im; pdw r, y; unsigned char *buf;
    struct pdxb_init in;
    unsigned ww, wh, fl = 0, nc = 0;
    if (argc < 5) { fprintf(stderr, "usage\n"); return 2; }
    f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
    buf = malloc(1 << 20); n = fread(buf, 1, 1 << 20, f); fclose(f);
    if (n > 0xFFFF) { fprintf(stderr, "fixture over 64K\n"); return 2; }
    memcpy(pd_host_mem + ((unsigned long)SEG_SRC << 4), buf, n);
    ww = atoi(argv[3]); wh = atoi(argv[4]);
    if (argc > 5) fl = strtoul(argv[5], 0, 0);
    if (argc > 6) nc = atoi(argv[6]);
    memset(&in, 0, sizeof in);
    in.slot_seg = SEG_WS; in.slot_kb = getenv("PDXJ_KB") ? atoi(getenv("PDXJ_KB")) : 60;
    pd_fcopy_out(&in, SEG_XB, 0, sizeof in);
    pdxj_verb(PDV_X_INIT, SEG_XB, 0, 0);
    memset(&im, 0, sizeof im);
    im.iseg = SEG_SRC; im.ioff = 0; im.ilen = (pdw)n; im.codec = PDCD_JPX;
    im.want_w = ww; im.want_h = wh; im.flags = fl; im.ncomp = nc;
    pd_fcopy_out(&im, SEG_XB, 0, sizeof im);
    r = pdxj_verb(PDV_IMG_OPEN, SEG_XB, 0, 0);
    pd_fcopy_in(SEG_XB, 0, &im, sizeof im);
    if (r) { printf("REFUSED %u (rc %u)\n", im.out_w, r); return 1; }
    printf("%u %u %u %u %u %u\n", im.scale, im.out_w, im.out_h, im.out_mode, im.out_ncomp, im.rowbytes);
    if (argc > 2 && strcmp(argv[2], "-")) {
        f = fopen(argv[2], "wb");
        for (y = 0; y < im.out_h; y++) {
            r = pdxj_verb(PDV_IMG_ROW, SEG_ROW, 0, y);
            if (r) { printf("ROW %u failed rc %u why %u\n", y, r, g_why); return 1; }
            fwrite(pd_host_mem + ((unsigned long)SEG_ROW << 4), 1, im.rowbytes, f);
        }
        fclose(f);
    }
    pdxj_verb(PDV_IMG_CLOSE, 0, 0, 0);
    return 0;
}
