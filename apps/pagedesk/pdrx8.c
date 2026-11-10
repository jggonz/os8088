/* ============================================================================
 * os8088 - apps/pagedesk/pdrx8.c      PDREND: the 8-bit band (transparency)
 *
 * PAGEDESK-SPEC 20.22.10. #included by pdrend.c after pdrd.c (the sinks it
 * replaces) and before pdri.c. OWNED BY blend.
 *
 * A band a transparent mark touches is composed in strips of at most 8 rows
 * on one grey channel, in the X8 region (6 planes of 8 x band-width bytes and
 * a 1 KiB row buffer), then quantised into the band. The walker below is the
 * band walker of pdrd.c's first version, grown the transparency records;
 * a band with none walks exactly as before.
 * ==========================================================================*/

#define XN        32            /* pixels a chunk composes at a time            */
#define X8_MAXW   768           /* the widest band the region is sized for      */

/* ---- the region and the strip --------------------------------------------- */
static pdw g_x8seg, g_x8kb;     /* PDV_REND_INIT's x8_seg / x8_kb               */
static pdw g_x8on;              /* the region fits this view's band             */
static pdw g_x8pl, g_x8s;       /* plane bytes (8 rows), row stride             */
static pdw g_x8need;            /* KiB the view wants                           */
static pdw g_x8ask;             /* PDE_NEEDMEM answered once this view          */
static pdw g_x8any;             /* a transparent mark in this pass              */
static pdb g_x8map[32];         /* one bit a 16-row band (256 bands)            */

static pdi g_xy0, g_xy1;        /* the strip's rows (absolute)                  */
static pdw g_xd;                /* layer depth 0..2                             */
static pdw g_xfloor;            /* the depth a group end may not pop below      */
static pdw g_xskip;             /* group begins refused (depth): ends to ignore */
static pdb g_xk[3];             /* 1 isolated (premultiplied), 0 opaque colour  */
static pdb g_xko[3];            /* knockout                                     */
static pdb g_xga[3], g_xgb[3], g_xgs[3]; /* the group's alpha, mode, soft mask  */
static pdw g_xa, g_xbm, g_xsm;  /* the walker's transparency                    */
static pdw g_xlum;              /* the walker's colour (STATE aux)              */
static pdw g_xmcur;             /* mask id + 1 held in MK for this strip, 0 none */
static pdw g_xinmask;           /* walking a mask block                         */
static pdw g_xmid, g_xmkind, g_xmbc;
static pdw g_woff;              /* the walker's position in the list            */
static pdw g_mres, g_mend;      /* resume offset, end of the block              */
static pdw g_msv[12];           /* the walker's state while a mask is built     */
static pdw g_nmask;             /* masks defined this pass                      */
static pdw g_mboff[16];         /* ... and where their MASKB records are        */

/* the chunk working set */
static pdb xc_[XN], xa_[XN], xm_[XN], x0_[XN], xs_[XN];
static pdw x_const, x_sv;       /* source: one grey, or xs_[]                   */
static pdb g_lut[256], g_lutk[3];
static pdw g_lutv;

#define PLN(p) ((pdw)((p) * g_x8pl))

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
        z = (pdi)(pdr_muldiv((pdi)(16 * b - 3060), (pdi)b, 255) + 1020);
        d = (pdw)pdr_muldiv(z, (pdi)b, 255);
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
#else
void pdr_x8xlat(pdw seg, pdw off, pdw n, const pdb *lut);
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
    pd_fcopy_in(g_x8seg, (pdw)(PLN(cp) + o), xc_, n);
    if (d) pd_fcopy_in(g_x8seg, (pdw)(PLN(ap) + o), xa_, n);
    if (g_xsm) pd_fcopy_in(g_x8seg, (pdw)(PLN(5) + o), xm_, n);
    if (ko && !iso) pd_fcopy_in(g_x8seg, (pdw)(PLN(d > 1 ? 1 : 0) + o), x0_, n);
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
    pd_fcopy_out(xc_, g_x8seg, (pdw)(PLN(cp) + o), n);
    if (d) pd_fcopy_out(xa_, g_x8seg, (pdw)(PLN(ap) + o), n);
}

/* a constant-colour span of the walker's state, band-relative columns */
static void x8_span(pdi y, pdi xa, pdi xb)
{
    pdw row = (pdw)(y - g_xy0), n = (pdw)(xb - xa + 1), c, x = (pdw)xa, d = g_xd, a = g_xa, o = (pdw)(row * g_x8s + x);
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
    if (iso) pd_ffill(g_x8seg, PLN(2 * d - 1), 0, n);
    else pd_fcopy(g_x8seg, PLN(2 * d - 1), g_x8seg, PLN(d > 1 ? 1 : 0), n);
    pd_ffill(g_x8seg, PLN(2 * d), 0, n);
}

/* the colour of a group's pixel with its alpha divided out */
static pdw x_gcol(pdw iso, pdw c, pdw A, pdw cb)
{
    pdi v;
    if (iso) { if (A >= 255) return c; v = (pdi)((c * 255u) / A); return v > 255 ? 255 : (pdw)v; }
    v = (pdi)(cb + pdr_muldiv((pdi)(c - cb), 255, (pdi)A));
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
            o = (pdw)(r * g_x8s + x);
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
            o = (pdw)(r * g_x8s + x);
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
static void x8_save(void)
{
    g_msv[0] = (pdw)g_scx0; g_msv[1] = (pdw)g_scy0; g_msv[2] = (pdw)g_scx1; g_msv[3] = (pdw)g_scy1;
    g_msv[4] = g_sclid; g_msv[5] = g_sc4; g_msv[6] = g_xlum;
    g_msv[7] = g_xa; g_msv[8] = g_xbm; g_msv[9] = g_xsm; g_msv[10] = g_xfloor;
    g_msv[11] = (pdw)(g_spat[0] | (g_spat[1] << 8));
}
static void x8_rest(void)
{
    g_scx0 = (pdi)g_msv[0]; g_scy0 = (pdi)g_msv[1]; g_scx1 = (pdi)g_msv[2]; g_scy1 = (pdi)g_msv[3];
    g_sclid = g_msv[4]; g_sc4 = (pdb)g_msv[5]; g_xlum = g_msv[6];
    g_xa = g_msv[7]; g_xbm = g_msv[8]; g_xsm = g_msv[9]; g_xfloor = g_msv[10];
}
/* make mask `sm` (id + 1) the one in MK. 1 = it is; 0 = it cannot be (the mark
 * is drawn unmasked); 2 = a build has begun and the walker is now inside its
 * block - the record that asked is read again when the block ends */
static pdw x8_mk(pdw sm)
{
    static pdb mh[DLH + 2];
    pdw id = (pdw)(sm - 1);
    if (sm == g_xmcur) return 1;
    if (g_xinmask || g_xd >= 2 || id >= g_nmask) { inc_note(PDF_INC_LIMIT, "soft mask depth"); return 0; }
    pd_fcopy_in(g_dls, g_mboff[id], mh, DLH + 2);
    g_xmkind = mh[1]; g_xmbc = mh[DLH];
    g_mend = (pdw)(g_mboff[id] + (pdw)r16(mh + 2));
    x8_save();
    g_mres = g_woff; g_xmid = sm;
    g_woff = (pdw)(g_mboff[id] + DLH + 2);
    x8_push(1, 0, 255, 0, 0);
    g_xfloor = g_xd;
    g_xa = 255; g_xbm = 0; g_xsm = 0;
    g_xinmask = 1;
    return 2;
}
static void x8_mkend(void)
{
    while (g_xd > g_xfloor) g_xd--;                    /* groups the block left open */
    x8_mkdone();
    x8_rest();
    g_woff = g_mres; g_xinmask = 0; g_xmcur = g_xmid;
}

/* ---- images ---------------------------------------------------------------- */
/* an image's /SMask or /Mask is read into MK at the image's own resolution for
 * this strip (20.22.10.8); the image stream is on top of PDENG's stack, so the
 * mask's is opened, read and closed in one go and the image goes on after it */
static pdi g_imxl, g_imyt, g_imwq, g_imhq;
static pdw g_imsave, g_imk;                   /* g_xsm to restore; MK holds an image mask */
static struct pd_fx g_x_fx;
static void x8_imgset(const pdb *b, pdw flags, pdi xl, pdi yt, pdi wq, pdi hq, pdi x0, pdi x1, pdi ry0, pdi ry1)
{
    pdw obj, gen, W, H, rb, cur = 0xFFFF, got, r, n, c, i, x, sr, stencil, inv;
    pdi y, qy, xm;
    g_imk = 0; g_imsave = g_xsm;
    g_imxl = xl; g_imyt = yt; g_imwq = wq; g_imhq = hq;
    if (!(flags & 0x60) || !g_xs) return;
    obj = (pdw)r16(b + 20); gen = (pdw)r16(b + 22);
    stencil = (flags & 0x40) ? 1 : 0;
    if (dict_get(obj, gen, "Width", 0, 0) || g_dg.kind != PDK_INT) goto bad;
    W = (pdw)g_dg.num.hi;
    if (dict_get(obj, gen, "Height", 0, 0) || g_dg.kind != PDK_INT) goto bad;
    H = (pdw)g_dg.num.hi;
    inv = 0;
    if (stencil && dict_get(obj, gen, "Decode", 0, 0) == 0 && g_dg.kind == PDK_ARR && dict_num(obj, gen, "Decode", 0, &g_x_fx) == 0 && g_x_fx.hi >= 1) inv = 1;
    if (!stencil) {
        if (dict_get(obj, gen, "BitsPerComponent", 0, 0) || g_dg.kind != PDK_INT || g_dg.num.hi != 8) goto bad;
    }
    rb = stencil ? (pdw)((W + 7) >> 3) : W;
    if (!W || !H || W > 4096 || H > 0x7FFF || rb > 1024) goto bad;
    if (strm_open(obj, gen, 0, 0)) goto bad;
    for (y = ry0; y <= ry1; y++) {
        if (y < g_xy0 || y > g_xy1) continue;
        qy = (pdi)(y * 4 + 2);
        if (qy <= g_imyt) sr = 0;
        else if (qy >= (pdi)(g_imyt + g_imhq)) sr = (pdw)(H - 1);
        else sr = (pdw)pdr_muldiv((pdi)(qy - g_imyt), (pdi)H, g_imhq);
        if (sr >= H) sr = (pdw)(H - 1);
        while (cur != sr) {                              /* forward only: the stream has no seek */
            if (cur != 0xFFFF && cur > sr) goto badclose;
            got = 0;
            while (got < rb) {
                if (pdr_eng(PDV_ENG_STRM_READ, g_x8seg, (pdw)(PLN(6) + got), (pdw)(rb - got)) || !pdr_eng_ax) goto badclose;
                got += pdr_eng_ax;
            }
            cur = cur == 0xFFFF ? 0 : (pdw)(cur + 1);
        }
        r = (pdw)(y - g_xy0);
        for (x = (pdw)(x0 - g_bx0); x <= (pdw)(x1 - g_bx0); x += c) {
            c = (pdw)(x1 - g_bx0 - x + 1); if (c > XN) c = XN;
            n = (pdw)(r * g_x8s + x);
            if (g_imsave) pd_fcopy_in(g_x8seg, (pdw)(PLN(5) + n), xm_, c);
            for (i = 0; i < c; i++) {
                xm = (pdi)pdr_muldiv((pdi)(((pdi)(x + i + g_bx0)) * 4 + 2 - g_imxl), (pdi)W, g_imwq);
                if (xm < 0) xm = 0;
                if (xm >= (pdi)W) xm = (pdi)(W - 1);
                if (stencil) {
                    pdb v = pd_fpeekb(g_x8seg, (pdw)(PLN(6) + (xm >> 3)));
                    v = (pdb)((v >> (7 - (xm & 7))) & 1);
                    xs_[i] = (pdb)((v ^ inv) ? 0 : 255);      /* a set bit is transparent */
                } else xs_[i] = pd_fpeekb(g_x8seg, (pdw)(PLN(6) + xm));
                if (g_imsave) xs_[i] = (pdb)x_mul(xs_[i], xm_[i]);
            }
            pd_fcopy_out(xs_, g_x8seg, (pdw)(PLN(5) + n), c);
        }
    }
    strm_close();
    g_imk = 1; g_xsm = 1; g_xmcur = 0;
    return;
badclose:
    strm_close();
bad:
    inc_note(PDF_INC_TRANSP, "image mask");
}
static void x8_imgend(void)
{
    if (g_imk) { g_xsm = g_imsave; g_imk = 0; g_xmcur = 0; }
}

/* one image row (pdr_imgrow's twin, 20.22.10.8): grey from the source row,
 * through the strip's transparency and soft mask */
static void x8_imgrow(pdi y)
{
    pdw row = (pdw)(y - g_xy0), mode = pdr_ip.smode & 0xFF, flip = (pdr_ip.smode & 0x100) != 0;
    pdw idx, lo = pdr_ip.xlo, hi = pdr_ip.xhi, v, bit, n, c, i, x, run, rs;
    static pdb sb[3];
    x = (pdw)pdr_ip.x0; n = (pdw)(pdr_ip.x1 - pdr_ip.x0 + 1);
    run = 0; rs = 0;
    x_const = 0;
    while (n) {
        c = n > XN ? XN : n;
        for (i = 0; i < c; i++) {
            idx = hi;
            if (idx >= pdr_ip.sw) idx = (pdw)(pdr_ip.sw - 1);
            if (flip) idx = (pdw)(pdr_ip.sw - 1 - idx);
            bit = 0;
            if (mode == 0 || mode == 3) {
                bit = (pd_fpeekb(pdr_ip.sseg, (pdw)(pdr_ip.soff + (idx >> 3))) >> (7 - (idx & 7))) & 1;
                v = bit ? 255 : 0;
            } else if (mode == 1) v = pd_fpeekb(pdr_ip.sseg, (pdw)(pdr_ip.soff + idx));
            else {
                pd_fcopy_in(pdr_ip.sseg, (pdw)(pdr_ip.soff + idx * 3), sb, 3);
                v = (pdw)((77u * sb[0] + 150u * sb[1] + 29u * sb[2]) >> 8);
            }
            if (mode == 3) {                              /* a stencil: runs of the colour */
                if (pdr_ip.inv ? bit : !bit) { if (!run) rs = (pdw)(x + i); run = 1; }
                else if (run) { x8_span(y, (pdi)rs, (pdi)(x + i - 1)); run = 0; }
            } else xs_[i] = (pdb)(pdr_ip.inv ? 255 - v : v);
            lo = (pdw)(lo + pdr_ip.slo);
            hi = (pdw)(hi + pdr_ip.shi + (lo < pdr_ip.slo));
        }
        if (mode != 3) { x_const = 0; x8_chunk(row, x, c); }
        x += c; n -= c;
    }
    if (run) x8_span(y, (pdi)rs, (pdi)(pdr_ip.x1));
}

/* ---- quantising a strip into the band ---------------------------------------- */
static pdw q4(pdw v, pdw par)           /* 20.19.2's seven levels */
{
    if (v < 21) return 0;
    if (v < 64) return par ? 8 : 0;
    if (v < 106) return 8;
    if (v < 149) return par ? 7 : 8;
    if (v < 191) return 7;
    if (v < 234) return par ? 15 : 7;
    return 15;
}
static pdb xo_[XN / 2 + 3];
static void x8_quant(void)
{
    pdi y, xa, xb, x;
    pdw c, j, bo, nb, k, ro;
    for (y = g_xy0; y <= g_xy1; y++) {
        if (y < g_pgy0 || y > g_pgy1) continue;
        xa = g_pgx0 < g_bx0 ? g_bx0 : g_pgx0;
        xb = g_pgx1 > (pdi)(g_bx0 + g_bw - 1) ? (pdi)(g_bx0 + g_bw - 1) : g_pgx1;
        if (xb < xa) continue;
        xa = (pdi)(xa - g_bx0); xb = (pdi)(xb - g_bx0);
        ro = (pdw)((pdw)(y - g_by0) * g_bstride);
        for (x = xa; x <= xb; x = (pdi)(x + c)) {
            c = (pdw)(XN - (x & (XN - 1)));
            if (c > (pdw)(xb - x + 1)) c = (pdw)(xb - x + 1);
            pd_fcopy_in(g_x8seg, (pdw)((pdw)(y - g_xy0) * g_x8s + x), xc_, c);
            if (g_bpp == 4) {
                bo = (pdw)(ro + (x >> 1)); nb = (pdw)(((x + c - 1) >> 1) - (x >> 1) + 1);
                pd_fcopy_in(g_bseg, bo, xo_, nb);
                for (j = 0; j < c; j++) {
                    k = q4(xc_[j], (pdw)((x + j + y - g_pgy0) & 1));
                    if ((x + j) & 1) xo_[((x + j) >> 1) - (x >> 1)] = (pdb)((xo_[((x + j) >> 1) - (x >> 1)] & 0xF0) | k);
                    else             xo_[((x + j) >> 1) - (x >> 1)] = (pdb)((xo_[((x + j) >> 1) - (x >> 1)] & 0x0F) | (k << 4));
                }
            } else {
                bo = (pdw)(ro + (x >> 3)); nb = (pdw)(((x + c - 1) >> 3) - (x >> 3) + 1);
                pd_fcopy_in(g_bseg, bo, xo_, nb);
                for (j = 0; j < c; j++) {
                    k = ((pdw)(xc_[j] * 17u) >> 8) > bayer[((y & 3) << 2) | ((x + j) & 3)];
                    if (k) xo_[((x + j) >> 3) - (x >> 3)] |= (pdb)(0x80 >> ((x + j) & 7));
                    else   xo_[((x + j) >> 3) - (x >> 3)] &= (pdb)~(0x80 >> ((x + j) & 7));
                }
            }
            pd_fcopy_out(xo_, g_bseg, bo, nb);
        }
    }
}

/* gfx's sink (20.22.10.7): grey 0..255, a constant or one a pixel */
#ifndef PD_HAVE_XGSPAN
static void xg_span(pdi y, pdi xa, pdi xb) { (void)y; (void)xa; (void)xb; }
#endif
void pdr_sink_grey(pdi y, pdi xa, pdi xb, const pdb *gv, pdw v)
{
    pdi x0 = xa, x;
    pdw n, c, i, s, ro;
    if (xa < g_scx0) xa = g_scx0;
    if (xb > g_scx1) xb = g_scx1;
    if (xa < g_bx0) xa = g_bx0;
    if (xb > (pdi)(g_bx0 + g_bw - 1)) xb = (pdi)(g_bx0 + g_bw - 1);
    if (xb < xa || y < g_scy0 || y > g_scy1 || y < g_wlo || y > g_whi) return;
    if (gv) gv += xa - x0;
    xa = (pdi)(xa - g_bx0); xb = (pdi)(xb - g_bx0);
    if (g_xs) {
        x_const = gv == 0; x_sv = v;
        n = (pdw)(xb - xa + 1); x = xa;
        while (n) {
            c = n > XN ? XN : n;
            if (gv) { for (i = 0; i < c; i++) xs_[i] = gv[(x - xa) + i]; }
            x8_chunk((pdw)(y - g_xy0), (pdw)x, c);
            x = (pdi)(x + c); n -= c;
        }
        return;
    }
    ro = (pdw)((pdw)(y - g_by0) * g_bstride);
    for (x = xa; x <= xb; x++) {
        s = gv ? gv[x - xa] : v;
        if (g_bpp == 1) pdr_fill1(g_bseg, ro, x, x, ((pdw)(s * 17u) >> 8) > bayer[((y & 3) << 2) | (x & 3)] ? 0xFF : 0);
        else pdr_fill4(g_bseg, ro, x, x, q4(s, (pdw)((x + y - g_pgy0) & 1)));
    }
}

/* ---- the band walker (pdrd.c's first version, with the transparency records) ---- */
static pdw x8_want(void)
{
    pdw b0, b1, b;
    if (!g_x8on || g_thumb) return 0;
    b0 = (pdw)((g_wlo - g_vcy0) >> 4); b1 = (pdw)((g_whi - g_vcy0) >> 4);
    for (b = b0; b <= b1; b++)
        if (b > 255 || (g_x8map[b >> 3] & (1 << (b & 7)))) return 1;
    return 0;
}

static void walk_core(void)
{
    pdw type, len, ne, sm, r;
    pdi y0, y1, a, yy0, yy1, x0, x1;
    static pdb pl[6];
    g_scx0 = g_vcx0; g_scy0 = g_vcy0; g_scx1 = g_vcx1; g_scy1 = g_vcy1;
    g_sclid = 0xFF; g_sc4 = 0; pat_make(g_spat, 0, 1);
    g_xa = 255; g_xbm = 0; g_xsm = 0; g_xlum = 0;
    g_woff = 0;
    for (;;) {
        if (g_xinmask && g_woff >= g_mend) { x8_mkend(); continue; }
        if (g_woff >= g_dllen) break;
        pd_fcopy_in(g_dls, g_woff, g_hdr, DLH);
        type = g_hdr[0]; len = (pdw)r16(g_hdr + 2);
        if (len < DLH || len > g_dllen - g_woff) break;     /* only whole records */
        y0 = r16(g_hdr + 4); y1 = r16(g_hdr + 6);
        if (type == DLR_STATE) {
            pd_fcopy_in(g_dls, g_woff + DLH, g_stl, 14);
            g_scx0 = r16(g_stl); g_scy0 = r16(g_stl + 2);
            g_scx1 = r16(g_stl + 4); g_scy1 = r16(g_stl + 6);
            g_sclid = g_stl[8]; g_sc4 = g_stl[9];
            pdr_cpy(g_spat, g_stl + 10, 4);
            g_xlum = g_hdr[1];
        } else if (type == DLR_XSTATE) {
            if (g_xs) {
                pd_fcopy_in(g_dls, g_woff + DLH, pl, 4);
                g_xa = pl[0]; g_xbm = pl[1]; sm = pl[2];
                r = sm ? x8_mk(sm) : 1;
                if (r == 2) continue;
                g_xsm = r == 1 ? sm : 0;
            }
        } else if (y1 >= g_wlo && y0 <= g_whi) {
            yy0 = y0 < g_wlo ? g_wlo : y0;
            yy1 = y1 > g_whi ? g_whi : y1;
            if (type == DLR_RECT) {
                pd_fcopy_in(g_dls, g_woff + DLH, pl, 4);
                x0 = r16(pl); x1 = r16(pl + 2);
                if (g_sclid < PDR_NCLIP && g_mkok) { for (a = yy0; a <= yy1; a++) shape_clipped(a, 0, x0, x1, 0, 0); }
                else for (a = yy0; a <= yy1; a++) span_out(a, x0, x1);
            } else if (type == DLR_FILL) {
                pd_fcopy_in(g_dls, g_woff + DLH, pl, 6);
                ne = (pdw)r16(pl + 4);
                if (ne > PDR_NEDGE) ne = PDR_NEDGE;
                g_fe = (pdw)(g_woff + DLH + 6);       /* the edges are scanned in the list itself */
                if (g_sclid < PDR_NCLIP && g_mkok) { for (a = yy0; a <= yy1; a++) shape_clipped(a, 1, 0, 0, ne, g_hdr[1]); }
                else rast_edges(g_dls, g_fe, ne, g_hdr[1], yy0, yy1);
            } else if (type == DLR_GLYPHS) {
                draw_glyphs(g_woff, len, yy0, yy1);
            } else if (type == DLR_IMAGE) {
                draw_image(g_woff, len, yy0, yy1);
                if (g_xs) x8_imgend();
            } else if (type == DLR_GROUP && g_xs) {
                if (!(g_hdr[1] & 0x80)) {
                    pd_fcopy_in(g_dls, g_woff + DLH, pl, 4);
                    sm = pl[2];
                    if (sm) { r = x8_mk(sm); if (r == 2) continue; if (!r) sm = 0; }
                    if (g_xd >= 2) { g_xskip++; inc_note(PDF_INC_LIMIT, "group depth"); }
                    else {
                        x8_push((pdw)((g_hdr[1] & 1) || (g_xd && g_xk[g_xd])), (pdw)((g_hdr[1] >> 1) & 1), pl[0], pl[1], sm);
                        g_xa = 255; g_xbm = 0; g_xsm = 0;
                    }
                } else if (g_xskip) g_xskip--;
                else if (g_xd > g_xfloor) {
                    sm = g_xgs[g_xd];
                    if (sm && sm != g_xmcur) {
                        r = x8_mk(sm);
                        if (r == 2) continue;
                        if (!r) g_xgs[g_xd] = 0;
                    }
                    x8_gend();
                    g_xa = 255; g_xbm = 0; g_xsm = 0;
                }
            }
        }
        g_woff += len;
    }
}

#ifdef PD_HOST
#include <stdio.h>
/* the harness reads the strips before they are quantised: PDB_DUMP names a file the
 * canvas' 8-bit rows are written to at exit (hosttest/pdr_test.c); 255 = never drawn 8-bit */
static pdb pdb_img[2048][1024];
static pdw pdb_w, pdb_h;
static pdb pdb_row[2048];
static void pdb_dump_strip(void)
{
    pdi y;
    pdw x;
    for (y = g_xy0; y <= g_xy1; y++) if (y >= 0 && y < 2048) pdb_row[y] = 1;
    for (y = g_xy0; y <= g_xy1; y++)
        for (x = 0; x < g_x8s && (pdw)(g_bx0 + x) < 1024 && y >= 0 && y < 2048; x++) {
            pdb_img[y][g_bx0 + x] = pd_fpeekb(g_x8seg, (pdw)((pdw)(y - g_xy0) * g_x8s + x));
            if ((pdw)(g_bx0 + x + 1) > pdb_w) pdb_w = (pdw)(g_bx0 + x + 1);
            if ((pdw)(y + 1) > pdb_h) pdb_h = (pdw)(y + 1);
        }
}
void pdb_dump_write(const char *path, int w, int h)
{
    FILE *f = fopen(path, "wb");
    int y;
    for (y = 0; y < h; y++) fwrite(pdb_img[y], 1, (size_t)w, f);
    fwrite(pdb_row, 1, (size_t)h, f);                 /* then one flag a row: drawn in 8 bits */
    fclose(f);
}
#endif
static void x8_strips(void)
{
    pdi lo = g_wlo, hi = g_whi, y;
    for (y = lo; y <= hi; y = (pdi)(y + 8)) {
        g_wlo = y; g_whi = (pdi)(y + 7) > hi ? hi : (pdi)(y + 7);
        g_xy0 = g_wlo; g_xy1 = g_whi;
        g_xs = 1; g_xd = 0; g_xfloor = 0; g_xskip = 0; g_xinmask = 0; g_xmcur = 0;
        g_xk[0] = 0; g_xko[0] = 0; g_imk = 0;
        pd_ffill(g_x8seg, 0, 255, (pdw)((g_xy1 - g_xy0 + 1) * g_x8s));
        walk_core();
        g_xd = 0;
#ifdef PD_HOST
        pdb_dump_strip();
#endif
        x8_quant();
        g_xs = 0;
    }
    g_wlo = lo; g_whi = hi;
}

static void walk_band(pdi ry0, pdi ry1)
{
    (void)ry0; (void)ry1;
    if (x8_want()) x8_strips();
    else walk_core();
}

/* ---- the view: the region, its size, and the question to the primary --------- */
static void x8_init(pdw seg, pdw kb)
{
    g_x8seg = seg; g_x8kb = kb > 63 ? 63 : kb;
    if (!g_x8seg) g_x8kb = 0;
    g_x8on = g_x8need && g_x8seg && g_x8kb >= g_x8need;     /* a re-INIT after PDE_NEEDMEM */
}
static void x8_view(void)
{
    pdw need;
    g_x8ask = 0; g_x8on = 0; g_lutv = 0;
    if (g_bw > X8_MAXW) { g_x8need = 0; return; }
    g_x8s = (pdw)g_bw; g_x8pl = (pdw)(8 * g_bw);
    need = (pdw)(6 * g_x8pl + 1024u);
    g_x8need = (pdw)((need + 1023u) >> 10);
    g_x8on = g_x8seg && g_x8kb >= g_x8need;
}
/* the end of interpretation, before any band of the pass is drawn (20.22.10.2) */
static pdw xb_needmem(void)
{
    if (!g_x8any || g_x8on) return 0;
    if (!g_x8need) { inc_note(PDF_INC_APPROX, "band too wide for transparency"); return 0; }
    if (g_x8ask) { inc_note(PDF_INC_APPROX, "no RAM for transparency"); return 0; }
    g_x8ask = 1;
    return g_x8need;
}
