/* ============================================================================
 * os8088 - apps/loom/lmprev.c
 *
 * PREVIEW (WEAVE-SPEC 1.7). #included by apps/loom/loom.c after lmed.c.
 *
 * 1.7, in full: "Loom additionally offers Preview - the compiled UI stream
 * rendered in a child area by the SAME shared component includes WEAVE paints
 * with; widgets draw and arm/fire natively but no bytecode runs, and the pane
 * is labelled `Preview: layout and controls only - Run runs the app`."
 *
 * ---------------------------------------------------------------------------
 * THE FOUR THINGS IT IS
 * ---------------------------------------------------------------------------
 * 1. IT PACKS. Preview is a Pack that does not touch the disk: the same
 *    ovl_pack() File > Pack Bundle runs, into the same transient output claim
 *    (WEAVE-SPEC 11.4), and the same refusals with the same sentences. That
 *    is not a shortcut - it is the only way the preview can be of the thing
 *    that will run, which is what a preview means.
 *
 * 2. IT KEEPS THE CLAIM while the pane is up, and gives it back the moment
 *    the pane goes down. The pack claims are transient by 11.4 and this is
 *    the one path that holds one across a callback boundary; it is why
 *    lm_prev_off() exists and why every exit from the pane goes through it.
 *
 * 3. IT REFUSES HONESTLY. A project that will not compile has no UI stream to
 *    walk, so Preview shows the pack error - the same `<file>:<line>:
 *    <message>` the sidebar shows - rather than an empty box.
 *
 * 4. AND IT DRAWS THE CARD (wave 7). The flow walk and the component painter
 *    are WEAVE's own - apps/weave/wflow.c and apps/weave/wpaint.c, the same
 *    text, compiled a second time into LOOM.WPV, a SECOND RESIDENT SEGMENT
 *    (WEAVE-SPEC 1.2.4). Not a second painter: WEAVE-SPEC 1.2 forbids one by
 *    name, and apps/loom/lmpvmod.c is the whole of what wave 7 wrote around
 *    the two shared bodies.
 *
 * ---------------------------------------------------------------------------
 * WHY A SEGMENT AND NOT AN OVERLAY, IN ONE PARAGRAPH (WEAVE-SPEC 1.7.1)
 * ---------------------------------------------------------------------------
 * SPEC.md 73.14's overlay moves CODE and leaves "every global, literal and
 * bss byte it names resident and DS-relative". What does not fit in LOOM is
 * the DATA: the walk's 2,500-byte layout table and the painter's six tables
 * keyed by comp_id come to ~4.7KB, against the 594 bytes wave 6 closed under
 * SPEC.md 20.1's ceiling with. An overlay cannot move one byte of that, so it
 * is not that the tenant list is full - it is that the instrument does not
 * cut in this direction. A second segment has a DS of its own and costs this
 * image one dword in apps/loom/lmpv.inc, a claim while the program lives, and
 * nothing at all until somebody opens the pane. The module measured 16,174
 * bytes of image and 5,268 of bss when wave 7 shipped it.
 *
 * ---------------------------------------------------------------------------
 * THE LABEL MOVED TO THE STATUS ROW, and 1.7 records the amendment
 * ---------------------------------------------------------------------------
 * Wave 6's pane had six rows of prose in it and the label was the first of
 * them. The pane now holds a picture, so the label goes where LOOM's other
 * sentences go - the status row - and costs the card no cells. It is put
 * there when the pane goes up and by every repaint of the pane, because a
 * status row is one line and anything else that writes it wins.
 * ==========================================================================*/

/* ============================================================================
 * THE MODULE (WEAVE-SPEC 1.2.4)
 *
 * apps/weave/wpvabi.inc is the contract, apps/loom/lmpv.inc the five-routine
 * seam, apps/weave/wpvabi.h the C copy of the numbers - guarded in both
 * assemblies. The lifecycle is WEAVE.WSM's (1.2.2) verbatim: read ONCE, on
 * demand, stamped four ways before it is believed, and RESIDENT from that
 * moment until the instance closes. There is no unload and no refusal after
 * the first success, which is what makes it safe to far-call from inside a
 * W_PAINT.
 * ==========================================================================*/

static unsigned lm_pvseg;               /* LOOM.WPV's claim, 0 = not loaded */
static int      lm_pvparm[WPVP_NW];     /* WPVV_PAINT's block, in OUR DS */
static int      lm_pverr;               /* the last load's refusal, LM_PVE_* */

#define LM_PVE_OK    0
#define LM_PVE_MEM   1                  /* no room for the claim */
#define LM_PVE_GONE  2                  /* not on this disk / short read */
#define LM_PVE_STALE 3                  /* magic, ABI, size or bss disagrees */

/* lm_pv_need - the module, loaded once.  1 = bound, 0 = lm_pverr says why.
 *
 * IT IS CALLED BEFORE THE PACK CLAIMS AND NOT INSIDE THE PAINT, and both
 * halves of that are deliberate. Before the claims, because a machine that
 * cannot host a 21KB module should say so rather than after it has taken
 * 112KB it is about to give back (WEAVE-SPEC 11.4). Not inside the paint,
 * because a claim and a floppy read under the gfx lock is seconds of held
 * lock on the target machine - so by the time W_PAINT far-calls the module,
 * the module is either resident or known to be absent. */
static int lm_pv_need(void)
{
    unsigned kb, got;
    int k;

    if (lm_pvseg)
        return 1;
    lm_pverr = LM_PVE_OK;
    kb = (unsigned) lpv_kb();
    lm_pvseg = os88_mem_claim((int) kb);
    if (lm_pvseg == 0) {
        lm_pverr = LM_PVE_MEM;
        return 0;
    }
    got = os88_file_read_seg("LOOM.WPV", lm_pvseg, kb << 10);
    if (got < lpv_bytes()) {
        os88_mem_free(lm_pvseg);        /* a half-believed module is worse
                                         * than none (crt0.asm's rule) */
        lm_pvseg = 0;
        lm_pverr = LM_PVE_GONE;
        return 0;
    }
    k = lpv_stamp(lm_pvseg);            /* the four words, in the assembly
                                         * that owns their constants */
    if (k) {
        os88_mem_free(lm_pvseg);
        lm_pvseg = 0;
        lm_pverr = k == 1 ? LM_PVE_GONE : LM_PVE_STALE;
        return 0;
    }
    lpv_bindmod(lm_pvseg);
    return 1;
}

/* lm_pv_why - the sentence, in WEAVE-SPEC 10.3's shape and NAMING THE FILE.
 *
 * 10.3: "a disk somebody has taken the package off without its modules is the
 * case, it is paid once and visibly". `make loomdisk` puts LOOM.O88, LOOM.OVL
 * and LOOM.WPV in one folder for exactly this reason. */
static const char *lm_pv_why(void)
{
    if (lm_pverr == LM_PVE_MEM)
        return "Not enough memory for LOOM.WPV.";
    if (lm_pverr == LM_PVE_STALE)
        return "LOOM.WPV does not match this program.";
    return "LOOM.WPV is not on this disk.";
}

/* lm_prev_off - give the output claim back and go back to editing. Every exit
 * from the pane comes through here, including the close guard's, because a
 * 62KB claim held past the pane is 62KB the next Pack cannot have. */
static void lm_prev_off(void)
{
    lm_pack_free();
    if (lm_state == LM_ST_PREVIEW)
        lm_state = LM_ST_EDIT;
    lm_ed_invalidate();                 /* the pane held something else */
    lm_menusync();
}

/* lm_prev_on - pack into the claim and put the pane up. The claim is NOT
 * freed on success: the pane owns it until lm_prev_off(). */
static void lm_prev_on(void)
{
    int ok;

    lm_saveall();                       /* the same reason Pack saves first
                                         * (WEAVE-SPEC 11.3, and lmproj.c's
                                         * lm_pack says it at length) */
    if (lm_anymod())
        return;                         /* the save refused and said why */
    if (!lm_pv_need()) {
        lm_say(lm_pv_why());            /* 10.3's sentence, naming the module
                                         * and not the project. The pane does
                                         * NOT go up: there is nothing it
                                         * could draw, and a Preview with a
                                         * refusal in it is what wave 6 had */
        return;
    }
    if (!lm_pack_claim())
        return;
    lm_clearerr();
    ok = ovl_pack();
    if (!ok && !lm_failed()) {
        lm_pack_free();
        lm_say("LOOM.OVL is missing or stale; Preview cannot compile.");
        return;
    }
    if (!ok) {
        /* The pane still goes up: WEAVE-SPEC 11.3 wants the caret on the
         * first error, and a Preview that silently did nothing would be a
         * command with no visible effect. The claim goes back - there is
         * nothing in it. */
        lm_pack_free();
        lm_say(lm_errtext());
        if (lm_errslot() >= 0 && lm_errslot() < LM_NSLOT
            && lm_shave[lm_errslot()]) {
            lm_switch(lm_errslot());
            lm_ed_goline(lm_errline() - 1);
        }
        return;
    }
    lm_packlen = lm_outlen();
    lm_state = LM_ST_PREVIEW;
    lm_ed_invalidate();
    lm_menusync();
}

static void lm_prev_toggle(void)
{
    if (lm_state == LM_ST_PREVIEW)
        lm_prev_off();
    else
        lm_prev_on();
}

/* lm_prev_row - one row of PROSE in the pane, for the paths that have no
 * picture to draw: a bundle the module will not read, and a module that is
 * not on the disk. It is the wave-6 body, kept for exactly those two cases.
 *
 * Every line is ONE os88_font_run() with its own padding, which is the erase
 * (SPEC.md 6.1): the pane held the editor a moment ago and a shorter line
 * would leave the tail of a longer one on the glass. The pen is lm_ex, a
 * multiple of 8, so each takes font_run's single-store fast path on both 1bpp
 * adapters. */
static void lm_prev_row(int r, const char *s)
{
    int i, n;

    n = (int) os88_strlen(s);
    if (n > lm_ecols)
        n = lm_ecols;
    for (i = 0; i < n; i++)
        lm_rb[i] = s[i];
    lm_putrow(r, n);                    /* THE SAME DOOR THE EDITOR DRAWS
                                         * THROUGH, so the shadow tells the
                                         * truth about the glass even when the
                                         * pane is showing something that is
                                         * not a source file */
}

static void lm_prev_note(const char *s)
{
    int r;

    lm_prev_row(0, s);
    r = 1;
    while (r < lm_erows) {
        lm_prev_row(r, "");
        r++;
    }
}

/* lm_prev_paint - THE PICTURE (WEAVE-SPEC 1.7, 1.2.4).
 *
 * One far call. Everything it draws is drawn by apps/weave/wflow.c and
 * apps/weave/wpaint.c through apps/weave/wdraw.inc, which is the same text
 * WEAVE.O88 runs - so `weavesim --render --preview` predicts this pane cell
 * for cell and tests/weaveprev.py asserts that it does.
 *
 * THE GROUND IS ALREADY CLEAN and nothing here whitens it: lm_repaint() is
 * the only way in, W_PAINT arrives with the content whitened by the kernel,
 * and every other caller has just called lm_wipe(). A fill here would be the
 * erase-then-draw double-draw over the whole pane, which is one of the three
 * defects an emulator cannot show (CLAUDE.md).
 *
 * THE SHADOW IS NOT USED and must not be trusted afterwards. lm_putrow()'s
 * per-cell shadow is a claim about a row of TEXT, and the module draws
 * controls; lm_repaint() invalidates both shadows on every entry, which is
 * what makes that safe (loom.c says so at length). The three refusal paths
 * DO go through lm_putrow(), because they are text and the pane may hold text
 * one repaint and a card the next.
 *
 * EVERY REFUSAL IS ONE LINE, and that is a size decision with the arithmetic
 * attached: a string literal is a RESIDENT byte (SPEC.md 73.14 - only code
 * moves into an overlay), and this package closed wave 7 with 366 bytes under
 * SPEC.md 20.1's ceiling. Wave 6's pane carried six rows of prose because it
 * had nothing else to show; the pane now has a picture, and the sentences
 * that remain are the three cases where there is not one.
 *
 * WHAT IT COSTS ON THE TARGET. A card's first paint is WEAVE-SPEC 14's own
 * row: ~1.25 s fully lettered on CGA, ~2.59 s on VGA, ~2.85 s on Hercules -
 * and a Preview is a repaint of exactly that, so a pane that fills the window
 * costs the same seconds WEAVE's own first card does. It is a MENU COMMAND
 * and it happens once per toggle, which is what makes that affordable; a
 * Preview repainted per keystroke would not be, and nothing repaints it per
 * keystroke because the pane is not the editor. */
static void lm_prev_paint(void)
{
    int ok;

    lm_ed_caret_off();
    lm_say("Preview: layout and controls only - Run runs the app");

    if (lm_pvseg == 0) {
        lm_prev_note(lm_pv_why());
        return;
    }
    if (lm_packlen == 0) {
        lm_prev_note("The project compiled to an empty bundle.");
        return;
    }

    lm_pvparm[WPVP_X >> 1] = lm_ex;
    lm_pvparm[WPVP_Y >> 1] = lm_oy;
    lm_pvparm[WPVP_W >> 1] = lm_ecols * 8;
    lm_pvparm[WPVP_H >> 1] = lm_erows * 8;
    lm_pvparm[WPVP_CARD >> 1] = 0;      /* 2.2's entry card. A card switcher
                                         * is a later wave's; the module takes
                                         * an index so that it can be one */

    ok = (int) lpv_call(WPVV_PAINT, lm_outseg, (unsigned) lm_pvparm,
                        (unsigned) lm_win);
    if (ok == 1)
        return;

    /* 0 with a WPVE_* in the high byte, or a bare 0 for "no module bound" -
     * which cannot happen here, because lm_pvseg is non-zero. */
    if ((ok >> 8) == WPVE_PANE)
        lm_prev_note("The pane is too small to lay this card out.");
    else
        lm_prev_note("LOOM.WPV cannot read the staged bundle.");
}
