/* ============================================================================
 * os8088 - apps/pagedesk/pdu.h     PageDesk's primary: constants and state
 *
 * PAGEDESK-SPEC 2.2's primary segment is ONE translation unit (SPEC.md 73.1):
 * pagedesk.c #includes the pdu*.c parts after this header, and every piece of
 * mutable state lives here, at file scope. A package is loaded once per
 * instance, so file scope IS per-instance state - two PageDesk windows share
 * no document, view, job or claim (PD-01).
 *
 * Built twice: on the target by SmallerC (pagedesk.c), and pdulay.c alone on
 * the host under PD_HOST by hosttest/pdu_lay.c, the layout's own test.
 * ==========================================================================*/
#ifndef PDU_H
#define PDU_H

#ifndef PD_HOST
#include "os88.h"
#endif
#include "pdabi.h"

/* --- the parts table (pagedesk.asm's %if holds these) ------------------- */
#define PDU_PART_ENG   0
#define PDU_PART_REND  1
#define PDU_PART_WRT   2
#define PDU_PART_RES   3          /* PDRES [0, 65024)            */
#define PDU_PART_RES2  4          /* PDRES [65024, end), adjacent */

/* --- module vectors (pdui.inc's pdu_vec) --------------------------------- */
#define PDU_M_ENG   0
#define PDU_M_REND  1
#define PDU_M_WRT   2

/* --- os88ui.inc's scroll-bar answers (pagedesk.asm's %if holds these) ---- */
#define PDU_SB_NONE  0
#define PDU_SB_UP    1
#define PDU_SB_DOWN  2
#define PDU_SB_PGUP  3
#define PDU_SB_PGDN  4
#define PDU_SB_THUMB 5

/* --- layouts (PAGEDESK-SPEC 17.1) ----------------------------------------- */
#define PDU_L_FULL     0
#define PDU_L_MID      1
#define PDU_L_COMPACT  2

/* the thresholds, on the live CONTENT box (never the adapter) */
#define PDU_FULL_W   600
#define PDU_FULL_H   260        /* 17.1 says 300; 20.6.4 says why 260 */
#define PDU_MID_H    240

/* --- metrics (20.6.4) ----------------------------------------------------- */
#define PDU_CW       8          /* the kernel's 8x8 cell */
#define PDU_BAR_F    16         /* a FULL/MID bar row */
#define PDU_BAR_C    12         /* the COMPACT combined bar */
#define PDU_STAT_F   12         /* the status line */
#define PDU_STAT_C   10
#define PDU_SIDE_W   136        /* Pages/Outline sidebar, a multiple of 8 */
#define PDU_NOTE_W   160        /* Notes & edits, a multiple of 8 */
#define PDU_SBAR_W   16         /* the canvas's vertical bar */
#define PDU_GROW_W   16         /* the grow box the status line stops short of */
#define PDU_MARGIN   8          /* desk around the page, each side */
#define PDU_MINCANVAS 160       /* below this the panels fold away */

/* --- controls ------------------------------------------------------------- */
#define C_NONE      0
#define C_OPEN      1
#define C_SAVEAS    2
#define C_UNDO      3
#define C_REDO      4
#define C_PREV      5
#define C_PAGE      6           /* the n/N field */
#define C_NEXT      7
#define C_ZOUT      8
#define C_ZOOM      9           /* the zoom field: opens the zoom list */
#define C_ZIN       10
#define C_FIT       11          /* Fit: opens the zoom list */
#define C_FIND      12
#define C_M_READ    13          /* the four modes, in order */
#define C_M_ANNOT   14
#define C_M_EDIT    15
#define C_M_PAGES   16
#define C_T_SELECT  17          /* tools */
#define C_T_HILITE  18
#define C_T_NOTE    19
#define C_T_TEXT    20
#define C_T_MARK    21
#define C_T_MORE    22
#define C_T_INK     23          /* More's */
#define C_T_LINE    24
#define C_T_RECT    25
#define C_T_ELLIPSE 26
#define C_T_SIGN    27
#define C_E_DELETE  28          /* Edit mode */
#define C_P_ROTL    29          /* Pages mode */
#define C_P_ROTR    30
#define C_P_DELETE  31
#define C_P_BLANK   32
#define C_P_EXTRACT 33
#define C_SB_PAGES  34          /* sidebar */
#define C_SB_OUTL   35
#define C_SB_HIDE   36
#define C_NP_CLOSE  37          /* notes panel */
#define C_NP_FILTER 38
#define C_PANEL_PG  39          /* COMPACT: the page chooser */
#define C_PANEL_NT  40          /* COMPACT: the notes popover */
#define C_EV_OPEN   41          /* the empty view's two buttons */
#define C_EV_SAMPLE 42
#define C_NCTLID    43

#define PDU_MAXCTL  40          /* rects laid out at once */

/* control flags, per laid-out control */
#define CF_FIELD    1           /* drawn as a field (sunken), not a button */
#define CF_BAR1     2           /* on the first (file) bar */
#define CF_BAR2     4           /* on the mode bar */
#define CF_PANEL    8           /* on a panel */
#define CF_EMPTY    16          /* on the empty view */

struct pdu_ctl {
    int id;
    int x1, y1, x2, y2;         /* absolute, inclusive */
    int flags;
};

/* --- regions (absolute, inclusive; x1 > x2 = absent) --------------------- */
struct pdu_lay {
    int kind;                   /* PDU_L_* */
    int ox, oy, w, h;           /* content origin and size */
    struct os88_rect bar1;      /* file/navigation bar (FULL/MID) */
    struct os88_rect bar2;      /* mode bar, or COMPACT's combined bar */
    struct os88_rect side;      /* Pages/Outline */
    struct os88_rect notes;     /* Notes & edits */
    struct os88_rect canvas;    /* the page view; x1 and x2+1 multiples of 8 */
    struct os88_rect sbar;      /* its vertical bar */
    struct os88_rect status;
    int nctl;
    struct pdu_ctl ctl[PDU_MAXCTL];
};

/* --- modes, tools, popovers ---------------------------------------------- */
#define PDU_MODE_READ   0
#define PDU_MODE_ANNOT  1
#define PDU_MODE_EDIT   2
#define PDU_MODE_PAGES  3

#define P_NONE   0
#define P_ZOOM   1
#define P_MORE   2
#define P_FIND   3
#define P_PAGES  4
#define P_NOTES  5
#define P_HELP   6              /* an overlay card in the canvas */

/* --- the document ---------------------------------------------------------- */
#define D_EMPTY   0
#define D_OPENING 1
#define D_OPEN    2
#define D_FAILED  3

/* --- jobs (PAGEDESK-SPEC 15) ---------------------------------------------- */
#define J_NONE    0
#define J_OPEN    1
#define J_RENDER  2
#define J_SAVE    3

/* ---- notes ---- */
#define P_NOTE   7              /* the note popover / inspector (pdunotes.c) */
#ifndef PD_HOST
/* resident hooks (pdunotes.c) */
static void pdu_notes_paint(void);      /* the panel */
static void pdu_notes_page(void);
static void pdu_nt_reset(void);
static void pdu_nt_filter(void);
static void pdu_nt_idle(void);
static void pdu_nt_srcload(void);
static int  pdu_nt_owed(void);
static void pdu_nt_pclose(void);
static int  pdu_nt_click(int x, int y);
static int  pdu_nt_pclick(int x, int y);
static int  pdu_nt_key(int ascii, int scan);
static void pdu_nt_ppaint(void);
static int  pdu_nt_pw(void);
static int  pdu_nt_prows(void);
/* the overlay's entry points (pdovlnotes.c, pduinsp.c) */
static int  ovl_nt_body(void);
static int  ovl_nt_click(int x, int y);
static int  ovl_nt_pclick(int x, int y);
static int  ovl_nt_key(int ascii, int scan);
static int  ovl_nt_ppaint(void);
static void ovl_nt_pclose(void);
static void ovl_nt_page(void);
static void ovl_nt_dirty(void);
static void ovl_nt_idle(void);
static void ovl_nt_relist(void);
static void ovl_nt_srcload(void);
static void ovl_nt_new(unsigned page, const struct pd_rect *r);
static void ovl_nt_open(int id);
static void ovl_nt_inspect(int id);
/* WHAT EDIT CALLS (D19, 20.9.5) - real resident functions with exactly the
 * prototypes edit's block declares, so the two blocks may sit in either
 * order; PDU_NOTES switches off edit's own stand-ins (pduedit.c). Nobody
 * calls notes to report a change: the panel polls pdu_ed_gen(). */
#define PDU_NOTES 1
void     pdu_nt_new(unsigned page_id, const struct pd_rect *r); /* the Note tool */
void     pdu_nt_open(int i);                                    /* a note's popover */
void     pdu_nt_inspect(int i);                                 /* Text properties */
#endif
/* ---- end notes ---- */

#endif /* PDU_H */
