/* ============================================================================
 * os8088 - apps/pagedesk/pdxfol.c      PDXF: the outline recorder and rasteriser
 *
 * #included by pdxf.c. PAGEDESK-SPEC 20.22.8. The format readers (pdxfsf.c,
 * pdxfcf.c, pdxft1.c) call ol_move/ol_line/ol_quad/ol_cubic/ol_close; the
 * recorded ops are then either exported (PDV_FNT_PATH) or rasterised
 * (PDV_FNT_GLYPH). Fixed point only: font units in, 26.6 pixels inside.
 *
 * pdx_accline() and pdx_pack() are the inner loops: pdxf.asm replaces both
 * with assembly (same globals, same results - hosttest/pdx_check.py holds the
 * two to each other byte for byte).
 * ==========================================================================*/

#define X6(x)  ((pdw)((((pdw)(x)) << 2) + (((pdw)(x)) << 1)))
#define X10(x) ((pdw)((((pdw)(x)) << 3) + (((pdw)(x)) << 1)))

/* ---- the outline recorder ------------------------------------------------ */
#define OL_CAP 3072                 /* pdi words: ~600 points                  */
#define OLM 1                       /* M x y                                   */
#define OLL 2                       /* L x y                                   */
#define OLQ 3                       /* Q cx cy x y                             */
#define OLC 4                       /* C x1 y1 x2 y2 x y                       */
#define OLZ 5                       /* close                                   */
static pdi g_ol[OL_CAP];
static pdw g_oln, g_olfull;
static pdi g_ocx, g_ocy;            /* the current point, font units           */
static pdw g_oopen;

static void ol_reset(void) { g_oln = 0; g_olfull = 0; g_oopen = 0; g_ocx = g_ocy = 0; }
static void ol_put(pdw op, pdw n, const pdi *v)
{
    pdw i;
    if (g_olfull || g_oln + 1 + n > OL_CAP) { g_olfull = 1; return; }
    g_ol[g_oln++] = (pdi)op;
    for (i = 0; i < n; i++) g_ol[g_oln++] = v[i];
}
static pdi g_ov[6];
static void ol_close(void)
{
    if (g_oopen) { ol_put(OLZ, 0, g_ov); g_oopen = 0; }
}
static void ol_move(pdi x, pdi y)
{
    ol_close();
    g_ov[0] = x; g_ov[1] = y; ol_put(OLM, 2, g_ov);
    g_ocx = x; g_ocy = y; g_oopen = 1;
}
static void ol_line(pdi x, pdi y)
{
    if (!g_oopen) ol_move(g_ocx, g_ocy);
    g_ov[0] = x; g_ov[1] = y; ol_put(OLL, 2, g_ov);
    g_ocx = x; g_ocy = y;
}
static void ol_quad(pdi cx, pdi cy, pdi x, pdi y)
{
    if (!g_oopen) ol_move(g_ocx, g_ocy);
    g_ov[0] = cx; g_ov[1] = cy; g_ov[2] = x; g_ov[3] = y; ol_put(OLQ, 4, g_ov);
    g_ocx = x; g_ocy = y;
}
static void ol_cubic(pdi x1, pdi y1, pdi x2, pdi y2, pdi x, pdi y)
{
    if (!g_oopen) ol_move(g_ocx, g_ocy);
    g_ov[0] = x1; g_ov[1] = y1; g_ov[2] = x2; g_ov[3] = y2; g_ov[4] = x; g_ov[5] = y;
    ol_put(OLC, 6, g_ov);
    g_ocx = x; g_ocy = y;
}

/* ---- helpers --------------------------------------------------------------- */
static pdi pdx_abs(pdi v) { return v < 0 ? (pdi)-v : v; }
static pdw pdx_isqrt(pdw v)
{
    pdw r = 0, b = 0x4000;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v = (pdw)(v - (r + b)); r = (pdw)((r >> 1) + b); }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}
static pdi pdx_clamp(pdi v, pdi lo, pdi hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- the accumulator (signed area, 1 px = 256) ------------------------------ */
#define AC_W   34                   /* cells a row: 32 + 2 guard               */
#define AC_H   48                   /* rows                                    */
pdi pdx_acc[AC_W * AC_H];           /* (global: pdxfrs.inc reaches it by name)  */
pdw pdx_acw;                        /* glyph width in px (<= 32)               */
pdw pdx_ach;                        /* rows in use (<= AC_H)                   */

#ifdef PDXF_ASM
void pdx_accline(pdi x0, pdi y0, pdi x1, pdi y1);   /* pdxfrs.inc */
#else
/* one edge in 26.6 device coordinates relative to the bitmap's top-left
 * (x right, y DOWN). Accumulates into pdx_acc. */
static void pdx_accline(pdi x0, pdi y0, pdi x1, pdi y1)
{
    pdi dir = 1, t, ya, yb, xa, xb, xl, xr, dx, dyl, d, ymax, row, il, ir, a0, a1, a2, am, sd, l0, lr, wd, xm;
    pdw cell;
    pdi *a;
    if (y0 == y1) return;
    if (y0 > y1) { dir = -1; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    ymax = (pdi)(pdx_ach << 6);
    if (y1 <= 0 || y0 >= ymax) return;
    dx = (pdi)(x1 - x0); dyl = (pdi)(y1 - y0);
    row = y0 < 0 ? 0 : (pdi)(y0 >> 6);
    for (; (row << 6) < y1 && row < (pdi)pdx_ach; row++) {
        ya = (pdi)(row << 6); if (ya < y0) ya = y0;
        yb = (pdi)((row << 6) + 64); if (yb > y1) yb = y1;
        xa = (pdi)(x0 + pdx_muldiv((pdi)(ya - y0), dx, dyl));
        xb = (pdi)(x0 + pdx_muldiv((pdi)(yb - y0), dx, dyl));
        xl = xa < xb ? xa : xb; xr = xa < xb ? xb : xa;
        xl = pdx_clamp(xl, 0, (pdi)(pdx_acw << 6)); xr = pdx_clamp(xr, 0, (pdi)(pdx_acw << 6));
        d = (pdi)(dir * (yb - ya) * 4);
        a = pdx_acc + row * AC_W;
        il = (pdi)(xl >> 6);
        if (xr - (il << 6) <= 64) {                 /* inside one cell */
            xm = (pdi)(((xl + xr) >> 1) - (il << 6));
            t = pdx_muldiv(d, xm, 64);
            a[il] = (pdi)(a[il] + d - t); a[il + 1] = (pdi)(a[il + 1] + t);
            continue;
        }
        wd = (pdi)(xr - xl);
        ir = (pdi)((xr + 63) >> 6);
        l0 = (pdi)(64 - (xl - (il << 6)));
        lr = (pdi)(xr - ((ir - 1) << 6));
        a0 = pdx_muldiv(pdx_muldiv(d, l0, wd), l0, 128);
        am = pdx_muldiv(pdx_muldiv(d, lr, wd), lr, 128);
        if (ir == il + 2) {
            a[il] = (pdi)(a[il] + a0); a[il + 1] = (pdi)(a[il + 1] + d - a0 - am); a[ir] = (pdi)(a[ir] + am);
        } else {
            sd = pdx_muldiv(d, 64, wd);
            a1 = pdx_muldiv(sd, (pdi)(32 + l0), 64);
            a[il] = (pdi)(a[il] + a0); a[il + 1] = (pdi)(a[il + 1] + a1 - a0);
            for (cell = (pdw)(il + 2); cell + 1 < (pdw)ir; cell++) a[cell] = (pdi)(a[cell] + sd);
            a2 = (pdi)(a1 + (ir - il - 3) * sd);
            a[ir - 1] = (pdi)(a[ir - 1] + d - a2 - am); a[ir] = (pdi)(a[ir] + am);
        }
    }
}

#endif /* PDXF_ASM */

/* the coverage threshold (of 256) at which a pixel is ink; hosttest/pdx_check.py
 * set it (PAGEDESK-SPEC 20.22.8) */
#ifndef PDXF_THR
#define PDXF_THR 128
#endif
/* pack pdx_acc into the far buffer: rows of (w+7)/8 bytes, MSB first. Returns bytes */
#ifdef PDXF_ASM
pdw pdx_pack(pdw dseg, pdw doff);                   /* pdxfrs.inc */
#else
static pdw pdx_pack(pdw dseg, pdw doff)
{
    pdw r, x, wb = (pdw)((pdx_acw + 7) >> 3), bit;
    pdi s, c;
    pdb v;
    pdi *a;
    for (r = 0; r < pdx_ach; r++) {
        a = pdx_acc + r * AC_W; s = 0; v = 0; bit = 0;
        for (x = 0; x < pdx_acw; x++) {
            s = (pdi)(s + a[x]);
            c = pdx_abs(s);
            v = (pdb)(v << 1);
            if (c >= PDXF_THR) v |= 1;
            if (++bit == 8) { pd_fpokeb(dseg, (pdw)(doff + r * wb + (x >> 3)), v); v = 0; bit = 0; }
        }
        if (bit) pd_fpokeb(dseg, (pdw)(doff + r * wb + (pdx_acw >> 3)), (pdb)(v << (8 - bit)));
    }
    return (pdw)(wb * pdx_ach);
}
#endif /* PDXF_ASM */

/* ---- flattening ---------------------------------------------------------- */
static pdi g_fx, g_fy;              /* the current point, 26.6 device           */
static void fl_line(pdi x, pdi y) { pdx_accline(g_fx, g_fy, x, y); g_fx = x; g_fy = y; }
static void fl_quad(pdi cx, pdi cy, pdi x, pdi y)
{
    pdi ddx = (pdi)(g_fx - 2 * cx + x), ddy = (pdi)(g_fy - 2 * cy + y), n, k, nn;
    pdw dd = (pdw)(pdx_abs(ddx) > pdx_abs(ddy) ? pdx_abs(ddx) : pdx_abs(ddy));
    pdi px, py, w0, w1, w2;
    pdi sx = g_fx, sy = g_fy;
    n = (pdi)(1 + pdx_isqrt((pdw)(dd >> 5)));
    if (n > 12) n = 12;
    nn = (pdi)(n * n);
    for (k = 1; k < n; k++) {
        w0 = (pdi)((n - k) * (n - k)); w1 = (pdi)(2 * k * (n - k)); w2 = (pdi)(k * k);
        px = (pdi)(pdx_muldiv(sx, w0, nn) + pdx_muldiv(cx, w1, nn) + pdx_muldiv(x, w2, nn));
        py = (pdi)(pdx_muldiv(sy, w0, nn) + pdx_muldiv(cy, w1, nn) + pdx_muldiv(y, w2, nn));
        fl_line(px, py);
    }
    fl_line(x, y);
}
static void fl_cubic(pdi x1, pdi y1, pdi x2, pdi y2, pdi x, pdi y)
{
    pdi d1x = (pdi)(g_fx - 2 * x1 + x2), d1y = (pdi)(g_fy - 2 * y1 + y2);
    pdi d2x = (pdi)(x1 - 2 * x2 + x), d2y = (pdi)(y1 - 2 * y2 + y);
    pdw dd = 0, v, n, k;
    pdi sx = g_fx, sy = g_fy, px, py, m = 0, w0, w1, w2, w3, nnn;
    v = (pdw)pdx_abs(d1x); if (v > dd) dd = v;
    v = (pdw)pdx_abs(d1y); if (v > dd) dd = v;
    v = (pdw)pdx_abs(d2x); if (v > dd) dd = v;
    v = (pdw)pdx_abs(d2y); if (v > dd) dd = v;
    n = 1 + pdx_isqrt((pdw)(dd >> 4));
    if (n > 14) n = 14;
    nnn = (pdi)(n * n * n); (void)m;
    for (k = 1; k < n; k++) {
        w0 = (pdi)((n - k) * (n - k) * (n - k)); w1 = (pdi)(3 * k * (n - k) * (n - k));
        w2 = (pdi)(3 * k * k * (n - k)); w3 = (pdi)(k * k * k);
        px = (pdi)(pdx_muldiv(sx, w0, nnn) + pdx_muldiv(x1, w1, nnn) + pdx_muldiv(x2, w2, nnn) + pdx_muldiv(x, w3, nnn));
        py = (pdi)(pdx_muldiv(sy, w0, nnn) + pdx_muldiv(y1, w1, nnn) + pdx_muldiv(y2, w2, nnn) + pdx_muldiv(y, w3, nnn));
        fl_line(px, py);
    }
    fl_line(x, y);
}

/* ---- rasterise the recorded outline ---------------------------------------- */
static pdi g_rlb;                   /* left bearing, px                          */
static pdw g_rwpx;                  /* bitmap width, px                          */
static pdi g_ox, g_oy;              /* a point being transformed                  */
static pdi g_t[6];
static pdi g_sxc, g_syc;
/* the outline's coordinates -> 26.6 device (y down from the baseline row) */
static pdi tx_x(pdi x) { return pdx_clamp(pdx_muldiv(x, (pdi)g_em64, (pdi)g_upem), -20000, 20000); }
static pdi tx_y(pdi y, pdw asc) { return pdx_clamp((pdi)((pdi)(asc << 6) - pdx_muldiv(y, (pdi)g_em64, (pdi)g_upem)), -20000, 20000); }

/* returns the bytes written (rows*wb), 0 for an empty glyph (g_rwpx = 0),
 * 0xFFFF when it does not fit (wider than 32 px, or too tall) */
static pdw rs_glyph(pdw asc, pdw rows, pdw dseg, pdw doff, pdw cap)
{
    pdw i, n, k, nc, wb;
    pdi xmin = 32000, xmax = -32000, x, y, v;
    if (g_olfull) return 0xFFFF;
    if (rows > AC_H || !g_upem || !g_em64) return 0xFFFF;
    /* pass 1: transform in place, find the horizontal extent */
    for (i = 0; i < g_oln; ) {
        k = (pdw)g_ol[i++];
        nc = k == OLM || k == OLL ? 2 : k == OLQ ? 4 : k == OLC ? 6 : 0;
        for (n = 0; n < nc; n += 2) {
            x = tx_x(g_ol[i + n]); y = tx_y(g_ol[i + n + 1], asc);
            g_ol[i + n] = x; g_ol[i + n + 1] = y;
            if (x < xmin) xmin = x;
            if (x > xmax) xmax = x;
        }
        i += nc;
    }
    if (xmin > xmax) { g_rwpx = 0; g_rlb = 0; return 0; }
    g_rlb = (pdi)(xmin >> 6);
    v = (pdi)((xmax + 63) >> 6);
    if (v - g_rlb > 32) return 0xFFFF;
    g_rwpx = (pdw)(v - g_rlb);
    if (!g_rwpx) return 0;
    wb = (pdw)((g_rwpx + 7) >> 3);
    if (wb * rows > cap) return 0xFFFF;
    pdx_acw = g_rwpx; pdx_ach = rows;
    g_fx = g_fy = 0;
    for (i = 0; i < AC_W * AC_H; i++) pdx_acc[i] = 0;
    /* pass 2: edges, relative to the bitmap's left (xmin is free now: the left bearing in 26.6,
     * shifted unsigned because a negative signed shift is undefined on the host) */
    xmin = (pdi)((pdw)g_rlb << 6);
    g_sxc = g_syc = 0;
    for (i = 0; i < g_oln; ) {
        k = (pdw)g_ol[i++];
        if (k == OLM) {
            if (g_sxc != g_fx || g_syc != g_fy) pdx_accline(g_fx, g_fy, g_sxc, g_syc);   /* close the open contour */
            g_fx = (pdi)(g_ol[i] - xmin); g_fy = g_ol[i + 1]; g_sxc = g_fx; g_syc = g_fy; i += 2;
        } else if (k == OLL) {
            fl_line((pdi)(g_ol[i] - xmin), g_ol[i + 1]); i += 2;
        } else if (k == OLQ) {
            fl_quad((pdi)(g_ol[i] - xmin), g_ol[i + 1], (pdi)(g_ol[i + 2] - xmin), g_ol[i + 3]); i += 4;
        } else if (k == OLC) {
            fl_cubic((pdi)(g_ol[i] - xmin), g_ol[i + 1], (pdi)(g_ol[i + 2] - xmin), g_ol[i + 3],
                     (pdi)(g_ol[i + 4] - xmin), g_ol[i + 5]); i += 6;
        } else if (k == OLZ) {
            fl_line(g_sxc, g_syc);
        }
    }
    if (g_sxc != g_fx || g_syc != g_fy) pdx_accline(g_fx, g_fy, g_sxc, g_syc);
    return pdx_pack(dseg, doff);
}
