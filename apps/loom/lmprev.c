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
 * THE THREE THINGS IT IS, AND THE ONE IT IS NOT YET
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
 * WHAT IT DOES NOT DO IN THIS WAVE IS DRAW THE COMPONENTS, and the reason is
 * written here rather than left as a gap:
 *
 *   THE FLOW WALK IS SHARED, NEVER COPIED (WEAVE-SPEC 1.2: "When LOOM lands
 *   (wave 6) it takes the same walk - moved to a shared `.inc`, or called
 *   through one - and never a second copy"). apps/weave/wflow.c is C and can
 *   be #included from here; what it NAMES is the difficulty. It reads
 *   fourteen of WEAVE's own globals (w_seg, w_soff[], w_sextra[], w_lay[],
 *   w_state, w_entry, w_cw/w_ch/w_ox/w_oy, w_org, w_sz, w_atom_off,
 *   w_atom_len) and calls w_draw_run in apps/weave/wdraw.inc, and the PAINTER
 *   that turns its w_lay[] table into pixels is apps/weave/wpaint.c - 880
 *   lines that additionally name the component value arrays, the field pool,
 *   the grid's cell store and the canvas module.
 *
 *   Standing all of that up in LOOM means either #including wpaint.c and its
 *   dependencies, or WRITING A SECOND, SMALLER PAINTER - and the second is
 *   exactly what 1.2's "never a second copy" rule forbids, for the reason
 *   1.2 gives: two layouts that must agree cell-for-cell is the failure
 *   WEAVE-SPEC 11's byte-identity rule exists to prevent, said about code.
 *
 *   So this wave ships the plumbing, the claim discipline, the label 1.7
 *   pins, and the refusal - and the shared walk is wired in when the resident
 *   size line can take the shared painter with it. The arithmetic is in this
 *   wave's report and the seam is lm_prev_paint() below, which is the ONE
 *   function that changes.
 *
 * WHAT IT DOES SHOW, so that the pane is not a stub with a sentence in it:
 * the staged bundle's SIZE, its component count and its card count, all read
 * back out of the image ovl_pack() just wrote (WEAVE-SPEC 2.2, 2.5). Those
 * are facts about the thing that will run, they come from the real packer,
 * and they are the half of "what will this look like" that does not need a
 * painter.
 * ==========================================================================*/

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

/* lm_prev_paint - THE SEAM (see this file's header).
 *
 * Today: the label WEAVE-SPEC 1.7 pins, and what the pack produced. Tomorrow:
 * the shared flow walk over the staged image and the shared cores over its
 * w_lay[] table, with everything below the label replaced and nothing above
 * it touched.
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
                                         * not a source file - and so a
                                         * Preview repaint that changes
                                         * nothing costs nothing */
}

static void lm_prev_paint(void)
{
    int r;

    lm_ed_caret_off();
    lm_prev_row(0, "Preview: layout and controls only - Run runs the app");
    lm_prev_row(1, "");
    if (lm_packlen == 0) {
        lm_prev_row(2, "The project compiled to an empty bundle.");
        lm_prev_row(3, "");
        lm_prev_row(4, "LOOM's script and sheet compilers land in this same");
        lm_prev_row(5, "wave; until they do there is no UI stream to walk and");
        lm_prev_row(6, "nothing to draw. Pack Bundle reports the same thing.");
        r = 7;
    } else {
        lm_l0();
        lm_ls("Bundle staged: ");
        lm_ln((int) lm_packlen);
        lm_ls(" bytes, ");
        lm_ln(lm_ncomp);
        lm_ls(" components on ");
        lm_ln(lm_ncard);
        lm_ls(" card(s).");
        lm_prev_row(2, lm_line);
        lm_prev_row(3, "");
        lm_prev_row(4, "The components are drawn by the flow walk and the");
        lm_prev_row(5, "paint cores WEAVE shares with this program, which");
        lm_prev_row(6, "arrive with the painter (WEAVE-SPEC 1.2, 1.7).");
        r = 7;
    }
    while (r < lm_erows) {
        lm_prev_row(r, "");
        r++;
    }
}
