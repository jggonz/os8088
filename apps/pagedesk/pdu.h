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
#define PDU_PART_RENDC 2          /* PDREND's code part, eager (20.22.7.9) */
#define PDU_PART_WRT   3
#define PDU_PART_RES   4          /* PDRES [0, 65024)            */
#define PDU_PART_RES2  5          /* PDRES [65024, end), adjacent */
#define PDU_PART_XK    6          /* the slot's occupants (20.22.2.3) */
#define PDU_PART_XG    7
#define PDU_PART_XF    8
#define PDU_PART_XI    9
#define PDU_PART_XJ    10
#define PDU_PART_X8    11         /* PDX8, read into the X8 region (20.22.10.11);
                                   * the overlay is row 12, last */

/* --- module vectors (pdui.inc's pdu_vec) --------------------------------- */
#define PDU_M_ENG   0
#define PDU_M_REND  1
#define PDU_M_SLOT  2          /* the slot's occupant, PDWRT among them */
#define PDU_M_WRT   PDU_M_SLOT
#define PDU_OCC_WRT 6          /* PDWRT, the slot's sixth occupant (20.22.2.1) */
#define PDU_NOCC    7          /* occupant ids 1..6 (PDX_KEY..PDX_JPX, PDWRT) */

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
#define PDU_BAR_F    16         /* the first build's bar row: the empty view's card */
#define PDU_BAR1_F   24         /* 20.21.4: the file bar (FULL/MID) */
#define PDU_BAR2_F   22         /* ...and the mode bar */
#define PDU_BTN_H    20         /* a raised button, either bar */
#define PDU_BAR_C    12         /* the COMPACT combined bar */
#define PDU_STAT_F   14         /* the status line (20.21.8) */
#define PDU_STAT_C   10
#define PDU_SIDE_HEAD 26        /* the Pages panel's tab row (20.21.6) */
#define PDU_SIDE_FOOT 24        /* ...and its foot: "N pages" and put away */
#define PDU_NOTE_HEAD 50        /* Notes & edits: the title and the filter (20.21.7) */
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
#define C_PGOF      43          /* 20.21.1: "of 12" beside the page field */
#define C_NCTLID    44

#define PDU_MAXCTL  48          /* rects laid out at once (separators too) */

/* control flags, per laid-out control */
#define CF_FIELD    1           /* drawn as a field (sunken), not a button */
#define CF_BAR1     2           /* on the first (file) bar */
#define CF_BAR2     4           /* on the mode bar */
#define CF_PANEL    8           /* on a panel */
#define CF_EMPTY    16          /* on the empty view */
#define CF_SEP      32          /* 20.21.4: a group's etched rule (id C_NONE) */
#define CF_FLAT     64          /* a tool: no frame until it is the active one */
#define CF_ICON     128         /* a picture leads the caption (with the kit) */
#define CF_NOCAP    256         /* ...and stands alone: no caption */
#define CF_DROP     512         /* the caption ends in the down triangle */
#define CF_LABEL    1024        /* plain text on the ground, not a control */

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

/* ---- nav: PD-09 navigation, PD-10 selection and Find, PD-11 thumbnails,
 * outline and links (PAGEDESK-SPEC 20.10) ---------------------------------- */
#define J_FIND    4             /* a Find across pages (15.1, D14c)          */
#define J_TEXT    5             /* one text pass: pick, word, quads, copy    */
#define J_THUMB   6             /* one thumbnail                             */

/* the SCRATCH claim (PDU_DC_SCRATCH, 2 KiB) is cut in two */
#define PDU_SC_QUADS   0        /* 96 selection quads, 16 bytes each         */
#define PDU_SC_QMAX    96
#define PDU_SC_LINKS   1536     /* 42 links of 12 bytes: rect, dest, index   */
#define PDU_SC_LMAX    42
#define PDU_TH_SLOTS   8        /* 8 cells of 512 bytes in PDU_DC_THUMB      */

#ifndef PD_HOST
#define PDU_NAV   1             /* pduedit.c's selection stand-ins go       */
/* the cross-file calls nav makes and answers (all static, one TU) */
static int  pdu_sel_active(void);
static void ovl_sel_clear(void);
static void pdu_sel_drop(void);
static void pdu_tx_aborted(void);
static void pdu_sel_after(struct os88_rect *r);
static int  ovl_sel_quads(pd_pt *dst, unsigned cap);
static int  ovl_sel_text(char *dst, unsigned cap);
static int  pdu_side_sel(void);
static void ovl_copy(void);
static void ovl_selall(void);
static void pdu_find_run(int again);
static void ovl_find_prev(void);
static int  pdu_nav_press(int x, int y);
static int  pdu_nav_thumb(int y);
static int  pdu_nav_owed(void);
static void ovl_nav_work(void);
static void pdu_nav_page(void);
static void pdu_nav_noted(int n);
static void ovl_side_click(int x, int y);
static void ovl_side_wheel(int notches);
static void ovl_back(void);
static void ovl_forward(void);
static void ovl_lk_load(void);
static void pdu_th_reset(void);
static int  pdu_job_nav(void);
static void pdu_fill(int x1, int y1, int x2, int y2, int c);
static void pdu_cutrun(int cx, int y, const char *s, int maxw);
static void pdu_ctl_repaint(int id);
static void pdu_scroll_to(int nsx, int nsy);
static void pdu_goto(int n);
static void pdu_tell(const char *s);
static void pdu_say2(const char *a, const char *b);
static void pdu_cat(char *d, const char *s, int cap);
static int  pdu_isect(struct os88_rect *a, struct os88_rect *b, struct os88_rect *o);
static void pdu_status_paint(void);
static void pdu_progress(const char *verb, const char *what);
static void pdu_wsay(const char *s);
static int  pdu_cell(int k, struct os88_rect *r);
static void pdu_side_paint(void);
static void pdu_side_page(int old);
static void pdu_ctl_paint(int i);
static int  pdu_inr(struct os88_rect *r, int x, int y);
static void pdu_nav_reset(void);
static void pdu_job_start(int kind);
static void pdu_job_abort_render(void);
static void pdu_render_want(struct os88_rect *r);
static int  ovl_th_pick(void);
static void ovl_sel_points(int x0, int y0, int x1, int y1, int word);
static void ovl_txwant_start(void);
static void ovl_find_begin(void);
static void ovl_sel_quads_owed(void);
static void ovl_sel_show(void);
static void ovl_sel_view(void);
static void ovl_sel_set(int pg, unsigned lo, unsigned hi);
static void ovl_th_begin(int pg);
static void ovl_find_step(void);
static void ovl_tx_step(void);
static void ovl_th_step(void);
static void ovl_lk_say(void);
static void ovl_ol_paint(struct os88_rect *o);
static void ovl_ol_click(int x, int y);
static void ovl_ol_work(void);
static void pdu_cell_paint(int k);
static int  pdu_side_rows(void);
static void pdu_side_list(struct os88_rect *o);
/* the pdui.inc seam: os88ui.inc's scroll-bar thumb drag (nav) */
int      pdu_sbgrab(int *blk, int y);
int      pdu_sbtrack(int *blk, int y);
int      pdu_sbdrop(int *blk);
#endif
/* ---- end nav ---- */
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
/* ---- edit ---- (PAGEDESK-SPEC 20.8; pduedit.c, pduundo.c, pdutool.c) --- */
/* these ride the overlay (SPEC.md 73.14): the macro IS the rename the overlay
 * cutter keys on, so every caller in the one translation unit reaches the shim */
#define pdu_ed_get ovl_ed_get
#define pdu_ed_new ovl_ed_new
#define pdu_ed_set ovl_ed_set
#define pdu_ed_text ovl_ed_text
#define pdu_ed_text_get ovl_ed_text_get
#define pdu_ed_pts ovl_ed_pts
#define pdu_ed_pts_get ovl_ed_pts_get
#define pdu_ed_del ovl_ed_del
#define pdu_ed_copy_src ovl_ed_copy_src
#define pdu_ed_first ovl_ed_first
#define pdu_ed_next ovl_ed_next
#define pdu_ed_move ovl_ed_move
#define pdu_ed_sel_fix ovl_ed_sel_fix
#define pdu_ed_xon_l ovl_ed_xon_l
#define pdu_ed_show ovl_ed_show
#define pdu_ed_select ovl_ed_select
#define pdu_tx_key ovl_tx_key
#define pdu_tx_begin ovl_tx_begin
#define pdu_tx_end ovl_tx_end
#define pdu_undo_begin ovl_undo_begin
#define pdu_undo_end ovl_undo_end
#define pdu_undo ovl_undo
#define pdu_redo ovl_redo
#define pdu_undo_pageop ovl_undo_pageop
#define pdu_undo_drop ovl_undo_drop
#define pdu_ed_xon_w ovl_ed_xon_w
#define pdu_ed_canvas2 ovl_ed_canvas2

int      pdu_ed_count(void);
int      pdu_ed_get(int i, struct pd_edit *e);
int      pdu_ed_new(struct pd_edit *e, const char *text, unsigned tlen,
                    const pd_pt *pts, unsigned npts);
int      pdu_ed_set(int i, const struct pd_edit *e);
int      pdu_ed_text(int i, const char *text, unsigned tlen);
unsigned pdu_ed_text_get(int i, char *dst, unsigned cap);
int      pdu_ed_pts(int i, const pd_pt *pts, unsigned npts);
unsigned pdu_ed_pts_get(int i, pd_pt *dst, unsigned cap);
int      pdu_ed_del(int i);
int      pdu_ed_copy_src(unsigned obj, unsigned gen, struct pd_edit *e);
int      pdu_ed_first(unsigned page);
int      pdu_ed_next(int i, unsigned page);
unsigned pdu_ed_seg(void);
void     pdu_undo_begin(int label);
void     pdu_undo_end(void);
int      pdu_undo(void);
int      pdu_redo(void);
int      pdu_undo_can(void);
int      pdu_redo_can(void);
void     pdu_undo_pageop(const void *delta, unsigned len);
int      pdu_dirty(void);
void     pdu_saved(void);
/* nav (20.10) supplies these two; the quads are 4 points (x, y pairs) each */
int      pdu_sel_quads(pd_pt *dst, unsigned cap);   /* cap = quads */
int      pdu_sel_text(char *dst, unsigned cap);
/* the hooks the shell's files call (20.8) */
void     pdu_ed_init(void);             /* a document's claims exist */
void     pdu_ed_gone(void);             /* the document is going */
void     pdu_ed_sel_fix(void);
void     pdu_ed_xon_l(void);            /* the selection's outline: lock held */
void     pdu_ed_xoff_l(void);
void     pdu_ed_xon(void);              /* ...and for a caller without it */
void     pdu_ed_xoff(const struct os88_rect *r);   /* a render of r begins */
void     pdu_ed_canvas(void);
void     pdu_ed_xon_w(void);
void     pdu_ed_canvas2(void);
void     pdu_ed_show(void);
void     pdu_ed_select(int i);
int      pdu_ed_selected(void);
int      pdu_ed_move(int i, int dx, int dy);
int      pdu_tx_key(int ascii);
void     pdu_tx_begin(int i, int isnew);
void     pdu_tx_end(void);
void     pdu_pg_apply(const void *delta, unsigned len, int undo);
#define pdu_undo_cmd ovl_undo_cmd
#define pdu_tool_click ovl_tool_click
#define pdu_tool_hilite ovl_tool_hilite
#define pdu_tool_delete ovl_tool_delete
#define pdu_tool_enter ovl_tool_enter
#define pdu_tool_hint ovl_tool_hint
#define pdu_undo_cmd ovl_undo_cmd
int      pdu_tool_click(int x, int y);
int      pdu_tool_hilite(void);         /* nav calls it when a Highlight selection ends */
int      pdu_tool_delete(void);
int      pdu_tool_enter(void);
void     pdu_tool_hint(int id);
void     pdu_undo_cmd(int redo);
unsigned pdu_ed_gen(void);              /* bumped by every mutation, undo, redo, selection change */
extern char pdu_tbuf[1025];             /* ONE near text buffer, shared with notes (D19) */
/* notes (20.9) implements these; edit stubs them until PDU_NOTES is defined */
void     pdu_nt_new(unsigned page_id, const struct pd_rect *r);
void     pdu_nt_open(int i);
void     pdu_nt_inspect(int i);
/* ---- end edit ---- */
/* ---- deepstk ---- */
/* the private module stack (PAGEDESK-SPEC 20.15, D23): pdumod.c's
 * pdu_stk_wake closes the document from the wake after an overrun, with
 * these two, which are defined after pdumod.c in the one translation unit */
#ifndef PD_HOST
static void pdu_close_doc(void);
static void pdu_wsay(const char *s);
#endif
/* ---- end deepstk ---- */

/* ---- prefs ---- (PAGEDESK-SPEC 20.18; pdupref.c, pdovlpref.c) */
#define P_PREFS  8              /* the Preferences popover */
#ifndef PD_HOST
/* the settings: 'PDCF', version 2, view, FULL panels, MID panels, mono
 * marks, Flate cache, module cache. The file is these eleven bytes (20.18.1,
 * 20.22.6); a version-1 file is the first ten */
static char pdp_cfg[11] = { 'P', 'D', 'C', 'F', 2, 0, 3, 0, 0, 0, 0 };
#define pdu_pref_modcache() (pdp_cfg[10])   /* 0 Auto, 1 Off (plat, O2) */
static const char pdp_formmsg[] =
    "Form fields are shown as printed; filling is not supported.";
static char pdp_form;                   /* the open document has an AcroForm */
static const char *pdp_done(void);
static void pdp_load(void);
static void pdp_view(void);
static void pdp_closed(void);
static void pdp_wake(void);
static void ovl_pf_paint(void);
static void ovl_pf_key(int ascii, int scan);
static void ovl_pf_click(int x, int y);
static void ovl_pf_save(void);
static void ovl_pf_load(void);
static void ovl_pf_wake(void);
static int  ovl_au_do(int op, int val);
static void ovl_au_cancel(void);        /* a leaf: pdu_pop_shut calls it */
static void ovl_au_pnt(void);
static void ovl_au_ky(int ascii);
#define AU_PW    1
#define AU_PWBAD 2
#define AU_R6    3
#define AU_CHECK 4
#define AU_COLS  36

static char pdu_au_pw[128];             /* the typed password (resident bss: an overlay's data is resident) */
static int  pdu_au_len;

static char pdu_au_mode;                /* AU_*: the popover is asking/working */
static signed char pdu_au_ans;          /* 0 waiting, 1 answered, -1 cancelled */
static int  pdu_au_val;                 /* AU_R6: minutes; AU_CHECK: permille */

#define pdu_au_poll() (pdu_au_ans)

/* the popover is going by some other road (pdu_pop_shut): a question still
 * open is a cancel (the overlay's ovl_au_cancel, which wipes the buffer) */
#define pdu_au_closed() do { if (pdu_au_mode) ovl_au_cancel(); } while (0)

/* the calls plat's ladder makes (20.22.6); one overlay entry, and a refused
 * fetch makes ask/askr6/busy answer 0 */
#define pdu_au_ask(bad)  ovl_au_do((bad) ? 2 : 1, 0)
#define pdu_au_askr6(s)  ovl_au_do(3, (int)(((s) + 59u) / 60u))
#define pdu_au_busy()    ovl_au_do(4, 0)
#define pdu_au_prog(p)   ovl_au_do(5, (p))
#define pdu_au_close()   ovl_au_do(6, 0)
#endif
/* ---- end prefs ---- */
/* ---- recov ---- */
/* the recovery journal (PAGEDESK-SPEC 20.17): pdurecov.c's resident part */
void     pdu_rc_cmd(void);              /* a command completed (pdu_undo_end) */
void     pdu_rc_wake(void);             /* the wake: write, check, answer */
int      pdu_rc_key(int ascii);         /* a prompt owns the keys */
void     pdu_rc_begin(void);            /* a document is being opened */
void     pdu_rc_fin(void);              /* a save is about to replace its target */
int      pdu_rc_close(void);            /* the close negotiator's question */
#ifndef PD_HOST
static void ovl_rc_drop(void);          /* a save starts: an unanswered finish offer goes */
#endif
/* the two hooks in files the host tests compile unchanged (pduundo.c,
 * pduedit.c) are no-ops there unless a test defines them first */
#ifdef PD_HOST
#ifndef PDU_RC_CMD
#define PDU_RC_CMD() ((void)0)
#define PDU_RC_SEED() 0
#endif
#else
#define PDU_RC_CMD() pdu_rc_cmd()
/* the /NM counter's first value (20.17.7): the tick's low ten bits (a
 * 56-second wheel - PDWRT's nonce already tells sources apart), a multiple
 * of 16 below 0x4000, so 128 records stay under 0x8000: PDWRT's
 * duplicated-page /NM (0x8000 + page + k, pdwpg.c) owns the top half */
#define PDU_RC_SEED() ((os88_ticks() << 4) & 0x3FF0u)
#endif
/* ---- end recov ---- */

#endif /* PDU_H */
