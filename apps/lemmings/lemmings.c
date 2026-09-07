/* ============================================================================
 * os8088 - apps/lemmings/lemmings.c
 *
 * LEMMINGS (SPEC.md 92) - Lemmings (DMA Design / Psygnosis, 1991, the DOS
 * release) as an os8088 C package. This file is the translation unit's ROOT:
 * the constants, the state, the prototypes, the callbacks the shim declares,
 * the empty menu set and the #includes of the rest, in dependency order.
 *
 * ONE TRANSLATION UNIT (SPEC.md 73.1): `nasm -f bin` has no notion of an
 * external symbol, so a C package is one compilation split by CONCERN into
 * #included .c files. MAKE CANNOT SEE THROUGH A #include - every file below is
 * a written prerequisite of $(BUILD)/lemmings.raw.asm in the Makefile, and
 * without that an edit here leaves a stale .o88 that reads exactly like a
 * change having done nothing (LESSONS.md 9).
 *
 * WHAT WAVE 1 IS. The desktop window is a LAUNCHER and nothing else (SPEC.md
 * 92): the four ratings, the level names of the chosen rating PAGED to the live
 * window height, the original's preview fields beside them, Play, four state
 * rows that give SPEC.md 47's greying a control to sit on, and the standard
 * About card. Play shows the level's own preview screen - the original's seven
 * lines in the original's wording - and wave 5 moves exactly that content
 * inside SPEC.md 53's bracket, into the second foreign mode each adapter needs
 * to hold its 640x350 surface. The raster the level itself needs arrives in
 * wave 2 (lemblit.inc, lemmask.inc, lemfont.inc), the game in wave 3, the whole
 * level set in wave 4.
 *
 * WHERE THE WORDS COME FROM. The level names, the preview fields, the rating
 * and style names and every sentence that greys a control are read off the
 * CONVERTED BAND FILES (SPEC.md 92.3, docs/lemband-format.md). This program
 * owns SIX literals and no more, and they are listed at their declaration
 * below; that is what SPEC.md 92.6's resident budget rests on, together with
 * the wave-1 overlay split.
 *
 * ATTRIBUTION. Lemmings is (C) 1991 DMA Design / Psygnosis. Every table, string
 * and offset derived from the original or from the three reimplementations that
 * document it names its source at the line that carries it; the full list is in
 * README.TXT beside the package, and the About card names the two principals
 * and points at it (SPEC.md 92.2). Nothing GPL or LGPL is copied.
 *
 * THE FOUR RULES (apps/cc/os88.h, SPEC.md 73.5, docs/C-TOOLCHAIN.md): every
 * addressable object is static - the address of an automatic is a stack offset
 * resolved through DS, SS != DS here, and tools/cc8086.py refuses it; no struct
 * goes by value, because the copy is a string instruction and ES is the
 * kernel's; no long, float, double, bit-field or anonymous union; and frames
 * stay under 96 bytes.
 *
 * ONE ASSUMPTION RECORDED HERE RATHER THAN GUESSED AT. LEMS_GREY_SAVE, the
 * sentence behind the Save Progress row, names the live CD specifically ("The
 * live CD cannot be written..."), while the predicate this program greys on is
 * the broader one it can actually test: whether SYSTEM/APPDATA answered at all
 * (SPEC.md 19.9, lemovl.c's ovl_progress_read). On a floppy carrying only this
 * package and its bands there is no such folder, so the row greys with a
 * sentence one case narrower than the fact. Closing that is a one-line change
 * to tools/os88lem.py's string table - another agent's file in this wave - and
 * belongs with wave 4's progress work.
 * ==========================================================================*/

#include "os88.h"
#include "lemstr.h"                 /* the generated LEMS_* ids - build/ */

/* --- the window, authored against the 640x480 reference (SPEC.md 39) --------
 * A reference, not a promise: wm_create clamps this onto the live screen and
 * every layout number is read back from os88_wm_geom() rather than assumed. On
 * a CGA the content box is 200 - MBAR_H 20 - DOCK_H 24 - TITLE_H 18 - 1 = 137
 * pixels, and the level list PAGES into whatever it gets (LESSONS.md 8: the
 * Font list ran off the bottom of a 200-line screen because it was sized from
 * its LENGTH). */
#define LEM_WIN_X    40
#define LEM_WIN_Y    40
#define LEM_WIN_W   512
#define LEM_WIN_H   208
#define LEM_MIN_W   288
#define LEM_MIN_H   120

/* --- the shape of the converted data (docs/lemband-format.md) -------------- */
#define LEM_RATINGS      4
#define LEM_PERRAT      30
#define LEM_ENTSZ       64          /* one level entry; the stride is a SHIFT */
#define LEM_RATBUF    2048          /* LEMR<n>.LEM is exactly this on every
                                     * disk and every geometry, so one rating's
                                     * bss is a constant */
#define LEM_STRBUF    8192          /* LEMSTR.LEM is 4,096 today - 4,089 bytes
                                     * of table in a 512-padded band - and is
                                     * read WHOLE; it is the same on every
                                     * geometry (tools/os88lem.py).
                                     *
                                     * TWICE THE BAND AND NOT ONE STEP ABOVE IT.
                                     * The converter pads to 512, so the next
                                     * size this file can ever take is 4,608 -
                                     * and lem_str_load() REFUSES a read that
                                     * filled the buffer, because a band that
                                     * filled it may have been truncated and a
                                     * truncated directory indexes strings that
                                     * are not there. A buffer of 4,096 was what
                                     * this had, and this wave's own two
                                     * per-adapter Mode facts plus the Psygnosis
                                     * copyright took the band from 3,584 to
                                     * 4,096 - the very step that would have
                                     * taken lem_str_load() to 0, lem_data_load()
                                     * to 0, and every disk in every geometry to
                                     * a launcher with no level names on it and
                                     * the sentence "The converted level data is
                                     * not in this folder.", which names the
                                     * wrong cause: the bands are all there. bss
                                     * is the cheap half of this budget (SPEC.md
                                     * 92.6) and 4,096 of it buys a real
                                     * ceiling: seven more rows of the size of
                                     * the ones just added. tools/os88lem.py
                                     * asserts the SAME 8,192 beside
                                     * build_string_band(), so the day it is
                                     * reached the CONVERTER fails by name on
                                     * the host rather than the shipped package
                                     * failing on the glass. */

#define LEM_BANK_STYLE   0          /* which half of LEMMAN's cost table */
#define LEM_BANK_SPEC    1

/* --- the glass shadow (LESSONS.md 6: design it in wave 1, not as polish) ----
 * One character and one attribute per cell. 64 columns is 512 pixels, which is
 * the window this launcher is authored at; a wider window is laid out inside
 * 64 cells rather than growing two 1,280-byte arrays, because bss is cheap but
 * it is not free and SPEC.md 92.6's budget is 81% spent before this file
 * starts. The stride is a power of two so `row * stride` is a SHIFT and never
 * an imul the gate has to find a dead scratch register for (LESSONS.md 3). */
#define LEM_SH_COLS     64
#define LEM_SH_ROWS     20
#define LEM_SH_SHIFT     6          /* 1 << 6 == LEM_SH_COLS */

/* The cell attributes. Two are a font_run ink/paper pair; LEM_AT_GREY is the
 * DISABLED PEN, which is a colour AND a flag, and on the two 1bpp adapters the
 * flag is the entire difference (SPEC.md 47.2, 39.4).
 *
 * LEM_AT_TEXT IS ZERO AND THAT IS LOAD-BEARING. The kernel white-fills the
 * WHOLE content before W_PAINT unless a window took WF_OWNBG (kernel/wm.inc's
 * own comment at the interlock, SPEC.md 11.90.2), so after a paint the glass is
 * exactly "spaces, black on white" - which is what lem_sh_blank() seeds the
 * shadow with, and which is why a full repaint draws only the cells that carry
 * something. A blank cell costs nothing at all rather than ~900 us. */
#define LEM_AT_TEXT     0           /* black on white - and the background */
#define LEM_AT_SEL      1           /* white on black - the selection */
#define LEM_AT_GREY     2           /* the disabled pen */

/* THERE IS NO LIGHT-GREY ATTRIBUTE, and there was: the tab strip drew its
 * three unselected ratings as black on OS88_LGRAY, which looks like a tab on a
 * VGA and is THREE SOLID BLACK RECTANGLES on CGA and Hercules - the paper
 * rounds to black there, and so does the ink, so the words were not on the
 * glass at all (build/port-shots/wave1-cga-launcher.png, before this). SPEC.md
 * 39.4 says to look at a drawing change on a 1bpp adapter before calling it
 * done, and this is what that looks like when you do. The selection is inverse
 * video, which is the distinction that has to survive, and it survives on all
 * three adapters; an unselected tab is ordinary text. */

/* The launcher's rows that are not level rows. Their order is their id. */
#define LEM_ROW_MODE    0
#define LEM_ROW_MUSIC   1
#define LEM_ROW_CODE    2
#define LEM_ROW_SAVE    3
#define LEM_STATE_ROWS  4

/* The first list row. Row 0 is the tab strip and row 1 is the gap under it. */
#define LEM_LISTTOP     2

/* The three screens this wave has. */
#define LEM_SC_LIST     0
#define LEM_SC_PREVIEW  1           /* the level's own preview, wave 5 moves it
                                     * inside the bracket (SPEC.md 92.4.3) */
#define LEM_SC_FACT     2           /* the sentence behind a greyed control */

/* ------------------------------------------------------------------------- */
/* Prototypes for everything. They are here rather than scattered because the
 * HOST HARNESS compiles this same C with clang, which is stricter about
 * prototypes than SmallerC is: `call to undeclared function` and `static
 * declaration follows non-static declaration` are both errors there and neither
 * is one here (LESSONS.md 3). */

/* lemtab.c */
static int  lem_state_fact(int row);
static int  lem_state_label(int row);
static int  lem_style_name(int style);

/* lemtext.c */
static int  lem_u16(const unsigned char *p, int off);
static int  lem_u8(const unsigned char *p, int off);
static void lem_u32dec(const unsigned char *p, int off, char *dst, unsigned cap);
static const char *lem_str(int id);
static int  lem_str_load(void);
static void lem_arg_reset(void);
static void lem_arg_add(const char *s);
static char *lem_arg_slot(void);
static void lem_arg_num(unsigned v);
static void lem_arg_pct(unsigned num, unsigned den);
static void lem_arg_u32(const unsigned char *buf, int off);
static void lem_fmt(char *dst, unsigned cap, const char *fmt);
static void lem_para_fmt(const char *fmt);
static void lem_setn(char *dst, unsigned cap, unsigned v);
static void lem_fit(char *dst, unsigned cap, const char *src, int cols);
static const char *lem_trim(const char *s);
static void lem_para_reset(void);
static void lem_para_add(const char *s);
static void lem_para_nl(void);
static void lem_wrap(void);
static const char *lem_wrap_line(int i);

/* lemload.c */
static int  lem_man_u16(int off);
static int  lem_man_u8(int off);
static const char *lem_geom(void);
static int  lem_cost_row(int kind, int n);
static int  lem_cost_clus(int kind, int n);
static int  lem_cost_parts(int kind, int n);
static const char *lem_bandname(int kind, int n);
static const unsigned char *lem_entry(int row);
static int  lem_ent_u8(int row, int off);
static int  lem_ent_u16(int row, int off);
static const char *lem_ent_name(int row);
static int  lem_ent_here(int row);
static int  lem_rating_load(int rating);
static int  lem_data_load(void);

/* lemui.c */
static int  lem_rowy(int row);
static int  lem_colx(int col);
static void lem_sh_blank(void);
static void lem_layout(void *win);
static void lem_repaint(void *win, int full, int arm);
static void lem_pv_defer(void *win);
static void lem_select(void *win, int row);
static void lem_set_rating(void *win, int rating);
static int  lem_hit(int x, int y);
static int  lem_state_grey(int row);
static int  lem_play_ok(void);
static void lem_show_fact(void *win, int labelid, int strid, int level);
static void lem_back(void *win);
static void lem_play(void *win);

/* lemovl.c - everything in LEMMINGS.OVL (SPEC.md 73.14, 92.6) */
static int  ovl_ready(void);
static void ovl_chrome(int screen);
static void ovl_fmt(int line, int level, char *dst, unsigned cap);
static void ovl_preview(int level);
static int  ovl_greyfact(int labelid, int strid, int level);
static void ovl_about(void *win);
static void ovl_data_leave(void);
static int  ovl_progress_read(void);
static int  ovl_progress_write(int rating, int level);

/* this file */
static int  lem_probe_mode(void *win);
static void lem_first_wake(void);
static void lem_kick(void *win);
static void lem_abdismiss(void *win);

/* ------------------------------------------------------------------------- */
/* State. All static: see the four rules in the header above. */

static void *lem_win;
static int   lem_screen;            /* LEM_SC_* */
static int   lem_rating;            /* 0..3 */
static int   lem_sel;               /* 0..29 WITHIN the rating, never 0..119:
                                     * the launcher holds one rating at a time
                                     * and that is what makes LEMR<n>.LEM the
                                     * file (docs/lemband-format.md) */
static int   lem_top;               /* first level row shown in the list */
static int   lem_rows;              /* text rows that FIT right now */
static int   lem_cols;              /* content width in 8px cells, capped */
static int   lem_nlist;             /* level rows on the page */
static int   lem_listw;             /* the left pane's width in cells */
static int   lem_pvx, lem_pvw;      /* the preview pane's column and width */
static int   lem_playrow;           /* the Play control's row */
static int   lem_staterow;          /* the first state row */
static int   lem_playw = 8;         /* ...and the Play control's width, written
                                     * by the compose and read by the hit test
                                     * so the two cannot drift */
static int   lem_tabx[LEM_RATINGS];
static int   lem_tabw[LEM_RATINGS];
static int   lem_ok;                /* the band files were read */
static int   lem_ovl;               /* 0 not asked, 1 resident, -1 refused */
static int   lem_savable;           /* SYSTEM/APPDATA answered */
static int   lem_prog[LEM_RATINGS]; /* furthest level reached per rating, -1 */
static int   lem_mode_ok;           /* this display can set 320x200x16 */
static int   lem_vidkind;           /* OS88_VID_* */
static int   lem_snd;               /* the speaker path answered */
static int   lem_abon;              /* the About card is up */
static int   lem_waked;             /* the first wake has happened */
static int   lem_progread;          /* ...and the progress file has been read */
static int   lem_ratpend;           /* a RATING LOAD is owed to os88_onwake:
                                     * LEMR<n>.LEM is a whole-file read and the
                                     * key that asks for it is delivered with
                                     * the gfx lock HELD (lemui.c
                                     * lem_set_rating) */
static int   lem_list_hold;         /* ...and while it is owed, the level rows
                                     * are left exactly as the shadow has them:
                                     * blanking them would be ~260 glyph cells
                                     * on an autorepeating key and then letter
                                     * the same rows again a moment later */
static int   lem_progwrite;         /* a progress WRITE is owed, for the same
                                     * reason one rating load is */
static int   lem_pw_rat;            /* ...and what it was asked to record, taken
                                     * at the keystroke rather than read back at
                                     * the wake, where the rating may have moved
                                     * on */
static int   lem_pw_lvl;
static int   lem_gutc;              /* the GUTTER column between the two panes,
                                     * or -1. No composed run may cross it, so
                                     * the cell is always blank, always matches
                                     * the shadow and is NEVER drawn - which is
                                     * what lets ovl_chrome() put the vertical
                                     * rule inside it and never have it rubbed
                                     * out (lemui.c lem_brk) */
static int   lem_pv_hold;           /* compose the level rows but LEAVE the
                                     * preview pane as the shadow has it */
static int   lem_pv_due;            /* ...and a settle timer is pending for it */

static struct os88_pt   lem_org;
static struct os88_size lem_sz;

/* the shadow */
static char          lem_sh[LEM_SH_COLS * LEM_SH_ROWS];
static unsigned char lem_sha[LEM_SH_COLS * LEM_SH_ROWS];
static int           lem_sh_ok;     /* it describes the glass */

/* scratch - static, and SHARED, so nothing here may be live across a call that
 * also uses it (LESSONS.md 13's shared-scratch trap, one level down) */
static char lem_line[LEM_SH_COLS + 1];
static char lem_num[8];
static char lem_num2[8];

/* the About card's line array. RESIDENT, because only CODE moves to the module
 * (SPEC.md 73.14) - and because os88_paint() redraws the card with
 * os88_about_card_d() without entering the module again. */
static const char *lem_ab[12];
/* 48 and not 40: the port line is 80 characters and halving it gives a
 * 41-character second half, which a 40-byte buffer cut to "...own data file"
 * on the glass (build/port-shots/wave1-vga-about.png, before this). 48 cells
 * is 384 pixels and the card clamps to the live content box, so it still fits
 * the narrow one. */
static char lem_ab1[48];
static char lem_ab2[48];

/* the cost counters the host harness prints on every build (LESSONS.md 6). A
 * visible redraw and a double-draw flash are two of the three defects that
 * never show in a screendump; this table is how they are seen at all. */
static int lem_c_calls;             /* drawing calls this repaint */
static int lem_c_cells;             /* glyph cells this repaint */

/* THE SIX LITERALS THIS PROGRAM OWNS. Everything else the user reads is in
 * LEMSTR.LEM (SPEC.md 92.3, 92.6). These six cannot be:
 *   - the window TITLE has to outlive the window (os88.h's own rule) and so
 *     cannot point into a buffer anything reloads;
 *   - the no-data refusal is what is said when the BAND could not be read;
 *   - the About card's first row is the house form, "<product> for os8088",
 *     which apps/weave/weave.c also spells out rather than composing;
 *   ...and lemui.c owns three more of the same kind - the adapter and mode
 *   names on the Mode row, which are numbers about THIS BUILD and this machine
 *   rather than anything of the original's. */
/* ...and the TITLE is the band's LEMS_PROD where the band answered, which is
 * what the generated header is for: "a renamed string is a compile error here,
 * not a wrong sentence on the glass" (build/lemstr.h). This literal is the
 * FALLBACK - a disk whose band could not be read still gets a named window -
 * and lem_name below is what the window and the menu set are actually given.
 * It is safe to point them at the band: lem_strbuf is static bss, read once in
 * os88_main() and never reloaded, so it outlives the window (os88.h's rule). */
static const char lem_title[] = "Lemmings";
static const char *lem_name = lem_title;
static const char lem_prodline[] = "Lemmings for os8088";
static const char lem_nodata[] =
    "The converted level data is not in this folder.";

/* THE MENU SET IS EMPTY (SPEC.md 12.2, 73.12; apps/word's idiom). A package's
 * header name is 15 characters and is also the .OVL stem and the association,
 * so it cannot say what the product is called; an empty set whose AM_NAME is
 * the product puts "Lemmings" in the kernel bar instead of the header's
 * "LEMMINGS". A set with no menus has no item to pick, so os88_oncmd() below
 * can never be called - but CC_HAS_MENUS has to be declared anyway, because
 * os88_menu_set() is the slot that patches the trampoline in.
 *
 * NOT const: os88_menu_set() WRITES set->oncmd, and a set in .rodata makes that
 * patch silent nonsense (os88.h says so at the struct). */
static struct os88_menuset lem_menus = { lem_title, 0, 0, { { 0, 0, 0 } } };

/* --- the parts of the program, in dependency order -------------------------
 * Every one of these is a written prerequisite of $(BUILD)/lemmings.raw.asm.
 * lemovl.c is LAST because it names what the four above it define, and because
 * that is where the module boundary falls in the generated assembly. */
#include "lemtab.c"                 /* const tables and field offsets only */
#include "lemtext.c"                /* the string ACCESSOR, not the strings */
#include "lemload.c"                /* the band reader, resident half */
#include "lemui.c"                  /* the launcher */
#include "lemovl.c"                 /* everything ovl_* -> LEMMINGS.OVL */

/* WAVE 2 AND 3 ADD, in this order and between lemload.c and lemui.c:
 *   #include "lemgame.c"           the per-tick step
 *   #include "lemact.c"            the eighteen action handlers
 *   #include "lemobj.c"            objects and triggers
 *   #include "lemdraw.c"           the frame
 * ...each with its own prerequisite line in the Makefile before it exists. */

/* ============================================================================
 * The two things this file decides for itself
 * ==========================================================================*/

/* lem_probe_mode - which foreign mode this DISPLAY could play in, asked with
 * OUR window.
 *
 * kernel/fsx.inc's own comment records the one mistake here: a package that
 * asked before its window existed was told about the display it was LAUNCHED
 * from. So this runs after os88_wm_create() and takes lem_win.
 *
 * THE BIT THAT GREYS THE MODE ROW IS THE BIT os88_fsx_mode() WOULD REFUSE ON,
 * which is SPEC.md 47 rule 4's one predicate for both. An EGA answers 0x000F -
 * the CGA-compatible modes only (kernel/fsx.inc:117) - so a sixteen-colour card
 * gets four colours and the row says so with the fact. */
static int lem_probe_mode(void *win)
{
    static unsigned char kind;
    int caps;

    kind = OS88_VID_VGA;
    caps = os88_fsx_caps(win, &kind);
    lem_vidkind = (int)kind;
    if (caps < 0)
        return 0;
    return (caps & (1 << OS88_FSXM_VGA0D)) != 0;
}

/* lem_first_wake - THE MODULE, and nothing else.
 *
 * THE MODULE IS FORCED RESIDENT HERE. There is no instance to resolve a module
 * for while the entry proc runs (SPEC.md 73.14; RunCPM paid for this one,
 * LESSONS.md 13), and cc_ovneed TOASTS ITS OWN REFUSAL - a kernel drawing call
 * this C never wrote and cannot intercept. In a window that toast is an
 * ordinary thing; inside wave 2's bracket it would be painted with DESKTOP
 * geometry into a foreign mode (SPEC.md 92.5). So it happens once, here, with
 * the window up, and lem_ovl remembers the answer so no later call can reach
 * that path again.
 *
 * IT DOES NOT TOAST A REASON OF ITS OWN, and the version that did said the
 * WRONG one: cc_ovneed has already toasted 'LEMMINGS.OVL is not on this disk',
 * 'does not match this program' or 'Not enough memory for LEMMINGS.OVL'
 * (crt0.asm:1157-1159) - the true cause, with the file named - and a second
 * toast RETIRES the first (SPEC.md 59), replacing an accurate sentence about
 * the module with an inaccurate one about the band files, which are all
 * present. SPEC.md 47's "grey a fact, never a guess" in its toast form.
 *
 * IT IS NOT CALLED FROM W_PAINT AND IT USED TO BE. ovl_ready() is cc_ovneed:
 * an OSAPI_MEM_CLAIM, a directory walk of a 39-entry folder and a 2,752-byte
 * read - unbounded floppy I/O, between the kernel's own gfx_lock and
 * gfx_unlock, on the FIRST paint of every launch, with every other window's
 * painter stopped behind the lock. That is hundreds of milliseconds to a couple
 * of seconds of frozen desktop on the target (PERFORMANCE.md: ~400 ms an
 * int 13h), and it is the same cost the paragraph below refuses to pay for the
 * progress file - the smaller of the two reads. tests/covl is not the
 * precedent it looked like: covl is a capability gate whose whole subject is
 * the far call, and cword enters its module from a menu command or with it
 * already resident.
 *
 * SO THE FIRST PAINT COMPOSES THE PREVIEW PANE EMPTY, and os88_onwake() - the
 * one lock-free callback - makes the module resident and repaints in FULL, so
 * ovl_chrome() goes down with it. A launch is two frames instead of one and
 * neither of them holds the machine.
 *
 * THE PROGRESS FILE IS NOT READ HERE EITHER, for the same reason one level
 * along: reading it is a dive into SYSTEM/APPDATA - two directory walks and
 * four folder changes. os88_onwake() does that half too, and by SPEC.md 74.1 it
 * is on the UI task, lock free, and "may call the file slots". */
static void lem_first_wake(void)
{
    if (lem_waked)
        return;
    lem_waked = 1;
    lem_ovl = ovl_ready() ? 1 : -1;
}

/* lem_kick - re-post the wake when an errand is still owed.
 *
 * os88_wm_wake() answers -1 "when the ring was full of other events and nothing
 * was posted: kick again from your next callback" (os88.h), and this program
 * now owes THREE things to os88_onwake() - the module, the rating file and the
 * progress write. Ignoring that answer is a rating tab that moved with a list
 * under it that never followed, or a progress file that is never written, and
 * neither says anything on the glass. So every callback that the user's own
 * input reaches kicks again first. It costs nothing when a wake is already
 * queued: the kernel keeps at most one per window, "so kicking from every
 * callback is free and cannot fill the 16-record event ring". */
static void lem_kick(void *win)
{
    if (lem_ratpend || lem_progwrite || !lem_waked)
        os88_wm_wake(win);
}

/* lem_abdismiss - take the About card down.
 *
 * THE CARD IS OPAQUE OVER ITS OWN RECT (apps/os88ui.inc's os88ui_about_d): it
 * overwrote the cells under it without the shadow knowing, so a repaint that
 * still trusts the shadow finds every composed run equal to it, marks nothing
 * dirty and issues ZERO drawing calls - the card stays on the glass until the
 * next expose or resize. apps/weave/weave.c's w_abdismiss is the precedent and
 * says the same thing in the same words ("the whole content: the card was
 * opaque over its own rect").
 *
 * INVALIDATING THE SHADOW IS NOT ENOUGH, AND THE GLASS IS WHERE THAT WAS
 * SETTLED. `lem_sh_ok = 0` re-letters every CELL, and a cell is 8 pixels of a
 * 9-pixel pitch (LEM_RPITCH) with LEM_PADX/PADY of 2 around the block: the
 * card's frame and its drop shadow cross the LEADING row between every pair of
 * text rows and the columns past the last whole cell, and no font_run of ours
 * ever writes those pixels. So the first build of this fix left a dotted ghost
 * of the card's border behind on a dismissal - visible in
 * build/port-shots/w2-dismiss-zoom.png, a dashed rule across the preview pane
 * and a dotted column down its right edge - with every character correctly
 * re-lettered around it.
 *
 * SO IT FILLS, WHICH IS WHAT weave's w_repaint2 DOES FOR IT AND THIS PROGRAM
 * HAS NEVER DONE. One os88_gfx_fill over the content box is 756 us, it puts
 * back exactly the white ground kernel/wm.inc's WF_OWNBG interlock lays down
 * before a W_PAINT, and it lets the shadow be SEEDED blank rather than
 * invalidated - so the repaint that follows draws the cells that carry
 * something and skips the ones that do not, which is the same 28 calls a
 * W_PAINT costs instead of every blank cell in the box. The fill and the seed
 * are one fact stated twice and must not drift.
 *
 * The clip region is armed here and lem_repaint() is told NOT to arm it again:
 * the fill has to be inside the same region as the letters, and a refusal
 * means not one pixel of us shows, so nothing is drawn and the shadow is left
 * INVALID for the expose that follows. */
static void lem_abdismiss(void *win)
{
    lem_abon = 0;
    if (os88_wm_clip_set(win) != 0) {
        lem_sh_ok = 0;
        return;
    }
    lem_layout(win);                    /* lem_org / lem_sz for the fill */
    os88_set_color(OS88_WHITE);
    os88_gfx_fill(lem_org.x, lem_org.y,
                  lem_org.x + lem_sz.w - 1, lem_org.y + lem_sz.h - 1);
    os88_set_color(OS88_BLACK);
    lem_sh_blank();                     /* the glass IS blank now */
    lem_repaint(win, 1, 0);             /* 0: the region above is ours */
}

/* ============================================================================
 * The callbacks the shim declares (apps/lemmings/lemmings.asm)
 * ==========================================================================*/

void os88_paint(void *win)          /* W_PAINT: the gfx lock is ALREADY held */
{
    lem_kick(win);                  /* free when one is queued, and the only
                                     * thing that recovers a REFUSED post */

    /* NO DISK I/O HERE, AND THERE WAS. This used to call lem_first_wake() so
     * that the very first frame had the module behind it - the kick posted from
     * os88_main() is delivered "a few event-loop passes later" (SPEC.md 74.1)
     * and the first W_PAINT beats it, so the launcher came up with the level
     * names on it and an EMPTY preview pane. The cure was worse: cc_ovneed is a
     * heap claim, a directory walk and a 2,752-byte read, and W_PAINT arrives
     * with the gfx lock HELD and must not block. What is on the glass for those
     * few passes is a launcher with an empty pane; what it was is a frozen
     * machine. The wake repaints in FULL and the pane and the rule arrive with
     * it (lem_first_wake, os88_onwake).
     *
     * The kernel has just white-filled the whole content under us, so the
     * shadow does not describe the glass any more - but it is not UNKNOWN
     * either, which is the difference that pays for itself: it describes a
     * blank box, so the repaint below draws the cells that carry something and
     * skips the ones that do not (LESSONS.md 6, and kernel/wm.inc's WF_OWNBG
     * interlock is what makes the fill a fact rather than a hope). */
    lem_sh_blank();
    lem_repaint(win, 1, 0);         /* 0: W_PAINT arrives with the region
                                     * ALREADY armed (SPEC.md 11.3) and it is
                                     * the ONE callback that does */
    if (lem_abon)
        os88_about_card_d(win, lem_ab);   /* _d: this paint's region is armed
                                           * already (SPEC.md 20.5.1.1) */
}

/* W_ONRESIZE (SPEC.md 11.98): the content box CHANGED and we did not ask - an
 * adapter change, or a drag onto a shorter display. WE MUST NOT DRAW HERE; a
 * full repaint follows immediately. What this is for is being laid out
 * correctly before that paint runs rather than a frame later. */
void os88_onresize(int w, int h, void *win)
{
    (void)w;
    (void)h;

    /* RE-PROBE, BECAUSE THIS CALLBACK IS DELIVERED ON A CHANGE OF ADAPTER KIND
     * (SPEC.md 39.16.3.3 and 11.98): drag the launcher from the VGA onto the
     * Hercules on vm/xt-multimon and the display answering for this window is a
     * different card with a different caps mask. lem_mode_ok was written once,
     * in os88_main(), and the Mode row then lettered "VGA / 320x200x16" and
     * refused to grey on a display where os88_fsx_mode() would refuse - which
     * is SPEC.md 47 rule 4's ONE PREDICATE FOR BOTH turning into two, and is
     * exactly the mistake kernel/fsx.inc records ("pass your own window and ask
     * AGAIN where the answer is used"). It decides and draws nothing, which is
     * what W_ONRESIZE requires; lem_probe_mode() also rewrites lem_vidkind from
     * the answering display, so lem_adapter() follows it. */
    lem_mode_ok = lem_probe_mode(win);

    lem_sh_ok = 0;
    lem_layout(win);
    /* A paragraph is wrapped to the LIVE content width, so a box that just
     * changed width has a stale wrap in it - and the row composer would then
     * hand os88_font_run() lines longer than the window and rely on lem_fit to
     * cut them, which loses the words rather than moving them. Re-wrapping here
     * is one pass over ~500 bytes and it happens before the repaint that
     * follows this callback, which is the whole reason W_ONRESIZE exists
     * (SPEC.md 11.98: be laid out correctly BEFORE that paint, and draw
     * nothing here). */
    if (lem_screen != LEM_SC_LIST)
        lem_wrap();
}

void os88_onclick(int x, int y, void *win)
{
    int hit;

    lem_kick(win);
    if (lem_abon) {
        lem_abdismiss(win);
        return;
    }
    if (lem_screen != LEM_SC_LIST) {
        /* The original's own footer on both screens: "Press mouse button to
         * continue" (Lemmix Base.Strings.pas SPreviewScreen_...). */
        lem_back(win);
        return;
    }

    hit = lem_hit(x, y);
    if (hit == LEM_HIT_NONE)
        return;
    if (hit >= LEM_HIT_ROW) {
        lem_select(win, lem_top + (hit - LEM_HIT_ROW));
        return;
    }
    if (hit >= LEM_HIT_STATE && hit < LEM_HIT_STATE + LEM_STATE_ROWS) {
        /* A greyed control answers with the FACT that greys it (SPEC.md 47),
         * and the fact is a sentence in the band rather than in this image. */
        hit -= LEM_HIT_STATE;
        if (lem_state_grey(hit))
            lem_show_fact(win, lem_state_label(hit), lem_state_fact(hit), -1);
        return;
    }
    if (hit >= LEM_HIT_TAB && hit < LEM_HIT_TAB + LEM_RATINGS) {
        lem_set_rating(win, hit - LEM_HIT_TAB);
        return;
    }
    if (hit == LEM_HIT_PLAY)
        lem_play(win);
}

void os88_onkey(int ascii, int scan, void *win)
{
    int n;

    lem_kick(win);
    if (lem_abon) {
        lem_abdismiss(win);
        return;
    }
    if (lem_screen != LEM_SC_LIST) {
        if (ascii == 27 || ascii == 13 || ascii == ' ')
            lem_back(win);
        return;
    }

    n = lem_sel;
    if (scan == LEM_SC_UP)
        n--;
    else if (scan == LEM_SC_DOWN)
        n++;
    else if (scan == LEM_SC_PGUP)
        n -= lem_nlist;
    else if (scan == LEM_SC_PGDN)
        n += lem_nlist;
    else if (scan == LEM_SC_HOME)
        n = 0;
    else if (scan == LEM_SC_END)
        n = LEM_PERRAT - 1;
    else if (scan == LEM_SC_LEFT) {
        lem_set_rating(win, (lem_rating + LEM_RATINGS - 1) % LEM_RATINGS);
        return;
    } else if (scan == LEM_SC_RIGHT) {
        lem_set_rating(win, (lem_rating + 1) % LEM_RATINGS);
        return;
    } else if (ascii == 13 || ascii == ' ') {
        lem_play(win);
        return;
    } else {
        return;
    }

    if (n < 0)
        n = 0;
    if (n >= LEM_PERRAT)
        n = LEM_PERRAT - 1;
    lem_select(win, n);
}

/* W_ONWAKE (SPEC.md 74.1) - the one callback that runs WITHOUT the gfx lock,
 * and the first one this instance gets. See lem_first_wake().
 *
 * THE DISK TIME LIVES HERE AND NOWHERE ELSE. os88.h: W_ONWAKE is lock free, on
 * the UI task, "may call the file slots", and may take the lock itself around a
 * burst of drawing you can put a number on. The progress file's dive - the root
 * of this volume, then SYSTEM, then APPDATA, then back - is that disk time, so
 * it is here rather than in W_PAINT, which must not block. The burst that
 * follows it is one PARTIAL repaint: the only thing the answer can change is
 * the Save Progress row's greying and its value. */
void os88_onwake(void *win)
{
    int drew;

    lem_first_wake();               /* the MODULE: a claim, a directory walk and
                                     * a 2,752-byte read, and this is the only
                                     * callback that may spend them */
    drew = 0;

    if (!lem_progread && lem_ovl == 1) {
        lem_progread = 1;
        lem_savable = ovl_progress_read();
        drew = 1;
    }

    /* THE RATING THE USER ASKED FOR, read here rather than on the key that
     * asked. LEMR<n>.LEM is a whole-file read - a directory walk, a FAT walk
     * and the data run, three int 13h calls at ~400 ms apiece on the target -
     * and Left, Right and a tab click are all delivered with the gfx lock HELD
     * (os88.h at os88_onkey / os88_onclick). Left AUTOREPEATS at ~100 ms, so
     * that was ten presses queued behind every freeze. The flag is RE-ARMED and
     * never stacked: a second rating key while one is owed simply moves
     * lem_rating, and this reads whichever one the user stopped on. */
    if (lem_ratpend) {
        lem_ratpend = 0;
        lem_rating_load(lem_rating);
        lem_sel = 0;
        lem_top = 0;
        drew = 1;
    }

    /* ...AND THE PROGRESS WRITE, which is the same defect one size down: two
     * ordinal walks to find SYSTEM and APPDATA and then a write that rewrites
     * the FAT and a directory entry, on Enter, Space or a Play click. Nothing
     * on the glass waits for it - lem_prog[] was updated in memory at the
     * keystroke and the Save Progress row is lettered from THAT - so there is
     * no ordering to preserve and nothing to repaint for. */
    if (lem_progwrite && lem_ovl == 1) {
        lem_progwrite = 0;
        ovl_progress_write(lem_pw_rat, lem_pw_lvl);
    }

    if (!drew || !lem_sh_ok)
        return;                     /* the first paint has not run yet, and it
                                     * will draw the answer itself */
    if (lem_abon)
        return;                     /* the card is up and OPAQUE over its own
                                     * rect: painting the launcher under it
                                     * would mark those cells CLEAN in a shadow
                                     * that does not describe the glass, which
                                     * is the failure lem_abdismiss() is written
                                     * about. The dismissal repaints in full and
                                     * picks all of this up */
    os88_gfx_lock();
    /* FULL, because this is the first repaint with the module behind it and
     * ovl_chrome() is gated on `full` (lemui.c): the vertical rule between the
     * two panes goes down here. */
    lem_repaint(win, 1, 1);
    os88_gfx_unlock();
}

/* W_ONTIMER (SPEC.md 13.9) - ONE-SHOT, and it exists for exactly one thing: the
 * preview pane SETTLES.
 *
 * An arrow key changes the selection, which changes two level rows AND all
 * seven preview lines beside them - and the pane is ~140 of the ~180 glyph
 * cells a selection move costs, which is ~126 ms of the ~168 on a 4.77 MHz 8088
 * (PERFORMANCE.md's ~900 us a cell). Held down, the machine falls further
 * behind on every repeat, which is INPUT OVERRUN, the third of the three
 * defects PERFORMANCE.md Part 1 says an emulator cannot show you. So the list
 * rows are drawn on the keystroke and the pane is armed for LEM_PV_SETTLE ticks
 * and re-armed by each further key: one pane repaint after the arrow stops,
 * instead of one per repeat. This callback runs in W_ONCLICK's environment -
 * UI task, gfx lock HELD - so it may draw, and it arms its own clip region
 * because the kernel arms one for W_PAINT and for nothing else. */
void os88_ontimer(void *win)
{
    lem_kick(win);
    if (!lem_pv_due)
        return;                     /* a stale shot: be indifferent to it */
    lem_pv_due = 0;
    if (lem_abon || lem_screen != LEM_SC_LIST)
        return;
    lem_repaint(win, 0, 1);
}

void os88_oncmd(int item, int menu, void *win)
{
    /* Unreachable by construction: the registered set has no menus. It exists
     * so that its AM_NAME names the product in the kernel bar. */
    (void)item;
    (void)menu;
    (void)win;
}

void os88_about(void *win)
{
    lem_first_wake();               /* About may be the first thing clicked */
    if (lem_ovl != 1) {
        /* THE ATTRIBUTION SURFACE DEGRADES TO THE CREDIT AND NOT TO THE PORT'S
         * OWN NAME. With no module there is no card, and what a reader most
         * needs off it is whose work this is - which is a RESIDENT band string
         * and needs no module to read (SPEC.md 92.2). lem_prodline is the
         * fallback's fallback, for a disk with no band on it either. */
        os88_toast(lem_ok ? lem_str(LEMS_CREDIT1) : lem_prodline, 0);
        return;
    }
    lem_abon = 1;
    ovl_about(win);                 /* the plain form: os88_about() arrives with
                                     * NO clip region armed (SPEC.md 20.5.1.1) */
}

/* THE EXCLUSIVE BRACKET'S BODY (SPEC.md 53.1, 92.5), declared by CC_HAS_FSX and
 * DEFINED AND NOT YET CALLED - exactly as os88_oncmd() above is. Wave 2 fills
 * it: os88_fsx_mode() sets the raster, lemblit.inc draws it, os88_fsx_wait()
 * paces it and os88_fsx_key() reads the keyboard, because no event is
 * dispatched in here and the contract is that the app polls. Nothing calls
 * os88_fsx_run() in this wave, so nothing enters it.
 *
 * The trampoline in crt0.asm is what makes the two halves impossible to drift:
 * a %define with no C function behind it is an nasm error naming this function,
 * and this function with no %define is code the kernel never calls. */
void os88_fsx_main(void *win)
{
    (void)win;
}

/* --- the entry point (SPEC.md 20.2) ----------------------------------------
 * The gfx lock is NOT held here and the window does not exist yet. The FILE
 * slots ARE legal, so this is where the band files are read: the manifest, the
 * string band and the first rating. What may NOT happen here is the overlay
 * load (there is no instance to resolve a module for) - that is the first
 * wake's job.
 *
 * A REFUSAL HERE IS NOT FATAL. The window comes up and says what is missing,
 * which is SPEC.md 47's ordinary path and a great deal more use than a launch
 * that silently aborts. */
void *os88_main(void)
{
    static struct os88_video vid;
    int i;

    lem_rating = 0;
    lem_sel = 0;
    lem_top = 0;
    lem_screen = LEM_SC_LIST;
    for (i = 0; i < LEM_RATINGS; i++)
        lem_prog[i] = -1;

    lem_ok = lem_data_load();

    os88_video(&vid);
    lem_vidkind = vid.kind;
    lem_snd = (os88_snd_caps() != 0);

    lem_name = lem_ok ? lem_str(LEMS_PROD) : lem_title;
    if (lem_name[0] == 0)
        lem_name = lem_title;       /* a band that does not carry the row */
    lem_menus.name = lem_name;

    lem_win = os88_wm_create(LEM_WIN_X, LEM_WIN_Y, LEM_WIN_W, LEM_WIN_H,
                             lem_name);
    if (lem_win == 0)
        return 0;

    os88_wm_minsize(lem_win, LEM_MIN_W, LEM_MIN_H);
    os88_wm_sizable(lem_win, 1);        /* ...then lay out from the LIVE
                                         * geometry, never from these numbers */
    os88_wm_snap(lem_win, 1);           /* content origin on a multiple of 8, so
                                         * every font_run takes SPEC.md 6.1's
                                         * single-store path on 1bpp */
    os88_wm_onresize(lem_win);
    os88_wm_onwake(lem_win);
    os88_wm_ontimer(lem_win);           /* installed, armed only by a selection
                                         * move (os88_ontimer above). It may be
                                         * REFUSED on a kern_small machine,
                                         * which is why lem_pv_defer() tests
                                         * os88_wm_timer()'s answer and has a
                                         * second path (SPEC.md 13.8.2) */
    os88_menu_set(lem_win, &lem_menus);
    os88_about_set(lem_win);

    lem_mode_ok = lem_probe_mode(lem_win);

    lem_sh_ok = 0;
    os88_wm_wake(lem_win);              /* ask for the first wake: the overlay
                                         * and the progress file are read there
                                         * and neither may happen here */
    return lem_win;
}
