/* ============================================================================
 * os8088 - apps/infones/niabout.c      the About panel - OUT OF LINE
 *
 * Part of INFONES (SPEC.md 91.7.3). Derived from InfoNES fe3295c0 under
 * Apache-2.0 - see apps/infones/LICENSE.TXT. Section 4(b): derived from
 * InfoNES fe3295c0, restructured for 8086 real mode.
 *
 * WHAT THE ROWS QUOTE, AND WHY FROM ONE FILE. The InfoNES tree carries FOUR
 * incompatible pairings of a version with a tagline and a copyright, and the
 * one taken here is src/linux/InfoNES_System_Linux.cpp:45 and :324-328 -
 * `InfoNES v0.96J`, `A fast and portable NES emulator`, `Copyright (c)
 * 1999-2005 Jay's Factory <jays_factory@excite.co.jp>` - because that handler
 * is the only live About in the tree that pairs a VERSION with the tagline
 * and a copyright at all. The other three, recorded so that no later wave
 * re-picks: the Win32 About (InfoNES_System_Win.cpp:517-533) prints APP_NAME,
 * and `grep -rn APP_NAME .` over the whole tree returns nothing, so it cannot
 * be the authority for a version line, and its own copyright reads
 * 1999-2004; the SDL front end (InfoNES_System_SDL.cpp:32) says v0.97J RC1
 * while its banner at :164-166 reads "Copyright (C) 1998-2006 Jay's Factory,
 * SDL Ports by mata", so taking that version obliges naming mata; and
 * src/zaurus is v0.93J RC4. The .rc's IDD_DIALOG at :113-124 is `#if 0` dead
 * code and is not the authority either.
 *
 * SO THIS PANEL MAKES NO VERSION CLAIM OF ITS OWN beyond naming the reference
 * it was written against: `InfoNES v0.96J (@fe3295c0)`.
 *
 * #included into apps/infones/infones.c - ONE translation unit (73.1).
 *
 * ----------------------------------------------------------------------------
 * NINE ROWS IS A 640x200 COMPATIBILITY CONSTANT (LESSONS.md 8)
 * ----------------------------------------------------------------------------
 * A control's y is 6 + row * 10 and NOTHING CLAMPS IT: a 19-row About box put
 * its OK button on the DESKTOP on CGA and Hercules, and it looked right on
 * VGA. Nine rows puts the button at 6 + 9 * 10 + 11 = 107 against the ~122 a
 * framed content box has on a 200-line adapter (apps/c64/c64about.c:29-46's
 * measured shape). ADDING A ROW HERE IS A COMPATIBILITY CHANGE, not a
 * cosmetic one.
 *
 * WHAT IS DELIBERATELY NOT HERE. The email address is not a licence
 * requirement. The Apache-2.0 section 4(b) modification notice and the
 * Nintendo trademark sentence each WRAP TO TWO ROWS at ~40 cells and would
 * take this panel to twelve; they are in every source header and in
 * README.TXT on the disk, which is where the licence puts them. And nothing
 * about HOW THE BUILD RENDERS: that the PPU is scanline-granular and that
 * there is no APU are facts about the build, and LESSONS.md 8 puts those in
 * the SPEC and in the greyed items that name them - which is where they are.
 *
 * ovl_*: the panel is DRAWN once per opening, so ovl_about_show is in the
 * module. Its CLOSE and its hit test stay RESIDENT below - a click is a
 * callback and a callback is reached by a near offset (apps/c64's split).
 * ==========================================================================*/

#define NI_ABT_ROWS 9
#define NI_ABT_W    280             /* ~35 cells: the longest row below is
                                     * `A fast and portable NES emulator` at
                                     * 32, and the panel is clamped to the
                                     * live content box besides */
#define NI_ABT_H    (6 + NI_ABT_ROWS * 10 + 16)

static const char *ni_abt_text[NI_ABT_ROWS] = {
    "About InfoNES",                     /* the panel's own title */
    "A fast and portable NES emulator",  /* Linux:325 */
    "InfoNES v0.96J (@fe3295c0)",        /* Linux:45 VERSION, and the commit
                                          * this port was written against */
    "os8088 port: NTSC",                 /* WHAT THIS PORT IS, and NOTHING
                                          * about how the build renders. NTSC
                                          * is a fact about the emulated
                                          * MACHINE (PAL is dropped, SPEC.md
                                          * 91.11), which is why it belongs -
                                          * apps/c64/c64about.c:52-58 keeps
                                          * `PAL` on the identical row for the
                                          * identical reason.
                                          *
                                          * `no APU` WAS ON THIS ROW AND IS
                                          * GONE. It is a thing the build
                                          * cannot do, and LESSONS.md 8 puts
                                          * those in the SPEC and in the greyed
                                          * item that names them - which is
                                          * exactly where it already is, twice:
                                          * the greyed `Mute (no APU)` label
                                          * and SPEC.md 91.10 (and the fact
                                          * row's own sentence). It would also
                                          * go stale the day the pulse-1
                                          * follow-up lands, which is what a
                                          * release note in an About box does.
                                          * c64 deleted `1bpp` and `no drive`
                                          * from its row on this argument. */
    "Copyright (c) 1999-2005",           /* Linux:326-327, lower-case (c) and
                                          * that year range, split over two
                                          * rows because it is 64 characters
                                          * whole and would wrap into three */
    "Jay's Factory",
    "Apache-2.0 - see LICENSE.TXT",      /* the licence, and the file that
                                          * ships beside the binary */
    "Scroll model from agnes (MIT)",     /* the second reference this port
                                          * actually reads from (SPEC.md 91.2) */
    "Ported by Jorge Gonzalez"
};

/* the longest row is `A fast and portable NES emulator` at 32 cells, and the
 * scratch that one row is CUT into before it is drawn (see the loop below) */
#define NI_ABT_CELLS 34
static char ni_abt_cut[NI_ABT_CELLS + 2];

static int ovl_about_show(void *win)
{
    int x, y, i, n, bx, by, cells;

    if (ni_geom(win) < 0)
        return 0;
    ni_abt_w = (ni_gw < NI_ABT_W) ? ni_gw : NI_ABT_W;
    ni_abt_h = (ni_gh < NI_ABT_H) ? ni_gh : NI_ABT_H;
    x = ni_gox + (ni_gw - ni_abt_w) / 2;
    y = ni_goy + (ni_gh - ni_abt_h) / 2;
    if (x < ni_gox)
        x = ni_gox;
    if (y < ni_goy)
        y = ni_goy;
    ni_abt_x = x;
    ni_abt_y = y;

    os88_set_color(OS88_WHITE);
    os88_gfx_fill(x, y, x + ni_abt_w - 1, y + ni_abt_h - 1);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(x, y, x + ni_abt_w - 1, y + ni_abt_h - 1);
    /* os88_font_run AND NOT font_str over a fill. What that removes is the
     * SECOND erase, not the first: the fill above is the panel's paper and
     * these runs are drawn over it once, where font_str would have wanted a
     * white fill PER ROW before its glyphs and written those cells a third
     * time (PERFORMANCE.md rule 2). The fill itself stays - a nine-row panel
     * needs a ground, its 6px top margin, its 2px inter-row gaps and its
     * button strip are pixels no run covers, and one 280x112 fill is ~756 us
     * against the ~7 ms of lettering it underlies. A run also cannot produce
     * the clip-granularity failure a wide erase followed by glyphs can, which
     * is exactly this context (SPEC.md 11.3). */
    /* CLAMPED IN BOTH AXES, WHICH THIS FILE'S OWN HEADER ALREADY KNEW AND
     * ONLY THE OK BUTTON HAD. `ni_abt_w/h` are min'd against the LIVE content
     * box above, so on a window at os88_wm_minsize's floor (200 x 80, and the
     * kernel honours a floor on every path that reduces a size - a create-time
     * fit, an adapter change, a drag onto a smaller display) the panel is ~198
     * wide and ~67 tall: a 32-cell row is 256 pixels and reached 66 pixels past
     * the right edge, and rows 7 and 8 were below the content box entirely.
     * os88_gfx_fill and os88_font_run are UNCLIPPED unless a clip region is
     * armed (SPEC.md 11.3) and this runs from a menu callback with none, so
     * that overrun lands on the window frame and on the desktop.
     *
     * The row is cut to the panel's own cell width into a scratch buffer, and
     * the loop stops at the last row whose 8-pixel band fits. */
    cells = (ni_abt_w - 16) / 8;
    if (cells > NI_ABT_CELLS)
        cells = NI_ABT_CELLS;
    for (i = 0; i < NI_ABT_ROWS && cells > 0; i++) {
        if (6 + i * 10 + 8 > ni_abt_h)
            break;                  /* the row would fall outside the panel */
        for (n = 0; n < cells && ni_abt_text[i][n]; n++)
            ni_abt_cut[n] = ni_abt_text[i][n];
        ni_abt_cut[n] = 0;
        os88_font_run(x + 8, y + 6 + i * 10, ni_abt_cut,
                      OS88_BLACK, OS88_WHITE);
    }

    bx = x + ni_abt_w - 56;
    by = y + 6 + NI_ABT_ROWS * 10;
    /* a content box shorter than the panel: the OK button stays INSIDE it */
    if (by + 13 > y + ni_abt_h - 1)
        by = y + ni_abt_h - 15;
    os88_gfx_frame(bx, by, bx + 47, by + 13);
    os88_font_run(bx + 12, by + 3, "OK", OS88_BLACK, OS88_WHITE);
    return 1;
}

/* ni_about_close - RESIDENT, and deliberately: it is reached from os88_onkey
 * and os88_onclick, which are callbacks, and a callback that had to load a
 * module to dismiss a panel would refuse on a disk with no .OVL and leave a
 * panel that cannot be closed. */
static void ni_about_close(void *win)
{
    ni_abt = 0;
    if (ni_geom(win) < 0)
        return;                     /* not visible: nothing to give back */

    /* IT ERASES THE LIVE CONTENT BOX AND NOT THE BANKED PANEL RECT, and that
     * is a correctness fix rather than a simplification. `ni_abt_x/y` are
     * ABSOLUTE SCREEN coordinates taken when the panel was drawn, and
     * os88_gfx_fill is UNCLIPPED unless a clip region is armed (SPEC.md 11.3)
     * - so if the window MOVED while the panel was up, this filled a white
     * rectangle wherever the panel used to be. Photographed: dragging the
     * window with About open and then pressing Esc whitened the old rect,
     * which by then covered this window's own title bar and a strip of the
     * DESKTOP beside it (build/port-shots/wave1r-11e-about-closed.png).
     *
     * The content box is read LIVE and is the exact area WF_OWNBG makes ours
     * to paint (SPEC.md 11.90.1), so the fill can no longer reach anything
     * that is not. One fill, then the fields as PAPER - unpadded runs over
     * white we have just laid down, which is 228 cells rather than 468. */
    os88_set_color(OS88_WHITE);
    os88_gfx_fill(ni_gox, ni_goy, ni_gox + ni_gw - 1, ni_goy + ni_gh - 1);
    ni_shadow_paper();              /* ...and it IS paper now, not merely
                                     * unknown (nipanel.c's two sentinels) */
    ni_panel_paint(win, 0);
}
