/* ============================================================================
 * os8088 - apps/cword/cword.c     Microsoft Word 1.1a, written in C
 *
 * A native reimplementation of Microsoft Word for Windows 1.1a ("Opus") as an
 * os8088 package, compiled from C by the toolchain of SPEC.md 67 and split
 * across two segments by the overlay of SPEC.md 67.14. The binding contract is
 * SPEC.md 67.12; the reasoning behind the UI it reproduces is docs/WORD-PLAN.md
 * and SPEC.md 65, which describe the same product ported by hand into assembly.
 *
 * THE USER INTERFACE IS OPUS'S, TAKEN FROM THE SOURCE AND NOT FROM MEMORY.
 * The nine-menu bar, every menu string, every mnemonic, the ribbon and ruler
 * layout, the status line's field order and the keyboard map come from the
 * Computer History Museum's 2014 release of the Word for Windows 1.1a source:
 *
 *     menus and mnemonics      Opus/resource/menus.cmd     (apps/cword/cwmenu.c)
 *     shortcut captions/keys   Opus/resource/keys.cmd
 *     ribbon and ruler         Opus/ibdefs.h
 *     status line fields       Opus/status.h
 *     dialog contents          Opus/dlg/*.des
 *     RTF in and out           Opus/RTFOUT.C, RTFIN.C      (apps/cword/cwrtf*)
 *
 *     Copyright (C) Microsoft Corporation for the material derived from that
 *     release. Source: Microsoft Word for Windows 1.1a source code, Computer
 *     History Museum release, 2014, directory `Opus/`.
 *
 * What could NOT be ported is the code: Opus is pcode built by a proprietary
 * CSL compiler against the Windows 2.x API, and none of that exists here. What
 * a user touches is what carries across, and it carries across exactly.
 *
 * IT IS NOT SPEC.md 65's PORT AND SHARES NOTHING WITH IT (SPEC.md 67.12). That
 * one is apps/word, hand-written assembly, package WORD, `.DOC`, build/word*.img
 * and vm/xt-word; this one is apps/cword, package CWORD, `.RTF`,
 * build/cword*.img and vm/386-c-word. Two programs may have the same ambition;
 * what they may not be is two things answering to one name.
 *
 * ---------------------------------------------------------------------------
 * THE MODE IS DRAFT VIEW, WHICH IS NOT A SIMPLIFICATION BUT A SHIPPED MODE
 * ---------------------------------------------------------------------------
 * os8088 has one 8x8 fixed face and no styles in the renderer (SPEC.md 6), and
 * Word 1.1a shipped View > Draft for exactly that situation: fixed pitch,
 * formatting shown synthesised, layout simplified, editing fast. That is the
 * mode this port lives in, and the synthesis follows SPEC.md 61.5's precedent -
 * bold is a second strike one pixel right, italic a strike one pixel up and
 * right, the three underlines are drawn rules, small caps is a case map, hidden
 * text leaves the layout. Each costs ONE extra call per RUN, never one per
 * glyph, which is the difference between affordable and not on a 4.77 MHz 8088.
 *
 * ---------------------------------------------------------------------------
 * WHERE THE CODE LIVES, AND WHY HALF OF IT IS NOT IN THIS SEGMENT
 * ---------------------------------------------------------------------------
 * A package's image and bss share 61,440 bytes and that ceiling IS the segment
 * (SPEC.md 33). The assembly port spends 54,952 of it. C costs two to three
 * times as much image for the same work (SPEC.md 67.9), so this program does
 * not fit in one segment and could not be made to.
 *
 * So it is in two. Everything on the redraw path - layout, wrap, the glass
 * shadow, the chrome, the keyboard - is resident and in this file. Everything
 * that runs ONCE PER COMMAND is in the overlay (apps/cword/cwovl.c, every
 * function named `ovl_*`): the dialogs, RTF in and out, Sort, Renumber, Table
 * of Contents. It ships as CWORD.OVL beside CWORD.O88 and is read into a heap
 * claim the first time one of those is asked for. The split is not arbitrary -
 * it is exactly the line between what a keystroke touches and what a menu
 * command touches, so nothing in the inner loop ever pays for it.
 *
 * ---------------------------------------------------------------------------
 * WHAT A KEYSTROKE COSTS, WHICH IS THE POINT OF THE WHOLE DESIGN
 * ---------------------------------------------------------------------------
 * PERFORMANCE.md prices any gfx_* call at 756 us flat and one 8x8 glyph cell at
 * ~900 us, so A REDRAW IS PRICED IN CALLS AND CELLS, NOT IN PIXELS. Repainting
 * this window whole is over a second on the target machine. So:
 *
 *   THE GLASS IS SHADOWED. cw_sh[]/cw_sha[] hold the character and the
 *   formatting of every cell CURRENTLY ON THE SCREEN; a repaint lays out the
 *   rows that could have changed, compares each against the shadow, and draws
 *   the columns that differ and nothing else.
 *
 *   THE LAYOUT STOPS EARLY. After inserting one character the rows below the
 *   edited paragraph hold the same text one byte further along, so the moment a
 *   row's new start equals its old plus that delta, every row below is
 *   identical and cw_relayout() stops.
 *
 *   SCROLLING MOVES PIXELS, and the chrome is drawn once and then touched only
 *   where it changes - a ribbon button that lit, a status field whose text is
 *   different. Opening a menu repaints only what the panel covered.
 *
 * ---------------------------------------------------------------------------
 * WHAT IT COSTS OF THE 60KB, AND OF THE SEGMENT BEYOND IT
 * ---------------------------------------------------------------------------
 * Measured by tools/os88pkg.py and tools/os88ovl.py on the shipping package:
 *
 *     resident image  37,062   the code above, its strings, the Opus tables
 *              bss    22,028   the document (4,000 + 4,000), the RTF staging
 *                              buffer (6,000), the glass shadow (6,144)
 *              -------------
 *              total  59,090   96% of APP_MAX_SIZE; 2,350 bytes spare
 *     CWORD.OVL        9,430   the dialogs, RTF in and out, search, Sort,
 *                              Renumber, Table of Contents
 *
 * THE TIGHTEST NUMBER IN THE PACKAGE IS CW_RTF_MAX, the RTF staging buffer, at
 * 6,000 bytes - which is why a document that is 4,000 characters of dense
 * formatting can be refused a save (it says so, and the document is untouched).
 * The next thing to do about this ceiling is not to shave code: it is to move
 * that buffer out of bss into a heap claim and read and write the file through
 * it there, the way SPEC.md 46.9 moved ArtfulType's document. Nothing else here
 * is a buffer worth 6KB.
 *
 * ---------------------------------------------------------------------------
 * WHAT A KEYSTROKE COSTS, MEASURED
 * ---------------------------------------------------------------------------
 * apps/cword/hosttest/cwuitest.c stubs the whole API with a model of the glass,
 * counts every drawing call and every glyph cell, and after each keystroke
 * rebuilds what the screen ought to show - from an independently written word
 * wrap - and compares it cell for cell. It prints this on every build:
 *
 *     a full repaint (W_PAINT), 71x24         132 calls  1899 cells  1808 ms
 *     one ordinary keystroke                    5 calls     8 cells     8 ms
 *     an arrow key that does not scroll         4 calls     7 cells     9 ms
 *     a keystroke that scrolls the view         8 calls     9 cells    14 ms
 *     typing at the START of a full line        5 calls    79 cells    75 ms
 *     Enter in the middle of a paragraph        5 calls    94 cells    88 ms
 *
 * The first line is what a full screen of text costs on this machine whatever
 * writes it; every other line is what the damage model buys. THREE REAL
 * DEFECTS came out of that harness and none would have shown in a screendump:
 * a row's break depends on the character one past its own end (so an edit in
 * row N can change row N-1's), the double underline landed where the next
 * row's italic goes, and a modal panel took the glass while the shadow went on
 * claiming what was under it.
 *
 * ---------------------------------------------------------------------------
 * THE FOUR C RULES (apps/cc/os88.h), AND WHERE THIS FILE MEETS THEM
 * ---------------------------------------------------------------------------
 *  1. NO ADDRESS OF AN AUTOMATIC - SS is LOW_SEG and DS is ours, so `&local`
 *     is a stack offset dereferenced through the wrong segment. Every buffer
 *     here is file scope, including the out-parameters. Refused at build time.
 *  2. NO STRING INSTRUCTION - ES is the kernel's. Nothing here assigns a
 *     struct, passes one by value or returns one. The menu table is INDEXED,
 *     which copies nothing. The one `rep movsb` in the package is hand-written
 *     in apps/cword/cword.asm, where ES is loaded on purpose and put back.
 *  3. NO long, NO float, NO bit-field.
 *  4. SMALL FRAMES - no local arrays; the build prints every frame size.
 * ==========================================================================*/

#include "os88.h"

#include "cwrtftbl.h"
#include "cwrtftbl.c"                   /* the Opus RTF tables - include once */
#include "cwrtfio.c"                    /* the document and RTF - include once */
#include "cwmenu.c"                     /* the nine menus, verbatim from Opus */

/* ==========================================================================
 * THE GRID AND THE CHROME
 *
 * Authored against 640x480 and CLAMPED ONTO THE LIVE SCREEN on every layout:
 * two adapters of three are 640x200 and a constant here would be wrong on both
 * (SPEC.md 39). CW_COLS_MAX x CW_ROWS_MAX only bounds the shadow.
 * ========================================================================*/
#define CW_COLS_MAX   76
#define CW_ROWS_MAX   24
#define CW_SH_STRIDE 128                /* a POWER OF TWO: `row * 76` is an
                                         * 8086 imul needing a scratch register
                                         * and tools/cc8086.py refuses the site
                                         * when it cannot prove one free;
                                         * `row * 128` is a shift. The wasted
                                         * bytes are bss, the cheap half of the
                                         * ceiling (SPEC.md 67.9) */
#define CW_PITCH      11                /* an 8-px glyph band and THREE px of
                                         * gap, which is one more than the
                                         * demonstrator needed and is the
                                         * difference between three attributes
                                         * and five. The ink outside the band
                                         * is: the italic overstrike one row
                                         * ABOVE it, the underline one row
                                         * below, and the DOUBLE underline two
                                         * rows below. At a pitch of 10 that
                                         * second rule lands exactly where the
                                         * next row's italic overstrike goes -
                                         * so a double-underlined line ate the
                                         * italic off the line beneath it, and
                                         * the strip erase then rubbed it out
                                         * again on every repaint. Found by
                                         * apps/cword/hosttest/cwuitest.c,
                                         * which models those two pixel rows
                                         * separately for exactly this reason.
                                         * The cost is one screen row on a
                                         * 640x200 adapter. */

#define CW_H_BAR      14                /* the nine-title menu bar */
#define CW_H_RIB      16                /* the ribbon      (View > Ribbon)     */
#define CW_H_RUL      26                /* the ruler       (View > Ruler)      */
#define CW_H_STA      14                /* the status line (View > Status Bar) */
                                        /* The ruler is 26 and not the
                                         * product's 18 because the product's
                                         * face is smaller than this machine's
                                         * only one: 12 px of buttons and an
                                         * 8 px row of inch numbers do not fit
                                         * in 18, and the first version drew
                                         * them on top of each other. Height
                                         * is the thing to spend here - the
                                         * strip toggles off from the View
                                         * menu and the text band takes the
                                         * room back. */

/* --- keys (int 16h scan codes) ------------------------------------------- */
#define CW_K_F1     0x3B
#define CW_K_F4     0x3E
#define CW_K_F5     0x3F
#define CW_K_F8     0x42
#define CW_K_SF4    0x57
#define CW_K_HOME   0x47
#define CW_K_UP     0x48
#define CW_K_PGUP   0x49
#define CW_K_LEFT   0x4B
#define CW_K_RIGHT  0x4D
#define CW_K_END    0x4F
#define CW_K_DOWN   0x50
#define CW_K_PGDN   0x51
#define CW_K_INS    0x52
#define CW_K_DEL    0x53
#define CW_K_F11    0x85
#define CW_K_F12    0x86

/* Ctrl+letter formatting, keys.cmd's map. Ctrl-H, Ctrl-I and Ctrl-M are folded
 * by int 16h into BkSp, Tab and Enter, so hidden text, italic and one indent
 * command have no Ctrl key here and reach the document through the ribbon and
 * the Format Character dialog instead - the same collision apps/notepad
 * documents. */
#define CW_C_BOLD   0x02                /* Ctrl-B */
#define CW_C_CENTER 0x03                /* Ctrl-C */
#define CW_C_DUL    0x04                /* Ctrl-D */
#define CW_C_OPEN   0x0F                /* Ctrl-O - open space before */
#define CW_C_CLOSE  0x05                /* Ctrl-E - close it */
#define CW_C_FIND   0x06                /* Ctrl-F */
#define CW_C_SCAP   0x0B                /* Ctrl-K */
#define CW_C_LEFT   0x0C                /* Ctrl-L */
#define CW_C_JUST   0x0A                /* Ctrl-J */
#define CW_C_INDENT 0x0E                /* Ctrl-N */
#define CW_C_GOTO   0x10                /* Ctrl-P - the Go To dialog here */
#define CW_C_RIGHT  0x12                /* Ctrl-R */
#define CW_C_SAVE   0x13                /* Ctrl-S */
#define CW_C_HANG   0x14                /* Ctrl-T */
#define CW_C_ULINE  0x15                /* Ctrl-U */
#define CW_C_UNHANG 0x07                /* Ctrl-G */
#define CW_C_WUL    0x17                /* Ctrl-W */
#define CW_C_UNDO   0x1A                /* Ctrl-Z, beside Alt+BkSp */

/* ==========================================================================
 * THE DOCUMENT'S FORMATTING
 *
 * CHARACTER formatting is one byte a character in cw_att[], mirroring every
 * move cw_buf[] makes. PARAGRAPH formatting LIVES ON THE PARAGRAPH MARK: the
 * '\n' that ends a paragraph carries, in its cw_att[] byte, an index into a
 * dictionary of unique paragraph formats. That is Word's own arrangement and
 * it is why undo, cut, paste and split/join need no third structure: a
 * paragraph's format travels with its mark through the same byte array as
 * everything else, and deleting a mark merges the paragraph into the next
 * one's format, which is exactly what Word does.
 *
 * CW_A_* below extend cwrtfio.c's three bits to Word's seven. Bit 7 is not
 * free - see cw_pap_of().
 * ========================================================================*/
#define CW_A_WUL      8                 /* word underline    (Ctrl-W) */
#define CW_A_DUL     16                 /* double underline  (Ctrl-D) */
#define CW_A_SCAP    32                 /* small caps        (Ctrl-K) */
#define CW_A_HID     64                 /* hidden text                */
#define CW_A_CHP    127                 /* every character bit        */

/* The paragraph mark's stand-in inside a row buffer. It is NOT a character
 * the kernel's face can letter - there is no pilcrow in 32..126 - so it is a
 * code that survives the shadow comparison (a change to Show Paragraph Marks
 * therefore dirties exactly the rows that carry one) and is turned into ink by
 * cw_pilcrow() at draw time. 20 is chosen because no document byte can be it:
 * the model holds printable ASCII and '\n' only. */
#define CW_MARK      20

#define CW_PAP_MAX   64                 /* unique paragraph formats. Word's own
                                         * dictionary is 256; 64 x 4 bytes is
                                         * what this ceiling affords, and the
                                         * 65th unique format is a refusal with
                                         * a reason rather than a silent
                                         * flattening (SPEC.md 47) */
#define CW_AL_LEFT    0
#define CW_AL_CENTER  1
#define CW_AL_RIGHT   2
#define CW_AL_JUST    3

/* The dictionary, four parallel byte arrays rather than an array of structs
 * so that a careless assignment cannot become a `rep movsb`. */
static unsigned char cw_pap_al[CW_PAP_MAX];    /* CW_AL_*                     */
static unsigned char cw_pap_sp[CW_PAP_MAX];    /* 0 single, 1 one-and-a-half,
                                                * 2 double                    */
static unsigned char cw_pap_op[CW_PAP_MAX];    /* space before the paragraph  */
static unsigned char cw_pap_li[CW_PAP_MAX];    /* left indent, in cells       */
static signed char   cw_pap_fi[CW_PAP_MAX];    /* first line, relative to it  */
static unsigned char cw_pap_ri[CW_PAP_MAX];    /* right indent                */
static int cw_pap_n = 1;                       /* entry 0 is the default and
                                                * is never reclaimed          */

/* ==========================================================================
 * STATE - all of it file scope, because everything addressable has to be
 * ========================================================================*/

static struct os88_size cw_sz;          /* the live content box */
static struct os88_pt   cw_org;

static int cw_cols;                     /* the live text grid */
static int cw_rows;
static int cw_tx;                       /* text area origin, screen px */
static int cw_ty;
static int cw_by;                       /* the menu bar's top */
static int cw_ry;                       /* the ribbon's, -1 = not shown */
static int cw_uy;                       /* the ruler's */
static int cw_sy;                       /* the status line's baseline */
static int cw_chrome_top;               /* where the text band's strip begins,
                                         * which is what a View toggle moves */

static int cw_cur;                      /* the caret, 0..cw_len */
static int cw_top;                      /* offset of the first visible row */
static int cw_attr;                     /* CHP that NEW characters get */
static int cw_pap_now;                  /* the PAP new paragraphs get. NOT
                                         * `cw_pap`: cwrtfio.c owns that name
                                         * for the RTF reader's property
                                         * record, and one translation unit
                                         * means one namespace (SPEC.md 67.1) */
static int cw_mod;                      /* changed since the last save */
static int cw_ovr;                      /* overtype (Ins), status lamp OVR */
static int cw_ext;                      /* F8 extend-selection, lamp EXT */
static int cw_showp;                    /* Show paragraph marks */
static int cw_page;                     /* View > Page rather than Draft */

static int cw_sel_on;                   /* a selection is up */
static int cw_anchor;                   /* ...and this is its other end */

static int cw_vrib = 1;                 /* the three View toggles */
static int cw_vrul = 1;
static int cw_vsta = 1;

/* The open dropdown: -1 = none. cw_m_sticky says a click opened it and it
 * stays open until something closes it; without it the menu is being dragged
 * through and the button going up chooses. */
static int cw_m_open = -1;
static int cw_m_item = -1;              /* the highlighted item, -1 = none */
static int cw_m_sticky;
static int cw_m_x1;                     /* the panel's rect, banked once when
                                         * it opens: the painter, the hit test,
                                         * the highlight and the close repaint
                                         * all read these four and cannot
                                         * disagree about where it is */
static int cw_m_y1;
static int cw_m_x2;
static int cw_m_y2;

/* The wrap, computed into file scope because `&local` does not build here. */
static int cw_w_end;                    /* one past the last character SHOWN */
static int cw_w_next;                   /* start of the next row, -1 = none */

static int cw_ls[CW_ROWS_MAX];          /* each visible row's start, -1 = past
                                         * the end of the text */
static int cw_le[CW_ROWS_MAX];          /* ...and its end */
static unsigned char cw_lp[CW_ROWS_MAX];/* ...and the PAP it is laid out in */
static int cw_from;                     /* the first row a repaint must touch */
static int cw_stop;                     /* ...and the first it need not */

/* THE SHADOW: what is on the glass right now, cell for cell. */
static char          cw_sh[CW_ROWS_MAX * CW_SH_STRIDE];
static unsigned char cw_sha[CW_ROWS_MAX * CW_SH_STRIDE];
static int           cw_sh_start[CW_ROWS_MAX];
static int           cw_sh_end[CW_ROWS_MAX];
static signed char   cw_sh_ind[CW_ROWS_MAX];   /* the row's left indent, in
                                                * cells: a row that moved
                                                * sideways changed even though
                                                * its text did not */
static int           cw_sh_ok;          /* 0 = the glass is unknown */

static char          cw_row[CW_COLS_MAX + 1];  /* the row being laid out */
static unsigned char cw_rowa[CW_COLS_MAX];
static char          cw_rt[CW_COLS_MAX + 1];   /* one run of it, NUL ended */
static int           cw_row_ind;               /* its left indent in cells */
static int           cw_row_txt;               /* ...and its TEXT length */

static int cw_wiped;                    /* the kernel just whitened our
                                         * content: a full repaint need not */
static int cw_car_on;
static int cw_car_x;
static int cw_car_y;

static char cw_fname[16];               /* "" until the document has a name */
static char cw_title[40];               /* "Microsoft Word - NAME" */
static char cw_wname[20];               /* the Window menu's item 1 */
static char cw_num[8];
static char cw_tmp[48];

/* The status line's fields, one shadow each: a field is repainted only when
 * its own text changes. */
static char cw_st_left[44];
static char cw_st_lamp[20];

/* --- the modal panels ----------------------------------------------------
 * A dialog here is a MODE and not a nested event loop, because this operating
 * system delivers input as callbacks and there is nothing to loop on. The
 * overlay draws the panel and owns its state; while cw_dlg is non-zero every
 * key and every click is handed to the overlay instead of to the document, and
 * the overlay answers 0 when the panel has come down. */
#define CW_DLG_CHAR       1
#define CW_DLG_PARA       2
#define CW_DLG_FIND       3
#define CW_DLG_REPLACE    4
#define CW_DLG_GOTO       5
#define CW_DLG_ABOUT      6
#define CW_DLG_SAVE_NEW   7             /* "Do you want to save changes?" and */
#define CW_DLG_SAVE_OPEN  8             /* which command is waiting behind it */
#define CW_DLG_SAVE_EXIT  9

static int cw_dlg;                      /* CW_DLG_*, 0 = none */
static int cw_pending;                  /* the CWA_* a save is running ahead of */
static int cw_quit;                     /* File > Close/Exit was chosen */
static int cw_hired;                    /* the worker is running */
static int cw_dragging;                 /* the button is down in the text */
static struct os88_mouse cw_ms;         /* the tracker's, at file scope
                                         * because `&local` does not build */

static int cw_ln;                       /* the caret's line and column, which
                                         * the status line shows and which are
                                         * derived once per repaint */
static int cw_col;
static int cw_pg;

/* ==========================================================================
 * THE ONE PIECE OF ASSEMBLY (apps/cword/cword.asm, SPEC.md 67.11)
 * ========================================================================*/
void cw_memmove(void *dst, const void *src, unsigned n);

/* ==========================================================================
 * FORWARD DECLARATIONS - this is one translation unit and the chrome, the
 * commands and the engine all reach each other.
 * ========================================================================*/
static void cw_show(void *win, int lo, int hi, int delta);
static void cw_redraw_all(void *win);
static void cw_status(void);
static void cw_ribbon(void);
static void cw_ruler(void);
static void cw_menubar(void);
static void cw_pilcrow(int x, int y, int ink);
static void cw_retitle(void *win);
static int  cw_layout(void *win);
static void cw_wrap(int s, int pap);
static int  cw_pap_of(int para_end);
static void cw_toast(const char *s);

/* THE OVERLAY'S DOORS (SPEC.md 67.14). Every one of these is named `ovl_*`,
 * so tools/cc8086.py emits its code into CWORD.OVL instead of into this
 * segment and turns each call below into a far call preceded by a
 * load-on-demand check.
 *
 * EACH ANSWERS 0 WHEN IT DID NOT HAPPEN, and that is the contract that makes
 * the refusal path free: a module that cannot be loaded - no heap, or a disk
 * without the file - toasts the reason and returns 0 without the call taking
 * place at all, which is indistinguishable here from a dialog the user
 * cancelled. Nothing in this file tests for it and nothing has to.
 *
 * The five things out there are the five that run ONCE PER COMMAND: the
 * dialogs, RTF in and out, search, and the three utilities. Nothing a
 * keystroke touches is among them. */
int ovl_dlg_open(void *win, int which);         /* 1 = the panel is up */
int ovl_dlg_key(void *win, int ascii, int scan);/* 1 = still up, 0 = closed */
int ovl_dlg_click(void *win, int x, int y);
int ovl_dlg_paint(void *win);                   /* after a W_PAINT under it */
int ovl_file_load(void *win, const char *name, unsigned lo, unsigned hi);
int ovl_file_store(void *win, const char *name);
int ovl_find_again(void *win, int back);
int ovl_util(void *win, int which);

/* ==========================================================================
 * SMALL HELPERS
 * ========================================================================*/

static int cw_streq(const char *a, const char *b)
{
    int i;

    for (i = 0; a[i] != 0; i++) {
        if (a[i] != b[i])
            return 0;
    }
    return b[i] == 0;
}

/* cw_pad - copy src into dst and space-fill to n, always NUL terminated. The
 * padding is what stops a shorter string leaving the tail of a longer one on
 * the glass: os88_font_run() letters exactly the cells it is given. */
static void cw_pad(char *dst, const char *src, int n)
{
    int i;

    i = 0;
    while (i < n && src[i] != 0) {
        dst[i] = src[i];
        i++;
    }
    while (i < n)
        dst[i++] = ' ';
    dst[i] = 0;
}

static void cw_toast(const char *s)
{
    os88_toast(s, 0);
}

static void cw_num_at(char *dst, int v, int at)
{
    int i;

    os88_utoa((unsigned)v, cw_num);
    for (i = 0; cw_num[i] != 0; i++)
        dst[at + i] = cw_num[i];
}

/* ==========================================================================
 * THE PARAGRAPH DICTIONARY
 *
 * cw_pap_find() is the only way an entry is made, and it DEDUPLICATES: a
 * document where every paragraph is left-aligned and unindented uses one
 * entry, which is what keeps a 64-entry dictionary sufficient for a real
 * document rather than a toy one.
 * ========================================================================*/

/* out: the index of an entry with these properties, making one if needed, or
 * -1 if the dictionary is full - a refusal the caller reports (SPEC.md 47). */
static int cw_pap_find(int al, int sp, int op, int li, int fi, int ri)
{
    int i;

    for (i = 0; i < cw_pap_n; i++) {
        if (cw_pap_al[i] == al && cw_pap_sp[i] == sp && cw_pap_op[i] == op &&
            cw_pap_li[i] == li && cw_pap_fi[i] == fi && cw_pap_ri[i] == ri)
            return i;
    }
    if (cw_pap_n >= CW_PAP_MAX)
        return -1;
    i = cw_pap_n;
    cw_pap_al[i] = (unsigned char)al;
    cw_pap_sp[i] = (unsigned char)sp;
    cw_pap_op[i] = (unsigned char)op;
    cw_pap_li[i] = (unsigned char)li;
    cw_pap_fi[i] = (signed char)fi;
    cw_pap_ri[i] = (unsigned char)ri;
    cw_pap_n++;
    return i;
}

/* cw_pap_of - the paragraph format in force for the paragraph CONTAINING
 * offset p. It is written on the paragraph's own mark, so this scans forward
 * to that mark; a last paragraph with no mark of its own takes the typing
 * format, which is what makes a document that has never had a mark typed into
 * it look like the ruler says it should.
 *
 * BIT 7 OF A MARK'S ATTRIBUTE BYTE IS NOT FREE, and this is where that shows:
 * the byte on a '\n' is a dictionary index and not a set of CHP bits, so
 * nothing may test CW_A_BOLD on it and nothing may write one there. Every
 * place that walks cw_att[] therefore asks what the character is first. */
static int cw_pap_of(int p)
{
    int i;

    for (i = p; i < cw_len; i++) {
        if (cw_buf[i] == '\n') {
            if (cw_att[i] < CW_PAP_MAX)
                return cw_att[i];
            return 0;
        }
    }
    return cw_pap_now;
}

/* The paragraph mark that carries the format for the paragraph at p, or -1
 * when it is the unterminated last one (whose format is the typing PAP). */
static int cw_pap_mark(int p)
{
    int i;

    for (i = p; i < cw_len; i++) {
        if (cw_buf[i] == '\n')
            return i;
    }
    return -1;
}

/* The text width available to a row of paragraph `pap`: the grid less its
 * indents. Never less than 8 cells, because a document with a silly indent
 * must still be editable. */
static int cw_pap_width(int pap, int first)
{
    int w;

    w = cw_cols - cw_pap_li[pap] - cw_pap_ri[pap];
    if (first)
        w = w - cw_pap_fi[pap];
    if (w < 8)
        w = 8;
    if (w > cw_cols)
        w = cw_cols;
    return w;
}

static int cw_pap_left(int pap, int first)
{
    int x;

    x = cw_pap_li[pap];
    if (first)
        x = x + cw_pap_fi[pap];
    if (x < 0)
        x = 0;
    if (x > cw_cols - 8)
        x = cw_cols - 8;
    return x;
}

/* ==========================================================================
 * LAYOUT
 * ========================================================================*/

/* cw_layout - the live geometry, and the ONE place the chrome's heights turn
 * into coordinates. Everything below measures from this and nothing from the
 * template numbers in os88_main(): the window is clamped onto whatever screen
 * it got (SPEC.md 39), and three of the four strips can be switched off.
 * out: 0, or -1 if the window is not visible - draw nothing. */
static int cw_layout(void *win)
{
    int c;
    int r;
    int y;
    int bot;

    if (os88_wm_geom(win, &cw_sz) != 0)
        return -1;
    os88_wm_content(win, &cw_org);

    cw_tx = cw_org.x + 8;               /* a multiple of 8: the content origin
                                         * is snapped to one, which is what
                                         * puts every os88_font_run() on the
                                         * single-store path at 1bpp (6.1) */
    y = cw_org.y;
    cw_by = y;
    y = y + CW_H_BAR;

    cw_ry = -1;
    if (cw_vrib) {
        cw_ry = y;
        y = y + CW_H_RIB;
    }
    cw_uy = -1;
    if (cw_vrul) {
        cw_uy = y;
        y = y + CW_H_RUL;
    }
    cw_chrome_top = y;
    cw_ty = y + 2;                      /* +2: the italic overstrike draws one
                                         * row ABOVE the first glyph band */

    bot = cw_org.y + cw_sz.h;
    cw_sy = 0;
    if (cw_vsta) {
        cw_sy = bot - 10;
        bot = bot - CW_H_STA;
    }

    c = (cw_sz.w - 16) / 8;
    if (c > CW_COLS_MAX)
        c = CW_COLS_MAX;
    if (c < 8)
        c = 8;
    r = (bot - cw_ty) / CW_PITCH;
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
    return 0;
}

/* cw_wrap - lay out the row that starts at s, in paragraph format `pap`.
 *
 * out: cw_w_end  one past the last character DRAWN on the row
 *      cw_w_next start of the next row, or -1 if this row ends the document
 *
 * The break is Word's: at a paragraph mark, at the last space that fits, or -
 * for a word longer than the line - hard at the margin. The space a line
 * breaks at is consumed, which is why cw_w_next is not always cw_w_end.
 *
 * HIDDEN CHARACTERS still occupy a cell in this pass. Dropping them from the
 * wrap as well as from the drawing is the correct behaviour and it makes the
 * backwards walk (cw_prev_line) disagree with the forwards one unless both
 * skip identically; the draft-view approximation is documented in SPEC.md 65.1
 * and this port keeps it. */
static void cw_wrap(int s, int pap)
{
    int i;
    int lim;
    int c;
    int first;
    int w;

    first = (s == 0 || cw_buf[s - 1] == '\n');
    w = cw_pap_width(pap, first);
    lim = s + w;
    if (lim > cw_len)
        lim = cw_len;

    for (i = s; i < lim; i++) {
        if (cw_buf[i] == '\n') {
            cw_w_end = i + (cw_showp ? 1 : 0);
            cw_w_next = i + 1;
            return;
        }
    }
    if (lim >= cw_len) {
        cw_w_end = cw_len;
        cw_w_next = -1;
        return;
    }

    c = cw_buf[lim];
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

static int cw_para_start(int p)
{
    while (p > 0 && cw_buf[p - 1] != '\n')
        p--;
    return p;
}

/* Going BACKWARDS through a wrap, which is the one direction word wrap does
 * not answer directly. A paragraph is short, so re-wrapping it from its start
 * is cheaper than any structure that would remember the answer. */
static int cw_prev_line(int s)
{
    int p;
    int n;
    int pap;

    if (s <= 0)
        return 0;
    p = cw_para_start(s - 1);
    pap = cw_pap_of(p);
    for (;;) {
        cw_wrap(p, pap);
        n = cw_w_next;
        if (n < 0 || n >= s)
            return p;
        p = n;
    }
}

/* cw_relayout - the visible rows, from row `from` down.
 *
 * THE EARLY STOP IS THE OPTIMISATION THE WHOLE REDRAW PATH RESTS ON. Wrapping
 * is deterministic and depends only on the text from the row's start onwards,
 * so if a row's new start is exactly its old start plus delta, the text from
 * there on is byte for byte what it was, every wrap decision below it is the
 * one it was, and every row below shows what it already shows. Only the
 * recorded offsets need moving.
 *
 * IT ALSO NEEDS THE PARAGRAPH FORMAT TO BE UNCHANGED, which is why cw_lp[] is
 * compared too: an insertion that did not move a row's text can still have
 * changed its indent, and a row that moved sideways has to be redrawn even
 * though it says the same thing. */
static void cw_relayout(int from, int lo, int hi, int delta)
{
    int r;
    int k;
    int s;
    int pap;

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
            cw_lp[r] = 0;
            continue;
        }
        pap = cw_pap_of(s);
        cw_lp[r] = (unsigned char)pap;
        cw_wrap(s, pap);
        cw_le[r] = cw_w_end;
        s = cw_w_next;
    }
}

/* The visible row a document offset is on, or -1 if it is off the top or the
 * bottom. The caret at the very end of a wrapped row belongs to that row and
 * not to the start of the next one, which is what the `<=` says. */
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

/* The x indent, in cells, of visible row r - which is the paragraph's left
 * indent, plus its first-line indent when the row starts a paragraph, plus
 * whatever centring or right-aligning the row's own length asks for.
 *
 * CENTRE AND RIGHT ROUND DOWN TO A WHOLE CELL and not to a pixel, which is
 * draft view's own simplification and is worth a great deal here: every
 * os88_font_run() stays on a byte boundary and takes the single-store path on
 * the two 1bpp adapters (SPEC.md 6.1). Justified records the attribute and
 * renders as left, exactly as the real draft view did. */
static int cw_row_indent(int r, int n)
{
    int pap;
    int first;
    int x;
    int w;

    if (cw_ls[r] < 0)
        return 0;
    pap = cw_lp[r];
    first = (cw_ls[r] == 0 || cw_buf[cw_ls[r] - 1] == '\n');
    x = cw_pap_left(pap, first);
    w = cw_pap_width(pap, first);

    if (cw_pap_al[pap] == CW_AL_CENTER && n < w)
        x = x + (w - n) / 2;
    else if (cw_pap_al[pap] == CW_AL_RIGHT && n < w)
        x = x + (w - n);
    if (x < 0)
        x = 0;
    if (x + n > cw_cols)
        x = cw_cols - n;
    if (x < 0)
        x = 0;
    return x;
}

/* ==========================================================================
 * DRAWING THE TEXT
 *
 * Every routine below is called with the gfx lock ALREADY HELD - from
 * os88_paint(), a key, a click or a menu command (SPEC.md 11).
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
    int n;

    if (cw_car_on || cw_m_open >= 0 || cw_sel_on)
        return;                         /* a menu is over the text, or the
                                         * selection is the caret */
    r = cw_row_of(cw_cur);
    if (r < 0)
        return;
    col = cw_cur - cw_ls[r];
    n = cw_le[r] - cw_ls[r];
    if (col < 0 || col > cw_cols)
        return;
    cw_car_x = cw_tx + (cw_row_indent(r, n) + col) * 8;
    cw_car_y = cw_ty + r * CW_PITCH;
    if (cw_car_x < cw_tx || cw_car_x > cw_tx + cw_cols * 8 - 1)
        return;
    os88_gfx_xor_fill(cw_car_x, cw_car_y, cw_car_x, cw_car_y + 7);
    cw_car_on = 1;
}

/* cw_draw_run - one run of equal-CHP cells of one row.
 *
 * ONE os88_font_run() lays the cells' background and their glyphs in a single
 * pass, so the line is never momentarily blank - the erase-then-letter pair is
 * the canonical double-draw and it is invisible in an emulator.
 *
 * WEIGHT AND SLANT ARE SYNTHESISED, and honestly: the kernel has ONE 8x8 face,
 * so bold is the same run overstruck one pixel right and italic the same run
 * overstruck one pixel up and right. They are approximations of typefaces that
 * do not exist here, they are visibly different from each other and from roman
 * on all three adapters, and each costs ONE extra call - not one per glyph.
 * The three underlines are rules in the gap CW_PITCH leaves: underline under
 * the whole run, word underline under its non-space spans only, double
 * underline as two rules a pixel apart. Small caps was case-mapped when the
 * row was built, and hidden text was dropped there.
 *
 * in:  c0, c1 - the run's columns within the row, inclusive
 *      x0     - the row's leftmost pixel; y - its glyph band top */
static void cw_draw_run(int c0, int c1, int x0, int y, int a)
{
    int i;
    int n;
    int x;
    int w;
    int s;

    n = 0;
    for (i = c0; i <= c1; i++) {
        /* THE PARAGRAPH MARK IS NOT A CHARACTER THIS MACHINE HAS. The face is
         * 32..126 (SPEC.md 6), so the mark rides through the row buffer as
         * CW_MARK and becomes a SPACE in the run - the opaque run still lays
         * its background - and is drawn afterwards by cw_pilcrow(). Leaving
         * the code in the string drew nothing at all, which is what the first
         * build of Show Paragraph Marks did: the button lit and the document
         * looked unchanged. */
        cw_rt[n] = (cw_row[i] == CW_MARK) ? ' ' : cw_row[i];
        n++;
    }
    cw_rt[n] = 0;

    x = x0 + c0 * 8;
    w = n * 8;

    os88_font_run(x, y, cw_rt, OS88_BLACK, OS88_WHITE);
    for (i = c0; i <= c1; i++) {
        if (cw_row[i] == CW_MARK)
            cw_pilcrow(x0 + i * 8, y, 0);
    }

    if (a & (CW_A_BOLD | CW_A_ITAL | CW_A_ULINE | CW_A_WUL | CW_A_DUL))
        os88_set_color(OS88_BLACK);
    if (a & CW_A_BOLD)
        os88_font_str(x + 1, y, cw_rt);
    if (a & CW_A_ITAL)
        os88_font_str(x + 1, y - 1, cw_rt);
    if (a & CW_A_ULINE)
        os88_gfx_hline(x, x + w - 1, y + 8);
    if (a & CW_A_DUL) {
        os88_gfx_hline(x, x + w - 1, y + 8);
        os88_gfx_hline(x, x + w - 1, y + 9);
    }
    if (a & CW_A_WUL) {
        /* under the non-space spans only, which is what makes it a WORD
         * underline. One call per span; a run of ordinary prose is one or two
         * of them, and the alternative - a call per cell - would be 8 to 60. */
        i = 0;
        while (i < n) {
            if (cw_rt[i] == ' ') {
                i++;
                continue;
            }
            s = i;
            while (i < n && cw_rt[i] != ' ')
                i++;
            os88_gfx_hline(x + s * 8, x + i * 8 - 1, y + 8);
        }
    }
}

/* cw_build_row - row r's text and formatting into cw_row[]/cw_rowa[], padded
 * to the full width so that a shortened row erases what it used to show.
 * out: cw_row_ind, the row's left indent in cells; the return is the number of
 *      cells of actual text. */
static int cw_build_row(int r)
{
    int i;
    int n;
    int s;
    int e;
    int c;
    int a;

    s = cw_ls[r];
    e = cw_le[r];
    n = 0;
    if (s >= 0) {
        for (i = s; i < e; i++) {
            if (n >= cw_cols)
                break;
            c = cw_buf[i];
            a = cw_att[i];
            if (c == '\n') {
                /* the paragraph mark, which is shown only when Show is on -
                 * and its attribute byte is a PAP index, not CHP bits */
                if (!cw_showp)
                    break;
                c = CW_MARK;            /* drawn, not lettered */
                a = 0;
            } else {
                if (a & CW_A_HID)
                    continue;           /* hidden: no cell at all */
                if ((a & CW_A_SCAP) && c >= 'a' && c <= 'z')
                    c = c - 32;
            }
            cw_row[n] = (char)c;
            cw_rowa[n] = (unsigned char)(a & CW_A_CHP);
            n++;
        }
    }
    cw_row_ind = cw_row_indent(r, n);
    cw_row_txt = n;                     /* the TEXT, before the padding: a row
                                         * whose pixels are unspecified is
                                         * cleared with one fill and then only
                                         * this many cells are lettered */
    while (n + cw_row_ind < cw_cols) {
        cw_row[n] = ' ';
        cw_rowa[n] = 0;
        n++;
    }
    return n;
}

/* cw_render_row - lay out row r, compare it against the glass, and draw the
 * columns that differ. The comparison is the damage rect in miniature: typing
 * at the end of a line differs in one column and draws one cell. */
static void cw_render_row(int r)
{
    int base;
    int i;
    int n;
    int c0;
    int c1;
    int a;
    int j;
    int y;
    int u0;
    int u1;
    int x0;
    int ind;

    base = r * CW_SH_STRIDE;
    y = cw_ty + r * CW_PITCH;

    n = cw_build_row(r);
    ind = cw_row_ind;
    x0 = cw_tx + ind * 8;

    /* what actually changed. A row whose INDENT moved is redrawn whole: its
     * cells all landed somewhere else, and comparing them against the shadow
     * cell by cell would say "no change" while the pixels say otherwise. */
    /* THE ROW CAME INTO VIEW FROM A SCROLL, so its pixels are whatever
     * os88_gfx_scroll() left there - unspecified, by contract (SPEC.md 5.5),
     * and cw_glass_scroll() marks the row's recorded start -2 to say so. One
     * white fill clears the band AND the two attribute strips outside it, and
     * then only the TEXT is lettered.
     *
     * The alternative is what this did first: mark every cell of the shadow
     * impossible and let the ordinary comparison letter all 71 cells as one
     * opaque run. That is 71 glyph cells - 64 ms on the target - to put a row
     * on the screen that usually holds a few words, and it was the most
     * expensive thing a keystroke could do. This test has to come FIRST in the
     * chain, which is not obvious and is why it is written here: the
     * comparison below would otherwise match on the indent, find every cell
     * different (they are 0xFF), and letter the row full width anyway. */
    if (cw_sh_ok && cw_sh_start[r] == -2) {
        os88_set_color(OS88_WHITE);
        os88_gfx_fill(cw_tx, y - 1, cw_tx + cw_cols * 8 - 1, y + 9);
        c0 = 0;
        c1 = cw_row_txt - 1;
    } else if (cw_sh_ok && cw_sh_ind[r] == (signed char)ind) {
        c0 = -1;
        c1 = -1;
        for (i = 0; i < n; i++) {
            if (cw_sh[base + i] != cw_row[i] ||
                cw_sha[base + i] != cw_rowa[i]) {
                if (c0 < 0)
                    c0 = i;
                c1 = i;
            }
        }
        if (c0 < 0) {
            cw_sh_start[r] = cw_ls[r];
            cw_sh_end[r] = cw_le[r];
            return;
        }
        /* ONE COLUMN OF BLEED AT THE RIGHT END. Bold and italic overstrike a
         * pixel to the right, so a cell whose glyph has ink in its rightmost
         * column puts a pixel into the next one; if the formatting there has
         * just been switched off and that cell is outside the damaged span,
         * nothing repaints it. One extra cell removes it. */
        if (c1 + 1 < n && (cw_sha[base + c1] & (CW_A_BOLD | CW_A_ITAL)))
            c1++;
    } else {
        if (cw_sh_ok && cw_sh_ind[r] != (signed char)ind) {
            /* the row moved sideways: whiten the whole band first, because the
             * new text may be narrower than the old and no font_run of it
             * would cover where the old one was */
            os88_set_color(OS88_WHITE);
            os88_gfx_fill(cw_tx, y - 1, cw_tx + cw_cols * 8 - 1, y + 9);
        }
        c0 = 0;
        c1 = n - 1;
    }
    if (c1 < c0)
        return;

    /* THE INK OUTSIDE THE GLYPH BAND. os88_font_run() repaints rows y..y+7, so
     * the attributes that draw outside it - italic one row above, the three
     * underlines one and two rows below - would leave a stale line behind when
     * switched off. The shadow says exactly where the old ones were, so this
     * whitens that span and only that span. */
    if (cw_sh_ok && cw_sh_ind[r] == (signed char)ind) {
        u0 = -1;
        u1 = -1;
        for (i = c0; i <= c1; i++) {
            if (cw_sha[base + i] & (CW_A_ULINE | CW_A_WUL | CW_A_DUL)) {
                if (u0 < 0)
                    u0 = i;
                u1 = i;
            }
        }
        if (u0 >= 0) {
            os88_set_color(OS88_WHITE);
            os88_gfx_fill(x0 + u0 * 8, y + 8, x0 + u1 * 8 + 7, y + 9);
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
            os88_gfx_hline(x0 + u0 * 8, x0 + u1 * 8 + 8, y - 1);
        }
    }

    /* draw the damaged span, one call per run of equal formatting */
    i = c0;
    while (i <= c1) {
        a = cw_rowa[i];
        j = i;
        while (j <= c1 && cw_rowa[j] == a)
            j++;
        cw_draw_run(i, j - 1, x0, y, a);
        i = j;
    }

    /* the glass now says this */
    for (i = 0; i < n; i++) {
        cw_sh[base + i] = cw_row[i];
        cw_sha[base + i] = cw_rowa[i];
    }
    for (i = n; i < cw_cols; i++) {
        cw_sh[base + i] = ' ';
        cw_sha[base + i] = 0;
    }
    cw_sh_start[r] = cw_ls[r];
    cw_sh_end[r] = cw_le[r];
    cw_sh_ind[r] = (signed char)ind;
}

/* --- the selection -------------------------------------------------------
 * XOR over the cells between the anchor and the caret, drawn as one call per
 * visible row rather than one per cell. It is put on and taken off around
 * every change, so the shadow never has to know about it: the glass under an
 * XOR is recoverable by XORing again, which is the whole reason to use it. */
static void cw_sel_bounds(void)
{
    if (cw_anchor <= cw_cur) {
        cw_w_end = cw_anchor;           /* borrowed as scratch: both are set
                                         * and read in the same breath here */
        cw_w_next = cw_cur;
    } else {
        cw_w_end = cw_cur;
        cw_w_next = cw_anchor;
    }
}

static void cw_sel_xor(void)
{
    int r;
    int a;
    int b;
    int s;
    int e;
    int n;
    int x0;

    if (!cw_sel_on)
        return;
    cw_sel_bounds();
    a = cw_w_end;
    b = cw_w_next;
    if (a == b)
        return;
    for (r = 0; r < cw_rows; r++) {
        s = cw_ls[r];
        e = cw_le[r];
        if (s < 0)
            break;
        if (b <= s || a > e)
            continue;
        n = cw_build_row(r);
        x0 = cw_tx + cw_row_ind * 8;
        s = (a > s) ? a - cw_ls[r] : 0;
        e = (b < e) ? b - cw_ls[r] : e - cw_ls[r];
        if (e > n)
            e = n;
        if (e <= s)
            continue;
        os88_gfx_xor_fill(x0 + s * 8, cw_ty + r * CW_PITCH,
                          x0 + e * 8 - 1, cw_ty + r * CW_PITCH + 7);
    }
}

#include "cwchrome.c"                   /* the bar, the ribbon, the ruler and
                                         * the status line - drawing only */

/* ==========================================================================
 * THE REPAINT
 * ========================================================================*/

/* cw_glass_scroll - move the text band by one row instead of redrawing it
 * (SPEC.md 5.5). Four calls and one row of glyphs, against a redraw of the
 * whole view.
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
    y2 = cw_ty + (cw_rows - 1) * CW_PITCH + 9;
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
            cw_sh_ind[i] = cw_sh_ind[i + 1];
        }
        dst = cw_rows - 1;
    } else {
        cw_memmove(cw_sh + CW_SH_STRIDE, cw_sh, (unsigned)cnt);
        cw_memmove(cw_sha + CW_SH_STRIDE, cw_sha, (unsigned)cnt);
        for (i = cw_rows - 1; i > 0; i--) {
            cw_sh_start[i] = cw_sh_start[i - 1];
            cw_sh_end[i] = cw_sh_end[i - 1];
            cw_sh_ind[i] = cw_sh_ind[i - 1];
        }
        dst = 0;
    }

    /* THE ROW THAT CAME INTO VIEW holds whatever was under it - gfx_scroll
     * invents no pixels. Marking its shadow with a character no text can equal
     * AND every attribute bit set does the whole job: the character makes
     * cw_render_row() repaint every cell, and the attributes make its strip
     * erase whiten the two pixel rows outside the glyph band that no
     * os88_font_run() covers. */
    src = dst * CW_SH_STRIDE;
    for (i = 0; i < cw_cols; i++) {
        cw_sh[src + i] = (char)0xFF;
        cw_sha[src + i] = 0xFF;
    }
    cw_sh_start[dst] = -2;
    cw_sh_end[dst] = -2;
    cw_sh_ind[dst] = 0;
    return 0;
}

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
            cw_wrap(cw_top, cw_pap_of(cw_top));
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

/* cw_caret_place - the Ln/Col/Pg the status line shows. Derived once per
 * repaint and not per keystroke, and NOT by walking the document: the line
 * number is counted from the top of the view, which is what a draft-view
 * status line means by it. A page is 54 lines, which is what makes At and Ln
 * agree by construction (SPEC.md 65.2). */
static void cw_caret_place(void)
{
    int r;
    int i;
    int ln;

    r = cw_row_of(cw_cur);
    cw_col = 1;
    if (r >= 0)
        cw_col = cw_cur - cw_ls[r] + 1;

    ln = 1;
    for (i = 0; i < cw_cur && i < cw_len; i++) {
        if (cw_buf[i] == '\n')
            ln++;
    }
    cw_ln = ln;
    cw_pg = (ln - 1) / 54 + 1;
}

/* cw_show - the single repaint entry point for the TEXT.
 *
 * in:  lo, hi - the document range that changed, and delta how far the text
 *      after it moved. lo = -1 means "everything", lo = -2 "nothing changed,
 *      only the caret moved". The gfx lock is held by the caller. */
static void cw_show(void *win, int lo, int hi, int delta)
{
    int r;

    if (cw_layout(win) != 0) {
        cw_sh_ok = 0;
        cw_car_on = 0;
        return;
    }
    if (os88_wm_obscured(win)) {
        cw_sh_ok = 0;
        cw_car_on = 0;
        return;
    }

    cw_caret_off();
    cw_sel_xor();                       /* off, if it was on */

    if (lo == -1)
        cw_sh_ok = 0;
    if (!cw_sh_ok) {
        cw_from = 0;
        lo = -1;
        cw_ls[0] = cw_top;
        cw_relayout(0, -1, 0, 0);
    } else {
        cw_from = 0;
        if (lo >= 0) {
            cw_from = cw_row_from(lo);
            /* AND THE ROW BEFORE IT. A row's line break is decided by the
             * character one PAST its own end - the wrap looks at
             * cw_buf[start + width] to see whether the word that did not fit
             * begins with a space - so an edit inside row N can change row
             * N-1's break, and starting the relayout at the row that CONTAINS
             * the edit leaves that row on the glass one word short.
             *
             * Found by apps/cword/hosttest/cwuitest.c, which is what that
             * harness is for: five backspaces after five inserts, and the
             * fifth one made a word fit on the row above again. The glass kept
             * `...three times` where the document said `...three times acr`
             * for the rest of the session - a stale cell, which PERFORMANCE.md
             * names as one of the three defects an emulator cannot show you.
             * It costs one extra row laid out and compared, which finds no
             * difference and draws nothing. */
            if (cw_from > 0)
                cw_from--;
        }
        if (lo == -2) {
            cw_stop = 0;
            cw_from = 0;
        } else {
            cw_relayout(cw_from, lo, hi, delta);
        }
    }

    cw_follow_caret();

    if (!cw_sh_ok) {
        if (!cw_wiped) {
            os88_set_color(OS88_WHITE);
            os88_gfx_fill(cw_tx, cw_ty - 1, cw_tx + cw_cols * 8 - 1,
                          cw_ty + (cw_rows - 1) * CW_PITCH + 9);
        }
        cw_menubar();
        cw_ribbon();
        cw_ruler();
    }
    cw_wiped = 0;

    for (r = cw_from; r < cw_stop; r++)
        cw_render_row(r);
    cw_sh_ok = 1;

    cw_caret_place();
    cw_status();
    cw_sel_xor();                       /* back on */
    cw_caret_on();
}

/* Everything: after a View toggle, a New, an Open, a dialog coming down. */
static void cw_redraw_all(void *win)
{
    cw_sh_ok = 0;
    cw_car_on = 0;
    cw_st_left[0] = 0;
    cw_st_lamp[0] = 0;
    cw_show(win, -1, 0, 0);
}

#include "cwdrop.c"                     /* the dropdown menus: paint, hit test,
                                         * highlight, and the repaint that
                                         * takes one back off the glass */

/* ==========================================================================
 * EDITING
 * ========================================================================*/

static void cw_touch(void)
{
    cw_mod = 1;
}

/* cw_insert - one character at the caret in the current formatting. The tail
 * of both arrays moves with the hand-written mover; at the end of the
 * document, which is where typing happens, it moves nothing. */
static int cw_insert(int c, int a)
{
    unsigned n;

    if (cw_len >= CW_DOC_MAX)
        return 0;
    n = (unsigned)(cw_len - cw_cur);
    if (n != 0) {
        cw_memmove(cw_buf + cw_cur + 1, cw_buf + cw_cur, n);
        cw_memmove(cw_att + cw_cur + 1, cw_att + cw_cur, n);
    }
    cw_buf[cw_cur] = (char)c;
    cw_att[cw_cur] = (unsigned char)a;
    cw_len++;
    return 1;
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

/* --- the selection, as an edit ------------------------------------------- */

static int cw_sel_kill(void *win)
{
    int a;
    int b;
    int i;

    if (!cw_sel_on)
        return 0;
    cw_sel_bounds();
    a = cw_w_end;
    b = cw_w_next;
    cw_sel_on = 0;
    cw_ext = 0;
    if (a == b)
        return 0;
    for (i = b - 1; i >= a; i--)
        cw_remove(i);
    cw_cur = a;
    cw_touch();
    cw_show(win, a, b, a - b);
    return 1;
}

/* cw_moved - the selection follows the caret in extend mode, and is dropped
 * by any plain move. */
static void cw_moved(void *win, int was)
{
    (void)was;
    if (cw_ext) {
        if (!cw_sel_on) {
            cw_sel_on = 1;
            cw_anchor = was;
        }
    } else if (cw_sel_on) {
        cw_sel_on = 0;
    }
    cw_show(win, -2, 0, 0);
}

static void cw_type(void *win, int c)
{
    int lo;

    if (cw_sel_on)
        cw_sel_kill(win);
    if (cw_len >= CW_DOC_MAX) {
        cw_toast("This document is full.");
        return;
    }
    /* OVERTYPE replaces the character under the caret except at a paragraph
     * mark or the end of the document, where it inserts - which is what Word
     * does, and what keeps a paragraph from being eaten by a typo. */
    if (cw_ovr && cw_cur < cw_len && cw_buf[cw_cur] != '\n') {
        cw_buf[cw_cur] = (char)c;
        cw_att[cw_cur] = (unsigned char)cw_attr;
        lo = cw_cur;
        cw_cur++;
        cw_touch();
        cw_show(win, lo, cw_cur, 0);
        return;
    }
    lo = cw_cur;
    if (!cw_insert(c, cw_attr))
        return;
    cw_cur++;
    cw_touch();
    cw_show(win, lo, cw_cur, 1);
}

/* A paragraph mark carries the paragraph's format, so typing Enter makes a
 * NEW mark that ends the paragraph being typed - and it inherits the format
 * the caret is in, which is what makes a run of paragraphs keep their indent. */
static void cw_type_para(void *win)
{
    int lo;
    int pap;

    if (cw_sel_on)
        cw_sel_kill(win);
    if (cw_len >= CW_DOC_MAX) {
        cw_toast("This document is full.");
        return;
    }
    pap = cw_pap_of(cw_cur);
    lo = cw_cur;
    if (!cw_insert('\n', pap))
        return;
    cw_cur++;
    cw_touch();
    cw_show(win, lo, cw_cur, 1);
}

static void cw_backspace(void *win)
{
    if (cw_sel_kill(win))
        return;
    if (cw_cur <= 0)
        return;
    cw_cur--;
    cw_remove(cw_cur);
    cw_touch();
    cw_show(win, cw_cur, cw_cur + 1, -1);
}

static void cw_del(void *win)
{
    if (cw_sel_kill(win))
        return;
    if (cw_cur >= cw_len)
        return;
    cw_remove(cw_cur);
    cw_touch();
    cw_show(win, cw_cur, cw_cur + 1, -1);
}

/* --- caret movement ------------------------------------------------------ */

static void cw_up(void)
{
    int r;
    int col;
    int s;

    r = cw_row_of(cw_cur);
    if (r < 0)
        return;
    col = cw_cur - cw_ls[r];
    if (r > 0) {
        s = cw_ls[r - 1];
        cw_cur = s + col;
        if (cw_cur > cw_le[r - 1])
            cw_cur = cw_le[r - 1];
    } else {
        s = cw_prev_line(cw_ls[0]);
        if (s == cw_ls[0])
            return;
        cw_wrap(s, cw_pap_of(s));
        cw_cur = s + col;
        if (cw_cur > cw_w_end)
            cw_cur = cw_w_end;
    }
    if (cw_cur < 0)
        cw_cur = 0;
}

static void cw_down(void)
{
    int r;
    int col;
    int s;

    r = cw_row_of(cw_cur);
    if (r < 0)
        return;
    col = cw_cur - cw_ls[r];
    if (r + 1 < cw_rows && cw_ls[r + 1] >= 0) {
        s = cw_ls[r + 1];
        cw_cur = s + col;
        if (cw_cur > cw_le[r + 1])
            cw_cur = cw_le[r + 1];
    } else {
        cw_wrap(cw_ls[r], cw_lp[r]);
        if (cw_w_next < 0)
            return;
        s = cw_w_next;
        cw_wrap(s, cw_pap_of(s));
        cw_cur = s + col;
        if (cw_cur > cw_w_end)
            cw_cur = cw_w_end;
    }
    if (cw_cur > cw_len)
        cw_cur = cw_len;
}

/* ==========================================================================
 * FORMATTING COMMANDS
 * ========================================================================*/

/* cw_chp_apply - set or clear CHP bits over the selection, or - with no
 * selection - on what is typed next, which is what Word does with an
 * insertion point. `how`: 0 = toggle, 1 = set these, 2 = clear ALL, 3 = clear
 * these. The Format Character dialog uses 1 and 3 in that order, which is how
 * a dialog that shows seven boxes applies all seven answers rather than only
 * the ones that are ticked.
 * out: the range that changed, through cw_show(). */
static void cw_chp_apply(void *win, int bits, int how)
{
    int a;
    int b;
    int i;
    int on;

    if (!cw_sel_on) {
        if (how == 2)
            cw_attr = 0;
        else if (how == 1)
            cw_attr = cw_attr | bits;
        else if (how == 3)
            cw_attr = cw_attr & ~bits;
        else
            cw_attr = cw_attr ^ bits;
        cw_ribbon();
        cw_status();
        return;
    }
    cw_sel_bounds();
    a = cw_w_end;
    b = cw_w_next;

    /* A TOGGLE OVER A SPAN ASKS THE SPAN, not the caret: if every character
     * already has the bit, the command takes it off; otherwise it puts it on
     * everywhere. That is what makes bolding a half-bold sentence do the
     * obvious thing rather than invert it. */
    on = 1;
    for (i = a; i < b; i++) {
        if (cw_buf[i] == '\n')
            continue;
        if ((cw_att[i] & bits) != bits) {
            on = 0;
            break;
        }
    }
    for (i = a; i < b; i++) {
        if (cw_buf[i] == '\n')
            continue;               /* a mark's byte is a PAP index */
        if (how == 2)
            cw_att[i] = 0;
        else if (how == 3)
            cw_att[i] = (unsigned char)(cw_att[i] & ~bits);
        else if (how == 1 || !on)
            cw_att[i] = (unsigned char)(cw_att[i] | bits);
        else
            cw_att[i] = (unsigned char)(cw_att[i] & ~bits);
    }
    cw_touch();
    cw_show(win, a, b, 0);
    cw_ribbon();
}

/* cw_pap_apply - change the paragraph format of every paragraph the selection
 * touches, or of the one the caret is in. `what` names the field and `v` the
 * value; -1 for a field means "leave it".
 *
 * It goes through cw_pap_find(), so two paragraphs given the same format share
 * one dictionary entry and the 65th UNIQUE format is the refusal, not the 65th
 * paragraph. */
static void cw_pap_apply(void *win, int al, int sp, int op, int li, int fi,
                         int ri)
{
    int a;
    int b;
    int i;
    int m;
    int p;
    int nal;
    int nsp;
    int nop;
    int nli;
    int nfi;
    int nri;
    int idx;
    int touched;

    if (cw_sel_on) {
        cw_sel_bounds();
        a = cw_w_end;
        b = cw_w_next;
    } else {
        a = cw_cur;
        b = cw_cur;
    }
    a = cw_para_start(a);
    touched = 0;

    for (i = a; i <= b; i++) {
        m = cw_pap_mark(i);
        p = (m >= 0 && cw_att[m] < CW_PAP_MAX) ? cw_att[m] : cw_pap_now;

        nal = (al < 0) ? cw_pap_al[p] : al;
        nsp = (sp < 0) ? cw_pap_sp[p] : sp;
        nop = (op < 0) ? cw_pap_op[p] : op;
        nli = (li == -128) ? cw_pap_li[p] : li;
        nfi = (fi == -128) ? cw_pap_fi[p] : fi;
        nri = (ri == -128) ? cw_pap_ri[p] : ri;
        if (nli < 0)
            nli = 0;
        if (nri < 0)
            nri = 0;
        if (nli > cw_cols - 8)
            nli = cw_cols - 8;

        idx = cw_pap_find(nal, nsp, nop, nli, nfi, nri);
        if (idx < 0) {
            cw_toast("Too many different paragraph formats.");
            break;
        }
        if (m >= 0)
            cw_att[m] = (unsigned char)idx;
        else
            cw_pap_now = idx;               /* the unterminated last paragraph */
        touched = 1;

        if (m < 0)
            break;
        i = m;                          /* on to the next paragraph */
    }
    if (!touched)
        return;
    cw_touch();
    cw_show(win, -1, 0, 0);
    cw_ruler();
}

/* The ruler's and the keyboard's alignment/spacing/indent commands, each one
 * line through cw_pap_apply(). */
static void cw_align(void *win, int al)   { cw_pap_apply(win, al, -1, -1, -128, -128, -128); }
static void cw_spacing(void *win, int sp) { cw_pap_apply(win, -1, sp, -1, -128, -128, -128); }
static void cw_openspc(void *win, int op) { cw_pap_apply(win, -1, -1, op, -128, -128, -128); }

static void cw_indent(void *win, int by)
{
    int p;

    p = cw_pap_of(cw_cur);
    cw_pap_apply(win, -1, -1, -1, cw_pap_li[p] + by, -128, -128);
}

static void cw_hang(void *win, int on)
{
    int p;

    p = cw_pap_of(cw_cur);
    if (on)
        cw_pap_apply(win, -1, -1, -1, cw_pap_li[p] + 5, -5, -128);
    else
        cw_pap_apply(win, -1, -1, -1, -128, 0, -128);
}

#include "cwcmd.c"                      /* the command dispatcher, the mouse
                                         * and the keyboard map */
#include "cwovl.c"                      /* ...and everything that runs once per
                                         * command, which is in the OTHER
                                         * segment (SPEC.md 67.14) */

/* ==========================================================================
 * THE CALLBACKS
 * ========================================================================*/

void os88_paint(void *win)
{
    cw_wiped = 1;                       /* the kernel whitened our content */
    cw_sh_ok = 0;
    cw_car_on = 0;
    cw_st_left[0] = 0;
    cw_st_lamp[0] = 0;
    cw_m_open = -1;                     /* a dropdown does not survive a
                                         * repaint it did not ask for */
    cw_show(win, -1, 0, 0);
    if (cw_dlg)
        ovl_dlg_paint(win);             /* the panel was over all of that */

    /* The worker is hired at the FIRST paint and not in os88_main(), because
     * os88_task_spawn() requires the gfx lock held and refuses before the
     * instance is published (SPEC.md 20.6). A refusal is normal and transient
     * - the task table is 12 slots - so this retries on the next paint rather
     * than treating it as fatal. */
    if (!cw_hired && os88_task_spawn(win) == 0)
        cw_hired = 1;
}

void *os88_main(void)
{
    void *win;

    cw_pap_find(CW_AL_LEFT, 0, 0, 0, 0, 0);     /* entry 0, the default */
    cw_doc_clear();
    cw_cur = 0;
    cw_top = 0;
    os88_strcpy(cw_title, "Microsoft Word - Document", sizeof(cw_title));
    os88_strcpy(cw_wname, "1 Document", sizeof(cw_wname));

    win = os88_wm_create(24, 40, 592, 384, cw_title);
    if (win == 0)
        return 0;

    /* Content origin on a multiple of 8, so every os88_font_run() takes the
     * single-store path on the two 1bpp adapters (SPEC.md 6.1, 11.94). */
    os88_wm_snap(win, 1);
    os88_wm_sizable(win, 1);

    /* The kernel's bar carries the app NAME and nothing else: this program's
     * nine menus are drawn inside its own window because MENU_APPMAX is five
     * (SPEC.md 12.2, 65.2). About there opens the same box Help > About does. */
    os88_about_set(win);
    os88_assoc_set("RTF", "CWORD");     /* double-clicking a document on the
                                         * desktop launches this (SPEC.md 54.5) */
    os88_wm_onmouseup(win);             /* the release half of a drag */
    return win;
}
