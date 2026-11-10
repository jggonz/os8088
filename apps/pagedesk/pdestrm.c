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

/* ---- the filter chain, without recursion (D21) ------------------------------
 * Stage i decodes the bytes of stage i - 1 (stage -1 is the raw file). A stage
 * is a push machine, st_step: offered nothing (SG_ASK) it produces a byte from
 * what it holds or answers SG_NEED; fed a byte of the stage below (or -1, that
 * stage's end) it produces a byte, SG_NEED or -1. sg_run walks the stages
 * lo..top with one index: down while a stage needs input, up while one has a
 * byte, so the chain costs no stack per stage.
 * The one Flate stage cannot be a push machine (it pulls bits in the middle of
 * a header), so it is the BOTTOM source of the stages above it, and what is
 * below it is reached through sget_lo, which contains no Flate stage: the call
 * graph is sget -> sg_flate -> fl_get -> ... -> fl_infill -> sget_lo, a chain
 * and not a cycle.
 * Every byte EVERY stage produces counts against PDE_WORK_HI: the 1 MiB output
 * bound (st_account) is the top stage's, and a lower stage that only feeds
 * white space to an ASCIIHex above it - /Filter [/RL /RL /RL /AHx] - would
 * otherwise run 128^3 bytes of work a raw byte inside ONE call (PD-28). */
#define SG_ASK  (-2)
#define SG_NEED (-3)

/* n bytes some stage produced: 1 when that is over the work bound, and then
 * every stage has ended with PDE_LIMIT. */
static int sg_add(struct sctx *c, pdw n)
{
    pdw k = c->wlo;
    c->wlo = (pdw)(k + n);
    if (c->wlo < k && ++c->whi >= PDE_WORK_HI) {
        lx_err = PDE_LIMIT;
        for (k = 0; k < PD_MAXFILTERS; k++) c->fdone[k] = 1;
        return 1;
    }
    return 0;
}

/* ASCIIHex: fcnt 1 while the first digit of a pair (fbyte) waits for its second.
 * Run length: fcnt > 0 literal bytes still to copy, fcnt < 0 repeats of fbyte
 * still to give, fcnt 0 a length byte to read - fbyte bit 8 set when it has been
 * read (its low byte) and the byte after it is wanted. */
/* ASCII85 (7.4.3, 20.22.3.6): fcnt 0..4 digits held, the group in fbyte (low
 * word) and fhi; fcnt -1..-4 bytes of the group still to give, the next in
 * fhi's high byte; -9..-12 the same with the data ended after them, -8 ended. */
static int st_a85(struct sctx *c, int i, int in)
{
    int *n = c->fcnt + i, d = 0, m = 0;
    pdw *lo = c->fbyte + i, *hi = c->fhi + i;
    if (*n >= 0) {
        if (in == SG_ASK || (in >= 0 && is_ws(in))) return SG_NEED;
        if (in == 'z' && *n == 0) { *lo = 0; *hi = 0; *n = -4; }
        else {
            if (in < 0 || in == '~') {                  /* the end: a last group of 2..4 */
                if (*n == 0) { *n = -8; return -1; }
                if (*n == 1) { lx_err = PDE_SYNTAX; return -1; }
                m = *n - 1;                             /* the bytes it gives; padded with 'u' */
                d = 'u';
            } else if (in < '!' || in > 'u') { lx_err = PDE_SYNTAX; return -1; }
            else d = in;
            do {
                pdm_ovf_clear();
                pdm_u32_mul16(&m_a, *lo, 85);
                pdm_u32_mul16(&m_b, *hi, 85);
                pdm_u32_set(&m_c, m_b.lo, 0);
                pdm_u32_add(&m_a, &m_a, &m_c);
                pdm_u32_add16(&m_a, (pdw)(d - '!'));
                if (m_b.hi || pdm_ovf()) { lx_err = PDE_SYNTAX; return -1; }   /* over 2^32 - 1 */
                *lo = m_a.lo; *hi = m_a.hi;
            } while (++*n < 5 && m);
            if (*n < 5) return SG_NEED;
            *n = m ? -8 - m : -4;
        }
    }
    if (*n == -8) return -1;
    d = (int)(*hi >> 8);                                /* the group's next byte, big-endian */
    *hi = (pdw)((*hi << 8) | (*lo >> 8));
    *lo = (pdw)(*lo << 8);
    (*n)++;
    return d;
}

static int st_step(struct sctx *c, int i, int in)
{
    int v, *n = c->fcnt + i;
    pdw *fb = c->fbyte + i;
    if (c->flt[i] == PDF_K_A85) return st_a85(c, i, in);
    if (c->flt[i] == PDF_K_HEX) {
        if (in == SG_ASK || (in >= 0 && is_ws(in))) return SG_NEED;
        if (in < 0 || in == '>') {
            if (*n) { *n = 0; c->fdone[i] = 1; return (int)*fb << 4; }   /* an odd last digit */
            return -1;
        }
        v = hexv(in);
        if (v < 0) { lx_err = PDE_SYNTAX; return -1; }
        if (!*n) { *fb = (pdw)v; *n = 1; return SG_NEED; }
        *n = 0;
        return ((int)*fb << 4) | v;
    }
    /* run length */
    v = *n;
    if (v < 0) { *n = v + 1; return (int)*fb; }
    if (in == SG_ASK) return SG_NEED;
    if (in < 0) return -1;
    if (v > 0) { *n = v - 1; return in; }
    v = (int)*fb;
    if (!(v & 0x100)) {                                 /* the length byte */
        if (in == 128) return -1;
        *fb = (pdw)(0x100 | in);
        return SG_NEED;
    }
    v &= 255;                                           /* the byte after it */
    *fb = (pdw)in;
    *n = v < 128 ? v : v - 256;                         /* v + 1 copies, or 257 - v repeats: this one and 256 - v more */
    return in;
}

/* Stages lo..top. in = SG_ASK: a byte of stage top, -1 at its end, or SG_NEED
 * when stage lo wants a byte of the stage below it; call again with that byte
 * (or -1) as in. Every stage above lo is then waiting on the one below it. A
 * stage that has ended is asked nothing more; one that asked for input is fed
 * whatever comes, including the end. */
static int sg_run(struct sctx *c, int lo, int top, int in)
{
    int i = in == SG_ASK ? top : lo, r;
    for (;;) {
        r = (in == SG_ASK && c->fdone[i]) ? -1 : st_step(c, i, in);
        if (r == SG_NEED) {
            if (i == lo) return SG_NEED;
            i--;
            in = SG_ASK;
            continue;
        }
        if (r == -1) c->fdone[i] = 1;          /* a stage that ends stays ended */
        else if (r >= 0 && sg_add(c, 1)) r = -1;
        if (i == top) return r;
        i++;
        in = r;
    }
}

/* One byte of stage top of a chain with no Flate stage at or below it. */
static int sget_lo(struct sctx *c, int top)
{
    int r = SG_ASK;
    if (top < 0) return raw_get(c);
    for (;;) {
        r = sg_run(c, 0, top, r);
        if (r != SG_NEED) return r;
        r = raw_get(c);
    }
}

/* One byte of the Flate stage k (pdeflate.c, 20.13), counted like any stage's. */
static int sg_flate(struct sctx *c, int k)
{
    int b;
    if (c->fdone[k]) return -1;
    b = fl_get(c);
    if (b < 0) c->fdone[k] = 1;
    else if (sg_add(c, 1)) b = -1;
    return b;
}

/* One decoded byte from stage top (top < 0: the raw file bytes), or -1. */
static int sget(struct sctx *c, int top)
{
    int k = top, r = SG_ASK;
    if (top < 0) return raw_get(c);
    if (c->filters & (PDFL_FLATE | PDFL_LZW))          /* the one slot stage: Flate or LZW */
        while (k >= 0 && !FLK_SLOT(c->flt[k])) k--;
    else k = -1;
    if (k < 0) return sget_lo(c, top);
    if (k == top) return sg_flate(c, k);
    for (;;) {
        r = sg_run(c, k + 1, top, r);
        if (r != SG_NEED) return r;
        r = sg_flate(c, k);
    }
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
        if (FLK_SLOT(c->flt[c->nflt - 1])) n = (pdw)(n + fl_take(c, c->ib + n, (pdw)(PDE_IB - n)));
    }
    if (n && st_account(c, n)) return 0;
    return n;
}

/* ---- a stream object's header --------------------------------------------- */

/* A filter's name to its kind (20.22.3.6): 0 unknown; PDF_K_CODEC + PDCD_*
 * an image codec, which the chain may stop before */
static const pdb flt_kinds[16] = {
    PDF_K_HEX, PDF_K_HEX, PDF_K_RLE, PDF_K_RLE, PDF_K_FLATE, PDF_K_FLATE, PDF_K_LZW, PDF_K_LZW,
    PDF_K_A85, PDF_K_A85, PDF_K_CODEC + PDCD_DCT, PDF_K_CODEC + PDCD_DCT, PDF_K_CODEC + PDCD_CCITT,
    PDF_K_CODEC + PDCD_CCITT, PDF_K_CODEC + PDCD_JBIG2, PDF_K_CODEC + PDCD_JPX };
static const pdb flt_bits[6] = { 0, PDFL_HEX, PDFL_RLE, PDFL_FLATE, PDFL_LZW, PDFL_A85 };
static pdw flt_kind(const pdb *name)
{
    pdw k = kw_ix(name, "ASCIIHexDecode\0AHx\0RunLengthDecode\0RL\0FlateDecode\0Fl\0LZWDecode\0LZW\0"
                        "ASCII85Decode\0A85\0DCTDecode\0DCT\0CCITTFaxDecode\0CCF\0JBIG2Decode\0JPXDecode\0");
    return k ? flt_kinds[k - 1] : 0;
}
static pdw flt_bit(pdw k) { return k < 6 ? flt_bits[k] : 0; }

/* One /Filter name onto sp_flt. A codec ends the chain (the caller reads its
 * coded bytes) when sp_accept says the caller takes them. */
static pdw sp_accept, sp_codec;
static pdw flt_add(const pdb *name)
{
    pdw k = flt_kind(name);
    if (!k || sp_codec) return PDE_FILTER;              /* unknown, or a stage after a codec */
    if (k >= PDF_K_CODEC) {
        if (!(sp_accept & PDSA_CODEC)) return PDE_FILTER;
        sp_codec = (pdw)(k - PDF_K_CODEC);
        return PDE_OK;
    }
    if (FLK_SLOT(k)) {
        if (!g_nfl && !flt_osw) return PDE_FLATE;       /* no slot on this machine */
        if (sp_filters & (PDFL_FLATE | PDFL_LZW)) return PDE_FILTER;   /* one slot stage a chain */
    }
    if (sp_nflt >= PD_MAXFILTERS) return PDE_LIMIT;
    sp_flt[sp_nflt++] = (pdb)k;
    sp_filters |= flt_bit(k);
    return PDE_OK;
}

/* "stream" just read: its end of line, and where the data starts into *ds */
static pdw st_eol(struct pd_u32 *ds)
{
    int c = lx_getc();
    if (c == 13 && lx_peek() == 10) c = lx_getc();
    if (c != 10) return PDE_SYNTAX;
    lx_tell(ds);
    return PDE_OK;
}
/* The data at *ds for *len ends inside the file, at "endstream" */
static pdw st_end(const struct pd_u32 *ds, const struct pd_u32 *len)
{
    pdw rc;
    pdm_ovf_clear();
    pdm_u32_add(&s_a, ds, len);
    if (pdm_ovf() || pdm_u32_cmp(&s_a, &fl_size) > 0) return PDE_TRUNC;
    L = &fobj;
    lx_seek(&s_a);
    rc = lx_token();
    if (rc == PDE_IO) return rc;
    if (rc || tk.kind != PDT_OP || !str_eq(tk.text, "endstream")) return PDE_SYNTAX;
    return PDE_OK;
}

/* The lexer is just inside the object's dictionary (ov.kind == PDK_DICT).
 * Fills sp_kind; for a stream also sp_dstart / sp_len (validated against the
 * file and the "endstream" keyword) and leaves the lexer after "endstream".
 * For a plain dictionary, tk holds the token that followed it. */
static pdw ob_stream_dict(pdw num, pdw gen)
{
    pdw rc;
    lx_tell(&ds_pos);
    rc = ob_skipc();
    if (rc) return rc;
    rc = lx_token();
    if (rc) return rc;
    if (tk.kind != PDT_OP || !str_eq(tk.text, "stream")) { sp_kind = PDK_DICT; return PDE_OK; }
    sp_kind = PDK_STRM;
    rc = st_eol(&sp_dstart);
    if (rc) goto bad;
    lx_seek(&ds_pos);
    rc = ob_dict_find("Length");
    if (rc == PDE_NOTFOUND) { rc = PDE_SYNTAX; goto bad; }
    if (rc) return rc;
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind == PDK_REF) { rc = ob_follow(); if (rc) return rc; }
    if (ov.kind != PDK_INT || ov.neg || ov.range) { rc = PDE_SYNTAX; goto bad; }
    u32_cp(&sp_len, &ov.iv);
    rc = st_end(&sp_dstart, &sp_len);
    if (rc == PDE_IO) return rc;
    (void)gen;
bad:
    if (rc) err_obj = num;
    return rc;
}

/* The stream's /Filter chain into sp_flt (needs sp_kind == PDK_STRM). */
static pdw ob_filters1(pdw num);
static pdw ob_filters(pdw num)
{
    pdw rc = ob_filters1(num);
    if (!rc && (sp_filters & (PDFL_FLATE | PDFL_LZW))) rc = fl_parms(num);
    return rc;
}
static pdw ob_filters1(pdw num)
{
    pdw rc, rn, rg;
    int hops;
    sp_nflt = 0; sp_filters = 0; sp_codec = 0;
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
    if (ob_hdr.hi & 0x8000) {                   /* a member: its value's bytes (20.22.3.3) */
        if (ov.kind == PDK_ARR || ov.kind == PDK_DICT) {
            rc = ob_skipc();
            if (rc) return rc;
        }
        lx_tell(&s_c);
        pdm_u32_sub(&ex_len, &s_c, &sp_off);
        return PDE_OK;
    }
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

/* A stream context, fresh: the raw bytes at ds for len, the chain flt. */
static void nz(void *p, pdw n)
{
    pdb *q = (pdb *)p;
    while (n--) *q++ = 0;
}
static void ctx_init(struct sctx *c, const struct pd_u32 *ds, const struct pd_u32 *len,
                     pdw nflt, const pdb *flt, pdw filters)
{
    pdw i;
    nz(&c->lx, sizeof(struct lxs));             /* all but the lookahead buffer, zero */
    nz(&c->rpos, (pdw)((pdb *)(c + 1) - (pdb *)&c->rpos));
    c->lx.buf = c->ib; c->lx.mode = 1;
    u32_cp(&c->rpos, ds);
    u32_cp(&c->rrem, len);
    c->nflt = nflt;
    for (i = 0; i < PD_MAXFILTERS; i++) c->flt[i] = flt[i];
    c->filters = filters;
}

static pdw st_push(pdw filtered, pdw num, pdw gen)
{
    struct sctx *c;
    pdw i;
    if (nctx >= PDE_MAXCTX) return PDE_LIMIT;
    c = &ctxs[nctx];
    ctx_init(c, &sp_dstart, &sp_len, filtered ? sp_nflt : 0, sp_flt, sp_filters);
    c->onum = num; c->ogen = gen;               /* crypt's key (20.22.5) */
    fl_drop(c);
    if (c->nflt && (sp_filters & (PDFL_FLATE | PDFL_LZW))) {
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
    sp_accept = sx.accept;                      /* the codec pass-through (20.22.3.6) */
    rc = ob_filters(num);
    sp_accept = 0;
    if (rc) return rc;
    rc = st_push(1, num, gen);
    if (rc) return rc;
    if (sp_nflt == 0) u32_cp(&sx.declen, &sp_len);
    else { sx.declen.lo = 0xFFFF; sx.declen.hi = 0xFFFF; }
    sx.filters = sp_filters;
    sx.codec = sp_codec;
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
                if (FLK_SLOT(c->flt[c->nflt - 1])) k = (pdw)(k + fl_take(c, tmpb + k, (pdw)(want - k)));
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
