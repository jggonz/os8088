/* ============================================================================
 * os8088 - apps/pagedesk/pdulay.c    THE LAYOUT (PAGEDESK-SPEC 17.1, 20.6.4)
 *
 * One pure function: the live content box, the panel wishes and the mode in;
 * every region and every control rectangle out. It draws nothing, calls
 * nothing and reads no global - so the same file is compiled on the host by
 * hosttest/pdu_lay.c and checked at VGA 640x480, EGA 640x350, Hercules
 * 720x348 AND 720x350, and CGA 640x200 (PD-04's two heights), and the target
 * and the test cannot hold different opinions about a pixel.
 *
 * THE LAYOUT IS CHOSEN FROM THE CONTENT BOX, NEVER FROM THE ADAPTER (17.1):
 * a VGA window dragged small gets MID or COMPACT exactly as a CGA one does.
 * ==========================================================================*/

/* labels: [FULL, COMPACT]. A field's text is drawn live; its entry gives the
 * WIDTH in characters ("999/999", "400%"). */
static const char *pdu_lab[C_NCTLID * 2] = {
    "", "",                         /* C_NONE */
    "Open", "Open",
    "Save As", "Save",
    "Undo", "Undo",
    "Redo", "Redo",
    "<", "<",
    "999/999", "999/999",           /* C_PAGE: width only */
    ">", ">",
    "-", "-",
    "400%", "400%",                 /* C_ZOOM: width only */
    "+", "+",
    "Fit", "Fit",
    "Find", "Find",
    "Read", "Read",
    "Annotate", "Annot",
    "Edit", "Edit",
    "Pages", "Pages",
    "Select", "Sel",
    "Highlight", "Hilite",
    "Note", "Note",
    "Text", "Text",
    "Mark", "Mark",
    "More", "More",
    "Ink", "Ink",
    "Line", "Line",
    "Rectangle", "Rect",
    "Ellipse", "Oval",
    "Sign", "Sign",
    "Delete", "Del",
    "Rotate L", "RotL",
    "Rotate R", "RotR",
    "Delete", "Del",
    "Blank", "Blank",
    "Extract", "Extr",
    "Pages", "Pages",
    "Outline", "Outline",
    "<", "<",
    "x", "x",
    "This page", "This page",       /* drawn live: This page / All pages */
    "Pg", "Pg",
    "Notes", "Notes",
    "Open...", "Open...",
    "Sample guide", "Sample guide"
};

static int pdu_strlen0(const char *s)
{
    int n;
    n = 0;
    while (s[n])
        n++;
    return n;
}

/* a mode's tools, in bar order, 0-terminated; COMPACT folds Mark into More */
static const int pdu_tools_read[]  = { C_T_SELECT, 0 };
static const int pdu_tools_annot[] = { C_T_SELECT, C_T_HILITE, C_T_NOTE,
                                       C_T_TEXT, C_T_MARK, C_T_MORE, 0 };
static const int pdu_tools_annotc[] = { C_T_SELECT, C_T_HILITE, C_T_NOTE,
                                        C_T_TEXT, C_T_MORE, 0 };
static const int pdu_tools_edit[]  = { C_T_SELECT, C_T_TEXT, C_E_DELETE, 0 };
static const int pdu_tools_pages[] = { C_P_ROTL, C_P_ROTR, C_P_DELETE,
                                       C_P_BLANK, C_P_EXTRACT, 0 };

static const int *pdu_tools(int mode, int compact)
{
    if (mode == PDU_MODE_ANNOT)
        return compact ? pdu_tools_annotc : pdu_tools_annot;
    if (mode == PDU_MODE_EDIT)
        return pdu_tools_edit;
    if (mode == PDU_MODE_PAGES)
        return pdu_tools_pages;
    return pdu_tools_read;
}

static void pdu_rset(struct os88_rect *r, int x1, int y1, int x2, int y2)
{
    r->x1 = x1; r->y1 = y1; r->x2 = x2; r->y2 = y2;
}

/* the cursor a bar is laid out with */
static int lay_x, lay_xr, lay_y1, lay_y2, lay_pad, lay_gap, lay_full;

/* place control `id` at the cursor; 0 when it does not fit (it is then not
 * laid out at all - every bar control is also in a menu or on a key) */
static int lay_put(struct pdu_lay *L, int id, int flags)
{
    int wch, w;
    struct pdu_ctl *c;

    if (L->nctl >= PDU_MAXCTL)
        return 0;
    wch = pdu_strlen0(pdu_lab[id * 2 + (lay_full ? 0 : 1)]);
    w = wch * PDU_CW + lay_pad;
    if (lay_x + w - 1 > lay_xr)
        return 0;
    c = &L->ctl[L->nctl];
    c->id = id;
    c->x1 = lay_x;
    c->x2 = lay_x + w - 1;
    c->y1 = lay_y1;
    c->y2 = lay_y2;
    c->flags = flags;
    L->nctl++;
    lay_x = lay_x + w + lay_gap;
    return 1;
}

/* put a control at the RIGHT end of the bar, moving the right limit in */
static int lay_putr(struct pdu_lay *L, int id, int flags)
{
    int w;
    struct pdu_ctl *c;

    if (L->nctl >= PDU_MAXCTL)
        return 0;
    w = pdu_strlen0(pdu_lab[id * 2 + (lay_full ? 0 : 1)]) * PDU_CW + lay_pad;
    if (lay_xr - w + 1 < lay_x)
        return 0;
    c = &L->ctl[L->nctl];
    c->id = id;
    c->x2 = lay_xr;
    c->x1 = lay_xr - w + 1;
    c->y1 = lay_y1;
    c->y2 = lay_y2;
    c->flags = flags;
    L->nctl++;
    lay_xr = c->x1 - 1 - lay_gap;
    return 1;
}

static void lay_bar(struct os88_rect *bar, int full)
{
    lay_full = full;
    lay_x = bar->x1 + (full ? 4 : 2);
    lay_xr = bar->x2 - (full ? 4 : 2);
    lay_y1 = bar->y1 + 1;
    lay_y2 = bar->y2 - 1;
    lay_pad = full ? 12 : 6;
    lay_gap = full ? 2 : 1;
}

static void lay_group(void)
{
    lay_x = lay_x + (lay_full ? 8 : 5);
}

/* the layout kind for a content box (17.1; 20.6.4 for the 260) */
int pdu_kind(int w, int h)
{
    if (w >= PDU_FULL_W && h >= PDU_FULL_H)
        return PDU_L_FULL;
    if (h >= PDU_MID_H && w >= PDU_FULL_W)
        return PDU_L_MID;
    return PDU_L_COMPACT;
}

/* pdu_layout - every region and control.
 *   ox, oy, w, h      the content box (os88_wm_content / os88_wm_geom)
 *   side_on, notes_on the user's panel wishes for this layout kind
 *   mode              PDU_MODE_*
 *   empty             1 = no document: the empty view's two buttons
 * The panels actually shown come back in L->side / L->notes (x1 > x2 = not
 * shown): a canvas that would fall under PDU_MINCANVAS folds them, Notes
 * first (PD-04: "collapse panels at narrow window sizes"). */
void pdu_layout(struct pdu_lay *L, int ox, int oy, int w, int h,
                int side_on, int notes_on, int mode, int empty)
{
    int x2, y2, top, bot, full, cx1, cx2, avail, cw, sbw;
    const int *t;
    struct os88_rect *b;

    L->ox = ox; L->oy = oy; L->w = w; L->h = h;
    L->nctl = 0;
    x2 = ox + w - 1;
    y2 = oy + h - 1;

    L->kind = pdu_kind(w, h);
    full = (L->kind != PDU_L_COMPACT);

    if (full) {
        pdu_rset(&L->bar1, ox, oy, x2, oy + PDU_BAR_F - 1);
        pdu_rset(&L->bar2, ox, oy + PDU_BAR_F, x2, oy + 2 * PDU_BAR_F - 1);
        top = oy + 2 * PDU_BAR_F;
        pdu_rset(&L->status, ox, y2 - PDU_STAT_F + 1, x2, y2);
    } else {
        pdu_rset(&L->bar1, ox + 1, oy, ox, oy - 1);           /* absent */
        pdu_rset(&L->bar2, ox, oy, x2, oy + PDU_BAR_C - 1);
        top = oy + PDU_BAR_C;
        pdu_rset(&L->status, ox, y2 - PDU_STAT_C + 1, x2, y2);
    }
    bot = L->status.y1 - 1;

    /* --- panels: COMPACT has none docked (17.1: popovers instead) ------- */
    if (!full) {
        side_on = 0;
        notes_on = 0;
    }
    for (;;) {
        cx1 = ox + (side_on ? PDU_SIDE_W : 0);
        avail = x2 + 1 - (notes_on ? PDU_NOTE_W : 0) - PDU_SBAR_W - cx1;
        if (avail >= PDU_MINCANVAS || (!side_on && !notes_on))
            break;
        if (notes_on)
            notes_on = 0;
        else
            side_on = 0;
    }
    if (avail < 8)
        avail = 8;
    cw = avail & ~7;                    /* x2+1 a multiple of 8 (gfx_scroll) */
    cx2 = cx1 + cw - 1;
    sbw = PDU_SBAR_W;
    if (!notes_on)
        sbw = sbw + (avail - cw);       /* the odd pixels go to the bar... */
    if (side_on)
        pdu_rset(&L->side, ox, top, ox + PDU_SIDE_W - 1, bot);
    else
        pdu_rset(&L->side, ox + 1, top, ox, bot);
    pdu_rset(&L->canvas, cx1, top, cx2, bot);
    pdu_rset(&L->sbar, cx2 + 1, top, cx2 + sbw, bot);
    if (notes_on)                       /* ...or to Notes & edits */
        pdu_rset(&L->notes, cx2 + sbw + 1, top, x2, bot);
    else
        pdu_rset(&L->notes, x2 + 1, top, x2, bot);

    /* --- the file/navigation bar (17.2) ---------------------------------- */
    if (full) {
        lay_bar(&L->bar1, 1);
        lay_put(L, C_OPEN, CF_BAR1);
        lay_put(L, C_SAVEAS, CF_BAR1);
        lay_group();
        lay_put(L, C_UNDO, CF_BAR1);
        lay_put(L, C_REDO, CF_BAR1);
        lay_group();
        lay_put(L, C_PREV, CF_BAR1);
        lay_put(L, C_PAGE, CF_BAR1 | CF_FIELD);
        lay_put(L, C_NEXT, CF_BAR1);
        lay_group();
        lay_put(L, C_ZOUT, CF_BAR1);
        lay_put(L, C_ZOOM, CF_BAR1 | CF_FIELD);
        lay_put(L, C_ZIN, CF_BAR1);
        lay_put(L, C_FIT, CF_BAR1);
        lay_group();
        lay_put(L, C_FIND, CF_BAR1);
        b = &L->bar2;
        lay_bar(b, 1);
    } else {
        /* COMPACT: ONE combined bar - navigation, zoom, modes, tools; the
         * file commands are the menu bar's (17.2) */
        b = &L->bar2;
        lay_bar(b, 0);
        lay_putr(L, C_PANEL_NT, CF_BAR2);
        lay_putr(L, C_PANEL_PG, CF_BAR2);
        lay_put(L, C_PREV, CF_BAR2);
        lay_put(L, C_PAGE, CF_BAR2 | CF_FIELD);
        lay_put(L, C_NEXT, CF_BAR2);
        lay_group();
        lay_put(L, C_ZOUT, CF_BAR2);
        lay_put(L, C_ZOOM, CF_BAR2 | CF_FIELD);
        lay_put(L, C_ZIN, CF_BAR2);
        lay_group();
    }

    /* --- modes, then the mode's tools ------------------------------------ */
    lay_put(L, C_M_READ, CF_BAR2);
    lay_x = lay_x - lay_gap;            /* the four modes abut, as a tab row */
    lay_put(L, C_M_ANNOT, CF_BAR2);
    lay_x = lay_x - lay_gap;
    lay_put(L, C_M_EDIT, CF_BAR2);
    lay_x = lay_x - lay_gap;
    lay_put(L, C_M_PAGES, CF_BAR2);
    lay_group();
    t = pdu_tools(mode, !full);
    while (*t) {
        lay_put(L, *t, CF_BAR2);
        t++;
    }

    /* --- the panels' own controls ---------------------------------------- */
    if (side_on) {
        lay_bar(&L->side, 1);
        lay_y1 = L->side.y1 + 2;
        lay_y2 = lay_y1 + PDU_BAR_F - 3;
        lay_put(L, C_SB_PAGES, CF_PANEL);
        lay_x = lay_x - lay_gap;
        lay_put(L, C_SB_OUTL, CF_PANEL);
        lay_y1 = L->side.y2 - PDU_BAR_F + 2;
        lay_y2 = L->side.y2 - 2;
        lay_putr(L, C_SB_HIDE, CF_PANEL);
    }
    if (notes_on) {
        lay_bar(&L->notes, 1);
        lay_y1 = L->notes.y1 + 2;
        lay_y2 = lay_y1 + PDU_BAR_F - 3;
        lay_putr(L, C_NP_CLOSE, CF_PANEL);
        lay_y1 = lay_y2 + 4;
        lay_y2 = lay_y1 + PDU_BAR_F - 3;
        lay_put(L, C_NP_FILTER, CF_PANEL);
    }

    /* --- the empty view: Open and the sample guide, centred ------------- */
    if (empty) {
        int cy, bw1, bw2, gx;
        bw1 = 7 * PDU_CW + 12;
        bw2 = 12 * PDU_CW + 12;
        gx = (L->canvas.x1 + L->canvas.x2 + 1 - bw1 - bw2 - 8) / 2;
        cy = (L->canvas.y1 + L->canvas.y2) / 2 + 4;
        lay_full = 1;
        lay_pad = 12;
        lay_gap = 8;
        lay_x = gx;
        lay_xr = L->canvas.x2;
        lay_y1 = cy;
        lay_y2 = cy + PDU_BAR_F - 3;
        lay_put(L, C_EV_OPEN, CF_EMPTY);
        lay_put(L, C_EV_SAMPLE, CF_EMPTY);
    }
}

/* the control at (x, y), or C_NONE; its index in *idx */
int pdu_hit(struct pdu_lay *L, int x, int y, int *idx)
{
    int i;
    struct pdu_ctl *c;

    for (i = 0; i < L->nctl; i++) {
        c = &L->ctl[i];
        if (x >= c->x1 && x <= c->x2 && y >= c->y1 && y <= c->y2) {
            *idx = i;
            return c->id;
        }
    }
    *idx = -1;
    return C_NONE;
}

/* the laid-out index of control id, -1 = not on the glass */
int pdu_find(struct pdu_lay *L, int id)
{
    int i;
    for (i = 0; i < L->nctl; i++)
        if (L->ctl[i].id == id)
            return i;
    return -1;
}
