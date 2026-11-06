/* ============================================================================
 * os8088 - apps/pagedesk/pdupage.c   PAGE OPERATIONS, THE RESIDENT PART
 *                                    (PAGEDESK-SPEC 13.7, 13.8, 14.6, 20.11)
 *
 * The page map (pdwmap.h) is the order of the pages the user SEES, in the
 * claim PDU_DC_PAGES; with none (pdu_pgmap = 0) view page i is source page
 * i. EVERY view-index-to-page lookup goes through pdu_pg_id() - the renderer,
 * the text pass, Find, the thumbnails - and pdu_npages is the VIEW's count, so
 * nothing else in the primary changes meaning.
 *
 * The commands, the prompts and the dialogs are ovl_pg_* in pdovlpage.c (a
 * menu command's code is the overlay's, 2.2): what stays here is a lookup, the
 * greying, and the doors.
 * ==========================================================================*/

/* commands beyond the Pages-mode buttons (C_P_*): menu-only */
#define PGC_DUP     100
#define PGC_UP      101
#define PGC_DOWN    102
#define PGC_MOVE    103
#define PGC_INSERT  104
#define PGC_NEW     105
#define PGC_FIRST   100                 /* PGC_FIRST..PGC_NEW */

/* the prompt in the status line (ovl_pg_key) */
#define PK_NONE     0
#define PK_DELETE   1
#define PK_EXTRACT  2
#define PK_MOVE     3
#define PK_NEWSIZE  4
#define PK_REPLACE  5

static int pdu_pgmap;           /* the view is a page map, not the source's order */
static unsigned pdu_srcn;       /* pages in the source document */
static int pdu_pk;              /* PK_* */
static int pdu_pgfd;            /* the file dialog was raised for 1 New or 2 Extract */
static int pdu_savekind;        /* the running job: 0 Save As, 1 New, 2 Extract */
static char pdu_pkbuf[24];
static int pdu_pklen;
static unsigned pdu_newp[5];    /* New's one page: pd_newpage */
static int pdu_reopening;       /* PDENG is being given its document back */
static struct os88_place pdu_tgtplace;  /* the folder a Save As / New / Extract writes into */
static unsigned pdu_xtn;        /* the Extract list's length (in the claim, 480 bytes up) */
#define PDW_ST_CONFIRM 0x0100u  /* the writer's answer: the target exists (20.5.9) */
#define PDW_ST_REPLACE 0x0200u
static unsigned pdu_resume;     /* the page + 1 to return to after a re-open */
static int pdu_pgsaved;         /* "Saved <target>" is said once the re-open is through */

static int ovl_save_begin(const char *name, int kind);
static int ovl_save_done(unsigned err);
static void pdu_save_done(unsigned err);
static int ovl_pg_cmd(int id);
static int ovl_pg_key(int ascii, int scan);
static int ovl_pg_onfile(const char *name);
static int ovl_pg_apply(const unsigned *d, int undo);

static unsigned pdu_pgw(unsigned i)
{
    return pd_fpeekw(pdu_dcseg(PDU_DC_PAGES), (unsigned)(2 * i));
}

/* 20.11.1: the page map's lookups - the ones every other agent calls */
unsigned pdu_pg_count(void)
{
    return pdu_npages;
}

unsigned pdu_pg_id(unsigned i)
{
    return pdu_pgmap ? PDM_IDX(pdu_pgw(i)) : i;
}

static unsigned pdu_pg_kind(unsigned i)
{
    return pdu_pgmap ? PDM_KIND(pdu_pgw(i)) : PDM_SRC;
}

/* 1 = view page i shows a source page that an earlier view page also shows,
 * or a page that is not the source's (a blank): such a page owns no edit
 * records (13.7), so the renderer must not draw the page id's marks on it */
int pdu_pg_isdup(unsigned i)
{
    unsigned k;
    if (!pdu_pgmap)
        return 0;
    if (pdu_pg_kind(i) != PDM_SRC)
        return 1;
    for (k = 0; k < i; k++)
        if (pdu_pg_kind(k) == PDM_SRC && pdu_pg_id(k) == pdu_pg_id(i))
            return 1;
    return 0;
}

/* a page operation as a command (13.6, 13.7), for edit's undo: the delta is
 * the 4 words ovl_pg_do built, undo = 1 reverts it */
/* APPLIED AT THE WAKE: undo reaches this from deep in whatever ended the
 * typing (a zoom, a scroll), and applying here put the overlay's fetch and
 * the page map's repaint under it - 452 B of the UI task's 384 in the stack
 * gate (pdstk, D22). One command is one page operation, so one slot. */
static unsigned pdu_pgow[4];
static int pdu_pgow_on, pdu_pgow_undo;
void pdu_pg_apply(const void *d, unsigned len, int undo)
{
    os88_memcpy(pdu_pgow, d, sizeof(pdu_pgow));
    pdu_pgow_undo = undo;
    pdu_pgow_on = 1;
    pdu_kick();
}

static void pdu_pg_wake(void)
{
    if (pdu_pgow_on) {
        pdu_pgow_on = 0;
        os88_gfx_lock();                /* a callback's paints run locked */
        ovl_pg_apply(pdu_pgow, pdu_pgow_undo);
        os88_gfx_unlock();
    }
}

/* a page command's id: the Pages-mode buttons C_P_* and the menu-only PGC_* */
static int pdu_pg_isop(int id)
{
    return (id >= C_P_ROTL && id <= C_P_EXTRACT) || (id >= PGC_FIRST && id <= PGC_NEW);
}

/* the greyed-with-a-reason answer for a page command; 0 = enabled. The
 * caller (pdu_why) has already refused it with no document open. */
static const char *pdu_pg_why(int id)
{
    if (pdu_rdonly)
        return "This document is signed: it opens read-only.";
    /* the map and the Extract list are 240 entries each (pdwmap.h) */
    if (id != C_P_EXTRACT && (pdu_npages > PDM_MAXMAP
        || (pdu_npages == PDM_MAXMAP && (id == PGC_DUP || id == C_P_BLANK))))
        return "Page operations handle up to 240 pages.";
    if (id == C_P_DELETE && pdu_npages < 2)
        return "The last page cannot be deleted.";
    if (id == PGC_UP && pdu_page == 0)
        return "This is the first page.";
    if (id == PGC_DOWN && pdu_page + 1 >= pdu_npages)
        return "This is the last page.";
    return 0;
}

/* the page commands' menu rows (12.4): menu << 4 | item, the command, the
 * text - ONE table for both the sync and the dispatch. The items follow
 * pducmd.c's: File 5-6 after Close, Edit 6-10 after Find previous, Document
 * 9-10 after Go to page (a pull-down holds 11). Insert from another PDF
 * (PGC_INSERT, 20.11.7) has no row until it is built. */
#define PDU_PGMN 9
static const unsigned char pdu_pgmrow[PDU_PGMN] = {
    0x05, 0x06, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x39, 0x3A };
static const unsigned char pdu_pgmcmd[PDU_PGMN] = {
    C_P_EXTRACT, C_P_BLANK, PGC_UP, PGC_DOWN, PGC_MOVE, PGC_DUP,
    C_P_DELETE, C_P_ROTL, C_P_ROTR };
static const char *const pdu_pgmtxt[PDU_PGMN] = {
    "\001Extract pages...", "\001Insert blank page",
    "\001Move page earlier", "\001Move page later", "\001Move page to...",
    "\001Duplicate page", "\001Delete page", "\001Rotate page left",
    "\001Rotate page right" };

static const char *pdu_why(int id);
static void pdu_pg_msync(const char **mf, const char **me, const char **md)
{
    int i, r;
    const char **m;
    for (i = 0; i < PDU_PGMN; i++) {
        r = pdu_pgmrow[i];
        m = (r >> 4) == 0 ? mf : (r >> 4) == 1 ? me : md;
        m[r & 15] = pdu_pgmtxt[i] + (pdu_why(pdu_pgmcmd[i]) == 0);
    }
}

/* a menu pick that is a page command: its id, else 0 */
static int pdu_pg_menucmd(int menu, int item)
{
    int i;
    for (i = 0; i < PDU_PGMN; i++)
        if (pdu_pgmrow[i] == ((menu << 4) | item))
            return pdu_pgmcmd[i];
    return 0;
}

/* a key while a prompt is up (consumes every key, the overlay's) */
static int pdu_pg_key(int ascii, int scan)
{
    if (pdu_pk == PK_NONE)
        return 0;
    return ovl_pg_key(ascii, scan);
}
