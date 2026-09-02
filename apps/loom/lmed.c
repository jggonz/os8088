/* ============================================================================
 * os8088 - apps/loom/lmed.c
 *
 * THE EDITOR. #included by apps/loom/loom.c after lmproj.c.
 *
 * A plain monospaced SOURCE editor: no wrap, no styles, no proportional
 * anything. A line that runs off the right is reached by scrolling
 * horizontally, which is the only honest choice for a language whose
 * indentation is part of how it reads - wrapping WML would move a closing
 * angle bracket onto a line of its own and a pack error's column would stop
 * meaning anything.
 *
 * ---------------------------------------------------------------------------
 * WHY IT IS C, AND NOT NOTE PAD'S ENGINE
 * ---------------------------------------------------------------------------
 * WEAVE-SPEC 1.2 said "Note Pad's editor engine transplanted with prefix
 * `lm_` (the SPEC.md 68 precedent)", and the transplant was researched and
 * loses. SPEC.md 68's np_ -> wd_ rename was free ONLY because it stayed
 * inside one assembler, one segment and one flat namespace: apps/word is
 * assembly and so is apps/notepad, so the rename was a rename. LOOM is a C
 * package - a different segment model (SS != DS), a different calling
 * convention, and a compiler that cannot see an nasm equ - and Note Pad's
 * state is 35 hand-numbered `equ` offsets into one block, which its own
 * comment calls "a large risk for no measurable gain" to renumber. What
 * survives the transplant is what is worth having anyway: the DAMAGE MODEL,
 * which is written out below and is apps/cword/cword.c's.
 *
 * ---------------------------------------------------------------------------
 * THE DAMAGE MODEL, WHICH IS THE POINT OF THIS FILE
 * ---------------------------------------------------------------------------
 * On the target machine one 8x8 glyph cell is ~900 us and a 78-cell row is
 * ~71 ms (CLAUDE.md's table). A 51-row pane repainted per keystroke is over
 * THREE SECONDS. So:
 *
 *   THE SHADOW IS THE SINGLE SOURCE OF TRUTH ABOUT THE GLASS. One byte per
 *   screen cell, holding the character that is on the screen there. Every
 *   repaint goes through lm_putrow(), which builds what the row SHOULD show,
 *   compares it against the shadow (lm_shdiff, in apps/loom/lmui.inc), and
 *   issues ONE os88_font_run() over the changed columns only - or none at
 *   all. Nothing else draws text in this pane, Preview included.
 *
 *   IT LIVES IN THE SOURCE CLAIM and not in bss: 88 x 52 is 4,576 bytes and
 *   this package has hundreds spare, not thousands (apps/loom/loom.c's
 *   LM_SHBASE says it at length). It is per-GLASS state rather than per-
 *   package state, so a claim is where it belongs anyway.
 *
 *   SCROLLING MOVES PIXELS AND MOVES THE SHADOW WITH THEM. A view scroll, a
 *   line inserted and a line joined are all os88_gfx_scroll() plus the same
 *   shift applied to the shadow, plus POISONING the rows the scroll vacated
 *   (0xFF, a byte no display cell can hold, so the compare always fires).
 *   The shadow therefore never lies, and the correctness of every operation
 *   below reduces to one question: does the shadow still describe the glass?
 *   The one place the answer was NO cost a run's worth of stale rows: a
 *   W_PAINT arrives with the content already whitened BY THE KERNEL, so
 *   lm_repaint() invalidates both shadows on every entry (loom.c says so
 *   there) - a shadow kept across one claims a row is already right about
 *   pixels that are gone.
 *
 *   THE PADDING IS THE ERASE. A row is built to exactly lm_ecols cells with
 *   spaces on the end, so a shorter line overwrites the tail of the longer
 *   one it replaced in the SAME font_run. The erase-then-letter pair is the
 *   canonical double-draw in this tree and it is invisible in an emulator.
 *
 *   THE CARET IS AN XOR BANKED AT ITS OWN POSITION. It is turned OFF before
 *   anything else happens and ON at the end, so no scroll ever carries a
 *   caret with it and no row is ever drawn over one.
 *
 * WHAT EACH OPERATION COSTS. Derived BY CONSTRUCTION from the code below
 * rather than measured by a harness - LOOM has no apps/cword/hosttest/
 * cwuitest.c equivalent in this wave, and saying so is better than implying a
 * measurement that was not taken. `calls` counts primitive calls (~756 us
 * each) and `cells` glyph cells (~900 us each), CLAUDE.md's table; the pane is
 * 66 columns x 51 rows, which is what a VGA window opened at SPEC.md 11.95's
 * standard rect gives with the sidebar showing.
 *
 *     a full pane repaint (W_PAINT)         78 calls  3565 cells  3.27 s
 *     a printable typed at the line's end    3 calls     1 cell     3 ms
 *     a printable typed at its START         3 calls    66 cells    62 ms
 *     an arrow that does not scroll          2 calls     0 cells     2 ms
 *     an arrow that scrolls one line         7 calls    66 cells    65 ms
 *     Enter in the middle of a line          8 calls  ~100 cells    96 ms
 *     Backspace joining two lines            8 calls  ~100 cells    96 ms
 *     PgDn                                  56 calls  3300 cells  3.01 s
 *
 * The first and last lines are what a screen of text costs on this machine
 * whatever writes it - a full screen of glyphs is a full screen of glyphs -
 * and every line between them is what the model buys. Enter and Backspace are
 * eight calls rather than fifty because os88_gfx_scroll() moves the fifty rows
 * that only MOVED: one primitive instead of fifty font runs, and the two rows
 * that actually changed are the only ones lettered.
 * ==========================================================================*/

/* LM_SH_STRIDE, LM_SH_ROWS, LM_SHBASE and LM_CLAIMKB are in apps/loom/loom.c,
 * because lmproj.c takes the claim and is #included before this file. What
 * they mean is in the paragraph above them; what the shadow IS is above. */
#define LM_POISON     0xFF              /* a byte no display cell can hold */

static char lm_rb[LM_SH_STRIDE + 2];    /* one row under construction */

static int      lm_ltab[LM_MAXLINE + 1];  /* every line's first byte */
static unsigned lm_caret;                 /* the caret, a byte offset */
static int      lm_top;                   /* the first visible line */
static int      lm_hoff;                  /* ...and the first visible column */
static int      lm_caron, lm_carx, lm_cary;
static int      lm_sblk[7];               /* os88ui.inc's scroll block */
static int      lm_sbdrawn;               /* the pos the bar last drew */
static int      lm_sbhave;                /* ...and whether it is drawn at all */

/* The scan codes an editor wants. Mirrored from apps/cword/cword.c, which
 * mirrored them from the XT's own keyboard: the extended arrows land on these
 * too, the E0 prefix naming no key of its own (apps/os88api.inc's KSC_*). */
#define LM_K_HOME   0x47
#define LM_K_UP     0x48
#define LM_K_PGUP   0x49
#define LM_K_LEFT   0x4B
#define LM_K_RIGHT  0x4D
#define LM_K_END    0x4F
#define LM_K_DOWN   0x50
#define LM_K_PGDN   0x51
#define LM_K_DEL    0x53

/* ============================================================================
 * THE LINE TABLE
 *
 * One word a line, LM_MAXLINE of them (loom.h's 400, four times the largest
 * demo). It is rebuilt WHOLE only when a slot is loaded or switched, by
 * apps/loom/lmui.inc's assembly scanner - a per-byte loop over as much as
 * 6KB, which through os88_peek() would be ~68 ms of near calls and in
 * assembly is a few hundred microseconds.
 *
 * EVERY EDIT ADJUSTS IT INSTEAD OF REBUILDING IT, and that is not a
 * micro-optimisation: a rebuild is ~39 ms on the target machine (6,144 bytes
 * at ~30 clocks a byte on a 4.77 MHz 8088), which is a rescan of the whole
 * file on every keystroke. An insert shifts at most 400 words, which is under
 * 2 ms, and it is exactly as correct.
 * ==========================================================================*/

static void lm_relines(void)
{
    if (lm_srcseg == 0 || !lm_shave[lm_slot]) {
        lm_ltab[0] = 0;
        lm_nline = 1;
        return;
    }
    lm_nline = (int) lm_lines(lm_srcseg, (unsigned) lm_slot * LM_TEXTMAX,
                              lm_slen[lm_slot], lm_ltab, LM_MAXLINE);
    if (lm_nline < 1)
        lm_nline = 1;
}

/* lm_lineof - which line an offset is on. A binary search rather than a walk
 * because it runs once per keystroke over up to 400 entries: nine compares
 * against two hundred. */
static int lm_lineof(unsigned off)
{
    int lo, hi, mid;

    lo = 0;
    hi = lm_nline - 1;
    while (lo < hi) {
        mid = (lo + hi + 1) >> 1;
        if ((unsigned) lm_ltab[mid] <= off)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

static unsigned lm_lineend(int line)
{
    if (line + 1 < lm_nline)
        return (unsigned) lm_ltab[line + 1] - 1;   /* before the newline */
    return lm_slen[lm_slot];
}

static void lm_tshift(int from, int d)
{
    int i;

    for (i = from; i < lm_nline; i++)
        lm_ltab[i] = lm_ltab[i] + d;
}

static void lm_tins(int at, int val)
{
    int i;

    if (lm_nline >= LM_MAXLINE)
        return;
    for (i = lm_nline; i > at; i--)
        lm_ltab[i] = lm_ltab[i - 1];
    lm_ltab[at] = val;
    lm_nline++;
}

static void lm_trem(int at)
{
    int i;

    if (at <= 0 || at >= lm_nline)
        return;
    for (i = at; i + 1 < lm_nline; i++)
        lm_ltab[i] = lm_ltab[i + 1];
    lm_nline--;
}

/* ============================================================================
 * THE SHADOW
 * ==========================================================================*/

/* lm_shoff - where row r's shadow lives in the source claim. */
static unsigned lm_shoff(int r)
{
    return LM_SHBASE + (unsigned) r * LM_SH_STRIDE;
}

static void lm_ed_invalidate(void)
{
    if (lm_srcseg)
        lm_sfill(lm_srcseg, LM_SHBASE, LM_POISON,
                 (unsigned) LM_SH_ROWS * LM_SH_STRIDE);
    lm_sbhave = 0;
    lm_caron = 0;
}

static void lm_poison(int r)
{
    if (r < 0 || r >= LM_SH_ROWS || lm_srcseg == 0)
        return;
    lm_sfill(lm_srcseg, lm_shoff(r), LM_POISON, LM_SH_STRIDE);
}

/* lm_shmove - shift the shadow the same way os88_gfx_scroll() just shifted
 * the pixels. Rows `from`..`to` move by `d` ROWS, positive = downwards.
 *
 * THIS IS NOT gfx_scroll's SIGN AND THE COMMENT USED TO SAY IT WAS.
 * OSAPI_GFX_SCROLL's dy is "positive scrolls the content UP"
 * (apps/os88api.inc), which is the opposite of this. Getting it backwards is
 * invisible in the model - the FILE stays perfectly correct, because the line
 * table and the claim never see a pixel - and shows on the glass as rows that
 * are two lines out. It was found exactly that way, by typing an Enter and
 * reading the saved file against the screen.
 *
 * ONE MOVE AND NOT A LOOP OVER ROWS, because the rows are contiguous in the
 * claim: the whole band is (to-from+1) * LM_SH_STRIDE bytes and lm_move()
 * picks its own direction for an overlapping range, which is exactly the
 * question a row-by-row loop would have to answer by hand. */
static void lm_shmove(int from, int to, int d)
{
    if (lm_srcseg == 0 || to < from)
        return;
    lm_move(lm_srcseg, lm_shoff(from + d), lm_shoff(from),
            (unsigned) (to - from + 1) * LM_SH_STRIDE);
}

/* ============================================================================
 * THE CARET
 *
 * An XOR bar, banked at its own position. XOR because putting it back costs
 * exactly what putting it up cost and needs no knowledge of what is under it;
 * banked because the position it has to be removed from is the one it was
 * DRAWN at, not the one the caret is at now - which is the defect this
 * pattern exists to prevent and the reason lm_carx/lm_cary are separate
 * variables from the caret offset.
 *
 * IT DOES NOT BLINK. SPEC.md 13.9 gives a window one one-shot timer and a
 * blink spends it; on the target machine a blink is two XOR rects every half
 * second for ever, whether or not anybody is typing. A static caret says the
 * same thing for nothing, and apps/loom/loom.asm's missing CC_HAS_ONTIMER is
 * where that decision is recorded.
 * ==========================================================================*/

static void lm_ed_caret_off(void)
{
    if (!lm_caron)
        return;
    os88_gfx_xor_fill(lm_carx, lm_cary, lm_carx + 1, lm_cary + 7);
    lm_caron = 0;
}

static void lm_ed_caret_on(void)
{
    int line, col, r, c;

    if (lm_caron || lm_state != LM_ST_EDIT)
        return;
    line = lm_lineof(lm_caret);
    col = (int) (lm_caret - (unsigned) lm_ltab[line]);
    r = line - lm_top;
    c = col - lm_hoff;
    if (r < 0 || r >= lm_erows || c < 0 || c > lm_ecols)
        return;                         /* off the pane: nothing to draw, and
                                         * nothing banked to take back */
    lm_carx = lm_ex + c * 8;
    lm_cary = lm_oy + r * 8;
    os88_gfx_xor_fill(lm_carx, lm_cary, lm_carx + 1, lm_cary + 7);
    lm_caron = 1;
}

/* ============================================================================
 * DRAWING
 * ==========================================================================*/

/* lm_ed_row - one row of the pane, and ONLY the columns that changed.
 * Answers 1 if it drew.
 *
 * The row is built to exactly lm_ecols cells: the text, then spaces. Bytes
 * below 0x20 become spaces rather than glyphs - a tab in a source file is a
 * legal byte and the cell font has a picture for every code point, so an
 * unconverted tab would draw as a character and the caret column would stop
 * agreeing with the text. TABS ARE ONE COLUMN HERE, deliberately: expanding
 * them would make the editor's column and the compiler's column different
 * numbers, and WEAVE-SPEC 10.5's messages carry a LINE and the caret has to
 * land on it. */
/* lm_putrow - THE ONE PLACE ANY ROW OF THE PANE IS DRAWN. lm_rb holds `n`
 * characters; this pads it to lm_ecols, diffs it against the shadow, and
 * issues at most one os88_font_run() over the changed span. Preview draws
 * through it too (lmprev.c), which is what keeps the shadow honest when the
 * pane is showing something that is not the editor. */
static int lm_putrow(int r, int n)
{
    int i, first, last;
    unsigned d;
    char save;

    if (r < 0 || r >= lm_erows || r >= LM_SH_ROWS || lm_srcseg == 0)
        return 0;
    for (i = n; i < lm_ecols; i++)
        lm_rb[i] = ' ';
    lm_rb[lm_ecols] = 0;

    d = lm_shdiff(lm_srcseg, lm_shoff(r), lm_rb, (unsigned) lm_ecols);
    if (d == 0xFFFF)
        return 0;                       /* identical: not one pixel */
    first = (int) (d & 0xFF);
    last = (int) (d >> 8);
    w_pcopy(lm_rb + first, lm_srcseg, lm_shoff(r) + (unsigned) first,
            (unsigned) (last - first + 1));
    save = lm_rb[last + 1];
    lm_rb[last + 1] = 0;
    os88_font_run(lm_ex + first * 8, lm_oy + r * 8, lm_rb + first,
                  OS88_BLACK, OS88_WHITE);
    lm_rb[last + 1] = save;
    return 1;
}

static int lm_ed_row(int r)
{
    int line, i, n;
    unsigned s, e;

    line = lm_top + r;
    n = 0;
    if (line >= 0 && line < lm_nline && lm_shave[lm_slot]) {
        s = (unsigned) lm_ltab[line] + (unsigned) lm_hoff;
        e = lm_lineend(line);
        if (s < e) {
            n = (int) (e - s);
            if (n > lm_ecols)
                n = lm_ecols;
            w_copy(lm_srcseg, (unsigned) lm_slot * LM_TEXTMAX + s, lm_rb,
                   (unsigned) n);
        }
    }
    for (i = 0; i < n; i++)
        if ((unsigned char) lm_rb[i] < 0x20)
            lm_rb[i] = ' ';
    return lm_putrow(r, n);
}

/* lm_ed_bar - the scroll bar. The WHOLE bar is sixteen calls and moving the
 * thumb is three, so the two cases are separate: os88ui.inc's os88ui_sbmove
 * translates the thumb and leaves the frame, the rules, the arrows and the
 * untouched track exactly where they are. */
static void lm_ed_bar(int force)
{
    int old;

    old = lm_sbdrawn;
    lm_sblk[0] = lm_sbx;
    lm_sblk[1] = lm_oy;
    lm_sblk[2] = lm_sbx + LM_SBW - 1;
    lm_sblk[3] = lm_oy + lm_erows * 8 - 1;
    lm_sblk[4] = lm_nline;
    lm_sblk[5] = lm_erows;
    lm_sblk[6] = lm_top;
    if (force || !lm_sbhave) {
        lm_sbar(lm_sblk);
        lm_sbhave = 1;
    } else if (old != lm_top)
        lm_sbmove(lm_sblk, old);
    lm_sbdrawn = lm_top;
}

static void lm_ed_paint(int force)
{
    int r;

    if (lm_state != LM_ST_EDIT)
        return;
    lm_ed_caret_off();
    if (force)
        lm_ed_invalidate();
    for (r = 0; r < lm_erows; r++)
        lm_ed_row(r);
    lm_ed_bar(force);
    lm_ed_caret_on();
}

/* ============================================================================
 * THE VIEW
 * ==========================================================================*/

/* lm_view_to - put the first visible line at `t`, MOVING THE PIXELS rather
 * than redrawing them when the two views overlap. os88_gfx_scroll() is one
 * primitive against up to 24 font runs, and its own contract says x1 and x2+1
 * must be multiples of 8 - which they are by construction here, because
 * WEAVE-SPEC 7.1.2's rounded origin makes every cell column one.
 *
 * A REFUSED SCROLL IS A NORMAL PATH (a clip region that does not wholly
 * contain the rect refuses, and so does an obscured window): the shadow is
 * poisoned whole and the next lm_ed_paint() redraws the pane. That is the
 * slow path taken honestly rather than a picture that is quietly wrong. */
static void lm_view_to(int t)
{
    int dr, r;

    if (t > lm_nline - 1)
        t = lm_nline - 1;
    if (t < 0)
        t = 0;
    if (t == lm_top)
        return;
    lm_ed_caret_off();
    dr = t - lm_top;                    /* how many LINES the view advances:
                                         * positive = further down the file,
                                         * which moves the PIXELS UP, which is
                                         * exactly OSAPI_GFX_SCROLL's positive
                                         * dy. One variable, one sign, and it
                                         * is the API's */
    lm_top = t;
    if (dr >= lm_erows || dr <= -lm_erows) {
        lm_ed_invalidate();             /* nothing overlaps: no pixel is worth
                                         * moving */
        return;
    }
    if (os88_gfx_scroll(lm_ex, lm_oy, lm_ex + lm_ecols * 8 - 1,
                        lm_oy + lm_erows * 8 - 1, dr * 8) != 0) {
        lm_ed_invalidate();
        return;
    }
    if (dr > 0) {                       /* content up: the BOTTOM is vacated */
        lm_shmove(dr, lm_erows - 1, -dr);
        for (r = lm_erows - dr; r < lm_erows; r++)
            lm_poison(r);
    } else {                            /* content down: the TOP is vacated */
        lm_shmove(0, lm_erows - 1 + dr, -dr);
        for (r = 0; r < -dr; r++)
            lm_poison(r);
    }
}

/* lm_see - make the caret visible, vertically and horizontally. The vertical
 * half goes through lm_view_to() and therefore scrolls pixels; the horizontal
 * half cannot (a horizontal scroll would move the sidebar's pixels into the
 * pane through the same rect) and poisons instead, which is honest: changing
 * lm_hoff changes every row. */
static void lm_see(void)
{
    int line, col;

    line = lm_lineof(lm_caret);
    col = (int) (lm_caret - (unsigned) lm_ltab[line]);
    if (line < lm_top)
        lm_view_to(line);
    else if (line >= lm_top + lm_erows)
        lm_view_to(line - lm_erows + 1);
    if (col < lm_hoff) {
        lm_hoff = col;
        lm_ed_caret_off();
        lm_ed_invalidate();
    } else if (col > lm_hoff + lm_ecols - 1) {
        lm_hoff = col - lm_ecols + 1;
        lm_ed_caret_off();
        lm_ed_invalidate();
    }
}

static void lm_ed_reset(void)
{
    lm_caret = 0;
    lm_top = 0;
    lm_hoff = 0;
    lm_relines();
    lm_ed_invalidate();
}

static void lm_ed_goline(int line)
{
    if (line < 0)
        line = 0;
    if (line >= lm_nline)
        line = lm_nline - 1;
    lm_caret = (unsigned) lm_ltab[line];
    lm_hoff = 0;
    /* Put the target a third of the way down rather than at the very top: an
     * error the caret jumps to is read with the lines AROUND it (WEAVE-SPEC
     * 11.3's whole point is the loop's speed), and a line pinned to row 0 has
     * no context above it. */
    lm_view_to(line - lm_erows / 3);
    lm_see();
    lm_ed_paint(0);
}

/* ============================================================================
 * EDITING
 *
 * Every one of these does the same four things in the same order: refuse if
 * it cannot be done, move the bytes, adjust the line table, and tell the
 * glass what changed. The last step is always expressed as a SHADOW
 * operation - a poison, or a scroll plus a shift - and never as a repaint,
 * because lm_ed_paint() reads the shadow and decides for itself.
 * ==========================================================================*/

static void lm_refuse(const char *s)
{
    lm_say(s);
    lm_status_paint(0);
}

/* lm_ins - one byte at the caret. */
static void lm_ins(int ch)
{
    unsigned base, len;
    int line, r;

    base = (unsigned) lm_slot * LM_TEXTMAX;
    len = lm_slen[lm_slot];
    if (len + 1 > LM_TEXTMAX) {
        lm_l0();
        lm_ls(lm_fname(lm_slot));
        lm_ls(" is at its ");
        lm_ln(LM_TEXTMAX);
        lm_ls("-byte ceiling; LOOM will not truncate a source.");
        lm_refuse(lm_line);
        return;
    }
    if (ch == '\n' && lm_nline >= LM_MAXLINE) {
        lm_l0();
        lm_ls(lm_fname(lm_slot));
        lm_ls(" is at its ");
        lm_ln(LM_MAXLINE);
        lm_ls("-line ceiling.");
        lm_refuse(lm_line);
        return;
    }

    lm_ed_caret_off();
    lm_move(lm_srcseg, base + lm_caret + 1, base + lm_caret,
            len - lm_caret);
    w_pb(lm_srcseg, base + lm_caret, (unsigned) ch);
    lm_slen[lm_slot] = len + 1;
    if (!lm_smod[lm_slot]) {
        lm_smod[lm_slot] = 1;
        lm_side_paint();                /* the `*` appeared: one 12-cell row */
    }

    line = lm_lineof(lm_caret);
    lm_tshift(line + 1, 1);
    if (ch == '\n') {
        r = line - lm_top;
        /* OPEN A ROW WITH ONE PRIMITIVE. Everything from the caret's row down
         * moves one row lower; the row the scroll vacates is the caret's own,
         * which is about to be redrawn anyway. 23 font runs become 1. */
        if (r >= 0 && r < lm_erows - 1
            && os88_gfx_scroll(lm_ex, lm_oy + r * 8,
                               lm_ex + lm_ecols * 8 - 1,
                               lm_oy + lm_erows * 8 - 1, -8) == 0) {
            /* -8, because OSAPI_GFX_SCROLL's positive dy scrolls the content
             * UP and opening a row moves it DOWN (apps/os88api.inc). */
            lm_shmove(r, lm_erows - 2, 1);
            lm_poison(r);
        } else
            lm_ed_invalidate();
        lm_tins(line + 1, (int) lm_caret + 1);
    } else
        lm_poison(line - lm_top);
    lm_caret++;
    lm_see();
    lm_ed_paint(0);                     /* which draws the bar and puts the
                                         * caret back - see lm_ed_paint */
}

/* lm_del - the byte AT `p`. Backspace is lm_del(caret-1) with the caret moved
 * first, which is the one difference between the two keys. */
static void lm_del(unsigned p)
{
    unsigned base, len;
    int line, r, nl;

    len = lm_slen[lm_slot];
    if (p >= len)
        return;
    base = (unsigned) lm_slot * LM_TEXTMAX;
    nl = (w_b(lm_srcseg, base + p) == '\n');

    lm_ed_caret_off();
    lm_move(lm_srcseg, base + p, base + p + 1, len - p - 1);
    lm_slen[lm_slot] = len - 1;
    if (!lm_smod[lm_slot]) {
        lm_smod[lm_slot] = 1;
        lm_side_paint();
    }

    line = lm_lineof(p);
    if (nl) {
        r = line - lm_top;
        lm_trem(line + 1);
        lm_tshift(line + 1, -1);
        /* CLOSE A ROW WITH ONE PRIMITIVE, the mirror of the open above: every
         * row below the join moves one row up and the last row is vacated. */
        if (r >= 0 && r < lm_erows - 1
            && os88_gfx_scroll(lm_ex, lm_oy + (r + 1) * 8,
                               lm_ex + lm_ecols * 8 - 1,
                               lm_oy + lm_erows * 8 - 1, 8) == 0) {
            /* +8: closing a row moves the content UP, which is this API's
             * positive direction (apps/os88api.inc). */
            lm_shmove(r + 2, lm_erows - 1, -1);
            lm_poison(lm_erows - 1);
            lm_poison(r);               /* the joined line is longer now */
        } else
            lm_ed_invalidate();
    } else {
        lm_tshift(line + 1, -1);
        lm_poison(line - lm_top);
    }
    lm_caret = p;
    lm_see();
    lm_ed_paint(0);
}

/* ============================================================================
 * MOVING
 * ==========================================================================*/

static void lm_moveto(unsigned off)
{
    if (off > lm_slen[lm_slot])
        off = lm_slen[lm_slot];
    lm_ed_caret_off();
    lm_caret = off;
    lm_see();
    lm_ed_paint(0);
}

/* lm_vmove - up or down `d` lines, keeping the column where it can. A column
 * past the end of the target line lands at its end, which is every editor's
 * behaviour and the only one that does not need a remembered "goal column"
 * this editor has no room for. */
static void lm_vmove(int d)
{
    int line, col;
    unsigned s, e;

    line = lm_lineof(lm_caret);
    col = (int) (lm_caret - (unsigned) lm_ltab[line]);
    line += d;
    if (line < 0)
        line = 0;
    if (line >= lm_nline)
        line = lm_nline - 1;
    s = (unsigned) lm_ltab[line];
    e = lm_lineend(line);
    if (s + (unsigned) col > e)
        lm_moveto(e);
    else
        lm_moveto(s + (unsigned) col);
}

/* ============================================================================
 * THE KEYSTROKE
 * ==========================================================================*/

static void lm_ed_key(int ascii, int scan)
{
    int line;

    if (!lm_shave[lm_slot])
        return;
    if (scan == LM_K_LEFT) {
        if (lm_caret > 0)
            lm_moveto(lm_caret - 1);
        return;
    }
    if (scan == LM_K_RIGHT) {
        if (lm_caret < lm_slen[lm_slot])
            lm_moveto(lm_caret + 1);
        return;
    }
    if (scan == LM_K_UP) {
        lm_vmove(-1);
        return;
    }
    if (scan == LM_K_DOWN) {
        lm_vmove(1);
        return;
    }
    if (scan == LM_K_PGUP) {
        lm_vmove(-(lm_erows - 1));
        return;
    }
    if (scan == LM_K_PGDN) {
        lm_vmove(lm_erows - 1);
        return;
    }
    if (scan == LM_K_HOME) {
        line = lm_lineof(lm_caret);
        lm_hoff = 0;
        lm_moveto((unsigned) lm_ltab[line]);
        return;
    }
    if (scan == LM_K_END) {
        line = lm_lineof(lm_caret);
        lm_moveto(lm_lineend(line));
        return;
    }
    if (scan == LM_K_DEL) {
        lm_del(lm_caret);
        return;
    }
    if (ascii == 8) {                   /* Backspace */
        if (lm_caret > 0)
            lm_del(lm_caret - 1);
        return;
    }
    if (ascii == 13 || ascii == 10) {
        lm_ins('\n');
        return;
    }
    if (ascii == 9) {                   /* Tab - one byte, as it is in the
                                         * file. WEAVE-SPEC 3.1 collapses
                                         * whitespace at pack time, so what a
                                         * tab means to the compiler is
                                         * exactly what a space means */
        lm_ins('\t');
        return;
    }
    if (ascii >= 0x20 && ascii <= 0x7E)
        lm_ins(ascii);
}

/* ============================================================================
 * THE CLICK
 * ==========================================================================*/

static int lm_ed_click(int x, int y)
{
    int r, c, part, line;
    unsigned s, e;

    /* The scroll bar first: it owns the right LM_SBW pixels of the pane, and
     * os88ui_sbhit answers OS88UI_SBNONE for a point outside it, so a click
     * anywhere else falls straight through. What an arrow DOES to a view is
     * the caller's - os88ui.inc's own header says so, and it is why the bar
     * reports the thumb as a part of its own. */
    if (x >= lm_sbx && lm_sbhave) {
        part = lm_sbhit(lm_sblk, x, y);
        if (part == LM_SB_UP)
            lm_view_to(lm_top - 1);
        else if (part == LM_SB_DOWN)
            lm_view_to(lm_top + 1);
        else if (part == LM_SB_PGUP)
            lm_view_to(lm_top - (lm_erows - 1));
        else if (part == LM_SB_PGDN || part == LM_SB_THUMB)
            lm_view_to(lm_top + (lm_erows - 1));
        else
            return 0;
        lm_ed_paint(0);
        return 1;
    }

    r = (y - lm_oy) / 8;
    c = (x - lm_ex) / 8;
    if (r < 0 || r >= lm_erows || c < 0)
        return 0;
    line = lm_top + r;
    if (line >= lm_nline)
        line = lm_nline - 1;
    s = (unsigned) lm_ltab[line] + (unsigned) (lm_hoff + c);
    e = lm_lineend(line);
    if (s > e)
        s = e;
    lm_moveto(s);
    return 1;
}
