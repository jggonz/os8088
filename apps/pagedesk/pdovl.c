/* ============================================================================
 * os8088 - apps/pagedesk/pdovl.c    THE OVERLAY'S TENANTS (PAGEDESK-SPEC 2.2)
 *
 * Every ovl_* function is cut by tools/os88ovl.py into PAGEDESK.O88's last
 * part and fetched the first time one is called (SPEC.md 73.14.2, LAZY). Only
 * CODE moves: the strings below stay resident and DS-relative. A refused
 * fetch (no heap, the disk gone) makes the call return 0 having done
 * nothing, so each caller asks for a status and treats 0 as "it did not".
 *
 * Tenants are what a MENU reaches and a keystroke on the page never does:
 * the Help cards (Keyboard, the XT profile, Document info). Preferences,
 * page operations' dialogs and recovery join them in wave 2.
 * ==========================================================================*/

#define PDU_CARD_ROW 10

static const char *pdu_hkeys[] = {
    "Keyboard",
    "",
    "Ctrl+O  Open        Ctrl+S  Save As",
    "Ctrl+Z  Undo        Ctrl+Y  Redo",
    "Ctrl+F  Find        F3      Find next",
    "PgUp    Prev page   PgDn    Next page",
    "Home    First page  End     Last page",
    "Ctrl+G  Go to page  + -     Zoom",
    "F5-F8   Read, Annotate, Edit, Pages",
    "1-6     The mode's tools, in order",
    "Arrows  Scroll and pan the page",
    "Tab     Next control, Enter presses",
    "Esc     Close, cancel, then deselect",
    0
};

static const char *pdu_hprof[] = {
    "The PageDesk XT PDF profile",
    "",
    "Reads: classic xref tables, updates,",
    "  ASCIIHex and RunLength streams,",
    "  Helvetica, Times and Courier text,",
    "  paths, 1- and 8-bit grey images.",
    "Refuses, saying why: Flate on the XT,",
    "  xref streams, encryption, JPEG.",
    "Signed files open read-only.",
    "Never runs JavaScript or links.",
    "No printing: Save As writes a PDF.",
    0
};

static char pdu_iline[10][40];
static const char *pdu_hinfo[11];


static const char **ovl_card_lines(int which)
{
    int k;
    if (which == 0)
        return pdu_hkeys;
    if (which == 1)
        return pdu_hprof;
    /* Document info: the facts, and the heap - which is how a leak shows
     * itself on a machine with no debugger (20.6.2) */
    k = 0;
    os88_strcpy(pdu_iline[k], "Document info", 40);
    pdu_hinfo[k] = pdu_iline[k]; k++;
    os88_strcpy(pdu_iline[k], "File:     ", 40);
    pdu_cat(pdu_iline[k], pdu_name, 40);
    pdu_hinfo[k] = pdu_iline[k]; k++;
    os88_strcpy(pdu_iline[k], "Pages:    ", 40);
    os88_utoa(pdu_npages, pdu_num);
    pdu_cat(pdu_iline[k], pdu_num, 40);
    pdu_hinfo[k] = pdu_iline[k]; k++;
    os88_strcpy(pdu_iline[k], "PDF:      1.", 40);
    os88_utoa(pdu_xopen.version % 10, pdu_num);
    pdu_cat(pdu_iline[k], pdu_num, 40);
    pdu_cat(pdu_iline[k], ", revisions ", 40);
    os88_utoa(pdu_xopen.nrev, pdu_num);
    pdu_cat(pdu_iline[k], pdu_num, 40);
    pdu_hinfo[k] = pdu_iline[k]; k++;
    os88_strcpy(pdu_iline[k], pdu_rdonly ? "Access:   read-only (signed)"
                                         : "Access:   annotations allowed", 40);
    pdu_hinfo[k] = pdu_iline[k]; k++;
    os88_strcpy(pdu_iline[k], "Page:     ", 40);
    pdu_cat(pdu_iline[k], pdu_inc ? "incomplete rendering" : "fully drawn",
            40);
    pdu_hinfo[k] = pdu_iline[k]; k++;
    /* the PDRES INDEX the primary holds (D3): a bank that did not load is
     * not an error, so this line is where it shows */
    if (pdu_bank) {
        os88_strcpy(pdu_iline[k], "Fonts:    index ", 40);
        os88_utoa(pdu_banklen, pdu_num);
        pdu_cat(pdu_iline[k], pdu_num, 40);
        pdu_cat(pdu_iline[k], " bytes", 40);
    } else
        os88_strcpy(pdu_iline[k], "Fonts:    none (system face)", 40);
    pdu_hinfo[k] = pdu_iline[k]; k++;
    /* the layout as chosen from the LIVE content box (17.1, 20.6.4) - the
     * number a host test can only estimate */
    os88_strcpy(pdu_iline[k], pdu_L.kind == PDU_L_FULL ? "Layout:   full "
                : pdu_L.kind == PDU_L_MID ? "Layout:   mid " : "Layout:   compact ", 40);
    os88_utoa(pdu_L.w, pdu_num);
    pdu_cat(pdu_iline[k], pdu_num, 40);
    pdu_cat(pdu_iline[k], "x", 40);
    os88_utoa(pdu_L.h, pdu_num);
    pdu_cat(pdu_iline[k], pdu_num, 40);
    pdu_hinfo[k] = pdu_iline[k]; k++;
    os88_strcpy(pdu_iline[k], "Free memory: ", 40);
    os88_utoa(os88_mem_total_kb(), pdu_num);
    pdu_cat(pdu_iline[k], pdu_num, 40);
    pdu_cat(pdu_iline[k], " KB", 40);
    pdu_hinfo[k] = pdu_iline[k]; k++;
    pdu_hinfo[k] = 0;
    return pdu_hinfo;
}

static int ovl_card_n(const char **l)
{
    int n;
    n = 0;
    while (l[n])
        n++;
    return n;
}

/* the card's width in characters and (pdu_ovrows) its rows on screen; 0 when
 * the overlay could not be had - which is what a refused call answers */
static int ovl_card_size(int which)
{
    const char **l;
    int n, w, k, fit;
    l = ovl_card_lines(which);
    n = ovl_card_n(l);
    w = 0;
    for (k = 0; k < n; k++)
        if (os88_strlen(l[k]) > w)
            w = os88_strlen(l[k]);
    /* rows in PDU_POP_ROW units for the placement; the card itself uses
     * PDU_CARD_ROW. PD-03: it must fit the canvas, so a long card pages. */
    fit = (pdu_ch() - 12) / PDU_CARD_ROW - 1;
    if (n + 1 > fit)
        n = fit - 1;
    pdu_ovrows = ((n + 1) * PDU_CARD_ROW + PDU_POP_ROW - 1) / PDU_POP_ROW;
    return w + 1;
}

static void ovl_card(int which, struct os88_rect *r, int mono)
{
    const char **l;
    int n, k, per, y, first;

    l = ovl_card_lines(which);
    n = ovl_card_n(l);
    pdu_fill(r->x1, r->y1, r->x2, r->y2, OS88_WHITE);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(r->x1, r->y1, r->x2, r->y2);
    os88_gfx_frame(r->x1 + 1, r->y1 + 1, r->x2 - 1, r->y2 - 1);
    per = (r->y2 - r->y1 + 1 - 8) / PDU_CARD_ROW - 1;
    if (per < 1)
        per = 1;
    first = pdu_hpage * per;
    if (first >= n)
        first = 0;
    y = r->y1 + 5;
    for (k = first; k < n && k < first + per; k++) {
        os88_font_run(r->x1 + 8, y, l[k], OS88_BLACK,
                      (k == 0 && !mono) ? OS88_WHITE : OS88_WHITE);
        y = y + PDU_CARD_ROW;
    }
    os88_font_run(r->x1 + 8, r->y2 - PDU_CARD_ROW,
                  (first + per < n) ? "Enter: more   Esc: close"
                                    : "Esc or a click closes",
                  OS88_BLACK, OS88_WHITE);
}

/* Enter on a card: the next screenful, or close at the end */
static int ovl_card_more(int which)
{
    const char **l;
    int n, per;
    struct os88_rect *r;
    r = &pdu_poprect;
    l = ovl_card_lines(which);
    n = ovl_card_n(l);
    per = (r->y2 - r->y1 + 1 - 8) / PDU_CARD_ROW - 1;
    if (per < 1)
        per = 1;
    if ((pdu_hpage + 1) * per >= n)
        return 1;
    pdu_hpage++;
    ovl_card(which, r, pdu_mono);
    return 2;
}
#include "pdovlpage.c"          /* pages: the page-operation commands and prompts */
