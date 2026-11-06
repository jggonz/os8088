/* ============================================================================
 * os8088 - apps/pagedesk/pduinsp.c   THE TEXT PROPERTIES INSPECTOR
 *                                    (PAGEDESK-SPEC 17.3, 20.9; PD-14)
 *
 * Family, size, bold, italic, colour, alignment, Delete and Done for ONE text
 * box, in the Notes & edits panel's place (FULL, MID) or in a popover
 * (COMPACT, or a panel that is hidden). The control table is resident data;
 * every function is an ovl_* (the overlay, pdovl.c includes nothing of this -
 * pagedesk.c includes this file, and the cut is by name). Every change is one
 * command through 20.8's calls - begin, set, end - and the box is rendered
 * again by its own rect, never the whole page.
 * ==========================================================================*/
#ifndef PD_HOST

static void ovl_nt_box(struct os88_rect *r, int paper, int ink, const char *s);
static void ovl_nt_pad(const char *s, int cols);
static void ovl_nt_redraw(const struct pd_rect *r);

#define PDU_NI_FONT  0
#define PDU_NI_SZM   1
#define PDU_NI_SZP   2
#define PDU_NI_BOLD  3
#define PDU_NI_ITAL  4
#define PDU_NI_COL   5
#define PDU_NI_AL    6                  /* AL, AL + 1, AL + 2: L, C, R */
#define PDU_NI_DEL   9
#define PDU_NI_DONE  10
#define PDU_NI_N     11

/* the grid, in 8-pixel columns and rows of the area */
static const char pdu_ni_col[PDU_NI_N] = { 6, 6, 15, 0, 8, 6, 6, 10, 14, 0, 10 };
static const char pdu_ni_row[PDU_NI_N] = { 1, 2, 2, 3, 3, 4, 5, 5, 5, 6, 6 };
static const char pdu_ni_w[PDU_NI_N]   = { 11, 3, 3, 7, 8, 11, 3, 3, 3, 8, 6 };

static const char *pdu_ni_fam[3] = { "Helvetica", "Times", "Courier" };
static const unsigned char pdu_ni_sizes[9] = { 8, 9, 10, 11, 12, 14, 16, 18, 24 };
static const unsigned char pdu_ni_cols[7] = { PDC_BLACK, PDC_BLUE, PDC_RED,
                                              PDC_GREEN, PDC_MAGENTA,
                                              PDC_BROWN, PDC_DGRAY };
static const char *pdu_ni_cname[7] = { "Black", "Blue", "Red", "Green",
                                       "Magenta", "Brown", "Gray" };

static struct os88_rect pdu_nt_ir;      /* the area's origin; rows are irh */
static int pdu_nt_irh = 16;
static int pdu_nt_ipaper;               /* what the area stands on */

/* control k's rect */
static void ovl_nt_ictlr(int k, struct os88_rect *r)
{
    r->x1 = pdu_nt_ir.x1 + pdu_ni_col[k] * PDU_CW;
    r->x2 = r->x1 + pdu_ni_w[k] * PDU_CW - 1;
    r->y1 = pdu_nt_ir.y1 + pdu_ni_row[k] * pdu_nt_irh;
    r->y2 = r->y1 + pdu_nt_irh - 3;
}

/* the record (in pdu_nt_e2) is still there and still a text box */
static int ovl_nt_iload(void)
{
    if (pdu_ed_get(pdu_nt_isel, &pdu_nt_e2) != 0 || pdu_nt_e2.kind != PDA_TEXT
        || (pdu_nt_e2.flags & PDEF_DELETED))
        return 0;
    return 1;
}

/* Done: back to the list, or the popover away */
static void ovl_nt_idone(void)
{
    if (pdu_nt_insp) {
        pdu_nt_insp = 0;
        pdu_notes_paint();
    } else if (pdu_pop == P_NOTE) {
        pdu_nt_pm = 0;
        pdu_nt_nocommit = 1;
        pdu_pop_close();
    }
}

/* Text properties for record id */
static void ovl_nt_inspect(int id)
{
    pdu_nt_isel = id;
    if (id < 0 || id >= PDU_NT_SBASE || !ovl_nt_iload()) {
        pdu_tell("Select a text box first.");
        return;
    }
    if (PDU_NT_PANEL() && pdu_pop == P_NONE) {
        pdu_nt_insp = 1;
        pdu_notes_paint();
    } else {
        pdu_nt_pm = 1;
        pdu_pop_open(P_NOTE, C_NONE);
    }
}

/* one control, from the record in pdu_nt_e2 */
static void ovl_nt_ictl(int k)
{
    struct os88_rect *r;
    const char *lab;
    int act, paper, ink, i;

    r = &pdu_nt_cr;
    ovl_nt_ictlr(k, r);
    act = 0;
    paper = OS88_WHITE;
    ink = OS88_BLACK;
    i = pdu_nt_e2.style >> 4;
    if (k == PDU_NI_FONT)
        lab = pdu_ni_fam[i > 2 ? 2 : i];
    else if (k == PDU_NI_SZM || k == PDU_NI_SZP)
        lab = k == PDU_NI_SZM ? "-" : "+";
    else if (k == PDU_NI_BOLD || k == PDU_NI_ITAL) {
        lab = k == PDU_NI_BOLD ? "Bold" : "Italic";
        act = (pdu_nt_e2.style >> (k - PDU_NI_BOLD)) & 1;
    } else if (k == PDU_NI_COL) {
        for (i = 0; i < 7 && pdu_ni_cols[i] != pdu_nt_e2.colour; i++)
            ;
        lab = i < 7 ? pdu_ni_cname[i] : "Other";  /* a colour C does not cycle to */
        if (!pdu_mono) {
            paper = pdu_nt_e2.colour & 15;
            ink = paper <= PDC_BROWN || paper == PDC_DGRAY
                  ? OS88_WHITE : OS88_BLACK;     /* the dark half: white */
        }
    } else if (k == PDU_NI_DEL)
        lab = "Delete";
    else if (k == PDU_NI_DONE)
        lab = "Done";
    else {
        i = k - PDU_NI_AL;
        lab = i == 0 ? "L" : i == 1 ? "C" : "R";
        act = pdu_nt_e2.align == i;
    }
    if (act) {
        paper = pdu_cact;
        ink = OS88_WHITE;
    }
    ovl_nt_box(r, paper, ink, lab);
    if (k == PDU_NI_SZM) {              /* the value between - and + */
        os88_strcpy(pdu_line + 40, " ", 4);
        os88_utoa(pdu_nt_e2.size, pdu_num);
        pdu_cat(pdu_line + 40, pdu_num, 12);
        pdu_cat(pdu_line + 40, " pt", 12);
        ovl_nt_pad(pdu_line + 40, 6);
        os88_font_run(pdu_nt_ir.x1 + 9 * PDU_CW,
                      r->y1 + (r->y2 - r->y1 + 1 - 8) / 2, pdu_line,
                      OS88_BLACK, pdu_nt_ipaper);
    }
}

static int ovl_nt_ipaint(struct os88_rect *o, int rh)
{
    int k, y;
    static const char *lab[4] = { "Font ", "Size ", "Color", "Align" };
    static const char lrow[4] = { 1, 2, 4, 5 };

    pdu_nt_irh = rh;
    pdu_nt_ir.x1 = o->x1 + (rh == 16 ? 2 : 0);
    pdu_nt_ir.y1 = o->y1 + (rh == 16 ? 2 : 0);
    pdu_nt_ipaper = rh == 16 ? pdu_cbar : OS88_WHITE;
    if (!ovl_nt_iload()) {
        os88_font_run(pdu_nt_ir.x1, pdu_nt_ir.y1 + 4, "No text box",
                      OS88_BLACK, pdu_nt_ipaper);
        return 1;
    }
    os88_font_run(pdu_nt_ir.x1, pdu_nt_ir.y1 + (rh - 8) / 2,
                  "Text properties", OS88_BLACK, pdu_nt_ipaper);
    for (k = 0; k < 4; k++) {
        y = pdu_nt_ir.y1 + lrow[k] * rh + (rh - 3 - 8) / 2;
        os88_font_run(pdu_nt_ir.x1, y, lab[k], OS88_BLACK, pdu_nt_ipaper);
    }
    for (k = 0; k < PDU_NI_N; k++)
        ovl_nt_ictl(k);
    return 1;
}

/* what control k does to the record: one command */
static void ovl_nt_iact(int k)
{
    int i, v;
    struct pd_edit *e;

    if (!ovl_nt_iload()) {
        ovl_nt_idone();
        return;
    }
    e = &pdu_nt_e2;
    if (k == PDU_NI_DONE) {
        ovl_nt_idone();
        return;
    }
    if (k == PDU_NI_DEL) {
        pdu_undo_begin(0);
        pdu_ed_del(pdu_nt_isel);
        pdu_undo_end();
        pdu_ed_select(-1);
        pdu_nt_sel = pdu_nt_edsel = -1;
        ovl_nt_redraw(&e->rect);
        ovl_nt_idone();
        ovl_nt_dirty();
        return;
    }
    if (k == PDU_NI_FONT)
        e->style = (((e->style >> 4) + 1) % 3 << 4) | (e->style & 3);
    else if (k == PDU_NI_SZM || k == PDU_NI_SZP) {
        for (i = 8; i > 0 && pdu_ni_sizes[i] > e->size; i--)
            ;
        if (k == PDU_NI_SZP) {
            if (i < 8)
                i++;
        } else if (pdu_ni_sizes[i] == e->size && i > 0)
            i--;
        e->size = pdu_ni_sizes[i];
    } else if (k == PDU_NI_BOLD || k == PDU_NI_ITAL)
        e->style = e->style ^ (1 << (k - PDU_NI_BOLD));
    else if (k == PDU_NI_COL) {
        for (i = 0; i < 7 && pdu_ni_cols[i] != e->colour; i++)
            ;
        e->colour = pdu_ni_cols[(i + 1) % 7];     /* not found: i = 7 -> Blue */
    } else
        e->align = k - PDU_NI_AL;
    pdu_undo_begin(0);
    v = pdu_ed_set(pdu_nt_isel, e);
    pdu_undo_end();
    if (v < 0) {
        pdu_tell("That change was not kept.");
        return;
    }
    ovl_nt_redraw(&e->rect);
    ovl_nt_ictl(k);
    if (k >= PDU_NI_AL && k <= PDU_NI_AL + 2)           /* the other two */
        for (i = PDU_NI_AL; i <= PDU_NI_AL + 2; i++)
            if (i != k)
                ovl_nt_ictl(i);
    if (k == PDU_NI_SZP)
        ovl_nt_ictl(PDU_NI_SZM);        /* repaints the value between */
}

static int ovl_nt_iclick(int x, int y)
{
    int k;
    for (k = 0; k < PDU_NI_N; k++) {
        ovl_nt_ictlr(k, &pdu_tr);
        if (pdu_inr(&pdu_tr, x, y)) {
            ovl_nt_iact(k);
            return 1;
        }
    }
    return 1;
}

/* keys: Esc and Enter are Done, Del deletes, and one letter a control: B
 * bold, I italic, + - size, F family, C colour, L M R alignment. In a popover
 * every key is ours; in the panel the rest go on to the page. */
static int ovl_nt_ikey(int ascii, int scan)
{
    static const char keys[10] = "bi+-fclmr";
    static const char ids[9] = { PDU_NI_BOLD, PDU_NI_ITAL, PDU_NI_SZP,
                                 PDU_NI_SZM, PDU_NI_FONT, PDU_NI_COL,
                                 PDU_NI_AL, PDU_NI_AL + 1, PDU_NI_AL + 2 };
    int k, i;
    k = -1;
    if (ascii == 27) {
        if (pdu_job != J_NONE)
            return 0;
        k = PDU_NI_DONE;
    } else if (ascii == 13)
        k = PDU_NI_DONE;
    else if (ascii == 0 && scan == 0x53)
        k = PDU_NI_DEL;
    else if (ascii) {
        if (ascii >= 'A' && ascii <= 'Z')
            ascii = ascii + 32;
        if (ascii == '=')
            ascii = '+';
        for (i = 0; i < 9; i++)
            if (keys[i] == ascii)
                k = ids[i];
    }
    if (k < 0)
        return pdu_pop == P_NOTE;
    ovl_nt_iact(k);
    return 1;
}
#endif /* !PD_HOST */
