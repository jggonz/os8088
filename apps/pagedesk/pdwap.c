/* ============================================================================
 * os8088 - apps/pagedesk/pdwap.c      annotation dictionaries and appearance
 *                                     streams (PAGEDESK-SPEC 13.3, 20.5.4-5)
 *
 * #included by pdwrt.c. Everything is generated from one pd_edit record
 * (copied into g_ed) plus its text and points, which stay in the edit store's
 * far segment. Geometry is pt16 page space throughout; an appearance is drawn
 * in coordinates relative to the annotation's own /Rect lower-left, with a
 * /BBox of [0 0 W H] and no /Matrix (5.5), so the reader's 12.5.5 algorithm is
 * a pure translation.
 * ==========================================================================*/
#include "pdwwid.h"

/* ---- shared per-annotation state ---------------------------------------- */
static struct pd_edit g_ed;      /* the record being written                 */
static pdw  g_edseg;             /* the edit store's segment                 */
static char g_txt[PDW_TXTMAX + 2];
static pdw  g_tlen;
static pd_pt g_rx0, g_ry0, g_rx1, g_ry1;   /* the /Rect actually written      */
static pdw  g_nm_hi, g_nm_lo;    /* the document nonce (20.5.2)              */
static pdw  g_pgnum, g_pggen;    /* the page object the annotation belongs to */

static const char *const pw_pal[16] = {   /* the 16 VGA colours as PDF rgb     */
    "0 0 0", "0 0 0.667", "0 0.667 0", "0 0.667 0.667",
    "0.667 0 0", "0.667 0 0.667", "0.667 0.333 0", "0.667 0.667 0.667",
    "0.333 0.333 0.333", "0.333 0.333 1", "0.333 1 0.333", "0.333 1 1",
    "1 0.333 0.333", "1 0.333 1", "1 1 0.333", "1 1 1" };

static void pb_col(const char *op)       /* "<r g b> rg|RG" for g_ed.colour   */
{
    pb_s(pw_pal[g_ed.colour & 15]); pb_c(' '); pb_s(op); pb_c('\n');
}

/* ---- point heap access ---------------------------------------------------- */
static pd_pt pt_x(pdw i) { return (pd_pt)pd_fpeekw(g_edseg, (pdw)(g_ed.pts_off + 4u * i)); }
static pd_pt pt_y(pdw i) { return (pd_pt)pd_fpeekw(g_edseg, (pdw)(g_ed.pts_off + 4u * i + 2)); }
static pdw   pt_pen(pdw i) { return pt_x(i) == PDW_PENUP; }
static pd_pt lx(pd_pt v) { return (pd_pt)(v - g_rx0); }
static pd_pt ly(pd_pt v) { return (pd_pt)(v - g_ry0); }
static void pb_xy(pd_pt x, pd_pt y) { pb_pt(x); pb_c(' '); pb_pt(y); pb_c(' '); }
static void pb_lxy(pd_pt x, pd_pt y) { pb_xy(lx(x), ly(y)); }

/* ---- loading a record --------------------------------------------------- */
static void ed_head(pdw i)               /* the 48-byte record only          */
{
    pd_fcopy_in(g_edseg, (pdw)(i * sizeof(struct pd_edit)), &g_ed, sizeof(struct pd_edit));
}
static void ed_text(void)                /* ...and its text, capped          */
{
    g_tlen = g_ed.text_len;
    if (g_tlen > PDW_TXTMAX) g_tlen = PDW_TXTMAX;
    if (g_tlen) pd_fcopy_in(g_edseg, g_ed.text_off, g_txt, g_tlen);
    g_txt[g_tlen] = 0;
}
static void ed_load(pdw i) { ed_head(i); ed_text(); }

/* ---- the /Rect ---------------------------------------------------------- */
static void rect_norm(void)
{
    pd_pt t;
    g_rx0 = g_ed.rect.x0; g_ry0 = g_ed.rect.y0; g_rx1 = g_ed.rect.x1; g_ry1 = g_ed.rect.y1;
    if (g_rx0 > g_rx1) { t = g_rx0; g_rx0 = g_rx1; g_rx1 = t; }
    if (g_ry0 > g_ry1) { t = g_ry0; g_ry0 = g_ry1; g_ry1 = t; }
}
static void rect_pts(pdw first, pdw n, pd_pt pad)   /* bbox of points, grown by pad */
{
    pdw i, any = 0; pd_pt x, y;
    for (i = first; i < (pdw)(first + n); i++) {
        if (pt_pen(i)) continue;
        x = pt_x(i); y = pt_y(i);
        if (!any) { g_rx0 = g_rx1 = x; g_ry0 = g_ry1 = y; any = 1; continue; }
        if (x < g_rx0) g_rx0 = x; if (x > g_rx1) g_rx1 = x;
        if (y < g_ry0) g_ry0 = y; if (y > g_ry1) g_ry1 = y;
    }
    if (!any) { rect_norm(); return; }
    g_rx0 = (pd_pt)(g_rx0 - pad); g_ry0 = (pd_pt)(g_ry0 - pad);
    g_rx1 = (pd_pt)(g_rx1 + pad); g_ry1 = (pd_pt)(g_ry1 + pad);
}
#define NOTE_W16  320       /* 20 pt */
#define NOTE_H16  288       /* 18 pt */
#define ARROW16(w16) ((pdw)(128 + 2 * (w16)))   /* an open arrow's barb: 8 pt + 2 widths */

static pdw ed_w16(void)                 /* line width of a Line/Rect/Ellipse/Area, pt16 */
{
    pdw w = g_ed.style ? g_ed.style : 1;
    return (pdw)(w * 16);
}
static void ed_rect(void)
{
    switch (g_ed.kind) {
    case PDA_NOTE:
        g_rx0 = g_ed.rect.x0; g_ry0 = g_ed.rect.y0;          /* the click: only the origin counts */
        g_rx1 = (pd_pt)(g_rx0 + NOTE_W16); g_ry1 = (pd_pt)(g_ry0 + NOTE_H16);
        break;
    case PDA_HIGHLIGHT:
        rect_pts(0, g_ed.pts_n, 0); break;
    case PDA_INK: case PDA_SIGN:
        rect_pts(0, g_ed.pts_n, (pd_pt)(g_ed.size * 4 + 16)); break;
    case PDA_LINE:
        rect_pts(0, 2, (pd_pt)(ARROW16(ed_w16()) + ed_w16())); break;
    default:
        rect_norm(); break;
    }
    if (g_rx1 <= g_rx0) g_rx1 = (pd_pt)(g_rx0 + 16);
    if (g_ry1 <= g_ry0) g_ry1 = (pd_pt)(g_ry0 + 16);
}
static void pb_rectarr(void)
{
    pb_s("/Rect ["); pb_xy(g_rx0, g_ry0); pb_xy(g_rx1, g_ry1); pb_s("] ");
}

/* ---- the base-14 faces ---------------------------------------------------- */
static const char *const pw_base[12] = {
    "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique",
    "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic",
    "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique" };
static const char *const pw_resn[12] = {
    "Helv", "HeBo", "HeOb", "HeBO", "TiRo", "TiBo", "TiIt", "TiBI",
    "Cour", "CoBo", "CoOb", "CoBO" };
static pdw ft_face(void)
{
    pdw fam = (pdw)(g_ed.style >> 4);
    if (fam > 2) fam = 0;
    return (pdw)(fam * 4 + (g_ed.style & 3));
}
static pdw ft_cw(pdb c)                 /* advance, 1/1000 em, WinAnsi code */
{
    pdw f = ft_face();
    if (c < 32) c = 32;
    if (f >= 8) return 600;             /* Courier                          */
    switch (f) {
    case 0: case 2: return pdw_w_helv[c - 32];
    case 1: case 3: return pdw_w_helvb[c - 32];
    case 4: return pdw_w_tim[c - 32];
    case 5: return pdw_w_timb[c - 32];
    case 6: return pdw_w_timi[c - 32];
    }
    return pdw_w_timbi[c - 32];
}

/* ---- the wrapped FreeText (20.5.6) ---------------------------------------- */
#define FT_PAD16 32                     /* 2 pt inside the frame           */
static pdw ft_s16, ft_lead, ft_avail;
static struct pd_u32 ft_limit;
static pdi ft_y;
static pdw ft_emit;

static pdw em_to_pt16(const struct pd_u32 *em, pdw s16)   /* em/1000 * s16, exact enough */
{
    static struct pd_u32 q, t; pdw r, v;
    r = pdm_u32_divmod16(&q, em, 1000);
    pdm_u32_mul16(&t, r, s16);
    pdm_u32_divmod16(&t, &t, 1000);
    v = (pdw)(q.lo * s16 + t.lo);       /* q*s16 < 32768 by construction   */
    return v;
}
static void ft_line(pdw st, pdw n, const struct pd_u32 *w)
{
    static pdw x, lw;
    if (!ft_emit) return;
    lw = em_to_pt16(w, ft_s16);
    x = FT_PAD16;
    if (g_ed.align == 1 && ft_avail > lw)      x = (pdw)(FT_PAD16 + (ft_avail - lw) / 2);
    else if (g_ed.align == 2 && ft_avail > lw) x = (pdw)(FT_PAD16 + ft_avail - lw);
    if (n == 0) return;
    pb_s("1 0 0 1 "); pb_pt((pd_pt)x); pb_c(' '); pb_pt((pd_pt)ft_y); pb_s(" Tm ");
    pb_wintext(g_txt + st, n); pb_s(" Tj\n");
}
static void ft_run(void)
{
    static pdw i, j, st, brk, end, nxt, eop, cw;
    static struct pd_u32 w, bw, cwv;
    static pdb c;
    i = 0;
    while (i < g_tlen && ft_y >= 0 && !g_err) {
        st = i; brk = 0xFFFF; eop = 0; end = nxt = 0;
        pdm_u32_set(&w, 0, 0); pdm_u32_set(&bw, 0, 0);
        for (j = i; ; j++) {
            if (j >= g_tlen) { end = j; nxt = j; eop = 1; break; }
            c = (pdb)g_txt[j];
            if (c == '\n' || c == '\r') {
                end = j; nxt = (pdw)(j + 1); eop = 1;
                if (c == '\r' && nxt < g_tlen && g_txt[nxt] == '\n') nxt++;
                break;
            }
            cw = ft_cw(c);
            if (c == ' ') {
                pdm_u32_set(&cwv, 0, cw);
                pdm_u32_add(&cwv, &w, &cwv);
                if (pdm_u32_cmp(&cwv, &ft_limit) > 0) { end = j; nxt = (pdw)(j + 1); break; }
                brk = j; u32cp(&bw, &w);
            } else if (j > st) {
                pdm_u32_set(&cwv, 0, cw);
                pdm_u32_add(&cwv, &w, &cwv);
                if (pdm_u32_cmp(&cwv, &ft_limit) > 0) {
                    if (brk != 0xFFFF) { end = brk; u32cp(&w, &bw); nxt = (pdw)(brk + 1); }
                    else { end = j; nxt = j; }
                    break;
                }
            }
            pdm_u32_add16(&w, cw);
        }
        if (!eop) while (nxt < g_tlen && g_txt[nxt] == ' ') nxt++;
        ft_line(st, (pdw)(end - st), &w);
        ft_y = (pdi)(ft_y - (pdi)ft_lead);
        i = nxt;
    }
}
static void ft_setup(void)
{
    static struct pd_u32 a;
    pdw size = g_ed.size ? g_ed.size : 12;
    ft_s16 = (pdw)(size * 16);
    ft_lead = (pdw)(ft_s16 + ft_s16 / 5);
    ft_avail = (pdw)(g_rx1 - g_rx0);
    ft_avail = ft_avail > (pdw)(2 * FT_PAD16) ? (pdw)(ft_avail - 2 * FT_PAD16) : 0;
    pdm_u32_mul16(&a, ft_avail, 1000);
    pdm_u32_divmod16(&ft_limit, &a, ft_s16);
    ft_y = (pdi)((g_ry1 - g_ry0) - FT_PAD16 - (pdi)(ft_s16 - ft_s16 / 5));
}

/* ---- appearance-stream content, one routine a kind ------------------------ */
static void ap_note(void)
{
    /* a speech bubble in the annotation's colour with three text lines */
    pb_s("q\n"); pb_col("rg"); pb_s("0 0 0 RG 0.8 w 1 j\n");
    pb_s("3 17 m 17 17 l 18.5 17 19 16.5 19 15 c 19 7 l 19 5.5 18.5 5 17 5 c "
         "9 5 l 5 1 l 6 5 l 3 5 l 1.5 5 1 5.5 1 7 c 1 15 l 1 16.5 1.5 17 3 17 c h B\n");
    pb_s("4 13 m 16 13 l 4 10.5 m 16 10.5 l 4 8 m 11 8 l S\nQ\n");
}
static void ap_frame(void)                  /* Area mark, Rect, Ellipse */
{
    pd_pt w16 = (pd_pt)ed_w16(), h = (pd_pt)(w16 / 2);
    pd_pt W = (pd_pt)(g_rx1 - g_rx0), H = (pd_pt)(g_ry1 - g_ry0);
    pd_pt x0 = h, y0 = h, x1 = (pd_pt)(W - h), y1 = (pd_pt)(H - h);
    pd_pt cx, cy, rx, ry, kx, ky;
    static struct pd_u32 t;
    pb_s("q\n"); pb_col("RG"); pb_pt(w16); pb_s(" w\n");
    if (g_ed.kind == PDA_AREA && (g_ed.align & 1)) pb_s("[3 3] 0 d\n");
    if (g_ed.kind != PDA_ELLIPSE) {
        pb_xy(x0, y0); pb_pt((pd_pt)(x1 - x0)); pb_c(' '); pb_pt((pd_pt)(y1 - y0)); pb_s(" re S\n");
    } else {
        rx = (pd_pt)((x1 - x0) / 2); ry = (pd_pt)((y1 - y0) / 2);
        if (rx < 0) rx = 0;             /* a frame thinner than its border */
        if (ry < 0) ry = 0;
        cx = (pd_pt)(x0 + rx); cy = (pd_pt)(y0 + ry);
        pdm_u32_mul16(&t, (pdw)rx, 5523); pdm_u32_divmod16(&t, &t, 10000); kx = (pd_pt)t.lo;
        pdm_u32_mul16(&t, (pdw)ry, 5523); pdm_u32_divmod16(&t, &t, 10000); ky = (pd_pt)t.lo;
        pb_xy((pd_pt)(cx + rx), cy); pb_s("m\n");
        pb_xy((pd_pt)(cx + rx), (pd_pt)(cy + ky)); pb_xy((pd_pt)(cx + kx), (pd_pt)(cy + ry)); pb_xy(cx, (pd_pt)(cy + ry)); pb_s("c\n");
        pb_xy((pd_pt)(cx - kx), (pd_pt)(cy + ry)); pb_xy((pd_pt)(cx - rx), (pd_pt)(cy + ky)); pb_xy((pd_pt)(cx - rx), cy); pb_s("c\n");
        pb_xy((pd_pt)(cx - rx), (pd_pt)(cy - ky)); pb_xy((pd_pt)(cx - kx), (pd_pt)(cy - ry)); pb_xy(cx, (pd_pt)(cy - ry)); pb_s("c\n");
        pb_xy((pd_pt)(cx + kx), (pd_pt)(cy - ry)); pb_xy((pd_pt)(cx + rx), (pd_pt)(cy - ky)); pb_xy((pd_pt)(cx + rx), cy); pb_s("c h S\n");
    }
    pb_s("Q\n");
}
static void ap_text(void)
{
    if (!g_tlen) return;
    ft_setup(); ft_emit = 1;
    pb_s("BT\n"); pb_name(pw_resn[ft_face()]); pb_c(' '); pb_u((pdw)(ft_s16 / 16)); pb_s(" Tf\n");
    pb_col("rg");
    ft_run();
    pb_s("ET\n");
}
/* integer square root of a pd_u32 (digit by digit) */
static pdw isqrt32(const struct pd_u32 *n)
{
    static struct pd_u32 r, t;
    static pdw res; pdw i;
    res = 0;
    u32cp(&r, n);
    for (i = 15; ; i--) {                       /* try each result bit, high first */
        pdm_u32_mul16(&t, (pdw)(res | (1u << i)), (pdw)(res | (1u << i)));
        if (pdm_u32_cmp(&t, &r) <= 0) res |= (pdw)(1u << i);
        if (i == 0) break;
    }
    return res;
}
/* one open-arrow head at (tx,ty) pointing along the unit vector (ux,uy) */
static struct pd_fx fx_c = {59392u, 0}, fx_s = {27696, 0};
static void arrow_head(pd_pt tx, pd_pt ty, const struct pd_fx *ux, const struct pd_fx *uy, pdw len16)
{
    static struct pd_fx L, a, b, c, d, v1x, v1y, v2x, v2y;
    pd_pt b1x, b1y, b2x, b2y;
    pdm_fx_int(&L, (pdi)len16);
    pdm_fx_mul(&a, ux, &fx_c); pdm_fx_mul(&b, uy, &fx_s);        /* ux c, uy s */
    pdm_fx_mul(&c, uy, &fx_c); pdm_fx_mul(&d, ux, &fx_s);        /* uy c, ux s */
    pdm_fx_sub(&v1x, &a, &b); pdm_fx_add(&v1y, &c, &d);          /* rot +theta */
    pdm_fx_add(&v2x, &a, &b); pdm_fx_sub(&v2y, &c, &d);          /* rot -theta */
    pdm_fx_mul(&v1x, &v1x, &L); pdm_fx_mul(&v1y, &v1y, &L);
    pdm_fx_mul(&v2x, &v2x, &L); pdm_fx_mul(&v2y, &v2y, &L);
    b1x = (pd_pt)(tx - pdm_fx_round(&v1x)); b1y = (pd_pt)(ty - pdm_fx_round(&v1y));
    b2x = (pd_pt)(tx - pdm_fx_round(&v2x)); b2y = (pd_pt)(ty - pdm_fx_round(&v2y));
    pb_lxy(b1x, b1y); pb_s("m "); pb_lxy(tx, ty); pb_s("l "); pb_lxy(b2x, b2y); pb_s("l S\n");
}
static void ap_line(void)
{
    static struct pd_u32 s1, s2;
    static struct pd_fx ux, uy, nx, ny, fl;
    pd_pt x1 = pt_x(0), y1 = pt_y(0), x2 = pt_x(1), y2 = pt_y(1);
    pdi dx = (pdi)((x2 >> 2) - (x1 >> 2)), dy = (pdi)((y2 >> 2) - (y1 >> 2));
    pdw len;
    pdw w16 = ed_w16();
    pb_s("q\n"); pb_col("RG"); pb_pt((pd_pt)w16); pb_s(" w 1 J 1 j\n");
    pb_lxy(x1, y1); pb_s("m "); pb_lxy(x2, y2); pb_s("l S\n");
    pdm_u32_mul16(&s1, (pdw)(dx < 0 ? -dx : dx), (pdw)(dx < 0 ? -dx : dx));
    pdm_u32_mul16(&s2, (pdw)(dy < 0 ? -dy : dy), (pdw)(dy < 0 ? -dy : dy));
    pdm_u32_add(&s1, &s1, &s2);
    len = isqrt32(&s1);
    if (len && (g_ed.align & 3)) {
        pdm_fx_int(&fl, (pdi)len); pdm_fx_int(&nx, dx); pdm_fx_int(&ny, dy);
        pdm_fx_div(&ux, &nx, &fl); pdm_fx_div(&uy, &ny, &fl);
        /* PAGEDESK-SPEC 13.1.1: align bit 0 = an arrow at the START (point 0),
           bit 1 = at the END (point 1) - the order of /LE */
        if (g_ed.align & 2) arrow_head(x2, y2, &ux, &uy, ARROW16(w16));
        if (g_ed.align & 1) { pdm_fx_neg(&nx, &ux); pdm_fx_neg(&ny, &uy); arrow_head(x1, y1, &nx, &ny, ARROW16(w16)); }
    }
    pb_s("Q\n");
}
/* ---- BUDGETED LOOPS (20.5.5) ---------------------------------------------
 * The four emitters that walk the point heap - the highlight's QuadPoints and
 * appearance, the ink's InkList and appearance - run a bounded number of
 * points a call and are re-entered with their state in bl_* , so no slice
 * writes more than about 1.5 KB and a 500-point stroke is a handful of slices
 * rather than one long one (15.2). */
#define BL_PTS 96
static pdw bl_i, bl_cnt, bl_open;
static pd_pt bl_lx, bl_ly;
static void bl_reset(void) { bl_i = 0; bl_cnt = 0; bl_open = 0; }

/* /QuadPoints: every point of the highlight, "x y " each */
static pdw list_quads(void)
{
    pdw n = (pdw)(g_ed.pts_n & ~3u), k = 0;
    while (bl_i < n && k < BL_PTS) { pb_xy(pt_x(bl_i), pt_y(bl_i)); bl_i++; k++; }
    return bl_i >= n;
}
/* /InkList: [x y x y ...] per stroke, the pen-up marker closing one */
static pdw list_ink(void)
{
    pdw k = 0;
    while (bl_i < g_ed.pts_n && k < BL_PTS) {
        if (pt_pen(bl_i)) { if (bl_open) { pb_s("] "); bl_open = 0; } }
        else {
            if (!bl_open) { pb_c('['); bl_open = 1; }
            pb_xy(pt_x(bl_i), pt_y(bl_i));
        }
        bl_i++; k++;
    }
    if (bl_i >= g_ed.pts_n) { if (bl_open) { pb_s("] "); bl_open = 0; } return 1; }
    return 0;
}
static pdw ak_list(void) { return g_ed.kind == PDA_HIGHLIGHT ? list_quads() : list_ink(); }

/* the appearance of a highlight: filled quads, UL UR LR LL, multiplied */
static pdw ap_hl_step(void)
{
    pdw nq = (pdw)(g_ed.pts_n / 4), b, k = 0;
    if (bl_i == 0 && !bl_open) { pb_s("/GS gs\n"); pb_col("rg"); bl_open = 1; }
    while (bl_i < nq && k < BL_PTS / 4) {
        b = (pdw)(bl_i * 4);
        pb_lxy(pt_x(b), pt_y(b)); pb_s("m ");
        pb_lxy(pt_x((pdw)(b + 1)), pt_y((pdw)(b + 1))); pb_s("l ");
        pb_lxy(pt_x((pdw)(b + 3)), pt_y((pdw)(b + 3))); pb_s("l ");
        pb_lxy(pt_x((pdw)(b + 2)), pt_y((pdw)(b + 2))); pb_s("l h f\n");
        bl_i++; k++;
    }
    return bl_i >= nq;
}
/* the appearance of ink: round-capped, round-joined polylines, a lone point a dot */
static pdw ap_ink_step(void)
{
    pdw k = 0;
    if (bl_i == 0 && !bl_open) { pb_s("q\n"); pb_col("RG"); pb_pt((pd_pt)(g_ed.size * 8)); pb_s(" w 1 J 1 j\n"); bl_open = 1; }
    while (bl_i <= g_ed.pts_n && k < BL_PTS) {
        if (bl_i == g_ed.pts_n || pt_pen(bl_i)) {            /* end of a stroke */
            if (bl_cnt == 1) { pb_lxy(bl_lx, bl_ly); pb_s("l "); }   /* a dot   */
            if (bl_cnt) pb_s("S\n");
            bl_cnt = 0;
        } else {
            bl_lx = pt_x(bl_i); bl_ly = pt_y(bl_i);
            pb_lxy(bl_lx, bl_ly); pb_s(bl_cnt ? "l " : "m "); bl_cnt++;
            if ((bl_cnt & 7) == 0) pb_c('\n');
        }
        bl_i++; k++;
    }
    if (bl_i > g_ed.pts_n) { pb_s("Q\n"); return 1; }
    return 0;
}
/* one step of any appearance's content; 1 = complete. Kinds without a point
   loop are one step. bl_reset() before the first call of a pass. */
static pdw ap_step(void)
{
    if (g_ed.kind == PDA_HIGHLIGHT) return ap_hl_step();
    if (g_ed.kind == PDA_INK || g_ed.kind == PDA_SIGN) return ap_ink_step();
    switch (g_ed.kind) {
    case PDA_NOTE:      ap_note(); break;
    case PDA_AREA: case PDA_RECT: case PDA_ELLIPSE: ap_frame(); break;
    case PDA_TEXT:      ap_text(); break;
    case PDA_LINE:      ap_line(); break;
    }
    return 1;
}

/* ---- the appearance stream object ----------------------------------------- */
static struct pd_u32 ap_len;            /* the counted length of the stream's bytes */
static void ap_resources(void)
{
    pb_s("/Resources <<");
    if (g_ed.kind == PDA_HIGHLIGHT) pb_s(" /ExtGState << /GS << /Type /ExtGState /BM /Multiply >> >>");
    if (g_ed.kind == PDA_TEXT && g_tlen) {
        pb_s(" /Font << "); pb_name(pw_resn[ft_face()]);
        pb_s(" << /Type /Font /Subtype /Type1 /BaseFont "); pb_name(pw_base[ft_face()]);
        pb_s(" /Encoding /WinAnsiEncoding >> >>");
    }
    pb_s(" >> ");
}
static void ap_head(pdw num)
{
    pw_obj(num, 0);
    pb_s("<< /Type /XObject /Subtype /Form /FormType 1 /BBox [0 0 ");
    pb_pt((pd_pt)(g_rx1 - g_rx0)); pb_c(' '); pb_pt((pd_pt)(g_ry1 - g_ry0)); pb_s("] ");
    ap_resources();
    pb_s("/Length "); pb_u32(&ap_len, 0); pb_s(" >>\nstream\n");
}
static void ap_tail(void) { pb_s("\nendstream\n"); pw_endobj(); }

/* ---- the annotation dictionary ------------------------------------------- */
/* A REPLACED source annotation keeps the keys that are its identity and its
 * history rather than the editor's (20.5.5): /NM (13.2), /T (whose note it is -
 * the current author is NOT written over someone else's), /Popup and
 * /CreationDate, copied verbatim as "/Key value " out of the source object by
 * an_prep. g_keepf says which arrived: KEEP_NM, KEEP_T. */
#define KEEP_NM 1
#define KEEP_T  2
static char g_keep[200];
static pdw  g_keepn, g_keepf;
static void pb_nm(void)
{
    if (g_keepf & KEEP_NM) return;          /* in g_keep, written by pb_common */
    pb_s("/NM (PD-"); pb_hex4(g_nm_hi); pb_hex4(g_nm_lo); pb_c('-'); pb_hex4(g_ed.id); pb_s(") ");
}
static void pb_common(pdw f)
{
    pb_nm();
    if (g_keepn) pb_n(g_keep, g_keepn);
    pb_s("/F "); pb_u(f); pb_s(" /P "); pb_u(g_pgnum); pb_c(' '); pb_u(g_pggen); pb_s(" R ");
    if (pdw_date[0]) { pb_s("/M ("); pb_n(pdw_date, pw_len(pdw_date)); pb_s(") "); }
}
static void pb_cref(void)               /* /C [r g b] */
{
    pb_s("/C ["); pb_s(pw_pal[g_ed.colour & 15]); pb_s("] ");
}
static void pb_bs(pdw w16, pdw dashed)
{
    pb_s("/BS << /W "); pb_pt((pd_pt)w16);
    if (dashed) pb_s(" /S /D /D [3 3]"); else pb_s(" /S /S");
    pb_s(" >> ");
}
static void pb_contents(void) { pb_s("/Contents "); pb_textstr(g_txt, g_tlen); pb_c(' '); }
static void pb_author(void)
{
    if (g_keepf & KEEP_T) return;           /* the source's own /T, in g_keep */
    if (pdw_author[0]) { pb_s("/T "); pb_textstr(pdw_author, pw_len(pdw_author)); pb_c(' '); }
}

/* the end of every ordinary annotation: the common keys, /AP, endobj */
static void annot_z(pdw apnum)
{
    pb_common(4);
    pb_s("/AP << /N "); pb_u(apnum); pb_s(" 0 R >> >>\n"); pw_endobj();
}
/* the object header and the dictionary up to the point list's opening "[" for
 * a kind that has one, else the whole object. Returns 1 when more follows
 * (the list, then annot_c). A hidden review reply is whole and has no /AP. */
static pdw annot_a(pdw num, pdw gen, pdw apnum, pdw parent_n, pdw parent_g)
{
    if (g_ed.kind == PDA_NOTE && g_ed.irt) {
        pw_obj(num, gen);
        pb_s("<< /Type /Annot /Subtype /Text "); pb_rectarr();
        pb_s("/IRT "); pb_u(parent_n); pb_c(' '); pb_u(parent_g); pb_s(" R /StateModel /Review /State ");
        pb_s(g_ed.state == PDS_COMPLETED ? "/Completed " : "/None ");
        pb_author(); pb_common(32);
        pb_s(">>\n"); pw_endobj();
        return 0;
    }
    pw_obj(num, gen);
    pb_s("<< /Type /Annot ");
    switch (g_ed.kind) {
    case PDA_NOTE:
        pb_s("/Subtype /Text "); pb_rectarr(); pb_s("/Name /Comment /Open false ");
        pb_contents(); pb_author(); pb_cref(); break;
    case PDA_HIGHLIGHT:
        pb_s("/Subtype /Highlight "); pb_rectarr(); pb_s("/QuadPoints [");
        return 1;
    case PDA_AREA:
        pb_s("/Subtype /Square "); pb_rectarr(); pb_cref(); pb_bs(ed_w16(), g_ed.align & 1);
        if (g_tlen) pb_contents();
        pb_s("/Subj (Area mark) "); break;
    case PDA_RECT:
        pb_s("/Subtype /Square "); pb_rectarr(); pb_cref(); pb_bs(ed_w16(), 0); break;
    case PDA_ELLIPSE:
        pb_s("/Subtype /Circle "); pb_rectarr(); pb_cref(); pb_bs(ed_w16(), 0); break;
    case PDA_TEXT:
        pb_s("/Subtype /FreeText "); pb_rectarr(); pb_contents(); pb_s("/DA (/");
        pb_s(pw_resn[ft_face()]); pb_c(' '); pb_u(g_ed.size ? g_ed.size : 12); pb_c(' ');
        pb_s("Tf "); pb_s(pw_pal[g_ed.colour & 15]); pb_s(" rg) /Q "); pb_u(g_ed.align > 2 ? 0 : g_ed.align);
        pb_s(" /Border [0 0 0] "); break;
    case PDA_INK: case PDA_SIGN:
        pb_s("/Subtype /Ink "); pb_rectarr(); pb_cref(); pb_bs((pdw)(g_ed.size * 8), 0);
        pb_s("/InkList [");
        return 1;
    case PDA_LINE:
        pb_s("/Subtype /Line "); pb_rectarr(); pb_cref(); pb_bs(ed_w16(), 0);
        pb_s("/L ["); pb_xy(pt_x(0), pt_y(0)); pb_xy(pt_x(1), pt_y(1)); pb_s("] /LE [");
        pb_s((g_ed.align & 1) ? "/OpenArrow " : "/None "); pb_s((g_ed.align & 2) ? "/OpenArrow" : "/None");
        pb_s("] "); break;
    }
    annot_z(apnum);
    return 0;
}
/* after the point list */
static void annot_c(pdw apnum)
{
    if (g_ed.kind == PDA_HIGHLIGHT) {
        pb_s("] "); pb_cref();
        if (g_tlen) pb_contents();
    } else {
        pb_s("] ");
        if (g_ed.kind == PDA_SIGN) pb_s("/Subj (Drawn signature) ");
    }
    annot_z(apnum);
}
