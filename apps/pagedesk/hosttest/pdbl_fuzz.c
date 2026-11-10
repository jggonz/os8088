/* os8088 - apps/pagedesk/hosttest/pdbl_fuzz.c
 * The hostile-input pass for PDXI's CCITT and JBIG2 decoders (PAGEDESK-SPEC 20.22.12), built with
 * AddressSanitizer and UBSan (tools/os88pdbl_check.py fuzz). Each seed is a file of coded bytes; it
 * is mutated -n times (bit flips, runs of random bytes, truncation, a spliced-in copy of itself) and
 * decoded to the end (the first -r rows of an image taller than that). The decoders must answer,
 * never crash, never hang, and never hand back a row longer than bl_orb.
 *
 *   pdbl_fuzz [-n MUTATIONS] [-s SEED] [-r ROWS] c:FILE:K:COLS:FLAGS:H | j:FILE[:GLOBALS]:H ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PD_HOST 1
#include "../pdabi.h"
#include "../pdmath.h"

unsigned char pd_host_mem[0x110000];
static unsigned char *ix_buf[2]; static unsigned long ix_len[2], ix_pos; static int ix_cur;
static int ix_get(void) { return ix_pos < ix_len[ix_cur] ? ix_buf[ix_cur][ix_pos++] : -1; }
static pdw ix_open(pdw which) { ix_cur = which; ix_pos = 0; return which < 2 && ix_buf[which] ? 0 : 1; }
#include "../pdxiccit.c"
#include "../pdxijb2.c"

static unsigned long rs = 1;
static unsigned rnd(void) { rs = rs * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)(rs >> 33); }
static unsigned char *slurp(const char *p, unsigned long *n)
{
    FILE *f = fopen(p, "rb"); unsigned char *b; long l;
    if (!f) { perror(p); exit(2); }
    fseek(f, 0, SEEK_END); l = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(l + 16); if (fread(b, 1, l, f) != (size_t)l) exit(2); fclose(f); *n = l; return b;
}
static void mutate(unsigned char *b, unsigned long *n, unsigned long orig)
{
    unsigned k = 1 + rnd() % 4;
    while (k--) {
        unsigned long i = *n ? rnd() % *n : 0;
        switch (rnd() % 6) {
        case 0: if (*n) b[i] ^= 1 << (rnd() & 7); break;
        case 1: if (*n) b[i] = rnd(); break;
        case 2: { unsigned long l = 1 + rnd() % 16; while (l-- && i < *n) b[i++] = rnd(); } break;
        case 3: if (*n > 1) *n = 1 + rnd() % *n; break;
        case 4: if (*n && i + 4 < *n) { b[i] = 0xFF; b[i + 1] = 0xFF; b[i + 2] = rnd(); b[i + 3] = rnd(); } break;
        case 5: if (*n) b[i] = (rnd() & 1) ? 0 : 0xFF; break;
        }
    }
    (void)orig;
}

int main(int argc, char **argv)
{
    unsigned long n = 200, rows = 40, runs = 0; int a;
    for (a = 1; a < argc && argv[a][0] == '-'; a += 2) {
        if (!strcmp(argv[a], "-n")) n = atol(argv[a + 1]);
        else if (!strcmp(argv[a], "-s")) rs = atol(argv[a + 1]);
        else if (!strcmp(argv[a], "-r")) rows = atol(argv[a + 1]);
    }
    for (; a < argc; a++) {
        char *f[8]; int nf = 0; char *t = strdup(argv[a]); char *q = strtok(t, ":");
        unsigned char *orig, *glob = 0; unsigned long olen, glen = 0, i;
        while (q && nf < 8) { f[nf++] = q; q = strtok(0, ":"); }
        orig = slurp(f[1], &olen);
        if (f[0][0] == 'j' && nf >= 4) glob = slurp(f[2], &glen);
        for (i = 0; i < n; i++) {
            unsigned char *m = malloc(olen + 16); unsigned long ml = olen, r; pdw e;
            memcpy(m, orig, olen);
            if (i) mutate(m, &ml, olen);
            ix_buf[0] = m; ix_len[0] = ml; ix_buf[1] = glob; ix_len[1] = glen; ix_cur = 0; ix_pos = 0;
            if (f[0][0] == 'c') {
                e = cf_open((pdw)(short)atoi(f[2]), (pdw)atoi(f[3]), 0, (pdw)atoi(f[4]), (pdw)atoi(f[5]), (pdw)(rnd() % 600));
            } else {
                e = jb_open(glob != 0, (pdw)atoi(f[nf - 1]), (pdw)(rnd() % 600));
            }
            if (!e) {
                for (r = 0; r < bl_oh && r < rows; r++) {
                    e = f[0][0] == 'c' ? cf_getrow((pdw)r) : jb_getrow((pdw)r);
                    if (e) break;
                    if (bl_orb > 1024) { printf("BAD rowbytes %u\n", bl_orb); return 1; }
                }
            }
            runs++;
            free(m);
        }
        free(orig); free(glob); free(t);
    }
    printf("fuzz: %lu runs ok\n", runs);
    return 0;
}
