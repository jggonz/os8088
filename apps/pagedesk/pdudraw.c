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

/* the face's pens (20.21.4): the bevel's light and dark lines. On a 1bpp
 * display grey is white, so a raised control is its frame with a black
 * bottom and right inside it */
static int pdu_chi, pdu_clo;

static void pdu_colors(void)
{
    pdu_mono = pdu_vid.bpp < 4;
    pdu_cbar = pdu_mono ? OS88_WHITE : OS88_LGRAY;
    pdu_cact = pdu_mono ? OS88_BLACK : OS88_CYAN;
    pdu_cdesk = OS88_DGRAY;
    pdu_chi = OS88_WHITE;
    pdu_clo = pdu_mono ? OS88_BLACK : OS88_DGRAY;
    pk_mode(pdu_mono);
}

/* the four lines of a bevel inside a frame at x1..x2, y1..y2: tl along the
 * top and left, br along the bottom and right, each ONE pixel, no corner
 * written twice */
static void pdu_bevel(int x1, int y1, int x2, int y2, int tl, int br)
{
    os88_set_color(tl);
    os88_gfx_hline(x1 + 1, x2 - 1, y1 + 1);
    os88_gfx_vline(x1 + 1, y1 + 2, y2 - 1);
    os88_set_color(br);
    os88_gfx_hline(x1 + 2, x2 - 1, y2 - 1);
    os88_gfx_vline(x2 - 1, y1 + 2, y2 - 2);
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
static struct os88_rect pdu_pr, pdu_cc;

/* a piece of the canvas's desk cut to the clip being painted (pdu_cc): the
 * desk's own ground (colour -1), or a solid colour - the page's shadow */
static struct os88_rect pdu_dk;
static void pdu_deskc(int x1, int y1, int x2, int y2, int colour)
{
    pdu_dk.x1 = x1;
    pdu_dk.y1 = y1;
    pdu_dk.x2 = x2;
    pdu_dk.y2 = y2;
    if (!pdu_isect(&pdu_dk, &pdu_cc, &pdu_dk))
        return;
    if (colour < 0)
        pdu_desk(pdu_dk.x1, pdu_dk.y1, pdu_dk.x2, pdu_dk.y2);
    else
        pdu_fill(pdu_dk.x1, pdu_dk.y1, pdu_dk.x2, pdu_dk.y2, colour);
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
            return pdu_L.kind == PDU_L_COMPACT ? "-/-" : "-";
        os88_utoa(pdu_page + 1, pdu_lbuf);
        if (pdu_L.kind != PDU_L_COMPACT)
            return pdu_lbuf;            /* "of N" is its own label (20.21.1) */
        n = os88_strlen(pdu_lbuf);
        pdu_lbuf[n] = '/';
        os88_utoa(pdu_npages, pdu_lbuf + n + 1);
        return pdu_lbuf;
    }
    if (id == C_PGOF) {
        if (pdu_doc != D_OPEN)
            return "of -";
        os88_strcpy(pdu_lbuf, "of ", sizeof(pdu_lbuf));
        os88_utoa(pdu_npages, pdu_lbuf + 3);
        return pdu_lbuf;
    }
    if (id == C_FIT && pdu_L.kind != PDU_L_COMPACT)
        return pdu_fit == 1 ? "Fit width" : pdu_fit == 2 ? "Fit page" : "Zoom";
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
    if (id == C_T_MORE && pdu_L.kind != PDU_L_COMPACT && pdu_doc == D_OPEN
        && (pdu_tool == C_T_MARK || (pdu_tool >= C_T_LINE && pdu_tool <= C_T_ELLIPSE)))
        return 1;                       /* 20.21.1: Shape's list holds the tool */
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

/* one control (20.21.4): its frame and bevel, then its whole INSIDE as one
 * composed band - picture, caption and the drop-down mark ORed into it and
 * put down in one call with its paper. NOTHING IS WRITTEN TWICE: the band
 * covers every pixel inside the bevel, so no ground is filled under it.
 *
 * AND NOTHING UNCHANGED IS WRITTEN AT ALL (CLAUDE.md, PERFORMANCE.md rules
 * 1-2): each laid-out control remembers what it last showed (pdu_cst: the
 * look and its colours; pdu_clab: the caption), and
 *   - the same look and caption: nothing
 *   - the same look, a new caption (the page field's 2 -> 3): the band, ONE
 *     call
 *   - anything else, or the ground was just filled (pdu_cground, set by the
 *     bar and panel painters round their loops): the frame, the bevel and the
 *     band - 6 calls raised, 2 a field or a tool
 * The cache is keyed on (id, x1, y1), so a relayout that moves or replaces
 * a control finds nothing and takes the full path. */
static int pdu_cground = -1;            /* the paper every control the caller
                                         * is about to paint stands on; -1 =
                                         * not known */
static int pdu_cid[PDU_MAXCTL], pdu_cx1[PDU_MAXCTL], pdu_cy1[PDU_MAXCTL];
static unsigned pdu_cst[PDU_MAXCTL];    /* 0 = nothing known */
static char pdu_clab[PDU_MAXCTL][16];
static int pdu_held = -1;               /* a raised button the mouse holds */

/* each control's picture, PK_I_* + 1 (0 none) */
static const unsigned char pdu_cico[C_NCTLID] = {
    0, PK_I_OPEN + 1, PK_I_SAVE + 1, PK_I_UNDO + 1, PK_I_REDO + 1,
    PK_I_PREV + 1, 0, PK_I_NEXT + 1, PK_I_ZOOMOUT + 1, 0, PK_I_ZOOMIN + 1,
    0, PK_I_FIND + 1, 0, 0, 0, 0,
    PK_I_SELECT + 1, PK_I_HIGHLIGHT + 1, PK_I_NOTE + 1, PK_I_TEXT + 1,
    PK_I_MARK + 1, PK_I_SHAPE + 1, PK_I_DRAW + 1, PK_I_LINE + 1,
    PK_I_RECT + 1, PK_I_ELLIPSE + 1, PK_I_SIGN + 1, PK_I_DELETE + 1,
    PK_I_ROTL + 1, PK_I_ROTR + 1, PK_I_DELETE + 1, PK_I_BLANK + 1,
    PK_I_EXTRACT + 1, 0, 0, 0, 0, 0, 0, 0, PK_I_OPEN + 1, 0, 0
};

/* put the band pk_begin opened down at screen x, y: ink on paper, greyed
 * (1bpp) when grey. ONE blit, through the SDK's thunk, and the pen put back
 * for the thumbnails' and the page's own BLIT1 bands */
static void pdu_kput(int x, int y, int ink, int paper, int grey)
{
    if (pk_pen(y, ink, paper, grey)) {
        os88_gfx_blit1_seg(pk_bseg, 0, pk_st, x, y, pk_w, pk_h);
        pk_pen0();
    }
}

/* compose a caption (and picture, and drop mark) centred in a band w x h at
 * screen x, and put it down at x, y in ink on paper. Its arguments are the
 * pdu_bc_* statics, set by the caller: a call with thirteen stacked
 * arguments was 26 bytes of the UI stack at the bottom of the Notes list's
 * chain (D22's 384, tools/os88pdstk.py). pdu_bc_cut = the caption, at most
 * the band's width. The shared tail of every control and label */
static int pdu_bc_x, pdu_bc_y, pdu_bc_w, pdu_bc_h, pdu_bc_ico, pdu_bc_ink;
static int pdu_bc_pap, pdu_bc_dis, pdu_bc_foc, pdu_bc_dxy, pdu_bc_f;
static unsigned pdu_bc_fl;
static const char *pdu_bc_cut;

static void pdu_bandcap(void)
{
    static int adv, n, cw, bx, ty, w, h;

    w = pdu_bc_w;
    h = pdu_bc_h;
    if (w <= 0 || h <= 0 || !pk_begin(w, h, pdu_bc_x))
        return;
    adv = pk_adv(pdu_bc_f);
    n = os88_strlen(pdu_bc_cut);
    cw = n * adv;
    if (pdu_bc_ico)
        cw = cw + 16 + (n ? 4 : 0);
    if (pdu_bc_fl & CF_DROP)
        cw = cw + adv + 4;
    bx = (w - cw) / 2 + pdu_bc_dxy;
    if (bx < 0)
        bx = 0;
    if (pdu_bc_ico) {
        pk_icon(bx, (h - 16) / 2 + pdu_bc_dxy, pdu_bc_ico - 1);
        bx = bx + 20;
    }
    ty = (h - pk_rows(pdu_bc_f)) / 2 + pdu_bc_dxy;
    bx = pk_text(bx, ty, pdu_bc_cut, pdu_bc_f);
    if (pdu_bc_fl & CF_DROP) {
        pdu_lbuf[0] = 0x1F;             /* the down triangle (20.21.2) */
        pdu_lbuf[1] = 0;
        pk_text(bx + 4, ty, pdu_lbuf, pdu_bc_f);
    }
    if (pdu_bc_foc) {                   /* PD-23: where Tab is, a ring */
        pk_hline(0, w - 1, 0);
        pk_hline(0, w - 1, h - 1);
        for (n = 1; n < h - 1; n++) {
            pk_hline(0, 0, n);
            pk_hline(w - 1, w - 1, n);
        }
    }
    if (pk_pen(pdu_bc_y, pdu_bc_ink, pdu_bc_pap, pdu_bc_dis && pdu_mono)) {
        os88_gfx_blit1_seg(pk_bseg, 0, pk_st, pdu_bc_x, pdu_bc_y, pk_w, pk_h);
        pk_pen0();                      /* pdu_kput's, inline: a frame less */
    }
}

/* a picture index for the overlay (PK_I_* + 1, 0 none): none without the kit */
static int pdu_kit_ico(int ico)
{
    return pk_haskit() ? ico : 0;
}

static void pdu_ctl_paint(int i)
{
    /* static: a click's repaint reaches here from deep in the overlay's
     * commit chain (D22's 384, tools/os88pdstk.py) */
    static struct pdu_ctl *c;
    static const char *s;
    static int act, dis, foc, ink, paper, n, k, ground, in, same, max, ico;
    static int held, compact, look;
    static unsigned st, fl;

    c = &pdu_L.ctl[i];
    fl = c->flags;
    if (fl & CF_SEP) {                  /* the etched rule: with its ground */
        if (pdu_cground >= 0) {
            os88_set_color(pdu_clo);
            os88_gfx_vline(c->x1, c->y1 + 1, c->y2 - 1);
            os88_set_color(pdu_chi);
            os88_gfx_vline(c->x2, c->y1 + 1, c->y2 - 1);
        }
        return;
    }
    compact = pdu_L.kind == PDU_L_COMPACT;
    s = pdu_label(c->id);
    act = pdu_active(c->id);
    dis = pdu_why(c->id) != 0;
    foc = (c->id == pdu_focus && !dis);  /* PD-23: where Tab is */
    held = (c->id == pdu_held && !act && !dis);
    ground = (fl & CF_EMPTY) ? OS88_WHITE : pdu_cbar;
    ico = (fl & CF_ICON) && pk_haskit() ? pdu_cico[c->id] : 0;
    /* the look: 0 label, 1 field, 2 flat tool, 3 raised, 4 plain (COMPACT) */
    look = compact ? 4 : (fl & CF_LABEL) ? 0 : (fl & CF_FIELD) ? 1
           : (fl & CF_FLAT) ? 2 : 3;
    if (act && !dis && look != 0) {
        paper = pdu_cact;
        ink = OS88_WHITE;
    } else {
        paper = look == 1 ? OS88_WHITE : ground;
        ink = OS88_BLACK;
    }
    if (dis && !pdu_mono)
        ink = OS88_DGRAY;
    in = look == 3 ? 2 : look == 0 ? 0 : 1;
    st = 1 | (act ? 2 : 0) | (dis ? 4 : 0) | (foc ? 8 : 0) | (held ? 16 : 0)
         | ((unsigned)paper << 5) | ((unsigned)ink << 9);
    /* the caption, cut to the inside */
    n = (fl & CF_NOCAP) && ico ? 0 : os88_strlen(s);
    k = compact ? PK_SYS : PK_CAP;
    max = c->x2 - c->x1 + 1 - 2 * in - (compact ? 0 : 4) - (ico ? 20 : 0)
          - ((fl & CF_DROP) ? pk_adv(k) + 4 : 0);
    max = max / pk_adv(k);
    if (n > max)
        n = max < 0 ? 0 : max;
    if (n > 15)
        n = 15;
    for (k = 0; k < n; k++)
        pdu_cut[k] = s[k];
    pdu_cut[n] = 0;
    same = pdu_cid[i] == c->id && pdu_cx1[i] == c->x1 && pdu_cy1[i] == c->y1
           && pdu_cst[i] == st;
    if (pdu_cground >= 0)
        same = 0;                       /* a fresh ground: all of it */
    else if (same) {
        for (k = 0; pdu_clab[i][k] == pdu_cut[k] && pdu_cut[k]; k++)
            ;
        if (pdu_clab[i][k] == pdu_cut[k])
            return;                     /* nothing changed */
        same = 2;                       /* the band alone */
    }
    if (!same && look != 0) {
        if (dis)
            os88_gfx_pen(1);
        else
            os88_set_color(OS88_BLACK);
        if (look == 2 && !act && !foc)  /* an idle tool has no frame */
            os88_set_color(ground);
        if (look != 2 || act || foc || pdu_cground < 0)
            os88_gfx_frame(c->x1, c->y1, c->x2, c->y2);
        if (dis)
            os88_gfx_pen(0);
        if (look == 3) {
            if (act || held)
                pdu_bevel(c->x1, c->y1, c->x2, c->y2, pdu_clo, paper);
            else
                pdu_bevel(c->x1, c->y1, c->x2, c->y2, pdu_chi, pdu_clo);
        }
    }
    pdu_bc_x = c->x1 + in;
    pdu_bc_y = c->y1 + in;
    pdu_bc_w = c->x2 - c->x1 + 1 - 2 * in;
    pdu_bc_h = c->y2 - c->y1 + 1 - 2 * in;
    pdu_bc_cut = pdu_cut;
    pdu_bc_fl = compact ? 0 : fl;
    pdu_bc_ico = ico;
    pdu_bc_ink = ink;
    pdu_bc_pap = paper;
    pdu_bc_dis = dis;
    pdu_bc_foc = foc && look != 0;
    pdu_bc_dxy = held ? 1 : 0;
    pdu_bc_f = compact ? PK_SYS : PK_CAP;
    pdu_bandcap();
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
    /* 20.21.4: the ground, a white rule along its top and a dark one along
     * its bottom (COMPACT: the black rule the first build drew) */
    if (pdu_L.kind == PDU_L_COMPACT) {
        pdu_fill(b->x1, b->y1, b->x2, b->y2 - 1, pdu_cbar);
        os88_set_color(OS88_BLACK);
    } else {
        os88_set_color(pdu_chi);
        os88_gfx_hline(b->x1, b->x2, b->y1);
        pdu_fill(b->x1, b->y1 + 1, b->x2, b->y2 - 1, pdu_cbar);
        os88_set_color(pdu_clo);
    }
    os88_gfx_hline(b->x1, b->x2, b->y2);
    pdu_cground = pdu_cbar;
    for (i = 0; i < pdu_L.nctl; i++)
        if (pdu_L.ctl[i].flags & flag)
            pdu_ctl_paint(i);
    pdu_cground = -1;
}

/* --- the status line: ONE font_run of a composed line (20.6.4) ----------- */
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

/* one line in font f, cut to w pixels, as ONE band at x, y - its ground and
 * its ink in the one call (20.21.3) */
static void pdu_ptext(int x, int y, int w, const char *s, int f, int ink,
                      int paper)
{
    int n, k;
    n = w / pk_adv(f);
    if (n > (int)sizeof(pdu_cut) - 1)
        n = sizeof(pdu_cut) - 1;
    for (k = 0; k < n && s[k]; k++)
        pdu_cut[k] = s[k];
    pdu_cut[k] = 0;
    if (pk_begin(w, pk_rows(f), x)) {
        pk_text(0, 0, pdu_cut, f);
        pdu_kput(x, y, ink, paper, 0);
    }
}

/* a sunken well: dark top and left, light bottom and right (20.21.6) */
static void pdu_sunken(int x1, int y1, int x2, int y2)
{
    os88_set_color(pdu_clo);
    os88_gfx_hline(x1, x2, y1);
    os88_gfx_vline(x1, y1 + 1, y2);
    os88_set_color(pdu_mono ? OS88_BLACK : pdu_chi);
    os88_gfx_hline(x1 + 1, x2, y2);
    os88_gfx_vline(x2, y1 + 1, y2 - 1);
}

/* --- the status line (20.21.8): ONE band, three fields ------------------- */
/* the window's grow box sits in the content's bottom-right corner
 * (os88_wm_grow), so the band stops PDU_GROW_W short of the edge */
static int pdu_status_w(void)
{
    int w;
    w = pdu_L.status.x2 - pdu_L.status.x1 + 1 - PDU_GROW_W;
    return w < 0 ? 0 : w;
}

/* s, cut to n characters, at band x in font f */
static int pdu_stext(int x, const char *s, int n, int f)
{
    int k;
    if (n > (int)sizeof(pdu_cut) - 1)
        n = sizeof(pdu_cut) - 1;
    for (k = 0; k < n && s[k]; k++)
        pdu_cut[k] = s[k];
    pdu_cut[k] = 0;
    return pk_text(x, (pdu_L.status.y2 - pdu_L.status.y1 - pk_rows(f)) / 2,
                   pdu_cut, f);
}

static void pdu_status_paint(void)
{
    struct os88_rect *r;
    int w, f, adv, n, cx, rx, lw;
    const char *keys;

    r = &pdu_L.status;
    w = pdu_status_w();
    f = pdu_L.kind == PDU_L_COMPACT ? PK_SYS : PK_CAP;
    adv = pk_adv(f);
    if (!pk_begin(w, r->y2 - r->y1, r->x1))
        return;
    /* right: the keys the mode answers to (FULL and MID) */
    rx = w;
    if (pdu_L.kind != PDU_L_COMPACT) {
        keys = pdu_doc == D_OPEN ? "Ctrl+S Save As | Esc Select"
                                 : "Ctrl+O Open";
        n = os88_strlen(keys);
        rx = w - 4 - n * adv;
        if (rx > w / 2)
            pdu_stext(rx, keys, n, f);
        else
            rx = w;
    }
    /* centre: the page, when there is room for it */
    cx = rx;
    if (pdu_doc == D_OPEN && pdu_L.kind != PDU_L_COMPACT) {
        os88_strcpy(pdu_line, "Page ", sizeof(pdu_line));
        os88_utoa(pdu_page + 1, pdu_num);
        pdu_cat(pdu_line, pdu_num, sizeof(pdu_line));
        pdu_cat(pdu_line, " of ", sizeof(pdu_line));
        os88_utoa(pdu_npages, pdu_num);
        pdu_cat(pdu_line, pdu_num, sizeof(pdu_line));
        n = os88_strlen(pdu_line);
        lw = os88_strlen(pdu_msg) * adv + 4 + 2 * adv;
        cx = (w - n * adv) / 2;
        if (cx > lw && cx + n * adv + adv < rx)
            pdu_stext(cx, pdu_line, n, f);
        else
            cx = rx;
    }
    /* left: the message, cut where the centre or the right begins */
    pdu_stext(4, pdu_msg, (cx - 4 - adv) / adv, f);
    pdu_kput(r->x1, r->y1 + 1, OS88_BLACK, pdu_cbar, 0);
}

/* the whole status strip: the rule above it, the band, and the ground right
 * of the band where the grow box goes - every pixel once */
static void pdu_status_full(void)
{
    struct os88_rect *r;
    r = &pdu_L.status;
    os88_set_color(pdu_clo);
    os88_gfx_hline(r->x1, r->x2, r->y1);
    pdu_fill(r->x1 + pdu_status_w(), r->y1 + 1, r->x2, r->y2, pdu_cbar);
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
    pdu_fill(cx - w / 2, cy - 34, cx + w / 2, cy + PDU_BTN_H + 6, OS88_WHITE);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(cx - w / 2, cy - 34, cx + w / 2, cy + PDU_BTN_H + 6);
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
    /* the desk round the page AND its shadow (20.21.5): above, below, left,
     * right of the page-and-shadow box, the box's two corners the shadow
     * leaves, and the shadow - each cut to c */
    pdu_deskc(c->x1, c->y1, c->x2, pdu_pr.y1 - 1, -1);
    pdu_deskc(c->x1, pdu_pr.y2 + 3, c->x2, c->y2, -1);
    pdu_deskc(c->x1, pdu_pr.y1, pdu_pr.x1 - 1, pdu_pr.y2 + 2, -1);
    pdu_deskc(pdu_pr.x2 + 3, pdu_pr.y1, c->x2, pdu_pr.y2 + 2, -1);
    pdu_deskc(pdu_pr.x2 + 1, pdu_pr.y1, pdu_pr.x2 + 2, pdu_pr.y1 + 1, -1);
    pdu_deskc(pdu_pr.x1, pdu_pr.y2 + 1, pdu_pr.x1 + 1, pdu_pr.y2 + 2, -1);
    pdu_deskc(pdu_pr.x2 + 1, pdu_pr.y1 + 2, pdu_pr.x2 + 2, pdu_pr.y2 + 2,
              OS88_BLACK);
    pdu_deskc(pdu_pr.x1 + 2, pdu_pr.y2 + 1, pdu_pr.x2, pdu_pr.y2 + 2,
              OS88_BLACK);
    if (pdu_isect(c, &pdu_pr, &pdu_tr)) {
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
