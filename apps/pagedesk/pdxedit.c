/* ============================================================================
 * os8088 - apps/pagedesk/pdxedit.c   PDV_GFX_EDIT: the edit store's marks, in PDXG
 *
 * PAGEDESK-SPEC 13.3 / 17.4 (what a mark looks like) and 20.22.7 (why it lives
 * here). OWNED BY rcore; #included by gfx's pdxg.c AFTER pdxdash.c (it uses
 * xd_hypot and xd_muldiv), whose door gives PDV_GFX_EDIT a case that calls
 * xe_verb(bx, cx); and by hosttest/pdr_xmod.c on the host.
 *
 * These were PDREND's own (pdra.c ed_*) until the any-PDF round needed the
 * room (the lead's 23:24 decision). The geometry is computed here exactly as
 * it was there, and handed back as a list of drawing ops in PAGE space (pt16)
 * that pdra.c's ed_exec runs through the same to_dev / path / paint as before,
 * so a mark is drawn to the same pixel. The Text tool's box stays in PDREND:
 * its line breaking needs the core's font widths.
 *
 * One call = one batch: ops are written at XB offset PDXE_OPS until the first
 * PAINT (the core strokes last, because a dash uses the XB) or until the next
 * op would not fit; `more` says the record goes on. A record is REPLAYED from
 * its start on each call and the first `skip` ops are dropped - the records
 * are small - except ink, whose thousands of points resume from st[].
 * Every local is static and nothing recurses (20.15's stack bound).
 * Needs: pdmath (the far movers), pdabi.h, pdxdash.c.
 * ==========================================================================*/

static struct pdxb_emark g_xe;
static struct pd_edit g_xed;
static pdw xe_seg, xe_off;              /* the XB                                   */
static pdw xe_cnt, xe_wn, xe_full;      /* ops seen, words written, batch closed    */
#define XE_CAP     ((PD_XB_BYTES - PDXE_OPS) / 2 - 7)   /* the core reads 14 bytes an op */
#define XE_PENUP   ((pdi)(-32767 - 1))   /* PD_PENUP */
#define XE_MOVE    1
#define XE_LINE    2
#define XE_INK     3
#define XE_CURVE   4
#define XE_CLOSE   5
#define XE_PAINT   6
#define XE_SETB    7        /* bits 8-15 the field (XF_*), the argument its value */
#define XE_LW      8
/* SETB's fields: pdra.c's ed_f[] in this order */
#define XF_SLUM    0
#define XF_SC4     1
#define XF_FLUM    2
#define XF_FC4     3
#define XF_DN      4
#define XF_DASH0   5
#define XF_DASH1   6
#define XF_DPHLO   7
#define XF_DPHHI   8
#define XF_CAP     9
#define XF_JOIN    10

/* one op and its `na` arguments (in xe_a): written whole or not at all */
static pdi xe_a[6];
static void xe_opn(pdw op, pdw na)
{
    pdw k, o;
    if (xe_full) { g_xe.more = 1; return; }
    if (xe_cnt < g_xe.skip) { xe_cnt++; return; }
    if (xe_wn + 1 + na > XE_CAP) { xe_full = 1; g_xe.more = 1; return; }
    o = (pdw)(xe_off + PDXE_OPS + xe_wn * 2);
    pd_fpokew(xe_seg, o, op);
    for (k = 0; k < na; k++) pd_fpokew(xe_seg, (pdw)(o + 2 + k * 2), (pdw)xe_a[k]);
    xe_wn = (pdw)(xe_wn + 1 + na); xe_cnt++;
    if ((op & 0xFF) == XE_PAINT) xe_full = 1;
}
static void xe_op(pdw op, pdw na, pdi a0, pdi a1) { xe_a[0] = a0; xe_a[1] = a1; xe_opn(op, na); }
/* paint(fill, 0, stroke, close) */
static void xe_paint(pdw f, pdw s, pdw c) { xe_op((pdw)(XE_PAINT | (f << 8) | (s << 9) | (c << 10)), 0, 0, 0); }
static void xe_pt(pdi x, pdi y, pdw mv) { xe_op(mv ? XE_MOVE : XE_LINE, 2, x, y); }
static void xe_set(pdw f, pdw v) { xe_op((pdw)(XE_SETB | (f << 8)), 1, (pdi)v, 0); }

/* luminance of the 16 palette colours (D9: the EGA 4-level triple, 6 is brown) */
static const pdb xe_lum[16] = { 0, 19, 99, 118, 51, 70, 100, 170, 85, 104, 184, 203, 136, 155, 235, 255 };
/* a palette index as the colour of a fill or a stroke; a 1bpp page draws strokes
 * and text black (10.5) */
static void xe_col(pdw stroke, pdw c)
{
    pdw lum = xe_lum[c & 15];
    c &= 15;
    if (g_xe.bpp == 1 && stroke) lum = 0;
    xe_set(stroke ? XF_SLUM : XF_FLUM, lum);
    xe_set(stroke ? XF_SC4 : XF_FC4, c);
}
static void xe_lw(pdi w16) { xe_op(XE_LW, 1, w16, 0); }
/* a two-element dash from phase 0; on = 0 solid */
static void xe_dash(pdw on, pdw off)
{
    xe_set(XF_DN, on ? 2 : 0);
    if (!on) return;
    xe_set(XF_DASH0, on); xe_set(XF_DASH1, off); xe_set(XF_DPHLO, 0); xe_set(XF_DPHHI, 0);
}
/* coordinate `yy` (0 x, 1 y) of the record's point `i` (pt16) */
static pdi xe_ept(pdw i, pdw yy)
{
    return (pdi)pd_fpeekw(g_xe.eseg, (pdw)(g_xed.pts_off + i * 4 + (yy ? 2 : 0)));
}

/* A small path language for the marks that fill a box: pairs of coordinates in a
 * grid of n cells over the box (the note's y grid is 36); -1 move, -2 line,
 * -3 curve (3 pairs), -4 close, -5 fill+stroke, -6 stroke, -7 end */
static const pdi xe_note[] = {          /* PDWRT's speech bubble (pdwap.c ap_note) in a 20 x 18 pt box, half points */
    -1, 6, 34, -2, 34, 34, -3, 37, 34, 38, 33, 38, 30, -2, 38, 14, -3, 38, 11, 37, 10, 34, 10,
    -2, 18, 10, -2, 10, 2, -2, 12, 10, -2, 6, 10, -3, 3, 10, 2, 11, 2, 14, -2, 2, 30,
    -3, 2, 33, 3, 34, 6, 34, -4, -5,
    -1, 8, 26, -2, 32, 26, -1, 8, 21, -2, 32, 21, -1, 8, 16, -2, 22, 16, -6, -7 };
static const pdi xe_rect[] = { -1, 0, 0, -2, 1, 0, -2, 1, 1, -2, 0, 1, -4, -6, -7 };
static const pdi xe_ell[] = {           /* four curves, kappa 0.5523, on a 1000 grid */
    -1, 1000, 500, -3, 1000, 776, 776, 1000, 500, 1000, -3, 224, 1000, 0, 776, 0, 500,
    -3, 0, 224, 224, 0, 500, 0, -3, 776, 0, 1000, 224, 1000, 500, -4, -6, -7 };
static void xe_prog(const pdi *p, pdi n, pdi bx, pdi by, pdi bw, pdi bh)
{
    pdi op, k, m;
    if (bw <= 0 || bh <= 0) return;
    while (*p != -7) {
        op = *p++;
        if (op == -4) { xe_op(XE_CLOSE, 0, 0, 0); continue; }
        if (op == -5) { xe_paint(1, 1, 0); continue; }
        if (op == -6) { xe_paint(0, 1, 0); continue; }
        m = op == -3 ? 6 : 2;
        for (k = 0; k < m; k += 2, p += 2) {
            xe_a[k] = (pdi)(bx + xd_muldiv(p[0], bw, n));
            xe_a[k + 1] = (pdi)(by + xd_muldiv(p[1], bh, n == 40 ? 36 : n));
        }
        if (op == -3) xe_opn(XE_CURVE, 6);
        else xe_pt(xe_a[0], xe_a[1], op == -1);
    }
}

/* the quads' vertex orders: UL UR LR LL for a fill or a frame, LL LR for the underline */
static const char xe_qfill[] = { 0, 1, 3, 2, 9 };
static const char xe_qunder[] = { 2, 3, 9 };
/* mode 0 an open stroke, 1 a fill, 2 a closed stroke */
static void xe_quad(pdw b, const char *o, pdw mode)
{
    pdw k;
    for (k = 0; o[k] < 4; k++) xe_pt(xe_ept((pdw)(b + o[k]), 0), xe_ept((pdw)(b + o[k]), 1), !k);
    if (mode == 0) xe_paint(0, 1, 0); else xe_paint(mode == 1, mode == 2, 1);
}
/* the quads: filled in a light form of the colour on 4bpp (drawn FIRST, so the
 * text stays on top); on 1bpp 17.4's marks by `mono_mark`: 0 underline and a dotted
 * frame, 1 underline, 2 dotted frame, 3 a light hatch (a dither, drawn first too) */
static void xe_highlight(void)
{
    pdw nq = (pdw)(g_xed.pts_n >> 2), q, mm = g_xe.mono, c = g_xed.colour;
    if (nq > 64) nq = 64;
    if (g_xe.bpp == 4) xe_col(0, (c == 0 || c == 7 || c == 15) ? 14 : (c | 8));
    else if (mm == 3) xe_col(0, 7);
    else { xe_col(1, 0); xe_lw(16); }
    for (q = 0; q < nq; q++) {
        if (g_xe.bpp == 4 || mm == 3) { xe_quad((pdw)(q * 4), xe_qfill, 1); continue; }
        if (mm != 2) { xe_dash(0, 0); xe_quad((pdw)(q * 4), xe_qunder, 0); }
        if (mm != 1) { xe_dash(4, 8); xe_quad((pdw)(q * 4), xe_qfill, 2); }
    }
}

/* ink and signature strokes: round caps and joins, a one-point stroke is a dot.
 * st[0] = the next point + 1 (0 = not begun; n + 2 = only the paint is left),
 * st[1] the pen (0 between strokes, 1 one point so far, 2 more), st[2..3] the
 * stroke's first point */
static void xe_ink(void)
{
    pdw i, n = g_xed.pts_n;
    pdi x, y;
    if (n > 4096) n = 4096;
    g_xe.skip = 0;                                   /* ink resumes from st[], never replays */
    if (!g_xe.st[0]) {
        xe_col(1, g_xed.colour); xe_lw((pdi)((g_xed.size ? g_xed.size : 4) * 8));
        xe_set(XF_CAP, 1); xe_set(XF_JOIN, 1);
        g_xe.st[0] = 1;
    }
    for (i = (pdw)(g_xe.st[0] - 1); i <= n; i++) {
        if (xe_wn + 3 > XE_CAP) { g_xe.st[0] = (pdw)(i + 1); g_xe.more = 1; return; }
        x = i < n ? xe_ept(i, 0) : XE_PENUP;
        if (x == XE_PENUP) {
            if (g_xe.st[1] == 1) xe_pt((pdi)g_xe.st[2], (pdi)g_xe.st[3], 0);
            g_xe.st[1] = 0;
            continue;
        }
        y = xe_ept(i, 1);
        xe_op(g_xe.st[1] ? XE_INK : XE_MOVE, 2, x, y);
        if (!g_xe.st[1]) { g_xe.st[1] = 1; g_xe.st[2] = (pdw)x; g_xe.st[3] = (pdw)y; } else g_xe.st[1] = 2;
    }
    g_xe.st[0] = (pdw)(n + 2);
    if (xe_wn + 1 > XE_CAP) { g_xe.more = 1; return; }
    xe_paint(0, 1, 0);
}

/* the arrowhead at (tx, ty) of a line from (fx, fy): two barbs 25 degrees off the
 * line, 8 pt + 2 widths long (PDWRT's recipe), as one open stroke */
static void xe_head(pdi tx, pdi ty, pdi fx, pdi fy, pdi lw16)
{
    pdi dx = (pdi)((tx >> 1) - (fx >> 1)), dy = (pdi)((ty >> 1) - (fy >> 1)), len = xd_hypot(dx, dy);
    pdi ux, uy, L = (pdi)(128 + 2 * lw16), a, b, c, d;
    if (!len) return;
    ux = xd_muldiv(dx, 1000, len); uy = xd_muldiv(dy, 1000, len);
    a = xd_muldiv(906, ux, 1000); b = xd_muldiv(423, uy, 1000);
    c = xd_muldiv(906, uy, 1000); d = xd_muldiv(423, ux, 1000);
    xe_pt((pdi)(tx - xd_muldiv(L, (pdi)(a - b), 1000)), (pdi)(ty - xd_muldiv(L, (pdi)(c + d), 1000)), 1);
    xe_pt(tx, ty, 0);
    xe_pt((pdi)(tx - xd_muldiv(L, (pdi)(a + b), 1000)), (pdi)(ty - xd_muldiv(L, (pdi)(c - d), 1000)), 0);
    xe_paint(0, 1, 0);
}
static void xe_line(void)
{
    pdi lw = g_xed.style ? g_xed.style : 1, x0 = xe_ept(0, 0), y0 = xe_ept(0, 1), x1 = xe_ept(1, 0), y1 = xe_ept(1, 1);
    if (g_xed.pts_n < 2) return;
    lw = (pdi)(lw << 4);
    xe_col(1, g_xed.colour); xe_lw(lw);                        /* butt caps: a plain rule stays a rule */
    xe_pt(x0, y0, 1); xe_pt(x1, y1, 0); xe_paint(0, 1, 0);
    if (g_xed.align & 1) xe_head(x0, y0, x1, y1, lw);
    if (g_xed.align & 2) xe_head(x1, y1, x0, y0, lw);
}

/* one record's marks; the kinds as pdra.c's annot_edits switches on them */
static void xe_rec(void)
{
    pdw k = g_xed.kind;
    pdi lw = g_xed.style ? g_xed.style : 1;
    lw = (pdi)(lw << 4);                                        /* a frame's border, pt16 */
    switch (k) {
    case PDA_NOTE:
        xe_col(0, g_xe.bpp == 1 ? 15 : g_xed.colour); xe_col(1, 0); xe_lw(13);
        xe_set(XF_JOIN, 1);
        xe_prog(xe_note, 40, g_xed.rect.x0, g_xed.rect.y0, (pdi)(g_xed.rect.x1 - g_xed.rect.x0), (pdi)(g_xed.rect.y1 - g_xed.rect.y0));
        break;
    case PDA_HIGHLIGHT: xe_highlight(); break;
    case PDA_TEXT: break;                                       /* the core draws the Text box */
    case PDA_INK: case PDA_SIGN: xe_ink(); break;
    case PDA_LINE: xe_line(); break;
    default:                                                    /* Area mark, Rect, Ellipse: inset by half the border */
        if (k == 0 || k > PDA_SIGN) break;
        xe_col(1, g_xed.colour); xe_lw(lw);
        if (k == PDA_AREA && (g_xed.align & 1)) xe_dash(12, 12);
        xe_prog(k == PDA_ELLIPSE ? xe_ell : xe_rect, k == PDA_ELLIPSE ? 1000 : 1,
                (pdi)(g_xed.rect.x0 + (lw >> 1)), (pdi)(g_xed.rect.y0 + (lw >> 1)),
                (pdi)(g_xed.rect.x1 - g_xed.rect.x0 - lw), (pdi)(g_xed.rect.y1 - g_xed.rect.y0 - lw));
        break;
    }
}

/* PDV_GFX_EDIT: BX:CX = the block. AX 0. */
static pdw xe_verb(pdw bx, pdw cx)
{
    xe_seg = bx; xe_off = cx;
    pd_fcopy_in(bx, cx, &g_xe, sizeof g_xe);
    pd_fcopy_in(g_xe.eseg, (pdw)(g_xe.rec * 48), &g_xed, 48);
    xe_cnt = 0; xe_wn = 0; xe_full = 0; g_xe.more = 0;
    xe_rec();
    if (g_xed.kind != PDA_INK && g_xed.kind != PDA_SIGN) g_xe.skip = xe_cnt;
    g_xe.n = xe_wn;
    pd_fcopy_out(&g_xe, bx, cx, sizeof g_xe);
    return 0;
}
