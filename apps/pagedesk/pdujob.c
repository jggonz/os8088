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
/* PDREND hears the abort at the next wake's top (pdu_rend_sync), before any
 * slice could run - slices run only from the wake, so nothing is drawn in
 * between. Telling it here put a module call (and its kernel calls) under
 * every view change, ten frames down a click: 498 B of the UI task's 384
 * in the stack gate (pdstk, D22) once the page map's undo was real. */
static void pdu_rend_sync(void)
{
    if (pdu_rabort) {
        pdu_rabort = 0;
        pdu_mcall(PDU_M_REND, PDV_REND_ABORT, 0, 0, 0);
    }
}

static void pdu_job_abort_render(void)
{
    if (pdu_job != J_RENDER)
        return;
    pdu_rabort = 1;
    pdu_job = J_NONE;
    pdu_render_want(&pdu_rrun);         /* kicks the wake */
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

static int pdu_opauth;                  /* 20.22.2.6: PDXK in, ui's steps owed */
static unsigned char pdu_pfl;           /* 20.22.4.5: the pre-flight - 1 the next
                                         * file owed, 2 opening it */
static unsigned char pdu_pfk;           /* its slot; PDM_NSLOT = the document */
static unsigned char pdu_pfm;           /* the foreign slots the map names */

/* Escape and every way out: stop what runs, release what it held */
static void pdu_job_abort(void)
{
    int reopen;
    reopen = 0;
    if (pdu_opauth == 1 && pdu_au_mode)     /* ui: an open ask goes: closed, */
        pdu_au_close();                 /* and painting nothing meanwhile (F3) */
    if (pdu_job == J_RENDER) {
        pdu_mcall(PDU_M_REND, PDV_REND_ABORT, 0, 0, 0);
    } else if (pdu_job == J_OPEN) {
        pdu_doc_release();
        pdu_doc = D_EMPTY;
    } else if (pdu_job == J_SAVE) {
        if (pdu_pfl)                    /* the pre-flight's file (PDENG */
            pdu_mcall(PDU_M_ENG, PDV_ENG_CLOSE, 0, 0, 0);   /* owes the document) */
        else
            pdu_mcall(PDU_M_WRT, PDV_WRT_ABORT, 0, 0, 0);
        pdu_pfl = 0;
        pdu_wrt_out();
        reopen = (pdu_xsave.mode & PDW_OUT_REOPEN) != 0;          /* pages */
    } else if (pdu_job >= J_FIND) {     /* nav: find, text pass, thumbnail */
        pdu_mcall(PDU_M_REND, PDV_REND_ABORT, 0, 0, 0);
        pdu_tx_aborted();               /* nav: what the pass held goes back */
    }
    pdu_fsop_end();
    pdu_job = J_NONE;
    pdu_jcancel = 0;
    if (reopen)
        pdu_reopen_begin();             /* pages: PDENG was left on the output (20.11.5) */
}

static void pdu_job_start(int kind)
{
    pdu_lad_begin();                    /* 20.22.2.6: a page view's counts */
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
    pdu_pshown = 1;
    os88_strcpy(pdu_line, verb, sizeof(pdu_line));
    pdu_cat(pdu_line, what, sizeof(pdu_line));
    pdu_cat(pdu_line, " (", sizeof(pdu_line));     /* QA R15: the slices, in brackets - */
    os88_utoa(pdu_jslices, pdu_num);                /* never read as the page */
    pdu_cat(pdu_line, pdu_num, sizeof(pdu_line));
    pdu_cat(pdu_line, ") - Esc cancels", sizeof(pdu_line));
    pdu_wsay(pdu_line);
    pdu_pshown = 1;                     /* polish: after the say, which clears it */
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
    pdu_rc_begin();                     /* recov */
    if (pdu_doc == D_OPEN || pdu_doc == D_OPENING)
        pdu_doc_release();
    pdu_rwant = 0;
    pdu_pgwant = 0;
    pdu_changes = 0;
    pdu_stale = 0;                      /* QA F31: a new file */
    pdu_dskout = 0;                     /* QA F21: nothing missed yet */
    pdu_nt_reset();                     /* notes */
    pdu_inc = 0;
    pdu_nav_reset();                    /* nav: history, selection, panels */
    pdu_rdonly = 0;
    pdu_npages = 0;
    pdu_pgmap = 0;                      /* pages */
    pdu_pk = PK_NONE;
    pdu_reopening = 0;
    os88_strcpy(pdu_name, name, sizeof(pdu_name));
    os88_file_here(&pdu_docplace);      /* every caller stood there first */
    os88_memset(&pdu_xopen, 0, sizeof(pdu_xopen));
    os88_strcpy(pdu_xopen.name, name, sizeof(pdu_xopen.name));
    pdu_doc = D_OPENING;
    pdu_opfirst = 1;
    pdu_opauth = 0;
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
    pdp_load();                         /* prefs: SYSTEM/APPDATA/PAGEDESK.CFG, before the Flate rule */
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
static char pdu_opsaid;                 /* "Opened X" stands: the first draw's line replaces it (R16) */
char pdu_ovidle;                        /* pdu_ovl_fail: idle work is fetching the overlay - 1 the
                                         * panels', 2 the recovery look after an open */
static char pdu_ovhold;                 /* that fetch was refused: the ladder waits for the user */
static int pdu_incpg;                   /* page + 1 whose incompleteness
                                         * was said; 0 = not yet */
static int pdu_page_load(int n)
{
    unsigned r;
    if (pdu_stale)
        return PDE_CHANGED;             /* QA F31: never the changed bytes */
    r = pdu_mcall(PDU_M_ENG, PDV_ENG_PAGEINFO, pdu_pg_id(n), 0, 0);   /* pages */
    if (pdu_mcf)
        return r ? r : PDE_LIMIT;
    pd_fcopy_in(pdu_xb, 0, &pdu_pinfo, sizeof(pdu_pinfo));
    if (pdu_pgmap)                                                  /* pages: 13.7 */
        pdu_pinfo.rotate = (pdu_pinfo.rotate + 90 * PDM_ROT(pdu_pgw(n))) % 360;
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
    if (pdu_opauth == 1) {              /* the key, ui's prompt (20.22.2.6) */
        r = ovl_au_slice(0);
        if (r == PDE_BUSY)
            return;
        pdu_opauth = 2;                 /* once an open */
        pdu_mcf = r != 0;
        if (r)
            goto failed;
    }
    pd_fcopy_out(&pdu_xopen, pdu_xb, 0, sizeof(pdu_xopen));
    r = pdu_mcall(PDU_M_ENG, PDV_ENG_OPEN, 0, 0, 0);
    pd_fcopy_in(pdu_xb, 0, &pdu_xopen, sizeof(pdu_xopen));
    pdu_jslices++;
    r = pdu_ladck(PDU_M_ENG, r);        /* a region: then OPEN again */
    if (r == 0xFFFF)
        return;
    if (pdu_mcf && r == PDE_PASSWORD && !pdu_opauth) {
        pdu_opauth = 1;                 /* OPEN paused at its state 7 (20.22.5) */
        if ((r = ovl_au_slice(1)) == PDE_BUSY)
            return;
        pdu_opauth = 2;
        pdu_mcf = r != 0;
        if (!r) {
            if (pdu_ovl_in())
                return;                 /* no question, no prompt: OPEN again */
            pdu_mcf = 1;                /* QA F21: the overlay could not be read */
            r = PDE_IO;                 /* (its toast said why); an OPEN with no */
        }                               /* key left the next open refused */
    }
    if (!pdu_mcf && r == PDE_BUSY) {    /* D11: BUSY is CF=0, never CF=1 */
        if (!pdu_reopening)             /* pages: the save's outcome stays said */
            pdu_progress("Opening ", pdu_name);
        return;
    }
failed:
    pdu_job = J_NONE;
    if (pdu_mcf || pdu_xopen.npages == 0
        || (pdu_reopening && pdu_xopen.npages != pdu_srcn)) {       /* pages */
        pdu_reopening = 0;
        pdu_pgsaved = 0;
        pdu_doc_release();
        pdu_doc = D_FAILED;
        pdu_err = pdu_dskout ? PDE_IO : (pdu_mcf && r) ? r : PDE_SYNTAX;   /* QA F21: never 'damaged' for our disk */
        pdu_say2("Could not open ", pdu_name);
        pdu_dirty_all = 1;
        return;
    }
    if (pdu_reopening) {                /* pages: PDENG has its document back (20.11.5) */
        pdu_reopening = 2;              /* verify-insert: the reload below keeps the save's line */
        pdu_pgwant = pdu_page + 1;
        pdu_kick();
        return;
    }
    if (pdu_socc == PDX_KEY)            /* the key is set: PDXK's RAM back */
        pdu_slot_free();
#ifdef PDU_LADTEST
    {   /* A DEVELOPER'S LOOK at 20.22.2 (define PDU_LADTEST in pagedesk.c and
         * pagedesk.asm): every occupant in and out of the slot at each open,
         * then a toast - loads off the disk, copies back from the cache, the
         * cache's kind (1 XMS, 2 EMS), the last answer, the largest free KiB */
        static char m[28];
        unsigned o, q = 0;
        for (o = 1; o <= PDU_OCC_WRT && !q; o++)
            q = pdu_slot_load(o);
        pdu_slot_free();
        m[0] = 0;
        pdu_cat(m, "d", sizeof(m)); os88_utoa(pdu_tdisk, pdu_num); pdu_cat(m, pdu_num, sizeof(m));
        pdu_cat(m, " c", sizeof(m)); os88_utoa(pdu_tcache, pdu_num); pdu_cat(m, pdu_num, sizeof(m));
        pdu_cat(m, " x", sizeof(m)); os88_utoa(pdu_ckind, pdu_num); pdu_cat(m, pdu_num, sizeof(m));
        pdu_cat(m, " r", sizeof(m)); os88_utoa(q, pdu_num); pdu_cat(m, pdu_num, sizeof(m));
        pdu_cat(m, " f", sizeof(m)); os88_utoa(os88_mem_largest_p() >> 6, pdu_num); pdu_cat(m, pdu_num, sizeof(m));
        os88_toast(m, 0);
    }
#endif
    pdu_npages = pdu_xopen.npages;
    pdu_srcn = pdu_npages;              /* pages */
    pdu_rdonly = (pdu_xopen.flags & PDO_READONLY) != 0;
    pdu_sidetop = 0;
    pdp_view();                         /* prefs: the default view (was pdu_fit = 1) */
    r = pdu_page_load(pdu_resume && pdu_resume <= pdu_npages ? pdu_resume - 1 : 0);   /* pages */
    pdu_resume = 0;
    if (r) {
        pdu_doc_release();
        pdu_doc = D_FAILED;
        pdu_err = r;
        pdu_say2("Could not open ", pdu_name);
        pdu_dirty_all = 1;
        return;
    }
    pdu_doc = D_OPEN;
    pdp_form = 0;                       /* prefs: set where the note is said */
    if (pdu_rdonly)
        pdu_say(pdu_errtext(PDE_SIGNED));
    else if (pdu_pgsaved) {             /* pages: "Saved X", said after the re-open */
        pdu_say2("Saved ", pdu_target);
        pdu_pgsaved = 0;
    } else if (pdu_xopen.flags & PDO_ACROFORM) {  /* prefs: PD-26 */
        pdp_form = 1;
        pdu_say(pdp_formmsg);
    } else {
        os88_strcpy(pdu_line, "Opened ", sizeof(pdu_line));
        pdu_cat(pdu_line, pdu_name, sizeof(pdu_line));
        pdu_say(pdu_line);
        pdu_opsaid = 1;                 /* QA R16: the first page's own line replaces it */
    }
    pdu_dirty_all = 1;
}

/* ---------------------------------------------------------------------------
 * RENDER
 * -------------------------------------------------------------------------*/
/* polish (20.19.1): what a render owes and will not draw now - cancelled,
 * failed, or held behind a popover or the About card - becomes white paper
 * from row y down, never the pixels that lay there: since the canvas lays no
 * white ahead of PDREND, those are the last page, the last zoom or a closed
 * dialog. Only the page part: the desk around it is the canvas paint's, and
 * fresh. Lock held; sets the clip itself. */
static struct os88_rect pdu_gr;
static int pdu_ry;
static void ovl_ground(struct os88_rect *r, int y)
{
    if (!pdu_isect(r, &pdu_L.canvas, &pdu_gr))
        return;
    if (y > pdu_gr.y1)
        pdu_gr.y1 = y;
    pdu_page_rect(&pdu_pr);
    if (pdu_isect(&pdu_gr, &pdu_pr, &pdu_gr) && os88_wm_clip_set(pdu_win) == 0)
        pdu_fill(pdu_gr.x1, pdu_gr.y1, pdu_gr.x2, pdu_gr.y2, OS88_WHITE);
}

/* a render of pdu_rrun stopped short (Esc, or PDREND failed): paper below
 * the last band it blitted (`ask`: PDREND says which, ABORT CX=1), and then
 * every pixel of pdu_rrun is fresh, so nav's selection goes back on over the
 * whole of it and edit's outline with it - the XOR invariant holds */
static void ovl_render_rest(int ask)
{
    pdu_ry = pdu_rrun.y1;
    if (ask) {
        pdu_mcall(PDU_M_REND, PDV_REND_ABORT, 0, 1, 0);
        if (!pdu_mcf)
            pd_fcopy_in(pdu_xb, 0, &pdu_ry, 2);
    }
    os88_gfx_lock();
    ovl_ground(&pdu_rrun, pdu_ry);
    os88_gfx_unlock();
    pdu_sel_after(&pdu_rrun);
    pdu_ed_xon();
}

static int pdu_render_begin(void)
{
    unsigned r, bad;

    if (!pdu_isect(&pdu_rr, &pdu_L.canvas, &pdu_rrun) || pdu_pg_kind(pdu_page) != PDM_SRC) {   /* pages: a blank is paper */
        pdu_rwant = 0;
        return 0;
    }
    pdu_rwant = 0;
    os88_memcpy(&pdu_xrend.view, &pdu_view, sizeof(pdu_view));
    pdu_xrend.view.clip_x0 = pdu_rrun.x1;
    pdu_xrend.view.clip_y0 = pdu_rrun.y1;
    pdu_xrend.view.clip_x1 = pdu_rrun.x2;
    pdu_xrend.view.clip_y1 = pdu_rrun.y2;
    pdu_xrend.page = pdu_pg_id(pdu_page);                           /* pages */
    pdu_xrend.win_off = (unsigned)pdu_win;
    pdu_xrend.mono_mark = pdp_cfg[8];               /* prefs (17.4) */
    pdu_xrend.edits_seg = pdu_ed_seg();             /* edit (D15) */
    pdu_xrend.edits_count = pdu_ed_count();
    pdu_xrend.rflags = PDRF_ANNOTS | PDRF_EDITS;
    pdu_xrend.hide = 0;
    pdu_ed_xoff(&pdu_rrun);                         /* edit: the outline off first */
    pd_fcopy_out(&pdu_xrend, pdu_xb, 0, sizeof(pdu_xrend));
    pdu_rend_sync();                    /* an owed abort before a new begin */
    if (pdu_stale) {                    /* QA F31: paper, not the changed file */
        r = PDE_CHANGED;
        bad = 1;
    } else {
        r = pdu_mcall(PDU_M_REND, PDV_REND_BEGIN, 0, 0, 0);
        bad = pdu_mcf;
    }
    if (bad) {
        if (pdu_stale != 1)             /* the save's refusal stands the first time */
            pdu_say2("Could not draw the page: ", pdu_errtext(r));
        if (pdu_stale)
            pdu_stale = 2;
        os88_gfx_lock();
        pdu_status_paint();
        os88_gfx_unlock();
        ovl_render_rest(0);             /* polish: none of it drawn */
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
    if ((r = pdu_ladck(PDU_M_REND, r)) == 0xFFFF)
        return;                         /* 20.22.2.6: STEP again, next slice */
    if (!pdu_mcf && r == PDE_BUSY) {
        os88_utoa(pdu_page + 1, pdu_num);       /* QA R15: the page it IS */
        pdu_progress("Drawing page ", pdu_num);
        return;
    }
    pdu_job = J_NONE;
    if (pdu_mcf) {
        pdu_wsay(pdu_errtext(r));
        ovl_render_rest(1);             /* polish: paper below its last band */
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
    } else if (pdp_form || pdu_jslices > 8 || pdu_pshown || pdu_opsaid) /* prefs: a form's note; polish: never leave "Esc cancels" */
        pdu_wsay(pdp_done());
    pdu_opsaid = 0;
}

/* ---------------------------------------------------------------------------
 * SAVE AS (14.1: only to a new name)
 * -------------------------------------------------------------------------*/
/* "D:YYYYMMDDHHmmSS" from the OS clock (SPEC.md 37), or "" with no RTC:
 * a PIT clock since boot was never set, and a date of 1980 is a fact the
 * file would carry as if it were true. dst holds at least 17 bytes. */
static unsigned pdu_clkw[4];
static void ovl_d2(char *p, unsigned v)
{
    p[0] = (char)('0' + v / 10);
    p[1] = (char)('0' + v % 10);
}
static void ovl_datestamp(char *dst)   /* save only: the overlay's */
{
    dst[0] = 0;
    if (!pdu_clk(pdu_clkw))
        return;
    dst[0] = 'D';
    dst[1] = ':';
    ovl_d2(dst + 2, pdu_clkw[0] / 100);
    ovl_d2(dst + 4, pdu_clkw[0] % 100);
    ovl_d2(dst + 6, pdu_clkw[1] >> 8);
    ovl_d2(dst + 8, pdu_clkw[1] & 0xFF);
    ovl_d2(dst + 10, pdu_clkw[2] >> 8);
    ovl_d2(dst + 12, pdu_clkw[2] & 0xFF);
    ovl_d2(dst + 14, pdu_clkw[3] & 0xFF);
    dst[16] = 0;
}

static const unsigned pdu_savever[3] = { PDV_WRT_SAVE, PDV_WRT_NEWDOC,
                                         PDV_WRT_EXTRACT };
static const unsigned pdu_savemode[3] = { PDSV_INCREMENTAL, PDSV_FRESH,
                                          PDSV_EXTRACT };

static void pdu_save_done(unsigned err)
{
    pdu_wrt_out();                      /* the writer's room is the overlay's (QA F25) */
    if (!ovl_save_done(err) && (pdu_xsave.mode & PDW_OUT_REOPEN))
        pdu_reopen_begin();             /* the overlay could not be had: PDENG still needs its document */
}

/* PDENG gets the source back: OPEN again with the claims it still holds
 * (CLOSE keeps them), nothing of ours released - the edits and the page map
 * are the user's work */
static void pdu_reopen_begin(void)
{
    pdu_at_doc();
    os88_memset(&pdu_xopen, 0, sizeof(pdu_xopen));
    os88_strcpy(pdu_xopen.name, pdu_name, sizeof(pdu_xopen.name));
    pdu_reopening = 1;
    pdu_opauth = 0;                     /* the held key answers (20.22.2.10) */
    pdu_job_start(J_OPEN);
}

static void pdu_save_step(void)
{
    unsigned r;
    if (pdu_pfl && ovl_ifl_step())       /* the Insert's keys first (20.22.4.5) */
        return;
    /* PDWRT in at the first slice, HERE and not in the overlay that began
     * the save: no overlay code stands on the stack, so release-before-refuse
     * may give the overlay's 51.5 KiB to the writer (QA F25, 20.22.2.4) */
    if ((r = pdu_slot_load(PDU_OCC_WRT)) != 0) {    /* (resident: 0 at once) */
        pdu_job = J_NONE;
        pdu_save_done(r);
        return;
    }
    pdu_rc_fin();                       /* recov */
    if (pdu_savekind == 1)              /* the XB survives nothing: New's page, again */
        pd_fcopy_out(pdu_newp, pdu_xb, 512, sizeof(pdu_newp));
    pd_fcopy_out(&pdu_xsave, pdu_xb, 0, sizeof(pdu_xsave));
    r = pdu_mcall(PDU_M_WRT, pdu_savever[pdu_savekind], 0, 0, 0);
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
    if (pdu_mcf && r == PDE_EXISTS && pdu_xsave.state == PDW_ST_CONFIRM) {
        pdu_pk = PK_REPLACE;            /* 14.3: the target is there - ask */
        pdu_say2(pdu_target, " exists. Enter replaces it, Esc keeps it.");
        pdu_wsay(pdu_msg);
        return;
    }
    pdu_save_done(pdu_mcf ? r : 0);
}

/* ---------------------------------------------------------------------------
 * THE WAKE HANDLER: one slice, then re-post while there is work
 * -------------------------------------------------------------------------*/
/* The idle ladder's work is the overlay's, and the overlay is the one part
 * a render drops for its occupants (20.22.2.4). A refused fetch used to be
 * retried on every wake - the same "Not enough memory" toast again and again
 * while nothing on the machine had changed, and the page drawn all along.
 * Now the thumbnails' and notes' refusal says what waits (pdu_ovl_fail), and
 * the ladder holds until the user does something: a key, a click or a
 * command clears the hold, as does the overlay arriving by another path */
static void pdu_idle_ovl(int notes)
{
    pdu_ovidle = notes || !(pdu_txwant || pdu_fwant || pdu_lkact || pdu_selshow
                            || (pdu_selq < 0 && pdu_selpg >= 0));
    if (notes)
        pdu_nt_idle();
    else
        ovl_nav_work();
    pdu_ovidle = 0;
    if (!pdu_ovl_in())
        pdu_ovhold = 1;
}

void os88_onwake(void *win)
{
    int r;

    if (win != pdu_win)
        return;
    pdu_stk_wake();                     /* deepstk */
    if (pdu_ovl_in())
        pdu_ovhold = 0;                 /* the panels' work can run again */
    pdu_rend_sync();                    /* an abort a view change owed */
    pdu_pg_wake();                      /* pages: an undone/redone page op */
    if (pdu_job == J_SAVE)              /* pages: a save stands in the TARGET's folder (D14) */
        os88_file_goto_q_mark(pdu_tgtplace.clus, pdu_tgtplace.vol);
    else
        pdu_at_doc();
    if (pdu_job == J_THUMB && (pdu_rwant || pdu_pgwant || pdu_txwant || pdu_fwant
                               || pdu_lkact))
        pdu_job_abort();                /* nav: a picture waits for everything else */
    if (pdu_popcls) {                   /* a close a deep click asked for */
        pdu_popcls = 0;
        os88_gfx_lock();                /* a callback's paints run locked; */
        pdu_pop_close();                /* the wake's must take it (pdujob.c 6) */
        os88_gfx_unlock();
    }
    if (pdu_popdmg_on) {                /* a popover switch: ground + new, once */
        pdu_popdmg_on = 0;
        pdu_popown = 0;
        os88_gfx_lock();
        pdu_paint_rect(&pdu_popdmg);
        os88_gfx_unlock();
    } else if (pdu_popown) {            /* a new popover: its own pixels */
        pdu_popown = 0;
        if (pdu_pop != P_NONE) {
            os88_gfx_lock();
            pdu_pop_paint();
            os88_gfx_unlock();
        }
    }
    pdp_wake();                         /* prefs: the dialog's debts */
    if (pdu_jcancel) {
        int was;
        was = pdu_job;
        pdu_job_abort();
        if (was == J_RENDER) {
            pdu_rwant = 0;              /* cancelled: the page stays partial */
            ovl_render_rest(1);         /* polish: paper, not old pixels, below */
        }
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
            if (pdu_reopening)          /* verify-insert */
                pdu_reopening = 0;
            else
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
             && !pdu_ovhold && pdu_nav_owed())
        pdu_idle_ovl(0);                /* nav: the idle ladder */

    if (pdu_job == J_NONE && !pdu_ovhold && pdu_nt_owed())     /* notes */
        pdu_idle_ovl(1);

    pdu_rc_wake();                      /* recov */
    if ((pdu_job != J_NONE && !pdu_au_waits()) || pdu_pgwant || pdu_argp
        || (!pdu_ovhold && pdu_nav_owed()))
        pdu_kick();                     /* a job waiting for the user's key does not spin (F4) */
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
    if (!pdu_rc_close())                /* recov: Save / Discard / Cancel (20.17.8) */
        return 0;
    if (pdu_job == J_SAVE || pdu_pk == PK_REPLACE) {         /* pages: a parked replace too */
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
