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
        return pdu_npall ? "All pages" : "This page";
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

/* one control: ground, frame, centred label. 2-4 calls. */
static void pdu_ctl_paint(int i)
{
    struct pdu_ctl *c;
    const char *s;
    int act, dis, ink, paper, tx, ty, n, ground;

    c = &pdu_L.ctl[i];
    s = pdu_label(c->id);
    act = pdu_active(c->id);
    dis = pdu_why(c->id) != 0;
    ground = (c->flags & (CF_FIELD | CF_EMPTY)) ? OS88_WHITE : pdu_cbar;
    if (act && !dis) {
        paper = pdu_cact;
        ink = OS88_WHITE;
    } else {
        paper = ground;
        ink = OS88_BLACK;
    }
    pdu_fill(c->x1, c->y1, c->x2, c->y2, paper);
    if (dis) {
        os88_gfx_pen(1);
        ink = pdu_mono ? OS88_BLACK : OS88_DGRAY;
    } else
        os88_set_color(OS88_BLACK);
    os88_gfx_frame(c->x1, c->y1, c->x2, c->y2);
    if (c->id == pdu_focus && !dis)      /* PD-23: where Tab is */
        os88_gfx_frame(c->x1 + 1, c->y1 + 1, c->x2 - 1, c->y2 - 1);
    n = os88_strlen(s);
    tx = c->x1 + (c->x2 - c->x1 + 1 - n * PDU_CW) / 2;
    ty = c->y1 + (c->y2 - c->y1 + 1 - 8) / 2;
    os88_font_run(tx, ty, s, ink, paper);
    if (dis)
        os88_gfx_pen(0);
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
    for (i = 0; i < pdu_L.nctl; i++)
        if (pdu_L.ctl[i].flags & flag)
            pdu_ctl_paint(i);
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
static char pdu_cut[64];
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

static char pdu_sline[96];
static char pdu_sright[24];
static void pdu_status_paint(void)
{
    struct os88_rect *r;
    int cols, i, n;
    const char *ctr;

    r = &pdu_L.status;
    /* the window's grow box sits in the content's bottom-right corner
     * (os88_wm_grow), so the line stops PDU_GROW_W short of the edge */
    cols = (r->x2 - r->x1 + 1 - PDU_GROW_W) / PDU_CW;
    if (cols > 95)
        cols = 95;
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
    os88_font_run(r->x1, r->y1 + (r->y2 - r->y1 + 1 - 8) / 2 + 1, pdu_sline,
                  OS88_BLACK, OS88_WHITE);
    /* the line above it, once */
    os88_set_color(OS88_BLACK);
    os88_gfx_hline(r->x1, r->x2, r->y1);
}

/* the status line's ground band (rows the font_run does not cover) */
static void pdu_status_full(void)
{
    struct os88_rect *r;
    r = &pdu_L.status;
    pdu_fill(r->x1, r->y1, r->x2, r->y2, OS88_WHITE);
    pdu_status_paint();                 /* pdu_paint_rect puts the grow
                                         * box back over the fill, last */
}

/* say s on the status line - the one place a reason, a result or an error
 * goes (PD-02: a greyed control explains itself here) */
static void pdu_say(const char *s)
{
    os88_strcpy(pdu_msg, s, sizeof(pdu_msg));
}

static void pdu_say2(const char *a, const char *b)
{
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

/* --- the Pages / Outline sidebar ------------------------------------------ */
#define PDU_CELL_W  56
#define PDU_CELL_H  72
static int pdu_outn;                    /* outline titles cached at open */
static char pdu_outt[12][26];
static int pdu_outpg[12];

/* the list area inside the sidebar */
static void pdu_side_list(struct os88_rect *o)
{
    o->x1 = pdu_L.side.x1 + 4;
    o->x2 = pdu_L.side.x2 - 4;
    o->y1 = pdu_L.side.y1 + PDU_BAR_F + 2;
    o->y2 = pdu_L.side.y2 - PDU_BAR_F;
}

static int pdu_side_rows(void)
{
    struct os88_rect *o;
    o = &pdu_tr;
    pdu_side_list(o);
    return (o->y2 - o->y1 + 1 - 4) / (PDU_CELL_H + 12);
}

/* the page cell k's rect (k counted from pdu_sidetop); 0 = off the list */
static int pdu_cell(int k, struct os88_rect *r)
{
    struct os88_rect *o;
    int row, col;
    o = &pdu_tr;
    pdu_side_list(o);
    row = k / 2;
    col = k % 2;
    r->x1 = o->x1 + 4 + col * (PDU_CELL_W + 8);
    r->x2 = r->x1 + PDU_CELL_W - 1;
    r->y1 = o->y1 + 4 + row * (PDU_CELL_H + 12);
    r->y2 = r->y1 + PDU_CELL_H - 1;
    return r->y2 + 10 <= o->y2;
}

static struct os88_rect pdu_cr;
static void pdu_cell_paint(int k)
{
    int pg, act, n;
    pg = pdu_sidetop + k;
    if (pg >= (int)pdu_npages || !pdu_cell(k, &pdu_cr))
        return;
    act = (pg == (int)pdu_page);
    pdu_fill(pdu_cr.x1, pdu_cr.y1, pdu_cr.x2, pdu_cr.y2, OS88_WHITE);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(pdu_cr.x1, pdu_cr.y1, pdu_cr.x2, pdu_cr.y2);
    if (act)                            /* the current page: a heavy frame */
        os88_gfx_frame(pdu_cr.x1 - 2, pdu_cr.y1 - 2, pdu_cr.x2 + 2,
                       pdu_cr.y2 + 2);
    /* 10.8: a page not yet drawn shows its number and "...", never a fake
     * picture - and PDREND's thumbnail verb is wave 2 */
    os88_font_run(pdu_cr.x1 + (PDU_CELL_W - 24) / 2, pdu_cr.y1 + 32, "...",
                  OS88_BLACK, OS88_WHITE);
    os88_utoa(pg + 1, pdu_num);
    n = os88_strlen(pdu_num);
    os88_font_run(pdu_cr.x1 + (PDU_CELL_W - n * PDU_CW) / 2, pdu_cr.y2 + 3,
                  pdu_num, act ? OS88_WHITE : OS88_BLACK,
                  act ? pdu_cact : pdu_cbar);
}

static void pdu_side_paint(void)
{
    struct os88_rect *s, *o;
    int i, k, rows;

    s = &pdu_L.side;
    if (s->x1 > s->x2)
        return;
    pdu_fill(s->x1, s->y1, s->x2 - 1, s->y2, pdu_cbar);
    os88_set_color(OS88_BLACK);
    os88_gfx_vline(s->x2, s->y1, s->y2);
    for (i = 0; i < pdu_L.nctl; i++)
        if (pdu_L.ctl[i].id == C_SB_PAGES || pdu_L.ctl[i].id == C_SB_OUTL
            || pdu_L.ctl[i].id == C_SB_HIDE)
            pdu_ctl_paint(i);
    o = &pdu_tr;
    pdu_side_list(o);
    pdu_fill(o->x1, o->y1, o->x2, o->y2, pdu_mono ? OS88_WHITE : pdu_cbar);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(o->x1, o->y1, o->x2, o->y2);
    if (pdu_doc != D_OPEN) {
        os88_font_run(o->x1 + 4, o->y1 + 6, "No document", OS88_BLACK,
                      pdu_mono ? OS88_WHITE : pdu_cbar);
    } else if (pdu_sbtab == 0) {
        rows = pdu_side_rows();
        for (k = 0; k < rows * 2; k++)
            pdu_cell_paint(k);
    } else {
        if (pdu_outn == 0)
            os88_font_run(o->x1 + 4, o->y1 + 6, "No outline", OS88_BLACK,
                          pdu_mono ? OS88_WHITE : pdu_cbar);
        for (k = 0; k < pdu_outn && o->y1 + 6 + k * 12 + 8 < o->y2; k++)
            os88_font_run(o->x1 + 4, o->y1 + 6 + k * 12, pdu_outt[k],
                          OS88_BLACK, pdu_mono ? OS88_WHITE : pdu_cbar);
    }
    /* the bottom row: "12 pages" beside the hide button */
    if (pdu_doc == D_OPEN) {
        os88_utoa(pdu_npages, pdu_line);
        pdu_cat(pdu_line, pdu_npages == 1 ? " page" : " pages",
                sizeof(pdu_line));
        os88_font_run(s->x1 + 4, s->y2 - PDU_BAR_F + 5, pdu_line, OS88_BLACK,
                      pdu_cbar);
    }
}

/* --- Notes & edits (17.3) -------------------------------------------------- */
static void pdu_notes_list(struct os88_rect *o)
{
    o->x1 = pdu_L.notes.x1 + 4;
    o->x2 = pdu_L.notes.x2 - 4;
    o->y1 = pdu_L.notes.y1 + 2 * PDU_BAR_F + 4;
    o->y2 = pdu_L.notes.y2 - 4;
}

static void pdu_notes_body(struct os88_rect *o, int paper)
{
    int y;
    y = o->y1 + 6;
    if (pdu_doc != D_OPEN) {
        os88_font_run(o->x1 + 4, y, "No document", OS88_BLACK, paper);
        return;
    }
    if (pdu_changes == 0) {
        os88_font_run(o->x1 + 4, y, "No notes or edits", OS88_BLACK, paper);
        y = y + 12;
    }
    if (!pdu_npall && pdu_pinfo.nannots) {
        os88_utoa(pdu_pinfo.nannots, pdu_line);
        pdu_cat(pdu_line, " in the file", sizeof(pdu_line));
        os88_font_run(o->x1 + 4, y, pdu_line, OS88_BLACK, paper);
    }
}

static void pdu_notes_paint(void)
{
    struct os88_rect *n, *o;
    int i, paper;

    n = &pdu_L.notes;
    if (n->x1 > n->x2)
        return;
    pdu_fill(n->x1 + 1, n->y1, n->x2, n->y2, pdu_cbar);
    os88_set_color(OS88_BLACK);
    os88_gfx_vline(n->x1, n->y1, n->y2);
    os88_font_run(n->x1 + 4, n->y1 + 5, "Notes & edits", OS88_BLACK,
                  pdu_cbar);
    for (i = 0; i < pdu_L.nctl; i++)
        if (pdu_L.ctl[i].id == C_NP_CLOSE || pdu_L.ctl[i].id == C_NP_FILTER)
            pdu_ctl_paint(i);
    o = &pdu_tr;
    pdu_notes_list(o);
    paper = pdu_mono ? OS88_WHITE : pdu_cbar;
    pdu_fill(o->x1, o->y1, o->x2, o->y2, paper);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(o->x1, o->y1, o->x2, o->y2);
    pdu_notes_body(o, paper);
}

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
    for (i = 0; i < pdu_L.nctl; i++)
        if (pdu_L.ctl[i].flags & CF_EMPTY)
            pdu_ctl_paint(i);
}

/* paint the canvas inside clip: the desk around the page, the page's white
 * (PDU_PREFILL: the stub renderer draws only ink), and ask the render job
 * for the page part. Never draws the page's content itself. */
#define PDU_PREFILL 1
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
    pdu_page_rect(&pdu_pr);
    /* the desk: above, below, left, right of the page, each cut to c */
    pdu_desk(c->x1, c->y1, c->x2, (pdu_pr.y1 - 1 < c->y2 ? pdu_pr.y1 - 1 : c->y2));
    pdu_desk(c->x1, (pdu_pr.y2 + 1 > c->y1 ? pdu_pr.y2 + 1 : c->y1), c->x2, c->y2);
    if (pdu_isect(c, &pdu_pr, &pdu_tr)) {
        pdu_desk(c->x1, pdu_tr.y1, pdu_tr.x1 - 1, pdu_tr.y2);
        pdu_desk(pdu_tr.x2 + 1, pdu_tr.y1, c->x2, pdu_tr.y2);
#if PDU_PREFILL
        pdu_fill(pdu_tr.x1, pdu_tr.y1, pdu_tr.x2, pdu_tr.y2, OS88_WHITE);
#endif
        pdu_render_want(&pdu_tr);
    }
}

/* --- the popover (one at a time, inside the canvas) ------------------------ */
static void pdu_pop_paint(void);       /* pducmd.c owns their contents */

/* --- the window -------------------------------------------------------------- */
static void pdu_paint_rect(struct os88_rect *r)
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
    if (pdu_pop != P_NONE && pdu_isect(r, &pdu_poprect, o))
        pdu_pop_paint();
    if (pdu_about)
        os88_about_card_d(pdu_win, pdu_abtext);
    os88_wm_grow(pdu_win);
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
