/* ============================================================================
 * os8088 - apps/c64/c64about.c        the About panel - OUT OF LINE
 *
 * Part of C64 (C64-SPEC §12). Derived from VICE 3.10, Copyright (C)
 * 1996-2025 the VICE team, GPL-2-or-later - see apps/c64/COPYING. The rows
 * are src/arch/gtk3/uiabout.c's title and model string, configure.ac's
 * vice_version, and VICE's README lines 186-290 (the copyright notice and
 * "The ROM files ... are Copyright (C) by Commodore Business Machines").
 *
 * #included into apps/c64/c64.c - ONE translation unit (SPEC.md 73.1).
 *
 * ovl_*: the panel is drawn once per opening. Its CLOSE and its hit test stay
 * RESIDENT in c64.c - the rcabout.c split - because a click is a callback and
 * a callback is reached by a near offset (13.3).
 *
 * WHAT IT SAYS AND WHAT IT DOES NOT. The product, the version, what this port
 * is, the VICE copyright, the licence and whose the ROMs are. NOTHING about
 * how the build renders: that the screen is 1bpp, that collisions answer 0
 * and that the VIC is serviced at raster-line granularity are facts about the
 * build, and they belong in docs/C64-SPEC.md and in the greyed menu items
 * that name them (LESSONS.md 8).
 *
 * TWELVE ROWS IS A 640x200 NUMBER (LESSONS.md 8). A control's y is
 * 6 + row * 10 and nothing clamps it, so a 19-row panel puts its OK button on
 * the DESKTOP on CGA and Hercules. This one is eight rows and its OK sits at
 * 6 + 8 * 10 + 11 = 97 against the ~122 a framed CGA content box has.
 * ==========================================================================*/

#define C64_ABT_ROWS 8
/* THE PANEL IS AS WIDE AS THE C64 SCREEN AND ITS BORDER - 336 - and that is a
 * redraw decision rather than a taste. At 280 px in a 336-px box it left a
 * 28-px strip of C64 screen down each side, so an expose while the panel was
 * up had to compose and blit every row the panel covered - all forty cells of
 * each - and then paint the panel over the middle of what it had just drawn:
 * the erase-then-letter pair PERFORMANCE.md's rule 2 names, ~120 ms of it,
 * and invisible in any screendump. 336 makes "the rows the panel covers"
 * exact, so os88_paint skips them entirely (c64.c's c64_hold_r0/r1) and
 * nothing under the panel is drawn at all. Both the panel and the screen are
 * centred in the content box, so the panel still covers the screen edge to
 * edge in FULLSCREEN, where the box is 640 wide and the screen 320 (9.8); and
 * the width is clamped to the live box, so it is right on a CGA window too.
 * os88_paint checks the horizontal cover before it holds a single row. */
#define C64_ABT_W    C64_W_W                /* 336 */
#define C64_ABT_H    (6 + C64_ABT_ROWS * 10 + 16)

static const char *c64_abt_text[C64_ABT_ROWS] = {
    "About VICE",                           /* uiabout.c:164 window title */
    "The Commodore 64 Emulator",            /* uiabout.c's model string */
    "VICE 3.10",                            /* configure.ac vice_version */
    /* WHAT THIS PORT IS, and nothing about HOW IT RENDERS. PAL is a fact
     * about the emulated MACHINE (c64.h:35-40's PAL model, C64-SPEC §5.2's
     * 63 x 312 = 19,656-cycle frame) and belongs here. `1bpp` and `no drive`
     * were on this row and are gone: the first is how the build draws and the
     * second is a thing it cannot do, and LESSONS.md 8 puts both in the SPEC
     * and in the greyed items that name them - which is where they already
     * are, on File > Attach disk image and File > Reset drive #8, and in
     * C64-SPEC §9.6 and §11.2. This file's own header says so eleven lines
     * above, and the row said otherwise. */
    "os8088 port: PAL",
    "Copyright 1996-2025, VICE team",       /* uiabout.c:244 */
    "GPL-2 or later - see COPYING",         /* uiabout.c:235 */
    "ROMs Copyright Commodore",             /* README 186-290 */
    "Business Machines"
};

static int c64_abt;                         /* the panel is up */
static int c64_abt_x, c64_abt_y;            /* ...and where, for the hit test */
static int c64_abt_w, c64_abt_h;            /* ...and how big, clamped to the
                                             * live content box: os88_paint
                                             * and the close both need the
                                             * exact rect (9.3) */

static int ovl_about_show(void *win)
{
    int x, y, i, bx, by;

    if (c64_geom(win) < 0)
        return 0;
    c64_abt_w = (c64_gw < C64_ABT_W) ? c64_gw : C64_ABT_W;
    c64_abt_h = (c64_gh < C64_ABT_H) ? c64_gh : C64_ABT_H;
    /* THE PANEL IS SNAPPED TO THE CELL GRID, and that is what makes
     * os88_paint's hold rows EXACT. c64_hold_r0/r1 are (y - screen_y) / 8 and
     * C truncates toward zero, so an unsnapped panel left the partly covered
     * cell row at each end held-but-uncovered: with the shipped geometry, six
     * pixel rows above the panel and four below it were inside a held row and
     * outside the panel, and WF_OWNBG means nobody whitens them - two
     * full-width strips of whatever damaged them, until the panel was closed.
     * Rounding the height UP to a whole cell and the origin DOWN onto the
     * grid costs nothing and makes both ends exact. */
    c64_abt_h = (c64_abt_h + 7) & ~7;
    if (c64_abt_h > c64_gh)
        c64_abt_h -= 8;
    x = c64_gox + (c64_gw - c64_abt_w) / 2;
    y = c64_goy + (c64_gh - c64_abt_h) / 2;
    if (x < c64_gox)
        x = c64_gox;
    if (y < c64_goy)
        y = c64_goy;                        /* the panel clamps to the content
                                             * box, and eight rows is what
                                             * keeps the button inside it */
    if (y + c64_abt_h > c64_goy + c64_gh)
        y = c64_goy + c64_gh - c64_abt_h;
    if (y > c64_gsy)
        y = c64_gsy + ((y - c64_gsy) / c64_sch) * c64_sch;  /* onto the cell
                                                            * grid, which is 8
                                                            * or 16 px (9.8) */
    c64_abt_x = x;
    c64_abt_y = y;

    os88_set_color(OS88_WHITE);
    os88_gfx_fill(x, y, x + c64_abt_w - 1, y + c64_abt_h - 1);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(x, y, x + c64_abt_w - 1, y + c64_abt_h - 1);
    /* os88_font_run AND NOT font_str+fill: the cells' paper and their glyphs
     * in one pass, so the 198 glyph cells this panel carries are not written
     * twice over the fill above (PERFORMANCE.md rule 2), and - the reason
     * os88.h:467 gives - a run cannot produce the clip-granularity failure a
     * wide erase followed by glyphs can, which is exactly the context here:
     * this runs under W_PAINT's damage clip (SPEC.md 11.3). */
    for (i = 0; i < C64_ABT_ROWS; i++)
        os88_font_run(x + 8, y + 6 + i * 10, c64_abt_text[i],
                      OS88_BLACK, OS88_WHITE);

    bx = x + c64_abt_w - 56;
    by = y + 6 + C64_ABT_ROWS * 10;
    /* a content box shorter than the panel: the OK button stays INSIDE it
     * (LESSONS.md 8 - a 19-row About box put its OK on the DESKTOP) */
    if (by + 13 > y + c64_abt_h - 1)
        by = y + c64_abt_h - 15;
    os88_gfx_frame(bx, by, bx + 47, by + 13);
    os88_font_run(bx + 12, by + 3, "OK", OS88_BLACK, OS88_WHITE);
    return 1;
}
