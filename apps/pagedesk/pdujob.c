/* ============================================================================
 * os8088 - apps/pagedesk/pdujob.c   THE JOB SCHEDULER (PAGEDESK-SPEC 15,
 *                                   20.6.5)
 *
 * No worker (15.1): every long operation is a JOB, a resumable state machine
 * stepped by os88_onwake() - the UI task, the gfx lock NOT held - one bounded
 * slice per wake, each slice re-posting the wake. Mouse and keyboard events
 * queue between wakes, so Escape and clicks are seen at every slice boundary,
 * and a cancelled job gives back what it held and leaves the document as it
 * was (15.3).
 *
 * ONE job runs at a time. What can be OWED while it runs:
 *   pdu_pgwant   a page to go to        (PAGEINFO, one verb, between jobs)
 *   pdu_rwant    a rect of page to draw (a render job, when nothing runs)
 *   pdu_argp     the launch document    (an open job)
 *
 * The jobs:
 *   J_OPEN    PDV_ENG_OPEN until it stops answering PDE_BUSY; then page 0,
 *             the outline titles, the fit, a full repaint and a render.
 *   J_RENDER  PDV_REND_BEGIN once, PDV_REND_STEP a band a wake (the module
 *             takes the gfx lock per band itself, 15.2), PDV_REND_STATUS at
 *             the end for the Incomplete-rendering line (10.6).
 *   J_SAVE    PDWRT fetched and bound, PDV_WRT_SAVE until done; the writer's
 *             byte total goes to OSAPI_FS_OP (one bar for the whole save).
 *
 * The exchange block is NOT assumed to survive between two calls (PDREND
 * calls PDENG through it): each job keeps its struct here and copies it out
 * before every slice and back after.
 * ==========================================================================*/

static struct pdxb_render pdu_xrend;
static struct pdxb_rstatus pdu_xrst;
static struct pdxb_save pdu_xsave;

/* the launch document (SPEC.md 54.10), opened from the first wake */
static char pdu_argname[16];
static struct os88_place pdu_argplace;
static int pdu_argp;

#define PDU_PROGRESS_TICKS 18           /* 15.3: progress after a second */

static void pdu_kick(void)
{
    os88_wm_wake(pdu_win);
}

/* stand in the document's folder before a module touches it. PDENG reads
 * pdu_name, and PDWRT the source, by NAME in the instance's current folder
 * (pdxb_open, 20.5.12 item 2), and a Standard File dialog moves that folder
 * (SPEC.md 19.2.1) - a Save As browsed elsewhere, or an Open cancelled after
 * browsing, would otherwise send every later page read to the wrong place.
 * Inside one volume this is a word and no I/O (os88_file_goto_q_mark). */
static void pdu_at_doc(void)
{
    if (pdu_doc == D_OPEN || pdu_doc == D_OPENING)
        os88_file_goto_q_mark(pdu_docplace.clus, pdu_docplace.vol);
}

/* union r into the rect a render is owed for */
static void pdu_render_want(struct os88_rect *r)
{
    if (!pdu_rwant) {
        os88_memcpy(&pdu_rr, r, sizeof(pdu_rr));
        pdu_rwant = 1;
    } else {
        if (r->x1 < pdu_rr.x1) pdu_rr.x1 = r->x1;
        if (r->y1 < pdu_rr.y1) pdu_rr.y1 = r->y1;
        if (r->x2 > pdu_rr.x2) pdu_rr.x2 = r->x2;
        if (r->y2 > pdu_rr.y2) pdu_rr.y2 = r->y2;
    }
    pdu_kick();
}

static void pdu_fsop_end(void)
{
    if (pdu_fsop) {
        os88_fs_op(0, 0);
        pdu_fsop = 0;
    }
}

/* a running render is abandoned and its rect owed again (the next wake
 * restarts it - after a popover closes, after a scroll has shifted it) */
static void pdu_job_abort_render(void)
{
    if (pdu_job != J_RENDER)
        return;
    pdu_mcall(PDU_M_REND, PDV_REND_ABORT, 0, 0, 0);
    pdu_job = J_NONE;
    pdu_render_want(&pdu_rrun);
}

/* the canvas pixels moved by dy: so did what is owed (pduview.c) */
static void pdu_job_shift(int dy)
{
    int had;
    had = (pdu_job == J_RENDER);
    pdu_job_abort_render();
    if (pdu_rwant || had) {
        pdu_rr.y1 = pdu_rr.y1 + dy;
        pdu_rr.y2 = pdu_rr.y2 + dy;
        if (pdu_rr.y1 < pdu_L.canvas.y1) pdu_rr.y1 = pdu_L.canvas.y1;
        if (pdu_rr.y2 > pdu_L.canvas.y2) pdu_rr.y2 = pdu_L.canvas.y2;
        if (pdu_rr.y1 > pdu_rr.y2)
            pdu_rwant = 0;
    }
}

/* Escape and every way out: stop what runs, release what it held */
static void pdu_job_abort(void)
{
    if (pdu_job == J_RENDER) {
        pdu_mcall(PDU_M_REND, PDV_REND_ABORT, 0, 0, 0);
    } else if (pdu_job == J_OPEN) {
        pdu_doc_release();
        pdu_doc = D_EMPTY;
    } else if (pdu_job == J_SAVE) {
        pdu_mcall(PDU_M_WRT, PDV_WRT_ABORT, 0, 0, 0);
        pdu_wrt_out();
    } else if (pdu_job >= J_FIND) {     /* nav: find, text pass, thumbnail */
        pdu_mcall(PDU_M_REND, PDV_REND_ABORT, 0, 0, 0);
        pdu_tx_aborted();               /* nav: what the pass held goes back */
    }
    pdu_fsop_end();
    pdu_job = J_NONE;
    pdu_jcancel = 0;
}

static void pdu_job_start(int kind)
{
    pdu_job = kind;
    pdu_jcancel = 0;
    pdu_jslices = 0;
    pdu_jt0 = os88_ticks();
    pdu_kick();
}

/* the status line, from the wake handler: take the lock for one font_run */
static void pdu_wsay(const char *s)
{
    pdu_say(s);
    os88_gfx_lock();
    pdu_status_paint();
    os88_gfx_unlock();
}

/* 15.3: a job past a second shows that it is alive and how to stop it */
static void pdu_progress(const char *verb, const char *what)
{
    if ((unsigned)(os88_ticks() - pdu_jt0) < PDU_PROGRESS_TICKS)
        return;
    if (pdu_jslices & 3)
        return;                         /* a status repaint every 4 slices */
    os88_strcpy(pdu_line, verb, sizeof(pdu_line));
    pdu_cat(pdu_line, what, sizeof(pdu_line));
    pdu_cat(pdu_line, " ", sizeof(pdu_line));
    os88_utoa(pdu_jslices, pdu_num);
    pdu_cat(pdu_line, pdu_num, sizeof(pdu_line));
    pdu_cat(pdu_line, " - Esc cancels", sizeof(pdu_line));
    pdu_wsay(pdu_line);
}

/* ---------------------------------------------------------------------------
 * OPEN
 * -------------------------------------------------------------------------*/
static void pdu_title_set(void)
{
    if (pdu_doc == D_OPEN) {
        os88_strcpy(pdu_title, "PageDesk - ", sizeof(pdu_title));
        pdu_cat(pdu_title, pdu_name, sizeof(pdu_title));
        if (pdu_dirty())                /* edit: the save point, not a count */
            pdu_cat(pdu_title, " *", sizeof(pdu_title));
        if (pdu_rdonly)
            pdu_cat(pdu_title, " (read-only)", sizeof(pdu_title));
    } else
        os88_strcpy(pdu_title, "PageDesk", sizeof(pdu_title));
    os88_wm_title(pdu_win, pdu_title);
}

/* begin opening `name` in the folder this instance stands in. Lock held or
 * not - it draws nothing itself, it owes a repaint. */
static int pdu_dirty_all;               /* the wake repaints everything */
static int pdu_opfirst;                 /* the open's first slice is ours */
static void pdu_open_begin(const char *name)
{
    pdu_job_abort();
    if (pdu_doc == D_OPEN || pdu_doc == D_OPENING)
        pdu_doc_release();
    pdu_rwant = 0;
    pdu_pgwant = 0;
    pdu_changes = 0;
    pdu_nt_reset();                     /* notes */
    pdu_inc = 0;
    pdu_nav_reset();                    /* nav: history, selection, panels */
    pdu_rdonly = 0;
    pdu_npages = 0;
    os88_strcpy(pdu_name, name, sizeof(pdu_name));
    os88_file_here(&pdu_docplace);      /* every caller stood there first */
    os88_memset(&pdu_xopen, 0, sizeof(pdu_xopen));
    os88_strcpy(pdu_xopen.name, name, sizeof(pdu_xopen.name));
    pdu_doc = D_OPENING;
    pdu_opfirst = 1;
    pdu_say2("Opening ", name);
    pdu_dirty_all = 1;
    pdu_job_start(J_OPEN);
}

/* the open's FIRST slice: the font bank (once per instance, a disk read)
 * and the document set of claims - here, off the lock, not in the callback
 * that asked */
static int pdu_open_first(void)
{
    int r;
    pdu_opfirst = 0;
    pdu_bank_load();                    /* a missing bank is not fatal */
    r = pdu_doc_claim();
    if (r == 0) {
        pdu_ed_init();                  /* edit */
        return 0;
    }
    pdu_job = J_NONE;
    pdu_doc = D_FAILED;
    pdu_err = r;
    if (r == PDE_NOMEM) {
        os88_strcpy(pdu_msg, "Not enough memory: PageDesk needs ",
                    sizeof(pdu_msg));
        os88_utoa(pdu_needkb, pdu_num);
        pdu_cat(pdu_msg, pdu_num, sizeof(pdu_msg));
        pdu_cat(pdu_msg, " KB.", sizeof(pdu_msg));
    } else
        pdu_say(pdu_errtext(r));
    pdu_dirty_all = 1;
    return r;
}

/* PAGEINFO for page n, then the view. 0 or a PDE_*. No lock: a disk read. */
static int pdu_incpg;                   /* page + 1 whose incompleteness
                                         * was said; 0 = not yet */
static int pdu_page_load(int n)
{
    unsigned r;
    r = pdu_mcall(PDU_M_ENG, PDV_ENG_PAGEINFO, n, 0, 0);
    if (pdu_mcf)
        return r ? r : PDE_LIMIT;
    pd_fcopy_in(pdu_xb, 0, &pdu_pinfo, sizeof(pdu_pinfo));
    pdu_page = n;
    pdu_inc = 0;
    pdu_incpg = 0;
    pdu_sy = 0;
    pdu_sx = 0;
    pdu_view_set();
    pdu_nav_page();                     /* nav: selection kept or dropped, links */
    pdu_nt_srcload();                   /* notes: this page's source annots */
    return 0;
}

static void pdu_open_step(void)
{
    unsigned r;
    if (pdu_opfirst) {
        pdu_open_first();
        return;
    }
    pd_fcopy_out(&pdu_xopen, pdu_xb, 0, sizeof(pdu_xopen));
    r = pdu_mcall(PDU_M_ENG, PDV_ENG_OPEN, 0, 0, 0);
    pd_fcopy_in(pdu_xb, 0, &pdu_xopen, sizeof(pdu_xopen));
    pdu_jslices++;
    if (!pdu_mcf && r == PDE_BUSY) {    /* D11: BUSY is CF=0, never CF=1 */
        pdu_progress("Opening ", pdu_name);
        return;
    }
    pdu_job = J_NONE;
    if (pdu_mcf || pdu_xopen.npages == 0) {
        pdu_doc_release();
        pdu_doc = D_FAILED;
        pdu_err = (pdu_mcf && r) ? r : PDE_SYNTAX;
        pdu_say2("Could not open ", pdu_name);
        pdu_dirty_all = 1;
        return;
    }
    pdu_npages = pdu_xopen.npages;
    pdu_rdonly = (pdu_xopen.flags & PDO_READONLY) != 0;
    pdu_sidetop = 0;
    pdu_fit = 1;
    r = pdu_page_load(0);
    if (r) {
        pdu_doc_release();
        pdu_doc = D_FAILED;
        pdu_err = r;
        pdu_say2("Could not open ", pdu_name);
        pdu_dirty_all = 1;
        return;
    }
    pdu_doc = D_OPEN;
    if (pdu_rdonly)
        pdu_say(pdu_errtext(PDE_SIGNED));
    else {
        os88_strcpy(pdu_line, "Opened ", sizeof(pdu_line));
        pdu_cat(pdu_line, pdu_name, sizeof(pdu_line));
        pdu_say(pdu_line);
    }
    pdu_dirty_all = 1;
}

/* ---------------------------------------------------------------------------
 * RENDER
 * -------------------------------------------------------------------------*/
static int pdu_render_begin(void)
{
    unsigned r;

    if (!pdu_isect(&pdu_rr, &pdu_L.canvas, &pdu_rrun)) {
        pdu_rwant = 0;
        return 0;
    }
    pdu_rwant = 0;
    os88_memcpy(&pdu_xrend.view, &pdu_view, sizeof(pdu_view));
    pdu_xrend.view.clip_x0 = pdu_rrun.x1;
    pdu_xrend.view.clip_y0 = pdu_rrun.y1;
    pdu_xrend.view.clip_x1 = pdu_rrun.x2;
    pdu_xrend.view.clip_y1 = pdu_rrun.y2;
    pdu_xrend.page = pdu_page;
    pdu_xrend.win_off = (unsigned)pdu_win;
    pdu_xrend.mono_mark = 0;
    pdu_xrend.edits_seg = pdu_ed_seg();             /* edit (D15) */
    pdu_xrend.edits_count = pdu_ed_count();
    pdu_xrend.rflags = PDRF_ANNOTS | PDRF_EDITS;
    pdu_xrend.hide = 0;
    pdu_ed_xoff(&pdu_rrun);                         /* edit: the outline off first */
    pd_fcopy_out(&pdu_xrend, pdu_xb, 0, sizeof(pdu_xrend));
    r = pdu_mcall(PDU_M_REND, PDV_REND_BEGIN, 0, 0, 0);
    if (pdu_mcf) {
        pdu_say2("Could not draw the page: ", pdu_errtext(r));
        os88_gfx_lock();
        pdu_status_paint();
        os88_gfx_unlock();
        return 0;
    }
    pdu_job_start(J_RENDER);
    return 1;
}

static void pdu_render_step(void)
{
    unsigned r;
    r = pdu_mcall(PDU_M_REND, PDV_REND_STEP, 0, 0, 0);
    pdu_jslices++;
    if (!pdu_mcf && r == PDE_BUSY) {
        pdu_progress("Drawing page ", "");
        return;
    }
    pdu_job = J_NONE;
    if (pdu_mcf) {
        pdu_wsay(pdu_errtext(r));
        return;
    }
    pdu_sel_after(&pdu_rrun);           /* nav: the selection back on the page */
    pdu_ed_xon();                       /* edit: the outline back on the new pixels */
    /* 10.6: say what was skipped, by name, or nothing at all */
    r = pdu_mcall(PDU_M_REND, PDV_REND_STATUS, 0, 0, 0);
    if (!pdu_mcf) {
        pd_fcopy_in(pdu_xb, 0, &pdu_xrst, sizeof(pdu_xrst));
        pdu_inc = pdu_xrst.incomplete;
        pdu_xrst.first[31] = 0;
        os88_strcpy(pdu_incwhy, pdu_xrst.first, sizeof(pdu_incwhy));
    }
    /* ONCE A PAGE: a repair of a damaged strip (a dialog closing, a
     * popover going) draws the same incomplete page again, and saying so
     * again would overwrite what the user just did - a save's outcome was
     * lost exactly that way. Help > Document info keeps the fact. */
    if (pdu_inc) {
        if (pdu_incpg != pdu_page + 1) {
            pdu_incpg = pdu_page + 1;
            pdu_say2("Incomplete rendering - ", pdu_incwhy);
            pdu_wsay(pdu_msg);
        }
    } else if (pdu_jslices > 8)
        pdu_wsay("Page drawn.");
}

/* ---------------------------------------------------------------------------
 * SAVE AS (14.1: only to a new name)
 * -------------------------------------------------------------------------*/
/* "D:YYYYMMDDHHmmSS" from the OS clock (SPEC.md 37), or "" with no RTC:
 * a PIT clock since boot was never set, and a date of 1980 is a fact the
 * file would carry as if it were true. dst holds at least 17 bytes. */
static unsigned pdu_clkw[4];
static void pdu_d2(char *p, unsigned v)
{
    p[0] = (char)('0' + v / 10);
    p[1] = (char)('0' + v % 10);
}
static void pdu_datestamp(char *dst)
{
    dst[0] = 0;
    if (!pdu_clk(pdu_clkw))
        return;
    dst[0] = 'D';
    dst[1] = ':';
    pdu_d2(dst + 2, pdu_clkw[0] / 100);
    pdu_d2(dst + 4, pdu_clkw[0] % 100);
    pdu_d2(dst + 6, pdu_clkw[1] >> 8);
    pdu_d2(dst + 8, pdu_clkw[1] & 0xFF);
    pdu_d2(dst + 10, pdu_clkw[2] >> 8);
    pdu_d2(dst + 12, pdu_clkw[2] & 0xFF);
    pdu_d2(dst + 14, pdu_clkw[3] & 0xFF);
    dst[16] = 0;
}

static void pdu_save_begin(const char *name)
{
    int r;

    pdu_job_abort_render();
    if (pdu_doc != D_OPEN)
        return;
    os88_strcpy(pdu_target, name, sizeof(pdu_target));
    r = pdu_wrt_in_now();
    if (r) {
        pdu_say2("Could not save: ", pdu_errtext(r));
        pdu_status_paint();
        return;
    }
    os88_memset(&pdu_xsave, 0, sizeof(pdu_xsave));
    os88_strcpy(pdu_xsave.target, name, sizeof(pdu_xsave.target));
    /* D9: what an incremental save copies, and the stamp it writes. The
     * source is the open document's 8.3 name; author stays "" (no author
     * preference in wave 1). */
    os88_strcpy(pdu_xsave.source, pdu_name, sizeof(pdu_xsave.source));
    pdu_datestamp(pdu_xsave.date);
    pdu_xsave.mode = PDSV_INCREMENTAL;
    pdu_xsave.edits_seg = pdu_ed_seg();             /* edit (13.1.1) */
    pdu_xsave.edits_count = pdu_ed_count();
    pdu_say2("Saving ", name);
    pdu_status_paint();
    pdu_job_start(J_SAVE);
}

static void pdu_save_step(void)
{
    unsigned r;
    pd_fcopy_out(&pdu_xsave, pdu_xb, 0, sizeof(pdu_xsave));
    r = pdu_mcall(PDU_M_WRT, PDV_WRT_SAVE, 0, 0, 0);
    pd_fcopy_in(pdu_xb, 0, &pdu_xsave, sizeof(pdu_xsave));
    pdu_jslices++;
    if (!pdu_fsop && (pdu_xsave.total.lo | pdu_xsave.total.hi))
        pdu_fsop = os88_fs_op(pdu_xsave.total.hi, pdu_xsave.total.lo) == 0;
    if (!pdu_mcf && r == PDE_BUSY) {
        pdu_progress("Saving ", pdu_target);
        return;
    }
    pdu_job = J_NONE;
    pdu_fsop_end();
    pdu_wrt_out();                      /* 16.2: the writer is freed after */
    if (pdu_mcf) {
        os88_strcpy(pdu_line, "Could not save ", sizeof(pdu_line));
        pdu_cat(pdu_line, pdu_target, sizeof(pdu_line));
        pdu_cat(pdu_line, ": ", sizeof(pdu_line));
        pdu_cat(pdu_line, pdu_errtext(r), sizeof(pdu_line));
        pdu_wsay(pdu_line);
        return;
    }
    pdu_saved();                        /* edit: the save point */
    pdu_title_set();
    os88_strcpy(pdu_line, "Saved ", sizeof(pdu_line));
    pdu_cat(pdu_line, pdu_target, sizeof(pdu_line));
    pdu_wsay(pdu_line);
}

/* ---------------------------------------------------------------------------
 * THE WAKE HANDLER: one slice, then re-post while there is work
 * -------------------------------------------------------------------------*/
void os88_onwake(void *win)
{
    int r;

    if (win != pdu_win)
        return;
    pdu_at_doc();
    if (pdu_job == J_THUMB && (pdu_rwant || pdu_pgwant || pdu_txwant || pdu_fwant
                               || pdu_lkact))
        pdu_job_abort();                /* nav: a picture waits for everything else */
    if (pdu_jcancel) {
        int was;
        was = pdu_job;
        pdu_job_abort();
        if (was == J_RENDER)
            pdu_rwant = 0;              /* cancelled: the page stays partial */
        pdu_wsay(was == J_RENDER ? "Drawing cancelled - the page is incomplete."
                                 : pdu_errtext(PDE_CANCEL));
        if (was == J_OPEN)
            pdu_dirty_all = 1;
    }

    /* the launch document, first (SPEC.md 54.10) */
    if (pdu_argp && pdu_job == J_NONE) {
        pdu_argp = 0;
        if (os88_file_goto_q_mark(pdu_argplace.clus, pdu_argplace.vol) != 0)
            pdu_say("Could not reach that document's folder.");
        else
            pdu_open_begin(pdu_argname);
    }

    if (pdu_job == J_OPEN)
        pdu_open_step();
    else if (pdu_job == J_SAVE)
        pdu_save_step();
    else if (pdu_job_nav()) {
        /* nav: find, a text pass or a thumbnail took this slice */
    } else if (pdu_job == J_RENDER) {
        if (pdu_pop != P_NONE || pdu_about)
            pdu_job_abort_render();     /* never draw under a popover */
        else
            pdu_render_step();
    } else if (pdu_pgwant && pdu_doc == D_OPEN) {
        int old;
        old = pdu_page;
        r = pdu_page_load(pdu_pgwant - 1);
        pdu_pgwant = 0;
        os88_gfx_lock();
        if (r)
            pdu_tell(pdu_errtext(r));
        else {
            pdu_say("");
            pdu_canvas_full();
            pdu_side_page(old);         /* two cells, or a new list */
            pdu_notes_page();           /* the one line that can change */
        }
        os88_gfx_unlock();
        pdu_menusync();
    }

    if (pdu_dirty_all) {
        pdu_dirty_all = 0;
        pdu_title_set();
        pdu_menusync();
        os88_gfx_lock();
        pdu_relayout();
        pdu_paint_all();
        os88_gfx_unlock();
    }

    if (pdu_job == J_NONE && pdu_rwant && pdu_doc == D_OPEN
        && pdu_pop == P_NONE && !pdu_about && pdu_gs != 2)
        pdu_render_begin();
    else if (pdu_job == J_NONE && !pdu_pgwant && !pdu_argp && !pdu_rwant
             && pdu_nav_owed())
        ovl_nav_work();                 /* nav: the idle ladder */

    if (pdu_job == J_NONE && pdu_nt_owed())     /* notes */
        pdu_nt_idle();

    if (pdu_job != J_NONE || pdu_pgwant || pdu_argp || pdu_nav_owed())
        pdu_kick();
}

/* ---------------------------------------------------------------------------
 * CLOSE (SPEC.md 75.1): the window is going. The instance's death frees every
 * claim (SPEC.md 50), but not a file: a save stopped here would leave
 * PDWRT's temporary file behind, so the writer is told first. Wave 1 has no
 * edits to lose, so the answer is always yes; wave 2's unsaved-changes alert
 * goes in front of this.
 * -------------------------------------------------------------------------*/
int os88_onclose(void *win)
{
    if (win != pdu_win)
        return 1;
    if (pdu_job == J_SAVE) {
        pdu_mcall(PDU_M_WRT, PDV_WRT_ABORT, 0, 0, 0);
        pdu_fsop_end();
        pdu_job = J_NONE;
    }
    if (pdu_doc == D_OPEN || pdu_doc == D_OPENING)
        pdu_doc_release();
    pdu_doc = D_EMPTY;
    pdu_job = J_NONE;
    pdu_mod_quit();
    return 1;
}
