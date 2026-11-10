/* ============================================================================
 * os8088 - apps/pagedesk/pduside.c   THE PAGES / OUTLINE SIDEBAR
 *                                    (PAGEDESK-SPEC 10.8, 17.1; 20.10.4)
 *
 * Moved here from pdudraw.c with the thumbnails: a cell is a page's picture
 * once PDREND has drawn it (PDV_REND_THUMB, one 56x72 1bpp cell in the THUMB
 * claim's 512-byte slots) and its number and "..." until then - never a fake
 * picture (10.8). Pictures come ONE AT A TIME from the idle ladder
 * (ovl_nav_work): the current page's first, then the rest of what is visible,
 * and a page the renderer cannot draw is marked and not tried again.
 *
 * The THUMB claim is shared with the Outline: the two tabs are never both
 * on screen, and a tab switch empties whichever the other had (pdu_th_reset,
 * pdu_oln = 0). The Outline itself is the overlay's (pdovlnav.c).
 * ==========================================================================*/

#define PDU_CELL_W  56
#define PDU_CELL_H  72
#define PDU_TH_BYTES 512

static unsigned pdu_thpg[PDU_TH_SLOTS];         /* page + 1; |0x8000 = failed */
static unsigned pdu_thage[PDU_TH_SLOTS];
static unsigned pdu_thclk;
static int pdu_thslot, pdu_thpage, pdu_thst;
static struct pdxb_thumb pdu_xth;
static int pdu_sidesel = -1;

/* the outline (overlay): entries in the THUMB claim */
static int pdu_oln;                             /* entries, 0 = not loaded */
static int pdu_oltop;                           /* first row shown */
static int pdu_olxp;                            /* entry + 1 to expand/collapse */
static int pdu_olsel = -1;

static int pdu_side_sel(void)
{
    return pdu_sidesel >= 0 && pdu_sidesel < (int)pdu_npages ? pdu_sidesel : -1;
}

static void pdu_th_reset(void)
{
    int i;
    for (i = 0; i < PDU_TH_SLOTS; i++)
        pdu_thpg[i] = 0;
    pdu_oln = 0;
    pdu_oltop = 0;
    pdu_olsel = -1;
    pdu_olxp = 0;
    pdu_sidesel = -1;
}

/* the list area inside the sidebar */
/* the list well inside the sidebar (20.21.6): between the tab row and the
 * foot, its sunken frame on these edges */
static void pdu_side_list(struct os88_rect *o)
{
    o->x1 = pdu_L.side.x1 + 4;
    o->x2 = pdu_L.side.x2 - 5;
    o->y1 = pdu_L.side.y1 + PDU_SIDE_HEAD;
    o->y2 = pdu_L.side.y2 - PDU_SIDE_FOOT;
}

#define PDU_CELL_P  (PDU_CELL_H + 20)   /* a cell, its number and the gap */
static struct os88_rect pdu_cr;

static int pdu_side_rows(void)
{
    struct os88_rect *o;
    int n;
    o = &pdu_tr;
    pdu_side_list(o);
    n = (o->y2 - o->y1 + 1 - 6) / PDU_CELL_P;
    return n > PDU_TH_SLOTS / 2 ? PDU_TH_SLOTS / 2 : n;     /* eight cells */
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
    r->x1 = o->x1 + 5 + col * (PDU_CELL_W + 8);
    r->x2 = r->x1 + PDU_CELL_W - 1;
    r->y1 = o->y1 + 5 + row * PDU_CELL_P;
    r->y2 = r->y1 + PDU_CELL_H - 1;
    return r->y2 + 16 <= o->y2;
}

/* the cell's number under it (20.21.6): black on the panel's ground, or
 * the current page's in a teal box with white ink - the strips either side
 * of the box are the ground, so no pixel is written twice */
static void pdu_cell_num(int act)
{
    int n, w, bx, y, f, g;
    f = PK_CAP;
    g = pdu_mono ? OS88_WHITE : pdu_cbar;
    n = os88_strlen(pdu_num);
    y = pdu_cr.y2 + 4;
    if (!act) {
        w = n * pk_adv(f);
        if (pk_begin(PDU_CELL_W, pk_rows(f) + 2, pdu_cr.x1)) {
            pk_text((PDU_CELL_W - w) / 2, 1, pdu_num, f);
            pdu_kput(pdu_cr.x1, y, OS88_BLACK, g, 0);
        }
        return;
    }
    w = n * pk_adv(f) + 8;
    bx = pdu_cr.x1 + (PDU_CELL_W - w) / 2;
    pdu_fill(pdu_cr.x1, y, bx - 1, y + pk_rows(f) + 1, g);
    pdu_fill(bx + w, y, pdu_cr.x2, y + pk_rows(f) + 1, g);
    if (pk_begin(w, pk_rows(f) + 2, bx)) {
        pk_text(4, 1, pdu_num, f);
        pdu_kput(bx, y, OS88_WHITE, pdu_cact, 0);
    }
}

/* the current page's ring, two pixels outside the cell's shadow */
static void pdu_cell_ring(int act)
{
    os88_set_color(act ? pdu_cact : (pdu_mono ? OS88_WHITE : pdu_cbar));
    os88_gfx_frame(pdu_cr.x1 - 3, pdu_cr.y1 - 3, pdu_cr.x2 + 3, pdu_cr.y2 + 3);
    os88_gfx_frame(pdu_cr.x1 - 2, pdu_cr.y1 - 2, pdu_cr.x2 + 2, pdu_cr.y2 + 2);
}

/* the slot holding page pg's picture: -1 none yet, -2 it cannot be drawn */
static int pdu_th_find(int pg)
{
    int i;
    for (i = 0; i < PDU_TH_SLOTS; i++) {
        if (pdu_thpg[i] == (unsigned)(pg + 1))
            return i;
        if (pdu_thpg[i] == (unsigned)((pg + 1) | 0x8000))
            return -2;
    }
    return -1;
}

static void pdu_cell_paint(int k)
{
    int pg, act, n, slot;
    pg = pdu_sidetop + k;
    if (pg >= (int)pdu_npages || !pdu_cell(k, &pdu_cr))
        return;
    act = (pg == (int)pdu_page);
    slot = pdu_th_find(pg);
    if (slot >= 0 && pdu_dcseg(PDU_DC_THUMB)
        && os88_gfx_blit1_seg(pdu_dcseg(PDU_DC_THUMB), slot * PDU_TH_BYTES,
                              PDU_CELL_W / 8, pdu_cr.x1, pdu_cr.y1,
                              PDU_CELL_W, PDU_CELL_H) == 0) {
        pdu_thage[slot] = ++pdu_thclk;
        os88_set_color(OS88_BLACK);
        os88_gfx_frame(pdu_cr.x1, pdu_cr.y1, pdu_cr.x2, pdu_cr.y2);
    } else {
        pdu_fill(pdu_cr.x1, pdu_cr.y1, pdu_cr.x2, pdu_cr.y2, OS88_WHITE);
        os88_set_color(OS88_BLACK);
        os88_gfx_frame(pdu_cr.x1, pdu_cr.y1, pdu_cr.x2, pdu_cr.y2);
        /* 10.8: a page not yet drawn shows its number and "...", and one the
         * renderer could not draw says so */
        os88_font_run(pdu_cr.x1 + (PDU_CELL_W - 24) / 2, pdu_cr.y1 + 32,
                      slot == -2 ? " - " : "...", OS88_BLACK, OS88_WHITE);
    }
    os88_set_color(pdu_clo);            /* a one-pixel shadow (20.21.6) */
    os88_gfx_vline(pdu_cr.x2 + 1, pdu_cr.y1 + 1, pdu_cr.y2 + 1);
    os88_gfx_hline(pdu_cr.x1 + 1, pdu_cr.x2, pdu_cr.y2 + 1);
    if (act)                            /* the current page: the teal ring */
        pdu_cell_ring(1);
    os88_utoa(pg + 1, pdu_num);
    n = 0;
    pdu_cell_num(act);
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
    pdu_cground = pdu_cbar;
    for (i = 0; i < pdu_L.nctl; i++)
        if (pdu_L.ctl[i].id == C_SB_PAGES || pdu_L.ctl[i].id == C_SB_OUTL
            || pdu_L.ctl[i].id == C_SB_HIDE)
            pdu_ctl_paint(i);
    pdu_cground = -1;
    o = &pdu_tr;
    pdu_side_list(o);
    pdu_sunken(o->x1, o->y1, o->x2, o->y2);   /* the list's well (20.21.6) */
    o->x1++;
    o->y1++;
    o->x2--;
    o->y2--;
    pdu_fill(o->x1, o->y1, o->x2, o->y2, pdu_mono ? OS88_WHITE : pdu_cbar);
    if (pdu_doc != D_OPEN) {
        pdu_ptext(o->x1 + 4, o->y1 + 6, o->x2 - o->x1 - 7, "No document",
                  PK_TXT, OS88_BLACK, pdu_mono ? OS88_WHITE : pdu_cbar);
    } else if (pdu_sbtab == 0) {
        rows = pdu_side_rows();
        for (k = 0; k < rows * 2; k++)
            pdu_cell_paint(k);
        pdu_thwant = 1;
        pdu_kick();                     /* the pictures still owed */
    } else if (!(pdu_xopen.flags & PDO_OUTLINE)) {
        pdu_ptext(o->x1 + 4, o->y1 + 6, o->x2 - o->x1 - 7, "No outline",
                  PK_TXT, OS88_BLACK, pdu_mono ? OS88_WHITE : pdu_cbar);
    } else if (pdu_oln <= 0) {
        pdu_ptext(o->x1 + 4, o->y1 + 6, o->x2 - o->x1 - 7,
                  pdu_oln ? "No entries" : "Reading...",
                  PK_TXT, OS88_BLACK, pdu_mono ? OS88_WHITE : pdu_cbar);
        pdu_kick();
    } else
        ovl_ol_paint(o);
    /* the foot: "12 pages" beside the hide button */
    if (pdu_doc == D_OPEN) {
        os88_utoa(pdu_npages, pdu_line);
        pdu_cat(pdu_line, pdu_npages == 1 ? " page" : " pages",
                sizeof(pdu_line));
        pdu_ptext(s->x1 + 6, s->y2 - PDU_SIDE_FOOT + 8, 80, pdu_line, PK_CAP,
                  OS88_BLACK, pdu_cbar);
    }
}

/* the current page moved from `old`: the sidebar follows it. A page still
 * in the list costs two cells' marks (the heavy frame off one, on the other,
 * and their numbers); a page off the list pages the list by whole
 * screenfuls, which is a new list and is painted as one. Lock held. */
static void pdu_cell_mark(int k, int act)
{
    if (k < 0 || pdu_sidetop + k >= (int)pdu_npages || !pdu_cell(k, &pdu_cr))
        return;
    pdu_cell_ring(act);
    os88_utoa(pdu_sidetop + k + 1, pdu_num);
    pdu_cell_num(act);
}

static void pdu_side_page(int old)
{
    int cells;
    if (pdu_L.side.x1 > pdu_L.side.x2 || pdu_doc != D_OPEN)
        return;
    cells = pdu_side_rows() * 2;
    if (cells <= 0)
        return;
    if ((int)pdu_page < pdu_sidetop || (int)pdu_page >= pdu_sidetop + cells) {
        pdu_sidetop = ((int)pdu_page / cells) * cells;
        if (pdu_sbtab == 0)
            pdu_side_paint();
        return;
    }
    if (pdu_sbtab != 0 || old == (int)pdu_page)
        return;
    pdu_cell_mark(old - pdu_sidetop, 0);
    pdu_cell_mark((int)pdu_page - pdu_sidetop, 1);
}

/* a click in the sidebar's list: a page cell or an outline row */
static void ovl_side_click(int x, int y)
{
    int k;
    struct os88_rect *o;
    o = &pdu_tr;
    pdu_side_list(o);
    if (!pdu_inr(o, x, y))
        return;
    if (pdu_sbtab == 1) {
        if (pdu_oln > 0)
            ovl_ol_click(x, y);
        return;
    }
    for (k = 0; k < pdu_side_rows() * 2; k++)
        if (pdu_cell(k, &pdu_cr) && x >= pdu_cr.x1 && x <= pdu_cr.x2
            && y >= pdu_cr.y1 && y <= pdu_cr.y2 + 10) {
            pdu_sidesel = pdu_sidetop + k;
            if (pdu_sidesel < (int)pdu_npages) {
                pdu_hjump = 1;
                pdu_goto(pdu_sidesel);
            }
            return;
        }
}

/* the wheel over the sidebar's list scrolls the list, not the page */
static void ovl_side_wheel(int notches)
{
    int cells, n, t;
    cells = pdu_side_rows() * 2;
    if (pdu_sbtab == 0) {
        t = pdu_sidetop + notches * 2;
        n = (int)pdu_npages - 1;
        if (t > n) t = n - n % (cells > 0 ? cells : 2);
        if (t < 0) t = 0;
        if (t != pdu_sidetop) {
            pdu_sidetop = t;
            pdu_side_paint();
        }
        return;
    }
    if (pdu_oln <= 0)
        return;
    t = pdu_oltop + notches + (notches << 1);
    if (t > pdu_oln - 1) t = pdu_oln - 1;
    if (t < 0) t = 0;
    if (t != pdu_oltop) {
        pdu_oltop = t;
        pdu_side_paint();
    }
}

/* ---- thumbnails: the J_THUMB job ----------------------------------------- */
/* the first visible page without a picture (the current page first), or -1 */
static int ovl_th_pick(void)
{
    int k, pg, cells;
    if (pdu_sbtab != 0 || pdu_doc != D_OPEN || pdu_L.side.x1 > pdu_L.side.x2
        || !pdu_dcseg(PDU_DC_THUMB))
        return -1;
    cells = pdu_side_rows() * 2;
    if ((int)pdu_page >= pdu_sidetop && (int)pdu_page < pdu_sidetop + cells
        && pdu_th_find(pdu_page) == -1)
        return pdu_page;
    for (k = 0; k < cells; k++) {
        pg = pdu_sidetop + k;
        if (pg >= (int)pdu_npages)
            break;
        if (pdu_th_find(pg) == -1)
            return pg;
    }
    return -1;
}

static void ovl_th_begin(int pg)
{
    int i, slot;
    slot = -1;
    for (i = 0; i < PDU_TH_SLOTS; i++)
        if (pdu_thpg[i] == 0) {
            slot = i;
            break;
        }
    if (slot < 0)                       /* none free: the oldest picture */
        for (i = 0, slot = 0; i < PDU_TH_SLOTS; i++)
            if ((int)(pdu_thage[i] - pdu_thage[slot]) < 0)
                slot = i;
    pdu_oln = 0;                        /* the claim is the pictures' now */
    pdu_thpg[slot] = 0;
    pdu_thage[slot] = ++pdu_thclk;
    pdu_thslot = slot;
    pdu_thpage = pg;
    pdu_thst = 0;
    pdu_job_start(J_THUMB);
}

static void ovl_th_step(void)
{
    unsigned r;
    int k;

    if (pdu_thst == 0) {
        os88_memset(&pdu_xth, 0, sizeof(pdu_xth));
        pdu_xth.page = pdu_thpage;
        pdu_xth.w = PDU_CELL_W;
        pdu_xth.h = PDU_CELL_H;
        pdu_xth.dseg = pdu_dcseg(PDU_DC_THUMB);
        pdu_xth.doff = pdu_thslot * PDU_TH_BYTES;
        pdu_thst = 1;
    }
    pd_fcopy_out(&pdu_xth, pdu_xb, 0, sizeof(pdu_xth));
    r = pdu_mcall(PDU_M_REND, PDV_REND_THUMB, 0, 0, 0);
    pdu_jslices++;
    if (!pdu_mcf && r == PDE_BUSY) {
        pd_fcopy_in(pdu_xb, 0, &pdu_xth, sizeof(pdu_xth));
        return;
    }
    pdu_job = J_NONE;
    pdu_thpg[pdu_thslot] = (pdu_thpage + 1) | (pdu_mcf ? 0x8000 : 0);
    k = pdu_thpage - pdu_sidetop;
    if (pdu_sbtab == 0 && k >= 0 && k < pdu_side_rows() * 2) {
        os88_gfx_lock();
        pdu_cell_paint(k);
        os88_gfx_unlock();
    }
}
