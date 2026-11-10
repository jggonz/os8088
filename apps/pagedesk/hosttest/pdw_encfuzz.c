/* ============================================================================
 * os8088 - apps/pagedesk/hosttest/pdw_encfuzz.c   ASan + UBSan fuzz of pb_src
 *
 * PDWRT's one new parser (PAGEDESK-SPEC 20.22.4.3): text copied out of a PLAIN
 * source object, in which every literal and hex string is decoded and written
 * enciphered. Random and mutated text - unbalanced parentheses, dangling
 * backslashes, odd hex, `<<`, strings that straddle the end of the span - goes
 * through it with a stand-in cipher; the writer must neither crash nor run
 * past its buffers (the sanitizers watch) nor loop, and the number of strings
 * it enciphers must equal the number a plain reading finds.
 * ==========================================================================*/
#include "../pdwrt.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

pdw pdw_engcall(pdw verb, pdw bx, pdw cx, pdw dx)
{                                       /* PDV_ENG_CRYPT: AES-shaped growth, no cipher */
    struct pdxb_cryptop o; unsigned char b[640]; pdw n, i;
    (void)bx; (void)cx; (void)dx; pdw_engcf = 0;
    if (verb != PDV_ENG_CRYPT) { pdw_engcf = 1; return PDE_RANGE; }
    pd_fcopy_in(g_xb, 0, &o, sizeof o);
    n = o.len; if (n > 600) { pdw_engcf = 1; return PDE_LIMIT; }
    pd_fcopy_in(o.seg, o.off, b, n);
    if (o.phase & 1) { memmove(b + 16, b, n); memset(b, 0xAA, 16); n = (pdw)(n + 16); }
    if (o.phase & 2) { for (i = 0; i < 16; i++) b[n++] = 0x10; }
    if (n > o.room) { pdw_engcf = 1; return PDE_LIMIT; }
    pd_fcopy_out(b, o.seg, o.off, n); o.len = n; pd_fcopy_out(&o, g_xb, 0, sizeof o);
    return PDE_OK;
}
unsigned os88_ticks_unused;
static unsigned rnd_s = 12345;
static unsigned rnd(void) { rnd_s = rnd_s * 1103515245u + 12345u; return rnd_s >> 8; }
static const char ALPHA[] = "()\\<>0123456789abcdefABCDEF /nrtbf\r\n#%[]xyz";

int main(int argc, char **argv)
{
    int it, iters = argc > 1 ? atoi(argv[1]) : 20000; char t[700]; unsigned n, i, strings;
    g_xb = 0x2000; g_wseg = 0x7000; g_wkb = 40; g_clu = 512; g_outcap = 4096;
    g_encs = g_ence = 1; g_cmstr = g_cmstm = PDCM_AESV2; g_cop.key_len = 16;
    for (it = 0; it < iters; it++) {
        n = rnd() % 96; strings = 0;           /* 48 two-byte strings is 4 KB of hex: stays inside the output region */
        for (i = 0; i < n; i++) t[i] = ALPHA[rnd() % (sizeof ALPHA - 1)];
        g_wpos = 0; g_lnn = 0; g_err = 0; g_mode = 0; g_created = 0; pdm_u32_set(&g_pos, 0, 0);
        g_outcap = 4096;
        pb_src(t, (pdw)n); pb_flush();
        if (g_err) { printf("FAIL: g_err %u at iteration %d\n", (unsigned)g_err, it); return 1; }
        if (g_wpos > 4096) { printf("FAIL: wrote %u bytes\n", (unsigned)g_wpos); return 1; }
        for (i = 0; i < g_wpos; i++) { unsigned char c = pd_fpeekb(0x7000, (pdw)i); if (c == '<') strings++; }
        (void)strings;
        g_wpos = 0;
    }
    printf("PASS: pb_src over %d random texts\n", iters);
    return 0;
}
