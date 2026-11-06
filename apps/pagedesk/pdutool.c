/* ============================================================================
 * os8088 - apps/pagedesk/pdutool.c   THE ANNOTATION TOOLS (PAGEDESK-SPEC 17.2,
 *                                    20.8; PD-12..PD-16)
 *
 * Every function here is an ovl_* and rides the overlay (SPEC.md 73.14): a
 * tool is a gesture on the page, a menu-class command, never the keystroke
 * path. What stays resident is the data (the statics below) and the typing
 * and the selection outline in pduedit.c.
 *
 * A gesture is MODAL by nature - press, drag, release - and this OS delivers
 * a press and a release but no motion (SPEC.md 13.7), so it polls
 * os88_mouse() and yields every pass, exactly as CWORD's menu and ruler do
 * (cwcmd.c's cw_track_menu): the gfx lock is held for as long as the button
 * is down, which is what a rubber band means, and a guard bounds a mouse that
 * never lets go. The only pixels a gesture draws are an XOR band that it
 * takes off again; the annotation itself is PDREND's, drawn from the edit
 * store once the record exists (D15).
 *
 * Geometry is converted ONCE, at the release: the band is in screen pixels,
 * the record in page space (5.1) through pdm_view_inv, so zoom, scroll and
 * rotation never touch a stored byte.
 * ==========================================================================*/

#define OT_BAND   0                     /* a rectangle band */
#define OT_LINE   1                     /* a line band */
#define OT_INK    2                     /* a freehand stroke */
#define OT_MOVE   3                     /* the selection follows the pointer */
#define OT_SIZE   4                     /* a corner follows it */

#define OT_NPTS   128                   /* points in one stroke / signature */
#define OT_TOL    4                     /* pixels: a click's reach */
#define OT_DRAG   3                     /* pixels before a click is a drag */

static struct os88_mouse ot_m;
static struct os88_rect ot_r;           /* the band, screen */
static struct pd_rect ot_pr;            /* ...and as a page rect */
static struct pd_edit ot_e;
static int ot_sx, ot_sy;                /* the press */
static int ot_ax, ot_ay;                /* the band's fixed corner / line start */
static int ot_cx, ot_cy;                /* the pointer */
static struct os88_rect ot_o;           /* the outline a move carries */
static int ot_moved;
static pd_px ot_px, ot_py;
static pd_pt ot_pts[OT_NPTS * 2];       /* ink / signature / highlight quads */
static int ot_last;                     /* the last record clicked */
static unsigned ot_tick;

/* default colours, by kind (PDC_*) */
static const char ot_col[10] = { 0, PDC_YELLOW, PDC_YELLOW, PDC_LRED, PDC_BLUE,
                                 PDC_RED, PDC_RED, PDC_RED, PDC_RED,
                                 PDC_BLACK };
static const char ot_lab[10] = { 0, UL_NOTE, UL_HILITE, UL_MARK, UL_TEXT,
                                 UL_INK, UL_LINE, UL_RECT, UL_ELLIPSE,
                                 UL_SIGN };

static int ot_abs(int v) { return v < 0 ? -v : v; }

/* ---- XOR drawing, cut to the canvas (the selection's code, not remembered) */
static void ovl_xrun(int x1, int y1, int x2, int y2)
{
    ed_xrem = 0;
    ovl_ed_xf(x1, y1, x2, y2);
}

static void ovl_xframe(struct os88_rect *r)
{
    ed_xrem = 0;
    ovl_ed_frame(r, 0);
}

/* a line as runs along its major axis: calls = the minor extent + 1, so an
 * 800-pixel line that is nearly flat costs a handful of fills, not 800 */
static void ovl_xline(int x0, int y0, int x1, int y1)
{
    int dx, dy, sx, sy, err, a, b, run, steep, t;

    dx = ot_abs(x1 - x0);
    dy = ot_abs(y1 - y0);
    steep = dy > dx;
    if (steep) {
        t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
        t = dx; dx = dy; dy = t;
    }
    sx = x1 >= x0 ? 1 : -1;
    sy = y1 >= y0 ? 1 : -1;
    err = dx >> 1;
    a = x0;                             /* the run's start on the major axis */
    b = y0;                             /* ...and its minor coordinate */
    run = x0;
    while (run != x1) {
        run += sx;
        err -= dy;
        if (err < 0) {
            err += dx;
            if (steep) ovl_xrun(b, a, b, run - sx);
            else       ovl_xrun(a, b, run - sx, b);
            a = run;
            b += sy;
        }
    }
    if (steep) ovl_xrun(b, a, b, run);
    else       ovl_xrun(a, b, run, b);
}

/* the band for a gesture mode at the pointer (draw it, draw it again: gone) */
static void ovl_shape(int mode)
{
    if (mode == OT_LINE)
        ovl_xline(ot_ax, ot_ay, ot_cx, ot_cy);
    else
        ovl_xframe(&ot_r);
}

/* ---- page <-> screen ------------------------------------------------------ */
/* the page rect of a screen rect (pixel edges, so x2 + 1), clamped to the
 * page's box */
static void ovl_prect(const struct os88_rect *s, struct pd_rect *p)
{
    int k;
    pd_pt x, y;
    p->x0 = p->y0 = 32767;
    p->x1 = p->y1 = -32767;
    for (k = 0; k < 2; k++) {           /* opposite corners stay opposite */
        pdm_view_inv(&ot_px, &ot_py, &pdu_view, k ? s->x2 + 1 : s->x1,
                     k ? s->y2 + 1 : s->y1);
        x = ot_px;
        y = ot_py;
        if (x < p->x0) p->x0 = x;
        if (x > p->x1) p->x1 = x;
        if (y < p->y0) p->y0 = y;
        if (y > p->y1) p->y1 = y;
    }
    if (p->x0 < pdu_view.box.x0) p->x0 = pdu_view.box.x0;
    if (p->y0 < pdu_view.box.y0) p->y0 = pdu_view.box.y0;
    if (p->x1 > pdu_view.box.x1) p->x1 = pdu_view.box.x1;
    if (p->y1 > pdu_view.box.y1) p->y1 = pdu_view.box.y1;
}

static int ot_qx, ot_qy;                /* ovl_p2s's answer */
static void ovl_p2s(pd_pt x, pd_pt y)
{
    pdm_view_fwd(&ot_px, &ot_py, &pdu_view, x, y);
    ot_qx = ot_px;
    ot_qy = ot_py;
}

/* ---- the pointer loop ------------------------------------------------------- */
static void ovl_clamp(void)
{
    struct os88_rect *c;
    c = &pdu_L.canvas;
    if (ot_cx < c->x1) ot_cx = c->x1;
    if (ot_cx > c->x2) ot_cx = c->x2;
    if (ot_cy < c->y1) ot_cy = c->y1;
    if (ot_cy > c->y2) ot_cy = c->y2;
}

/* one stroke is captured in SCREEN pixels at ot_pts[ot_from..], thinned as it
 * goes (a point only when the pointer is 3 pixels from the last kept), drawn
 * as it grows, and made page space by ovl_ink_done */
static int ot_k;                        /* points in the stroke so far */
static int ot_from;                     /* ...and where they start */

static void ovl_ink_add(int x, int y)
{
    int i;
    i = (ot_from + ot_k) << 1;
    if (ot_k > 0 && ot_abs(x - ot_pts[i - 2]) < 3
        && ot_abs(y - ot_pts[i - 1]) < 3)
        return;
    if (ot_from + ot_k >= OT_NPTS - 2)
        return;                         /* the documented stroke limit */
    ot_pts[i] = x;
    ot_pts[i + 1] = y;
    if (ot_k > 0)
        ovl_xline(ot_pts[i - 2], ot_pts[i - 1], x, y);
    else
        ovl_xrun(x, y, x, y);
    ot_k++;
}

/* the stroke becomes page space (it was thinned as it was drawn) */
static void ovl_ink_done(void)
{
    int k;
    for (k = ot_from; k < ot_from + ot_k; k++) {
        pdm_view_inv(&ot_px, &ot_py, &pdu_view, ot_pts[k << 1],
                     ot_pts[(k << 1) + 1]);
        ot_pts[k << 1] = ot_px;
        ot_pts[(k << 1) + 1] = ot_py;
    }
}

/* the band for the pointer where it is */
static void ovl_band(int mode)
{
    int dx, dy;
    if (mode == OT_MOVE) {
        dx = ot_cx - ot_sx;
        dy = ot_cy - ot_sy;
        ot_r.x1 = ot_o.x1 + dx;
        ot_r.x2 = ot_o.x2 + dx;
        ot_r.y1 = ot_o.y1 + dy;
        ot_r.y2 = ot_o.y2 + dy;
        return;
    }
    ot_r.x1 = ot_ax < ot_cx ? ot_ax : ot_cx;
    ot_r.x2 = ot_ax < ot_cx ? ot_cx : ot_ax;
    ot_r.y1 = ot_ay < ot_cy ? ot_ay : ot_cy;
    ot_r.y2 = ot_ay < ot_cy ? ot_cy : ot_ay;
}

/* poll until the button comes up, drawing the band for `mode`; it is taken
 * off again before this returns. ot_moved: the pointer travelled OT_DRAG. */
static void ovl_loop(int mode)
{
    int guard, have;
    ot_cx = ot_sx;
    ot_cy = ot_sy;
    ot_moved = 0;
    have = 0;
    guard = 0;
    for (;;) {
        os88_mouse(&ot_m);
        if ((ot_m.btn & 1) == 0)
            break;
        if ((ot_m.x != ot_cx || ot_m.y != ot_cy)
            && (ot_moved || ot_abs(ot_m.x - ot_sx) >= OT_DRAG
                || ot_abs(ot_m.y - ot_sy) >= OT_DRAG)) {
            if (have)
                ovl_shape(mode);
            ot_cx = ot_m.x;
            ot_cy = ot_m.y;
            ovl_clamp();
            if (mode == OT_INK) {
                if (!ot_moved)
                    ovl_ink_add(ot_sx, ot_sy);
                ovl_ink_add(ot_cx, ot_cy);
                have = 0;
            } else {
                ovl_band(mode);
                ovl_shape(mode);
                have = 1;
            }
            ot_moved = 1;
        }
        os88_task_yield();
        if (++guard > 30000)
            break;
    }
    if (have)
        ovl_shape(mode);
}

/* ---- making records ---------------------------------------------------------- */
static void ovl_full(void)
{
    pdu_tell(pdu_ed_err == PDE_LIMIT
             ? "The change limit (128) is reached."
             : "The edit store is full.");
}

/* a new record of `kind` over page rect ot_pr (+ points); defaults per kind.
 * The index, or -1 having said why. */
static int ovl_make(int kind, const pd_pt *pts, unsigned n)
{
    int i;
    os88_memset(&ot_e, 0, sizeof(ot_e));
    ot_e.kind = kind;
    ot_e.page = pdu_ed_pg();
    os88_memcpy(&ot_e.rect, &ot_pr, sizeof(ot_pr));
    ot_e.colour = ot_col[kind];
    if (kind == PDA_AREA || kind == PDA_RECT || kind == PDA_ELLIPSE
        || kind == PDA_LINE)
        ot_e.style = 1;                 /* border / line width, points */
    if (kind == PDA_LINE)
        ot_e.align = 2;                 /* an arrow at the end (13.1.1) */
    if (kind == PDA_INK)
        ot_e.size = 4;                  /* 2 pt, in half points */
    if (kind == PDA_SIGN)
        ot_e.size = 3;
    if (kind == PDA_TEXT)
        ot_e.size = 12;                 /* Helvetica 12, left */
    pdu_undo_begin(ot_lab[kind]);
    i = pdu_ed_new(&ot_e, "", 0, pts, n);
    pdu_undo_end();
    if (i < 0)
        ovl_full();
    return i;
}

/* slide ot_pr inside the page's box without resizing it */
static void ovl_fit(void)
{
    struct pd_rect *b;
    int d;
    b = &pdu_view.box;
    if (ot_pr.x1 > b->x1) { d = ot_pr.x1 - b->x1; ot_pr.x0 -= d; ot_pr.x1 -= d; }
    if (ot_pr.x0 < b->x0) { d = b->x0 - ot_pr.x0; ot_pr.x0 += d; ot_pr.x1 += d; }
    if (ot_pr.y1 > b->y1) { d = ot_pr.y1 - b->y1; ot_pr.y0 -= d; ot_pr.y1 -= d; }
    if (ot_pr.y0 < b->y0) { d = b->y0 - ot_pr.y0; ot_pr.y0 += d; ot_pr.y1 += d; }
}

/* a w x h POINT box with its top-left at the pixel, as a screen rect */
static void ovl_box_at(int sx, int sy, int wpt, int hpt)
{
    ot_r.x1 = sx;
    ot_r.y1 = sy;
    ot_r.x2 = sx + pdu_muldiv(wpt, pdu_view.zoom, 100) - 1;
    ot_r.y2 = sy + pdu_muldiv(pdu_muldiv(hpt, pdu_view.zoom, 100),
                              pdu_view.asp_num, pdu_view.asp_den) - 1;
}

/* ---- hit testing (screen space, topmost first) -------------------------------- */
static int ovl_in(const struct os88_rect *r, int x, int y, int pad)
{
    return x >= r->x1 - pad && x <= r->x2 + pad && y >= r->y1 - pad
        && y <= r->y2 + pad;
}

/* does record ed_w contain the pixel? */
static int ovl_test(int px, int py)
{
    int kind;

    kind = ed_w.kind;
    ovl_ed_scr(&ed_w.rect, 0, &ot_r);
    if (kind == PDA_NOTE || kind == PDA_TEXT || kind == PDA_HIGHLIGHT)
        return ovl_in(&ot_r, px, py, 2);
    if (kind == PDA_RECT || kind == PDA_ELLIPSE || kind == PDA_AREA) {
        if (!ovl_in(&ot_r, px, py, OT_TOL))
            return 0;
        if (ot_r.x2 - ot_r.x1 < 3 * OT_TOL || ot_r.y2 - ot_r.y1 < 3 * OT_TOL)
            return 1;
        return !(px > ot_r.x1 + OT_TOL && px < ot_r.x2 - OT_TOL
                 && py > ot_r.y1 + OT_TOL && py < ot_r.y2 - OT_TOL);
    }
    return ovl_in(&ot_r, px, py, OT_TOL);   /* Ink, Sign, Line: the box */
}

static int ovl_hit(int px, int py)
{
    int i;
    for (i = (int)pdu_ed_n - 1; i >= 0; i--) {
        if (ovl_ed_rd(i, &ed_w) || (ed_w.flags & PDEF_DELETED)
            || ed_w.page != pdu_ed_pg())
            continue;
        if (ovl_test(px, py))
            return i;
    }
    return -1;
}

/* which corner (UL UR LL LR) of the selected record is at the pixel? -1 none */
static int ovl_handle(int px, int py)
{
    int k, hx, hy;
    if (pdu_ed_sel < 0 || ovl_ed_rd(pdu_ed_sel, &ed_w))
        return -1;
    if (ed_w.kind != PDA_TEXT && ed_w.kind != PDA_RECT
        && ed_w.kind != PDA_ELLIPSE && ed_w.kind != PDA_AREA)
        return -1;
    ovl_ed_scr(&ed_w.rect, 3, &ot_r);
    for (k = 0; k < 4; k++) {
        hx = (k & 1) ? ot_r.x2 : ot_r.x1;
        hy = (k & 2) ? ot_r.y2 : ot_r.y1;
        if (ot_abs(px - hx) <= 5 && ot_abs(py - hy) <= 5)
            return k;
    }
    return -1;
}

/* ---- the Select tool: pick, move, resize ---------------------------------------- */
/* move the selection: the band is its outline carried by the pointer, the
 * record moves by the page-space difference between the press and the
 * release (so the translation is exact pt16 under any zoom or rotation) */
static void ovl_move(void)
{
    int i, dx, dy;
    pd_pt x0, y0;
    struct pd_rect *r;

    i = pdu_ed_sel;
    if (ovl_ed_rd(i, &ed_w))
        return;
    ovl_ed_scr(&ed_w.rect, 3, &ot_o);
    pdu_ed_xoff_l();
    ovl_loop(OT_MOVE);
    if (!ot_moved)
        return;
    pdm_view_inv(&ot_px, &ot_py, &pdu_view, ot_sx, ot_sy);
    x0 = ot_px;
    y0 = ot_py;
    pdm_view_inv(&ot_px, &ot_py, &pdu_view, ot_cx, ot_cy);
    dx = (pd_pt)(ot_px - x0);
    dy = (pd_pt)(ot_py - y0);
    r = &pdu_view.box;                  /* stay on the page */
    if (dx > r->x1 - ed_w.rect.x1) dx = r->x1 - ed_w.rect.x1;
    if (dx < r->x0 - ed_w.rect.x0) dx = r->x0 - ed_w.rect.x0;
    if (dy > r->y1 - ed_w.rect.y1) dy = r->y1 - ed_w.rect.y1;
    if (dy < r->y0 - ed_w.rect.y0) dy = r->y0 - ed_w.rect.y0;
    if (dx == 0 && dy == 0)
        return;
    pdu_undo_begin(UL_MOVE);
    pdu_ed_move(i, dx, dy);
    pdu_undo_end();
}

/* drag corner h of the selection; the opposite corner stays */
static void ovl_resize(int h)
{
    int i, fx, fy;
    pd_pt v[4];

    i = pdu_ed_sel;
    if (ovl_ed_rd(i, &ed_w))
        return;
    pdu_ed_xoff_l();
    ovl_ed_scr(&ed_w.rect, 3, &ot_o);
    fx = (h & 1) ? ot_o.x1 : ot_o.x2;   /* the opposite corner stays */
    fy = (h & 2) ? ot_o.y1 : ot_o.y2;
    ot_ax = fx;
    ot_ay = fy;
    ovl_loop(OT_BAND);
    if (!ot_moved)
        return;
    ot_r.x1 += 3; ot_r.y1 += 3; ot_r.x2 -= 3; ot_r.y2 -= 3;   /* the outline's pad */
    if (ot_r.x2 <= ot_r.x1 || ot_r.y2 <= ot_r.y1)
        return;
    ovl_prect(&ot_r, &ot_pr);
    if (ot_pr.x1 - ot_pr.x0 < 64 || ot_pr.y1 - ot_pr.y0 < 64) {
        pdu_tell("Too small: 4 points at least.");
        return;
    }
    os88_memcpy(&ot_e, &ed_w, sizeof(ot_e));
    os88_memcpy(&ot_e.rect, &ot_pr, sizeof(ot_pr));
    pdu_undo_begin(UL_RESIZE);
    pdu_ed_set(i, &ot_e);
    pdu_undo_end();
}

static int ovl_texty(int i)             /* a record that takes typing */
{
    return pdu_ed_get(i, &ed_v) == 0
        && (ed_v.kind == PDA_NOTE || ed_v.kind == PDA_TEXT);
}

/* the Select tool: handle, record, or nothing (nav's text selection) */
static int ovl_select_click(int x, int y)
{
    int h, i, dbl;

    h = ovl_handle(x, y);
    if (h >= 0) {
        ot_sx = x;
        ot_sy = y;
        ovl_resize(h);
        return 1;
    }
    i = ovl_hit(x, y);
    if (i < 0) {
        pdu_ed_select(-1);
        return 0;                       /* the page is nav's to select in */
    }
    dbl = (i == ot_last && i == pdu_ed_sel
           && (unsigned)(os88_ticks() - ot_tick) < 12 && ovl_texty(i));
    pdu_ed_select(i);
    ot_sx = x;
    ot_sy = y;
    if (dbl) {
        if (ovl_texty(i) && ed_v.kind == PDA_NOTE)
            pdu_nt_open(i);
        else
            pdu_tx_begin(i, 0);
        return 1;
    }
    ovl_move();
    ot_last = i;
    ot_tick = os88_ticks();
    return 1;
}

/* ---- the creation tools ----------------------------------------------------------- */
/* Note: an icon box centred on the click, then typing */
static void ovl_note(int x, int y)
{
    pdm_view_inv(&ot_px, &ot_py, &pdu_view, x, y);
    ot_pr.x0 = ot_px - 160;             /* 20 x 18 pt (13.3) */
    ot_pr.x1 = ot_px + 160;
    ot_pr.y0 = ot_py - 144;
    ot_pr.y1 = ot_py + 144;
    ovl_fit();
    pdu_nt_new(pdu_ed_pg(), &ot_pr);    /* notes' popover makes the record */
}

/* Text: drag a frame, or click for the default one; then typing */
static void ovl_text(int x, int y)
{
    int i;
    ot_sx = ot_ax = x;
    ot_sy = ot_ay = y;
    ovl_loop(OT_BAND);
    if (!ot_moved || ot_cx - x < 12 || ot_cy - y < 8) {
        ovl_box_at(x, y, 144, 36);
        if (ot_r.x2 > pdu_L.canvas.x2) {
            ot_r.x1 -= ot_r.x2 - pdu_L.canvas.x2;
            ot_r.x2 = pdu_L.canvas.x2;
        }
    }
    ovl_prect(&ot_r, &ot_pr);
    i = ovl_make(PDA_TEXT, (const pd_pt *)0, 0);
    if (i < 0)
        return;
    pdu_ed_select(i);
    pdu_tx_begin(i, 1);
}

/* Mark, Rect, Ellipse: a dragged frame */
static void ovl_frame(int kind, int x, int y)
{
    ot_sx = ot_ax = x;
    ot_sy = ot_ay = y;
    ovl_loop(OT_BAND);
    if (!ot_moved)
        return;
    ovl_prect(&ot_r, &ot_pr);
    if (ot_pr.x1 - ot_pr.x0 < 64 || ot_pr.y1 - ot_pr.y0 < 64) {
        pdu_tell("Too small: 4 points at least.");
        pdu_render_want(&pdu_L.canvas);
        return;
    }
    ovl_make(kind, (const pd_pt *)0, 0);
}

static void ovl_line(int x, int y)
{
    ot_sx = ot_ax = x;
    ot_sy = ot_ay = y;
    ovl_loop(OT_LINE);
    if (!ot_moved)
        return;
    pdm_view_inv(&ot_px, &ot_py, &pdu_view, x, y);
    ot_pts[0] = ot_px;
    ot_pts[1] = ot_py;
    pdm_view_inv(&ot_px, &ot_py, &pdu_view, ot_cx, ot_cy);
    ot_pts[2] = ot_px;
    ot_pts[3] = ot_py;
    ovl_ed_bbox(&ot_pr, ot_pts, 2);
    ovl_make(PDA_LINE, ot_pts, 2);
}

/* Ink and Sign: one stroke, placed when the button comes up. A signature is
 * one stroke here (a multi-stroke capture is not built) and says what it is. */
static void ovl_ink(int kind, int x, int y)
{
    ot_sx = x;
    ot_sy = y;
    ot_from = 0;
    ot_k = 0;
    ovl_loop(OT_INK);
    if (ot_k < 2) {
        pdu_render_want(&pdu_L.canvas);
        return;
    }
    ovl_ink_done();
    if (ot_k >= OT_NPTS - 2)
        pdu_tell("Stroke limit reached; the stroke is kept.");
    else if (kind == PDA_SIGN)
        pdu_tell("Drawn signature - not a certificate.");
    ovl_ed_bbox(&ot_pr, ot_pts, ot_k);
    ovl_make(kind, ot_pts, ot_k);
}

/* Highlight: the current text selection becomes quads */
static int ovl_hilite(void)
{
    int nq;
    nq = pdu_sel_quads(ot_pts, 12);
    if (nq <= 0) {
        pdu_tell("Select text first.");
        return 0;
    }
    ovl_ed_bbox(&ot_pr, ot_pts, nq << 2);
    return ovl_make(PDA_HIGHLIGHT, ot_pts, nq << 2) >= 0;
}

/* ============================================================================
 * THE ENTRY POINTS (the primary calls these; each is one far call)
 * ==========================================================================*/
/* a click on the canvas. 1 = the tool took it. */
int ovl_tool_click(int x, int y)
{
    int t;

    if (pdu_doc != D_OPEN || pdu_rdonly || pdu_mode == PDU_MODE_READ
        || pdu_mode == PDU_MODE_PAGES)
        return 0;
    t = pdu_tool;
    if (pdu_tx >= 0) {                  /* a click in the box typed into stays */
        if (ovl_hit(x, y) == pdu_tx)
            return 1;
        pdu_tx_end();
    }
    pdu_ed_xoff_l();
    if (t != C_T_SELECT && t != C_T_TEXT)
        pdu_ed_select(-1);
    switch (t) {
    case C_T_SELECT:
        t = ovl_select_click(x, y);
        pdu_ed_show();
        return t;
    case C_T_NOTE:
        ovl_note(x, y);
        return 1;
    case C_T_TEXT:
        t = ovl_hit(x, y);
        if (t >= 0 && ovl_texty(t)) {   /* an existing box: type into it */
            pdu_ed_select(t);
            pdu_tx_begin(t, 0);
            return 1;
        }
        pdu_ed_select(-1);
        ovl_text(x, y);
        return 1;
    case C_T_MARK:
        ovl_frame(PDA_AREA, x, y);
        return 1;
    case C_T_RECT:
        ovl_frame(PDA_RECT, x, y);
        return 1;
    case C_T_ELLIPSE:
        ovl_frame(PDA_ELLIPSE, x, y);
        return 1;
    case C_T_LINE:
        ovl_line(x, y);
        return 1;
    case C_T_INK:
        ovl_ink(PDA_INK, x, y);
        return 1;
    case C_T_SIGN:
        ovl_ink(PDA_SIGN, x, y);
        return 1;
    }
    return 0;                           /* Highlight: nav selects, then asks */
}

/* Highlight pressed (or nav's selection gesture ended with it armed) */
int ovl_tool_hilite(void)
{
    return ovl_hilite();
}

/* Delete / the Delete button: 1 = something went */
int ovl_tool_delete(void)
{
    int i;
    i = pdu_ed_sel;
    if (i < 0 || pdu_tx >= 0)
        return 0;
    pdu_ed_xoff_l();
    pdu_ed_sel = -1;
    pdu_undo_begin(UL_DELETE);
    pdu_ed_del(i);
    pdu_undo_end();
    pdu_tell("Deleted.");
    return 1;
}

/* Enter: open the selected note or text for typing */
int ovl_tool_enter(void)
{
    if (pdu_ed_sel >= 0 && pdu_tx < 0 && ovl_texty(pdu_ed_sel)) {
        pdu_tx_begin(pdu_ed_sel, 0);
        return 1;
    }
    return 0;
}

/* a line on the status line for the tool just chosen */
void ovl_tool_hint(int id)
{
    const char *s;
    s = 0;
    switch (id) {
    case C_T_SELECT:  s = "Click a mark to select; drag to move."; break;
    case C_T_HILITE:  s = "Select text, then press Highlight."; break;
    case C_T_NOTE:    s = "Click to place a note, then type."; break;
    case C_T_TEXT:    s = "Drag a frame, then type."; break;
    case C_T_MARK:    s = "Drag a rectangle (an area, not text)."; break;
    case C_T_INK:     s = "Draw with the button down."; break;
    case C_T_LINE:    s = "Drag from the tail to the arrow."; break;
    case C_T_RECT:    s = "Drag a rectangle."; break;
    case C_T_ELLIPSE: s = "Drag the box the ellipse fits."; break;
    case C_T_SIGN:    s = "Draw; Enter places it. Not a certificate."; break;
    }
    if (s)
        pdu_tell(s);
}
