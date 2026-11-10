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
 * WIDTH in characters ("999", "400%"). With the kit (20.21.2) a picture-only
 * control (CF_NOCAP) shows no caption; these are its words without the kit. */
static const char *pdu_lab[C_NCTLID * 2] = {
    "", "",                         /* C_NONE (a separator) */
    "Open", "Open",
    "Save As", "Save",
    "Undo", "Undo",
    "Redo", "Redo",
    "<", "<",
    "999", "999/999",               /* C_PAGE: width only */
    ">", ">",
    "-", "-",
    "400%", "400%",                 /* C_ZOOM: width only */
    "+", "+",
    "Fit width", "Fit",             /* drawn live: Fit width / Fit page / Zoom */
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
    "Shape", "More",
    "Draw", "Ink",
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
    "\x11", "<",
    "\x9E", "x",
    "This page", "This page",       /* drawn live: This page / All pages */
    "Pg", "Pg",
    "Notes", "Notes",
    "Open...", "Open...",
    "Sample guide", "Sample guide",
    "of 999", "of 999"              /* C_PGOF: drawn live */
};

/* each control's look on the FULL/MID bars (20.21.4); COMPACT draws every
 * one as the first build did. Without the kit CF_ICON and CF_NOCAP fall
 * away and the caption is drawn. */
static const unsigned pdu_lsty[C_NCTLID] = {
    CF_SEP,                                     /* C_NONE */
    CF_ICON, CF_ICON,                           /* Open, Save As */
    CF_ICON | CF_NOCAP, CF_ICON | CF_NOCAP,     /* Undo, Redo */
    CF_ICON | CF_NOCAP, CF_FIELD,               /* Prev, the page field */
    CF_ICON | CF_NOCAP,                         /* Next */
    CF_ICON | CF_NOCAP, CF_FIELD,               /* zoom out, the zoom field */
    CF_ICON | CF_NOCAP, CF_DROP, CF_ICON,       /* zoom in, Fit, Find */
    0, 0, 0, 0,                                 /* the four modes */
    CF_FLAT | CF_ICON, CF_FLAT | CF_ICON,       /* Select, Highlight */
    CF_FLAT | CF_ICON, CF_FLAT | CF_ICON,       /* Note, Text */
    CF_FLAT | CF_ICON, CF_FLAT | CF_DROP | CF_ICON, /* Mark, Shape */
    CF_FLAT | CF_ICON, CF_FLAT | CF_ICON,       /* Draw (Ink), Line */
    CF_FLAT | CF_ICON, CF_FLAT | CF_ICON,       /* Rectangle, Ellipse */
    CF_FLAT | CF_ICON,                          /* Sign */
    CF_FLAT | CF_ICON,                          /* Edit's Delete */
    CF_FLAT | CF_ICON, CF_FLAT | CF_ICON,       /* Rotate L, Rotate R */
    CF_FLAT | CF_ICON, CF_FLAT | CF_ICON,       /* Delete, Blank */
    CF_FLAT | CF_ICON,                          /* Extract */
    0, 0, 0, 0, CF_DROP,                        /* tabs, hide, close, filter */
    0, 0,                                       /* COMPACT's Pg, Notes */
    CF_ICON, 0,                                 /* the empty view's two */
    CF_LABEL                                    /* of 12 */
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

/* the FULL/MID bar's Annotate tools, the design's order (20.21.1): Mark,
 * Line, Rectangle and Ellipse are Shape's list. The keys 1-6 keep the
 * first build's order (pdu_tools), so 5 is still Mark */
static const int pdu_tools_annotb[] = { C_T_SELECT, C_T_HILITE, C_T_NOTE,
                                        C_T_TEXT, C_T_INK, C_T_MORE,
                                        C_T_SIGN, 0 };

static const int *pdu_tools(int mode, int compact);
static const int *pdu_bartools(int mode, int compact)
{
    if (mode == PDU_MODE_ANNOT && !compact)
        return pdu_tools_annotb;
    return pdu_tools(mode, compact);
}

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
static int lay_x, lay_xr, lay_y1, lay_y2, lay_pad, lay_gap, lay_full, lay_kit;

/* a control's width (20.21.4): measured from its caption in the face's
 * advance - 6 with the kit, the kernel's 8 without it and on COMPACT */
static int lay_w(int id)
{
    int n, adv, w;
    unsigned st;

    n = pdu_strlen0(pdu_lab[id * 2 + (lay_full ? 0 : 1)]);
    if (!lay_full)
        return n * PDU_CW + lay_pad;
    st = pdu_lsty[id];
    adv = lay_kit ? 6 : PDU_CW;
    if (!lay_kit)
        st &= ~(CF_ICON | CF_NOCAP);
    if (st & CF_LABEL)
        return n * adv + 4;
    if (st & CF_FIELD)
        return n * adv + 10;
    w = (st & CF_NOCAP) ? 0 : n * adv;
    if (st & CF_DROP)
        w = w + adv + 4;
    if (st & CF_ICON)
        w = w + 16 + ((st & CF_NOCAP) ? 0 : 4);
    if (st & CF_FLAT)
        return w + 6;                   /* a frame and two of paper a side */
    if (st & CF_NOCAP)
        return w + 8;                   /* the bevel's two and two of paper */
    return w + ((st & CF_ICON) ? 16 : 12);
}

static unsigned lay_flags(int id, int flags)
{
    unsigned st;
    if (!lay_full)
        return flags;
    st = pdu_lsty[id] & ~CF_SEP;
    if (!lay_kit)
        st &= ~(CF_ICON | CF_NOCAP);
    return flags | st;
}

/* place control `id` at the cursor; 0 when it does not fit (it is then not
 * laid out at all - every bar control is also in a menu or on a key) */
static int lay_put(struct pdu_lay *L, int id, int flags)
{
    int w;
    struct pdu_ctl *c;

    if (L->nctl >= PDU_MAXCTL)
        return 0;
    w = lay_w(id);
    if (lay_x + w - 1 > lay_xr)
        return 0;
    c = &L->ctl[L->nctl];
    c->id = id;
    c->x1 = lay_x;
    c->x2 = lay_x + w - 1;
    c->y1 = lay_y1;
    c->y2 = lay_y2;
    c->flags = lay_flags(id, flags);
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
    w = lay_w(id);
    if (lay_xr - w + 1 < lay_x)
        return 0;
    c = &L->ctl[L->nctl];
    c->id = id;
    c->x2 = lay_xr;
    c->x1 = lay_xr - w + 1;
    c->y1 = lay_y1;
    c->y2 = lay_y2;
    c->flags = lay_flags(id, flags);
    L->nctl++;
    lay_xr = c->x1 - 1 - lay_gap;
    return 1;
}

static void lay_bar(struct os88_rect *bar, int full)
{
    lay_full = full;
    lay_x = bar->x1 + (full ? 6 : 2);
    lay_xr = bar->x2 - (full ? 6 : 2);
    lay_y1 = bar->y1 + 1;
    lay_y2 = bar->y2 - 1;
    if (full && bar->y2 - bar->y1 + 1 > PDU_BTN_H + 2) {
        lay_y1 = bar->y1 + 2;           /* the file bar: 24 rows, buttons 20 */
        lay_y2 = lay_y1 + PDU_BTN_H - 1;
    }
    lay_pad = full ? 12 : 6;
    lay_gap = full ? 4 : 1;
}

/* a group ends: on FULL/MID an etched rule between it and the next
 * (20.21.4), a separator "control" that is never hit (id C_NONE) */
static void lay_group(struct pdu_lay *L, int flag)
{
    struct pdu_ctl *c;
    if (!lay_full) {
        lay_x = lay_x + 5;
        return;
    }
    if (L->nctl < PDU_MAXCTL && lay_x + 12 <= lay_xr) {
        c = &L->ctl[L->nctl];
        c->id = C_NONE;
        c->x1 = lay_x + 4;
        c->x2 = lay_x + 5;
        c->y1 = lay_y1;
        c->y2 = lay_y2;
        c->flags = flag | CF_SEP;
        L->nctl++;
    }
    lay_x = lay_x + 10;
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
 *   kit               1 = the face kit is loaded: pictures, the 6-pixel face
 * The panels actually shown come back in L->side / L->notes (x1 > x2 = not
 * shown): a canvas that would fall under PDU_MINCANVAS folds them, Notes
 * first (PD-04: "collapse panels at narrow window sizes"). */
void pdu_layout(struct pdu_lay *L, int ox, int oy, int w, int h,
                int side_on, int notes_on, int mode, int empty, int kit)
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
    lay_kit = kit;

    if (full) {
        pdu_rset(&L->bar1, ox, oy, x2, oy + PDU_BAR1_F - 1);
        pdu_rset(&L->bar2, ox, oy + PDU_BAR1_F, x2,
                 oy + PDU_BAR1_F + PDU_BAR2_F - 1);
        top = oy + PDU_BAR1_F + PDU_BAR2_F;
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
        /* the design's file bar (20.21.1): Open, Save As | Undo Redo |
         * Prev [n] of N Next | - [zoom] + Fit | ... Find at the right end.
         * A pair that abuts takes its gap back */
        lay_bar(&L->bar1, 1);
        lay_putr(L, C_FIND, CF_BAR1);
        lay_put(L, C_OPEN, CF_BAR1);
        lay_put(L, C_SAVEAS, CF_BAR1);
        lay_group(L, CF_BAR1);
        if (lay_put(L, C_UNDO, CF_BAR1))
            lay_x = lay_x - lay_gap;
        lay_put(L, C_REDO, CF_BAR1);
        lay_group(L, CF_BAR1);
        lay_put(L, C_PREV, CF_BAR1);
        lay_put(L, C_PAGE, CF_BAR1 | CF_FIELD);
        lay_put(L, C_PGOF, CF_BAR1);
        lay_put(L, C_NEXT, CF_BAR1);
        lay_group(L, CF_BAR1);
        if (lay_put(L, C_ZOUT, CF_BAR1))
            lay_x = lay_x - lay_gap;
        if (lay_put(L, C_ZOOM, CF_BAR1 | CF_FIELD))
            lay_x = lay_x - lay_gap;
        lay_put(L, C_ZIN, CF_BAR1);
        lay_put(L, C_FIT, CF_BAR1);
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
        lay_group(L, CF_BAR2);
        lay_put(L, C_ZOUT, CF_BAR2);
        lay_put(L, C_ZOOM, CF_BAR2 | CF_FIELD);
        lay_put(L, C_ZIN, CF_BAR2);
        lay_group(L, CF_BAR2);
    }

    /* --- modes, then the mode's tools ------------------------------------ */
    /* the four modes abut, as a tab row: the gap is taken back only after a
     * control that was placed */
    if (lay_put(L, C_M_READ, CF_BAR2))
        lay_x = lay_x - lay_gap;
    if (lay_put(L, C_M_ANNOT, CF_BAR2))
        lay_x = lay_x - lay_gap;
    if (lay_put(L, C_M_EDIT, CF_BAR2))
        lay_x = lay_x - lay_gap;
    lay_put(L, C_M_PAGES, CF_BAR2);
    lay_group(L, CF_BAR2);
    if (full)
        lay_gap = 2;
    t = pdu_bartools(mode, !full);
    while (*t) {
        lay_put(L, *t, CF_BAR2);
        t++;
    }

    /* --- the panels' own controls ---------------------------------------- */
    if (side_on) {
        lay_bar(&L->side, 1);
        lay_y1 = L->side.y1 + 4;
        lay_y2 = lay_y1 + 18 - 1;
        if (lay_put(L, C_SB_PAGES, CF_PANEL))
            lay_x = lay_x - lay_gap;
        lay_put(L, C_SB_OUTL, CF_PANEL);
        /* the bottom row is a NEW row: its left edge is the panel's again
         * (the first build left lay_x past Outline, so lay_putr found no room
         * and the hide button was never laid out - the Pages panel could be
         * hidden only from the View menu) */
        lay_x = L->side.x1 + 4;
        lay_y1 = L->side.y2 - PDU_SIDE_FOOT + 4;
        lay_y2 = lay_y1 + 18 - 1;
        lay_putr(L, C_SB_HIDE, CF_PANEL);
    }
    if (notes_on) {
        lay_bar(&L->notes, 1);
        lay_y1 = L->notes.y1 + 4;
        lay_y2 = lay_y1 + 18 - 1;
        lay_putr(L, C_NP_CLOSE, CF_PANEL);
        lay_y1 = lay_y2 + 6;
        lay_y2 = lay_y1 + 18 - 1;
        lay_put(L, C_NP_FILTER, CF_PANEL);
    }

    /* --- the empty view: Open and the sample guide, centred ------------- */
    if (empty) {
        int cy, bw1, bw2, gx;
        lay_full = 1;
        lay_pad = 12;
        bw1 = lay_w(C_EV_OPEN);
        bw2 = lay_w(C_EV_SAMPLE);
        gx = (L->canvas.x1 + L->canvas.x2 + 1 - bw1 - bw2 - 8) / 2;
        cy = (L->canvas.y1 + L->canvas.y2) / 2 + 2;
        lay_gap = 8;
        lay_x = gx;
        lay_xr = L->canvas.x2;
        lay_y1 = cy;
        lay_y2 = cy + PDU_BTN_H - 1;
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
        if (c->flags & (CF_SEP | CF_LABEL))
            continue;                   /* drawn, never pressed */
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
