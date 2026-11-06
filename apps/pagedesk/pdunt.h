/* ============================================================================
 * os8088 - apps/pagedesk/pdunt.h   NOTES: the edit store's calls (20.8) and
 * the stand-ins used until `edit` lands (merge feat/pagedesk, then delete the
 * PDU_NT_STUB block and build with the real pduedit.c / pduundo.c).
 *
 * Everything below the prototypes is a STAND-IN: an empty store, so the panel
 * reads "No notes or edits" on the target, and a fake store in the host test
 * (hosttest/pdu_nt.c), which defines the real names itself and leaves
 * PDU_NT_STUB off.
 * ==========================================================================*/
#ifndef PDUNT_H
#define PDUNT_H

/* PAGEDESK-SPEC 20.8, as pinned */
int      pdu_ed_count(void);
int      pdu_ed_get(int i, struct pd_edit *e);
int      pdu_ed_new(struct pd_edit *e, const char *text, unsigned tlen,
                    const pd_pt *pts, unsigned npts);
int      pdu_ed_set(int i, const struct pd_edit *e);
int      pdu_ed_text(int i, const char *text, unsigned tlen);
unsigned pdu_ed_text_get(int i, char *dst, unsigned cap);
int      pdu_ed_del(int i);
void     pdu_undo_begin(int label);
void     pdu_undo_end(void);

/* ASKED of `edit` (notes-report.md): the canvas selection, both ways. A
 * record index, -1 for none. */
void     pdu_ed_select(int i);
int      pdu_ed_selected(void);
unsigned pdu_ed_gen(void);     /* bumped on every mutation, undo, redo, selection change */

#ifdef PDU_NT_STUB
static int pdu_nt_stubsel = -1;
static char pdu_tbuf[1025];                 /* edit's, once it lands */
unsigned pdu_ed_gen(void) { return 0; }
int  pdu_ed_count(void) { return 0; }
int  pdu_ed_get(int i, struct pd_edit *e) { return -1; }
int  pdu_ed_new(struct pd_edit *e, const char *text, unsigned tlen,
                const pd_pt *pts, unsigned npts) { return -1; }
int  pdu_ed_set(int i, const struct pd_edit *e) { return -1; }
int  pdu_ed_text(int i, const char *text, unsigned tlen) { return -1; }
unsigned pdu_ed_text_get(int i, char *dst, unsigned cap) { return 0; }
int  pdu_ed_del(int i) { return -1; }
void pdu_undo_begin(int label) { }
void pdu_undo_end(void) { }
void pdu_ed_select(int i) { pdu_nt_stubsel = i; }
int  pdu_ed_selected(void) { return pdu_nt_stubsel; }
#endif

/* A LOOK-ONLY store (PDU_NT_DEMO, set by hand in pagedesk.c for a boot to
 * look at the panel, the popover and the inspector before `edit` lands; never
 * committed turned on): eight records in memory, text in a small heap. */
#ifdef PDU_NT_DEMO
static struct pd_edit pdu_nd_r[8];
static char pdu_nd_t[8][40];
static int pdu_nd_n, pdu_nd_id = 1, pdu_nd_sel = -1;
static unsigned pdu_nd_gen;
static char pdu_tbuf[1025];
unsigned pdu_ed_gen(void) { return pdu_nd_gen; }
static void pdu_nd_add(int kind, int page, int id, int irt, int state,
                       int x, int y, const char *t)
{
    struct pd_edit *e;
    e = &pdu_nd_r[pdu_nd_n];
    os88_memset(e, 0, sizeof(*e));
    e->kind = kind; e->page = page; e->id = id; e->irt = irt;
    e->state = state; e->flags = PDEF_DIRTY;
    e->rect.x0 = x; e->rect.y0 = y; e->rect.x1 = x + 320; e->rect.y1 = y + 288;
    e->colour = kind == PDA_TEXT ? PDC_BLUE : PDC_YELLOW;
    e->style = 0; e->size = 12;
    os88_strcpy(pdu_nd_t[pdu_nd_n], t, 40);
    pdu_nd_n++;
}
static void pdu_nd_seed(void)
{
    if (pdu_nd_n)
        return;
    /* pages 0 and 1, so a two-page sample (MARKUP.PDF) shows all four */
    pdu_nd_add(PDA_NOTE, 0, 1, 0, 0, 4000, 5000, "Check the serial port.");
    pdu_nd_add(PDA_HIGHLIGHT, 0, 2, 0, 0, 3000, 4000, "");
    pdu_nd_add(PDA_TEXT, 0, 3, 0, 0, 2000, 3000, "Tested on two IBM XTs.");
    pdu_nd_add(PDA_NOTE, 1, 4, 0, 0, 2000, 3000, "A note on page two");
    pdu_nd_id = 5;
}
int  pdu_ed_count(void) { pdu_nd_seed(); return pdu_nd_n; }
int  pdu_ed_get(int i, struct pd_edit *e)
{
    pdu_nd_seed();
    if (i < 0 || i >= pdu_nd_n) return -1;
    os88_memcpy(e, &pdu_nd_r[i], sizeof(*e));
    return 0;
}
int  pdu_ed_new(struct pd_edit *e, const char *text, unsigned tlen,
                const pd_pt *pts, unsigned npts)
{
    int n;
    pdu_nd_seed();
    if (pdu_nd_n >= 8) return -1;
    n = pdu_nd_n++;
    os88_memcpy(&pdu_nd_r[n], e, sizeof(*e));
    pdu_nd_r[n].id = pdu_nd_id++;
    pdu_nd_r[n].flags |= PDEF_DIRTY;   /* as edit does: PDEF_IRTSRC kept */
    pdu_nd_gen++;
    if (tlen > 39) tlen = 39;
    os88_memcpy(pdu_nd_t[n], text, tlen);
    pdu_nd_t[n][tlen] = 0;
    return n;
}
int  pdu_ed_set(int i, const struct pd_edit *e)
{
    if (i < 0 || i >= pdu_nd_n) return -1;
    os88_memcpy(&pdu_nd_r[i], e, sizeof(*e));
    pdu_nd_gen++;
    return 0;
}
int  pdu_ed_text(int i, const char *text, unsigned tlen)
{
    if (i < 0 || i >= pdu_nd_n) return -1;
    if (tlen > 39) tlen = 39;
    os88_memcpy(pdu_nd_t[i], text, tlen);
    pdu_nd_t[i][tlen] = 0;
    pdu_nd_gen++;
    return 0;
}
unsigned pdu_ed_text_get(int i, char *dst, unsigned cap)
{
    unsigned n;
    n = os88_strlen(pdu_nd_t[i]);
    if (n >= cap) n = cap - 1;
    os88_memcpy(dst, pdu_nd_t[i], n);
    dst[n] = 0;
    return n;
}
int  pdu_ed_del(int i)
{
    if (i < 0 || i >= pdu_nd_n) return -1;
    pdu_nd_r[i].flags |= PDEF_DELETED;
    pdu_nd_gen++;
    return 0;
}
void pdu_undo_begin(int label) { }
void pdu_undo_end(void) { }
void pdu_ed_select(int i) { pdu_nd_sel = i; pdu_nd_gen++; }
int  pdu_ed_selected(void) { return pdu_nd_sel; }
#endif

#endif /* PDUNT_H */
