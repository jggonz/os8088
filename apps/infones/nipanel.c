/* ============================================================================
 * os8088 - apps/infones/nipanel.c      the FRONT PANEL (SPEC.md 91.7)
 *
 * Part of INFONES (SPEC.md 91). Derived from InfoNES fe3295c0 under
 * Apache-2.0 - see apps/infones/LICENSE.TXT. Section 4(b): derived from
 * InfoNES fe3295c0, restructured for 8086 real mode.
 *
 * WHAT THIS FILE FOLLOWS, LINE BY LINE: the seven ROM-info rows and their
 * ORDER are InfoNES's own Help > ROM information -
 * src/linux/InfoNES_System_Linux.cpp:313-319, identically
 * src/win32/InfoNES_System_Win.cpp:504-508 - down to the labels `Mapper`,
 * `PRG ROM`, `CHR ROM`, `Mirroring`, `SRAM`, `4 Screen`, `Trainer`, the
 * `V`/`H` and `Yes`/`No` values, and the units: PRG is counted in 16KB banks
 * and PRINTED in KB (byRomSize * 16), CHR in 8KB banks printed in KB
 * (byVRomSize * 8). The Linux front end is the one taken because it is the
 * same file the About box is quoted from (SPEC.md 91.2.2), so the two agree
 * about their separator. Nothing else in this file is InfoNES's: the panel
 * itself is os8088's, because InfoNES's window is a PICTURE and this one is a
 * readout (SPEC.md 91.9).
 *
 * #included into apps/infones/infones.c - ONE translation unit (73.1).
 *
 * ----------------------------------------------------------------------------
 * IT IS SHADOWED AND DELTA-DRAWN, AND THAT IS THE DESIGN RATHER THAN THE
 * POLISH (PERFORMANCE.md rule 1, LESSONS.md 6)
 * ----------------------------------------------------------------------------
 * `ni_txt[f]` is what field f SHOULD show and `ni_sh[f]` is what is ON THE
 * GLASS. A repaint compares the two and letters only the fields that differ,
 * ONE os88_font_run each - never a fill and then glyphs, which writes every
 * one of those pixels twice with a visible gap between the passes on the
 * machine this OS is for (SPEC.md 6.6). A field update is therefore ONE call
 * and about 40 cells - ~36 ms on a 4.77 MHz 8088 - and a whole panel is 13
 * calls and ~470 ms, which is what thirteen rows of text cost whoever draws
 * them.
 *
 * THE SHADOW IS DROPPED WHENEVER ANYTHING ELSE OWNS THE GLASS: a W_PAINT (the
 * kernel has just whitened our content), the About panel, the Standard File
 * dialog, and - from wave 2 - the bracket's exit repaint. A shadow that
 * describes what a dialog covered is a panel that draws nothing and is wrong
 * (LESSONS.md 6: after a Save the row still read `iH#there bold`).
 * ==========================================================================*/

/* --- the fields, in the order they are drawn ----------------------------- */
#define NI_F_ROM     0              /* the loaded ROM's name */
#define NI_F_MAPPER  1              /* InfoNES's seven, in InfoNES's order */
#define NI_F_PRG     2
#define NI_F_CHR     3
#define NI_F_MIRROR  4
#define NI_F_SRAM    5
#define NI_F_FOUR    6
#define NI_F_TRAIN   7
#define NI_F_RATE    8              /* BOTH rates (SPEC.md 91.7.1) */
#define NI_F_KEYS1   9              /* the controls legend... */
#define NI_F_KEYS2  10
#define NI_F_STATE  11              /* No ROM / Ready / Running / a refusal */
#define NI_F_FACT   12              /* THE FACT LINE (SPEC.md 91.7.2) */
#define NI_F_N      13

#define NI_FCELLS   40              /* the widest field, in glyph cells */
#define NI_FMAX     (NI_FCELLS + 2) /* ...and the buffer that holds one */

/* THE WINDOW'S AUTHORED SIZE. 13 rows at a 10-pixel pitch is 130, plus 6 of
 * top margin and 4 of bottom, which is 140 of content; 40 cells is 320 pixels
 * plus 2 x 5 of margin. os88_main CLAMPS the height to the live desktop
 * before it asks (a 200-line adapter's is about 148 rows), and this file lays
 * out from the geometry it actually got - never from these numbers. */
#define NI_PANEL_W  334
#define NI_PANEL_H  140
#define NI_ROW_H    10
#define NI_MARGIN_X 5
#define NI_MARGIN_Y 5

static char ni_txt[NI_F_N][NI_FMAX];    /* what each field should show */
static char ni_sh[NI_F_N][NI_FMAX];     /* ...and what is on the glass */
static char ni_padbuf[NI_FMAX];         /* the padded copy that is drawn.
                                         * NOT `ni_pad`, which is nippu.c's
                                         * controller latch: ONE TRANSLATION
                                         * UNIT IS ONE NAMESPACE (LESSONS.md
                                         * 3), and the two collided on the
                                         * first host compile */

static int ni_sh_cells = -1;            /* the field width the shadow was
                                         * written at (SPEC.md 39: the live
                                         * screen can change under us) */

/* the live content box, refreshed by ni_geom */
static int ni_gox, ni_goy, ni_gw, ni_gh;

/* the About panel's latch and rect (niabout.c owns the drawing) */
static int ni_abt;
static int ni_abt_x, ni_abt_y, ni_abt_w, ni_abt_h;

/* --- the fact line's sentences (SPEC.md 91.7.2) --------------------------
 * A MENU_DIS item cannot be clicked - os88api.inc:1945-1972 has the kernel
 * refuse to highlight or select one - so there is no click, no command and
 * nothing to toast, and a refusal handler written for a greyed item is
 * unreachable code. The SHORT form is in the label; THE SENTENCE IS HERE, and
 * this is where it is read.
 *
 * They are indexed by the greying predicate that is TRUE, most specific
 * first, so that the line always says the reason a user is most likely to be
 * asking about. */
#define NI_FACT_NONE  0
#define NI_FACT_MUTE  1
#define NI_FACT_CLIP  2
#define NI_FACT_FSX   3
#define NI_FACT_NOROM 4
#define NI_FACT_N     5

/* EVERY SENTENCE IS <= NI_FCELLS AND IS THE SPEC's OWN CHARACTERS. SPEC.md
 * 91.10 is the contract and this row is what a user reads, so the two are the
 * same string and not two paraphrases of one - the first draft of 91.10
 * quoted an 82-character sentence at a field 40 cells wide, which is a
 * contract stating something the glass cannot hold.
 *
 * NI_FACT_PIC IS GONE, AND ITS DELETION IS THE POINT. Wave 1 greyed every
 * Options item with `No picture yet: this build reads ROMs.` - a fact about
 * that BUILD - and wave 2 made it false by writing the composer and the
 * bracket. A greying may not outlive its reason (SPEC.md 47), so the item,
 * the constant and the sentence go together in one edit rather than the
 * sentence being left behind to describe a program that no longer exists.
 *
 * ...AND ITS DELETION LEFT A HOLE, WHICH NI_FACT_NOROM FILLS. Four Options
 * items - the frame-skip trio and Full screen - grey on `there is no ROM`,
 * and after NI_FACT_PIC went there was no sentence anywhere for that state:
 * a user with nothing loaded saw four dithered items and a fact line talking
 * about the clip. A greying names a fact and the fact has to be READABLE. */
static const char *ni_facts[NI_FACT_N] = {
    "",
    /* Options > Mute. The 2A03 mixes five voices and the PC speaker plays
     * one square wave (SPEC.md 91.10). The pulse-1 arithmetic that WOULD be
     * the follow-up is recorded in the SPEC, not here. */
    "No APU: five voices, one speaker.",
    /* Options > Clip. The bracket's mode is 13h on every tier (SPEC.md
     * 91.6.1), which is 200 rows, so 240 NES rows are ALWAYS reduced and
     * there is nothing for a toggle to toggle. Shipping it as a live item
     * that is structurally inert is the exact shape LESSONS.md 1 names. */
    "Clip is forced at 320x200: 240 rows.",
    /* Options > Full screen, on a display whose OSAPI_FSX_CAPS mask does not
     * carry FSXM_VGA13. IT NAMES THE MODE AND NOT THE DISPLAY'S WHOLE
     * CAPABILITY, because wave 1's `This display offers no fullscreen mode.`
     * became FALSE the moment the predicate narrowed to 13h by name:
     * kernel/fsx.inc's fsx_capstab gives CGA and EGA FSXM_CGA320 (a real
     * 320x200x4 fullscreen mode) and Hercules FSXM_HERC (720x348), so those
     * displays DO offer one - what they do not offer is 320x200x256, which is
     * a fact about THIS BUILD's present and not about the glass. SPEC.md 47
     * is grey a FACT, and a sentence the code itself knows to be untrue is
     * not one. */
    "No 320x200x256 mode on this display.",
    /* The four Options items that need a machine: the frame-skip trio and
     * Full screen. It is the predicate they already grey on (ni_have_rom), so
     * this sentence is that predicate said out loud. */
    "No ROM loaded: open one to run it."
};
static int ni_fact;

/* --- THE FACT LINE IS ONE ROW AND MORE THAN ONE FACT IS TRUE -------------
 * There is exactly one fact row (thirteen is what a 200-line adapter's
 * desktop holds - see NI_PANEL_H, and the CGA window ends on the last one),
 * and more than one fact is true at once: the forced clip and the silent APU
 * always, no 320x200x256 mode on some displays, and no ROM until one is
 * opened. A fixed pick showed one of them and hid
 * the others forever - the greyed `Mute (no APU)` item's SENTENCE was
 * reachable only by pressing M, which appears in no menu, no legend and no
 * dialog.
 *
 * So the row is a ROTATION over the facts that are TRUE, advanced by
 * ni_menu_state() - which runs after a launch, a load, a reset, a stop and a
 * refusal, and NOT from a repaint. That trigger is the point: a window
 * dragged across the screen repaints many times and the row does not move,
 * so the rotation is never a flicker; and a user who does anything at all
 * sees every fact this build is greying things for. SPEC.md 91.7.2 records
 * it. The M key still jumps straight to the Mute sentence. */
static int ni_fact_ix;              /* which of the true facts is showing */
static int ni_fact_started;         /* ...and whether the first pick was made,
                                     * so that the launch shows the FIRST fact
                                     * rather than the second */

/* --- the two rates, and the state line -----------------------------------
 * The three words themselves are nirun.c's, beside ni_rate_calc, which is the
 * only thing that ever writes them: a measurement declared in the file that
 * DRAWS it and written by the file that TAKES it is a number with two owners.
 * This file only formats them (ni_panel_rate below). */

/* ni_streq - there is no strcmp in the SDK (SPEC.md 73.9). */
static int ni_streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (*a == *b);
}

static int ni_geom(void *win)
{
    static struct os88_pt org;      /* rule 1: an out-parameter is a STATIC */
    static struct os88_size sz;

    if (os88_wm_geom(win, &sz) < 0)
        return -1;                  /* not visible: nothing to draw */
    os88_wm_content(win, &org);
    ni_gox = org.x;
    ni_goy = org.y;
    ni_gw = sz.w;
    ni_gh = sz.h;
    return 0;
}

/* ni_setfield - what a field SHOULD show. It draws nothing: the difference is
 * spent at the next ni_panel_paint, which is what makes twelve field updates
 * in one command cost one repaint rather than twelve. */
static void ni_setfield(int f, const char *s)
{
    /* NI_FCELLS + 1 AND NOT NI_FMAX, so that no field text can ever be wider
     * than the widest field the layout admits. The PAPER branch below draws
     * ni_txt UNPADDED and unclipped - that is the whole of what makes a
     * thirteen-row repaint affordable - and with a 41-character string in a
     * 40-cell field it would letter the last cell onto the border and the
     * desktop, outside any clip region (this package arms none). Clamping at
     * the one place text ENTERS the panel closes it for all four repaint
     * branches instead of once per branch. */
    os88_strcpy(ni_txt[f], s, NI_FCELLS + 1);
}

/* --- THE THREE WAYS A SHADOW STOPS BEING TRUE, AND THEY ARE NOT THE SAME ---
 * All three mean "the shadow no longer describes the glass" and they cost
 * different amounts to repair, which is why they are three sentinels and not
 * one. The first draft had only one and the harness caught it in a step: a
 * long refusal followed by `Ready` left `128KB, file is 40KB` on the row,
 * because "unknown" had been treated as "paper".
 *
 *   UNKNOWN - something else OWNED those pixels and what is on them cannot be
 *   known: the Standard File dialog, the About panel, a menu pull-down. The
 *   repair is a run padded to the FULL field width, because the thing that
 *   has to be erased may be anything and may be longer than the new text.
 *
 *   PAPER - we have just whitened those pixels ourselves, in os88_paint's own
 *   WF_OWNBG fill. There is nothing to erase, so the repair is the text and
 *   only the text: 225 cells for a whole panel instead of 468, which is 219
 *   ms of a 4.77 MHz 8088 (PERFORMANCE.md rule 2).
 *
 *   PART - the same as PAPER, and the row was ALREADY CORRECT before the
 *   fill, so only the CELLS the fill covered have to be put back. SPEC.md
 *   11.90.2's own sentence for this slot is "a partial repaint is per ELEMENT,
 *   not per pixel", and here the element is a row: a window dragged off the
 *   left 48 pixels of the panel damages 6 cells of 40, and re-lettering all
 *   thirteen rows at full width costs ~245 cells / ~230 ms of a 4.77 MHz 8088
 *   where 78 cells / ~76 ms was owed. The distinction from PAPER is the whole
 *   of the safety: a row may be narrowed only when what is OUTSIDE the fill is
 *   known to be right, which is exactly "the shadow matched the text", and any
 *   other row - one a dialog covered, one whose text changed - is repainted
 *   whole. */
#define NI_SH_UNKNOWN 1             /* none is a byte a field text can */
#define NI_SH_PAPER   2             /* begin with, so no accidental match */
#define NI_SH_PART    3

/* the damaged CELL span the PART rows are cut to, banked by
 * ni_shadow_paper_rect from the same rect the fill used */
static int ni_pap_c0, ni_pap_c1;

static void ni_shadow_drop(void)
{
    int f;

    for (f = 0; f < NI_F_N; f++)
        ni_sh[f][0] = NI_SH_UNKNOWN;
}

/* ni_shadow_paper - every field's pixels are ours and white. */
static void ni_shadow_paper(void)
{
    int f;

    for (f = 0; f < NI_F_N; f++)
        ni_sh[f][0] = NI_SH_PAPER;
}

/* ni_shadow_paper_rect - ...and the WF_OWNBG partial expose: only the fields
 * whose 8-pixel glyph band intersects the band we just filled, and only the
 * CELLS of them that it covered. x1/y1/x2/y2 are SCREEN coordinates, which is
 * what os88_wm_damage answers in (SPEC.md 11.90.2), and ni_geom must have run.
 *
 * The cut is on the cell grid by construction: os88_wm_snap(win, 1) puts the
 * content origin on a multiple of 8 (infones.c), so a cell boundary and a
 * pixel column agree. */
static void ni_shadow_paper_rect(int x1, int y1, int x2, int y2)
{
    int f, y, x0;

    x0 = ni_gox + NI_MARGIN_X;
    ni_pap_c0 = (x1 <= x0) ? 0 : (x1 - x0) / 8;
    ni_pap_c1 = (x2 < x0) ? -1 : (x2 - x0) / 8;

    for (f = 0; f < NI_F_N; f++) {
        y = ni_goy + NI_MARGIN_Y + f * NI_ROW_H;
        if (y + 7 >= y1 && y <= y2) {
            /* PART only where the row was right before the fill: then the
             * cells outside the fill are still right and only the covered
             * ones are owed. Anything else - a row a dialog owned, a row
             * whose text has moved on - is paper for its whole width. */
            ni_sh[f][0] = ni_streq(ni_txt[f], ni_sh[f])
                            ? NI_SH_PART : NI_SH_PAPER;
        }
    }
}

/* ni_fact_rotate - advance the fact row to the next TRUE fact. `nofsx` and
 * `norom` are the two predicates that are not constants in this build, and
 * each is passed in rather than asked here: they live in nimenu.c, which is
 * compiled after this part, and two spellings of a predicate are two answers
 * waiting to disagree. */
static void ni_fact_rotate(int nofsx, int norom)
{
    static unsigned char t[NI_FACT_N];  /* rule 1 again: an indexed array is
                                         * an ADDRESSED object and may not be
                                         * an automatic (SPEC.md 73.5) */
    int n;

    n = 0;
    if (norom)
        t[n++] = NI_FACT_NOROM;     /* FIRST, because it is the most specific:
                                     * it is the state four of the six Options
                                     * items are dithered by, and the one a
                                     * user with an empty panel is asking
                                     * about */
    t[n++] = NI_FACT_CLIP;          /* true in every mode this build enters,
                                     * and the one a user meets first */
    if (nofsx)
        t[n++] = NI_FACT_FSX;
    t[n++] = NI_FACT_MUTE;          /* ...and the PERMANENT one, whose item
                                     * also carries the short form in its own
                                     * label */

    if (ni_fact_started)
        ni_fact_ix++;
    ni_fact_started = 1;
    if (ni_fact_ix >= n || ni_fact_ix < 0)
        ni_fact_ix = 0;
    ni_fact = (int)t[ni_fact_ix];
}

/* ni_panel_paint - the repaint. `whole` says the kernel (or a panel) has just
 * owned our content, so the shadow describes nothing.
 *
 * The gfx lock must be HELD by the caller: os88_paint and os88_oncmd are
 * entered with it, and os88_onwake takes it around its own burst. */
static void ni_panel_paint(void *win, int whole)
{
    int f, y, cells, i, n, a, b, lt, ls, gc;

    if (ni_geom(win) < 0)
        return;
    if (whole)
        ni_shadow_drop();
    if (ni_abt)
        return;                     /* the About panel owns the box; its close
                                     * drops the shadow and repaints */

    cells = (ni_gw - 2 * NI_MARGIN_X) / 8;
    if (cells > NI_FCELLS)
        cells = NI_FCELLS;
    if (cells < 1)
        return;
    if (cells != ni_sh_cells) {
        /* THE FIELD GOT NARROWER OR WIDER (an adapter change, a display
         * swap): the shadow describes cells that are no longer the same
         * cells, so it describes nothing. */
        ni_shadow_drop();
        ni_sh_cells = cells;
    }

    for (f = 0; f < NI_F_N; f++) {
        y = NI_MARGIN_Y + f * NI_ROW_H;
        if (y + 8 > ni_gh)
            break;                  /* THE BOX IS SHORTER THAN THE PANEL. A
                                     * field below it is not drawn at all -
                                     * never drawn outside the content, which
                                     * on a 200-line adapter is what put a
                                     * whole About box's OK button on the
                                     * DESKTOP once (LESSONS.md 8) */
        if (ni_streq(ni_txt[f], ni_sh[f]))
            continue;               /* THE DELTA: only a field whose text
                                     * changed is re-lettered */

        if (ni_sh[f][0] == NI_SH_PART) {
            /* OUR OWN PAPER, AND ONLY THE CELLS UNDER IT. The row was right
             * before the fill, so what is owed is the span the fill covered
             * and nothing either side of it. Nothing is padded here for the
             * PAPER branch's reason, and the span is clipped to the text: past
             * its end the fill left white, which is what that part of the row
             * held anyway. */
            a = ni_pap_c0;
            b = ni_pap_c1;
            if (b >= cells)
                b = cells - 1;
            lt = (int)os88_strlen(ni_txt[f]);
            if (b >= lt)
                b = lt - 1;
            if (a <= b) {
                for (i = a; i <= b; i++)
                    ni_padbuf[i - a] = ni_txt[f][i];
                ni_padbuf[b - a + 1] = 0;
                os88_font_run(ni_gox + NI_MARGIN_X + a * 8, ni_goy + y,
                              ni_padbuf, OS88_BLACK, OS88_WHITE);
            }
            os88_strcpy(ni_sh[f], ni_txt[f], NI_FMAX);
            continue;
        }

        if (ni_sh[f][0] == NI_SH_PAPER) {
            /* THE GLASS UNDER THIS ROW IS OUR OWN WHITE FILL: there is
             * nothing to erase and the run is the TEXT, unpadded. Padding it
             * would write paper over paper - 40 cells to say `Ready`, 36 ms
             * of a 4.77 MHz 8088 per row, 243 wasted cells on a whole
             * repaint, which is more than half of it (cwchrome.c:600-607 is
             * the same arithmetic on Word's status line). */
            n = 0;
            while (ni_txt[f][n] && n < cells) {     /* CLAMPED TO THE LIVE
                                                     * CELL COUNT, which the
                                                     * other three branches
                                                     * already are: ni_setfield
                                                     * bounds the text at
                                                     * NI_FCELLS, and this
                                                     * bounds it at what THIS
                                                     * window's content box
                                                     * actually holds */
                ni_padbuf[n] = ni_txt[f][n];
                n++;
            }
            ni_padbuf[n] = 0;
            os88_font_run(ni_gox + NI_MARGIN_X, ni_goy + y, ni_padbuf,
                          OS88_BLACK, OS88_WHITE);
            os88_strcpy(ni_sh[f], ni_txt[f], NI_FMAX);
            continue;
        }

        if (ni_sh[f][0] == NI_SH_UNKNOWN) {
            /* SOMETHING ELSE OWNED THESE PIXELS and what is on them cannot be
             * known - a dialog, the About panel, a pull-down. The erase has
             * to cover the whole field, because what is under it may be
             * longer than the new text and may not be ours at all. One run,
             * paper and glyphs in one pass. */
            n = 0;
            while (ni_txt[f][n] && n < cells) {
                ni_padbuf[n] = ni_txt[f][n];
                n++;
            }
            for (i = n; i < cells; i++)
                ni_padbuf[i] = ' ';
            ni_padbuf[cells] = 0;
            os88_font_run(ni_gox + NI_MARGIN_X, ni_goy + y, ni_padbuf,
                          OS88_BLACK, OS88_WHITE);
            os88_strcpy(ni_sh[f], ni_txt[f], NI_FMAX);
            continue;
        }

        /* A DELTA AGAINST TEXT THAT IS REALLY THERE. Pad only as far as the
         * LONGER of the two strings - past that the glass is already paper -
         * and then letter only the SPAN that differs. `Ready` -> `Running`
         * is six cells and not thirty-six. */
        lt = (int)os88_strlen(ni_txt[f]);
        ls = (int)os88_strlen(ni_sh[f]);
        n = (lt > ls) ? lt : ls;
        if (n > cells)
            n = cells;
        for (i = 0; i < n; i++)
            ni_padbuf[i] = (i < lt) ? ni_txt[f][i] : ' ';
        ni_padbuf[n] = 0;

        a = 0;
        while (a < n) {
            gc = (a < ls) ? ni_sh[f][a] : ' ';
            if (ni_padbuf[a] != gc)
                break;
            a++;
        }
        b = n - 1;
        while (b > a) {
            gc = (b < ls) ? ni_sh[f][b] : ' ';
            if (ni_padbuf[b] != gc)
                break;
            b--;
        }
        if (a <= b) {
            ni_padbuf[b + 1] = 0;
            os88_font_run(ni_gox + NI_MARGIN_X + a * 8, ni_goy + y,
                          ni_padbuf + a, OS88_BLACK, OS88_WHITE);
        }
        /* ...and the shadow is stored even when a > b, which is the case
         * where the two texts differ only PAST the field width: nothing was
         * drawn because nothing could be, and re-testing it every repaint
         * would be a field that is permanently dirty. */
        os88_strcpy(ni_sh[f], ni_txt[f], NI_FMAX);
    }
}

/* ni_repaint - from a context that holds the lock and has changed fields. */
static void ni_repaint(void *win)
{
    ni_panel_paint(win, 0);
}

/* ni_panel_rate - both figures, composed from integers by os88_utoa (SPEC.md
 * 91.7.1). The rendered one is held in HUNDREDTHS and written as
 * utoa(t/100), '.', two digits, because there is no formatter and `float` is
 * poisoned - `%4.2f` cannot be written here at all.
 *
 * HUNDREDTHS AND NOT TENTHS, and the change is a measurement rather than a
 * preference: a machine that paints sixty frames a second is 0.017 s a frame,
 * which in tenths is `0.0` - a field that has stopped saying anything at
 * exactly the end of the range where the number is good news. The 8088's
 * ~2.2 s a frame reads `2.20` and a 486's `0.02`, and both fit. */
static void ni_panel_rate(void)
{
    unsigned t;

    if (!ni_rate_known) {
        ni_setfield(NI_F_RATE, "NES  --%    --  s/frame");
        return;
    }
    os88_strcpy(ni_msg, "NES  ", NI_MSGMAX);
    ni_appnum(ni_msg, ni_rate_nes);
    ni_app(ni_msg, "%    ");
    t = ni_rate_spf;
    ni_appnum(ni_msg, t / 100);
    ni_app(ni_msg, ".");
    ni_num[0] = (char)('0' + (int)((t / 10) % 10));
    ni_num[1] = (char)('0' + (int)(t % 10));
    ni_num[2] = 0;
    ni_app(ni_msg, ni_num);
    ni_app(ni_msg, "  s/frame");

    /* ...AND THE FRAMES THE MACHINE COULD NOT KEEP, when there are any
     * (nirun.c's ni_overload, the plan's R6). The pacer drops the debt past
     * four catch-up frames a wake and counts the drop, and until this row
     * carried it the counter was write-only: the one claim a reader would
     * want it for - "the 8088 lives in that branch permanently", which is
     * SPEC.md 91.12's whole demonstration - was unobservable on the machine
     * it is about. Nothing is said when the count is zero, so a machine that
     * keeps up shows the two rates and no third number.
     *
     * APPENDED ONLY IF IT FITS. The row is 40 cells (NI_FCELLS) and a fast
     * machine's own figures are wider than an 8088's; ni_setfield would
     * truncate the SUFFIX onto the rates, which is a field that has stopped
     * saying the thing it exists to say. */
    if (ni_overload) {
        unsigned d;
        int len;

        d = ni_overload;
        if (d > 9999)
            d = 9999;
        os88_utoa(d, ni_num);
        len = (int)os88_strlen(ni_msg) + 7 + (int)os88_strlen(ni_num);
        if (len <= NI_FCELLS) {
            ni_app(ni_msg, "  drop ");
            ni_app(ni_msg, ni_num);
        }
    }
    ni_setfield(NI_F_RATE, ni_msg);
}

/* ni_panel_rom - the seven InfoNES rows, in InfoNES's order and with
 * InfoNES's labels and values. With no ROM every value is `-`: a panel that
 * keeps the last ROM's numbers after a refusal is telling a lie. */
static void ni_panel_rom(void)
{
    int have;

    have = (ni_state == NI_ST_READY || ni_state == NI_ST_RUN);

    os88_strcpy(ni_msg, "ROM : ", NI_MSGMAX);
    ni_app(ni_msg, have ? ni_romname : "(none)");
    ni_setfield(NI_F_ROM, ni_msg);

    if (!have) {
        ni_setfield(NI_F_MAPPER, "Mapper : -");
        ni_setfield(NI_F_PRG,    "PRG ROM : -");
        ni_setfield(NI_F_CHR,    "CHR ROM : -");
        ni_setfield(NI_F_MIRROR, "Mirroring : -");
        ni_setfield(NI_F_SRAM,   "SRAM : -");
        ni_setfield(NI_F_FOUR,   "4 Screen : -");
        ni_setfield(NI_F_TRAIN,  "Trainer : -");
        return;
    }

    os88_strcpy(ni_msg, "Mapper : ", NI_MSGMAX);
    ni_appnum(ni_msg, ni_mapper);
    ni_setfield(NI_F_MAPPER, ni_msg);

    os88_strcpy(ni_msg, "PRG ROM : ", NI_MSGMAX);
    ni_appnum(ni_msg, ni_prg16 * 16);   /* byRomSize * 16, printed in KB */
    ni_app(ni_msg, "KB");
    ni_setfield(NI_F_PRG, ni_msg);

    /* CHR ROM, or CHR RAM where the header asks for none: a game that writes
     * its own tiles through $2006/$2007 is a shipped case (SPEC.md 91.9), and
     * printing `CHR ROM : 0KB` for it would be true of the file and useless
     * about the machine. */
    os88_strcpy(ni_msg, ni_chr8 ? "CHR ROM : " : "CHR RAM : ", NI_MSGMAX);
    ni_appnum(ni_msg, ni_chr8 ? ni_chr8 * 8 : 8);   /* byVRomSize * 8 */
    ni_app(ni_msg, "KB");
    ni_setfield(NI_F_CHR, ni_msg);

    os88_strcpy(ni_msg, "Mirroring : ", NI_MSGMAX);
    ni_app(ni_msg, ni_mirror ? "V" : "H");
    ni_setfield(NI_F_MIRROR, ni_msg);

    os88_strcpy(ni_msg, "SRAM : ", NI_MSGMAX);
    ni_app(ni_msg, ni_sram ? "Yes" : "No");
    ni_setfield(NI_F_SRAM, ni_msg);

    os88_strcpy(ni_msg, "4 Screen : ", NI_MSGMAX);
    ni_app(ni_msg, ni_fourscr ? "Yes" : "No");
    ni_setfield(NI_F_FOUR, ni_msg);

    os88_strcpy(ni_msg, "Trainer : ", NI_MSGMAX);
    ni_app(ni_msg, ni_trainer ? "Yes" : "No");
    ni_setfield(NI_F_TRAIN, ni_msg);
}

static void ni_panel_state(void)
{
    if (ni_state == NI_ST_NOROM)
        ni_setfield(NI_F_STATE, "No ROM");
    else if (ni_state == NI_ST_READY)
        ni_setfield(NI_F_STATE, "Ready");
    else if (ni_state == NI_ST_RUN)
        ni_setfield(NI_F_STATE, "Running");
}

/* ni_panel_fact - the fact line. ONE predicate answers it, most specific
 * first, and every greyed item in this package has a row here. A later wave
 * that adds a greyed item and forgets its sentence is a defect no build step
 * can catch, which is why the rule is in SPEC.md 91.7.2 as well. */
static void ni_panel_fact(void)
{
    os88_strcpy(ni_msg, "", NI_MSGMAX);
    if (ni_fact != NI_FACT_NONE)
        os88_strcpy(ni_msg, ni_facts[ni_fact], NI_MSGMAX);
    ni_setfield(NI_F_FACT, ni_msg);
}

/* ni_panel_settle - THE PANEL'S TEXT AS IT MUST STAND WHEN THE BRACKET ENDS,
 * settled INSIDE the bracket, before the FSX proc returns.
 *
 * SPEC.md 53.6 step 4 runs wm_paint_all UNDER THE STILL-HELD LOCK BEFORE
 * OSAPI_FSX_RUN returns, and that repaint is our own os88_paint: it letters
 * every field from ni_txt as it stands at that moment. Set the post-session
 * text AFTER ni_fsx_go returns and three rows are lettered twice - state,
 * rate and fact - and one of them is lettered WRONG the first time: for the
 * whole of the kernel's thirteen-row restore (~220 ms on the target) the
 * panel says `Running` for a session that has already ended, then flips to
 * `Ready`. That is the double-draw flash CLAUDE.md names as invisible in an
 * emulator, and it was measured and read as noise: "789 pixels in three
 * bands - the rate field and the fact line".
 *
 * So the text is settled here, the kernel's own repaint letters the FINAL
 * strings ONCE, and ni_go_full's trailing ni_panel_paint finds shadow == text
 * and draws nothing. Everything this touches is RESIDENT and none of it is an
 * ovl_*, so SPEC.md 91.6.5 holds: no overlay is reached from inside the
 * bracket. It draws nothing itself - ni_panel_state/rate/fact only write
 * ni_txt - so it takes no lock and needs none. */
static void ni_panel_settle(void)
{
    ni_state = NI_ST_READY;
    ni_panel_state();
    ni_panel_rate();                /* both figures, measured (SPEC.md 91.7.1) */
    if (ni_brkfact == 1)
        ni_fact = NI_FACT_MUTE;     /* `M` inside the bracket has no glass to
                                     * print on - a toast would land on a bar
                                     * the user cannot see - so the fact it
                                     * asked for is read HERE (SPEC.md 91.6.4)
                                     */
    else if (ni_brkfact == 2)
        ni_fact = NI_FACT_CLIP;
    ni_brkfact = 0;
    ni_panel_fact();
}

/* ni_panel_init - every field's first text, from os88_main. It draws nothing:
 * the kernel shows the window and W_PAINT follows. */
static void ni_panel_init(void)
{
    ni_shadow_drop();
    ni_panel_rom();
    ni_panel_rate();
    /* The legend, two rows. Its keys are SPEC.md 91.6.4's table and their
     * authorities are named there - the D-pad and the A/B bit numbers from
     * InfoNES's SDL front end, Start and Select from agnes cross-read with
     * nofrendo, R reset and Page Up/Page Down frame skip from InfoNES's own
     * add_key (:262-266, :290-301), and F/Esc from SPEC.md 11.2.1 rather than
     * InfoNES's Q.
     *
     * PgUp/PgDn IS ON THE LEGEND BECAUSE IT IS THE ONLY FRAME-SKIP CONTROL
     * THAT EXISTS INSIDE THE BRACKET - the Options menu is not reachable
     * there - and wave 2 made it live while naming it on no surface at all,
     * which is the defect SPEC.md 91.7.2 argues in its own words about `M`.
     *
     * EVERY KEY HERE SAYS WHAT IT DOES, and the separator is what pays for
     * it. A draft cut the verb from `F/Esc` to make room, which left the one
     * key that ENDS a session in which the machine has taken the whole screen
     * as the only entry on either row naming a key and not a function - read
     * on the panel, BEFORE the user is inside. The eight items are 72
     * characters; at one space between them the rows are 38 and 40 cells of
     * the 40 (NI_FCELLS) each, so nothing is cut at all. */
    ni_setfield(NI_F_KEYS1, "Pad arrows A X B Z Start Enter R reset");
    ni_setfield(NI_F_KEYS2, "Select RShift F/Esc leave PgUp/PgDn skip");
    ni_panel_state();
    ni_panel_fact();                /* THE FACT ROW IS ni_menu_state's, which
                                     * os88_main has already called: it picks
                                     * from the facts that are true and
                                     * advances the rotation (ni_fact_rotate).
                                     * This only puts the chosen sentence in
                                     * the field. */
}
