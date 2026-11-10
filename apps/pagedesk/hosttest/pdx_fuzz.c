/* PDXF's parsers under ASan + UBSan on hostile bytes (PAGEDESK-SPEC 20.22.8, PAGEDESK-SPEC 20.22 L7).
 *
 *   pdx_fuzz [-n ITER] [-s SEED] file...           font programs (TrueType, CFF, Type 1: found by their bytes)
 *   pdx_fuzz -c [-n ITER] [-s SEED]                two CMap texts (ToUnicode, Encoding) parsed through cm_parse
 *   pdx_fuzz -z [-n ITER] [-s SEED] file...        the in-place inflater (pdxfz.c, 20.22.8, "Streams without a Flate slot"): each file zlib-compressed
 *                                                  at levels 0, 1 and 9, inflated whole (must equal the file) then mutated
 *
 * Each iteration mutates a few bytes (flips, repeats, truncations, 0xFF/0x00 runs) of a seed, puts it in the
 * workspace, opens it and rasterises every glyph at 12 px. The run must finish; a sanitizer report is the failure.
 *   cc -fsanitize=address,undefined -g -DPD_HOST -I apps/pagedesk pdx_fuzz.c pdmath.c
 */
#define PD_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "../pdxf.c"

unsigned char pd_host_mem[1 << 20];
pdw pdx_eng_ax;
static const unsigned char *fz_src; static unsigned fz_len, fz_pos;
pdw pdx_eng(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdx_eng_ax = 0;
    if (verb == PDV_ENG_STRM_OPEN) { fz_pos = 0; return 0; }
    if (verb == PDV_ENG_STRM_READ) {
        unsigned n = fz_len - fz_pos; if (n > dx) n = dx;
        memcpy(pd_host_mem + (((unsigned long)bx << 4) + cx), fz_src + fz_pos, n); fz_pos += n; pdx_eng_ax = (pdw)n; return 0;
    }
    return 0;
}
pdi pdx_muldiv(pdi a, pdi b, pdi c)
{
    long long p = (long long)a * b, d = c, q;
    int neg = (p < 0) != (d < 0);
    if (p < 0) p = -p;
    if (d < 0) d = -d;
    if (!d) return 0;
    q = (p + d / 2) / d;
    if (q > 32767) q = 32767;
    return (pdi)(neg ? -q : q);
}
static unsigned long rs = 1;
static unsigned rnd(void) { rs = rs * 6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(rs >> 33); }
static void mutate(unsigned char *b, unsigned *n)
{
    unsigned k = 1 + rnd() % 6, i, j, p, l;
    for (i = 0; i < k && *n; i++) {
        p = rnd() % *n;
        switch (rnd() % 6) {
        case 0: b[p] ^= (unsigned char)(1u << (rnd() & 7)); break;
        case 1: b[p] = (unsigned char)rnd(); break;
        case 2: b[p] = 0xFF; if (p + 1 < *n) b[p + 1] = 0xFF; break;
        case 3: l = 1 + rnd() % 16; for (j = 0; j < l && p + j < *n; j++) b[p + j] = (unsigned char)(rnd() & 1 ? 0 : 0xFF); break;
        case 4: if (rnd() % 8 == 0) *n = p ? p : 1; break;
        case 5: if (p + 4 < *n) memcpy(b + p, b + (rnd() % (*n - 4)), 4); break;
        }
    }
}
static const char cm1[] = "/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n/CMapName /X def\n1 begincodespacerange <0000> <FFFF> endcodespacerange\n"
    "3 beginbfchar <0003> <0020> <0004> <0041> <0005> <00660069> endbfchar\n2 beginbfrange <0010> <0020> <0061> <0030> <0032> [<0041> <0042> <0043>] endbfrange\nendcmap\n";
static const char cm2[] = "/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n/CIDSystemInfo << /Registry (Adobe) >> def\n/CMapName /Y def /usecmap\n"
    "1 begincodespacerange <00> <FF> endcodespacerange\n2 begincidrange <0100> <01FF> 1 <0200> <0210> 300 endcidrange\n2 begincidchar <0300> 9 <0301> 10 endcidchar\nendcmap\n";

int main(int argc, char **argv)
{
    int iters = 300, ai, cmaps = 0; long seed = 1;
    static unsigned char buf[65536], mut[65536];
    for (ai = 1; ai < argc && argv[ai][0] == '-'; ai++) {
        if (!strcmp(argv[ai], "-n")) iters = atoi(argv[++ai]);
        else if (!strcmp(argv[ai], "-s")) seed = atol(argv[++ai]);
        else if (!strcmp(argv[ai], "-c")) cmaps = 1;
        else if (!strcmp(argv[ai], "-z")) cmaps = 2;
    }
    rs = (unsigned long)seed * 2654435761UL + 12345;
    g_ws = 0x3000; g_pcap = 37376; g_stgseg = g_ws; g_stgoff = (pdw)(g_pcap + PDXF_CIDWIN); g_xb = 0x2000;
    if (cmaps == 2) {                                  /* the inflater: the zlib stream at the region's top, out from its bottom */
        static unsigned char zb[70000];
        for (; ai < argc; ai++) {
            FILE *f = fopen(argv[ai], "rb"); long it; unsigned n0, n, lv, ok = 0, more = 0; uLongf zl;
            if (!f) { perror(argv[ai]); return 2; }
            n0 = (unsigned)fread(buf, 1, sizeof buf, f); fclose(f);
            for (lv = 0; lv < 3; lv++) {
                zl = sizeof zb;
                if (compress2(zb, &zl, buf, n0, lv == 0 ? 0 : lv == 1 ? 1 : 9) != Z_OK) return 2;
                if ((unsigned)zl + n0 > 65000u - 0x400) continue;
                for (it = 0; it < iters; it++) {
                    pdw top, got;
                    memcpy(mut, zb, zl); n = (unsigned)zl;
                    if (it) mutate(mut, &n);
                    g_pcap = (pdw)(n0 + n + (it ? rnd() % 64 : 0));
                    top = (pdw)(g_pcap - n);
                    memcpy(pd_host_mem + (0x3000UL << 4) + top, mut, n);
                    g_zseg = 0x3000; g_zin = top; g_zinend = g_pcap; g_zout = g_zstart = 0; g_zlim = g_pcap;
                    got = z_inflate();
                    if (!it && (got != n0 || g_zmore || memcmp(pd_host_mem + (0x3000UL << 4), buf, n0))) {
                        fprintf(stderr, "pdx_fuzz -z: %s level %u: inflated %u of %u bytes wrong (more %u err %u)\n", argv[ai], lv, got, n0, g_zmore, g_zerr);
                        return 1;
                    }
                    if (!it) ok++;
                    more += g_zmore;
                }
            }
            printf("pdx_fuzz -z: %-24s %u levels exact, %ld mutations each, clean\n", argv[ai], ok, (long)iters);
        }
        g_pcap = 37376;
        return 0;
    }
    if (cmaps) {
        long it; unsigned n; int which;
        for (it = 0; it < iters * 20L; it++) {
            which = (int)(rnd() & 1);
            n = which ? (unsigned)sizeof cm2 - 1 : (unsigned)sizeof cm1 - 1;
            memcpy(mut, which ? cm2 : cm1, n);
            if (it) mutate(mut, &n);
            fz_src = mut; fz_len = n; fz_pos = 0;
            if (getenv("PDX_FZ_TRACE")) { FILE *tf = fopen("/tmp/pdx_fz_last.bin", "wb"); fwrite(mut, 1, n, tf); fclose(tf); }
            cm_parse((pdw)(rnd() & 1), 0x6000, 0, 2048);
            if (g_cmcnt > 340) { fprintf(stderr, "cmap count %u\n", g_cmcnt); return 1; }
        }
        printf("pdx_fuzz: %ld CMap parses, clean\n", iters * 20L);
        return 0;
    }
    for (; ai < argc; ai++) {
        FILE *f = fopen(argv[ai], "rb"); long it; unsigned n0, n, g, opened = 0, drawn = 0;
        if (!f) { perror(argv[ai]); return 2; }
        n0 = (unsigned)fread(buf, 1, sizeof buf, f); fclose(f);
        for (it = 0; it < iters; it++) {
            memcpy(mut, buf, n0); n = n0;
            if (it) mutate(mut, &n);
            memcpy(pd_host_mem + (0x3000UL << 4), mut, n);
            g_pseg = 0x3000; g_plen = (pdw)n;
            if (!fnt_open()) continue;
            opened++;
            g_em64 = 12 * 64;
            { pdw ng = g_kind == PDFK_TT ? g_sf_ng : g_kind == PDFK_CFF ? g_cf_ng : (pdw)(g_t1_ng + 1); if (ng > 150) ng = 150;
              for (g = 0; g < ng; g++) { if (fnt_glyph(g, 12 * 64, 12, 16, 0x7000, 0, 4096) != 0xFFFF) drawn++; } }
        }
        printf("pdx_fuzz: %-28s %ld mutations, %u opened, %u glyphs drawn, clean\n", argv[ai], (long)iters, opened, drawn);
    }
    return 0;
}
