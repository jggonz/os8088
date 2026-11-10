/* ============================================================================
 * os8088 - apps/pagedesk/pdxgm.c    PDXG's 16.16 transcendentals (20.22.9)
 *
 * log2, exp2, pow, sqrt, sin, cos, atan2, the sRGB encode: linear interpolation
 * over the tables of pdxgmt.c, in struct pd_fx. #included by pdxg.c. No
 * recursion, no long. An angle is degrees, as the PostScript calculator has it.
 * ==========================================================================*/
#include "pdxgmt.c"

typedef struct pd_fx fx;

static void fx_i(fx *r, pdi n) { pdm_fx_int(r, n); }
static void fx_raw(fx *r, pdi hi, pdw lo) { r->hi = hi; r->lo = lo; }
static pdi fx_sign(const fx *a) { return a->hi < 0 ? -1 : (a->hi == 0 && a->lo == 0 ? 0 : 1); }
static void fx_add(fx *r, const fx *a, const fx *b) { pdm_fx_add(r, a, b); }
static void fx_sub(fx *r, const fx *a, const fx *b) { pdm_fx_sub(r, a, b); }
static void fx_mul(fx *r, const fx *a, const fx *b) { pdm_fx_mul(r, a, b); }
static void fx_div(fx *r, const fx *a, const fx *b)
{
    if (b->hi == 0 && b->lo == 0) { fx_raw(r, 0, 0); return; }
    pdm_fx_div(r, a, b);
}
static pdi fx_cmp(const fx *a, const fx *b) { return pdm_fx_cmp(a, b); }
static void fx_clamp01(fx *r)
{
    if (r->hi < 0) fx_raw(r, 0, 0);
    else if (r->hi > 0) fx_raw(r, 1, 0);
}
/* x in 0..1 (16.16) to 0..255, rounded */
static pdw fx_to255(const fx *a)
{
    fx t, k;
    fx_raw(&k, 255, 0);
    t = *a;
    fx_clamp01(&t);
    fx_mul(&t, &t, &k);
    return (pdw)pdm_fx_round(&t);
}

/* the high 16 bits of a*b */
static pdw gm_mulh(pdw a, pdw b)
{
    struct pd_u32 r;
    pdm_u32_mul16(&r, a, b);
    return r.hi;
}
/* tab[i] interpolated at i + f/65536 */
static pdw gm_tab(const pdw *t, pdw i, pdw f)
{
    pdw a = t[i], b = t[i + 1];
    if (b >= a) return a + gm_mulh(b - a, f);
    return a - gm_mulh(a - b, f);
}

/* log2 of a positive value; <= 0 gives a very negative number */
static void fx_log2(fx *r, const fx *x)
{
    pdw lo = x->lo, m;
    pdi e;
    if (x->hi <= 0 && lo == 0) { fx_raw(r, -16, 0); return; }
    if (x->hi < 0) { fx_raw(r, -16, 0); return; }
    if (x->hi > 0) {
        pdw hi = (pdw)x->hi;
        e = 0;
        while ((hi >> (e + 1)) != 0) e++;
        m = e ? (pdw)((lo >> e) | (hi << (16 - e))) : lo;
    } else {
        pdw p = 15;
        while (p > 0 && !(lo & (1u << p))) p--;
        e = (pdi)p - 16;
        m = p ? (pdw)(lo << (16 - p)) : 0;
    }
    /* m: the 16 bits below the leading one */
    r->hi = e;
    r->lo = gm_tab(gm_log2, m >> 10, (pdw)((m & 0x3FF) << 6));
}

static void fx_exp2(fx *r, const fx *x)
{
    pdi ip = x->hi;
    pdw m, hi, lo;
    if (ip > 14) { fx_raw(r, 32767, 0xFFFF); return; }
    if (ip < -16) { fx_raw(r, 0, 0); return; }
    m = gm_tab(gm_exp2, x->lo >> 10, (pdw)((x->lo & 0x3FF) << 6));
    hi = 1; lo = m;                 /* 1 + m/65536 */
    if (ip > 0) {
        hi = (pdw)((hi << ip) | (lo >> (16 - ip)));
        lo = (pdw)(lo << ip);
    } else if (ip < 0) {
        pdi s = -ip;
        if (s >= 16) { lo = 1; hi = 0; }
        else { lo = (pdw)((lo >> s) | (hi << (16 - s))); hi = (pdw)(hi >> s); }
    }
    r->hi = (pdi)hi; r->lo = lo;
}

/* x ^ y for x >= 0 */
static void fx_pow(fx *r, const fx *x, const fx *y)
{
    fx l;
    if (y->hi == 0 && y->lo == 0) { fx_i(r, 1); return; }
    if (x->hi < 0 || (x->hi == 0 && x->lo == 0)) { fx_raw(r, 0, 0); return; }
    if (y->hi == 1 && y->lo == 0) { *r = *x; return; }
    fx_log2(&l, x);
    fx_mul(&l, &l, y);
    fx_exp2(r, &l);
}

static void fx_sqrt(fx *r, const fx *x)
{
    fx l, g, q, h;
    pdi i;
    if (x->hi < 0 || (x->hi == 0 && x->lo == 0)) { fx_raw(r, 0, 0); return; }
    fx_log2(&l, x);
    /* l / 2 (arithmetic) */
    l.lo = (pdw)((l.lo >> 1) | ((pdw)l.hi << 15));
    l.hi >>= 1;
    fx_exp2(&g, &l);
    fx_raw(&h, 0, 0x8000);
    for (i = 0; i < 3; i++) {
        if (g.hi == 0 && g.lo == 0) break;
        fx_div(&q, x, &g);
        fx_add(&g, &g, &q);
        fx_mul(&g, &g, &h);
    }
    *r = g;
}

/* sine of an angle in degrees */
static void fx_sin(fx *r, const fx *deg)
{
    fx d, k, z;
    pdi neg = 0;
    d = *deg;
    while (d.hi < 0) d.hi += 360;
    while (d.hi >= 360) d.hi -= 360;
    if (d.hi >= 180) { d.hi -= 180; neg = 1; }
    if (d.hi > 90 || (d.hi == 90 && d.lo)) { fx_i(&k, 180); fx_sub(&d, &k, &d); }
    if (d.hi >= 90) fx_i(r, 1);
    else fx_raw(r, 0, gm_tab(gm_sin, (pdw)d.hi, d.lo));
    if (neg) { fx_raw(&z, 0, 0); fx_sub(r, &z, r); }
}
static void fx_cos(fx *r, const fx *deg)
{
    fx d, n;
    fx_i(&n, 90);
    fx_sub(&d, &n, deg);
    fx_sin(r, &d);
}

/* atan2(num, den) in degrees, 0..360 (the PostScript atan) */
static void fx_atan2(fx *r, const fx *num, const fx *den)
{
    fx a, b, q, t;
    pdi swap = 0, quad;
    pdw v;
    pdw i, f;
    fx_raw(&a, num->hi < 0 ? -num->hi - (num->lo ? 1 : 0) : num->hi,
           num->hi < 0 ? (pdw)(0 - num->lo) : num->lo);
    fx_raw(&b, den->hi < 0 ? -den->hi - (den->lo ? 1 : 0) : den->hi,
           den->hi < 0 ? (pdw)(0 - den->lo) : den->lo);
    if (fx_cmp(&a, &b) > 0) { t = a; a = b; b = t; swap = 1; }
    if (b.hi == 0 && b.lo == 0) { fx_raw(r, 0, 0); return; }
    fx_div(&q, &a, &b);                 /* 0..1 */
    i = q.hi ? 64 : (q.lo >> 10);
    f = q.hi ? 0 : (pdw)((q.lo & 0x3FF) << 6);
    v = i >= 64 ? gm_atan[64] : gm_tab(gm_atan, i, f);     /* degrees * 256 */
    fx_raw(&t, (pdi)(v >> 8), (pdw)((v & 0xFF) << 8));
    if (swap) { fx_i(&q, 90); fx_sub(&t, &q, &t); }
    /* t is the first-quadrant angle of (|den|, |num|) */
    quad = (den->hi < 0) ? (num->hi < 0 ? 3 : 2) : (num->hi < 0 ? 4 : 1);
    if (quad == 2) { fx_i(&q, 180); fx_sub(&t, &q, &t); }
    else if (quad == 3) { fx_i(&q, 180); fx_add(&t, &q, &t); }
    else if (quad == 4) { fx_i(&q, 360); fx_sub(&t, &q, &t); }
    *r = t;
}

/* linear light 0..1 to the sRGB-encoded value 0..1 */
static void fx_senc(fx *r, const fx *lin)
{
    fx x = *lin;
    pdw i, f, v;
    fx_clamp01(&x);
    if (x.hi) { fx_i(r, 1); return; }
    i = x.lo >> 10; f = (pdw)((x.lo & 0x3FF) << 6);
    /* coarse table near black (the toe is steep): refine with two more bits */
    v = gm_tab(gm_senc, i, f);
    r->hi = 0; r->lo = v; if (v == 65535) fx_i(r, 1);
}
