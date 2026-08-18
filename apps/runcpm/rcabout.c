/* ============================================================================
 * os8088 - apps/runcpm/rcabout.c       the About panel (SPEC.md 71.4)
 *
 * Part of RUNCPM, a reimplementation of RunCPM 6.9 by Marcelo Dantas /
 * "Mockba the Borg" (https://github.com/MockbaTheBorg/RunCPM, MIT licence,
 * Copyright (c) 2017 Mockba the Borg). #included by runcpm.c.
 *
 * WHAT IT SAYS, AND WHERE EACH LINE COMES FROM. The product line is
 * readme.md's title, 'RunCPM - Z80 CP/M emulator'; the second is main.c 76's
 * banner line, 'CP/M Emulator v6.9 by Marcelo Dantas', as it stands; the
 * licence line is RunCPM/LICENSE's first two lines, 'MIT License' and
 * 'Copyright (c) 2017 Mockba the Borg'; and between them three lines say
 * what this port is (SPEC.md 71's opening paragraph, condensed) - the product,
 * the version, what this port is, the attribution, OK, AND NOTHING ELSE
 * (LESSONS.md 8: what the build renders or greys is a fact about the build
 * and belongs in SPEC.md 71.4 and in the greyed items, not here).
 *
 * TWELVE ROWS, AND THAT NUMBER IS THE 640x200 ADAPTERS' (SPEC.md 39,
 * LESSONS.md 8). A control's y is 6 + row * 10 from the panel's top; the
 * PANEL clamps to the live content box but a control's row does not, so a
 * table with more rows than the window is tall puts its OK button on the
 * desktop on CGA and Hercules. 6 + 10 * 10 + 10 = 116 against the ~122 those
 * adapters leave (a CGA framed window's content is 136 rows; the panel is
 * 126). RC_AB_ROWS is a compatibility constant: adding a row looks right on
 * VGA and is wrong on two adapters of three.
 *
 * HOW IT IS DRAWN. One white fill, two frames, one composed band per label
 * (rcterm.c's rc_draw_band: rc_band + gfx_blit1, 860 us + 173 us a cell,
 * font_run only when blit1 refuses - the same choice the terminal makes,
 * SPEC.md 71.2), the OK button (a fill, two frames, a band): ~20 drawing
 * calls, once per About. The panel's x is snapped to a byte boundary so
 * every band starts on one.
 *
 * WHAT IT DOES TO THE MACHINE. THE Z80 IS PAUSED WHILE THE PANEL IS UP:
 * os88_onwake runs no slice and posts no wake, os88_onkey routes every key
 * here (Esc / Enter / space press OK; Alt+F is refused, because the latch
 * would repaint the window whole under the panel), os88_onclick every click
 * (OK, or the panel swallows it - modal). Nothing therefore changes the
 * model while the glass is not the terminal's, so no row goes dirty under
 * the panel and a kernel repaint (a menu dropped over the window) can flush
 * nothing stale through its damage clip; the machine picks up where it was
 * when the panel comes down (rc_about_close re-kicks). RunCPM's own console
 * has no modal box; a paused CP/M program is the one behaviour this panel
 * adds, and it is stated in SPEC.md 71.4.
 *
 * WHAT IT COSTS TO TAKE DOWN. Not a full repaint: the panel's rectangle is
 * DAMAGE - rc_blank_rect (rcterm.c, the same routine os88_paint's partial
 * expose uses) fills it white ONCE and marks the cells under it KNOWN BLANK
 * in the shadow, and rc_flush then draws each row only to the span the model
 * differs - the text that was under the panel and nothing else (harness: over
 * a full TE screen, 1 fill + one band per row the panel covered; over a
 * prompt screen, the fill and the rows with text). Invalidating the shadow
 * would draw the whole screen again (a full TE screen is 26 calls / 1,903
 * cells - 1.9 s on the target).
 *
 * The show is module code (ovl_*, SPEC.md 70.14): once per command, and
 * 'the module refused' (a disk without RUNCPM.OVL, toasted by cc_ovneed)
 * means the panel does not open - the machine is not paused, nothing to
 * take down. The close and the hit test are resident: a dozen compares.
 * ==========================================================================*/

#define RC_AB_ROWS 12                        /* a 640x200 number - see above */
#define RC_AB_BW   6                         /* the OK button, cells */

struct rc_abl {
    unsigned char col;                       /* cells from the panel's left */
    unsigned char row;                       /* rows from its top */
    const char   *text;
};
static const struct rc_abl rc_ab_lines[] = {
    { 1, 0, "About" },
    { 4, 2, "RunCPM - Z80 CP/M emulator" },              /* readme.md */
    { 4, 3, "CP/M Emulator v" RC_VERSION " by Marcelo Dantas" },   /* main.c 76 */
    { 4, 5, "A native reimplementation of RunCPM 6.9 for" },
    { 4, 6, "os8088: a Z80 running CP/M 2.2 in a window, drives" },
    { 4, 7, "as folders, Digital Research's CCP at the prompt." },
    { 4, 8, "MIT License, Copyright (c) 2017 Mockba the Borg." }   /* LICENSE */
};
#define RC_AB_NL 7
#define RC_AB_OKROW 10                       /* 6 + 10*10 + 10 = 116 < ~122 */

/* the panel's live geometry, resident bss: the close fills it, the click
 * tests the button (rc_ab_bx/by is the OK box's top-left) */
static int rc_ab_x, rc_ab_y, rc_ab_w, rc_ab_h, rc_ab_bx, rc_ab_by;
static unsigned char rc_abrow[RC_COLS];      /* a label's cells for the band */

/* one label as ONE band (rc_draw_band: blit1, or font_run if refused);
 * x is on a byte boundary by construction (the panel's x is snapped) */
static void ovl_ab_text(int x, int y, const char *s)
{
    int n = (int)os88_strlen(s);
    if (n > RC_COLS)
        n = RC_COLS;
    os88_memcpy(rc_abrow, s, n);
    os88_memset(rc_want_at, 0, RC_ATTRB);   /* no reverse cell */
    rc_draw_band(x, y, rc_abrow, 0, n);
}

/* ovl_about_show - lay the panel out from the LIVE content box and draw it
 * (lock held: the About handler's, a paint's). Sets rc_abt. Module code:
 * answers 1; 0 = the module refused and nothing happened. */
static int ovl_about_show(void *win)
{
    static struct os88_pt org;
    static struct os88_size sz;
    int i, w, x, y;

    if (os88_wm_geom(win, &sz) < 0)
        return 1;                            /* not visible: nothing to draw */
    os88_wm_content(win, &org);

    /* sized from the widest label (col*8 + 24 + text), the button never
     * wider; clamped onto the content box - two adapters of three are
     * 640x200 (SPEC.md 39) */
    w = (RC_AB_BW + 6) << 3;
    for (i = 0; i < RC_AB_NL; i++) {
        int r = (rc_ab_lines[i].col << 3) + 24 +
                ((int)os88_strlen(rc_ab_lines[i].text) << 3);
        if (r > w)
            w = r;
    }
    if (w > sz.w - 8)
        w = sz.w - 8;
    rc_ab_h = RC_AB_ROWS * 10 + 6;
    if (rc_ab_h > sz.h - 8)
        rc_ab_h = sz.h - 8;
    rc_ab_w = w;
    x = org.x + ((sz.w - w) >> 1);
    if (x < org.x + 2)
        x = org.x + 2;
    rc_ab_x = x & ~7;                        /* every band on a byte boundary */
    y = org.y + ((sz.h - rc_ab_h) >> 1);
    if (y < org.y + 2)
        y = org.y + 2;
    rc_ab_y = y;
    x = rc_ab_x;

    rc_abt = 1;
    os88_set_color(OS88_WHITE);
    os88_gfx_fill(x, y, x + w, y + rc_ab_h);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(x, y, x + w, y + rc_ab_h);
    os88_gfx_frame(x + 2, y + 2, x + w - 2, y + rc_ab_h - 2);
    rc_n_calls += 3;
    for (i = 0; i < RC_AB_NL; i++)
        ovl_ab_text(x + 8 + (rc_ab_lines[i].col << 3),
                    y + 6 + rc_ab_lines[i].row * 10, rc_ab_lines[i].text);

    /* the OK button, centred, and it wears the focus frame: Enter presses
     * it (cword's dialogs, the same shape) */
    rc_ab_bx = x + ((((w >> 3) - RC_AB_BW) >> 1) << 3);
    rc_ab_by = y + 6 + RC_AB_OKROW * 10;
    /* the box is y-3..y+10 and the focus frame y-1..y+8, one pixel row clear
     * of the 8-row band the text goes down as - a frame line at y would be
     * cut where the band's cells cover it (seen at zoom 8) */
    os88_set_color(OS88_WHITE);
    os88_gfx_fill(rc_ab_bx, rc_ab_by - 3, rc_ab_bx + (RC_AB_BW << 3) - 1, rc_ab_by + 10);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(rc_ab_bx, rc_ab_by - 3, rc_ab_bx + (RC_AB_BW << 3) - 1, rc_ab_by + 10);
    os88_gfx_frame(rc_ab_bx + 2, rc_ab_by - 1, rc_ab_bx + (RC_AB_BW << 3) - 3, rc_ab_by + 8);
    rc_n_calls += 3;
    ovl_ab_text(rc_ab_bx + 16, rc_ab_by, "OK");
    return 1;
}

/* rc_about_close - the panel comes down (lock held: a key's or a click's):
 * its rectangle is damage, the rows under it are drawn to where they differ,
 * and the paused machine is kicked back into its slices. */
static void rc_about_close(void *win)
{
    rc_abt = 0;
    if (os88_wm_clip_set(win) == 0) {        /* something of us shows */
        rc_blank_rect(win, rc_ab_x, rc_ab_y, rc_ab_x + rc_ab_w, rc_ab_y + rc_ab_h);
        rc_flush(win);
    } else {
        rc_sh_inval();                       /* nothing shows: the next
                                              * expose repaints */
    }
    if (rc_wants_wake())
        os88_wm_wake(win);
}

/* is (x, y) - absolute, as W_ONCLICK gives it - on the OK button? */
static int rc_about_hit_ok(int x, int y)
{
    return x >= rc_ab_bx && x < rc_ab_bx + (RC_AB_BW << 3) &&
           y >= rc_ab_by - 3 && y <= rc_ab_by + 10;
}
