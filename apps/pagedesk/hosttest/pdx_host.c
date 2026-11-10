/* PDXF host harness (PAGEDESK-SPEC 20.22.8): renders glyphs of a font program
 * through the same C the module is built from.
 *   pdx_host <fontfile> <px> <asc> <rows> <gid>...      ASCII art + metrics
 *   pdx_host <fontfile> <px> <asc> <rows> -u <cp>...     by Unicode via the cmap
 *   pdx_host -bin ...  : raw records to stdout: gid w lb adv nbytes bytes...
 */
#define PD_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pdxf.c"

unsigned char pd_host_mem[1 << 20];
static long long mul64(long a, long b) { return (long long)a * b; }
pdi pdx_muldiv(pdi a, pdi b, pdi c)
{
    long long p = mul64(a, b), q, d = c;
    int neg = (p < 0) != (d < 0);
    if (p < 0) p = -p;
    if (d < 0) d = -d;
    q = (p + d / 2) / d;
    if (q > 32767) q = 32767;
    return (pdi)(neg ? -q : q);
}

int main(int argc, char **argv)
{
    FILE *f; long n; int px, asc, rows, i, bin = 0, byuni = 0, ai = 1;
    static unsigned char buf[65536];
    if (argc > 1 && !strcmp(argv[1], "-bin")) { bin = 1; ai++; }
    if (argc < ai + 5) { fprintf(stderr, "usage\n"); return 2; }
    f = fopen(argv[ai], "rb"); if (!f) { perror(argv[ai]); return 2; }
    n = (long)fread(buf, 1, sizeof buf, f); fclose(f);
    memcpy(pd_host_mem + (0x1000 << 4), buf, n);
    g_pseg = 0x1000; g_plen = (pdw)n;
    px = atoi(argv[ai + 1]); asc = atoi(argv[ai + 2]); rows = atoi(argv[ai + 3]);
    if (!fnt_open()) { fprintf(stderr, "cannot open font\n"); return 1; }
    for (i = ai + 4; i < argc; i++) {
        unsigned gid; pdw got, wb, r, c;
        if (!strcmp(argv[i], "-u")) { byuni = 1; continue; }
        gid = (unsigned)strtoul(argv[i], 0, 0);
        if (byuni) gid = cm_get(g_cm_uni, gid);
        got = fnt_glyph(gid, (pdw)(px * 64), (pdw)asc, (pdw)rows, 0x2000, 0, 4096);
        if (bin) {
            printf("%u %u %d %u %u\n", gid, (unsigned)g_rwpx, (int)g_rlb, (unsigned)g_gadv, got == 0xFFFF ? 0xFFFFu : (unsigned)got);
            if (got && got != 0xFFFF) { fwrite(pd_host_mem + (0x2000 << 4), 1, got, stdout); printf("\n"); }
            continue;
        }
        printf("gid %u w=%u lb=%d adv64=%u bytes=%u\n", gid, (unsigned)g_rwpx, (int)g_rlb, (unsigned)g_gadv, (unsigned)got);
        if (got == 0xFFFF || !got) continue;
        wb = (pdw)((g_rwpx + 7) >> 3);
        for (r = 0; r < (pdw)rows; r++) {
            for (c = 0; c < g_rwpx; c++) putchar((pd_host_mem[(0x2000 << 4) + r * wb + (c >> 3)] >> (7 - (c & 7))) & 1 ? '#' : (r == (pdw)asc ? '_' : '.'));
            putchar('\n');
        }
    }
    return 0;
}
