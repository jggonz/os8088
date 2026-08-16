/* ============================================================================
 * os8088 - apps/cword/cword.c          CWORD, a word processor written in C
 *
 * The first application of the os8088 C toolchain (SPEC.md 67), and a
 * descendant of Microsoft Word by a real line of inheritance rather than by
 * resemblance: it reads and writes RTF through tables transcribed out of the
 * Word for Windows 1.1a source code. apps/cword/cwrtftbl.[ch] hold those
 * tables and their full attribution; apps/cword/cwrtfio.c walks them.
 *
 *     Copyright (C) Microsoft Corporation for the material in
 *     apps/cword/cwrtftbl.[ch] and the interpreter shape in cwrtfio.c.
 *     Source: Microsoft Word for Windows 1.1a source code, Computer History
 *             Museum release, 2014, directory `Opus/`.
 *
 * THIS FILE - the user interface - is not derived from Opus. Its window
 * layout, its damage model and its redraw path are os8088's own, and the
 * house it echoes is the one in apps/notepad and apps/texpad.
 *
 * IT IS NOT SPEC.md 65's WORD PORT and shares nothing with it: that one is
 * apps/word, hand-written assembly, package WORD, `.DOC` and `.WTX`,
 * build/word*.img, vm/xt-word and vm/386-word. This one is apps/cword,
 * package CWORD, `.RTF`, build/cword*.img and vm/386-c-word. Two programs may
 * have similar ambitions; what they may not be is two things answering to one
 * name (SPEC.md 67.12).
 *
 * ---------------------------------------------------------------------------
 * WHAT IT DOES, AND WHAT IT DELIBERATELY DOES NOT
 * ---------------------------------------------------------------------------
 * A window with a menu bar (File / Edit / Format) and an About panel. One
 * document in a static buffer of 4,000 characters. Typing, Backspace, Delete,
 * the four arrows, Home and End, a click to place the caret, and word wrap.
 * Three character attributes - bold, italic, underline - carried per
 * character and round-tripped through RTF. Open and Save through the Standard
 * File dialog (SPEC.md 38).
 *
 * No tables, borders, footnotes, sections, columns, fields, styles or
 * printing: those are the 1,700 lines of Opus's RTFRARE.C and they are out of
 * scope by decision, not by accident. No selection either, which is what
 * makes the Format commands set the attribute that NEW characters get - the
 * same thing Word does with an insertion point and nothing selected.
 *
 * ---------------------------------------------------------------------------
 * WHAT A KEYSTROKE COSTS, WHICH IS THE POINT OF THE WHOLE DESIGN
 * ---------------------------------------------------------------------------
 * The target is a 4.77 MHz 8088. PERFORMANCE.md prices any gfx_* call at
 * **756 us flat** and one 8x8 glyph cell at **~900 us**, so A REDRAW IS PRICED
 * IN CALLS AND CELLS, NOT IN PIXELS. This window is 69 columns by 24 rows on a
 * 640x480 screen: repainting all of it is 29 calls and 1,682 cells - **1.5
 * SECONDS**. A program that repaints its text on every keystroke is unusable
 * on the target machine and looks perfect in QEMU, which is precisely the
 * defect PERFORMANCE.md says an emulator cannot show you. So:
 *
 *   THE GLASS IS SHADOWED. cw_sh[]/cw_sha[] hold the character and the
 *   attribute of every cell CURRENTLY ON THE SCREEN. A repaint lays out the
 *   rows that could have changed, compares each against the shadow, and draws
 *   the columns that differ and nothing else.
 *
 *   THE LAYOUT STOPS EARLY. After an insertion of one character, the rows
 *   below the edited paragraph hold the same text one byte further along. The
 *   moment a row's new start equals its old start plus that delta, every row
 *   below it is identical too - the wrap from there on depends on nothing that
 *   changed - so cw_relayout() stops, fixes the remaining rows' offsets by the
 *   delta and returns. It is the difference between touching 24 rows and two.
 *
 *   SCROLLING MOVES PIXELS. One row of scroll is os88_gfx_scroll() plus the
 *   one row that came into view (SPEC.md 5.5 makes the same argument).
 *
 * MEASURED, NOT ESTIMATED. The numbers below come from a host harness that
 * stubs the whole API with a model of the glass, counts every drawing call and
 * every glyph cell, and after each keystroke rebuilds what the screen ought to
 * show - from an independently written word wrap - and compares it cell for
 * cell. Two real defects came out of it that no screenshot would have shown:
 * a stale attribute strip after Clear All, and a redundant pair of white lines
 * on the scroll path. The us column is the count multiplied by PERFORMANCE.md's
 * two constants.
 *
 *   a full repaint (W_PAINT), 69x24            29 calls  1682 cells  1536 ms
 *   typing at the end of the document           3 calls     1 cell      3.2 ms
 *   backspace at the end of the document        3 calls     1 cell      3.2 ms
 *   typing one BOLD character                   4 calls     2 cells     4.8 ms
 *   an arrow key that does not scroll           2 calls     0 cells     1.5 ms
 *   typing at the START of a full 69-col line   3 calls    65 cells    61 ms
 *   Enter in the middle of a paragraph          4 calls    73 cells    69 ms
 *   a caret move that scrolls one row           6 calls    69 cells    67 ms
 *
 * The first five are what typing feels like: two to four calls and a cell or
 * two, well under a tenth of one 18.2 Hz tick. The last three are the honest
 * worst cases, and each is work the screen actually needs - inserting at the
 * start of a line moves the whole line one cell right, and a scroll brings a
 * row of text into view that was not there. None of them is the whole view,
 * which is the number they have to be compared against: 1.5 seconds.
 *
 * The other cost with a number on it is the memory move an insertion makes.
 * cw_memmove() in apps/cword/cword.asm is a hand-written `rep movsb` (SPEC.md
 * 67.11: the inner loop is assembly, C composes) and moves the tail of both
 * arrays - about 17 cycles a byte on an 8088, so ~3 ms per 1,000 characters
 * after the caret. At the END of the document, which is where typing happens,
 * it moves nothing.
 *
 * ---------------------------------------------------------------------------
 * THE FOUR C RULES (apps/cc/os88.h), AND WHERE THIS FILE MEETS THEM
 * ---------------------------------------------------------------------------
 *  1. NO ADDRESS OF AN AUTOMATIC. Every buffer here is file scope, including
 *     the one-row scratch cw_row[] and the digit buffers, and both os88_pt /
 *     os88_size out-parameters. tools/cc8086.py refuses the bp-relative `lea`
 *     by name, so this is a build failure rather than a bug (SPEC.md 67.5).
 *  2. NO STRING INSTRUCTION FROM C - ES is the kernel's. Nothing here assigns
 *     a struct, passes one by value or returns one; the group stack in
 *     cwrtfio.c is parallel arrays for the same reason. The one `rep movsb`
 *     in the package is hand-written in the shim, where ES is loaded on
 *     purpose and restored.
 *  3. NO long, NO float. The only 32-bit quantity in sight is a file size,
 *     and it arrives as two words (SPEC.md 67.7).
 *  4. SMALL FRAMES. Nothing here has a local array; the largest frame in the
 *     build report is a handful of ints (SPEC.md 67.8).
 *
 * ---------------------------------------------------------------------------
 * BUILD
 * ---------------------------------------------------------------------------
 *     apps/cword/build.sh              host checks, then the five-step chain
 *     make test TESTAPPS=build/cword.img
 *
 * build.sh runs four checks before it compiles anything for the 8086, and each
 * of them stops the build:
 *
 *     apps/cword/cwrtfchk.c            the Opus tables check themselves
 *     apps/cword/cwrtfrt.c             the RTF reader and writer round-trip
 *     apps/cword/hosttest/cwuitest.c   THE REDRAW PATH, against a model of the
 *                                      glass - and it prints the cost table
 *                                      above on every build, which is where
 *                                      those numbers come from
 *     apps/cword/hosttest/cwmovetest   cw_memmove on a real x86 in QEMU, with
 *                                      SS != DS, which is the only condition
 *                                      under which its bugs would show
 *
 * ---------------------------------------------------------------------------
 * WHAT IT COSTS OF THE 60KB (APP_MAX_SIZE = 61,440 for image + bss together)
 * ---------------------------------------------------------------------------
 * Measured by tools/os88pkg.py on the shipping package:
 *
 *     image  20,086      the code, the string literals and the Opus tables
 *     bss    27,050      the document (4,000 + 4,000), the RTF buffer
 *                        (12,000), the glass shadow (2 x 24 x 128 = 6,144)
 *     -----------------
 *     total  47,136      77% of the ceiling, 14,304 bytes spare
 *
 * The two knobs are CW_DOC_MAX and CW_RTF_MAX in apps/cword/cwrtfio.c, and
 * both are bss: SPEC.md 67.9's rule is that bss is the cheap half - it costs
 * no floppy bytes - so a bigger document is the affordable change and more
 * code is not. The shadow's 128-byte stride wastes 52 bytes a row on purpose;
 * the note at CW_SH_STRIDE says why.
 * ==========================================================================*/

#include "os88.h"

#include "cwrtftbl.h"
#include "cwrtftbl.c"                   /* the Opus tables - include once */
#include "cwrtfio.c"                    /* the document and RTF - include once */

/* OS88_FDLG_SAVE USED TO BE MISSING FROM apps/cc/os88.h - a comment opened
 * after OS88_FDLG_OPEN's value and closed only at the end of the next line,
 * which ate the whole #define. cword, as the first C package with a file
 * dialog, was the one that found it. The header defines it now, so this is a
 * belt-and-braces guard rather than a workaround: it costs nothing, and it
 * keeps cword buildable against an older copy of the SDK header. */
#ifndef OS88_FDLG_SAVE
#define OS88_FDLG_SAVE 1
#endif

/* --- the grid ------------------------------------------------------------
 * Authored against 640x480 and CLAMPED ONTO THE LIVE SCREEN every layout: two
 * adapters of three are 640x200 and a constant here would be wrong on both
 * (SPEC.md 39). CW_COLS_MAX x CW_ROWS_MAX only bounds the shadow. */
#define CW_COLS_MAX   76                /* 76 x 8 = 608 px of text */
#define CW_ROWS_MAX   24
#define CW_SH_STRIDE 128                /* the shadow's row stride, and it is a
                                         * POWER OF TWO on purpose rather than
                                         * CW_COLS_MAX. `row * 76` is an 8086
                                         * `imul` with a register operand,
                                         * which needs a scratch register, and
                                         * tools/cc8086.py refuses the site
                                         * when it cannot prove one is free -
                                         * correctly, and it said so by name.
                                         * `row * 128` is a shift. The 52 bytes
                                         * a row wastes are bss, which is the
                                         * cheap half of the ceiling (SPEC.md
                                         * 67.9): 2.5KB of nothing, against an
                                         * indexing multiply on the redraw
                                         * path. */
#define CW_PITCH      10                /* an 8-px glyph band, 2 px of gap:
                                         * the italic overstrike lives in the
                                         * row above the band and the
                                         * underline in the row below it, so
                                         * the two attributes never collide */
#define CW_STATUS_H   14                /* the rule and the status line */

/* --- keys (int 16h scan codes, as apps/notepad names them) ---------------- */
#define CW_K_HOME   0x47
#define CW_K_UP     0x48
#define CW_K_LEFT   0x4B
#define CW_K_RIGHT  0x4D
#define CW_K_END    0x4F
#define CW_K_DOWN   0x50
#define CW_K_DEL    0x53

#define CW_C_BOLD   0x02                /* Ctrl-B. Ctrl-I is Tab and Ctrl-M is */
#define CW_C_OPEN   0x0F                /* Enter, so italic has no shortcut -  */
#define CW_C_SAVE   0x13                /* the menu is its only door           */
#define CW_C_ULINE  0x15

/* --- menus (SPEC.md 12.2): the item index the kernel hands os88_oncmd() --- */
#define CW_M_FILE   0
#define CW_M_EDIT   1
#define CW_M_FMT    2

#define CW_F_NEW    0
#define CW_F_OPEN   1
#define CW_F_SAVE   2
#define CW_F_SAVEAS 3

#define CW_E_COPY   0
#define CW_E_PASTE  1
#define CW_E_CLEAR  2

#define CW_T_BOLD   0
#define CW_T_ITAL   1
#define CW_T_ULINE  2
#define CW_T_PLAIN  3

/* ==========================================================================
 * STATE - all of it file scope, because everything addressable has to be
 * ========================================================================*/

static struct os88_size cw_sz;          /* the live content box */
static struct os88_pt   cw_org;
static struct os88_video cw_vid;

static int cw_cols;                     /* the live grid */
static int cw_rows;
static int cw_tx;                       /* text area origin, screen px */
static int cw_ty;
static int cw_sy;                       /* the status line's baseline */

static int cw_cur;                      /* the caret, 0..cw_len */
static int cw_top;                      /* offset of the first visible row */
static int cw_attr;                     /* what NEW characters get */
static int cw_par;                      /* paragraphs before the caret */
static int cw_mod;                      /* changed since the last save */
static int cw_arm;                      /* a destructive command is armed */

static int cw_about_up;                 /* the About panel owns the glass */

/* THE RECT THE ABOUT PANEL LAST PUT INK IN, and a flag saying it is still on
 * the glass. The panel is bigger than the text band on all four sides - it is
 * framed, and the frame lies outside cw_tx..cw_tx+cw_cols*8-1 horizontally and
 * outside the glyph rows vertically - so the "repaint everything" fill in
 * cw_show(), which is sized to the TEXT, does not reach it. Taking the panel
 * down therefore used to leave its whole frame drawn on the screen, plus, on a
 * screen with few enough rows that the panel outgrows the text band (CGA and
 * Hercules at 640x200, ~13 rows), the bottom lines of its text as well.
 *
 * Measured before the fix, VGA 640x480: after About was dismissed the column
 * x=36 was black for the full height of the content and the row y=366 was
 * black across it - neither of them present in the same screen before About
 * was ever opened. On CGA the words "Computer History Museum release, 2014."
 * stayed legible above the status rule.
 *
 * It is the DRAWN rect and not a recomputation, so the two cannot drift, and
 * it is four statics rather than an out-parameter because `&local` does not
 * build here (SPEC.md 67.5). The rect is a strict superset of the text band,
 * so it REPLACES that fill rather than adding a second one - still one call. */
static int cw_about_ink;
static int cw_ab_x1;
static int cw_ab_y1;
static int cw_ab_x2;
static int cw_ab_y2;

/* The wrap, computed by cw_wrap() into file scope for the same reason every
 * out-parameter here is a static: `&local` does not build (SPEC.md 67.5). */
static int cw_w_end;                    /* one past the last character SHOWN */
static int cw_w_next;                   /* start of the next row, -1 = none */

static int cw_ls[CW_ROWS_MAX];          /* each visible row's start, -1 = the
                                         * row is past the end of the text */
static int cw_le[CW_ROWS_MAX];          /* ...and its end */
static int cw_from;                     /* the first row a repaint must touch */
static int cw_stop;                     /* ...and the first it need not */

/* THE SHADOW: what is on the glass right now, cell for cell. This is the
 * whole damage model - see the header. */
static char          cw_sh[CW_ROWS_MAX * CW_SH_STRIDE];
static unsigned char cw_sha[CW_ROWS_MAX * CW_SH_STRIDE];
static int           cw_sh_start[CW_ROWS_MAX];
static int           cw_sh_end[CW_ROWS_MAX];
static int           cw_sh_ok;          /* 0 = the glass is unknown: repaint all */

static char          cw_row[CW_COLS_MAX + 1];    /* the row being laid out */
static unsigned char cw_rowa[CW_COLS_MAX];
static char          cw_rt[CW_COLS_MAX + 1];     /* one run of it, NUL ended */

static int cw_wiped;                    /* the kernel just whitened our content:
                                         * a full repaint need not do it again */
static int cw_car_on;                   /* the caret is on the glass */
static int cw_car_x;
static int cw_car_y;

static char cw_fname[16];               /* "" until the document has a name */
static char cw_title[32];
static char cw_num[8];
static char cw_tmp[40];                 /* string building, kept apart from
                                         * cw_row[] and cw_rt[] on purpose:
                                         * those two are the redraw path's and
                                         * sharing them would be a bug waiting
                                         * for the day the order changes */
static struct os88_place cw_place;      /* where a launched-on document lives */

/* The status line, one shadow per field: a field is repainted only when its
 * TEXT changes, which is why the caret column is not one of them - it would
 * change on every keystroke and cost 9 cells (8 ms) to say so. */
static char cw_st_name[18];
static char cw_st_par[12];
static char cw_st_att[8];

/* ==========================================================================
 * THE ONE PIECE OF ASSEMBLY (apps/cword/cword.asm, SPEC.md 67.11)
 *
 * A byte move that handles both directions. C cannot write one: the ascending
 * loop in the runtime corrupts an insertion (it would trail its own output),
 * and a descending loop written in C costs 3-5x what `rep movsb` costs on the
 * machine this has to run on. It is the only routine in the package that
 * loads ES, and it puts it back.
 * ========================================================================*/
void cw_memmove(void *dst, const void *src, unsigned n);

/* ==========================================================================
 * SMALL HELPERS
 * ========================================================================*/

static int cw_streq(const char *a, const char *b)
{
    int i;

    i = 0;
    while (a[i] == b[i]) {
        if (a[i] == 0)
            return 1;
        i++;
    }
    return 0;
}

/* cw_pad - copy src into dst, space filled to exactly n characters and NUL
 * terminated. The padding is how a shortening field erases the character it
 * used to have without a separate fill (SPEC.md 6.1: one decision per cell). */
static void cw_pad(char *dst, const char *src, int n)
{
    int i;

    i = 0;
    while (i < n && src[i] != 0) {
        dst[i] = src[i];
        i++;
    }
    while (i < n) {
        dst[i] = ' ';
        i++;
    }
    dst[n] = 0;
}

static void cw_toast(const char *s)
{
    os88_toast(s, 0);
}

/* The file error, said in words. SPEC.md 47's rule is that a refusal names
 * the fact it tested; "could not save" names nothing. */
static const char *cw_ferr_text(void)
{
    int e;

    e = os88_ferr();
    if (e == OS88_FERR_NODISK)
        return "No disk in the drive";
    if (e == OS88_FERR_IO)
        return "Disk error";
    if (e == OS88_FERR_NAME)
        return "That name is not a valid filename";
    if (e == OS88_FERR_NOENT)
        return "No such file";
    if (e == OS88_FERR_EXIST)
        return "A file of that name is already there";
    if (e == OS88_FERR_FULL)
        return "The disk is full";
    if (e == OS88_FERR_DIRFULL)
        return "The folder is full";
    if (e == OS88_FERR_PROT)
        return "That file is protected";
    if (e == OS88_FERR_WPROT)
        return "The disk is write protected";
    if (e == OS88_FERR_BIG)
        return "That file is too big";
    return "The disk refused it";
}

/* ==========================================================================
 * LAYOUT
 * ========================================================================*/

/* cw_layout - the live geometry. Everything below is measured from this and
 * nothing from the template numbers in os88_main(): the window is clamped
 * onto whatever screen it got (SPEC.md 39).
 * out: 0, or -1 if the window is not visible - draw nothing. */
static int cw_layout(void *win)
{
    int c;
    int r;

    if (os88_wm_geom(win, &cw_sz) != 0)
        return -1;
    os88_wm_content(win, &cw_org);

    cw_tx = cw_org.x + 8;               /* a multiple of 8, because the content
                                         * origin is snapped to one: that is
                                         * what puts every os88_font_run() on
                                         * the single-store path on the two
                                         * 1bpp adapters (SPEC.md 6.1, 11.94) */
    cw_ty = cw_org.y + 5;               /* +5: the italic overstrike draws one
                                         * row ABOVE the first glyph band */

    c = (cw_sz.w - 16) / 8;
    if (c > CW_COLS_MAX)
        c = CW_COLS_MAX;
    if (c < 8)
        c = 8;
    r = (cw_sz.h - 6 - CW_STATUS_H) / CW_PITCH;
    if (r > CW_ROWS_MAX)
        r = CW_ROWS_MAX;
    if (r < 1)
        r = 1;

    if (c != cw_cols || r != cw_rows) {
        cw_cols = c;
        cw_rows = r;
        cw_sh_ok = 0;                   /* a different grid: the shadow is not
                                         * about this screen any more */
    }
    cw_sy = cw_org.y + cw_sz.h - 10;
    return 0;
}

/* cw_wrap - lay out the row that starts at s.
 *
 * in:  s, 0..cw_len
 * out: cw_w_end  one past the last character DRAWN on the row
 *      cw_w_next start of the next row, or -1 if this row ends the document
 *
 * The break is Word's: at a paragraph end, at the last space that fits, or -
 * for a word longer than the line - hard at the margin. The space a line
 * breaks at is consumed, which is why cw_w_next is not always cw_w_end. */
static void cw_wrap(int s)
{
    int i;
    int lim;
    int c;

    lim = s + cw_cols;
    if (lim > cw_len)
        lim = cw_len;

    for (i = s; i < lim; i++) {
        if (cw_buf[i] == '\n') {
            cw_w_end = i;
            cw_w_next = i + 1;
            return;
        }
    }
    if (lim >= cw_len) {                /* the row reaches the end of the text */
        cw_w_end = cw_len;
        cw_w_next = -1;
        return;
    }

    c = cw_buf[lim];                    /* the first character that did NOT fit */
    if (c == '\n' || c == ' ') {
        cw_w_end = lim;
        cw_w_next = lim + 1;
        return;
    }
    for (i = lim - 1; i > s; i--) {
        if (cw_buf[i] == ' ') {
            cw_w_end = i;
            cw_w_next = i + 1;
            return;
        }
    }
    cw_w_end = lim;                     /* one word, longer than the line */
    cw_w_next = lim;
}

/* cw_para_start / cw_prev_line - going BACKWARDS through a wrap, which is the
 * one direction word wrap does not answer directly. A paragraph is short, so
 * re-wrapping it from its start is cheaper than any structure that would
 * remember the answer. */
static int cw_para_start(int p)
{
    while (p > 0 && cw_buf[p - 1] != '\n')
        p--;
    return p;
}

static int cw_prev_line(int s)
{
    int p;
    int n;

    if (s <= 0)
        return 0;
    p = cw_para_start(s - 1);
    for (;;) {
        cw_wrap(p);
        n = cw_w_next;
        if (n < 0 || n >= s)
            return p;
        p = n;
    }
}

/* cw_relayout - the visible rows, from row `from` down.
 *
 * in:  from      the first row that can have changed; cw_ls[from] is correct
 *      lo, hi    the range of the document that changed, or lo < 0 for "all"
 *      delta     how far the text after it moved (+1 for an insertion)
 * out: cw_ls[]/cw_le[] current, and cw_stop = the first row that needs no
 *      repaint because everything from there down is unchanged
 *
 * THE EARLY STOP IS THE OPTIMISATION THE WHOLE REDRAW PATH RESTS ON, so here
 * is the argument in full. Wrapping is deterministic and depends only on the
 * text from the row's start onwards. If a row's new start is exactly its old
 * start plus delta, then the text from there on is byte for byte what it was,
 * so every wrap decision below it is the one it was, so every row below it
 * shows what it already shows. Nothing below needs laying out, comparing or
 * drawing - only its recorded offsets need moving by delta. */
static void cw_relayout(int from, int lo, int hi, int delta)
{
    int r;
    int k;
    int s;

    cw_stop = cw_rows;
    s = cw_ls[from];

    for (r = from; r < cw_rows; r++) {
        if (r > from && cw_sh_ok && lo >= 0 && s >= 0 && s >= hi &&
            cw_sh_start[r] >= 0 && s == cw_sh_start[r] + delta) {
            for (k = r; k < cw_rows; k++) {
                if (cw_sh_start[k] >= 0) {
                    cw_sh_start[k] = cw_sh_start[k] + delta;
                    cw_sh_end[k]   = cw_sh_end[k] + delta;
                }
                cw_ls[k] = cw_sh_start[k];
                cw_le[k] = cw_sh_end[k];
            }
            cw_stop = r;
            return;
        }

        cw_ls[r] = s;
        if (s < 0) {
            cw_le[r] = -1;
            continue;
        }
        cw_wrap(s);
        cw_le[r] = cw_w_end;
        s = cw_w_next;
    }
}

/* cw_row_of - the visible row a document offset is on, or -1 if it is off
 * the top or the bottom. The caret at the very end of a wrapped row belongs
 * to that row and not to the start of the next one, which is what the `<=`
 * against cw_le[] says. */
static int cw_row_of(int pos)
{
    int r;

    for (r = 0; r < cw_rows; r++) {
        if (cw_ls[r] < 0)
            return -1;
        if (cw_ls[r] > pos)
            return -1;
        if (pos <= cw_le[r])
            return r;
    }
    return -1;
}

/* The last row that starts at or before pos - "which row does a change at pos
 * begin to affect". */
static int cw_row_from(int pos)
{
    int r;
    int best;

    best = 0;
    for (r = 0; r < cw_rows; r++) {
        if (cw_ls[r] < 0 || cw_ls[r] > pos)
            break;
        best = r;
    }
    return best;
}

/* ==========================================================================
 * DRAWING
 *
 * Every routine below is called with the gfx lock ALREADY HELD - from
 * os88_paint(), a key, a click or a menu command (SPEC.md 11) - and takes it
 * nowhere.
 * ========================================================================*/

static void cw_caret_off(void)
{
    if (!cw_car_on)
        return;
    os88_gfx_xor_fill(cw_car_x, cw_car_y, cw_car_x, cw_car_y + 7);
    cw_car_on = 0;
}

static void cw_caret_on(void)
{
    int r;
    int col;

    if (cw_car_on)
        return;
    r = cw_row_of(cw_cur);
    if (r < 0)
        return;
    col = cw_cur - cw_ls[r];
    if (col < 0 || col > cw_cols)
        return;
    cw_car_x = cw_tx + col * 8;
    cw_car_y = cw_ty + r * CW_PITCH;
    os88_gfx_xor_fill(cw_car_x, cw_car_y, cw_car_x, cw_car_y + 7);
    cw_car_on = 1;
}

/* cw_draw_run - one run of equal-attribute cells of one row.
 *
 * ONE os88_font_run() lays the cells' background and their glyphs in a single
 * pass, so the line is never momentarily blank - the erase-then-letter pair
 * is the canonical double-draw and it is invisible in an emulator
 * (PERFORMANCE.md, SPEC.md 6.1).
 *
 * WEIGHT AND SLANT ARE SYNTHESISED, and honestly: the kernel has ONE 8x8
 * face, so bold is the same run overstruck one pixel to the right (thicker
 * stems) and italic is the same run overstruck one pixel up and to the right
 * (strokes leaning up-right). They are approximations of a typeface that does
 * not exist here, they are visibly different from each other and from roman
 * on all three adapters, and each costs ONE extra call - not one per glyph.
 * Underline is one rule under the run, in the gap CW_PITCH leaves.
 *
 * in:  c0, c1 - the run's columns, inclusive; y - the row's glyph band top */
static void cw_draw_run(int c0, int c1, int y, int a)
{
    int i;
    int n;
    int x;
    int w;

    n = 0;
    for (i = c0; i <= c1; i++) {
        cw_rt[n] = cw_row[i];
        n++;
    }
    cw_rt[n] = 0;

    x = cw_tx + c0 * 8;
    w = n * 8;

    os88_font_run(x, y, cw_rt, OS88_BLACK, OS88_WHITE);

    if (a & (CW_A_BOLD | CW_A_ITAL | CW_A_ULINE))
        os88_set_color(OS88_BLACK);
    if (a & CW_A_BOLD)
        os88_font_str(x + 1, y, cw_rt);
    if (a & CW_A_ITAL)
        os88_font_str(x + 1, y - 1, cw_rt);
    if (a & CW_A_ULINE)
        os88_gfx_hline(x, x + w - 1, y + 8);
}

/* cw_render_row - lay out row r, compare it against the glass, and draw the
 * columns that differ.
 *
 * The comparison is the damage rect (SPEC.md 11.90 in miniature): typing at
 * the end of a line differs in one column and draws one cell. */
static void cw_render_row(int r)
{
    int base;
    int s;
    int e;
    int i;
    int n;
    int c0;
    int c1;
    int a;
    int j;
    int y;
    int u0;
    int u1;

    base = r * CW_SH_STRIDE;
    y = cw_ty + r * CW_PITCH;

    /* the row's text and formatting, padded out to the full width so that a
     * shortened row erases what it used to show */
    s = cw_ls[r];
    e = cw_le[r];
    n = 0;
    if (s >= 0) {
        for (i = s; i < e; i++) {
            if (n >= cw_cols)
                break;
            cw_row[n] = cw_buf[i];
            cw_rowa[n] = cw_att[i];
            n++;
        }
    }
    while (n < cw_cols) {
        cw_row[n] = ' ';
        cw_rowa[n] = 0;
        n++;
    }

    /* what actually changed */
    if (cw_sh_ok) {
        c0 = -1;
        c1 = -1;
        for (i = 0; i < cw_cols; i++) {
            if (cw_sh[base + i] != cw_row[i] || cw_sha[base + i] != cw_rowa[i]) {
                if (c0 < 0)
                    c0 = i;
                c1 = i;
            }
        }
        if (c0 < 0) {
            cw_sh_start[r] = s;         /* nothing to draw, but the offsets
                                         * moved and the next comparison needs
                                         * them */
            cw_sh_end[r] = e;
            return;
        }
        /* ONE COLUMN OF BLEED AT THE RIGHT END OF THE SPAN. Bold and italic
         * overstrike a pixel to the right, so a cell whose glyph has ink in
         * its RIGHTMOST column puts a pixel into the cell after it. If the
         * formatting there has just been switched off and that next cell is
         * outside the damaged span, nothing repaints it and the pixel stays.
         * One extra cell removes it.
         *
         * THE SCALE OF THIS, HONESTLY: the kernel's face is the IBM ROM 8x8
         * set, in which almost every printable glyph leaves column 7 blank -
         * it is the spacing column. Underscore is the exception. So this is
         * insurance against a single pixel behind a bold underscore, it costs
         * one cell, and the LEFT end of a span has the mirror image of it
         * which is deliberately NOT fixed: repainting cell c0 erases the bleed
         * the cell before it put there, and widening the span leftwards only
         * moves that edge one cell along - the only real fix is to repaint
         * from the start of the run, which turns a one-cell keystroke into a
         * whole-run one on a bold line. Measured in the host harness, and one
         * pixel is not worth 60 cells (54 ms on the target). */
        if (c1 + 1 < cw_cols && (cw_sha[base + c1] & (CW_A_BOLD | CW_A_ITAL)))
            c1++;
    } else {
        c0 = 0;
        c1 = cw_cols - 1;
    }

    /* THE INK OUTSIDE THE GLYPH BAND. os88_font_run() repaints rows y..y+7,
     * so the two attributes that draw outside it - italic one row above,
     * underline one row below - would leave a stale line behind when they are
     * switched off. The shadow says exactly where the old ones were, so this
     * whitens that span and only that span, rather than clearing the strips
     * on every repaint (which would flash a line that is not changing). */
    if (cw_sh_ok) {
        u0 = -1;
        u1 = -1;
        for (i = c0; i <= c1; i++) {
            if (cw_sha[base + i] & CW_A_ULINE) {
                if (u0 < 0)
                    u0 = i;
                u1 = i;
            }
        }
        if (u0 >= 0) {
            os88_set_color(OS88_WHITE);
            os88_gfx_hline(cw_tx + u0 * 8, cw_tx + u1 * 8 + 7, y + 8);
        }
        u0 = -1;
        u1 = -1;
        for (i = c0; i <= c1; i++) {
            if (cw_sha[base + i] & CW_A_ITAL) {
                if (u0 < 0)
                    u0 = i;
                u1 = i;
            }
        }
        if (u0 >= 0) {
            os88_set_color(OS88_WHITE);
            os88_gfx_hline(cw_tx + u0 * 8, cw_tx + u1 * 8 + 8, y - 1);
        }
    }

    /* draw the damaged span, one call per run of equal formatting */
    i = c0;
    while (i <= c1) {
        a = cw_rowa[i];
        j = i;
        while (j <= c1 && cw_rowa[j] == a)
            j++;
        cw_draw_run(i, j - 1, y, a);
        i = j;
    }

    /* the glass now says this */
    for (i = 0; i < cw_cols; i++) {
        cw_sh[base + i] = cw_row[i];
        cw_sha[base + i] = cw_rowa[i];
    }
    cw_sh_start[r] = s;
    cw_sh_end[r] = e;
}

/* cw_status - the three fields, each repainted only when its own text
 * changes. The caret's COLUMN is deliberately not among them: it changes on
 * every keystroke and would cost 9 cells (8 ms on the target) to say so,
 * which is three times the cost of the character that was typed. */
static void cw_status(void)
{
    int x;
    int c;

    if (cw_cols < 24)
        return;                         /* no room: the text is what matters */

    /* the document's name, and whether it has unsaved changes */
    cw_pad(cw_rt, cw_fname[0] ? cw_fname : "Untitled", 13);
    if (cw_mod)
        cw_rt[12] = '*';
    if (!cw_streq(cw_rt, cw_st_name)) {
        os88_font_run(cw_tx, cw_sy, cw_rt, OS88_BLACK, OS88_WHITE);
        os88_strcpy(cw_st_name, cw_rt, sizeof(cw_st_name));
    }

    /* which paragraph the caret is in. Not the LINE, because a display line
     * is a wrap and counting them means wrapping the document from its start
     * - 4,000 characters of C on every keystroke, to update a number that
     * usually did not change. A paragraph count is maintained incrementally
     * by cw_moved(), in time proportional to how far the caret went. */
    os88_strcpy(cw_tmp, "Par ", sizeof(cw_tmp));
    os88_utoa((unsigned)(cw_par + 1), cw_num);
    os88_strcpy(cw_tmp + 4, cw_num, sizeof(cw_tmp) - 4);
    cw_pad(cw_rt, cw_tmp, 8);
    if (!cw_streq(cw_rt, cw_st_par)) {
        os88_font_run(cw_tx + 14 * 8, cw_sy, cw_rt, OS88_BLACK, OS88_WHITE);
        os88_strcpy(cw_st_par, cw_rt, sizeof(cw_st_par));
    }

    /* what the next character typed will look like */
    if (cw_cols < 32)
        return;
    cw_rt[0] = (cw_attr & CW_A_BOLD) ? 'B' : '-';
    cw_rt[1] = ' ';
    cw_rt[2] = (cw_attr & CW_A_ITAL) ? 'I' : '-';
    cw_rt[3] = ' ';
    cw_rt[4] = (cw_attr & CW_A_ULINE) ? 'U' : '-';
    cw_rt[5] = 0;
    if (!cw_streq(cw_rt, cw_st_att)) {
        c = cw_cols - 5;
        x = cw_tx + c * 8;
        os88_font_run(x, cw_sy, cw_rt, OS88_BLACK, OS88_WHITE);
        os88_strcpy(cw_st_att, cw_rt, sizeof(cw_st_att));
    }
}

/* cw_frame - the parts of the window that never change: one rule over the
 * status line. Drawn by the full repaint only. */
static void cw_frame(void)
{
    os88_set_color(OS88_BLACK);
    os88_gfx_hline(cw_org.x + 4, cw_org.x + cw_sz.w - 5, cw_sy - 4);
}

/* --- the About panel ------------------------------------------------------
 * A panel inside our own content rather than a second window: a window costs
 * a slot in the table and a whole second paint path, and this has nothing to
 * say that needs one. It is also where the attribution belongs, because it is
 * the part of the program a user can see. */
static const char *cw_about_lines[] = {
    "CWord - a word processor in C",
    "",
    "The first application of the os8088 C",
    "toolchain (SPEC.md 67). RTF in and out.",
    "",
    "The RTF tables are derived from the",
    "Microsoft Word for Windows 1.1a source,",
    "Copyright (C) Microsoft Corporation,",
    "Computer History Museum release, 2014.",
    "",
    "Click or press a key."
};
#define CW_ABOUT_LINES 11

static void cw_draw_about(void)
{
    int i;
    int y;
    int x1;
    int x2;
    int y2;

    x1 = cw_org.x + 4;
    x2 = cw_org.x + cw_sz.w - 5;

    /* -3 AND NOT -5, WHICH IS NOT THE SYMMETRY IT LOOKS LIKE. The other three
     * edges sit 4px inside the content, and this one used to as well; but the
     * panel covers the status line, and the status line's glyph band is
     * cw_sy..cw_sy+7 = (cw_sz.h - 10)..(cw_sz.h - 3). A bottom at -5 erases
     * six of those eight rows and leaves the seventh - the baseline row, the
     * one with the most ink in it - showing as a dashed line of ghost text
     * under the panel's own border. Measured on VGA 640x480: the panel framed
     * at y=366 with the word "TEST1.RTF  * Par 45" still legible at y=367.
     *
     * -3 is the last row the status glyphs can reach on ANY geometry, because
     * cw_sy is defined from cw_sz.h, so this covers it exactly rather than by
     * how much it happens to need here. It stays two rows inside the content
     * (the last content row is cw_sz.h - 1), so it cannot touch the window
     * frame the WM owns. */
    y2 = cw_org.y + cw_sz.h - 3;

    os88_set_color(OS88_WHITE);
    os88_gfx_fill(x1, cw_org.y + 4, x2, y2);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(x1, cw_org.y + 4, x2, y2);

    cw_ab_x1 = x1;                      /* what the next full repaint has to */
    cw_ab_y1 = cw_org.y + 4;            /* erase - see cw_about_ink          */
    cw_ab_x2 = x2;
    cw_ab_y2 = y2;
    cw_about_ink = 1;

    y = cw_org.y + 10;
    for (i = 0; i < CW_ABOUT_LINES; i++) {
        if (y + 8 > y2 - 2)
            break;
        if (cw_about_lines[i][0] != 0) {
            cw_pad(cw_rt, cw_about_lines[i], cw_cols - 2);
            os88_font_run(cw_tx, y, cw_rt, OS88_BLACK, OS88_WHITE);
        }
        y = y + CW_PITCH;
    }

    /* the glass no longer shows the document */
    cw_sh_ok = 0;
    cw_car_on = 0;
    cw_st_name[0] = 0;
    cw_st_par[0] = 0;
    cw_st_att[0] = 0;
}

/* ==========================================================================
 * THE REPAINT
 * ========================================================================*/

/* cw_glass_scroll - move the text band by one row instead of redrawing it
 * (SPEC.md 5.5). 4 calls and one row of glyphs, against 24 calls and 1,824
 * glyphs for a redraw of the view: 57 ms against 1.7 s.
 * in:  n = +1 the view goes down (content moves up), -1 the other way
 * out: 0 moved and the shadow moved with it, -1 refused - repaint instead */
static int cw_glass_scroll(int n)
{
    int y1;
    int y2;
    int x2;
    int i;
    int src;
    int dst;
    int cnt;

    if (!cw_sh_ok)
        return -1;

    y1 = cw_ty - 1;
    y2 = cw_ty + (cw_rows - 1) * CW_PITCH + 8;
    x2 = cw_tx + cw_cols * 8 - 1;

    if (os88_gfx_scroll(cw_tx, y1, x2, y2, n * CW_PITCH) != 0)
        return -1;

    cnt = (cw_rows - 1) * CW_SH_STRIDE;
    if (n > 0) {
        cw_memmove(cw_sh, cw_sh + CW_SH_STRIDE, (unsigned)cnt);
        cw_memmove(cw_sha, cw_sha + CW_SH_STRIDE, (unsigned)cnt);
        for (i = 0; i + 1 < cw_rows; i++) {
            cw_sh_start[i] = cw_sh_start[i + 1];
            cw_sh_end[i] = cw_sh_end[i + 1];
        }
        dst = cw_rows - 1;
    } else {
        cw_memmove(cw_sh + CW_SH_STRIDE, cw_sh, (unsigned)cnt);
        cw_memmove(cw_sha + CW_SH_STRIDE, cw_sha, (unsigned)cnt);
        for (i = cw_rows - 1; i > 0; i--) {
            cw_sh_start[i] = cw_sh_start[i - 1];
            cw_sh_end[i] = cw_sh_end[i - 1];
        }
        dst = 0;
    }

    /* THE ROW THAT CAME INTO VIEW holds whatever was under it - gfx_scroll
     * invents no pixels and says the vacated rows are unspecified (SPEC.md
     * 5.5). Marking its shadow with a character no text can equal AND with
     * every attribute bit set does the whole job in one stroke: the character
     * makes cw_render_row() repaint every cell of the row, and the attributes
     * make its strip erase whiten the italic row above the band and the
     * underline row below it across the full width - which are exactly the
     * two pixel rows of the vacated band that no os88_font_run() covers.
     *
     * Two explicit white hlines here would work too, and were what this did
     * first; they are the same pixels written twice, and the host harness
     * says so - removing either mechanism on its own still passes, removing
     * both fails. Two calls, 1.5 ms on the target, for nothing. */
    src = dst * CW_SH_STRIDE;
    for (i = 0; i < cw_cols; i++) {
        cw_sh[src + i] = (char)0xFF;
        cw_sha[src + i] = 0xFF;
    }
    cw_sh_start[dst] = -2;          /* an offset no row can have */
    cw_sh_end[dst] = -2;
    return 0;
}

/* cw_follow_caret - scroll until the caret is on screen. One row of movement
 * moves the pixels; more than one is rare enough (Open, a jump) to repaint. */
static void cw_follow_caret(void)
{
    int n;
    int guard;
    int s;

    n = 0;
    guard = 0;
    while (cw_row_of(cw_cur) < 0 && guard < 400) {
        guard++;
        if (cw_cur < cw_ls[0]) {
            s = cw_prev_line(cw_top);
            if (s == cw_top)
                break;
            cw_top = s;
            n--;
        } else {
            cw_wrap(cw_top);
            if (cw_w_next < 0)
                break;
            cw_top = cw_w_next;
            n++;
        }
        cw_ls[0] = cw_top;
        cw_relayout(0, -1, 0, 0);
    }
    if (n == 0)
        return;

    cw_from = 0;
    cw_stop = cw_rows;
    if (n == 1 || n == -1) {
        if (cw_glass_scroll(n) == 0)
            return;
    }
    cw_sh_ok = 0;
}

/* cw_show - the single repaint entry point.
 *
 * in:  lo, hi - the document range that changed, and delta how far the text
 *      after it moved. lo = -1 means "everything", lo = -2 means "nothing
 *      changed, only the caret moved".
 *
 * The gfx lock is held by the caller in every case (SPEC.md 11). */
static void cw_show(void *win, int lo, int hi, int delta)
{
    int r;

    if (cw_layout(win) != 0) {          /* not visible: nothing to draw, and */
        cw_sh_ok = 0;                   /* the glass will be someone else's  */
        cw_car_on = 0;
        return;
    }
    if (cw_about_up)
        return;                         /* the panel owns the content */
    if (os88_wm_obscured(win)) {
        /* Something is over us. Drawing now would put ink on another window's
         * pixels, and the kernel will call os88_paint() when we are uncovered
         * - so record that the glass is not ours and stop. */
        cw_sh_ok = 0;
        cw_car_on = 0;
        return;
    }

    cw_caret_off();

    if (lo == -1)
        cw_sh_ok = 0;
    if (!cw_sh_ok) {
        cw_from = 0;
        lo = -1;
        cw_ls[0] = cw_top;
        cw_relayout(0, -1, 0, 0);
    } else {
        cw_from = 0;
        if (lo >= 0)
            cw_from = cw_row_from(lo);
        if (lo == -2) {
            cw_stop = 0;                /* nothing on the page changed */
            cw_from = 0;
        } else {
            cw_relayout(cw_from, lo, hi, delta);
        }
    }

    cw_follow_caret();

    /* A REPAINT OF EVERYTHING, and there are two kinds of those. After
     * W_PAINT the kernel has already whitened the content and cw_wiped says
     * so. After New, Clear All, an Open, the About panel coming down, or a
     * jump too far to scroll, the old ink is still there - INCLUDING the two
     * 1-px attribute strips that no os88_font_run() covers, because they lie
     * outside the glyph band. One white fill takes the lot.
     *
     * THIS TEST IS AFTER cw_follow_caret() AND NOT BEFORE IT, and that is not
     * a tidying: a caret that jumped more than one row gives up on scrolling
     * and invalidates the shadow THERE, inside that call. Tested before it,
     * the fill never happened on that path, and a paste that moved the caret
     * a screenful left every underline and italic strip from the old text
     * behind on the new. Found by apps/cword/hosttest/cwuitest.c, which is
     * the sort of thing that harness is for - it is one pixel row and it
     * would have looked like dirt on the screen.
     *
     * The fill does mean the glyph rows are painted twice on this path. The
     * alternative is two hlines per row, which is 2 x 24 calls to save one,
     * and this path already costs the whole view. */
    if (!cw_sh_ok) {
        if (!cw_wiped) {
            os88_set_color(OS88_WHITE);
            if (cw_about_ink)
                os88_gfx_fill(cw_ab_x1, cw_ab_y1, cw_ab_x2, cw_ab_y2);
            else
                os88_gfx_fill(cw_tx, cw_ty - 1, cw_tx + cw_cols * 8 - 1,
                              cw_ty + (cw_rows - 1) * CW_PITCH + 8);
        }
        /* Cleared on the wiped path too: there the KERNEL whitened the
         * content, so the panel is already gone and a bigger fill would be a
         * second erase of pixels nobody has written since. */
        cw_about_ink = 0;
        cw_frame();
    }
    cw_wiped = 0;

    for (r = cw_from; r < cw_stop; r++)
        cw_render_row(r);
    cw_sh_ok = 1;

    cw_status();
    cw_caret_on();
}

/* ==========================================================================
 * EDITING
 * ========================================================================*/

static void cw_touch(void)
{
    cw_mod = 1;                         /* the status field and the greying of
                                         * Save both read this; both notice on
                                         * their own, by comparison */
}

/* cw_insert - one character at the caret, in the current formatting.
 * The tail of both arrays moves by one with the hand-written mover; at the
 * end of the document it moves nothing, which is where typing happens. */
static void cw_insert(int c)
{
    unsigned n;

    if (cw_len >= CW_DOC_MAX)
        return;
    n = (unsigned)(cw_len - cw_cur);
    if (n != 0) {
        cw_memmove(cw_buf + cw_cur + 1, cw_buf + cw_cur, n);
        cw_memmove(cw_att + cw_cur + 1, cw_att + cw_cur, n);
    }
    cw_buf[cw_cur] = (char)c;
    cw_att[cw_cur] = (unsigned char)cw_attr;
    cw_len++;
}

static void cw_remove(int at)
{
    unsigned n;

    n = (unsigned)(cw_len - at - 1);
    if (n != 0) {
        cw_memmove(cw_buf + at, cw_buf + at + 1, n);
        cw_memmove(cw_att + at, cw_att + at + 1, n);
    }
    cw_len--;
}

/* cw_moved - keep the paragraph counter right across a caret move, in time
 * proportional to the DISTANCE moved rather than to the document. Counting
 * from the start on every arrow key would be 4,000 iterations - about 20 ms
 * on the target - to update a number that usually did not change. */
static void cw_moved(int was)
{
    int i;

    if (cw_cur > was) {
        for (i = was; i < cw_cur; i++) {
            if (cw_buf[i] == '\n')
                cw_par++;
        }
    } else {
        for (i = cw_cur; i < was; i++) {
            if (cw_buf[i] == '\n')
                cw_par--;
        }
    }
}

static void cw_type(void *win, int c)
{
    if (cw_len >= CW_DOC_MAX) {
        cw_toast("Document full - 4000 characters is the limit");
        return;
    }
    cw_insert(c);
    cw_cur++;
    if (c == '\n')
        cw_par++;
    cw_touch();
    cw_show(win, cw_cur - 1, cw_cur, 1);
}

static void cw_backspace(void *win)
{
    if (cw_cur <= 0)
        return;
    cw_cur--;
    if (cw_buf[cw_cur] == '\n')
        cw_par--;
    cw_remove(cw_cur);
    cw_touch();
    cw_show(win, cw_cur, cw_cur, -1);
}

static void cw_del(void *win)
{
    if (cw_cur >= cw_len)
        return;
    cw_remove(cw_cur);
    cw_touch();
    cw_show(win, cw_cur, cw_cur, -1);
}

/* --- caret movement ------------------------------------------------------- */

static void cw_up(void)
{
    int r;
    int col;
    int s;
    int was;

    r = cw_row_of(cw_cur);
    if (r < 0)
        return;
    col = cw_cur - cw_ls[r];
    if (r > 0) {
        s = cw_ls[r - 1];
    } else {
        s = cw_prev_line(cw_ls[0]);
        if (s == cw_ls[0])
            return;
    }
    cw_wrap(s);
    was = cw_cur;
    cw_cur = s + col;
    if (cw_cur > cw_w_end)
        cw_cur = cw_w_end;
    cw_moved(was);
}

static void cw_down(void)
{
    int r;
    int col;
    int s;
    int was;

    r = cw_row_of(cw_cur);
    if (r < 0)
        return;
    col = cw_cur - cw_ls[r];
    if (r + 1 < cw_rows && cw_ls[r + 1] >= 0) {
        s = cw_ls[r + 1];
    } else {
        cw_wrap(cw_ls[r]);
        if (cw_w_next < 0)
            return;
        s = cw_w_next;
    }
    cw_wrap(s);
    was = cw_cur;
    cw_cur = s + col;
    if (cw_cur > cw_w_end)
        cw_cur = cw_w_end;
    cw_moved(was);
}

/* ==========================================================================
 * THE DOCUMENT AS A FILE
 * ========================================================================*/

static void cw_retitle(void *win)
{
    os88_strcpy(cw_tmp, "CWord - ", sizeof(cw_tmp));
    os88_strcpy(cw_tmp + 8, cw_fname[0] ? cw_fname : "Untitled",
                sizeof(cw_tmp) - 8);
    if (cw_streq(cw_tmp, cw_title))
        return;
    os88_strcpy(cw_title, cw_tmp, sizeof(cw_title));
    os88_wm_title(win, cw_title);       /* 17 rows of caption, and only when
                                         * the name actually changed */
}

static void cw_newdoc(void *win)
{
    cw_doc_clear();
    cw_cur = 0;
    cw_top = 0;
    cw_par = 0;
    cw_attr = 0;
    cw_mod = 0;
    cw_fname[0] = 0;
    cw_retitle(win);
}

static void cw_save_as(void *win, const char *name)
{
    int n;

    n = cw_rtf_build();
    if (n < 0) {
        /* The document is longer than CW_RTF_MAX bytes of RTF. Refusing is
         * the whole point: half an RTF file is not a file (SPEC.md 47). */
        cw_toast("Too much formatting to save - shorten the document");
        return;
    }
    if (os88_file_write(name, cw_rtf, (unsigned)n) != 0) {
        cw_toast(cw_ferr_text());
        return;
    }
    os88_strcpy(cw_fname, name, sizeof(cw_fname));
    cw_mod = 0;
    cw_retitle(win);
    cw_toast("Saved");
}

/* out: 1 if the document changed and the whole view must be repainted, 0 if
 * the open was refused - and a refusal must NOT repaint, because a repaint of
 * unchanged text is the 1.7 seconds this whole file exists to avoid. */
static int cw_open(void *win, const char *name, unsigned size_lo,
                   unsigned size_hi)
{
    unsigned n;
    int rc;

    /* REFUSE BEFORE TOUCHING THE DISK. The dialog already knows the size; a
     * floppy does not, and finding out costs about ten seconds of motor that
     * the user cannot tell from a load that works (SPEC.md 38.6). */
    if (size_hi != 0 || size_lo > (unsigned)CW_RTF_MAX) {
        cw_toast("That file is too big for CWord (12000 bytes)");
        return 0;
    }

    n = os88_file_read(name, cw_rtf, (unsigned)CW_RTF_MAX);
    if (n == 0) {
        cw_toast(os88_ferr() ? cw_ferr_text() : "That file is empty");
        return 0;
    }

    rc = cw_rtf_parse((int)n);
    if (rc == CW_RTF_NOTRTF) {
        cw_toast("Not an RTF file - the document was not changed");
        return 0;
    }

    cw_cur = 0;
    cw_top = 0;
    cw_par = 0;
    cw_attr = 0;
    cw_mod = 0;
    os88_strcpy(cw_fname, name, sizeof(cw_fname));
    cw_retitle(win);

    if (rc == CW_RTF_TRUNC)
        cw_toast("Opened, but the text was cut at 4000 characters");
    else if (rc == CW_RTF_DEEP)
        cw_toast("Opened, but the RTF nested too deeply to finish");
    return 1;
}

/* cw_confirm - the stand-in for an alert box, which this API does not have.
 *
 * A command that would throw away unsaved work ARMS on the first press, says
 * so where a message costs no pixels (SPEC.md 59), and goes through on the
 * second. It is the honest shape of "are you sure" in a system with no modal
 * dialog: the fact it tests is cw_mod, and the user is told which fact.
 *
 * in:  armed - whether the arming press was the LAST thing that happened.
 *      The keyboard has to pass this in rather than read cw_arm, because a
 *      key handler clears the arming on any other keystroke and would
 *      otherwise clear its own.
 * out: 1 = go ahead, 0 = armed, tell the user and do nothing */
static int cw_confirm(int armed, const char *what)
{
    if (!cw_mod || armed) {
        cw_arm = 0;
        return 1;
    }
    cw_arm = 1;
    os88_strcpy(cw_tmp, "Unsaved changes - choose ", sizeof(cw_tmp));
    os88_strcpy(cw_tmp + 25, what, sizeof(cw_tmp) - 25);
    cw_toast(cw_tmp);
    return 0;
}

/* The two file commands, shared by the menu and the keyboard so that Ctrl-O
 * cannot quietly skip a guard the menu applies. */
static void cw_ask_open(void *win, int armed)
{
    if (!cw_confirm(armed, "Open again to discard"))
        return;
    os88_file_dlg(OS88_FDLG_OPEN, win, 0);
}

static void cw_ask_save(void *win, int as)
{
    if (!as && cw_fname[0]) {
        cw_save_as(win, cw_fname);      /* it has a name: just save it */
        return;
    }
    os88_file_dlg(OS88_FDLG_SAVE, win,
                  cw_fname[0] ? cw_fname : "UNTITLED.RTF");
}

/* ==========================================================================
 * MENUS
 *
 * An item whose string begins with OS88_MENU_DIS is drawn disabled
 * (SPEC.md 47), and every one of these greys a FACT rather than a guess:
 * Save is grey when there is nothing unsaved, Copy and Clear when the
 * document is empty, Paste when the clipboard is empty. The pointers are
 * re-pointed after anything that could change one of those facts; the array
 * of pointers is writable, and the strings are not.
 * ========================================================================*/

static const char *cw_mi_file[4] = { "New", "Open...", "Save", "Save As..." };
static const char *cw_mi_edit[3] = { "Copy All", "Paste", "Clear All" };
static const char *cw_mi_fmt[4]  = { "Bold", "Italic", "Underline", "Plain" };

/* NOT const: os88_menu_set() writes the oncmd field, because a C program
 * cannot name the assembly trampoline the kernel has to call (SPEC.md 12.2). */
static struct os88_menuset cw_menus = {
    "CWord", 0, 3,
    {
        { "File",   cw_mi_file, 4 },
        { "Edit",   cw_mi_edit, 3 },
        { "Format", cw_mi_fmt,  4 }
    }
};

static void cw_menu_state(void)
{
    cw_mi_file[CW_F_SAVE]  = cw_mod ? "Save" : "\1Save";
    cw_mi_edit[CW_E_COPY]  = cw_len ? "Copy All" : "\1Copy All";
    cw_mi_edit[CW_E_CLEAR] = cw_len ? "Clear All" : "\1Clear All";
    cw_mi_edit[CW_E_PASTE] = (os88_clip_size() > 0) ? "Paste" : "\1Paste";
}

/* --- the clipboard (SPEC.md 55) ------------------------------------------
 * Text only, both ways, and that is a narrowing worth stating: the clipboard
 * is one buffer of TEXT for the whole machine, so a Copy loses the bold and a
 * Paste arrives in whatever the caret's current formatting is. cw_rtf is
 * borrowed as the paste scratch - it is 12,000 bytes of bss that is otherwise
 * idle between an Open and a Save, and a second buffer would be 12,000 bytes
 * of the 60KB ceiling spent on having two names for one thing. */
/* cw_paste_ch - the one filter, used twice: once to count what will fit and
 * once to copy it. The document model is printable ASCII and '\n' (the head
 * of apps/cword/cwrtfio.c), so a tab becomes a space, a CR is dropped because
 * the LF beside it is the paragraph, and anything else is dropped whole.
 * out: the character to insert, or 0 for "not this one" */
static int cw_paste_ch(int c)
{
    if (c == 9)
        return ' ';
    if (c == 10)
        return '\n';
    if (c < 32 || c > 126)
        return 0;
    return c;
}

static void cw_paste(void *win)
{
    int n;
    int i;
    int c;
    int lo;
    int fit;
    int over;
    int room;
    unsigned tail;

    n = os88_clip_size();
    if (n <= 0)
        return;
    if (n > CW_RTF_MAX)
        n = CW_RTF_MAX;
    n = os88_clip_get(cw_rtf, (unsigned)n);
    if (n <= 0)
        return;

    /* HOW MANY WILL FIT, before anything moves. The alternative - inserting
     * one character at a time - moves the tail of both arrays once PER
     * CHARACTER: pasting 4,000 characters into the middle of a full document
     * would move 16 million bytes, which is four minutes on the target
     * machine (PERFORMANCE.md prices `rep movsb` at ~17 cycles a byte on an
     * 8088). Counting first and opening the hole once moves it twice. */
    room = CW_DOC_MAX - cw_len;
    fit = 0;
    over = 0;
    for (i = 0; i < n; i++) {
        if (cw_paste_ch((unsigned char)cw_rtf[i]) == 0)
            continue;               /* not text: dropped, and not a shortfall */
        if (fit >= room) {
            over = 1;
            break;
        }
        fit++;
    }
    if (fit <= 0) {
        cw_toast("Nothing to paste, or no room for it");
        return;
    }

    lo = cw_cur;
    tail = (unsigned)(cw_len - cw_cur);
    if (tail != 0) {
        cw_memmove(cw_buf + cw_cur + fit, cw_buf + cw_cur, tail);
        cw_memmove(cw_att + cw_cur + fit, cw_att + cw_cur, tail);
    }
    for (i = 0; i < n; i++) {
        c = cw_paste_ch((unsigned char)cw_rtf[i]);
        if (c == 0)
            continue;
        if (cw_cur >= lo + fit)
            break;
        cw_buf[cw_cur] = (char)c;
        cw_att[cw_cur] = (unsigned char)cw_attr;
        cw_cur++;
        if (c == '\n')
            cw_par++;
    }
    cw_len = cw_len + fit;

    if (over) {
        /* Some of it did not FIT - which is not the same as some of it not
         * being text, and only the first is worth saying. It is worth saying
         * because "it pasted" and "it pasted the first 900 characters" look
         * identical on the screen (SPEC.md 47). */
        cw_toast("Only part of the clipboard fitted - 4000 characters is the "
                 "limit");
    }
    cw_touch();
    cw_show(win, lo, cw_cur, fit);
}

/* ==========================================================================
 * THE CALLBACKS THE SHIM DECLARED
 * ========================================================================*/

/* W_PAINT (SPEC.md 11). The gfx lock is ALREADY HELD and the kernel has just
 * whitened our content, so the shadow describes nothing: everything is drawn.
 * This is the ONE path that costs the full 24 calls and 1,824 cells. */
void os88_paint(void *win)
{
    if (cw_layout(win) != 0)
        return;
    cw_sh_ok = 0;
    cw_wiped = 1;                       /* the kernel whitened the content on
                                         * its way here (we did not ask for
                                         * os88_wm_ownbg), so the full repaint
                                         * below must not whiten it again */
    cw_car_on = 0;
    cw_st_name[0] = 0;
    cw_st_par[0] = 0;
    cw_st_att[0] = 0;

    if (cw_about_up) {
        cw_draw_about();
        return;
    }
    cw_show(win, -1, 0, 0);
}

void os88_onkey(int ascii, int scan, void *win)
{
    int was;
    int row;
    int armed;

    if (cw_about_up) {                  /* any key takes the panel down */
        cw_about_up = 0;
        cw_show(win, -1, 0, 0);
        return;
    }
    armed = cw_arm;                     /* any keystroke disarms a pending
                                         * "press it again to discard" - the
                                         * one that armed it included, which
                                         * is why the flag is taken first and
                                         * passed on rather than read again */
    cw_arm = 0;

    if (ascii >= 32 && ascii <= 126) {
        cw_type(win, ascii);
        cw_menu_state();
        return;
    }
    if (ascii == 13) {
        cw_type(win, '\n');
        cw_menu_state();
        return;
    }
    if (ascii == 8) {
        cw_backspace(win);
        cw_menu_state();
        return;
    }
    if (ascii == CW_C_BOLD || ascii == CW_C_ULINE) {
        cw_attr = cw_attr ^ (ascii == CW_C_BOLD ? CW_A_BOLD : CW_A_ULINE);
        cw_show(win, -2, 0, 0);
        return;
    }
    if (ascii == CW_C_SAVE) {
        cw_ask_save(win, 0);
        return;
    }
    if (ascii == CW_C_OPEN) {
        cw_ask_open(win, armed);
        return;
    }

    was = cw_cur;
    if (scan == CW_K_LEFT) {
        if (cw_cur > 0)
            cw_cur--;
    } else if (scan == CW_K_RIGHT) {
        if (cw_cur < cw_len)
            cw_cur++;
    } else if (scan == CW_K_HOME) {
        row = cw_row_of(cw_cur);
        if (row >= 0)
            cw_cur = cw_ls[row];
    } else if (scan == CW_K_END) {
        row = cw_row_of(cw_cur);
        if (row >= 0)
            cw_cur = cw_le[row];
    } else if (scan == CW_K_UP) {
        cw_up();                        /* these two keep the paragraph count */
        was = cw_cur;                   /* themselves */
    } else if (scan == CW_K_DOWN) {
        cw_down();
        was = cw_cur;
    } else if (scan == CW_K_DEL) {
        cw_del(win);
        cw_menu_state();
        return;
    } else {
        return;                         /* a key this program has no use for */
    }

    cw_moved(was);
    cw_show(win, -2, 0, 0);
}

void os88_onclick(int x, int y, void *win)
{
    int r;
    int col;
    int was;

    if (cw_about_up) {
        cw_about_up = 0;
        cw_show(win, -1, 0, 0);
        return;
    }
    if (cw_layout(win) != 0)
        return;
    cw_arm = 0;

    r = 0;
    if (y > cw_ty)
        r = (y - cw_ty) / CW_PITCH;
    if (r >= cw_rows)
        r = cw_rows - 1;

    col = 0;
    if (x > cw_tx)
        col = (x - cw_tx) / 8;
    if (col > cw_cols)
        col = cw_cols;

    was = cw_cur;
    if (cw_ls[r] < 0) {
        cw_cur = cw_len;                /* below the text: the end of it */
    } else {
        cw_cur = cw_ls[r] + col;
        if (cw_cur > cw_le[r])
            cw_cur = cw_le[r];
    }
    cw_moved(was);
    cw_show(win, -2, 0, 0);
}

void os88_oncmd(int item, int menu, void *win)
{
    int armed;

    if (cw_about_up) {
        cw_about_up = 0;
        cw_show(win, -1, 0, 0);
    }

    /* Any command disarms a pending "choose it again to discard" - including
     * the one that armed it, which is why the flag is taken here and passed
     * on rather than read inside cw_confirm(). Without this, arming New and
     * then choosing Bold would leave New armed, and the next New would throw
     * the document away without asking. */
    armed = cw_arm;
    cw_arm = 0;

    if (menu == CW_M_FILE) {
        if (item == CW_F_NEW) {
            if (!cw_confirm(armed, "New again to discard them"))
                return;
            cw_newdoc(win);
            cw_show(win, -1, 0, 0);
        } else if (item == CW_F_OPEN) {
            cw_ask_open(win, armed);
        } else if (item == CW_F_SAVE) {
            if (!cw_mod)
                return;                 /* the item is greyed; belt and braces */
            cw_ask_save(win, 0);
        } else if (item == CW_F_SAVEAS) {
            cw_ask_save(win, 1);
        }
    } else if (menu == CW_M_EDIT) {
        if (item == CW_E_COPY) {
            if (cw_len <= 0)
                return;
            if (os88_clip_put(cw_buf, (unsigned)cw_len) != 0)
                cw_toast("The clipboard would not take it");
            else
                cw_toast("Copied as plain text - formatting is not carried");
        } else if (item == CW_E_PASTE) {
            cw_paste(win);
        } else if (item == CW_E_CLEAR) {
            if (cw_len <= 0)
                return;
            if (!cw_confirm(armed, "Clear All again to discard"))
                return;
            cw_doc_clear();
            cw_cur = 0;
            cw_top = 0;
            cw_par = 0;
            cw_mod = 1;
            cw_show(win, -1, 0, 0);
        }
    } else if (menu == CW_M_FMT) {
        if (item == CW_T_BOLD)
            cw_attr = cw_attr ^ CW_A_BOLD;
        else if (item == CW_T_ITAL)
            cw_attr = cw_attr ^ CW_A_ITAL;
        else if (item == CW_T_ULINE)
            cw_attr = cw_attr ^ CW_A_ULINE;
        else if (item == CW_T_PLAIN)
            cw_attr = 0;
        cw_show(win, -2, 0, 0);
    }

    cw_menu_state();
}

void os88_about(void *win)
{
    if (cw_layout(win) != 0)
        return;
    cw_about_up = 1;
    cw_draw_about();
}

/* The Standard File dialog's answer, long after the call that opened it
 * (SPEC.md 38.6). Cancel calls nothing at all, which is why there is no
 * cancel path here. */
void os88_onfile(int mode, const char *name, unsigned size_lo,
                 unsigned size_hi, void *win)
{
    int changed;

    changed = 0;
    if (mode == OS88_FDLG_OPEN)
        changed = cw_open(win, name, size_lo, size_hi);
    else
        cw_save_as(win, name);          /* a save changes no pixel of the text */

    cw_menu_state();

    /* An open repaints everything, because everything is different. A save,
     * and a refusal of either, repaints NOTHING but the status line and the
     * caret - the text on the glass is still the text. */
    cw_show(win, changed ? -1 : -2, 0, 0);
}

/* ==========================================================================
 * THE ENTRY POINT (SPEC.md 20.2, 21 step 8)
 *
 * The UI task, the gfx lock NOT held, no window yet and the instance not
 * published. Create the window and return it; 0 aborts the launch. Do not
 * draw here - the loader shows the window, which paints it.
 * ========================================================================*/
void *os88_main(void)
{
    void *win;
    unsigned n;
    int w;
    int h;
    int x;
    int y;

    /* THE DOCUMENT FIRST, THE WINDOW SECOND. os88_main() is one of the two
     * places a file call is legal (SPEC.md 67.4), and reading the launched-on
     * document here means the window can be CREATED with its name in the
     * caption - where doing it afterwards would need os88_wm_title(), which
     * draws, and the gfx lock is not held here.
     *
     * Claiming the extension is the other half of the same feature (SPEC.md
     * 54.5): `.RTF` is cword's, and `.DOC` and `.WTX` belong to SPEC.md 65's
     * assembly Word port - this program must never answer to either. */
    os88_assoc_set("RTF", "CWORD");

    os88_strcpy(cw_title, "CWord - Untitled", sizeof(cw_title));
    if (os88_arg_file(cw_fname, &cw_place) == 0) {
        if (os88_file_goto(&cw_place) != 0) {
            cw_fname[0] = 0;
        } else {
            n = os88_file_read(cw_fname, cw_rtf, (unsigned)CW_RTF_MAX);
            if (n == 0 || cw_rtf_parse((int)n) == CW_RTF_NOTRTF) {
                cw_fname[0] = 0;        /* not ours after all: start empty and
                                         * say nothing - the user asked for a
                                         * document, and an error box before
                                         * the window exists has nowhere to go */
                cw_doc_clear();
            } else {
                os88_strcpy(cw_title, "CWord - ", sizeof(cw_title));
                os88_strcpy(cw_title + 8, cw_fname, sizeof(cw_title) - 8);
            }
        }
    }

    /* Size the frame from the LIVE screen. 640x480 is a reference and not a
     * promise: two adapters of three are 640x200, and a window authored at
     * 340 px tall would be clamped into something with two rows of text
     * (SPEC.md 39). */
    os88_video(&cw_vid);

    w = cw_vid.w - 64;
    if (w > 592)                        /* 74 columns of text and the margins */
        w = 592;
    h = cw_vid.dock_top - OS88_MBAR_H - 24;
    if (h > 344)
        h = 344;
    if (h < 96)                         /* OS88_WMIN_H, so wm_create clamps
                                         * rather than refuses on a 200-row
                                         * screen with a tall dock */
        h = 96;
    x = (cw_vid.w - w) / 2;
    y = OS88_MBAR_H + 8;

    win = os88_wm_create(x, y, w, h, cw_title);
    if (win == 0)
        return 0;

    os88_wm_snap(win, 1);               /* content origin on a multiple of 8:
                                         * the single-store path for every
                                         * font_run (SPEC.md 6.1, 11.94), and
                                         * what os88_gfx_scroll() needs to
                                         * accept the text band at all */
    os88_menu_set(win, &cw_menus);
    os88_about_set(win);
    cw_menu_state();
    return win;
}
