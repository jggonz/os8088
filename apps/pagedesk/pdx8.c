/* ============================================================================
 * os8088 - apps/pagedesk/pdx8.c                        PDX8.MOD (PAGEDESK-SPEC 20.22.10.11)
 *
 * The raster compositor of transparency: a part PDREND's walker far-calls once
 * a span, a row or a record. It lives in the X8 region at the region's base
 * (the part's image + bss at most PDX8_RES bytes), and the strip's planes follow
 * it at seg:PDX8_RES. OWNED BY blend. One translation unit; pdx8.asm is the
 * header, the door and the primitives (the quantiser, xlat, muldiv, the call
 * into PDENG). Compiled twice, as PDWRT is: by cc8086 for the target, and by
 * the host `cc -DPD_HOST` into the PDREND harness, where PDREND's x8_call
 * reaches pdx8_verb() directly.
 *
 * A band a transparent mark touches is composed in strips of at most 8 rows
 * on one grey channel in six planes of 8 x band-width bytes (20.22.10.2) and
 * quantised into the band when the strip ends. Everything about the display
 * list that PDREND decides (clips, glyphs, edges) stays in PDREND: it paints a
 * constant-colour span through PDV_X8_SPAN. What PDX8 reads itself is the
 * transparency records of the list (XSTATE, GROUP, MASKB) at the offset it is
 * given, because a soft mask is built by walking part of the list.
 *
 * C RULES (SPEC.md 73): no &local, no struct copies, frames under 96 bytes.
 * ==========================================================================*/
#ifdef PD_HOST
#include "hosttest/pdr_host.h"
#else
#include "os88.h"
#endif
#include "pdabi.h"
#include "pdmath.h"

#define XN        32            /* pixels a chunk composes at a time            */
#define X8_MAXW   768           /* the widest band the region is sized for      */

/* ---- the engine, the door, the answers ------------------------------------ */
#ifndef DLR_XSTATE
#define DLR_XSTATE  6           /* the display list's transparency records (pdrend.h) */
#define DLR_GROUP   7
#define DLR_MASKB   8
#define DLR_MASKE   9
#endif
struct pdr_xq { pdw sseg, soff, dseg, doff, x0, n, par, bpp; };
extern struct pdr_xq pdr_xq;
void pdr_x8quant(void);
#ifdef PD_HOST
extern pdw pdr_eng(pdw verb, pdw bx, pdw cx, pdw dx);
extern pdw pdr_eng_ax;
extern pdi pdr_muldiv(pdi a, pdi b, pdi c);
#define x8_eng(v, b, c, d)  pdr_eng(v, b, c, d)
#define x8_eng_ax           pdr_eng_ax
#define x8_muldiv           pdr_muldiv
#else
pdw x8_eng(pdw verb, pdw bx, pdw cx, pdw dx);     /* pdx8.asm: 0, or the PDE_ code; AX in x8_eng_ax */
extern pdw x8_eng_ax;
pdi x8_muldiv(pdi a, pdi b, pdi c);               /* pdx8.asm */
void pdr_x8xlat(pdw seg, pdw off, pdw n, const pdb *lut);
#endif

pdw x8_cf, x8_ocx, x8_odx;      /* the door's carry flag and the CX and DX it returns */

static pdw g_xb;                /* the exchange block                           */
static pdw g_x8seg, g_x8kb;     /* the region                                   */
static pdw g_x8pl, g_x8s;       /* plane bytes (8 rows), row stride             */
static pdw g_x8f;               /* X8F_* since the last X8C_END                 */

/* the band (X8C_BAND) */
static pdw g_dls, g_dllen, g_bw, g_bseg, g_bstride, g_bpp;
static pdi g_bx0, g_by0, g_pgx0, g_pgy0, g_pgx1, g_pgy1;

static pdw g_nmask;             /* soft masks defined this pass                 */
static pdw g_mboff[16];         /* where each one's MASKB record is             */
static pdi g_xy0, g_xy1;        /* the strip's rows (absolute)                  */
static pdw g_xd;                /* layer depth 0..2                             */
static pdw g_xfloor;            /* the depth a group end may not pop below      */
static pdw g_xskip;             /* group begins refused (depth): ends to ignore */
static pdb g_xk[3];             /* 1 isolated (premultiplied), 0 opaque colour  */
static pdb g_xko[3];            /* knockout                                     */
static pdb g_xga[3], g_xgb[3], g_xgs[3]; /* the group's alpha, mode, soft mask  */
static pdw g_xa, g_xbm, g_xsm;  /* the walker's transparency                    */
static pdw g_xlum;              /* the walker's colour                          */
static pdw g_xmcur;             /* mask id + 1 held in MK for this strip, 0 none */
static pdw g_xinmask;           /* walking a mask block                         */
static pdw g_xmid, g_xmkind, g_xmbc;
static pdw g_mres, g_mend;      /* resume offset, end of the block              */
static pdw g_msv[5];            /* the walker's state while a mask is built     */

/* the chunk working set */
static pdb xc_[XN], xa_[XN], xm_[XN], x0_[XN], xs_[XN];
static pdw x_const, x_sv;       /* source: one grey, or xs_[]                   */
static pdb g_lut[256], g_lutk[3];
static pdw g_lutv;

#define PLN(p) ((pdw)((p) * g_x8pl))
#define DLH    8
#define r16(p) ((pdi)((p)[0] | ((p)[1] << 8)))

/* ---- 8-bit arithmetic ------------------------------------------------------ */
static pdw x_mul(pdw a, pdw b)          /* round(a * b / 255) */
{
    pdw t = (pdw)(a * b + 128u);
    return (pdw)((t + (t >> 8)) >> 8);
}
static pdw x_lerp(pdw p, pdw q, pdw a)  /* p + a * (q - p) */
{
    return q >= p ? (pdw)(p + x_mul(a, (pdw)(q - p))) : (pdw)(p - x_mul(a, (pdw)(p - q)));
}
static pdw x_isqrt(pdw v)
{
    pdw r = 0, t, bit;
    for (bit = 128; bit; bit >>= 1) { t = r | bit; if (t * t <= v) r = t; }
    return r;
}
static pdw x_hl(pdw b, pdw s)           /* hard light */
{
    pdw u;
    if (s < 128) return x_mul(b, (pdw)(s << 1));
    u = (pdw)((s << 1) - 255);
    return (pdw)(b + u - x_mul(b, u));
}
static pdw x_sl(pdw b, pdw s)           /* soft light */
{
    pdw d, u;
    pdi z;
    if (s < 128) return (pdw)(b - x_mul((pdw)(255 - (s << 1)), x_mul(b, (pdw)(255 - b))));
    u = (pdw)((s << 1) - 255);
    if (b <= 63) {
        z = (pdi)(x8_muldiv((pdi)(16 * b - 3060), (pdi)b, 255) + 1020);
        d = (pdw)x8_muldiv(z, (pdi)b, 255);
    } else d = x_isqrt((pdw)(b * 255u));
    return x_lerp(b, d, u);
}
/* the blend function B(backdrop b, source s) of 20.22.10.6 */
static pdw x_bl(pdw m, pdw b, pdw s)
{
    pdw t;
    switch (m) {
    case 1:  return x_mul(b, s);
    case 2:  return (pdw)(b + s - x_mul(b, s));
    case 3:  return x_hl(s, b);
    case 4:  return b < s ? b : s;
    case 5:  return b > s ? b : s;
    case 6:  if (!b) return 0;
             if (s == 255) return 255;
             t = (pdw)((b * 255u) / (255 - s)); return t > 255 ? 255 : t;
    case 7:  if (b == 255) return 255;
             if (!s) return 0;
             t = (pdw)(((255 - b) * 255u) / s); return t > 255 ? 0 : (pdw)(255 - t);
    case 8:  return x_hl(b, s);
    case 9:  return x_sl(b, s);
    case 10: return b > s ? (pdw)(b - s) : (pdw)(s - b);
    case 11: return (pdw)(b + s - (x_mul(b, s) << 1));
    case 12: case 13: case 14: return b;        /* on grey: the backdrop (20.22.10.1) */
    }
    return s;                                    /* Normal, Luminosity */
}

/* the host twin of the assembly xlat loop: far bytes through a table */
#ifdef PD_HOST
static void pdr_x8xlat(pdw seg, pdw off, pdw n, const pdb *lut)
{
    while (n--) { pd_fpokeb(seg, off, lut[pd_fpeekb(seg, off)]); off++; }
}
#endif

static void x8_lutb(pdw s, pdw a, pdw bm)
{
    pdw d;
    if (g_lutv && g_lutk[0] == s && g_lutk[1] == a && g_lutk[2] == bm) return;
    for (d = 0; d < 256; d++) g_lut[d] = (pdb)x_lerp(d, x_bl(bm, d, s), a);
    g_lutk[0] = (pdb)s; g_lutk[1] = (pdb)a; g_lutk[2] = (pdb)bm; g_lutv = 1;
}

/* ---- the compositor -------------------------------------------------------- */
/* compose n <= XN pixels of the strip row `row`, from band-relative x, onto the
 * current layer: source grey x_sv or xs_[i], alpha g_xa * the soft mask */
static void x8_chunk(pdw row, pdw x, pdw n)
{
    pdw d = g_xd, o = (pdw)(row * g_x8s + x), cp = d ? (pdw)(2 * d - 1) : 0, ap = (pdw)(2 * d);
    pdw i, a, s, c, A, bp, iso = g_xk[d], ko = g_xko[d];
    pd_fcopy_in(g_x8seg, (pdw)(PDX8_RES + PLN(cp) + o), xc_, n);
    if (d) pd_fcopy_in(g_x8seg, (pdw)(PDX8_RES + PLN(ap) + o), xa_, n);
    if (g_xsm) pd_fcopy_in(g_x8seg, (pdw)(PDX8_RES + PLN(5) + o), xm_, n);
    if (ko && !iso) pd_fcopy_in(g_x8seg, (pdw)(PDX8_RES + PLN(d > 1 ? 1 : 0) + o), x0_, n);
    for (i = 0; i < n; i++) {
        a = g_xa;
        if (g_xsm) a = x_mul(a, xm_[i]);
        if (!a && !ko) continue;
        s = x_const ? x_sv : xs_[i];
        c = xc_[i];
        if (!iso) {
            if (ko) c = x0_[i];
            xc_[i] = (pdb)x_lerp(c, x_bl(g_xbm, c, s), a);
            if (d) { A = xa_[i]; xa_[i] = (pdb)(ko ? a : (pdw)(A + a - x_mul(A, a))); }
        } else {
            A = xa_[i];
            if (ko) { c = 0; A = 0; }
            bp = s;
            if (g_xbm && A) {
                bp = A >= 255 ? c : (pdw)((c * 255u) / A);
                if (bp > 255) bp = 255;
                bp = x_lerp(s, x_bl(g_xbm, bp, s), A);
            }
            c = (pdw)(x_mul((pdw)(255 - a), c) + x_mul(a, bp));
            xc_[i] = (pdb)(c > 255 ? 255 : c);
            xa_[i] = (pdb)(A + a - x_mul(A, a));
        }
    }
    pd_fcopy_out(xc_, g_x8seg, (pdw)(PDX8_RES + PLN(cp) + o), n);
    if (d) pd_fcopy_out(xa_, g_x8seg, (pdw)(PDX8_RES + PLN(ap) + o), n);
}

/* a constant-colour span of the walker's state, band-relative columns */
static void x8_span(pdi y, pdi xa, pdi xb)
{
    pdw row = (pdw)(y - g_xy0), n = (pdw)(xb - xa + 1), c, x = (pdw)xa, d = g_xd, a = g_xa, o = (pdw)(PDX8_RES + row * g_x8s + x);
    x_const = 1; x_sv = g_xlum;
    if (!g_xsm && !g_xko[d]) {
        if (!a) return;
        if (a == 255 && !g_xbm) {                       /* opaque Normal: fills */
            if (!d) { pd_ffill(g_x8seg, o, (pdb)g_xlum, n); return; }
            pd_ffill(g_x8seg, (pdw)(PLN(2 * d - 1) + o), (pdb)g_xlum, n);
            pd_ffill(g_x8seg, (pdw)(PLN(2 * d) + o), 255, n);
            return;
        }
        if (!d) { x8_lutb(g_xlum, a, g_xbm); pdr_x8xlat(g_x8seg, o, n, g_lut); return; }
    }
    while (n) { c = n > XN ? XN : n; x8_chunk(row, x, c); x += c; n -= c; }
}

/* ---- layers ---------------------------------------------------------------- */
static void x8_push(pdw iso, pdw ko, pdw ga, pdw gb, pdw gs)
{
    pdw d = (pdw)(g_xd + 1), n = (pdw)((g_xy1 - g_xy0 + 1) * g_x8s);
    g_xd = d; g_xk[d] = (pdb)iso; g_xko[d] = (pdb)ko;
    g_xga[d] = (pdb)ga; g_xgb[d] = (pdb)gb; g_xgs[d] = (pdb)gs;
    if (iso) pd_ffill(g_x8seg, (pdw)(PDX8_RES + PLN(2 * d - 1)), 0, n);
    else pd_fcopy(g_x8seg, (pdw)(PDX8_RES + PLN(2 * d - 1)), g_x8seg, (pdw)(PDX8_RES + PLN(d > 1 ? 1 : 0)), n);
    pd_ffill(g_x8seg, (pdw)(PDX8_RES + PLN(2 * d)), 0, n);
}

/* the colour of a group's pixel with its alpha divided out */
static pdw x_gcol(pdw iso, pdw c, pdw A, pdw cb)
{
    pdi v;
    if (iso) { if (A >= 255) return c; v = (pdi)((c * 255u) / A); return v > 255 ? 255 : (pdw)v; }
    v = (pdi)(cb + x8_muldiv((pdi)(c - cb), 255, (pdi)A));
    return v < 0 ? 0 : v > 255 ? 255 : (pdw)v;
}

/* composite the top layer onto its parent with the group's alpha, mode and
 * mask, and pop it (20.22.10.5). Only pixels with alpha are touched. */
static void x8_gend(void)
{
    pdw d = g_xd, pd = (pdw)(d - 1), iso = g_xk[d], piso = pd ? g_xk[pd] : 0;
    pdw ga = g_xga[d], gb = g_xgb[d], gsm = g_xgs[d];
    pdw rows = (pdw)(g_xy1 - g_xy0 + 1), r, x, c, i, o, A, g, a, cb, cg, v, pa;
    pdw lp = (pdw)(2 * d - 1), la = (pdw)(2 * d), pcp = pd ? (pdw)(2 * pd - 1) : 0, pap = (pdw)(2 * pd);
    for (r = 0; r < rows; r++) {
        for (x = 0; x < g_x8s; x += c) {
            c = (pdw)(g_x8s - x); if (c > XN) c = XN;
            o = (pdw)(PDX8_RES + r * g_x8s + x);
            pd_fcopy_in(g_x8seg, (pdw)(PLN(la) + o), xs_, c);
            for (i = 0; i < c && !xs_[i]; i++) ;
            if (i == c) continue;
            pd_fcopy_in(g_x8seg, (pdw)(PLN(lp) + o), x0_, c);
            pd_fcopy_in(g_x8seg, (pdw)(PLN(pcp) + o), xc_, c);
            if (pd) pd_fcopy_in(g_x8seg, (pdw)(PLN(pap) + o), xa_, c);
            if (gsm) pd_fcopy_in(g_x8seg, (pdw)(PLN(5) + o), xm_, c);
            for (i = 0; i < c; i++) {
                A = xs_[i];
                if (!A) continue;
                g = ga;
                if (gsm) g = x_mul(g, xm_[i]);
                a = x_mul(A, g);
                cb = xc_[i];
                pa = pd ? xa_[i] : 255;
                if (!piso) {                            /* an opaque-colour parent */
                    if (!gb) v = iso ? (pdw)(cb - x_mul(a, cb) + x_mul(g, x0_[i])) : x_lerp(cb, x0_[i], g);
                    else {
                        cg = x_gcol(iso, x0_[i], A, cb);
                        v = x_lerp(cb, x_bl(gb, cb, cg), a);
                    }
                } else {                                /* an isolated parent, an isolated child */
                    if (!gb) v = (pdw)(x_mul((pdw)(255 - a), cb) + x_mul(g, x0_[i]));
                    else {
                        cg = x_gcol(1, x0_[i], A, 0);
                        v = pa >= 255 ? cb : pa ? (pdw)((cb * 255u) / pa) : cg;
                        if (pa) { if (v > 255) v = 255; v = x_lerp(cg, x_bl(gb, v, cg), pa); }
                        v = (pdw)(x_mul((pdw)(255 - a), cb) + x_mul(a, v));
                    }
                }
                xc_[i] = (pdb)(v > 255 ? 255 : v);
                if (pd) xa_[i] = (pdb)(pa + a - x_mul(pa, a));
            }
            pd_fcopy_out(xc_, g_x8seg, (pdw)(PLN(pcp) + o), c);
            if (pd) pd_fcopy_out(xa_, g_x8seg, (pdw)(PLN(pap) + o), c);
        }
    }
    g_xd = pd;
}

/* the top layer becomes the soft mask in MK and is popped: luminosity (the
 * layer over its backdrop colour bc) or alpha */
static void x8_mkdone(void)
{
    pdw d = g_xd, rows = (pdw)(g_xy1 - g_xy0 + 1), r, x, c, i, o, m;
    for (r = 0; r < rows; r++) {
        for (x = 0; x < g_x8s; x += c) {
            c = (pdw)(g_x8s - x); if (c > XN) c = XN;
            o = (pdw)(PDX8_RES + r * g_x8s + x);
            pd_fcopy_in(g_x8seg, (pdw)(PLN(2 * d - 1) + o), xc_, c);
            pd_fcopy_in(g_x8seg, (pdw)(PLN(2 * d) + o), xa_, c);
            for (i = 0; i < c; i++) {
                m = xa_[i];
                if (!g_xmkind) { m = (pdw)(xc_[i] + x_mul((pdw)(255 - m), g_xmbc)); if (m > 255) m = 255; }
                xm_[i] = (pdb)m;
            }
            pd_fcopy_out(xm_, g_x8seg, (pdw)(PLN(5) + o), c);
        }
    }
    g_xd = (pdw)(d - 1);
}

/* ---- the soft mask in force ------------------------------------------------ */
static pdw g_woff;              /* the record being handled                     */

/* make mask `sm` (id + 1) the one in MK. 1 = it is; 0 = it cannot be (the mark
 * is drawn unmasked); 2 = a build has begun and the walker is now inside its
 * block - the record that asked is read again when the block ends */
static pdw x8_mk(pdw sm)
{
    static pdb mh[DLH + 2];
    pdw id = (pdw)(sm - 1);
    if (sm == g_xmcur) return 1;
    if (g_xinmask || g_xd >= 2 || id >= g_nmask) { g_x8f |= X8F_LIMIT; return 0; }
    pd_fcopy_in(g_dls, g_mboff[id], mh, DLH + 2);
    g_xmkind = mh[1]; g_xmbc = mh[DLH];
    g_mend = (pdw)(g_mboff[id] + (pdw)r16(mh + 2));
    g_msv[0] = g_xlum; g_msv[1] = g_xa; g_msv[2] = g_xbm; g_msv[3] = g_xsm; g_msv[4] = g_xfloor;
    g_mres = g_woff; g_xmid = sm;
    g_woff = (pdw)(g_mboff[id] + DLH + 2);
    x8_push(1, 0, 255, 0, 0);
    g_xfloor = g_xd;
    g_xa = 255; g_xbm = 0; g_xsm = 0;
    g_xinmask = 1;
    return 2;
}
static pdw x8_mkend(void)
{
    while (g_xd > g_xfloor) g_xd--;                    /* groups the block left open */
    x8_mkdone();
    g_xlum = g_msv[0]; g_xa = g_msv[1]; g_xbm = g_msv[2]; g_xsm = g_msv[3]; g_xfloor = g_msv[4];
    g_woff = g_mres; g_xinmask = 0; g_xmcur = g_xmid;
    return g_woff;
}

/* ---- the transparency records ------------------------------------------------ */
/* XSTATE or GROUP at DL offset `off`. Answers the offset to go on from; x8_ocx = 1
 * when a mask block was begun (PDREND saves the walker's own state) and x8_odx =
 * the block's end while one is being walked */
static pdw x8_rec(pdw off)
{
    static pdb h[DLH], pl[4];
    pdw len, sm, r;
    pd_fcopy_in(g_dls, off, h, DLH);
    len = (pdw)r16(h + 2);
    g_woff = off; x8_ocx = 0;
    pd_fcopy_in(g_dls, (pdw)(off + DLH), pl, 4);
    if (h[0] == DLR_XSTATE) {
        g_xa = pl[0]; g_xbm = pl[1]; sm = pl[2];
        r = sm ? x8_mk(sm) : 1;
        if (r == 2) { x8_ocx = 1; x8_odx = g_mend; return g_woff; }
        g_xsm = r == 1 ? sm : 0;
    } else if (h[0] == DLR_GROUP) {
        if (!(h[1] & 0x80)) {
            sm = pl[2];
            if (sm) { r = x8_mk(sm); if (r == 2) { x8_ocx = 1; x8_odx = g_mend; return g_woff; } if (!r) sm = 0; }
            if (g_xd >= 2) { g_xskip++; g_x8f |= X8F_LIMIT; }
            else {
                x8_push((pdw)((h[1] & 1) || (g_xd && g_xk[g_xd])), (pdw)((h[1] >> 1) & 1), pl[0], pl[1], sm);
                g_xa = 255; g_xbm = 0; g_xsm = 0;
            }
        } else if (g_xskip) g_xskip--;
        else if (g_xd > g_xfloor) {
            sm = g_xgs[g_xd];
            if (sm && sm != g_xmcur) {
                r = x8_mk(sm);
                if (r == 2) { x8_ocx = 1; x8_odx = g_mend; return g_woff; }
                if (!r) g_xgs[g_xd] = 0;
            }
            x8_gend();
            g_xa = 255; g_xbm = 0; g_xsm = 0;
        }
    }
    x8_odx = g_xinmask ? g_mend : 0;
    return (pdw)(off + len);
}

#include "pdx8i.c"

/* ---- images ---------------------------------------------------------------- */
/* an image's /SMask or /Mask is read into MK at the image's own resolution for
 * this strip (20.22.10.8); the image stream is on top of PDENG's stack, so the
 * mask's is opened, read and closed in one go and the image goes on after it */
static pdi g_imxl, g_imyt, g_imwq, g_imhq;
static pdw g_imsave, g_imk;                   /* g_xsm to restore; MK holds an image mask */
static struct pdxb_strm g_sq;
static struct pdxb_x8img g_si;
static struct pdxb_x8row g_sr;
static void x8_imgset(void)
{
    pdw obj, gen, W, H, rb, cur = 0xFFFF, got, r, n, c, i, x, sr, stencil, inv;
    pdi y, qy, xm, ry0, ry1, x0, x1;
    pd_fcopy_in(g_xb, 0, &g_si, sizeof g_si);
    g_imk = 0; g_imsave = g_xsm;
    g_imxl = g_si.xl; g_imyt = g_si.yt; g_imwq = g_si.wq; g_imhq = g_si.hq;
    ry0 = g_si.ry0; ry1 = g_si.ry1; x0 = g_si.x0; x1 = g_si.x1;
    if (!(g_si.flags & 0x60)) return;
    stencil = (g_si.flags & 0x40) ? 1 : 0;
    gt_g.objnum = g_si.obj; gt_g.gen = g_si.gen;            /* the image's own /SMask or /Mask reference */
    if (gt_get(stencil ? "Mask" : "SMask", 0) || gt_q.kind != PDK_REF) goto bad;
    obj = gt_q.refnum; gen = gt_q.refgen;
    if (x8_dget(obj, gen, "Width", 0) || gt_q.kind != PDK_INT) goto bad;
    W = (pdw)gt_q.num.hi;
    if (x8_dget(obj, gen, "Height", 0) || gt_q.kind != PDK_INT) goto bad;
    H = (pdw)gt_q.num.hi;
    inv = 0;
    if (stencil && x8_dget(obj, gen, "Decode", "[0]") == 0 && gt_q.num.hi >= 1) inv = 1;
    if (!stencil) {
        if (x8_dget(obj, gen, "BitsPerComponent", 0) || gt_q.kind != PDK_INT || gt_q.num.hi != 8) goto bad;
    }
    rb = stencil ? (pdw)((W + 7) >> 3) : W;
    if (!W || !H || W > 4096 || H > 0x7FFF || rb > 1024) goto bad;
    x8_zero(&g_sq, sizeof g_sq);
    g_sq.objnum = obj; g_sq.gen = gen;
    pd_fcopy_out(&g_sq, g_xb, 0, sizeof g_sq);
    if (x8_eng(PDV_ENG_STRM_OPEN, 0, 0, 0)) goto bad;
    for (y = ry0; y <= ry1; y++) {
        if (y < g_xy0 || y > g_xy1) continue;
        qy = (pdi)(y * 4 + 2);
        if (qy <= g_imyt) sr = 0;
        else if (qy >= (pdi)(g_imyt + g_imhq)) sr = (pdw)(H - 1);
        else sr = (pdw)x8_muldiv((pdi)(qy - g_imyt), (pdi)H, g_imhq);
        if (sr >= H) sr = (pdw)(H - 1);
        while (cur != sr) {                              /* forward only: the stream has no seek */
            if (cur != 0xFFFF && cur > sr) goto badclose;
            got = 0;
            while (got < rb) {
                if (x8_eng(PDV_ENG_STRM_READ, g_x8seg, (pdw)(PDX8_RES + PLN(6) + got), (pdw)(rb - got)) || !x8_eng_ax) goto badclose;
                got += x8_eng_ax;
            }
            cur = cur == 0xFFFF ? 0 : (pdw)(cur + 1);
        }
        r = (pdw)(y - g_xy0);
        for (x = (pdw)(x0 - g_bx0); x <= (pdw)(x1 - g_bx0); x += c) {
            c = (pdw)(x1 - g_bx0 - x + 1); if (c > XN) c = XN;
            n = (pdw)(PDX8_RES + r * g_x8s + x);
            if (g_imsave) pd_fcopy_in(g_x8seg, (pdw)(PLN(5) + n), xm_, c);
            for (i = 0; i < c; i++) {
                xm = (pdi)x8_muldiv((pdi)(((pdi)(x + i + g_bx0)) * 4 + 2 - g_imxl), (pdi)W, g_imwq);
                if (xm < 0) xm = 0;
                if (xm >= (pdi)W) xm = (pdi)(W - 1);
                if (stencil) {
                    pdb v = pd_fpeekb(g_x8seg, (pdw)(PDX8_RES + PLN(6) + (xm >> 3)));
                    v = (pdb)((v >> (7 - (xm & 7))) & 1);
                    xs_[i] = (pdb)((v ^ inv) ? 0 : 255);      /* a set bit is transparent */
                } else xs_[i] = pd_fpeekb(g_x8seg, (pdw)(PDX8_RES + PLN(6) + xm));
                if (g_imsave) xs_[i] = (pdb)x_mul(xs_[i], xm_[i]);
            }
            pd_fcopy_out(xs_, g_x8seg, (pdw)(PLN(5) + n), c);
        }
    }
    x8_eng(PDV_ENG_STRM_CLOSE, 0, 0, 0);
    g_imk = 1; g_xsm = 1; g_xmcur = 0;
    return;
badclose:
    x8_eng(PDV_ENG_STRM_CLOSE, 0, 0, 0);
bad:
    g_x8f |= X8F_TRANSP;
}
static void x8_imgend(void)
{
    if (g_imk) { g_xsm = g_imsave; g_imk = 0; g_xmcur = 0; }
}

/* one image row (pdr_imgrow's twin, 20.22.10.8): grey from the source row,
 * through the strip's transparency and soft mask */
static void x8_imgrow(pdi y)
{
    pdw row = (pdw)(y - g_xy0), mode, flip;
    pdw idx, lo, hi, v, bit, n, c, i, x, run, rs;
    static pdb sb[3];
    pd_fcopy_in(g_xb, 0, &g_sr, sizeof g_sr);
    mode = g_sr.smode & 0xFF; flip = (g_sr.smode & 0x100) != 0;
    lo = g_sr.xlo; hi = g_sr.xhi;
    x = (pdw)g_sr.x0; n = (pdw)(g_sr.x1 - g_sr.x0 + 1);
    run = 0; rs = 0;
    x_const = 0;
    while (n) {
        c = n > XN ? XN : n;
        for (i = 0; i < c; i++) {
            idx = hi;
            if (idx >= g_sr.sw) idx = (pdw)(g_sr.sw - 1);
            if (flip) idx = (pdw)(g_sr.sw - 1 - idx);
            bit = 0;
            if (mode == 0 || mode == 3) {
                bit = (pd_fpeekb(g_sr.sseg, (pdw)(g_sr.soff + (idx >> 3))) >> (7 - (idx & 7))) & 1;
                v = bit ? 255 : 0;
            } else if (mode == 1) v = pd_fpeekb(g_sr.sseg, (pdw)(g_sr.soff + idx));
            else {
                pd_fcopy_in(g_sr.sseg, (pdw)(g_sr.soff + idx * 3), sb, 3);
                v = (pdw)((77u * sb[0] + 150u * sb[1] + 29u * sb[2]) >> 8);
            }
            if (mode == 3) {                              /* a stencil: runs of the colour */
                if (g_sr.inv ? bit : !bit) { if (!run) rs = (pdw)(x + i); run = 1; }
                else if (run) { x8_span(y, (pdi)rs, (pdi)(x + i - 1)); run = 0; }
            } else xs_[i] = (pdb)(g_sr.inv ? 255 - v : v);
            lo = (pdw)(lo + g_sr.slo);
            hi = (pdw)(hi + g_sr.shi + (lo < g_sr.slo));
        }
        if (mode != 3) { x_const = 0; x8_chunk(row, x, c); }
        x += c; n -= c;
    }
    if (run) x8_span(y, (pdi)rs, (pdi)(g_sr.x1));
}

/* ---- quantising a strip into the band ---------------------------------------- */
static void x8_quant(void)
{
    pdi y, xa, xb;
    for (y = g_xy0; y <= g_xy1; y++) {
        if (y < g_pgy0 || y > g_pgy1) continue;
        xa = g_pgx0 < g_bx0 ? g_bx0 : g_pgx0;
        xb = g_pgx1 > (pdi)(g_bx0 + g_bw - 1) ? (pdi)(g_bx0 + g_bw - 1) : g_pgx1;
        if (xb < xa) continue;
        xa = (pdi)(xa - g_bx0); xb = (pdi)(xb - g_bx0);
        pdr_xq.sseg = g_x8seg; pdr_xq.soff = (pdw)(PDX8_RES + (pdw)(y - g_xy0) * g_x8s + xa);
        pdr_xq.dseg = g_bseg; pdr_xq.x0 = (pdw)xa; pdr_xq.n = (pdw)(xb - xa + 1); pdr_xq.bpp = g_bpp;
        if (g_bpp == 4) { pdr_xq.doff = (pdw)((pdw)(y - g_by0) * g_bstride + (xa >> 1)); pdr_xq.par = (pdw)((xa + y - g_pgy0) & 1); }
        else            { pdr_xq.doff = (pdw)((pdw)(y - g_by0) * g_bstride + (xa >> 3)); pdr_xq.par = (pdw)((y & 3) << 2); }
        pdr_x8quant();
    }
}

/* ---- the controls ------------------------------------------------------------ */
static void x8_band(void)
{
    static struct pdxb_x8band b;
    pd_fcopy_in(g_xb, 0, &b, sizeof b);
    g_dls = b.dls; g_dllen = b.dllen; g_bx0 = b.bx0; g_by0 = b.by0; g_bw = b.bw;
    g_bseg = b.bseg; g_bstride = b.bstride; g_bpp = b.bpp;
    g_pgx0 = b.pgx0; g_pgy0 = b.pgy0; g_pgx1 = b.pgx1; g_pgy1 = b.pgy1;
    g_x8s = g_bw; g_x8pl = (pdw)(8 * g_bw);
    g_lutv = 0;
}
static void x8_begin(pdi y0, pdi y1)
{
    g_xy0 = y0; g_xy1 = y1;
    g_xd = 0; g_xfloor = 0; g_xskip = 0; g_xinmask = 0; g_xmcur = 0;
    g_xk[0] = 0; g_xko[0] = 0; g_imk = 0;
    g_xa = 255; g_xbm = 0; g_xsm = 0; g_xlum = 0;
    pd_ffill(g_x8seg, PDX8_RES, 255, (pdw)((y1 - y0 + 1) * g_x8s));
}

/* ---- the door's verbs ---------------------------------------------------------- */
pdw pdx8_verb(pdw verb, pdw bx, pdw cx, pdw dx)
{
    x8_cf = 0; x8_ocx = 0; x8_odx = 0;
    switch (verb) {
    case PDV_X8_INIT:
        g_x8seg = bx; g_x8kb = cx; g_xb = dx;
        return PDE_OK;
    case PDV_X8_CTL:
        if (bx == X8C_BAND) { x8_band(); return PDE_OK; }
        if (bx == X8C_BEGIN) { x8_begin((pdi)cx, (pdi)dx); return PDE_OK; }
        if (bx == X8C_END) {
            g_xd = 0;
            x8_quant();
            x8_ocx = g_x8f; g_x8f = 0;
            return PDE_OK;
        }
        if (bx == X8C_LUM) { g_xlum = cx; return PDE_OK; }
        if (bx == X8C_MKEND) { x8_ocx = 1; return x8_mkend(); }
        if (bx == X8C_PASS) { x8d_pass(cx); return PDE_OK; }
        if (bx == X8C_WANT) return x8d_want((pdi)cx, (pdi)dx);
        break;
    case PDV_X8_SPAN: x8_span((pdi)bx, (pdi)cx, (pdi)dx); return PDE_OK;
    case PDV_X8_GS:   return x8d_gs(bx, cx, dx);
    case PDV_X8_DEF:  return x8d_def(bx, cx, dx);
    case PDV_X8_REC:  return x8_rec(bx);
    case PDV_X8_IMG:
        if (bx == X8I_SET) { x8_imgset(); return PDE_OK; }
        if (bx == X8I_ROW) { x8_imgrow((pdi)cx); return PDE_OK; }
        if (bx == X8I_END) { x8_imgend(); return PDE_OK; }
        break;
    }
    x8_cf = 1;
    return PDE_RANGE;
}
