/* ============================================================================
 * pdeverb.c - the verbs (pdabi.h PDV_ENG_*) and the dispatcher
 * ==========================================================================*/

static struct pd_u32 od_pos;                    /* the current dict, just inside */

static void doc_reset(void)
{
    g_open = 0; nctx = 0; nxs = 0; nrev = 0; npages = 0; pw_sp = 0; arena_top = 0;
    op_state = 0;
    tr_size = 0; tr_root_n = 0; tr_root_g = 0; tr_info_n = 0; tr_info_g = 0; tr_has_root = 0;
    tr_hasid = 0; tr_idlen = 0; doc_flags = 0; doc_ver = 0;
    ix_on = 0; ix_n = 0; tr_maxsize = 0; tr_tflags = 0; tr_enc_k = 0; tr_nodict = 0;
    fd_on = 0; rp_on = 0;
    os_reset();
    cr_reset();
    u32_zero(&xr_next); u32_zero(&tr_start); u32_zero(&tr_dict);
    fobj.buf = fo_buf; fobj.mode = 0; fobj.n = 0; fobj.i = 0; fobj.smode = 0;
    u32_zero(&fobj.fpos);
    L = &fobj;
    if (g_filed) fl_invalidate();
}

/* ---- INIT / OPEN / CLOSE ----------------------------------------------------- */

static pdw g_ifs, g_ifk, g_iff;         /* the Flate claim INIT was given */
static pdw v_init(pdw xbseg)
{
    static struct pdxb_init ini;
    pdw os = g_oseg;
    pd_fcopy_in(xbseg, 0, &ini, sizeof(struct pdxb_init));
    g_xb = xbseg;
    /* any PDF (20.22.3.4): the regions OPEN asked for with PDE_NEEDMEM */
    g_xseg = ini.xidx_seg; g_xkb = ini.xidx_kb;
    g_oseg = ini.osw_kb >= PDE_OSW_KB ? ini.osw_seg : 0; g_okb = ini.osw_kb;
    if (g_oseg != os) os_release();
    /* ...and a re-INIT that brings them, with every other claim the same,
     * keeps the document and OPEN's state (it amends 20.2.2) */
    if (g_ready && (g_open || op_state) && ini.cache_seg == g_cseg && ini.cache_kb == g_ckb &&
        ini.arena_seg == g_aseg && ini.arena_kb == g_akb && ini.tables_seg == g_tseg &&
        ini.tables_kb == g_tkb && ini.flate_seg == g_ifs && ini.flate_kb == g_ifk && ini.flags == g_iff)
        return PDE_OK;
    g_ifs = ini.flate_seg; g_ifk = ini.flate_kb; g_iff = ini.flags;
    g_cseg = ini.cache_seg; g_ckb = ini.cache_kb;
    g_aseg = ini.arena_seg; g_akb = ini.arena_kb;
    g_tseg = ini.tables_seg; g_tkb = ini.tables_kb;
    fl_init(ini.flags, ini.flate_seg, ini.flate_kb);                  /* the Flate tier's claim, if any (20.13) */
    /* PD D2: INIT is re-callable and a claim may be 0 until OPEN needs it */
    g_filed = 0;
    g_ready = 1;
    doc_reset();
    return PDE_OK;
}

static pdw open_step(void)
{
    pdw rc;
    switch (op_state) {
    case 0:
        pd_ffill(g_tseg, PDE_TBL_CACHE, 0xFF, PDE_OFFCACHE * PDE_OCBYTES);   /* every slot empty */
        rc = op_start();
        if (rc) return rc;
        op_state = OPS_REV;
        return PDE_BUSY;
    case OPS_REV:
        rc = op_rev();
        if (rc) return rc;
        if (u32_is0(&xr_next)) op_state = ix_on ? OPS_FOLD : tr_enc_k ? OPS_CRYPT : OPS_CAT;
        return PDE_BUSY;
    case OPS_FOLD:                              /* 20.22.3.1 */
        rc = op_fold();
        if (rc) return rc;
        op_state = OPS_WIN;
        return PDE_BUSY;
    case OPS_SCAN1: case OPS_SCANIX: case OPS_SCAN2:    /* 20.22.3.5 */
        rc = op_repair();
        if (rc) return rc;
        op_state = OPS_WIN;
        /* fall through */
    case OPS_WIN:                               /* 20.22.3.3 */
        if ((doc_flags & PDO_OBJSTM) && !g_oseg) {
            pde_needmem = PDMEM_OSW; pde_needkb = PDE_OSW_KB;
            return PDE_NEEDMEM;
        }
        op_state = OPS_CRYPT;
        /* fall through */
    case OPS_CRYPT:                             /* 20.22.3.4, 20.22.5 */
        if (tr_enc_k) {
            rc = cr_open();
            if (rc) return rc;
        }
        op_state = OPS_CAT;
        /* fall through */
    case OPS_CAT:
        rc = op_catalog();
        if (rc) return rc;
        pw_begin();
        rc = pw_visit(pw_root_n, pw_root_g, 0);
        if (rc) return rc;
        op_state = OPS_TREE;
        return PDE_BUSY;
    case OPS_TREE:
        rc = pw_step();
        if (rc) return rc;
        pw_end();
        if (npages == 0) { err_detail = PDX_NOPAGES; return PDE_SYNTAX; }
        return PDE_OK;
    }
    return PDE_RANGE;
}

static pdw v_open(void)
{
    pdw rc, k;
    pd_fcopy_in(g_xb, 0, &xo, sizeof(struct pdxb_open));
    if (xo.state == 0) {
        if (g_ckb < 8 || g_akb < 8 || g_tkb < 11 || !g_cseg || !g_aseg || !g_tseg) return PDE_NOMEM;
        for (k = 0; k < 13 && xo.name[k]; k++) ;
        xo.name[k] = 0;
        doc_reset();
        err_obj = 0; err_detail = 0;
    } else if (xo.state != op_state) {
        doc_reset();
        return PDE_RANGE;
    }
    rc = open_step();
    if (rc && rc != PDE_BUSY && rc != PDE_NEEDMEM && rp_start(rc)) rc = PDE_BUSY;   /* repair (20.22.3.5) */
    if (rc == PDE_BUSY || rc == PDE_NEEDMEM || (rc == PDE_PASSWORD && op_state == OPS_CRYPT)) {
        /* PDE_PASSWORD keeps the document half open: SETKEY, then OPEN with
         * this state resumes at the key (20.22.3.4, 20.22.4.4) */
        xo.state = op_state;
        pd_fcopy_out(&xo, g_xb, 0, sizeof(struct pdxb_open));
        return rc;
    }
    if (rc == PDE_PASSWORD) {                           /* not a failure of the file: OPEN resumes after SETKEY */
        xo.state = op_state;
        pd_fcopy_out(&xo, g_xb, 0, sizeof(struct pdxb_open));
        return rc;
    }
    if (rc) {
        xo.err_obj = err_obj; xo.err_detail = err_detail; xo.state = 0;
        pd_fcopy_out(&xo, g_xb, 0, sizeof(struct pdxb_open));
        if (pw_sp || npages) pw_end();
        doc_reset();
        g_filed = 0;
        return rc;
    }
    g_open = 1;
    xo.state = 0; xo.npages = npages; xo.nrev = nrev; xo.version = doc_ver;
    xo.flags = doc_flags; xo.size.lo = fl_size.lo; xo.size.hi = fl_size.hi;
    xo.err_obj = 0; xo.err_detail = 0;
    pd_fcopy_out(&xo, g_xb, 0, sizeof(struct pdxb_open));
    return PDE_OK;
}

static pdw v_close(void)
{
    if (pw_sp) pw_end();
    doc_reset();
    g_filed = 0;
    return PDE_OK;
}

/* ---- object verbs ------------------------------------------------------------- */

/* the kind of object (num, gen), for a resource reference */
static pdw okind;                               /* ob_kind_of's answer */
static pdw ob_kind_of(pdw num, pdw gen)
{
    pdw rc;
    rc = ob_enter(num, gen);
    if (rc) return rc;
    if (ob_null) { okind = PDK_NULL; return PDE_OK; }
    rc = ob_value();
    if (rc) return rc;
    okind = ov.kind;
    if (ov.kind == PDK_DICT) {
        rc = ob_skipc();
        if (rc) return rc;
        rc = lx_token();
        if (rc) return rc;
        if (tk.kind == PDT_OP && str_eq(tk.text, "stream")) okind = PDK_STRM;
    }
    return PDE_OK;
}

static pdw v_obj_find(void)
{
    static struct pdxb_objref rf;
    static struct pdxb_objinfo oi;
    pdw rc;
    pd_fcopy_in(g_xb, 0, &rf, sizeof(struct pdxb_objref));
    rc = ob_extent(rf.objnum, rf.gen);
    if (rc) return rc;
    oi.objnum = rf.objnum; oi.gen = rf.gen; oi.kind = sp_kind;
    u32_cp(&oi.offset, &sp_off); u32_cp(&oi.length, &ex_len);
    pd_fcopy_out(&oi, g_xb, 0, sizeof(struct pdxb_objinfo));
    return PDE_OK;
}

/* RAW_COPY (20.2.6, 20.22.3.3): the bytes of an object from `done`. A member
 * of an object stream is its value's bytes, framed "n 0 obj\n" ... "\nendobj\n"
 * with PDRC_PLAIN; any other object is the file's bytes (crypt's hook makes
 * PLAIN of an encrypted file's object). */
static char rc_text[24];                /* the synthesised text: digits, " 0 obj\n", "\nendobj\n" */
static struct pd_u32 rs_pos[3], rs_len[3];      /* RAW_COPY's text as up to three pieces */
static pdw rs_txt;                              /* bit i: piece i is rc_text from rs_pos[i].lo */

/* RAW_COPY (20.2.6, 20.22.3.3): the bytes of an object from `done`, as up to
 * three pieces of the file, an object stream or rc_text. PDRC_PLAIN frames
 * an object-stream member "n 0 obj\n" ... "\nendobj\n" and writes an
 * indirect stream /Length as the number (20.22.4.2); an encrypted file's
 * other objects are crypt's hook. */
static pdw v_raw_copy(void)
{
    static struct pdxb_rawcopy rw;
    static struct pd_u32 p, e;
    pdw rc, want, i, k, t;
    pd_fcopy_in(g_xb, 0, &rw, sizeof(struct pdxb_rawcopy));
    rw.got = 0;
    if (rw.objnum == PDRC_ENCDICT) {                    /* a direct /Encrypt's text (20.22.4.1) */
        if (tr_enc_k != 2) return PDE_NOTFOUND;
        L = &fobj;
        lx_seek(&tr_enc_pos);                           /* just inside its "<<" */
        rc = ob_skipc();
        if (rc) return rc;
        lx_tell(&s_c);
        u32_cp(&sp_off, &tr_enc_pos);
        u32_sub16(&sp_off, 2);
        pdm_u32_sub(&ex_len, &s_c, &sp_off);
        if (ex_len.hi || ex_len.lo > 640) return PDE_LIMIT;
        sp_kind = PDK_DICT;
        rw.mode = PDRC_EXACT;
    } else rc = ob_extent(rw.objnum, rw.gen);
    if (rc) return rc;
    if (sp_kind == PDK_NULL) return PDE_NOTFOUND;
    rs_txt = 0;
    u32_cp(&rs_pos[0], &sp_off); u32_cp(&rs_len[0], &ex_len);
    u32_zero(&rs_len[1]); u32_zero(&rs_len[2]);
    if ((rw.mode & 0xFF) == PDRC_PLAIN) {          /* crypt ORs PDRC_STRCIPHER into the high byte */
        if (sp_off.hi & 0x8000) {                       /* a member: its frame */
            pdm_u32_set(&p, 0, rw.objnum);
            k = pdm_u32_todec(rc_text, &p, 0);
            for (i = 0; i < 15; i++) rc_text[k + i] = " 0 obj\n\nendobj\n"[i];
            rs_txt = 5;
            u32_zero(&rs_pos[0]); pdm_u32_set(&rs_len[0], 0, (pdw)(k + 7));
            u32_cp(&rs_pos[1], &sp_off); u32_cp(&rs_len[1], &ex_len);
            pdm_u32_set(&rs_pos[2], 0, (pdw)(k + 7)); pdm_u32_set(&rs_len[2], 0, 8);
        } else if (tr_enc_k) {
            rc = cr_raw_plain(&rw);
            if (rc != PDE_NOTFOUND) return rc;
        } else if (sp_kind == PDK_STRM && sp_lref) {   /* /Length n g R -> the number */
            pdm_u32_sub(&rs_len[0], &sp_lpos, &sp_off);
            rs_txt = 2;
            u32_zero(&rs_pos[1]); pdm_u32_set(&rs_len[1], 0, pdm_u32_todec(rc_text, &sp_len, 0));
            u32_cp(&rs_pos[2], &sp_lend);
            pdm_u32_add(&e, &sp_off, &ex_len);
            pdm_u32_sub(&rs_len[2], &e, &sp_lend);
        }
    }
    pdm_u32_add(&e, &rs_len[0], &rs_len[1]);
    pdm_u32_add(&e, &e, &rs_len[2]);                    /* the whole text */
    if (pdm_u32_cmp(&rw.done, &e) > 0) return PDE_RANGE;
    pdm_u32_sub(&s_c, &e, &rw.done);                    /* bytes left */
    want = rw.dmax;
    if (want > 0xFFF0) want = 0xFFF0;                   /* pd_fcopy's largest move */
    if (s_c.hi == 0 && s_c.lo < want) want = s_c.lo;
    /* the copy must not cross the end of the destination's segment: fl_copy
     * adds its progress to doff in 16 bits, so a wrap would land at dseg:0 */
    if (rw.doff != 0 && want > (pdw)(0 - rw.doff)) return PDE_RANGE;
    u32_cp(&p, &rw.done);                               /* where in the pieces */
    for (i = 0; i < 3 && rw.got < want; i++) {
        if (pdm_u32_cmp(&p, &rs_len[i]) >= 0) { pdm_u32_sub(&p, &p, &rs_len[i]); continue; }
        pdm_u32_sub(&e, &rs_len[i], &p);                /* left in this piece */
        k = (pdw)(want - rw.got);
        if (e.hi == 0 && e.lo < k) k = e.lo;
        t = (pdw)(rw.doff + rw.got);
        if (rs_txt & (1 << i)) {
            pd_fcopy_out(rc_text + rs_pos[i].lo + p.lo, rw.dseg, t, k);
        } else {
            pdm_u32_add(&p, &p, &rs_pos[i]);
            if ((p.hi & 0x8000 ? os_copy(&p, 0, rw.dseg, t, k) : fl_read_far(&p, rw.dseg, t, k)) != k)
                return lx_err ? lx_err : eof_err();
        }
        rw.got = (pdw)(rw.got + k);
        u32_zero(&p);                                   /* the next piece from its start */
    }
    pdm_u32_add16(&rw.done, rw.got);
    pd_fcopy_out(&rw, g_xb, 0, sizeof(struct pdxb_rawcopy));
    return PDE_OK;
}

/* ---- DICT_GET ------------------------------------------------------------------ */

static struct pdxb_dictget dg;

static pdw count_items(void)
{
    pdw rc, n = 0;
    if (ov.kind == PDK_ARR) {
        for (;;) {
            rc = ob_value();
            if (rc) return rc;
            if (ov.kind == PDK_CLOSE) break;
            if (ov.kind == PDK_ARR || ov.kind == PDK_DICT) { rc = ob_skipc(); if (rc) return rc; }
            if (++n > PD_MAXITEMS) return PDE_LIMIT;
        }
    } else {
        for (;;) {
            rc = lx_token();
            if (rc) return rc;
            if (tk.kind == PDT_DICTE) break;
            if (tk.kind != PDT_NAME) return PDE_SYNTAX;
            rc = ob_skipval();
            if (rc) return rc;
            if (++n > PD_MAXITEMS) return PDE_LIMIT;
        }
    }
    dg.count = n;
    return PDE_OK;
}

/* "[12]" -> 12, or -1 */
static int key_index(const char *k)
{
    int v = 0, i = 1;
    if (k[0] != '[') return -1;
    while (k[i] >= '0' && k[i] <= '9') {
        if (v > PD_MAXITEMS / 10) return -1;            /* before the multiply: int is 16 bits */
        v = v * 10 + (k[i] - '0');
        if (v > PD_MAXITEMS) return -1;
        i++;
    }
    if (k[i] != ']' || i == 1) return -1;
    return v;
}

static pdw v_dict_get(void)
{
    pdw rc, k, j;
    int idx;
    pd_fcopy_in(g_xb, 0, &dg, sizeof(struct pdxb_dictget));
    cr_curn = dg.objnum; cr_curg = dg.gen;                 /* strings read below are this object's */
    dg.kind = PDK_NULL; dg.refnum = 0; dg.refgen = 0; dg.len = 0; dg.count = 0; dg.text[0] = 0;
    dg.num.lo = 0; dg.num.hi = 0;
    dg.key[0][31] = 0; dg.key[1][31] = 0; dg.key[2][31] = 0;
    k = 0;
    if (dg.objnum == 0 && dg.gen == 0) {
        if (tr_nodict) return PDE_NOTFOUND;            /* a repaired file with no trailer */
        L = &fobj;
        lx_seek(&tr_dict);
        ov_clear(); ov.kind = PDK_DICT;
    } else {
        /* perf: the same object's same first key as a call before - a font's
         * /Widths asked for [0], [1], [2]... - is found where it was: the
         * name is read back at that offset and must end exactly there, so a
         * different key or a moved one cannot match (and the file does not
         * change while it is open; fl_invalidate forgets the offset) */
        rc = 1;
        if (dg.objnum == dk_obj && dg.gen == dk_gen && dg.key[0][0]) {
            for (j = 0; dg.key[0][j]; j++) ;
            u32_cp(&dc_a, &dk_pos);                     /* L is fobj: pde_verb set it */
            u32_sub16(&dc_a, (pdw)(j + 1));
            if (dc_a.hi & 0x8000) cr_curn = 0;          /* a member: its strings are the stream's, decrypted */
            lx_seek(&dc_a);
            if (lx_token() == 0 && tk.kind == PDT_NAME && str_eq(tk.text, dg.key[0])) {
                lx_tell(&dc_a);
                if (pdm_u32_cmp(&dc_a, &dk_pos) == 0 && ob_value() == 0) { rc = 0; k = 1; }
            }
            if (rc) lx_err = 0;                         /* a window that cannot be re-read (20.22.3.3) */
        }
        if (rc) {
            rc = ob_enter(dg.objnum, dg.gen);
            if (rc) return rc;
            if (ob_null) return PDE_NOTFOUND;
            rc = ob_value();
            if (rc) return rc;
        }
    }
    for (; k < 3 && dg.key[k][0]; k++) {
        rc = ob_follow();
        if (rc) return rc;
        idx = key_index(dg.key[k]);
        if (idx >= 0) {
            if (ov.kind != PDK_ARR) return PDE_NOTFOUND;
            /* perf: the array cursor. The array is named by the file offset
             * of its first item, so element n+1 after element n of the SAME
             * array resumes where n ended instead of re-reading 0..n: a
             * /Widths walked [0], [1], [2]... is linear, not quadratic */
            lx_tell(&dc_a);
            j = 0;
            if ((pdw)idx > dc_idx && pdm_u32_cmp(&dc_a, &dc_arr) == 0) { lx_seek(&dc_pos); j = (pdw)(dc_idx + 1); }
            for (; j < (pdw)idx; j++) {
                rc = ob_value();
                if (rc) return rc;
                if (ov.kind == PDK_CLOSE) return PDE_NOTFOUND;
                if (ov.kind == PDK_ARR || ov.kind == PDK_DICT) { rc = ob_skipc(); if (rc) return rc; }
            }
            rc = ob_value();
            if (rc) return rc;
            if (ov.kind == PDK_CLOSE) return PDE_NOTFOUND;
            if (ov.kind != PDK_ARR && ov.kind != PDK_DICT) { u32_cp(&dc_arr, &dc_a); dc_idx = (pdw)idx; lx_tell(&dc_pos); }
        } else {
            if (ov.kind != PDK_DICT) return PDE_NOTFOUND;
            rc = ob_dict_find(dg.key[k]);
            if (rc) return rc;
            if (k == 0 && dg.objnum) { dk_obj = dg.objnum; dk_gen = dg.gen; lx_tell(&dk_pos); }
            rc = ob_value();
            if (rc) return rc;
        }
    }
    if (ov.kind == PDK_CLOSE || ov.kind == PDK_WORD || ov.kind == PDK_EOF) return PDE_SYNTAX;
    dg.kind = ov.kind;
    switch (ov.kind) {
    case PDK_INT: case PDK_REAL: case PDK_BOOL:
        dg.num.lo = ov.fx.lo; dg.num.hi = ov.fx.hi;
        if ((ov.range || ov.fxr) && ov.kind != PDK_BOOL) {          /* saturated: say so */
            pd_fcopy_out(&dg, g_xb, 0, sizeof(struct pdxb_dictget));
            return PDE_RANGE;
        }
        break;
    case PDK_REF: dg.refnum = ov.rnum; dg.refgen = ov.rgen; break;
    case PDK_NAME: case PDK_STR:
        dg.len = ov.len;
        for (j = 0; j < ov.len && j < 127; j++) dg.text[j] = (char)ov.text[j];
        dg.text[j] = 0;
        break;
    case PDK_ARR: case PDK_DICT:
        rc = count_items();
        if (rc) return rc;
        break;
    }
    pd_fcopy_out(&dg, g_xb, 0, sizeof(struct pdxb_dictget));
    return PDE_OK;
}

/* ---- RES_LOOKUP ------------------------------------------------------------------ */

static struct pdxb_reslookup_x rx;     /* its tail: rflags, cnum, cgen, coff */
static pdw v_res_lookup(void)
{
    static struct pdxb_reslookup rl;
    static struct pd_u32 rds;
    pdw rc, num, gen;
    pd_fcopy_in(g_xb, 0, &rl, sizeof(struct pdxb_reslookup));
    rl.category[15] = 0; rl.name[31] = 0;
    if (rl.page != 0xFFFF) {
        rc = pg_get(rl.page);
        if (rc) return rc;
        num = pgn; gen = pgg;
        rc = pg_inherit(num, gen, "Resources");
        if (rc) return rc;
        rc = ob_value();
        if (rc) return rc;
    } else {
        rc = ob_enter(rl.resobj, rl.resgen);
        if (rc) return rc;
        if (ob_null) return PDE_NOTFOUND;
        rc = ob_value();
        if (rc) return rc;
        if (ov.kind != PDK_DICT) return PDE_NOTFOUND;
        lx_tell(&rds);
        rc = ob_dict_find("Resources");
        if (rc == PDE_NOTFOUND) {                       /* the object IS a resource dictionary */
            lx_seek(&rds);
            ov.kind = PDK_DICT;
        } else if (rc) return rc;
        else {
            rc = ob_value();
            if (rc) return rc;
        }
    }
    rc = ob_follow();
    if (rc) return rc;
    if (ov.kind != PDK_DICT) return PDE_NOTFOUND;
    rc = ob_dict_find(rl.category);
    if (rc) return rc;
    rc = ob_value();
    if (rc) return rc;
    rc = ob_follow();
    if (rc) return rc;
    if (ov.kind != PDK_DICT) return PDE_NOTFOUND;
    rc = ob_dict_find(rl.name);
    if (rc) return rc;
    lx_skipws();
    lx_tell(&rds);
    rc = ob_value();
    if (rc) return rc;
    rx.rflags = 0;
    if (ov.kind == PDK_REF) {
        rl.objnum = ov.rnum; rl.gen = ov.rgen;
        rc = ob_kind_of(rl.objnum, rl.gen);
        if (rc) return rc;
        if (okind == PDK_NULL) return PDE_NOTFOUND;
        rl.kind = okind;
    } else {
        if (ov.kind == PDK_CLOSE || ov.kind == PDK_WORD || ov.kind == PDK_EOF) return PDE_SYNTAX;
        rl.objnum = 0; rl.gen = 0; rl.kind = ov.kind;
        rx.rflags = PDRL_DIRECT; rx.cnum = ob_cur_n; rx.cgen = ob_cur_g;   /* 20.22.3.8 */
        pdm_u32_sub(&rx.coff, &rds, &ob_hdr);
    }
    pd_fcopy_out(&rl, g_xb, 0, sizeof(struct pdxb_reslookup));
    pd_fcopy_out(&rx.rflags, g_xb, sizeof(struct pdxb_reslookup), sizeof rx - sizeof(struct pdxb_reslookup));
    return PDE_OK;
}

/* ---- TRAILER ----------------------------------------------------------------------- */

static pdw v_trailer(void)
{
    static struct pdxb_trailer tr;
    pdw i;
    tr.size = tr_size; tr.root_num = tr_root_n; tr.root_gen = tr_root_g;
    tr.info_num = tr_info_n; tr.info_gen = tr_info_g;
    u32_cp(&tr.startxref, &tr_start);
    for (i = 0; i < 16; i++) {
        tr.id0[i] = tr_hasid ? tr_id[i] : 0;
        tr.id0_hi[i] = tr_hasid ? tr_id[16 + i] : 0;     /* /ID[0] whole (20.22.3.4) */
    }
    tr.id0_len = tr_idlen;
    for (i = 0; i < 16; i++) tr.id0_hi[i] = (tr_hasid && tr_idlen > 16) ? tr_id[16 + i] : 0;
    tr.encrypt_num = (tr_enc_k == 1) ? tr_enc_n : 0; tr.encrypt_gen = (tr_enc_k == 1) ? tr_enc_g : 0;   /* eng's (20.22.3.4) */
    tr.tflags = (tr_enc_k == 2) ? PDTR_ENCDIRECT : 0;
    tr.version = doc_ver;
    tr.encrypt_num = tr_enc_k == 1 ? tr_enc_n : 0;
    tr.encrypt_gen = tr_enc_k == 1 ? tr_enc_g : 0;
    tr.tflags = (pdw)(tr_tflags | (tr_enc_k == 2 ? PDTR_ENCDIRECT : 0));
    pd_fcopy_out(&tr, g_xb, 0, sizeof(struct pdxb_trailer));
    return PDE_OK;
}

/* ---- OUTLINE / ANNOTS ---------------------------------------------------------------- */

/* enter (num, gen) as a dictionary; od_pos is just inside it */
static pdw obj_dict(pdw num, pdw gen)
{
    pdw rc;
    rc = ob_enter(num, gen);
    if (rc) return rc;
    if (ob_null) return PDE_NOTFOUND;
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind != PDK_DICT) return PDE_NOTFOUND;
    lx_tell(&od_pos);
    return PDE_OK;
}
/* the head of /key of the dictionary entered by obj_dict */
static pdw od_key(const char *key)
{
    pdw rc;
    L = &fobj;
    lx_seek(&od_pos);
    rc = ob_dict_find(key);
    if (rc) return rc;
    return ob_value();
}

/* PD text string -> PD Latin, NUL-terminated, at most max-1 characters */
static void to_latin(char *dst, pdw max, const pdb *src, pdw n)
{
    pdw i, o = 0;
    if (n >= 2 && src[0] == 0xFE && src[1] == 0xFF) {
        for (i = 2; (pdw)(i + 1) < n && o < (pdw)(max - 1); i += 2)
            dst[o++] = (src[i] == 0 && src[i + 1] >= 32) ? (char)src[i + 1] : '?';
    } else {
        for (i = 0; i < n && o < (pdw)(max - 1); i++) dst[o++] = (src[i] < 32) ? ' ' : (char)src[i];
    }
    dst[o] = 0;
}
static pdw strn(void) { return ov.len < (pdw)(PDE_VT - 1) ? ov.len : (pdw)(PDE_VT - 1); }

/* ov is a destination value: the page index of its array's first element */
static pdw dest_page(void)
{
    pdw rc;
    if (ov.kind == PDK_REF) { rc = ob_follow(); if (rc) return 0xFFFF; }
    if (ov.kind != PDK_ARR) return 0xFFFF;
    rc = ob_value();
    if (rc || ov.kind != PDK_REF) return 0xFFFF;
    return pg_index_of(ov.rnum);
}

/* nav (wave 2): links (PAGEDESK-SPEC 20.10.2) */
static void os_cpy(char *d, const char *s) { while ((*d++ = *s++) != 0) ; }
#include "pdelink.c"

static pdw v_outline(void)
{
    static struct pdxb_outline ol;
    pdw rc, num, gen;
    pd_fcopy_in(g_xb, 0, &ol, sizeof(struct pdxb_outline));
    if (ol.objnum == 0) {
        rc = obj_dict(tr_root_n, tr_root_g);
        if (rc) return rc;
        rc = od_key("Outlines");
        if (rc) return rc;
        if (ov.kind != PDK_REF) return PDE_NOTFOUND;
        rc = obj_dict(ov.rnum, ov.rgen);
        if (rc) return rc;
        rc = od_key("First");
        if (rc) return rc;
        if (ov.kind != PDK_REF) return PDE_NOTFOUND;
        ol.objnum = ov.rnum; ol.gen = ov.rgen;
    }
    num = ol.objnum; gen = ol.gen;
    rc = obj_dict(num, gen);
    if (rc) return rc;
    ol.next_num = 0; ol.next_gen = 0; ol.first_num = 0; ol.first_gen = 0;
    ol.count = 0; ol.dest_page = 0xFFFF; ol.title[0] = 0;
    rc = od_key("Next");
    if (rc && rc != PDE_NOTFOUND) return rc;
    if (!rc && ov.kind == PDK_REF) { ol.next_num = ov.rnum; ol.next_gen = ov.rgen; }
    rc = od_key("First");
    if (rc && rc != PDE_NOTFOUND) return rc;
    if (!rc && ov.kind == PDK_REF) { ol.first_num = ov.rnum; ol.first_gen = ov.rgen; }
    rc = od_key("Count");
    if (rc && rc != PDE_NOTFOUND) return rc;
    if (!rc && ov.kind == PDK_INT && !ov.range) ol.count = ov.iv.lo;
    rc = od_key("Title");
    if (rc && rc != PDE_NOTFOUND) return rc;
    if (!rc && ov.kind == PDK_STR) to_latin(ol.title, 64, ov.text, strn());
    rc = od_key("Dest");
    if (rc == PDE_OK) ol.dest_page = dest_page();
    else if (rc == PDE_NOTFOUND) {
        rc = od_key("A");
        if (rc == PDE_OK) {
            if (ov.kind == PDK_REF) rc = ob_follow();
            if (!rc && ov.kind == PDK_DICT) {
                rc = ob_dict_find("D");
                if (!rc) { rc = ob_value(); if (!rc) ol.dest_page = dest_page(); }
            }
        }
        if (rc && rc != PDE_NOTFOUND) return rc;
    } else return rc;
    pd_fcopy_out(&ol, g_xb, 0, sizeof(struct pdxb_outline));
    return PDE_OK;
}

static pdw v_annots(pdw page, pdw index)
{
    static struct pdxb_annot an;
    static struct pd_rect rr;
    pdw rc, num, gen, k, an_n, an_g;
    rc = pg_get(page);
    if (rc) return rc;
    num = pgn; gen = pgg;
    rc = obj_dict(num, gen);
    if (rc) return PDE_RANGE;
    rc = od_key("Annots");
    if (rc == PDE_NOTFOUND) return PDE_RANGE;
    if (rc) return rc;
    if (ov.kind == PDK_REF) { rc = ob_follow(); if (rc) return rc; }
    if (ov.kind != PDK_ARR) return PDE_RANGE;
    for (k = 0; k < index; k++) {
        rc = ob_skipval();
        if (rc == PDE_SYNTAX && ov.kind == PDK_CLOSE) return PDE_RANGE;
        if (rc) return rc;
    }
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind == PDK_CLOSE) return PDE_RANGE;
    an.objnum = 0; an.gen = 0; an.subtype[0] = 0; an.nm[0] = 0; an.flags = 0; an.has_ap = 0;
    an.irt_num = 0; an.state = PDS_NONE; an.contents_len = 0; an.contents[0] = 0;
    an.rect.x0 = 0; an.rect.y0 = 0; an.rect.x1 = 0; an.rect.y1 = 0;
    if (ov.kind != PDK_REF) { pd_fcopy_out(&an, g_xb, 0, sizeof(struct pdxb_annot)); return PDE_OK; }
    an_n = ov.rnum; an_g = ov.rgen;
    an.objnum = an_n;
    if (obj_dict(an_n, an_g) != PDE_OK) { pd_fcopy_out(&an, g_xb, 0, sizeof(struct pdxb_annot)); return PDE_OK; }
    rc = od_key("Subtype");
    if (!rc && ov.kind == PDK_NAME) {
        for (k = 0; k < 15 && k < ov.len; k++) an.subtype[k] = (char)ov.text[k];
        an.subtype[k] = 0;
    } else if (rc && rc != PDE_NOTFOUND) return rc;
    rc = od_key("Rect");
    if (!rc && ov.kind == PDK_ARR) {
        if (pg_rect(&rr) == PDE_OK) { an.rect.x0 = rr.x0; an.rect.y0 = rr.y0; an.rect.x1 = rr.x1; an.rect.y1 = rr.y1; }
    } else if (rc && rc != PDE_NOTFOUND) return rc;
    rc = od_key("NM");
    if (!rc && ov.kind == PDK_STR) to_latin(an.nm, 24, ov.text, strn());
    else if (rc && rc != PDE_NOTFOUND) return rc;
    rc = od_key("F");
    if (!rc && ov.kind == PDK_INT && !ov.range) an.flags = ov.iv.lo;
    rc = od_key("IRT");
    if (!rc && ov.kind == PDK_REF) an.irt_num = ov.rnum;
    rc = od_key("State");
    if (!rc && (ov.kind == PDK_STR || ov.kind == PDK_NAME) && str_eq(ov.text, "Completed")) an.state = PDS_COMPLETED;
    rc = od_key("Contents");
    if (!rc && ov.kind == PDK_STR) {
        an.contents_len = ov.len;
        to_latin(an.contents, 160, ov.text, strn());
    }
    rc = od_key("AP");
    if (!rc) {
        if (ov.kind == PDK_REF) rc = ob_follow();
        if (!rc && ov.kind == PDK_DICT && ob_dict_find("N") == PDE_OK) an.has_ap = 1;
    }
    if (str_eq((pdb *)an.subtype, "Link")) lk_fill(&an);        /* nav: 20.10.2 */
    pd_fcopy_out(&an, g_xb, 0, sizeof(struct pdxb_annot));
    return PDE_OK;
}

/* ---- the dispatcher ---------------------------------------------------------------------- */

unsigned pde_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
{
    static struct pdxb_fingerprint fp;
    pdw rc;
    pde_ax = 0; pde_dx = 0; pde_ioerr = 0; lx_err = 0;
    if (verb == PDV_ENG_INIT) {
        rc = v_init((pdw)bx);
    } else if (!g_ready) {
        rc = PDE_RANGE;
    } else {
        L = &fobj;
        switch (verb) {
        case PDV_ENG_OPEN:        rc = v_open(); break;
        case PDV_ENG_CLOSE:       rc = v_close(); break;
        case PDV_ENG_FINGERPRINT:
            if (!g_filed) { rc = PDE_RANGE; break; }
            rc = fl_fingerprint(&fp);
            if (!rc) pd_fcopy_out(&fp, g_xb, 0, sizeof(struct pdxb_fingerprint));
            break;
        case PDV_ENG_CRYPTINFO:   rc = g_filed ? cr_info() : PDE_RANGE; break;     /* 20.22.5 */
        case PDV_ENG_SETKEY:      rc = cr_setkey(); break;
        case PDV_ENG_ARENA_MARK:  pde_ax = arena_top; rc = PDE_OK; break;
        case PDV_ENG_ARENA_REL:
            if (bx > arena_top) rc = PDE_RANGE; else { arena_top = (pdw)bx; rc = PDE_OK; }
            break;
        default:
            /* a half-open document (PDE_PASSWORD) answers DICT_GET and TRAILER:
             * PDXK reads /Encrypt through them (20.22.3.8, 20.22.5) */
            if (!g_open && !(op_state == OPS_CRYPT &&
                             (verb == PDV_ENG_DICT_GET || verb == PDV_ENG_TRAILER))) { rc = PDE_RANGE; break; }
            switch (verb) {
            case PDV_ENG_PAGEINFO:   rc = v_pageinfo((pdw)bx); break;
            case PDV_ENG_OBJ_FIND:   rc = v_obj_find(); break;
            case PDV_ENG_DICT_GET:   rc = v_dict_get(); break;
            case PDV_ENG_STRM_OPEN:  rc = v_strm_open(); break;
            case PDV_ENG_STRM_READ:  rc = v_strm_read((pdw)bx, (pdw)cx, (pdw)dx); break;
            case PDV_ENG_STRM_CLOSE: rc = v_strm_close(); break;
            case PDV_ENG_TOKEN:      rc = v_token(); break;
            case PDV_ENG_RES_LOOKUP: rc = v_res_lookup(); break;
            case PDV_ENG_RAW_COPY:   rc = v_raw_copy(); break;
            case PDV_ENG_TRAILER:    rc = v_trailer(); break;
            case PDV_ENG_OUTLINE:    rc = v_outline(); break;
            case PDV_ENG_ANNOTS:     rc = v_annots((pdw)bx, (pdw)cx); break;
            case PDV_ENG_SEARCH:     rc = v_search(); break;     /* nav: 20.10.1 */
            default:                 rc = PDE_RANGE; break;
            }
        }
    }
    /* a failed read is the truth, whatever the parser made of the gap - and
     * the gap is not kept: a token cut short by it may have set dict_get's
     * array cursor mid-number (verify-perf), so the cursor, the key cache and
     * the file cache all go */
    if (rc != PDE_BUSY && pde_ioerr) { rc = PDE_IO; fl_invalidate(); }
    if (rc == PDE_BUSY) { pde_fail = 0; return PDE_BUSY; }  /* D11: more to do is not a failure - CF = 0, AX = PDE_BUSY */
    if (rc == PDE_NEEDMEM) {                    /* nor is a region to claim: CX = PDMEM_*, DX = KiB (20.22.1) */
        pde_fail = 0; pde_cx = pde_needmem; pde_dx = pde_needkb;
        return PDE_NEEDMEM;
    }
    pde_fail = rc;
    return rc ? rc : pde_ax;
}
