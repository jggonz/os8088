/* ============================================================================
 * os8088 - apps/pagedesk/pdufind.c   FIND, A JOB (PAGEDESK-SPEC 9.3, 15.1;
 *                                    DECISIONS D14c; 20.10.1)
 *
 * Find is the J_FIND job, one slice a wake: PDREND's text map (9.2) gives a
 * page's glyphs 74 at a time, written straight into the exchange block where
 * PDENG's matcher (pdesrch.c) reads them, so a piece is copied once. A hit is
 * a page and two glyph runs; it becomes the SELECTION (pdusel.c), which is
 * how it is shown, scrolled to and copied.
 *
 *   Enter            from the top of this page
 *   F3 / Find next   after the current selection, wrapping past the last page
 *   Shift+F3         the last hit before the current selection, wrapping
 *
 * It is cancellable at every slice (Esc), reports where it is after a second
 * (15.3), and a page is searched from its start every time - a page's map is
 * never kept (9.2).
 * ==========================================================================*/

static struct pdxb_search pdu_xfind;
static int pdu_fwant;                   /* 1 fresh, 2 next, 3 previous */
static int pdu_fdir;
static int pdu_fpg, pdu_fcnt, pdu_flim;
static int pdu_fst;                     /* 0 begin a page, 1 streaming it */
static int pdu_fnew;                    /* the next SEARCH call starts a page */
static unsigned pdu_frun0, pdu_fbefore;
static int pdu_fcand, pdu_fwrap;
static unsigned pdu_fclo, pdu_fchi;

/* the Find popover's Enter and F3 / Shift+F3 */
static void pdu_find_run(int again)
{
    if (pdu_doc != D_OPEN || pdu_nlen == 0)
        return;
    pdu_fwant = again ? 2 : 1;
    pdu_kick();
}

static void ovl_find_prev(void)
{
    if (pdu_doc != D_OPEN || pdu_nlen == 0)
        return;
    pdu_fwant = 3;
    pdu_kick();
}

/* the idle ladder starts it (ovl_nav_work) */
static void ovl_find_begin(void)
{
    int w;
    w = pdu_fwant;
    pdu_fwant = 0;
    pdu_fdir = (w == 3) ? -1 : 1;
    pdu_fpg = pdu_page;
    pdu_frun0 = 0;
    pdu_fbefore = 0xFFFF;
    if (w != 1 && pdu_selpg >= 0) {
        pdu_fpg = pdu_selpg;
        if (w == 2)
            pdu_frun0 = pdu_selhi + 1;
        else
            pdu_fbefore = pdu_sello;
    }
    pdu_fcnt = 0;
    pdu_fwrap = 0;
    pdu_fst = 0;
    pdu_flim = pdu_npages;
    if (pdu_frun0 || pdu_fbefore != 0xFFFF)
        pdu_flim++;                     /* the start page again, for its other half */
    pdu_job_start(J_FIND);
}

static void ovl_find_hit(unsigned lo, unsigned hi)
{
    pdu_job = J_NONE;
    os88_gfx_lock();
    ovl_sel_hide();                     /* the hit before this one */
    os88_gfx_unlock();
    ovl_sel_set(pdu_fpg, lo, hi);
    pdu_selgoto = 1;
    os88_strcpy(pdu_line, "Found on page ", sizeof(pdu_line));
    os88_utoa(pdu_fpg + 1, pdu_num);
    pdu_cat(pdu_line, pdu_num, sizeof(pdu_line));
    if (pdu_fwrap)
        pdu_cat(pdu_line, " (wrapped)", sizeof(pdu_line));
    os88_strcpy(pdu_selmsg, pdu_line, sizeof(pdu_selmsg));  /* the boxes' pass
                                         * may put its progress over it */
    pdu_wsay(pdu_line);
    if (pdu_fpg != (int)pdu_page) {
        pdu_hjump = 1;
        pdu_goto(pdu_fpg);              /* the quads follow the page load */
    }
    pdu_kick();
}

static void ovl_find_miss(void)
{
    pdu_job = J_NONE;
    os88_strcpy(pdu_line, "No match for \"", sizeof(pdu_line));
    pdu_cat(pdu_line, pdu_needle, sizeof(pdu_line));
    pdu_cat(pdu_line, "\"", sizeof(pdu_line));
    pdu_wsay(pdu_line);
}

/* the page is over: the next one, or the answer */
static void ovl_find_next_page(void)
{
    pdu_fst = 0;
    if (++pdu_fcnt >= pdu_flim) {
        ovl_find_miss();
        return;
    }
    pdu_frun0 = 0;
    pdu_fbefore = 0xFFFF;
    if (pdu_fdir > 0) {
        if (++pdu_fpg >= (int)pdu_npages) {
            pdu_fpg = 0;
            pdu_fwrap = 1;
        }
    } else {
        if (--pdu_fpg < 0) {
            pdu_fpg = pdu_npages - 1;
            pdu_fwrap = 1;
        }
    }
}

/* one slice: a TEXTMAP piece, then SEARCH over it until it has no more hits */
static void ovl_find_step(void)
{
    unsigned r, glyph;
    int n, fin;

    fin = 0;
    if (pdu_fst == 0) {
        os88_memset(&pdu_xtm, 0, sizeof(pdu_xtm));
        pdu_xtm.page = pdu_fpg;
        pdu_xtm.dseg = pdu_xb;
        pdu_xtm.doff = 128;
        pdu_xtm.dmax = 896;
        pdu_fst = 1;
        pdu_fnew = 1;
        pdu_fcand = 0;
    }
    pd_fcopy_out(&pdu_xtm, pdu_xb, 0, sizeof(pdu_xtm));
    r = pdu_mcall(PDU_M_REND, PDV_REND_TEXTMAP, 0, 0, 0);
    pdu_jslices++;
    if (pdu_mcf) {
        pdu_job = J_NONE;
        pdu_wsay(pdu_errtext(r));
        return;
    }
    pd_fcopy_in(pdu_xb, 0, &pdu_xtm, sizeof(pdu_xtm));
    n = pdu_xtm.count;
    glyph = pdu_frun0;
    while (n > 0) {
        os88_memset(&pdu_xfind, 0, sizeof(pdu_xfind));
        os88_strcpy(pdu_xfind.needle, pdu_needle, sizeof(pdu_xfind.needle));
        pdu_xfind.page = pdu_fpg;
        pdu_xfind.glyph = glyph;
        pdu_xfind.state = !pdu_fnew;
        pdu_xfind.found = n;
        pd_fcopy_out(&pdu_xfind, pdu_xb, 0, sizeof(pdu_xfind));
        r = pdu_mcall(PDU_M_ENG, PDV_ENG_SEARCH, 0, 0, 0);
        pdu_fnew = 0;
        if (pdu_mcf) {
            pdu_mcall(PDU_M_REND, PDV_REND_ABORT, 0, 0, 0);
            pdu_job = J_NONE;
            pdu_wsay(r == PDE_RANGE ? "Type something to find." : pdu_errtext(r));
            return;
        }
        pd_fcopy_in(pdu_xb, 0, &pdu_xfind, sizeof(pdu_xfind));
        if (!pdu_xfind.found)
            break;
        if (pdu_fdir > 0) {
            if (!pdu_xtm.done)
                pdu_mcall(PDU_M_REND, PDV_REND_ABORT, 0, 0, 0);
            ovl_find_hit(pdu_xfind.glyph, pdu_xfind.glyph_end);
            return;
        }
        /* backwards: the last hit that starts before the limit */
        if (pdu_xfind.glyph >= pdu_fbefore) {
            fin = 1;                    /* nothing later can count */
            break;
        }
        pdu_fcand = 1;
        pdu_fclo = pdu_xfind.glyph;
        pdu_fchi = pdu_xfind.glyph_end;
        glyph = pdu_xfind.glyph_end + 1;
    }
    if (pdu_xtm.done || fin) {
        if (!pdu_xtm.done)
            pdu_mcall(PDU_M_REND, PDV_REND_ABORT, 0, 0, 0);
        if (pdu_fcand)
            ovl_find_hit(pdu_fclo, pdu_fchi);
        else
            ovl_find_next_page();
        return;
    }
    os88_utoa(pdu_fpg + 1, pdu_num);
    pdu_progress("Searching page ", pdu_num);
}
