/* ============================================================================
 * os8088 - apps/weave/wevent.c
 *
 * THE EVENT RING, THE SLICES AND THE HANDLERS (WEAVE-SPEC 4.9-4.11).
 * #included by apps/weave/weave.c after wact.c: this is the half that runs
 * bytecode, and wact.c is the half that may not.
 *
 * ---------------------------------------------------------------------------
 * THE SHAPE, WHICH IS RunCPM's (SPEC.md 74.1)
 * ---------------------------------------------------------------------------
 * A callback enqueues and posts OSAPI_WM_WAKE and returns. Some passes later
 * the UI task dispatches W_ONWAKE - the one callback that runs WITHOUT the
 * gfx lock - and that is where a handler is started and run in slices. The
 * wake re-posts only while a handler is unfinished or the ring is non-empty
 * (4.10), so an idle app costs zero CPU: a spinning wake is ~1,400 task
 * switches a second of dead UI task at 693 us each.
 *
 * ---------------------------------------------------------------------------
 * WHY DRAWING IS DEFERRED TO THE END OF A SLICE
 * ---------------------------------------------------------------------------
 * A `SETP` marks its component DIRTY and draws nothing; the flush below takes
 * the gfx lock once per slice and repaints exactly the components that
 * changed. Two reasons, and the second is the load-bearing one:
 *
 *   - a handler that writes `status.text` three times repaints once, which is
 *     PERFORMANCE.md's rule about primitive calls rather than pixels;
 *   - ONWAKE does NOT hold the lock, and a slice is 51-154 ms at the
 *     contracted 10-30k ops/s (4.10). Taking the lock for the slice would
 *     hold it for that long against SPEC.md 7.3's 37-70 ms click-to-action
 *     bar - which is the bar `weavelat` measures. Taking it per gfx call
 *     would be a lock pair per primitive. Once per slice, around the
 *     components that actually moved, is both.
 * ==========================================================================*/

/* ============================================================================
 * THE RING (WEAVE-SPEC 4.9) - the policy is wvm.inc's; this is the doorway
 * ==========================================================================*/

static int w_wake_owed;                 /* a wake is posted and unspent */

static void w_kick(void)
{
    if (w_wake_owed)
        return;
    if (os88_wm_wake(w_win) == 0)
        w_wake_owed = 1;
}

/* w_enq - one event into the ring, and the BEL that 4.9 rule 5 asks for.
 *
 * A refused NON-key is a coalesced or dropped event and is silent by design;
 * a refused KEY means the ring is genuinely full of keys, and the platform's
 * answer to a refused keystroke is a beep rather than nothing (the RunCPM
 * precedent). Input overrun is one of the three defect classes CLAUDE.md
 * names as invisible in every emulator, which is why the refusal is audible
 * rather than logged. */
static void w_enq(int comp, int atom, int d1, int d2)
{
    if (w_state != W_ST_RUN)
        return;
    if (wvm_enq(comp, atom, d1, d2) == 0) {
        if (atom == WA_ONKEY)
            os88_snd_tone(880, 2, 0);
        return;
    }
    w_kick();
}

/* ============================================================================
 * WEAVE-SPEC 10.6.1's SENTENCES
 *
 * The core raises a CODE (wvm.inc has no status row and no formatter); this
 * is the only place with somewhere to put a sentence, so the text lives here.
 * The table is indexed by the code and every row is 10.6.1's exact wording.
 * ==========================================================================*/

static const char *w_errtext[WE_NERR] = {
    "type mismatch.", "divide by zero.", "out of string space.",
    "too deep.", "array index ", "bad opcode.", "bad builtin.",
    "no component ", "no property.", "no method.", "no method.",
    "list index ", "grid cell ", "card ", "rand of "
};

/* w_scripterr - 10.6: the handler is already stopped (the core cleared the
 * stacks and kept the ring); this is the sentence and nothing else.
 *
 * WHERE IT LANDS. 10.6 says "the status row", and on a card WEAVE HAS NO
 * STATUS ROW: 7.1.1 gives the family no status strip and w_repaint2 hands
 * the whole content area to the card, because a sentence drawn over the app's
 * last row is worse than one the toast already showed. So it goes to the
 * platform's own transient row and is KEPT in w_status for the overlay's
 * diagnostics, which is where a user who missed the toast can read it. */
static void w_scripterr(void)
{
    int c;

    c = wvm_errcode();
    w_l0();
    w_ls("Script error in fn ");
    w_ln((unsigned)wvm_errfn());
    w_ls(": ");
    w_ls(c >= 0 && c < WE_NERR ? w_errtext[c] : "stopped.");
    if (c == WE_AIDX || c == WE_LIDX) {
        w_ln((unsigned)wvm_erra());
        w_ls(" of ");
        w_ln((unsigned)wvm_errb());
        w_ls(".");
    } else if (c == WE_NOCOMP || c == WE_CARD || c == WE_RAND) {
        w_ln((unsigned)wvm_erra());
        w_ls(".");
    } else if (c == WE_GCELL) {
        w_ln((unsigned)wvm_erra());
        w_ls(",");
        w_ln((unsigned)wvm_errb());
        w_ls(".");
    }
    os88_strcpy(w_serr, w_line, sizeof(w_serr));

    /* THE TOAST HOLDS 23 CHARACTERS (`TOAST_MAX`, SPEC.md 59.8) and the
     * shortest sentence above is `Script error in fn 0: type mismatch.` at
     * 35. Booted, that came out as `Script error in fn 0: di` - the alarm
     * intact and the CAUSE cut off, which is the half a user needs.
     *
     * So the two jobs are split: the toast is the ALARM and says where, and
     * Bundle Info is the DETAIL and says what - which is what 1.2 already
     * names that overlay for. `Script error in fn 250.` is 22 characters at
     * the worst function index the format allows, so it never clips. */
    w_l0();
    w_ls("Script error in fn ");
    w_ln((unsigned)wvm_errfn());
    w_ls(".");
    os88_toast(w_line, 0);
}

/* ============================================================================
 * THE DIRTY SET AND ITS FLUSH
 * ==========================================================================*/

static unsigned char w_dirty[256];
static int           w_ndirty;
static int           w_cardpend;        /* app.go() asked for a full repaint */

static void w_touch(int id)
{
    if (id < 0 || id > 255 || w_dirty[id])
        return;
    w_dirty[id] = 1;
    w_ndirty++;
}

/* w_flush - repaint what changed, under ONE lock hold.
 *
 * The clip is armed here for w_paint_card's reason, said again: nothing arms
 * one around a package's own drawing, and a component at the bottom of a card
 * would otherwise draw its frame through the window border and across the
 * dock. WD_PAD is on (w_padnow), because what is under these is the LAST
 * state and not clean ground. */
static void w_flush(void)
{
    int i, clipped;

    if (w_state != W_ST_RUN)
        return;
    if (w_cardpend) {
        w_cardpend = 0;
        w_ndirty = 0;
        os88_memset(w_dirty, 0, sizeof(w_dirty));
        os88_gfx_lock();
        w_repaint2(w_win, 1);           /* 6.12: a card switch is ONE full
                                         * repaint, priced and shown as such */
        os88_gfx_unlock();
        return;
    }
    if (w_ndirty == 0)
        return;
    os88_gfx_lock();
    if (w_layout(w_win)) {
        clipped = os88_wm_clip_set(w_win) == 0;
        w_padnow = WD_PAD;
        for (i = 0; i < w_nlay; i++) {
            if (!w_dirty[w_lay[i].id])
                continue;
            if (w_cflag[w_lay[i].id] & CF_HIDDEN)
                w_wipe_one(i);          /* hiding does not reflow (7.2), so
                                         * the rect is simply given back to
                                         * the paper */
            else
                w_paint_comp(i);
        }
        w_padnow = 0;
        os88_gfx_pen(0);
        if (clipped)
            os88_wm_clip_clear();
    }
    os88_gfx_unlock();
    os88_memset(w_dirty, 0, sizeof(w_dirty));
    w_ndirty = 0;
}

/* ============================================================================
 * THE COLLECTOR'S EXTRA ROOTS (WEAVE-SPEC 4.8.1)
 * ==========================================================================*/

static void w_gcmark(void)
{
    int k;

    for (k = 0; k < 256; k++)
        if (w_ctext[k])
            wvm_mark(WT_STR, (int)w_ctext[k]);
    for (k = 0; k < w_nlset; k++)
        wvm_mark(WT_STR, (int)w_lseth[k]);
}

/* ============================================================================
 * THE SLICE MODEL (WEAVE-SPEC 4.10)
 * ==========================================================================*/

static int      w_budget = W_SLICE0;
static unsigned w_slicet;               /* the tick the slice began on */
static int      w_samet;                /* same-tick exhaustions in a row */
static unsigned w_startt;               /* ...and the tick the HANDLER began */
static int      w_waited;               /* 4.11: Wait re-armed the counter */
static int      w_alerting;             /* an alert of ours is up */

/* w_adapt - 4.10's two arms, and only EXHAUSTED slices are timed.
 * Doubled after four same-tick exhaustions, halved when a slice spanned two
 * ticks. The cap is 1,536 and the floor 128, and 4.10 carries the honest
 * arithmetic for both (51-154 ms at the contract, which is why the halving
 * arm exists at all). */
static void w_adapt(unsigned began)
{
    if (os88_ticks() != began) {
        w_budget >>= 1;
        if (w_budget < W_SLICEMIN)
            w_budget = W_SLICEMIN;
        w_samet = 0;
        return;
    }
    w_samet++;
    if (w_samet >= 4) {
        w_samet = 0;
        w_budget <<= 1;
        if (w_budget > W_SLICEMAX)
            w_budget = W_SLICEMAX;
    }
}

/* w_runaway - 4.11's alert, in the shape this machine's shared engine has.
 *
 * 4.11 asks for buttons `Stop` / `Wait`, and os88ui.inc's sets are fixed -
 * OK, Yes/No, Save/Discard/Cancel (SPEC.md 75.3) - so the question is asked
 * as a question: Yes stops, No waits. The sentence is 33 characters against
 * OS88UI_AMAX's 34. WEAVE-SPEC 4.11 is amended to say so rather than the
 * runtime quietly using different words from the ones the contract pins. */
static void w_runaway(void)
{
    if (w_alerting)
        return;
    os88_strcpy(w_amsg, "Script is still running. Stop it?", sizeof(w_amsg));
    os88_gfx_lock();
    if (wd_ask(w_amsg, w_win, WD_AYESNO) == 0) {
        w_alerting = 1;
        w_alertkind = 1;                /* ...the runaway one, not alert()'s */
    } else {
        w_waited = 1;                   /* refused (one is already up): wait,
                                         * which is the safe half of 4.11 */
        w_startt = os88_ticks();
    }
    os88_gfx_unlock();
}

/* w_step - one slice of the running handler.  Answers 1 when work remains. */
static int w_step(void)
{
    int r;
    unsigned began;

    if (!wvm_busy())
        return 0;
    began = os88_ticks();
    r = wvm_slice(w_budget);
    if (r == WR_MORE)
        w_adapt(began);
    w_flush();
    if (r == WR_GC) {
        w_gcmark();                     /* 4.8.1's roots, before 4.8's marks */
        wvm_gc();
        return 1;
    }
    if (r == WR_ERR) {
        w_scripterr();
        return 0;
    }
    if (r == WR_DONE || r == WR_IDLE)
        return 0;
    /* 4.11: a handler still unfinished after 90 ticks raises the alert. The
     * counter is on the HANDLER and not on the slice, which is what makes a
     * long-but-finite loop different from a hang. */
    if ((unsigned)(os88_ticks() - w_startt) > W_RUNAWAY && !w_alerting)
        w_runaway();
    return 1;
}

/* ============================================================================
 * DISPATCH (WEAVE-SPEC 4.9.1)
 * ==========================================================================*/

/* w_nargs - how many of the record's words are arguments (4.9.1's table).
 * Written as a switch and not an array because the atoms are sparse (48..60)
 * and a 13-entry table indexed by atom-48 is the same bytes with a subtraction
 * in front of it. */
static int w_nargs(int atom)
{
    switch (atom) {
    case WA_ONCLICK:  return 0;
    case WA_ONCHANGE: return 1;
    case 53:          return 1;         /* oncalc */
    case 57:          return 1;         /* ontick */
    default:          return 2;
    }
}

/* w_bound - the function bound to (comp, atom), or -1.
 * An event binding is an ordinary PROPS record (2.6): the event atom as the
 * name, PK_FUNC as the kind. So this is w_pfind and nothing more - which is
 * what interning the markup at pack time bought. */
static int w_bound(int comp, int atom)
{
    unsigned v, props;
    int i;

    if (comp == 0)
        return -1;
    i = w_find_lay(comp < 0 ? 0 : comp);
    if (i < 0)
        return -1;
    props = w_lay[i].props;
    v = w_pfind(props, (unsigned)atom);
    if (!w_pfound)
        return -1;
    return (int)v;
}

/* w_dispatch - take one record and start its handler.  Answers 1 when one
 * started, 0 when the record was discarded (4.9.1: no binding, no handler). */
static int w_dispatch(void)
{
    int fn, n;

    if (!wvm_deq(w_rec))
        return 0;
    if (w_rec[1] == WA_ONTIMER || w_rec[1] == WA_ONALERT) {
        fn = w_rec[2];                  /* 4.9.1: the function is data1 */
        if (w_rec[1] == WA_ONALERT)
            wvm_argi(w_rec[3]);
        if (wvm_begin(fn) == 0)
            return 0;
        w_startt = os88_ticks();
        return 1;
    }
    if (w_rec[1] == WA_ONCOMMAND) {
        fn = w_menu_fn(w_rec[2], w_rec[3]);
        if (fn < 0)
            return 0;
        wvm_argi(w_rec[2]);
        wvm_argi(w_rec[3]);
        if (wvm_begin(fn) == 0)
            return 0;
        w_startt = os88_ticks();
        return 1;
    }
    fn = w_bound(w_rec[0], w_rec[1]);
    if (fn < 0)
        return 0;                       /* no binding: the record is discarded */
    n = w_nargs(w_rec[1]);
    if (n > 0)
        wvm_argi(w_rec[2]);
    if (n > 1)
        wvm_argi(w_rec[3]);
    if (wvm_begin(fn) == 0)
        return 0;
    w_startt = os88_ticks();
    return 1;
}

/* ============================================================================
 * W_ONWAKE - the drain (SPEC.md 74.1, WEAVE-SPEC 4.10)
 * ==========================================================================*/

static void w_wake(void)
{
    int guard;

    w_wake_owed = 0;
    if (w_state != W_ST_RUN)
        return;
    /* One slice per wake, and at most a handful of empty dispatches: a record
     * with no binding costs nothing and there are at most 16 of them, so
     * draining them in one pass is cheaper than a wake each. */
    guard = 20;
    while (guard-- > 0) {
        if (wvm_busy()) {
            if (w_step())
                break;                  /* still going: re-post and yield */
            continue;                   /* finished: take the next record */
        }
        if (!w_dispatch())
            break;
    }
    w_flush();
    if (wvm_busy() || wvm_rcount() > 0)
        w_kick();                       /* 4.10: re-post ONLY while there is
                                         * something to do - an idle app costs
                                         * zero CPU */
}

/* ============================================================================
 * THE TIMER, MULTIPLEXED (WEAVE-SPEC 6.7's caret and 8.2's timer())
 *
 * OSAPI_WM_TIMER is ONE one-shot per window (SPEC.md 13.9) and WEAVE has two
 * customers for it, so the runtime owns it and serves whichever deadline is
 * nearer. Without that the caret and a script timer would each cancel the
 * other, silently, and which of the two worked would depend on the order the
 * app happened to arm them in.
 * ==========================================================================*/

static unsigned w_tcaret;               /* the next blink, in ticks */
static unsigned w_tscript;              /* ...and the script timer's deadline */
static int      w_tscript_on;
static int      w_tscript_fn;

static void w_arm(void)
{
    unsigned now, when;
    int d;

    if (w_state != W_ST_RUN || !w_tmrok)
        return;
    now = os88_ticks();
    when = 0;
    if (w_focus >= 0) {
        when = w_tcaret;
    }
    if (w_tscript_on && (when == 0 || (int)(w_tscript - when) < 0))
        when = w_tscript;
    if (when == 0)
        return;
    d = (int)(when - now);
    if (d < 1)
        d = 1;
    if (os88_wm_timer(w_win, d) != 0)
        w_tmrok = 0;                    /* 10.2: kern_small carries the slot
                                         * and not the body. The caret goes
                                         * static and timer() is inert, which
                                         * the load already said out loud */
}

static void w_ontimer_body(void *win)
{
    unsigned now;

    now = os88_ticks();
    if (w_focus >= 0 && (int)(now - w_tcaret) >= 0) {
        w_caret(win);
        w_tcaret = now + W_BLINK;
    }
    if (w_tscript_on && (int)(now - w_tscript) >= 0) {
        w_tscript_on = 0;
        w_enq(0, WA_ONTIMER, w_tscript_fn, 0);
    }
    w_arm();
}

/* ============================================================================
 * THE ALERT'S COMPLETION (wui.inc calls this)
 * ==========================================================================*/

static void w_alertdone(int button)
{
    int fn;

    w_alerting = 0;
    if (w_alertkind == 1) {             /* 4.11's runaway question */
        w_alertkind = 0;
        if (button == 0) {              /* Yes: STOP - stacks cleared, ring
                                         * preserved, globals as they are */
            wvm_abort();
            os88_toast("Script stopped.", 0);
        } else {
            w_startt = os88_ticks();    /* No: WAIT, re-armed for 90 more */
            w_waited = 1;
        }
        w_kick();
        return;
    }
    fn = w_alertfn;                     /* 8.2: the callback arrives AFTER the
                                         * dismissal, as an onalert event */
    w_alertfn = -1;
    if (fn >= 0)
        w_enq(0, WA_ONALERT, fn, button < 0 ? 0 : 1);
    w_kick();
}
