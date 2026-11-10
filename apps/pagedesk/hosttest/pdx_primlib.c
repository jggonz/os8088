/* the C twins of pdxfrs.inc, for hosttest/pdx_primtest.py: pdxfol.c's accumulator and pack, as a shared library */
#include "../pdabi.h"
#include "../pdmath.h"
unsigned char pd_host_mem[0x110000];
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
static pdw g_upem, g_em64;
#include "../pdxfol.c"
void twin_accline(short x0, short y0, short x1, short y1) { pdx_accline(x0, y0, x1, y1); }
unsigned short twin_pack(unsigned short seg, unsigned short off) { return pdx_pack(seg, off); }
