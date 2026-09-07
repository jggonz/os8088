/* ============================================================================
 * os8088 - apps/lemmings/lemui.c   (#included by lemmings.c)
 *
 * THE LAUNCHER, RESIDENT HALF (SPEC.md 92): the layout, the hit test, the
 * shadow and the damage-only repaint. Everything here runs on a KEYSTROKE or a
 * CLICK, which is why it is resident and why lemovl.c's chrome, formatting,
 * About card and greying facts are not (SPEC.md 73.14: split by FREQUENCY).
 *
 * THE PAGE IS SIZED FROM THE LIVE WINDOW AND NEVER FROM THE LIST'S LENGTH.
 * cword's Font list ran off the bottom of a 200-line screen because it wrapped
 * from its own length (LESSONS.md 8), and on a CGA the content box here is
 * 200 - MBAR_H 20 - DOCK_H 24 - TITLE_H 18 - 1 = 137 pixels, which is fifteen
 * 9-pixel rows and seven level rows once the tabs, the Play control and the
 * four state rows have their own. os88_wm_geom() is asked on every layout and
 * the bottom block is anchored to the BOTTOM of whatever it answers, so no row
 * can fall past the dock on any adapter.
 *
 * THE GLASS IS SHADOWED (LESSONS.md 6: design this in wave 1, not as polish).
 * One character and one attribute per cell; a repaint composes the rows that
 * could have changed ONE AT A TIME into a row buffer, compares each cell
 * against the shadow, and draws only the RUNS that differ. A selection move is
 * two rows and a handful of cells rather than a screen; a full repaint of the
 * 64x20 box is what a screen of text costs whoever draws it. The row-at-a-time
 * shape is deliberate: a second full-screen compose buffer would be another
 * 2,560 bytes of a budget that is 81% spent before this file starts (SPEC.md
 * 92.6).
 *
 * ONE DECISION PER CELL, ONE CALL PER RUN (PERFORMANCE.md, LESSONS.md 6). Every
 * cell is drawn by os88_font_run(), which puts the paper and the glyph down in
 * one pass - never a fill followed by letters, which writes every one of those
 * pixels twice with a visible gap between the passes on the target machine.
 *
 * ATTRIBUTION: no derived material is in this file. The wording it draws comes
 * out of LEMSTR.LEM (SPEC.md 92.3); Lemmings is (C) 1991 DMA Design / Psygnosis
 * and README.TXT carries the full attribution.
 * ==========================================================================*/

/* --- geometry, all read back and none of it assumed (SPEC.md 39) ----------- */
#define LEM_CELLW      8            /* the kernel face is 8x8 (SPEC.md 6) */
#define LEM_RPITCH     9            /* ...one pixel of leading between rows */
#define LEM_PADY       2
#define LEM_PADX       2

/* The rows below the level list, counted from the BOTTOM of the content box:
 * one blank, the Play control, and the four state rows. */
#define LEM_TAILROWS   6

static int lem_rowy(int row)
{
    return lem_org.y + LEM_PADY + row * LEM_RPITCH;
}

static int lem_colx(int col)
{
    return lem_org.x + LEM_PADX + col * LEM_CELLW;
}

/* --- the composed row, and the run the flush draws ------------------------- */
static char          lem_row[LEM_SH_COLS + 1];
static unsigned char lem_rowa[LEM_SH_COLS];
static char          lem_draw[LEM_SH_COLS + 1];

static void lem_row_clear(void)
{
    int i;

    for (i = 0; i < lem_cols; i++) {
        lem_row[i] = ' ';
        lem_rowa[i] = LEM_AT_TEXT;
    }
    lem_row[lem_cols] = 0;
}

/* lem_put - `s` into the composed row at `col`, at most `w` cells, attribute
 * `at` over the whole of `w` so the run's paper is continuous. Anything past
 * the row is dropped here rather than by the window's clip, which is the only
 * place it CAN be dropped: os88_font_run() clips to the window and to nothing
 * inside it. */
static void lem_put(int col, int w, const char *s, int at)
{
    int i;

    if (col < 0) {
        w += col;
        col = 0;
    }
    if (col + w > lem_cols)
        w = lem_cols - col;
    for (i = 0; i < w; i++) {
        lem_row[col + i] = (*s != 0) ? *s : ' ';
        if (*s != 0)
            s++;
        lem_rowa[col + i] = (unsigned char)at;
    }
}

/* --- the four attributes, and what each is on the glass --------------------
 * LEM_AT_GREY is a colour AND a flag: os88_gfx_pen(1) sets CDGRAY and the
 * [gfx_dis] flag, and on the two 1bpp adapters the FLAG is the entire
 * difference, because the colour alone rounds to solid black (SPEC.md 47.2,
 * 39.4). Grey the whole control, never just its caption - which here is the
 * whole run, label and value together.
 *
 * THERE ARE THREE AND NOT FOUR: see lemmings.c's attribute block for what the
 * light-grey one did on CGA.
 *
 * THE PEN IS HALF OF IT AND THE INK IS THE OTHER HALF, and the first build
 * shipped only the pen. os88_gfx_pen(1) sets CDGRAY and the [gfx_dis] flag,
 * but a RUN takes its ink as an ARGUMENT and C can no more read
 * os88_set_color() back than assembly can (SPEC.md 6.6.5) - so the pen alone
 * left the greyed rows drawn in the OS88_BLACK this passed, and on VGA they
 * came out pixel-identical to the live one. Measured off the glass:
 * build/port-shots/wave1-vga-state.png before the fix has two colours in the
 * Music row and they are pure black and pure white. So a greyed run names
 * OS88_DGRAY as its ink AND rides the pen, which is exactly what
 * apps/cword/cwdrop.c:167-178 does for a disabled menu item, and each half
 * carries a different adapter: the colour is the whole of it on VGA and the
 * FLAG is the whole of it on Hercules and CGA, where the colour rounds to
 * solid black (SPEC.md 47.2, 39.4, 6.1.12).
 *
 * IT ANSWERS THROUGH TWO STATICS AND NOT THROUGH OUT-PARAMETERS. `&ink` on an
 * automatic is a BP-relative offset dereferenced through DS, SS != DS here, and
 * tools/cc8086.py refuses it by name - which it did, at this exact call site,
 * on the first build (SPEC.md 73.5, os88.h rule 1; LESSONS.md 4 says half the
 * API is out-parameters and every one of them is a static). */
static int lem_ink_v;
static int lem_paper_v;

static void lem_ink(int at)
{
    if (at == LEM_AT_SEL) {
        lem_ink_v = OS88_WHITE;
        lem_paper_v = OS88_BLACK;
    } else if (at == LEM_AT_GREY) {
        lem_ink_v = OS88_DGRAY;
        lem_paper_v = OS88_WHITE;
    } else {
        lem_ink_v = OS88_BLACK;
        lem_paper_v = OS88_WHITE;
    }
}

/* lem_sh_blank - the shadow now says "spaces, black on white", which is what
 * the kernel's own content fill just put on the glass (kernel/wm.inc's WF_OWNBG
 * interlock: with the flag clear it white-fills the WHOLE content before
 * W_PAINT). Seeding it is not the same as invalidating it, and the difference
 * is most of a full repaint: an empty cell then matches and is never drawn. */
static void lem_sh_blank(void)
{
    int i;

    for (i = 0; i < LEM_SH_COLS * LEM_SH_ROWS; i++) {
        lem_sh[i] = ' ';
        lem_sha[i] = LEM_AT_TEXT;
    }
    lem_sh_ok = 1;
}

/* --- THE DAMAGE RANGE, and why the shadow alone was not enough --------------
 *
 * The shadow decides whether a cell is DRAWN. It does not decide whether a row
 * is COMPOSED and SCANNED, and the first build of this file composed and
 * scanned every row on every repaint with a comment saying that cost "the
 * arithmetic of a row and nothing on the glass". Read the codegen instead:
 * lem_flush_row()'s per-COLUMN body is ~80 emitted instructions, ~24 of them
 * with a memory operand, so one column of one row is ~300-450 us on a 4.77 MHz
 * 8088 - HALF A GLYPH CELL to decide not to draw. Twenty rows of 64 columns is
 * ~1,280 columns and ~400-580 ms of pure scanning, on a keystroke the harness
 * priced at "3 calls, 78 cells" = 70 ms. The arrow keys autorepeat at ~100 ms,
 * so the mechanism built to prevent input overrun was causing it - which is
 * PERFORMANCE.md rule 5 exactly: the shadow emitted the designed number of
 * calls and paid for them by hand in the scan, and a host harness that counts
 * only calls and cells prices that scan at zero.
 *
 * SO EVERY PARTIAL CALLER NAMES THE ROWS IT CHANGED. lem_rlo..lem_rhi is an
 * inclusive range, empty when rhi < rlo, widened by lem_mark() and consumed by
 * the next lem_repaint(). A row outside it is a row whose shadow already
 * describes the glass, so not composing it changes no invariant and draws no
 * cell differently - it just does not spend the scan. `full` (W_PAINT, the
 * dismissal, a screen change, the first wake) still means every row.
 *
 * THE ONE THING A CALLER CANNOT KNOW is whether lem_layout() will move the
 * page: a selection leaving the page re-letters the whole list. lem_repaint()
 * compares lem_top across the layout and widens to the list itself when it
 * moved, so the callers stay simple and the page-turn stays correct. */
/* IT IS A FLAG PER ROW AND NOT A LOW/HIGH PAIR, and the first version of it was
 * the pair. A selection move damages two level rows near the TOP and the Play
 * control at the BOTTOM, so a contiguous span between them is fourteen rows of
 * the twenty - the harness read exactly that ("a selection move COMPOSED AND
 * SCANNED 14 rows") and it is most of the defect still there. Twenty bytes of
 * bss buy the rows that were actually damaged; lem_rlo/lem_rhi survive only as
 * the loop's own bounds, so an unmarked repaint walks nothing at all. */
/* ...AND IT IS BOUNDED BY COLUMN AS WELL, for the same reason one level down.
 * A selection move marks three rows and lem_flush_row() scanned the FULL
 * lem_cols of each - 189 columns at the ~300-450 us a column measured above, so
 * 57-85 ms of pure scanning against ~63 ms of actual drawing, on a key that
 * autorepeats every ~100 ms. The two level rows only ever change their LIST
 * half: lem_gutc already splits every level row at the gutter and the pane half
 * beyond it is untouched by a selection. Forty bytes of bss carry the bound and
 * take the keystroke's scan from 189 columns to ~78. lem_mark() itself marks
 * the WHOLE row, so every other caller is unchanged and a narrow mark is
 * something a caller opts into. */
static unsigned char lem_dirty[LEM_SH_ROWS];
static unsigned char lem_dc0[LEM_SH_ROWS];
static unsigned char lem_dc1[LEM_SH_ROWS];
static int lem_rlo = 0;
static int lem_rhi = -1;

static void lem_mark_cols(int row, int c0, int c1)
{
    if (row < 0 || row >= LEM_SH_ROWS)
        return;
    if (c0 < 0)
        c0 = 0;
    if (c1 > LEM_SH_COLS - 1)
        c1 = LEM_SH_COLS - 1;
    if (c1 < c0)
        return;
    if (lem_dirty[row]) {
        /* a second mark on the same row WIDENS: two callers naming different
         * halves must not leave one of them unscanned */
        if (c0 > lem_dc0[row])
            c0 = lem_dc0[row];
        if (c1 < lem_dc1[row])
            c1 = lem_dc1[row];
    }
    lem_dc0[row] = (unsigned char)c0;
    lem_dc1[row] = (unsigned char)c1;
    lem_dirty[row] = 1;
    if (lem_rhi < lem_rlo) {
        lem_rlo = row;
        lem_rhi = row;
        return;
    }
    if (row < lem_rlo)
        lem_rlo = row;
    if (row > lem_rhi)
        lem_rhi = row;
}

static void lem_mark(int row)
{
    lem_mark_cols(row, 0, LEM_SH_COLS - 1);
}

static void lem_mark_range(int lo, int hi)
{
    int r;

    for (r = lo; r <= hi; r++)
        lem_mark(r);
}

/* lem_mark_level - a LEVEL's row on the page, if it is on the page at all.
 * Read with the layout that is on the glass, which is the pre-layout one; when
 * the repaint's own layout moves the page, lem_repaint() widens past this.
 *
 * THE LIST HALF AND NOT THE ROW: a selection changes the bar and the name, both
 * of which stop at the gutter (lem_row_level pads to lem_listw). The preview
 * pane beside it is the settle timer's, and that marks its rows with
 * lem_mark_pane(). */
static void lem_mark_level(int lvl)
{
    if (lvl >= lem_top && lvl < lem_top + lem_nlist)
        lem_mark_cols(LEM_LISTTOP + (lvl - lem_top), 0, lem_listw - 1);
}

/* lem_mark_pane - a preview-pane row's PANE HALF, which is the half the settle
 * timer changes.
 *
 * THE SETTLE PATH USED TO MARK THESE ROWS WHOLE and that was the same defect
 * lem_mark_cols was built for, one size up: seven rows at lem_cols = 63 is 441
 * columns scanned for ~140 cells that all live in the 27-column pane, so 252 of
 * them - 76-113 ms at the ~300-450 us a column measured in SPEC.md 92.7.1 - are
 * spent re-scanning a list half that is already right on the glass. It IS
 * already right: lem_pv_defer() composes and flushes the level rows on the
 * keystroke with lem_pv_hold set, and the rows it does not mark are rows whose
 * shadow already describes the glass by the invariant above.
 *
 * The gutter cell at lem_listw is deliberately NOT in the range: it is
 * ovl_chrome()'s vertical rule (lemovl.c), which no row composer writes. */
static void lem_mark_pane(int row)
{
    lem_mark_cols(row, lem_pvx, LEM_SH_COLS - 1);
}

static void lem_mark_pane_rows(void)
{
    int r;

    for (r = LEM_LISTTOP; r < LEM_LISTTOP + LEM_PV_LINES; r++)
        lem_mark_pane(r);
}

/* lem_flush_row - the composed row against the shadow, drawing only the runs
 * that differ. A run is a maximal span of one attribute; it is drawn if ANY
 * cell in it differs, because os88_font_run() is priced per CALL (756 us) plus
 * per CELL (~900 us) and splitting a run to save two cells buys a second call
 * that costs more than they do.
 *
 * ...WITH ONE TRIM, at both ends, and only where the cell is ALREADY RIGHT: a
 * trailing space on a black-on-white run whose shadow already says space is a
 * cell the glass does not need. It is worth writing because the run this saves
 * on is the common one - a level row pads its whole pane width so the SELECTION
 * bar is continuous, and on the twelve rows that are not selected most of that
 * width is blank. Measured on the host harness: a full repaint of the 189-pixel
 * box went 903 cells to 733 and a selection move 246 to 180, at no cost to any
 * row that carries text to its edge. */
/* lem_brk - a run may not extend INTO this column.
 *
 * There is one such column and it is the GUTTER between the level list and the
 * preview pane (lem_gutc, set by the repaint). Breaking there does three things
 * and each of them is worth having on its own:
 *
 *   - the gutter cell is always a space with LEM_AT_TEXT and the shadow always
 *     says the same, so the run over it is never dirty and the cell is NEVER
 *     DRAWN. That is what lets ovl_chrome() put the vertical rule inside it and
 *     have it survive every repaint. The rule used to sit at
 *     lem_colx(lem_listw) - 2, which is pixel 6 of cell listw-1 - INSIDE the
 *     span every level row paints - so the first paint cut holes in it and
 *     every selection move cut more;
 *   - a level row's list half and its preview line stop being ONE run with the
 *     blank gap between them drawn as paper. The trim at the ends of a run
 *     cannot reach a gap in the middle of one, so those blanks were ~900 us
 *     each, on every row, on every full repaint;
 *   - and the two halves become independently dirty, which is what makes the
 *     deferred pane below cost nothing to compose.
 *
 * IT IS TWO LOCALS AND NOT A FUNCTION, and that is a measurement rather than a
 * style. It used to be `static int lem_brk(int c)` called from inside the
 * per-column condition; SmallerC does not inline, so build/lemmings.raw.asm
 * carried a real `call _lem_brk` per column - ~61 a row, ~1,220 a full repaint,
 * 13.4 ms at PERFORMANCE.md's 11 us for a near call+ret before the body's own
 * three loads of lem_gutc. It was paid on every column of every row where it
 * provably cannot fire: lem_row_compose() sets lem_gutc = -1 for row 0, the
 * Play row, the four state rows and every row of both text screens, so on a
 * fact or preview screen 100% of those calls returned 0 at the first compare.
 * Two locals read once a row cost two compares a column instead. */
static void lem_flush_row(int row, int c0, int c1)
{
    int c, e, i, at, dirty;
    int g0, g1;
    unsigned char *sh;
    char *shc;

    shc = lem_sh + (row << LEM_SH_SHIFT);
    sh = lem_sha + (row << LEM_SH_SHIFT);

    g0 = lem_gutc;
    g1 = (g0 >= 0) ? g0 + 1 : -1;

    if (c1 > lem_cols - 1)
        c1 = lem_cols - 1;
    c = (c0 > 0) ? c0 : 0;
    while (c <= c1) {
        at = lem_rowa[c];
        e = c;
        dirty = 0;
        while (e <= c1 && lem_rowa[e] == (unsigned char)at &&
               (e == c || (e != g0 && e != g1))) {
            if (!lem_sh_ok || shc[e] != lem_row[e] || sh[e] != (unsigned char)at)
                dirty = 1;
            e++;
        }
        if (dirty && at == LEM_AT_TEXT && lem_sh_ok) {
            while (e > c && lem_row[e - 1] == ' ' && shc[e - 1] == ' ' &&
                   sh[e - 1] == LEM_AT_TEXT)
                e--;
            while (c < e && lem_row[c] == ' ' && shc[c] == ' ' &&
                   sh[c] == LEM_AT_TEXT)
                c++;
            if (c == e) {
                c = e;
                continue;
            }
        }
        if (dirty) {
            for (i = c; i < e; i++) {
                lem_draw[i - c] = lem_row[i];
                shc[i] = lem_row[i];
                sh[i] = (unsigned char)at;
            }
            lem_draw[e - c] = 0;
            lem_ink(at);
            if (at == LEM_AT_GREY)
                os88_gfx_pen(1);
            os88_font_run(lem_colx(c), lem_rowy(row), lem_draw,
                          lem_ink_v, lem_paper_v);
            if (at == LEM_AT_GREY)
                os88_gfx_pen(0);
            lem_c_calls++;
            lem_c_cells += e - c;
        } else {
            for (i = c; i < e; i++) {
                shc[i] = lem_row[i];
                sh[i] = (unsigned char)at;
            }
        }
        c = e;
    }
}

/* --- layout ---------------------------------------------------------------- */

static void lem_layout(void *win)
{
    int n;

    if (os88_wm_geom(win, &lem_sz) != 0) {
        lem_sz.w = 320;
        lem_sz.h = 120;
    }
    os88_wm_content(win, &lem_org);

    lem_cols = (lem_sz.w - LEM_PADX) / LEM_CELLW;
    if (lem_cols > LEM_SH_COLS)
        lem_cols = LEM_SH_COLS;
    if (lem_cols < 8)
        lem_cols = 8;

    /* The last row's glyph band has to END inside the box, not start inside it
     * (LESSONS.md 13: wm_geom's content height is W_H - TITLE_H - 1, and the
     * row the model had off the end was never on the glass). */
    lem_rows = (lem_sz.h - LEM_PADY - 8) / LEM_RPITCH + 1;
    if (lem_rows > LEM_SH_ROWS)
        lem_rows = LEM_SH_ROWS;
    if (lem_rows < 3)
        lem_rows = 3;

    /* The bottom block is anchored to the BOTTOM, so the list takes what is
     * left and never the other way round. */
    lem_playrow = lem_rows - LEM_TAILROWS + 1;
    lem_staterow = lem_playrow + 1;
    lem_nlist = lem_playrow - 1 - LEM_LISTTOP;
    if (lem_nlist < 1)
        lem_nlist = 1;

    lem_listw = (lem_cols >> 1) + 4;
    if (lem_listw > lem_cols - 12)
        lem_listw = lem_cols - 12;
    if (lem_listw < 10)
        lem_listw = 10;
    if (lem_listw > lem_cols)
        lem_listw = lem_cols;
    lem_pvx = lem_listw + 1;
    lem_pvw = lem_cols - lem_pvx;
    if (lem_pvw < 0)
        lem_pvw = 0;

    /* KEEP THE SELECTION ON THE PAGE, AND MOVE THE PAGE A PAGE AT A TIME.
     *
     * A row-at-a-time scroll re-letters EVERY list row on the keystroke that
     * leaves the page, not two: on the 189-pixel box that is twelve rows, one
     * of them a full-width AT_SEL run and eleven ~22-cell TEXT runs, ~12 calls
     * and ~280 glyph cells - 9 ms + 252 ms = ~260 ms at PERFORMANCE.md's 756 us
     * a call and ~900 us a cell. The arrow keys autorepeat at ~100 ms, so a
     * held Down paid twelve cheap repeats and then that, on every repeat after
     * it: input overrun, the third defect an emulator cannot show you.
     *
     * A page costs the same ~260 ms ONCE per lem_nlist keys instead, and the
     * ordinary keystroke stays what SPEC.md 92.6's table says it is - two level
     * rows. There is no fidelity cost: the original has no level list at all
     * (it is a code entry and a preview screen), so nothing here is copied from
     * it and the cheaper of two honest behaviours wins. The selection lands on
     * the first row of the new page going down and the last going up, which is
     * where the eye already is. */
    if (lem_sel < lem_top)
        lem_top = lem_sel - lem_nlist + 1;
    if (lem_sel >= lem_top + lem_nlist)
        lem_top = lem_sel;
    n = LEM_PERRAT - lem_nlist;
    if (n < 0)
        n = 0;
    if (lem_top > n)
        lem_top = n;
    if (lem_top < 0)
        lem_top = 0;
}

/* --- the tab strip ---------------------------------------------------------
 * The four ratings, in the original's order, each named by the band. Their
 * column ranges are recorded as they are composed so that lem_hit() answers
 * from the SAME arithmetic the drawing used and the two cannot drift. */
static void lem_row_tabs(void)
{
    int i, c, w;
    const char *s;

    lem_row_clear();
    c = 0;
    for (i = 0; i < LEM_RATINGS; i++) {
        s = lem_str((int)lem_rating_str[i]);
        w = (int)os88_strlen(s) + 2;
        lem_tabx[i] = c;
        lem_tabw[i] = w;
        if (c + w > lem_cols)
            lem_tabw[i] = 0;
        else
            lem_put(c, w, "", (i == lem_rating) ? LEM_AT_SEL : LEM_AT_TEXT);
        if (c + 1 + (int)os88_strlen(s) <= lem_cols)
            lem_put(c + 1, (int)os88_strlen(s), s,
                    (i == lem_rating) ? LEM_AT_SEL : LEM_AT_TEXT);
        c += w + 1;
    }
}

/* --- one level row ---------------------------------------------------------
 * "NN Name", the number 1-based within the rating the way the original counts
 * it, and the name TRIMMED of the leading spaces the original centres its
 * titles with (Lemmix calls Title.Trim before drawing). A row whose graphic set
 * or special picture is not on this disk is present and GREYED (SPEC.md 47) and
 * clicking it says which file and what it would have cost. */
static void lem_row_level(int row)
{
    int lvl, at;

    lvl = lem_top + row;
    lem_row_clear();
    if (lvl >= LEM_PERRAT || !lem_rat_ok)
        return;
    at = lem_ent_here(lvl) ? LEM_AT_TEXT : LEM_AT_GREY;
    if (lvl == lem_sel && at == LEM_AT_TEXT)
        at = LEM_AT_SEL;
    lem_put(0, lem_listw, "", at);
    /* Column 0 is the selection gutter. A GREYED row still has to show it is
     * the selection and inverse video is not available to it - grey is a pen
     * and a flag, not a colour pair - so the marker is a cell, which reads the
     * same on all three adapters including the two where grey rounds to
     * black (SPEC.md 47.2, 39.4). */
    if (lvl == lem_sel && at == LEM_AT_GREY)
        lem_put(0, 1, ">", at);
    lem_setn(lem_num2, sizeof(lem_num2), (unsigned)(lvl + 1));
    lem_put(3 - (int)os88_strlen(lem_num2), (int)os88_strlen(lem_num2),
            lem_num2, at);
    lem_fit(lem_line, sizeof(lem_line), lem_trim(lem_ent_name(lvl)),
            lem_listw - 4);
    lem_put(4, lem_listw - 4, lem_line, at);
}

/* --- the preview fields, beside the list -----------------------------------
 * The original's own seven lines and its own wording (Lemmix
 * GameScreen.Preview.pas GetScreenLinesAndColors, Base.Strings.pas:411-418),
 * formatted in the overlay because formatting is not on a keystroke's critical
 * path in any sense that matters here and the resident half is the budget
 * (SPEC.md 92.6). With no module the pane is blank and the launcher has already
 * said so once. */
static void lem_pvline(int line, char *dst, unsigned cap)
{
    dst[0] = 0;
    if (lem_ovl == 1)
        ovl_fmt(line, lem_sel, dst, cap);
}

/* --- the four state rows ---------------------------------------------------
 * The controls SPEC.md 47's greying sits on. Each names its label from the band
 * and, where it is greyed, is drawn with the disabled pen; clicking it shows
 * the FACT that greys it, which is a sentence the converter wrote and this
 * program never composed. */
static int lem_state_grey(int row)
{
    if (row == LEM_ROW_MODE)
        return !lem_mode_ok;
    if (row == LEM_ROW_SAVE)
        return !lem_savable;
    return 1;                       /* Music and Level Code on every machine:
                                     * what greys them is a fact about the DATA
                                     * (SPEC.md 92.8), not about this hardware */
}

/* lem_modeval - the Mode row's VALUE: the adapter this window is on and the
 * mode the game will play in on it. It is the one place this program letters
 * words of its own rather than the band's, because these are numbers about THIS
 * BUILD and this machine rather than anything of the original's - and they are
 * short on purpose (SPEC.md 92.6's image is the budget). */
static const char lem_ad_vga[]  = "VGA";
static const char lem_ad_herc[] = "Hercules";
static const char lem_ad_cga[]  = "CGA";
static const char lem_ad_ega[]  = "EGA";
static const char lem_md_16[]   = "320x200x16";
static const char lem_md_4[]    = "320x200x4";
static const char lem_md_herc[] = "720x348";

static const char *lem_adapter(void)
{
    if (lem_vidkind == OS88_VID_HERC)
        return lem_ad_herc;
    if (lem_vidkind == OS88_VID_CGA)
        return lem_ad_cga;
    if (lem_vidkind == OS88_VID_EGA)
        return lem_ad_ega;
    return lem_ad_vga;
}

static const char *lem_modename(void)
{
    if (lem_mode_ok)
        return lem_md_16;
    if (lem_vidkind == OS88_VID_HERC)
        return lem_md_herc;
    return lem_md_4;
}

static void lem_row_state(int row)
{
    int at, n;

    lem_row_clear();
    at = lem_state_grey(row) ? LEM_AT_GREY : LEM_AT_TEXT;
    lem_fit(lem_line, sizeof(lem_line), lem_str(lem_state_label(row)),
            lem_cols - 2);
    lem_put(1, lem_cols - 1, lem_line, at);
    n = 16;
    if (n > lem_cols - 2)
        return;
    if (row == LEM_ROW_MODE) {
        lem_put(n, lem_cols - n, lem_adapter(), at);
        n += (int)os88_strlen(lem_adapter()) + 1;
        if (n < lem_cols)
            lem_put(n, lem_cols - n, lem_modename(), at);
    } else if (row == LEM_ROW_SAVE && lem_savable &&
               lem_prog[lem_rating] >= 0) {
        lem_setn(lem_line, sizeof(lem_line),
                 (unsigned)(lem_prog[lem_rating] + 1));
        lem_put(n, lem_cols - n, lem_line, at);
    }
}

/* --- the Play control ------------------------------------------------------
 * A run of inverse video rather than a framed widget: os88ui.inc's button is
 * not compiled into this package (OS88UI_NOBTN in the shim), the label is the
 * band's own word, and greying it is the pen over the same run - which is what
 * makes it legible as DISABLED on the two 1bpp adapters, where a greyed frame
 * is a dotted ring and a greyed caption is a checkerboard (SPEC.md 47.2). */
static int lem_play_ok(void)
{
    return lem_ok && lem_rat_ok && lem_ent_here(lem_sel);
}

static void lem_row_play(void)
{
    lem_row_clear();
    lem_fit(lem_line, sizeof(lem_line), lem_str(LEMS_PLAY), lem_cols - 4);
    lem_playw = (int)os88_strlen(lem_line) + 4;
    if (lem_playw > lem_cols)
        lem_playw = lem_cols;
    lem_put(0, lem_playw, "", lem_play_ok() ? LEM_AT_SEL : LEM_AT_GREY);
    lem_put(2, lem_playw - 2, lem_line,
            lem_play_ok() ? LEM_AT_SEL : LEM_AT_GREY);
}

/* --- the three screens' rows ------------------------------------------------ */

/* lem_row_keep - leave columns `from`.. exactly as the shadow has them, so the
 * flush finds them equal and draws nothing there.
 *
 * This is how the preview pane is DEFERRED without a double-draw flash: the
 * alternative - composing the pane as blanks and letting the flush erase it -
 * would white the seven lines on the keystroke and letter them again a fifth of
 * a second later, which is drawing those pixels twice with a visible gap
 * between the passes and is the second of PERFORMANCE.md's three
 * emulator-invisible defects. Only valid while lem_sh_ok says the shadow
 * describes the glass, which is what lem_pv_defer() tests before it arms. */
static void lem_row_keep(int row, int from)
{
    int c, i;

    i = row << LEM_SH_SHIFT;
    for (c = from; c < lem_cols; c++) {
        lem_row[c] = lem_sh[i + c];
        lem_rowa[c] = lem_sha[i + c];
    }
}

static void lem_row_list(int row)
{
    int i;

    if (row == 0) {
        lem_row_tabs();
        return;
    }
    if (row >= LEM_LISTTOP && row < LEM_LISTTOP + lem_nlist) {
        i = row - LEM_LISTTOP;
        if (lem_list_hold) {
            /* A RATING LOAD IS OWED (lem_set_rating). The rows on the glass
             * are the rating the user just left; blanking them here would cost
             * ~260 glyph cells on a key that autorepeats, and then letter the
             * same rows again when the file lands - the same pixels twice with
             * a visible gap, which is the second of PERFORMANCE.md's three
             * emulator-invisible defects. So the glass keeps what it has, the
             * TAB STRIP moves under the key so the press is acknowledged, and
             * os88_onwake() draws the new rating when it has actually read it.
             * Only valid while lem_sh_ok, which lem_set_rating tests. */
            lem_row_clear();
            lem_row_keep(row, 0);
            return;
        }
        lem_row_level(i);
        if (lem_pvw > 2 && i < LEM_PV_LINES && lem_rat_ok) {
            if (lem_pv_hold) {
                lem_row_keep(row, lem_pvx);
                return;
            }
            lem_pvline(i, lem_line, sizeof(lem_line));
            lem_fit(lem_line, sizeof(lem_line), lem_line, lem_pvw);
            lem_put(lem_pvx, lem_pvw, lem_line, LEM_AT_TEXT);
        }
        return;
    }
    if (row == lem_playrow) {
        lem_row_play();
        return;
    }
    if (row >= lem_staterow && row < lem_staterow + LEM_STATE_ROWS) {
        lem_row_state(row - lem_staterow);
        return;
    }
    lem_row_clear();
    if (row == 1 && !lem_ok) {
        /* The one sentence that cannot live in the band, because it is what is
         * said when the band could not be read. */
        lem_put(1, lem_cols - 1, lem_nodata, LEM_AT_TEXT);
    }
}

/* lem_row_text - a row of the wrapped-paragraph screens (the level preview and
 * a greyed control's fact). Both put the original's own "Press mouse button to
 * continue" on the last row, which is the footer Lemmix's preview screen has
 * (Base.Strings.pas SPreviewScreen_PressMouseButtonToContinue). */
static void lem_row_text(int row)
{
    int i, n;

    lem_row_clear();
    if (row == lem_rows - 1) {
        lem_fit(lem_line, sizeof(lem_line), lem_str(LEMS_PV_PRESS), lem_cols - 2);
        n = (lem_cols - (int)os88_strlen(lem_line)) >> 1;
        if (n < 0)
            n = 0;
        lem_put(n, lem_cols - n, lem_line, LEM_AT_TEXT);
        return;
    }
    i = row - 1;
    if (i < 0 || i >= lem_wrapn)
        return;
    lem_fit(lem_line, sizeof(lem_line), lem_wrap_line(i), lem_cols - 2);
    lem_put(1, lem_cols - 1, lem_line, LEM_AT_TEXT);
}

/* lem_row_compose - and the gutter break is decided PER ROW, not per screen.
 *
 * Only a LEVEL row has two panes with a gap between them. A state row, the Play
 * control and the tab strip each pad one run across the whole width, and
 * breaking those at the gutter would buy nothing and cost two more drawing
 * calls apiece - 756 us each - which is what the first version of this did
 * (32 calls a full repaint against 28). The level rows are the ones the rule is
 * drawn beside and the ones whose middle gap is worth not drawing. */
static void lem_row_compose(int row)
{
    lem_gutc = -1;
    if (lem_screen != LEM_SC_LIST) {
        lem_row_text(row);
        return;
    }
    lem_row_list(row);
    if (lem_pvw > 2 && row >= LEM_LISTTOP && row < LEM_LISTTOP + lem_nlist)
        lem_gutc = lem_listw;
}

/* --- the repaint -----------------------------------------------------------
 * `full` redraws every row and takes the chrome with it; otherwise only the
 * rows that could have changed are composed, which for a selection move is two
 * level rows plus the preview pane's seven and the Play control. The shadow is
 * what makes "could have changed" cheap enough to be the default. */
static void lem_repaint(void *win, int full, int arm)
{
    int r, lo, hi, oldtop, oldrows, oldcols, fc0, fc1;

    lem_c_calls = 0;
    lem_c_cells = 0;
    lem_c_rows = 0;
    lem_c_cols = 0;

    /* A SHADOW THAT DOES NOT DESCRIBE THE GLASS IS A FULL REPAINT BY
     * DEFINITION, and saying so here makes lem_brk()'s invariant true rather
     * than nearly true. lem_flush_row() marks every cell dirty when !lem_sh_ok
     * - the GUTTER cell included - and the rule ovl_chrome() draws lives at
     * pixel 3 of that very cell, so re-lettering it erases the rule; but the
     * chrome is redrawn only when `full`. Today the combination is hard to
     * reach (lem_pv_defer's fallback arm and os88_ontimer both pass full = 0
     * and both need lem_sh_ok to be 0, which needs the window to become
     * drawable without a W_PAINT) - one wave-2 caller of lem_repaint(win, 0,
     * ...) makes it live. It costs nothing when the shadow is good. */
    if (!lem_sh_ok)
        full = 1;

    oldtop = lem_top;
    oldrows = lem_rows;
    oldcols = lem_cols;
    lem_layout(win);

    /* A RESIZE IS A FULL REPAINT WHATEVER THE CALLER MARKED: every row's
     * columns move and the shadow is indexed by the OLD geometry. A page turn
     * is a full LIST: lem_layout() moves lem_top when the selection leaves the
     * page, and then every level row carries a different name (the comment in
     * lem_layout says what a page turn costs and why it is a page and not a
     * row). Neither is knowable at the call site, which is why both are decided
     * here and the callers stay two lem_mark()s long. */
    if (lem_rows != oldrows || lem_cols != oldcols)
        full = 1;
    else if (lem_top != oldtop)
        lem_mark_range(LEM_LISTTOP, lem_playrow - 1);

    /* ARM THE CLIP REGION UNLESS THE KERNEL ALREADY DID (SPEC.md 11.3). It arms
     * one for W_PAINT and FOR NOTHING ELSE, and os88_font_run() clips to the
     * window rect and to nothing inside it - so a repaint reached from a click,
     * a key, the settle timer or the first wake letters straight over whatever
     * window is covering ours. Worse, the flush then marks those cells clean,
     * so the shadow claims a glass it never reached and the next compare
     * answers "nothing changed" for them (apps/c64/c64.c records exactly this).
     * A refusal means not one pixel of us shows: draw nothing AND leave the
     * shadow INVALID, so the expose that follows repaints in full. The region
     * dies at the kernel's own gfx_unlock, so there is nothing to undo. */
    if (arm && os88_wm_clip_set(win) < 0) {
        lem_sh_ok = 0;
        return;
    }

    /* THE CHROME'S SIDE OF THE LOOP IS THE SIDE IT IS NOT DRAWN OVER. Leaving
     * the list, the two rules have to come down BEFORE the paragraph's text
     * goes over their columns; on the list they go down AFTER the rows, because
     * a full repaint re-letters the gutter cell (the shadow was blanked) and
     * would rub the vertical rule out again. On a PARTIAL repaint neither is
     * needed at all: nothing composed can reach either of them. */
    if (lem_screen != LEM_SC_LIST && full && lem_ovl == 1 && !lem_glass_blank)
        ovl_chrome(lem_screen);
    lem_glass_blank = 0;

    /* There is ONE loop and not two - and it runs over the DAMAGE RANGE, not
     * over the box (see lem_mark above for what composing every row was
     * costing). `full` is the whole box; a partial repaint is what its caller
     * marked, clamped to the rows that exist. */
    if (full) {
        lo = 0;
        hi = lem_rows - 1;
    } else {
        lo = lem_rlo;
        hi = lem_rhi;
        if (hi > lem_rows - 1)
            hi = lem_rows - 1;
    }
    for (r = lo; r <= hi; r++) {
        if (full) {
            fc0 = 0;
            fc1 = lem_cols - 1;
        } else {
            if (!lem_dirty[r])
                continue;
            fc0 = lem_dc0[r];
            fc1 = lem_dc1[r];
            if (fc1 > lem_cols - 1)
                fc1 = lem_cols - 1;
        }
        lem_row_compose(r);
        lem_flush_row(r, fc0, fc1);
        lem_c_rows++;
        lem_c_cols += fc1 - fc0 + 1;
    }

    /* CONSUMED, whatever was drawn - including a row marked past the bottom of
     * a box that has since shrunk, which is why this clears the whole array
     * rather than the range. */
    for (r = 0; r < LEM_SH_ROWS; r++)
        lem_dirty[r] = 0;
    lem_rlo = 0;
    lem_rhi = -1;

    if (lem_screen == LEM_SC_LIST && full && lem_ovl == 1)
        ovl_chrome(lem_screen);
    lem_sh_ok = 1;
}

/* lem_pv_defer - repaint the list NOW and let the preview pane SETTLE.
 *
 * The pane is seven of the ~180 glyph cells a selection move costs and about
 * 140 of them, so on a 4.77 MHz 8088 it is ~126 ms of the ~168 an arrow key
 * spends - and at autorepeat the machine falls that far behind on every key
 * held (PERFORMANCE.md's input overrun). The list rows go down on the
 * keystroke, where the user is looking; the pane is armed for LEM_PV_SETTLE
 * ticks and re-armed by every further key, so a held arrow draws it ONCE, when
 * the arrow stops.
 *
 * TWO SECOND PATHS, and both are ordinary. os88_wm_timer() is REFUSED on a
 * kern_small machine (SPEC.md 13.8.2), and the shadow may not describe the
 * glass yet - either way the pane is simply drawn now, which is what this
 * program did before the timer existed. */
#define LEM_PV_SETTLE 3             /* ticks at 18.2 Hz - 165 ms, longer than
                                     * the BIOS autorepeat interval, so a held
                                     * key never reaches the shot */

static void lem_pv_defer(void *win)
{
    if (lem_sh_ok && os88_wm_timer(win, LEM_PV_SETTLE) == 0) {
        lem_pv_due = 1;
        lem_pv_hold = 1;
        lem_repaint(win, 0, 1);     /* the caller marked the two level rows */
        lem_pv_hold = 0;
        return;
    }
    lem_pv_due = 0;
    /* No timer and no settle, so the pane is drawn on this keystroke: its rows
     * join the two the caller marked - the PANE HALF of them, because the list
     * half of those rows is what the caller's own two marks already cover and
     * what every other row on the page already has right. */
    lem_mark_pane_rows();
    lem_repaint(win, 0, 1);
}

/* --- selection -------------------------------------------------------------- */

static void lem_select(void *win, int row)
{
    if (row < 0)
        row = 0;
    if (row >= LEM_PERRAT)
        row = LEM_PERRAT - 1;
    if (row == lem_sel)
        return;
    /* THE ROWS THIS CHANGES, NAMED HERE (lemui.c lem_mark): the level row that
     * loses the selection bar, the one that gains it, and the Play control,
     * whose greying is a predicate over the selection. The preview pane's rows
     * are marked by lem_pv_defer() only on the arm that actually draws it. A
     * page turn is caught inside lem_repaint(), which is the one thing this
     * call site cannot see. */
    lem_mark_level(lem_sel);
    lem_sel = row;
    lem_mark_level(lem_sel);
    /* THE CONTROL AND NOT ITS ROW. lem_row_play() writes one run of lem_playw
     * cells and lem_row_clear()'s spaces after it, which the shadow always
     * already carries - so the 55 columns past the control are scanned at
     * ~300-450 us each to draw nothing, and the flush's end-trim then discards
     * them. That is 19-28 ms of a ~50 ms scan on a key that autorepeats at
     * ~100 ms. lem_playw is written by the compose that put the control on the
     * glass, exactly as lem_listw is when lem_mark_level() reads it. */
    lem_mark_cols(lem_playrow, 0, lem_playw - 1);
    lem_pv_defer(win);
}

/* lem_set_rating - AND IT READS NO FILE.
 *
 * lem_rating_load() is os88_file_read() on LEMR<n>.LEM: a whole-file open, so a
 * directory walk, a FAT walk and the data run - three int 13h calls, ~400 ms
 * apiece on the target and more with the motor spun down (PERFORMANCE.md). This
 * is reached from LEFT and RIGHT (lemmings.c os88_onkey) and from a tab click,
 * and os88.h pins both of those as "the UI task, the gfx lock HELD". So it was
 * over a second of TOTALLY FROZEN GLASS per press, with every other window's
 * painter stopped behind the lock, on keys that autorepeat at ~100 ms. It is
 * invisible here for the usual reason: QEMU's floppy answers instantly.
 *
 * WHAT HAPPENS INSTEAD is the shape this file already uses for the progress
 * read. The rating moves, the load is OWED to os88_onwake() - the one lock-free
 * callback - and the tab strip is repainted immediately so the key is
 * acknowledged. lem_sel and lem_top are moved by the WAKE and not here, so the
 * page on the glass and the page the hit test answers from stay the same page
 * until the new one is actually readable.
 *
 * RE-ARMED RATHER THAN STACKED: a second rating key while one is owed moves
 * lem_rating again and the wake reads whichever the user stopped on, which is
 * also what makes a held Left one file read rather than thirty. */
static void lem_set_rating(void *win, int rating)
{
    if (rating < 0 || rating >= LEM_RATINGS || rating == lem_rating)
        return;
    lem_rating = rating;
    lem_ratpend = 1;
    lem_pv_due = 0;                 /* nothing to settle: the pane is held */
    if (lem_sh_ok) {
        /* THE TAB STRIP AND THE STATE ROWS, and nothing else: the level rows
         * are HELD (lem_row_list's lem_list_hold arm) until the wake has
         * actually read the new rating's file, and Save Progress is per rating
         * so it moves with the tab. */
        lem_mark(0);
        lem_mark_range(lem_staterow, lem_staterow + LEM_STATE_ROWS - 1);
        lem_list_hold = 1;
        lem_repaint(win, 0, 1);
        lem_list_hold = 0;
    }
    os88_wm_wake(win);
}

/* --- the hit test ----------------------------------------------------------
 * Answered from the SAME numbers the compose used, which is why lem_tabx[] and
 * lem_playw are written there rather than recomputed here. */
static int lem_hit(int x, int y)
{
    int row, col, i;

    row = (y - lem_org.y - LEM_PADY);
    if (row < 0)
        return LEM_HIT_NONE;
    row = row / LEM_RPITCH;
    col = (x - lem_org.x - LEM_PADX) / LEM_CELLW;
    if (row >= lem_rows || col < 0 || col >= lem_cols)
        return LEM_HIT_NONE;

    if (row == 0) {
        for (i = 0; i < LEM_RATINGS; i++)
            if (lem_tabw[i] > 0 && col >= lem_tabx[i] &&
                col < lem_tabx[i] + lem_tabw[i])
                return LEM_HIT_TAB + i;
        return LEM_HIT_NONE;
    }
    if (row >= LEM_LISTTOP && row < LEM_LISTTOP + lem_nlist &&
        col < lem_listw)
        return LEM_HIT_ROW + (row - LEM_LISTTOP);
    if (row == lem_playrow && col < lem_playw)
        return LEM_HIT_PLAY;
    if (row >= lem_staterow && row < lem_staterow + LEM_STATE_ROWS)
        return LEM_HIT_STATE + (row - lem_staterow);
    return LEM_HIT_NONE;
}

/* --- the three screens' transitions -----------------------------------------
 *
 * lem_blank_glass - ONE FILL, then a shadow that says the glass is blank.
 *
 * A SCREEN CHANGE IS THE SAME SITUATION AS A DISMISSED OPAQUE CARD and takes
 * the same path, which this file owned one callback away (lemmings.c
 * lem_abdismiss) and did not use here. Going list -> fact, each of the twelve
 * level rows composes to a single lem_cols-wide TEXT run whose shadow still
 * carries the list half AND the preview pane, so the flush's end trims reach
 * almost nothing and every erased character is paid as a ~900 us glyph cell:
 * ~860 cells and ~20 calls, ~790 ms of frozen glass, on a click.
 *
 * One os88_gfx_fill over the content box is 756 us and puts back exactly the
 * white ground kernel/wm.inc's WF_OWNBG interlock lays down before a W_PAINT;
 * seeding the shadow blank rather than invalidating it then lets the repaint
 * letter the cells the NEW screen carries and skip the ones it does not. The
 * fill and the seed are one fact stated twice and must not drift.
 *
 * IT ARMS THE REGION AND ITS CALLERS PASS arm = 0: the fill has to be inside
 * the same clip region as the letters. A refusal means not one pixel of us
 * shows, so nothing is drawn and the shadow is left INVALID for the expose that
 * follows - which is why this answers a status and every caller tests it. */
static int lem_blank_glass(void *win)
{
    if (os88_wm_clip_set(win) != 0) {
        lem_sh_ok = 0;
        return 0;
    }
    lem_layout(win);                    /* lem_org / lem_sz for the fill */
    os88_set_color(OS88_WHITE);
    os88_gfx_fill(lem_org.x, lem_org.y,
                  lem_org.x + lem_sz.w - 1, lem_org.y + lem_sz.h - 1);
    os88_set_color(OS88_BLACK);
    lem_sh_blank();                     /* the glass IS blank now */
    /* ...so ovl_chrome()'s ERASE arm has nothing left to erase: the fill took
     * both rules down with everything else. It still runs on the LIST side,
     * where it DRAWS them (lem_repaint's two call sites). */
    lem_glass_blank = 1;
    return 1;
}

static void lem_show_fact(void *win, int labelid, int strid, int level)
{
    if (lem_ovl != 1 || strid == LEMS_NONE)
        return;
    if (!ovl_greyfact(labelid, strid, level))
        return;
    lem_wrap();
    lem_screen = LEM_SC_FACT;
    if (!lem_blank_glass(win))
        return;
    lem_repaint(win, 1, 0);             /* 0: the region above is ours */
}

static void lem_back(void *win)
{
    lem_screen = LEM_SC_LIST;
    if (!lem_blank_glass(win))
        return;
    lem_repaint(win, 1, 0);
}

/* lem_play - what the Play control does in WAVE 1: the level's own preview
 * screen, in the original's wording and its own seven lines, drawn in the
 * window. Wave 5 moves exactly this content inside SPEC.md 53's bracket, into
 * the second foreign mode each adapter needs to hold the original's 640x350
 * layout, lettered in MAIN.DAT's own purple font over its own brown background
 * (SPEC.md 92.4.3) - and the raster the level itself needs arrives in wave 2.
 * What is here now is the content and the transition, not a placeholder.
 *
 * THE MODULE IS FORCED RESIDENT BEFORE ANY OF IT, and that is not tidiness:
 * cc_ovneed TOASTS ITS OWN REFUSAL, a kernel drawing call this C never wrote
 * and cannot intercept, and once the bracket owns the screen that toast is
 * painted with desktop geometry into a foreign mode (SPEC.md 92.5). So the
 * launch refuses IN A WINDOW here, where a toast is an ordinary thing. */
static void lem_play(void *win)
{
    /* THE MODULE IS NOT MADE RESIDENT HERE, AND IT USED TO BE. lem_first_wake()
     * calls ovl_ready(), which tools/cc8086.py fronts with cc_ovneed: an
     * OSAPI_MEM_CLAIM, then a directory walk of a 39-entry folder and a
     * 2,760-byte read of LEMMINGS.OVL - 3+ int 13h at ~400 ms apiece on the
     * target - and Enter, Space and a Play click all arrive with the GFX LOCK
     * HELD, so every other window's painter stops behind it. That is exactly
     * the defect SPEC.md 92.6's first table row records as removed from
     * W_PAINT, put back one callback along.
     *
     * IT WAS ALSO REDUNDANT AS THE NET IT WAS WRITTEN TO BE: lem_kick() tests
     * !lem_waked and re-posts the wake from os88_onclick and os88_onkey before
     * either of this function's callers reaches it, so a REFUSED post (os88.h:
     * -1, the ring full) is already recovered - and on that one path the guard
     * did the freezing instead of the deferring. A first Play press with the
     * module still owed returns silently at the lem_ovl test below and the
     * second one works. */
    if (!lem_play_ok()) {
        if (lem_rat_ok && !lem_ent_here(lem_sel))
            lem_show_fact(win, LEMS_NONE,
                          lem_ent_u8(lem_sel, LEM_E_MISSING), lem_sel);
        return;
    }
    if (lem_ovl != 1) {
        /* NOTHING IS SAID HERE, and the version that said something said the
         * wrong thing. What refused is LEMMINGS.OVL, and cc_ovneed already
         * toasted the true reason with the file named - "LEMMINGS.OVL is not on
         * this disk", "does not match this program", "Not enough memory for
         * LEMMINGS.OVL" (crt0.asm:1157-1159) - at the first wake. The sentence
         * that used to go here is about the BAND FILES, which on such a disk
         * are all present, and a second toast retires the first (SPEC.md 59):
         * the accurate sentence was replaced by an inaccurate one. SPEC.md 47's
         * rule in its toast form - name a fact, never a guess. */
        return;
    }
    /* NOTHING IS RECORDED HERE, AND SOMETHING USED TO BE. This keystroke merely
     * OPENS the preview screen; in the original an access code is issued on
     * COMPLETING a level, and SPEC.md 92.8's Save Progress cell says of the
     * refusing case that "the session is played and the result is shown, and
     * nothing is recorded" - i.e. recording belongs to the RESULT. Advancing
     * lem_prog[] on Play credited a level that had not been played: the glass
     * read "Save Progress  1" on Tricky after one click, in a build with no
     * gameplay in it at all.
     *
     * The owner is the POSTVIEW's success path (wave 4's progress work, which
     * SPEC.md 92.8 names). Until there is a result to record, Save Progress
     * reads what the file said and nothing moves it. lem_pw_rat / lem_pw_lvl /
     * lem_progwrite and os88_onwake()'s deferred ovl_progress_write() stay
     * exactly as they are - the write is still owed to the one lock-free
     * callback, and it is the trigger that moves, not the mechanism. */
    ovl_preview(lem_sel);
    lem_wrap();
    lem_screen = LEM_SC_PREVIEW;
    if (!lem_blank_glass(win))
        return;
    lem_repaint(win, 1, 0);             /* 0: the region above is ours */
}
