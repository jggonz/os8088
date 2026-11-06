/* ============================================================================
 * os8088 - apps/pagedesk/pducmd.c   COMMANDS, MENUS, KEYS, CLICKS
 *                                   (PAGEDESK-SPEC 17.2, 17.5, 20.6.6; PD-23)
 *
 * Every visible control either acts or is greyed WITH A REASON (PD-02): one
 * table, pdu_why(), answers both "draw it grey?" and "what does the status
 * line say when it is pressed anyway?", so the picture and the explanation
 * cannot disagree. The menus are synced from the same table.
 * ==========================================================================*/

static void pdu_pop_close(void);
static void pdu_pop_shut(void);
static void pdu_popdmg_add(struct os88_rect *r);
static void pdu_about_off(void);
static void ovl_card(int which, struct os88_rect *r, int mono);
static int ovl_card_size(int which);
static int ovl_card_more(int which);
static int pdu_ovrows;
static int pdu_hpage;

/* --- the menus (17.2: no Print, 1.1). A literal starts with OS88_MENU_DIS
 * and an ENABLED item is the same pointer + 1, so one string serves both. */
#define MI_OPEN    0
#define MI_SAMPLE  1
#define MI_NEW     2
#define MI_SAVEAS  3
#define MI_CLOSE   4
#define MI_EXTRACT 5                    /* pages: File 5-8, Edit 8-10, Document 9-10 (pdupage.c's pdu_pgmrow) */
#define MI_BLANK   6
#define MI_DUP     7
#define MI_DELPG   8
static const char *pdu_mfile[10];                 /* insert: File row 9 */
#define ME_UNDO    0
#define ME_REDO    1
#define ME_DEL     2
#define ME_FIND    3
#define ME_NEXT    4
#define ME_PREV    5
#define ME_COPY    6                    /* nav */
#define ME_SELALL  7                    /* nav */
#define ME_MVUP    8                    /* pages: 8-10 */
#define ME_MVDN    9
#define ME_MVTO    10
static const char *pdu_medit[11];
#define MV_ZIN     0
#define MV_ZOUT    1
#define MV_100     2
#define MV_FITW    3
#define MV_FITP    4
#define MV_SIDE    5
#define MV_NOTES   6
#define MV_PREFS   7
static const char *pdu_mview[8];
#define MD_READ    0                /* ..MD_PAGES = 3: the four modes */
#define MD_PREV    4
#define MD_NEXT    5
#define MD_GOTO    6                /* First / Last are Home / End only: a
                                     * pull-down holds 11 (12.4), and nav's
                                     * history and pages' rotation took them */
#define MD_BACK    7                    /* nav */
#define MD_FWD     8                    /* nav */
#define MD_ROTL    9                    /* pages */
#define MD_ROTR    10
static const char *pdu_mdoc[11];
#define MH_KEYS    0
#define MH_PROFILE 1
#define MH_INFO    2
static const char *pdu_mhelp[3];

static struct os88_menuset pdu_menus = {
    "PageDesk", 0, 5,
    { { "File", pdu_mfile, 10 },                                     /* insert */
      { "Edit", pdu_medit, 11 },
      { "View", pdu_mview, 8 },
      { "Document", pdu_mdoc, 11 },
      { "Help", pdu_mhelp, 3 } }
};

static const char *pdu_mi(const char *lit, int on)
{
    return on ? lit + 1 : lit;
}

/* --- the reasons (PD-02). 0 = enabled. -------------------------------------- */
static const char *pdu_why(int id)
{
    int open;
    open = (pdu_doc == D_OPEN);
    if (id == PGC_NEW)                  /* pages */
        return 0;
    switch (id) {
    case C_OPEN: case C_EV_OPEN: case C_EV_SAMPLE: case C_SB_PAGES:
    case C_SB_OUTL: case C_SB_HIDE: case C_NP_CLOSE: case C_NP_FILTER:
    case C_PANEL_NT:
        return 0;
    }
    if (pdu_doc == D_OPENING)
        return "Still opening - Esc cancels.";
    if (!open)
        return "Open a document first.";
    switch (id) {
    case C_UNDO:
        return pdu_undo_can() ? 0 : "Nothing to undo.";      /* edit */
    case C_REDO:
        return pdu_redo_can() ? 0 : "Nothing to redo.";
    case C_PREV:
        return pdu_page == 0 ? "This is the first page." : 0;
    case C_NEXT:
        return pdu_page + 1 >= pdu_npages ? "This is the last page." : 0;
    case C_ZOUT:
        return pdu_zoom <= 25 ? "Already at the smallest zoom, 25%." : 0;
    case C_ZIN:
        return pdu_zoom >= 400 ? "Already at the largest zoom, 400%." : 0;
    case C_T_HILITE: case C_T_NOTE: case C_T_TEXT: case C_T_MARK:
    case C_T_INK: case C_T_LINE: case C_T_RECT: case C_T_ELLIPSE:
    case C_T_SIGN: case C_E_DELETE:
        if (pdu_rdonly)
            return "This document is signed: it opens read-only.";
        if (id == C_E_DELETE && pdu_ed_selected() < 0)       /* edit */
            return "Nothing is selected.";
        return 0;
    }
    if (pdu_pg_isop(id))                /* pages */
        return pdu_pg_why(id);
    return 0;
}

/* greyed? then say why on the status line, and answer 1 */
static int pdu_refuse(int id)
{
    const char *w;
    w = pdu_why(id);
    if (w == 0)
        return 0;
    pdu_tell(w);
    return 1;
}

static void pdu_menusync(void)
{
    int open, i;
    open = (pdu_doc == D_OPEN);
    pdu_mfile[MI_OPEN]   = pdu_mi("\001Open...  ^O", 1);
    pdu_mfile[MI_SAMPLE] = pdu_mi("\001Open sample guide", 1);
    pdu_mfile[MI_NEW]    = pdu_mi("\001New  ^N", 1);                    /* pages */
    pdu_mfile[MI_SAVEAS] = pdu_mi("\001Save As...  ^S", open);
    pdu_mfile[MI_CLOSE]  = pdu_mi("\001Close document", open);
    pdu_medit[ME_UNDO]   = pdu_mi("\001Undo  ^Z", !pdu_why(C_UNDO));
    pdu_medit[ME_REDO]   = pdu_mi("\001Redo  ^Y", !pdu_why(C_REDO));
    pdu_medit[ME_DEL]    = pdu_mi("\001Delete  Del", !pdu_why(C_E_DELETE));   /* edit */
    pdu_medit[ME_FIND]   = pdu_mi("\001Find...  ^F", open);
    pdu_medit[ME_NEXT]   = pdu_mi("\001Find next  F3", open && pdu_nlen);
    pdu_medit[ME_PREV]   = pdu_mi("\001Find previous  Sh+F3", open && pdu_nlen);
    pdu_medit[ME_COPY]   = pdu_mi("\001Copy  ^C", pdu_sel_active());      /* nav */
    pdu_medit[ME_SELALL] = pdu_mi("\001Select all  ^A", open);            /* nav */
    pdu_pg_msync(pdu_mfile, pdu_medit, pdu_mdoc);                     /* pages */
    pdu_mview[MV_ZIN]    = pdu_mi("\001Zoom in  +", !pdu_why(C_ZIN));
    pdu_mview[MV_ZOUT]   = pdu_mi("\001Zoom out  -", !pdu_why(C_ZOUT));
    pdu_mview[MV_100]    = pdu_mi("\001Actual size", open);
    pdu_mview[MV_FITW]   = pdu_mi("\001Fit width", open);
    pdu_mview[MV_FITP]   = pdu_mi("\001Fit page", open);
    i = (pdu_L.kind != PDU_L_COMPACT);
    pdu_mview[MV_SIDE]   = pdu_want_side[pdu_L.kind]
                           ? pdu_mi("\001Hide Pages panel", i)
                           : pdu_mi("\001Show Pages panel", i);
    pdu_mview[MV_NOTES]  = pdu_want_notes[pdu_L.kind]
                           ? pdu_mi("\001Hide Notes panel", i)
                           : pdu_mi("\001Show Notes panel", i);
    pdu_mview[MV_PREFS]  = pdu_mi("\001Preferences...", 0);
    pdu_mdoc[0] = pdu_mi(pdu_mode == 0 ? "\001* Read mode  F5"
                                       : "\001  Read mode  F5", open);
    pdu_mdoc[1] = pdu_mi(pdu_mode == 1 ? "\001* Annotate mode  F6"
                                       : "\001  Annotate mode  F6", open);
    pdu_mdoc[2] = pdu_mi(pdu_mode == 2 ? "\001* Edit mode  F7"
                                       : "\001  Edit mode  F7", open);
    pdu_mdoc[3] = pdu_mi(pdu_mode == 3 ? "\001* Pages mode  F8"
                                       : "\001  Pages mode  F8", open);
    pdu_mdoc[MD_PREV]  = pdu_mi("\001Previous page  PgUp", !pdu_why(C_PREV));
    pdu_mdoc[MD_NEXT]  = pdu_mi("\001Next page  PgDn", !pdu_why(C_NEXT));
    pdu_mdoc[MD_GOTO]  = pdu_mi("\001Go to page...  ^G", open);
    pdu_mdoc[MD_BACK]  = pdu_mi("\001Back  Alt+Left", open && pdu_hbn);    /* nav */
    pdu_mdoc[MD_FWD]   = pdu_mi("\001Forward  Alt+Right", open && pdu_hfn); /* nav */
    pdu_mhelp[MH_KEYS]    = "Keyboard...";
    pdu_mhelp[MH_PROFILE] = "The XT PDF profile...";
    pdu_mhelp[MH_INFO]    = pdu_mi("\001Document info...", open);
}

/* ============================================================================
 * LAYOUT, AGAIN: the live content box (17.1) and the panel wishes
 * ==========================================================================*/
static int pdu_lw = -1, pdu_lh, pdu_lx, pdu_ly, pdu_lside, pdu_lnotes;
static void pdu_relayout(void)
{
    int k;
    os88_wm_content(pdu_win, &pdu_pt);
    os88_wm_geom(pdu_win, &pdu_sz);
    pdu_colors();
    k = pdu_kind(pdu_sz.w, pdu_sz.h);
    pdu_layout(&pdu_L, pdu_pt.x, pdu_pt.y, pdu_sz.w, pdu_sz.h,
               pdu_want_side[k], pdu_want_notes[k], pdu_mode,
               pdu_doc != D_OPEN);
    if (pdu_doc == D_OPEN)
        pdu_view_set();
    if (pdu_lw != pdu_sz.w || pdu_lh != pdu_sz.h || pdu_lx != pdu_pt.x
        || pdu_ly != pdu_pt.y) {
        pdu_lw = pdu_sz.w;
        pdu_lh = pdu_sz.h;
        pdu_lx = pdu_pt.x;
        pdu_ly = pdu_pt.y;
        pdu_job_abort_render();         /* its clip was the old canvas */
        pdu_menusync();
    }
}

/* the panels or the mode changed: lay out again and paint the lot */
static void pdu_reflow(void)
{
    pdu_job_abort_render();
    pdu_rwant = 0;
    pdu_relayout();
    pdu_paint_all();
    pdu_menusync();
}

/* ============================================================================
 * POPOVERS (17.1's COMPACT: page chooser and notes; every layout: zoom,
 * More, Find, the overlay's help cards). One at a time, inside the canvas;
 * Escape closes it before anything touches the document (PD-03).
 * ==========================================================================*/
static const char *pdu_zoomitems[8] = { "Fit width", "Fit page", "50%",
                                        "75%", "100%", "150%", "200%",
                                        "400%" };
static const int pdu_zoomval[8] = { -1, -2, 50, 75, 100, 150, 200, 400 };
static const int pdu_moreids[6] = { C_T_MARK, C_T_INK, C_T_LINE, C_T_RECT,
                                    C_T_ELLIPSE, C_T_SIGN };
static int pdu_helpw;                   /* P_HELP: which card */
#define PDU_POP_ROW 12

static int pdu_pop_rows(void)
{
    if (pdu_pop == P_ZOOM)
        return 8;
    if (pdu_pop == P_MORE)
        return pdu_L.kind == PDU_L_COMPACT ? 6 : 5;
    if (pdu_pop == P_FIND)
        return 2;
    if (pdu_pop == P_NOTES || pdu_pop == P_NOTE)
        return pdu_nt_prows();          /* notes */
    if (pdu_pop == P_PAGES)
        return (pdu_npages + 7) / 8 + 1;
    return 0;
}

static const char *pdu_pop_item(int k)
{
    if (pdu_pop == P_ZOOM)
        return pdu_zoomitems[k];
    if (pdu_pop == P_MORE) {
        if (pdu_L.kind != PDU_L_COMPACT)
            k++;
        return pdu_lab[pdu_moreids[k] * 2];
    }
    return "";
}

/* place the popover under its source control, inside the canvas */
static void pdu_pop_place(int wch, int rows)
{
    struct os88_rect *c, *p;
    int i, w, h;
    c = &pdu_L.canvas;
    p = &pdu_poprect;
    w = wch * PDU_CW + 16;
    h = rows * PDU_POP_ROW + 8;
    i = pdu_find(&pdu_L, pdu_popsrc);
    if (i >= 0) {
        p->x1 = pdu_L.ctl[i].x1;
        p->y1 = c->y1 + 2;
    } else {
        p->x1 = (c->x1 + c->x2 + 1 - w) / 2;
        p->y1 = c->y1 + 8;
    }
    if (p->x1 + w - 1 > c->x2 - 2)
        p->x1 = c->x2 - 2 - w + 1;
    if (p->x1 < c->x1 + 2)
        p->x1 = c->x1 + 2;
    p->x1 = p->x1 & ~7;                 /* rows of text on the 8 grid */
    if (p->x1 < c->x1)
        p->x1 = c->x1;
    p->x2 = p->x1 + w - 1;
    if (p->x2 > c->x2)
        p->x2 = c->x2;
    p->y2 = p->y1 + h - 1;
    if (p->y2 > c->y2 - 2)
        p->y2 = c->y2 - 2;
}


static void pdu_pop_paint(void)
{
    struct os88_rect *p;
    int k, y, n, paper, ink, x, pg;
    const char *s;

    p = &pdu_poprect;
    if (pdu_pop == P_HELP) {
        ovl_card(pdu_helpw, p, pdu_mono);
        return;
    }
    if (pdu_pop == P_NOTES || pdu_pop == P_NOTE) {      /* notes */
        pdu_nt_ppaint();
        return;
    }
    pdu_fill(p->x1, p->y1, p->x2, p->y2, OS88_WHITE);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(p->x1, p->y1, p->x2, p->y2);
    os88_gfx_frame(p->x1 + 1, p->y1 + 1, p->x2 - 1, p->y2 - 1);
    y = p->y1 + 4;
    if (pdu_pop == P_FIND) {
        os88_strcpy(pdu_line, "Find: ", sizeof(pdu_line));
        pdu_cat(pdu_line, pdu_needle, sizeof(pdu_line));
        pdu_cat(pdu_line, "_", sizeof(pdu_line));
        os88_font_run(p->x1 + 8, y + 2, pdu_line, OS88_BLACK, OS88_WHITE);
        os88_font_run(p->x1 + 8, y + 2 + PDU_POP_ROW,
                      "Enter: find  Esc: close", OS88_BLACK, OS88_WHITE);
        return;
    }
    if (pdu_pop == P_PAGES) {
        os88_font_run(p->x1 + 8, y + 2, "Go to page:", OS88_BLACK,
                      OS88_WHITE);
        for (pg = 0; pg < (int)pdu_npages; pg++) {
            x = p->x1 + 8 + (pg % 8) * 4 * PDU_CW;
            k = y + 2 + (pg / 8 + 1) * PDU_POP_ROW;
            if (k + 8 > p->y2)
                break;
            os88_utoa(pg + 1, pdu_num);
            n = (pg == (int)pdu_page);
            os88_font_run(x, k, pdu_num, n ? OS88_WHITE : OS88_BLACK,
                          n ? pdu_cact : OS88_WHITE);
        }
        return;
    }
    n = pdu_pop_rows();
    for (k = 0; k < n; k++) {
        s = pdu_pop_item(k);
        paper = OS88_WHITE;
        ink = OS88_BLACK;
        if (pdu_pop == P_ZOOM && ((pdu_zoomval[k] < 0 && pdu_fit == -pdu_zoomval[k])
            || (pdu_zoomval[k] > 0 && !pdu_fit && pdu_zoom == pdu_zoomval[k]))) {
            paper = pdu_cact;
            ink = OS88_WHITE;
        }
        if (pdu_pop == P_MORE) {
            int id;
            id = pdu_moreids[k + (pdu_L.kind != PDU_L_COMPACT)];
            if (pdu_why(id))
                os88_gfx_pen(1);
            os88_font_run(p->x1 + 8, y + 2 + k * PDU_POP_ROW, s,
                          pdu_why(id) ? (pdu_mono ? OS88_BLACK : OS88_DGRAY)
                                      : OS88_BLACK, paper);
            os88_gfx_pen(0);
        } else
            os88_font_run(p->x1 + 8, y + 2 + k * PDU_POP_ROW, s, ink, paper);
    }
}

static void pdu_pop_open(int kind, int src)
{
    int w, rows, sw;
    sw = 0;
    if (pdu_pop != P_NONE) {            /* one popover for another: the old */
        pdu_popdmg_add(&pdu_poprect);   /* one's ground and the new one are */
        pdu_pop_shut();                 /* ONE repaint at the wake, so no */
        sw = 1;                         /* pixel is drawn twice and the click */
    }                                   /* is not charged both paints (pdstk) */
    pdu_job_abort_render();
    pdu_pop = kind;
    pdu_popsrc = src;
    rows = pdu_pop_rows();
    w = 12;
    if (kind == P_FIND) w = 30;
    if (kind == P_NOTES || kind == P_NOTE) w = pdu_nt_pw();  /* notes */
    if (kind == P_PAGES) w = 32;
    if (kind == P_HELP) {
        pdu_hpage = 0;
        w = ovl_card_size(pdu_helpw);
        rows = pdu_ovrows;
    }
    if (w == 0) {                       /* the overlay could not be had */
        pdu_pop = P_NONE;
        return;
    }
    pdu_pop_place(w, rows);
    pdu_ctl_repaint(src);
    if (sw)                             /* a switch: ground + new, once */
        pdu_popdmg_add(&pdu_poprect);
    else {                              /* a fresh popover: its own pixels */
        pdu_popown = 1;                 /* only - at the wake, whose chain is */
        pdu_kick();                     /* a dispatcher deep and not a click's */
    }                                   /* ten (pdstk measured 386 of 384) */
}

/* a popover's own Done/Esc, from deep in a click: the close (and the repaint
 * under it) happens at the next wake, one level below the dispatcher instead
 * of ten - the static stack gate measured the in-place close at 388 B of the
 * UI task's 384 (pdstk, D22). Input to the popover meanwhile is refused. */
/* the union a popover switch owes, painted once by the wake (pdu_paint_rect:
 * the ground, then the new popover where it meets it) */
static void pdu_popdmg_add(struct os88_rect *r)
{
    if (!pdu_popdmg_on) {
        os88_memcpy(&pdu_popdmg, r, sizeof(pdu_popdmg));
        pdu_popdmg_on = 1;
    } else {
        if (r->x1 < pdu_popdmg.x1) pdu_popdmg.x1 = r->x1;
        if (r->y1 < pdu_popdmg.y1) pdu_popdmg.y1 = r->y1;
        if (r->x2 > pdu_popdmg.x2) pdu_popdmg.x2 = r->x2;
        if (r->y2 > pdu_popdmg.y2) pdu_popdmg.y2 = r->y2;
    }
    pdu_kick();
}

static void pdu_pop_close_soon(void)
{
    pdu_popcls = 1;
    pdu_kick();
}

/* the popover's STATE goes - notes told, focus home, the owed render kicked -
 * and nothing under it is painted: pdu_pop_open's switch owes that to the
 * wake. Split from pdu_pop_close so the static stack gate sees no paint here */
static void pdu_pop_shut(void)
{
    int src, old;
    if (pdu_pop == P_NONE)
        return;
    pdu_nt_pclose();                    /* notes */
    pdu_pop = P_NONE;
    src = pdu_popsrc;
    pdu_popsrc = C_NONE;
    /* PD-23: focus goes back home - to the control that opened it. A card
     * a MENU opened has no home (C_NONE) and leaves the focus where it was;
     * the first build cleared it there without repainting the control that
     * lost it, which kept its focus frame on the glass */
    old = pdu_focus;
    if (src != C_NONE)
        pdu_focus = src;
    pdu_ctl_repaint(src);
    if (old != pdu_focus)
        pdu_ctl_repaint(old);
    pdu_kick();                         /* the render that waited */
}

static void pdu_pop_close(void)
{
    if (pdu_pop == P_NONE)
        return;
    os88_memcpy(&pdu_dmg, &pdu_poprect, sizeof(pdu_dmg));
    pdu_pop_shut();
    pdu_paint_under(&pdu_dmg);          /* P_NONE already: nothing over it */
}

/* ============================================================================
 * THE COMMANDS
 * ==========================================================================*/
static void pdu_sample(void)
{
    if (os88_file_goto_q_mark(pdu_home.clus, pdu_home.vol) != 0) {
        pdu_tell("The disk PageDesk came from is not in a drive.");
        return;
    }
    pdu_open_begin("XTGUIDE.PDF");
}

static void pdu_open_dlg(void)
{
    pdu_pgfd = 0;                       /* pages */
    if (os88_file_dlg(OS88_FDLG_OPEN, pdu_win, 0) != 0)
        pdu_tell("The file dialog is already open.");
}

/* Save As offers a NEW name (14.1): the stem cut to 7 and a '2' */
static char pdu_defname[14];
static void pdu_saveas_dlg(void)
{
    int i;
    pdu_pgfd = 0;                       /* pages */
    for (i = 0; i < 7 && pdu_name[i] && pdu_name[i] != '.'; i++)
        pdu_defname[i] = pdu_name[i];
    pdu_defname[i] = '2';
    os88_strcpy(pdu_defname + i + 1, ".PDF", 6);
    if (os88_file_dlg(OS88_FDLG_SAVE, pdu_win, pdu_defname) != 0)
        pdu_tell("The file dialog is already open.");
}

static void pdu_close_doc(void)
{
    pdu_job_abort();
    pdu_pop_close();
    if (pdu_doc == D_OPEN || pdu_doc == D_OPENING)
        pdu_doc_release();
    pdu_doc = D_EMPTY;
    pdu_rwant = 0;
    pdu_npages = 0;
    pdu_pgmap = 0;                      /* pages */
    pdu_pk = PK_NONE;
    pdu_changes = 0;
    pdu_nt_reset();                     /* notes */
    pdu_say("Document closed.");
    pdu_title_set();
    pdu_reflow();
}

static void pdu_set_mode(int m)
{
    pdu_tx_end();                       /* edit */
    if (pdu_doc != D_OPEN) {
        pdu_tell(pdu_why(C_M_READ));
        return;
    }
    pdu_mode = m;
    pdu_tool = (m == PDU_MODE_PAGES) ? C_NONE : C_T_SELECT;
    os88_wm_cursor(pdu_win, OS88_CUR_ARROW);
    pdu_pop_close();
    pdu_relayout();
    pdu_bar_paint(&pdu_L.bar2, CF_BAR2);
    pdu_menusync();
}

static void pdu_set_tool(int id)
{
    int old;
    if (pdu_refuse(id))
        return;
    old = pdu_tool;
    pdu_tx_end();                       /* edit */
    if (id != old)
        pdu_tool = id;
    pdu_ctl_repaint(old);
    pdu_ctl_repaint(id);
    os88_wm_cursor(pdu_win, id == C_T_SELECT ? OS88_CUR_ARROW
                                             : OS88_CUR_CROSS);
    pdu_tool_hint(id);                  /* edit */
}

/* Find is nav's J_FIND job: pdufind.c */

static void pdu_cmd(int id)
{
    if (pdu_refuse(id))
        return;
    pdu_tx_end();                       /* edit: a command ends the typing */
    if (pdu_pg_isop(id)) {              /* pages */
        ovl_pg_cmd(id);
        return;
    }
    switch (id) {
    case C_UNDO:                        /* edit */
        pdu_undo_cmd(0);
        return;
    case C_REDO:                        /* edit */
        pdu_undo_cmd(1);
        return;
    case C_E_DELETE:                    /* edit */
        pdu_tool_delete();
        return;
    case C_OPEN: case C_EV_OPEN:
        pdu_open_dlg();
        return;
    case C_EV_SAMPLE:
        pdu_sample();
        return;
    case C_SAVEAS:
        pdu_saveas_dlg();
        return;
    case C_PREV:
        pdu_goto(pdu_page - 1);
        return;
    case C_NEXT:
        pdu_goto(pdu_page + 1);
        return;
    case C_PAGE:
        pdu_fedit = 1;
        pdu_flen = 0;
        pdu_fbuf[0] = 0;
        pdu_ctl_repaint(C_PAGE);
        pdu_tell("Type a page number, Enter to go, Esc to cancel.");
        return;
    case C_ZOUT:
        pdu_zoom_step(-1);
        return;
    case C_ZIN:
        pdu_zoom_step(1);
        return;
    case C_ZOOM: case C_FIT:
        pdu_pop_open(P_ZOOM, id);
        return;
    case C_FIND:
        pdu_pop_open(P_FIND, id);
        return;
    case C_M_READ: case C_M_ANNOT: case C_M_EDIT: case C_M_PAGES:
        pdu_set_mode(id - C_M_READ);
        return;
    case C_T_MORE:
        pdu_pop_open(P_MORE, id);
        return;
    case C_SB_PAGES: case C_SB_OUTL:
        pdu_sbtab = (id == C_SB_OUTL);
        pdu_th_reset();                 /* nav: the claim changes hands */
        pdu_side_paint();
        return;
    case C_SB_HIDE:
        pdu_want_side[pdu_L.kind] = 0;
        pdu_reflow();
        return;
    case C_NP_CLOSE:
        pdu_want_notes[pdu_L.kind] = 0;
        pdu_reflow();
        return;
    case C_NP_FILTER:
        pdu_nt_filter();                /* notes: This page, All pages, Changes */
        return;
    case C_PANEL_PG:
        pdu_pop_open(P_PAGES, id);
        return;
    case C_PANEL_NT:
        pdu_pop_open(P_NOTES, id);
        return;
    }
    if (id >= C_T_SELECT && id <= C_T_SIGN) {
        pdu_set_tool(id);
        if (id == C_T_HILITE)           /* edit: a selection already made */
            pdu_tool_hilite();
    }
}

/* a menu command: the gfx lock held, like a click (SPEC.md 12.2) */
void os88_oncmd(int item, int menu, void *win)
{
    int pg;
    pdu_about_off();
    pg = pdu_pg_menucmd(menu, item);   /* pages: the page commands' rows */
    if (pg) {
        pdu_cmd(pg);
        return;
    }
    if (menu == 0) {
        if (item == MI_OPEN) pdu_cmd(C_OPEN);
        else if (item == MI_SAMPLE) pdu_sample();
        else if (item == MI_SAVEAS) pdu_cmd(C_SAVEAS);
        else if (item == MI_CLOSE) pdu_close_doc();
        else if (item == MI_NEW) pdu_cmd(PGC_NEW);                  /* pages */
    } else if (menu == 1) {
        if (item == ME_UNDO) pdu_cmd(C_UNDO);
        else if (item == ME_REDO) pdu_cmd(C_REDO);
        else if (item == ME_FIND) pdu_cmd(C_FIND);
        else if (item == ME_NEXT) pdu_find_run(1);
        else if (item == ME_PREV) ovl_find_prev();      /* nav */
        else if (item == ME_COPY) ovl_copy();           /* nav */
        else if (item == ME_SELALL) ovl_selall();       /* nav */
        else if (item == ME_DEL) pdu_cmd(C_E_DELETE);   /* edit */
        else pdu_tell("Nothing is selected.");
    } else if (menu == 2) {
        if (item == MV_ZIN) pdu_cmd(C_ZIN);
        else if (item == MV_ZOUT) pdu_cmd(C_ZOUT);
        else if (item == MV_100) pdu_zoom_to(100, 0);
        else if (item == MV_FITW) pdu_zoom_to(0, 1);
        else if (item == MV_FITP) pdu_zoom_to(0, 2);
        else if (item == MV_SIDE) {
            pdu_want_side[pdu_L.kind] = !pdu_want_side[pdu_L.kind];
            pdu_reflow();
        } else if (item == MV_NOTES) {
            pdu_want_notes[pdu_L.kind] = !pdu_want_notes[pdu_L.kind];
            pdu_reflow();
        } else
            pdu_tell("Preferences are not in this build yet.");
    } else if (menu == 3) {
        if (item <= 3) pdu_set_mode(item);
        else if (item == MD_PREV) pdu_cmd(C_PREV);
        else if (item == MD_NEXT) pdu_cmd(C_NEXT);
        else if (item == MD_GOTO) pdu_cmd(C_PAGE);
        else if (item == MD_BACK) ovl_back();           /* nav */
        else if (item == MD_FWD) ovl_forward();         /* nav */
    } else if (menu == 4) {
        pdu_helpw = item;
        pdu_pop_open(P_HELP, C_NONE);
    }
}

/* ============================================================================
 * ABOUT (12.2's About PageDesk, the INDEX story)
 * ==========================================================================*/
void os88_about(void *win)
{
    pdu_pop_close();
    pdu_job_abort_render();
    pdu_about = 1;
    os88_about_card(win, pdu_abtext);
}

static void pdu_about_off(void)
{
    if (!pdu_about)
        return;
    pdu_about = 0;
    pdu_paint_all();
    pdu_kick();
}

/* ============================================================================
 * THE FILE DIALOG'S ANSWER (SPEC.md 38): Cancel calls nothing, so the
 * current document is untouched (PD-01)
 * ==========================================================================*/
static struct os88_place pdu_hereplace;
void os88_onfile(int mode, const char *name, unsigned size_lo,
                 unsigned size_hi, void *win)
{
    if (pdu_pgfd == 3) {                /* insert: a file to take pages from, not to open */
        ovl_pg_onfile(name);
        return;
    }
    if (mode == OS88_FDLG_OPEN) {
        pdu_open_begin(name);
        return;
    }
    if (pdu_pgfd) {                     /* pages: an Extract or a New */
        ovl_pg_onfile(name);
        return;
    }
    if (pdu_doc != D_OPEN)
        return;
    /* D14 (20.11.3): the writer reads the source in ITS folder and writes
     * here, so a Save As browsed into another folder is just a Save As */
    os88_file_here(&pdu_hereplace);
    {
        int i, same;
        same = 1;
        for (i = 0; name[i] || pdu_name[i]; i++)
            if (name[i] != pdu_name[i])
                same = 0;
        if (same && pdu_hereplace.clus == pdu_docplace.clus && pdu_hereplace.vol == pdu_docplace.vol) {
            pdu_tell("Save As writes a new file: choose another name.");
            pdu_at_doc();
            return;
        }
    }
    if (!ovl_save_begin(name, 0))      /* pages: the overlay could not be had */
        pdu_tell(pdu_errtext(PDE_NOMEM));
}

/* ============================================================================
 * KEYS (17.5, PD-23)
 * ==========================================================================*/
#define K_F3     0x3D
#define K_SF3    0x56
#define K_F5     0x3F
#define K_F8     0x42
#define K_HOME   0x47
#define K_UP     0x48
#define K_PGUP   0x49
#define K_LEFT   0x4B
#define K_RIGHT  0x4D
#define K_END    0x4F
#define K_DOWN   0x50
#define K_PGDN   0x51
#define K_DEL    0x53
#define K_TAB    0x0F

/* the next enabled control after (dir 1) or before (dir -1) the focus */
static void pdu_focus_move(int dir)
{
    int i, n, k, old;
    n = pdu_L.nctl;
    if (n == 0)
        return;
    i = pdu_find(&pdu_L, pdu_focus);
    for (k = 0; k < n; k++) {
        i = i + dir;
        if (i >= n) i = 0;
        if (i < 0) i = n - 1;
        if (!pdu_why(pdu_L.ctl[i].id))
            break;
    }
    old = pdu_focus;
    pdu_focus = pdu_L.ctl[i].id;
    pdu_ctl_repaint(old);
    pdu_ctl_repaint(pdu_focus);
}

/* the page field owns every key but Esc and Enter (17.5) */
static void pdu_field_key(int ascii)
{
    int n;
    if (ascii == 27) {
        pdu_fedit = 0;
        pdu_ctl_repaint(C_PAGE);
        pdu_tell("");
        return;
    }
    if (ascii == 13) {
        pdu_fedit = 0;
        n = 0;
        pdu_flen = 0;
        while (pdu_fbuf[pdu_flen])
            n = n * 10 + (pdu_fbuf[pdu_flen++] - '0');
        pdu_ctl_repaint(C_PAGE);
        if (n >= 1 && n <= (int)pdu_npages) {
            pdu_tell("");
            pdu_goto(n - 1);
        } else {
            os88_strcpy(pdu_line, "There is no such page: the document has ",
                        sizeof(pdu_line));
            os88_utoa(pdu_npages, pdu_num);
            pdu_cat(pdu_line, pdu_num, sizeof(pdu_line));
            pdu_cat(pdu_line, ".", sizeof(pdu_line));
            pdu_tell(pdu_line);
        }
        return;
    }
    if (ascii == 8) {
        if (pdu_flen > 0)
            pdu_fbuf[--pdu_flen] = 0;
    } else if (ascii >= '0' && ascii <= '9' && pdu_flen < 4) {
        pdu_fbuf[pdu_flen++] = ascii;
        pdu_fbuf[pdu_flen] = 0;
    }
    pdu_ctl_repaint(C_PAGE);
}

/* the Find popover's line */
static void pdu_find_key(int ascii)
{
    if (ascii == 13) {
        pdu_pop_close_soon();           /* nav: the job runs under the page */
        pdu_find_run(0);
        return;
    }
    if (ascii == 8) {
        if (pdu_nlen > 0)
            pdu_needle[--pdu_nlen] = 0;
    } else if (ascii >= 32 && ascii < 127 && pdu_nlen < 30) {
        pdu_needle[pdu_nlen++] = ascii;
        pdu_needle[pdu_nlen] = 0;
    } else
        return;
    pdu_pop_paint();
}

void os88_onkey(int ascii, int scan, void *win)
{
    int ch;

    if (pdu_about) {
        pdu_about_off();
        return;
    }
    ascii = ascii & 0xFF;
    if (pdu_pg_key(ascii, scan))        /* pages: a prompt owns the keys */
        return;
    if (pdu_tx < 0 && pdu_nt_key(ascii, scan))  /* notes: after an open text box */
        return;
    /* Escape, in order (PD-03, 17.5): a running job, a popover, the page
     * field, the keyboard focus, then the tool */
    if (ascii == 27) {
        if (pdu_job != J_NONE && !pdu_reopening) {    /* pages: PDENG must get its document back */
            pdu_jcancel = 1;
            pdu_tell("Cancelling...");
            pdu_kick();
        } else if (pdu_pop != P_NONE)
            pdu_pop_close();
        else if (pdu_fedit)
            pdu_field_key(27);
        else if (pdu_tx >= 0)           /* edit: finish the typing */
            pdu_tx_end();
        else if (pdu_ed_selected() >= 0) /* edit: deselect */
            pdu_ed_select(-1);
        else if (pdu_focus != C_NONE) {
            ch = pdu_focus;
            pdu_focus = C_NONE;
            pdu_ctl_repaint(ch);
        } else if (pdu_sel_active())
            ovl_sel_clear();            /* nav: 17.5, then deselect */
        else if (pdu_tool != C_T_SELECT && pdu_mode != PDU_MODE_PAGES)
            pdu_set_tool(C_T_SELECT);
        return;
    }
    if (pdu_fedit) {
        pdu_field_key(ascii);
        return;
    }
    if (pdu_tx >= 0 && pdu_tx_key(ascii)) /* edit: the open text owns the keys */
        return;
    if (pdu_pop == P_FIND && ascii) {
        pdu_find_key(ascii);
        return;
    }
    if (pdu_pop == P_HELP) {
        if ((ascii == 13 || ascii == ' ') && ovl_card_more(pdu_helpw) != 2)
            pdu_pop_close();
        return;
    }
    if (pdu_pop == P_NOTES || pdu_pop == P_NOTE) {       /* notes */
        if (ascii == 13 || ascii == ' ')
            pdu_pop_close();
        return;
    }
    switch (ascii) {
    case 15: pdu_cmd(C_OPEN); return;                    /* ^O */
    case 14: pdu_cmd(PGC_NEW); return;                   /* ^N, pages */
    case 19: pdu_cmd(C_SAVEAS); return;                  /* ^S */
    case 26: pdu_cmd(C_UNDO); return;                    /* ^Z */
    case 25: pdu_cmd(C_REDO); return;                    /* ^Y */
    case 6:  pdu_cmd(C_FIND); return;                    /* ^F */
    case 3:  ovl_copy(); return;                         /* ^C (nav) */
    case 1:  ovl_selall(); return;                       /* ^A (nav) */
    case 7:  pdu_cmd(C_PAGE); return;                    /* ^G */
    case 9:  pdu_focus_move(1); return;                  /* Tab */
    case '+': case '=': pdu_cmd(C_ZIN); return;
    case '-': case 0x1F: pdu_cmd(C_ZOUT); return;        /* - and ^- */
    case 13: case ' ':
        if (ascii == 13 && pdu_tool_enter())    /* edit */
            return;
        if (pdu_focus != C_NONE && pdu_find(&pdu_L, pdu_focus) >= 0)
            pdu_cmd(pdu_focus);
        else if (pdu_doc != D_OPEN && ascii == 13)
            pdu_open_dlg();
        return;
    }
    if (ascii >= '1' && ascii <= '6' && pdu_doc == D_OPEN) {
        const int *t;
        t = pdu_tools(pdu_mode, pdu_L.kind == PDU_L_COMPACT);
        ch = ascii - '1';
        while (ch > 0 && *t) {
            t++;
            ch--;
        }
        if (*t)
            pdu_cmd(*t);
        return;
    }
    if (ascii)
        return;
    switch (scan) {
    case K_TAB:   pdu_focus_move(-1); return;            /* Shift+Tab */
    case K_PGUP:  pdu_cmd(C_PREV); return;
    case K_PGDN:  pdu_cmd(C_NEXT); return;
    case K_HOME:  pdu_goto(0); return;
    case K_END:   pdu_goto(pdu_npages - 1); return;
    case K_UP:    pdu_scroll_by(0, -16); return;
    case K_DOWN:  pdu_scroll_by(0, 16); return;
    case K_LEFT:  pdu_scroll_by(-16, 0); return;
    case K_RIGHT: pdu_scroll_by(16, 0); return;
    case K_DEL:   pdu_cmd(C_E_DELETE); return;     /* edit */
    case K_F3:    pdu_find_run(1); return;
    case K_SF3:   ovl_find_prev(); return;
    case 0x9B:    ovl_back(); return;                    /* Alt+Left (nav) */
    case 0x9D:    ovl_forward(); return;                 /* Alt+Right (nav) */
    }
    if (scan >= K_F5 && scan <= K_F8)
        pdu_set_mode(scan - K_F5);
}

void os88_onwheel(int notches, void *win)
{
    static struct os88_mouse wm;
    os88_mouse(&wm);                    /* nav: over the sidebar it scrolls the list */
    if (pdu_inr(&pdu_L.side, wm.x, wm.y) && pdu_doc == D_OPEN)
        ovl_side_wheel(notches);
    else
        pdu_scroll_by(0, notches * OS88_WHEEL_STEPS * 8);
}

/* ============================================================================
 * CLICKS
 * ==========================================================================*/
static int pdu_inr(struct os88_rect *r, int x, int y)
{
    return x >= r->x1 && x <= r->x2 && y >= r->y1 && y <= r->y2;
}

static void pdu_pop_click(int x, int y)
{
    struct os88_rect *p;
    int k, v;
    if (pdu_popcls)                     /* closing at the wake: no more input */
        return;
    if (pdu_nt_pclick(x, y))            /* notes */
        return;
    p = &pdu_poprect;
    k = (y - p->y1 - 4) / PDU_POP_ROW;
    if (pdu_pop == P_ZOOM) {
        if (k < 0 || k >= 8)
            return;
        v = pdu_zoomval[k];
        pdu_pop_close();
        if (v < 0)
            pdu_zoom_to(0, -v);
        else
            pdu_zoom_to(v, 0);
        return;
    }
    if (pdu_pop == P_MORE) {
        if (k < 0 || k >= pdu_pop_rows())
            return;
        v = pdu_moreids[k + (pdu_L.kind != PDU_L_COMPACT)];
        if (pdu_refuse(v))
            return;
        pdu_pop_close();
        pdu_set_tool(v);
        return;
    }
    if (pdu_pop == P_PAGES) {
        k = k - 1;
        v = (x - p->x1 - 8) / (4 * PDU_CW);
        if (k < 0 || v < 0 || v > 7)
            return;
        v = k * 8 + v;
        if (v < (int)pdu_npages) {
            pdu_pop_close();
            pdu_goto(v);
        }
        return;
    }
    pdu_pop_close();                    /* a help card, Find */
}

void os88_onclick(int x, int y, void *win)
{
    int id, k, r;
    struct os88_rect *o;

    if (pdu_about) {
        pdu_about_off();
        return;
    }
    if (pdu_pop != P_NONE) {
        if (pdu_inr(&pdu_poprect, x, y))
            pdu_pop_click(x, y);
        else
            pdu_pop_close();            /* a click outside only closes it */
        return;
    }
    if (pdu_fedit) {
        pdu_fedit = 0;
        pdu_ctl_repaint(C_PAGE);
    }
    id = pdu_hit(&pdu_L, x, y, &pdu_hidx);
    if (id != C_NONE) {
        if (pdu_focus != C_NONE && pdu_focus != id) {
            k = pdu_focus;
            pdu_focus = C_NONE;
            pdu_ctl_repaint(k);
        }
        pdu_cmd(id);
        return;
    }
    if (pdu_nt_click(x, y))             /* notes: the panel's list */
        return;
    if (pdu_inr(&pdu_L.sbar, x, y) && pdu_doc == D_OPEN) {
        pdu_sb_fill();
        r = pdu_sbhit(pdu_sb, x, y);
        k = pdu_ch() - 16;
        if (r == PDU_SB_THUMB) pdu_nav_thumb(y);         /* nav: the drag */
        else if (r == PDU_SB_UP) pdu_scroll_by(0, -16);
        else if (r == PDU_SB_DOWN) pdu_scroll_by(0, 16);
        else if (r == PDU_SB_PGUP) pdu_scroll_by(0, -k);
        else if (r == PDU_SB_PGDN) pdu_scroll_by(0, k);
        return;
    }
    if (pdu_inr(&pdu_L.side, x, y) && pdu_doc == D_OPEN) {
        ovl_side_click(x, y);           /* nav: cells, outline rows */
        return;
    }
    if (pdu_inr(&pdu_L.canvas, x, y) && pdu_doc == D_OPEN) {
        if (pdu_tool_click(x, y))       /* edit: the annotation tools */
            return;
        if (pdu_mode == PDU_MODE_PAGES)
            pdu_tell("Pages mode: use the buttons above or the menus.");
        else if (pdu_tool == C_T_SELECT || pdu_tool == C_T_HILITE)
            pdu_nav_press(x, y);        /* nav: a link, or a text selection */
        else
            pdu_tell(pdu_why(pdu_tool) ? pdu_why(pdu_tool)
                                       : "That tool is not in this build yet.");
    }
}
