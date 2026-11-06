/* ============================================================================
 * os8088 - apps/pagedesk/pdovlnotes.c   THE OVERLAY'S NOTES TENANTS
 *                                       (PAGEDESK-SPEC 20.9)
 *
 * Included from pdovl.c, so every ovl_* below is cut into the overlay part
 * (SPEC.md 73.14) and fetched the first time one is called - which pdunotes.c
 * arranges to be a click or a paint that is about notes. The pixels of a
 * card, the list, its clicks, Resolve/Reopen, jump-to, the note popover and
 * its text field live here; pduinsp.c has the inspector.
 *
 * A card is the Hercules mockup's: kind and page, the first words, and on a
 * note its Resolve/Reopen button - 40 rows, every pixel written once but the
 * button's. On COMPACT a row is one line of text.
 * ==========================================================================*/

static char pdu_nt_w[48];               /* a card's title, a line's text */

static void ovl_nt_pbtn(int k, struct os88_rect *r);
static void ovl_nt_plistp(void);
static void ovl_nt_pact(int k);
static int  ovl_nt_pnote(void);
static int  ovl_nt_ipaint(struct os88_rect *o, int rh);
static int  ovl_nt_iclick(int x, int y);
static int  ovl_nt_ikey(int ascii, int scan);
static pd_px pdu_nt_p0, pdu_nt_p1, pdu_nt_p2, pdu_nt_p3;  /* &local: no */

/* pdu_line := s cut or padded to cols characters */
static void ovl_nt_pad(const char *s, int cols)
{
    int i;
    for (i = 0; i < cols && s[i]; i++)
        pdu_line[i] = s[i];
    while (i < cols)
        pdu_line[i++] = ' ';
    pdu_line[cols] = 0;
}

/* a labelled button: the frame, the slabs round the label and the label in
 * one font_run - every pixel once, so a toggled state does not flash the
 * caption (20.6.4's rule, PERFORMANCE.md rule 2) */
static void ovl_nt_box(struct os88_rect *r, int paper, int ink, const char *s)
{
    int lx, ly, lx2;
    lx = r->x1 + (r->x2 - r->x1 + 1 - os88_strlen(s) * PDU_CW) / 2;
    ly = r->y1 + (r->y2 - r->y1 + 1 - 8) / 2;
    lx2 = lx + os88_strlen(s) * PDU_CW;
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(r->x1, r->y1, r->x2, r->y2);
    if (ly > r->y1 + 1)
        pdu_fill(r->x1 + 1, r->y1 + 1, r->x2 - 1, ly - 1, paper);
    if (ly + 8 < r->y2)
        pdu_fill(r->x1 + 1, ly + 8, r->x2 - 1, r->y2 - 1, paper);
    if (lx > r->x1 + 1)
        pdu_fill(r->x1 + 1, ly, lx - 1, ly + 7, paper);
    if (lx2 < r->x2)
        pdu_fill(lx2, ly, r->x2 - 1, ly + 7, paper);
    os88_font_run(lx, ly, s, ink, paper);
}

/* the list's rect in the panel: under the header */
static void ovl_nt_plist(struct os88_rect *o)
{
    o->x1 = pdu_L.notes.x1 + 4;
    o->x2 = pdu_L.notes.x2 - 4;
    o->y1 = pdu_L.notes.y1 + 2 * PDU_BAR_F + 4;
    o->y2 = pdu_L.notes.y2 - 4;
}

/* rows, strip and button rects for pdu_nt_o, cards (the panel) or lines */
static void ovl_nt_geom(int card)
{
    struct os88_rect *o;
    int avail, h;
    o = &pdu_nt_o;
    pdu_nt_cardst = card;
    h = card ? PDU_NT_PITCH : PDU_NT_LINE;
    pdu_nt_y0 = o->y1 + (card ? 2 : 0);
    pdu_nt_tx = o->x1 + (card ? 4 : 0);
    pdu_nt_cols = card ? 18 : (o->x2 - o->x1 + 1) / PDU_CW;
    pdu_nt_ny = card ? o->y2 - 29 : o->y2 - PDU_NT_LINE + 1;
    avail = pdu_nt_ny - pdu_nt_y0 - (card ? 2 : 0);
    pdu_nt_vis = (avail + (card ? 2 : 0)) / h;
    if (pdu_nt_vis < 1)
        pdu_nt_vis = 1;
    if (pdu_nt_top > pdu_nt_cnt - pdu_nt_vis)
        pdu_nt_top = pdu_nt_cnt - pdu_nt_vis;
    if (pdu_nt_top < 0)
        pdu_nt_top = 0;
    h = pdu_nt_y0 * 4 + pdu_nt_vis * 2 + card + pdu_nt_tx * 64;
    if (h != pdu_nt_gkey) {
        pdu_nt_gkey = h;
        pdu_nt_rok = 0;
    }
    if (!pdu_nt_rok) {                  /* nothing on show is known */
        os88_memset(pdu_nt_rsig, 0, sizeof(pdu_nt_rsig));
        pdu_nt_ssig = 0;
        pdu_nt_rok = 1;
    }
}

/* a row's picture as one word: what it says and how (a changed word is a
 * repaint; an unchanged one is no call at all) */
static unsigned ovl_nt_sig(unsigned h, const char *s)
{
    while (*s)
        h = (h << 5) - h + (unsigned char)*s++;
    return h;
}

/* the strip under the list: "<   1-4/9   >" or blanks, one font_run */
static void ovl_nt_strip(void)
{
    int n, b;
    char *m;
    m = pdu_line + 40;
    if (pdu_nt_cnt > pdu_nt_vis) {
        b = pdu_nt_top + pdu_nt_vis;
        if (b > pdu_nt_cnt)
            b = pdu_nt_cnt;
        os88_utoa(pdu_nt_top + 1, m);
        pdu_cat(m, "-", 20);
        os88_utoa(b, pdu_num);
        pdu_cat(m, pdu_num, 20);
        pdu_cat(m, "/", 20);
        os88_utoa(pdu_nt_cnt, pdu_num);
        pdu_cat(m, pdu_num, 20);
    } else
        m[0] = 0;
    ovl_nt_pad("", pdu_nt_cols);       /* blanks, then the count centred */
    n = (pdu_nt_cols - os88_strlen(m)) / 2;
    os88_memcpy(pdu_line + n, m, os88_strlen(m));
    if (pdu_nt_cnt > pdu_nt_vis) {
        if (pdu_nt_top > 0)
            pdu_line[0] = '<';
        if (pdu_nt_top + pdu_nt_vis < pdu_nt_cnt)
            pdu_line[pdu_nt_cols - 1] = '>';
    }
    b = ovl_nt_sig(7, pdu_line) | 1;
    if (b == pdu_nt_ssig)
        return;
    pdu_nt_ssig = b;
    os88_font_run(pdu_nt_tx, pdu_nt_ny + 2, pdu_line, OS88_BLACK,
                  pdu_nt_cardst ? pdu_cbar : OS88_WHITE);
}

/* paint row r: item id (an edit's record in pdu_nt_e), or a blank slot */
static void ovl_nt_row(int r, int id, int sel)
{
    struct os88_rect *o;
    int x1, y1, ink, paper, kind, page, rep, done, n, btn;
    const char *word;
    char *t;

    o = &pdu_nt_o;
    x1 = o->x1 + 3;
    y1 = pdu_nt_y0 + r * (pdu_nt_cardst ? PDU_NT_PITCH : PDU_NT_LINE);
    if (id < 0) {
        if (r < PDU_NT_ROWS) {
            if (pdu_nt_rsig[r] == 1)
                return;
            pdu_nt_rsig[r] = 1;         /* 1 = blank */
        }
        if (pdu_nt_cardst)
            pdu_fill(o->x1, y1, o->x2, y1 + PDU_NT_CARD - 1, pdu_cbar);
        else
            pdu_fill(pdu_nt_tx, y1, pdu_nt_tx + pdu_nt_cols * PDU_CW - 1,
                     y1 + 7, OS88_WHITE);
        return;
    }
    rep = 0;
    word = "";
    t = pdu_line + 40;
    t[0] = 0;                           /* the preview, 17 characters */
    if (id >= PDU_NT_SBASE) {
        n = id - PDU_NT_SBASE;
        kind = pdu_nt_sk[n];
        page = pdu_page;
        os88_strcpy(t, pdu_nt_stx[n], 18);
        done = kind == PDA_NOTE && ovl_nt_done(1, pdu_nt_so[n]);
    } else {
        kind = pdu_nt_e.kind;
        page = PDU_NT_VPAGE(pdu_nt_e.page);
        rep = PDU_NT_ISREPLY(&pdu_nt_e);
        n = pdu_ed_text_get(id, t, 18);
        t[n < 17 ? n : 17] = 0;
        done = kind == PDA_NOTE && !rep && ovl_nt_done(0, pdu_nt_e.id);
        if (pdu_npall == 2)
            word = ovl_nt_change(&pdu_nt_e);
        if (rep)
            os88_strcpy(t, pdu_nt_e.state == PDS_COMPLETED ? "resolved"
                        : "reopened", 18);
    }
    if (kind < 1 || kind > 9)
        kind = 0;
    /* a record with no text (a mark, ink, a shape) has a blank second line:
     * its title already says what it is, and a table of stand-in phrases
     * was 93 resident bytes */
    if (word[0] == 0 && done)
        word = "Resolved";
    paper = sel ? pdu_cact : OS88_WHITE;
    ink = sel ? OS88_WHITE : OS88_BLACK;
    /* the title in pdu_nt_w: "Note - Page 3" on a card, "Note p3" on a line */
    os88_strcpy(pdu_nt_w, rep ? "Review" : pdu_nt_kname[kind], 48);
    pdu_cat(pdu_nt_w, pdu_nt_cardst ? " - Page " : " p", 48);
    os88_utoa(page + 1, pdu_num);
    pdu_cat(pdu_nt_w, pdu_num, 48);
    btn = kind == PDA_NOTE && !rep && pdu_npall != 2 ? 2 + done : 0;
    if (r < PDU_NT_ROWS) {              /* the same picture: no call */
        n = ovl_nt_sig(ovl_nt_sig(ovl_nt_sig(sel + done * 2 + btn * 4,
                                             pdu_nt_w), t), word) | 2;
        if (pdu_nt_rsig[r] == (unsigned)n)
            return;
        pdu_nt_rsig[r] = n;
    }

    if (!pdu_nt_cardst) {               /* COMPACT: one line */
        pdu_cat(pdu_nt_w, done ? " ok: " : ": ", 48);
        pdu_cat(pdu_nt_w, t, 48);
        ovl_nt_pad(pdu_nt_w, pdu_nt_cols);
        os88_font_run(pdu_nt_tx, y1, pdu_line, ink, paper);
        return;
    }

    /* the card: 146 x 36, interior 18 cells wide, so no slab beside a row.
     * title / preview / a 12-row band: a status word and, on a note, the
     * button (72 wide, the band's right half) */
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(x1, y1, x1 + 145, y1 + PDU_NT_CARD - 1);
    pdu_fill(x1 + 1, y1 + 1, x1 + 144, y1 + 2, paper);
    ovl_nt_pad(pdu_nt_w, 18);
    os88_font_run(x1 + 1, y1 + 3, pdu_line, ink, paper);
    pdu_fill(x1 + 1, y1 + 11, x1 + 144, y1 + 12, paper);
    ovl_nt_pad(t, 18);
    os88_font_run(x1 + 1, y1 + 13, pdu_line, ink, paper);
    pdu_fill(x1 + 1, y1 + 21, x1 + 144, y1 + 22, paper);
    pdu_fill(x1 + 1, y1 + 23, x1 + 72, y1 + 24, paper);
    ovl_nt_pad(word, 9);
    os88_font_run(x1 + 1, y1 + 25, pdu_line, ink, paper);
    pdu_fill(x1 + 1, y1 + 33, x1 + 72, y1 + 34, paper);
    pdu_nt_cr.x1 = x1 + 73;
    pdu_nt_cr.x2 = x1 + 144;
    pdu_nt_cr.y1 = y1 + 23;
    pdu_nt_cr.y2 = y1 + 34;
    if (btn)
        ovl_nt_box(&pdu_nt_cr, paper, ink, done ? "Reopen" : "Resolve");
    else
        pdu_fill(pdu_nt_cr.x1, pdu_nt_cr.y1, pdu_nt_cr.x2, pdu_nt_cr.y2, paper);
}

/* rows first..last (the iterator walks once); the empty list says so */
static void ovl_nt_rows(int first, int last)
{
    int r, id;
    if (last >= pdu_nt_vis)
        last = pdu_nt_vis - 1;
    if (pdu_nt_cnt == 0) {              /* blank rows 1.., then row 0 says so */
        for (r = first > 0 ? first : 1; r <= last; r++)
            ovl_nt_row(r, -1, 0);
        if (first > 0 || pdu_nt_rsig[0] == 4 + 8 * (unsigned)pdu_npall)
            return;
        pdu_nt_rsig[0] = 4 + 8 * pdu_npall;     /* bit 1 clear: no row's sig */
        ovl_nt_pad(pdu_npall == 2 ? "No changes yet" :
                   pdu_npall ? "No notes or edits" : "None on this page",
                   pdu_nt_cols);
        if (!pdu_nt_cardst) {           /* a line is its own row */
            os88_font_run(pdu_nt_tx, pdu_nt_y0, pdu_line, OS88_BLACK,
                          OS88_WHITE);
            return;
        }
        id = pdu_cbar;
        r = PDU_NT_CARD;
        pdu_fill(pdu_nt_o.x1, pdu_nt_y0, pdu_nt_o.x2, pdu_nt_y0 + 5, id);
        if (pdu_nt_tx > pdu_nt_o.x1)
            pdu_fill(pdu_nt_o.x1, pdu_nt_y0 + 6, pdu_nt_tx - 1,
                     pdu_nt_y0 + 13, id);
        if (pdu_nt_tx + pdu_nt_cols * PDU_CW <= pdu_nt_o.x2)
            pdu_fill(pdu_nt_tx + pdu_nt_cols * PDU_CW, pdu_nt_y0 + 6,
                     pdu_nt_o.x2, pdu_nt_y0 + 13, id);
        os88_font_run(pdu_nt_tx, pdu_nt_y0 + 6, pdu_line, OS88_BLACK, id);
        if (r > 14)
            pdu_fill(pdu_nt_o.x1, pdu_nt_y0 + 14, pdu_nt_o.x2,
                     pdu_nt_y0 + r - 1, id);
        return;
    }
    ovl_nt_seek(pdu_nt_top + first);
    for (r = first; r <= last; r++) {
        id = ovl_nt_step();
        ovl_nt_row(r, id, id >= 0 && id == pdu_nt_sel);
    }
}

/* Text properties > : the panel's last row, grey until a text box is chosen */
static void ovl_nt_tpbtn(void)
{
    struct os88_rect *o;
    int on;
    o = &pdu_nt_o;
    on = pdu_nt_sel >= 0 && pdu_nt_sel < PDU_NT_SBASE
         && pdu_ed_get(pdu_nt_sel, &pdu_nt_e) == 0 && pdu_nt_e.kind == PDA_TEXT;
    if (on == pdu_nt_tpon)
        return;                         /* it already says that */
    pdu_nt_tpon = on;
    pdu_nt_cr.x1 = o->x1 + 2;
    pdu_nt_cr.x2 = o->x2 - 2;
    pdu_nt_cr.y1 = o->y2 - 15;
    pdu_nt_cr.y2 = o->y2 - 1;
    if (!on)
        os88_gfx_pen(1);
    ovl_nt_box(&pdu_nt_cr, pdu_cbar, on || pdu_mono ? OS88_BLACK : OS88_DGRAY,
               "Text properties >");
    os88_gfx_pen(0);
}

/* the whole list: rows, strip. Lock held. */
static void ovl_nt_listpaint(void)
{
    ovl_nt_sync();
    pdu_nt_seen = (int)pdu_ed_gen();    /* the store as it is now shown */
    ovl_nt_geom(pdu_nt_cardst);
    ovl_nt_rows(0, pdu_nt_vis - 1);
    ovl_nt_strip();
}

/* the panel's body: the inspector, or the list and its button */
static int ovl_nt_body(void)
{
    ovl_nt_plist(&pdu_nt_o);
    if (pdu_nt_insp)
        return ovl_nt_ipaint(&pdu_nt_o, 16);
    pdu_nt_srcwant();
    pdu_nt_cardst = 1;
    ovl_nt_listpaint();
    ovl_nt_tpbtn();
    return 1;
}

/* the list region again (a filter, a scroll, the store) - not while the
 * inspector or a popover has the panel or the page */
static void ovl_nt_relist(void)
{
    if (PDU_NT_PANEL() && pdu_pop == P_NONE && !pdu_nt_insp)
        ovl_nt_body();
}

/* the page is the one the selected item is on: bring its rect into view */
static void ovl_nt_reveal(void)
{
    int py;
    pdu_nt_rvl = 0;
    pdm_view_fwd(&pdu_nt_p0, &pdu_nt_p1, &pdu_view, pdu_nt_rr.x0, pdu_nt_rr.y0);
    pdm_view_fwd(&pdu_nt_p2, &pdu_nt_p3, &pdu_view, pdu_nt_rr.x1, pdu_nt_rr.y1);
    py = pdu_nt_p1 < pdu_nt_p3 ? pdu_nt_p1 : pdu_nt_p3;
    if (py < pdu_L.canvas.y1 + 8 || py > pdu_L.canvas.y2 - 24)
        pdu_scroll_to(pdu_sx, pdu_sy + py - (pdu_L.canvas.y1 + 24));
}

static void ovl_nt_page(void)
{
    if (pdu_npall != 2) {               /* the source rows are the page's */
        if (pdu_npall == 0)
            pdu_nt_top = 0;
        pdu_nt_cnt = -1;
        ovl_nt_relist();
    }
    if (pdu_nt_rvl)
        ovl_nt_reveal();
}

/* the record's rect on screen, rendered again: the page is not filled first,
 * so a render of the rect is all a change needs */
static void ovl_nt_redraw(const struct pd_rect *r)
{
    pdm_view_fwd(&pdu_nt_p0, &pdu_nt_p1, &pdu_view, r->x0, r->y0);
    pdm_view_fwd(&pdu_nt_p2, &pdu_nt_p3, &pdu_view, r->x1, r->y1);
    pdu_nt_cr.x1 = (pdu_nt_p0 < pdu_nt_p2 ? pdu_nt_p0 : pdu_nt_p2) - 4;
    pdu_nt_cr.x2 = (pdu_nt_p0 < pdu_nt_p2 ? pdu_nt_p2 : pdu_nt_p0) + 4;
    pdu_nt_cr.y1 = (pdu_nt_p1 < pdu_nt_p3 ? pdu_nt_p1 : pdu_nt_p3) - 4;
    pdu_nt_cr.y2 = (pdu_nt_p1 < pdu_nt_p3 ? pdu_nt_p3 : pdu_nt_p1) + 4;
    if (pdu_isect(&pdu_nt_cr, &pdu_L.canvas, &pdu_tr))
        pdu_render_want(&pdu_tr);
}

/* the source cache: this page's annotations, 8 at most */
static void ovl_nt_srcload(void)
{
    int k, n, kd, j;
    struct pdxb_annot *a;

    /* the head of each answer into pdu_line (72 bytes; nothing else is using
     * it between paints) and the first 17 characters straight into the cache,
     * never pdu_tbuf: that is edit's open text box as well as ours (D19) */
    a = (struct pdxb_annot *)pdu_line;
    pdu_nt_ns = 0;
    pdu_nt_nsr = 0;
    pdu_nt_spage = pdu_page + 1;
    pdu_nt_cnt = -1;
    if (pdu_nt_sel >= PDU_NT_SBASE)
        pdu_nt_sel = -1;                /* a slot of another page's cache */
    n = pdu_pinfo.nannots;
    if (n > PDU_NT_SCAN)
        n = PDU_NT_SCAN;
    for (k = 0; k < n; k++) {
        pdu_mcall(PDU_M_ENG, PDV_ENG_ANNOTS, PDU_NT_PGID(pdu_page), k, 0);
        if (pdu_mcf)
            continue;
        pd_fcopy_in(pdu_xb, 0, a, PDU_NT_AHEAD);
        if (a->irt_num) {
            if (pdu_nt_nsr < 6) {
                pdu_nt_srk[pdu_nt_nsr] = a->irt_num;
                pdu_nt_srs[pdu_nt_nsr++] = a->state;
            }
            continue;
        }
        kd = ovl_nt_kind(a->subtype);
        if (kd == 0 || pdu_nt_ns >= PDU_NT_SRC)
            continue;
        j = pdu_nt_ns++;
        pdu_nt_so[j] = a->objnum;
        pdu_nt_sk[j] = kd;
        pdu_nt_sh[j] = 0;
        os88_memcpy(&pdu_nt_sr[j], &a->rect, sizeof(struct pd_rect));
        pd_fcopy_in(pdu_xb, PDU_NT_AHEAD, pdu_nt_stx[j], 17);
        pdu_nt_stx[j][17] = 0;
    }
}

/* the store (or a selection) changed - pdu_ed_gen() moved, or one of our own
 * commands ran. Repaints the rows that changed, when the list is on screen. */
static void ovl_nt_dirty(void)
{
    int s;
    pdu_nt_gen++;
    pdu_nt_seen = (int)pdu_ed_gen();
    s = pdu_ed_selected();
    if (s != pdu_nt_edsel) {
        pdu_nt_edsel = s;
        pdu_nt_sel = s;
    }
    if (pdu_doc != D_OPEN)
        return;
    if (pdu_pop == P_NOTES)
        ovl_nt_plistp();
    else if (pdu_pop == P_NONE)
        ovl_nt_relist();
}

/* the wake handler, idle: this page's source cache when it is owed, then ONE
 * repaint of the rows that changed - for the load, a popover that closed or
 * the store's generation */
static void ovl_nt_idle(void)
{
    unsigned was;
    was = pdu_nt_spage;
    if (pdu_nt_late)
        pdu_nt_srcload();
    if (pdu_nt_post || (int)pdu_ed_gen() != pdu_nt_seen
        || pdu_nt_spage != was) {
        pdu_nt_post = 0;
        os88_gfx_lock();
        ovl_nt_dirty();
        os88_gfx_unlock();
    }
}

/* --- Resolve / Reopen (13.4): a reply record ----------------------------------- */
static void ovl_nt_resolve(int id)
{
    int src, done, r;
    unsigned key;
    struct pd_edit *e;

    e = &pdu_nt_e2;
    os88_memset(e, 0, sizeof(*e));
    src = id >= PDU_NT_SBASE;
    if (src) {
        key = pdu_nt_so[id - PDU_NT_SBASE];
        os88_memcpy(&e->rect, &pdu_nt_sr[id - PDU_NT_SBASE],
                    sizeof(struct pd_rect));
        e->page = PDU_NT_PGID(pdu_page);
    } else {
        if (pdu_ed_get(id, &pdu_nt_e) != 0)
            return;
        key = pdu_nt_e.id;
        os88_memcpy(&e->rect, &pdu_nt_e.rect, sizeof(struct pd_rect));
        e->page = pdu_nt_e.page;
    }
    done = ovl_nt_done(src, key);
    e->kind = PDA_NOTE;
    e->flags = src ? PDEF_IRTSRC : 0;
    e->irt = key;
    e->state = done ? PDS_NONE : PDS_COMPLETED;
    pdu_undo_begin(UL_NOTE);
    r = pdu_ed_new(e, "", 0, 0, 0);
    pdu_undo_end();
    if (r < 0) {
        pdu_tell("The edit store is full: save, then continue.");
        return;
    }
    pdu_tell(done ? "Note reopened." : "Note resolved.");
    ovl_nt_dirty();
}

/* select item id and jump to it: the page, the record, the view */
static void ovl_nt_pick(int id)
{
    int page, k;

    pdu_nt_sel = id;
    if (id < PDU_NT_SBASE) {
        if (pdu_ed_get(id, &pdu_nt_e) != 0)
            return;
        page = PDU_NT_VPAGE(pdu_nt_e.page);
        os88_memcpy(&pdu_nt_rr, &pdu_nt_e.rect, sizeof(struct pd_rect));
        pdu_nt_edsel = id;
    } else {
        page = pdu_page;
        os88_memcpy(&pdu_nt_rr, &pdu_nt_sr[id - PDU_NT_SBASE],
                    sizeof(struct pd_rect));
        pdu_nt_edsel = -1;
    }
    pdu_ed_select(pdu_nt_edsel);
    if (page != (int)pdu_page) {
        if (page >= (int)pdu_npages)
            pdu_tell("That page is not in this document.");
        else {
            pdu_nt_rvl = 1;
            pdu_goto(page);             /* the wake loads it; pdu_notes_page */
        }
    } else
        ovl_nt_reveal();
    /* the list follows the item; the rows' pictures make this the two rows
     * the selection touched, or the list when it had to move */
    k = ovl_nt_find(id);
    if (k >= 0 && (k < pdu_nt_top || k >= pdu_nt_top + pdu_nt_vis))
        pdu_nt_top = k < pdu_nt_top ? k : k - pdu_nt_vis + 1;
    ovl_nt_relist();
}

/* one click on row r of the list */
static void ovl_nt_rowclick(int r, int x, int y, int popover)
{
    int id, note;
    ovl_nt_seek(pdu_nt_top + r);
    id = ovl_nt_step();
    if (id < 0)
        return;
    note = id >= PDU_NT_SBASE ? pdu_nt_sk[id - PDU_NT_SBASE] == PDA_NOTE
           : pdu_nt_e.kind == PDA_NOTE && !PDU_NT_ISREPLY(&pdu_nt_e);
    if (popover) {
        pdu_pop_close();
        ovl_nt_pick(id);
        if (id < PDU_NT_SBASE && note)
            ovl_nt_open(id);
        else if (id < PDU_NT_SBASE && pdu_nt_e.kind == PDA_TEXT)
            ovl_nt_inspect(id);
        return;
    }
    /* the card's Resolve / Reopen button: the band's right 72 px */
    if (pdu_npall != 2 && note && x >= pdu_nt_o.x1 + 3 + 73
        && y >= pdu_nt_y0 + r * PDU_NT_PITCH + 23)
        ovl_nt_resolve(id);
    else if (id == pdu_nt_sel && id < PDU_NT_SBASE) {   /* again: edit it */
        if (note)
            ovl_nt_open(id);
        else if (pdu_nt_e.kind == PDA_TEXT)
            ovl_nt_inspect(id);
    } else
        ovl_nt_pick(id);
}

/* the strip's two ends scroll by a screenful: 1 = it was the strip */
static int ovl_nt_stripclick(int x, int y)
{
    int k;
    if (y < pdu_nt_ny)
        return 0;
    k = pdu_nt_top;
    if (x < pdu_nt_o.x1 + 40)
        k = k - pdu_nt_vis;
    else if (x > pdu_nt_o.x2 - 40)
        k = k + pdu_nt_vis;
    if (k > pdu_nt_cnt - pdu_nt_vis)
        k = pdu_nt_cnt - pdu_nt_vis;
    if (k < 0)
        k = 0;
    if (k != pdu_nt_top) {
        pdu_nt_top = k;
        if (pdu_pop == P_NOTES)
            ovl_nt_plistp();
        else
            ovl_nt_relist();
    }
    return 1;
}

/* a click in the panel's body: 1 = ours */
static int ovl_nt_click(int x, int y)
{
    int r;
    ovl_nt_plist(&pdu_nt_o);
    if (x < pdu_nt_o.x1 || x > pdu_nt_o.x2)
        return 0;
    if (pdu_nt_insp)
        return ovl_nt_iclick(x, y);
    ovl_nt_sync();
    ovl_nt_geom(1);
    if (y >= pdu_nt_o.y2 - 15) {                /* Text properties */
        ovl_nt_inspect(pdu_nt_sel);
        return 1;
    }
    if (ovl_nt_stripclick(x, y))
        return 1;
    r = (y - pdu_nt_y0) / PDU_NT_PITCH;
    if (y >= pdu_nt_y0 && r < pdu_nt_vis)
        ovl_nt_rowclick(r, x, y, 0);
    return 1;
}

/* --- the COMPACT popover: the list, one line each ------------------------------ */
static int ovl_nt_ppaint(void)
{
    struct os88_rect *p;
    int y;

    p = &pdu_poprect;
    pdu_fill(p->x1, p->y1, p->x2, p->y2, OS88_WHITE);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(p->x1, p->y1, p->x2, p->y2);
    os88_gfx_frame(p->x1 + 1, p->y1 + 1, p->x2 - 1, p->y2 - 1);
    y = p->y1 + 4;
    if (pdu_pop == P_NOTE)
        return ovl_nt_pnote();
    os88_font_run(p->x1 + 8, y + 2, "Notes & edits", OS88_BLACK, OS88_WHITE);
    os88_set_color(OS88_BLACK);
    os88_gfx_hline(p->x1 + 2, p->x2 - 2, y + PDU_POP_ROW - 1);
    pdu_nt_rok = 0;                     /* just filled: nothing on show */
    ovl_nt_plistp();
    return 1;
}

/* the popover's list again - rows, strip - over what it last showed */
static void ovl_nt_plistp(void)
{
    struct os88_rect *p, *o;
    p = &pdu_poprect;
    pdu_nt_srcwant();
    o = &pdu_nt_o;
    o->x1 = p->x1 + 8;
    o->x2 = p->x2 - 8;
    o->y1 = p->y1 + 4 + PDU_POP_ROW + 2;
    o->y2 = p->y2 - 4;
    pdu_nt_cardst = 0;
    ovl_nt_listpaint();
}

/* a click in a popover: 1 when it was ours */
static int ovl_nt_pclick(int x, int y)
{
    int r;
    struct os88_rect *b;
    if (pdu_pop == P_NOTE) {
        if (pdu_nt_pm)
            return ovl_nt_iclick(x, y);
        b = &pdu_nt_cr;
        for (r = 0; r < 3; r++) {
            ovl_nt_pbtn(r, b);
            if (pdu_inr(b, x, y)) {
                ovl_nt_pact(r);
                return 1;
            }
        }
        return 1;
    }
    ovl_nt_sync();
    ovl_nt_geom(0);
    if (y >= pdu_nt_ny) {
        if (x >= pdu_nt_o.x1 + 40 && x <= pdu_nt_o.x2 - 40)
            pdu_pop_close();
        else
            ovl_nt_stripclick(x, y);
        return 1;
    }
    r = (y - pdu_nt_y0) / PDU_NT_LINE;
    if (y >= pdu_nt_y0 && r < pdu_nt_vis)
        ovl_nt_rowclick(r, x, y, 1);
    return 1;
}

/* ============================================================================
 * THE NOTE POPOVER (the CGA mockup; every layout): a bounded text field -
 * 13.1's 1,024-byte cap, the typed text kept when the store refuses - with
 * Resolve/Reopen, Delete and Done. A NEW note is not in the store until Done
 * has text in it, so an empty or cancelled one is never a change (PD-12).
 * Esc discards what was typed; Enter, Done and any other way out keep it.
 * A note that came with the file is listed, resolved and jumped to, but its
 * text is the source's (13.1's copy-on-edit is `edit`'s tool).
 * ==========================================================================*/

/* the three buttons: 0 Resolve/Reopen, 1 Delete, 2 Done */
static void ovl_nt_pbtn(int k, struct os88_rect *r)
{
    r->x1 = pdu_poprect.x1 + 8 + (k == 0 ? 0 : k == 1 ? 80 : 152);
    r->x2 = r->x1 + (k == 0 ? 72 : k == 1 ? 64 : 48) - 1;
    r->y1 = pdu_poprect.y1 + 68;
    r->y2 = r->y1 + 15;
}

/* the counter in the title and the last four lines of the text, one font_run
 * a line, so nothing is erased first. all = 0 (a key): only the line or two
 * the key touched (the character and the cursor), unless the text scrolled -
 * a keystroke is two font_runs on an XT, not five */
static void ovl_nt_ptext(int all)
{
    struct os88_rect *p;
    int L, first, k, i, pos, lo, hi;
    char *m;

    p = &pdu_poprect;
    m = pdu_line + 40;
    os88_utoa(pdu_nt_blen, m);
    pdu_cat(m, "/1024", 20);
    ovl_nt_pad(m, 9);
    os88_font_run(p->x2 - 7 - 9 * PDU_CW, p->y1 + 6, pdu_line, OS88_BLACK,
                  OS88_WHITE);
    L = (pdu_nt_blen + 32) / 32;
    first = L > 4 ? L - 4 : 0;
    lo = (pdu_nt_blen > 0 ? pdu_nt_blen - 1 : 0) / 32 - first;
    hi = (pdu_nt_blen + 1) / 32 - first;
    if (all || first != pdu_nt_pfirst) {
        lo = 0;
        hi = 3;
    }
    pdu_nt_pfirst = first;
    for (k = 0; k < 4; k++) {
        if (k < lo || k > hi)
            continue;
        pos = (first + k) * 32;
        for (i = 0; i < 32; i++)
            pdu_line[i] = pos + i < pdu_nt_blen ? pdu_nt_buf[pos + i]
                          : pos + i == pdu_nt_blen ? '_' : ' ';
        pdu_line[32] = 0;
        os88_font_run(p->x1 + 8, p->y1 + 19 + k * 12, pdu_line, OS88_BLACK,
                      OS88_WHITE);
    }
}

/* is the popover's note resolved? (the button says Reopen then) */
static int ovl_nt_pdone(void)
{
    ovl_nt_sync();
    if (pdu_nt_pid >= PDU_NT_SBASE)
        return ovl_nt_done(1, pdu_nt_so[pdu_nt_pid - PDU_NT_SBASE]);
    if (pdu_nt_pid >= 0 && pdu_ed_get(pdu_nt_pid, &pdu_nt_e) == 0)
        return ovl_nt_done(0, pdu_nt_e.id);
    return 0;
}

/* the popover's content, its frame already drawn */
static int ovl_nt_pnote(void)
{
    struct os88_rect *p;
    int k;
    p = &pdu_poprect;
    if (pdu_nt_pm) {
        pdu_tr.x1 = p->x1 + 8;
        pdu_tr.y1 = p->y1 + 4;
        return ovl_nt_ipaint(&pdu_tr, 12);
    }
    os88_font_run(p->x1 + 8, p->y1 + 6, pdu_nt_pid < 0 ? "New note " : "Note     ",
                  OS88_BLACK, OS88_WHITE);
    os88_set_color(OS88_BLACK);
    os88_gfx_hline(p->x1 + 2, p->x2 - 2, p->y1 + 15);
    ovl_nt_ptext(1);
    for (k = 0; k < 3; k++) {
        ovl_nt_pbtn(k, &pdu_nt_cr);
        ovl_nt_box(&pdu_nt_cr, OS88_WHITE, OS88_BLACK,
                   k == 0 ? (ovl_nt_pdone() ? "Reopen" : "Resolve")
                   : k == 1 ? "Delete" : "Done");
    }
    return 1;
}

/* a new note at rect r on page id: the Note tool's call */
static void ovl_nt_new(unsigned page, const struct pd_rect *r)
{
    pdu_nt_pm = 0;
    pdu_nt_pid = -1;
    pdu_nt_blen = 0;
    pdu_nt_buf[0] = 0;
    pdu_nt_ptxt = 0;
    pdu_nt_ppg = page;
    os88_memcpy(&pdu_nt_prc, r, sizeof(struct pd_rect));
    pdu_pop_open(P_NOTE, C_NONE);
}

/* an existing note of the store */
static void ovl_nt_open(int id)
{
    int n;
    if (id < 0 || id >= PDU_NT_SBASE || pdu_ed_get(id, &pdu_nt_e) != 0)
        return;
    pdu_nt_pm = 0;
    pdu_nt_pid = id;
    pdu_nt_ptxt = 0;
    n = pdu_ed_text_get(id, pdu_nt_buf, PDU_NT_MAXTXT + 1);
    if (n > PDU_NT_MAXTXT)
        n = PDU_NT_MAXTXT;
    pdu_nt_buf[n] = 0;
    pdu_nt_blen = n;
    os88_memcpy(&pdu_nt_prc, &pdu_nt_e.rect, sizeof(struct pd_rect));
    pdu_pop_open(P_NOTE, C_NONE);
}

/* keep what was typed: 0 done (or nothing to keep), -1 refused - and the
 * popover stays open with the text in it */
static int ovl_nt_commit(void)
{
    int r;
    struct pd_edit *e;
    if (pdu_nt_pm)
        return 0;
    e = &pdu_nt_e2;
    if (pdu_nt_pid < 0) {
        if (pdu_nt_blen == 0)
            return 0;
        os88_memset(e, 0, sizeof(*e));
        e->kind = PDA_NOTE;
        e->page = pdu_nt_ppg;
        e->colour = PDC_YELLOW;
        os88_memcpy(&e->rect, &pdu_nt_prc, sizeof(struct pd_rect));
        pdu_undo_begin(UL_NOTE);
        r = pdu_ed_new(e, pdu_nt_buf, pdu_nt_blen, 0, 0);
        pdu_undo_end();
        if (r >= 0) {
            pdu_nt_pid = r;
            ovl_nt_redraw(&pdu_nt_prc);
        }
    } else if (pdu_nt_ptxt) {
        pdu_undo_begin(UL_TYPE);
        r = pdu_ed_text(pdu_nt_pid, pdu_nt_buf, pdu_nt_blen);
        pdu_undo_end();
    } else
        return 0;
    if (r < 0) {
        pdu_tell("The edit store is full: save, then continue.");  /* pooled */
        return -1;
    }
    pdu_nt_ptxt = 0;
    pdu_nt_sel = pdu_nt_edsel = pdu_nt_pid;
    pdu_ed_select(pdu_nt_pid);
    return 0;
}

/* pdu_pop_close's first line: any way of dismissing the popover but Esc keeps
 * the typing; the list repaints on the next idle, once the popover is gone */
static void ovl_nt_pclose(void)
{
    if (!pdu_nt_nocommit)
        ovl_nt_commit();
    pdu_nt_nocommit = 0;
    pdu_nt_post = 1;
}

/* Resolve/Reopen, Delete, Done */
static void ovl_nt_pact(int k)
{
    int id;
    if (k == 0 && pdu_nt_pid < 0 && pdu_nt_blen == 0) {
        pdu_tell("Type the note first.");
        return;
    }
    if (k != 1 && ovl_nt_commit() < 0)
        return;
    id = pdu_nt_pid;
    if (k == 1 && id >= 0 && pdu_ed_get(id, &pdu_nt_e) == 0) {
        os88_memcpy(&pdu_nt_prc, &pdu_nt_e.rect, sizeof(struct pd_rect));
        pdu_undo_begin(UL_DELETE);
        pdu_ed_del(id);
        pdu_undo_end();
        pdu_ed_select(-1);
        pdu_nt_sel = pdu_nt_edsel = -1;
        ovl_nt_redraw(&pdu_nt_prc);
    }
    pdu_nt_nocommit = 1;
    pdu_pop_close();
    if (k == 0)
        ovl_nt_resolve(id);
}

/* every key while the note popover, the inspector or the list popover is up.
 * 1 = consumed. */
static int ovl_nt_key(int ascii, int scan)
{
    if (pdu_pop == P_NOTE && pdu_nt_pm == 0) {
        if (ascii == 27) {
            pdu_nt_nocommit = 1;
            pdu_pop_close();
        } else if (ascii == 13)
            ovl_nt_pact(2);
        else if (ascii == 8 && pdu_nt_blen > 0) {
            pdu_nt_buf[--pdu_nt_blen] = 0;
            pdu_nt_ptxt = 1;
            ovl_nt_ptext(0);
        } else if (ascii >= 32 && ascii < 127) {
            if (pdu_nt_blen >= PDU_NT_MAXTXT)
                pdu_tell("A note holds 1,024 characters.");
            else {
                pdu_nt_buf[pdu_nt_blen++] = ascii;
                pdu_nt_buf[pdu_nt_blen] = 0;
                pdu_nt_ptxt = 1;
                ovl_nt_ptext(0);
            }
        }
        return 1;
    }
    if (pdu_nt_insp || pdu_pop == P_NOTE)
        return ovl_nt_ikey(ascii, scan);
    if (ascii == 0 && (scan == 0x48 || scan == 0x50)) {   /* the list popover */
        pdu_nt_top = pdu_nt_top + (scan == 0x48 ? -1 : 1);
        if (pdu_nt_top < 0)
            pdu_nt_top = 0;
        ovl_nt_plistp();                /* geom clamps the bottom */
        return 1;
    }
    return 0;
}
