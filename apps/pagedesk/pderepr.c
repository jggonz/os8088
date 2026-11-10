/* ============================================================================
 * pderepr.c - a damaged file's cross-reference, rebuilt by a scan
 * (PAGEDESK-SPEC 20.22.3.5, AP-02)
 *
 * OPEN failed in its cross-reference with PDE_XREF, PDE_TRUNC, PDE_XREFSTM or
 * a missing root: the chain is thrown away and the whole file is walked as
 * tokens instead. "n g obj" is an object at the offset of n, a later one
 * replacing an earlier one (an incremental update's rule); an object stream's
 * members are entered where the stream stands in the file, so the file's
 * order decides between a member and a direct object of the same number; the
 * last trailer (a "trailer" dictionary or a /Type /XRef stream's) names the
 * root, and the last /Type /Catalog stands in when none does. Pass 1 finds
 * the highest object number so the flat index can be asked for; pass 2 fills
 * it. A slice walks PDE_SCANBYTES of the file; a stream's data is skipped by
 * its /Length when that checks out, else by looking for "endstream".
 * ==========================================================================*/

static struct pd_u32 rp_pos, rp_beg, rp_t, rp_d, rp_e;  /* the scan; the slice's start; scratch */
static struct pd_u32 rp_p1, rp_p2;      /* where the last two integers began          */
static pdw rp_n1, rp_n2, rp_nn;         /* the last two integers, how many in a row   */
static pdw rp_max, rp_pass, rp_on;      /* pass 1's answer; the pass; repairing at all */
static pdw rp_cat_n, rp_cat_g;          /* the last /Type /Catalog                    */

/* Every byte to "endstream" (a stream whose /Length does not check out). */
static void rp_skipdata(void)
{
    static const char es[] = "endstream";
    pdw k = 0;
    int c;
    for (;;) {
        c = lx_getc();
        if (c < 0) return;
        if (c == es[k]) { if (++k == 9) return; }
        else k = (pdw)(c == 'e');
    }
}

/* A trailer's keys, the lexer just inside its dictionary: the last one in
 * the file wins, so what an earlier one said is forgotten first. */
static void rp_trailer(void)
{
    tr_has_root = 0; tr_info_n = 0; tr_hasid = 0; tr_enc_k = 0; tr_nodict = 0;
    lx_tell(&tr_dict);
    (void)tr_keys(1);
}

/* An object stream's members (the lexer just inside its dictionary): pass 1
 * counts their numbers into rp_max, pass 2 enters them. The lexer is left
 * after "endstream". */
static pdw rp_members(pdw stm)
{
    pdw rc, k;
    struct osgeo *g;
    rc = og_here(stm);
    if (rc) return rc;
    lx_tell(&rp_e);                             /* after "endstream" */
    if (!g_oseg) { pde_needmem = PDMEM_OSW; pde_needkb = PDE_OSW_KB; return PDE_NEEDMEM; }
    g = &og[og_i];
    os_pos.hi = (pdw)(0x8000 | stm); os_pos.lo = 0;
    lx_seek(&os_pos);
    for (k = 0; k < g->n && !os_pair(); k++) {
        if (rp_pass == 1) { if (os_pn > rp_max) rp_max = os_pn; }
        else { ix_put(os_pn, k, (pdw)(0x8000 | stm), 1); doc_flags |= PDO_OBJSTM; }
    }
    lx_err = 0;                                 /* a short or broken stream: what it held is in */
    L = &fobj;
    lx_seek(&rp_e);
    return PDE_OK;
}

/* "n g obj" just read, n at rp_p2: the object, and what kind of thing it is. */
static pdw rp_obj(pdw num)
{
    pdw rc, t = 0;
    if (rp_pass == 1) { if (num > rp_max) rp_max = num; }
    else ix_put(num, rp_p2.lo, rp_p2.hi, 1);
    rc = ob_value();
    if (rc || ov.kind != PDK_DICT) return rc == PDE_IO ? rc : PDE_OK;
    lx_tell(&rp_d);
    if (ob_dict_find("Type") == PDE_OK && ob_value() == PDE_OK && ov.kind == PDK_NAME)
        t = str_eq(ov.text, "ObjStm") ? 1 : str_eq(ov.text, "XRef") ? 2 : str_eq(ov.text, "Catalog") ? 3 : 0;
    lx_seek(&rp_d);
    if (t == 3) { rp_cat_n = num; rp_cat_g = rp_n1; }
    if (t == 2 && rp_pass == 1) { rp_trailer(); lx_seek(&rp_d); }
    if (t == 1 && num <= PDE_OSMAX) {
        rc = rp_members(num);
        if (rc == PDE_NEEDMEM || rc == PDE_IO) return rc;
        if (!rc) return PDE_OK;
        L = &fobj;
        lx_seek(&rp_d);
    }
    /* the dictionary's end; a stream after it is skipped by its /Length */
    if (ob_skipc()) return PDE_OK;
    lx_tell(&rp_e);
    if (lx_token() || tk.kind != PDT_OP || !str_eq(tk.text, "stream")) { lx_seek(&rp_e); return PDE_OK; }
    lx_seek(&rp_d);
    rc = ob_stream_dict(num, rp_n1);
    if (rc == PDE_IO) return rc;
    if (rc || sp_kind != PDK_STRM) lx_seek(&rp_e);     /* "stream" again, and the byte scan */
    return PDE_OK;
}

/* One slice of pass rp_pass. PDE_OK at the end of the file, PDE_BUSY when the
 * slice is spent (rp_pos then names the next token), PDE_NEEDMEM / PDE_IO. */
static pdw rp_scan(void)
{
    pdw rc;
    L = &fobj;
    lx_seek(&rp_pos);
    u32_cp(&rp_beg, &rp_pos);
    for (;;) {
        lx_skipws();                                    /* rp_t: where the token begins */
        lx_tell(&rp_t);
        pdm_u32_sub(&s_a, &rp_t, &rp_beg);
        if (s_a.hi || s_a.lo >= PDE_SCANBYTES) { u32_cp(&rp_pos, &rp_t); return PDE_BUSY; }
        lx_err = 0;
        rc = lx_token();
        if (rc == PDE_IO || pde_ioerr) return PDE_IO;
        if (rc) {                                       /* not a token: on from where it failed */
            lx_tell(&rp_e);
            if (!pdm_u32_cmp(&rp_e, &rp_t)) pdm_u32_add16(&rp_e, 1);
            lx_seek(&rp_e);
            rp_nn = 0;
            continue;
        }
        if (tk.kind == PDT_EOF) return PDE_OK;
        if (tk.kind == PDT_NUM && !tk.neg && !tk.real) {
            rp_n2 = rp_n1; u32_cp(&rp_p2, &rp_p1);
            rp_n1 = tk.iv.hi ? 0xFFFF : tk.iv.lo;
            u32_cp(&rp_p1, &rp_t);
            rp_nn++;
            continue;
        }
        if (tk.kind == PDT_OP) {
            if (str_eq(tk.text, "obj") && rp_nn >= 2 && rp_n2 && rp_n2 != 0xFFFF) {
                u32_cp(&rp_pos, &rp_p2);                /* a NEEDMEM resumes at this object */
                rc = rp_obj(rp_n2);
                if (rc) return rc;
            } else if (str_eq(tk.text, "trailer")) {
                lx_tell(&rp_d);
                if (rp_pass == 1 && !lx_token() && tk.kind == PDT_DICTB) rp_trailer();
                lx_seek(&rp_d);
            } else if (str_eq(tk.text, "stream")) rp_skipdata();
        }
        rp_nn = 0;
    }
}

/* OPEN failed with rc: is it a cross-reference a scan can rebuild? Then the
 * document is set back to a file and nothing else, and the scan begins. */
static pdw rp_start(pdw rc)
{
    pdw k;
    if (rp_on || (op_state == 0 && err_detail != PDX_NOSTART && rc != PDE_XREF)) return 0;
    if (rc != PDE_XREF && rc != PDE_TRUNC && rc != PDE_XREFSTM && !(rc == PDE_SYNTAX && err_detail == PDX_NOROOT))
        return 0;
    if (!g_filed) return 0;
    if (pw_sp || npages) pw_end();
    pw_sp = 0; npages = 0;
    nctx = 0; os_reset();
    nrev = 0; nxs = 0; ix_on = 0; ix_n = 0; fd_on = 0;
    tr_size = 0; tr_maxsize = 0; tr_has_root = 0; tr_info_n = 0; tr_hasid = 0; tr_enc_k = 0;
    tr_tflags = 0; tr_nodict = 1;
    doc_flags = PDO_REPAIRED;
    for (k = 0; k < PDE_OFFCACHE * PDE_OCBYTES; k += 2) pd_fpokew(g_tseg, (pdw)(PDE_TBL_CACHE + k), 0xFFFF);
    u32_zero(&rp_pos); rp_nn = 0; rp_max = 0; rp_cat_n = 0; rp_pass = 1; rp_on = 1;
    err_obj = 0; err_detail = 0;
    op_state = OPS_SCAN1;
    return 1;
}

/* OPEN's repair states (20.22.3.4): 8 pass 1, 9 the index, 10 pass 2. */
static pdw op_repair(void)
{
    pdw rc;
    if (op_state == OPS_SCAN1) {
        rc = rp_scan();
        if (rc) return rc;
        op_state = OPS_SCANIX;
    }
    if (op_state == OPS_SCANIX) {
        rc = ix_ready(rp_max < 0xFFFF ? (pdw)(rp_max + 1) : 0xFFFF);
        if (rc) return rc;
        u32_zero(&rp_pos); rp_nn = 0; rp_pass = 2;
        op_state = OPS_SCAN2;
        return PDE_BUSY;
    }
    rc = rp_scan();
    if (rc) return rc;
    if (!tr_has_root && rp_cat_n) { tr_root_n = rp_cat_n; tr_root_g = rp_cat_g; tr_has_root = 1; }
    return PDE_OK;
}
