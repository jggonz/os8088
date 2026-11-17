/* pdi_twin - the C twins' side of the Unicorn differential test (pdi_asm.py).
 * stdin: records; stdout: results. See pdi_asm.py for the formats. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PD_HOST 1
#include "../pdxi.c"
extern unsigned char pd_host_mem[];
pdw pdi_engcall(pdw v, pdw b, pdw c, pdw d) { (void)v; (void)b; (void)c; (void)d; pdi_engcf = 1; return PDE_RANGE; }
static void rd(void *p, int n) { if (fread(p, 1, n, stdin) != (size_t)n) exit(0); }
int main(void)
{
    unsigned char kind;
    while (fread(&kind, 1, 1, stdin) == 1) {
        if (kind == 'I') {                       /* IDCT: mode, coef[64], qt[64] */
            uint16_t mode, stride = 11; int16_t coef[64]; uint16_t qt[64]; int n, i;
            unsigned char out[8 * 11];
            rd(&mode, 2); rd(coef, 128); rd(qt, 128);
            memset(pd_host_mem + 0x1000 * 16, 0xA5, 8 * 11);
            { pdi c[64]; pdw q[64]; for (i = 0; i < 64; i++) { c[i] = coef[i]; q[i] = qt[i]; }
              pdi_idct(c, q, mode, 0x1000, 0, stride); }
            n = mode == 0 ? 8 : (mode == 1 ? 4 : 2);
            memcpy(out, pd_host_mem + 0x1000 * 16, 8 * 11);
            fwrite(out, 1, 8 * 11, stdout); (void)n;
        } else if (kind == 'B') {                /* block: counts dc/ac, vals, ebuf, pos */
            static struct dj_huff dc, ac; static unsigned char cdc[16], cac[16];
            uint16_t pos, ndc, nac; pdi coef[64]; pdi diff; int16_t c16[64]; int i;
            rd(cdc, 16); rd(&ndc, 2); rd(dc.vals, 256);
            rd(cac, 16); rd(&nac, 2); rd(ac.vals, 256);
            rd(pdi_ebuf, 1420); rd(&pos, 2);
            if (dj_dht_build(&dc, cdc, ndc) || dj_dht_build(&ac, cac, nac)) { fputc(1, stdout); continue; }
            fputc(0, stdout);
            fwrite(&dc, 1, 838, stdout); fwrite(&ac, 1, 838, stdout);
            pdi_pos = pos;
            diff = pdi_block(&dc, &ac, coef);
            for (i = 0; i < 64; i++) c16[i] = coef[i];
            fwrite(c16, 2, 64, stdout); { int16_t d = diff; fwrite(&d, 2, 1, stdout); }
            { uint16_t p = pdi_pos; fwrite(&p, 2, 1, stdout); }
        } else if (kind == 'S') {                /* bits: n, ebuf, pos */
            uint16_t n, pos, v, p;
            rd(&n, 2); rd(pdi_ebuf, 1420); rd(&pos, 2);
            pdi_pos = pos; v = pdi_bits(n); p = pdi_pos;
            fwrite(&v, 2, 1, stdout); fwrite(&p, 2, 1, stdout);
        }
    }
    return 0;
}
