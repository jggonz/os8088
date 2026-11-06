/* ============================================================================
 * pdexref.c - the cross-reference and OPEN (PAGEDESK-SPEC 7.3, 7.4)
 *
 * No xref index in RAM: only a table of subsections, (first, count, file
 * offset of its first 20-byte entry), newest revision first, plus a
 * direct-mapped cache of resolved offsets in the tables claim. Object n is the
 * 20 bytes read through the block cache from the newest subsection holding it.
 * ==========================================================================*/

/* digits[0..n) as a pd_u32; 0 = ok, 1 = does not fit */
static int u32_dec(struct pd_u32 *r, const pdb *s, pdw n)
{
    pdw i;
    u32_zero(r);
    for (i = 0; i < n; i++) {
        pdm_ovf_clear();
        pdm_u32_mul16(&m_a, r->lo, 10);
        pdm_u32_mul16(&m_b, r->hi, 10);
        if (m_b.hi) return 1;
        pdm_u32_set(&m_c, m_b.lo, 0);
        pdm_u32_add(r, &m_a, &m_c);
        pdm_u32_add16(r, (pdw)(s[i] - '0'));
        if (pdm_ovf()) return 1;
    }
    return 0;
}

/* Does subsection (first, count) at entry offset `off` hold `num`? Then read
 * and judge its 20-byte entry. 0 = not here, 1 = answered (xo_state set),
 * 2 = an error (rc in xr_rc). */
static pdw xr_rc;
static pdw xr_entry(pdw num, pdw first, pdw count, const struct pd_u32 *off)
{
    static pdb eb[20];
    pdw d, k, slot;
    if (num < first) return 0;
    d = (pdw)(num - first);
    if (d >= count) return 0;
    slot = (pdw)(PDE_TBL_CACHE + (num & (PDE_OFFCACHE - 1)) * PDE_OCBYTES);
    pdm_u32_mul16(&s_a, 20, d);
    pdm_u32_add(&s_c, off, &s_a);
    if (fl_read(&s_c, eb, 20) != 20) { xr_rc = eof_err(); return 2; }
    for (k = 0; k < 20; k++)
        if ((k < 10 || (k >= 11 && k < 16)) && (eb[k] < '0' || eb[k] > '9')) goto bad;
    if (eb[10] != ' ' || eb[16] != ' ') goto bad;
    if (eb[17] != 'n' && eb[17] != 'f') goto bad;
    if (!((eb[18] == ' ' && (eb[19] == 13 || eb[19] == 10)) || (eb[18] == 13 && eb[19] == 10))) goto bad;
    if (u32_dec(&s_c, eb, 10)) goto bad;
    if (u32_dec(&s_b, eb + 11, 5) || s_b.hi) goto bad;     /* generation <= 65535 */
    if (eb[17] == 'f') {
        pd_fpokew(g_tseg, slot, num);
        pd_fpokew(g_tseg, (pdw)(slot + 2), 0);
        pd_fpokew(g_tseg, (pdw)(slot + 4), 0xFFFF);
        xo_state = 1;
        return 1;
    }
    if (s_c.lo < 9 && s_c.hi == 0) goto bad;
    if (pdm_u32_cmp(&s_c, &fl_size) >= 0) goto bad;
    xo_off.lo = s_c.lo; xo_off.hi = s_c.hi;
    pd_fpokew(g_tseg, slot, num);
    pd_fpokew(g_tseg, (pdw)(slot + 2), xo_off.lo);
    pd_fpokew(g_tseg, (pdw)(slot + 4), xo_off.hi);
    xo_state = 2;
    return 1;
bad:
    err_obj = num;
    err_detail = nrev;
    xr_rc = PDE_XREF;
    return 2;
}

/* The subsection header at *pos ("first count" + white space), read straight
 * off the file: *pos becomes the first entry. 0 = a header, 1 = "trailer"
 * or anything else. Independent of the object lexer, which is mid-parse when
 * a reference is resolved. */
static pdw xh_first, xh_count;
static pdw xr_header(struct pd_u32 *pos)
{
    static pdb hb[32];
    pdw n, i = 0, j, v, t;
    n = fl_read(pos, hb, 32);                      /* advanced pos by n: undo below */
    u32_sub16(pos, n);
    while (i < n && is_ws(hb[i])) i++;
    for (j = 0; j < 2; j++) {
        v = 0;
        if (i >= n || hb[i] < '0' || hb[i] > '9') return 1;
        while (i < n && hb[i] >= '0' && hb[i] <= '9') {
            if (v > 6553) return 1;
            t = (pdw)(v * 10);
            v = (pdw)(t + (hb[i] - '0'));
            if (v < t) return 1;
            i++;
        }
        if (j == 0) xh_first = v; else xh_count = v;
        if (i >= n || !is_ws(hb[i])) return 1;
        while (i < n && (hb[i] == ' ')) i++;
        if (j == 0 && (i >= n)) return 1;
    }
    while (i < n && is_ws(hb[i])) i++;
    pdm_u32_add16(pos, i);
    return 0;
}

/* Resolve object `num` through the newest revision that mentions it:
 * xo_state 0 absent, 1 free, 2 in use (xo_off). */
static pdw xr_find(pdw num)
{
    static struct pd_u32 sp;
    pdw r, j, e, last, a, first, count, t, slot, tag;
    xo_state = 0;
    if (num == 0) { xo_state = 1; return PDE_OK; }
    slot = (pdw)(PDE_TBL_CACHE + (num & (PDE_OFFCACHE - 1)) * PDE_OCBYTES);
    tag = pd_fpeekw(g_tseg, slot);
    if (tag == num && num != 0xFFFF) {                 /* 0xFFFF = an empty slot, so 65535 is uncacheable */
        xo_off.lo = pd_fpeekw(g_tseg, (pdw)(slot + 2));
        xo_off.hi = pd_fpeekw(g_tseg, (pdw)(slot + 4));
        xo_state = (xo_off.hi == 0xFFFF) ? 1 : 2;
        return PDE_OK;
    }
    for (r = 0; r < nrev; r++) {
        last = 0xFFFF;
        for (j = 0; j < rv_n[r]; j++) {
            e = (pdw)(rv_lo[r] + j);
            if (!rv_thin[r]) {                          /* exact: any order */
                t = xr_entry(num, xs[e].first, xs[e].count, &xs[e].off);
                if (t == 2) return xr_rc;
                if (t == 1) return PDE_OK;
            } else if (xs[e].first <= num) last = e;
        }
        if (rv_thin[r] && last != 0xFFFF) {
            e = last;
            t = xr_entry(num, xs[e].first, xs[e].count, &xs[e].off);
            if (t == 2) return xr_rc;
            if (t == 1) return PDE_OK;
            pdm_u32_mul16(&s_a, 20, xs[e].count);
            pdm_u32_add(&sp, &xs[e].off, &s_a);
            for (a = 1; a < xs[e].span; a++) {         /* the subsections it stands for */
                if (xr_header(&sp)) break;
                first = xh_first; count = xh_count;
                if (num < first) break;                 /* ascending: past it */
                t = xr_entry(num, first, count, &sp);
                if (t == 2) return xr_rc;
                if (t == 1) return PDE_OK;
                pdm_u32_mul16(&s_a, 20, count);
                pdm_u32_add(&sp, &sp, &s_a);
            }
        }
    }
    return PDE_OK;
}

/* ---- OPEN, step 0: the file, the header, startxref ---------------------- */
static pdw op_start(void)
{
    pdw rc, n, i, j, found;
    static const char sx[] = "startxref";
    rc = fl_open(xo.name);
    if (rc) return rc;
    if (fl_size.hi == 0 && fl_size.lo < 9) { err_detail = PDX_NOHEADER; return PDE_TRUNC; }
    u32_zero(&s_c);
    n = fl_read(&s_c, tailbuf, 16);
    if (n < 8) return eof_err();
    if (tailbuf[0] == 'C' && tailbuf[1] == 'Z') { err_detail = PDX_PACKED; return PDE_SYNTAX; }
    if (tailbuf[0] != '%' || tailbuf[1] != 'P' || tailbuf[2] != 'D' || tailbuf[3] != 'F' ||
        tailbuf[4] != '-' || tailbuf[5] < '0' || tailbuf[5] > '9' || tailbuf[6] != '.' ||
        tailbuf[7] < '0' || tailbuf[7] > '9') { err_detail = PDX_NOHEADER; return PDE_SYNTAX; }
    doc_ver = (pdw)((tailbuf[5] - '0') * 10 + (tailbuf[7] - '0'));
    /* the last PDE_TAIL bytes: find the LAST "startxref" */
    u32_cp(&s_c, &fl_size);
    if (fl_size.hi || fl_size.lo > PDE_TAIL) u32_sub16(&s_c, PDE_TAIL); else u32_zero(&s_c);
    n = fl_read(&s_c, tailbuf, PDE_TAIL);
    if (pde_ioerr) return PDE_IO;
    found = 0xFFFF;
    for (i = 0; (pdw)(i + 9) <= n; i++) {
        for (j = 0; j < 9; j++) if (tailbuf[i + j] != (pdb)sx[j]) break;
        if (j == 9) found = i;
    }
    if (found == 0xFFFF) { err_detail = PDX_NOSTART; return PDE_TRUNC; }
    i = (pdw)(found + 9);
    while (i < n && is_ws(tailbuf[i])) i++;
    j = i;
    while (j < n && tailbuf[j] >= '0' && tailbuf[j] <= '9') j++;
    if (j == i) { err_detail = PDX_NOSTART; return PDE_TRUNC; }
    if ((pdw)(j - i) > 10) return PDE_XREF;
    if (u32_dec(&tr_start, tailbuf + i, (pdw)(j - i))) return PDE_XREF;
    if (pdm_u32_cmp(&tr_start, &fl_size) >= 0 || u32_is0(&tr_start)) return PDE_XREF;
    u32_cp(&xr_next, &tr_start);
    nxs = 0; nrev = 0;
    return PDE_OK;
}

static pdw xr_unord;
static pdw xr_add(pdw first, pdw count, const struct pd_u32 *off)
{
    pdw lo, n, j;
    lo = rv_lo[nrev]; n = rv_n[nrev];
    if (n == PDE_REVSUB) {                              /* thin: pairs become checkpoints */
        if (xr_unord) return PDE_LIMIT;
        for (j = 0; j < PDE_REVSUB / 2; j++) {
            xs[lo + j].first = xs[lo + 2 * j].first;
            xs[lo + j].count = xs[lo + 2 * j].count;
            u32_cp(&xs[lo + j].off, &xs[lo + 2 * j].off);
            xs[lo + j].span = (pdw)(xs[lo + 2 * j].span + xs[lo + 2 * j + 1].span);
        }
        n = PDE_REVSUB / 2;
        rv_thin[nrev] = 1;
    }
    xs[lo + n].first = first; xs[lo + n].count = count; xs[lo + n].span = 1;
    u32_cp(&xs[lo + n].off, off);
    rv_n[nrev] = (pdw)(n + 1);
    nxs = (pdw)(lo + n + 1);
    return PDE_OK;
}

/* ---- OPEN, step 1: one revision (xref table + trailer) ------------------ */
static pdw op_rev(void)
{
    pdw rc, first, count, i, sawsize = 0, isnewest;
    pdw n, prevfirst;
    isnewest = (pdw)(nrev == 0);
    err_detail = nrev;
    for (i = 0; i < nrev; i++)
        if (pdm_u32_cmp(&xr_seen[i], &xr_next) == 0) return PDE_XREF;   /* /Prev loop */
    if (nrev >= PD_MAXREV) return PDE_LIMIT;
    u32_cp(&xr_seen[nrev], &xr_next);
    rv_lo[nrev] = (pdw)(nrev * PDE_REVSUB); rv_n[nrev] = 0; rv_thin[nrev] = 0;
    xr_unord = 0; prevfirst = 0;
    L = &fobj;
    lx_seek(&xr_next);
    rc = lx_token();
    if (rc) return PDE_XREF;
    if (tk.kind == PDT_NUM) {                           /* "n g obj": an xref stream? */
        rc = lx_token();
        if (!rc && tk.kind == PDT_NUM) rc = lx_token();
        if (!rc && tk.kind == PDT_OP && str_eq(tk.text, "obj")) {
            rc = ob_value();
            if (!rc && ov.kind == PDK_DICT && ob_dict_find("Type") == PDE_OK) {
                rc = ob_value();
                if (!rc && ov.kind == PDK_NAME && str_eq(ov.text, "XRef")) return PDE_XREFSTM;
            }
        }
        return PDE_XREF;
    }
    if (tk.kind != PDT_OP || !str_eq(tk.text, "xref")) return PDE_XREF;
    for (;;) {
        rc = lx_token();
        if (rc) return rc;
        if (tk.kind == PDT_OP && str_eq(tk.text, "trailer")) break;
        if (tk.kind != PDT_NUM || tk.neg || tk.real || tk.range) return PDE_XREF;
        if (tk.iv.hi) return PDE_LIMIT;                 /* object numbers are 16 bits here */
        first = tk.iv.lo;
        rc = lx_token();
        if (rc) return rc;
        if (tk.kind != PDT_NUM || tk.neg || tk.real || tk.range) return PDE_XREF;
        if (tk.iv.hi) return PDE_LIMIT;
        count = tk.iv.lo;
        n = (pdw)(0xFFFF - first);
        if (count > n && (count != (pdw)(n + 1))) return PDE_XREF;     /* first+count <= 65536 */
        lx_skipws();
        lx_tell(&s_c);
        pdm_u32_mul16(&s_a, 20, count);
        pdm_u32_add(&s_b, &s_c, &s_a);
        if (pdm_u32_cmp(&s_b, &fl_size) > 0) return PDE_TRUNC;
        if (count) {
            if (first < prevfirst) xr_unord = 1;
            prevfirst = first;
            rc = xr_add(first, count, &s_c);
            if (rc) return rc;
        }
        lx_seek(&s_b);
    }
    rc = lx_token();
    if (rc) return rc;
    if (tk.kind != PDT_DICTB) return PDE_XREF;
    if (isnewest) lx_tell(&tr_dict);
    u32_zero(&xr_next);
    for (;;) {
        rc = lx_token();
        if (rc) return rc;
        if (tk.kind == PDT_DICTE) break;
        if (tk.kind != PDT_NAME) return PDE_XREF;
        if (str_eq(tk.text, "Size")) {
            rc = ob_value();
            if (rc) return rc;
            if (ov.kind != PDK_INT || ov.neg || ov.iv.hi) return PDE_XREF;
            if (isnewest) tr_size = ov.iv.lo;
            sawsize = 1;
        } else if (str_eq(tk.text, "Root")) {
            rc = ob_value();
            if (rc) return rc;
            if (ov.kind != PDK_REF) return PDE_XREF;
            if (!tr_has_root) { tr_root_n = ov.rnum; tr_root_g = ov.rgen; tr_has_root = 1; }
        } else if (str_eq(tk.text, "Info")) {
            rc = ob_value();
            if (rc) return rc;
            if (ov.kind == PDK_REF && tr_info_n == 0) { tr_info_n = ov.rnum; tr_info_g = ov.rgen; }
        } else if (str_eq(tk.text, "Prev")) {
            rc = ob_value();
            if (rc) return rc;
            if (ov.kind != PDK_INT || ov.neg || ov.range) return PDE_XREF;
            u32_cp(&xr_next, &ov.iv);
            if (u32_is0(&xr_next) || pdm_u32_cmp(&xr_next, &fl_size) >= 0) return PDE_XREF;
        } else if (str_eq(tk.text, "Encrypt")) {
            return PDE_ENCRYPT;
        } else if (str_eq(tk.text, "ID")) {
            rc = ob_value();
            if (rc) return rc;
            if (ov.kind == PDK_ARR) {
                rc = ob_value();
                if (rc) return rc;
                if (ov.kind == PDK_STR && !tr_hasid) {
                    for (i = 0; i < 16; i++) tr_id[i] = 0;
                    for (i = 0; i < ov.len && i < 16 && i < PDE_VT - 1; i++) tr_id[i] = ov.text[i];
                    tr_idlen = ov.len; tr_hasid = 1;
                }
                if (ov.kind == PDK_ARR || ov.kind == PDK_DICT) {
                    rc = ob_skipc();
                    if (rc) return rc;
                }
                if (ov.kind != PDK_CLOSE) {         /* the rest of the array */
                    rc = ob_skipc();
                    if (rc) return rc;
                }
            } else if (ov.kind == PDK_DICT) {
                rc = ob_skipc();
                if (rc) return rc;
            }
        } else {
            rc = ob_skipval();
            if (rc) return rc;
        }
    }
    if (isnewest && !sawsize) return PDE_XREF;
    nrev++;
    return PDE_OK;
}

/* ---- OPEN, step 2: the catalog ------------------------------------------ */
static pdw op_catalog(void)
{
    pdw rc, i, nf;
    if (!tr_has_root) { err_detail = PDX_NOROOT; return PDE_XREF; }
    rc = ob_enter(tr_root_n, tr_root_g);
    if (rc) return rc;
    if (ob_null) { err_detail = PDX_NOROOT; return PDE_SYNTAX; }
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind != PDK_DICT) { err_obj = tr_root_n; return PDE_SYNTAX; }
    /* one pass over the catalog */
    pw_root_n = 0;
    for (;;) {
        rc = lx_token();
        if (rc) return rc;
        if (tk.kind == PDT_DICTE) break;
        if (tk.kind != PDT_NAME) return PDE_SYNTAX;
        if (str_eq(tk.text, "Pages")) {
            rc = ob_value();
            if (rc) return rc;
            if (ov.kind == PDK_REF) { pw_root_n = ov.rnum; pw_root_g = ov.rgen; }
        } else if (str_eq(tk.text, "Outlines")) {
            doc_flags |= PDO_OUTLINE;
            rc = ob_skipval();
            if (rc) return rc;
        } else if (str_eq(tk.text, "Perms")) {
            doc_flags |= PDO_READONLY;
            rc = ob_skipval();
            if (rc) return rc;
        } else if (str_eq(tk.text, "AcroForm")) {
            doc_flags |= PDO_ACROFORM;
            rc = ob_skipval();
            if (rc) return rc;
        } else if (str_eq(tk.text, "MarkInfo")) {
            rc = ob_skipval();
            if (rc) return rc;
            doc_flags |= PDO_TAGGED;
        } else {
            rc = ob_skipval();
            if (rc) return rc;
        }
    }
    if (pw_root_n == 0) { err_detail = PDX_NOROOT; return PDE_SYNTAX; }
    if (doc_flags & PDO_ACROFORM) {
        rc = ob_enter(tr_root_n, tr_root_g);
        if (!rc && !ob_null) rc = ob_value();
        if (!rc && ov.kind == PDK_DICT) rc = ob_dict_find("AcroForm");
        else rc = PDE_NOTFOUND;
        if (!rc) rc = ob_value();
        if (!rc) rc = ob_follow();
        if (!rc && ov.kind == PDK_DICT) {
            rc = ob_dict_find("SigFlags");
            if (!rc) {
                rc = ob_value();
                if (!rc && ov.kind == PDK_INT && (ov.iv.lo & 1)) doc_flags |= PDO_READONLY;
            }
        }
        (void)i; (void)nf;
    }
    return PDE_OK;
}
