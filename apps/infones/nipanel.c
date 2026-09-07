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
#define NI_FACT_PIC   2
#define NI_FACT_CLIP  3
#define NI_FACT_FSX   4
#define NI_FACT_N     5

/* EVERY SENTENCE IS <= NI_FCELLS AND IS THE SPEC's OWN CHARACTERS. SPEC.md
 * 91.10 is the contract and this row is what a user reads, so the two are the
 * same string and not two paraphrases of one - the first draft of 91.10
 * quoted an 82-character sentence at a field 40 cells wide, which is a
 * contract stating something the glass cannot hold.
 *
 * PLANNED NI_FACT_CLIP - wave 2, and it is named on this one line because
 * build.sh's nifact gate reads it: the gate refuses a sentence that nothing
 * in the package can ever select, and this is how a wave declares one that is
 * written early on purpose. Its predicate is the live FSI_H of the mode this
 * package would enter, which needs OSAPI_FSX_MODE and does not exist in wave
 * 1 - nifsx.inc answers the caps MASK and nothing else. Until then `Clip top
 * and bottom` is greyed by NI_FACT_PIC with the rest of Options. */
static const char *ni_facts[NI_FACT_N] = {
    "",
    /* Options > Mute. The 2A03 mixes five voices and the PC speaker plays
     * one square wave (SPEC.md 91.10). The pulse-1 arithmetic that WOULD be
     * the follow-up is recorded in the SPEC, not here. */
    "No APU: five voices, one speaker.",
    /* Wave 1's fact, and it is a fact about this build rather than about the
     * machine (LESSONS.md 8): the composer and the bracket land in wave 2, so
     * everything that shows a picture is greyed and says so. A greying may
     * not outlive its reason (SPEC.md 47) - wave 2 deletes this row. */
    "No picture yet: this build reads ROMs.",
    "Clip is forced at 320x200: 240 rows.",
    "This display offers no fullscreen mode."
};
static int ni_fact;

/* --- THE FACT LINE IS ONE ROW AND MORE THAN ONE FACT IS TRUE -------------
 * There is exactly one fact row (thirteen is what a 200-line adapter's
 * desktop holds - see NI_PANEL_H, and the CGA window ends on the last one),
 * and this build has three true facts at once: no picture yet, no APU, and on
 * some displays no fullscreen mode. A fixed pick showed one of them and hid
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

/* --- the two rates, and the state line ----------------------------------- */
static unsigned ni_rate_nes;        /* percent of a real NES, a plain unsigned
                                     * (SPEC.md 91.7.1: there is no float and
                                     * no formatter here) */
static unsigned ni_rate_spf;        /* TENTHS of a second per painted frame */
static int ni_rate_known;

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
    os88_strcpy(ni_txt[f], s, NI_FMAX);
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

/* ni_fact_rotate - advance the fact row to the next TRUE fact. `nofsx` is the
 * one predicate that is not a constant in this build. */
static void ni_fact_rotate(int nofsx)
{
    static unsigned char t[NI_FACT_N];  /* rule 1 again: an indexed array is
                                         * an ADDRESSED object and may not be
                                         * an automatic (SPEC.md 73.5) */
    int n;

    n = 0;
    t[n++] = NI_FACT_PIC;           /* wave 1's build fact, which greys five
                                     * of the six Options items and is the one
                                     * a user meets first */
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
            os88_font_run(ni_gox + NI_MARGIN_X, ni_goy + y, ni_txt[f],
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
 * 91.7.1). The rendered one is held in TENTHS and written as
 * utoa(t/10), '.', '0' + t%10, because there is no formatter and `float` is
 * poisoned - `%4.1f` cannot be written here at all. */
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
    ni_appnum(ni_msg, t / 10);
    ni_app(ni_msg, ".");
    ni_num[0] = (char)('0' + (int)(t % 10));
    ni_num[1] = 0;
    ni_app(ni_msg, ni_num);
    ni_app(ni_msg, "  s/frame");
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
     * nofrendo, and F/Esc from SPEC.md 11.2.1 rather than InfoNES's Q. */
    ni_setfield(NI_F_KEYS1, "Pad arrows   A X   B Z   Start Enter");
    ni_setfield(NI_F_KEYS2, "Select RShift   R reset   F/Esc leave");
    ni_panel_state();
    ni_panel_fact();                /* THE FACT ROW IS ni_menu_state's, which
                                     * os88_main has already called: it picks
                                     * from the facts that are true and
                                     * advances the rotation (ni_fact_rotate).
                                     * This only puts the chosen sentence in
                                     * the field. */
}
