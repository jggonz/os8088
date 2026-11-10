/* ============================================================================
 * os8088 - apps/pagedesk/pdwgc.c       the graph copy: Extract (13.8, 20.11.4)
 *
 * #included by pdwrt.c after pdwpg.c. Extract writes a FRESH document holding
 * only the chosen pages and what they reach:
 *
 *   1. PH_XROOT   PAGEINFO of every chosen page: its object is a root
 *   2. PH_XDISC   a fixpoint over two bit maps (one bit per source object
 *                 number, in the writer buffer): `seen` = reachable, `scan` =
 *                 its references have been read. An object whose /Type is
 *                 Page, Pages or Catalog (and is not a root) is NOT copied -
 *                 every reference to it becomes null, which is how a link's
 *                 destination on a page that is not here is dropped - and a
 *                 reference to an object the source does not have is null.
 *                 A prefix count per 128 numbers makes rank = new number.
 *   3. PH_XHEAD   objects 1-3: catalog, flat /Pages, /Info
 *   4. PH_XCOPY   every seen object in ascending source order, so new numbers
 *                 4.. are the xref's one ascending run: the dictionary with
 *                 each "n g R" rewritten, the stream bytes (everything from
 *                 `stream` to `endobj`) copied verbatim in buffer-sized pieces
 *   5. PH_XBLANK  the blank pages
 *
 * A root page is written from its PAGEINFO: /Parent the new /Pages, /Rotate
 * final, MediaBox / CropBox / inherited /Resources materialised (13.8).
 * ==========================================================================*/

#ifdef PDW_TRACE                /* the host harness names the line a refusal came from */
#include <stdio.h>
#undef PDE_SYNTAX
#define PDE_SYNTAX (fprintf(stderr, "PDE_SYNTAX at %s:%d\n", __FILE__, __LINE__), 1)
#endif
static pdw gc_popc(pdw v) { pdw c = 0; while (v) { v = (pdw)(v & (pdw)(v - 1)); c++; } return c; }
static pdw gc_get(pdw base, pdw w) { return pd_fpeekw(g_wseg, (pdw)(base + 2u * w)); }

static void gc_mark(pdw n)
{
    pdw w = (pdw)(n >> 4), b = (pdw)(1u << (n & 15)), v;
    if (!n || n >= g_tr.size) return;   /* not in the xref: its references become null */
    if (gc_get(gc_scan, w) & b) return;
    v = gc_get(gc_seen, w);
    if (v & b) return;
    if (gc_cnt >= gc_cap) { gc_over = 1; return; }
    pd_fpokew(g_wseg, (pdw)(gc_seen + 2u * w), (pdw)(v | b));
    gc_cnt++; gc_dirty = 1;
}
static pdw gc_root(pdw n);
static pdw gc_newnum(pdw n)             /* the window's first number + the rank among seen objects; 0 = not copied */
{
    pdw w = (pdw)(n >> 4), b = (pdw)(1u << (n & 15)), k, c;
    if (!n || n >= g_tr.size) return 0;
    if (!(gc_get(gc_seen, w) & b)) {    /* Insert: a chosen page is not in the set, it has its number already */
        if (!gc_ins) return 0;
        c = gc_root(n);
        return c ? tg(T_PMNUM, (pdw)(c - 1)) : 0;
    }
    c = gc_get(gc_pref, (pdw)(w >> 3));
    for (k = (pdw)((w >> 3) << 3); k < w; k++) c = (pdw)(c + gc_popc(gc_get(gc_seen, k)));
    c = (pdw)(c + gc_popc((pdw)(gc_get(gc_seen, w) & (pdw)(b - 1))));
    return (pdw)(gc_b0 + c);
}
static pdw gc_root(pdw n)               /* the map entry (+1) whose page object is n */
{
    pdw i;
    for (i = 0; i < pm_n; i++) if (tg(gc_ins ? T_FSRC : T_PMNUM, i) == n) return (pdw)(i + 1);
    return 0;
}
static pdw gc_pg(pdw e) { return gc_ins ? (pdw)(PDM_IDX(e) & 1023u) : PDM_IDX(e); }
static pdw gc_mine(pdw e)
{
    if (gc_ins) return (pdw)(PDM_KIND(e) == PDM_FOREIGN && ((PDM_IDX(e) >> 10) & 3u) == gc_slot);
    return (pdw)(PDM_KIND(e) == PDM_SRC);
}
static pdw gc_isdict(pdw ds) { return (pdw)(ds + 1 < sc_n && g_pg[ds] == '<' && g_pg[ds + 1] == '<'); }

/* the first chunk of object n; PDE_OK, or the engine's refusal */
static pdw gc_first(pdw n) { return eng_chunk(n, 0, &gc_zero); }

static void gc_exclude(pdw w, pdw b)    /* scanned, but not seen: never copied */
{
    pd_fpokew(g_wseg, (pdw)(gc_seen + 2u * w), (pdw)(gc_get(gc_seen, w) & (pdw)~b));
    if (gc_cnt) gc_cnt--;
}

/* PH_XDISC: one object a call. GC_DONE = the fixpoint is reached */
static pdw gc_disc(void)
{
    static pdw v, b, k, n, r, ds, de, f, root, w, c, j;
    b = 1;
    k = 0;
    for (;;) {
        if (gc_w >= gc_nw) {
            if (gc_over) return sv_bad(16);
            if (!gc_dirty) break;
            gc_dirty = 0; gc_w = 0;
        }
        v = (pdw)(gc_get(gc_seen, gc_w) & (pdw)~gc_get(gc_scan, gc_w));
        if (v) {
            while (!(v & b)) { b = (pdw)(b << 1); k++; }
            n = (pdw)((gc_w << 4) | k);
            pd_fpokew(g_wseg, (pdw)(gc_scan + 2u * gc_w), (pdw)(gc_get(gc_scan, gc_w) | b));
            root = gc_root(n);
            r = gc_first(n);
            if (r == PDE_IO || r == PDE_TRUNC || (r && root)) return r;  /* a chosen page must copy */
            if (r) { gc_exclude(gc_w, b); return PDE_OK; }              /* a dangling reference */
            ds = sc_body();
            if (ds == SC_BAD) { if (root) return PDE_SYNTAX; gc_exclude(gc_w, b); return PDE_OK; }
            if (gc_isdict(ds)) {
                f = dict_find(ds, "Type");
                if (f == SC_BAD) return PDE_SYNTAX;
                if (gc_ins && !root) {          /* 20.16.3: no form field, no script comes along */
                    if (dict_find(ds, "JS") == 1) return sv_bad(18);
                    if (dict_find(ds, "Subtype") == 1 && sc_eqname((pdw)(sc_vs + 1), sc_ve, "Widget")) return sv_bad(18);
                    f = dict_find(ds, "Type");
                }
                if (f == 1 && !root && (sc_eqname((pdw)(sc_vs + 1), sc_ve, "Page") || sc_eqname((pdw)(sc_vs + 1), sc_ve, "Pages")
                                        || sc_eqname((pdw)(sc_vs + 1), sc_ve, "Catalog"))) {
                    gc_exclude(gc_w, b); return PDE_OK;
                }
            }
            de = sc_val(ds, 1);
            if (de == SC_BAD) return sv_bad(14);                           /* a dictionary over the buffer */
            rs_mode = 1;
            if (root) {
                r = eng_pageinfo(gc_pg(pd_fpeekw(g_sv.pages_seg, (pdw)(2 * (root - 1))))); if (r) return r;
                r = pg_fetch(n, g_pi.gen, 1, 0); if (r) return r;
                ds = sc_body(); de = sc_val(ds, 1);
                rs_walk(ds, de, 0);
                if (ih_a) rs_walk(ih_a, ih_b, 0);
            } else rs_walk(ds, de, 0);
            if (gc_over) return sv_bad(16);
            return PDE_OK;
        }
        gc_w++;
    }
    /* the fixpoint: the prefix counts, the total. Insert: the chosen pages leave the set first,
       so the rank counts the copied objects alone (their numbers were given up front) */
    if (gc_ins) for (w = 0; w < pm_n; w++) {
        j = tg(T_FSRC, w);
        if (j) { k = (pdw)(j >> 4); pd_fpokew(g_wseg, (pdw)(gc_seen + 2u * k), (pdw)(gc_get(gc_seen, k) & (pdw)~(1u << (j & 15)))); }
    }
    c = 0;
    for (w = 0; w < gc_nw; w++) {
        if (!(w & 7)) pd_fpokew(g_wseg, (pdw)(gc_pref + 2u * (w >> 3)), c);
        j = gc_get(gc_seen, w);
        if (j) c = (pdw)(c + gc_popc(j));
    }
    gc_total = c;
    return GC_DONE;
}

/* the number of the first blank page: after every copied object */
static pdw gc_blank0(void) { return (pdw)(4 + gc_total); }

/* PH_XHEAD: catalog, /Pages, /Info (objects 1-3) */
static void gc_head(void)
{
    static pdw i, e, b, v;
    b = 0;
    v = (pdw)(g_tr.version < 15 ? 15 : g_tr.version);
    pb_s("%PDF-1."); pb_u((pdw)(v % 10)); pb_s("\n%\xE2\xE3\xCF\xD3\n");
    pw_obj(1, 0); pb_s("<< /Type /Catalog /Pages 2 0 R >>\n"); pw_endobj();
    pw_obj(2, 0); pb_s("<< /Type /Pages /Kids [");
    for (i = 0; i < pm_n; i++) {
        e = pd_fpeekw(g_sv.pages_seg, (pdw)(2 * i));
        pb_c(' ');
        if (PDM_KIND(e) == PDM_SRC) pb_u(gc_newnum(tg(T_PMNUM, i)));
        else pb_u((pdw)(gc_blank0() + b++));
        pb_s(" 0 R");
    }
    pb_s(" ] /Count "); pb_u(pm_n); pb_s(" >>\n"); pw_endobj();
    pw_obj(3, 0); pb_s("<< /Producer (PageDesk for os8088)");
    if (pdw_date[0]) { pb_s(" /CreationDate ("); pb_n(pdw_date, pw_len(pdw_date)); pb_c(')'); }
    pb_s(" >>\n"); pw_endobj();
}

/* PH_XCOPY: one object, or one piece of a stream, a call. GC_DONE = all copied */
static pdw gc_copy(void)
{
    static pdw w, v, b, k, n, r, ds, de, q, root, e;
    if (gc_sub) {                                   /* the rest of a stream object */
        r = eng_chunk(gc_cur, 0, &gc_pos); if (r) return r;
        if (sc_n) { pb_flush(); pw_n(g_pg, sc_n); pdm_u32_add16(&gc_pos, sc_n); }
        if (sc_n < PDW_PGBUF) { pb_s("\n"); gc_sub = 0; }
        return PDE_OK;
    }
    for (;;) {                                      /* the next seen object at or after gc_n */
        w = (pdw)(gc_n >> 4);
        if (w >= gc_nw) return GC_DONE;
        v = gc_get(gc_seen, w);
        if (gc_n & 15) v = (pdw)(v & (pdw)~((1u << (gc_n & 15)) - 1u));
        if (v) break;
        gc_n = (pdw)((w + 1) << 4);
        if (!gc_n) return GC_DONE;
    }
    b = 1; k = 0; while (!(v & b)) { b = (pdw)(b << 1); k++; }
    n = (pdw)((w << 4) | k);
    gc_n = (pdw)(n + 1); if (!gc_n) gc_n = 0xFFFF;
    gc_cur = n;
    rs_mode = 1;
    root = gc_root(n);
    if (root) {
        e = pd_fpeekw(g_sv.pages_seg, (pdw)(2 * (root - 1)));
        r = eng_pageinfo(PDM_IDX(e)); if (r) return r;
        r = pg_fetch(n, g_pi.gen, 1, 0); if (r) return r;
        ds = sc_body();
        pw_obj(gc_k++, 0);
        r = dk_copy(ds, PGK_PARENT | PGK_ROTATE, 1); if (r) return r;
        pb_s(" /Parent 2 0 R /Rotate "); pb_u(pm_rotfinal(PDM_ROT(e)));
        pg_geo();
        if (ih_a) { pb_s(" /Resources "); rs_walk(ih_a, ih_b, 1); }
        pb_s(" >>\n"); pw_endobj();
        return PDE_OK;
    }
    r = gc_first(n); if (r) return r;
    ds = sc_body(); if (ds == SC_BAD) return PDE_SYNTAX;
    de = sc_val(ds, 1); if (de == SC_BAD) return sv_bad(14);
    pw_obj(gc_k++, 0);
    rs_walk(ds, de, 1);
    q = sc_skip(de);
    if (q + 6 <= sc_n && g_pg[q] == 's' && g_pg[q + 1] == 't' && g_pg[q + 2] == 'r' && g_pg[q + 3] == 'e'
        && g_pg[q + 4] == 'a' && g_pg[q + 5] == 'm') {
        pb_c('\n'); pb_flush(); pw_n(g_pg + q, (pdw)(sc_n - q));
        if (sc_n >= PDW_PGBUF) { pdm_u32_set(&gc_pos, 0, sc_n); gc_sub = 1; }
        else pb_s("\n");
    } else pb_s("\nendobj\n");
    return PDE_OK;
}
