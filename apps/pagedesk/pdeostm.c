/* ============================================================================
 * pdeostm.c - object streams, read in place (PAGEDESK-SPEC 20.22.3.3)
 *
 * A member of an object stream is read through the same funnel as the file:
 * a position with bit 31 set names its container (hi = 0x8000 | the stream's
 * object number, lo = the offset in its decoded bytes), and lx_fill hands
 * such a position to os_copy instead of the block cache. So every parser
 * routine that saves a position, follows a reference and seeks back works
 * across a hop into or out of an object stream, unchanged.
 *
 * The decoded bytes are the OBJECT-STREAM WINDOW's: the PDMEM_OSW claim is a
 * Flate slot's shape (pdeflate.h), used as slot FL_OSLOT by the window's own
 * stream context `osc`, which is never on the open-stream stack. Its 32 KiB
 * ring is Flate's history, so it holds the stream's last decoded bytes: a
 * read ahead of them decodes on, a read behind them decodes again from the
 * start. Re-decoding happens in the middle of a token, so it parses nothing:
 * a stream's geometry is read once, at a safe point (os_geo, from ob_enter),
 * into one of PDE_OSGEO records. Nothing here calls ob_enter, ob_follow or
 * ob_num - the call graph stays a tree (D21) and the parser's saved
 * positions (on_pos, ds_pos, sp_*) are never disturbed.
 * ==========================================================================*/

static struct sctx osc;                 /* the window's stream                   */
static struct osgeo og[PDE_OSGEO];
static pdw og_next, og_i;               /* the record reused next; os_geo's answer */
static pdw ow_g = 0xFFFF;               /* the record whose stream is in the window */
static pdw ow_out, ow_end;              /* decoded bytes delivered; the stream ended */
static struct pd_u32 os_pos;            /* os_find's answer                      */

/* The window's slot is given back (OPEN's xref stream borrowed it, or the
 * document is closing). */
static void os_release(void)
{
    if (fl_sc[FL_OSLOT]) { fl_sc[FL_OSLOT] = 0; if (fl_ld == FL_OSLOT) fl_ld = FL_NONE; }
    ow_g = 0xFFFF;
}
static void os_reset(void)
{
    pdw i;
    os_release();
    for (i = 0; i < PDE_OSGEO; i++) og[i].stm = 0;
    og_next = 0;
}

/* ---- geometry, at a safe point -------------------------------------------- */

/* Object stream `stm`'s geometry into a record (og_i); the lexer is just
 * inside its dictionary. Reads /Type /ObjStm, /N, /First, /Length (direct,
 * or a reference to an object in no object stream), /Filter (names) and
 * refuses a predictor; checks "stream" and "endstream" as ob_stream_dict
 * does, and leaves the lexer after "endstream". */
static struct pd_u32 og_sv, og_len;
static pdw og_here(pdw stm)
{
    pdw rc, k, typ = 0, n = 0xFFFF, first = 0xFFFF, haslen = 0, flate = 0;
    struct osgeo *g;
    for (;;) {
        rc = lx_token();
        if (rc) return rc;
        if (tk.kind == PDT_DICTE) break;
        if (tk.kind != PDT_NAME) return PDE_XREF;
        k = kw_ix(tk.text, "Type\0N\0First\0Length\0Filter\0DecodeParms\0");
        rc = ob_value();
        if (rc) return rc;
        if (k == 4 && ov.kind == PDK_REF) {             /* /Length n g R, read now */
            lx_tell(&og_sv);
            rc = ob_find1(ov.rnum, ov.rgen);
            if (rc == OB_INSTM) rc = PDE_XREF;
            if (!rc && !ob_null) rc = ob_value();
            if (rc) return rc;
            L = &fobj;
            lx_seek(&og_sv);
        }
        if (k == 1) typ = (pdw)(ov.kind == PDK_NAME && str_eq(ov.text, "ObjStm"));
        else if (k >= 2 && k <= 4) {
            if (ov.kind != PDK_INT || ov.neg || ov.iv.hi) return PDE_XREF;
            if (k == 2) n = ov.iv.lo; else if (k == 3) first = ov.iv.lo;
            else { u32_cp(&og_len, &ov.iv); haslen = 1; }
        } else if (k == 5) {                            /* /FlateDecode or [/FlateDecode]: its history is the ring */
            k = (pdw)(ov.kind == PDK_ARR);
            if (k) {
                rc = ob_value();
                if (rc) return rc;
            }
            if (ov.kind != PDK_NAME || flt_kind(ov.text) != PDF_K_FLATE) return PDE_FILTER;
            if (k) {
                rc = ob_value();
                if (rc) return rc;
                if (ov.kind != PDK_CLOSE) return PDE_FILTER;
            }
            flate = 1;
        } else if (k == 6 && ov.kind != PDK_NULL) {     /* a predictor cannot share the ring */
            if (ov.kind != PDK_DICT) return PDE_FILTER;
            rc = ob_dict_find("Predictor");
            if (rc == PDE_OK) {
                rc = ob_value();
                if (rc) return rc;
                if (ov.kind != PDK_INT || ov.iv.lo > 1 || ov.iv.hi) return PDE_FILTER;
            } else if (rc != PDE_NOTFOUND) return rc;
        }
        if (ov.kind == PDK_ARR || ov.kind == PDK_DICT) {
            rc = ob_skipc();
            if (rc) return rc;
        }
    }
    if (!typ || n == 0xFFFF || first == 0xFFFF || !haslen) return PDE_XREF;
    rc = lx_token();
    if (rc) return rc;
    if (tk.kind != PDT_OP || !str_eq(tk.text, "stream")) return PDE_XREF;
    rc = st_eol(&og_sv);
    if (!rc) rc = st_end(&og_sv, &og_len);
    if (rc) return rc;
    og_i = og_next;
    og_next = (pdw)((og_next + 1) & (PDE_OSGEO - 1));
    if (ow_g == og_i) ow_g = 0xFFFF;
    g = &og[og_i];
    g->stm = stm; u32_cp(&g->dstart, &og_sv); u32_cp(&g->len, &og_len);
    g->first = first; g->n = n; g->flate = flate;
    return PDE_OK;
}

/* Object stream `stm`'s record into og_i: kept, or read now. The stream
 * object itself must be in no object stream (7.5.7). */
static pdw os_geo(pdw stm)
{
    pdw rc, i;
    for (i = 0; i < PDE_OSGEO; i++)
        if (og[i].stm == stm) { og_i = i; return PDE_OK; }
    rc = ob_find1(stm, 0);
    if (rc == OB_INSTM || (!rc && ob_null)) rc = PDE_XREF;
    if (!rc) rc = ob_value();
    if (!rc && ov.kind != PDK_DICT) rc = PDE_XREF;
    if (!rc) rc = og_here(stm);
    if (rc) err_obj = stm;
    return rc;
}

/* ---- the window ----------------------------------------------------------- */

static void ctx_init(struct sctx *c, const struct pd_u32 *ds, const struct pd_u32 *len,
                     pdw nflt, const pdb *flt, pdw filters);

/* Decode stream og[i] from its start. */
static void os_rewind(pdw i)
{
    static const pdb og_fl[PD_MAXFILTERS] = { PDF_K_FLATE };
    struct osgeo *g = &og[i];
    ctx_init(&osc, &g->dstart, &g->len, g->flate, og_fl, g->flate ? PDFL_FLATE : 0);
    cr_sn = g->stm; cr_sg = 0; cr_skip = 0; cr_win = 1;   /* an object stream is gen 0 (7.5.7) */
    cr_ctx_open(&osc);                          /* the window's own RC4 state: no refusal */
    cr_win = 0;
    if (g->flate) {
        fl_sc[FL_OSLOT] = &osc; fl_fresh[FL_OSLOT] = 1;
        fl_pp[FL_OSLOT][0] = 0; fl_pp[FL_OSLOT][1] = 0; fl_pp[FL_OSLOT][2] = 1; fl_pp[FL_OSLOT][3] = 8;
        if (fl_ld == FL_OSLOT) fl_ld = FL_NONE;     /* its state is the old stream's: drop it */
    }
    ow_g = i; ow_out = 0; ow_end = 0;
}

/* At most 128 more decoded bytes into the ring (Flate, the last stage when it
 * is there, writes them itself: its history IS the ring). */
static void os_more(void)
{
    struct sctx *c = &osc;
    pdw n = 0, a, t;
    int b;
    while (n < 128) {
        b = sget(c, (int)c->nflt - 1);
        if (b < 0) break;
        tmpb[n++] = (pdb)b;
        if (c->filters & PDFL_FLATE) n = (pdw)(n + fl_take(c, tmpb + n, (pdw)(128 - n)));
    }
    if (n > (pdw)(0xFFFF - ow_out)) n = (pdw)(0xFFFF - ow_out);    /* offsets are 16 bits */
    if (!n) { ow_end = 1; return; }
    if (!(c->filters & PDFL_FLATE)) {
        a = (pdw)(ow_out & 0x7FFF);
        t = (pdw)(0x8000 - a);
        if (t > n) t = n;
        pd_fcopy_out(tmpb, g_oseg, a, t);
        if (n > t) pd_fcopy_out(tmpb + t, g_oseg, 0, (pdw)(n - t));
    }
    ow_out = (pdw)(ow_out + n);
}

/* lx_fill's and RAW_COPY's read of a position in an object stream: up to n
 * bytes into dst (near) or dseg:doff (far, dst 0), *pos advanced; fewer at
 * the stream's end. A failure is lx_err (the lexer then sees the end). */
static pdw os_copy(struct pd_u32 *pos, pdb *dst, pdw dseg, pdw doff, pdw n)
{
    pdw stm = (pdw)(pos->hi & 0x7FFF), p = pos->lo, e, k, a, t, i;
    if (!g_oseg) { lx_err = PDE_NOMEM; return 0; }
    if (ow_g == 0xFFFF || og[ow_g].stm != stm || (ow_out > PDE_OSRING && p < (pdw)(ow_out - PDE_OSRING))) {
        for (i = 0; i < PDE_OSGEO && og[i].stm != stm; i++) ;
        if (i >= PDE_OSGEO) { lx_err = PDE_LIMIT; return 0; }   /* its record was reused (20.22.3.3) */
        os_rewind(i);
    }
    e = (pdw)(p + n);
    if (e < p) e = 0xFFFF;
    while (ow_out < e && !ow_end && !lx_err) os_more();
    if (p >= ow_out) return 0;
    k = (pdw)(ow_out - p);
    if (k > n) k = n;
    a = (pdw)(p & 0x7FFF);
    t = (pdw)(0x8000 - a);
    if (t > k) t = k;
    if (dst) {
        pd_fcopy_in(g_oseg, a, dst, t);
        if (k > t) pd_fcopy_in(g_oseg, 0, dst + t, (pdw)(k - t));
    } else {
        pd_fcopy(dseg, doff, g_oseg, a, t);
        if (k > t) pd_fcopy(dseg, (pdw)(doff + t), g_oseg, 0, (pdw)(k - t));
    }
    pos->lo = (pdw)(p + k);
    return k;
}

/* ---- members -------------------------------------------------------------- */

/* One header pair "num off" at the lexer into os_pn, os_po: 0, or 1 when
 * it is not one. */
static pdw os_pn, os_po;
static pdw os_pair(void)
{
    pdw a;
    if (lx_token() || tk.kind != PDT_NUM || tk.neg || tk.real || tk.iv.hi) return 1;
    a = tk.iv.lo;
    if (lx_token() || tk.kind != PDT_NUM || tk.neg || tk.real || tk.iv.hi) return 1;
    os_pn = a; os_po = tk.iv.lo;
    return 0;
}

/* Member `idx` of stream `stm`, which must be object `num`, into os_pos
 * (ob_null when the stream does not hold it): pair idx first, then every
 * pair (the index in the cross-reference is a hint, 7.5.7). */
/* Every pair os_find reads goes into the offset cache, tagged with its
 * stream (QA R6): a member found later needs no read of the header, which
 * is at the stream's start and so, past the ring, a decode from the start */
static void os_note(pdw stm, pdw first)
{
    pdw slot, k = (pdw)(first + os_po);
    if (k < first || k == 0xFFFF || os_pn == 0xFFFF) return;
    slot = (pdw)(PDE_TBL_CACHE + (os_pn & (PDE_OFFCACHE - 1)) * PDE_OCBYTES);
    pd_fpokew(g_tseg, slot, os_pn);
    pd_fpokew(g_tseg, (pdw)(slot + 2), k);
    pd_fpokew(g_tseg, (pdw)(slot + 4), (pdw)(0x8000 | stm));
}

static pdw os_find(pdw stm, pdw idx, pdw num)
{
    pdw rc, k;
    struct osgeo *g;
    rc = os_geo(stm);
    if (rc) return rc;
    g = &og[og_i];
    L = &fobj;
    os_pos.hi = (pdw)(0x8000 | stm); os_pos.lo = 0;
    lx_seek(&os_pos);
    if (idx < g->n) {
        for (k = 0; k < idx && !os_pair(); k++) os_note(stm, g->first);
        if (k == idx && !os_pair()) {
            os_note(stm, g->first);
            if (os_pn == num) goto found;
        }
        lx_seek(&os_pos);
    }
    for (k = 0; k < g->n && !os_pair(); k++) {
        os_note(stm, g->first);
        if (os_pn == num) goto found;
    }
    if (lx_err) return lx_err;
    ob_null = 1;
    return PDE_OK;
found:
    k = (pdw)(g->first + os_po);
    if (k < g->first || k == 0xFFFF) { err_obj = num; return PDE_LIMIT; }
    os_pos.lo = k;
    return PDE_OK;
}

/* ob_enter's second half: object `num` is member xo_off.lo of object stream
 * xo_off.hi & 0x7FFF. The lexer is left at its value. */
static pdw ox_enter(pdw num, pdw gen)
{
    pdw rc, slot, stm = (pdw)(xo_off.hi & 0x7FFF), idx = xo_off.lo;
    if (gen) { ob_null = 1; return PDE_OK; }          /* a member's generation is 0 */
    slot = (pdw)(PDE_TBL_CACHE + (num & (PDE_OFFCACHE - 1)) * PDE_OCBYTES);
    os_pos.hi = pd_fpeekw(g_tseg, (pdw)(slot + 4));
    if (pd_fpeekw(g_tseg, slot) == num && num != 0xFFFF && os_pos.hi == (pdw)(0x8000 | stm))
        os_pos.lo = pd_fpeekw(g_tseg, (pdw)(slot + 2));    /* this stream's pair, read before */
    else {
        rc = os_find(stm, idx, num);                    /* (which caches it) */
        if (rc || ob_null) return rc;
    }
    L = &fobj;
    lx_seek(&os_pos);
    u32_cp(&ob_hdr, &os_pos);
    xo_gen = 0;
    return PDE_OK;
}
