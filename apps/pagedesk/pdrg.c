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
    struct pd_fx tc, tw, tz, tl, rise, fs;  /* text state                    */
    pdi  cx0, cy0, cx1, cy1;        /* clip rectangle, pixels, inclusive     */
    pdb  cap, join, pad_;
    pdb  flum, fc4, slum, sc4;      /* fill / stroke: luminance, VGA index   */
    pdb  fncs, sncs;                /* colour components of the space (1,3,4)*/
    pdb  tr, font;                  /* render mode, font cache index (255=none)*/
    pdb  clid;                      /* innermost polygon clip in the pool, 0xFF none */
    pdw  dph;                       /* dash phase, quarter points, already reduced modulo the period (< 2,040) */
    pdb  dn, dash[4];               /* dash pattern: elements (0 = solid) and lengths, quarter points */
};
/* the current state lives here; the SAVED states (q) and the font cache live in
 * the workspace at the tail of dl_seg (PDR_WSB bytes, PAGEDESK-SPEC 20.3.1) */
static struct pdr_gs g_gcur;
static pdw g_gsn;                   /* depth: saved states below the current */
#define GS g_gcur
static pdw g_ws;                    /* workspace segment (dl_seg + DL capacity) */
#define WS_GS   0u                  /* PD_MAXGSTACK x sizeof(struct pdr_gs)    */
#define WS_FN   1344u               /* PDR_NFONT x sizeof(struct pdr_font); the 16 states end at 16 x 84 */
#define WS_ED   4096u               /* PDR_NEDGE staged edges, 8 bytes each    */
#define WS_CE   6144u               /* PDR_NCLIPE clip edges, 8 bytes each     */
/* the 16 saved states end before the font table: held by the compiler on both builds */
typedef char pdr_ck_gs[PD_MAXGSTACK * sizeof(struct pdr_gs) <= WS_FN ? 1 : -1];
static void gs_save(pdw n) { pd_fcopy_out(&g_gcur, g_ws, (pdw)(WS_GS + n * sizeof(struct pdr_gs)), sizeof(struct pdr_gs)); }
static void gs_load(pdw n) { pd_fcopy_in(g_ws, (pdw)(WS_GS + n * sizeof(struct pdr_gs)), &g_gcur, sizeof(struct pdr_gs)); }

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

#ifdef PD_HOST
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
#else
void pdr_cpy(void *d, const void *s, pdw n);     /* pdrprim.inc (perf) */
void pdr_zero(void *d, pdw n);
#endif
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
static const char *g_fnote;            /* perf: font_get's first font note */
static void inc_note(pdw bit, const char *name)
{
    pdw i;
    if (bit == PDF_INC_FONT && !g_fnote) g_fnote = name;
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
    if (r > (pdw)(0x7FFF >> sh)) return 0x7FFF;      /* saturate: a length is never negative */
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
/* the string operand (pdri.c's Tj/TJ/'/"), here so that pdrd.c can borrow it */
static pdb g_str[PDR_NSTR];
/* ROOM (PAGEDESK-SPEC 20.22.7): the path, g_str and the band rasteriser's
 * scratch are never live at once. Interpretation and rasterisation never
 * interleave inside a pass: a pass interprets to its end (or is restarted,
 * which empties the path) before any band of it is drawn, and a raster step
 * is followed by a fresh pass. So pdrd.c's active-edge list, crossings and
 * clip masks are LENT the interpretation-only arrays below (1,088 bytes of
 * bss). Anything new that runs at raster time and wants scratch may borrow
 * them too, under the same rule; nothing that runs while interpreting may. */
typedef char pdr_ck_lend1[PDR_NEDGE * 2 <= PDR_NSTR ? 1 : -1];               /* g_act on g_str */
typedef char pdr_ck_lend2[PDR_NSPAN * 3 <= PDR_NPATH * 2 ? 1 : -1];          /* g_cx, g_cd on g_vx */
typedef char pdr_ck_lend3[PDR_MAXSTRIDE * 2 <= PDR_NPATH * 2 ? 1 : -1];      /* g_mk on g_vy */

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
    if (!g_cur || g_nv == 0 || g_sub >= g_nv) return;
    g_vf[g_sub] |= 2;
    g_cpx = g_vx[g_sub]; g_cpy = g_vy[g_sub];
}

/* cubic flattening by de Casteljau subdivision. FLATNESS BOUND: a piece is
 * flat when both second differences of its control polygon sum (|dx|+|dy|)
 * to <= 1 qpx, so the chord is within 3/4 * 1 qpx = 0.19 pixel of the curve;
 * depth is capped at 6 (64 pieces a curve - the operation limit).
 *
 * ITERATIVE, over a stack in the bss (rendfix): this ran on the UI task's
 * 512-byte stack (SS is the caller's, PAGEDESK-SPEC 3.1) below the shell, two
 * module doors and the interpreter, and recursing seven deep at 50 bytes a
 * level overran it into the kernel's menu bar (.lowbss under task 0's stack):
 * XTGUIDE.PDF page 3's cable curve. Same pieces, same order: the left half is
 * taken first. At most depth + 1 pieces are pending, so 7 is the bound. */
#define PDR_BZMAX 7
static pdi g_bz[PDR_BZMAX * 8];      /* pending pieces: x0 y0 x1 y1 x2 y2 x3 y3 */
static pdw g_bzd[PDR_BZMAX];         /* ...and each one's depth               */
static void bez(pdi x0, pdi y0, pdi x1, pdi y1, pdi x2, pdi y2, pdi x3, pdi y3)
{
    pdi a, b, c, e, f, m1, m2;
    pdi *p, *r, *t;
    pdw n, d;
    g_bz[0] = x0; g_bz[1] = y0; g_bz[2] = x1; g_bz[3] = y1;
    g_bz[4] = x2; g_bz[5] = y2; g_bz[6] = x3; g_bz[7] = y3;
    g_bzd[0] = 0;
    n = 1;
    while (n) {
        n--;
        p = g_bz + n * 8;
        d = g_bzd[n];
        m1 = 0; m2 = 0;
        for (r = p; r < p + 2; r++) {      /* the x's, then the y's */
            m1 = (pdi)(m1 + pdr_abs((pdi)(r[0] - 2 * r[2] + r[4])));
            m2 = (pdi)(m2 + pdr_abs((pdi)(r[2] - 2 * r[4] + r[6])));
        }
        if (d >= 6 || (m1 <= 1 && m2 <= 1) || g_nv >= PDR_NPATH || n + 2 > PDR_BZMAX) {
            path_add(p[6], p[7], 0);
            continue;
        }
        /* the left half on top (t), the right half where this piece was (its
         * end point stays) */
        for (r = p; r < p + 2; r++) {
            t = r + 8;
            a = (pdi)((r[0] + r[2]) >> 1);
            b = (pdi)((r[2] + r[4]) >> 1);
            c = (pdi)((r[4] + r[6]) >> 1);
            e = (pdi)((a + b) >> 1);
            f = (pdi)((b + c) >> 1);
            t[0] = r[0]; t[2] = a; t[4] = e;
            t[6] = r[0] = (pdi)((e + f) >> 1);
            r[2] = f; r[4] = c;
        }
        g_bzd[n] = g_bzd[n + 1] = (pdw)(d + 1);
        n += 2;
    }
}
static void path_curve(pdi x1, pdi y1, pdi x2, pdi y2, pdi x3, pdi y3)
{
    if (!g_cur) { path_move(x1, y1); }
    bez(g_cpx, g_cpy, x1, y1, x2, y2, x3, y3);
}

/* ---- edges: the staging area every filled shape goes through ------------ */
static pdi g_etmp[4];                /* x0 y0 x1 y1 in qpx, original direction: one edge, on its way to the workspace */
static pdw g_ne;
static pdi g_eylo, g_eyhi;          /* the group's rows, qpx, for culling    */

/* append an edge; horizontal edges cannot cross a scan line and wholly
 * out-of-group ones cannot matter, so neither is kept */
static void edge_add(pdi x0, pdi y0, pdi x1, pdi y1)
{
    /* every stored edge lies in +/-PDR_QMAX: a stroke's offset or a miter can
     * reach past it, and pdr_scan's in-line divide relies on |x1 - x0| < 32768 */
    x0 = pdr_clampq(x0); y0 = pdr_clampq(y0); x1 = pdr_clampq(x1); y1 = pdr_clampq(y1);
    if (y0 == y1) return;
    if (y0 < y1) { if (y1 < g_eylo || y0 > g_eyhi) return; }
    else         { if (y0 < g_eylo || y1 > g_eyhi) return; }
    if (g_ne >= PDR_NEDGE) return;       /* callers flush before this        */
    g_etmp[0] = x0; g_etmp[1] = y0; g_etmp[2] = x1; g_etmp[3] = y1;
    pd_fcopy_out(g_etmp, g_ws, (pdw)(WS_ED + g_ne * 8), 8);
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
    if (n == 1 || nseg == 0) {                       /* a dot; at a hairline's width a round one is a */
        if (GS.cap == 1 && g_shw > 2) { octagon(xs[0], ys[0], g_shw); }   /* pixel: an octagon of half a pixel can miss every pixel centre */
        else if (GS.cap) emit_rect_q((pdi)(xs[0] - g_shw), (pdi)(ys[0] - g_shw),
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
                    /* the tip can be ~14 half widths out (and rounding of the
                     * x100 directions makes it more): clamp BEFORE the add, so
                     * the sum cannot wrap 16 bits; edge_add clamps the result */
                    mx = pdr_clampq(mx); my = pdr_clampq(my);
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


/* ---- dash patterns (the `d` operator; PAGEDESK-SPEC 20.12.4, 20.22.7) -------
 * GS.dash holds up to four lengths in quarter points. The WALK - cutting a
 * polyline where the pattern changes - is PDXG's (pdxdash.c, PDV_GFX_DASH):
 * the core hands it a subpath in the exchange block and strokes every "on"
 * piece it hands back as its own open polyline (so each carries the line's
 * caps). The module absent, the stroke is skipped and the pass asks for it
 * (the ladder, 20.22.2); refused, the subpath is stroked solid. */
static pdw xb_seg(void);            /* pdrv.c: the exchange block (pdrf.c's g_xb) */
#ifndef PDV_GFX_DASH
#define PDV_GFX_DASH 0xCE           /* gfx's (20.22.9); defined here until its block lands */
#endif
#define XD_XS 160u                  /* the subpath's x in the XB, after the block, then its y */
#define XD_YS (XD_XS + PDR_NPATH * 2)
typedef char pdr_ck_xd[sizeof(struct pdxb_dash) <= XD_XS && XD_YS + PDR_NPATH * 2 <= PD_XB_BYTES ? 1 : -1];
static struct pdxb_dashin g_din;
static struct pdxb_dashout g_dout;
static void dash_poly(const pdi *xs, const pdi *ys, pdw n, pdw closed)
{
    pdw r, k, xb;
    if (!x_have(PDX_GFX)) {
        x_need(PDX_GFX);
        if (x_gone(PDX_GFX)) stroke_poly(xs, ys, n, closed);
        return;
    }
    xb = xb_seg();
    pd_fcopy_out(xs, xb, XD_XS, (pdw)(n << 1));
    pd_fcopy_out(ys, xb, XD_YS, (pdw)(n << 1));
    g_din.vseg = xb; g_din.xs = XD_XS; g_din.ys = XD_YS; g_din.n = n; g_din.closed = closed;
    ctm_scale(&g_din.scale);
    g_din.dph = GS.dph; g_din.dn = GS.dn; pdr_cpy(g_din.dash, GS.dash, 4); g_din.st = 0;
    pd_fcopy_out(&g_din, xb, sizeof(struct pdxb_dashout), sizeof g_din);
    for (k = 0; k < 4000; k++) {
        r = x_call(PDV_GFX_DASH, xb, 0, 0);
        if (x_cf || r == 2) { if (!k) stroke_poly(xs, ys, n, closed); return; }
        if (r != 1) return;
        pd_fcopy_in(xb, 0, &g_dout, sizeof g_dout);
        if (g_dout.on < 1 || g_dout.on > PDXD_NPT) return;
        stroke_poly(g_dout.ox, g_dout.oy, g_dout.on, 0);
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
    g_ne = 0;
    for (s = 0; s < g_nv; s = e) {
        for (e = s + 1; e < g_nv && !(g_vf[e] & 1); e++) ;
        closed = (g_vf[s] & 2) != 0;
        /* drop repeated points, IN PLACE: the vacated tail repeats the last kept
         * point, so a clip that still wants this path sees the same polygon */
        n = 0;
        for (i = s; i < e; i++) {
            if (n && g_vx[s + n - 1] == g_vx[i] && g_vy[s + n - 1] == g_vy[i]) continue;
            g_vx[s + n] = g_vx[i]; g_vy[s + n] = g_vy[i]; n++;
        }
        if (n == 0) continue;
        {
            pdw m = n;
            if (closed && m > 1 && g_vx[s] == g_vx[s + m - 1] && g_vy[s] == g_vy[s + m - 1]) m--;
            for (i = s + n; i < e; i++) { g_vx[i] = g_vx[s + n - 1]; g_vy[i] = g_vy[s + n - 1]; }
            if (GS.dn) dash_poly(g_vx + s, g_vy + s, m, closed && m > 1);
            else stroke_poly(g_vx + s, g_vy + s, m, closed && m > 1);
        }
    }
    stroke_flush();
}
