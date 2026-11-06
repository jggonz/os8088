/* ============================================================================
 * pdepage.c - the page tree, inherited attributes, resources (PAGEDESK-SPEC 8)
 *
 * The tree is walked once, at OPEN, with an explicit stack of PD_MAXDEPTH
 * frames; a bitmap over object numbers (one bit each, 8 KiB of the arena
 * claim, released when the walk ends) is the visited set, so a node reached
 * twice - a cycle, or a page shared by two parents - is PDE_CYCLE. The page
 * table in the tables claim is 8 bytes a page: object number, generation,
 * flags and a reserved word. Inherited MediaBox, CropBox, Rotate and Resources
 * are never stored: each is found by walking /Parent from the page, on demand.
 * ==========================================================================*/

static const pdb pw_bit[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
static pdw pw_mark;
static struct pd_u32 pw_ds;

static pdw pw_testset(pdw num)
{
    pdw off, b, m;
    off = (pdw)(pw_mark + (num >> 3));
    m = pw_bit[num & 7];
    b = pd_fpeekb(g_aseg, off);
    if (b & m) return 1;
    pd_fpokeb(g_aseg, off, (pdb)(b | m));
    return 0;
}

static void pw_begin(void)
{
    pw_mark = arena_top;
    arena_top = (pdw)(arena_top + PDE_VISBYTES);
    pd_ffill(g_aseg, pw_mark, 0, PDE_VISBYTES);
    pw_sp = 0; pw_visits = 0; npages = 0;
}
static void pw_end(void)
{
    arena_top = pw_mark;
}

/* PDF 7.7.3.2: a node's /Parent is the node it hangs from. A /Parent that is
 * not (a damaged writer) is tolerated unless following it loops - back to the
 * node itself, or on for more than PD_MAXDEPTH hops - which is PDE_CYCLE. The
 * lexer is just inside the node's dictionary and is moved. */
static pdw pw_parent(pdw num, pdw expect)
{
    pdw rc, pn, pg, hops;
    rc = ob_dict_find("Parent");
    if (rc == PDE_NOTFOUND) return PDE_OK;
    if (rc) return rc;
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind != PDK_REF || ov.rnum == expect) return PDE_OK;
    for (hops = 0; hops <= PD_MAXDEPTH; hops++) {
        pn = ov.rnum; pg = ov.rgen;
        if (pn == num) { err_obj = num; return PDE_CYCLE; }
        rc = ob_enter(pn, pg);
        if (rc) return rc;
        if (ob_null) return PDE_OK;
        rc = ob_value();
        if (rc) return rc;
        if (ov.kind != PDK_DICT) return PDE_OK;
        rc = ob_dict_find("Parent");
        if (rc == PDE_NOTFOUND) return PDE_OK;
        if (rc) return rc;
        rc = ob_value();
        if (rc) return rc;
        if (ov.kind != PDK_REF) return PDE_OK;
    }
    err_obj = num;
    return PDE_CYCLE;
}

/* Visit one node by reference. Pushes a frame for an intermediate node, adds a
 * page for a leaf. A free or absent object is skipped. */
static pdw pw_visit(pdw num, pdw gen, pdw parent_n)
{
    pdw rc;
    if (num == 0) return PDE_SYNTAX;
    if (pw_testset(num)) { err_obj = num; return PDE_CYCLE; }
    if (++pw_visits > (pdw)(PD_MAXPAGES * 2 + 32)) return PDE_LIMIT;
    rc = ob_enter(num, gen);
    if (rc) return rc;
    if (ob_null) return PDE_OK;
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind != PDK_DICT) { err_obj = num; return PDE_SYNTAX; }
    lx_tell(&pw_ds);
    rc = pw_parent(num, parent_n);
    if (rc) return rc;
    lx_seek(&pw_ds);
    rc = ob_dict_find("Kids");
    if (rc == PDE_NOTFOUND) {
        lx_seek(&pw_ds);                            /* an empty /Pages node is not a page */
        rc = ob_dict_find("Type");
        if (rc == PDE_OK) {
            rc = ob_value();
            if (rc) return rc;
            if (ov.kind == PDK_NAME && str_eq(ov.text, "Pages")) return PDE_OK;
            if (ov.kind == PDK_NAME && !str_eq(ov.text, "Page")) { err_obj = num; return PDE_SYNTAX; }
        } else if (rc != PDE_NOTFOUND) return rc;
        if (npages >= PD_MAXPAGES) return PDE_LIMIT;
        pd_fpokew(g_tseg, (pdw)(npages * 8), num);
        pd_fpokew(g_tseg, (pdw)(npages * 8 + 2), gen);
        pd_fpokew(g_tseg, (pdw)(npages * 8 + 4), 0);
        pd_fpokew(g_tseg, (pdw)(npages * 8 + 6), 0);
        npages++;
        return PDE_OK;
    }
    if (rc) return rc;
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind == PDK_REF) {
        rc = ob_follow();
        if (rc) return rc;
        if (ob_null) return PDE_OK;
    }
    if (ov.kind != PDK_ARR) { err_obj = num; return PDE_SYNTAX; }
    if (pw_sp >= PD_MAXDEPTH) { err_obj = num; return PDE_DEPTH; }
    pw_st[pw_sp].num = num; pw_st[pw_sp].gen = gen;
    lx_tell(&pw_st[pw_sp].kpos);
    pw_sp++;
    return PDE_OK;
}

/* One slice of the walk: PDE_BUSY = more to do. */
static pdw pw_step(void)
{
    pdw rc, budget = PDE_NODEBUDGET;
    struct pw_frame *f;
    while (budget--) {
        if (pw_sp == 0) return PDE_OK;
        f = &pw_st[pw_sp - 1];
        L = &fobj;
        lx_seek(&f->kpos);
        rc = ob_value();
        if (rc) { err_obj = f->num; return rc; }
        if (ov.kind == PDK_CLOSE) { pw_sp--; continue; }
        if (ov.kind == PDK_REF) {
            lx_tell(&f->kpos);
            rc = pw_visit(ov.rnum, ov.rgen, f->num);
            if (rc) return rc;
        } else if (ov.kind == PDK_NULL) {
            lx_tell(&f->kpos);
        } else { err_obj = f->num; return PDE_SYNTAX; }
    }
    return PDE_BUSY;
}

/* ---- on-demand inherited attributes -------------------------------------- */

static pdw pgn, pgg;                            /* pg_get's answer */
static pdw pg_get(pdw page)
{
    if (page >= npages) return PDE_RANGE;
    pgn = pd_fpeekw(g_tseg, (pdw)(page * 8));
    pgg = pd_fpeekw(g_tseg, (pdw)(page * 8 + 2));
    return PDE_OK;
}

/* Find `key` on the page or the nearest ancestor that has it. 0 = found, the
 * lexer at the value; PDE_NOTFOUND = no ancestor has it. */
static pdw pg_inherit(pdw num, pdw gen, const char *key)
{
    pdw rc, depth;
    static struct pd_u32 ds;
    for (depth = 0; depth <= PD_MAXDEPTH; depth++) {
        rc = ob_enter(num, gen);
        if (rc) return rc;
        if (ob_null) return PDE_NOTFOUND;
        rc = ob_value();
        if (rc) return rc;
        if (ov.kind != PDK_DICT) { err_obj = num; return PDE_SYNTAX; }
        lx_tell(&ds);
        rc = ob_dict_find(key);
        if (rc != PDE_NOTFOUND) return rc;
        lx_seek(&ds);
        rc = ob_dict_find("Parent");
        if (rc == PDE_NOTFOUND) return PDE_NOTFOUND;
        if (rc) return rc;
        rc = ob_value();
        if (rc) return rc;
        if (ov.kind != PDK_REF) return PDE_NOTFOUND;
        num = ov.rnum; gen = ov.rgen;
    }
    return PDE_DEPTH;
}

/* The lexer is just inside an array: 4 numbers into r (pt16), normalised. */
static pdw pg_rect(struct pd_rect *r)
{
    static struct pd_fx v[4];
    pdw rc, k;
    pd_pt t;
    pdm_ovf_clear();
    for (k = 0; k < 4; k++) {
        rc = ob_num(&v[k]);
        if (rc) return rc;
    }
    r->x0 = pdm_fx_to_pt16(&v[0]); r->y0 = pdm_fx_to_pt16(&v[1]);
    r->x1 = pdm_fx_to_pt16(&v[2]); r->y1 = pdm_fx_to_pt16(&v[3]);
    if (pdm_ovf()) return PDE_RANGE;
    if (r->x0 > r->x1) { t = r->x0; r->x0 = r->x1; r->x1 = t; }
    if (r->y0 > r->y1) { t = r->y0; r->y0 = r->y1; r->y1 = t; }
    return PDE_OK;
}

/* An inherited box. 0 = read, PDE_NOTFOUND = absent everywhere. */
static pdw pg_box(pdw num, pdw gen, const char *key, struct pd_rect *r)
{
    pdw rc;
    rc = pg_inherit(num, gen, key);
    if (rc) return rc;
    rc = ob_value();
    if (rc) return rc;
    rc = ob_follow();
    if (rc) return rc;
    if (ov.kind != PDK_ARR) { err_obj = num; return PDE_SYNTAX; }
    rc = pg_rect(r);
    if (rc) err_obj = num;
    return rc;
}

static pdw v_pageinfo(pdw page)
{
    static struct pdxb_pageinfo pi;
    pdw rc, num, gen, rot;
    static struct pd_fx rf;
    static struct pd_u32 ds;
    rc = pg_get(page);
    if (rc) return rc;
    num = pgn; gen = pgg;
    pi.objnum = num; pi.gen = gen; pi.flags = 0; pi.nannots = 0;
    rc = pg_box(num, gen, "MediaBox", &pi.media);
    if (rc == PDE_NOTFOUND) {                      /* PDF 8.4.7 default: Letter */
        pi.media.x0 = 0; pi.media.y0 = 0; pi.media.x1 = 612 * 16; pi.media.y1 = 792 * 16;
        rc = PDE_OK;
    }
    if (rc) return rc;
    if (pi.media.x1 == pi.media.x0 || pi.media.y1 == pi.media.y0) { err_obj = num; return PDE_RANGE; }
    rc = pg_box(num, gen, "CropBox", &pi.crop);
    if (rc == PDE_NOTFOUND) { pi.crop.x0 = pi.media.x0; pi.crop.y0 = pi.media.y0; pi.crop.x1 = pi.media.x1; pi.crop.y1 = pi.media.y1; rc = PDE_OK; }
    if (rc) return rc;
    pi.rotate = 0;
    rc = pg_inherit(num, gen, "Rotate");
    if (rc == PDE_OK) {
        rc = ob_num(&rf);
        if (rc) { err_obj = num; return rc; }
        if (rf.lo != 0 || (rf.hi % 90) != 0) { err_obj = num; return PDE_RANGE; }
        rot = (pdw)(((rf.hi % 360) + 360) % 360);
        pi.rotate = rot;
    } else if (rc != PDE_NOTFOUND) return rc;
    /* /Annots and /Contents are the page's own */
    rc = ob_enter(num, gen);
    if (rc) return rc;
    rc = ob_value();
    if (rc) return rc;
    lx_tell(&ds);
    pi.flags = PDP_NOCONTENT;
    rc = ob_dict_find("Contents");
    if (rc == PDE_OK) {
        rc = ob_value();
        if (rc) return rc;
        if (ov.kind != PDK_NULL) pi.flags = 0;
    } else if (rc != PDE_NOTFOUND) return rc;
    lx_seek(&ds);
    rc = ob_dict_find("Annots");
    if (rc == PDE_OK) {
        rc = ob_value();
        if (rc) return rc;
        if (ov.kind == PDK_REF) { rc = ob_follow(); if (rc) return rc; }
        if (ov.kind == PDK_ARR) {
            for (;;) {
                rc = ob_skipval();
                if (rc == PDE_SYNTAX && ov.kind == PDK_CLOSE) break;
                if (rc) return rc;
                if (++pi.nannots > PD_MAXITEMS) return PDE_LIMIT;
            }
        }
    } else if (rc != PDE_NOTFOUND) return rc;
    pd_fcopy_out(&pi, g_xb, 0, sizeof(struct pdxb_pageinfo));
    return PDE_OK;
}

/* page index of an object number, or 0xFFFF */
static pdw pg_index_of(pdw num)
{
    pdw i;
    for (i = 0; i < npages; i++)
        if (pd_fpeekw(g_tseg, (pdw)(i * 8)) == num) return i;
    return 0xFFFF;
}
