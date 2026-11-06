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
#define PGC_FIRST   100

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
static char pdu_pgnote[24];     /* said once the re-open is through */

static void pdu_save_replace(void);
static int ovl_save_begin(const char *name, int kind);
static int ovl_save_done(unsigned err);
static void ovl_save_pages(void);
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
void pdu_pg_apply(const void *d, unsigned len, int undo)
{
    ovl_pg_apply((const unsigned *)d, undo);
}

/* the greyed-with-a-reason answer for a page command; 0 = enabled */
static const char *pdu_pg_why(int id)
{
    if (id == PGC_NEW)
        return 0;
    if (pdu_rdonly)
        return "This document is signed: it opens read-only.";
    if (pdu_npages > PDM_MAXMAP && !pdu_pgmap)
        return "Page operations handle up to 240 pages.";
    if (id == C_P_DELETE && pdu_npages < 2)
        return "The last page cannot be deleted.";
    if (id == PGC_UP && pdu_page == 0)
        return "This is the first page.";
    if (id == PGC_DOWN && pdu_page + 1 >= pdu_npages)
        return "This is the last page.";
    return 0;
}

static void pdu_pg_cmd(int id)
{
    ovl_pg_cmd(id);
}

/* a key while a prompt is up (consumes every key) */
static int pdu_pg_key(int ascii, int scan)
{
    if (pdu_pk == PK_NONE)
        return 0;
    if (ascii == 27) {
        if (pdu_pk == PK_REPLACE) {     /* keep the target: the temp file goes (14.3) */
            pdu_pk = PK_NONE;
            pdu_mcall(PDU_M_WRT, PDV_WRT_ABORT, 0, 0, 0);
            pdu_save_done(PDE_CANCEL);
            return 1;
        }
        pdu_pk = PK_NONE;
        pdu_tell("Cancelled.");
        return 1;
    }
    return ovl_pg_key(ascii, scan);
}
