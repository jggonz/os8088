/* ============================================================================
 * os8088 - apps/pagedesk/pdusel.c   TEXT SELECTION AND COPY
 *                                   (PAGEDESK-SPEC 9.2, 12, 17.4; 20.10.3)
 *
 * A selection is a page and a RANGE OF GLYPH RUNS (lo..hi) - the running
 * index PDREND's text map gives every glyph (20.3.4). Everything else is
 * derived from the text on demand and nothing of a page's map is kept:
 *
 *   quads   one box a line, page space, in the SCRATCH claim (16 bytes each)
 *   pixels  XOR inversion over the quads on the glass (17.4: never saved)
 *   text    run again, only when it is wanted (Copy, ovl_sel_text)
 *
 * A text pass is the J_TEXT job: one PDV_REND_TEXTMAP slice a wake, 74 glyph
 * records a slice read out of the exchange block where PDREND put them. Four
 * modes share it - PICK (two points to two runs), WORD (grow a run to its
 * word), QUAD (the range to boxes) and COPY (the range to text) - because a
 * mode is only what is done with each glyph.
 *
 * THE GLASS: a selection is XORed on after the page under it is whole. A
 * render of a region wipes the XOR in that region, so the render job calls
 * pdu_sel_after() with the rect it finished and the hook XORs the selection
 * back in there: the invariant is "the glass shows the page with the
 * selection inverted in every region drawn since", and nothing is drawn
 * twice that was not changed (CLAUDE.md, PERFORMANCE.md rule 2).
 * ==========================================================================*/

#define TX_PICK 1
#define TX_WORD 2
#define TX_QUAD 3
#define TX_COPY 4

static int pdu_selpg = -1;              /* the page; -1 = no selection */
static unsigned pdu_sello, pdu_selhi;   /* glyph runs, inclusive */
static int pdu_selq;                    /* quads in scratch; -1 = owed */
static int pdu_selvis;                  /* the quads are XORed on the glass */
static int pdu_selshow;                 /* ...and owed to it when idle */
static int pdu_selgoto;                 /* a Find hit: bring it into view */
static int pdu_selhl;                   /* a gesture made with Highlight armed:
                                         * its boxes become edit's record */

/* the pass */
static struct pdxb_textmap pdu_xtm;
static struct pdtm_glyph pdu_g;
static int pdu_txm;                     /* TX_* */
static int pdu_txst;                    /* 0 = begin, 1 = streaming */
static int pdu_txwant;                  /* a pass owed: 0 or a TX_* */
static int pdu_txpg;                    /* the page it reads */
static pd_pt pdu_pax, pdu_pay, pdu_pfx, pdu_pfy;   /* PICK's two points */
static unsigned pdu_pas, pdu_pfs, pdu_par, pdu_pfr; /* best scores and runs */
static int pdu_pfirst;
static int pdu_pword;                   /* after PICK, grow to the word */
/* the previous glyph (the blank rule shared with PDENG's matcher) */
static int pdu_pvh;
static pd_pt pdu_pvx1, pdu_pvy0;
/* WORD */
static unsigned pdu_ws, pdu_wprev, pdu_wtgt;
static int pdu_wvalid, pdu_wfound, pdu_wbroke;
/* QUAD */
static int pdu_qopen;
static pd_pt pdu_qx0, pdu_qy0, pdu_qx1, pdu_qy1;
static int pdu_qn;
/* COPY */
static unsigned pdu_cpseg, pdu_cpoff, pdu_cpcap;
static int pdu_cpfirst, pdu_cpover, pdu_cpsync, pdu_cpclaim;
static unsigned pdu_cpseg0;

static unsigned pdu_scr(void) { return pdu_dcseg(PDU_DC_SCRATCH); }

static int pdu_sel_active(void) { return pdu_selpg >= 0; }

static int ovl_sel_quads(pd_pt *dst, unsigned cap)
{
    unsigned n;
    if (pdu_selpg != (int)pdu_page || pdu_selq <= 0 || !pdu_scr())
        return 0;
    n = pdu_selq;
    if (n > cap)
        n = cap;
    pd_fcopy_in(pdu_scr(), PDU_SC_QUADS, dst, n * 16);
    return n;
}

/* ---- the glass -------------------------------------------------------------- */
static struct os88_rect pdu_sqr;        /* one quad on screen */
static pd_px pdu_spx0, pdu_spy0, pdu_spx1, pdu_spy1;

/* quad q's screen rect, cut to c. 0 when nothing of it shows */
static int ovl_sel_rect(int q, struct os88_rect *c)
{
    unsigned s;
    pd_pt x0, y0, x1, y1;
    s = pdu_scr();
    x0 = pd_fpeekw(s, q * 16);              /* UL x, UL y, UR x, UR y, LL.. */
    y1 = pd_fpeekw(s, q * 16 + 2);
    x1 = pd_fpeekw(s, q * 16 + 4);
    y0 = pd_fpeekw(s, q * 16 + 10);         /* LL y */
    pdm_view_fwd(&pdu_spx0, &pdu_spy0, &pdu_view, x0, y0);
    pdm_view_fwd(&pdu_spx1, &pdu_spy1, &pdu_view, x1, y1);
    pdu_sqr.x1 = pdu_spx0 < pdu_spx1 ? pdu_spx0 : pdu_spx1;
    pdu_sqr.x2 = (pdu_spx0 < pdu_spx1 ? pdu_spx1 : pdu_spx0) - 1;
    pdu_sqr.y1 = pdu_spy0 < pdu_spy1 ? pdu_spy0 : pdu_spy1;
    pdu_sqr.y2 = (pdu_spy0 < pdu_spy1 ? pdu_spy1 : pdu_spy0) - 1;
    if (pdu_sqr.x2 < pdu_sqr.x1) pdu_sqr.x2 = pdu_sqr.x1;
    if (pdu_sqr.y2 < pdu_sqr.y1) pdu_sqr.y2 = pdu_sqr.y1;
    return pdu_isect(&pdu_sqr, c, &pdu_sqr);
}

/* XOR the selection's quads inside c. THE GFX LOCK MUST BE HELD: it is not
 * re-entrant (a byte with no owner), and this is reached from callbacks, which
 * hold it, and from the wake handler, which does not and takes it around the
 * call. */
static struct os88_rect pdu_sxc;
static void ovl_sel_xor(struct os88_rect *c)
{
    int q;
    if (pdu_selq <= 0 || !pdu_scr())
        return;
    if (!pdu_isect(c, &pdu_L.canvas, &pdu_sxc))
        return;
    if (os88_wm_clip_set(pdu_win) == 0)
        for (q = 0; q < pdu_selq; q++)
            if (ovl_sel_rect(q, &pdu_sxc))
                os88_gfx_xor_fill(pdu_sqr.x1, pdu_sqr.y1, pdu_sqr.x2, pdu_sqr.y2);
}

static int ovl_sel_busy(void)
{
    return pdu_job == J_RENDER || pdu_rwant || pdu_pop != P_NONE || pdu_about;
}

/* show the selection on the glass, or owe it until the page is whole */
static void ovl_sel_show(void)
{
    if (pdu_selq <= 0 || pdu_selvis || pdu_selpg != (int)pdu_page)
        return;
    if (ovl_sel_busy()) {
        pdu_selshow = 1;                /* no kick: what makes it busy ends in a
                                         * render, and the render's wake comes */
        return;
    }
    pdu_selshow = 0;
    pdu_selvis = 1;
    os88_gfx_lock();
    ovl_sel_xor(&pdu_L.canvas);
    os88_gfx_unlock();
}

/* a render of r finished: put the selection back inside it (called with the
 * lock NOT held, from the wake handler) */
static void pdu_sel_after(struct os88_rect *r)
{
    if (pdu_selvis && pdu_selpg == (int)pdu_page) {
        os88_gfx_lock();
        ovl_sel_xor(r);
        os88_gfx_unlock();
    }
}

/* the names `edit` calls (COMMON.md's interface, pdu.h's edit block); the
 * work is the overlay's. `cap` counts QUADS for pdu_sel_quads (20.10.3) */
int pdu_sel_quads(pd_pt *dst, unsigned cap) { return ovl_sel_quads(dst, cap); }
int pdu_sel_text(char *dst, unsigned cap) { return ovl_sel_text(dst, cap); }

/* take the selection off the glass. A page being drawn is repainted instead
 * of XORed: its pixels are not whole to invert back */
static void ovl_sel_hide(void)
{
    int q;
    if (!pdu_selvis)
        return;
    pdu_selvis = 0;
    if (ovl_sel_busy()) {
        for (q = 0; q < pdu_selq; q++)
            if (ovl_sel_rect(q, &pdu_L.canvas))
                pdu_render_want(&pdu_sqr);
        return;
    }
    pdu_selvis = 1;                     /* the XOR below needs it to draw */
    ovl_sel_xor(&pdu_L.canvas);
    pdu_selvis = 0;
}

static void ovl_sel_clear(void)
{
    if (pdu_selpg < 0)
        return;
    ovl_sel_hide();
    pdu_selpg = -1;
    pdu_selq = 0;
    pdu_selshow = 0;
    pdu_menusync();
}

/* pdu_job_abort stopped a nav job (Esc, a press on the page, a close). A
 * QUAD pass's boxes will never come; a COPY pass gives back its claim, and
 * one that wrote into the THUMB claim has spoiled the pictures in it - the
 * first build did neither, so an Esc during a long Copy kept the claim until
 * the window closed and left the Pages panel blitting text bytes. */
static void pdu_tx_aborted(void)
{
    if (pdu_job != J_TEXT)
        return;
    if (pdu_txm == TX_QUAD || pdu_txm == TX_PICK || pdu_txm == TX_WORD) {
        pdu_selhl = 0;
        if (pdu_txm == TX_QUAD) {
            pdu_selpg = -1;
            pdu_selq = 0;
        }
    } else if (pdu_txm == TX_COPY && !pdu_cpsync) {
        if (pdu_cpclaim) {
            os88_mem_free(pdu_cpseg0);
            pdu_cpclaim = 0;
        } else {
            pdu_th_reset();
            pdu_thwant = 1;
        }
    }
}

/* the page is changing: the glass is about to be repainted whole */
static void pdu_sel_drop(void)
{
    pdu_selhl = 0;
    pdu_selpg = -1;
    pdu_selq = 0;
    pdu_selvis = 0;
    pdu_selshow = 0;
    pdu_txwant = 0;
}

/* ---- the text pass --------------------------------------------------------------- */
/* 0 none, 1 a blank between the previous glyph and this, 2 a new line.
 * The same rule PDENG's matcher uses (pdesrch.c): a line apart, or a fifth
 * of an em of gap. */
static int ovl_gapk(struct pdtm_glyph *g)
{
    unsigned dy, dx;
    if (pdu_pvh == 0)
        return 0;
    dy = g->y0 > pdu_pvy0 ? g->y0 - pdu_pvy0 : pdu_pvy0 - g->y0;
    dx = g->x0 > pdu_pvx1 ? g->x0 - pdu_pvx1 : 0;
    if (dy > (unsigned)pdu_pvh / 2)
        return 2;
    return dx > (unsigned)pdu_pvh / 5 ? 1 : 0;
}

static void ovl_prev(struct pdtm_glyph *g)
{
    pdu_pvx1 = g->x1;
    pdu_pvy0 = g->y0;
    pdu_pvh = g->y1 - g->y0;
    if (pdu_pvh < 0)
        pdu_pvh = -pdu_pvh;
}

/* distance of a point to a glyph box, as one score: lines weigh four times
 * a column, so the nearest LINE wins before the nearest letter */
static unsigned ovl_score(pd_pt px, pd_pt py, struct pdtm_glyph *g)
{
    unsigned dx, dy;
    dx = px < g->x0 ? g->x0 - px : (px > g->x1 ? px - g->x1 : 0);
    dy = py < g->y0 ? g->y0 - py : (py > g->y1 ? py - g->y1 : 0);
    if (dx > 8000) dx = 8000;
    if (dy > 8000) dy = 8000;
    return dy * 4 + dx;
}

static void ovl_quad_flush(void)
{
    static pd_pt q[8];
    if (!pdu_qopen)
        return;
    pdu_qopen = 0;
    if (pdu_qn >= PDU_SC_QMAX)
        return;
    q[0] = pdu_qx0; q[1] = pdu_qy1;     /* UL UR LL LR (13.1.1) */
    q[2] = pdu_qx1; q[3] = pdu_qy1;
    q[4] = pdu_qx0; q[5] = pdu_qy0;
    q[6] = pdu_qx1; q[7] = pdu_qy0;
    pd_fcopy_out(q, pdu_scr(), PDU_SC_QUADS + pdu_qn * 16, 16);
    pdu_qn++;
}

static void ovl_cp_put(int c)
{
    if (pdu_cpoff >= pdu_cpcap) {
        pdu_cpover = 1;
        return;
    }
    os88_poke(pdu_cpseg, pdu_cpoff, c);
    pdu_cpoff++;
}

/* one glyph. 1 = stop the pass (the answer is complete) */
static int ovl_tx_glyph(struct pdtm_glyph *g)
{
    int gap, blank, stop, d;
    unsigned s;

    stop = 0;
    gap = ovl_gapk(g);
    blank = (g->code == ' ' || g->code == 0xA0);
    switch (pdu_txm) {
    case TX_PICK:
        s = ovl_score(pdu_pax, pdu_pay, g);
        if (pdu_pfirst || s < pdu_pas) { pdu_pas = s; pdu_par = g->run; }
        s = ovl_score(pdu_pfx, pdu_pfy, g);
        if (pdu_pfirst || s < pdu_pfs) { pdu_pfs = s; pdu_pfr = g->run; }
        pdu_pfirst = 0;
        break;
    case TX_WORD:
        if ((blank || gap) && pdu_wfound) {
            pdu_selhi = pdu_wprev;
            pdu_wbroke = 1;
            stop = 1;
            break;
        }
        if (blank || gap)
            pdu_wvalid = 0;
        if (!blank) {
            if (!pdu_wvalid) {
                pdu_ws = g->run;
                pdu_wvalid = 1;
            }
            if (g->run == pdu_wtgt) {
                pdu_wfound = 1;
                pdu_sello = pdu_ws;
            }
            pdu_wprev = g->run;
        }
        break;
    case TX_QUAD:
        if (g->run > pdu_selhi) {
            stop = 1;
            break;
        }
        if (g->run < pdu_sello)
            break;
        d = g->y0 - pdu_qy0;
        if (d < 0) d = -d;
        if (pdu_qopen && d <= (pdu_qy1 - pdu_qy0) / 2) {
            if (g->x0 < pdu_qx0) pdu_qx0 = g->x0;
            if (g->x1 > pdu_qx1) pdu_qx1 = g->x1;
            if (g->y0 < pdu_qy0) pdu_qy0 = g->y0;
            if (g->y1 > pdu_qy1) pdu_qy1 = g->y1;
        } else {
            ovl_quad_flush();
            pdu_qopen = 1;
            pdu_qx0 = g->x0; pdu_qx1 = g->x1;
            pdu_qy0 = g->y0; pdu_qy1 = g->y1;
        }
        break;
    case TX_COPY:
        if (g->run > pdu_selhi) {
            stop = 1;
            break;
        }
        if (g->run < pdu_sello)
            break;
        if (!pdu_cpfirst && !blank) {
            if (gap == 2) {
                ovl_cp_put(13);
                ovl_cp_put(10);
            } else if (gap == 1)
                ovl_cp_put(' ');
        }
        pdu_cpfirst = 0;
        ovl_cp_put(g->code < 32 ? ' ' : g->code);
        break;
    }
    ovl_prev(g);
    return stop;
}

/* a pass starts: the state its mode needs */
static void ovl_txp_begin(int mode)
{
    pdu_txm = mode;
    pdu_txst = 0;
    pdu_pvh = 0;
    pdu_pfirst = 1;
    pdu_wvalid = 0;
    pdu_wfound = 0;
    pdu_wbroke = 0;
    pdu_wprev = 0;
    pdu_qopen = 0;
    pdu_qn = 0;
    pdu_cpfirst = 1;
    pdu_cpover = 0;
    if (pdu_job != J_TEXT)
        pdu_job_start(J_TEXT);
}

/* the pass is over: what its mode owed */
static void ovl_sel_set(int pg, unsigned lo, unsigned hi)
{
    pdu_selpg = pg;
    pdu_sello = lo < hi ? lo : hi;
    pdu_selhi = lo < hi ? hi : lo;
    pdu_selq = -1;
    pdu_selvis = 0;
}

/* a Find hit or a finished PICK/WORD: compute the boxes */
static void ovl_sel_quads_owed(void)
{
    pdu_txpg = pdu_selpg;
    ovl_txp_begin(TX_QUAD);
}

static void ovl_cp_finish(void)
{
    unsigned n;
    n = pdu_cpoff;
    pdu_job = J_NONE;
    if (pdu_cpsync)
        return;
    if (pdu_cpover && pdu_cpclaim) {
        pdu_tell("The selection is over 32 KB of text: it was not copied.");
        n = 0;
        pdu_cpover = 0;
    } else if (n == 0) {
        pdu_tell("There is no text in the selection.");
    } else if (os88_clip_put_seg(pdu_cpseg, 0, n) != 0) {
        pdu_tell("The clipboard refused the copy.");
    } else {
        os88_utoa(n, pdu_num);
        os88_strcpy(pdu_line, "Copied ", sizeof(pdu_line));
        pdu_cat(pdu_line, pdu_num, sizeof(pdu_line));
        pdu_cat(pdu_line, pdu_cpover ? " characters - the rest did not fit." : " characters.",
                sizeof(pdu_line));
        pdu_wsay(pdu_line);
    }
    if (pdu_cpclaim) {
        os88_mem_free(pdu_cpseg0);
        pdu_cpclaim = 0;
    } else {
        pdu_th_reset();                 /* the claim held pictures or outline */
        pdu_thwant = 1;
        pdu_kick();
    }
}

static void ovl_tx_done(void)
{
    switch (pdu_txm) {
    case TX_PICK:
        if (pdu_pfirst) {
            pdu_job = J_NONE;
            pdu_wsay("There is no text on this page to select.");
            return;
        }
        ovl_sel_set(pdu_txpg, pdu_par, pdu_pfr);
        if (pdu_pword) {
            pdu_pword = 0;
            pdu_wtgt = pdu_par;
            pdu_txpg = pdu_selpg;
            ovl_txp_begin(TX_WORD);
            return;
        }
        ovl_sel_quads_owed();
        return;
    case TX_WORD:
        if (!pdu_wfound)
            pdu_sello = pdu_selhi = pdu_wtgt;
        else if (!pdu_wbroke)
            pdu_selhi = pdu_wprev;
        ovl_sel_quads_owed();
        return;
    case TX_QUAD:
        ovl_quad_flush();
        pdu_selq = pdu_qn;
        pdu_job = J_NONE;
        pdu_menusync();
        if (pdu_selgoto) {
            pdu_selgoto = 0;
            ovl_sel_view();
        }
        ovl_sel_show();
        if (pdu_selq == 0) {
            pdu_selpg = -1;
            pdu_selhl = 0;
            pdu_wsay("There is no text there.");
        } else if (pdu_selhl) {         /* 20.8: Highlight from a selection */
            pdu_selhl = 0;
            os88_gfx_lock();
            if (pdu_tool == C_T_HILITE && pdu_tool_hilite())
                ovl_sel_clear();        /* the record is drawn in its place */
            os88_gfx_unlock();
        }
        return;
    case TX_COPY:
        ovl_cp_finish();
        return;
    }
    pdu_job = J_NONE;
}

/* one slice of the pass: one TEXTMAP call, its records handled in order */
static void ovl_tx_step(void)
{
    unsigned r;
    int i, n, stop;

    if (pdu_txst == 0) {
        os88_memset(&pdu_xtm, 0, sizeof(pdu_xtm));
        pdu_xtm.page = pdu_txpg;
        pdu_xtm.dseg = pdu_xb;
        pdu_xtm.doff = 128;
        pdu_xtm.dmax = 896;
        pdu_txst = 1;
    }
    pd_fcopy_out(&pdu_xtm, pdu_xb, 0, sizeof(pdu_xtm));
    r = pdu_mcall(PDU_M_REND, PDV_REND_TEXTMAP, 0, 0, 0);
    pdu_jslices++;
    if (pdu_mcf) {
        pdu_job = J_NONE;
        if (pdu_txm == TX_QUAD) {
            pdu_selpg = -1;
            pdu_selq = 0;
        }
        pdu_wsay(pdu_errtext(r));
        return;
    }
    pd_fcopy_in(pdu_xb, 0, &pdu_xtm, sizeof(pdu_xtm));
    n = pdu_xtm.count;
    stop = 0;
    for (i = 0; i < n && !stop; i++) {
        pd_fcopy_in(pdu_xb, 128 + i * 12, &pdu_g, 12);
        stop = ovl_tx_glyph(&pdu_g);
    }
    if (stop || pdu_xtm.done) {
        if (!pdu_xtm.done)
            pdu_mcall(PDU_M_REND, PDV_REND_ABORT, 0, 0, 0);
        ovl_tx_done();
        return;
    }
    pdu_progress("Reading the text", "");
}

/* ---- gestures ------------------------------------------------------------------ */
/* a pass from two screen points. word = 1 grows the pick to its word. */
static void ovl_sel_points(int x0, int y0, int x1, int y1, int word)
{
    pdm_view_inv(&pdu_pax, &pdu_pay, &pdu_view, x0, y0);
    pdm_view_inv(&pdu_pfx, &pdu_pfy, &pdu_view, x1, y1);
    pdu_pword = word;
    pdu_selhl = (pdu_tool == C_T_HILITE);
    pdu_txpg = pdu_page;
    pdu_txwant = TX_PICK;
    pdu_kick();
}

/* Select all: the whole page's text */
static void ovl_selall(void)
{
    if (pdu_doc != D_OPEN)
        return;
    ovl_sel_clear();
    ovl_sel_set(pdu_page, 0, 0xFFFF);
    pdu_txwant = TX_QUAD;
    pdu_txpg = pdu_page;
    pdu_kick();
}

/* the pass owed, started from the idle ladder (ovl_nav_work) */
static void ovl_txwant_start(void)
{
    int m;
    m = pdu_txwant;
    pdu_txwant = 0;
    pdu_txpg = (m == TX_QUAD || m == TX_COPY) ? pdu_selpg : pdu_page;
    ovl_txp_begin(m);
}

/* a Find hit: scroll the first box into view, a third of the way down */
static struct os88_rect pdu_sbig = { -30000, -30000, 30000, 30000 };
static void ovl_sel_view(void)
{
    int nx, ny;
    struct os88_rect *c;
    if (pdu_selq <= 0 || !ovl_sel_rect(0, &pdu_sbig))
        return;
    c = &pdu_L.canvas;
    nx = pdu_sx;
    ny = pdu_sy;
    if (pdu_sqr.y1 < c->y1 || pdu_sqr.y2 > c->y2)
        ny = pdu_sy + (pdu_sqr.y1 - c->y1) - pdu_ch() / 3;
    if (pdu_sqr.x1 < c->x1 || pdu_sqr.x2 > c->x2)
        nx = pdu_sx + (pdu_sqr.x1 - c->x1) - pdu_cw() / 3;
    if (nx == pdu_sx && ny == pdu_sy)
        return;
    os88_gfx_lock();
    pdu_scroll_to(nx, ny);
    os88_gfx_unlock();
}

/* ---- Copy ----------------------------------------------------------------------- */
/* Ctrl+C / Edit > Copy: the selected text to the OS clipboard (12), CR LF
 * between lines, a pass of its own (the text is not kept). The buffer is a
 * claim of the run's length times three, given back at once, or - when no
 * claim can be had - the THUMB claim's 4 KiB, whose pictures then come back
 * lazily. */
static void ovl_copy(void)
{
    unsigned want, cap;
    if (pdu_selpg < 0 || pdu_doc != D_OPEN) {
        pdu_tell("Nothing is selected.");
        return;
    }
    if (pdu_job != J_NONE && pdu_job != J_THUMB) {      /* a picture yields */
        pdu_tell("Wait for the current job, or press Esc.");
        return;
    }
    want = pdu_selhi - pdu_sello + 1;
    if (pdu_selhi == 0xFFFF || want > 10900)
        want = 10900;
    pdu_cpclaim = 0;
    pdu_cpseg0 = 0;
    pdu_cpsync = 0;
    cap = (want << 1) + want + 16;
    /* a claim FIRST, whatever the size: the THUMB claim is the fallback only,
     * because borrowing it costs every picture in the Pages panel a render
     * again (the first build borrowed it for every Copy under 1,360
     * characters) */
    pdu_cpseg0 = os88_mem_claim_p((cap + 15) >> 4);
    if (pdu_cpseg0) {
        pdu_cpclaim = 1;
        pdu_cpseg = pdu_cpseg0;
        pdu_cpcap = cap > 32768u ? 32768u : cap;
    } else {
        /* the THUMB claim, 4 KiB: the pictures (or the Outline) it held are
         * given up and come back lazily. NOT the SCRATCH claim - the
         * selection's boxes live at its offset 0 */
        pdu_cpseg = pdu_dcseg(PDU_DC_THUMB);
        pdu_cpcap = 4096;
    }
    pdu_cpoff = 0;
    pdu_txwant = TX_COPY;
    pdu_kick();
}

/* for `edit` (20.8): the selection's text, now. Runs the pass to its end on
 * this call - slow on a dense page, so never from a callback that holds the
 * lock - into dst. Returns the characters written, 0 for none. */
static int ovl_sel_text(char *dst, unsigned cap)
{
    unsigned n;
    if (pdu_selpg < 0 || pdu_job != J_NONE || cap < 2)
        return 0;
    pdu_cpclaim = 0;
    pdu_cpseg = pd_ds();
    pdu_cpoff = (unsigned)dst;
    pdu_cpcap = (unsigned)dst + cap - 1;
    pdu_cpsync = 1;
    pdu_txpg = pdu_selpg;
    ovl_txp_begin(TX_COPY);
    while (pdu_job == J_TEXT)
        ovl_tx_step();
    n = pdu_cpoff - (unsigned)dst;
    dst[n] = 0;
    return n;
}
