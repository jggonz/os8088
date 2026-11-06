/* ============================================================================
 * os8088 - apps/pagedesk/pdwpg.c       page operations (PAGEDESK-SPEC 13.7,
 *                                      20.11.2)
 *
 * #included by pdwrt.c after pdwsave.c. An incremental save whose page map
 * (pdwmap.h) is not the source's own order rewrites the page tree FLAT: the
 * root /Pages node is written again with /Kids in the new order, and
 *   - a page that is rotated, is under a node other than the root, or has
 *     annotation edits is written again (inherited /Resources, /MediaBox and
 *     /CropBox materialised when it is moved to the root);
 *   - a duplicate is a NEW page object sharing /Contents and /Resources by
 *     reference whose annotations are copied to new objects (new /NM, /P
 *     retargeted, /Popup and /IRT remapped);
 *   - a blank page is a new object of the size of the page it names.
 * The same dictionary-walking and reference-rewriting is what the graph copy
 * (pdwgc.c) uses to copy objects between documents.
 * ==========================================================================*/
#ifdef PDW_TRACE                /* the host harness names the line a refusal came from */
#include <stdio.h>
#undef PDE_SYNTAX
#define PDE_SYNTAX (fprintf(stderr, "PDE_SYNTAX at %s:%d\n", __FILE__, __LINE__), 1)
#endif
#define PGK_PARENT 1
#define PGK_ROTATE 2
#define PGK_ANNOTS 4
#define PGK_KIDS   8
#define PGK_COUNT  16
#define PGK_NM     32
#define DM_MAX     16

static char g_inh[600];                 /* an inherited /Resources                    */
static pdw pf_par, pf_parg, pf_hasres, pf_hasmed, pf_hascrop, ih_a, ih_b;
static pdw rs_mode;                     /* 0: the duplicate's map, 1: the graph copy's */
static pdw dm_old[DM_MAX], dm_gen[DM_MAX], dm_n, dm_new, dm_pgold, dm_pgnew;


/* ---- walking the keys of the dictionary at ds in g_pg ----------------------- */
static pdw dk_p, dk_np, dk_ne, dk_vs, dk_ve;
static pdw dk_next(void)                /* 1 = a key, 0 = the end, SC_BAD = malformed */
{
    dk_p = sc_skip(dk_p);
    if (dk_p >= sc_n) return SC_BAD;
    if (g_pg[dk_p] == '>') return 0;
    if (g_pg[dk_p] != '/') return SC_BAD;
    dk_np = dk_p; dk_ne = sc_tok((pdw)(dk_p + 1));
    dk_vs = sc_skip(dk_ne); dk_ve = sc_val(dk_vs, 1);
    if (dk_ve == SC_BAD) return SC_BAD;
    dk_p = dk_ve;
    return 1;
}
static pdw dk_first(pdw ds) { dk_p = (pdw)(ds + 2); return dk_next(); }
static pdw dk_bit(void)
{
    if (sc_eqname((pdw)(dk_np + 1), dk_ne, "Parent")) return PGK_PARENT;
    if (sc_eqname((pdw)(dk_np + 1), dk_ne, "Rotate")) return PGK_ROTATE;
    if (sc_eqname((pdw)(dk_np + 1), dk_ne, "Annots")) return PGK_ANNOTS;
    if (sc_eqname((pdw)(dk_np + 1), dk_ne, "Kids")) return PGK_KIDS;
    if (sc_eqname((pdw)(dk_np + 1), dk_ne, "Count")) return PGK_COUNT;
    if (sc_eqname((pdw)(dk_np + 1), dk_ne, "NM")) return PGK_NM;
    return 0;
}

/* ---- rewriting the references in g_pg[a,b) -------------------------------------
 * emit = 1: write the text with every "n g R" mapped (rs_map; 0 = null);
 * emit = 0: only gc_mark each referenced object. Strings, hex strings and
 * names are skipped by syntax, so "5 0 R" inside a string is left alone. */
static void gc_mark(pdw n);
static pdw  gc_newnum(pdw n);
static pdw rs_map(pdw n)
{
    static pdw k;
    if (rs_mode) return gc_newnum(n);
    for (k = 0; k < dm_n; k++) if (dm_old[k] == n) return (pdw)(dm_new + k);
    if (n == dm_pgold) return dm_pgnew;
    return n;
}
static void rs_walk(pdw a, pdw b, pdw emit)
{
    static pdw g;
    pdw p = a, m = a, q, r, n, nn, nest; pdb c;
    while (p < b) {
        c = (pdb)g_pg[p];
        if (c == '(') {
            nest = 1; p++;
            while (p < b && nest) {
                if (g_pg[p] == '\\') p++;
                else if (g_pg[p] == '(') nest++;
                else if (g_pg[p] == ')') nest--;
                p++;
            }
        } else if (c == '<') {
            if (p + 1 < b && g_pg[p + 1] == '<') p += 2;
            else { while (p < b && g_pg[p] != '>') p++; p++; }
        } else if (c >= '0' && c <= '9' && (p == a || sc_ws((pdb)g_pg[p - 1]) || sc_delim((pdb)g_pg[p - 1]))) {
            q = sc_tok(p);
            if (sc_isnum(p, q)) {
                r = sc_skip(q);
                if (sc_isnum(r, sc_tok(r))) {
                    r = sc_skip(sc_tok(r));
                    if (r < b && g_pg[r] == 'R' && (r + 1 >= b || sc_ws((pdb)g_pg[r + 1]) || sc_delim((pdb)g_pg[r + 1]))) {
                        n = sc_ref(p, (pdw)(r + 1), &g);
                        if (emit) {
                            pb_n(g_pg + m, (pdw)(p - m));
                            nn = n ? rs_map(n) : 0;
                            if (nn) { pb_u(nn); pb_s(" 0 R"); } else pb_s("null");
                            m = (pdw)(r + 1);
                        } else if (n) gc_mark(n);
                        p = (pdw)(r + 1);
                        continue;
                    }
                }
            }
            p = q;
        } else p++;
    }
    if (emit) pb_n(g_pg + m, (pdw)(b - m));
}

/* the dictionary at ds written again, but for the keys in `skip`; the value of
 * every other key goes through rs_walk (map = 1) or verbatim. The dictionary
 * is left OPEN: the caller appends keys and writes " >>". */
static pdw dk_copy(pdw ds, pdw skip, pdw map)
{
    static pdw r;
    pb_s("<<");
    for (r = dk_first(ds); r == 1; r = dk_next()) {
        if (dk_bit() & skip) continue;
        pb_c(' '); pb_n(g_pg + dk_np, (pdw)(dk_ne - dk_np)); pb_c(' ');
        if (map) rs_walk(dk_vs, dk_ve, 1); else pb_n(g_pg + dk_vs, (pdw)(dk_ve - dk_vs));
    }
    return r == 0 ? PDE_OK : PDE_SYNTAX;
}

/* ---- a page, loaded with what moving it to the root needs ---------------------- */
/* page num into g_pg with the facts the rewrite wants; with `inh` and no
 * /Resources of its own, the nearest ancestor's /Resources (below `stop`) is
 * copied into g_inh and appended after the object text (ih_a..ih_b) */
static pdw pg_fetch(pdw num, pdw gen, pdw inh, pdw stop)
{
    static pdw g;
    pdw r, ds, f, n, depth, len = 0;
    r = eng_raw(num, gen); if (r) return r;
    ds = sc_body(); if (ds == SC_BAD) return PDE_SYNTAX;
    pf_par = 0; pf_parg = 0; ih_a = 0; ih_b = 0;
    f = dict_find(ds, "Parent"); if (f == SC_BAD) return PDE_SYNTAX;
    if (f == 1) pf_par = sc_ref(sc_vs, sc_ve, &pf_parg);
    pf_hasres = (pdw)(dict_find(ds, "Resources") == 1);
    pf_hasmed = (pdw)(dict_find(ds, "MediaBox") == 1);
    pf_hascrop = (pdw)(dict_find(ds, "CropBox") == 1);
    if (!inh || pf_hasres) return PDE_OK;
    n = pf_par; g = pf_parg;
    for (depth = 0; n && n != stop && depth < 8; depth++) {
        r = eng_raw(n, g); if (r) return r;
        ds = sc_body(); if (ds == SC_BAD) return PDE_SYNTAX;
        f = dict_find(ds, "Resources"); if (f == SC_BAD) return PDE_SYNTAX;
        if (f == 1) {
            len = (pdw)(sc_ve - sc_vs);
            if (len > (pdw)(sizeof g_inh)) return sv_bad(13);
            pw_cpy(g_inh, g_pg + sc_vs, len);
            break;
        }
        f = dict_find(ds, "Parent");
        n = 0; if (f == 1) n = sc_ref(sc_vs, sc_ve, &g);
    }
    r = eng_raw(num, gen); if (r) return r;           /* the page again, intact in g_pg */
    if (!len) return PDE_OK;
    ih_a = (pdw)(sc_n + 1);
    pw_cpy(g_pg + ih_a, g_inh, len);
    ih_b = (pdw)(ih_a + len); sc_n = ih_b;
    return PDE_OK;
}
static void pb_rect(const struct pd_rect *r)
{
    pb_c('['); pb_xy(r->x0, r->y0); pb_xy(r->x1, r->y1); pb_c(']');
}
/* the boxes a page moved to the root must carry (g_pi is the page's PAGEINFO) */
static void pg_geo(void)
{
    if (!pf_hasmed) { pb_s(" /MediaBox "); pb_rect(&g_pi.media); }
    if (!pf_hascrop && (g_pi.crop.x0 != g_pi.media.x0 || g_pi.crop.y0 != g_pi.media.y0
                        || g_pi.crop.x1 != g_pi.media.x1 || g_pi.crop.y1 != g_pi.media.y1)) {
        pb_s(" /CropBox "); pb_rect(&g_pi.crop);
    }
}
static pdw pm_rotfinal(pdw rot) { return (pdw)((g_pi.rotate + 90u * rot) % 360u); }

/* ---- the indirect /Annots array of the page in g_pg, revised --------------------- */
static pdw pg_annarr(pdw page)
{
    static pdw ag;
    pdw an, ds, e, r;
    an = sc_ref(sc_vs, sc_ve, &ag); if (!an) return PDE_SYNTAX;
    r = eng_raw(an, ag); if (r) return r;
    ds = sc_body(); if (ds == SC_BAD || g_pg[ds] != '[') return PDE_SYNTAX;
    e = sc_val(ds, 1); if (e == SC_BAD) return PDE_SYNTAX;
    pw_obj(an, ag);
    r = pg_array(ds, e, page); if (r) return r;
    pb_s("\n"); pw_endobj();
    return PDE_OK;
}

/* ---- the original page, written again if anything about it changed --------------- */
/* g_pi = the page. rot = quarter turns added; edited = it has annotation edits.
 * With no page map (pm_on = 0) this is the 14.2 step 2 patch of /Annots alone. */
static pdw pg_rev(pdw page, pdw rot, pdw edited)
{
    static pdw r, ds, f, reparent, ind, inl, av, ae, skip, num, gen;
    ind = 0;
    inl = 0;
    av = 0;
    ae = 0;
    num = g_pi.objnum;
    gen = g_pi.gen;
    r = pg_fetch(num, gen, 0, pm_root); if (r) return r;
    reparent = (pdw)(pm_on && !pm_flat && pf_par != pm_root);
    ds = sc_body(); f = dict_find(ds, "Annots");
    if (f == SC_BAD) return PDE_SYNTAX;
    if (f == 1) { if (g_pg[sc_vs] == '[') inl = 1; else ind = 1; }
    if (!rot && !reparent && !edited) return PDE_OK;
    if (edited && ind) { r = pg_annarr(page); if (r) return r; }
    if (!rot && !reparent && ind) return PDE_OK;                 /* the array was all that changed */
    r = pg_fetch(num, gen, reparent, pm_root); if (r) return r;  /* reloads g_pg (and the inherited text) */
    ds = sc_body(); f = dict_find(ds, "Annots");
    if (f == 1 && g_pg[sc_vs] == '[') { av = sc_vs; ae = sc_ve; }
    skip = 0;
    if (reparent) skip |= PGK_PARENT;
    if (rot || reparent) skip |= PGK_ROTATE;
    if (edited && !ind) skip |= PGK_ANNOTS;
    pw_obj(num, gen);
    r = dk_copy(ds, skip, 0); if (r) return r;
    if (reparent) { pb_s(" /Parent "); pb_u(pm_root); pb_s(" 0 R"); }
    if (rot || reparent) { pb_s(" /Rotate "); pb_u(pm_rotfinal(rot)); }
    if (edited && !ind) {
        pb_s(" /Annots ");
        if (inl) { r = pg_array(av, ae, page); if (r) return r; }
        else { pb_c('['); pg_adds(page); pb_s(" ]"); }
    }
    if (reparent) {
        pg_geo();
        if (ih_a) { rs_mode = 0; dm_n = 0; dm_pgold = 0; pb_s(" /Resources "); rs_walk(ih_a, ih_b, 1); }
    }
    pb_s(" >>\n"); pw_endobj();
    return PDE_OK;
}

/* ---- a new blank page of g_pi's size ------------------------------------------------ */
static void pm_blank_obj(pdw num, pdw parent, pdw rot)
{
    pw_obj(num, 0);
    pb_s("<< /Type /Page /Parent "); pb_u(parent); pb_s(" 0 R /MediaBox "); pb_rect(&g_pi.media);
    pb_s(" /Resources << >> /Rotate "); pb_u(pm_rotfinal(rot)); pb_s(" >>\n");
    pw_endobj();
}

/* ---- a duplicate: its annotations copied, then the page ----------------------------- */
static void dm_list(pdw vs, pdw ve)     /* the refs of the array at g_pg[vs,ve) into dm_old */
{
    static pdw g;
    pdw p = (pdw)(vs + 1), ie, n;
    for (;;) {
        p = sc_skip(p);
        if (p >= ve || g_pg[p] == ']') break;
        ie = sc_val(p, 2); if (ie == SC_BAD) break;
        n = sc_ref(p, ie, &g);
        if (n && dm_n < DM_MAX) { dm_old[dm_n] = n; dm_gen[dm_n] = g; dm_n++; }
        p = ie;
    }
}
static pdw pm_dup(pdw i, pdw rot)
{
    static pdw g;
    pdw r, ds, f, k, newp = tg(T_PMNUM, i), pgnum = g_pi.objnum, pggen = g_pi.gen, an;
    dm_n = 0;
    r = eng_raw(pgnum, pggen); if (r) return r;
    ds = sc_body(); f = dict_find(ds, "Annots"); if (f == SC_BAD) return PDE_SYNTAX;
    if (f == 1 && g_pg[sc_vs] == '[') dm_list(sc_vs, sc_ve);
    else if (f == 1) {
        an = sc_ref(sc_vs, sc_ve, &g);
        if (an) {
            r = eng_raw(an, g); if (r) return r;
            ds = sc_body(); if (ds != SC_BAD && g_pg[ds] == '[') { f = sc_val(ds, 1); if (f != SC_BAD) dm_list(ds, f); }
        }
    }
    rs_mode = 0; dm_new = (pdw)(newp + 1); dm_pgold = pgnum; dm_pgnew = newp;
    for (k = 0; k < dm_n; k++) {                         /* each annotation, as a new object */
        r = eng_raw(dm_old[k], dm_gen[k]); if (r) return r;
        ds = sc_body(); if (ds == SC_BAD || g_pg[ds] != '<') return PDE_SYNTAX;
        pw_obj((pdw)(newp + 1 + k), 0);
        pb_s("<<");
        for (f = dk_first(ds); f == 1; f = dk_next()) {
            if (dk_bit() == PGK_NM) {
                pb_s(" /NM (PD-"); pb_hex4(g_nm_hi); pb_hex4(g_nm_lo); pb_c('-'); pb_hex4((pdw)(0x8000u + newp + k)); pb_c(')');
                continue;
            }
            pb_c(' '); pb_n(g_pg + dk_np, (pdw)(dk_ne - dk_np)); pb_c(' ');
            rs_walk(dk_vs, dk_ve, 1);
        }
        pb_s(" >>\n"); pw_endobj();
    }
    r = pg_fetch(pgnum, pggen, 1, pm_root); if (r) return r;
    ds = sc_body();
    pw_obj(newp, 0);
    r = dk_copy(ds, PGK_PARENT | PGK_ROTATE | PGK_ANNOTS, 0); if (r) return r;
    pb_s(" /Parent "); pb_u(pm_root); pb_s(" 0 R /Rotate "); pb_u(pm_rotfinal(rot));
    if (dm_n) {
        pb_s(" /Annots [");
        for (k = 0; k < dm_n; k++) { pb_c(' '); pb_u((pdw)(newp + 1 + k)); pb_s(" 0 R"); }
        pb_s(" ]");
    }
    pg_geo();
    if (ih_a) { dm_n = 0; dm_pgold = 0; pb_s(" /Resources "); rs_walk(ih_a, ih_b, 1); }
    pb_s(" >>\n"); pw_endobj();
    return PDE_OK;
}

/* ---- the map, validated; the root read; numbers assigned ----------------------------- */
/* 0 = the source's own order (pm_on stays 0), else pm_on with the numbers of the
 * new objects taken from sv_base up */
static pdw pm_prep(void)
{
    static pdw n, i, e, k, idx, ident, next, r, ds, f, kids, cnt, b, p, ie;
    n = g_sv.pages_count;
    ident = 1;
    kids = 0;
    cnt = 0;
    pm_on = 0; pm_n = 0; pm_flat = 1;
    if (!n) return PDE_OK;
    if (!g_sv.pages_seg) return PDE_NOMEM;
    if (n > PDM_MAXMAP) return sv_bad(11);
    tclr(T_PMIN, 192);
    for (i = 0; i < n; i++) {
        e = pd_fpeekw(g_sv.pages_seg, (pdw)(2 * i)); k = PDM_KIND(e); idx = PDM_IDX(e);
        if (k != PDM_SRC && k != PDM_BLANK) { g_sv.err_detail = 14; return PDE_RANGE; }
        if (idx >= PD_MAXPAGES) return PDE_RANGE;
        if (k != PDM_SRC || PDM_ROT(e) || idx != i) ident = 0;
        if (k == PDM_SRC) bm_set(T_PMIN, idx);
    }
    r = eng_pageinfo(n);                      /* a page of the source left out? */
    sv_pgidx = 0xFFFF;
    if (ident && r != PDE_OK) return PDE_OK;
    pm_on = 1; pm_n = n;
    /* the root of the page tree: its other keys are kept, /Kids and /Count are new */
    r = eng_raw(g_tr.root_num, g_tr.root_gen); if (r) return r;
    ds = sc_body(); if (ds == SC_BAD) return PDE_SYNTAX;
    f = dict_find(ds, "Pages"); if (f != 1) return PDE_SYNTAX;
    pm_root = sc_ref(sc_vs, sc_ve, &pm_rootgen); if (!pm_root) return PDE_SYNTAX;
    r = eng_raw(pm_root, pm_rootgen); if (r) { g_sv.err_detail = 13; return r; }
    ds = sc_body(); if (ds == SC_BAD) return PDE_SYNTAX;
    for (f = dk_first(ds); f == 1; f = dk_next()) {
        b = dk_bit();
        if (b == PGK_KIDS) {
            p = (pdw)(dk_vs + 1);
            for (;;) {
                p = sc_skip(p);
                if (p >= dk_ve || g_pg[p] == ']') break;
                ie = sc_val(p, 2); if (ie == SC_BAD) return PDE_SYNTAX;
                kids++; p = ie;
            }
        } else if (b == PGK_COUNT) for (p = dk_vs; p < dk_ve && g_pg[p] >= '0' && g_pg[p] <= '9'; p++) cnt = (pdw)(cnt * 10 + (g_pg[p] - '0'));
    }
    if (f != 0) return PDE_SYNTAX;
    pm_flat = (pdw)(kids && kids == cnt);       /* every kid is a leaf (single-page nodes excepted) */
    next = sv_base;
    for (i = 0; i < n; i++) {
        e = pd_fpeekw(g_sv.pages_seg, (pdw)(2 * i)); k = PDM_KIND(e); idx = PDM_IDX(e);
        ts(T_PMNUM, i, 0);
        if (k == PDM_SRC) {
            if (bm_get(T_PMDONE, idx)) {         /* a duplicate */
                r = eng_pageinfo(idx); if (r) return r;
                if (g_pi.nannots > DM_MAX) return sv_bad(12);
                ts(T_PMNUM, i, next); next = (pdw)(next + 1 + g_pi.nannots);
            } else bm_set(T_PMDONE, idx);
        } else ts(T_PMNUM, i, next++);
        if (next > 0xFFF0) return PDE_LIMIT;
    }
    tclr(T_PMDONE, 64);   /* now: the first occurrence is processed */
    sv_pgidx = 0xFFFF;
    sv_base = next;
    return PDE_OK;
}

/* one map entry per call (PH_PGOPS) */
static pdw pm_step(void)
{
    static pdw e, k, idx, rot, r;
    if (pm_i >= pm_n) { sv_phase = PH_CAT; return PDE_OK; }
    e = pd_fpeekw(g_sv.pages_seg, (pdw)(2 * pm_i)); k = PDM_KIND(e); idx = PDM_IDX(e); rot = PDM_ROT(e);
    r = eng_pageinfo(idx); if (r) return r;
    if (k == PDM_BLANK) pm_blank_obj(tg(T_PMNUM, pm_i), pm_root, rot);
    else if (!bm_get(T_PMDONE, idx)) {                                   /* the original */
        bm_set(T_PMDONE, idx);
        ts(T_PMNUM, pm_i, g_pi.objnum);
        r = pg_rev(idx, rot, bm_get(T_PMED, idx)); if (r) return r;
    } else {
        r = pm_dup(pm_i, rot); if (r) return r;
    }
    pm_i++;
    return PDE_OK;
}

/* the root /Pages node again, /Kids in the new order (13.7): its other keys stay */
static pdw pm_tree(void)
{
    static pdw i, r, ds;
    r = eng_raw(pm_root, pm_rootgen); if (r) return r;
    ds = sc_body(); if (ds == SC_BAD) return PDE_SYNTAX;
    pw_obj(pm_root, pm_rootgen);
    r = dk_copy(ds, PGK_KIDS | PGK_COUNT, 0); if (r) return r;
    pb_s(" /Kids [");
    for (i = 0; i < pm_n; i++) { pb_c(' '); pb_u(tg(T_PMNUM, i)); pb_s(" 0 R"); }
    pb_s(" ] /Count "); pb_u(pm_n); pb_s(" >>\n"); pw_endobj();
    return PDE_OK;
}
