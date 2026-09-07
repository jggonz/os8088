/* ============================================================================
 * os8088 - tests/cfsx/cfsx.c
 *
 * THE CAPABILITY GATE FOR THE EXCLUSIVE BRACKET IN C (SPEC.md 53, reached
 * from C per SPEC.md 92.2). tests/chello proved a compiled package can hold a
 * window and tests/covl proved half of it can live in a second segment; this
 * one proves a compiled package can BORROW THE MACHINE - take the screen away
 * from the desktop, set a foreign video mode, drive the VGA's own registers,
 * change mode again in the middle, far-call its overlay from in there, and
 * hand the desktop back.
 *
 * It is under tests/ because it proves a capability rather than being one
 * (CLAUDE.md: everything in apps/ ships, nothing in tests/ does).
 *
 * ---------------------------------------------------------------------------
 * WHY IT PUTS NUMBERS ON THE GLASS
 * ---------------------------------------------------------------------------
 * Every claim below is the sort that assembles cleanly, boots, and is wrong -
 * a picture that is merely different rather than absent. Three of them had
 * never been done anywhere in this tree before SPEC.md 92 needed them:
 *
 *   THE CRTC LINE COMPARE with a panel standing still under a scrolling top
 *   half; a MODE CHANGE INSIDE a bracket; and an ovl_* FAR-CALLED FROM INSIDE
 *   one.
 *
 * So the gate prints, in the foreign mode, with the BIOS's own font: the caps
 * bitmask, all ten FSI fields, the rect fsx_surf answers, the counter only
 * os88_fsx_main() can bump, the three line-compare registers READ BACK, and
 * what the overlay answered. A screendump of any stage is a complete
 * statement about that stage.
 *
 * ---------------------------------------------------------------------------
 * THE EIGHT THINGS IT MAKES VISIBLE
 * ---------------------------------------------------------------------------
 *  1. os88_fsx_caps() ASKED WITH OUR OWN WINDOW, in hex with the VID_* kind,
 *     in the window before the bracket and again inside it. Asking with 0
 *     answers about the window we were LAUNCHED from, which is the defect
 *     kernel/fsx.inc's own comment records against Missile Command.
 *  2. THE BRACKET ACTUALLY RUNS: cf_entered is bumped by os88_fsx_main() and
 *     by nothing else, and the window shows it after the bracket returns.
 *  3. ALL TEN FSI FIELDS printed inside the foreign mode.
 *  4. A SIXTEEN-COLOUR RAMP in mode 0Dh - a plane that does not get written
 *     shows as a missing or doubled bar and as nothing else.
 *  5. THE SPLIT SCREEN: line compare at scanline 320, the bottom 40 pixel
 *     rows standing still while the top scrolls one pixel a tick by the start
 *     address and the pel pan together. Both halves carry a caption, so "it
 *     scrolled" and "it stood still" are readable in ONE screendump.
 *  6. A MID-BRACKET MODE CHANGE: 0Dh -> 12h -> 0Dh, FSI printed each time.
 *  7. THE OVERLAY FROM INSIDE THE BRACKET, in three arms the operator picks
 *     between - and the third one is the reason this gate found something:
 *
 *       SPACE  force the module resident BEFORE entering (SPEC.md 92.5's
 *              rule), with a SPEC.md 11.2 fullscreen window stacked under
 *              the bracket the way apps/missile stacks one
 *       L      enter first and load the module INSIDE, fullscreen stacked
 *       N      ...and with NO fullscreen window under it
 *
 *     Booted against build/cfsxnoovl.img, which carries no module at all,
 *     SPACE refuses in the app's own words with nothing toasted, which is
 *     92.5 working. L refuses cleanly TOO - and that is not 92.5 working, it
 *     is menu_draw_bar's own `wm_fs_vis` guard: a visible fullscreen window
 *     is over the bar, so the toast composes nothing. N takes that guard away
 *     and the kernel's toast lands in the middle of mode 0Dh, drawn at the
 *     DESKTOP's 80-byte stride across the top of a 40-byte screen.
 *
 *     So there are two independent things keeping a toast off a foreign mode,
 *     the rule and the stacked window, and a program that has only one of
 *     them is one edit away from the picture the N arm photographs.
 *  8. os88_fsx_surf()'s rect, os88_fsx_key() reading the keys that drive
 *     every stage, and os88_fsx_wait(OS88_FSXW_TICK) pacing the scroll.
 *
 * ---------------------------------------------------------------------------
 * AND ONE THING IT MEASURED THAT IS NOT ON THE LIST
 * ---------------------------------------------------------------------------
 * os88_fsx_surf() ANSWERS THE DESKTOP'S EXTENT, NOT THE FOREIGN MODE'S. In
 * mode 0Dh, on a one-display VGA whose desktop is 640x480, it answers
 * (0,0)-(639,479) and not (0,0)-(319,199) - kernel/fsx.inc reads [vid_cw] /
 * [vid_ch], the live block that fsx.inc's own header says "keeps describing
 * the DESKTOP mode". It is the coordinate system the DRAWING SLOTS take, and
 * those are all refused in here, so nothing is broken - but an app that sizes
 * its raster from it gets 640x480 for a 320x200 screen. THE FSI's w AND h ARE
 * THE AUTHORITY inside a foreign mode. The screendump of stage 1 has both
 * numbers on it, four lines apart.
 *
 * ---------------------------------------------------------------------------
 * DRIVING IT
 * ---------------------------------------------------------------------------
 *     make cfsx
 *     make test TESTAPPS=build/cfsx.img          ...and the refusal arm:
 *     make test TESTAPPS=build/cfsxnoovl.img
 *
 * Double-click Disk B, then CFSX.O88. In the window: SPACE, L and N are the
 * three arms above and R resets the counters. Inside the bracket, any key
 * advances a stage and Esc leaves at once.
 * ==========================================================================*/

#include "os88.h"

#define CF_X   96
#define CF_Y   96
#define CF_W   400
#define CF_H   152

/* The split screen's three numbers. CF_LC is in SCANLINES and mode 0Dh double
 * scans, so 320 is pixel row 160 and the panel is the bottom 40 pixel rows -
 * and 320 needs bit 8 of the line compare, which is the bit that makes the
 * register a three-piece one and the reason this is worth a gate. */
#define CF_LC        320
#define CF_PANEL     40                 /* pixel rows below the compare */
#define CF_VSTRIDE   80                 /* bytes: a 640-pixel virtual line */
#define CF_VROWS     200
#define CF_SCROLLTOP 3200               /* CF_PANEL * CF_VSTRIDE: the first
                                         * byte the top half shows, so the
                                         * panel's rows are never also at the
                                         * top of the screen */
#define CF_PANMAX    320                /* 640 virtual - 320 visible */

/* --- the hand-written half (tests/cfsx/cfsxvga.inc) ------------------------
 * Every kernel drawing slot is refused inside the bracket, so the pixels are
 * written into the FSI's framebuffer segment and the VGA is talked to by
 * port. C has no `out dx, al`, so none of this could be C. */
void cf_gcmode(int mode);
void cf_fill(unsigned seg, unsigned off, unsigned n, int colour);
void cf_ramp(unsigned seg, unsigned stride, unsigned y, unsigned h);
void cf_diag(unsigned seg, unsigned stride, unsigned row0, unsigned rows);
void cf_panel(unsigned seg, unsigned stride, unsigned rows);
void cf_tty(int row, int col, const char *s, int colour);
int  cf_crtcget(int index);
int  cf_acget(int index);
void cf_split(unsigned scanline);
void cf_panmode(int on);
void cf_nosplit(void);
void cf_offset(int words);
void cf_scroll(unsigned addr, int pan);
int  cf_bdacols(int cols);
void cf_vsync(void);

/* --- state, all static (os88.h rule 1: &automatic is refused at build time) */

static unsigned      cf_caps_out;       /* fsx_caps outside the bracket... */
static unsigned char cf_kind_out;
static unsigned      cf_caps_in;        /* ...and inside it */
static unsigned char cf_kind_in;

static int cf_entered;                  /* ONLY os88_fsx_main() bumps this */
static int cf_runret;                   /* what os88_fsx_run() answered */
static int cf_stage;                    /* how far the last bracket got */
static int cf_modes;                    /* fsx_mode calls that returned 0 */
static int cf_frames;                   /* scrolled frames */

static int cf_want_pre;                 /* this run forces the module in
                                         * first (SPEC.md 92.5) */
static int cf_want_fs;                  /* ...and stacks SPEC.md 11.2's
                                         * fullscreen window under the
                                         * bracket, which turns out to be
                                         * what keeps a toast off the glass */
static int cf_pre_ok;                   /* ...and whether that worked */
static int cf_ovl;                      /* what ovl_probe() answered */
static int cf_ovl_where;                /* 0 none, 1 before, 2 inside */

static struct os88_fsi  cf_fsi;
static struct os88_rect cf_surf;
static int              cf_surfr;

static struct os88_pt   cf_org;
static struct os88_size cf_sz;

static char cf_line[84];
static char cf_num[8];
static char cf_hexb[8];

/* --- little string helpers, covl's ---------------------------------------- */

static void cf_app(const char *s)
{
    unsigned n;

    n = os88_strlen(cf_line);
    os88_strcpy(cf_line + n, s, sizeof(cf_line) - n);
}

static void cf_appn(int v)
{
    os88_utoa((unsigned)v, cf_num);
    cf_app(cf_num);
}

/* Four hex digits. The caps bitmask and the framebuffer segment are both read
 * as bit patterns and neither is readable in decimal. */
static void cf_apph(unsigned v)
{
    static char cf_digits[17] = "0123456789ABCDEF";

    cf_hexb[0] = cf_digits[(v >> 12) & 15];
    cf_hexb[1] = cf_digits[(v >> 8) & 15];
    cf_hexb[2] = cf_digits[(v >> 4) & 15];
    cf_hexb[3] = cf_digits[v & 15];
    cf_hexb[4] = 0;
    cf_app(cf_hexb);
}

static void cf_apph2(unsigned v)
{
    static char cf_digits2[17] = "0123456789ABCDEF";

    cf_hexb[0] = cf_digits2[(v >> 4) & 15];
    cf_hexb[1] = cf_digits2[v & 15];
    cf_hexb[2] = 0;
    cf_app(cf_hexb);
}

/* ===========================================================================
 * THE MODULE. One function, compiled into `.modc` and shipped as CFSX.OVL
 * because its name begins with `ovl_` (SPEC.md 73.14).
 *
 * Four arguments for tests/covl's reason - a far call pushes four bytes where
 * a near call pushes two, so every argument is two bytes further from the
 * frame pointer than the compiler thought, and a fixup that rewrote too few
 * of them reads the saved CS as an argument. 1234 or nothing.
 *
 * What is new here is WHERE it is called from: inside the bracket, with the
 * gfx lock held, the scheduler passing only this task and a foreign video
 * mode on the glass. Getting there costs a floppy read if the module is not
 * already resident, which is why SPEC.md 92.5 says to force it in first - and
 * why this gate can be told to do the opposite.
 * ========================================================================= */
static int ovl_probe(int a, int b, int c, int d)
{
    return ((a * 10 + b) * 10 + c) * 10 + d;
}

/* ===========================================================================
 * ...and back to the resident program.
 * ========================================================================= */

/* cf_fsi_lines - the ten FSI fields, three lines, into the foreign mode.
 * `row` is the first text row to use. */
static void cf_fsi_lines(int row)
{
    cf_line[0] = 0;
    cf_app("FSI seg ");
    cf_apph(cf_fsi.seg);
    cf_app(" w ");
    cf_appn((int)cf_fsi.w);
    cf_app(" h ");
    cf_appn((int)cf_fsi.h);
    cf_tty(row, 0, cf_line, 15);

    cf_line[0] = 0;
    cf_app("    stride ");
    cf_appn((int)cf_fsi.stride);
    cf_app(" flags ");
    cf_apph2(cf_fsi.flags);
    cf_app(" bpp ");
    cf_appn(cf_fsi.bpp);
    cf_tty(row + 1, 0, cf_line, 15);

    cf_line[0] = 0;
    cf_app("    banks ");
    cf_appn(cf_fsi.banks);
    cf_app(" pages ");
    cf_appn(cf_fsi.pages);
    cf_app(" bstep ");
    cf_appn((int)cf_fsi.bstep);
    cf_app(" mode ");
    cf_appn(cf_fsi.mode);
    cf_tty(row + 2, 0, cf_line, 15);
}

/* cf_head - the two lines every stage carries, so that a screendump of any
 * one of them says which arm produced it and how far it had got. */
static void cf_head(const char *what)
{
    cf_line[0] = 0;
    cf_app("CFSX  SPEC.md 53 bracket gate  ");
    cf_app(what);
    cf_tty(0, 0, cf_line, 15);

    cf_line[0] = 0;
    cf_app("caps out ");
    cf_apph(cf_caps_out);
    cf_app(" k");
    cf_appn(cf_kind_out);
    cf_app("  in ");
    cf_apph(cf_caps_in);
    cf_app(" k");
    cf_appn(cf_kind_in);
    cf_tty(1, 0, cf_line, 15);

    cf_line[0] = 0;
    cf_app("enter ");
    cf_appn(cf_entered);
    cf_app(" stage ");
    cf_appn(cf_stage);
    cf_app(" modes ");
    cf_appn(cf_modes);
    cf_tty(2, 0, cf_line, 15);
}

/* cf_ovl_line - what the overlay did, and WHERE it was asked for.
 *
 * The refusal is the half worth reading: cc_ovneed answers 0 and the program
 * says so in its own words, in the foreign mode, with the BIOS's font. If a
 * kernel toast appears above this line then the module was loaded INSIDE the
 * bracket and failed there, which is exactly what SPEC.md 92.5 forbids and
 * what the L arm of this gate exists to photograph. */
static void cf_ovl_line(int row)
{
    cf_line[0] = 0;
    cf_app("OVL ");
    if (cf_ovl_where == 1)
        cf_app("forced resident BEFORE: ");
    else if (cf_ovl_where == 2)
        cf_app("loaded INSIDE bracket: ");
    else
        cf_app("not asked for: ");
    if (cf_ovl == 0)
        cf_app("REFUSED, 0");
    else
        cf_appn(cf_ovl);
    cf_tty(row, 0, cf_line, cf_ovl ? 15 : 12);

    cf_line[0] = 0;
    cf_app("ovl_probe(1,2,3,4) wants 1234   fs ");
    cf_appn(cf_want_fs);
    cf_tty(row + 1, 0, cf_line, 7);
}

/* cf_pause - block on a key. 1 = Esc, leave the bracket now.
 *
 * os88_fsx_key() is the only way to read the keyboard in here: no event is
 * dispatched inside a bracket and the contract is that the app polls. */
static int cf_pause(void)
{
    int k;

    k = os88_fsx_key(1);
    return ((k & 0xFF) == 27) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * STAGE 1 - mode 0Dh, everything the kernel will tell us, and a colour ramp.
 * ------------------------------------------------------------------------- */
static int cf_stage_info(void)
{
    if (os88_fsx_mode(OS88_FSXM_VGA0D, &cf_fsi) != 0)
        return 1;                       /* refused: nothing to draw on */
    cf_modes++;
    cf_stage = 1;

    cf_surfr = os88_fsx_surf(&cf_surf);

    cf_gcmode(2);
    cf_ramp(cf_fsi.seg, cf_fsi.stride, 104, 48);
    cf_gcmode(0);

    cf_head("0Dh");
    cf_fsi_lines(3);

    cf_line[0] = 0;
    cf_app("surf ");
    cf_appn(cf_surf.x1);
    cf_app(",");
    cf_appn(cf_surf.y1);
    cf_app(" - ");
    cf_appn(cf_surf.x2);
    cf_app(",");
    cf_appn(cf_surf.y2);
    cf_app("  r");
    cf_appn(cf_surfr);
    cf_tty(6, 0, cf_line, 15);

    /* THE OVERLAY CALL GOES HERE AND NOT EARLIER, and the position is the
     * measurement rather than a tidy-up. cc_ovneed's refusal ends in a kernel
     * toast, and a toast composes the MENU BAR - desktop geometry, an 80-byte
     * stride - into whatever framebuffer is on the glass, which in mode 0Dh
     * is the top 26 pixel rows. Called before the screen was lettered, the
     * three header lines drawn afterwards painted over exactly that band and
     * the refusal arm photographed clean: the defect this gate exists to
     * photograph, hidden by the gate. Everything above rows 0-2 is on the
     * glass before the call now, so a toast survives into the screendump. */
    if (cf_want_pre) {
        if (cf_pre_ok)
            cf_ovl = ovl_probe(1, 2, 3, 4);
    } else {
        cf_ovl_where = 2;
        cf_ovl = ovl_probe(1, 2, 3, 4);
    }

    cf_ovl_line(7);

    cf_tty(10, 0, "16-colour ramp below: 0..15", 15);
    cf_tty(21, 0, "any key = next stage, Esc = leave", 14);
    return cf_pause();
}

/* ---------------------------------------------------------------------------
 * STAGE 2 - THE SPLIT SCREEN (SPEC.md 92.4's raster, proved)
 *
 * The canvas is 640 pixels wide and the screen shows 320 of it, so the scroll
 * is a scroll and not a shear: without the wider logical line, advancing the
 * start address by one byte pulls the NEXT ROW's first pixels in at the right
 * edge. Everything below the line compare comes from framebuffer offset 0
 * whatever the start address is - which is why the panel's content is drawn
 * at row 0 and the top half starts CF_PANEL rows in.
 * ------------------------------------------------------------------------- */
static int cf_stage_split(void)
{
    int oldcols;
    int pan;
    int k;
    int lc18, lc07, lc09, ac10;

    cf_stage = 2;
    pan = 0;

    /* THE LOGICAL LINE GOES WIDE FIRST, BEFORE A WORD OF TEXT IS DRAWN, and
     * getting that order wrong is the reason this comment is here. The VGA
     * BIOS's graphics character writer does not take the bytes per scan row
     * out of the BDA at all: it reads the CRTC's own Offset register and
     * multiplies. Drawn before the register moved, every line landed at a
     * QUARTER of the pitch the next one used - four captions inside the panel
     * and the rest at the wrong rows - which reads as a broken split screen
     * and is nothing of the kind. The BDA word is still swapped below,
     * because the column WRAP is read from there and a caption at column 40
     * would otherwise fold onto the next line. */
    cf_offset(CF_VSTRIDE / 2);          /* CRTC 0x13 is in units of 2 bytes */
    oldcols = cf_bdacols(CF_VSTRIDE);   /* 80 columns: 80 bytes is 640 pixels
                                         * is 80 cells, and the two 80s being
                                         * the same number is a coincidence of
                                         * this canvas rather than a rule */

    cf_gcmode(2);
    cf_fill(cf_fsi.seg, 0, CF_VSTRIDE * CF_VROWS, 0);
    cf_panel(cf_fsi.seg, CF_VSTRIDE, CF_PANEL);
    cf_diag(cf_fsi.seg, CF_VSTRIDE, CF_PANEL, CF_VROWS - CF_PANEL);
    cf_gcmode(0);

    cf_tty(0, 0, "STATIC PANEL - below the line compare", 15);
    cf_tty(3, 0, "this panel must NOT move or pan", 14);

    /* Two captions a row, one at each end of the 640-wide canvas, so that a
     * single screendump says where in the sweep it was taken: the screen
     * shows 320 pixels of 640 and cannot hold both. */
    cf_tty(5, 0, "TOP HALF - LEFT EDGE of the canvas", 15);
    cf_tty(5, 40, "TOP HALF - RIGHT EDGE, scroll to me", 15);
    cf_tty(9, 0, "<<< one pixel a tick: start address", 15);
    cf_tty(9, 40, "plus pel panning (AC 13) >>>", 15);
    cf_tty(13, 0, "the white posts are 64 pixels apart", 15);
    cf_tty(13, 40, "os88_fsx_wait(FSXW_TICK) paces it", 15);
    cf_tty(17, 0, "LEFT: nothing below the compare", 15);
    cf_tty(17, 40, "RIGHT: ...moves with this half", 15);
    cf_tty(21, 0, "any key = next stage, Esc = leave", 15);
    cf_tty(21, 40, "any key = next stage, Esc = leave", 15);

    cf_split(CF_LC);
    cf_panmode(1);                      /* ...and the panel does not pan */
    cf_scroll(CF_SCROLLTOP, 0);

    lc18 = cf_crtcget(0x18);
    lc07 = cf_crtcget(0x07);
    lc09 = cf_crtcget(0x09);
    ac10 = cf_acget(0x10);

    cf_line[0] = 0;
    cf_app("read back  18=");
    cf_apph2((unsigned)lc18);
    cf_app(" 07=");
    cf_apph2((unsigned)lc07);
    cf_app(" 09=");
    cf_apph2((unsigned)lc09);
    cf_app(" AC10=");
    cf_apph2((unsigned)ac10);
    cf_tty(1, 0, cf_line, 15);

    for (;;) {
        k = os88_fsx_key(0);
        if (k != 0) {
            cf_nosplit();
            cf_offset(20);              /* mode 0Dh's own 40-byte line */
            cf_scroll(0, 0);
            cf_bdacols(oldcols);
            return ((k & 0xFF) == 27) ? 1 : 0;
        }

        pan++;
        if (pan >= CF_PANMAX)
            pan = 0;
        cf_vsync();
        cf_scroll((unsigned)(CF_SCROLLTOP + (pan >> 3)), pan & 7);
        cf_frames++;

        /* EVERY FRAME, and the pel value is the reason. Printed every eighth
         * frame it was always a multiple of eight, because the counter and
         * the pan are the same number - so every screendump showed pel 0 and
         * the one thing this stage exists to prove, that the step is smaller
         * than a byte, was the one thing invisible in it. */
        cf_line[0] = 0;
        cf_app("pan ");
        cf_appn(pan);
        cf_app(" pel ");
        cf_appn(pan & 7);
        cf_app(" addr ");
        cf_appn(CF_SCROLLTOP + (pan >> 3));
        cf_app("  frames ");
        cf_appn(cf_frames);
        cf_app("     ");
        cf_tty(2, 0, cf_line, 15);
        os88_fsx_wait(OS88_FSXW_TICK);
    }
}

/* ---------------------------------------------------------------------------
 * STAGE 3 - THE MODE CHANGE, 0Dh -> 12h, in the middle of the bracket.
 * ------------------------------------------------------------------------- */
static int cf_stage_12h(void)
{
    if (os88_fsx_mode(OS88_FSXM_VGA12, &cf_fsi) != 0) {
        cf_tty(20, 0, "mode 12h REFUSED", 12);
        return cf_pause();
    }
    cf_modes++;
    cf_stage = 3;

    cf_surfr = os88_fsx_surf(&cf_surf);

    cf_gcmode(2);
    cf_ramp(cf_fsi.seg, cf_fsi.stride, 240, 120);
    cf_gcmode(0);

    cf_head("12h - CHANGED MID-BRACKET");
    cf_fsi_lines(3);

    cf_line[0] = 0;
    cf_app("surf ");
    cf_appn(cf_surf.x1);
    cf_app(",");
    cf_appn(cf_surf.y1);
    cf_app(" - ");
    cf_appn(cf_surf.x2);
    cf_app(",");
    cf_appn(cf_surf.y2);
    cf_tty(6, 0, cf_line, 15);

    cf_ovl_line(8);
    cf_tty(11, 0, "640x480x16: same bracket, second mode", 15);
    cf_tty(26, 0, "any key = back to 0Dh, Esc = leave", 14);
    return cf_pause();
}

/* ---------------------------------------------------------------------------
 * STAGE 4 - ...and back to 0Dh, which is the half of the mode change that a
 * preview screen actually needs and the half that is easy to leave untested.
 * ------------------------------------------------------------------------- */
static int cf_stage_back(void)
{
    if (os88_fsx_mode(OS88_FSXM_VGA0D, &cf_fsi) != 0) {
        cf_tty(20, 0, "mode 0Dh REFUSED on the way back", 12);
        return cf_pause();
    }
    cf_modes++;
    cf_stage = 4;

    cf_gcmode(2);
    cf_ramp(cf_fsi.seg, cf_fsi.stride, 104, 48);
    cf_gcmode(0);

    cf_head("0Dh AGAIN");
    cf_fsi_lines(3);
    cf_ovl_line(6);
    cf_tty(9, 0, "three mode sets, one bracket", 15);
    cf_tty(21, 0, "any key = leave the bracket", 14);
    return cf_pause();
}

/* ---------------------------------------------------------------------------
 * THE BRACKET ITSELF. It does not return until this function does.
 * ------------------------------------------------------------------------- */
void os88_fsx_main(void *win)
{
    cf_entered++;
    cf_frames = 0;
    cf_modes = 0;
    cf_stage = 0;

    cf_caps_in = (unsigned)os88_fsx_caps(win, &cf_kind_in);

    if (cf_stage_info())
        return;
    if (cf_stage_split())
        return;
    if (cf_stage_12h())
        return;
    cf_stage_back();
}

/* --- the window ----------------------------------------------------------- */

static void cf_draw(void *win)
{
    int x, y;

    if (os88_wm_geom(win, &cf_sz) != 0)
        return;
    os88_wm_content(win, &cf_org);
    x = cf_org.x + 8;
    y = cf_org.y + 8;

    cf_caps_out = (unsigned)os88_fsx_caps(win, &cf_kind_out);

    os88_font_run(x, y, "C exclusive-bracket gate (SPEC.md 53)",
                  OS88_BLACK, OS88_WHITE);
    y += 16;

    cf_line[0] = 0;
    cf_app("caps ");
    cf_apph(cf_caps_out);
    cf_app("  kind ");
    cf_appn(cf_kind_out);
    cf_app("  (in-bracket ");
    cf_apph(cf_caps_in);
    cf_app(" ");
    cf_appn(cf_kind_in);
    cf_app(")   ");
    os88_font_run(x, y, cf_line, OS88_BLACK, OS88_WHITE);
    y += 12;

    /* cf_entered is bumped by os88_fsx_main() and by nothing else, so a
     * non-zero number here is the bracket having run and returned. */
    cf_line[0] = 0;
    cf_app("entered ");
    cf_appn(cf_entered);
    cf_app("  run ");
    cf_appn(cf_runret);
    cf_app("  stage ");
    cf_appn(cf_stage);
    cf_app("  modes ");
    cf_appn(cf_modes);
    cf_app("  frames ");
    cf_appn(cf_frames);
    cf_app("   ");
    os88_font_run(x, y, cf_line, OS88_BLACK, OS88_WHITE);
    y += 12;

    cf_line[0] = 0;
    cf_app("overlay ");
    if (cf_ovl_where == 1)
        cf_app("pre-loaded");
    else if (cf_ovl_where == 2)
        cf_app("in-bracket");
    else
        cf_app("untouched ");
    cf_app("  probe ");
    cf_appn(cf_ovl);
    cf_app("  pre ");
    cf_appn(cf_pre_ok);
    cf_app("    ");
    os88_font_run(x, y, cf_line, OS88_BLACK, OS88_WHITE);
    y += 16;

    os88_font_run(x, y, "SPACE: force CFSX.OVL resident, then enter",
                  OS88_BLACK, OS88_WHITE);
    y += 12;
    os88_font_run(x, y, "L:     enter first, load CFSX.OVL inside",
                  OS88_BLACK, OS88_WHITE);
    y += 12;
    os88_font_run(x, y, "N:     ...and with NO fullscreen window under it",
                  OS88_BLACK, OS88_WHITE);
    y += 12;
    os88_font_run(x, y, "R: reset      inside: any key = next, Esc = out",
                  OS88_BLACK, OS88_WHITE);
}

void os88_paint(void *win)
{
    cf_draw(win);
}

/* cf_enter - the whole gate.
 *
 * The SPEC.md 11.2 fullscreen window is stacked UNDER the bracket the way
 * apps/missile stacks it: what it buys is that the screen the desktop comes
 * back to looks like this program rather than like the desktop, which is what
 * a game's own preview screen wants. It is not the bracket and it is not
 * required by it - a refusal here is not a reason not to enter.
 *
 * pre != 0 forces the module resident BEFORE the bracket is entered, which is
 * SPEC.md 92.5's rule. When that fails, the flag says so and the bracket
 * never asks again: cc_ovneed's refusal path ends in a kernel toast, and a
 * toast paints desktop geometry into whatever foreign mode is on the glass. */
static void cf_enter(void *win, int pre, int full)
{
    int fs;

    cf_ovl = 0;
    cf_ovl_where = 0;
    cf_pre_ok = 0;
    cf_want_pre = pre;
    cf_want_fs = full;

    if (pre) {
        cf_ovl_where = 1;
        cf_ovl = ovl_probe(1, 2, 3, 4); /* on the DESKTOP, where a refusal's
                                         * toast belongs */
        cf_pre_ok = cf_ovl ? 1 : 0;
    }

    fs = -1;
    if (full)
        fs = os88_fullscreen(win, 1);
    cf_runret = os88_fsx_run(win, 0);
    if (fs == 0)
        os88_fullscreen(win, 0);
    cf_draw(win);
}

void os88_onkey(int ascii, int scan, void *win)
{
    (void)scan;
    if (ascii == 'r' || ascii == 'R') {
        cf_entered = 0;
        cf_runret = 0;
        cf_stage = 0;
        cf_modes = 0;
        cf_frames = 0;
        cf_ovl = 0;
        cf_ovl_where = 0;
        cf_pre_ok = 0;
        cf_caps_in = 0;
        cf_kind_in = 0;
        cf_draw(win);
    } else if (ascii == ' ') {
        cf_enter(win, 1, 1);
    } else if (ascii == 'l' || ascii == 'L') {
        cf_enter(win, 0, 1);
    } else if (ascii == 'n' || ascii == 'N') {
        cf_enter(win, 0, 0);
    }
}

void os88_onclick(int x, int y, void *win)
{
    (void)x;
    (void)y;
    cf_enter(win, 1, 1);
}

void *os88_main(void)
{
    void *win;

    win = os88_wm_create(CF_X, CF_Y, CF_W, CF_H, "C FSX Gate");
    if (win == 0)
        return 0;
    os88_wm_snap(win, 1);
    return win;
}
