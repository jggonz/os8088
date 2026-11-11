/* ============================================================================
 * os8088 - apps/pagedesk/pdunotes.c   NOTES & EDITS (PAGEDESK-SPEC 17.3,
 *                                     13.4, 20.9; stories PD-12, PD-16)
 *
 * A VIRTUAL list over two things and no card per note in memory: the edit
 * store's records (20.8's calls, read one at a time) and the annotations the
 * SOURCE page carries (PDV_ENG_ANNOTS, read once a page into an 8-entry cache
 * of previews). Three filters: This page, All pages, Changes (every record
 * the store holds, deleted and replies included - PD-16's list).
 *
 * THE SIZE RULE (COMMON.md, the frequency rule): this file is the RESIDENT
 * half - the state, the pure model, and a thin hook per call site that tests
 * a cheap predicate BEFORE it reaches for the overlay, so an idle keystroke
 * or a click elsewhere never fetches it. Everything with pixels or logic is
 * an ovl_nt_* function: the model here (it also compiles on the host,
 * hosttest/pdu_nt.c) and the view in pdovlnotes.c and pduinsp.c.
 *
 * Redraw: a selection change repaints the two rows it touched, a scroll the
 * list, a page change the list only under This page - never the panel's
 * chrome (PERFORMANCE.md rule 1).
 * ==========================================================================*/

#define PDU_NT_SRC   8                  /* source annotations cached a page */
#define PDU_NT_REP   12                 /* review replies kept for the state */
#define PDU_NT_SBASE 1000               /* item ids >= this are source ones */
#define PDU_NT_SCAN  32                 /* /Annots elements read a page */
#define PDU_NT_CARD  46                 /* a card's height (20.21.7) ... */
#define PDU_NT_PITCH 50                 /* ... and its pitch */
#define PDU_NT_LINE  12                 /* a COMPACT row */
#define PDU_NT_MAXTXT 1024              /* 13.1: a record's text */
#define PDU_NT_ROWS  12                 /* rows whose last picture is kept */
#define PDU_NT_AHEAD 62                 /* pdxb_annot's bytes before contents */
typedef char pdu_nt_ahead_ok[sizeof(struct pdxb_annot) == PDU_NT_AHEAD + 160
                            ? 1 : -1];  /* contents is the last 160 bytes */

#define PDU_NT_PANEL() (pdu_L.notes.x1 <= pdu_L.notes.x2)
#define PDU_NT_ISREPLY(e) ((e)->kind == PDA_NOTE \
                           && ((e)->irt != 0 || ((e)->flags & PDEF_IRTSRC)))
#ifdef PD_HOST                          /* the host test has no page map */
#define PDU_NT_VPAGE(id) ((int)(id))
#define PDU_NT_PGID(v)   (v)
#else                                   /* verify-insert: a record holds the page ID (20.11.1) */
static int ovl_pg_view(unsigned id);    /* pdovlpage.c: pdu_pg_id's inverse */
#define PDU_NT_VPAGE(id) ovl_pg_view(id)
#define PDU_NT_PGID(v)   pdu_pg_id(v)
#endif

/* ---- the source cache ------------------------------------------------------ */
static unsigned pdu_nt_so[PDU_NT_SRC];  /* object number */
static char pdu_nt_sk[PDU_NT_SRC];      /* PDA_* kind */
static char pdu_nt_sh[PDU_NT_SRC];      /* 1: a PageDesk record stands in */
static struct pd_rect pdu_nt_sr[PDU_NT_SRC];
static char pdu_nt_stx[PDU_NT_SRC][18]; /* the first 17 characters */
static int pdu_nt_ns;
static unsigned pdu_nt_spage;           /* the page + 1 the cache is of */
static unsigned pdu_nt_srk[6];          /* source replies: parent object */
static char pdu_nt_srs[6];              /* ...and the state they set */
static int pdu_nt_nsr;

/* ---- review replies in the store, rebuilt by ovl_nt_sync ----------------- */
static unsigned pdu_nt_rk[PDU_NT_REP];
static char pdu_nt_rsrc[PDU_NT_REP];
static char pdu_nt_rst[PDU_NT_REP];
static int pdu_nt_nr;

/* ---- the list's state -------------------------------------------------------- */
static struct pd_edit pdu_nt_e;         /* the last item ovl_nt_step gave */
static struct pd_edit pdu_nt_e2;        /* a record being made or changed */
static int pdu_nt_pos;                  /* the iterator */
static int pdu_nt_cnt;                  /* items under the filter, -1 = recount */
static int pdu_nt_key0, pdu_nt_key1, pdu_nt_key2, pdu_nt_key3;
static int pdu_nt_gen;                  /* bumped by pdu_notes_dirty */
static int pdu_nt_top;                  /* first item shown */
static int pdu_nt_sel = -1;             /* selected item id, -1 none */
static int pdu_nt_edsel = -2;           /* the canvas selection last adopted */
static int pdu_nt_late;                 /* the source cache is owed */
static int pdu_nt_post;                 /* the list repaints on the next idle */
static int pdu_nt_insp;                 /* the panel shows the inspector */
static int pdu_nt_isel;                 /* ...of this record */
static int pdu_nt_rvl;                  /* a reveal waits for the page */
static struct pd_rect pdu_nt_rr;        /* ...of this rect */
static struct os88_rect pdu_nt_o;       /* the list's rect (panel or popover) */
static int pdu_nt_cardst;               /* 1 = cards, 0 = one line each */
static struct os88_rect pdu_nt_cr;      /* a control's rect, scratch */
static int pdu_nt_vis, pdu_nt_y0, pdu_nt_ny, pdu_nt_tx, pdu_nt_cols;
/* what each row, the strip and the Text properties button last SHOWED, so a
 * relist paints only what changed (20.9.2). pdu_nt_rok = 0 forgets it all:
 * the panel or the popover was filled under the list. */
static unsigned pdu_nt_rsig[PDU_NT_ROWS];
static unsigned pdu_nt_ssig;
static int pdu_nt_rok;
static int pdu_nt_tpon = -1;
static int pdu_nt_gkey;                 /* the geometry the sigs are of */

/* ---- the note popover ----------------------------------------------------------- */
#define pdu_nt_buf pdu_tbuf             /* D19: edit's one shared near text buffer */
static int pdu_nt_seen;                 /* pdu_ed_gen() the list last showed */
static int pdu_nt_blen;
static int pdu_nt_pfirst;               /* the text's first line on show */
static int pdu_nt_pid = -1;             /* item id; -1 = a note not yet made */
static int pdu_nt_pm;                   /* 0 the note editor, 1 the inspector */
static int pdu_nt_ptxt;                 /* the typed text differs from the store */
static int pdu_nt_nocommit;             /* Esc: let go of what was typed */
static unsigned pdu_nt_ppg;
static struct pd_rect pdu_nt_prc;

static const char *pdu_nt_kname[10] = { "?", "Note", "Highlight", "Area", "Text",
                                        "Ink", "Line", "Rect", "Oval",
                                        "Sign" };

/* ============================================================================
 * THE MODEL (ovl_*; pure; hosttest/pdu_nt.c compiles this half)
 * ==========================================================================*/

/* does record e belong under the filter (0 this page, 1 all, 2 changes)? */
static int ovl_nt_pass(const struct pd_edit *e)
{
    if (pdu_npall == 2)
        return 1;
    if ((e->flags & PDEF_DELETED) || PDU_NT_ISREPLY(e))
        return 0;
    return pdu_npall == 1 || e->page == PDU_NT_PGID(pdu_page);
}

/* a note's review state, 13.4: 1 = resolved. The newest reply wins, and a
 * PageDesk reply is newer than a source one. */
static int ovl_nt_done(int src, unsigned key)
{
    int k;
    for (k = pdu_nt_nr - 1; k >= 0; k--)
        if (pdu_nt_rk[k] == key && pdu_nt_rsrc[k] == src)
            return pdu_nt_rst[k] == PDS_COMPLETED;
    if (src)
        for (k = pdu_nt_nsr - 1; k >= 0; k--)
            if (pdu_nt_srk[k] == key)
                return pdu_nt_srs[k] == PDS_COMPLETED;
    return 0;
}

/* one pass over the store: the replies, the source annotations a record
 * stands in for, and the count. Runs only when something changed. */
static void ovl_nt_sync(void)
{
    int n, i, k, src, rk;
    n = pdu_ed_count();
    if (n == pdu_nt_key0 && (int)pdu_ed_gen() + pdu_nt_gen == pdu_nt_key1
        && (int)pdu_page == pdu_nt_key2 && pdu_npall == pdu_nt_key3
        && pdu_nt_cnt >= 0)
        return;
    pdu_nt_key0 = n;
    pdu_nt_key1 = (int)pdu_ed_gen() + pdu_nt_gen;
    pdu_nt_key2 = (int)pdu_page;
    pdu_nt_key3 = pdu_npall;
    pdu_nt_nr = 0;
    pdu_nt_cnt = 0;
    for (k = 0; k < pdu_nt_ns; k++) {
        pdu_nt_sh[k] = 0;
        if (pdu_npall != 2)
            pdu_nt_cnt++;
    }
    for (i = 0; i < n; i++) {
        if (pdu_ed_get(i, &pdu_nt_e) != 0)
            continue;
        if (pdu_nt_e.src_num)
            for (k = 0; k < pdu_nt_ns; k++)
                if (pdu_nt_so[k] == pdu_nt_e.src_num && !pdu_nt_sh[k]) {
                    pdu_nt_sh[k] = 1;
                    if (pdu_npall != 2)
                        pdu_nt_cnt--;
                }
        if (PDU_NT_ISREPLY(&pdu_nt_e) && !(pdu_nt_e.flags & PDEF_DELETED)) {
            src = (pdu_nt_e.flags & PDEF_IRTSRC) != 0;
            rk = -1;
            for (k = 0; k < pdu_nt_nr; k++)
                if (pdu_nt_rk[k] == pdu_nt_e.irt && pdu_nt_rsrc[k] == src)
                    rk = k;
            if (rk < 0 && pdu_nt_nr < PDU_NT_REP)
                rk = pdu_nt_nr++;
            if (rk >= 0) {
                pdu_nt_rk[rk] = pdu_nt_e.irt;
                pdu_nt_rsrc[rk] = src;
                pdu_nt_rst[rk] = pdu_nt_e.state;
            }
        }
        if (ovl_nt_pass(&pdu_nt_e))
            pdu_nt_cnt++;
    }
}

/* the next item's id - an edit index, or PDU_NT_SBASE + a cache slot - or -1.
 * An edit's record is left in pdu_nt_e. */
static int ovl_nt_step(void)
{
    int n, i;
    n = pdu_ed_count();
    while (pdu_nt_pos < n) {
        i = pdu_nt_pos++;
        if (pdu_ed_get(i, &pdu_nt_e) == 0 && ovl_nt_pass(&pdu_nt_e))
            return i;
    }
    if (pdu_npall == 2)
        return -1;
    if (pdu_nt_pos < PDU_NT_SBASE)
        pdu_nt_pos = PDU_NT_SBASE;
    while (pdu_nt_pos - PDU_NT_SBASE < pdu_nt_ns) {
        i = pdu_nt_pos++ - PDU_NT_SBASE;
        if (!pdu_nt_sh[i])
            return PDU_NT_SBASE + i;
    }
    return -1;
}

/* position the iterator at item k of the list */
static void ovl_nt_seek(int k)
{
    pdu_nt_pos = 0;
    while (k > 0 && ovl_nt_step() >= 0)
        k--;
}

/* the list index of item id, or -1 */
static int ovl_nt_find(int id)
{
    int k, i;
    pdu_nt_pos = 0;
    for (k = 0; (i = ovl_nt_step()) >= 0; k++)
        if (i == id)
            return k;
    return -1;
}

/* the kind a source /Subtype means to the list, 0 = not listed (Link, Popup,
 * Stamp, Widget ...) */
static int ovl_nt_kind(const char *s)
{
    switch (s[0]) {
    case 'T': return s[1] == 'e' ? PDA_NOTE : 0;      /* Text, not TrapNet */
    case 'H': case 'U': return PDA_HIGHLIGHT;   /* Highlight, Underline */
    case 'F': return s[1] == 'r' ? PDA_TEXT : 0;      /* FreeText, not FileAttachment */
    case 'I': return PDA_INK;
    case 'C': return s[1] == 'i' ? PDA_ELLIPSE : 0;   /* Circle, not Caret */
    case 'L': return s[3] == 'e' ? PDA_LINE : 0;      /* Line, not Link */
    case 'S':
        if (s[1] == 'q')                        /* Square, Squiggly */
            return s[3] == 'a' ? PDA_RECT : PDA_HIGHLIGHT;
        return s[1] == 't' && s[2] == 'r' ? PDA_HIGHLIGHT : 0;
    }
    return 0;
}

/* the word for a change, in Changes (PD-16) */
static const char *ovl_nt_change(const struct pd_edit *e)
{
    if (e->flags & PDEF_DELETED)
        return "Deleted";
    if (PDU_NT_ISREPLY(e))
        return e->state == PDS_COMPLETED ? "Resolved" : "Reopened";
    if (e->src_num)
        return "Edited";
    return (e->flags & PDEF_MOVED) ? "Moved" : "Added";
}

#ifndef PD_HOST
/* ============================================================================
 * THE HOOKS (resident): each tests before it calls
 * ==========================================================================*/

/* the panel: its chrome is ours, always, so a refused overlay still leaves a
 * panel that closes and filters; the body is the overlay's */
static void pdu_notes_paint(void)
{
    struct os88_rect *n;
    int i;

    n = &pdu_L.notes;
    if (n->x1 > n->x2)
        return;
    pdu_fill(n->x1 + 1, n->y1, n->x2, n->y2, pdu_cbar);
    os88_set_color(OS88_BLACK);
    os88_gfx_vline(n->x1, n->y1, n->y2);
    /* the title in the kit's 12 px face, ruled off under it (20.21.7) */
    pdu_ptext(n->x1 + 6, n->y1 + 6, n->x2 - n->x1 - 32, "Notes & edits",
              PK_TTL, OS88_BLACK, pdu_cbar);
    os88_set_color(pdu_clo);
    os88_gfx_hline(n->x1 + 4, n->x2 - 4, n->y1 + 24);
    pdu_cground = pdu_cbar;
    for (i = 0; i < pdu_L.nctl; i++)
        if (pdu_L.ctl[i].id == C_NP_CLOSE || pdu_L.ctl[i].id == C_NP_FILTER)
            pdu_ctl_paint(i);
    pdu_cground = -1;
    pdu_nt_rok = 0;                     /* the list's ground was just filled */
    pdu_nt_tpon = -1;
    if (pdu_doc != D_OPEN)
        pdu_ptext(n->x1 + 8, n->y1 + PDU_NOTE_HEAD + 6, n->x2 - n->x1 - 12,
                  "No document", PK_TXT, OS88_BLACK, pdu_cbar);
    else
        ovl_nt_body();
}

/* a page change: This page and All pages both list the SHOWN page's source
 * annotations, so both follow it (only the rows that changed are painted);
 * a reveal the jump was waiting for runs here too */
static void pdu_notes_page(void)
{
    if (PDU_NT_PANEL() && pdu_doc == D_OPEN && (pdu_npall != 2 || pdu_nt_rvl))
        ovl_nt_page();
}

/* the source cache, once a page, when the panel or its popover will show it
 * and no job is using the engine (an ANNOTS call moves its object state) -
 * a thumbnail between its slices aside, with the overlay already in:
 * PDREND keeps its state in its own globals and its streams are their own
 * contexts, which its resource lookups already interleave with */
static void pdu_nt_srcload(void)
{
    if (pdu_doc != D_OPEN || pdu_nt_spage == pdu_page + 1
        || (!PDU_NT_PANEL() && pdu_pop != P_NOTES)) {
        pdu_nt_late = 0;                /* nothing owed: up to date, or */
        return;                         /* loaded when it is shown */
    }
    if (pdu_job != J_NONE && (pdu_job != J_THUMB || !pdu_ovl_in())) {
        pdu_nt_late = 1;
        return;
    }
    pdu_nt_late = 0;
    ovl_nt_srcload();
}

/* the paint paths' form: never an engine call under a paint (D21: a module
 * verb at the bottom of the paint chain is the deepest stack PageDesk has).
 * A cache of another page is emptied - the list never shows it - and the
 * wake loads this page's and repaints the rows that changed. */
static void pdu_nt_srcwant(void)
{
    if (pdu_doc != D_OPEN || pdu_nt_spage == pdu_page + 1)
        return;
    pdu_nt_ns = 0;
    pdu_nt_nsr = 0;
    pdu_nt_cnt = -1;
    if (pdu_nt_sel >= PDU_NT_SBASE)
        pdu_nt_sel = -1;
    pdu_nt_late = 1;
    pdu_kick();
}

static int pdu_nt_owed(void)
{
    return pdu_nt_late || pdu_nt_post || (pdu_nt_rvl && !pdu_pgwant)
           || (pdu_doc == D_OPEN && (int)pdu_ed_gen() != pdu_nt_seen
               && PDU_NT_PANEL() && pdu_pop == P_NONE);
}

static void pdu_nt_idle(void)
{
    ovl_nt_idle();
}

/* the document closed or another opened */
static void pdu_nt_reset(void)
{
    pdu_nt_ns = 0;
    pdu_nt_nsr = 0;
    pdu_nt_spage = 0;
    pdu_nt_top = 0;
    pdu_nt_sel = -1;
    pdu_nt_edsel = -2;
    pdu_nt_cnt = -1;
    pdu_nt_insp = 0;
    pdu_nt_rvl = 0;
    pdu_nt_late = 0;
    pdu_nt_post = 0;
}

/* the filter button (C_NP_FILTER): This page, All pages, Changes */
static void pdu_nt_filter(void)
{
    pdu_npall = (pdu_npall + 1) % 3;
    pdu_nt_top = 0;
    pdu_nt_cnt = -1;
    pdu_ctl_repaint(C_NP_FILTER);
    ovl_nt_relist();
}

/* a click in the panel's body (the header's controls are pdu_hit's) */
static int pdu_nt_click(int x, int y)
{
    if (pdu_doc != D_OPEN || pdu_pop != P_NONE || !PDU_NT_PANEL()
        || !pdu_inr(&pdu_L.notes, x, y)
        || y < pdu_L.notes.y1 + PDU_NOTE_HEAD)
        return 0;
    return ovl_nt_click(x, y);
}

/* the popover's: the list (P_NOTES) or the note and inspector (P_NOTE) */
static int pdu_nt_pclick(int x, int y)
{
    if (pdu_pop != P_NOTES && pdu_pop != P_NOTE)
        return 0;
    return ovl_nt_pclick(x, y);
}

/* every key while a note field, the inspector or the list popover is up */
static int pdu_nt_key(int ascii, int scan)
{
    if (pdu_nt_insp && !PDU_NT_PANEL())
        pdu_nt_insp = 0;                /* the panel was put away */
    if (pdu_pop != P_NOTES && pdu_pop != P_NOTE
        && (!pdu_nt_insp || pdu_fedit   /* the page field keeps its keys, */
            || pdu_pop != P_NONE))      /* and another popover its Esc */
        return 0;
    return ovl_nt_key(ascii, scan);
}

static void pdu_nt_ppaint(void)
{
    ovl_nt_ppaint();
}

/* edit's three calls (D19, 20.9.5): the Note tool's pending note, and a
 * double-click on a note or a text box */
void pdu_nt_new(unsigned page_id, const struct pd_rect *r)
{
    ovl_nt_new(page_id, r);
}

void pdu_nt_open(int i)
{
    ovl_nt_open(i);
}

void pdu_nt_inspect(int i)
{
    ovl_nt_inspect(i);
}

static void pdu_nt_pclose(void)
{
    if (pdu_pop == P_NOTE)
        ovl_nt_pclose();
}

/* the popover's size, in characters and in 12-pixel rows (pdu_pop_open) */
static int pdu_nt_pw(void)
{
    if (pdu_pop == P_NOTE)
        return pdu_nt_pm ? 22 : 32;
    return 28;
}

static int pdu_nt_prows(void)
{
    return pdu_pop == P_NOTE && pdu_nt_pm ? 8 : 7;
}
#endif /* !PD_HOST */
