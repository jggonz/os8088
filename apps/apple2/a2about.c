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

/* ...AND MACHINE > POWER ON'S CONFIRMATION IS THE SAME PANEL WITH A KIND
 * (section 10.2, apple2.c's A2_PAN_*). AppleWin's ConfirmReboot
 * (source/Windows/WinFrame.cpp:1997-2013) is MB_ICONWARNING|MB_YESNO titled
 * `Reboot`, and its first two lines are these two verbatim. The rest of that
 * box is about a `Confirm reboot` checkbox in a Configuration dialog this
 * port does not have, so it is not transcribed: a confirmation that points at
 * a control the reader cannot reach is the guess SPEC.md 47 forbids.
 *
 * THREE ROWS, AND THE ROW COUNT IS A 640x200 COMPATIBILITY CONSTANT for this
 * file's own reason - a control's y is `A2_ABT_PADY + row * A2_ABT_LH` and
 * nothing clamps it, so adding a row here is a decision and not an edit. At
 * three the panel is 52 pixels tall against the ~122 a framed CGA content box
 * has, with 70 to spare. */
#define A2_CFM_ROWS 3
#define A2_CFM_H    (A2_ABT_PADY + A2_CFM_ROWS * A2_ABT_LH + 16)

static const char *a2_cfm_text[2] = {
    "Are you sure you want to reboot?",     /* WinFrame.cpp:2003 */
    "(All data will be lost!)"              /* WinFrame.cpp:2004 */
};
static const char *a2_cfm_btn[2] = { "Yes", "No" };  /* MB_YESNO, in MB_YESNO's
                                                      * own order: Yes is
                                                      * button 0 and is where
                                                      * a2_cfm_bx[0] is */

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
    "Apple II Plus Emulator",               /* AppleWin source/Common.h:50,
                                             * TITLE_APPLE_2_PLUS - section
                                             * 16.1's LONG product name, and
                                             * the window title's own string
                                             * (apple2.c's a2_title).
                                             *
                                             * THE ARTICLE IS GONE AND THE
                                             * CITATION WAS WRONG. This row
                                             * said `The Apple II Plus
                                             * Emulator` and cited "MII's
                                             * model row"; MII contains no
                                             * such string anywhere, and the
                                             * definer is the #define above -
                                             * which every other place in this
                                             * package already quotes without
                                             * the article. So the one row
                                             * that names the product was the
                                             * one place the name was not the
                                             * product's, in a package whose
                                             * stated discipline is that no
                                             * string is typed from memory. */
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
 * lines the panel covers" is exact, and nothing under it is drawn at all.
 *
 * ...AND "THE BAND'S WIDTH" IS a2_gbw AND NOT A2_BANDW, which is the half of
 * that sentence wave 3's magnification broke. At 2x the band on the glass is
 * 640 pixels wide and a 320-wide panel leaves 160 pixels of LIVE Apple
 * picture down each side - and a2scr.c's hold is per ROW, so those strips
 * freeze instead. It is placed AT a2_gsx rather than centred in the content
 * box, so "as wide as the band" and "over the band" are one statement.
 *
 * THE SCREEN-TO-APPLE-LINE CONVERSION IS DIVIDED BY THE MAGNIFICATION, at the
 * second and last place the two coordinate systems cross (a2_blank_rect is
 * the first, and section 7.8 is the rule: the doubling is at BLIT time). This
 * is what the wave shipped wrong: without the divide, the panel at VGA
 * fullscreen held Apple lines [131,191] while covering [66,125] - two
 * DISJOINT ranges, so every line under the card was recomposed and blitted
 * straight over it on the next flush and the bottom third of the picture
 * froze.
 *
 * IT HOLDS ONLY LINES THE PANEL COVERS **WHOLLY**. At 2x an Apple line is two
 * screen rows, so the panel's edge can fall inside one: `ceil` at the top and
 * `floor` at the bottom, which at 1:1 is exactly what this always did. A
 * held line that is not covered would freeze a sliver; an unheld covered one
 * is drawn over the card. The half-line at each edge is drawn as damage when
 * the panel goes, like everything else the panel held. */
static int ovl_about_geom(void)
{
    int t;

    a2_abt_w = a2_gbw;
    if (a2_abt_w > a2_gw)
        a2_abt_w = a2_gw;
    a2_abt_h = (a2_pan_kind == A2_PAN_CFM) ? A2_CFM_H : A2_ABT_H;
    if (a2_abt_h > a2_gh)
        a2_abt_h = a2_gh;
    a2_abt_x = a2_gsx;
    if (a2_abt_x + a2_abt_w > a2_gox + a2_gw)
        a2_abt_x = a2_gox + a2_gw - a2_abt_w;
    if (a2_abt_x < a2_gox)
        a2_abt_x = a2_gox;
    a2_abt_y = a2_goy + (a2_gh - A2_STATH - a2_abt_h) / 2;
    if (a2_abt_y < a2_goy)
        a2_abt_y = a2_goy;
    /* ...AND AT 2x IT IS SNAPPED TO THE APPLE LINE GRID, top and height both,
     * so that "the lines it covers" and "the lines it covers WHOLLY" are the
     * same set. Without it the card's last screen row is half of an Apple
     * line the hold does not contain: that line is drawn on every flush, over
     * the card's own bottom edge, which is the very double-draw the hold
     * exists to stop - and a2uitest's audit sees it as panel pixels where the
     * shadow says picture. It moves the panel by at most one screen pixel. */
    if (a2_sch == 2) {
        if (a2_abt_y > a2_gsy)
            a2_abt_y -= (a2_abt_y - a2_gsy) & 1;
        a2_abt_h &= ~1;
    }

    /* the Apple scan lines it covers WHOLLY, in the shadow's own coordinates */
    t = a2_abt_y - a2_gsy;
    if (t < 0)
        t = 0;
    if (a2_sch == 2)
        a2_hold_l0 = a2_gl0 + ((t + 1) >> 1);
    else
        a2_hold_l0 = a2_gl0 + t;
    t = a2_abt_y + a2_abt_h - a2_gsy;
    if (t < 0)
        t = 0;
    if (a2_sch == 2)
        a2_hold_l1 = a2_gl0 + (t >> 1) - 1;
    else
        a2_hold_l1 = a2_gl0 + t - 1;
    if (a2_hold_l1 > A2_SCRH - 1)
        a2_hold_l1 = A2_SCRH - 1;
    /* l0 > l1 is the EMPTY range the flush and a2_about_gone already speak:
     * a panel that covers no whole Apple line holds none. */
    return 1;
}

/* ovl_about_draw - the panel on the glass, EITHER KIND.
 *
 * The frame, the ground, the centred rows and the clamp are one piece of code
 * with two texts and two button rows, which is what "a kind rather than a
 * second panel" buys: a change to the clamp cannot fix one panel and leave
 * the other broken. */
static int ovl_about_draw(void)
{
    int i, y, w, x, bx, by, nrows, nbtn;
    const char *row;

    os88_set_color(OS88_WHITE);
    os88_gfx_fill(a2_abt_x, a2_abt_y,
                  a2_abt_x + a2_abt_w - 1, a2_abt_y + a2_abt_h - 1);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(a2_abt_x, a2_abt_y,
                   a2_abt_x + a2_abt_w - 1, a2_abt_y + a2_abt_h - 1);
    nrows = (a2_pan_kind == A2_PAN_CFM) ? A2_CFM_ROWS : A2_ABT_ROWS;
    nbtn = (a2_pan_kind == A2_PAN_CFM) ? 2 : 1;
    for (i = 0; i < nrows; i++) {
        row = (a2_pan_kind == A2_PAN_CFM)
            ? ((i < 2) ? a2_cfm_text[i] : "")
            : a2_abt_text[i];
        if (row[0] == 0)
            continue;                       /* an empty row is a blank line
                                             * and not a call */
        w = (int)os88_strlen(row) * 8;
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
        os88_font_run(x, y, row, OS88_BLACK, OS88_WHITE);
#ifdef A2_HOST
        a2_n_run++;
        a2_n_cell += os88_strlen(row);
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
    by = a2_abt_y + A2_ABT_PADY + nrows * A2_ABT_LH;
    if (by + A2_CFM_BH > a2_abt_y + a2_abt_h - 1)
        by = a2_abt_y + a2_abt_h - A2_CFM_BH - 2;
    a2_cfm_by = by;
    for (i = 0; i < nbtn; i++) {
        /* ONE button centred, or TWO with a 16-pixel gap - and the rects are
         * REMEMBERED, because the resident click handler is what hit-tests
         * them (apple2.c's os88_onclick). A static an ovl_* writes stays
         * resident and DS-relative like every other: only code moves. */
        bx = a2_abt_x + (a2_abt_w - (nbtn * A2_CFM_BW + (nbtn - 1) * 16)) / 2
           + i * (A2_CFM_BW + 16);
        a2_cfm_bx[i] = bx;
        if (by <= a2_abt_y)
            continue;                       /* the panel was clamped to a short
                                             * content box: no room for a row
                                             * of buttons in it */
        os88_gfx_frame(bx, by, bx + A2_CFM_BW - 1, by + A2_CFM_BH - 1);
        row = (a2_pan_kind == A2_PAN_CFM) ? a2_cfm_btn[i] : "OK";
        w = (int)os88_strlen(row) * 8;
        os88_font_run(bx + (A2_CFM_BW - w) / 2, by + 3, row,
                      OS88_BLACK, OS88_WHITE);
#ifdef A2_HOST
        a2_n_run++;
        a2_n_cell += os88_strlen(row);
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
    /* ...AND THE SPEAKER GOES QUIET WITH THE CARD (APPLE2-SPEC section 8).
     * The panel owns the glass and the reader is looking at it, not at the
     * machine; a duration-0 tone would go on sounding underneath it. It is
     * here as well as in a2_spk_service's own list because the service is a
     * WAKE and this is the PICK - one is 55 ms later than the other, and the
     * one the user hears is the pick. */
    a2_sound_stop();
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
static void a2_panel_close(void *win, int yes)
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
    if (a2_pan_kind == A2_PAN_CFM && yes)
        a2_reset_req = A2_RST_POWER;        /* ...and the WAKE spends it: a
                                             * power-on is a 48KB fill and
                                             * this runs under the desktop's
                                             * gfx lock (apple2.c's
                                             * a2_reset_service) */
    a2_pan_kind = A2_PAN_ABOUT;
    a2_kick = 1;
    os88_wm_wake(win);
}

/* a2_about_close - the About panel's own route out, which is a2_panel_close
 * with the answer that a panel carrying no question has. */
static void a2_about_close(void *win)
{
    a2_panel_close(win, 0);
}
