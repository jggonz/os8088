/* ============================================================================
 * os8088 - apps/pagedesk/pdxdash.c   PDV_GFX_DASH: the dash walker, in PDXG
 *
 * PAGEDESK-SPEC 20.12.4 (what a dash is) and 20.22.7 (why it lives here).
 * OWNED BY rcore; #included by gfx's pdxg.c, whose door gives PDV_GFX_DASH a
 * case that calls xd_verb(bx, cx), and by hosttest/pdr_xmod.c on the host.
 * It was PDREND's own (pdrg.c dash_poly) until the any-PDF round needed the
 * room: the walk is the same, step for step, so a dashed line is drawn to
 * the same pixel as before - only now one piece at a time, across calls.
 *
 * The block (struct pdxb_dash, pdabi.h) carries everything between calls:
 * PDREND fills `in` with st = 0 and calls; each call runs the walk until
 * a piece is ready (AX = 1, the piece in `out`), or the subpath is done
 * (AX = 0). The core strokes each piece as an open polyline. A pattern too
 * fine to tell from solid answers 2 and the core strokes the subpath whole.
 * Every local is static and nothing recurses (20.15's stack bound).
 * Needs: pdmath (pd_fx, pd_u32, the far movers), pdabi.h.
 * ==========================================================================*/

static struct pdxb_dash g_xd;
#define XD g_xd

/* integer square root of an unsigned 16-bit value (pdrg.c pdr_isqrt) */
static pdw xd_isqrt(pdw v)
{
    pdw r = 0, b = 0x4000;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (pdw)((r >> 1) + b); }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}
/* hypot(dx, dy) to ~0.5% (pdrg.c pdr_hypot, exactly) */
static pdi xd_hypot(pdi dx, pdi dy)
{
    pdw a = (pdw)(dx < 0 ? -dx : dx), b = (pdw)(dy < 0 ? -dy : dy), sh = 0, r;
    while (a > 180 || b > 180) { a >>= 1; b >>= 1; sh++; }
    r = xd_isqrt((pdw)(a * a + b * b));
    if (r == 0 && (dx || dy)) r = 1;
    if (r > (pdw)(0x7FFF >> sh)) return 0x7FFF;
    return (pdi)(r << sh);
}
/* trunc(a*b/c), saturating at +-0x7FFF: pdrprim.inc's pdr_muldiv, exactly */
static pdi xd_muldiv(pdi a, pdi b, pdi c)
{
    static struct pd_u32 p, q;
    pdw ua = (pdw)(a < 0 ? -a : a), ub = (pdw)(b < 0 ? -b : b), uc = (pdw)(c < 0 ? -c : c), r, neg;
    pdm_u32_mul16(&p, ua, ub);
    neg = (pdw)(((p.hi || p.lo) && ((a < 0) != (b < 0))) != (c < 0));
    if (!uc || p.hi >= uc) r = 0x7FFF;
    else {
        pdm_u32_divmod16(&q, &p, uc);
        r = q.lo > 0x7FFF ? 0x7FFF : q.lo;
    }
    return neg ? (pdi)-(pdi)r : (pdi)r;
}

/* the piece's points are copied out; 1 */
static pdw xd_emit(void)
{
    pdw k;
    XD.out.on = XD.dpn;
    for (k = 0; k < XD.dpn; k++) { XD.out.ox[k] = XD.dpx[k]; XD.out.oy[k] = XD.dpy[k]; }
    return 1;
}
/* add (qx, qy) to the piece; 1 when the full piece was emitted first (the
 * walk goes on from its last point, as pdrg.c's dash_piece_add did) */
static pdw xd_add(void)
{
    pdw e = 0;
    if (XD.dpn >= PDXD_NPT) {
        e = xd_emit();
        XD.dpx[0] = XD.dpx[XD.dpn - 1]; XD.dpy[0] = XD.dpy[XD.dpn - 1]; XD.dpn = 1;
    }
    XD.dpx[XD.dpn] = XD.qx; XD.dpy[XD.dpn] = XD.qy; XD.dpn++;
    return e;
}
/* the piece is over: 1 when there was one to stroke */
static pdw xd_flush(void)
{
    pdw e = 0;
    if (XD.dpn == 2 && XD.dpx[0] == XD.dpx[1] && XD.dpy[0] == XD.dpy[1]) XD.dpn = 1;   /* a zero-length dash is a dot */
    if (XD.dpn) e = xd_emit();
    XD.dpn = 0;
    return e;
}
static void xd_next(void)
{
    XD.don ^= 1; XD.didx++;
    if (XD.didx >= XD.in.dn) XD.didx = 0;
    XD.drem = XD.len[XD.didx];
}
static pdi xd_vx(pdw i) { return (pdi)pd_fpeekw(XD.in.vseg, (pdw)(XD.in.xs + (i << 1))); }
static pdi xd_vy(pdw i) { return (pdi)pd_fpeekw(XD.in.vseg, (pdw)(XD.in.ys + (i << 1))); }

/* the walk's resume points */
#define XS_SEG   1      /* begin segment i                                 */
#define XS_IN    2      /* the inner loop's test                           */
#define XS_IN2   3      /* after the "on" piece's end point was added      */
#define XS_IN3   4      /* after that piece was flushed                    */
#define XS_SEGE  5      /* after the segment's end point was added         */
#define XS_END   6      /* the last flush                                  */
#define XS_DONE  7

static pdw xd_run(void)
{
    static struct pd_fx t1;
    static pdi sum, ph;
    pdw k;
    if (XD.in.st == 0) {
        if (XD.in.n < 1 || XD.in.n > 0x3FFF || XD.in.dn < 1 || XD.in.dn > 4) { XD.in.st = XS_DONE; return 2; }
        sum = 0;
        for (k = 0; k < XD.in.dn; k++) {
            pdm_fx_int(&t1, (pdi)XD.in.dash[k]);
            pdm_fx_mul(&t1, &t1, &XD.in.scale);
            XD.len[k] = (pdi)(t1.hi >> 2);                   /* quarter points -> qpx */
            sum = (pdi)(sum + XD.len[k]);
        }
        if (sum < 6) { XD.in.st = XS_DONE; return 2; }       /* too fine to tell from solid */
        XD.didx = 0; XD.don = 1; XD.drem = XD.len[0]; XD.dpn = 0; XD.dpc = 0;
        pdm_fx_int(&t1, (pdi)XD.in.dph);                     /* the phase moves the start along the pattern */
        pdm_fx_mul(&t1, &t1, &XD.in.scale);
        ph = (pdi)(t1.hi >> 2);
        for (k = 0; ph > 0 && k < 64; k++) {
            if (ph >= XD.drem) { ph = (pdi)(ph - XD.drem); xd_next(); }
            else { XD.drem = (pdi)(XD.drem - ph); ph = 0; }
        }
        if (XD.don) { XD.qx = xd_vx(0); XD.qy = xd_vy(0); xd_add(); }   /* an empty piece: never emits */
        XD.i = 0; XD.in.st = XS_SEG;
    }
    for (;;) {
        switch (XD.in.st) {
        case XS_SEG:
            if (XD.i >= XD.in.n || XD.dpc >= 3000) { XD.in.st = XS_END; break; }
            if (XD.i + 1 == XD.in.n) {
                if (!XD.in.closed) { XD.in.st = XS_END; break; }
                XD.x1 = xd_vx(0); XD.y1 = xd_vy(0);
            } else { XD.x1 = xd_vx(XD.i + 1); XD.y1 = xd_vy(XD.i + 1); }
            XD.x0 = xd_vx(XD.i); XD.y0 = xd_vy(XD.i);
            XD.slen = xd_hypot((pdi)(XD.x1 - XD.x0), (pdi)(XD.y1 - XD.y0));
            XD.pos = 0;
            XD.in.st = XS_IN;
            break;
        case XS_IN:
            if ((pdi)(XD.slen - XD.pos) > XD.drem && XD.dpc++ < 3000) {
                XD.pos = (pdi)(XD.pos + XD.drem);
                XD.qx = (pdi)(XD.x0 + xd_muldiv((pdi)(XD.x1 - XD.x0), XD.pos, XD.slen));
                XD.qy = (pdi)(XD.y0 + xd_muldiv((pdi)(XD.y1 - XD.y0), XD.pos, XD.slen));
                XD.in.st = XS_IN2;
                if (XD.don && xd_add()) return 1;
                break;
            }
            XD.drem = (pdi)(XD.drem - (pdi)(XD.slen - XD.pos));
            XD.in.st = XS_SEGE;
            if (XD.don) { XD.qx = XD.x1; XD.qy = XD.y1; if (xd_add()) return 1; }
            break;
        case XS_IN2:
            XD.in.st = XS_IN3;
            if (XD.don && xd_flush()) return 1;
            break;
        case XS_IN3:
            xd_next();
            XD.in.st = XS_IN;
            if (XD.don && xd_add()) return 1;
            break;
        case XS_SEGE:
            XD.i++;
            XD.in.st = XS_SEG;
            break;
        case XS_END:
            XD.in.st = XS_DONE;
            if (XD.don && xd_flush()) return 1;
            break;
        default:
            return 0;
        }
    }
}

/* PDV_GFX_DASH: BX:CX = the block. AX as the header says. */
static pdw xd_verb(pdw bx, pdw cx)
{
    pdw r;
    pd_fcopy_in(bx, cx, &g_xd, sizeof g_xd);
    r = xd_run();
    pd_fcopy_out(&g_xd, bx, cx, sizeof g_xd);
    return r;
}
