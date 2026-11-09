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
/* the kinds with corner handles: Area 3, Text 4, Rect 7, Ellipse 8 */
#define ED_SIZABLE(k) ((0x198 >> (k)) & 1)

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
static pdw pdu_ed_genv;                 /* pdu_ed_gen() (D19) */
/* near scratch shared by the tools (a Line's two points, a Highlight's 12
 * quads) and undo (a page operation's delta, 96 bytes): never both at once */
static pd_pt ed_pts[96];
static struct os88_rect ed_xr;          /* the XOR fill's rect; undo's 8 bytes */

/* undo (pduundo.c) -------------------------------------------------------- */
static int ovl_ud_log(int i, int tag, int o, int l);
#define UD_FIELD   1                    /* pduundo.c's UT_FIELD */
#define ovl_ud_field(i, o, l) ovl_ud_log((i), UD_FIELD | ((l) << 4), (o), (l))
static void ovl_ud_text(int i);
static void ovl_ud_create(void);
static void ovl_ud_move(int i, int dx, int dy);
static void ud_init(void);
static void ovl_ud_tr(int idx, int dx, int dy);
static void pdu_undo_drop(void);
static void ovl_pdu_ed_flush(void);
static void ovl_pdu_ed_touch_i(int i);
static int ovl_ud_trimok(int i);

#define ed_seg() pdu_dcseg(PDU_DC_EDITS)  /* a macro: no thunk from the overlay */

/* the page id of the page on the glass. The pages agent's view map
 * (pdu_pg_id) replaces this one line; until then a view index IS the id. */
#define pdu_ed_pg() pdu_page

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
    pdu_ed_idc = PDU_RC_SEED();         /* recov: /NM unique across sessions (20.17.7) */
    pdu_ed_err = 0;
    pdu_ed_genv++;                      /* a new store: every reader refreshes */
    ud_init();
}

int pdu_ed_count(void) { return (int)pdu_ed_n; }
unsigned pdu_ed_seg(void) { return ed_seg(); }

int pdu_ed_get(int i, struct pd_edit *e)
{
    return ovl_ed_rd(i, e);
}

/* ---- the screen rectangle of a page rect, and the marks ------------------ */
/* o = the box with corners (a, b) and (c, d), in any order, grown by pad */
static void ovl_ed_box(struct os88_rect *o, int a, int b, int c, int d, int pad)
{
    o->x1 = (a < c ? a : c) - pad;
    o->x2 = (a < c ? c : a) + pad;
    o->y1 = (b < d ? b : d) - pad;
    o->y2 = (b < d ? d : b) + pad;
}

static int ed_qx, ed_qy;
static void ovl_ed_scr(const struct pd_rect *r, int pad, struct os88_rect *o)
{
    /* opposite corners stay opposite under any R */
    pdm_view_fwd(&ed_px, &ed_py, &pdu_view, r->x0, r->y0);
    ed_qx = ed_px;
    ed_qy = ed_py;
    pdm_view_fwd(&ed_px, &ed_py, &pdu_view, r->x1, r->y1);
    ovl_ed_box(o, ed_qx, ed_qy, ed_px, ed_py, pad);
}

/* owe the renderer the record's screen rect, cut to the canvas, if it is on
 * the glass. pdu_render_want keeps the union, and nothing renders before the
 * next wake, so a command's marks still become ONE repaint of their union. */
static void ovl_ed_mark(const struct pd_edit *w)
{
    if (pdu_doc != D_OPEN || w->page != pdu_ed_pg())
        return;
    ovl_ed_scr(&w->rect, 6, &ed_sr);
    if (pdu_isect(&ed_sr, &pdu_L.canvas, &ed_sr))
        pdu_render_want(&ed_sr);
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
    if (pb && pts)                      /* NULL: already there (a captured stroke) */
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
    int fl0, fl, k, o, l, j;
    if (ovl_ed_rd(i, &ed_w))
        return -1;
    pdu_undo_begin(UL_NONE);
    ovl_ed_mark(&ed_w);
    fl0 = ed_w.flags;
    a = (unsigned char *)&ed_w;
    b = (unsigned char *)e;
    fl = (fl0 & ~PDEF_IRTSRC) | (e->flags & PDEF_IRTSRC) | PDEF_DIRTY;
    for (k = 0; k < 3; k++) {           /* rect 8; colour..align 4; irt, state 4 */
        o = k == 0 ? EO_RECT : k == 1 ? EO_COLOUR : EO_IRT;
        l = k ? 4 : 8;
        for (j = 0; j < l && a[o + j] == b[o + j]; j++)
            ;
        if (j < l) {                    /* the group differs: log, then copy */
            ovl_ud_field(i, o, l);
            os88_memcpy(a + o, b + o, l);
            if (!k)
                fl |= PDEF_MOVED;
        }
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
int pdu_ed_text(int i, const char *text, unsigned tlen)
{
    pdu_ed_err = 0;
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
    ovl_ud_log(i, 3, EO_PTS, 4);         /* UT_PTS */
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

int pdu_ed_del(int i)
{
    if (ovl_ed_rd(i, &ed_w))
        return -1;
    pdu_undo_begin(UL_NONE);
    ovl_ud_field(i, EO_FLAGS, 1);
    ovl_ed_mark(&ed_w);
    ed_w.flags |= PDEF_DELETED | PDEF_DIRTY;
    ovl_ed_wr(i, &ed_w);
    pdu_undo_end();
    return 0;
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
int pdu_ed_next(int i, unsigned page)
{
    for (i++; i >= 0 && i < (int)pdu_ed_n; i++)
        if (!(pd_fpeekb(ed_seg(), (unsigned)i * ED_REC + 1) & PDEF_DELETED)
            && pd_fpeekw(ed_seg(), (unsigned)i * ED_REC + 2) == page)
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
    ed_xr.x1 = x1;
    ed_xr.y1 = y1;
    ed_xr.x2 = x2;
    ed_xr.y2 = y2;
    if (!pdu_isect(&ed_xr, &pdu_L.canvas, &ed_xr) || (ed_xrem && ed_xn >= 8))
        return;
    if (ed_xrem)
        os88_memcpy(ed_xl[ed_xn++], &ed_xr, sizeof(ed_xr));
    os88_gfx_xor_fill(ed_xr.x1, ed_xr.y1, ed_xr.x2, ed_xr.y2);
}

/* the frame of a screen rect and, for a shape that resizes, a 4x4 handle
 * just OUTSIDE each corner - touching it, never over the frame, so no pixel
 * is flipped twice (a handle centred on the corner came out notched) */
static void ovl_ed_frame(struct os88_rect *r, int handles)
{
    int k, x, y;
    ovl_ed_xf(r->x1, r->y1, r->x2, r->y1);
    ovl_ed_xf(r->x1, r->y2, r->x2, r->y2);
    ovl_ed_xf(r->x1, r->y1 + 1, r->x1, r->y2 - 1);
    ovl_ed_xf(r->x2, r->y1 + 1, r->x2, r->y2 - 1);
    if (handles)
        for (k = 0; k < 4; k++) {
            x = (k & 1) ? r->x2 + 1 : r->x1 - 4;
            y = (k & 2) ? r->y2 + 1 : r->y1 - 4;
            ovl_ed_xf(x, y, x + 3, y + 3);
        }
}

/* the selection's outline: its screen rect inflated so it sits OUTSIDE the
 * shape, when it is on the page shown and nothing covers the canvas */
void pdu_ed_xon_l(void)
{
    if (ed_xon || pdu_pop != P_NONE || pdu_about || pdu_ed_sel < 0
        || pdu_doc != D_OPEN || ovl_ed_rd(pdu_ed_sel, &ed_v)
        || ed_v.page != pdu_ed_pg() || (ed_v.flags & PDEF_DELETED))
        return;
    ovl_ed_scr(&ed_v.rect, 3, &ed_sr);
    ed_xn = 0;
    ed_xrem = 1;
    ovl_ed_frame(&ed_sr, ED_SIZABLE(ed_v.kind));
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

/* the same two, for a caller that does NOT hold the lock (the wake handler).
 * A render of strip r takes the outline off only when one of its strips
 * meets r: PDREND writes nothing outside r, so an outline elsewhere stays
 * lit and its replay (8 XOR fills each way) is not spent on every repaint. */
static void ovl_ed_xoff_r(const struct os88_rect *r)
{
    int k;
    for (k = 0; k < ed_xn; k++)
        if (ed_xl[k][0] <= r->x2 && ed_xl[k][2] >= r->x1
            && ed_xl[k][1] <= r->y2 && ed_xl[k][3] >= r->y1)
            break;
    if (k == ed_xn)
        return;
    os88_gfx_lock();
    if (os88_wm_clip_set(pdu_win) == 0)
        pdu_ed_xoff_l();
    else
        ed_xon = 0;                     /* nothing of us shows: it is gone */
    os88_gfx_unlock();
}

void pdu_ed_xoff(const struct os88_rect *r)
{
    if (ed_xon)                         /* the overlay only with a selection lit */
        ovl_ed_xoff_r(r);
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
        && ed_v.page != pdu_ed_pg()) {
        pdu_ed_sel = -1;
        pdu_ed_genv++;                  /* a selection change (D19) */
    }
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
    pdu_ed_genv++;
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
        if (ed_w.text_off + len == pdu_ed_tt && ovl_ud_trimok(i))
            pdu_ed_tt--;                    /* a byte this run wrote: reuse it */
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
