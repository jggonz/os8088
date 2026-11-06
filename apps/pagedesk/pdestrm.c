/* ============================================================================
 * pdestrm.c - streams, filters, TOKEN, extents (PAGEDESK-SPEC 7.5)
 *
 * Up to PDE_MAXCTX streams are open at once, as a stack: PDV_ENG_STRM_OPEN
 * pushes, STRM_CLOSE pops, STRM_READ and TOKEN address the top. That is the
 * shape a content interpreter needs for Form XObjects (a form is opened while
 * its caller's stream is still being read).
 * A stream ends where /Length says, checked against "endstream" at OPEN.
 * ==========================================================================*/

static struct pd_u32 ds_pos;

static int raw_get(struct sctx *c)
{
    pdw want, n;
    if (c->rbi >= c->rbn) {
        want = PDE_RB;
        if (c->rrem.hi == 0 && c->rrem.lo < want) want = c->rrem.lo;
        if (!want) return -1;
        n = fl_read(&c->rpos, c->rb, want);
        if (!n) return -1;
        u32_sub16(&c->rrem, n);
        c->rbn = n; c->rbi = 0;
    }
    return c->rb[c->rbi++];
}

/* One decoded byte from stage i (i < 0: the raw file bytes), or -1.
 * sget counts every byte EVERY stage produces against PDE_WORK_HI: the 1 MiB
 * output bound (st_account) is the top stage's, and a lower stage that only
 * feeds white space to an ASCIIHex above it - /Filter [/RL /RL /RL /AHx] -
 * would otherwise run 128^3 bytes of work a raw byte inside ONE call (PD-28). */
static int sget1(struct sctx *c, int i);
static int sget(struct sctx *c, int i)
{
    int b;
    pdw k;
    if (i < 0) return raw_get(c);
    b = sget1(c, i);
    if (b >= 0 && ++c->wlo == 0 && ++c->whi >= PDE_WORK_HI) {
        lx_err = PDE_LIMIT;
        for (k = 0; k < PD_MAXFILTERS; k++) c->fdone[k] = 1;
        return -1;
    }
    return b;
}
static int sget1(struct sctx *c, int i)
{
    int b, d, v, n;
    if (c->fdone[i]) return -1;
    if (c->flt[i] == PDF_K_FLATE) {                     /* pdeflate.c (20.13) */
        b = fl_get(c);
        if (b < 0) c->fdone[i] = 1;
        return b;
    }
    if (c->flt[i] == PDF_K_HEX) {
        do { b = sget(c, i - 1); } while (b >= 0 && is_ws(b));
        if (b < 0 || b == '>') { c->fdone[i] = 1; return -1; }
        if (hexv(b) < 0) { lx_err = PDE_SYNTAX; c->fdone[i] = 1; return -1; }
        do { d = sget(c, i - 1); } while (d >= 0 && is_ws(d));
        if (d < 0 || d == '>') { c->fdone[i] = 1; return hexv(b) << 4; }
        v = hexv(d);
        if (v < 0) { lx_err = PDE_SYNTAX; c->fdone[i] = 1; return -1; }
        return (hexv(b) << 4) | v;
    }
    /* run length */
    if (c->fcnt[i] > 0) {
        c->fcnt[i]--;
        b = sget(c, i - 1);
        if (b < 0) c->fdone[i] = 1;
        return b;
    }
    if (c->fcnt[i] < 0) { c->fcnt[i]++; return (int)c->fbyte[i]; }
    b = sget(c, i - 1);
    if (b < 0 || b == 128) { c->fdone[i] = 1; return -1; }
    d = sget(c, i - 1);
    if (d < 0) { c->fdone[i] = 1; return -1; }
    if (b < 128) { c->fcnt[i] = b; return d; }
    n = 257 - b;
    c->fbyte[i] = (pdw)d;
    c->fcnt[i] = -(n - 1);
    return d;
}

/* The expansion bound (PD-28): 1 MiB of decoded output a stream. */
static int st_account(struct sctx *c, pdw n)
{
    pdm_u32_add16(&c->outc, n);
    if (c->outc.hi >= PDE_OUTMAX_HI) { lx_err = PDE_LIMIT; return 1; }
    return 0;
}

/* Refill a stream's lookahead buffer. */
static pdw st_decode(struct sctx *c)
{
    pdw n = 0, want;
    int b;
    if (lx_err) return 0;
    if (c->nflt == 0) {
        want = PDE_IB;
        if (c->rrem.hi == 0 && c->rrem.lo < want) want = c->rrem.lo;
        if (!want) return 0;
        n = fl_read(&c->rpos, c->ib, want);
        u32_sub16(&c->rrem, n);
        if (n < want) lx_err = eof_err();
        return n;
    }
    while (n < PDE_IB) {
        b = sget(c, (int)c->nflt - 1);
        if (b < 0) break;
        c->ib[n++] = (pdb)b;
    }
    if (n && st_account(c, n)) return 0;
    return n;
}

/* ---- a stream object's header --------------------------------------------- */

static pdw flt_kind(const pdb *name)
{
    if (str_eq(name, "ASCIIHexDecode") || str_eq(name, "AHx")) return PDF_K_HEX;
    if (str_eq(name, "RunLengthDecode") || str_eq(name, "RL")) return PDF_K_RLE;
    if (str_eq(name, "FlateDecode") || str_eq(name, "Fl")) return g_nfl ? PDF_K_FLATE : 100 + PDE_FLATE;
    return 100 + PDE_FILTER;
}
static pdw flt_add(const pdb *name)
{
    pdw k = flt_kind(name);
    if (k > 100) return (pdw)(k - 100);
    if (sp_nflt >= PD_MAXFILTERS) return PDE_LIMIT;
    if (k == PDF_K_FLATE && (sp_filters & PDFL_FLATE)) return PDE_FILTER;   /* one Flate stage a chain */
    sp_flt[sp_nflt++] = (pdb)k;
    sp_filters |= (k == PDF_K_HEX) ? PDFL_HEX : (k == PDF_K_FLATE) ? PDFL_FLATE : PDFL_RLE;
    return PDE_OK;
}

/* The lexer is just inside the object's dictionary (ov.kind == PDK_DICT).
 * Fills sp_kind; for a stream also sp_dstart / sp_len (validated against the
 * file and the "endstream" keyword) and leaves the lexer after "endstream".
 * For a plain dictionary, tk holds the token that followed it. */
static pdw ob_stream_dict(pdw num, pdw gen)
{
    pdw rc;
    int c;
    lx_tell(&ds_pos);
    rc = ob_skipc();
    if (rc) return rc;
    rc = lx_token();
    if (rc) return rc;
    if (tk.kind != PDT_OP || !str_eq(tk.text, "stream")) { sp_kind = PDK_DICT; return PDE_OK; }
    sp_kind = PDK_STRM;
    c = lx_getc();
    if (c == 13) { if (lx_peek() == 10) L->i++; else { err_obj = num; return PDE_SYNTAX; } }
    else if (c != 10) { err_obj = num; return PDE_SYNTAX; }
    lx_tell(&sp_dstart);
    lx_seek(&ds_pos);
    rc = ob_dict_find("Length");
    if (rc == PDE_NOTFOUND) { err_obj = num; return PDE_SYNTAX; }
    if (rc) return rc;
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind == PDK_REF) { rc = ob_follow(); if (rc) return rc; }
    if (ov.kind != PDK_INT || ov.neg || ov.range) { err_obj = num; return PDE_SYNTAX; }
    u32_cp(&sp_len, &ov.iv);
    pdm_ovf_clear();
    pdm_u32_add(&s_a, &sp_dstart, &sp_len);
    if (pdm_ovf() || pdm_u32_cmp(&s_a, &fl_size) > 0) { err_obj = num; return PDE_TRUNC; }
    L = &fobj;
    lx_seek(&s_a);
    rc = lx_token();
    if (rc == PDE_IO) return rc;
    if (rc || tk.kind != PDT_OP || !str_eq(tk.text, "endstream")) { err_obj = num; return PDE_SYNTAX; }
    (void)gen;
    return PDE_OK;
}

/* The stream's /Filter chain into sp_flt (needs sp_kind == PDK_STRM). */
static pdw ob_filters1(pdw num);
static pdw ob_filters(pdw num)
{
    pdw rc = ob_filters1(num);
    if (!rc && (sp_filters & PDFL_FLATE)) rc = fl_parms(num);
    return rc;
}
static pdw ob_filters1(pdw num)
{
    pdw rc, rn, rg;
    int hops;
    sp_nflt = 0; sp_filters = 0;
    lx_seek(&ds_pos);
    rc = ob_dict_find("F");                           /* an external file stream */
    if (rc == PDE_OK) { err_obj = num; return PDE_FILTER; }
    if (rc != PDE_NOTFOUND) return rc;
    lx_seek(&ds_pos);
    rc = ob_dict_find("Filter");
    if (rc == PDE_NOTFOUND) return PDE_OK;
    if (rc) return rc;
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind == PDK_REF) {
        lx_tell(&on_pos);
        rc = ob_follow();
        if (rc) return rc;
        /* (the chain lives in that object: read it here, no need to come back) */
    }
    if (ov.kind == PDK_NAME) {
        rc = flt_add(ov.text);
        if (rc) { err_obj = num; return rc; }
        return PDE_OK;
    }
    if (ov.kind == PDK_NULL) return PDE_OK;
    if (ov.kind != PDK_ARR) { err_obj = num; return PDE_SYNTAX; }
    for (hops = 0; hops < 16; hops++) {
        rc = ob_value();
        if (rc) return rc;
        if (ov.kind == PDK_CLOSE) return PDE_OK;
        if (ov.kind == PDK_REF) {
            rn = ov.rnum; rg = ov.rgen;
            lx_tell(&on_pos);
            rc = ob_enter(rn, rg);
            if (!rc && !ob_null) rc = ob_value();
            if (rc) return rc;
            L = &fobj;
            lx_seek(&on_pos);
        }
        if (ov.kind != PDK_NAME) { err_obj = num; return PDE_SYNTAX; }
        rc = flt_add(ov.text);
        if (rc) { err_obj = num; return rc; }
    }
    err_obj = num;
    return PDE_LIMIT;
}

/* Object extents: sp_kind, sp_off, ex_len = bytes through "endobj". */
static struct pd_u32 ex_len;
static pdw ob_extent(pdw num, pdw gen)
{
    pdw rc;
    rc = ob_enter(num, gen);
    if (rc) return rc;
    if (ob_null) { sp_kind = PDK_NULL; u32_zero(&sp_off); u32_zero(&ex_len); return PDE_OK; }
    u32_cp(&sp_off, &ob_hdr);
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind == PDK_CLOSE || ov.kind == PDK_WORD || ov.kind == PDK_EOF) { err_obj = num; return PDE_SYNTAX; }
    sp_kind = ov.kind;
    if (ov.kind == PDK_DICT) {
        rc = ob_stream_dict(num, gen);
        if (rc) return rc;
        if (sp_kind == PDK_STRM) {
            rc = lx_token();
            if (rc) return rc;
        }
    } else {
        if (ov.kind == PDK_ARR) {
            rc = ob_skipc();
            if (rc) return rc;
        }
        rc = lx_token();
        if (rc) return rc;
    }
    if (tk.kind != PDT_OP || !str_eq(tk.text, "endobj")) { err_obj = num; return PDE_SYNTAX; }
    lx_tell(&s_c);
    pdm_u32_sub(&ex_len, &s_c, &sp_off);
    return PDE_OK;
}

/* ---- the open-stream stack -------------------------------------------------- */

static pdw st_push(pdw filtered)
{
    struct sctx *c;
    pdw i;
    if (nctx >= PDE_MAXCTX) return PDE_LIMIT;
    c = &ctxs[nctx];
    c->lx.buf = c->ib; c->lx.n = 0; c->lx.i = 0; c->lx.mode = 1; c->lx.gen = 0;
    c->lx.smode = 0; c->lx.sdepth = 0;
    u32_zero(&c->lx.fpos);
    u32_cp(&c->rpos, &sp_dstart);
    u32_cp(&c->rrem, &sp_len);
    u32_zero(&c->outc);
    c->nflt = filtered ? sp_nflt : 0;
    for (i = 0; i < PD_MAXFILTERS; i++) {
        c->flt[i] = sp_flt[i]; c->fcnt[i] = 0; c->fbyte[i] = 0; c->fdone[i] = 0;
    }
    c->rbn = 0; c->rbi = 0; c->depth = 0; c->wlo = 0; c->whi = 0;
    c->filters = sp_filters;
    fl_drop(c);
    if (c->nflt && (sp_filters & PDFL_FLATE)) {
        i = fl_attach(c);
        if (i) return i;
    }
    nctx++;
    return PDE_OK;
}

/* A page's /Contents element -> the stream's object number. */
static pdw pcn, pcg;                            /* pg_contents' answer */
static pdw pg_contents(pdw page, pdw index)
{
    pdw rc, num, gen, k;
    static struct pd_u32 ds;
    rc = pg_get(page);
    if (rc) return rc;
    num = pgn; gen = pgg;
    rc = ob_enter(num, gen);
    if (rc) return rc;
    if (ob_null) { err_obj = num; return PDE_SYNTAX; }
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind != PDK_DICT) { err_obj = num; return PDE_SYNTAX; }
    rc = ob_dict_find("Contents");
    if (rc == PDE_NOTFOUND) return PDE_RANGE;
    if (rc) return rc;
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind == PDK_NULL) return PDE_RANGE;
    if (ov.kind == PDK_REF) {
        pcn = ov.rnum; pcg = ov.rgen;
        rc = ob_follow();
        if (rc) return rc;
        if (ob_null) return PDE_RANGE;
        if (ov.kind == PDK_DICT) return index == 0 ? PDE_OK : PDE_RANGE;
        if (ov.kind != PDK_ARR) { err_obj = num; return PDE_SYNTAX; }
    } else if (ov.kind != PDK_ARR) { err_obj = num; return PDE_SYNTAX; }
    for (k = 0; k < index; k++) {
        rc = ob_value();
        if (rc) return rc;
        if (ov.kind == PDK_CLOSE) return PDE_RANGE;
        if (ov.kind == PDK_ARR || ov.kind == PDK_DICT) { rc = ob_skipc(); if (rc) return rc; }
    }
    (void)ds;
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind == PDK_CLOSE) return PDE_RANGE;
    if (ov.kind != PDK_REF) { err_obj = num; return PDE_SYNTAX; }
    pcn = ov.rnum; pcg = ov.rgen;
    return PDE_OK;
}

static pdw v_strm_open(void)
{
    static struct pdxb_strm sx;
    pdw rc, num, gen;
    pd_fcopy_in(g_xb, 0, &sx, sizeof(struct pdxb_strm));
    num = sx.objnum; gen = sx.gen;
    if (num == 0) {
        rc = pg_contents(sx.page, sx.index);
        if (rc) return rc;
        num = pcn; gen = pcg;
    }
    rc = ob_enter(num, gen);
    if (rc) return rc;
    if (ob_null) return PDE_NOTFOUND;
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind != PDK_DICT) { err_obj = num; return PDE_SYNTAX; }
    rc = ob_stream_dict(num, gen);
    if (rc) return rc;
    if (sp_kind != PDK_STRM) { err_obj = num; return PDE_SYNTAX; }
    rc = ob_filters(num);
    if (rc) return rc;
    rc = st_push(1);
    if (rc) return rc;
    if (sp_nflt == 0) u32_cp(&sx.declen, &sp_len);
    else { sx.declen.lo = 0xFFFF; sx.declen.hi = 0xFFFF; }
    sx.filters = sp_filters;
    pd_fcopy_out(&sx, g_xb, 0, sizeof(struct pdxb_strm));
    return PDE_OK;
}

static pdw v_strm_close(void)
{
    if (nctx == 0) return PDE_RANGE;
    nctx--;
    return PDE_OK;
}

static pdw v_strm_read(pdw dseg, pdw doff, pdw dmax)
{
    struct sctx *c;
    pdw got = 0, k, want;
    int b;
    if (nctx == 0) return PDE_RANGE;
    if (dmax > 0xFFF0) dmax = 0xFFF0;
    if ((pdw)(doff + dmax) < doff && dmax) return PDE_RANGE;
    c = &ctxs[nctx - 1];
    lx_err = 0; pde_ioerr = 0;
    k = (pdw)(c->lx.n - c->lx.i);
    if (c->lx.n > c->lx.i) {
        if (k > dmax) k = dmax;
        if (k && !pd_fcopy_out(c->ib + c->lx.i, dseg, doff, k)) return PDE_RANGE;
        c->lx.i = (pdw)(c->lx.i + k);
        got = k;
    }
    while (got < dmax) {
        want = (pdw)(dmax - got);
        if (c->nflt == 0) {
            if (c->rrem.hi == 0 && c->rrem.lo < want) want = c->rrem.lo;
            if (!want) break;
            k = fl_read_far(&c->rpos, dseg, (pdw)(doff + got), want);
            u32_sub16(&c->rrem, k);
            got = (pdw)(got + k);
            if (k < want) { if (pde_ioerr) return PDE_IO; break; }
        } else {
            if (want > 128) want = 128;
            k = 0;
            while (k < want) {
                b = sget(c, (int)c->nflt - 1);
                if (b < 0) break;
                tmpb[k++] = (pdb)b;
            }
            if (k) {
                if (st_account(c, k)) return lx_err;
                if (!pd_fcopy_out(tmpb, dseg, (pdw)(doff + got), k)) return PDE_RANGE;
                got = (pdw)(got + k);
            }
            if (k < want) break;
        }
    }
    if (lx_err) return lx_err;
    if (pde_ioerr) return PDE_IO;
    pde_ax = got;
    return PDE_OK;
}

static pdw v_token(void)
{
    static struct pdxb_token xt;
    struct sctx *c;
    pdw rc, k, ret;
    int d;
    if (nctx == 0) return PDE_RANGE;
    c = &ctxs[nctx - 1];
    L = &c->lx;
    lx_err = 0; pde_ioerr = 0;
    rc = lx_token();
    ret = rc;
    if (!rc && tk.kind == PDT_EOF && lx_err) ret = lx_err;
    xt.kind = rc ? PDT_EOF : tk.kind;
    xt.num.lo = 0; xt.num.hi = 0; xt.len = 0; xt.more = 0; xt.text[0] = 0;
    if (!rc) {
        if (tk.kind == PDT_NUM) { xt.num.lo = tk.fx.lo; xt.num.hi = tk.fx.hi; if (tk.range || tk.fxr) ret = PDE_RANGE; }
        xt.len = tk.len; xt.more = tk.more;
        for (k = 0; k <= tk.len && k < 256; k++) xt.text[k] = (char)tk.text[k];
        if (tk.kind == PDT_ARRB || tk.kind == PDT_DICTB) {
            if (++c->depth > PD_MAXDEPTH) ret = PDE_DEPTH;
        } else if ((tk.kind == PDT_ARRE || tk.kind == PDT_DICTE) && c->depth) c->depth--;
        else if (tk.kind == PDT_OP && tk.len == 2 && tk.text[0] == 'I' && tk.text[1] == 'D') {
            d = lx_peek();
            if (d >= 0 && is_ws(d)) L->i++;           /* the single white space; binary data follows */
        }
    }
    L = &fobj;
    pd_fcopy_out(&xt, g_xb, 0, sizeof(struct pdxb_token));
    return ret;
}
