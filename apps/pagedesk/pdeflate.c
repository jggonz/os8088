/* ============================================================================
 * pdeflate.c - FlateDecode and the PNG / TIFF predictors (PAGEDESK-SPEC 20.13)
 *
 * Follows pdestrm.c in pdeng.c's one translation unit. The decoder is RFC 1950
 * / 1951: a zlib header, then stored, fixed and dynamic blocks. It is a
 * hand-written C decoder rather than apps/pixel/pxpng.asm's (20.13.2 says why
 * that one cannot be shared as source); its constants are checked against
 * pxpng.asm's by hosttest/pde_fl_check.py.
 *
 * Output is produced in fills of at most PDE_FLOB bytes into `fl.ob`, symbol
 * by symbol; a match too long for the room is kept as (mleft, mdist) and
 * finished by the next fill. A fill never blocks: its input is the stage
 * below (sget), which is the file, and a short input is a failure, not a
 * wait. The 32 KiB history is the slot's window in a far segment, written
 * once per fill (fl_flush) and read only for a match whose source lies before
 * the fill's first byte; a source inside the fill is read from `fl.ob`. A
 * failure is parked in fl.errc and surfaces, once, as lx_err when the bytes
 * decoded before it have been delivered.
 * ==========================================================================*/

static const pdw fl_lbase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const pdb fl_lext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const pdw fl_dbase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577 };
static const pdb fl_dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static const pdb fl_clord[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/* One scratch for the two things that never overlap in time: a dynamic
 * header's code lengths, and the predictor's row above (its 8 bytes of lead
 * first). Only the loaded decoder uses it, and a build or a recon is one call. */
static pdb fl_scr[320];
static pdw fl_offs[16];
static pdw fl_next[9];

/* ---- slots -------------------------------------------------------------------- */

#define FL_NONE 0xFFFFu
static struct sctx *fl_sc[PDE_NFL];     /* the context that owns slot k, 0 = free     */
static pdw fl_fresh[PDE_NFL];           /* attached, not yet loaded                   */
static pdw fl_pp[PDE_NFL][4];           /* its predictor: kind, row bytes, bpp, bpc   */
static pdw fl_ld;                       /* the slot whose state is in `fl`, or FL_NONE */

static pdw fl_segof(pdw k) { return (pdw)(g_fseg + k * PDE_FLSLOT_PARA); }

static void fl_init(pdw flags, pdw xbseg)
{
    static pdw ext[2];
    pdw k;
    g_fseg = 0; g_nfl = 0; fl_ld = FL_NONE;
    for (k = 0; k < PDE_NFL; k++) fl_sc[k] = 0;
    if (!(flags & PDXI_FLATE)) return;
    if (!pd_fcopy_in(xbseg, (pdw)sizeof(struct pdxb_init), ext, 4)) return;
    k = (pdw)(ext[1] / PDE_FLSLOT_KB);
    if (!ext[0] || !k) return;
    g_fseg = ext[0];
    g_nfl = k > PDE_NFL ? PDE_NFL : k;
}

/* A new stream context at c: any slot of a context that is no longer on the
 * stack (or is this one's, stale) is free. */
static void fl_drop(struct sctx *c)
{
    pdw k;
    for (k = 0; k < PDE_NFL; k++)
        if (fl_sc[k] >= c) {                    /* c is &ctxs[nctx]: the first that is not live */
            fl_sc[k] = 0;
            if (fl_ld == k) fl_ld = FL_NONE;
        }
}

/* Slot k's decoder into `fl`, the loaded one saved to its own slot first. */
static void fl_load(pdw k)
{
    if (fl_ld != FL_NONE) pd_fcopy_out(&fl, fl_segof(fl_ld), PDE_FLSAVE, (pdw)sizeof(struct flst));
    fl_ld = k;
    fl.seg = fl_segof(k);
    if (!fl_fresh[k]) { pd_fcopy_in(fl.seg, PDE_FLSAVE, &fl, (pdw)sizeof(struct flst)); return; }
    fl_fresh[k] = 0;
    for (fl.stage = 0; fl_sc[k]->flt[fl.stage] != PDF_K_FLATE; fl.stage++) ;
    fl.state = FS_HDR; fl.final = 0; fl.tab = 0; fl.errc = 0;
    fl.bb = 0; fl.bn = 0; fl.slen = 0; fl.mleft = 0; fl.mdist = 0;
    fl.wpos = 0; fl.whave = 0; fl.on = 0; fl.oi = 0; fl.pi = 0; fl.pe = 0;
    fl.pred = fl_pp[k][0]; fl.prow = fl_pp[k][1]; fl.bpp = fl_pp[k][2]; fl.bpc = fl_pp[k][3];
    fl.ptag = 0; fl.ppos = fl.prow; fl.pcur = 1;     /* the first call starts a row */
    if (fl.pred) {                                   /* the row above the first is zeros */
        pd_ffill(fl.seg, PDE_FLROWA, 0, fl.prow);
        pd_ffill(fl.seg, PDE_FLROWB, 0, fl.prow);
    }
}

/* ---- /DecodeParms ------------------------------------------------------------- */

static pdw fp_pred, fp_colors, fp_bpc, fp_cols;
static struct pd_fx fp_fx;

static pdw fl_dict(void)
{
    pdw rc, items = 0, *dst;
    for (;;) {
        rc = lx_token();
        if (rc) return rc;
        if (tk.kind == PDT_DICTE) return PDE_OK;
        if (tk.kind == PDT_EOF) return eof_err();
        if (tk.kind != PDT_NAME) return PDE_SYNTAX;
        if (++items > PD_MAXITEMS) return PDE_LIMIT;
        dst = 0;
        if (str_eq(tk.text, "Predictor")) dst = &fp_pred;
        else if (str_eq(tk.text, "Colors")) dst = &fp_colors;
        else if (str_eq(tk.text, "BitsPerComponent")) dst = &fp_bpc;
        else if (str_eq(tk.text, "Columns")) dst = &fp_cols;
        if (!dst) {
            rc = ob_skipval();
            if (rc) return rc;
            continue;
        }
        rc = ob_num(&fp_fx);
        if (rc == PDE_SYNTAX || rc == PDE_RANGE) return PDE_FILTER;
        if (rc) return rc;
        if (fp_fx.lo != 0 || fp_fx.hi < 1) return PDE_FILTER;   /* not a positive integer */
        *dst = (pdw)fp_fx.hi;
    }
}

/* The Flate stage's parameters into fp_*, and whether this build does them. */
static pdw fl_parms(pdw num)
{
    pdw rc, k, idx = 0;
    fp_pred = 1; fp_colors = 1; fp_bpc = 8; fp_cols = 1;
    for (k = 0; k < sp_nflt; k++) if (sp_flt[k] == PDF_K_FLATE) idx = k;
    lx_seek(&ds_pos);
    rc = ob_dict_find("DecodeParms");
    if (rc == PDE_NOTFOUND) {
        lx_seek(&ds_pos);
        rc = ob_dict_find("DP");
        if (rc == PDE_NOTFOUND) return PDE_OK;
    }
    if (rc) return rc;
    rc = ob_value();
    if (!rc && ov.kind == PDK_REF) rc = ob_follow();
    if (!rc && ov.kind == PDK_ARR) {            /* one entry a filter: ours is idx'th */
        for (k = 0; k < idx && !rc; k++) rc = ob_skipval();
        if (!rc) rc = ob_value();
        if (!rc && ov.kind == PDK_REF) rc = ob_follow();
    }
    if (!rc && ov.kind == PDK_DICT) rc = fl_dict();
    if (!rc && fp_pred != 1) {
        if ((fp_pred != 2 && (fp_pred < 10 || fp_pred > 15)) || fp_colors > 4 ||
            (fp_bpc != 1 && fp_bpc != 2 && fp_bpc != 4 && fp_bpc != 8 && fp_bpc != 16) ||
            (fp_pred == 2 && fp_bpc < 8)) rc = PDE_FILTER;              /* sub-byte TIFF components too */
        else if (fp_cols > (pdw)(32768u / (pdw)(fp_colors * fp_bpc))) rc = PDE_LIMIT;   /* a row over 4 KiB */
    }
    if (rc) err_obj = num;
    return rc;
}

static pdw fl_attach(struct sctx *c)
{
    pdw k;
    for (k = 0; k < g_nfl; k++) if (!fl_sc[k]) break;
    if (k >= g_nfl) return PDE_LIMIT;               /* a third Flate stream open at once */
    fl_sc[k] = c; fl_fresh[k] = 1;
    fl_pp[k][0] = fp_pred == 1 ? 0 : (fp_pred == 2 ? 2 : 1);
    fl_pp[k][1] = (pdw)((pdw)(fp_cols * (pdw)(fp_colors * fp_bpc) + 7) >> 3);
    fl_pp[k][2] = (pdw)((pdw)(fp_colors * fp_bpc + 7) >> 3);
    fl_pp[k][3] = fp_bpc;
    return PDE_OK;
}

/* ---- bits ----------------------------------------------------------------------- */

/* At least n (<= 8) bits in the buffer, or 0: the input has ended. */
static int fl_need(pdw n)
{
    int b;
    while (fl.bn < n) {
        b = sget(fl_sc[fl_ld], (int)fl.stage - 1);
        if (b < 0) return 0;
        fl.bb = (pdw)(fl.bb | (pdw)((pdw)b << fl.bn));
        fl.bn = (pdw)(fl.bn + 8);
    }
    return 1;
}

static void fl_fail(pdw code)
{
    if (!fl.errc) fl.errc = code;
    fl.state = FS_END;
}

/* n (<= 16) bits; 0 and a TRUNC failure when the input ends. */
static pdw fl_bits(pdw n)
{
    pdw v;
    if (n == 0) return 0;
    if (n > 8) {
        v = fl_bits(8);
        return (pdw)(v | (pdw)(fl_bits((pdw)(n - 8)) << 8));
    }
    if (!fl_need(n)) { fl_fail(eof_err()); return 0; }
    v = (pdw)(fl.bb & (pdw)((1u << n) - 1));
    fl.bb = (pdw)(fl.bb >> n);
    fl.bn = (pdw)(fl.bn - n);
    return v;
}

/* ---- Huffman tables ------------------------------------------------------------- */

/* Code lengths len[0..n) into cnt (codes of each length), sym (symbols by code
 * order) and, when lut is not 0, the 8-bit lookup. 0, or PDE_SYNTAX for an
 * over-subscribed set, and for an incomplete one unless it is zlib's single
 * code of length 1 (or, `complete`, never). */
static pdw fl_build(pdw *cnt, pdw *sym, pdw *lut, const pdb *len, pdw n, pdw complete)
{
    pdw i, l, left, mx = 0, code, r, k, c;
    for (l = 0; l < 16; l++) cnt[l] = 0;
    for (i = 0; i < n; i++) cnt[len[i]]++;
    if (lut) for (k = 0; k < 256; k++) lut[k] = 0;
    if (cnt[0] == n) return PDE_OK;
    left = 1;
    for (l = 1; l < 16; l++) {
        left = (pdw)(left << 1);
        if (cnt[l] > left) return PDE_SYNTAX;
        left = (pdw)(left - cnt[l]);
        if (cnt[l]) mx = l;
    }
    if (left && (complete || mx != 1)) return PDE_SYNTAX;
    fl_offs[1] = 0;
    for (l = 1; l < 15; l++) fl_offs[l + 1] = (pdw)(fl_offs[l] + cnt[l]);
    for (i = 0; i < n; i++) if (len[i]) sym[fl_offs[len[i]]++] = i;
    if (!lut) return PDE_OK;
    code = 0;
    for (l = 1; l < 9; l++) {
        code = (pdw)((code + (l > 1 ? cnt[l - 1] : 0)) << 1);
        fl_next[l] = code;
    }
    for (i = 0; i < n; i++) {
        l = len[i];
        if (l == 0 || l > 8) continue;
        c = fl_next[l]++;
        r = 0;                                  /* the code, bit-reversed */
        for (k = 0; k < l; k++) { r = (pdw)((r << 1) | (c & 1)); c = (pdw)(c >> 1); }
        for (k = r; k < 256; k = (pdw)(k + (1u << l))) lut[k] = (pdw)(i | (l << 9));
    }
    return PDE_OK;
}

/* One symbol by walking the canonical code a bit at a time: -1 input ended,
 * -2 no such code. */
static int fl_walk(pdw *cnt, pdw *sym)
{
    pdw code = 0, first = 0, idx = 0, len, count;
    for (len = 1; len < 16; len++) {
        if (!fl_need(1)) return -1;
        code = (pdw)(code | (fl.bb & 1));
        fl.bb = (pdw)(fl.bb >> 1);
        fl.bn = (pdw)(fl.bn - 1);
        count = cnt[len];
        if (code < (pdw)(first + count)) return (int)sym[(pdw)(idx + (pdw)(code - first))];
        idx = (pdw)(idx + count);
        first = (pdw)((first + count) << 1);
        code = (pdw)(code << 1);
    }
    return -2;
}

static int fl_lsym(void)
{
    pdw e, l;
    fl_need(8);                                 /* (fewer at the very end: the lookup is then checked) */
    e = fl.lut[fl.bb & 255];
    if (e) {
        l = (pdw)(e >> 9);
        if (l > fl.bn) return -1;
        fl.bb = (pdw)(fl.bb >> l);
        fl.bn = (pdw)(fl.bn - l);
        return (int)(e & 511);
    }
    return fl_walk(fl.lcnt, fl.lsym);
}

static void fl_fixed(void)
{
    pdw i;
    for (i = 0; i < 288; i++) fl_scr[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    fl_build(fl.lcnt, fl.lsym, fl.lut, fl_scr, 288, 0);
    for (i = 0; i < 32; i++) fl_scr[i] = 5;
    fl_build(fl.dcnt, fl.dsym, 0, fl_scr, 32, 0);
    fl.tab = 1;
}

/* A dynamic block's header (RFC 1951 3.2.7). 0, or the failure. */
static pdw fl_dynamic(void)
{
    pdw hlit, hdist, hclen, i, n, rep, rv, rc;
    int s;
    hlit = (pdw)(fl_bits(5) + 257);
    hdist = (pdw)(fl_bits(5) + 1);
    hclen = (pdw)(fl_bits(4) + 4);
    if (hlit > 286 || hdist > 30) fl_fail(PDE_SYNTAX);
    for (i = 0; i < 19; i++) fl_scr[i] = 0;
    for (i = 0; i < hclen; i++) fl_scr[fl_clord[i]] = (pdb)fl_bits(3);
    if (fl.state == FS_END) return fl.errc;
    /* the code-length code is read with the distance table's arrays */
    rc = fl_build(fl.dcnt, fl.dsym, 0, fl_scr, 19, 1);
    if (rc) return rc;
    n = (pdw)(hlit + hdist);
    for (i = 0; i < n; ) {
        s = fl_walk(fl.dcnt, fl.dsym);
        if (s == -1) return eof_err();
        if (s < 0) return PDE_SYNTAX;
        if (s < 16) { fl_scr[i++] = (pdb)s; continue; }
        rv = 0;
        if (s == 16) {
            if (i == 0) return PDE_SYNTAX;
            rv = fl_scr[i - 1];
            rep = (pdw)(3 + fl_bits(2));
        } else if (s == 17) rep = (pdw)(3 + fl_bits(3));
        else rep = (pdw)(11 + fl_bits(7));
        if (fl.state == FS_END) return fl.errc;
        if ((pdw)(i + rep) > n) return PDE_SYNTAX;
        while (rep--) fl_scr[i++] = (pdb)rv;
    }
    if (fl_scr[256] == 0) return PDE_SYNTAX;                  /* no end-of-block code */
    rc = fl_build(fl.lcnt, fl.lsym, fl.lut, fl_scr, hlit, 0);
    if (rc) return rc;
    rc = fl_build(fl.dcnt, fl.dsym, 0, fl_scr + hlit, hdist, 0);
    if (rc) return rc;
    fl.tab = 2;
    return PDE_OK;
}

/* ---- the history ----------------------------------------------------------------- */

/* The fill's bytes ob[0..on) into the window. */
static void fl_flush(void)
{
    pdw n = fl.on, take;
    if (!n) return;
    take = (pdw)(PDE_FLWIN - fl.wpos);
    if (take > n) take = n;
    pd_fcopy_out(fl.ob, fl.seg, fl.wpos, take);
    if (n > take) pd_fcopy_out(fl.ob + take, fl.seg, 0, (pdw)(n - take));
    fl.wpos = (pdw)((fl.wpos + n) & (PDE_FLWIN - 1));
    fl.whave = (pdw)(fl.whave + n);
    if (fl.whave > PDE_FLWIN) fl.whave = PDE_FLWIN;
}

/* As much of the pending match as the room holds. */
static void fl_match(void)
{
    pdw n = fl.mleft, d = fl.mdist, k, src, take;
    if (n > (pdw)(PDE_FLOB - fl.on)) n = (pdw)(PDE_FLOB - fl.on);
    if (d > fl.on) {
        k = (pdw)(d - fl.on);                   /* how far before this fill the source starts */
        if (k > fl.whave) { fl_fail(PDE_SYNTAX); return; }      /* too far back */
        src = (pdw)((fl.wpos - k) & (PDE_FLWIN - 1));
        if (k > n) k = n;
        take = (pdw)(PDE_FLWIN - src);
        if (take > k) take = k;
        pd_fcopy_in(fl.seg, src, fl.ob + fl.on, take);
        if (k > take) pd_fcopy_in(fl.seg, 0, fl.ob + fl.on + take, (pdw)(k - take));
        fl.on = (pdw)(fl.on + k); fl.mleft = (pdw)(fl.mleft - k); n = (pdw)(n - k);
    }
    fl.mleft = (pdw)(fl.mleft - n);
    while (n--) { fl.ob[fl.on] = fl.ob[fl.on - d]; fl.on++; }
}

/* ---- the decoder -------------------------------------------------------------------- */

static void fl_fill(void)
{
    int s;
    pdw v, len, ds;
    fl.on = 0; fl.oi = 0;
    while (fl.on < PDE_FLOB && fl.state != FS_END) {
        if (fl.mleft) { fl_match(); continue; }
        switch (fl.state) {
        case FS_HDR:
            v = fl_bits(8); len = fl_bits(8);
            if (fl.state == FS_END) break;
            if ((v & 15) != 8 || (v >> 4) > 7 || (len & 0x20) || ((pdw)(v * 256 + len)) % 31 != 0)
                fl_fail(PDE_SYNTAX);
            else fl.state = FS_BLK;
            break;
        case FS_BLK:
            fl.final = fl_bits(1);
            v = fl_bits(2);
            if (fl.state == FS_END) break;
            if (v == 0) {
                v = (pdw)(fl.bn & 7);           /* to the byte boundary */
                fl.bb = (pdw)(fl.bb >> v); fl.bn = (pdw)(fl.bn - v);
                len = fl_bits(16); ds = fl_bits(16);
                if (fl.state == FS_END) break;
                if (len != (pdw)(~ds & 0xFFFF)) { fl_fail(PDE_SYNTAX); break; }
                fl.slen = len;
                fl.state = len ? FS_STORED : (fl.final ? FS_END : FS_BLK);
            } else if (v == 1) {
                if (fl.tab != 1) fl_fixed();
                fl.state = FS_CODES;
            } else if (v == 2) {
                v = fl_dynamic();
                if (v) fl_fail(v);
                else fl.state = FS_CODES;
            } else fl_fail(PDE_SYNTAX);
            break;
        case FS_STORED:
            v = fl_bits(8);
            if (fl.state == FS_END) break;
            fl.ob[fl.on++] = (pdb)v;
            if (--fl.slen == 0) fl.state = fl.final ? FS_END : FS_BLK;
            break;
        default:                                /* FS_CODES */
            s = fl_lsym();
            if (s >= 0 && s < 256) { fl.ob[fl.on++] = (pdb)s; break; }
            if (s == 256) { fl.state = fl.final ? FS_END : FS_BLK; break; }
            if (s < 0 || s > 285) { fl_fail(s == -1 ? eof_err() : PDE_SYNTAX); break; }
            s -= 257;
            len = (pdw)(fl_lbase[s] + fl_bits(fl_lext[s]));
            s = fl_walk(fl.dcnt, fl.dsym);
            if (s < 0 || s >= 30) { fl_fail(s == -1 ? eof_err() : PDE_SYNTAX); break; }
            ds = (pdw)(fl_dbase[s] + fl_bits(fl_dext[s]));
            if (fl.state != FS_END) { fl.mleft = len; fl.mdist = ds; }
            break;
        }
    }
    fl_flush();
}

/* ---- delivery ------------------------------------------------------------------------ */

/* The next byte of the decoder's output (before any predictor), or -1 at its
 * end - where a parked failure is reported, once. */
static int fl_rawb(void)
{
    for (;;) {
        if (fl.oi < fl.on) return fl.ob[fl.oi++];
        if (fl.state == FS_END) {
            if (fl.errc) { lx_err = fl.errc; fl.errc = 0; }
            return -1;
        }
        fl_fill();
    }
}

/* ---- predictors ------------------------------------------------------------------------ */

/* Up to PDE_PRC more bytes of the unfiltered rows into pout[8..]; 0 at the end. */
static pdw fl_prfill(void)
{
    pdw n, k, lead, from, cur, prev;
    int r, x, a, b, c, p, pa, pb;
    if (fl.ppos >= fl.prow) {
        if (fl.pred == 1) {
            r = fl_rawb();
            if (r < 0) return 0;
            if (r > 4) { fl_fail(PDE_SYNTAX); fl.oi = fl.on; fl_rawb(); return 0; }
            fl.ptag = (pdw)r;
        }
        fl.pcur ^= 1;
        fl.ppos = 0;
        for (k = 0; k < 8; k++) fl.pout[k] = 0;
    }
    n = (pdw)(fl.prow - fl.ppos);
    if (n > PDE_PRC) n = PDE_PRC;
    for (k = 0; k < n; k++) {
        r = fl_rawb();
        if (r < 0) break;
        fl.pout[8 + k] = (pdb)r;
    }
    if (!k) return 0;
    n = k;
    cur = fl.pcur ? PDE_FLROWB : PDE_FLROWA;
    if (fl.pred == 1) {
        prev = fl.pcur ? PDE_FLROWA : PDE_FLROWB;
        if (fl.ptag >= 2) {                     /* the row above, with 8 bytes of lead */
            from = fl.ppos >= 8 ? (pdw)(fl.ppos - 8) : 0;
            lead = (pdw)(fl.ppos - from);
            for (k = 0; k < (pdw)(8 - lead); k++) fl_scr[k] = 0;
            pd_fcopy_in(fl.seg, (pdw)(prev + from), fl_scr + (8 - lead), (pdw)(lead + n));
        }
        for (k = 0; k < n; k++) {
            x = fl.pout[8 + k];
            a = fl.pout[8 + k - fl.bpp];
            b = fl_scr[8 + k];
            switch (fl.ptag) {
            case 1: x += a; break;
            case 2: x += b; break;
            case 3: x += (a + b) >> 1; break;
            case 4:
                c = fl_scr[8 + k - fl.bpp];
                p = a + b - c; pa = p - a; pb = p - b; p -= c;
                if (pa < 0) pa = -pa;
                if (pb < 0) pb = -pb;
                if (p < 0) p = -p;
                x += (pa <= pb && pa <= p) ? a : (pb <= p ? b : c);
                break;
            }
            fl.pout[8 + k] = (pdb)x;
        }
    } else if (fl.bpc == 8) {
        for (k = 0; k < n; k++)
            fl.pout[8 + k] = (pdb)(fl.pout[8 + k] + fl.pout[8 + k - fl.bpp]);
    } else {                                    /* 16 bits a component, big-endian */
        for (k = 0; (pdw)(k + 1) < n; k += 2) {
            x = (fl.pout[8 + k] << 8) | fl.pout[9 + k];
            x += (fl.pout[8 + k - fl.bpp] << 8) | fl.pout[9 + k - fl.bpp];
            fl.pout[8 + k] = (pdb)(x >> 8);
            fl.pout[9 + k] = (pdb)x;
        }
    }
    pd_fcopy_out(fl.pout + 8, fl.seg, (pdw)(cur + fl.ppos), n);
    fl.ppos = (pdw)(fl.ppos + n);
    fl.pi = 8; fl.pe = (pdw)(8 + n);
    for (k = 0; k < 8; k++) fl.pout[k] = fl.pout[n + k];     /* the lead of the next chunk */
    return n;
}

/* ---- the stage ----------------------------------------------------------------------------- */

static int fl_get(struct sctx *c)
{
    pdw k;
    for (k = 0; k < PDE_NFL && fl_sc[k] != c; k++) ;
    if (k >= PDE_NFL) return -1;
    if (fl_ld != k) fl_load(k);
    if (!fl.pred) return fl_rawb();
    if (fl.pi >= fl.pe && !fl_prfill()) return -1;
    return fl.pout[fl.pi++];
}
