/* ============================================================================
 * os8088 - apps/pagedesk/pdudraw.c   THE CHROME (PAGEDESK-SPEC 17, 20.6.4)
 *
 * Every pixel PageDesk draws that is not the page: the bars, the panels, the
 * desk around the page, the scroll bar, the status line, the popovers. The
 * page itself is PDREND's, drawn by the render job (pdujob.c).
 *
 * The rules, from CLAUDE.md and PERFORMANCE.md:
 *   - a redraw is priced in CALLS: a button is two (frame + font_run) on a
 *     bar whose ground was filled once; the status line is ONE font_run of a
 *     composed line; a state change repaints only the control that changed
 *   - text is font_run, never a fill then a transparent string (6.6)
 *   - nothing reads the adapter KIND: colour is `bpp >= 4`, and on a 1bpp
 *     display an active control is INVERTED and a greyed one is the kernel's
 *     disabled pen (17.4, SPEC.md 47)
 * ==========================================================================*/

static const unsigned char pdu_pat50[8] = { 0xAA, 0x55, 0xAA, 0x55,
                                            0xAA, 0x55, 0xAA, 0x55 };

static void pdu_colors(void)
{
    pdu_mono = pdu_vid.bpp < 4;
    pdu_cbar = pdu_mono ? OS88_WHITE : OS88_LGRAY;
    pdu_cact = pdu_mono ? OS88_BLACK : OS88_CYAN;
    pdu_cdesk = OS88_DGRAY;
}

static int pdu_isect(struct os88_rect *a, struct os88_rect *b,
                     struct os88_rect *o)
{
    o->x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    o->y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    o->x2 = a->x2 < b->x2 ? a->x2 : b->x2;
    o->y2 = a->y2 < b->y2 ? a->y2 : b->y2;
    return o->x1 <= o->x2 && o->y1 <= o->y2;
}

static void pdu_fill(int x1, int y1, int x2, int y2, int c)
{
    if (x1 > x2 || y1 > y2)
        return;
    os88_set_color(c);
    os88_gfx_fill(x1, y1, x2, y2);
}

/* the desk around the page: a 50% pattern on 1bpp (the mockups' look), the
 * dark grey on colour */
static void pdu_desk(int x1, int y1, int x2, int y2)
{
    if (x1 > x2 || y1 > y2)
        return;
    if (pdu_mono)
        os88_gfx_fill_pat(x1, y1, x2, y2, pdu_pat50);
    else
        pdu_fill(x1, y1, x2, y2, pdu_cdesk);
}

static char pdu_cut[64];                /* a cut copy for one font_run */

/* --- a control's live label ---------------------------------------------- */
static char pdu_lbuf[16];
static const char *pdu_label(int id)
{
    int n;
    if (id == C_PAGE) {
        if (pdu_fedit) {
            os88_strcpy(pdu_lbuf, pdu_fbuf, sizeof(pdu_lbuf));
            n = os88_strlen(pdu_lbuf);
            pdu_lbuf[n] = '_';
            pdu_lbuf[n + 1] = 0;
            return pdu_lbuf;
        }
        if (pdu_doc != D_OPEN)
            return "-/-";
        os88_utoa(pdu_page + 1, pdu_lbuf);
        n = os88_strlen(pdu_lbuf);
        pdu_lbuf[n] = '/';
        os88_utoa(pdu_npages, pdu_lbuf + n + 1);
        return pdu_lbuf;
    }
    if (id == C_ZOOM) {
        if (pdu_doc != D_OPEN)
            return "-";
        os88_utoa(pdu_zoom, pdu_lbuf);
        n = os88_strlen(pdu_lbuf);
        pdu_lbuf[n] = '%';
        pdu_lbuf[n + 1] = 0;
        return pdu_lbuf;
    }
    if (id == C_NP_FILTER)
        return pdu_npall == 2 ? "Changes" : pdu_npall ? "All pages" : "This page";
    return pdu_lab[id * 2 + (pdu_L.kind == PDU_L_COMPACT ? 1 : 0)];
}

/* is control id shown ACTIVE (a latched mode, tool, tab or open popover)? */
static int pdu_active(int id)
{
    if (id >= C_M_READ && id <= C_M_PAGES)
        return pdu_doc == D_OPEN && id - C_M_READ == pdu_mode;
    if (id == pdu_tool && id >= C_T_SELECT && id <= C_T_SIGN)
        return pdu_doc == D_OPEN;
    if (id == C_SB_PAGES)
        return pdu_sbtab == 0;
    if (id == C_SB_OUTL)
        return pdu_sbtab == 1;
    if (pdu_pop != P_NONE && id == pdu_popsrc)
        return 1;
    if (id == C_PAGE && pdu_fedit)
        return 1;
    return 0;
}

static const char *pdu_why(int id);     /* pducmd.c: 0 = enabled */

/* one control: ground, frame, centred label.
 *
 * NOTHING IS WRITTEN TWICE, AND NOTHING UNCHANGED IS WRITTEN AT ALL
 * (CLAUDE.md, PERFORMANCE.md rules 1-2). The first version filled the whole
 * control and then ran the label over the fill, so every repaint blanked the
 * label for a call's length - the page field flashed on every PgDn - and
 * pdu_canvas_full repainted six controls whether or not they had changed.
 * Now each laid-out control remembers what it last showed (pdu_cst: state
 * and colours; pdu_clab: the label), and:
 *   - the region under it was just filled with its paper (pdu_cground, set
 *     by the bar/panel/card painter around its loop): frame + label, 2 calls
 *   - same state, same label: nothing
 *   - same state, new label: the slivers either side of the new label on
 *     its rows, then the label - the old label's pixels are each written
 *     once (the page field's 2/12 -> 3/12)
 *   - anything else: the four slabs round the label inside the frame, the
 *     frame, the label
 * The cache is keyed on (id, x1, y1), so a relayout that moves or replaces
 * a control finds nothing and takes the full path. */
static int pdu_cground = -1;            /* the paper every control the caller
                                         * is about to paint stands on; -1 =
                                         * not known */
static int pdu_cid[PDU_MAXCTL], pdu_cx1[PDU_MAXCTL], pdu_cy1[PDU_MAXCTL];
static unsigned pdu_cst[PDU_MAXCTL];    /* 0 = nothing known */
static char pdu_clab[PDU_MAXCTL][16];

static void pdu_ctl_paint(int i)
{
    struct pdu_ctl *c;
    const char *s;
    int act, dis, foc, ink, paper, tx, ty, n, k, ground, in, same, max;
    int ry1, ry2;
    unsigned st;

    c = &pdu_L.ctl[i];
    s = pdu_label(c->id);
    act = pdu_active(c->id);
    dis = pdu_why(c->id) != 0;
    foc = (c->id == pdu_focus && !dis);  /* PD-23: where Tab is */
    ground = (c->flags & (CF_FIELD | CF_EMPTY)) ? OS88_WHITE : pdu_cbar;
    if (act && !dis) {
        paper = pdu_cact;
        ink = OS88_WHITE;
    } else {
        paper = ground;
        ink = OS88_BLACK;
    }
    if (dis)
        ink = pdu_mono ? OS88_BLACK : OS88_DGRAY;
    st = 1 | (act ? 2 : 0) | (dis ? 4 : 0) | (foc ? 8 : 0)
         | ((unsigned)paper << 4) | ((unsigned)ink << 8);
    in = foc ? 2 : 1;
    n = os88_strlen(s);
    max = (c->x2 - c->x1 + 1 - 2 * in) / PDU_CW;
    if (n > max)
        n = max;
    if (n > 15)
        n = 15;
    for (k = 0; k < n; k++)
        pdu_cut[k] = s[k];
    pdu_cut[n] = 0;
    tx = c->x1 + (c->x2 - c->x1 + 1 - n * PDU_CW) / 2;
    ty = c->y1 + (c->y2 - c->y1 + 1 - 8) / 2;
    same = pdu_cid[i] == c->id && pdu_cx1[i] == c->x1 && pdu_cy1[i] == c->y1;
    /* the label's rows, kept inside the frame(s): COMPACT's 10-row control
     * puts the label against the focus frame */
    ry1 = ty < c->y1 + in ? c->y1 + in : ty;
    ry2 = ty + 7 > c->y2 - in ? c->y2 - in : ty + 7;

    if (pdu_cground >= 0) {
        if (paper != pdu_cground)
            same = 0;                   /* the slabs, below */
        else
            same = 2;                   /* frame and label on the ground */
    } else if (same && pdu_cst[i] == st) {
        for (k = 0; pdu_clab[i][k] == pdu_cut[k] && pdu_cut[k]; k++)
            ;
        if (pdu_clab[i][k] == pdu_cut[k])
            return;                     /* nothing changed */
        pdu_fill(c->x1 + in, ry1, tx - 1, ry2, paper);
        pdu_fill(tx + n * PDU_CW, ry1, c->x2 - in, ry2, paper);
        same = 3;                       /* only the label, below */
    } else
        same = 0;
    if (same == 0) {
        pdu_fill(c->x1 + in, c->y1 + in, c->x2 - in, ty - 1, paper);
        pdu_fill(c->x1 + in, ty + 8, c->x2 - in, c->y2 - in, paper);
        pdu_fill(c->x1 + in, ry1, tx - 1, ry2, paper);
        pdu_fill(tx + n * PDU_CW, ry1, c->x2 - in, ry2, paper);
    }
    if (dis)
        os88_gfx_pen(1);
    else
        os88_set_color(OS88_BLACK);
    if (same != 3) {
        os88_gfx_frame(c->x1, c->y1, c->x2, c->y2);
        if (foc)
            os88_gfx_frame(c->x1 + 1, c->y1 + 1, c->x2 - 1, c->y2 - 1);
    }
    os88_font_run(tx, ty, pdu_cut, ink, paper);
    if (dis)
        os88_gfx_pen(0);
    pdu_cid[i] = c->id;
    pdu_cx1[i] = c->x1;
    pdu_cy1[i] = c->y1;
    pdu_cst[i] = st;
    os88_strcpy(pdu_clab[i], pdu_cut, sizeof(pdu_clab[i]));
}

/* repaint one control by id, if it is laid out */
static void pdu_ctl_repaint(int id)
{
    int i;
    i = pdu_find(&pdu_L, id);
    if (i >= 0)
        pdu_ctl_paint(i);
}

static void pdu_bar_paint(struct os88_rect *b, int flag)
{
    int i;
    if (b->x1 > b->x2)
        return;
    pdu_fill(b->x1, b->y1, b->x2, b->y2 - 1, pdu_cbar);
    os88_set_color(OS88_BLACK);
    os88_gfx_hline(b->x1, b->x2, b->y2);
    pdu_cground = pdu_cbar;
    for (i = 0; i < pdu_L.nctl; i++)
        if (pdu_L.ctl[i].flags & flag)
            pdu_ctl_paint(i);
    pdu_cground = -1;
}

/* --- the status line: ONE font_run of a composed line (20.6.4) ----------- */
static const char *pdu_adname(void)
{
    if (pdu_vid.kind == OS88_VID_HERC) return "Hercules";
    if (pdu_vid.kind == OS88_VID_CGA)  return "CGA";
    if (pdu_vid.kind == OS88_VID_EGA)  return "EGA";
    return pdu_vid.bpp >= 4 ? "VGA" : "Mono";
}

static void pdu_put(char *line, int cols, int at, const char *s)
{
    while (*s && at < cols) {
        if (at >= 0)
            line[at] = *s;
        at++;
        s++;
    }
}

/* one line centred on cx and CUT to maxw pixels: the cut is a copy, because
 * font_run draws to the string's NUL (the first version measured the cut
 * and then handed font_run the whole string, which ran off the card) */
static void pdu_cutrun(int cx, int y, const char *s, int maxw)
{
    int i, n;
    n = os88_strlen(s);
    if (n > (int)sizeof(pdu_cut) - 1)
        n = sizeof(pdu_cut) - 1;
    if (n * PDU_CW > maxw)
        n = maxw / PDU_CW;
    for (i = 0; i < n; i++)
        pdu_cut[i] = s[i];
    pdu_cut[n] = 0;
    os88_font_run(cx - n * PDU_CW / 2, y, pdu_cut, OS88_BLACK, OS88_WHITE);
}

/* the window's grow box sits in the content's bottom-right corner
 * (os88_wm_grow), so the line stops PDU_GROW_W short of the edge */
static int pdu_status_cols(void)
{
    int cols;
    cols = (pdu_L.status.x2 - pdu_L.status.x1 + 1 - PDU_GROW_W) / PDU_CW;
    if (cols > 95)
        cols = 95;
    if (cols < 0)
        cols = 0;
    return cols;
}

static int pdu_status_ty(void)
{
    return pdu_L.status.y1 + (pdu_L.status.y2 - pdu_L.status.y1 + 1 - 8) / 2
           + 1;
}

static char pdu_sline[96];
static char pdu_sright[24];
static void pdu_status_paint(void)
{
    struct os88_rect *r;
    int cols, i, n;
    const char *ctr;

    r = &pdu_L.status;
    cols = pdu_status_cols();
    for (i = 0; i < cols; i++)
        pdu_sline[i] = ' ';
    pdu_sline[cols] = 0;
    /* right: the adapter and its live geometry, as the mockups show */
    os88_strcpy(pdu_sright, pdu_adname(), sizeof(pdu_sright));
    n = os88_strlen(pdu_sright);
    pdu_sright[n] = ' ';
    os88_utoa(pdu_vid.w, pdu_sright + n + 1);
    n = os88_strlen(pdu_sright);
    pdu_sright[n] = 'x';
    os88_utoa(pdu_vid.h, pdu_sright + n + 1);
    n = os88_strlen(pdu_sright);
    pdu_put(pdu_sline, cols, cols - n - 1, pdu_sright);
    /* centre: the page, when there is room for it */
    ctr = 0;
    if (pdu_doc == D_OPEN && pdu_L.kind != PDU_L_COMPACT) {
        os88_strcpy(pdu_line, "Page ", sizeof(pdu_line));
        os88_utoa(pdu_page + 1, pdu_num);
        pdu_cat(pdu_line, pdu_num, sizeof(pdu_line));
        pdu_cat(pdu_line, " of ", sizeof(pdu_line));
        os88_utoa(pdu_npages, pdu_num);
        pdu_cat(pdu_line, pdu_num, sizeof(pdu_line));
        ctr = pdu_line;
    }
    /* left: the message, cut where the centre or the right begins */
    n = os88_strlen(pdu_msg);
    i = cols - os88_strlen(pdu_sright) - 3;
    if (ctr) {
        int c0;
        c0 = (cols - os88_strlen(ctr)) / 2;
        if (n + 2 < c0) {
            pdu_put(pdu_sline, cols, c0, ctr);
            i = c0 - 2;
        }
    }
    if (n > i)
        n = i;
    for (i = 0; i < n; i++)
        pdu_sline[1 + i] = pdu_msg[i];
    os88_font_run(r->x1, pdu_status_ty(), pdu_sline, OS88_BLACK, OS88_WHITE);
}

/* the whole status strip: the rule above it, the ground the font_run does
 * not cover (rows above and below the text, and right of its last cell,
 * where the grow box goes), then the line - every pixel once */
static void pdu_status_full(void)
{
    struct os88_rect *r;
    int ty;
    r = &pdu_L.status;
    ty = pdu_status_ty();
    os88_set_color(OS88_BLACK);
    os88_gfx_hline(r->x1, r->x2, r->y1);
    pdu_fill(r->x1, r->y1 + 1, r->x2, ty - 1, OS88_WHITE);
    pdu_fill(r->x1, ty + 8, r->x2, r->y2, OS88_WHITE);
    pdu_fill(r->x1 + pdu_status_cols() * PDU_CW, ty, r->x2, ty + 7,
             OS88_WHITE);
    pdu_status_paint();                 /* pdu_paint_rect puts the grow
                                         * box back over the fill, last */
}

/* say s on the status line - the one place a reason, a result or an error
 * goes (PD-02: a greyed control explains itself here) */
static int pdu_pshown;                  /* polish: the status line shows a job's
                                         * "- Esc cancels" line (pdu_progress) */
static void pdu_say(const char *s)
{
    pdu_pshown = 0;
    os88_strcpy(pdu_msg, s, sizeof(pdu_msg));
}

static void pdu_say2(const char *a, const char *b)
{
    pdu_pshown = 0;
    os88_strcpy(pdu_msg, a, sizeof(pdu_msg));
    pdu_cat(pdu_msg, b, sizeof(pdu_msg));
}

/* say and show, under the lock the caller holds */
static void pdu_tell(const char *s)
{
    pdu_say(s);
    pdu_status_paint();
}

/* --- the scroll bar -------------------------------------------------------- */
static void pdu_sb_fill(void)
{
    int ch;
    pdu_sb[0] = pdu_L.sbar.x1;
    pdu_sb[1] = pdu_L.sbar.y1;
    pdu_sb[2] = pdu_L.sbar.x2;
    pdu_sb[3] = pdu_L.sbar.y2;
    ch = pdu_L.canvas.y2 - pdu_L.canvas.y1 + 1;
    if (pdu_doc == D_OPEN) {
        pdu_sb[4] = pdu_ph + 2 * PDU_MARGIN;
        pdu_sb[5] = ch;
        pdu_sb[6] = pdu_sy;
    } else {
        pdu_sb[4] = 1;
        pdu_sb[5] = 1;
        pdu_sb[6] = 0;
    }
    if (pdu_sb[4] < pdu_sb[5])
        pdu_sb[4] = pdu_sb[5];
}

static void pdu_sbar_paint(void)
{
    pdu_sb_fill();
    pdu_sbar(pdu_sb);
}

/* --- the Pages / Outline sidebar: pduside.c (nav, 20.10.4) ----------------- */

/* --- the canvas -------------------------------------------------------------- */
/* the page's rect on screen (pduview.c keeps pdu_view.ox/oy right) */
static void pdu_page_rect(struct os88_rect *r)
{
    r->x1 = pdu_view.ox;
    r->y1 = pdu_view.oy;
    r->x2 = pdu_view.ox + pdu_pw - 1;
    r->y2 = pdu_view.oy + pdu_ph - 1;
}

static struct os88_rect pdu_pr, pdu_cc;

/* the empty view: what to do next, in words, and the two buttons (PD-01) */
static void pdu_empty_paint(struct os88_rect *clip)
{
    struct os88_rect *c;
    int cx, cy, i, w;
    const char *l1;
    const char *l2;

    c = &pdu_L.canvas;
    pdu_desk(clip->x1, clip->y1, clip->x2, clip->y2);
    cx = (c->x1 + c->x2) / 2;
    cy = (c->y1 + c->y2) / 2;
    w = 46 * PDU_CW;                    /* the longest PDE_* text is 44 */
    if (w > c->x2 - c->x1 - 8)
        w = c->x2 - c->x1 - 8;
    pdu_fill(cx - w / 2, cy - 34, cx + w / 2, cy + PDU_BAR_F + 6, OS88_WHITE);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(cx - w / 2, cy - 34, cx + w / 2, cy + PDU_BAR_F + 6);
    if (pdu_doc == D_FAILED) {
        os88_strcpy(pdu_line, "Could not open ", sizeof(pdu_line));
        pdu_cat(pdu_line, pdu_name, sizeof(pdu_line));
        l1 = pdu_line;
        l2 = pdu_errtext(pdu_err);
    } else if (pdu_doc == D_OPENING) {
        os88_strcpy(pdu_line, "Opening ", sizeof(pdu_line));
        pdu_cat(pdu_line, pdu_name, sizeof(pdu_line));
        l1 = pdu_line;
        l2 = "Esc cancels.";
    } else {
        l1 = "No document open.";
        l2 = "Open a PDF, or read the guide.";
    }
    pdu_cutrun(cx, cy - 28, l1, w - 8);
    pdu_cutrun(cx, cy - 16, l2, w - 8);
    pdu_cground = OS88_WHITE;           /* the card's own fill, above */
    for (i = 0; i < pdu_L.nctl; i++)
        if (pdu_L.ctl[i].flags & CF_EMPTY)
            pdu_ctl_paint(i);
    pdu_cground = -1;
}

/* paint the canvas inside clip: the desk around the page, the page's white
 * and ask the render job for the page part. Never draws the page's content
 * itself. 20.19.1: PDREND composes the whole band (ground, desk, ink) and
 * blits it once, so the white is laid here ONLY where no render will cover
 * the strip at once: a blank (paper) page, a popover or the About card over
 * the canvas, a held-button gesture, or a job already running (the strip
 * waits its turn and must not show what lay under it). Everywhere else the
 * strip gets its ground exactly once, from PDREND. */
static unsigned pdu_pg_kind(unsigned i);
static int pdu_pfill;                   /* verify-polish: a scroll's exposed strip */
static int pdu_prefill(void)
{
    return pdu_pg_kind(pdu_page) != PDM_SRC || pdu_pop != P_NONE || pdu_about
           || pdu_gs == 2 || pdu_job != J_NONE || pdu_pfill;
}
static void pdu_canvas_paint(struct os88_rect *clip)
{
    struct os88_rect *c;

    c = &pdu_cc;
    if (!pdu_isect(clip, &pdu_L.canvas, c))
        return;
    if (pdu_doc != D_OPEN) {
        pdu_empty_paint(c);
        return;
    }
    pdu_ed_xoff_l();                    /* edit: off while its pixels are its own */
    pdu_page_rect(&pdu_pr);
    /* the desk: above, below, left, right of the page, each cut to c */
    pdu_desk(c->x1, c->y1, c->x2, (pdu_pr.y1 - 1 < c->y2 ? pdu_pr.y1 - 1 : c->y2));
    pdu_desk(c->x1, (pdu_pr.y2 + 1 > c->y1 ? pdu_pr.y2 + 1 : c->y1), c->x2, c->y2);
    if (pdu_isect(c, &pdu_pr, &pdu_tr)) {
        pdu_desk(c->x1, pdu_tr.y1, pdu_tr.x1 - 1, pdu_tr.y2);
        pdu_desk(pdu_tr.x2 + 1, pdu_tr.y1, c->x2, pdu_tr.y2);
        if (pdu_prefill())
            pdu_fill(pdu_tr.x1, pdu_tr.y1, pdu_tr.x2, pdu_tr.y2, OS88_WHITE);
        pdu_render_want(&pdu_tr);
    }
    if (pdu_ed_selected() >= 0)         /* edit: back on, unless a render will */
        pdu_ed_show();
}

/* --- the popover (one at a time, inside the canvas) ------------------------ */
static void pdu_pop_paint(void);       /* pducmd.c owns their contents */

/* --- the window -------------------------------------------------------------- */
/* everything UNDER a popover in r. pdu_pop_close calls this half alone: it
 * has already set P_NONE, and the static stack gate (tools/os88pdstk.py)
 * cannot know that, so a call through pdu_paint_rect charged pdu_pop_paint's
 * whole list chain to every close - 400 B on a 384 B budget, unreachable */
static void pdu_paint_under(struct os88_rect *r)
{
    struct os88_rect *o;
    o = &pdu_cc;
    if (pdu_isect(r, &pdu_L.bar1, o))
        pdu_bar_paint(&pdu_L.bar1, CF_BAR1);
    if (pdu_isect(r, &pdu_L.bar2, o))
        pdu_bar_paint(&pdu_L.bar2, CF_BAR2);
    if (pdu_isect(r, &pdu_L.side, o))
        pdu_side_paint();
    if (pdu_isect(r, &pdu_L.notes, o))
        pdu_notes_paint();
    if (pdu_isect(r, &pdu_L.sbar, o))
        pdu_sbar_paint();
    if (pdu_isect(r, &pdu_L.status, o))
        pdu_status_full();
    pdu_canvas_paint(r);
    if (pdu_about)
        os88_about_card_d(pdu_win, pdu_abtext);
    os88_wm_grow(pdu_win);
}

static void pdu_paint_rect(struct os88_rect *r)
{
    pdu_paint_under(r);
    if (pdu_pop != P_NONE && pdu_isect(r, &pdu_poprect, &pdu_cc))
        pdu_pop_paint();
}

static void pdu_paint_all(void)
{
    pdu_tr.x1 = pdu_L.ox;
    pdu_tr.y1 = pdu_L.oy;
    pdu_tr.x2 = pdu_L.ox + pdu_L.w - 1;
    pdu_tr.y2 = pdu_L.oy + pdu_L.h - 1;
    os88_memcpy(&pdu_dmg, &pdu_tr, sizeof(pdu_dmg));
    pdu_paint_rect(&pdu_dmg);
}
