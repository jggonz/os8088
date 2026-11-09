/* ============================================================================
 * os8088 - apps/pagedesk/pagedesk.c    PageDesk's PRIMARY segment
 *                                      (docs/PAGEDESK-SPEC.md 2.2, 15, 17, 20.6)
 *
 * The window, the three layouts, the bars and panels, menus and keys, the
 * job scheduler that steps the modules, and the claims they work in. The
 * PDF itself is the modules' (PDENG reads it, PDREND draws it, PDWRT writes
 * it); this segment never parses a byte of one.
 *
 * ONE translation unit (SPEC.md 73.1): the pdu*.c parts are #included below
 * in dependency order, and pdovl.c's ovl_* functions are cut into the
 * overlay part by tools/os88ovl.py (SPEC.md 73.14).
 *
 * THE FOUR C RULES (SPEC.md 73) bind every line: every addressable object is
 * static, no struct assignment, no long, frames under 96 bytes.
 * ==========================================================================*/
#include "pdu.h"
#include "pdmath.h"
#include "pdwmap.h"            /* pages: the page map and PDW_OUT_REOPEN */

/* --- pdui.inc (the hand-written seam) ------------------------------------ */
void     pdu_mbind(int mod, unsigned seg);
unsigned pdu_mvecseg(int mod);
unsigned pdu_mcall(int mod, unsigned verb, unsigned bx, unsigned cx,
                   unsigned dx);
unsigned pdu_part_off(int i);
unsigned pdu_part_len(int i);
unsigned pdu_part_flags(int i);
char    *pdu_pkgname(void);
unsigned pdu_read_at_seg(const char *name, unsigned seg, unsigned cap,
                         unsigned off_lo, unsigned off_hi);
unsigned pdu_muldiv(unsigned a, unsigned b, unsigned c);
int      pdu_clk(unsigned *w);
void     pdu_sbar(int *blk);
void     pdu_sbmove(int *blk, int oldpos);
int      pdu_sbhit(int *blk, int x, int y);
/* apps/weave/wfetch.inc */
int      w_partfetch(int part);

/* ============================================================================
 * THE STATE - all of it, file scope, one instance's
 * ==========================================================================*/
static void *pdu_win;
static struct os88_video pdu_vid;       /* the window's display */
static struct os88_place pdu_home;      /* where PAGEDESK.O88 was launched */
static struct pdu_lay pdu_L;
static int pdu_mono;                    /* a 1bpp display (17.4) */
static int pdu_cbar, pdu_cact, pdu_cdesk;

static int pdu_want_side[3];            /* panel wishes per layout kind */
static int pdu_want_notes[3];
static int pdu_lkind = -1;              /* the kind the wishes were set for */

static int pdu_mode;                    /* PDU_MODE_* */
static int pdu_tool;                    /* C_T_* */
static int pdu_pop;                     /* P_* */
static struct os88_rect pdu_poprect;
static int pdu_popsrc;                  /* the control that opened it (PD-23:
                                         * focus returns there) */
static int pdu_focus;                   /* the control with the focus (an id) */
static int pdu_about;                   /* the About card is up */

static int pdu_doc;                     /* D_* */
static char pdu_name[14];               /* the open (or failed) file, 8.3 */
static struct os88_place pdu_docplace;  /* ...and the folder it is in: the
                                         * modules resolve pdu_name in the
                                         * CURRENT folder (pdxb_open), and a
                                         * file dialog moves that */
static int pdu_err;                     /* why the last open failed */
static int pdu_rdonly;
static unsigned pdu_npages, pdu_page;
static struct pdxb_pageinfo pdu_pinfo;
static struct pd_view pdu_view;
static int pdu_zoom = 100;
static int pdu_fit = 1;                 /* 0 none, 1 width, 2 page */
static int pdu_sx, pdu_sy;              /* pan and scroll, px into the page */
static int pdu_pw, pdu_ph;              /* the displayed page, px */
static int pdu_aspn = 1, pdu_aspd = 1;  /* 5.4 */
static int pdu_changes;                 /* the edit store's count (wave 2) */
static unsigned pdu_inc;                /* PDF_INC_* of the page shown */
static char pdu_incwhy[34];

static char pdu_msg[72];                /* the status line's left text */
static char pdu_title[48];
static int pdu_npall;                   /* Notes filter: 0 this page, 1 all */
static int pdu_sbtab;                   /* sidebar: 0 Pages, 1 Outline */
static int pdu_sidetop;                 /* first page cell in the sidebar */

static int pdu_fedit;                   /* the page field is being typed */
static char pdu_fbuf[8];
static int pdu_flen;
static char pdu_needle[40];             /* Find */
static int pdu_nlen;

static int pdu_sb[7];                   /* os88ui's scroll-bar block */
static struct pdxb_open pdu_xopen;      /* (moved up from pdujob.c: nav reads it) */
static struct pdxb_outline pdu_xout;    /* ...and so does the overlay's outline */
static int pdu_thwant;                  /* nav: a Pages cell may lack its picture */
static int pdu_hjump;                   /* nav: the next pdu_goto is a followed link, an
                                         * outline entry or a hit - history even if
                                         * the page is next door */
static int pdu_gs;                      /* nav: a press is being tracked (the
                                         * thumb 1, a selection 2); no page
                                         * render runs while a selection's band
                                         * is on the glass */

/* the job (PAGEDESK-SPEC 15) */
static int pdu_job;                     /* J_* */
static int pdu_jcancel;
static unsigned pdu_jslices, pdu_jt0;
static int pdu_rwant;                   /* a render is owed for pdu_rr */
static struct os88_rect pdu_rr;         /* ...this rect (screen) */
static struct os88_rect pdu_rrun;       /* the rect the running render has */
static int pdu_fsop;                    /* an OSAPI_FS_OP of ours is open */
static char pdu_target[14];             /* Save As's name */

/* scratch, file scope because &local does not build (SPEC.md 73.5) */
static struct os88_pt pdu_pt;
static struct os88_size pdu_sz;
static struct os88_rect pdu_dmg;
static struct os88_rect pdu_tr;
static char pdu_num[8];
static char pdu_line[72];
static int pdu_hidx;

/* forward declarations the parts need across one another */
static void pdu_mod_quit(void);
static void pdu_status_paint(void);
static void pdu_say(const char *s);
static void pdu_menusync(void);
static void pdu_relayout(void);
static void pdu_paint_all(void);
static void pdu_paint_rect(struct os88_rect *r);
static void pdu_paint_under(struct os88_rect *r);
static int pdu_popcls;                  /* pducmd.c: a popover close owed to the wake */
static int pdu_popdmg_on;               /* pducmd.c: a switch's repaint is owed */
static int pdu_popown;                  /* pducmd.c: a new popover's paint is owed */
static int pdu_rabort;                  /* pdujob.c: PDREND owes an abort (pdu_rend_sync) */
static struct os88_rect pdu_popdmg;
static void pdu_render_want(struct os88_rect *r);
static void pdu_canvas_full(void);
static void pdu_job_abort(void);
static void pdu_job_abort_render(void);
static void pdu_job_shift(int dy);
static void pdu_kick(void);
/* ---- pages (PAGEDESK-SPEC 20.11) ---- */
#define PDU_PAGES 1                     /* pdupage.c is real: edit drops its stand-in */
unsigned pdu_pg_count(void);
unsigned pdu_pg_id(unsigned view_index);
int  pdu_pg_isdup(unsigned view_index);
void pdu_pg_apply(const void *delta, unsigned len, int undo);
static void pdu_open_begin(const char *name);
static void pdu_reopen_begin(void);
static void pdu_tell(const char *s);
static void pdu_say2(const char *a, const char *b);
/* ---- end pages ---- */

/* About PageDesk (os88_about, SPEC.md 20.5.1.1) */
static const char *pdu_abtext[] = {
    "PageDesk",
    "A PDF reader and annotator for os8088",
    "",
    "The XT PDF profile: Help menu",
    "Wave 1 build",
    0
};

/* ============================================================================
 * THE PARTS, in dependency order
 * ==========================================================================*/
#include "pdulay.c"             /* 17.1: the pure layout */
#include "pduerr.c"             /* 3.6: one sentence per PDE_* */
#include "pdumod.c"             /* 3.1-3.4, 16.2: modules and claims */
#include "pdudraw.c"            /* every pixel of the chrome */
#include "pduview.c"            /* the page view: zoom, scroll, pages */
#include "pdupage.c"            /* pages: 13.7, 20.11: the page map (resident part) */
#include "pdusel.c"             /* nav: selection and Copy (20.10.3) */
#include "pdufind.c"            /* nav: Find, a job (20.10.1) */
#include "pduside.c"            /* nav: the Pages / Outline sidebar (20.10.4) */
#include "pdunav.c"             /* nav: history, gestures, links (20.10) */
#include "pdujob.c"             /* 15: the scheduler and its three jobs */
#include "pduedit.c"            /* 13.1, 20.8: the edit store (edit) */
#include "pduundo.c"            /* 13.6, 20.8: undo and redo (edit) */
#include "pdurecov.c"           /* recov: the recovery journal, resident (20.17) */
#include "pducmd.c"             /* commands, menus, keys, clicks */
#include "pdunotes.c"           /* 17.3: Notes & edits, the note popover */
#include "pduinsp.c"            /* 17.3: Text properties */
#include "pdupref.c"            /* prefs: the settings (20.18) */
#include "pdovl.c"              /* the overlay's tenants (ovl_*) */

/* ============================================================================
 * THE ENTRY (SPEC.md 20.2): runs before the window is on the glass
 * ==========================================================================*/
void *os88_main(void)
{
    void *win;
    int w, h, x, y, g, a, b;

    os88_file_here(&pdu_home);          /* parts and the sample live HERE */
    os88_video(&pdu_vid);

    /* the frame: the whole desktop the live display has (17.1 picks the
     * layout from what comes back, not from this) */
    x = 0;
    y = OS88_MBAR_H;
    w = pdu_vid.w;
    h = pdu_vid.dock_top - OS88_MBAR_H;
    os88_strcpy(pdu_title, "PageDesk", sizeof(pdu_title));
    win = os88_wm_create(x, y, w, h, pdu_title);
    if (win == 0)
        return 0;
    pdu_win = win;
    os88_wm_display(win, &pdu_vid);
    pdu_mono = pdu_vid.bpp < 4;

    /* 5.4: the pixel aspect from the live geometry, a 4:3 display assumed */
    a = pdu_vid.h << 2;
    b = pdu_vid.w;
    b = b + (b << 1);
    if (a > 0 && b > 0) {
        int p, q, t;
        p = a; q = b;
        while (q) { t = p % q; p = q; q = t; }
        g = p;
        pdu_aspn = a / g;
        pdu_aspd = b / g;
    }

    if (pdu_mod_launch() != 0)
        return 0;                       /* toasted; the loader unwinds */

    os88_wm_snap(win, 1);               /* x on a multiple of 8 (11.94) */
    os88_wm_sizable(win, 1);
    os88_wm_ownbg(win, 1);              /* we paint every pixel: damage rects */
    os88_wm_minsize(win, 320, 120);
    os88_wm_onresize(win);
    os88_wm_onwake(win);
    os88_wm_onclose(win);               /* a save is told to abort first */
    os88_wm_onwheel(win);
    os88_wm_onmouseup(win);             /* nav: the gestures' release... */
    os88_wm_ontimer(win);               /* ...and the poll that tracks them */
    os88_wm_cursor(win, OS88_CUR_ARROW);
    os88_about_set(win);
    os88_assoc_set("PDF", "PAGEDESK");

    pdu_want_side[PDU_L_FULL] = 1;
    pdu_want_notes[PDU_L_FULL] = 1;
    pdu_mode = PDU_MODE_READ;
    pdu_tool = C_T_SELECT;
    pdu_menusync();
    os88_menu_set(win, (struct os88_menuset *)&pdu_menus);

    /* the document a double-click named: banked here, opened from the wake
     * handler once the window is on the glass (SPEC.md 54.10) */
    if (os88_arg_file(pdu_argname, &pdu_argplace) == 0)
        pdu_argp = 1;
    pdu_say("Ready.");
    os88_wm_wake(win);
    return win;
}

/* W_PAINT: the gfx lock is held; draw the damage and nothing else */
void os88_paint(void *win)
{
    os88_wm_damage(win, &pdu_dmg);
    pdu_relayout();
    if (pdu_dmg.x1 > pdu_dmg.x2 || pdu_dmg.y1 > pdu_dmg.y2)
        return;
    pdu_paint_rect(&pdu_dmg);
}

void os88_onresize(int w, int h, void *win)
{
    /* SPEC.md 11.98: told after, must not draw. The next paint lays out. */
    os88_wm_display(win, &pdu_vid);
    pdu_mono = pdu_vid.bpp < 4;
    pdu_lkind = -2;
}
