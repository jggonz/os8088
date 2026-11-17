/* os8088 - apps/pagedesk/hosttest/pdxj_fuzz.c - PDXJ's parser fuzz target.
 *   pdxj_fuzz [-n mutations-per-file] [-s seed] file.j2k ...
 * Built with ASan + UBSan (the Makefile's pdxj-fuzz, or by hand). Every input is a
 * mutated copy of a fixture - flipped, overwritten and inserted bytes (the main
 * header twice as often), truncation, a random workspace size - opened and read
 * to the last row. A sanitizer report, a hang (alarm), or a write outside the
 * module's workspace (guard bytes either side of it in the host arena) is a
 * failure; a refusal or an error return is not. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#define PD_HOST 1
#include "../pdxj.c"
#include "../pdxjtw.c"

#define SEG_XB  0x2000
#define SEG_WS  0x3000
#define SEG_SRC 0x5000
#define SEG_ROW 0x4F00

static unsigned long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 11); }
static void onalarm(int s) { (void)s; fprintf(stderr, "HANG\n"); abort(); }

static int one(const unsigned char *d, long n, unsigned kb, unsigned want, unsigned flags)
{
    struct pdxb_img im; struct pdxb_init in; pdw r, y; unsigned char *g;
    long i; int rows = 0;
    memset(pd_host_mem + ((unsigned long)(SEG_WS - 0x100) << 4), 0xA5, 0x1000);
    memset(pd_host_mem + ((unsigned long)SEG_WS << 4) + 0xF000, 0xA5, 0x1000);
    memcpy(pd_host_mem + ((unsigned long)SEG_SRC << 4), d, n);
    memset(&in, 0, sizeof in);
    in.slot_seg = SEG_WS; in.slot_kb = kb;
    pd_fcopy_out(&in, SEG_XB, 0, sizeof in);
    pdxj_verb(PDV_X_INIT, SEG_XB, 0, 0);
    memset(&im, 0, sizeof im);
    im.iseg = SEG_SRC; im.ilen = (pdw)n; im.codec = PDCD_JPX; im.want_w = want; im.want_h = want; im.flags = flags;
    pd_fcopy_out(&im, SEG_XB, 0, sizeof im);
    r = pdxj_verb(PDV_IMG_OPEN, SEG_XB, 0, 0);
    pd_fcopy_in(SEG_XB, 0, &im, sizeof im);
    if (!r) {
        if (im.rowbytes > 1024) { fprintf(stderr, "rowbytes %u\n", im.rowbytes); abort(); }
        for (y = 0; y < im.out_h && y < 4000; y++) {
            r = pdxj_verb(PDV_IMG_ROW, SEG_ROW, 0, y);
            if (r) break;
            rows++;
        }
    }
    pdxj_verb(PDV_IMG_CLOSE, 0, 0, 0);
    g = pd_host_mem + ((unsigned long)(SEG_WS - 0x100) << 4);
    for (i = 0; i < 0x1000; i++) if (g[i] != 0xA5) { fprintf(stderr, "write below the workspace at -%ld\n", 0x1000 - i); abort(); }
    g = pd_host_mem + ((unsigned long)SEG_WS << 4) + 0xF000;
    for (i = 0; i < 0x1000; i++) if (g[i] != 0xA5) { fprintf(stderr, "write above the workspace at +%ld\n", i); abort(); }
    return rows;
}

int main(int argc, char **argv)
{
    int a = 1, nmut = 200, f; long total = 0, ok = 0;
    rs = 88088;
    while (a < argc && argv[a][0] == '-') {
        if (!strcmp(argv[a], "-n")) nmut = atoi(argv[++a]);
        else if (!strcmp(argv[a], "-s")) rs = strtoul(argv[++a], 0, 0) * 2654435761ul + 1;
        a++;
    }
    signal(SIGALRM, onalarm);
    for (f = a; f < argc; f++) {
        FILE *fp = fopen(argv[f], "rb"); unsigned char *buf = malloc(70000), *m = malloc(70000); long n, k, mn;
        int it, j;
        if (!fp) { perror(argv[f]); return 2; }
        n = fread(buf, 1, 65000, fp); fclose(fp);
        for (it = 0; it < nmut; it++) {
            memcpy(m, buf, n); mn = n;
            switch (rnd() % 6) {
            case 0: for (j = rnd() % 8 + 1; j; j--) m[rnd() % (n < 400 ? n : 400)] ^= 1u << (rnd() % 8); break;
            case 1: for (j = rnd() % 8 + 1; j; j--) m[rnd() % n] = (unsigned char)rnd(); break;
            case 2: for (j = rnd() % 4 + 1; j; j--) m[rnd() % (n < 400 ? n : 400)] = (unsigned char)rnd(); break;
            case 3: mn = rnd() % n; break;                                  /* truncate */
            case 4: k = rnd() % n; if (mn + 16 < 65000) { memmove(m + k + 16, m + k, mn - k); memset(m + k, rnd() & 0xFF, 16); mn += 16; } break;
            default: k = rnd() % (n < 400 ? n : 400); m[k] = 0xFF; if (k + 1 < n) m[k + 1] = (unsigned char)(0x4F + rnd() % 0x50); break;
            }
            alarm(20);
            ok += one(m, mn, (rnd() & 3) ? 60 : 8 + rnd() % 52, (rnd() & 1) ? 0 : 1 + rnd() % 200, (rnd() % 5) == 0 ? PDJ_ALPHA : 0) >= 0;
            alarm(0);
            total++;
        }
        free(buf); free(m);
    }
    printf("pdxj_fuzz: %ld inputs, no sanitizer report, no hang, no write outside the workspace\n", total);
    return 0;
}
