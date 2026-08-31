/* ============================================================================
 * os8088 - apps/loom/loom.c
 *
 * LOOM, the in-OS IDE for the Weave family (docs/WEAVE-SPEC.md 1.2, wave 6).
 * A C package (SPEC.md 73) with hand-written 8086 cores for the per-byte
 * loops, WEAVE's and CWORD's shape.
 *
 * WHAT IT IS. WEAVE-SPEC 1.1's last paragraph: "Loom closes the loop - an
 * in-OS IDE that edits the sources and packs the bundle on the machine,
 * byte-identical to the host packer (WEAVE-SPEC 11), so the whole
 * develop-run cycle lives on the target with zero new kernel bytes." One
 * window: a file switcher for the project's four sources down the left, a
 * plain source editor to its right, a status row along the bottom, and
 * `File > Pack Bundle` on `^P`.
 *
 * IT IS NOT WEAVE. WEAVE.O88 is a separate package - the WORD/CWORD
 * precedent, that two things may not answer to one name - and nothing in this
 * file may reach a `weave` PACKAGE name. What the two share they share as
 * SOURCE and never as a copy (WEAVE-SPEC 1.2, SPEC.md 20.5.1): apps/weave/
 * weave.h's format constants are #included below and apps/weave/wblob.inc's
 * claim accessors are %included by apps/loom/loom.asm.
 *
 * ---------------------------------------------------------------------------
 * THE FILES
 * ---------------------------------------------------------------------------
 *   loom.asm     the shim: the name, the callbacks, the icon, the association
 *   loom.h       THE CONTRACT FILE - the workspace layout, the model, the
 *                compilers' entry points. Read it before this one
 *   loom.c       THIS FILE - the state, the window, the menus, the callbacks
 *   lmerr.c      WEAVE-SPEC 10.5's pack-error voice and the fold
 *   lmatom.c     WEAVE-SPEC 2.7's interner
 *   lmproj.c     the project: the four slots, the claims, load and save,
 *                the sidebar, and SPEC.md 19.9's APPDATA preferences
 *   lmed.c       the editor - the caret, the line table, the damage model
 *   lmprev.c     WEAVE-SPEC 1.7's Preview
 *   lmwml.c      the WML compiler        \
 *   lmwjs.c      the WJS compiler         |  LOOM.OVL's tenants
 *   lmsheet.c    the FX pre-compiler      |  (WEAVE-SPEC 1.2)
 *   lmwrite.c    the bundle writer       /
 *   lmovl.c      the rest of LOOM.OVL: About, New Project, the project open
 *   lmui.inc     the alert, the scroll bar, the movers and the line scanner
 *
 * `nasm -f bin` has no notion of an external symbol, so a C package is ONE
 * compilation (SPEC.md 73.1): the .c files above are #included into this one,
 * and every one of them is a written prerequisite in the Makefile because
 * make cannot see through a #include.
 *
 * ---------------------------------------------------------------------------
 * THE FOUR C RULES (docs/C-TOOLCHAIN.md), AND WHERE THIS PACKAGE MEETS THEM
 * ---------------------------------------------------------------------------
 *  1. NO ADDRESS OF AN AUTOMATIC - SS is LOW_SEG and DS is ours, so `&local`
 *     is a stack offset dereferenced through the package segment. Every
 *     buffer here is file scope, including every out-parameter. Refused at
 *     build time by tools/cc8086.py, which is the only reason it is not a
 *     silent defect.
 *  2. NO STRING INSTRUCTION - ES is the kernel's. Nothing here assigns a
 *     struct, passes one by value or returns one. Every string instruction in
 *     this package is in apps/loom/lmui.inc or apps/weave/wblob.inc, and each
 *     one loads its segments on purpose and puts them back.
 *  3. NO long, NO float, NO bit-field.
 *  4. SMALL FRAMES - no local arrays; the build prints every frame size.
 *
 * ...AND ES = KERNEL_SEG ON ENTRY TO EVERY CALLBACK (SPEC.md 20.4), which is
 * why nothing below dereferences the `void *win` it is handed: it is an
 * offset in the KERNEL's segment, and every use of it goes back through an
 * os88_wm_* call.
 * ==========================================================================*/

#include "os88.h"
#include "../weave/weave.h"             /* WEAVE-SPEC 2's format constants -
                                         * SHARED SOURCE (1.2), which is why
                                         * this reaches into apps/weave/ */
#include "loom.h"

/* ============================================================================
 * THE STATE
 *
 * All of it static, and that is not a style: the address of an automatic is a
 * stack offset dereferenced through the package segment, because SS != DS,
 * and tools/cc8086.py refuses the build (SPEC.md 73.5). Every out-parameter
 * below is therefore a static too.
 *
 * A static is not re-entrant - it is per package INSTANCE, not per call - and
 * that is sound here only because every path that touches these runs on the
 * UI task and none of them re-enters. LOOM is instance-per-project: a second
 * project is a second instance in a segment of its own, so there is nothing
 * here to share.
 * ==========================================================================*/

#define LM_MSG   88                     /* one sentence, the status row's */

static void *lm_win;                    /* our window, banked at create */
static int   lm_state;                  /* LM_ST_* - and SPEC.md 54.8's third
                                         * trap is that the OPEN changes it,
                                         * so nothing may branch on it before
                                         * the launch document is spent */
static char  lm_status[LM_MSG];         /* the status row's sentence */
static char  lm_shown[LM_MSG];          /* ...and what is ON the glass, so the
                                         * row is repainted only when it
                                         * CHANGED (PERFORMANCE.md Part 5) */
static char  lm_line[LM_MSG];           /* one line under construction */
static char  lm_num[8];
static char  lm_amsg[40];               /* the alert's line - os88ui.inc holds
                                         * the POINTER and letters from it
                                         * when the alert paints, so it has to
                                         * outlive the call */
static int   lm_asking;                 /* LM_ASK_* - which question is up */
static int   lm_sidebar = 1;            /* View > Sidebar */

#define LM_ASK_NONE  0
#define LM_ASK_CLOSE 1                  /* SPEC.md 75.1's Save/Discard/Cancel */

/* The launch document (SPEC.md 54.5, 54.8). `lm_arghave` is separate from the
 * locator because 0,0 is a REAL locator - the root of volume A: - and so the
 * pair cannot speak for itself. */
static char              lm_argname[16];
static struct os88_place lm_argplace;
static int               lm_arghave;
static int               lm_pendok;

/* The live geometry (WEAVE-SPEC 7.1's rule, borrowed: the truth is the LIVE
 * screen and never a reference constant - SPEC.md 39). Derived every paint
 * from the window, never cached across one. */
static struct os88_pt   lm_org;
static struct os88_size lm_sz;
static int lm_ox, lm_oy;                /* the content origin, x rounded UP */
static int lm_cw, lm_ch;                /* the whole content, in cells */
static int lm_sidew;                    /* the sidebar, in cells (0 = hidden) */
static int lm_ex;                       /* the editor's pen, in PIXELS */
static int lm_ecols, lm_erows;          /* ...and its box, in cells */
static int lm_sbx;                      /* the scroll bar's x1, in pixels */

#define LM_SBW  14                      /* the bar's width - what both kernel
                                         * callers use and what os88ui.inc's
                                         * arrow glyph is drawn for */

/* THE EDITOR PANE'S BOUNDS, and where its glass shadow lives.
 *
 * The stride is sized from the widest pane the family can produce: Hercules
 * is 720 px, so 89 content cells (WEAVE-SPEC 7.1.1), less the 14-px scroll
 * bar, is 87 - and the sidebar can be HIDDEN, so its 12 cells may not be
 * subtracted. The row count is sized from the tallest: VGA's 480-px screen
 * gives 52 content rows.
 *
 * THE SHADOW IS NOT IN BSS. 88 x 52 is 4,576 bytes - 7.5% of SPEC.md 20.1's
 * whole 61,440 for image AND bss together - in a package whose overlay
 * tenants need every byte of the rest (LOOM.OVL's compilers keep their
 * literals and globals resident, SPEC.md 73.14). So it lives at the END of
 * the source claim, past the four LM_TEXTMAX slots, and lm_shdiff() in
 * apps/loom/lmui.inc is the assembly that reads it. apps/loom/lmed.c's header
 * carries what the shadow IS; this block is only where it is.
 *
 * These four are here rather than in lmed.c because lmproj.c takes the claim
 * and lmproj.c is #included first. */
#define LM_SH_STRIDE  88
#define LM_SH_ROWS    52
#define LM_SHBASE     (LM_NSLOT * LM_TEXTMAX)
#define LM_CLAIMKB    29                /* 24,576 of source + 4,576 of shadow
                                         * = 29,152, and a claim is whole KB */

/* lm_nline - how many lines the current source has. It belongs to lmed.c and
 * it is DECLARED HERE because lmproj.c reads it (the open's report says how
 * many lines were read) and lmproj.c is #included first. A C package is one
 * translation unit (SPEC.md 73.1) and a static cannot be forward-declared
 * where it is also defined, so the two choices are this or an ordering that
 * does not exist - lmed.c needs lmproj.c's slots and lmproj.c needs lmed.c's
 * count. apps/weave/weave.c carries four of these for the same reason and
 * calls the list "deliberately short: a name here is a seam between two
 * files". This is the only one. */
static int lm_nline;

/* ============================================================================
 * THE FORWARD DECLARATIONS
 *
 * A C package is ONE compilation (SPEC.md 73.1) and the parts below are
 * #included in dependency order, but four of them cross backwards - a menu
 * command repaints, a click switches a file, the close guard saves, the
 * editor puts a sentence in the status row - and no ordering makes all of
 * that forward-free. The list is deliberately short: a name here is a seam
 * between two files, and a long list would mean the split is in the wrong
 * place.
 * ==========================================================================*/
static void lm_say(const char *s);
static int  lm_layout(void *win);
static void lm_repaint(void *win, int clear);
static void lm_status_paint(int force);
static void lm_side_paint(void);
static void lm_side_invalidate(void);
static int  lm_nmodified(int *first);
static void lm_side_click(int x, int y);
static void lm_ed_invalidate(void);
static void lm_ed_paint(int force);
static void lm_ed_key(int ascii, int scan);
static int  lm_ed_click(int x, int y);
static void lm_ed_reset(void);
static void lm_ed_caret_off(void);
static void lm_ed_caret_on(void);
static void lm_ed_goline(int line);
static void lm_switch(int slot);
static int  lm_anymod(void);
static int  lm_save(int slot);
static void lm_saveall(void);
static void lm_prefs_read(void);
static void lm_prefs_write(void);
static void lm_open_pend(void *win);
static void lm_pack(void);
static void lm_prev_toggle(void);
static void lm_prev_paint(void);
static void lm_freeproject(void);
static int  ovl_openproj(void *win, const char *name);
static int  ovl_newproj(const char *name);
static int  ovl_about(void);

/* --- the bridges into apps/loom/lmui.inc --------------------------------- */
int      lm_ask(const char *msg, void *win, int set);
void     lm_sbar(int *blk);
void     lm_sbmove(int *blk, int oldpos);
int      lm_sbhit(int *blk, int x, int y);
void     lm_move(unsigned seg, unsigned dst, unsigned src, unsigned n);
void     lm_scopy(unsigned dseg, unsigned doff, unsigned sseg, unsigned soff,
                  unsigned n);
void     lm_sfill(unsigned seg, unsigned off, unsigned v, unsigned n);
unsigned lm_lines(unsigned seg, unsigned off, unsigned n, int *tab,
                  unsigned max);
unsigned lm_shdiff(unsigned seg, unsigned off, const char *row, unsigned n);

/* os88ui.inc's constants, mirrored here because a C file may not name an nasm
 * equ. apps/loom/loom.asm carries the %if that fails the build if either set
 * moves - which is the only thing that stops these two copies drifting. */
#define LM_ASAVE       2                /* Save / Discard / Cancel */
#define LM_SB_NONE     0
#define LM_SB_UP       1
#define LM_SB_DOWN     2
#define LM_SB_PGUP     3
#define LM_SB_PGDN     4
#define LM_SB_THUMB    5

/* ============================================================================
 * SMALL HELPERS
 * ==========================================================================*/

static void lm_l0(void)
{
    lm_line[0] = 0;
}

static void lm_ls(const char *s)
{
    unsigned n;

    n = os88_strlen(lm_line);
    os88_strcpy(lm_line + n, s, sizeof(lm_line) - n);
}

static void lm_ln(int v)
{
    os88_itoa(v, lm_num);
    lm_ls(lm_num);
}

/* lm_say - put a sentence in the status row and in the toast.
 *
 * WEAVE-SPEC 10.1's shape, which is C64-SPEC 1.4's: the sentence is in the
 * content area, the status row KEEPS it after the toast has retired itself,
 * and the toast fires too - never a bare failure. The row is drawn by
 * lm_status_paint() and only when the string CHANGED, which is why there are
 * two arrays: one is what we mean and one is what is on the glass. */
static void lm_say(const char *s)
{
    os88_strcpy(lm_status, s, sizeof(lm_status));
    os88_toast(s, 0);
}

/* lm_quiet - the same sentence WITHOUT the toast, for the routine reports
 * (`MAIN.WML, 41 lines.`) that would otherwise fire a toast per keystroke. */
static void lm_quiet(const char *s)
{
    os88_strcpy(lm_status, s, sizeof(lm_status));
}

/* ============================================================================
 * THE MENUS (SPEC.md 12.2)
 *
 * TWO OF THE FIVE, and the third is the kernel's: it puts `About Loom` and
 * `Close` into the pull-down named for the program, so a File > Close here
 * would be a second door to one room. WEAVE-SPEC 1.7's shortcuts carry their
 * key IN THE LABEL - `Pack Bundle  ^P` - because there is no such thing as a
 * menu accelerator on this machine: OSAPI_MENU_SET draws and tracks a bar and
 * nothing else, so a package that wants a shortcut reads it in its own
 * W_ONKEY and says so in the item. That is Note Pad's convention verbatim.
 *
 * NOT `const`: os88_menu_set() patches the set's oncmd field with the
 * runtime's command trampoline, and a set in .rodata takes the patch as
 * silent nonsense. The item arrays are not const either, because five of the
 * seven items GREY THEMSELVES when there is no project open - SPEC.md 47's
 * rule that a control states a FACT rather than refusing after the click, and
 * a leading OS88_MENU_DIS byte is what draws one disabled.
 * ==========================================================================*/

#define LM_CMD_NEW    0
#define LM_CMD_OPEN   1
#define LM_CMD_SAVE   2
#define LM_CMD_SAVEA  3
#define LM_CMD_PACK   4

#define LM_CMD_PREV   0
#define LM_CMD_SIDE   1

static const char *lm_fitems[5] = {
    "New Project...", "Open Project...",
    "\001Save  ^S", "\001Save All", "\001Pack Bundle  ^P"
};
static const char *lm_vitems[2] = { "\001Preview", "Sidebar" };

static struct os88_menuset lm_menus = {
    "Loom", 0, 2,
    { { "File", lm_fitems, 5 },
      { "View", lm_vitems, 2 } }
};

/* lm_menusync - the five items that are only meaningful with a project open.
 * The kernel reads these strings each time the menu drops, so swapping the
 * pointer is the whole of it. */
static void lm_menusync(void)
{
    int open;

    open = (lm_state != LM_ST_EMPTY);
    lm_fitems[LM_CMD_SAVE]  = open ? "Save  ^S"        : "\001Save  ^S";
    lm_fitems[LM_CMD_SAVEA] = open ? "Save All"        : "\001Save All";
    lm_fitems[LM_CMD_PACK]  = open ? "Pack Bundle  ^P" : "\001Pack Bundle  ^P";
    lm_vitems[LM_CMD_PREV]  = open
        ? (lm_state == LM_ST_PREVIEW ? "Edit" : "Preview")
        : "\001Preview";
}

/* ============================================================================
 * THE PARTS.  Order is the compiler's: a thing is declared before it is used,
 * and there is no second translation unit to hold a prototype for it.
 * ==========================================================================*/

#include "lmerr.c"                      /* 10.5's voice and 3.1's fold */
#include "lmatom.c"                     /* 2.7's interner */
#include "lmproj.c"                     /* the project, the claims, the
                                         * sidebar, 19.9's preferences */
#include "lmed.c"                       /* the editor and its damage model */
#include "lmprev.c"                     /* 1.7's Preview */
#include "lmwml.c"                      /* the WML compiler   \ LOOM.OVL's */
#include "lmwjs.c"                      /* the WJS compiler    | tenants   */
#include "lmsheet.c"                    /* the FX pre-compiler | (1.2)     */
#include "lmwrite.c"                    /* the bundle writer  /            */
#include "lmovl.c"                      /* ...and the rest of LOOM.OVL */

/* ============================================================================
 * THE GEOMETRY (WEAVE-SPEC 7.1's rule, applied to a different window)
 *
 * Every number comes off the LIVE screen. The reference constants are a
 * reference and not a promise, and reading them would be wrong on two
 * adapters of three (SPEC.md 39). The content origin's x is rounded UP to a
 * multiple of 8 for WEAVE-SPEC 7.1.2's reason - OSAPI_FONT_RUN's single-store
 * fast path needs the pen there, and an editor is nothing but font runs - and
 * os88_wm_snap() keeps it there as the window is dragged.
 *
 * IT DRAWS NOTHING. os88_onresize() calls it and SPEC.md 11.98 forbids
 * drawing in that callback.
 * ==========================================================================*/
static int lm_layout(void *win)
{
    int right;

    if (os88_wm_geom(win, &lm_sz) != 0)
        return 0;                       /* not visible: nothing to lay out */
    os88_wm_content(win, &lm_org);
    lm_ox = (lm_org.x + 7) & ~7;
    lm_oy = lm_org.y;
    lm_cw = (lm_org.x + lm_sz.w - lm_ox) / 8;
    lm_ch = lm_sz.h / 8;
    if (lm_cw < 24 || lm_ch < 4)
        return 0;                       /* below os88_wm_minsize()'s floor,
                                         * which the kernel is holding for us
                                         * anyway - this is the belt */

    /* The sidebar goes when the window is too narrow to carry it AND a usable
     * editor. SPEC.md 47's shape: the pane states the fact by not being
     * there, and View > Sidebar puts it back when there is room. */
    lm_sidew = lm_sidebar ? LM_SIDE_CELLS : 0;
    if (lm_sidew && lm_cw - lm_sidew < 20)
        lm_sidew = 0;

    right = lm_org.x + lm_sz.w - 1;     /* the content's last pixel column */
    lm_sbx = right - (LM_SBW - 1);
    lm_ex = lm_ox + lm_sidew * 8;
    lm_ecols = (lm_sbx - lm_ex) / 8;
    if (lm_ecols > LM_SH_STRIDE)
        lm_ecols = LM_SH_STRIDE;
    if (lm_ecols < 1)
        return 0;
    lm_erows = lm_ch - 1;               /* the status row is the last one */
    if (lm_erows > LM_SH_ROWS)
        lm_erows = LM_SH_ROWS;
    if (lm_erows < 1)
        return 0;
    return 1;
}

/* ============================================================================
 * PAINTING
 *
 * Four pictures, one entry. A project that is open gets the SIDEBAR, the
 * EDITOR and the STATUS ROW; Preview replaces the editor pane and leaves the
 * other two. Nothing open gets a short screen that says what to do.
 *
 * EVERY LINE OF THE CHROME IS ONE os88_font_run(): the cells' background and
 * their glyphs in a single pass (SPEC.md 6.1), so a line is never momentarily
 * blank. The erase-then-letter pair is the canonical double-draw in this tree
 * and it is invisible in an emulator. The pen is lm_ox or lm_ex, both
 * multiples of 8, so every one of these takes font_run's single-store fast
 * path on both 1bpp adapters.
 * ==========================================================================*/

/* lm_row - one cell row of the whole content area, by row index. Rows past CH
 * are not drawn at all: degradation is CLIPPING against the content box. */
static void lm_row(int r, const char *s)
{
    if (r < 0 || r >= lm_ch)
        return;
    os88_font_run(lm_ox, lm_oy + 8 * r, s, OS88_BLACK, OS88_WHITE);
}

/* lm_wipe - the content area, white. ONE fill, not a padded run per row:
 * padding every line to CW would cost a 79-cell font_run where a 34-cell one
 * would do - about 71 ms against 30 on the target. The kernel does this for
 * us before W_PAINT; every other caller owes it, because os88_font_run()
 * letters exactly the cells it is given and a shorter line leaves the tail of
 * the longer one it replaced on the glass. */
static void lm_wipe(void)
{
    os88_set_color(OS88_WHITE);
    os88_gfx_fill(lm_org.x, lm_org.y, lm_org.x + lm_sz.w - 1,
                  lm_org.y + lm_sz.h - 1);
}

/* lm_status_paint - the bottom row, and ONLY when it changed.
 *
 * Two arrays and one compare: lm_status is what we mean, lm_shown is what is
 * on the glass. A status row repainted on every keystroke is a 79-cell
 * font_run - 71 ms on the target machine - for a line that says the same
 * thing it said before, and it is exactly the kind of cost an emulator prices
 * at zero. The padding to CW is the erase (WEAVE-SPEC 6.2's rule): a shorter
 * sentence must not leave the tail of a longer one behind it.
 *
 * `force` is for the callers drawing over a content area that still holds the
 * LAST state - a repaint after a menu command - where the shadow is a claim
 * about pixels that have been overwritten. */
static char lm_srow[LM_SH_STRIDE + 2];

static void lm_status_paint(int force)
{
    int i, n, w;

    w = lm_cw;
    if (w > LM_SH_STRIDE)
        w = LM_SH_STRIDE;
    if (!force) {
        for (i = 0; i < LM_MSG; i++) {
            if (lm_status[i] != lm_shown[i])
                break;
            if (lm_status[i] == 0)
                return;                 /* identical: nothing to draw */
        }
        if (i == LM_MSG)
            return;
    }
    os88_strcpy(lm_shown, lm_status, sizeof(lm_shown));
    n = (int) os88_strlen(lm_status);
    if (n > w)
        n = w;
    for (i = 0; i < n; i++)
        lm_srow[i] = lm_status[i];
    for (i = n; i < w; i++)
        lm_srow[i] = ' ';
    lm_srow[w] = 0;
    os88_font_run(lm_ox, lm_oy + (lm_ch - 1) * 8, lm_srow,
                  OS88_BLACK, OS88_WHITE);
}

static void lm_paint_empty(void)
{
    /* THREE LINES AND NOT SIX. A literal is resident whatever draws it
     * (SPEC.md 73.14) and this package has tens of bytes spare, so this
     * screen says what to DO and stops; WEAVE-SPEC 11.2 is where a project's
     * shape is written down, and repeating it here would cost more image than
     * the sentence is worth. */
    lm_row(0, "LOOM - no project open.");
    lm_row(2, "File > Open Project... opens a .WML; a double-click on a .WML");
    lm_row(3, "or a .WJS opens its project directly (WEAVE-SPEC 1.5, 11.2).");
}

/* lm_repaint - draw the content.
 *
 * `clear` is the difference between the two kinds of caller, and it is a real
 * difference rather than a flag for tidiness. W_PAINT arrives with the
 * content ALREADY WHITENED by the kernel, so clearing there would be the
 * erase-then-letter double-draw over the whole box. Every other caller - a
 * menu command, the file dialog's completion, a Preview toggle - is drawing
 * over a content area that still holds the LAST state, so it clears AND
 * forces every shadow, because a shadow is a claim about pixels that are no
 * longer there. */
static void lm_repaint(void *win, int clear)
{
    if (!lm_layout(win))
        return;

    if (clear)
        lm_wipe();

    /* BOTH SHADOWS GO, ON EVERY ENTRY, AND THAT IS THE WHOLE POINT OF THIS
     * FUNCTION BEING THE ONLY WAY IN. A shadow is a claim about what is on
     * the GLASS, and every caller of lm_repaint() has just invalidated that
     * claim: `clear` callers by the lm_wipe() above, and W_PAINT because the
     * kernel whitens the content area before it calls us (that is exactly
     * what OSAPI_WM_OWNBG opts out of). A shadow kept across either one says
     * "that row is already right" about pixels that are gone - which draws
     * nothing where the row should be, or leaves the tail of whatever the
     * damage repaint restored. Both were seen on the glass.
     *
     * It costs a full pane repaint per W_PAINT, which is what a W_PAINT
     * already is. What it must NOT become is a full repaint per keystroke,
     * and it cannot: nothing on the editing paths calls lm_repaint(). */
    lm_ed_invalidate();
    lm_side_invalidate();

    if (lm_state == LM_ST_EMPTY) {
        lm_paint_empty();
        lm_status_paint(1);
        return;
    }
    lm_side_paint();
    if (lm_state == LM_ST_PREVIEW)
        lm_prev_paint();
    else
        lm_ed_paint(clear);
    lm_status_paint(1);
}

/* ============================================================================
 * THE CALLBACKS
 * ==========================================================================*/

void os88_paint(void *win)
{
    /* SPEND THE BANKED LAUNCH DOCUMENT FIRST, and then branch on the state -
     * never the other way round. SPEC.md 54.8's third trap is that loading is
     * not showing: a paint that tests its state before the load reads the
     * state the load was about to change, and the project then opens
     * perfectly and is covered by the empty screen, with no error anywhere.
     *
     * It happens HERE and not in the entry proc because opening a project
     * DRAWS - the refusal notice, the report, the toast - and the entry proc
     * holds no gfx lock and has no window on the screen yet. The flag makes
     * it happen exactly once. */
    if (lm_pendok) {
        lm_pendok = 0;
        lm_open_pend(win);
    }
    lm_repaint(win, 0);                 /* the W_PAINT case: already whitened */
}

/* W_ONRESIZE (SPEC.md 11.98): the content box changed and we did not ask - an
 * adapter change under us, or a drag across the seam onto a shorter display.
 * We MUST NOT DRAW here; a full repaint follows immediately. So this is the
 * chance to be laid out correctly BEFORE that paint runs rather than a frame
 * later. The shadow is invalidated because it describes a box that no longer
 * exists - every cell of it is about to be somewhere else. */
void os88_onresize(int w, int h, void *win)
{
    (void)w;
    (void)h;
    lm_ed_caret_off();                  /* its banked position is stale too */
    lm_layout(win);
    lm_ed_invalidate();
}

void os88_onclick(int x, int y, void *win)
{
    if (lm_state == LM_ST_EMPTY)
        return;
    if (!lm_layout(win))
        return;

    /* The sidebar first: it owns the left LM_SIDE_CELLS cells of every row
     * above the status row, and a click there switches the file or jumps the
     * caret to a pack error (WEAVE-SPEC 11.3). */
    if (lm_sidew && x < lm_ex) {
        lm_side_click(x, y);
        return;
    }
    if (lm_state == LM_ST_PREVIEW)
        return;                         /* 1.7: Preview's widgets arm and fire
                                         * natively, and there are none until
                                         * the painter lands - see lmprev.c */
    lm_ed_click(x, y);
}

void os88_onkey(int ascii, int scan, void *win)
{
    /* WEAVE-SPEC 1.7's shortcuts, read HERE and as CONTROL CHARACTERS. There
     * is no menu accelerator on this machine (OSAPI_MENU_SET draws and tracks
     * a bar and nothing else), and a bare letter would be eaten by the
     * editor - which is the same argument WEAVE-SPEC 1.7 makes about `^R`
     * against the `r` wave 2 shipped. */
    if (ascii == LM_K_SAVE) {
        if (lm_state != LM_ST_EMPTY) {
            lm_save(lm_slot);
            lm_side_paint();
            lm_status_paint(0);
        }
        return;
    }
    if (ascii == LM_K_PACK) {
        if (lm_state != LM_ST_EMPTY)
            lm_pack();
        return;
    }
    if (ascii == LM_K_NEXT) {
        if (lm_state != LM_ST_EMPTY)
            lm_switch(lm_nextslot());
        return;
    }
    if (lm_state != LM_ST_EDIT)
        return;
    if (!lm_layout(win))
        return;
    lm_ed_key(ascii, scan);
}

void os88_oncmd(int item, int menu, void *win)
{
    if (menu == 0) {
        if (item == LM_CMD_NEW) {
            os88_file_dlg(OS88_FDLG_SAVE, win, "MAIN.WML");
            return;
        }
        if (item == LM_CMD_OPEN) {
            os88_file_dlg(OS88_FDLG_OPEN, win, 0);
            return;
        }
        if (lm_state == LM_ST_EMPTY)
            return;                     /* the item is greyed; a click on a
                                         * greyed item never reaches us, and
                                         * this is the belt (SPEC.md 47) */
        if (item == LM_CMD_SAVE) {
            lm_save(lm_slot);
            lm_side_paint();
            lm_status_paint(0);
            return;
        }
        if (item == LM_CMD_SAVEA) {
            lm_saveall();
            lm_side_paint();
            lm_status_paint(0);
            return;
        }
        if (item == LM_CMD_PACK)
            lm_pack();
        return;
    }
    if (item == LM_CMD_PREV) {
        if (lm_state == LM_ST_EMPTY)
            return;
        lm_prev_toggle();
        lm_repaint(win, 1);
        return;
    }
    if (item == LM_CMD_SIDE) {
        lm_sidebar = !lm_sidebar;
        lm_repaint(win, 1);
    }
}

/* W_ONFILE: the Standard File dialog's completion (SPEC.md 38). THE SECOND
 * WAY IN, and it enters the ONE load path at the same place the association
 * route does - Frotz's "two ways in, one decision" rule. `mode` is what
 * separates New Project from Open: a SAVE dialog names a file that need not
 * exist, and that is exactly what "make me a project here" means. */
void os88_onfile(int mode, const char *name, unsigned size_lo,
                 unsigned size_hi, void *win)
{
    (void)size_lo;
    (void)size_hi;
    if (mode == OS88_FDLG_SAVE) {
        /* New Project: the skeleton goes down first, and the name it went
         * down under - which is `name` with its extension made `.WML` - is
         * what the ordinary open path is then handed. One project loader and
         * not two (apps/weave/wload.c's "two ways in, one decision", said
         * about creating instead of opening). */
        if (!ovl_newproj(name)) {
            lm_say("LOOM.OVL is missing; a project cannot be created.");
            lm_repaint(win, 1);
            return;
        }
        name = lm_argname;
    }
    if (!ovl_openproj(win, name))
        lm_say("LOOM.OVL is missing; a project cannot be opened.");
    lm_repaint(win, 1);                 /* over the LAST state - see lm_repaint */
}

void os88_about(void *win)
{
    /* THE OVERLAY, and a refused one returns 0 (apps/cc/crt0.asm). About is a
     * menu command and menu commands may refuse (SPEC.md 73.14), so a missing
     * LOOM.OVL draws nothing at all here and leaves the window alone - which
     * is the honest degradation, and it is why the answer is tested. */
    if (!ovl_about())
        lm_say("LOOM.OVL is missing; About cannot be shown.");
    lm_repaint(win, 1);
}

/* ============================================================================
 * THE CLOSE GUARD (SPEC.md 75.1, 75.3)
 *
 * The kernel asks before it closes the window, through every door there is -
 * the close box, the app-name menu's Close, the dock tile's context menu -
 * and this is in front of all of them. It is os88_onclick()'s environment:
 * the UI task, the gfx lock HELD, so it may draw, may call the file slots and
 * may raise an alert.
 *
 * A MODIFIED SOURCE IS THE ONE THING IN THIS PROGRAM THAT CANNOT BE
 * RECOVERED. So: nothing modified, let it go. Anything modified, REFUSE and
 * put up OS88UI_ASAVE - Save / Discard / Cancel - and lm_alertdone() calls
 * os88_wm_close() when the user has answered. A refusal with nothing behind
 * it is a window that cannot be closed and real mode has no way to take that
 * back, which is why the alert's own answer is tested: if os88ui_ask REFUSES
 * (one is already up, or the window table is full) we let the close happen
 * rather than trapping the user, and the toast says the work was lost.
 * ==========================================================================*/
static int lm_modfirst;                 /* the close guard's out-parameter,
                                         * file scope because `&local` is a
                                         * stack offset through the package
                                         * segment (SPEC.md 73.5) */

int os88_onclose(void *win)
{
    int n;

    n = lm_nmodified(&lm_modfirst);
    if (n == 0) {
        lm_prefs_write();               /* SPEC.md 19.9: the last project and
                                         * the last slot, written on the way
                                         * out. It has to be HERE and not only
                                         * on the alert's Save/Discard arms -
                                         * a window closed with nothing
                                         * modified never raises the alert,
                                         * which is the ordinary way a session
                                         * ends and was the way that wrote no
                                         * preference at all */
        return 1;
    }
    if (lm_asking == LM_ASK_CLOSE)
        return 0;                       /* the question is already up: the
                                         * second door waits for the first */
    /* NAME THE FILE THAT IS ACTUALLY UNSAVED, not the one being edited. The
     * first version of this asked about lm_slot, and the run showed it asking
     * "Save changes to FORM.WJS?" about an edit that was in FORM.WML - a
     * question whose answer the user cannot check. With more than one, the
     * count is the honest thing to say: OS88UI_AMAX is 34 characters and four
     * 8.3 names do not fit in them. */
    lm_l0();
    lm_ls("Save changes to ");
    if (n == 1)
        lm_ls(lm_fname(lm_modfirst));
    else {
        lm_ln(n);
        lm_ls(" files");
    }
    lm_ls("?");
    os88_strcpy(lm_amsg, lm_line, sizeof(lm_amsg));
    if (lm_ask(lm_amsg, win, LM_ASAVE) != 0) {
        os88_toast("Alert refused - closing", 0);
        return 1;                       /* never trap the user in a window */
    }
    lm_asking = LM_ASK_CLOSE;
    return 0;
}

/* lm_alertdone - os88ui.inc's completion, reached through lmui.inc's
 * lm_askdone trampoline. `button` is 0-based, or -1 for a DISMISSED alert -
 * the close box, the minimize box or Esc - which MUST mean Cancel and not
 * Save, because a dismissal is the one answer that arrives without the user
 * having chosen anything. */
void lm_alertdone(int button)
{
    int was;

    was = lm_asking;
    lm_asking = LM_ASK_NONE;
    if (was != LM_ASK_CLOSE)
        return;
    if (button == 0) {                  /* Save */
        lm_saveall();
        if (lm_anymod()) {
            lm_side_paint();
            lm_status_paint(0);
            return;                     /* the save FAILED and said so: the
                                         * window stays, which is the whole
                                         * point of asking */
        }
    } else if (button != 1) {           /* Cancel, or a dismissal (-1) */
        lm_status_paint(0);
        return;
    }
    lm_prefs_write();
    os88_wm_close(lm_win);              /* Save-that-worked, or Discard */
}

/* ============================================================================
 * THE ENTRY POINT (SPEC.md 20.2)
 *
 * The gfx lock is NOT held here and the window does not exist yet: create it,
 * attach what the kernel has to be told, bank the launch document, and return
 * the window. It may not draw and may not spawn.
 *
 * BX is reloaded for us: cc_entry does `mov bx, ax` from this function's
 * return value AFTER everything here has run (apps/cc/crt0.asm). That is
 * SPEC.md 54.8's own trap - OSAPI_ARG_FILE clobbers BX and the loader divides
 * by it with no int 0 handler, so a bogus BX hangs the machine with the
 * cursor still moving - and in C it is closed by construction.
 * ==========================================================================*/
void *os88_main(void)
{
    void *win;
    static struct os88_video v;

    /* An ordinary resizable window at SPEC.md 11.95's standard rect - the
     * whole desktop band - which is WEAVE's chrome and the browser's before
     * it. Every number comes off the LIVE screen (SPEC.md 39). */
    os88_video(&v);
    win = os88_wm_create(0, OS88_MBAR_H, v.w,
                         v.dock_top - OS88_MBAR_H - 1, "Loom");
    if (win == 0)
        return 0;                       /* the window table is full: abort */
    lm_win = win;

    os88_menu_set(win, &lm_menus);
    os88_about_set(win);
    os88_wm_sizable(win, 1);

    /* WEAVE-SPEC 7.1.2's whole reason, and an editor is the strongest case
     * for it in the family: with the content origin on a multiple of 8 every
     * cell column is too, and every os88_font_run() takes the single-store
     * fast path on both 1bpp adapters. A no-op on VGA, so it is set
     * unconditionally rather than after asking. */
    os88_wm_snap(win, 1);

    /* The floor, DECLARED rather than negotiated (SPEC.md 11.100.2): a
     * sidebar of 12 cells plus 20 cells of editor plus the scroll bar, and
     * four rows. This slot takes the OUTER frame - content plus the two side
     * borders, plus the title bar and the line under it. */
    os88_wm_minsize(win, 34 * 8 + 2, 6 * 8 + OS88_TITLE_H + 1);

    /* ...and TELL US when the box moves without anyone proposing it. */
    os88_wm_onresize(win);

    /* SPEC.md 75.1's negotiator. It is a SIDE TABLE and not a template word -
     * a missing install here is the quietest failure there is: the C
     * compiles, the %define in the shim is satisfied, os88_onclose() exists,
     * and the kernel simply never asks it. The symptom is a close box that
     * throws away the user's work in silence. */
    os88_wm_onclose(win);

    /* THE LAUNCH DOCUMENT (SPEC.md 54.5). Read-and-clear, so it is banked
     * here and spent in the first paint. os88_arg_file() cannot hand back the
     * name without the locator - they arrive together - which closes half of
     * SPEC.md 54.8's first trap by construction; the other half is spending
     * it, and lm_open_pend() is where that has to happen. */
    if (os88_arg_file(lm_argname, &lm_argplace) == 0) {
        lm_arghave = 1;
        lm_pendok = 1;
    }
    lm_menusync();
    return win;
}
