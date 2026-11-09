/* ============================================================================
 * os8088 - apps/pagedesk/pduundo.c   UNDO AND REDO (PAGEDESK-SPEC 13.6, 20.8;
 *                                    PD-16)
 *
 * A bounded history of 64 COMMANDS (a command is what one pdu_undo_begin /
 * pdu_undo_end bracket did) over a ring of 256 twelve-byte ENTRIES in
 * pdu_dcseg(PDU_DC_UNDO), 3 KiB of its 4. Never a PDF copy, never a bitmap:
 * an entry is the smallest old state that puts one thing back.
 *
 * Every entry is an INVOLUTION - applying it swaps the record's bytes (or a
 * heap top, or the store's count) with the entry's payload - so undo and redo
 * are the same walk in opposite orders and need no second copy of anything.
 * MOVE is the one that is not a swap: it stores the translation, and undo
 * applies its negation (exact in pt16, which a scale would not be, so a
 * resize is a FIELD entry holding the old rectangle).
 *
 * WHAT IS NOT LOGGED, and why that is right:
 *   - a change to a record BORN in the same command. Undo truncates the
 *     store to the count it had, which hides the record whole; the bytes
 *     stay where they were, so redo restores the count and the record is
 *     back as it ended, with nothing stored.
 *   - the second and later edits of one field of one record in one command
 *     (a drag's many sets): the FIRST old state is the one that matters.
 *   - typing: a text edit of the record the previous command typed into,
 *     with nothing between, joins that command (the coalescing of 13.6).
 *
 * THE SAVE POINT is an absolute command number. Undo back to it clears dirty;
 * a new command made from below it (the redo branch discarded) forgets it,
 * and so does the history running out of room.
 * ==========================================================================*/

#define UD_NCMD  64
#define UD_NENT  256
#define UD_SZ    12
#define UD_PGMAX 96                     /* a page operation's delta, bytes */

#define UT_FIELD  1                     /* | length << 4 */
#define UT_TEXT   2
#define UT_PTS    3
#define UT_CREATE 4
#define UT_MOVE   5
#define UT_PAGE   6
#define UT_PCHUNK 7

struct ud_ent {
    pdb tag, o;
    pdw idx;
    pdw w[4];
};
static struct ud_ent ud_en;             /* sizeof == UD_SZ on both targets */
typedef char ud_sz_ent[sizeof(struct ud_ent) == UD_SZ ? 1 : -1];

/* per command slot, in PDU_DC_UNDO after the entry ring (not near bss): its
 * first entry counter and how many entries it has */
#define UD_CMDS  (UD_NENT * UD_SZ)      /* 3,072: 64 x 4 bytes, to 3,328 of 4 KiB */
#define ud_cs(s) ovl_ud_cw((s) << 2)
#define ud_ce(s) ovl_ud_cw(((s) << 2) + 2)
static pdw ovl_ud_cw(pdw o)
{
    return pd_fpeekw(pdu_dcseg(PDU_DC_UNDO), UD_CMDS + o);
}
static void ovl_ud_cset(pdw o, pdw v)
{
    pd_fpokew(pdu_dcseg(PDU_DC_UNDO), UD_CMDS + o, v);
}
static pdw ud_c0, ud_n, ud_cur, ud_ntot;/* oldest command, retained, applied */
static pdw ud_base;                     /* the store's count at begin */
static int ud_depth, ud_open, ud_dead, ud_olab;
static int ud_trunc, ud_lost, ud_told;
static pdw ud_sav;                      /* the save point */
static int ud_savok;
static int ud_tyrec;                    /* typing: the record... */
static pdw ud_tycmd;                    /* ...and the command typed into */
static pdw ud_txfl;                     /* ...and the text top when it began */
#define ud_pg ((pdb *)ed_pts)           /* the page delta: the tools' scratch */
#define ud_tmp ((pdb *)&ed_xr)          /* 8 bytes: the XOR fill's scratch */
static pdw ud_ei;                       /* the entry being applied */

/* pages (20.11) supplies pdu_pg_apply; pdu.h's pages block defines PDU_PAGES */
#ifndef PDU_PAGES
void pdu_pg_apply(const void *delta, unsigned len, int undo) { }
#endif

static void ud_init(void)
{
    ud_c0 = ud_n = ud_cur = ud_ntot = 0;
    ud_depth = ud_open = ud_dead = 0;
    ud_trunc = ud_lost = ud_told = 0;
    ud_sav = 0;
    ud_savok = 1;
    ud_tyrec = -1;
    ud_base = 0;
}

static void ovl_ud_ld(pdw e)
{
    pd_fcopy_in(pdu_dcseg(PDU_DC_UNDO), (e & (UD_NENT - 1)) * UD_SZ, &ud_en,
                UD_SZ);
}

static void ovl_ud_st(pdw e)
{
    pd_fcopy_out(&ud_en, pdu_dcseg(PDU_DC_UNDO), (e & (UD_NENT - 1)) * UD_SZ,
                 UD_SZ);
}

/* forget the oldest command (announced once: 13.6) */
static void ovl_ud_drop(void)
{
    ud_c0++;
    ud_n--;
    ud_cur--;
    ud_trunc = 1;
}

/* the history cannot hold this command: forget it all, keep working */
static void ovl_ud_clear(void)
{
    ud_n = ud_cur = 0;
    ud_dead = 1;
    ud_savok = 0;
    ud_lost = 1;
    ud_tyrec = -1;
}

/* append ud_en to the open command, opening (committing) it on the first */
static int ovl_ud_push(void)
{
    pdw slot;

    if (ud_dead || pdu_dcseg(PDU_DC_UNDO) == 0)
        return 0;
    if (!ud_open) {
        if (ud_savok && ud_sav > (pdw)(ud_c0 + ud_cur))
            ud_savok = 0;               /* the saved state is on the branch */
        ud_n = ud_cur;                  /* new edits clear the redo branch */
        if (ud_n >= UD_NCMD)
            ovl_ud_drop();
        slot = (ud_c0 + ud_n) & (UD_NCMD - 1);
        ovl_ud_cset(slot << 2, ud_ntot);
        ovl_ud_cset((slot << 2) + 2, 0);
        ud_n++;
        ud_cur++;
        ud_open = 1;
    }
    while ((pdw)(ud_ntot - ud_cs(ud_c0 & (UD_NCMD - 1))) >= UD_NENT) {
        if (ud_cur <= 1) {
            ovl_ud_clear();
            return 0;
        }
        ovl_ud_drop();
    }
    slot = (ud_c0 + ud_cur - 1) & (UD_NCMD - 1);
    ovl_ud_st(ud_ntot);
    ovl_ud_cset((slot << 2) + 2, ud_ce(slot) + 1);
    ud_ntot++;
    return 1;
}

/* the open command's LAST entry for (tag, o, idx), loaded, 1; or -1. For a
 * MOVE, -1 also when a rectangle or points entry of idx came after it: a
 * move does not commute with those (undo walks backwards, so folding a later
 * move into an earlier one across a resize undoes to the wrong place). */
static int ovl_ud_find(int tag, int o, int idx)
{
    pdw slot, k;
    if (!ud_open)
        return -1;
    slot = (ud_c0 + ud_cur - 1) & (UD_NCMD - 1);
    for (k = ud_ce(slot); k-- > 0;) {
        ovl_ud_ld(ud_cs(slot) + k);
        if (ud_en.idx != (pdw)idx)
            continue;
        if (ud_en.tag == tag && ud_en.o == o) {
            ud_ei = ud_cs(slot) + k;
            return 1;
        }
        if (tag == UT_MOVE && (ud_en.tag == UT_PTS
            || (ud_en.tag == (UT_FIELD | (8 << 4)) && ud_en.o == EO_RECT)))
            return -1;
    }
    return -1;
}

/* ---- the loggers pduedit.c calls BEFORE it changes a thing --------------- */
/* l bytes of record i at o (FIELD), or its text/points words (TEXT, PTS: 4
 * bytes and, in w[2], the heap top) - unless the record was born in this
 * command or the command already holds this entry's first old state.
 * 1 = pushed. */
static int ovl_ud_log(int i, int tag, int o, int l)
{
    if (i >= (int)ud_base || ovl_ud_find(tag, o, i) > 0)
        return 0;
    ud_en.tag = tag;
    ud_en.o = o;
    ud_en.idx = i;
    pd_fcopy_in(pdu_dcseg(PDU_DC_EDITS), (unsigned)i * ED_REC + o, ud_en.w, l);
    if (tag == UT_TEXT)
        ud_en.w[2] = pdu_ed_tt;
    if (tag == UT_PTS)
        ud_en.w[2] = pdu_ed_pt;
    return ovl_ud_push();
}


static void ovl_ud_text(int i)
{
    /* typing joins the command that typed last, if nothing came between */
    if (!ud_open && ud_olab == UL_TYPE && ud_tyrec == i
        && ud_tycmd == (pdw)(ud_c0 + ud_cur) && ud_cur == ud_n && ud_cur > 0)
        return;
    if (ovl_ud_log(i, UT_TEXT, EO_TEXT, 4) > 0) {
        ud_tyrec = i;
        ud_tycmd = ud_c0 + ud_cur;
        ud_txfl = pdu_ed_tt;
    }
}

/* may a backspace give back the text heap's last byte? Only a byte the
 * current typing run wrote itself (at or above the top it began at): a byte
 * below that is the old text an undo puts back, and bytes above the top are
 * a redo branch's only while one exists, which coalescing already excludes */
static int ovl_ud_trimok(int i)
{
    return ud_tyrec == i && ud_tycmd == (pdw)(ud_c0 + ud_cur)
        && ud_cur == ud_n && ud_cur > 0 && !ud_dead
        && (pdw)(pdu_ed_tt - 1) >= ud_txfl;
}


static void ovl_ud_create(void)
{
    ud_en.tag = UT_CREATE;
    ud_en.o = 0;
    ud_en.idx = pdu_ed_n;
    ud_en.w[0] = pdu_ed_n;
    ud_en.w[1] = pdu_ed_tt;
    ud_en.w[2] = pdu_ed_pt;
    ud_en.w[3] = 0;
    if (ovl_ud_push()) {
        ud_tyrec = pdu_ed_n;            /* a new box is typed into at once */
        ud_tycmd = ud_c0 + ud_cur;
        ud_txfl = pdu_ed_tt;
    }
}

static void ovl_ud_move(int i, int dx, int dy)
{
    if (i >= (int)ud_base)
        return;
    if (ovl_ud_find(UT_MOVE, 0, i) > 0) {       /* a drag's steps are one move */
        ud_en.w[0] = (pdw)(ud_en.w[0] + dx);
        ud_en.w[1] = (pdw)(ud_en.w[1] + dy);
        ovl_ud_st(ud_ei);
        return;
    }
    ud_en.tag = UT_MOVE;
    ud_en.o = 0;
    ud_en.idx = i;
    ud_en.w[0] = (pdw)dx;
    ud_en.w[1] = (pdw)dy;
    ud_en.w[2] = ud_en.w[3] = 0;
    ovl_ud_push();
}

/* ---- the bracket ---------------------------------------------------------- */
void pdu_undo_begin(int label)
{
    if (ud_depth++ == 0) {
        ud_open = 0;
        ud_dead = 0;
        ud_olab = label;
        ud_base = pdu_ed_n;
    }
}

void pdu_undo_end(void)
{
    if (ud_depth > 0 && --ud_depth == 0) {
        ovl_pdu_ed_flush();
        PDU_RC_CMD();                   /* recov: a command completed (20.17.3) */
    }
}

/* ---- applying one entry (ud_en) -------------------------------------------- */

static void ovl_ud_tr(int idx, int dx, int dy)
{
    unsigned k, n;
    pd_pt v;
    unsigned off;
    if (ovl_ed_rd(idx, &ed_w))
        return;
    ed_w.rect.x0 += dx; ed_w.rect.x1 += dx;
    ed_w.rect.y0 += dy; ed_w.rect.y1 += dy;
    off = ed_w.pts_off;
    n = ed_w.pts_n;
    ovl_ed_wr(idx, &ed_w);
    for (k = 0; k < n; k++, off += 4) {
        v = (pd_pt)pd_fpeekw(pdu_dcseg(PDU_DC_EDITS), off);
        if (v == ED_PENUP)
            continue;
        pd_fpokew(pdu_dcseg(PDU_DC_EDITS), off, (pdw)(v + dx));
        pd_fpokew(pdu_dcseg(PDU_DC_EDITS), off + 2,
                  (pdw)(pd_fpeekw(pdu_dcseg(PDU_DC_EDITS), off + 2) + dy));
    }
}

static void ovl_ud_apply(int undo)
{
    int t, l, i, i2;
    pdw n, k, e0;

    t = ud_en.tag & 15;
    i = ud_en.idx;
    if (t == UT_PCHUNK)
        return;
    if (t == UT_PAGE) {                 /* the chunks sit just before it */
        n = ud_en.w[0];
        k = (n + 7) >> 3;
        e0 = ud_ei - k;
        for (i = 0; i < (int)k; i++) {
            ovl_ud_ld(e0 + i);
            os88_memcpy(ud_pg + (i << 3), ud_en.w, 8);
        }
        pdu_pg_apply(ud_pg, n, undo);
        return;
    }
    if (t == UT_CREATE) {
        /* mark the records that come or go while they are still readable */
        e0 = ud_en.w[0];                /* the count it swaps in */
        n = e0 > pdu_ed_n ? e0 : pdu_ed_n;
        k = e0 < pdu_ed_n ? e0 : pdu_ed_n;
        t = pdu_ed_n;
        pdu_ed_n = n;
        for (; k < n; k++)
            ovl_pdu_ed_touch_i((int)k);
        ud_en.w[0] = t;
        pdu_ed_n = e0;
        n = ud_en.w[1]; ud_en.w[1] = pdu_ed_tt; pdu_ed_tt = n;
        n = ud_en.w[2]; ud_en.w[2] = pdu_ed_pt; pdu_ed_pt = n;
        return;
    }
    ovl_pdu_ed_touch_i(i);
    if (t == UT_MOVE) {
        l = (pdi)ud_en.w[0];
        i2 = (pdi)ud_en.w[1];
        ovl_ud_tr(i, undo ? -l : l, undo ? -i2 : i2);
    } else {                            /* FIELD; TEXT, PTS: 4 bytes + a top */
        l = t == UT_FIELD ? ud_en.tag >> 4 : 4;
        i2 = i * ED_REC + ud_en.o;
        pd_fcopy_in(pdu_dcseg(PDU_DC_EDITS), i2, ud_tmp, l);
        pd_fcopy_out(ud_en.w, pdu_dcseg(PDU_DC_EDITS), i2, l);
        os88_memcpy(ud_en.w, ud_tmp, l);
        n = ud_en.w[2];                 /* the heap top: swapped too */
        if (t == UT_TEXT) {
            ud_en.w[2] = pdu_ed_tt;
            pdu_ed_tt = n;
        } else if (t == UT_PTS) {
            ud_en.w[2] = pdu_ed_pt;
            pdu_ed_pt = n;
        }
    }
    ovl_pdu_ed_touch_i(i);
}

/* ---- the walk -------------------------------------------------------------- */
static int ovl_ud_walk(int undo)
{
    pdw slot, k, e, c, m;
    if (ud_depth > 0)
        return -1;
    if (undo ? ud_cur == 0 : ud_cur >= ud_n)
        return -1;
    slot = (ud_c0 + (undo ? ud_cur - 1 : ud_cur)) & (UD_NCMD - 1);
    pdu_undo_begin(UL_NONE);
    c = ud_cs(slot);
    m = ud_ce(slot);
    for (k = 0; k < m; k++) {
        e = c + (undo ? m - 1 - k : k);
        ovl_ud_ld(e);
        ud_ei = e;
        ovl_ud_apply(undo);
        if ((ud_en.tag & 15) != UT_PCHUNK && (ud_en.tag & 15) != UT_PAGE)
            ovl_ud_st(e);
    }
    ud_cur = undo ? ud_cur - 1 : ud_cur + 1;
    ud_tyrec = -1;
    pdu_undo_end();
    return 0;
}

int pdu_undo(void) { return ovl_ud_walk(1); }
int pdu_redo(void) { return ovl_ud_walk(0); }
int pdu_undo_can(void) { return ud_cur > 0; }
int pdu_redo_can(void) { return ud_cur < ud_n; }

/* drop the command just undone (an empty note or text box cancelled) */
static void pdu_undo_drop(void)
{
    if (ovl_ud_walk(1) == 0)
        ud_n = ud_cur;
}

void pdu_undo_pageop(const void *delta, unsigned len)
{
    pdw k, n;
    pdu_undo_begin(UL_PAGE);
    if (len > UD_PGMAX || len == 0) {
        ud_olab = UL_PAGE;
        ovl_ud_clear();                     /* too big to keep: said, not hidden */
        pdu_undo_end();
        return;
    }
    n = (len + 7) >> 3;
    os88_memset(ud_pg, 0, UD_PGMAX);
    os88_memcpy(ud_pg, delta, len);
    for (k = 0; k < n; k++) {
        ud_en.tag = UT_PCHUNK;
        ud_en.o = 0;
        ud_en.idx = 0;
        os88_memcpy(ud_en.w, ud_pg + (k << 3), 8);
        ovl_ud_push();
    }
    ud_en.tag = UT_PAGE;
    ud_en.o = 0;
    ud_en.idx = 0;
    ud_en.w[0] = len;
    ud_en.w[1] = ud_en.w[2] = ud_en.w[3] = 0;
    ovl_ud_push();
    pdu_undo_end();
}

/* dirty = not at the save point */
int pdu_dirty(void)
{
    return !ud_savok || (pdw)(ud_c0 + ud_cur) != ud_sav;
}

void pdu_saved(void)
{
    ud_sav = ud_c0 + ud_cur;
    ud_savok = 1;
    ud_told = 0;
    ud_tyrec = -1;                      /* typing after a save is a new step */
}

/* ---- the words -------------------------------------------------------------- */
/* Edit > Undo / Redo: do it and say so (the label table is not kept: the
 * history is bytes the XT cannot spare, and "Undone." is true of every step) */
void pdu_undo_cmd(int redo)
{
    if (redo ? pdu_redo() : pdu_undo())
        return;
    pdu_tell(redo ? "Redone." : "Undone.");
}

/* the outermost end: hand the screen what changed, refresh what shows state */
static void ovl_pdu_ed_flush(void)
{
    int i, n;
    pdu_ed_genv++;
    n = 0;                              /* live records: the Notes count */
    for (i = 0; i < (int)pdu_ed_n; i++)
        if (!(pd_fpeekb(pdu_dcseg(PDU_DC_EDITS), (unsigned)i * ED_REC + 1)
              & PDEF_DELETED))
            n++;
    pdu_changes = n;
    pdu_ed_sel_fix();
    pdu_menusync();
    pdu_ctl_repaint(C_UNDO);
    pdu_ctl_repaint(C_REDO);
    pdu_title_set();
    if (ud_lost) {
        ud_lost = 0;
        pdu_tell("Too large to undo: the history was cleared.");
    } else if (ud_trunc) {
        ud_trunc = 0;
        if (ud_told)
            return;                     /* said once, until the next save */
        ud_told = 1;
        pdu_tell("Undo history full: the oldest step is dropped.");
    }
}

/* ---- the end of a typing session (it lives here: it asks the history) ---- */
/* end the session; a note or box that was born empty is not a change */
void pdu_tx_end(void)
{
    int i;
    if (pdu_tx < 0)
        return;
    i = pdu_tx;
    pdu_tx = -1;
    if (pdu_tx_new && ovl_ed_rd(i, &ed_w) == 0 && ed_w.text_len == 0
        && ud_tyrec == i && ud_tycmd == (pdw)(ud_c0 + ud_cur)
        && ud_cur == ud_n) {
        pdu_undo_drop();
        pdu_ed_sel = -1;
        pdu_ed_genv++;
        pdu_tell("An empty box is not kept.");
        return;
    }
    pdu_tell("");
}
