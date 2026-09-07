/* ============================================================================
 * os8088 - apps/apple2/a2about.c     the About panel - OUT OF LINE
 *
 * Part of APPLE2 (docs/APPLE2-SPEC.md section 11). #included into
 * apps/apple2/apple2.c - ONE translation unit (SPEC.md 73.1).
 * apps/apple2/ is GPL-2-or-later; see apps/apple2/COPYING. The panel's SHAPE
 * is MII's `ui_gl/mii_mui_about.c:96-140` - a title row, "The <machine>
 * Emulator", the version, the copyright and a "Thanks to:" block - and its
 * MECHANICS are the C64's.
 *
 * `ovl_*`: the panel is drawn once per opening. Its CLOSE and its hit test
 * stay RESIDENT, because a click is a callback and a callback is reached by a
 * near offset.
 *
 * BOTH BODIES ARE `ovl_` AND NOT ONLY THE SHELLS. ovl_about_geom and
 * ovl_about_draw are ~330 lines of emitted assembly between them and are
 * called from ovl_about_show and ovl_about_paint and from nowhere else, so
 * naming them a2_* left them RESIDENT with nothing keeping them there. Their
 * strings and statics stay resident and DS-relative either way, which is what
 * section 15.2's overlay term already accounts for.
 *
 * ----------------------------------------------------------------------------
 * WHAT IT SAYS AND WHAT IT DOES NOT (section 11, LESSONS.md 8)
 * ----------------------------------------------------------------------------
 * The product, the version, what this PORT is, the four attributions with
 * their licences, and whose the ROMs are. **NOTHING ABOUT HOW THE BUILD
 * RENDERS.** That the window is monochrome is a fact about the build and it
 * lives on its Video grey (section 10.3); that the speaker is a toggle
 * estimator lives on the status row (section 9). Draft-only notes were
 * removed from CWORD's panel for exactly this reason.
 *
 * TWELVE ROWS IS A 640x200 COMPATIBILITY CONSTANT and it carries this comment
 * saying so: a control's y is `6 + row * 10` and nothing clamps it, so a
 * 19-row panel puts its OK button on the DESKTOP on CGA and Hercules. This
 * one is ten rows and its bottom sits at 6 + 10*10 + 16 = 122 against the
 * ~122 a framed CGA content box has - which is why it is TEN and not twelve,
 * and why adding a row here is a decision and not an edit.
 *
 * THE `][+` GLYPH IS SPELLED "II Plus", because the kernel face is ASCII
 * 32..126 and the glyph draws NOTHING here (LESSONS.md 8's check-mark trap,
 * one machine along).
 *
 * WAVE 7 FIXES THE EXACT ROW TEXT against the authority row's file and checks
 * the product name across all four surfaces; the row CONTENT is the list in
 * section 11 and nothing else, and it is already that.
 * ==========================================================================*/

#define A2_ABT_ROWS 10
#define A2_ABT_LH   10                      /* the line pitch */
#define A2_ABT_PADY 6
#define A2_ABT_H    (A2_ABT_PADY + A2_ABT_ROWS * A2_ABT_LH + 16)

/* TEN ROWS, AND EVERY ONE OF THEM IS SECTION 11'S LIST. `APPLE2 for os8088`
 * stood in row 2 as the version and is not one - it is the product name
 * again, one line down - so the row carries a REAL version, on
 * apps/c64/c64about.c:46-49's shape: a version row, a `GPL-2 or later - see
 * COPYING` row (this package is GPL-2-or-later and the panel is where a
 * reader is told so), and the porter row that panel also carries.
 *
 * THE BUDGET IS TEN AND IT DID NOT MOVE. Three rows arrived and two of the
 * three blank spacers left: A2_ABT_H is a 640x200 compatibility constant
 * (this file's header) and adding a row is a decision, not an edit. */
static const char *a2_abt_text[A2_ABT_ROWS] = {
    "The Apple II Plus Emulator",           /* MII's model row, section 16.1's
                                             * LONG product name */
    "APPLE2 1.0 for os8088",                /* the VERSION row */
    "A 48K Apple II Plus, Applesoft in ROM", /* what this PORT is - the
                                              * MACHINE, not the rendering */
    "",
    "6502 core from VICE 3.10, GPL-2+",     /* the four attributions, with */
    "Apple II+ from AppleWin, GPL-2+",      /* their licences (section 1.3).
                                             * AppleWin's own headers say
                                             * "either version 2 ... or (at
                                             * your option) any later
                                             * version", so it is GPL-2+ and
                                             * not GPL-2 */
    "MII and apple2emu, MIT",
    "GPL-2 or later - see COPYING",         /* ...and what THIS package is,
                                             * which the licence requires the
                                             * distributed form to say */
    "ROMs (C) Apple Computer, Inc.",        /* whose the ROMs are */
    "Ported by Jorge Gonzalez"
};

/* a2_abt_up, a2_abt_x/y/w/h, a2_hold_l0 and a2_hold_l1 are all declared in
 * apple2.c above every #include, because a2scr.c's flush and os88_paint READ
 * them and this file WRITES them, and one definition is what keeps the two
 * from drifting. */

/* ovl_about_geom - measure and place the panel, and work out which APPLE SCAN
 * LINES it covers so that the flush can skip them entirely.
 *
 * THE PANEL IS AS WIDE AS THE BAND AND THAT IS A REDRAW DECISION rather than
 * a taste: at a narrower width it leaves a strip of Apple screen down each
 * side, so an expose while the panel is up has to compose and blit every line
 * the panel covers - all forty bytes of each - and then paint the panel over
 * the middle of what it just drew. That is PERFORMANCE.md's erase-then-letter
 * pair, and it is invisible in any screendump. At the band's own width "the
 * lines the panel covers" is exact, and nothing under it is drawn at all. */
static int ovl_about_geom(void)
{
    int y;

    a2_abt_w = A2_BANDW;
    if (a2_abt_w > a2_gw)
        a2_abt_w = a2_gw;
    a2_abt_h = A2_ABT_H;
    if (a2_abt_h > a2_gh)
        a2_abt_h = a2_gh;
    a2_abt_x = a2_gox + (a2_gw - a2_abt_w) / 2;
    a2_abt_y = a2_goy + (a2_gh - A2_STATH - a2_abt_h) / 2;
    if (a2_abt_y < a2_goy)
        a2_abt_y = a2_goy;

    /* the Apple scan lines it covers, in the shadow's own coordinates */
    y = a2_abt_y - a2_gsy + a2_gl0;
    a2_hold_l0 = (y < 0) ? 0 : y;
    y = a2_abt_y + a2_abt_h - 1 - a2_gsy + a2_gl0;
    a2_hold_l1 = (y >= A2_SCRH) ? A2_SCRH - 1 : y;
    if (a2_hold_l1 < a2_hold_l0)
        a2_hold_l1 = a2_hold_l0;
    return 1;
}

static int ovl_about_draw(void)
{
    int i, y, w, x, bx, by;

    os88_set_color(OS88_WHITE);
    os88_gfx_fill(a2_abt_x, a2_abt_y,
                  a2_abt_x + a2_abt_w - 1, a2_abt_y + a2_abt_h - 1);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(a2_abt_x, a2_abt_y,
                   a2_abt_x + a2_abt_w - 1, a2_abt_y + a2_abt_h - 1);
    for (i = 0; i < A2_ABT_ROWS; i++) {
        if (a2_abt_text[i][0] == 0)
            continue;                       /* an empty row is a blank line
                                             * and not a call */
        w = (int)os88_strlen(a2_abt_text[i]) * 8;
        x = a2_abt_x + (a2_abt_w - w) / 2;
        y = a2_abt_y + A2_ABT_PADY + i * A2_ABT_LH;
        if (y + 8 > a2_abt_y + a2_abt_h - 1)
            break;                          /* the panel was clamped to a
                                             * short content box: a row that
                                             * would fall outside it is not
                                             * drawn outside it */
        /* ONE CALL A ROW, ink over paper, so no pixel is written twice - the
         * fill above is the ground the frame stands on and nothing else
         * (PERFORMANCE.md rule 2). */
        os88_font_run(x, y, a2_abt_text[i], OS88_BLACK, OS88_WHITE);
#ifdef A2_HOST
        a2_n_run++;
        a2_n_cell += os88_strlen(a2_abt_text[i]);
#endif
    }

    /* THE OK BUTTON, IN THE SIXTEEN PIXELS A2_ABT_H ALREADY RESERVES FOR IT.
     * MII's own About has a real MUI_BUTTON_STYLE_DEFAULT OK
     * (mii_mui_about.c) and c64about.c draws one with the same clamp; without
     * it the reserved strip is blank and the panel offers no affordance at
     * all, though any click or key closes it.
     *
     * THE CLAMP IS THE POINT (LESSONS.md 8 - a 19-row About box put its OK on
     * the DESKTOP): a2_about_geom clamps a2_abt_h to a short content box, so
     * the button is placed against the panel's own foot when the rows have
     * eaten the space. */
    bx = a2_abt_x + (a2_abt_w - 48) / 2;
    by = a2_abt_y + A2_ABT_PADY + A2_ABT_ROWS * A2_ABT_LH;
    if (by + 13 > a2_abt_y + a2_abt_h - 1)
        by = a2_abt_y + a2_abt_h - 15;
    if (by > a2_abt_y) {
        os88_gfx_frame(bx, by, bx + 47, by + 13);
        os88_font_run(bx + 16, by + 3, "OK", OS88_BLACK, OS88_WHITE);
#ifdef A2_HOST
        a2_n_run++;
        a2_n_cell += 2;
#endif
    }
    return 1;
}

/* ovl_about_show - from os88_about(), which arrives UNDER the gfx lock and
 * with NO CLIP REGION ARMED (SPEC.md 11.3): the kernel arms one for a W_PAINT
 * and for nothing else, so a card drawn without one lands on top of whatever
 * window is covering ours. */
static int ovl_about_show(void *win)
{
    if (a2_geom(win) < 0)
        return 0;
    if (!ovl_about_geom())
        return 0;
    if (os88_wm_clip_set(win) < 0)
        return 0;                           /* not one pixel of us is visible */
    a2_abt_up = 1;
    if (!ovl_about_draw()) {
        a2_abt_up = 0;
        return 0;
    }
    return 1;
}

/* ovl_about_paint - the same panel from os88_paint(), where the kernel's own
 * region is already armed and re-arming would throw that paint's damage rect
 * away and redraw the whole card for a two-pixel repair. */
static int ovl_about_paint(void *win)
{
    (void)win;
    if (!ovl_about_geom())
        return 0;
    return ovl_about_draw();
}

/* a2_about_close - RESIDENT, because a click is a callback (this file's
 * header).
 *
 * THE CLOSE IS DRAWN AS DAMAGE AND NOT AS A REPAINT: exactly the scan lines
 * the panel held are forced, and the flush draws them. A full repaint here
 * would be ~200 ms on the target for a rectangle 122 pixels tall. */
static void a2_about_close(void *win)
{
    /* THE THREE STATEMENTS ARE a2_about_gone's (a2scr.c), because every route
     * out of the panel owes exactly them: the latch down, the hold range
     * emptied (l0 > l1, which is what the flush tests) and the panel's rect
     * handed on as damage.
     *
     * THE BORDER AND THE STATUS ROW ARE TESTED, NOT ASSUMED. Both used to be
     * invalidated unconditionally with a comment stating a CONDITION as if it
     * were a fact - "the panel covered the status row's left half on a short
     * window" - which cost 42 glyph cells, 37.8 ms, on every dismissal at the
     * shipping geometry, where the panel's foot (goy+165) is 43 pixels clear
     * of the row (goy+208). a2_blank_rect already asks exactly these two
     * questions of a screen rect, so this is one call rather than a second
     * copy of the test - and it forces the band lines the panel held, which
     * is what the loop above was doing by hand. On a 200-line CGA box the
     * panel really does reach the top border strip, and there it still says
     * so. */
    a2_about_gone();
    a2_kick = 1;
    os88_wm_wake(win);
}
