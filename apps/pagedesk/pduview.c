/* ============================================================================
 * os8088 - apps/pagedesk/pduview.c   THE PAGE VIEW (PAGEDESK-SPEC 5.3, 9.1)
 *
 * The view is a pd_view: the page's effective box, its rotation, the zoom,
 * the pixel aspect and where its top-left lands on screen. pdmath owns the
 * transform (5.3: pdm_view_* is the ONLY one); this file owns the policy -
 * which zoom, how far scrolled, which page - and the cheap ways to show a
 * change: a scroll MOVES the canvas pixels (os88_gfx_scroll) and asks the
 * renderer for the exposed strip only (PERFORMANCE.md rule 1).
 * ==========================================================================*/

static const int pdu_zsteps[11] = { 25, 33, 50, 67, 75, 100, 125, 150, 200,
                                    300, 400 };

static int pdu_cw(void) { return pdu_L.canvas.x2 - pdu_L.canvas.x1 + 1; }
static int pdu_ch(void) { return pdu_L.canvas.y2 - pdu_L.canvas.y1 + 1; }

/* the effective box (5.2): CropBox ^ MediaBox */
static void pdu_box(void)
{
    struct pd_rect *m, *c, *b;
    m = &pdu_pinfo.media;
    c = &pdu_pinfo.crop;
    b = &pdu_view.box;
    b->x0 = c->x0 > m->x0 ? c->x0 : m->x0;
    b->y0 = c->y0 > m->y0 ? c->y0 : m->y0;
    b->x1 = c->x1 < m->x1 ? c->x1 : m->x1;
    b->y1 = c->y1 < m->y1 ? c->y1 : m->y1;
    if (b->x1 <= b->x0 || b->y1 <= b->y0) {   /* a crop outside the media */
        b->x0 = m->x0; b->y0 = m->y0; b->x1 = m->x1; b->y1 = m->y1;
    }
}

/* the zoom that fits the page's width (mode 1) or all of it (mode 2) */
static int pdu_fitzoom(int mode)
{
    unsigned W, H, zw, zh;
    int cw, ch;

    W = pdu_view.box.x1 - pdu_view.box.x0;
    H = pdu_view.box.y1 - pdu_view.box.y0;
    if (pdu_view.rot == 90 || pdu_view.rot == 270) {
        zw = W; W = H; H = zw;
    }
    cw = pdu_cw() - 2 * PDU_MARGIN;
    ch = pdu_ch() - 2 * PDU_MARGIN;
    if (cw < 16) cw = 16;
    if (ch < 16) ch = 16;
    zw = pdu_muldiv(cw, 1600, W ? W : 1);
    if (mode == 2) {
        /* sy = sx * aspn / aspd, so the height fits at
         * ch * 1600 * aspd / (H * aspn) */
        zh = pdu_muldiv(ch, 1600, H ? H : 1);
        zh = pdu_muldiv(zh, pdu_aspd, pdu_aspn);
        if (zh < zw)
            zw = zh;
    }
    if (zw < 25) zw = 25;
    if (zw > 400) zw = 400;
    return zw;
}

/* clamp the scroll and place the page: pw/ph, ox/oy, the clip */
static void pdu_place(void)
{
    int cw, ch, ext;

    pdm_view_size(&pdu_pw, &pdu_ph, &pdu_view);
    cw = pdu_cw();
    ch = pdu_ch();
    ext = pdu_pw + 2 * PDU_MARGIN - cw;     /* horizontal: pan or centre */
    if (ext <= 0) {
        pdu_sx = 0;
        pdu_view.ox = pdu_L.canvas.x1 + (cw - pdu_pw) / 2;
    } else {
        if (pdu_sx > ext) pdu_sx = ext;
        if (pdu_sx < 0) pdu_sx = 0;
        pdu_view.ox = pdu_L.canvas.x1 + PDU_MARGIN - pdu_sx;
    }
    ext = pdu_ph + 2 * PDU_MARGIN - ch;     /* vertical: scroll */
    if (ext < 0) ext = 0;
    if (pdu_sy > ext) pdu_sy = ext;
    if (pdu_sy < 0) pdu_sy = 0;
    pdu_view.oy = pdu_L.canvas.y1 + PDU_MARGIN - pdu_sy;
    pdu_view.clip_x0 = pdu_L.canvas.x1;
    pdu_view.clip_y0 = pdu_L.canvas.y1;
    pdu_view.clip_x1 = pdu_L.canvas.x2;
    pdu_view.clip_y1 = pdu_L.canvas.y2;
}

/* rebuild the view from the page info, the zoom policy and the layout */
static void pdu_view_set(void)
{
    unsigned r;
    pdu_box();
    r = pdu_pinfo.rotate;
    if (r != 90 && r != 180 && r != 270)
        r = 0;
    pdu_view.rot = r;
    pdu_view.asp_num = pdu_aspn;
    pdu_view.asp_den = pdu_aspd;
    if (pdu_fit)
        pdu_zoom = pdu_fitzoom(pdu_fit);
    pdu_view.zoom = pdu_zoom;
    pdu_place();
}

/* the whole canvas again: desk, page white, a render for the page part, the
 * scroll bar, the bar fields and the status line. Lock held. */
static void pdu_canvas_full(void)
{
    pdu_ed_canvas();                    /* edit */
    pdu_job_abort_render();
    pdu_canvas_paint(&pdu_L.canvas);
    pdu_sbar_paint();
    pdu_ctl_repaint(C_PAGE);
    pdu_ctl_repaint(C_ZOOM);
    pdu_ctl_repaint(C_PREV);
    pdu_ctl_repaint(C_NEXT);
    pdu_ctl_repaint(C_ZOUT);
    pdu_ctl_repaint(C_ZIN);
    pdu_status_paint();
    if (pdu_pop != P_NONE)
        pdu_pop_paint();
}

/* zoom to z (an explicit percent: fit off), or fit (1 width, 2 page) */
static void pdu_zoom_to(int z, int fit)
{
    if (pdu_doc != D_OPEN)
        return;
    pdu_fit = fit;
    if (!fit) {
        if (z < 25) z = 25;
        if (z > 400) z = 400;
        pdu_zoom = z;
    }
    pdu_view_set();
    pdu_canvas_full();
    pdu_menusync();
}

/* one zoom step in or out of the current percent (9.1's steps) */
static void pdu_zoom_step(int dir)
{
    int i, z;
    z = pdu_zoom;
    if (dir > 0) {
        for (i = 0; i < 11; i++)
            if (pdu_zsteps[i] > z) {
                pdu_zoom_to(pdu_zsteps[i], 0);
                return;
            }
    } else {
        for (i = 10; i >= 0; i--)
            if (pdu_zsteps[i] < z) {
                pdu_zoom_to(pdu_zsteps[i], 0);
                return;
            }
    }
}

/* scroll to (nsx, nsy): move the pixels that stay, paint the strip that is
 * exposed. A covered window, a popover over the canvas or a jump bigger than
 * the canvas takes the full path instead. Lock held. */
static void pdu_scroll_to(int nsx, int nsy)
{
    int osy, osx, dy, ch, old;
    struct os88_rect *c;

    if (pdu_doc != D_OPEN)
        return;
    osx = pdu_sx;
    osy = pdu_sy;
    old = pdu_sb[6];
    pdu_sx = nsx;
    pdu_sy = nsy;
    pdu_place();
    if (pdu_sx == osx && pdu_sy == osy)
        return;                         /* against an end: draw nothing */
    pdu_ed_xoff_l();                    /* edit: the outline moves with the page */
    dy = pdu_sy - osy;
    ch = pdu_ch();
    c = &pdu_L.canvas;
    if (pdu_sx != osx || pdu_pop != P_NONE || pdu_about
        || os88_wm_obscured(pdu_win) || dy >= ch || -dy >= ch
        || os88_gfx_scroll(c->x1, c->y1, c->x2, c->y2, -dy) != 0) {
        pdu_canvas_full();
        return;
    }
    /* the pixels moved; so must any render in flight and any it owes */
    pdu_job_shift(-dy);
    pdu_tr.x1 = c->x1;
    pdu_tr.x2 = c->x2;
    if (dy > 0) {
        pdu_tr.y1 = c->y2 - dy + 1;
        pdu_tr.y2 = c->y2;
    } else {
        pdu_tr.y1 = c->y1;
        pdu_tr.y2 = c->y1 - dy - 1;
    }
    os88_memcpy(&pdu_dmg, &pdu_tr, sizeof(pdu_dmg));
    pdu_canvas_paint(&pdu_dmg);
    pdu_sb_fill();
    pdu_sbmove(pdu_sb, old);
}

static void pdu_scroll_by(int dx, int dy)
{
    pdu_scroll_to(pdu_sx + dx, pdu_sy + dy);
}

/* ask for page n: PAGEINFO is a disk read, so it is the wake handler's
 * (pdujob.c), never a callback's under the lock */
static int pdu_pgwant;                  /* page + 1, 0 = none */
static void pdu_goto(int n)
{
    if (pdu_doc != D_OPEN || n < 0 || n >= (int)pdu_npages)
        return;
    pdu_pgwant = n + 1;
    pdu_kick();
}
