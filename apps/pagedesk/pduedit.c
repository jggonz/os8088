/* ============================================================================
 * os8088 - apps/pagedesk/pduedit.c   THE EDIT STORE (PAGEDESK-SPEC 13.1, 13.1.1,
 *                                    20.8; PD-12..PD-16)
 *
 * The document's changes live in ONE segment, pdu_dcseg(PDU_DC_EDITS), 16 KiB:
 *
 *     0 ........ ED_MAX records of 48 bytes (struct pd_edit), in z-order
 *     ED_TEXT0 . the text heap, growing UP    (ED_TEXT0 .. pdu_ed_tt)
 *     ED_END ... the point heap, growing DOWN (pdu_ed_pt .. ED_END)
 *
 * so `text_off` and `pts_off` are byte offsets from edits_seg:0, exactly
 * what 13.1.1 pins and PDREND and PDWRT read. Nothing here knows a byte of
 * PDF: PDWRT turns the records into annotations and PDREND draws them.
 *
 * Every mutating call logs its own undo entry (pduundo.c) BEFORE it writes,
 * and marks the screen rectangle it changed (before and after), so a caller
 * brackets a user command with pdu_undo_begin/end and does nothing else:
 * the outermost end hands the union of the marks to the renderer.
 *
 * A heap is a BUMP allocator and a replaced block is simply abandoned; the
 * one thing that is reused is the tail of the LAST text block, which is what
 * typing and backspace are (a prefix-preserving edit never moves the text).
 * Undo never frees anything either: it restores the three tops, and the bytes
 * above a restored top stay where they were, which is what makes REDO free of
 * any stored copy (a new command discards the redo branch before it writes).
 *
 * THE LIMITS, said in words (13.1 asks for 256 records and a swap file; this
 * build keeps 128 records and the 10 KiB between the heaps, and refuses the
 * rest with PDE_LIMIT / PDE_NOMEM rather than losing what is typed):
 *   records 128, one text 1,024 bytes, text + points together ~10 KiB.
 * ==========================================================================*/

#define ED_REC    48
#define ED_MAX    128
#define ED_TEXT0  (ED_MAX * ED_REC)     /* 6,144 */
#define ED_END    16384u
#define ED_TMAX   1024
#define ED_PMAX   2000                  /* points in one record */
#define ED_PENUP  (-32767 - 1)          /* PD_PENUP; SmallerC refuses the literal -32768 */

/* record byte offsets (struct pd_edit) the undo log swaps */
#define EO_FLAGS   1
#define EO_RECT    4
#define EO_COLOUR  18
#define EO_TEXT    22
#define EO_PTS     26
#define EO_IRT     30

/* the undo labels (pdu_undo_begin): the words the status line says */
#define UL_NONE    0
#define UL_NOTE    1
#define UL_HILITE  2
#define UL_MARK    3
#define UL_TEXT    4
#define UL_INK     5
#define UL_LINE    6
#define UL_RECT    7
#define UL_ELLIPSE 8
#define UL_SIGN    9
#define UL_MOVE    10
#define UL_RESIZE  11
#define UL_DELETE  12
#define UL_TYPE    13
#define UL_PROPS   14
#define UL_PAGE    15

static int pdu_ed_err;                  /* PDE_NOMEM / PDE_LIMIT of the last -1 */
static pdw pdu_ed_n;                    /* records, deleted ones included */
static pdw pdu_ed_tt, pdu_ed_pt;        /* the heaps' tops */
static pdw pdu_ed_idc;                  /* the /NM counter */
static struct pd_edit ed_w, ed_v;       /* work records */
static pd_px ed_px, ed_py;
static struct os88_rect ed_sr;
static struct os88_rect pdu_ed_dr;      /* the screen rect the marks union */
static int pdu_ed_drv;                  /* ...and whether there is one */

/* undo (pduundo.c) -------------------------------------------------------- */
static void ovl_ud_field(int i, int o, int l);
static void ovl_ud_text(int i);
static void ovl_ud_pts(int i);
static void ovl_ud_create(void);
static void ovl_ud_move(int i, int dx, int dy);
static void ud_init(void);
static void ovl_ud_tr(int idx, int dx, int dy);
static void pdu_undo_drop(void);
static void ovl_pdu_ed_flush(void);
static void ovl_pdu_ed_touch_i(int i);

static unsigned ed_seg(void) { return pdu_dcseg(PDU_DC_EDITS); }

/* the page id of the page on the glass. The pages agent's view map
 * (pdu_pg_id) replaces this one line; until then a view index IS the id. */
static unsigned pdu_ed_pg(void) { return pdu_page; }

static int ovl_ed_rd(int i, struct pd_edit *e)
{
    if (i < 0 || i >= (int)pdu_ed_n)
        return -1;
    pd_fcopy_in(ed_seg(), (unsigned)i * ED_REC, e, ED_REC);
    return 0;
}

static void ovl_ed_wr(int i, const struct pd_edit *e)
{
    pd_fcopy_out(e, ed_seg(), (unsigned)i * ED_REC, ED_REC);
}

/* per document: called once the claim exists (not zeroed) */
void pdu_ed_init(void)
{
    pdu_ed_n = 0;
    pdu_ed_tt = ED_TEXT0;
    pdu_ed_pt = ED_END;
    pdu_ed_idc = 0;
    pdu_ed_drv = 0;
    pdu_ed_err = 0;
    ud_init();
}

int pdu_ed_count(void) { return (int)pdu_ed_n; }
unsigned pdu_ed_seg(void) { return ed_seg(); }

int pdu_ed_get(int i, struct pd_edit *e)
{
    return ovl_ed_rd(i, e);
}

/* ---- the screen rectangle of a page rect, and the marks ------------------ */
static void ovl_ed_scr(const struct pd_rect *r, int pad, struct os88_rect *o)
{
    int k;
    o->x1 = 32000; o->y1 = 32000; o->x2 = -32000; o->y2 = -32000;
    for (k = 0; k < 2; k++) {        /* opposite corners stay opposite under any R */
        pdm_view_fwd(&ed_px, &ed_py, &pdu_view, k ? r->x1 : r->x0, k ? r->y1 : r->y0);
        if (ed_px < o->x1) o->x1 = ed_px;
        if (ed_px > o->x2) o->x2 = ed_px;
        if (ed_py < o->y1) o->y1 = ed_py;
        if (ed_py > o->y2) o->y2 = ed_py;
    }
    o->x1 -= pad; o->y1 -= pad; o->x2 += pad; o->y2 += pad;
}

/* union the record's screen rect (if it is on the glass) into the owed one */
static void ovl_ed_mark(const struct pd_edit *w)
{
    if (pdu_doc != D_OPEN || w->page != pdu_ed_pg())
        return;
    ovl_ed_scr(&w->rect, 6, &ed_sr);
    if (ed_sr.x1 < pdu_L.canvas.x1) ed_sr.x1 = pdu_L.canvas.x1;
    if (ed_sr.y1 < pdu_L.canvas.y1) ed_sr.y1 = pdu_L.canvas.y1;
    if (ed_sr.x2 > pdu_L.canvas.x2) ed_sr.x2 = pdu_L.canvas.x2;
    if (ed_sr.y2 > pdu_L.canvas.y2) ed_sr.y2 = pdu_L.canvas.y2;
    if (ed_sr.x1 > ed_sr.x2 || ed_sr.y1 > ed_sr.y2)
        return;
    if (!pdu_ed_drv) {
        os88_memcpy(&pdu_ed_dr, &ed_sr, sizeof(pdu_ed_dr));
        pdu_ed_drv = 1;
        return;
    }
    if (ed_sr.x1 < pdu_ed_dr.x1) pdu_ed_dr.x1 = ed_sr.x1;
    if (ed_sr.y1 < pdu_ed_dr.y1) pdu_ed_dr.y1 = ed_sr.y1;
    if (ed_sr.x2 > pdu_ed_dr.x2) pdu_ed_dr.x2 = ed_sr.x2;
    if (ed_sr.y2 > pdu_ed_dr.y2) pdu_ed_dr.y2 = ed_sr.y2;
}

static void ovl_pdu_ed_touch_i(int i)
{
    if (ovl_ed_rd(i, &ed_v) == 0)
        ovl_ed_mark(&ed_v);
}

/* ---- a bump allocation -------------------------------------------------- */
static int ovl_ed_room(unsigned tbytes, unsigned pbytes)
{
    return pdu_ed_tt + tbytes + pbytes <= pdu_ed_pt;
}

/* bytes [a, a+n) of two near buffers equal? */
static int ovl_ed_same(const void *a, const void *b, unsigned n)
{
    const char *p, *q;
    p = (const char *)a;
    q = (const char *)b;
    while (n--)
        if (*p++ != *q++)
            return 0;
    return 1;
}

/* bbox of a point list (PENUP-separated), into r */
static void ovl_ed_bbox(struct pd_rect *r, const pd_pt *p, unsigned n)
{
    unsigned k;
    int first;
    first = 1;
    for (k = 0; k < n; k++, p += 2) {
        if (p[0] == ED_PENUP)
            continue;
        if (first) {
            r->x0 = r->x1 = p[0];
            r->y0 = r->y1 = p[1];
            first = 0;
            continue;
        }
        if (p[0] < r->x0) r->x0 = p[0];
        if (p[0] > r->x1) r->x1 = p[0];
        if (p[1] < r->y0) r->y0 = p[1];
        if (p[1] > r->y1) r->y1 = p[1];
    }
}

/* ============================================================================
 * 20.8
 * ==========================================================================*/
int pdu_ed_new(struct pd_edit *e, const char *text, unsigned tlen,
               const pd_pt *pts, unsigned npts)
{
    unsigned pb;
    int idx;

    pdu_ed_err = 0;
    if (ed_seg() == 0) {
        pdu_ed_err = PDE_NOMEM;
        return -1;
    }
    if (pdu_ed_n >= ED_MAX || tlen > ED_TMAX || npts > ED_PMAX) {
        pdu_ed_err = PDE_LIMIT;
        return -1;
    }
    pb = npts << 2;
    if (!ovl_ed_room(tlen, pb)) {
        pdu_ed_err = PDE_NOMEM;
        return -1;
    }
    pdu_undo_begin(UL_NONE);
    ovl_ud_create();
    idx = (int)pdu_ed_n;
    e->id = ++pdu_ed_idc;
    e->flags |= PDEF_DIRTY;
    e->text_off = pdu_ed_tt;
    e->text_len = tlen;
    e->pts_off = pdu_ed_pt - pb;
    e->pts_n = npts;
    os88_memset(e->reserved, 0, sizeof(e->reserved));
    if (tlen)
        pd_fcopy_out(text, ed_seg(), pdu_ed_tt, tlen);
    if (pb)
        pd_fcopy_out(pts, ed_seg(), e->pts_off, pb);
    ovl_ed_wr(idx, e);
    pdu_ed_n++;
    pdu_ed_tt += tlen;
    pdu_ed_pt = e->pts_off;
    ovl_ed_mark(e);
    pdu_undo_end();
    return idx;
}

int pdu_ed_set(int i, const struct pd_edit *e)
{
    unsigned char *a, *b;
    int fl0, fl;
    if (ovl_ed_rd(i, &ed_w))
        return -1;
    pdu_undo_begin(UL_NONE);
    ovl_ed_mark(&ed_w);
    fl0 = ed_w.flags;
    a = (unsigned char *)&ed_w;
    b = (unsigned char *)e;
    fl = (fl0 & ~PDEF_IRTSRC) | (e->flags & PDEF_IRTSRC) | PDEF_DIRTY;
    if (!ovl_ed_same(a + EO_RECT, b + EO_RECT, 8)) {
        ovl_ud_field(i, EO_RECT, 8);
        os88_memcpy(a + EO_RECT, b + EO_RECT, 8);
        fl |= PDEF_MOVED;
    }
    if (!ovl_ed_same(a + EO_COLOUR, b + EO_COLOUR, 4)) {
        ovl_ud_field(i, EO_COLOUR, 4);
        os88_memcpy(a + EO_COLOUR, b + EO_COLOUR, 4);
    }
    if (!ovl_ed_same(a + EO_IRT, b + EO_IRT, 4)) {
        ovl_ud_field(i, EO_IRT, 4);
        os88_memcpy(a + EO_IRT, b + EO_IRT, 4);
    }
    if (fl != fl0) {
        ovl_ud_field(i, EO_FLAGS, 1);
        ed_w.flags = fl;
    }
    ovl_ed_wr(i, &ed_w);
    ovl_ed_mark(&ed_w);
    pdu_undo_end();
    return 0;
}

/* replace a record's text. A new block at the heap's top unless it is empty;
 * the old bytes stay where they are (undo restores the offset). Typing does
 * not come through here: ovl_ed_tx extends or trims the tail in place. */
static int ovl_ed_text_set(int i, const char *text, unsigned tlen)
{
    if (ovl_ed_rd(i, &ed_w))
        return -1;
    if (tlen > ED_TMAX) {
        pdu_ed_err = PDE_LIMIT;
        return -1;
    }
    if (!ovl_ed_room(tlen, 0)) {
        pdu_ed_err = PDE_NOMEM;
        return -1;
    }
    pdu_undo_begin(UL_NONE);
    ovl_ed_mark(&ed_w);
    ovl_ud_text(i);
    if (tlen) {
        pd_fcopy_out(text, ed_seg(), pdu_ed_tt, tlen);
        ed_w.text_off = pdu_ed_tt;
        pdu_ed_tt += tlen;
    }
    ed_w.text_len = tlen;
    ed_w.flags |= PDEF_DIRTY;
    ovl_ed_wr(i, &ed_w);
    ovl_ed_mark(&ed_w);
    pdu_undo_end();
    return 0;
}

int pdu_ed_text(int i, const char *text, unsigned tlen)
{
    pdu_ed_err = 0;
    return ovl_ed_text_set(i, text, tlen);
}

unsigned pdu_ed_text_get(int i, char *dst, unsigned cap)
{
    unsigned n;
    if (ovl_ed_rd(i, &ed_v) || cap == 0)
        return 0;
    n = ed_v.text_len;
    if (n > cap - 1)
        n = cap - 1;
    if (n)
        pd_fcopy_in(ed_seg(), ed_v.text_off, dst, n);
    dst[n] = 0;
    return n;
}

int pdu_ed_pts(int i, const pd_pt *pts, unsigned npts)
{
    unsigned pb;
    pdu_ed_err = 0;
    if (ovl_ed_rd(i, &ed_w))
        return -1;
    if (npts > ED_PMAX) {
        pdu_ed_err = PDE_LIMIT;
        return -1;
    }
    pb = npts << 2;
    if (!ovl_ed_room(0, pb)) {
        pdu_ed_err = PDE_NOMEM;
        return -1;
    }
    pdu_undo_begin(UL_NONE);
    ovl_ed_mark(&ed_w);
    ovl_ud_pts(i);
    if (pb)
        pd_fcopy_out(pts, ed_seg(), pdu_ed_pt - pb, pb);
    pdu_ed_pt -= pb;
    ed_w.pts_off = pdu_ed_pt;
    ed_w.pts_n = npts;
    ed_w.flags |= PDEF_DIRTY;
    ovl_ed_wr(i, &ed_w);
    ovl_ed_mark(&ed_w);
    pdu_undo_end();
    return 0;
}

unsigned pdu_ed_pts_get(int i, pd_pt *dst, unsigned cap)
{
    unsigned n;
    if (ovl_ed_rd(i, &ed_v))
        return 0;
    n = ed_v.pts_n;
    if (n > cap)
        n = cap;
    if (n)
        pd_fcopy_in(ed_seg(), ed_v.pts_off, dst, n << 2);
    return n;
}

/* a flags change, logged: set `on`, clear `off` */
static int ovl_ed_flags(int i, int on, int off)
{
    if (ovl_ed_rd(i, &ed_w))
        return -1;
    pdu_undo_begin(UL_NONE);
    ovl_ud_field(i, EO_FLAGS, 1);
    ovl_ed_mark(&ed_w);
    ed_w.flags = (ed_w.flags | on | PDEF_DIRTY) & ~off;
    ovl_ed_wr(i, &ed_w);
    pdu_undo_end();
    return 0;
}

int pdu_ed_del(int i)
{
    return ovl_ed_flags(i, PDEF_DELETED, 0);
}

/* copy-on-edit of a source annotation (13.1). `e` arrives with what the
 * caller knows (kind, page, rect, colour...); the record that names the
 * source object is found, or made. A record that replaces a SOURCE annotation
 * and is then DELETED is the deletion itself: the writer drops the object. */
int pdu_ed_copy_src(unsigned obj, unsigned gen, struct pd_edit *e)
{
    unsigned i;
    for (i = 0; i < pdu_ed_n; i++) {
        pd_fcopy_in(ed_seg(), i * ED_REC, &ed_w, ED_REC);
        if (ed_w.src_num == obj && ed_w.src_gen == gen) {
            os88_memcpy(e, &ed_w, ED_REC);
            return (int)i;
        }
    }
    e->src_num = obj;
    e->src_gen = gen;
    e->flags = 0;
    return pdu_ed_new(e, "", 0, (const pd_pt *)0, 0);
}

/* live = not deleted; a record's page is a word at +2 */
static int ovl_ed_live_on(int i, unsigned page)
{
    if (pd_fpeekb(ed_seg(), (unsigned)i * ED_REC + 1) & PDEF_DELETED)
        return 0;
    return pd_fpeekw(ed_seg(), (unsigned)i * ED_REC + 2) == page;
}

int pdu_ed_next(int i, unsigned page)
{
    for (i++; i >= 0 && i < (int)pdu_ed_n; i++)
        if (ovl_ed_live_on(i, page))
            return i;
    return -1;
}

int pdu_ed_first(unsigned page)
{
    return pdu_ed_next(-1, page);
}

/* translate record i (its rectangle and every point) by (dx, dy) pt16 */
int pdu_ed_move(int i, int dx, int dy)
{
    if (ovl_ed_rd(i, &ed_w))
        return -1;
    pdu_undo_begin(UL_NONE);
    ovl_ed_mark(&ed_w);
    ovl_ud_move(i, dx, dy);
    ovl_ud_field(i, EO_FLAGS, 1);
    ovl_ud_tr(i, dx, dy);
    ovl_ed_rd(i, &ed_w);
    ed_w.flags |= PDEF_MOVED | PDEF_DIRTY;
    ovl_ed_wr(i, &ed_w);
    ovl_ed_mark(&ed_w);
    pdu_undo_end();
    return 0;
}

/* how many records are live (the Notes panel's count) */
static int ovl_pdu_ed_live(void)
{
    int i, n;
    n = 0;
    for (i = 0; i < (int)pdu_ed_n; i++)
        if (!(pd_fpeekb(ed_seg(), (unsigned)i * ED_REC + 1) & PDEF_DELETED))
            n++;
    return n;
}

/* nav (20.10) owns the selection; until it lands there is none. nav defines
 * PDU_NAV beside its include and these two are gone. */
#ifndef PDU_NOTES
void pdu_nt_new(unsigned page_id, const struct pd_rect *r) { pdu_tell("Notes are not in this build."); }
void pdu_nt_open(int i) { }
void pdu_nt_inspect(int i) { }
#endif
#ifndef PDU_NAV
int pdu_sel_quads(pd_pt *dst, unsigned cap) { return 0; }
int pdu_sel_text(char *dst, unsigned cap) { return 0; }
#endif

/* ============================================================================
 * THE SELECTION AND THE TYPING SESSION
 *
 * The selected record is drawn as an XOR outline (and corner handles on a
 * shape that resizes): the page under it is PDREND's, so nothing here ever
 * writes an annotation pixel, and an XOR can be taken back with no redraw.
 * The rule that keeps it honest, because a render overwrites pixels the XOR
 * knows nothing about:
 *
 *   - the strips actually drawn are KEPT (ed_xl), and taking the outline off
 *     replays exactly those, never a recomputation from a view that may have
 *     moved since;
 *   - a render BEGIN takes it off first, while the page under the render is
 *     still the page the XOR was drawn on, and the render's END puts it back.
 *     A scroll and a page change take it off before they move anything.
 * ==========================================================================*/
char pdu_tbuf[1025];
static pdw pdu_ed_genv;
unsigned pdu_ed_gen(void) { return pdu_ed_genv; }
static int pdu_ed_sel = -1;             /* the selected record, -1 none */
static int pdu_tx = -1;                 /* the record being typed into */
static int pdu_tx_new;                  /* ...born in this session */
static int ed_xon, ed_xn;
static int ed_xrem = 1;                 /* remember the strips (the selection's) */
static int ed_xl[8][4];

void pdu_ed_sel_fix(void)
{
    if (pdu_ed_sel >= 0 && (pdu_ed_sel >= (int)pdu_ed_n
        || (pd_fpeekb(ed_seg(), (unsigned)pdu_ed_sel * ED_REC + 1)
            & PDEF_DELETED)))
        pdu_ed_sel = -1;
    if (pdu_tx >= (int)pdu_ed_n)
        pdu_tx = -1;
}

/* one clipped XOR fill, remembered so it can be replayed */
static void ovl_ed_xf(int x1, int y1, int x2, int y2)
{
    struct os88_rect *c;
    c = &pdu_L.canvas;
    if (x1 < c->x1) x1 = c->x1;
    if (y1 < c->y1) y1 = c->y1;
    if (x2 > c->x2) x2 = c->x2;
    if (y2 > c->y2) y2 = c->y2;
    if (x1 > x2 || y1 > y2 || (ed_xrem && ed_xn >= 8))
        return;
    if (ed_xrem) {
        ed_xl[ed_xn][0] = x1;
        ed_xl[ed_xn][1] = y1;
        ed_xl[ed_xn][2] = x2;
        ed_xl[ed_xn][3] = y2;
        ed_xn++;
    }
    os88_gfx_xor_fill(x1, y1, x2, y2);
}

/* the frame of a screen rect, and a 5x5 handle on each corner (a handle
 * only when it is wholly on the canvas, so on and off agree) */
static void ovl_ed_frame(struct os88_rect *r, int handles)
{
    struct os88_rect *c;
    int x, y;
    c = &pdu_L.canvas;
    ovl_ed_xf(r->x1, r->y1, r->x2, r->y1);
    ovl_ed_xf(r->x1, r->y2, r->x2, r->y2);
    ovl_ed_xf(r->x1, r->y1 + 1, r->x1, r->y2 - 1);
    ovl_ed_xf(r->x2, r->y1 + 1, r->x2, r->y2 - 1);
    if (!handles)
        return;
    for (y = 0; y < 2; y++)
        for (x = 0; x < 2; x++) {
            int hx, hy;
            hx = x ? r->x2 : r->x1;
            hy = y ? r->y2 : r->y1;
            if (hx - 2 >= c->x1 && hx + 2 <= c->x2 && hy - 2 >= c->y1
                && hy + 2 <= c->y2)
                ovl_ed_xf(hx - 2, hy - 2, hx + 2, hy + 2);
        }
}

/* the screen rect of the selection (inflated so it sits OUTSIDE the shape) */
static int ovl_ed_selrect(struct os88_rect *o)
{
    if (pdu_ed_sel < 0 || pdu_doc != D_OPEN
        || ovl_ed_rd(pdu_ed_sel, &ed_v) || ed_v.page != pdu_ed_pg()
        || (ed_v.flags & PDEF_DELETED))
        return 0;
    ovl_ed_scr(&ed_v.rect, 3, o);
    return 1;
}

void pdu_ed_xon_l(void)
{
    if (ed_xon || pdu_pop != P_NONE || pdu_about || !ovl_ed_selrect(&ed_sr))
        return;
    ed_xn = 0;
    ed_xrem = 1;
    ovl_ed_frame(&ed_sr, ed_v.kind == PDA_TEXT || ed_v.kind == PDA_RECT
                     || ed_v.kind == PDA_ELLIPSE || ed_v.kind == PDA_AREA);
    ed_xon = 1;
}

void pdu_ed_xoff_l(void)
{
    int k;
    if (!ed_xon)
        return;
    for (k = 0; k < ed_xn; k++)
        os88_gfx_xor_fill(ed_xl[k][0], ed_xl[k][1], ed_xl[k][2], ed_xl[k][3]);
    ed_xon = 0;
}

/* the same two, for a caller that does NOT hold the lock (the wake handler) */
void pdu_ed_xoff(void)
{
    if (!ed_xon)
        return;
    os88_gfx_lock();
    if (os88_wm_clip_set(pdu_win) == 0)
        pdu_ed_xoff_l();
    else
        ed_xon = 0;                     /* nothing of us shows: it is gone */
    os88_gfx_unlock();
}

/* the same, resident: the render's end asks, and only a selection answers */
void pdu_ed_xon(void)
{
    if (pdu_ed_sel >= 0 && !ed_xon)
        pdu_ed_xon_w();
}

void pdu_ed_xon_w(void)
{
    os88_gfx_lock();
    if (os88_wm_clip_set(pdu_win) == 0)
        pdu_ed_xon_l();
    os88_gfx_unlock();
}

/* the canvas is about to be repainted whole (zoom, page change, fit): the
 * outline off first, and a selection or typing on another page let go */
void pdu_ed_canvas(void)
{
    pdu_ed_xoff_l();
    if (pdu_ed_sel >= 0 || pdu_tx >= 0)
        pdu_ed_canvas2();
}

void pdu_ed_canvas2(void)
{
    if (pdu_ed_sel >= 0 && ovl_ed_rd(pdu_ed_sel, &ed_v) == 0
        && ed_v.page != pdu_ed_pg())
        pdu_ed_sel = -1;
    if (pdu_tx >= 0 && ovl_ed_rd(pdu_tx, &ed_v) == 0
        && ed_v.page != pdu_ed_pg())
        pdu_tx_end();
}

/* the outline, unless a render owns the glass (its end draws it) */
void pdu_ed_show(void)
{
    if (!pdu_rwant && pdu_job != J_RENDER)
        pdu_ed_xon_l();
}

/* select record i (-1 none) */
void pdu_ed_select(int i)
{
    if (i == pdu_ed_sel)
        return;
    pdu_ed_xoff_l();
    pdu_ed_sel = i;
    pdu_ed_genv++;
    pdu_ed_show();
}

/* the document is going: forget everything WITHOUT touching the glass (the
 * empty view has been, or is about to be, painted over it) */
void pdu_ed_gone(void)
{
    ed_xon = 0;
    pdu_ed_sel = -1;
    pdu_tx = -1;
}

/* ---- typing ---------------------------------------------------------------- */
/* append ch to record i's text, or (ch < 0) drop its last character */
static int ovl_ed_tx(int i, int ch)
{
    unsigned len;
    if (ovl_ed_rd(i, &ed_w))
        return -1;
    len = ed_w.text_len;
    if (ch < 0 && len == 0)
        return 0;
    if (ch >= 0 && len >= ED_TMAX) {
        pdu_ed_err = PDE_LIMIT;
        return -1;
    }
    ovl_ed_mark(&ed_w);
    if (ch < 0) {
        ovl_ud_text(i);
        ed_w.text_len = len - 1;
    } else if (ed_w.text_off + len == pdu_ed_tt && ovl_ed_room(1, 0)) {
        ovl_ud_text(i);                     /* the tail of the last block: in place */
        pd_fpokeb(ed_seg(), pdu_ed_tt, (pdb)ch);
        pdu_ed_tt++;
        ed_w.text_len = len + 1;
    } else {
        if (!ovl_ed_room(len + 1, 0)) {
            pdu_ed_err = PDE_NOMEM;
            return -1;
        }
        ovl_ud_text(i);
        pd_fcopy(ed_seg(), pdu_ed_tt, ed_seg(), ed_w.text_off, len);
        pd_fpokeb(ed_seg(), pdu_ed_tt + len, (pdb)ch);
        ed_w.text_off = pdu_ed_tt;
        pdu_ed_tt += len + 1;
        ed_w.text_len = len + 1;
    }
    ed_w.flags |= PDEF_DIRTY;
    ovl_ed_wr(i, &ed_w);
    ovl_ed_mark(&ed_w);
    return 1;
}

/* a key for the open text. 1 = taken. Enter is a new line; Esc is the
 * shell's (it ends the session). */
int pdu_tx_key(int ascii)
{
    int ch, r;
    if (pdu_tx < 0)
        return 0;
    if (ascii == 8)
        ch = -1;
    else if (ascii == 13)
        ch = 10;
    else if (ascii >= 32)
        ch = ascii;
    else
        return 0;
    pdu_undo_begin(UL_TYPE);
    r = ovl_ed_tx(pdu_tx, ch);
    pdu_undo_end();
    if (r < 0)
        pdu_tell(pdu_ed_err == PDE_LIMIT
                 ? "Text limit: 1,024 characters."
                 : "The edit store is full.");
    return 1;
}

void pdu_tx_begin(int i, int isnew)
{
    pdu_tx = i;
    pdu_tx_new = isnew;
    pdu_tell("Typing: Esc finishes.");
}

/* the selected record, for the shell's greying */
int pdu_ed_selected(void) { return pdu_ed_sel; }
