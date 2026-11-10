/* ============================================================================
 * os8088 - apps/pagedesk/pdxgs.c    shading types 1-7 as FLAT POLYGONS (20.22.9)
 *
 * PDV_GFX_SHADE answers records (pdabi.h PDGX_REC_*): a gradient is bands of one colour each, a mesh is triangles
 * cut into flat sub-triangles whose count follows the colour change across them, a patch is 4 x 4 flat quads.
 * Every polygon is already in device quarter-pixels (the caller's CTM is applied here), so PDREND appends each as
 * an ordinary FILL and carries no shading rasteriser. #included by pdxg.c after pdxgc.c. No recursion; all static.
 *
 * A call writes whole UNITS into the caller's buffer and answers PDE_BUSY with `state` = units done when the
 * buffer is short; the caller calls again and the module re-parses, skipping `state` units (cheaply: a skipped
 * unit evaluates no function).
 * ==========================================================================*/
#define SH_MAXU   6000u         /* units in one shading                                      */
#define SH_QMAX   16000         /* PDREND's device clamp (PDR_QMAX)                          */
#define SH_MAXPT  96            /* points in one record                                      */

static pdw sh_cs_row;           /* the shading's colour space (first usable row) */
static pdw sh_ncomp;
static pdw sh_f0, sh_nf;        /* the shading's function(s) in fn_t: first, count */
static pdb sh_cr, sh_cg, sh_cb; /* sh_color's answer */
static pdw sh_bb_ok;
static fx  sh_bb[4];
static fx  sh_m[6];             /* the CTM */
static fx  sh_cov[4];           /* the user-space rectangle that covers the device clip */

/* ---- output: records, units, resume ------------------------------------------- */
static pdw sh_off, sh_cap, sh_full, sh_total, sh_skip, sh_out, sh_np, sh_steps;
static pdb sh_rec[PDGX_REC_HDR + SH_MAXPT * 4];

static void sh_putq(pdi qx, pdi qy)
{
    pdw o;
    if (sh_np >= SH_MAXPT) return;
    o = (pdw)(PDGX_REC_HDR + sh_np * 4);
    sh_rec[o] = (pdb)((pdw)qx & 255); sh_rec[o + 1] = (pdb)((pdw)qx >> 8);
    sh_rec[o + 2] = (pdb)((pdw)qy & 255); sh_rec[o + 3] = (pdb)((pdw)qy >> 8);
    sh_np++;
}
static pdi sh_clampq(pdi v) { return v > SH_QMAX ? (pdi)SH_QMAX : v < -SH_QMAX ? (pdi)-SH_QMAX : v; }
/* user space -> device quarter-pixels as 16.16 (no rounding) */
static void sh_devfx(const fx *x, const fx *y, fx *ox, fx *oy)
{
    static fx p, q;
    fx_mul(&p, x, &sh_m[0]); fx_mul(&q, y, &sh_m[2]); fx_add(ox, &p, &q); fx_add(ox, ox, &sh_m[4]);
    fx_mul(&p, x, &sh_m[1]); fx_mul(&q, y, &sh_m[3]); fx_add(oy, &p, &q); fx_add(oy, oy, &sh_m[5]);
}
static void sh_pt(const fx *x, const fx *y)
{
    static fx dx, dy;
    sh_devfx(x, y, &dx, &dy);
    sh_putq(sh_clampq(pdm_fx_round(&dx)), sh_clampq(pdm_fx_round(&dy)));
}
/* close the record being built: r g b flags, and the first contour's length */
static void sh_pend(pdw r, pdw g, pdw b, pdw fl, pdw n1)
{
    if (sh_np >= 3) {
        pdw n = (pdw)(PDGX_REC_HDR + sh_np * 4);
        sh_rec[0] = (pdb)r; sh_rec[1] = (pdb)g; sh_rec[2] = (pdb)b; sh_rec[3] = (pdb)fl;
        sh_rec[4] = (pdb)sh_np; sh_rec[5] = (pdb)n1;
        pd_fcopy_out(sh_rec, G.rseg, (pdw)(G.roff + sh_off), n);
        sh_off += n; sh_out++;
    }
    sh_np = 0;
}
static void sh_pendc(pdw n1) { sh_pend(sh_cr, sh_cg, sh_cb, 0, n1); }
/* a unit is skipped (answers 1) when an earlier call did it or the buffer already stopped the run */
static int sh_pre(void)
{
    if (sh_full) return 1;
    if (sh_total < sh_skip) { sh_total++; return 1; }
    if (sh_total >= SH_MAXU) { sh_full = 2; return 1; }
    return 0;
}
/* ...and the room for it is checked before anything is computed into it */
static int sh_post(pdw bytes)
{
    if (sh_cap - sh_off < bytes) { sh_full = 1; return 0; }
    sh_total++;
    return 1;
}
static int sh_unit(pdw bytes) { return !sh_pre() && sh_post(bytes); }

/* ---- a colour to r g b -------------------------------------------------------- */
/* comps of the shading's space -> sh_cr/cg/cb */
static void sh_color(const fx *comps)
{
    static fx rgb[3];
    if (cs_conv(sh_cs_row, comps, rgb)) { sh_cr = sh_cg = sh_cb = 255; return; }
    sh_cr = (pdb)fx_to255(&rgb[0]); sh_cg = (pdb)fx_to255(&rgb[1]); sh_cb = (pdb)fx_to255(&rgb[2]);
}

/* the shading function(s) at `in` (nin inputs) -> comps (sh_ncomp of them) */
static fx sh_cv[8];
static pdw sh_fn_at(const fx *in)
{
    static fx o[8];
    pdw i, n;
    if (sh_nf == 1) {
        n = fn_eval(sh_f0, in, sh_cv);
        return n;
    }
    for (i = 0; i < sh_nf && i < 8; i++) {
        n = fn_eval(sh_f0 + i, in, o);
        if (!n) return 0;
        fx_cp(&sh_cv[i], &o[0]);
    }
    return sh_nf;
}
/* the colour at t (the function's input) */
static void sh_color_at(const fx *t)
{
    if (sh_fn_at(t)) sh_color(sh_cv); else { sh_cr = sh_cg = sh_cb = 255; }
}

/* compile "Function": a function or an array of functions; true on success */
static pdw sh_functions(pdw d)
{
    pdw fn = gx_dget(d, "Function"), r, i, k;
    pdw res = gx_res(fn);
    sh_f0 = 0; sh_nf = 0;
    if (res == GN_NIL) return 0;
    if (gx_ty(res) == GN_ARR) {
        k = gx_alen(res);
        if (!k || k > 8) return 0;
        sh_f0 = fn_n;
        for (i = 0; i < k; i++) {
            r = fn_compile(gx_aget(res, i));
            if (r == FNE_NONE) return 0;
            if (i == 0) sh_f0 = r;
        }
        sh_nf = k;
        return 1;
    }
    r = fn_compile(fn);
    if (r == FNE_NONE) return 0;
    sh_f0 = r; sh_nf = 1;
    return 1;
}

/* ---- the header every type shares + the geometry of the call ------------------- */
static pdw sh_header(pdw d)
{
    static fx bb[4];
    pdw n, i, cs;
    cs = gx_dres(d, "ColorSpace");
    if (cs == GN_NIL) return 0;
    if (cs_compile(gx_dget(d, "ColorSpace"))) return 0;
    sh_cs_row = cs_first();
    sh_ncomp = cs_t[sh_cs_row].n;
    if (!sh_ncomp || sh_ncomp > 8) return 0;
    n = gx_nums(gx_dres(d, "BBox"), bb, 4);
    sh_bb_ok = n == 4;
    if (sh_bb_ok) for (i = 0; i < 4; i++) fx_cp(&sh_bb[i], &bb[i]);
    return 1;
}

/* the user-space rectangle that covers the device clip (+ 2 pixels): the inverse of the CTM on its corners.
 * answers 0 when the CTM is singular (nothing is visible) */
static pdw sh_cover(void)
{
    static fx det, t, u, ia, ib, ic, id, ie, if_, px, py, qx, qy, cx[2], cy[2];
    pdw i;
    fx_mul(&det, &sh_m[0], &sh_m[3]); fx_mul(&t, &sh_m[1], &sh_m[2]); fx_sub(&det, &det, &t);
    if (!det.hi && !det.lo) return 0;
    fx_div(&ia, &sh_m[3], &det); fx_div(&id, &sh_m[0], &det);
    fx_div(&ib, &sh_m[1], &det); fx_i(&t, 0); fx_sub(&ib, &t, &ib);
    fx_div(&ic, &sh_m[2], &det); fx_sub(&ic, &t, &ic);
    fx_mul(&t, &sh_m[2], &sh_m[5]); fx_mul(&u, &sh_m[3], &sh_m[4]); fx_sub(&t, &t, &u); fx_div(&ie, &t, &det);
    fx_mul(&t, &sh_m[1], &sh_m[4]); fx_mul(&u, &sh_m[0], &sh_m[5]); fx_sub(&t, &t, &u); fx_div(&if_, &t, &det);
    fx_i(&cx[0], (pdi)(G.clip[0] * 4 - 8)); fx_i(&cx[1], (pdi)((G.clip[2] + 1) * 4 + 8));
    fx_i(&cy[0], (pdi)(G.clip[1] * 4 - 8)); fx_i(&cy[1], (pdi)((G.clip[3] + 1) * 4 + 8));
    for (i = 0; i < 4; i++) {
        /* user = inv(dev): x = ia*X + ic*Y + ie, y = ib*X + id*Y + if */
        fx_mul(&px, &ia, &cx[i & 1]); fx_mul(&t, &ic, &cy[i >> 1]); fx_add(&px, &px, &t); fx_add(&px, &px, &ie);
        fx_mul(&py, &ib, &cx[i & 1]); fx_mul(&t, &id, &cy[i >> 1]); fx_add(&py, &py, &t); fx_add(&py, &py, &if_);
        if (!i) { fx_cp(&sh_cov[0], &px); fx_cp(&sh_cov[2], &px); fx_cp(&sh_cov[1], &py); fx_cp(&sh_cov[3], &py); }
        else {
            if (fx_cmp(&px, &sh_cov[0]) < 0) fx_cp(&sh_cov[0], &px);
            if (fx_cmp(&px, &sh_cov[2]) > 0) fx_cp(&sh_cov[2], &px);
            if (fx_cmp(&py, &sh_cov[1]) < 0) fx_cp(&sh_cov[1], &py);
            if (fx_cmp(&py, &sh_cov[3]) > 0) fx_cp(&sh_cov[3], &py);
        }
    }
    (void)qx; (void)qy;
    return 1;
}

/* ---- type 2: axial bands ---------------------------------------------------------- */
static fx sa_x0, sa_y0, sa_ux, sa_uy, sa_nx, sa_ny, sa_mlo, sa_mhi, sa_t0, sa_dt, sa_len;
/* the point at length l along the axis and m across it */
static void sa_pt(const fx *l, const fx *m)
{
    static fx x, y, t;
    fx_mul(&x, l, &sa_ux); fx_mul(&t, m, &sa_nx); fx_add(&x, &x, &t); fx_add(&x, &x, &sa_x0);
    fx_mul(&y, l, &sa_uy); fx_mul(&t, m, &sa_ny); fx_add(&y, &y, &t); fx_add(&y, &y, &sa_y0);
    sh_pt(&x, &y);
}
/* one band [la, lb] coloured as the axis fraction `sfrac` (16.16, 0..1) */
static void sa_band(const fx *la, const fx *lb, const fx *sfrac)
{
    static fx t;
    fx_mul(&t, &sa_dt, sfrac); fx_add(&t, &sa_t0, &t);
    sh_color_at(&t);
    sa_pt(la, &sa_mlo); sa_pt(lb, &sa_mlo); sa_pt(lb, &sa_mhi); sa_pt(la, &sa_mhi);
    sh_pendc(0);
}
static pdw sh_axial(pdw d)
{
    static fx co[4], dom[2], dx, dy, l2, t, u, slo, shi, lo, hi, la, lb, sf, nn, ex0, one;
    pdw n, i, ext0 = 0, ext1 = 0, ex;
    n = gx_nums(gx_dres(d, "Coords"), co, 4);
    if (n < 4) return 0;
    fx_raw(&dom[0], 0, 0); fx_i(&dom[1], 1);
    gx_nums(gx_dres(d, "Domain"), dom, 2);
    ex = gx_dres(d, "Extend");
    if (gx_ty(ex) == GN_ARR) {
        ext0 = gx_ty(gx_ares(ex, 0)) == GN_BOOL && GN_A(gx_ares(ex, 0));
        ext1 = gx_ty(gx_ares(ex, 1)) == GN_BOOL && GN_A(gx_ares(ex, 1));
    }
    if (!sh_functions(d)) return 0;
    fx_cp(&sa_x0, &co[0]); fx_cp(&sa_y0, &co[1]);
    fx_sub(&dx, &co[2], &co[0]); fx_sub(&dy, &co[3], &co[1]);
    fx_mul(&l2, &dx, &dx); fx_mul(&t, &dy, &dy); fx_add(&l2, &l2, &t);
    if (!l2.hi && !l2.lo) return 1;                      /* both points coincide: nothing is painted */
    fx_sqrt(&sa_len, &l2);
    fx_div(&sa_ux, &dx, &sa_len); fx_div(&sa_uy, &dy, &sa_len);
    fx_i(&t, 0); fx_sub(&sa_nx, &t, &sa_uy); fx_cp(&sa_ny, &sa_ux);
    fx_cp(&sa_t0, &dom[0]); fx_sub(&sa_dt, &dom[1], &dom[0]);
    /* the cover rectangle in the axis frame: l = (p - P0) . u, m = (p - P0) . n */
    for (i = 0; i < 4; i++) {
        fx_sub(&dx, &sh_cov[(i & 1) ? 2 : 0], &sa_x0); fx_sub(&dy, &sh_cov[(i & 2) ? 3 : 1], &sa_y0);
        fx_mul(&t, &dx, &sa_ux); fx_mul(&u, &dy, &sa_uy); fx_add(&lo, &t, &u);
        fx_mul(&t, &dx, &sa_nx); fx_mul(&u, &dy, &sa_ny); fx_add(&hi, &t, &u);
        if (!i) { fx_cp(&slo, &lo); fx_cp(&shi, &lo); fx_cp(&sa_mlo, &hi); fx_cp(&sa_mhi, &hi); continue; }
        if (fx_cmp(&lo, &slo) < 0) fx_cp(&slo, &lo);
        if (fx_cmp(&lo, &shi) > 0) fx_cp(&shi, &lo);
        if (fx_cmp(&hi, &sa_mlo) < 0) fx_cp(&sa_mlo, &hi);
        if (fx_cmp(&hi, &sa_mhi) > 0) fx_cp(&sa_mhi, &hi);
    }
    fx_i(&one, 1); fx_i(&ex0, 0);
    /* before the start */
    if (ext0 && fx_cmp(&slo, &ex0) < 0 && sh_unit(PDGX_REC_HDR + 16)) {
        fx_cp(&lb, &shi); if (fx_cmp(&lb, &ex0) > 0) fx_cp(&lb, &ex0);
        sa_band(&slo, &lb, &ex0);
    }
    /* the gradient itself over its visible part */
    fx_cp(&lo, &slo); if (fx_cmp(&lo, &ex0) < 0) fx_cp(&lo, &ex0);
    fx_cp(&hi, &shi); if (fx_cmp(&hi, &sa_len) > 0) fx_cp(&hi, &sa_len);
    if (fx_cmp(&hi, &lo) > 0) {
        fx_sub(&u, &hi, &lo);
        for (i = 0; i < sh_steps; i++) {
            if (!sh_unit(PDGX_REC_HDR + 16)) { if (sh_full) break; continue; }
            fx_i(&t, (pdi)i); fx_i(&nn, (pdi)sh_steps); fx_div(&sf, &t, &nn);
            fx_mul(&la, &u, &sf); fx_add(&la, &lo, &la);
            if (i + 1 == sh_steps) fx_cp(&lb, &hi);
            else { fx_i(&t, (pdi)(i + 1)); fx_div(&sf, &t, &nn); fx_mul(&lb, &u, &sf); fx_add(&lb, &lo, &lb); }
            /* colour at the middle of the band, as a fraction of the whole axis */
            fx_add(&t, &la, &lb); t.lo = (pdw)((t.lo >> 1) | ((pdw)t.hi << 15)); t.hi >>= 1;
            fx_div(&sf, &t, &sa_len);
            sa_band(&la, &lb, &sf);
        }
    }
    /* beyond the end */
    if (!sh_full && ext1 && fx_cmp(&shi, &sa_len) > 0 && sh_unit(PDGX_REC_HDR + 16)) {
        fx_cp(&la, &slo); if (fx_cmp(&la, &sa_len) < 0) fx_cp(&la, &sa_len);
        sa_band(&la, &shi, &one);
    }
    return 1;
}

/* ---- type 3: radial rings --------------------------------------------------------- */
static fx sr_cs[48], sr_sn[48];             /* the unit circle's M points */
static pdw sr_m;
static fx sr_x0, sr_y0, sr_r0, sr_dx, sr_dy, sr_dr, sr_t0, sr_dt, sr_f;
static fx sr_rc[2];                           /* the cover rectangle's centre */
/* contour of a circle: M points, or in the other winding when `rev` */
static void sr_circle(const fx *cx, const fx *cy, const fx *r, pdw rev)
{
    static fx rr, x, y, t;
    pdw k, j;
    fx_mul(&rr, r, &sr_f);
    for (k = 0; k < sr_m; k++) {
        j = rev ? (pdw)((sr_m - k) % sr_m) : k;
        fx_mul(&x, &rr, &sr_cs[j]); fx_add(&x, cx, &x);
        fx_mul(&y, &rr, &sr_sn[j]); fx_add(&y, cy, &y);
        sh_pt(&x, &y);
    }
    (void)t;
}
/* circle at s: centre and radius */
static fx sr_cx, sr_cy, sr_r;
static void sr_at(const fx *s)
{
    static fx t;
    fx_mul(&t, &sr_dx, s); fx_add(&sr_cx, &sr_x0, &t);
    fx_mul(&t, &sr_dy, s); fx_add(&sr_cy, &sr_y0, &t);
    fx_mul(&t, &sr_dr, s); fx_add(&sr_r, &sr_r0, &t);
    if (sr_r.hi < 0) { sr_r.hi = 0; sr_r.lo = 0; }
}
static fx sr_ax, sr_ay, sr_ar, sr_bx, sr_by, sr_br;       /* the two circles of a ring: A then B */
static void sr_ring(void)                                 /* the larger outside, the smaller a hole */
{
    pdw swap = fx_cmp(&sr_ar, &sr_br) < 0;
    const fx *ox = swap ? &sr_bx : &sr_ax, *oy = swap ? &sr_by : &sr_ay, *orr = swap ? &sr_br : &sr_ar;
    const fx *ix = swap ? &sr_ax : &sr_bx, *iy = swap ? &sr_ay : &sr_by, *ir = swap ? &sr_ar : &sr_br;
    sr_circle(ox, oy, orr, 0);
    if (ir->hi > 0 || ir->lo > 0x0800) {
        pdw n1 = sh_np;
        sr_circle(ix, iy, ir, 1);
        sh_pendc(n1);
    } else sh_pendc(0);
}
static void sr_set(pdw which, const fx *s)               /* circle at s into A (0) or B (1) */
{
    sr_at(s);
    if (which) { fx_cp(&sr_bx, &sr_cx); fx_cp(&sr_by, &sr_cy); fx_cp(&sr_br, &sr_r); }
    else       { fx_cp(&sr_ax, &sr_cx); fx_cp(&sr_ay, &sr_cy); fx_cp(&sr_ar, &sr_r); }
}
/* the radius that reaches past the cover rectangle from centre (cx, cy), with a margin: sr_cap */
static fx sr_cap, sr_hd;
static void sr_capof(const fx *cx, const fx *cy)
{
    static fx dx, dy, t;
    fx_sub(&dx, cx, &sr_rc[0]); fx_sub(&dy, cy, &sr_rc[1]);
    fx_mul(&t, &dx, &dx); fx_mul(&dx, &dy, &dy); fx_add(&t, &t, &dx); fx_sqrt(&t, &t);
    fx_add(&t, &t, &sr_hd);
    fx_raw(&dx, 1, 0x1999); fx_mul(&sr_cap, &t, &dx);                  /* x 1.1 */
}
/* a circle far enough out to cover the rectangle, centred on (cx, cy), into A (0) or B (1) */
static void sr_far(pdw which, const fx *cx, const fx *cy)
{
    sr_capof(cx, cy);
    if (which) { fx_cp(&sr_bx, cx); fx_cp(&sr_by, cy); fx_cp(&sr_br, &sr_cap); }
    else       { fx_cp(&sr_ax, cx); fx_cp(&sr_ay, cy); fx_cp(&sr_ar, &sr_cap); }
}

/* how far past an end of the s range the circles still matter, in s: until the radius runs out (the apex),
 * and no further than the coordinates can go. end 0 looks along -s. */
static void sr_ext(fx *k, const fx *rend, const fx *dr, const fx *cx, const fx *cy, const fx *dist, pdw end)
{
    static fx t, big;
    (void)cx; (void)cy;
    fx_i(k, 24);
    fx_i(&big, 20000);
    if (dist->hi > 0 || dr->hi > 0 || dr->hi < -1) {                      /* the coordinates: 20000 / the per-s travel */
        fx_cp(&t, dist); if (fx_cmp(dr, &t) > 0) fx_cp(&t, dr);
        fx_div(&t, &big, &t); if (fx_cmp(&t, k) < 0) fx_cp(k, &t);
    }
    /* the apex: the radius shrinks to 0 going outward when dr > 0 at end 0, or dr < 0 at end 1 */
    if ((end == 0 && dr->hi > 0) || (end == 1 && dr->hi < 0)) {
        fx_cp(&t, dr); if (t.hi < 0) { fx_i(&big, 0); fx_sub(&t, &big, &t); }
        fx_div(&t, rend, &t); if (fx_cmp(&t, k) < 0) fx_cp(k, &t);
    }
}

static pdw sh_radial(pdw d)
{
    static fx co[6], dom[2], t, u, dist, sf, nn, ex0, one, zero, s0, s1, rmax;
    pdw n, i, ext0 = 0, ext1 = 0, ex, nested, grow, rpx;
    n = gx_nums(gx_dres(d, "Coords"), co, 6);
    if (n < 6) return 0;
    fx_raw(&dom[0], 0, 0); fx_i(&dom[1], 1);
    gx_nums(gx_dres(d, "Domain"), dom, 2);
    ex = gx_dres(d, "Extend");
    if (gx_ty(ex) == GN_ARR) {
        ext0 = gx_ty(gx_ares(ex, 0)) == GN_BOOL && GN_A(gx_ares(ex, 0));
        ext1 = gx_ty(gx_ares(ex, 1)) == GN_BOOL && GN_A(gx_ares(ex, 1));
    }
    if (!sh_functions(d)) return 0;
    fx_cp(&sr_x0, &co[0]); fx_cp(&sr_y0, &co[1]); fx_cp(&sr_r0, &co[2]);
    fx_sub(&sr_dx, &co[3], &co[0]); fx_sub(&sr_dy, &co[4], &co[1]); fx_sub(&sr_dr, &co[5], &co[2]);
    if (sr_r0.hi < 0 || co[5].hi < 0) return 0;
    fx_cp(&sr_t0, &dom[0]); fx_sub(&sr_dt, &dom[1], &dom[0]);
    fx_i(&zero, 0); fx_i(&one, 1);
    fx_mul(&t, &sr_dx, &sr_dx); fx_mul(&u, &sr_dy, &sr_dy); fx_add(&t, &t, &u); fx_sqrt(&dist, &t);
    /* nested circles: one wholly inside the other (the common case, and an exact ring) */
    fx_add(&t, &sr_r0, &dist); nested = fx_cmp(&co[5], &t) >= 0 ? 1 : 0;             /* r1 >= r0 + |c1 - c0| */
    if (!nested) { fx_add(&t, &co[5], &dist); if (fx_cmp(&sr_r0, &t) >= 0) nested = 2; }
    grow = nested != 2;
    if (nested && !dist.hi && !dist.lo && !sr_dr.hi && !sr_dr.lo) return 1;     /* identical circles paint nothing */
    fx_add(&sr_rc[0], &sh_cov[0], &sh_cov[2]); sr_rc[0].lo = (pdw)((sr_rc[0].lo >> 1) | ((pdw)sr_rc[0].hi << 15)); sr_rc[0].hi >>= 1;
    fx_add(&sr_rc[1], &sh_cov[1], &sh_cov[3]); sr_rc[1].lo = (pdw)((sr_rc[1].lo >> 1) | ((pdw)sr_rc[1].hi << 15)); sr_rc[1].hi >>= 1;
    fx_sub(&t, &sh_cov[2], &sh_cov[0]); fx_sub(&u, &sh_cov[3], &sh_cov[1]);
    fx_mul(&t, &t, &t); fx_mul(&u, &u, &u); fx_add(&t, &t, &u); fx_sqrt(&sr_hd, &t);
    sr_hd.lo = (pdw)((sr_hd.lo >> 1) | ((pdw)sr_hd.hi << 15)); sr_hd.hi >>= 1;              /* half the diagonal */
    /* the polygon for a circle: sides by its size on the device, area-true radius */
    fx_cp(&rmax, &sr_r0); if (fx_cmp(&co[5], &rmax) > 0) fx_cp(&rmax, &co[5]);
    {
        static fx k4;
        fx_mul(&t, &sh_m[0], &sh_m[0]); fx_mul(&u, &sh_m[1], &sh_m[1]); fx_add(&t, &t, &u);
        fx_sqrt(&k4, &t);                                   /* qpx per unit */
        fx_mul(&t, &rmax, &k4);
        rpx = (pdw)pdm_fx_round(&t) >> 2;
    }
    if (rpx < 12) { sr_m = 16; fx_raw(&sr_f, 1, 0x0345); }
    else if (rpx < 48) { sr_m = 24; fx_raw(&sr_f, 1, 0x0176); }
    else if (rpx < 160) { sr_m = 32; fx_raw(&sr_f, 1, 0x00D2); }
    else { sr_m = 48; fx_raw(&sr_f, 1, 0x005E); }
    {   /* the unit circle: rotate (1, 0) by 360/M, M times */
        static fx cs, sn, a, b, p, q;
        fx_i(&t, 360); fx_i(&u, (pdi)sr_m); fx_div(&t, &t, &u);
        fx_cos(&cs, &t); fx_sin(&sn, &t);
        fx_i(&a, 1); fx_i(&b, 0);
        for (i = 0; i < sr_m; i++) {
            fx_cp(&sr_cs[i], &a); fx_cp(&sr_sn[i], &b);
            fx_mul(&p, &a, &cs); fx_mul(&q, &b, &sn); fx_sub(&p, &p, &q);
            fx_mul(&q, &a, &sn); fx_mul(&t, &b, &cs); fx_add(&q, &q, &t);
            fx_cp(&a, &p); fx_cp(&b, &q);
        }
    }
    (void)ex0; (void)nn; (void)s0; (void)s1;
    n = (pdw)(PDGX_REC_HDR + sr_m * 8);
    if (nested) {
        /* the s < 0 side: inside circle 0 (growing) or outside it (shrinking), in the colour at 0 */
        if (ext0 && sh_unit(n)) {
            sh_color_at(&sr_t0);
            sr_set(0, &zero);
            if (grow) { sr_circle(&sr_ax, &sr_ay, &sr_ar, 0); sh_pendc(0); }
            else { sr_far(1, &sr_ax, &sr_ay); sr_ring(); }
        }
        for (i = 0; i < sh_steps; i++) {
            if (!sh_unit(n)) { if (sh_full) break; continue; }
            fx_i(&t, (pdi)i); fx_i(&nn, (pdi)sh_steps); fx_div(&sf, &t, &nn); sr_set(0, &sf);
            fx_i(&t, (pdi)(i + 1)); fx_div(&sf, &t, &nn); sr_set(1, &sf);
            fx_i(&t, (pdi)(2 * i + 1)); fx_i(&nn, (pdi)(2 * sh_steps)); fx_div(&sf, &t, &nn);
            fx_mul(&t, &sr_dt, &sf); fx_add(&t, &sr_t0, &t);
            sh_color_at(&t);
            sr_ring();
        }
        if (!sh_full && ext1 && sh_unit(n)) {
            fx_add(&t, &sr_t0, &sr_dt); sh_color_at(&t);
            sr_set(0, &one);
            if (grow) { sr_far(1, &sr_ax, &sr_ay); sr_ring(); }
            else { sr_circle(&sr_ax, &sr_ay, &sr_ar, 0); sh_pendc(0); }
        }
        return 1;
    }
    /* circles that are not nested: discs in increasing s, the later painted over the earlier. The ends are
     * extended (8 discs each, closer together near the end) until the circles leave the rectangle */
    {
        static fx ka, kb, q, e0, e1;
        pdw ne0 = ext0 ? 8 : 0, ne1 = ext1 ? 8 : 0, tot = ne0 + sh_steps + 1 + ne1;
        sr_ext(&ka, &sr_r0, &sr_dr, &sr_x0, &sr_y0, &dist, 0);
        sr_ext(&kb, &co[5], &sr_dr, &co[3], &co[4], &dist, 1);
        for (i = 0; i < tot; i++) {
            if (!sh_unit(n)) { if (sh_full) break; continue; }
            if (i < ne0) { fx_i(&q, (pdi)(ne0 - i)); fx_i(&e0, (pdi)ne0); fx_div(&q, &q, &e0); fx_mul(&q, &q, &q); fx_mul(&sf, &q, &ka); fx_i(&t, 0); fx_sub(&sf, &t, &sf); }
            else if (i <= ne0 + sh_steps) { fx_i(&t, (pdi)(i - ne0)); fx_i(&nn, (pdi)sh_steps); fx_div(&sf, &t, &nn); }
            else { fx_i(&q, (pdi)(i - ne0 - sh_steps)); fx_i(&e1, (pdi)ne1); fx_div(&q, &q, &e1); fx_mul(&q, &q, &q); fx_mul(&sf, &q, &kb); fx_add(&sf, &sf, &one); }
            sr_set(0, &sf);
            sr_capof(&sr_ax, &sr_ay);
            if (fx_cmp(&sr_ar, &sr_cap) > 0) fx_cp(&sr_ar, &sr_cap);
            /* the colour at s clamped to [0, 1] */
            fx_cp(&u, &sf);
            if (u.hi < 0) { u.hi = 0; u.lo = 0; } else if (fx_cmp(&u, &one) > 0) fx_cp(&u, &one);
            fx_mul(&t, &sr_dt, &u); fx_add(&t, &sr_t0, &t);
            sh_color_at(&t);
            sr_circle(&sr_ax, &sr_ay, &sr_ar, 0); sh_pendc(0);
        }
    }
    return 1;
}

/* ---- vertices, quads and triangles ---------------------------------------------- */
struct shv { pdb r, g, b, pad; fx x, y; };
static struct shv sh_v[3];
static void shv_cp(struct shv *d, const struct shv *s) { d->r = s->r; d->g = s->g; d->b = s->b; fx_cp(&d->x, &s->x); fx_cp(&d->y, &s->y); }

/* a flat quad (the mean of its corner colours): grid cells and patch cells */
static void sh_quad(const struct shv *a, const struct shv *b, const struct shv *c, const struct shv *d)
{
    pdw r = (pdw)(a->r + b->r + c->r + d->r), g = (pdw)(a->g + b->g + c->g + d->g), bl = (pdw)(a->b + b->b + c->b + d->b);
    sh_pt(&a->x, &a->y); sh_pt(&b->x, &b->y); sh_pt(&c->x, &c->y); sh_pt(&d->x, &d->y);
    sh_pend(r >> 2, g >> 2, bl >> 2, 0, 0);
}

/* one triangle from sh_v[0..2], cut into k x k flat sub-triangles where the colour changes */
static pdi tl_x[36], tl_y[36];              /* the lattice points, (a, b) at a * (k + 1) + b */
static fx tl_f[6];
static pdw tl_k;
static pdw sh_dabs(pdi a, pdi b) { pdw d = (pdw)(a - b); return (d & 0x8000u) ? (pdw)(0 - d) : d; }
static void tl_sub(pdw a0, pdw b0, pdw a1, pdw b1, pdw a2, pdw b2, pdw nb, pdw nc)
{
    pdw k3 = (pdw)(tl_k * 3), na = (pdw)(k3 - nb - nc);
    sh_putq(tl_x[a0 * (tl_k + 1) + b0], tl_y[a0 * (tl_k + 1) + b0]);
    sh_putq(tl_x[a1 * (tl_k + 1) + b1], tl_y[a1 * (tl_k + 1) + b1]);
    sh_putq(tl_x[a2 * (tl_k + 1) + b2], tl_y[a2 * (tl_k + 1) + b2]);
    sh_pend((pdw)(((pdw)sh_v[0].r * na + (pdw)sh_v[1].r * nb + (pdw)sh_v[2].r * nc) / k3),
            (pdw)(((pdw)sh_v[0].g * na + (pdw)sh_v[1].g * nb + (pdw)sh_v[2].g * nc) / k3),
            (pdw)(((pdw)sh_v[0].b * na + (pdw)sh_v[1].b * nb + (pdw)sh_v[2].b * nc) / k3), 0, 0);
}
static void sh_tri(void)
{
    static fx dx[3], dy[3], t, u;
    static pdi qx[3], qy[3];
    pdw i, a, b, k, span, dmax, ext;
    for (i = 0; i < 3; i++) {
        sh_devfx(&sh_v[i].x, &sh_v[i].y, &dx[i], &dy[i]);
        qx[i] = sh_clampq(pdm_fx_round(&dx[i])); qy[i] = sh_clampq(pdm_fx_round(&dy[i]));
    }
    /* k from the colour change across the triangle (a step of ~28 levels), capped by its size on the device */
    dmax = 0;
    for (i = 0; i < 3; i++) {
        pdw j = (i + 1) % 3, d;
        d = sh_dabs(sh_v[i].r, sh_v[j].r); if (d > dmax) dmax = d;
        d = sh_dabs(sh_v[i].g, sh_v[j].g); if (d > dmax) dmax = d;
        d = sh_dabs(sh_v[i].b, sh_v[j].b); if (d > dmax) dmax = d;
    }
    ext = 0;
    for (i = 1; i < 3; i++) {
        span = sh_dabs(qx[i], qx[0]); if (span > ext) ext = span;
        span = sh_dabs(qy[i], qy[0]); if (span > ext) ext = span;
    }
    k = 1 + dmax / 28;
    if (k > 5) k = 5;
    span = 1 + (ext >> 5);                          /* 8 pixels a step */
    if (k > span) k = span;
    if (!sh_post((pdw)(k * k * (PDGX_REC_HDR + 12)))) return;
    tl_k = k;
    for (a = 0; a <= k; a++) { fx_i(&t, (pdi)a); fx_i(&u, (pdi)k); fx_div(&tl_f[a], &t, &u); }
    for (a = 0; a <= k; a++)
        for (b = 0; a + b <= k; b++) {
            static fx px, py;
            fx_sub(&t, &dx[1], &dx[0]); fx_mul(&t, &t, &tl_f[a]); fx_sub(&u, &dx[2], &dx[0]); fx_mul(&u, &u, &tl_f[b]);
            fx_add(&px, &dx[0], &t); fx_add(&px, &px, &u);
            fx_sub(&t, &dy[1], &dy[0]); fx_mul(&t, &t, &tl_f[a]); fx_sub(&u, &dy[2], &dy[0]); fx_mul(&u, &u, &tl_f[b]);
            fx_add(&py, &dy[0], &t); fx_add(&py, &py, &u);
            tl_x[a * (k + 1) + b] = sh_clampq(pdm_fx_round(&px)); tl_y[a * (k + 1) + b] = sh_clampq(pdm_fx_round(&py));
        }
    for (a = 0; a < k; a++)
        for (b = 0; a + b < k; b++) {
            tl_sub(a, b, a + 1, b, a, b + 1, 3 * a + 1, 3 * b + 1);                        /* up   */
            if (a + b + 1 < k) tl_sub(a + 1, b, a, b + 1, a + 1, b + 1, 3 * a + 2, 3 * b + 2);   /* down */
        }
}

/* ---- type 1: a grid of the domain, one unit a row ------------------------------------ */
#define SH_GRID 8
static fx sf_dom[4], sf_mat[6];
static void sf_row(pdw j, struct shv *row)
{
    static fx in[2], px, py, tmp, fa, fb, tx, ty;
    pdw i;
    fx_i(&fa, SH_GRID);
    fx_i(&tmp, (pdi)j); fx_div(&fb, &tmp, &fa);
    fx_sub(&ty, &sf_dom[3], &sf_dom[2]); fx_mul(&ty, &ty, &fb); fx_add(&in[1], &sf_dom[2], &ty);
    for (i = 0; i <= SH_GRID; i++) {
        fx_i(&tmp, (pdi)i); fx_div(&fb, &tmp, &fa);
        fx_sub(&tx, &sf_dom[1], &sf_dom[0]); fx_mul(&tx, &tx, &fb); fx_add(&in[0], &sf_dom[0], &tx);
        /* the matrix: [a b c d e f] -> x' = a x + c y + e, y' = b x + d y + f */
        fx_mul(&px, &sf_mat[0], &in[0]); fx_mul(&tmp, &sf_mat[2], &in[1]); fx_add(&px, &px, &tmp); fx_add(&px, &px, &sf_mat[4]);
        fx_mul(&py, &sf_mat[1], &in[0]); fx_mul(&tmp, &sf_mat[3], &in[1]); fx_add(&py, &py, &tmp); fx_add(&py, &py, &sf_mat[5]);
        fx_cp(&row[i].x, &px); fx_cp(&row[i].y, &py);
        sh_color_at(in);
        row[i].r = sh_cr; row[i].g = sh_cg; row[i].b = sh_cb;
    }
}
static pdw sh_function_based(pdw d)
{
    pdw i, j;
    struct shv *ra = (struct shv *)gx_ov, *rb = ra + SH_GRID + 1;
    fx_raw(&sf_dom[0], 0, 0); fx_i(&sf_dom[1], 1); fx_raw(&sf_dom[2], 0, 0); fx_i(&sf_dom[3], 1);
    gx_nums(gx_dres(d, "Domain"), sf_dom, 4);
    for (i = 0; i < 6; i++) fx_i(&sf_mat[i], (i == 0 || i == 3) ? 1 : 0);
    gx_nums(gx_dres(d, "Matrix"), sf_mat, 6);
    if (!sh_functions(d)) return 0;
    for (j = 0; j < SH_GRID; j++) {
        if (!sh_unit((pdw)(SH_GRID * (PDGX_REC_HDR + 16)))) { if (sh_full) break; continue; }
        sf_row(j, ra); sf_row(j + 1, rb);
        for (i = 0; i < SH_GRID; i++) sh_quad(&ra[i], &ra[i + 1], &rb[i + 1], &rb[i]);
    }
    return 1;
}

/* ---- the mesh bit reader ------------------------------------------------------- */
static pdb sb_buf[512];
static pdw sb_len, sb_pos, sb_bit, sb_eof;
static int sb_getbit(void)
{
    int b;
    if (sb_pos >= sb_len) {
        sb_len = gx_sread(sb_buf, 512); sb_pos = 0;
        if (!sb_len) { sb_eof = 1; return 0; }
    }
    b = (sb_buf[sb_pos] >> (7 - sb_bit)) & 1;
    if (++sb_bit == 8) { sb_bit = 0; sb_pos++; }
    return b;
}
static void sb_align(void) { if (sb_bit) { sb_bit = 0; sb_pos++; } }
static pdw sb_int(pdw n)            /* up to 16 significant bits; the rest skipped */
{
    pdw v = 0, i, m = n > 16 ? 16 : n;
    for (i = 0; i < n; i++) { int b = sb_getbit(); if (i < m) v = (pdw)((v << 1) | (pdw)b); }
    return v;
}
/* n bits -> 0..1 */
static void sb_norm(pdw n, struct pd_fx *r)
{
    pdw m = n > 16 ? 16 : n, v = sb_int(n);
    static struct pd_u32 q;
    if (m == 16) { r->hi = 0; r->lo = v; return; }
    pdm_u32_mul16(&q, v, 65535u);
    pdm_u32_divmod16(&q, &q, (pdw)((1u << m) - 1u));
    r->hi = 0; r->lo = q.lo;
}

static pdw sm_bpc, sm_bpcomp, sm_bpf, sm_ncv;       /* BitsPerCoordinate/Component/Flag, colour values a vertex */
static struct pd_fx sm_dec[16];
static pdw sm_hasfn;

/* decode raw 0..1 -> dec[2i] + raw * (dec[2i+1] - dec[2i]) */
static void sm_map(pdw i, const struct pd_fx *raw, struct pd_fx *r)
{
    static struct pd_fx t;
    fx_sub(&t, &sm_dec[2 * i + 1], &sm_dec[2 * i]); fx_mul(&t, &t, raw); fx_add(r, &sm_dec[2 * i], &t);
}

struct smv { struct pd_fx x, y; struct pd_fx c[4]; };
static struct smv sm_a, sm_b, sm_c, sm_cur;          /* the triangle machine's last vertices */

/* read a vertex's x, y (flag read by the caller) and colour values */
static void sm_vertex(struct smv *v)
{
    static struct pd_fx raw;
    pdw i;
    sb_norm(sm_bpc, &raw); sm_map(0, &raw, &v->x);
    sb_norm(sm_bpc, &raw); sm_map(1, &raw, &v->y);
    for (i = 0; i < sm_ncv; i++) { sb_norm(sm_bpcomp, &raw); sm_map(2 + i, &raw, &v->c[i]); }
}
/* the colour of a vertex: through the function, or directly */
static void sm_color(const struct smv *v, struct shv *o)
{
    if (sm_hasfn) sh_color_at(v->c); else sh_color(v->c);
    o->r = sh_cr; o->g = sh_cg; o->b = sh_cb;
}
static void sm_emit3(const struct smv *a, const struct smv *b, const struct smv *c)
{
    if (sh_pre()) return;
    fx_cp(&sh_v[0].x, &a->x); fx_cp(&sh_v[0].y, &a->y); sm_color(a, &sh_v[0]);
    fx_cp(&sh_v[1].x, &b->x); fx_cp(&sh_v[1].y, &b->y); sm_color(b, &sh_v[1]);
    fx_cp(&sh_v[2].x, &c->x); fx_cp(&sh_v[2].y, &c->y); sm_color(c, &sh_v[2]);
    sh_tri();
}
static void sm_cp(struct smv *d, const struct smv *s)
{
    pdw i;
    fx_cp(&d->x, &s->x); fx_cp(&d->y, &s->y);
    for (i = 0; i < 4; i++) fx_cp(&d->c[i], &s->c[i]);
}

/* ---- type 4: free-form triangles ------------------------------------------------ */
static pdw sh_freeform(void)
{
    pdw flag, state = 0;
    while (!sb_eof && !sh_full) {
        flag = sb_int(sm_bpf);
        sm_vertex(&sm_cur); sb_align();
        if (sb_eof) break;
        if (state == 0 || flag == 0) {
            /* a new triangle: this vertex and the next two (their flags are ignored) */
            sm_cp(&sm_a, &sm_cur);
            sb_int(sm_bpf); sm_vertex(&sm_b); sb_align();
            sb_int(sm_bpf); sm_vertex(&sm_c); sb_align();
            if (sb_eof) break;
            state = 1;
        } else if (flag == 1) {
            sm_cp(&sm_a, &sm_b); sm_cp(&sm_b, &sm_c); sm_cp(&sm_c, &sm_cur);
        } else {
            sm_cp(&sm_b, &sm_c); sm_cp(&sm_c, &sm_cur);
        }
        sm_emit3(&sm_a, &sm_b, &sm_c);
    }
    return 1;
}

/* ---- type 5: a lattice -------------------------------------------------------------- */
#define sm_prev ((struct smv *)gx_ov)
#define sm_row  ((struct smv *)(gx_ov + 24 * sizeof(struct smv)))
static pdw sh_lattice(pdw d)
{
    pdw vpr = (pdw)gx_int(gx_dres(d, "VerticesPerRow"), 0), i, have = 0;
    if (vpr < 2 || vpr > 24) return 0;
    while (!sb_eof && !sh_full) {
        for (i = 0; i < vpr; i++) { sm_vertex(&sm_row[i]); sb_align(); if (sb_eof) return 1; }
        if (have) {
            for (i = 0; i + 1 < vpr; i++) {
                sm_emit3(&sm_prev[i], &sm_prev[i + 1], &sm_row[i]);
                sm_emit3(&sm_prev[i + 1], &sm_row[i + 1], &sm_row[i]);
            }
        }
        for (i = 0; i < vpr; i++) sm_cp(&sm_prev[i], &sm_row[i]);
        have = 1;
    }
    return 1;
}

/* ---- types 6 and 7: patches ----------------------------------------------------------- */
static struct pd_fx pc_x[16], pc_y[16];       /* a patch's boundary points in stream order (+4 tensor interiors) */
static struct pd_fx pp_x[12], pp_y[12];       /* the previous patch's boundary points */
static struct smv pc_c[4], pp_c[4];           /* corner colours */
static fx pg_x[16], pg_y[16];                 /* the 4x4 grid, [i * 4 + j] */
static const pdb pc_ij[32] = { 0,0, 0,1, 0,2, 0,3, 1,3, 2,3, 3,3, 3,2, 3,1, 3,0, 2,0, 1,0, 1,1, 1,2, 2,2, 2,1 };
/* (i, j) of stream point k is pc_ij[2k], pc_ij[2k+1] */

/* a Coons patch's four interior points from the boundary (ISO 32000-1 8.7.4.5.7): each is
 * 1/9 (-4 a + 6 b + 6 c - 2 d - 2 e + 3 f + 3 g - h) over eight boundary points */
static const pdb pp_ix[4][8] = { {0,1,4,3,12,13,7,15}, {3,2,7,0,15,14,4,12}, {12,13,8,15,0,1,11,3}, {15,14,11,12,3,2,8,0} };
static const pdb pp_tg[4] = { 5, 6, 9, 10 };
static const signed char pp_cf[8] = { -4, 6, 6, -2, -2, 3, 3, -1 };
static void pp_grid_fill(pdw tensor)
{
    static fx s, t, c, nine;
    fx *g;
    pdw k, w, v;
    for (k = 0; k < (tensor ? 16u : 12u); k++) {
        w = pc_ij[2 * k] * 4 + pc_ij[2 * k + 1];
        fx_cp(&pg_x[w], &pc_x[k]); fx_cp(&pg_y[w], &pc_y[k]);
    }
    if (tensor) return;
    fx_i(&nine, 9);
    for (w = 0; w < 4; w++)
        for (v = 0; v < 2; v++) {
            g = v ? pg_y : pg_x;
            fx_raw(&s, 0, 0);
            for (k = 0; k < 8; k++) { fx_i(&c, pp_cf[k]); fx_mul(&t, &c, &g[pp_ix[w][k]]); fx_add(&s, &s, &t); }
            fx_div(&g[pp_tg[w]], &s, &nine);
        }
}

/* the grid evaluated on an n x n lattice, a row of points at a time (the bicubic Bernstein form, separable) */
static fx pt_w[9][4];                         /* Bernstein weights at i / n */
static fx pt_tx[4][9], pt_ty[4][9];           /* the rows of control points evaluated along v */
static struct shv pt_a[9], pt_b[9];           /* two rows of the surface */
static void pt_weights(pdw n)
{
    static fx t, u, a, b;
    pdw i;
    for (i = 0; i <= n; i++) {
        fx_i(&t, (pdi)i); fx_i(&u, (pdi)n); fx_div(&t, &t, &u);
        fx_i(&u, 1); fx_sub(&u, &u, &t);
        fx_mul(&a, &u, &u); fx_mul(&pt_w[i][0], &a, &u);
        fx_mul(&b, &t, &t); fx_mul(&pt_w[i][3], &b, &t);
        fx_mul(&a, &a, &t); fx_add(&pt_w[i][1], &a, &a); fx_add(&pt_w[i][1], &pt_w[i][1], &a);
        fx_mul(&b, &b, &u); fx_add(&pt_w[i][2], &b, &b); fx_add(&pt_w[i][2], &pt_w[i][2], &b);
    }
}
static void pt_dot(fx *r, const fx *w, const fx *p, pdw stride)       /* sum_a w[a] * p[a * stride] */
{
    static fx t;
    pdw a;
    fx_raw(r, 0, 0);
    for (a = 0; a < 4; a++) { fx_mul(&t, &w[a], &p[a * stride]); fx_add(r, r, &t); }
}
/* row i of the surface (u = i / n) into row[0..n]: position and colour */
static void pt_row(pdw i, pdw n, struct shv *row)
{
    static fx u, v, t, m0, m1;
    static struct smv col;
    pdw j, k;
    for (j = 0; j <= n; j++) {
        pt_dot(&row[j].x, pt_w[i], &pt_tx[0][j], 9); pt_dot(&row[j].y, pt_w[i], &pt_ty[0][j], 9);     /* sum_a W[i][a] T[a][j] */
        fx_i(&u, (pdi)i); fx_i(&t, (pdi)n); fx_div(&u, &u, &t);
        fx_i(&v, (pdi)j); fx_div(&v, &v, &t);
        for (k = 0; k < sm_ncv; k++) {
            fx_sub(&t, &pc_c[1].c[k], &pc_c[0].c[k]); fx_mul(&t, &t, &v); fx_add(&m0, &pc_c[0].c[k], &t);
            fx_sub(&t, &pc_c[2].c[k], &pc_c[3].c[k]); fx_mul(&t, &t, &v); fx_add(&m1, &pc_c[3].c[k], &t);
            fx_sub(&t, &m1, &m0); fx_mul(&t, &t, &u); fx_add(&col.c[k], &m0, &t);
        }
        sm_color(&col, &row[j]);
    }
}
static void pt_tessellate(void)
{
    static struct shv ca, cb;
    pdw i, j, a, n, c, dmax;
    if (sh_full) return;
    /* n from the colour change across the patch: 2, 4 or 8 */
    n = 4;
    sm_color(&pc_c[0], &ca);
    dmax = 0;
    for (i = 1; i < 4; i++) {
        sm_color(&pc_c[i], &cb);
        for (c = 0; c < 3; c++) {
            pdw x = c == 0 ? ca.r : c == 1 ? ca.g : ca.b, y = c == 0 ? cb.r : c == 1 ? cb.g : cb.b;
            pdw d = x > y ? x - y : y - x;
            if (d > dmax) dmax = d;
        }
    }
    if (dmax < 24) n = 2; else if (dmax > 96) n = 8;
    if (sh_total + n * n <= sh_skip) { sh_total += n * n; return; }      /* the whole patch was drawn by an earlier call */
    pt_weights(n);
    for (a = 0; a < 4; a++)
        for (j = 0; j <= n; j++) {
            pt_dot(&pt_tx[a][j], pt_w[j], &pg_x[a * 4], 1);
            pt_dot(&pt_ty[a][j], pt_w[j], &pg_y[a * 4], 1);
        }
    pt_row(0, n, pt_a);
    for (i = 0; i < n; i++) {
        pt_row(i + 1, n, pt_b);
        for (j = 0; j < n; j++)
            if (sh_unit(PDGX_REC_HDR + 16)) sh_quad(&pt_a[j], &pt_a[j + 1], &pt_b[j + 1], &pt_b[j]);
        for (j = 0; j <= n; j++) shv_cp(&pt_a[j], &pt_b[j]);
    }
}

static pdw sh_patches(pdw tensor)
{
    static struct pd_fx raw;
    pdw flag, np, nc, k, base, have = 0, i;
    static const pdb edge0[4] = { 0, 3, 6, 9 };      /* flag 0..3: which previous boundary point the new patch starts at */
    while (!sb_eof && !sh_full) {
        flag = sb_int(sm_bpf);
        if (sb_eof) break;
        if (flag > 3) break;
        if (flag == 0 || !have) { np = tensor ? 16 : 12; nc = 4; base = 0; flag = 0; }
        else { np = tensor ? 12 : 8; nc = 2; base = 4;
               for (k = 0; k < 4; k++) {
                   pdw s = (pdw)((edge0[flag] + k) % 12);
                   fx_cp(&pc_x[k], &pp_x[s]); fx_cp(&pc_y[k], &pp_y[s]);
               }
               if (flag == 1) { sm_cp(&pc_c[0], &pp_c[1]); sm_cp(&pc_c[1], &pp_c[2]); }
               else if (flag == 2) { sm_cp(&pc_c[0], &pp_c[2]); sm_cp(&pc_c[1], &pp_c[3]); }
               else { sm_cp(&pc_c[0], &pp_c[3]); sm_cp(&pc_c[1], &pp_c[0]); } }
        for (k = 0; k < np; k++) {
            if (flag != 0 && k >= np) break;
            sb_norm(sm_bpc, &raw); sm_map(0, &raw, &pc_x[(flag ? 4 : 0) + k]);
            sb_norm(sm_bpc, &raw); sm_map(1, &raw, &pc_y[(flag ? 4 : 0) + k]);
        }
        for (k = 0; k < nc; k++) {
            for (i = 0; i < sm_ncv; i++) { sb_norm(sm_bpcomp, &raw); sm_map(2 + i, &raw, &pc_c[(flag ? 2 : 0) + k].c[i]); }
        }
        (void)base;
        sb_align();
        if (sb_eof) break;
        pp_grid_fill(tensor);
        pt_tessellate();
        for (k = 0; k < 12; k++) { fx_cp(&pp_x[k], &pc_x[k]); fx_cp(&pp_y[k], &pc_y[k]); }
        for (k = 0; k < 4; k++) sm_cp(&pp_c[k], &pc_c[k]);
        have = 1;
    }
    return 1;
}

/* ---- the verb ------------------------------------------------------------------------------ */
static pdw v_shade(void)
{
    static fx dec[16];
    pdw d, ty, i, nd, hasfn, r = PDE_OK;
    d = gx_source();
    if (d == GN_NIL || gx_ty(d) != GN_DICT) return PDE_GRAPHICS;
    ty = (pdw)gx_int(gx_dres(d, "ShadingType"), 0);
    sh_total = 0; sh_skip = G.state; sh_off = 0; sh_full = 0; sh_out = 0; sh_np = 0;
    sh_cap = G.rcap;
    sh_steps = G.steps ? G.steps : 32;
    if (sh_steps < 4) sh_steps = 4;
    if (sh_steps > 96) sh_steps = 96;
    for (i = 0; i < 6; i++) fx_cp(&sh_m[i], &((fx *)&G.ctm)[i]);
    if (!sh_header(d)) return PDE_GRAPHICS;
    if (!sh_cover()) { G.rlen = 0; G.ntri = 0; G.ok = 1; G.state = 0; return PDE_OK; }
    if (!G.state && sh_bb_ok) {                                  /* /BBox: a clip polygon first */
        static fx bx, by;
        for (i = 0; i < 4; i++) {
            fx_cp(&bx, &sh_bb[(i == 1 || i == 2) ? 2 : 0]); fx_cp(&by, &sh_bb[(i >= 2) ? 3 : 1]);
            sh_pt(&bx, &by);
        }
        sh_pend(0, 0, 0, PDGX_REC_CLIP, 0);
    }
    if (ty == 2) { if (!sh_axial(d)) return PDE_GRAPHICS; }
    else if (ty == 3) { if (!sh_radial(d)) return PDE_GRAPHICS; }
    else if (ty == 1) { if (!sh_function_based(d)) return PDE_GRAPHICS; }
    else if (ty >= 4 && ty <= 7) {
        if (!G.objnum) return PDE_GRAPHICS;
        hasfn = gx_dget(d, "Function") != GN_NIL;
        if (hasfn && !sh_functions(d)) return PDE_GRAPHICS;
        sm_hasfn = hasfn;
        sm_ncv = hasfn ? 1 : sh_ncomp;
        if (sm_ncv > 4) return PDE_GRAPHICS;
        sm_bpc = (pdw)gx_int(gx_dres(d, "BitsPerCoordinate"), 0);
        sm_bpcomp = (pdw)gx_int(gx_dres(d, "BitsPerComponent"), 0);
        sm_bpf = (pdw)gx_int(gx_dres(d, "BitsPerFlag"), 0);
        if (!sm_bpc || sm_bpc > 32 || !sm_bpcomp || sm_bpcomp > 16 || (ty != 5 && (!sm_bpf || sm_bpf > 8))) return PDE_GRAPHICS;
        nd = gx_nums(gx_dres(d, "Decode"), dec, 16);
        if (nd < 4 + 2 * sm_ncv) return PDE_GRAPHICS;
        for (i = 0; i < nd; i++) fx_cp(&sm_dec[i], &dec[i]);
        sb_len = 0; sb_pos = 0; sb_bit = 0; sb_eof = 0;
        if (gx_sopen(G.objnum, G.gen) != PDE_OK) return PDE_GRAPHICS;
        if (ty == 4) sh_freeform();
        else if (ty == 5) { if (!sh_lattice(d)) { gx_sclose(); return PDE_GRAPHICS; } }
        else sh_patches(ty == 7);
        gx_sclose();
    } else return PDE_GRAPHICS;
    G.rlen = sh_off; G.ntri = sh_out; G.ok = 1; G.kind = sh_bb_ok ? 2 : 0;
    if (sh_full == 1) { G.state = sh_total; r = PDE_BUSY; }
    else G.state = 0;
    if (sh_full == 2) G.kind |= 1;                   /* the unit limit: the core notes PDF_INC_LIMIT */
    return r;
}
