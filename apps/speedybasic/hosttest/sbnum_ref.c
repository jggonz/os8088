/* Host reference for the target-only sbnum.inc numeric ABI. */
#include <stdint.h>
#include <limits.h>
#include "sbnum.h"

unsigned short sbn_alo;
short sbn_ahi;
short sbn_akind;
unsigned short sbn_blo;
short sbn_bhi;
short sbn_bkind;
short sbn_err;

static uint32_t sbnr_bits(unsigned short lo, short hi)
{
    return (uint32_t)lo | ((uint32_t)(uint16_t)hi << 16);
}

static int32_t sbnr_sbits(unsigned short lo, short hi)
{
    return (int32_t)sbnr_bits(lo, hi);
}

static void sbnr_puta(uint32_t v)
{
    sbn_alo = (unsigned short)v;
    sbn_ahi = (short)(v >> 16);
}

static void sbnr_putb(uint32_t v)
{
    sbn_blo = (unsigned short)v;
    sbn_bhi = (short)(v >> 16);
}

static void sbnr_fail(int e, int kind)
{
    sbn_err = (short)e;
    sbn_alo = 0;
    sbn_ahi = 0;
    sbn_akind = (short)kind;
}

static int sbnr_goodkind(int k)
{
    return k == SBN_LONG || k == SBN_FIXED;
}

static int sbnr_promote_a(void)
{
    int32_t a;
    if (sbn_akind == SBN_FIXED) return 1;
    if (sbn_akind != SBN_LONG) return 0;
    a = sbnr_sbits(sbn_alo, sbn_ahi);
    if (a < -32768 || a > 32767) return 0;
    sbnr_puta((uint32_t)a << 16);
    sbn_akind = SBN_FIXED;
    return 1;
}

static int sbnr_promote_b(void)
{
    int32_t b;
    if (sbn_bkind == SBN_FIXED) return 1;
    if (sbn_bkind != SBN_LONG) return 0;
    b = sbnr_sbits(sbn_blo, sbn_bhi);
    if (b < -32768 || b > 32767) return 0;
    sbnr_putb((uint32_t)b << 16);
    sbn_bkind = SBN_FIXED;
    return 1;
}

static int sbnr_prepare(void)
{
    sbn_err = SBN_OK;
    if (!sbnr_goodkind(sbn_akind) || !sbnr_goodkind(sbn_bkind)) {
        sbnr_fail(SBN_BADKIND, SBN_LONG);
        return 0;
    }
    if (sbn_akind == sbn_bkind) return 1;
    if (!sbnr_promote_a() || !sbnr_promote_b()) {
        sbnr_fail(SBN_OVERFLOW, SBN_FIXED);
        return 0;
    }
    return 1;
}

static uint64_t sbnr_mag(int32_t v)
{
    return v < 0 ? (uint64_t)(-(int64_t)v) : (uint64_t)v;
}

static int sbnr_put_signed_mag(uint64_t m, int neg, int kind)
{
    uint64_t lim;
    lim = neg ? UINT64_C(0x80000000) : UINT64_C(0x7fffffff);
    if (m > lim) {
        sbnr_fail(SBN_OVERFLOW, kind);
        return 0;
    }
    if (neg) sbnr_puta((uint32_t)(UINT64_C(0x100000000) - m));
    else sbnr_puta((uint32_t)m);
    sbn_akind = (short)kind;
    return 1;
}

void sbn_add(void)
{
    int64_t r;
    int kind;
    if (!sbnr_prepare()) return;
    kind = sbn_akind;
    r = (int64_t)sbnr_sbits(sbn_alo, sbn_ahi) +
        (int64_t)sbnr_sbits(sbn_blo, sbn_bhi);
    if (r < INT32_MIN || r > INT32_MAX) sbnr_fail(SBN_OVERFLOW, kind);
    else sbnr_puta((uint32_t)(int32_t)r);
}

void sbn_sub(void)
{
    int64_t r;
    int kind;
    if (!sbnr_prepare()) return;
    kind = sbn_akind;
    r = (int64_t)sbnr_sbits(sbn_alo, sbn_ahi) -
        (int64_t)sbnr_sbits(sbn_blo, sbn_bhi);
    if (r < INT32_MIN || r > INT32_MAX) sbnr_fail(SBN_OVERFLOW, kind);
    else sbnr_puta((uint32_t)(int32_t)r);
}

void sbn_mul(void)
{
    int32_t a;
    int32_t b;
    uint64_t p;
    uint64_t q;
    int neg;
    int kind;
    if (!sbnr_prepare()) return;
    kind = sbn_akind;
    a = sbnr_sbits(sbn_alo, sbn_ahi);
    b = sbnr_sbits(sbn_blo, sbn_bhi);
    neg = (a < 0) != (b < 0);
    p = sbnr_mag(a) * sbnr_mag(b);
    if (kind == SBN_LONG) q = p;
    else {
        q = p >> 16;
        if ((p & 0xffffu) > 0x8000u ||
            ((p & 0xffffu) == 0x8000u && (q & 1u))) ++q;
    }
    sbnr_put_signed_mag(q, neg, kind);
}

void sbn_div(void)
{
    int32_t a;
    int32_t b;
    uint64_t n;
    uint64_t d;
    uint64_t q;
    uint64_t r;
    int neg;
    int kind;
    if (!sbnr_prepare()) return;
    kind = sbn_akind;
    a = sbnr_sbits(sbn_alo, sbn_ahi);
    b = sbnr_sbits(sbn_blo, sbn_bhi);
    if (!b) {
        sbnr_fail(SBN_DIVZERO, kind);
        return;
    }
    neg = (a < 0) != (b < 0);
    n = sbnr_mag(a);
    d = sbnr_mag(b);
    if (kind == SBN_FIXED) n <<= 16;
    q = n / d;
    r = n % d;
    if (kind == SBN_FIXED &&
        (r > d - r || (r == d - r && (q & 1u)))) ++q;
    sbnr_put_signed_mag(q, neg, kind);
}

void sbn_fdiv(void)
{
    int32_t a;
    int32_t b;
    uint64_t n;
    uint64_t d;
    uint64_t q;
    uint64_t r;
    int neg;
    if (sbn_akind != SBN_LONG || sbn_bkind != SBN_LONG) {
        sbn_div();
        return;
    }
    sbn_err = SBN_OK;
    a = sbnr_sbits(sbn_alo, sbn_ahi);
    b = sbnr_sbits(sbn_blo, sbn_bhi);
    if (!b) {
        sbnr_fail(SBN_DIVZERO, SBN_FIXED);
        return;
    }
    neg = (a < 0) != (b < 0);
    n = sbnr_mag(a) << 16;
    d = sbnr_mag(b);
    q = n / d;
    r = n % d;
    if (r > d - r || (r == d - r && (q & 1u))) ++q;
    sbnr_put_signed_mag(q, neg, SBN_FIXED);
}

int sbn_cmp(void)
{
    int32_t a;
    int32_t b;
    sbn_err = SBN_OK;
    if (!sbnr_goodkind(sbn_akind) || !sbnr_goodkind(sbn_bkind)) {
        sbnr_fail(SBN_BADKIND, SBN_LONG);
        return 0;
    }
    if (sbn_akind != sbn_bkind) {
        if (sbn_akind == SBN_LONG) {
            a = sbnr_sbits(sbn_alo, sbn_ahi);
            if (a < -32768) return -1;
            if (a > 32767) return 1;
            sbnr_promote_a();
        } else {
            b = sbnr_sbits(sbn_blo, sbn_bhi);
            if (b < -32768) return 1;
            if (b > 32767) return -1;
            sbnr_promote_b();
        }
    }
    a = sbnr_sbits(sbn_alo, sbn_ahi);
    b = sbnr_sbits(sbn_blo, sbn_bhi);
    return a < b ? -1 : (a > b ? 1 : 0);
}

void sbn_neg(void)
{
    int32_t a;
    int kind;
    sbn_err = SBN_OK;
    kind = sbn_akind;
    if (!sbnr_goodkind(kind)) {
        sbnr_fail(SBN_BADKIND, SBN_LONG);
        return;
    }
    a = sbnr_sbits(sbn_alo, sbn_ahi);
    if (a == INT32_MIN) sbnr_fail(SBN_OVERFLOW, kind);
    else sbnr_puta((uint32_t)-a);
}

void sbn_floor(void)
{
    int32_t a;
    sbn_err = SBN_OK;
    if (sbn_akind == SBN_LONG) return;
    if (sbn_akind != SBN_FIXED) {
        sbnr_fail(SBN_BADKIND, SBN_LONG);
        return;
    }
    a = sbnr_sbits(sbn_alo, sbn_ahi);
    sbnr_puta((uint32_t)(a >> 16));
    sbn_akind = SBN_LONG;
}

void sbn_fix(void)
{
    int32_t a;
    sbn_err = SBN_OK;
    if (sbn_akind == SBN_LONG) return;
    if (sbn_akind != SBN_FIXED) {
        sbnr_fail(SBN_BADKIND, SBN_LONG);
        return;
    }
    a = sbnr_sbits(sbn_alo, sbn_ahi);
    sbnr_puta((uint32_t)(a / 65536));
    sbn_akind = SBN_LONG;
}

void sbn_cint(void)
{
    int32_t a;
    uint32_t m;
    uint32_t q;
    uint32_t f;
    int neg;
    sbn_err = SBN_OK;
    if (sbn_akind == SBN_LONG) return;
    if (sbn_akind != SBN_FIXED) {
        sbnr_fail(SBN_BADKIND, SBN_LONG);
        return;
    }
    a = sbnr_sbits(sbn_alo, sbn_ahi);
    neg = a < 0;
    m = (uint32_t)sbnr_mag(a);
    q = m >> 16;
    f = m & 0xffffu;
    if (f > 0x8000u || (f == 0x8000u && (q & 1u))) ++q;
    sbnr_put_signed_mag(q, neg, SBN_LONG);
}

void sbn_float(void)
{
    sbn_err = SBN_OK;
    if (!sbnr_goodkind(sbn_akind)) {
        sbnr_fail(SBN_BADKIND, SBN_FIXED);
        return;
    }
    if (!sbnr_promote_a()) sbnr_fail(SBN_OVERFLOW, SBN_FIXED);
}

void sbn_sqrt(void)
{
    uint64_t n;
    uint64_t r;
    uint64_t bit;
    uint32_t q;
    int32_t a;
    sbn_err = SBN_OK;
    if (!sbnr_goodkind(sbn_akind)) {
        sbnr_fail(SBN_BADKIND, SBN_FIXED);
        return;
    }
    a = sbnr_sbits(sbn_alo, sbn_ahi);
    if (a < 0) {
        sbnr_fail(SBN_DOMAIN, SBN_FIXED);
        return;
    }
    if (sbn_akind == SBN_LONG) {
        if ((uint32_t)a > UINT32_C(0x3fffffff)) {
            sbnr_fail(SBN_OVERFLOW, SBN_FIXED);
            return;
        }
        n = (uint64_t)(uint32_t)a << 32;
    } else n = (uint64_t)(uint32_t)a << 16;
    /* Restoring square root, two input bits per turn. */
    r = 0;
    q = 0;
    bit = UINT64_C(1) << 62;
    while (bit) {
        uint64_t t;
        q <<= 1;
        r = (r << 2) | (n >> 62);
        n <<= 2;
        t = ((uint64_t)q << 1) | 1u;
        if (r >= t) {
            r -= t;
            ++q;
        }
        bit >>= 2;
    }
    if (r > q) ++q;                 /* nearest Q16.16 result */
    sbnr_puta(q);
    sbn_akind = SBN_FIXED;
}

static void sbnr_fixed_mul_raw(uint32_t b)
{
    sbnr_putb(b);
    sbn_bkind = SBN_FIXED;
    sbn_mul();
}

static int sbnr_to_fixed(void)
{
    sbn_err = SBN_OK;
    if (!sbnr_goodkind(sbn_akind) || !sbnr_promote_a()) {
        sbnr_fail(sbn_akind == SBN_LONG ? SBN_OVERFLOW : SBN_BADKIND,
                  SBN_FIXED);
        return 0;
    }
    return 1;
}

static void sbnr_reduce(void)
{
    int32_t x;
    x = sbnr_sbits(sbn_alo, sbn_ahi);
    while (x > 205887) x -= 411775;
    while (x < -205887) x += 411775;
    sbnr_puta((uint32_t)x);
}

void sbn_sin(void)
{
    uint32_t x;
    uint32_t x2;
    int32_t v;
    if (!sbnr_to_fixed()) return;
    sbnr_reduce();
    v = sbnr_sbits(sbn_alo, sbn_ahi);
    if (v > 102944) v = 205887 - v;
    else if (v < -102944) v = -205887 - v;
    sbnr_puta((uint32_t)v);
    x = sbnr_bits(sbn_alo, sbn_ahi);
    sbnr_fixed_mul_raw(x);
    x2 = sbnr_bits(sbn_alo, sbn_ahi);
    sbnr_puta((uint32_t)-13);
    sbnr_fixed_mul_raw(x2);
    sbnr_puta(sbnr_bits(sbn_alo, sbn_ahi) + 546u);
    sbnr_fixed_mul_raw(x2);
    sbnr_puta(sbnr_bits(sbn_alo, sbn_ahi) - 10923u);
    sbnr_fixed_mul_raw(x2);
    sbnr_puta(sbnr_bits(sbn_alo, sbn_ahi) + 65536u);
    sbnr_fixed_mul_raw(x);
}

void sbn_cos(void)
{
    if (!sbnr_to_fixed()) return;
    sbnr_reduce();
    sbnr_puta(sbnr_bits(sbn_alo, sbn_ahi) + 102944u);
    sbn_sin();
}
