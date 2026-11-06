/* ============================================================================
 * os8088 - apps/pagedesk/pdrg.c      PDREND: state, geometry, paths, strokes
 *
 * #included by pdrend.c (one translation unit). PAGEDESK-SPEC 20.3.
 *
 * Device space is the 1/4-PIXEL grid ("qpx"): a coordinate is a pdi in
 * +/-PDR_QMAX, pixel p covers [4p, 4p+4) and its sample point is 4p+2.
 * ==========================================================================*/

/* ---- the graphics state ------------------------------------------------- */
struct pdr_gs {
    struct pd_mat ctm;              /* user space -> device qpx              */
    struct pd_fx lw;                /* line width, user units                */
    struct pd_fx miter;
    struct pd_fx dash[4];           /* user units                            */
    struct pd_fx dphase;
    struct pd_fx tc, tw, tz, tl, rise, fs;  /* text state                    */
    pdi  cx0, cy0, cx1, cy1;        /* clip rectangle, pixels, inclusive     */
    pdb  cap, join, ndash;
    pdb  flum, fc4, slum, sc4;      /* fill / stroke: luminance, VGA index   */
    pdb  fncs, sncs;                /* colour components of the space (1,3,4)*/
    pdb  tr, font;                  /* render mode, font cache index (255=none)*/
    pdb  ncl;                       /* non-rectangular clips active          */
    pdb  pad;
};
static struct pdr_gs g_gs[PD_MAXGSTACK + 1];
static pdw g_gsn;                   /* index of the current state            */
static struct pdr_gs *gsp;          /* ...and its address: sizeof is no power
                                     * of two, so a g_gs[g_gsn] costs 20 bytes
                                     * of shift-and-add at every use          */
#define GS (*gsp)
static void gs_set(pdw n) { g_gsn = n; gsp = &g_gs[n]; }

static pdw g_mode;                  /* PDR_*                                 */
static pdw g_fillrule, g_deskwhite;
static pdw g_gseg, g_gkb;           /* the glyph-cache claim (INIT)           */
/* the PDRES bank, attached by PDV_REND_FONTBANK / INIT (pdrd.c, pdrf.c) */
static pdw g_bankseg, g_bankok;     /* bank 0 (PDRES core): widths and names live there */
static pdw g_bk_seg[2], g_bk_n[2];  /* the PDFB banks: 0 PDRES core, 1 PDFONTX.RES (D3, D5) */
static pdw g_pkfam = 0xFF, g_pksty, g_pkem, g_pkres, g_pkscale;
static pdw g_inc;                   /* PDF_INC_* seen                        */
static char g_incname[32];
static pdw g_ops, g_passes;

static void pdr_cpy(void *d, const void *s, pdw n)
{
    pdb *a = (pdb *)d; const pdb *b = (const pdb *)s;
    while (n--) *a++ = *b++;
}
static void pdr_zero(void *d, pdw n)
{
    pdb *a = (pdb *)d;
    while (n--) *a++ = 0;
}
static pdw pdr_memeq(const pdb *a, const pdb *b, pdw n)
{
    while (n--) if (*a++ != *b++) return 0;
    return 1;
}
static pdi pdr_abs(pdi v) { return v < 0 ? (pdi)-v : v; }
static pdi pdr_min(pdi a, pdi b) { return a < b ? a : b; }
static pdi pdr_max(pdi a, pdi b) { return a > b ? a : b; }
static pdi pdr_clampq(pdi v)
{
    if (v > PDR_QMAX) return PDR_QMAX;
    if (v < -PDR_QMAX) return -PDR_QMAX;
    return v;
}

/* record an incomplete-rendering feature (10.6): the bit and, once, a name */
static void inc_note(pdw bit, const char *name)
{
    pdw i;
    if (!g_inc) {
        for (i = 0; i < 31 && name[i]; i++) g_incname[i] = name[i];
        g_incname[i] = 0;
    }
    g_inc |= bit;
}

/* ---- numbers ------------------------------------------------------------ */
/* integer square root of an unsigned 16-bit value */
static pdw pdr_isqrt(pdw v)
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
/* hypot(dx, dy) to ~0.5% for |dx|,|dy| <= 32766 */
static pdi pdr_hypot(pdi dx, pdi dy)
{
    pdw a = (pdw)pdr_abs(dx), b = (pdw)pdr_abs(dy), sh = 0, r;
    while (a > 180 || b > 180) { a >>= 1; b >>= 1; sh++; }
    r = pdr_isqrt((pdw)(a * a + b * b));
    if (r == 0 && (dx || dy)) r = 1;
    return (pdi)(r << sh);
}

/* fx -> 0..255 (the value is a PDF colour component in 0..1) */
static pdw fx_u8(const struct pd_fx *a)
{
    if (a->hi < 0) return 0;
    if (a->hi >= 1) return 255;
    return (pdw)(a->lo >> 8);
}

/* VGA palette index (PAGEDESK-SPEC 10.5) from RGB 0..255 */
static pdb rgb_c4(pdw r, pdw g, pdw b)
{
    pdw mx = r, mn = r, lum, bright, idx;
    if (g > mx) mx = g; if (b > mx) mx = b;
    if (g < mn) mn = g; if (b < mn) mn = b;
    lum = (pdw)((r * 77u + g * 150u + b * 29u) >> 8);
    if (mx - mn < 40) {             /* a grey: the four the palette has      */
        if (lum < 48) return 0;     /* black                                 */
        if (lum < 128) return 8;    /* dark grey                             */
        if (lum < 208) return 7;    /* light grey                            */
        return 15;                  /* white                                 */
    }
    bright = mx >= 192;
    idx = 0;
    if (r * 2 >= mx) idx |= 4;
    if (g * 2 >= mx) idx |= 2;
    if (b * 2 >= mx) idx |= 1;
    if (bright) idx |= 8;
    return (pdb)idx;
}

/* ---- the base matrix: page user space (pt) -> device qpx --------------- */
static struct pd_mat g_base;
static struct pd_fx t0, t1, t2, t3, t4;

/* build g_base from a box (pt16), rotation and per-axis scale in qpx per pt */
static void base_build(const struct pd_rect *bx, pdw rot, const struct pd_fx *sx,
                       const struct pd_fx *sy, pdi ox, pdi oy)
{
    struct pd_fx *px0 = &t0, *py0 = &t1, *px1 = &t2, *py1 = &t3;
    pdm_pt16_to_fx(px0, bx->x0); pdm_pt16_to_fx(py0, bx->y0);
    pdm_pt16_to_fx(px1, bx->x1); pdm_pt16_to_fx(py1, bx->y1);
    pdr_zero(&g_base, sizeof g_base);
    /* e/f start as the 4*origin; then +/- scale * box edge */
    pdm_fx_int(&g_base.e, (pdi)(ox * 4));
    pdm_fx_int(&g_base.f, (pdi)(oy * 4));
    switch (rot) {
    case 90:
        pdr_cpy(&g_base.b, sy, 4); pdr_cpy(&g_base.c, sx, 4);
        pdm_fx_mul(&t4, sx, py0); pdm_fx_sub(&g_base.e, &g_base.e, &t4);
        pdm_fx_mul(&t4, sy, px0); pdm_fx_sub(&g_base.f, &g_base.f, &t4);
        break;
    case 180:
        pdm_fx_neg(&g_base.a, sx); pdr_cpy(&g_base.d, sy, 4);
        pdm_fx_mul(&t4, sx, px1); pdm_fx_add(&g_base.e, &g_base.e, &t4);
        pdm_fx_mul(&t4, sy, py0); pdm_fx_sub(&g_base.f, &g_base.f, &t4);
        break;
    case 270:
        pdm_fx_neg(&g_base.b, sy); pdm_fx_neg(&g_base.c, sx);
        pdm_fx_mul(&t4, sx, py1); pdm_fx_add(&g_base.e, &g_base.e, &t4);
        pdm_fx_mul(&t4, sy, px1); pdm_fx_add(&g_base.f, &g_base.f, &t4);
        break;
    default:
        pdr_cpy(&g_base.a, sx, 4); pdm_fx_neg(&g_base.d, sy);
        pdm_fx_mul(&t4, sx, px0); pdm_fx_sub(&g_base.e, &g_base.e, &t4);
        pdm_fx_mul(&t4, sy, py1); pdm_fx_add(&g_base.f, &g_base.f, &t4);
        break;
    }
}

/* ---- device points ------------------------------------------------------ */
static struct pd_fx ta, tb;
static pdi g_dx, g_dy;              /* result of to_dev                      */
static void to_dev(const struct pd_fx *x, const struct pd_fx *y)
{
    pdr_cpy(&ta, x, 4); pdr_cpy(&tb, y, 4);
    pdm_mat_apply(&ta, &tb, &GS.ctm);
    g_dx = pdr_clampq(pdm_fx_round(&ta));
    g_dy = pdr_clampq(pdm_fx_round(&tb));
}
/* qpx -> the pixel whose sample point is nearest (rounding) */
static pdi q2p(pdi q) { return (pdi)((q + 2) >> 2); }
/* first pixel whose centre is >= q, and last pixel whose centre is < q */
static pdi q_lo(pdi q) { return (pdi)((q + 1) >> 2); }       /* ceil((q-2)/4) */
static pdi q_hi(pdi q) { return (pdi)(((q + 1) >> 2) - 1); }

/* ---- the path ----------------------------------------------------------- */
static pdi g_vx[PDR_NPATH], g_vy[PDR_NPATH];
static pdb g_vf[PDR_NPATH];         /* bit0 starts a subpath, bit1 closed    */
static pdw g_nv, g_cur, g_sub;      /* vertices, have a current point, start */
static pdi g_cpx, g_cpy;            /* current point in qpx                  */
static pdw g_pclip;                 /* W / W* pending: 1 nonzero, 2 even-odd */
static pdw g_plimit;

static void path_reset(void) { g_nv = 0; g_cur = 0; g_sub = 0; g_pclip = 0; }

static void path_add(pdi x, pdi y, pdb flag)
{
    if (g_nv >= PDR_NPATH) { g_plimit = 1; return; }
    g_vx[g_nv] = x; g_vy[g_nv] = y; g_vf[g_nv] = flag; g_nv++;
    g_cpx = x; g_cpy = y;
}
static void path_move(pdi x, pdi y)
{
    /* a moveto directly after a moveto replaces it */
    if (g_nv && (g_vf[g_nv - 1] & 1) && g_sub == g_nv - 1) g_nv--;
    g_sub = g_nv; g_cur = 1;
    path_add(x, y, 1);
}
static void path_line(pdi x, pdi y)
{
    if (!g_cur) { path_move(x, y); return; }
    path_add(x, y, 0);
}
static void path_close(void)
{
    if (!g_cur || g_nv == 0) return;
    g_vf[g_sub] |= 2;
    g_cpx = g_vx[g_sub]; g_cpy = g_vy[g_sub];
}

/* cubic flattening by de Casteljau subdivision. FLATNESS BOUND: a piece is
 * flat when both second differences of its control polygon sum (|dx|+|dy|)
 * to <= 1 qpx, so the chord is within 3/4 * 1 qpx = 0.19 pixel of the curve;
 * depth is capped at 6 (64 pieces a curve - the operation limit). */
static void bez(pdi x0, pdi y0, pdi x1, pdi y1, pdi x2, pdi y2, pdi x3, pdi y3,
                pdw d)
{
    pdi m1, m2, ax, ay, bx, by, cx, cy, ex, ey, fx, fy, gx, gy;
    m1 = (pdi)(pdr_abs((pdi)(x0 - 2 * x1 + x2)) + pdr_abs((pdi)(y0 - 2 * y1 + y2)));
    m2 = (pdi)(pdr_abs((pdi)(x1 - 2 * x2 + x3)) + pdr_abs((pdi)(y1 - 2 * y2 + y3)));
    if (d >= 6 || (m1 <= 1 && m2 <= 1) || g_nv >= PDR_NPATH) {
        path_add(x3, y3, 0);
        return;
    }
    ax = (pdi)((x0 + x1) >> 1); ay = (pdi)((y0 + y1) >> 1);
    bx = (pdi)((x1 + x2) >> 1); by = (pdi)((y1 + y2) >> 1);
    cx = (pdi)((x2 + x3) >> 1); cy = (pdi)((y2 + y3) >> 1);
    ex = (pdi)((ax + bx) >> 1); ey = (pdi)((ay + by) >> 1);
    fx = (pdi)((bx + cx) >> 1); fy = (pdi)((by + cy) >> 1);
    gx = (pdi)((ex + fx) >> 1); gy = (pdi)((ey + fy) >> 1);
    bez(x0, y0, ax, ay, ex, ey, gx, gy, d + 1);
    bez(gx, gy, fx, fy, cx, cy, x3, y3, d + 1);
}
static void path_curve(pdi x1, pdi y1, pdi x2, pdi y2, pdi x3, pdi y3)
{
    if (!g_cur) { path_move(x1, y1); }
    bez(g_cpx, g_cpy, x1, y1, x2, y2, x3, y3, 0);
}

/* ---- edges: the staging area every filled shape goes through ------------ */
static pdi g_ed[PDR_NEDGE][4];      /* x0 y0 x1 y1 in qpx, original direction */
static pdw g_ne;
static pdi g_eylo, g_eyhi;          /* the group's rows, qpx, for culling    */

/* append an edge; horizontal edges cannot cross a scan line and wholly
 * out-of-group ones cannot matter, so neither is kept */
static void edge_add(pdi x0, pdi y0, pdi x1, pdi y1)
{
    if (y0 == y1) return;
    if (y0 < y1) { if (y1 < g_eylo || y0 > g_eyhi) return; }
    else         { if (y0 < g_eylo || y1 > g_eyhi) return; }
    if (g_ne >= PDR_NEDGE) return;       /* callers flush before this        */
    g_ed[g_ne][0] = x0; g_ed[g_ne][1] = y0; g_ed[g_ne][2] = x1; g_ed[g_ne][3] = y1;
    g_ne++;
}

/* ---- DL writer interface (pdrd.c) --------------------------------------- */
static void emit_rect_q(pdi xa, pdi ya, pdi xb, pdi yb, pdw kind);
static void emit_fill_edges(pdw rule, pdw kind);
#define PK_FILL   0                 /* dithered on a 1bpp page               */
#define PK_STROKE 1                 /* solid                                 */
#define PK_TEXT   2                 /* the fill colour, solid                */

/* ---- strokes ------------------------------------------------------------ */
static pdi g_sw;                    /* stroke width, qpx (>= 4)              */
static pdi g_shw;                   /* half of it                            */

static void stroke_flush(void)
{
    if (g_ne) { emit_fill_edges(0, PK_STROKE); g_ne = 0; }
}
static void poly4(pdi x0, pdi y0, pdi x1, pdi y1, pdi x2, pdi y2, pdi x3, pdi y3)
{
    if (g_ne + 4 > PDR_NEDGE) stroke_flush();
    edge_add(x0, y0, x1, y1); edge_add(x1, y1, x2, y2);
    edge_add(x2, y2, x3, y3); edge_add(x3, y3, x0, y0);
}
static void poly3(pdi x0, pdi y0, pdi x1, pdi y1, pdi x2, pdi y2)
{
    if (g_ne + 3 > PDR_NEDGE) stroke_flush();
    edge_add(x0, y0, x1, y1); edge_add(x1, y1, x2, y2); edge_add(x2, y2, x0, y0);
}
/* an octagon (round join / round cap / dot) */
static const signed char oct_c[8] = { 100, 71, 0, -71, -100, -71, 0, 71 };
static const signed char oct_s[8] = { 0, 71, 100, 71, 0, -71, -100, -71 };
static void octagon(pdi cx, pdi cy, pdi r)
{
    static pdi ox[8], oy[8];
    pdw i;
    for (i = 0; i < 8; i++) {
        ox[i] = (pdi)(cx + pdr_muldiv(oct_c[i], r, 100));
        oy[i] = (pdi)(cy + pdr_muldiv(oct_s[i], r, 100));
    }
    if (g_ne + 8 > PDR_NEDGE) stroke_flush();
    for (i = 0; i < 8; i++) edge_add(ox[i], oy[i], ox[(i + 1) & 7], oy[(i + 1) & 7]);
}

/* one polyline. ends[] caps: kind 0 butt 1 round 2 square */
static pdi g_ux1, g_uy1, g_ux2, g_uy2;      /* unit directions, x100          */
static pdi g_l1, len, len2;
static void unit_dir(pdi dx, pdi dy, pdi *ux, pdi *uy, pdi *len)
{
    pdi l = pdr_hypot(dx, dy);
    *len = l;
    if (l == 0) { *ux = 0; *uy = 0; return; }
    *ux = pdr_muldiv(dx, 100, l); *uy = pdr_muldiv(dy, 100, l);
}

/* axis-aligned polylines become rectangles (the table-rule fast path) */
static pdw stroke_axis(const pdi *xs, const pdi *ys, pdw n, pdw closed)
{
    pdw i, j, nseg = closed ? n : n - 1;
    pdi ax, ay, bx, by, e0, e1, xa, xb, ya, yb;
    for (i = 0; i < nseg; i++) {
        j = (i + 1 == n) ? 0 : i + 1;
        if (xs[i] != xs[j] && ys[i] != ys[j]) return 0;
    }
    for (i = 0; i < nseg; i++) {
        j = (i + 1 == n) ? 0 : i + 1;
        ax = xs[i]; ay = ys[i]; bx = xs[j]; by = ys[j];
        if (ax == bx && ay == by) continue;
        /* extension at each end: a join (any interior vertex, or a closed
         * path) extends by the half width; an end takes its cap          */
        e0 = (closed || i > 0) ? g_shw : (GS.cap ? g_shw : 0);
        e1 = (closed || i + 1 < nseg) ? g_shw : (GS.cap ? g_shw : 0);
        if (ax == bx) {
            xa = (pdi)(ax - g_shw); xb = (pdi)(ax + g_shw);
            if (ay < by) { ya = (pdi)(ay - e0); yb = (pdi)(by + e1); }
            else         { ya = (pdi)(by - e1); yb = (pdi)(ay + e0); }
        } else {
            ya = (pdi)(ay - g_shw); yb = (pdi)(ay + g_shw);
            if (ax < bx) { xa = (pdi)(ax - e0); xb = (pdi)(bx + e1); }
            else         { xa = (pdi)(bx - e1); xb = (pdi)(ax + e0); }
        }
        emit_rect_q(xa, ya, xb, yb, PK_STROKE);
    }
    return 1;
}

static void stroke_poly(const pdi *xs, const pdi *ys, pdw n, pdw closed)
{
    pdw i, j, k, nseg = closed ? n : n - 1;
    pdi x0, y0, x1, y1, nx, ny, nx2, ny2, cr, dot, mx, my, s;
    pdi e;
    if (n == 1 || nseg == 0) {                       /* a dot */
        if (GS.cap == 1) { octagon(xs[0], ys[0], g_shw); }
        else if (GS.cap == 2) emit_rect_q((pdi)(xs[0] - g_shw), (pdi)(ys[0] - g_shw),
                                         (pdi)(xs[0] + g_shw), (pdi)(ys[0] + g_shw), PK_STROKE);
        return;
    }
    if (GS.cap != 1 && GS.join != 1 && stroke_axis(xs, ys, n, closed)) return;
    for (i = 0; i < nseg; i++) {
        j = (i + 1 == n) ? 0 : i + 1;
        x0 = xs[i]; y0 = ys[i]; x1 = xs[j]; y1 = ys[j];
        unit_dir((pdi)(x1 - x0), (pdi)(y1 - y0), &g_ux1, &g_uy1, &len);
        if (len == 0) continue;
        nx = pdr_muldiv((pdi)(y1 - y0), g_shw, len);
        ny = pdr_muldiv((pdi)(x0 - x1), g_shw, len);
        /* caps: square extends the end along the direction */
        e = 0;
        if (!closed && i == 0 && GS.cap == 2) {
            x0 = (pdi)(x0 - pdr_muldiv(g_ux1, g_shw, 100));
            y0 = (pdi)(y0 - pdr_muldiv(g_uy1, g_shw, 100));
        }
        if (!closed && i + 1 == nseg && GS.cap == 2) {
            x1 = (pdi)(x1 + pdr_muldiv(g_ux1, g_shw, 100));
            y1 = (pdi)(y1 + pdr_muldiv(g_uy1, g_shw, 100));
        }
        (void)e;
        poly4((pdi)(x0 + nx), (pdi)(y0 + ny), (pdi)(x1 + nx), (pdi)(y1 + ny),
              (pdi)(x1 - nx), (pdi)(y1 - ny), (pdi)(x0 - nx), (pdi)(y0 - ny));
        if (!closed && GS.cap == 1) {
            if (i == 0) octagon(xs[i], ys[i], g_shw);
            if (i + 1 == nseg) octagon(xs[j], ys[j], g_shw);
        }
        /* the join at the far end, with the next segment */
        if (closed || i + 1 < nseg) {
            k = (j + 1 == n) ? 0 : j + 1;
            unit_dir((pdi)(xs[k] - xs[j]), (pdi)(ys[k] - ys[j]), &g_ux2, &g_uy2, &len2);
            if (len2 == 0) continue;
            cr = (pdi)(g_ux1 * g_uy2 - g_uy1 * g_ux2);      /* x100 x100 <= 2e4 */
            dot = (pdi)(g_ux1 * g_ux2 + g_uy1 * g_uy2);
            if (GS.join == 1) {
                if (g_shw >= 6) octagon(xs[j], ys[j], g_shw);
                continue;
            }
            if (cr > -2 && cr < 2 && dot > 0) continue;     /* straight on   */
            /* outer side: +n when cr > 0 */
            s = cr > 0 ? 1 : -1;
            nx = pdr_muldiv((pdi)(ys[j] - ys[i]), g_shw, len);   /* n1 */
            ny = pdr_muldiv((pdi)(xs[i] - xs[j]), g_shw, len);
            nx2 = pdr_muldiv((pdi)(ys[k] - ys[j]), g_shw, len2); /* n2 */
            {
                pdi ny2 = pdr_muldiv((pdi)(xs[j] - xs[k]), g_shw, len2);
                pdi ax = (pdi)(xs[j] + s * nx), ay = (pdi)(ys[j] + s * ny);
                pdi bx = (pdi)(xs[j] + s * nx2), by = (pdi)(ys[j] + s * ny2);
                pdw miter_ok = 0;
                if (GS.join == 0) {
                    /* miter if 1/sin(theta/2) <= limit: (100+dot)*L^2 >= 200 */
                    pdi lim = pdm_fx_floor(&GS.miter);
                    if (lim < 1) lim = 1;
                    if (lim > 20) lim = 20;
                    if ((pdi)(100 + dot) > 0 &&
                        pdr_muldiv((pdi)(100 + dot), (pdi)(lim * lim), 200) >= 1) miter_ok = 1;
                }
                if (miter_ok) {
                    mx = pdr_muldiv((pdi)(nx + nx2), 100, (pdi)(100 + dot));
                    my = pdr_muldiv((pdi)(ny + ny2), 100, (pdi)(100 + dot));
                    mx = (pdi)(xs[j] + s * mx); my = (pdi)(ys[j] + s * my);
                    if (cr > 0) poly4(xs[j], ys[j], ax, ay, mx, my, bx, by);
                    else        poly4(xs[j], ys[j], bx, by, mx, my, ax, ay);
                } else {
                    if (cr > 0) poly3(xs[j], ys[j], ax, ay, bx, by);
                    else        poly3(xs[j], ys[j], bx, by, ax, ay);
                }
            }
        }
    }
}

/* dashing: walk a polyline and hand each dash to stroke_poly */
static pdi g_dl[4];                 /* dash lengths, qpx                     */
static pdi g_dtot;
static pdi g_dxs[32], g_dys[32];
static pdw g_dn;

static void dash_flush(void)
{
    if (g_dn >= 1) stroke_poly(g_dxs, g_dys, g_dn > 1 ? g_dn : 1, 0);
    g_dn = 0;
}
static void dash_poly(const pdi *xs, const pdi *ys, pdw n, pdw closed)
{
    pdw i, j, nseg = closed ? n : n - 1, di = 0, on;
    pdi rem, seglen, pos, ax, ay, bx, by, step, px, py;
    if (nseg == 0) return;
    /* initial position inside the dash pattern from the phase */
    on = 1; rem = g_dl[0];
    g_dn = 0;
    for (i = 0; i < nseg; i++) {
        j = (i + 1 == n) ? 0 : i + 1;
        ax = xs[i]; ay = ys[i]; bx = xs[j]; by = ys[j];
        seglen = pdr_hypot((pdi)(bx - ax), (pdi)(by - ay));
        if (seglen == 0) continue;
        pos = 0;
        if (on && g_dn == 0) { g_dxs[0] = ax; g_dys[0] = ay; g_dn = 1; }
        while (seglen - pos > rem) {
            step = rem;
            pos = (pdi)(pos + step);
            px = (pdi)(ax + pdr_muldiv((pdi)(bx - ax), pos, seglen));
            py = (pdi)(ay + pdr_muldiv((pdi)(by - ay), pos, seglen));
            if (on) {
                if (g_dn < 32) { g_dxs[g_dn] = px; g_dys[g_dn] = py; g_dn++; }
                dash_flush();
            } else {
                g_dxs[0] = px; g_dys[0] = py; g_dn = 1;
            }
            on = !on;
            di = (di + 1) % GS.ndash;
            rem = g_dl[di];
            if (rem < 2) rem = 2;
        }
        rem = (pdi)(rem - (seglen - pos));
        if (on && g_dn < 32) { g_dxs[g_dn] = bx; g_dys[g_dn] = by; g_dn++; }
    }
    if (on) dash_flush(); else g_dn = 0;
}

/* user-space scale of the CTM, qpx per user unit: a hypot of its first row,
 * taken in 1/256 units while the row is small enough for 16 bits, else in
 * whole units */
static void ctm_scale(struct pd_fx *out)
{
    pdi fa, fb, h;
    pdr_cpy(&ta, &GS.ctm.a, 4); pdr_cpy(&tb, &GS.ctm.b, 4);
    if (ta.hi < 0) pdm_fx_neg(&ta, &ta);
    if (tb.hi < 0) pdm_fx_neg(&tb, &tb);
    if (ta.hi < 120 && tb.hi < 120) {
        fa = (pdi)(ta.hi * 256 + (ta.lo >> 8));
        fb = (pdi)(tb.hi * 256 + (tb.lo >> 8));
        h = pdr_hypot(fa, fb);
        out->hi = (pdi)(h >> 8); out->lo = (pdw)((h & 255) << 8);
    } else {
        h = pdr_hypot(ta.hi, tb.hi);
        pdm_fx_int(out, h);
    }
}

static void stroke_path(void)
{
    struct pd_fx *sc = &t0, *w = &t1;
    pdw s, e, n, closed, i;
    pdi lw;
    ctm_scale(sc);
    pdm_fx_mul(w, &GS.lw, sc);                       /* qpx                   */
    lw = (pdi)(w->hi);
    if (w->hi > 4000) lw = 4000;
    if (lw < 4) lw = 4;                              /* a hairline is 1 px    */
    g_sw = lw; g_shw = (pdi)((lw + 1) >> 1);
    if (GS.ndash) {
        g_dtot = 0;
        for (i = 0; i < GS.ndash; i++) {
            pdm_fx_mul(w, &GS.dash[i], sc);
            g_dl[i] = w->hi < 2 ? 2 : (w->hi > 4000 ? 4000 : w->hi);
            g_dtot = (pdi)(g_dtot + g_dl[i]);
        }
    }
    g_ne = 0;
    for (s = 0; s < g_nv; s = e) {
        for (e = s + 1; e < g_nv && !(g_vf[e] & 1); e++) ;
        closed = (g_vf[s] & 2) != 0;
        /* drop repeated points */
        n = 0;
        {
            static pdi qx[PDR_NPATH], qy[PDR_NPATH];
            for (i = s; i < e; i++) {
                if (n && qx[n - 1] == g_vx[i] && qy[n - 1] == g_vy[i]) continue;
                qx[n] = g_vx[i]; qy[n] = g_vy[i]; n++;
            }
            if (closed && n > 1 && qx[0] == qx[n - 1] && qy[0] == qy[n - 1]) n--;
            if (n == 0) continue;
            if (GS.ndash && n > 1) dash_poly(qx, qy, n, closed);
            else stroke_poly(qx, qy, n, closed && n > 1);
        }
    }
    stroke_flush();
}
