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
#define ot_e ed_v                       /* a record being made (pduedit.c's) */
static int ot_sx, ot_sy;                /* the press */
static int ot_ax, ot_ay;                /* the band's fixed corner / line start */
static int ot_cx, ot_cy;                /* the pointer */
static struct os88_rect ot_o;           /* the outline a move carries */
static int ot_moved;
static pd_px ot_px, ot_py;
#define ot_pts ed_pts                   /* a Line's points, a Highlight's quads */
static int ot_last;                     /* the last record clicked */
static unsigned ot_tick;

/* default colours, by kind (PDC_*) */
static const char ot_col[10] = { 0, PDC_YELLOW, PDC_YELLOW, PDC_LRED, PDC_BLUE,
                                 PDC_RED, PDC_RED, PDC_RED, PDC_RED,
                                 PDC_BLACK };
static const char ot_lab[10] = { 0, UL_NOTE, UL_HILITE, UL_MARK, UL_TEXT,
                                 UL_INK, UL_LINE, UL_RECT, UL_ELLIPSE,
                                 UL_SIGN };

static int ovl_abs(int v) { return v < 0 ? -v : v; }   /* overlay: no thunk */

/* ---- XOR drawing, cut to the canvas (the selection's code, not remembered) */
static struct os88_rect ot_t;           /* scratch: a run, a page corner pair */
static void ovl_xrun(int x1, int y1, int x2, int y2)
{
    ovl_ed_box(&ot_t, x1, y1, x2, y2, 0);       /* a leftward or upward run */
    ed_xrem = 0;
    ovl_ed_xf(ot_t.x1, ot_t.y1, ot_t.x2, ot_t.y2);
}


/* one run of ovl_xline, major axis a..e at minor b; the first run of a line
 * drawn with ot_skip set leaves out its first pixel (the joint a stroke's
 * previous segment already flipped: XOR twice would put a hole there) */
static int ot_skip;
static void ovl_xseg(int steep, int sx, int a, int e, int b)
{
    if (ot_skip) {
        ot_skip = 0;
        if (a == e)
            return;
        a += sx;
    }
    if (steep) ovl_xrun(b, a, b, e);
    else       ovl_xrun(a, b, e, b);
}

/* a line as runs along its major axis: calls = the minor extent + 1, so an
 * 800-pixel line that is nearly flat costs a handful of fills, not 800 */
static void ovl_xline(int x0, int y0, int x1, int y1)
{
    int dx, dy, sx, sy, err, a, b, run, steep, t;

    dx = ovl_abs(x1 - x0);
    dy = ovl_abs(y1 - y0);
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
            ovl_xseg(steep, sx, a, run - sx, b);
            a = run;
            b += sy;
        }
    }
    ovl_xseg(steep, sx, a, run, b);
    ot_skip = 0;
}

/* the band for a gesture mode at the pointer (draw it, draw it again: gone) */
static void ovl_shape(int mode)
{
    if (mode == OT_LINE)
        ovl_xline(ot_ax, ot_ay, ot_cx, ot_cy);
    else {
        ed_xrem = 0;
        ovl_ed_frame(&ot_r, 0);
    }
}

/* ---- page <-> screen ------------------------------------------------------ */
/* the page rect of a screen rect (pixel edges, so x2 + 1), clamped to the
 * page's box */
static int ot_qx, ot_qy;                /* a corner */
static void ovl_prect(const struct os88_rect *s, struct pd_rect *p)
{
    /* opposite corners stay opposite */
    pdm_view_inv(&ot_px, &ot_py, &pdu_view, s->x1, s->y1);
    ot_qx = ot_px;
    ot_qy = ot_py;
    pdm_view_inv(&ot_px, &ot_py, &pdu_view, s->x2 + 1, s->y2 + 1);
    ovl_ed_box(&ot_t, ot_qx, ot_qy, ot_px, ot_py, 0);
    p->x0 = ot_t.x1;
    p->y0 = ot_t.y1;
    p->x1 = ot_t.x2;
    p->y1 = ot_t.y2;
    if (p->x0 < pdu_view.box.x0) p->x0 = pdu_view.box.x0;
    if (p->y0 < pdu_view.box.y0) p->y0 = pdu_view.box.y0;
    if (p->x1 > pdu_view.box.x1) p->x1 = pdu_view.box.x1;
    if (p->y1 > pdu_view.box.y1) p->y1 = pdu_view.box.y1;
}

/* ---- the pointer loop ------------------------------------------------------- */

/* one stroke is captured in SCREEN pixels straight into the edit store's
 * free gap - point k at ot_ib - 4(k + 1), ot_ib being the point heap's top -
 * so it lies, last point lowest, exactly where pdu_ed_new puts a record's
 * points: it is made page space and turned round (a record is first point
 * lowest) in place, and becomes the record with no copy and no near buffer. Thinned as it goes (a point only when the pointer is 3 pixels
 * from the last kept) and drawn as it grows. */
static int ot_k;                        /* points in the stroke so far */
static unsigned ot_ib;                  /* the point heap's top at the press */
static int ot_ix, ot_iy;                /* ovl_ipt's answer */

static void ovl_ipt(int k)
{
    ot_ix = (pd_pt)pd_fpeekw(ed_seg(), ot_ib - 4 - (k << 2));
    ot_iy = (pd_pt)pd_fpeekw(ed_seg(), ot_ib - 2 - (k << 2));
}

static void ovl_iput(int k, int x, int y)
{
    pd_fpokew(ed_seg(), ot_ib - 4 - (k << 2), x);
    pd_fpokew(ed_seg(), ot_ib - 2 - (k << 2), y);
}

/* point k of the stroke as XOR: a dot, or the segment from k-1 without its
 * first pixel. Drawing every k again in any order takes the stroke off. */
static void ovl_ink_seg(int k)
{
    int x, y;
    ovl_ipt(k);
    if (k == 0) {
        ovl_xrun(ot_ix, ot_iy, ot_ix, ot_iy);
        return;
    }
    x = ot_ix;
    y = ot_iy;
    ovl_ipt(k - 1);
    ot_skip = 1;
    ovl_xline(ot_ix, ot_iy, x, y);
}

static void ovl_ink_add(int x, int y)
{
    if (ot_k > 0) {
        ovl_ipt(ot_k - 1);
        if (ovl_abs(x - ot_ix) < 3 && ovl_abs(y - ot_iy) < 3)
            return;
    }
    if (ot_k >= OT_NPTS - 2 || !ovl_ed_room(0, (ot_k + 1) << 2))
        return;                         /* the stroke limit, or the store's */
    ovl_iput(ot_k, x, y);
    ovl_ink_seg(ot_k);
    ot_k++;
}

/* grow the box r (x1 y1 x2 y2 as words) to hold (x, y) */
static void ovl_grow(int *r, int x, int y)
{
    if (x < r[0]) r[0] = x;
    if (y < r[1]) r[1] = y;
    if (x > r[2]) r[2] = x;
    if (y > r[3]) r[3] = y;
}

/* ot_pr = the page box of n near points at ot_pts (a Line, a Highlight) */
static void ovl_pbox(int n)
{
    int k;
    ot_pr.x0 = ot_pr.y0 = 32767;
    ot_pr.x1 = ot_pr.y1 = -32767;
    for (k = 0; k < n; k++)
        ovl_grow((int *)&ot_pr, ot_pts[k << 1], ot_pts[(k << 1) + 1]);
}

/* the stroke becomes page space in place, then is turned round so its
 * first point is the record's first; ot_r = its screen box (for a refusal's
 * repaint), ot_pr = its page box (the record's rect) */
static void ovl_ink_done(void)
{
    int k, j, x, y;
    ot_r.x1 = ot_r.y1 = ot_pr.x0 = ot_pr.y0 = 32767;
    ot_r.x2 = ot_r.y2 = ot_pr.x1 = ot_pr.y1 = -32767;
    for (k = 0; k < ot_k; k++) {
        ovl_ipt(k);
        ovl_grow((int *)&ot_r, ot_ix, ot_iy);
        pdm_view_inv(&ot_px, &ot_py, &pdu_view, ot_ix, ot_iy);
        ovl_iput(k, ot_px, ot_py);
        ovl_grow((int *)&ot_pr, ot_px, ot_py);
    }
    for (k = 0, j = ot_k - 1; k < j; k++, j--) {
        ovl_ipt(k);
        x = ot_ix;
        y = ot_iy;
        ovl_ipt(j);
        ovl_iput(k, ot_ix, ot_iy);
        ovl_iput(j, x, y);
    }
}

/* poll until the button comes up, drawing the band for `mode`; it is taken
 * off again before this returns. ot_moved: the pointer travelled OT_DRAG. */
static void ovl_loop(int mode)
{
    int have;
    unsigned t0;
    ot_cx = ot_sx;
    ot_cy = ot_sy;
    ot_moved = 0;
    have = 0;
    t0 = os88_ticks();
    for (;;) {
        os88_mouse(&ot_m);
        if ((ot_m.btn & 1) == 0)
            break;
        if ((ot_m.x != ot_cx || ot_m.y != ot_cy)
            && (ot_moved || ovl_abs(ot_m.x - ot_sx) >= OT_DRAG
                || ovl_abs(ot_m.y - ot_sy) >= OT_DRAG)) {
            if (have)
                ovl_shape(mode);
            /* the pointer, kept on the canvas */
            ot_cx = ot_m.x < pdu_L.canvas.x1 ? pdu_L.canvas.x1
                  : ot_m.x > pdu_L.canvas.x2 ? pdu_L.canvas.x2 : ot_m.x;
            ot_cy = ot_m.y < pdu_L.canvas.y1 ? pdu_L.canvas.y1
                  : ot_m.y > pdu_L.canvas.y2 ? pdu_L.canvas.y2 : ot_m.y;
            if (mode == OT_INK) {
                if (!ot_moved)
                    ovl_ink_add(ot_sx, ot_sy);
                ovl_ink_add(ot_cx, ot_cy);
                have = 0;
            } else {
                if (mode == OT_MOVE)    /* the outline, carried */
                    ovl_ed_box(&ot_r, ot_o.x1 + ot_cx - ot_sx,
                               ot_o.y1 + ot_cy - ot_sy,
                               ot_o.x2 + ot_cx - ot_sx,
                               ot_o.y2 + ot_cy - ot_sy, 0);
                else                    /* the band from the fixed corner */
                    ovl_ed_box(&ot_r, ot_ax, ot_ay, ot_cx, ot_cy, 0);
                ovl_shape(mode);
                have = 1;
            }
            ot_moved = 1;
        }
        os88_task_yield();
        /* a mouse that never lets go is a broken driver: a MINUTE bounds it.
         * A pass count did not - 30,000 yields are 20 s on the XT and a
         * fraction of one on a fast machine, which ended drags mid-gesture */
        if ((unsigned)(os88_ticks() - t0) > 1092)
            break;
    }
    if (have)
        ovl_shape(mode);
}

/* ---- making records ---------------------------------------------------------- */
/* by kind: border/line width in points; Text 12 pt, Ink 2 pt and Sign 1.5 pt
 * in half points */
static const char ot_sty[10] = { 0, 0, 0, 1, 0, 0, 1, 1, 1, 0 };
static const char ot_siz[10] = { 0, 0, 0, 0, 12, 4, 0, 0, 0, 3 };

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
    ot_e.style = ot_sty[kind];
    ot_e.size = ot_siz[kind];
    if (kind == PDA_LINE)
        ot_e.align = 2;                 /* an arrow at the end (13.1.1) */
    pdu_undo_begin(ot_lab[kind]);
    i = pdu_ed_new(&ot_e, "", 0, pts, n);
    pdu_undo_end();
    if (i < 0)
        pdu_tell(pdu_ed_err == PDE_LIMIT
                 ? "The change limit (64) is reached."
                 : "The edit store is full.");
    return i;
}

/* ---- hit testing (screen space, topmost first) -------------------------------- */
/* is the pixel inside record ed_w's screen box grown by pad? (ot_r = the box) */
static int ovl_inr(int pad, int px, int py)
{
    ovl_ed_scr(&ed_w.rect, pad, &ot_r);
    return px >= ot_r.x1 && px <= ot_r.x2 && py >= ot_r.y1 && py <= ot_r.y2;
}

/* the topmost live record on the page shown that the pixel hits: a note, a
 * text box or a highlight by its box; a Rect, Ellipse or Area mark by its
 * BORDER (OT_TOL either side), so a click inside one reaches what is under
 * it - the whole box when it is too small to have an inside; Ink, Sign and
 * Line by their bounding box */
static int ovl_hit(int px, int py)
{
    int i, k;
    for (i = (int)pdu_ed_n - 1; i >= 0; i--) {
        if (ovl_ed_rd(i, &ed_w) || (ed_w.flags & PDEF_DELETED)
            || ed_w.page != pdu_ed_pg())
            continue;
        k = ed_w.kind;
        if (!ovl_inr(k == PDA_NOTE || k == PDA_TEXT || k == PDA_HIGHLIGHT
                     ? 2 : OT_TOL, px, py))
            continue;
        if (!ED_SIZABLE(k) || k == PDA_TEXT || !ovl_inr(-OT_TOL - 1, px, py))
            return i;
    }
    return -1;
}

/* ---- the Select tool: pick, move, resize ---------------------------------------- */
static int ovl_texty(int i)             /* a record that takes typing */
{
    return pdu_ed_get(i, &ed_v) == 0
        && (ed_v.kind == PDA_NOTE || ed_v.kind == PDA_TEXT);
}

/* the Select tool: a corner handle (resize), a record (select, move, or a
 * double-click to type), or nothing (the page: nav's text selection) */
static int ovl_select_click(int x, int y)
{
    int h, i, dbl;
    pd_pt x0, y0;
    struct pd_rect *r;

    /* which corner (UL UR LL LR) of the selected shape is under the pointer */
    i = pdu_ed_sel;
    h = -1;
    if (i >= 0 && ovl_ed_rd(i, &ed_w) == 0 && ED_SIZABLE(ed_w.kind)) {
        ovl_ed_scr(&ed_w.rect, 3, &ot_o);
        for (h = 3; h >= 0; h--)
            if (ovl_abs(x - ((h & 1) ? ot_o.x2 : ot_o.x1)) <= 5
                && ovl_abs(y - ((h & 2) ? ot_o.y2 : ot_o.y1)) <= 5)
                break;
    }
    if (h >= 0) {                       /* resize: the opposite corner stays */
        ot_ax = (h & 1) ? ot_o.x1 : ot_o.x2;
        ot_ay = (h & 2) ? ot_o.y1 : ot_o.y2;
        ovl_loop(OT_BAND);
        if (!ot_moved)
            return 1;
        ovl_ed_box(&ot_r, ot_r.x1, ot_r.y1, ot_r.x2, ot_r.y2, -3);  /* the pad */
        ovl_prect(&ot_r, &ot_pr);
        if (ot_pr.x1 - ot_pr.x0 < 64 || ot_pr.y1 - ot_pr.y0 < 64) {
            pdu_tell("Too small: 4 points at least.");
            return 1;
        }
        os88_memcpy(&ot_e, &ed_w, sizeof(ot_e));
        os88_memcpy(&ot_e.rect, &ot_pr, sizeof(ot_pr));
        pdu_undo_begin(UL_RESIZE);
        pdu_ed_set(i, &ot_e);
        pdu_undo_end();
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
    if (dbl) {
        if (ed_v.kind == PDA_NOTE)
            pdu_nt_open(i);
        else
            pdu_tx_begin(i, 0);
        return 1;
    }
    ot_last = i;
    ot_tick = os88_ticks();
    /* move: the band is the outline carried by the pointer; the record moves
     * by the page-space difference between the press and the release, so the
     * translation is exact pt16 under any zoom or rotation */
    ovl_ed_rd(i, &ed_w);
    ovl_ed_scr(&ed_w.rect, 3, &ot_o);
    ovl_loop(OT_MOVE);
    if (!ot_moved)
        return 1;
    pdm_view_inv(&ot_px, &ot_py, &pdu_view, x, y);
    x0 = ot_px;
    y0 = ot_py;
    pdm_view_inv(&ot_px, &ot_py, &pdu_view, ot_cx, ot_cy);
    x0 = (pd_pt)(ot_px - x0);
    y0 = (pd_pt)(ot_py - y0);
    r = &pdu_view.box;                  /* stay on the page */
    if (x0 > r->x1 - ed_w.rect.x1) x0 = r->x1 - ed_w.rect.x1;
    if (x0 < r->x0 - ed_w.rect.x0) x0 = r->x0 - ed_w.rect.x0;
    if (y0 > r->y1 - ed_w.rect.y1) y0 = r->y1 - ed_w.rect.y1;
    if (y0 < r->y0 - ed_w.rect.y0) y0 = r->y0 - ed_w.rect.y0;
    if (x0 || y0) {
        pdu_undo_begin(UL_MOVE);
        pdu_ed_move(i, x0, y0);
        pdu_undo_end();
    }
    return 1;
}

/* ============================================================================
 * THE ENTRY POINTS (the primary calls these; each is one far call)
 * ==========================================================================*/
/* a click on the canvas. 1 = the tool took it. Every creation gesture ends
 * at the release and converts ONCE, screen to page (5.1). */
/* verify-insert: a blank, an inserted page (20.16.2) or a duplicate (13.7)
 * owns no edit records - the writer saves a mark on the page id's original,
 * and an inserted page has no id here at all - so a new mark is refused in
 * words rather than saved somewhere else or not at all */
static const char pdu_svfirst[] = "Save first.";   /* verify-insert: marks, Duplicate, Extract */
static int ovl_tool_nomark(void)
{
    pdu_tell(pdu_svfirst);
    return 1;
}

int ovl_tool_click(int x, int y)
{
    int t, i, k;

    if (pdu_doc != D_OPEN || pdu_rdonly || pdu_mode == PDU_MODE_READ
        || pdu_mode == PDU_MODE_PAGES)
        return 0;
    t = pdu_tool;
    if (pdu_tx >= 0) {                  /* a click in the box typed into stays */
        if (ovl_hit(x, y) == pdu_tx)
            return 1;
        pdu_tx_end();
    }
    if (pdu_pg_isdup(pdu_page))         /* verify-insert: owns no marks */
        return t == C_T_SELECT || t == C_T_HILITE ? 0 : ovl_tool_nomark();
    pdu_ed_xoff_l();
    ot_sx = ot_ax = x;
    ot_sy = ot_ay = y;
    if (t == C_T_SELECT) {
        t = ovl_select_click(x, y);
        pdu_ed_show();
        return t;
    }
    if (ovl_ed_need()) {                /* QA F26: the store at the first edit */
        pdu_tell(pdu_errtext(PDE_NOMEM));
        return 1;
    }
    if (t == C_T_HILITE)
        return 0;                       /* Highlight: nav selects, then asks */
    if (t == C_T_TEXT) {                /* an existing box: type into it */
        i = ovl_hit(x, y);
        if (i >= 0 && ovl_texty(i)) {
            pdu_ed_select(i);
            pdu_tx_begin(i, 0);
            return 1;
        }
    }
    pdu_ed_select(-1);

    if (t == C_T_NOTE) {                /* 20 x 18 pt centred on the click (13.3) */
        pdm_view_inv(&ot_px, &ot_py, &pdu_view, x, y);
        ot_pr.x0 = ot_px - 160;
        ot_pr.x1 = ot_px + 160;
        ot_pr.y0 = ot_py - 144;
        ot_pr.y1 = ot_py + 144;
        k = ot_pr.x1 - pdu_view.box.x1; /* slid onto the page, never resized */
        if (k > 0) { ot_pr.x0 -= k; ot_pr.x1 -= k; }
        k = pdu_view.box.x0 - ot_pr.x0;
        if (k > 0) { ot_pr.x0 += k; ot_pr.x1 += k; }
        k = ot_pr.y1 - pdu_view.box.y1;
        if (k > 0) { ot_pr.y0 -= k; ot_pr.y1 -= k; }
        k = pdu_view.box.y0 - ot_pr.y0;
        if (k > 0) { ot_pr.y0 += k; ot_pr.y1 += k; }
        pdu_nt_new(pdu_ed_pg(), &ot_pr);    /* notes' popover makes the record */
        return 1;
    }

    if (t == C_T_INK || t == C_T_SIGN) {
        /* one stroke, placed when the button comes up; a signature is one
         * stroke here (multi-stroke capture is not built) and says what it is */
        ot_ib = pdu_ed_pt;
        ot_k = 0;
        ovl_loop(OT_INK);
        if (ot_k < 2) {                 /* a dot or nothing: take it off */
            if (ot_k)
                ovl_ink_seg(0);
            return 1;
        }
        ovl_ink_done();
        if (ot_k >= OT_NPTS - 2)
            pdu_tell("Stroke limit: the stroke is kept.");
        else if (t == C_T_SIGN)
            pdu_tell("Drawn signature. This is not a certificate signature.");
        /* the points are already in place below the heap's top: no copy */
        if (ovl_make(t == C_T_INK ? PDA_INK : PDA_SIGN, (const pd_pt *)0,
                     ot_k) < 0)
            pdu_render_want(&ot_r);     /* the XOR stroke goes: that strip only */
        return 1;
    }

    ovl_loop(t == C_T_LINE ? OT_LINE : OT_BAND);
    if (t == C_T_LINE) {                /* tail to head, an arrow at the head */
        if (!ot_moved)
            return 1;
        pdm_view_inv(&ot_px, &ot_py, &pdu_view, x, y);
        ot_pts[0] = ot_px;
        ot_pts[1] = ot_py;
        pdm_view_inv(&ot_px, &ot_py, &pdu_view, ot_cx, ot_cy);
        ot_pts[2] = ot_px;
        ot_pts[3] = ot_py;
        ovl_pbox(2);
        ovl_make(PDA_LINE, ot_pts, 2);
        return 1;
    }
    if (t == C_T_TEXT) {                /* a frame, or a click: 144 x 36 pt */
        if (!ot_moved || ot_r.x2 - ot_r.x1 < 12 || ot_r.y2 - ot_r.y1 < 8) {
            ot_r.x1 = x;
            ot_r.y1 = y;
            ot_r.x2 = x + pdu_muldiv(144, pdu_view.zoom, 100) - 1;
            ot_r.y2 = y + pdu_muldiv(pdu_muldiv(36, pdu_view.zoom, 100),
                                     pdu_view.asp_num, pdu_view.asp_den) - 1;
            if (ot_r.x2 > pdu_L.canvas.x2) {
                ot_r.x1 -= ot_r.x2 - pdu_L.canvas.x2;
                ot_r.x2 = pdu_L.canvas.x2;
            }
        }
    } else if (!ot_moved)
        return 1;
    ovl_prect(&ot_r, &ot_pr);
    if (ot_pr.x1 - ot_pr.x0 < 64 || ot_pr.y1 - ot_pr.y0 < 64) {
        /* the band is already off: nothing to repaint */
        pdu_tell(t == C_T_TEXT ? "Click on the page."
                               : "Too small: 4 points at least.");
        return 1;
    }
    k = t == C_T_TEXT ? PDA_TEXT : t == C_T_MARK ? PDA_AREA
      : t == C_T_RECT ? PDA_RECT : PDA_ELLIPSE;
    i = ovl_make(k, (const pd_pt *)0, 0);
    if (i >= 0 && k == PDA_TEXT) {      /* then typing */
        pdu_ed_select(i);
        pdu_tx_begin(i, 1);
    }
    return 1;
}

/* Highlight pressed (or nav's selection gesture ended with it armed) */
int ovl_tool_hilite(void)
{
    int nq;                             /* the text selection becomes quads */
    if (pdu_pg_isdup(pdu_page))         /* verify-insert */
        return !ovl_tool_nomark();
    nq = pdu_sel_quads(ot_pts, 12);
    if (nq <= 0) {
        pdu_tell("Select text first.");
        return 0;
    }
    ovl_pbox(nq << 2);
    return ovl_make(PDA_HIGHLIGHT, ot_pts, nq << 2) >= 0;
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
    case C_T_MARK:    s = "Drag a rectangle over an area."; break;
    case C_T_INK:     s = "Draw with the button down."; break;
    case C_T_LINE:    s = "Drag from the tail to the arrow."; break;
    case C_T_RECT:    s = "Drag a rectangle."; break;
    case C_T_ELLIPSE: s = "Drag the box the ellipse fits."; break;
    case C_T_SIGN:    s = "Draw one stroke."; break;
    }
    if (s) {
        pdu_tell(s);
        /* an edit tool: the edit store now (QA F26), the slot's room given
         * for it here and not under the edit's own click (ovl_ed_need) */
        if (id != C_T_SELECT && pdu_socc != PDU_OCC_WRT
            && (ovl_ed_need() || pdu_unclaimed_kb() < 32)) {   /* ...or the store took the reserve (16.1) */
            pdu_slot_free();
            ovl_ed_need();
        }
    }
}
