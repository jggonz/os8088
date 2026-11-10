/* ---- pdxjtw.c: the HOST TWINS of pdxjasm.inc (PD_HOST only) -------------------
 * Each function is the reference for the assembly routine of the same name;
 * hosttest/pdxj_asm.py runs the assembly in Unicorn and holds the two equal bit
 * for bit, including the 16-bit wrap of the intermediate sums. */
#include <stdint.h>
#include "hosttest/pdxj_mqtab.h"
extern unsigned char pd_host_mem[];

pdb xj_cx[19];
static uint16_t mq_a, mq_chi, mq_clo, mq_ct;
static pdb *mq_bp;

void xj_cxreset(void)
{
    int i;
    for (i = 0; i < 19; i++) xj_cx[i] = 0;
    xj_cx[0] = 4 << 1; xj_cx[17] = 3 << 1; xj_cx[18] = 46 << 1;
}
static void mq_bytein(void)
{
    uint32_t c = ((uint32_t)mq_chi << 16) | mq_clo;
    if (*mq_bp == 0xFF) {
        if (mq_bp[1] > 0x8F) { c += 0xFF00; mq_ct = 8; }
        else { mq_bp++; c += (uint32_t)*mq_bp << 9; mq_ct = 7; }
    } else { mq_bp++; c += (uint32_t)*mq_bp << 8; mq_ct = 8; }
    mq_chi = (uint16_t)(c >> 16); mq_clo = (uint16_t)c;
}
void xj_mqinit(pdb *buf)
{
    uint32_t c;
    mq_bp = buf;
    mq_chi = (uint16_t)*mq_bp; mq_clo = 0;
    mq_bytein();
    c = ((uint32_t)mq_chi << 16) | mq_clo;
    c <<= 7;
    mq_chi = (uint16_t)(c >> 16); mq_clo = (uint16_t)c;
    mq_ct -= 7;
    mq_a = 0x8000;
}
static void mq_renorm(void)
{
    uint32_t c;
    do {
        if (mq_ct == 0) mq_bytein();
        mq_a <<= 1;
        c = ((uint32_t)mq_chi << 16) | mq_clo;
        c <<= 1;
        mq_chi = (uint16_t)(c >> 16); mq_clo = (uint16_t)c;
        mq_ct--;
    } while (!(mq_a & 0x8000));
}
pdw xj_mqdec(pdw cx)
{
    unsigned s = xj_cx[cx], qe = xj_qe94[s], d;
    mq_a = (uint16_t)(mq_a - qe);
    if (mq_chi < qe) {
        if (mq_a < qe) { d = s & 1; xj_cx[cx] = xj_nmps94[s]; }
        else { d = (s & 1) ^ 1; xj_cx[cx] = xj_nlps94[s]; }
        mq_a = (uint16_t)qe;
        mq_renorm();
        return d;
    }
    mq_chi = (uint16_t)(mq_chi - qe);
    if (mq_a & 0x8000) return s & 1;
    if (mq_a < qe) { d = (s & 1) ^ 1; xj_cx[cx] = xj_nlps94[s]; }
    else { d = s & 1; xj_cx[cx] = xj_nmps94[s]; }
    mq_renorm();
    return d;
}

static int16_t *farp(pdw seg, pdw off) { return (int16_t *)(pd_host_mem + ((uint32_t)seg << 4) + off); }
static void lift(int16_t *d, int16_t *a, int16_t *b, pdw n, pdi k)
{
    pdw i;
    for (i = 0; i < n; i++) {
        int16_t s = (int16_t)(a[i] + b[i]);
        int32_t p = (int32_t)s * k + 8192;
        d[i] = (int16_t)(d[i] - (int16_t)(p >> 14));
    }
}
static void l53(int16_t *d, int16_t *a, int16_t *b, pdw n, pdw mode)
{
    pdw i;
    for (i = 0; i < n; i++) {
        int16_t s = (int16_t)(a[i] + b[i]);
        if (mode == 0) { s = (int16_t)(s + 2); d[i] = (int16_t)(d[i] - (s >> 2)); }
        else d[i] = (int16_t)(d[i] + (s >> 1));
    }
}
static void scal(int16_t *d, pdw n, pdi k)
{
    pdw i;
    for (i = 0; i < n; i++) d[i] = (int16_t)(((int32_t)d[i] * k + 8192) >> 14);
}
void xj_lift_n(pdi *dst, pdi *a, pdi *b, pdw n, pdi k) { lift(dst, a, b, n, k); }
void xj_lift_f(pdw seg, pdw dst, pdw a, pdw b, pdw n, pdi k) { lift(farp(seg, dst), farp(seg, a), farp(seg, b), n, k); }
void xj_l53_n(pdi *dst, pdi *a, pdi *b, pdw n, pdw mode) { l53(dst, a, b, n, mode); }
void xj_l53_f(pdw seg, pdw dst, pdw a, pdw b, pdw n, pdw mode) { l53(farp(seg, dst), farp(seg, a), farp(seg, b), n, mode); }
void xj_scal_n(pdi *dst, pdw n, pdi k) { scal(dst, n, k); }
void xj_scal_f(pdw seg, pdw dst, pdw n, pdi k) { scal(farp(seg, dst), n, k); }
void xj_inter(pdi *out, pdi *e, pdi *o, pdw lw, pdw nh)
{
    pdw i;
    for (i = 0; i < nh; i++) { out[2 * i] = e[i]; out[2 * i + 1] = o[i]; }
    if (lw > nh) out[2 * nh] = e[nh];
}
void xj_deq(pdw *mag, pdw *flg, pdw seg, pdw off, pdw n, pdw m, pdw sh, pdw rnd)
{
    pdw i; int16_t *d = farp(seg, off);
    for (i = 0; i < n; i++) {
        uint32_t p; int32_t c;
        if (!mag[i]) { d[i] = 0; continue; }
        p = (uint32_t)mag[i] * m + rnd;
        c = sh >= 32 ? 0 : (int32_t)(p >> sh);
        if (c > 32767) c = 32767;
        d[i] = (int16_t)((flg[i] & 0x8000) ? -c : c);
    }
}
void xj_grey(pdw sseg, pdw soff, pdw dseg, pdw doff, pdw n, pdi add, pdw sh,
             pdw maxv, pdb *tab, pdw inv)
{
    pdw i; int16_t *s = farp(sseg, soff); uint8_t *d = pd_host_mem + ((uint32_t)dseg << 4) + doff;
    for (i = 0; i < n; i++) {
        int32_t t = (int32_t)s[i] + add;
        int v;
        if (t > 32767) t = 32767;
        v = (int)(t >> sh);
        if (v < 0) v = 0;
        if (v > (int)maxv) v = (int)maxv;
        if (tab) v = tab[v];
        if (inv) v = 255 - v;
        d[i] = (uint8_t)v;
    }
}
