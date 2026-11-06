/* ============================================================================
 * os8088 - apps/pagedesk/pdmath.c   HOST reference of pdmath.h (PD_HOST only)
 *
 * The specification by example of every routine pdmath.inc implements on the
 * 8086. It uses the host's wide integers, which the target never has; the
 * differential host test runs both over the same inputs. Semantics pinned
 * here and in PAGEDESK-SPEC 4.2:
 *
 *   * pd_fx multiply and divide TRUNCATE TOWARD ZERO (magnitude, then sign),
 *     the shape an 8086 MUL/DIV sequence produces without a correction step.
 *   * Any result that does not fit sets the sticky overflow flag and the
 *     value saturates (u32: wraps are refused by saturating to 0xFFFFFFFF or
 *     0; fx: +/-0x7FFFFFFF).
 *   * pdm_fx_parse rounds to nearest, ties away from zero.
 *   * pdm_fx_to_pt16 rounds to nearest (ties away from zero), saturating.
 *   * pdm_view_fwd floors; pdm_view_inv takes the CEILING, so that
 *     fwd(inv(p)) == p for every pixel: a pixel maps to the smallest pt16
 *     that lands on it, its top-left corner (PAGEDESK-SPEC 5.3).
 * ==========================================================================*/
#ifndef PD_HOST
#error "pdmath.c is the host reference; the target uses pdmath.inc"
#endif
#include <string.h>
#include "pdmath.h"

typedef long long i64;
typedef unsigned long long u64;

static pdw g_ovf;
pdw  pdm_ovf(void)        { return g_ovf; }
void pdm_ovf_clear(void)  { g_ovf = 0; }

static u64  u32v(const struct pd_u32 *a) { return ((u64)a->hi << 16) | a->lo; }
static void u32s(struct pd_u32 *r, i64 v)
{
    if (v < 0) { g_ovf = 1; v = 0; }
    if (v > 0xFFFFFFFFLL) { g_ovf = 1; v = 0xFFFFFFFFLL; }
    r->lo = (pdw)(v & 0xFFFF); r->hi = (pdw)((v >> 16) & 0xFFFF);
}
static i64  fxv(const struct pd_fx *a) { return (i64)(int)(((unsigned)(unsigned short)a->hi << 16) | a->lo); }
static void fxs(struct pd_fx *r, i64 v)
{
    if (v > 0x7FFFFFFFLL)  { g_ovf = 1; v = 0x7FFFFFFFLL; }
    if (v < -0x7FFFFFFFLL) { g_ovf = 1; v = -0x7FFFFFFFLL; }
    r->lo = (pdw)(v & 0xFFFF); r->hi = (pdi)((v >> 16) & 0xFFFF);
}

void pdm_u32_set(struct pd_u32 *r, pdw hi, pdw lo) { r->hi = hi; r->lo = lo; }
void pdm_u32_add(struct pd_u32 *r, const struct pd_u32 *a, const struct pd_u32 *b) { u32s(r, (i64)u32v(a) + (i64)u32v(b)); }
void pdm_u32_sub(struct pd_u32 *r, const struct pd_u32 *a, const struct pd_u32 *b) { u32s(r, (i64)u32v(a) - (i64)u32v(b)); }
void pdm_u32_add16(struct pd_u32 *r, pdw n) { u32s(r, (i64)u32v(r) + n); }
pdi  pdm_u32_cmp(const struct pd_u32 *a, const struct pd_u32 *b) { u64 x = u32v(a), y = u32v(b); return x < y ? -1 : x > y ? 1 : 0; }
void pdm_u32_mul16(struct pd_u32 *r, pdw a, pdw b) { u32s(r, (i64)a * (i64)b); }
pdw  pdm_u32_divmod16(struct pd_u32 *q, const struct pd_u32 *a, pdw d)
{
    u64 v = u32v(a);
    if (d == 0) { g_ovf = 1; u32s(q, 0xFFFFFFFFLL); return 0; }
    u32s(q, (i64)(v / d));
    return (pdw)(v % d);
}
void pdm_u32_shr(struct pd_u32 *r, const struct pd_u32 *a, pdw n) { u32s(r, n >= 32 ? 0 : (i64)(u32v(a) >> n)); }
void pdm_u32_shl(struct pd_u32 *r, const struct pd_u32 *a, pdw n)
{
    u64 v = n >= 32 ? (u32v(a) ? 0x100000000ULL : 0) : (u32v(a) << n);
    u32s(r, v > 0xFFFFFFFFULL ? 0x100000000LL : (i64)v);
}
pdw pdm_u32_todec(char *buf, const struct pd_u32 *a, pdw width)
{
    char t[12]; int n = 0, i; u64 v = u32v(a);
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n < (int)width && n < 10) t[n++] = '0';
    for (i = 0; i < n; i++) buf[i] = t[n - 1 - i];
    buf[n] = 0;
    return (pdw)n;
}

void pdm_fx_int(struct pd_fx *r, pdi n) { fxs(r, (i64)n * 65536); }
pdi  pdm_fx_floor(const struct pd_fx *a) { i64 v = fxv(a); return (pdi)(v >= 0 ? v / 65536 : -((-v + 65535) / 65536)); }
pdi  pdm_fx_round(const struct pd_fx *a) { i64 v = fxv(a); return (pdi)(v >= 0 ? (v + 32768) / 65536 : -((-v + 32768) / 65536)); }
void pdm_fx_add(struct pd_fx *r, const struct pd_fx *a, const struct pd_fx *b) { fxs(r, fxv(a) + fxv(b)); }
void pdm_fx_sub(struct pd_fx *r, const struct pd_fx *a, const struct pd_fx *b) { fxs(r, fxv(a) - fxv(b)); }
void pdm_fx_neg(struct pd_fx *r, const struct pd_fx *a) { fxs(r, -fxv(a)); }
pdi  pdm_fx_cmp(const struct pd_fx *a, const struct pd_fx *b) { i64 x = fxv(a), y = fxv(b); return x < y ? -1 : x > y ? 1 : 0; }
void pdm_fx_mul(struct pd_fx *r, const struct pd_fx *a, const struct pd_fx *b)
{
    i64 x = fxv(a), y = fxv(b); int neg = (x < 0) != (y < 0);
    u64 m = (u64)(x < 0 ? -x : x) * (u64)(y < 0 ? -y : y);
    i64 q = (i64)(m >> 16);
    fxs(r, neg ? -q : q);
}
void pdm_fx_div(struct pd_fx *r, const struct pd_fx *a, const struct pd_fx *b)
{
    i64 x = fxv(a), y = fxv(b); int neg = (x < 0) != (y < 0); u64 q;
    if (y == 0) { g_ovf = 1; fxs(r, x < 0 ? -0x7FFFFFFFLL : 0x7FFFFFFFLL); return; }
    q = ((u64)(x < 0 ? -x : x) << 16) / (u64)(y < 0 ? -y : y);
    if (q > 0x7FFFFFFFULL) { g_ovf = 1; q = 0x7FFFFFFFULL; }
    fxs(r, neg ? -(i64)q : (i64)q);
}
pdw pdm_fx_parse(struct pd_fx *r, const char *s, pdw len)
{
    pdw i = 0; int neg = 0, any = 0; u64 ip = 0, fn = 0, fd = 1; u64 raw;
    if (i < len && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; i++; }
    while (i < len && s[i] >= '0' && s[i] <= '9') { ip = ip * 10 + (u64)(s[i] - '0'); if (ip > 40000) ip = 40000; i++; any = 1; }
    if (i < len && s[i] == '.') {
        i++;
        while (i < len && s[i] >= '0' && s[i] <= '9') {
            if (fd < 100000000ULL) { fn = fn * 10 + (u64)(s[i] - '0'); fd *= 10; }
            i++; any = 1;
        }
    }
    if (!any || i != len) { r->lo = 0; r->hi = 0; return PDE_SYNTAX; }
    raw = ip * 65536 + (fn * 65536 * 2 + fd) / (2 * fd);
    if (raw >= 0x80000000ULL) { fxs(r, neg ? -0x7FFFFFFFLL : 0x7FFFFFFFLL); return PDE_RANGE; }
    fxs(r, neg ? -(i64)raw : (i64)raw);
    return PDE_OK;
}
pdw pdm_fx_todec(char *buf, const struct pd_fx *a)
{
    i64 v = fxv(a); pdw n = 0; u64 m, ip, fr; int k; char d[5];
    if (v < 0) { buf[n++] = '-'; v = -v; }
    m = (u64)v;
    /* round to 4 fraction digits */
    m = (m * 10000 + 32768) >> 16;          /* value x 10000, rounded   */
    ip = m / 10000; fr = m % 10000;
    { struct pd_u32 t; t.lo = (pdw)(ip & 0xFFFF); t.hi = (pdw)(ip >> 16); n += pdm_u32_todec(buf + n, &t, 0); }
    if (fr) {
        for (k = 3; k >= 0; k--) { d[k] = (char)('0' + fr % 10); fr /= 10; }
        k = 4; while (k > 0 && d[k - 1] == '0') k--;
        buf[n++] = '.'; memcpy(buf + n, d, (size_t)k); n += (pdw)k;
    }
    if (n == 2 && buf[0] == '-' && buf[1] == '0') { buf[0] = '0'; n = 1; }
    buf[n] = 0;
    return n;
}
pd_pt pdm_fx_to_pt16(const struct pd_fx *a)
{
    i64 v = fxv(a), q = v >= 0 ? (v + 2048) / 4096 : -((-v + 2048) / 4096);
    if (q > PD_MAXPT)  { g_ovf = 1; q = PD_MAXPT; }
    if (q < -PD_MAXPT) { g_ovf = 1; q = -PD_MAXPT; }
    return (pd_pt)q;
}
void pdm_pt16_to_fx(struct pd_fx *r, pd_pt p) { fxs(r, (i64)p * 4096); }

void pdm_mat_ident(struct pd_mat *m)
{
    memset(m, 0, sizeof *m); pdm_fx_int(&m->a, 1); pdm_fx_int(&m->d, 1);
}
void pdm_mat_mul(struct pd_mat *r, const struct pd_mat *a, const struct pd_mat *b)
{
    struct pd_mat t; struct pd_fx p, q;
    pdm_fx_mul(&p, &a->a, &b->a); pdm_fx_mul(&q, &a->b, &b->c); pdm_fx_add(&t.a, &p, &q);
    pdm_fx_mul(&p, &a->a, &b->b); pdm_fx_mul(&q, &a->b, &b->d); pdm_fx_add(&t.b, &p, &q);
    pdm_fx_mul(&p, &a->c, &b->a); pdm_fx_mul(&q, &a->d, &b->c); pdm_fx_add(&t.c, &p, &q);
    pdm_fx_mul(&p, &a->c, &b->b); pdm_fx_mul(&q, &a->d, &b->d); pdm_fx_add(&t.d, &p, &q);
    pdm_fx_mul(&p, &a->e, &b->a); pdm_fx_mul(&q, &a->f, &b->c); pdm_fx_add(&t.e, &p, &q); pdm_fx_add(&t.e, &t.e, &b->e);
    pdm_fx_mul(&p, &a->e, &b->b); pdm_fx_mul(&q, &a->f, &b->d); pdm_fx_add(&t.f, &p, &q); pdm_fx_add(&t.f, &t.f, &b->f);
    *r = t;
}
void pdm_mat_apply(struct pd_fx *x, struct pd_fx *y, const struct pd_mat *m)
{
    struct pd_fx p, q, nx, ny;
    pdm_fx_mul(&p, x, &m->a); pdm_fx_mul(&q, y, &m->c); pdm_fx_add(&nx, &p, &q); pdm_fx_add(&nx, &nx, &m->e);
    pdm_fx_mul(&p, x, &m->b); pdm_fx_mul(&q, y, &m->d); pdm_fx_add(&ny, &p, &q); pdm_fx_add(&ny, &ny, &m->f);
    *x = nx; *y = ny;
}

/* ---- view (PAGEDESK-SPEC 5.3) ------------------------------------------ */
static i64 fdiv(i64 a, i64 b) { i64 q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }
static i64 cdiv(i64 a, i64 b) { return -fdiv(-a, b); }
static i64 clamp16(i64 v) { if (v > 32767) { g_ovf = 1; return 32767; } if (v < -32767) { g_ovf = 1; return -32767; } return v; }

static void uv_of(i64 *u, i64 *w, const struct pd_view *v, i64 x, i64 y)
{
    const struct pd_rect *b = &v->box;
    switch (v->rot) {
    case 90:  *u = y - b->y0; *w = x - b->x0; break;
    case 180: *u = b->x1 - x; *w = y - b->y0; break;
    case 270: *u = b->y1 - y; *w = b->x1 - x; break;
    default:  *u = x - b->x0; *w = b->y1 - y; break;
    }
}
void pdm_view_fwd(pd_px *px, pd_px *py, const struct pd_view *v, pd_pt x, pd_pt y)
{
    i64 u, w;
    uv_of(&u, &w, v, x, y);
    *px = (pd_px)clamp16(v->ox + fdiv(u * v->zoom, 1600));
    *py = (pd_px)clamp16(v->oy + fdiv(w * v->zoom * v->asp_num, 1600LL * v->asp_den));
}
void pdm_view_inv(pd_pt *x, pd_pt *y, const struct pd_view *v, pd_px px, pd_px py)
{
    const struct pd_rect *b = &v->box;
    i64 u = cdiv((i64)(px - v->ox) * 1600, v->zoom);
    i64 w = cdiv((i64)(py - v->oy) * 1600 * v->asp_den, (i64)v->zoom * v->asp_num);
    i64 X, Y;
    switch (v->rot) {
    case 90:  Y = u + b->y0; X = w + b->x0; break;
    case 180: X = b->x1 - u; Y = w + b->y0; break;
    case 270: Y = b->y1 - u; X = b->x1 - w; break;
    default:  X = u + b->x0; Y = b->y1 - w; break;
    }
    *x = (pd_pt)clamp16(X); *y = (pd_pt)clamp16(Y);
}
void pdm_view_size(pd_px *w, pd_px *h, const struct pd_view *v)
{
    i64 W = (i64)v->box.x1 - v->box.x0, H = (i64)v->box.y1 - v->box.y0, t;
    if (v->rot == 90 || v->rot == 270) { t = W; W = H; H = t; }
    *w = (pd_px)clamp16(fdiv(W * v->zoom, 1600));
    *h = (pd_px)clamp16(fdiv(H * v->zoom * v->asp_num, 1600LL * v->asp_den));
}

/* ---- far memory: the host arena (apps/pagedesk/hosttest/pdhost.c) ------ */
extern unsigned char pd_host_mem[];         /* 1 MiB, seg*16 + off      */
#define PDH(seg, off) (pd_host_mem + (((unsigned long)(seg) << 4) + (off)))
static pdw okspan(pdw off, pdw n) { return n <= 0xFFF0 && (unsigned)off + n <= 0x10000u; }
pdw pd_fcopy_in(pdw seg, pdw off, void *dst, pdw n) { if (!okspan(off, n)) return 0; memcpy(dst, PDH(seg, off), n); return n; }
pdw pd_fcopy_out(const void *src, pdw seg, pdw off, pdw n) { if (!okspan(off, n)) return 0; memcpy(PDH(seg, off), src, n); return n; }
pdw pd_fcopy(pdw dseg, pdw doff, pdw sseg, pdw soff, pdw n) { if (!okspan(doff, n) || !okspan(soff, n)) return 0; memmove(PDH(dseg, doff), PDH(sseg, soff), n); return n; }
pdb pd_fpeekb(pdw seg, pdw off) { return *PDH(seg, off); }
pdw pd_fpeekw(pdw seg, pdw off) { return (pdw)(PDH(seg, off)[0] | (PDH(seg, off)[1] << 8)); }
void pd_fpokeb(pdw seg, pdw off, pdb v) { *PDH(seg, off) = v; }
void pd_fpokew(pdw seg, pdw off, pdw v) { PDH(seg, off)[0] = (unsigned char)v; PDH(seg, off)[1] = (unsigned char)(v >> 8); }
pdw pd_ffill(pdw seg, pdw off, pdb v, pdw n) { if (!okspan(off, n)) return 0; memset(PDH(seg, off), v, n); return n; }

pdw pdm_crc16(pdw crc, const void *p, pdw n)
{
    const unsigned char *b = (const unsigned char *)p; int k;
    while (n--) { crc ^= (pdw)(*b++ << 8); for (k = 0; k < 8; k++) crc = (pdw)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1); }
    return crc;
}
pdw pdm_crc16_far(pdw crc, pdw seg, pdw off, pdw n) { return okspan(off, n) ? pdm_crc16(crc, PDH(seg, off), n) : crc; }
