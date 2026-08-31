/* ============================================================================
 * os8088 - apps/weave/weave.c
 *
 * WEAVE, the .WAB runtime (docs/WEAVE-SPEC.md). A C package (SPEC.md 73) with
 * hand-written 8086 cores for the hot loops, RunCPM's and C64's shape.
 *
 * WHAT IT IS. Weave runs web-style applications on a 4.77 MHz 8088 by
 * inverting the browser: the components are native, and the app's markup,
 * script and formulas are compiled at PACK TIME into one `.WAB` bundle that
 * this program interprets as a display list plus event-handler bytecode.
 * Nothing on the machine ever parses WML or WJS text (WEAVE-SPEC 1.1, 9.4).
 *
 * IT IS NOT LOOM. LOOM.O88 is a separate package - the WORD/CWORD precedent,
 * that two things may not answer to one name - and nothing in this file may
 * reach a `loom` name.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS WAVE IS (WEAVE-SPEC 13.1, wave 2: the runtime's core)
 * ---------------------------------------------------------------------------
 * The package declares itself and its association, accepts a bundle the Frotz
 * way, refuses before it reads, validates a hostile bundle field by field,
 * derives the three adapters' cell grid from the live screen, runs the flow
 * walk, and PAINTS THE COMPONENTS - every one of WEAVE-SPEC 6 that has a
 * picture in this wave, plus a list that scrolls and selects. It does NOT run
 * a line of bytecode, arm a button or edit a field: wpaint.c's w_onhit() is
 * the named seam wave 3 grows into.
 *
 * ---------------------------------------------------------------------------
 * THE FILES
 * ---------------------------------------------------------------------------
 *   weave.asm    the shim: the name, the callbacks, the icon, the association
 *   weave.h      the format's constants and the cores' prototypes
 *   weave.c      THIS FILE - the state, the window, the menus, the callbacks
 *   wval.c       the hostile-bundle reader (WEAVE-SPEC 2, 10.4)
 *   wflow.c      the flow walk (WEAVE-SPEC 7)
 *   wload.c      the accept idiom and the refusal sequence (1.5, 10.1-10.3)
 *   wpaint.c     the component painter and the hit test (WEAVE-SPEC 6)
 *   wovl.c       WEAVE.OVL: the refusable, UI-task-only command paths
 *   wblob.inc    the bundle claim's accessors (assembly)
 *   wdraw.inc    the paint and hit-test cores (assembly, WEAVE-SPEC 1.2)
 *
 * `nasm -f bin` has no notion of an external symbol, so a C package is ONE
 * compilation (SPEC.md 73.1): the .c files above are #included into this one,
 * and every one of them is a written prerequisite in the Makefile because
 * make cannot see through a #include.
 * ==========================================================================*/

#include "os88.h"
#include "weave.h"

/* ============================================================================
 * THE STATE
 *
 * All of it static, and that is not a style: the address of an automatic is a
 * stack offset dereferenced through the package segment, because SS != DS,
 * and tools/cc8086.py refuses the build (SPEC.md 73.5). Every out-parameter
 * below is therefore a static too.
 *
 * A static is not re-entrant - it is per package INSTANCE, not per call - and
 * that is sound here only because every path that touches these runs on the UI
 * task and none of them re-enters. WEAVE is instance-per-app (WEAVE-SPEC 1.4):
 * a second bundle is a second instance in a segment of its own, so there is
 * nothing here to share.
 * ==========================================================================*/

static void *w_win;                     /* our window, banked at create */
static int   w_state;                   /* W_ST_* - and 1.5's trap 3 is that
                                         * the load CHANGES it, so nothing may
                                         * branch on it before the load runs */
static char  w_msg[W_MSG];              /* section 10's sentence, on the glass */
static char  w_status[W_MSG];           /* ...and the row that keeps it */
static char  w_serr[W_MSG];             /* 10.6's LAST SCRIPT ERROR, in full -
                                         * a string of its own and not
                                         * w_status, because Bundle Info is
                                         * where a user goes to READ it and
                                         * ovl_info was overwriting w_status
                                         * with its own line on the way in.
                                         * Found on the glass: the toast said
                                         * `Script error in fn 0.`, Bundle
                                         * Info was opened to see which error,
                                         * and the listing's last row showed
                                         * the ask arithmetic instead */
static char  w_infoln[W_MSG];           /* ...and ovl_info's own line */
static char  w_line[W_MSG];             /* one line under construction */
static char  w_num[8];

/* The launch document (SPEC.md 54.5, 54.8). `w_arghave` is separate from the
 * locator because 0,0 is a REAL locator - the root of volume A: - and so the
 * pair cannot speak for itself. */
static char             w_name[16];
static struct os88_place w_place;
static int               w_arghave;
static int               w_pendok;

/* The bundle, as it stands in its claim (WEAVE-SPEC 2). */
static unsigned w_seg;                  /* the claim, 0 = none held */
static unsigned w_claimkb;
static unsigned w_fsize;                /* the size the DIRECTORY gave */
static unsigned w_size;                 /* ...and the header's word for it */
static unsigned w_flags, w_vmkb, w_gridkb, w_canvaskb, w_nsec, w_entry;
static unsigned w_applen;
static unsigned w_soff[W_NSECTYPE], w_slen[W_NSECTYPE], w_sextra[W_NSECTYPE];
static unsigned char w_shave[W_NSECTYPE];
static unsigned w_lastend;
static unsigned w_natoms, w_nfunc, w_nsprite, w_nformula;
static unsigned w_ncomp, w_ncard;
static unsigned char w_idseen[256];     /* comp_id uniqueness (2.5) */

/* The header probe (10.1): one cluster, read before the claim exists. */
static unsigned char w_probe[W_PROBE];
static unsigned      w_probelen;
static int           w_clus;            /* sectors per cluster, asked ONCE:
                                         * OSAPI_FILE_DFREE does no disk I/O
                                         * and is still ~105 ms on a 20MB hard
                                         * disk, because it counts every FAT
                                         * entry */

/* The live geometry (WEAVE-SPEC 7.1), derived every paint from the window and
 * never from a screen constant (SPEC.md 39). */
static struct os88_pt   w_org;
static struct os88_size w_sz;
static int w_ox, w_oy;                  /* the content origin, x rounded UP */
static int w_cw, w_ch;                  /* CW x CH, in cells and 8px rows */

/* The flow walk's output (WEAVE-SPEC 7.2), and the ONE table the painter and
 * the hit test both read - so the drawn control and the clickable control
 * cannot drift (apps/calc/calc.asm's cal_layout rule, SPEC.md 22). */
#define W_MAXROW W_MAXLAY
struct w_lay {
    unsigned props;                     /* PROPS block offset, 0xFFFF = none */
    unsigned char cx, cy;               /* cell column, cell row */
    unsigned char cw, ch;               /* cells wide, rows high */
    unsigned char id;                   /* comp_id (2.5) */
    unsigned char ctype;
    unsigned char style;
    unsigned char cflags;
    unsigned char row;                  /* which layout row it landed in */
};
static struct w_lay w_lay[W_MAXLAY];
static int w_nlay, w_nrow;
static int w_lay_cw, w_lay_ch, w_lay_card;   /* what the table was built for */

/* --- the VM's claim and the things that hang off it (WEAVE-SPEC 4) ------- */
static unsigned w_vmseg;                /* 0 = no VM is bound */
static int      w_rec[4];               /* one dequeued ring record (4.9) */
static char     w_amsg[WD_AMAX + 2];    /* the alert's line - os88ui.inc holds
                                         * the POINTER and letters from it when
                                         * the alert paints, so it has to
                                         * outlive the call */
static int      w_alertfn = -1;         /* 8.2's callback, -1 = none */
static int      w_alertkind;            /* 0 = alert(), 1 = 4.11's runaway */
static int      w_gsel_r = 1;           /* 6.9's selection - 1,1 until wave 4 */
static int      w_gsel_c = 1;           /*   owns a cell store to move it over */
static struct os88_place w_savhere2;    /* 19.9's walk, one step at a time */

/* ============================================================================
 * THE FORWARD DECLARATIONS
 *
 * `nasm -f bin` has no notion of an external symbol, so a C package is ONE
 * compilation (SPEC.md 73.1) and the parts below are #included into this file
 * in dependency order. Six of them are MUTUALLY dependent - a click enqueues
 * an event, a handler repaints a component, a builtin writes a file, a
 * refusal reloads - and no ordering makes all of that forward-free. These are
 * the ones that cross backwards, and the list is deliberately short: a name
 * here is a seam between two files, and a long list would mean the split is
 * in the wrong place.
 * ==========================================================================*/
static void w_repaint2(void *win, int clear);
static void w_reload(void *win);
static int  w_samename(const char *a, const char *b);
static void w_infield(int i, int x1, int y1, int x2, int y2, int dis);
static void w_press(int i, int x, int y);
static int  w_radio_holder(int i);
static int  w_iblk(int id);
static void w_enq(int comp, int atom, int d1, int d2);
static void w_kick(void);
static void w_gcmark(void);
static void w_touch(int id);
static void w_arm(void);
static void w_caret(void *win);
static int  w_menu_fn(int mi, int ii);
static int  ovl_savestate(void);
static int  ovl_loadstate(void);
static void w_saysav(const char *s);
static void ovl_menubuild(void *win);
static void ovl_comp_init(void);
static void ovl_iload(int id, unsigned props);
static void ovl_vmstart(void *win);
static void w_ialloc(int id, int cols);
static void w_istate(void);
static int  w_key(void *win, int ascii, int scan);
static void w_release(int x, int y);
static void w_wake(void);
static void w_ontimer_body(void *win);
static void w_alertdone(int button);

/* ============================================================================
 * SMALL HELPERS
 * ==========================================================================*/

static void w_l0(void)
{
    w_line[0] = 0;
}

static void w_ls(const char *s)
{
    unsigned n;

    n = os88_strlen(w_line);
    os88_strcpy(w_line + n, s, sizeof(w_line) - n);
}

static void w_ln(unsigned v)
{
    os88_utoa(v, w_num);
    w_ls(w_num);
}

/* w_say - put a sentence on the glass and in the toast, and KEEP it.
 *
 * WEAVE-SPEC 10.1's shape, which is C64-SPEC 1.4's: the window comes up, the
 * sentence is in the content area, the status row keeps it, and the toast
 * fires too - never a bare failed launch. */
/* w_saysav - 8.3's refusal, which is a TOAST and not the status row: a card
 * owns the whole content area (7.1.1 gives the family no status strip), so a
 * sentence drawn there would land on the app's last row. It is kept in
 * w_status for the overlay's diagnostics all the same. */
static void w_saysav(const char *s)
{
    os88_strcpy(w_serr, s, sizeof(w_serr));
    os88_toast("State refused - see Info", 0);   /* 24 -> clipped to 23, and
                                                  * the word that matters is
                                                  * the first. TOAST_MAX is 23
                                                  * (SPEC.md 59.8) and these
                                                  * sentences are 40+; the
                                                  * status string keeps the
                                                  * whole of it for Bundle
                                                  * Info, the way a script
                                                  * error does */
}

static void w_say(const char *s)
{
    os88_strcpy(w_msg, s, sizeof(w_msg));
    os88_strcpy(w_status, s, sizeof(w_status));
    os88_toast(s, 0);
}

/* ============================================================================
 * THE PARTS.  Order is the compiler's: a thing is declared before it is used,
 * and there is no second translation unit to hold a prototype for it.
 * ==========================================================================*/

/* --- the menus (SPEC.md 12.2) -------------------------------------------
 * NOT `const`: os88_menu_set() patches the set's oncmd field with the
 * runtime's command trampoline, and a set in .rodata takes the patch as
 * silent nonsense. The items array is not const either, because Reload greys
 * ITSELF when there is nothing to reload - SPEC.md 47's rule that a control
 * states a FACT rather than refusing after the click. */
/* --- the menus (SPEC.md 12.2, WEAVE-SPEC 6.11) ---------------------------
 *
 * ONE MENU OF OUR OWN, AND THE ARITHMETIC IS WHY. `MENU_APPMAX` is 5 - the
 * kernel's own bar bound - and WEAVE-SPEC 3.2 lets a bundle declare 5. Wave 2
 * spent two of the five on File and Bundle, which would have left an app
 * three. Folding WEAVE's three commands into one pull-down named for the
 * program leaves FOUR, which is as close as the bar gets; a bundle that
 * declares a fifth gets its first four and a toast that says so, because a
 * menu that is silently absent is a command the user cannot find and cannot
 * ask about. 6.11 is amended to carry that.
 *
 * NOT `const`: os88_menu_set() patches the set's oncmd field with the
 * runtime's command trampoline, and a set in .rodata takes the patch as
 * silent nonsense. The items are not const either, because Reload and Bundle
 * Info grey THEMSELVES when there is nothing to reload - SPEC.md 47's rule
 * that a control states a FACT rather than refusing after the click. */
#define W_CMD_OPEN   0
#define W_CMD_RELOAD 1
#define W_CMD_INFO   2

#define W_APPMENUS   4          /* MENU_APPMAX 5, less the one that is ours */
#define W_APPITEMS   8          /* 2.6.2's own per-menu bound */
#define W_TITLEMAX   9          /* 3.3: a menu title is <= 8 characters */
#define W_LABELMAX  26          /* ...and an item label <= 24 glyphs, plus the
                                 * leading OS88_MENU_DIS byte a greyed one
                                 * would carry, plus the NUL */

static const char *w_wv_items[3] = { "Open Bundle...", "\001Reload  ^R",
                                     "\001Bundle Info" };
static char        w_mtitle[W_APPMENUS][W_TITLEMAX];
static char        w_mlabel[W_APPMENUS][W_APPITEMS][W_LABELMAX];
static const char *w_mitems[W_APPMENUS][W_APPITEMS];

static struct os88_menuset w_menus = {
    "Weave", 0, 1,
    { { "Bundle", w_wv_items, 3 } }
};

/* w_menusync - the two items that are only meaningful with a bundle open.
 * A leading OS88_MENU_DIS byte is what draws an item disabled (SPEC.md 47),
 * and the kernel reads these strings each time the menu drops, so swapping
 * the pointer is the whole of it. */
static void w_menusync(void)
{
    if (w_state == W_ST_RUN) {
        w_wv_items[1] = "Reload  ^R";
        w_wv_items[2] = "Bundle Info";
    } else {
        w_wv_items[1] = "\001Reload  ^R";
        w_wv_items[2] = "\001Bundle Info";
    }
}

#include "wval.c"                       /* the hostile-bundle reader */
#include "wflow.c"                       /* the flow walk */
#include "wpaint.c"                      /* ...and what it hands the cores */
#include "wact.c"                        /* the press, the release, the field */
#include "wevent.c"                      /* the ring, the slices, the errors */
#include "wnative.c"                     /* the component and builtin surface */
#include "wstate.c"                      /* 8.3's .SAV, the only file surface */
#include "wload.c"                       /* the accept idiom and the refusals */
#include "wovl.c"                        /* WEAVE.OVL's tenants */

/* ============================================================================
 * PAINTING
 *
 * Three pictures, one entry. A bundle that loaded gets its CARD - every
 * component of WEAVE-SPEC 6, at the rect the flow walk produced, drawn by
 * wpaint.c through the assembly cores in wdraw.inc. A bundle that was refused
 * gets section 10's sentence. Nothing open gets 1.6's Deck.
 *
 * Every line of the last two is ONE os88_font_run(): the cells' background and
 * their glyphs in a single pass (SPEC.md 6.1), so a line is never momentarily
 * blank. The erase-then-letter pair is the canonical double-draw in this tree
 * and it is invisible in an emulator. The pen is w_ox, which 7.1.2 rounded up
 * to a multiple of 8 at LAYOUT time, so every one of these takes font_run's
 * single-store fast path on both 1bpp adapters.
 * ==========================================================================*/

/* w_row - one cell row of the content, by row index.  Rows below CH are not
 * drawn at all: 7.4's degradation is CLIPPING against the content box, never
 * reflowing below the minimum. */
static void w_row(int r, const char *s)
{
    if (r < 0 || r >= w_ch)
        return;
    os88_font_run(w_ox, w_oy + 8 * r, s, OS88_BLACK, OS88_WHITE);
}

/* w_wipe - the content area, white.  ONE fill, not a padded run per row:
 * padding every line to CW would cost a 79-cell font_run where a 34-cell one
 * would do - about 71 ms against 30 on the target - and this way the price is
 * one ~756 us primitive on a path the user's own action started, which
 * PERFORMANCE.md calls visible and honest. The kernel does this for us before
 * W_PAINT; every other caller owes it, because os88_font_run() letters exactly
 * the cells it is given and a shorter line leaves the tail of the longer one
 * it replaced on the glass. */
static void w_wipe(void)
{
    os88_set_color(OS88_WHITE);
    os88_gfx_fill(w_org.x, w_org.y, w_org.x + w_sz.w - 1,
                  w_org.y + w_sz.h - 1);
}

/* w_ctname - 2.5.1's ctype, spelled.  Resident because the LITERALS are
 * resident whatever this function is called from (SPEC.md 73.14), so putting
 * the switch in the overlay would move nine bytes of code and no strings. */
static const char *w_ctname(unsigned ct)
{
    switch (ct) {
    case WC_LABEL:  return "label";
    case WC_TEXT:   return "text";
    case WC_RULE:   return "rule";
    case WC_BOX:    return "box";
    case WC_SPACER: return "spacer";
    case WC_METER:  return "meter";
    case WC_BUTTON: return "button";
    case WC_CHECK:  return "check";
    case WC_RADIO:  return "radio";
    case WC_INPUT:  return "input";
    case WC_LIST:   return "list";
    case WC_GRID:   return "grid";
    case WC_CANVAS: return "canvas";
    case WC_SPRITE: return "sprite";
    default:        return "?";
    }
}

static void w_paint_deck(void)
{
    /* WEAVE-SPEC 1.6's Deck is normative from wave 1 and lands here; wave 2
     * ships File -> Open only, which 1.6 explicitly allows. The sentence says
     * what to do rather than what went wrong, because nothing did. */
    w_row(0, "WEAVE - no bundle open.");
    w_row(2, "File > Open Bundle... opens a .WAB, and a double-click on one");
    w_row(3, "opens it directly (the extension is declared in the header).");
}

/* w_repaint - draw the content.
 *
 * `clear` is the difference between the two kinds of caller, and it is a real
 * difference rather than a flag for tidiness. W_PAINT arrives with the content
 * ALREADY WHITENED by the kernel (that is what OSAPI_WM_OWNBG opts out of), so
 * clearing there would be the erase-then-letter double-draw over the whole
 * box. Every other caller - a menu command, the file dialog's completion, a
 * reload - is drawing over a content area that still holds the LAST state, and
 * os88_font_run() letters exactly the cells it is given: a shorter line leaves
 * the tail of the longer one it replaced on the glass. That was observed here
 * as `(ctype).1).`, the tail of an `(atom pool).` refusal, which reads as a
 * bug in the validator and is a stale-pixel defect (tests/covl/covl.c's own
 * lesson, and it is why a gate is read as an example).
 *
 * ONE fill, not a padded run per row: padding every line to CW would cost a
 * 79-cell font_run per row where a 34-cell one would do - about 71 ms against
 * 30 on the target - and this way the price is one ~756 us primitive on a path
 * the user's own action started, which PERFORMANCE.md calls visible and
 * honest. */
static void w_repaint2(void *win, int clear)
{
    if (!w_layout(win))
        return;                         /* not visible: nothing to draw */

    if (clear)
        w_wipe();                       /* over the LAST state - see w_wipe */

    if (w_state == W_ST_RUN) {
        w_paint_card(win);
        return;                         /* THE CARD OWNS THE CONTENT AREA.
                                         * 7.1.1's chrome has no status strip
                                         * and WEAVE adds no inset of its own
                                         * (7.1), so there is no row left to
                                         * put w_status in without drawing over
                                         * a component - and a sentence that
                                         * lands on top of the app's last row
                                         * is worse than one the toast already
                                         * showed. It is kept for the two
                                         * states below, which have the whole
                                         * box to themselves */
    }

    if (w_state == W_ST_ERR) {
        w_row(0, w_msg);
        w_row(2, "File > Open Bundle... tries another one.");
    } else
        w_paint_deck();

    /* The status row: the bottom row of the content grid, painted only when
     * there is something in it. WEAVE-SPEC 10.1 asks for a row that KEEPS the
     * sentence after the toast retires itself. */
    if (w_status[0])
        w_row(w_ch - 1, w_status);
}

static void w_repaint(void *win)
{
    w_repaint2(win, 0);                 /* the W_PAINT case: already whitened */
}

/* ============================================================================
 * THE CALLBACKS
 * ==========================================================================*/

void os88_paint(void *win)
{
    /* SPEND THE BANKED LAUNCH DOCUMENT FIRST, and then branch on the state -
     * never the other way round. SPEC.md 54.8's third trap is that loading is
     * not showing: a paint that tests its state before the load reads the
     * state the load was about to change, and the bundle then loads perfectly
     * and is covered by the Deck, with no error anywhere.
     *
     * It happens HERE and not in the entry proc because opening a bundle
     * DRAWS - the refusal notice, the report, the toast - and the entry proc
     * holds no gfx lock and has no window on the screen yet. The flag makes it
     * happen exactly once. */
    if (w_pendok) {
        w_pendok = 0;
        w_openpend(win);
    }
    w_repaint(win);
}

/* W_ONRESIZE (SPEC.md 11.98): the content box changed and we did not ask - an
 * adapter change under us, or a drag across the seam onto a shorter display.
 * We MUST NOT DRAW here; a full repaint follows immediately. So this is the
 * chance to be laid out correctly BEFORE that paint runs rather than a frame
 * later, which is exactly what WEAVE-SPEC 7.4 asks for: a resize re-runs the
 * walk and repaints the card once. */
void os88_onresize(int w, int h, void *win)
{
    (void)w;
    (void)h;
    w_layout(win);                      /* the walk and the rect table, both -
                                         * and w_layout draws nothing, which is
                                         * this callback's one hard rule */
}

/* W_ONCLICK - the press, and it is the HIT TEST's one caller.
 *
 * The table is rebuilt first, every time: the window may have moved since the
 * last paint, and a hit test against yesterday's pixels finds the wrong
 * control or none (apps/calc/calc.asm's cal_onclick, verbatim).
 *
 * SPEC.md 47 rule 4 - ONE predicate, three consumers - is the CF_DISABLED test
 * below: the same fact that greyed the control in w_paint_comp() refuses its
 * click here, rather than a second copy that can disagree with the picture. */
void os88_onclick(int x, int y, void *win)
{
    int i;

    if (w_state != W_ST_RUN)
        return;
    if (!w_layout(win))
        return;
    i = wd_hit(w_rect, w_nlay, x, y);
    if (i == 0)
        return;
    i--;
    if (w_lay[i].cflags & (CF_HIDDEN | CF_DISABLED))
        return;
    w_onhit(i, x, y);
}

void os88_onkey(int ascii, int scan, void *win)
{
    if (w_state == W_ST_RUN) {
        w_key(win, ascii, scan);        /* wact.c: ^R, Tab, then the armed
                                         * field (6.7, 1.7) */
        return;
    }
    if (ascii == 0x12) {                /* 1.7's shortcut still works with no
                                         * bundle open, because Reload is what
                                         * a refused one wants next */
        w_reload(win);
        w_repaint2(win, 1);
    }
}

/* W_ONMOUSEUP - the release half of SPEC.md 13.7's gesture, and the ONLY
 * place a button fires. The kernel guarantees exactly one of these per
 * W_ONCLICK, which is what makes os88ui_fire's clear-on-read safe. */
void os88_onmouseup(int x, int y, void *win)
{
    if (w_state != W_ST_RUN)
        return;
    if (!w_layout(win))
        return;
    w_release(x, y);
}

/* W_ONTIMER (SPEC.md 13.9) - the caret's blink and 8.2's timer(), multiplexed
 * over the one one-shot a window gets. wevent.c's w_ontimer_body says why. */
void os88_ontimer(void *win)
{
    if (w_state == W_ST_RUN)
        w_ontimer_body(win);
}

/* W_ONWAKE (SPEC.md 74.1) - the ONE callback that runs WITHOUT the gfx lock,
 * and therefore the only place a slice may run (WEAVE-SPEC 4.10). */
void os88_onwake(void *win)
{
    (void)win;
    w_wake();
}

void os88_oncmd(int item, int menu, void *win)
{
    /* MENU 0 IS OURS AND EVERY OTHER ONE IS THE BUNDLE'S (WEAVE-SPEC 6.11).
     * The kernel numbers an app's menus from 0 in the order the set declares
     * them, and the set is built by w_menubuild: ours first, then up to four
     * of the bundle's. So the app's menu k is at index k, 1-based already -
     * which is the number 2.6.2's blob and 3.4's oncommand record are both
     * written in, and it is why the record's data1 needs no adjustment and
     * its data2 needs exactly one. */
    if (menu > 0) {
        w_enq(0, WA_ONCOMMAND, menu, item + 1);
        return;
    }
    if (item == W_CMD_OPEN) {
        os88_file_dlg(OS88_FDLG_OPEN, win, 0);
        return;
    }
    if (item == W_CMD_RELOAD && w_state == W_ST_RUN) {
        w_reload(win);
        w_repaint2(win, 1);
        return;
    }
    if (item == W_CMD_INFO && w_state == W_ST_RUN)
        ovl_info();                     /* the overlay: a menu command may
                                         * refuse, which is what makes it the
                                         * canonical tenant (SPEC.md 73.14).
                                         *
                                         * NOTHING REPAINTS AFTER IT. It draws
                                         * the layout listing itself, over the
                                         * card, and the next W_PAINT brings
                                         * the card back - which is what a
                                         * diagnostic view should do and is the
                                         * only shape available: a listing the
                                         * PAINTER redrew would put an overlay
                                         * call on the paint path, and an
                                         * overlay call may refuse (73.14). A
                                         * refused load here draws nothing at
                                         * all and leaves the card alone, which
                                         * is the honest degradation */
}

/* W_ONFILE: the Standard File dialog's completion (SPEC.md 38). THE SECOND WAY
 * IN, and it enters the ONE load path at the same place the association route
 * does - Frotz's "two ways in, one decision" rule. The size arrives free here,
 * read out of the mount snapshot by the dialog, which is exactly what
 * WEAVE-SPEC 10.1 wants: refuse before touching the disk. */
void os88_onfile(int mode, const char *name, unsigned size_lo, unsigned size_hi,
                 void *win)
{
    (void)mode;
    w_open(win, name, size_lo, size_hi);
    w_repaint2(win, 1);                 /* over the LAST state - see w_repaint2 */
}

void os88_about(void *win)
{
    ovl_about();
    w_repaint2(win, 1);
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
 * by it with no int 0 handler, so a bogus BX hangs the machine with the cursor
 * still moving - and in C it is closed by construction. It is named here
 * anyway, because the next person to write one of these in assembly has to
 * know it is a trap that was closed rather than one that never existed.
 * ==========================================================================*/

void *os88_main(void)
{
    void *win;
    static struct os88_video v;

    /* WEAVE's window chrome is the browser's (WEAVE-SPEC 7.1.1): an ordinary
     * resizable window, no toolbar and no status strip of its own, opened at
     * SPEC.md 11.95's standard rect - the whole desktop band. Every number
     * comes off the LIVE screen; the reference constants are a reference and
     * not a promise, and reading them would be wrong on two adapters of three
     * (SPEC.md 39). */
    os88_video(&v);
    win = os88_wm_create(0, OS88_MBAR_H, v.w,
                         v.dock_top - OS88_MBAR_H - 1, "Weave");
    if (win == 0)
        return 0;                       /* the window table is full: abort */
    w_win = win;

    os88_menu_set(win, &w_menus);
    os88_about_set(win);
    os88_wm_sizable(win, 1);

    /* 7.1.2's whole reason: with the content origin on a multiple of 8, every
     * cell column is too, and every os88_font_run() in the family takes the
     * single-store fast path on both 1bpp adapters. A no-op on VGA, so it is
     * set unconditionally rather than after asking. */
    os88_wm_snap(win, 1);

    /* 7.4's floor, DECLARED rather than negotiated (SPEC.md 11.100.2): the
     * family's minimum content is 32x12 cells, and this slot takes the OUTER
     * frame - content plus the two side borders, and plus the title bar and
     * the line under it. Every path that reduces a size honours it, including
     * the ones no negotiator is consulted by. */
    os88_wm_minsize(win, 32 * 8 + 2, 12 * 8 + OS88_TITLE_H + 1);

    /* ...and TELL US when the box moves without anyone proposing it. */
    os88_wm_onresize(win);

    /* THE THREE THAT MAKE WAVE 3 HAPPEN, and every one of them is a template
     * word the kernel does not fill in for you (os88.h: "call from
     * os88_main() after os88_wm_create(); they are not template words"). A
     * missing install is the quietest failure in this file: the C compiles,
     * the %define in the shim is satisfied, the callback exists - and the
     * kernel simply never calls it. Verified as exactly that: a button drew
     * its down state on the press and stayed down for ever, because
     * W_ONMOUSEUP was never installed and the release reached nobody. */
    os88_wm_onmouseup(win);             /* 13.7's RELEASE - the only place a
                                         * <button> fires (WEAVE-SPEC 6.5) */
    os88_wm_onwake(win);                /* 74.1's kick - the only place a WVM
                                         * slice may run (4.10) */
    os88_wm_ontimer(win);               /* 13.9's one-shot - 6.7's caret and
                                         * 8.2's timer(), multiplexed */

    /* THE LAUNCH DOCUMENT (SPEC.md 54.5). Read-and-clear, so it is banked here
     * and spent in the first paint. os88_arg_file() cannot hand back the name
     * without the locator - they arrive together - which closes half of
     * SPEC.md 54.8's first trap by construction; the other half is spending
     * it, and w_openpend() is where that has to happen. */
    if (os88_arg_file(w_name, &w_place) == 0) {
        w_arghave = 1;
        w_pendok = 1;
    }
    w_menusync();
    return win;
}
