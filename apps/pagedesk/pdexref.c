/* ============================================================================
 * pdexref.c - the cross-reference and OPEN (PAGEDESK-SPEC 7.3, 7.4, 20.22.3)
 *
 * A classic chain keeps no xref index in RAM: only a table of subsections,
 * (first, count, file offset of its first 20-byte entry), newest revision
 * first, plus a direct-mapped cache of resolved offsets in the tables claim.
 * Object n is the 20 bytes read through the block cache from the newest
 * subsection holding it. A chain with a cross-reference STREAM (or a hybrid
 * section, or a repaired file) is folded instead into the FLAT INDEX of
 * 20.22.3.1, 4 bytes an object in the PDMEM_XIDX claim.
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

/* A classic 20-byte entry (20.2.3): 'n' with its offset in s_c, 'f', or 0
 * when it is not one. */
static pdb xr_eb[20];
static pdw xr_row(void)
{
    pdw k;
    for (k = 0; k < 20; k++)
        if ((k < 10 || (k >= 11 && k < 16)) && (xr_eb[k] < '0' || xr_eb[k] > '9')) return 0;
    if (xr_eb[10] != ' ' || xr_eb[16] != ' ') return 0;
    if (xr_eb[17] != 'n' && xr_eb[17] != 'f') return 0;
    if (!((xr_eb[18] == ' ' && (xr_eb[19] == 13 || xr_eb[19] == 10)) || (xr_eb[18] == 13 && xr_eb[19] == 10))) return 0;
    if (u32_dec(&s_c, xr_eb, 10)) return 0;
    if (u32_dec(&s_b, xr_eb + 11, 5) || s_b.hi) return 0;     /* generation <= 65535 */
    if (xr_eb[17] == 'f') return 'f';
    if (s_c.lo < 9 && s_c.hi == 0) return 0;
    if (pdm_u32_cmp(&s_c, &fl_size) >= 0) return 0;
    return 'n';
}

/* Does subsection (first, count) at entry offset `off` hold `num`? Then read
 * and judge its 20-byte entry. 0 = not here, 1 = answered (xo_state set),
 * 2 = an error (rc in xr_rc). */
static pdw xr_rc;
static pdw xr_entry(pdw num, pdw first, pdw count, const struct pd_u32 *off)
{
    pdw d, slot, t;
    if (num < first) return 0;
    d = (pdw)(num - first);
    if (d >= count) return 0;
    slot = (pdw)(PDE_TBL_CACHE + (num & (PDE_OFFCACHE - 1)) * PDE_OCBYTES);
    pdm_u32_mul16(&s_a, 20, d);
    pdm_u32_add(&s_c, off, &s_a);
    if (fl_read(&s_c, xr_eb, 20) != 20) { xr_rc = eof_err(); return 2; }
    t = xr_row();
    if (!t) {
        err_obj = num;
        err_detail = xr_rev;
        xr_rc = PDE_XREF;
        return 2;
    }
    if (t == 'f') { s_c.lo = 0; s_c.hi = 0xFFFF; xo_state = 1; }
    else { u32_cp(&xo_off, &s_c); xo_state = 2; }
    pd_fpokew(g_tseg, slot, num);
    pd_fpokew(g_tseg, (pdw)(slot + 2), s_c.lo);
    pd_fpokew(g_tseg, (pdw)(slot + 4), s_c.hi);
    return 1;
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

/* ---- the flat index (20.22.3.1): entry n at g_xseg + n/4 : (n%4)*4 ------ */
static void ix_get(pdw num)
{
    pdw sg = (pdw)(g_xseg + (num >> 2)), o = (pdw)((num & 3) << 2);
    ix_v.lo = pd_fpeekw(sg, o);
    ix_v.hi = pd_fpeekw(sg, (pdw)(o + 2));
}
/* Write entry num; unless `force`, only while it is still 0 (the newest
 * revision that mentions an object wins). An object past the index: ignored. */
static void ix_put(pdw num, pdw lo, pdw hi, pdw force)
{
    pdw sg = (pdw)(g_xseg + (num >> 2)), o = (pdw)((num & 3) << 2);
    if (num >= ix_n) return;
    if (!force) {
        ix_get(num);
        if (ix_v.lo || ix_v.hi) return;
    }
    pd_fpokew(sg, o, lo);
    pd_fpokew(sg, (pdw)(o + 2), hi);
}
/* The index for n entries, zeroed; PDE_NEEDMEM when the claim is smaller. */
static pdw ix_ready(pdw n)
{
    pdw kb, k;
    kb = (pdw)(((pdw)(n >> 8)) + ((n & 255) ? 1 : 0));
    if (!kb) kb = 1;
    if (!g_xseg || g_xkb < kb) { pde_needmem = PDMEM_XIDX; pde_needkb = kb; return PDE_NEEDMEM; }
    for (k = 0; k < kb; k++) pd_ffill((pdw)(g_xseg + (k << 6)), 0, 0, 1024);
    ix_n = n; ix_on = 1;
    return PDE_OK;
}

/* Resolve object `num` through the newest revision that mentions it:
 * xo_state 0 absent, 1 free, 2 in use (xo_off), 3 in an object stream
 * (xo_off.hi = 0x8000 | the stream, xo_off.lo = the member index). */
static pdw xr_find(pdw num)
{
    static struct pd_u32 sp;
    struct xsub *p, *x;
    pdw r, j, first, count, t, slot, tag;
    xo_state = 0;
    if (num == 0) { xo_state = 1; return PDE_OK; }
    if (ix_on) {                                       /* the flat index (20.22.3.1) */
        xr_rev = 0;
        if (num >= ix_n) return PDE_OK;
        ix_get(num);
        if (!ix_v.lo && !ix_v.hi) return PDE_OK;
        u32_cp(&xo_off, &ix_v);
        xo_state = ix_v.hi == 0xFFFF && ix_v.lo == 0xFFFF ? 1 : (ix_v.hi & 0x8000) ? 3 : 2;
        return PDE_OK;
    }
    slot = (pdw)(PDE_TBL_CACHE + (num & (PDE_OFFCACHE - 1)) * PDE_OCBYTES);
    tag = pd_fpeekw(g_tseg, slot);
    if (tag == num && num != 0xFFFF) {                 /* 0xFFFF = an empty slot, so 65535 is uncacheable */
        xo_off.lo = pd_fpeekw(g_tseg, (pdw)(slot + 2));
        xo_off.hi = pd_fpeekw(g_tseg, (pdw)(slot + 4));
        xo_state = (xo_off.hi == 0xFFFF) ? 1 : 2;
        xr_rev = 0;                                     /* a cached entry: its revision is not kept */
        return PDE_OK;
    }
    for (r = 0; r < nrev; r++) {
        xr_rev = r;
        x = 0;
        for (j = 0, p = xs + rv_lo[r]; j < rv_n[r]; j++, p++) {
            if (!rv_thin[r]) {                          /* exact: any order */
                t = xr_entry(num, p->first, p->count, &p->off);
                if (t == 2) return xr_rc;
                if (t == 1) return PDE_OK;
            } else if (p->first <= num) x = p;
        }
        if (x) {                                        /* thinned: from the checkpoint at or below */
            first = x->first; count = x->count;
            u32_cp(&sp, &x->off);
            /* the table is ascending, so the subsections after a checkpoint are
             * read from the file until one starts past num or the table ends */
            for (;;) {
                t = xr_entry(num, first, count, &sp);
                if (t == 2) return xr_rc;
                if (t == 1) return PDE_OK;
                pdm_u32_mul16(&s_a, 20, count);
                pdm_u32_add(&sp, &sp, &s_a);
                if (xr_header(&sp)) break;
                first = xh_first; count = xh_count;
                if (num < first) break;
            }
        }
    }
    return PDE_OK;
}

/* ---- OPEN, step 0: the file, the header, startxref ---------------------- */
static pdw xr_total;                    /* subsections named by the whole chain */
static pdw op_start(void)
{
    pdw rc, n, i, j, found;
    static const char sx[] = "startxref";
    pdb *tailbuf = (pdb *)ctxs;                 /* nothing is open yet: the stream contexts are scratch */
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
    nxs = 0; nrev = 0; xr_total = 0;
    return PDE_OK;
}

/* D10c: the whole chain may name at most PD_MAXSUBSEC subsections */
static pdw xr_unord;
static pdw xr_add(pdw first, pdw count, const struct pd_u32 *off)
{
    struct xsub *p, *q;
    pdw lo, n, j;
    if (++xr_total > PD_MAXSUBSEC) return PDE_LIMIT;
    lo = rv_lo[nrev]; n = rv_n[nrev];
    /* a thinned revision is searched in ascending order: one out of order,
     * before or AFTER the thinning, would hide objects (20.2.3) */
    if (xr_unord && (rv_thin[nrev] || n == PDE_REVSUB)) return PDE_LIMIT;
    p = xs + lo;
    if (n == PDE_REVSUB) {                              /* thin: every other one stays a checkpoint */
        for (j = 0, q = p; j < PDE_REVSUB / 2; j++, q += 2) {
            p[j].first = q->first; p[j].count = q->count;
            p[j].off.lo = q->off.lo; p[j].off.hi = q->off.hi;
        }
        n = PDE_REVSUB / 2;
        rv_thin[nrev] = 1;
    }
    p += n;
    p->first = first; p->count = count; p->off.lo = off->lo; p->off.hi = off->hi;
    rv_n[nrev] = (pdw)(n + 1);
    nxs = (pdw)(lo + n + 1);
    return PDE_OK;
}

/* ---- OPEN, step 1: one revision (xref table + trailer, or an xref stream) */

/* A trailer dictionary's keys, the lexer just inside it: a classic trailer or
 * a cross-reference stream's dictionary (which IS its revision's trailer,
 * 20.22.3.2). The newest revision's /Size, /Root, /Info, first /ID element and
 * /Encrypt win, an older one filling only what is missing; /Prev into
 * xr_next. What a stream needs (/Type /XRef, /W, /Index) and a hybrid's
 * /XRefStm come back in tk_*. */
static pdw tk_w[3], tk_wok, tk_isx, tk_hasix, tk_size, tk_sawsize;
static struct pd_u32 tk_ixpos, tk_xstm;
static pdw tr_keys(pdw isnewest)
{
    pdw rc, i, k;
    tk_wok = 0; tk_isx = 0; tk_hasix = 0; tk_sawsize = 0;
    u32_zero(&tk_xstm);
    for (;;) {
        rc = lx_token();
        if (rc) return rc;
        if (tk.kind == PDT_DICTE) return PDE_OK;
        if (tk.kind != PDT_NAME) return PDE_XREF;
        k = kw_ix(tk.text, "Size\0Prev\0Root\0Info\0Encrypt\0ID\0XRefStm\0Type\0W\0Index\0");
        rc = ob_value();
        if (rc) return rc;
        if (k == 1) {                                   /* Size */
            if (ov.kind != PDK_INT || ov.neg || ov.iv.hi) return PDE_XREF;
            if (isnewest) tr_size = ov.iv.lo;
            if (ov.iv.lo > tr_maxsize) tr_maxsize = ov.iv.lo;
            tk_size = ov.iv.lo; tk_sawsize = 1;
        } else if (k == 2) {                            /* Prev */
            if (ov.kind != PDK_INT || ov.neg || ov.range) return PDE_XREF;
            u32_cp(&xr_next, &ov.iv);
            if (u32_is0(&xr_next) || pdm_u32_cmp(&xr_next, &fl_size) >= 0) return PDE_XREF;
        } else if (k == 3) {                            /* Root */
            if (ov.kind != PDK_REF) return PDE_XREF;
            if (!tr_has_root) { tr_root_n = ov.rnum; tr_root_g = ov.rgen; tr_has_root = 1; }
        } else if (k == 4) {                            /* Info */
            if (ov.kind == PDK_REF && tr_info_n == 0) { tr_info_n = ov.rnum; tr_info_g = ov.rgen; }
        } else if (k == 5) {                            /* Encrypt: recorded, not refused (20.22.3.4) */
            if (!tr_enc_k) {
                if (ov.kind == PDK_REF) { tr_enc_k = 1; tr_enc_n = ov.rnum; tr_enc_g = ov.rgen; }
                else if (ov.kind == PDK_DICT) { tr_enc_k = 2; lx_tell(&tr_enc_pos); }
            }
        } else if (k == 6) {                            /* ID: its first string */
            if (ov.kind == PDK_ARR) {
                rc = ob_value();
                if (rc) return rc;
                if (ov.kind == PDK_STR && !tr_hasid) {
                    for (i = 0; i < 32; i++) tr_id[i] = (pdb)(i < ov.len ? ov.text[i] : 0);
                    tr_idlen = ov.len; tr_hasid = 1;
                }
                if (ov.kind == PDK_ARR || ov.kind == PDK_DICT) {
                    rc = ob_skipc();
                    if (rc) return rc;
                }
                if (ov.kind == PDK_CLOSE) continue;         /* that closed the array */
                ov.kind = PDK_ARR;                          /* the rest of it follows */
            }
        } else if (k == 7) {                            /* XRefStm */
            if (ov.kind == PDK_INT && !ov.neg && !ov.range) u32_cp(&tk_xstm, &ov.iv);
        } else if (k == 8) {                            /* Type */
            if (ov.kind == PDK_NAME && str_eq(ov.text, "XRef")) tk_isx = 1;
        } else if (k == 9 && ov.kind == PDK_ARR) {      /* W */
            for (i = 0;; i++) {
                rc = ob_value();
                if (rc) return rc;
                if (ov.kind == PDK_CLOSE) break;
                if (i >= 3 || ov.kind != PDK_INT || ov.neg || ov.iv.hi || ov.iv.lo > 4) return PDE_XREFSTM;
                tk_w[i] = ov.iv.lo;
            }
            tk_wok = (pdw)(i == 3);
            continue;
        } else if (k == 10 && ov.kind == PDK_ARR) {     /* Index: read by the fold */
            lx_tell(&tk_ixpos); tk_hasix = 1;
        }
        if (ov.kind == PDK_ARR || ov.kind == PDK_DICT) {
            rc = ob_skipc();
            if (rc) return rc;
        }
    }
}

/* "g obj <<" of a cross-reference stream, its object number just lexed */
static pdw xs_head(void)
{
    if (lx_token() || tk.kind != PDT_NUM) return PDE_XREF;
    if (lx_token() || tk.kind != PDT_OP || !str_eq(tk.text, "obj")) return PDE_XREF;
    if (lx_token() || tk.kind != PDT_DICTB) return PDE_XREF;
    return PDE_OK;
}

static pdw op_rev(void)
{
    pdw rc, first, count, i, isnewest;
    pdw n, prevfirst;
    isnewest = (pdw)(nrev == 0);
    err_detail = nrev;
    for (i = 0; i < nrev; i++)
        if (pdm_u32_cmp(&xr_seen[i], &xr_next) == 0) return PDE_XREF;   /* /Prev loop */
    if (nrev >= PD_MAXREV) return PDE_LIMIT;
    u32_cp(&xr_seen[nrev], &xr_next);
    rv_lo[nrev] = (pdw)(nrev * PDE_REVSUB); rv_n[nrev] = 0; rv_thin[nrev] = 0;
    xr_unord = 0; prevfirst = 0;
    rv_kind[nrev] = 0; u32_zero(&rv_xstm[nrev]);
    L = &fobj;
    lx_seek(&xr_next);
    rc = lx_token();
    if (rc) return PDE_XREF;
    if (tk.kind == PDT_NUM) {                           /* "n g obj": a cross-reference stream (20.22.3.2) */
        rc = xs_head();
        if (rc) return rc;
        if (isnewest) { lx_tell(&tr_dict); tr_tflags |= PDTR_XREFSTM; }
        u32_zero(&xr_next);
        rc = tr_keys(isnewest);
        if (rc) return rc;
        if (!tk_isx) return PDE_XREF;
        if (!tk_wok || !tk_sawsize) return PDE_XREFSTM;
        rv_kind[nrev] = 1;
        ix_on = 1;
        doc_flags |= PDO_XREFSTM;
        nrev++;
        return PDE_OK;
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
        if (count > n && (count != (pdw)(n + 1))) return PDE_LIMIT;    /* first+count <= 65536 (D8) */
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
    rc = tr_keys(isnewest);
    if (rc) return rc;
    if (isnewest && !tk_sawsize) return PDE_XREF;
    if (!u32_is0(&tk_xstm) && pdm_u32_cmp(&tk_xstm, &fl_size) < 0) {   /* a hybrid section (7.5.8.4) */
        u32_cp(&rv_xstm[nrev], &tk_xstm);
        ix_on = 1;
        doc_flags |= PDO_XREFSTM;
    }
    nrev++;
    return PDE_OK;
}

/* ---- OPEN, the fold: the chain into the flat index (20.22.3.1) ---------- */
#define FM_ALL 1                /* a classic table: every row                */
#define FM_N   2                /* a hybrid's table, its in-use rows ...     */
#define FM_STM 3                /* ... a cross-reference stream's rows ...   */
#define FM_F   4                /* ... then the hybrid table's free rows     */
static pdw fd_on, fd_go, fd_rev, fd_pass, fd_left, fd_obj, fd_cnt, fd_ixdone;
static pdw fd_w[3];
static struct pd_u32 fd_pos, fd_ipos, fd_f2;

static pdw fd_mode(void)
{
    if (rv_kind[fd_rev]) return fd_pass ? 0 : FM_STM;
    if (u32_is0(&rv_xstm[fd_rev])) return fd_pass ? 0 : FM_ALL;
    return fd_pass < 3 ? (pdw)(fd_pass + FM_N) : 0;
}

static pdw fd_crows(pdw m)
{
    pdw t;
    while (fd_left) {
        if (!fd_cnt) {
            if (xr_header(&fd_pos)) return PDE_OK;      /* "trailer" */
            fd_obj = xh_first; fd_cnt = xh_count;
            continue;
        }
        if (fl_read(&fd_pos, xr_eb, 20) != 20) return eof_err();
        t = xr_row();
        if (!t) { err_obj = fd_obj; err_detail = fd_rev; return PDE_XREF; }
        if (t == 'f') { if (m != FM_N) ix_put(fd_obj, 0xFFFF, 0xFFFF, 0); }
        else if (m != FM_F) ix_put(fd_obj, s_c.lo, s_c.hi, 0);
        fd_obj++; fd_cnt--; fd_left--;
    }
    return PDE_BUSY;
}

/* A cross-reference stream: its dictionary again, then the stream opened
 * through the ordinary chain on ctxs[0] (no stream is open during OPEN). */
static pdw st_push(pdw filtered, pdw num, pdw gen);
static pdw flt_osw;                     /* a slot stage may take the window's slot */
static pdw fd_sbegin(void)
{
    pdw rc, num;
    L = &fobj;
    lx_seek(rv_kind[fd_rev] ? &xr_seen[fd_rev] : &rv_xstm[fd_rev]);
    rc = lx_token();
    if (rc || tk.kind != PDT_NUM || tk.neg || tk.real || tk.iv.hi) return PDE_XREFSTM;
    num = tk.iv.lo;
    if (xs_head()) return PDE_XREFSTM;
    lx_tell(&fd_pos);
    rc = tr_keys(0);
    if (!rc && (!tk_isx || !tk_wok || !tk_sawsize)) rc = PDE_XREFSTM;
    if (!rc) {
        fd_w[0] = tk_w[0]; fd_w[1] = tk_w[1]; fd_w[2] = tk_w[2];
        u32_cp(&fd_ipos, &tk_ixpos);
        fd_ixdone = (pdw)!tk_hasix; fd_obj = 0; fd_cnt = fd_ixdone ? tk_size : 0;
        lx_seek(&fd_pos);
        rc = ob_stream_dict(num, 0);
    }
    if (!rc && sp_kind != PDK_STRM) rc = PDE_XREFSTM;
    flt_osw = (pdw)!g_nfl;              /* no Flate tier: the window's slot (20.22.3.2) */
    if (!rc) rc = ob_filters(num);
    if (!rc && flt_osw && (sp_filters & (PDFL_FLATE | PDFL_LZW)) && !g_oseg) {
        pde_needmem = PDMEM_OSW; pde_needkb = PDE_OSW_KB; rc = PDE_NEEDMEM;
    }
    if (!rc) rc = st_push(1, num, 0);
    flt_osw = 0;
    if (rc == PDE_SYNTAX || rc == PDE_XREF || rc == PDE_TRUNC) rc = PDE_XREFSTM;
    return rc;
}

/* w (<= 4) bytes of the open stream, big-endian, into s_a; 1 = it ended */
static pdw fd_getn(pdw w)
{
    int b;
    u32_zero(&s_a);
    while (w--) {
        L = &ctxs[0].lx;
        b = lx_getc();
        L = &fobj;
        if (b < 0) return 1;
        s_a.hi = (pdw)((s_a.hi << 8) | (s_a.lo >> 8));
        s_a.lo = (pdw)((s_a.lo << 8) | (pdw)b);
    }
    return 0;
}

static pdw fd_srows(void)
{
    pdw t;
    while (fd_left) {
        if (!fd_cnt) {                                  /* the next /Index pair */
            if (fd_ixdone) return PDE_OK;
            lx_seek(&fd_ipos);
            if (ob_value()) return PDE_XREFSTM;
            if (ov.kind == PDK_CLOSE) return PDE_OK;
            if (ov.kind != PDK_INT || ov.neg || ov.iv.hi) return PDE_XREFSTM;
            fd_obj = ov.iv.lo;
            if (ob_value() || ov.kind != PDK_INT || ov.neg || ov.iv.hi || ov.iv.lo > (pdw)(0xFFFF - fd_obj))
                return PDE_XREFSTM;
            fd_cnt = ov.iv.lo;
            lx_tell(&fd_ipos);
            continue;
        }
        t = 1;
        if (fd_w[0]) {
            if (fd_getn(fd_w[0])) break;
            t = s_a.hi || s_a.lo > 2 ? 9 : s_a.lo;
        }
        if (fd_getn(fd_w[1])) break;
        u32_cp(&fd_f2, &s_a);
        if (fd_getn(fd_w[2])) break;
        if (t == 0) ix_put(fd_obj, 0xFFFF, 0xFFFF, 0);
        else if (t == 1) {
            if ((fd_f2.hi == 0 && fd_f2.lo < 9) || pdm_u32_cmp(&fd_f2, &fl_size) >= 0) {
                err_obj = fd_obj; return PDE_XREFSTM;
            }
            ix_put(fd_obj, fd_f2.lo, fd_f2.hi, 0);
        } else if (t == 2) {
            if (fd_f2.hi || fd_f2.lo > PDE_OSMAX || s_a.hi || s_a.lo == 0xFFFF) { err_obj = fd_obj; return PDE_LIMIT; }
            ix_put(fd_obj, s_a.lo, (pdw)(0x8000 | fd_f2.lo), 0);
            doc_flags |= PDO_OBJSTM;
        }
        fd_obj++; fd_cnt--; fd_left--;
    }
    if (fd_left) return lx_err ? lx_err : PDE_XREFSTM;  /* the rows ended before /Index did */
    return PDE_BUSY;
}

static void os_release(void);
static pdw op_fold(void)
{
    pdw rc, m;
    if (!fd_on) {
        rc = ix_ready(tr_maxsize);
        if (rc) return rc;
        fd_on = 1; fd_rev = 0; fd_pass = 0; fd_go = 0;
    }
    fd_left = PDE_FOLDROWS;
    while (fd_rev < nrev) {
        m = fd_mode();
        if (!m) { fd_rev++; fd_pass = 0; continue; }
        if (!fd_go) {
            if (m == FM_STM) rc = fd_sbegin();
            else {
                lx_seek(&xr_seen[fd_rev]);
                rc = lx_token();                        /* "xref" (op_rev saw it) */
                lx_tell(&fd_pos);
                fd_cnt = 0;
            }
            if (rc) return rc;
            fd_go = 1;
        }
        rc = m == FM_STM ? fd_srows() : fd_crows(m);
        if (rc) return rc;                              /* PDE_BUSY: this slice's rows are spent */
        if (m == FM_STM) { nctx = 0; os_release(); }
        fd_go = 0; fd_pass++;
    }
    return PDE_OK;
}

/* ---- OPEN, step 2: the catalog ------------------------------------------ */
static const pdb op_cflag[4] = { PDO_OUTLINE, PDO_READONLY, PDO_ACROFORM, PDO_TAGGED };
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
        i = kw_ix(tk.text, "Pages\0Outlines\0Perms\0AcroForm\0MarkInfo\0");
        if (i == 1) {
            rc = ob_value();
            if (rc) return rc;
            if (ov.kind == PDK_REF) { pw_root_n = ov.rnum; pw_root_g = ov.rgen; }
            continue;
        }
        rc = ob_skipval();
        if (rc) return rc;
        if (i) doc_flags |= op_cflag[i - 2];
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
