/* ============================================================================
 * pdelex.c - the tokenizer and the in-place object parser (PAGEDESK-SPEC 7.1)
 *
 * One tokenizer, two sources: the file (the object parser's lexer, `fobj`) and
 * a decoded stream (a stream context's own lexer, for PDV_ENG_TOKEN). The
 * lexer reads through a 256-byte near buffer; the object parser works IN
 * PLACE on the file - it skips what it does not need and builds no tree, so
 * the "arena" is not where objects live (see 20.2).
 * ==========================================================================*/

/* The classes and the word scan are pdelex.inc's on the target (8086, a
 * table read apiece); these are their twins, which the host harnesses run. */
#ifdef PD_HOST
static int is_ws(int c) { return c == 32 || c == 10 || c == 13 || c == 9 || c == 12 || c == 0; }
static int is_delim(int c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '/' || c == '%';
}
static pdw lx_run(const pdb *src, pdw left, pdb *dst, pdw cap, pdw stop)
{
    pdw k = 0;
    while (k < left && !is_ws(src[k]) && !is_delim(src[k]) && src[k] != stop) {
        if (k < cap) dst[k] = src[k];
        k++;
    }
    return k;
}
static void iv_acc(struct pd_u32 *iv, pdw *f, int d);
/* t[0..n) as a PDF number: 0 = it is not one, else 1 | 2 (a '.') | 4 (the
 * integer part is 2^31 or more: *iv saturates at 0xFFFFFFFF); *iv is the
 * integer part, written only for a number */
static pdw lx_num(const pdb *t, pdw n, struct pd_u32 *iv)
{
    static struct pd_u32 a;
    pdw i = 0, f = 0;
    u32_zero(&a);
    if (n && (t[0] == '+' || t[0] == '-')) i = 1;
    for (; i < n; i++) {
        if (t[i] == '.') { if (f & 2) return 0; f |= 2; }
        else if (t[i] < '0' || t[i] > '9') return 0;
        else { f |= 1; if (!(f & 6)) iv_acc(&a, &f, t[i] - '0'); }
    }
    if (!(f & 1)) return 0;
    if (a.hi >= 0x8000) f |= 4;
    u32_cp(iv, &a);
    return f;
}
#else
int is_ws(int c);
int is_delim(int c);
pdw lx_run(const pdb *src, pdw left, pdb *dst, pdw cap, pdw stop);
pdw lx_num(const pdb *t, pdw n, struct pd_u32 *iv);
#endif
#ifdef PD_HOST
static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
#else
int hexv(int c);
int lx_getc(void);
int lx_peek(void);
void lx_skipws(void);
void lx_tell(struct pd_u32 *out);
#endif

static pdw st_decode(struct sctx *c);          /* pdestrm.c */
static pdw os_copy(struct pd_u32 *pos, pdb *dst, pdw dseg, pdw doff, pdw n);   /* pdeostm.c */

static pdw lx_fill(void)
{
    if (L->mode == 0) {
        /* a position names its container (20.22.3.3): the file, or an object stream */
        if (L->fpos.hi & 0x8000) L->n = os_copy(&L->fpos, L->buf, 0, 0, PDE_IB);
        else L->n = fl_read(&L->fpos, L->buf, PDE_IB);
        L->gen++;
    } else {
        L->n = st_decode((struct sctx *)L);
        L->gen++;
    }
    L->i = 0;
    return L->n;
}
#ifdef PD_HOST
static int lx_getc(void)
{
    if (L->i >= L->n && !lx_fill()) return -1;
    return L->buf[L->i++];
}
static int lx_peek(void)
{
    if (L->i >= L->n && !lx_fill()) return -1;
    return L->buf[L->i];
}
static void lx_skipws(void)
{
    int c;
    for (;;) {
        c = lx_peek();
        if (c < 0) return;
        if (is_ws(c)) { L->i++; continue; }
        if (c == '%') {
            while ((c = lx_getc()) >= 0 && c != 10 && c != 13) ;
            continue;
        }
        return;
    }
}

/* file mode: where the next byte is / go there */
static void lx_tell(struct pd_u32 *out)
{
    u32_cp(out, &L->fpos);
    if (L->n > L->i) u32_sub16(out, (pdw)(L->n - L->i));
}
static void lx_seek(const struct pd_u32 *pos)
{
    u32_cp(&L->fpos, pos);
    L->n = 0; L->i = 0;
    L->smode = 0;
}
/* the next byte that is not blank or a comment, -1 at the end */
static int lx_next(void) { lx_skipws(); return lx_peek(); }
#else
void lx_seek(const struct pd_u32 *pos);
void ov_clear(void);
int lx_next(void);
#endif

#ifdef PD_HOST
/* *iv = *iv * 10 + d, saturating at 0xFFFFFFFF with 4 in *f (lx_num's twin) */
static void iv_acc(struct pd_u32 *iv, pdw *f, int d)
{
    pdm_ovf_clear();
    pdm_u32_mul16(&m_a, iv->lo, 10);
    pdm_u32_mul16(&m_b, iv->hi, 10);
    if (m_b.hi) { *f |= 4; pdm_u32_set(iv, 0xFFFF, 0xFFFF); return; }
    pdm_u32_set(&m_c, m_b.lo, 0);
    pdm_u32_add(iv, &m_a, &m_c);
    pdm_u32_add16(iv, (pdw)d);
    if (pdm_ovf()) { *f |= 4; pdm_u32_set(iv, 0xFFFF, 0xFFFF); }
}
#endif

/* A literal or hex string, one piece (<= 255 bytes) per call. */
static pdw lx_strpiece(void)
{
    pdw n = 0;
    int c, d, v, h, e, k;
    tk.kind = PDT_STR;
    tk.more = 0;
    if (L->smode == 2) {
        for (;;) {
            if (n >= 255) { tk.more = 1; break; }
            c = lx_getc();
            if (c < 0) { tk.len = n; tk.text[n] = 0; return eof_err(); }
            if (c == '>') { L->smode = 0; break; }
            if (is_ws(c)) continue;
            h = hexv(c);
            if (h < 0) return PDE_SYNTAX;
            do {
                d = lx_getc();
                if (d < 0) { tk.len = n; tk.text[n] = 0; return eof_err(); }
            } while (is_ws(d));
            if (d == '>') { tk.text[n++] = (pdb)(h << 4); L->smode = 0; break; }
            v = hexv(d);
            if (v < 0) return PDE_SYNTAX;
            tk.text[n++] = (pdb)((h << 4) | v);
        }
    } else {
        for (;;) {
            if (n >= 255) { tk.more = 1; break; }
            c = lx_getc();
            if (c < 0) { tk.len = n; tk.text[n] = 0; return eof_err(); }
            if (c == '(') { L->sdepth++; tk.text[n++] = '('; }
            else if (c == ')') {
                if (--L->sdepth == 0) { L->smode = 0; break; }
                tk.text[n++] = ')';
            } else if (c == '\\') {
                d = lx_getc();
                if (d < 0) { tk.len = n; tk.text[n] = 0; return eof_err(); }
                if (d >= '0' && d <= '7') {
                    v = d - '0';
                    for (k = 0; k < 2; k++) {
                        e = lx_peek();
                        if (e < '0' || e > '7') break;
                        L->i++;
                        v = v * 8 + (e - '0');
                    }
                    tk.text[n++] = (pdb)(v & 255);
                } else if (d == 13) {
                    if (lx_peek() == 10) L->i++;
                } else if (d == 10) {
                    /* line continuation */
                } else {
                    switch (d) {
                    case 'n': v = 10; break;
                    case 'r': v = 13; break;
                    case 't': v = 9; break;
                    case 'b': v = 8; break;
                    case 'f': v = 12; break;
                    default:  v = d; break;
                    }
                    tk.text[n++] = (pdb)v;
                }
            } else if (c == 13) {
                if (lx_peek() == 10) L->i++;
                tk.text[n++] = 10;
            } else tk.text[n++] = (pdb)c;
        }
    }
    tk.text[n] = 0;
    tk.len = n;
    return PDE_OK;
}

static pdw lx_token(void)
{
    int c, h, h2;
    pdw n, total;
    tk.more = 0; tk.len = 0; tk.neg = 0; tk.real = 0; tk.range = 0; tk.fxr = 0;
    tk.text[0] = 0;
    if (L->smode) return lx_strpiece();
    lx_skipws();
    c = lx_getc();
    if (c < 0) { tk.kind = PDT_EOF; return lx_err ? lx_err : PDE_OK; }
    switch (c) {
    case '[': tk.kind = PDT_ARRB; return PDE_OK;
    case ']': tk.kind = PDT_ARRE; return PDE_OK;
    case '(':
        L->smode = 1; L->sdepth = 1;
        return lx_strpiece();
    case '<':
        if (lx_peek() == '<') { L->i++; tk.kind = PDT_DICTB; return PDE_OK; }
        L->smode = 2;
        return lx_strpiece();
    case '>':
        if (lx_peek() == '>') { L->i++; tk.kind = PDT_DICTE; return PDE_OK; }
        return PDE_SYNTAX;
    case ')':
        return PDE_SYNTAX;
    case '{': case '}':
        tk.kind = PDT_OP; tk.text[0] = (pdb)c; tk.text[1] = 0; tk.len = 1;
        return PDE_OK;
    case '/':
        n = 0;
        for (;;) {
            /* perf: the buffer's run of the name in one lx_run, which stops
             * at a '#' too; a name of PD_MAXNAME bytes or more is refused */
            if (L->i >= L->n && !lx_fill()) break;
            h = (int)lx_run(L->buf + L->i, (pdw)(L->n - L->i), tk.text + n, (pdw)(PD_MAXNAME + 1 - n), '#');
            L->i = (pdw)(L->i + h);
            n = (pdw)(n + h);
            if (n > PD_MAXNAME) return PDE_LIMIT;
            if (L->i >= L->n) continue;                 /* the buffer ended: refill */
            if (L->buf[L->i] != '#') break;             /* it ended at a class */
            L->i++;
            h = lx_getc(); h2 = lx_getc();
            if (hexv(h) < 0 || hexv(h2) < 0) return PDE_SYNTAX;
            if (n >= PD_MAXNAME) return PDE_LIMIT;
            tk.text[n++] = (pdb)(hexv(h) * 16 + hexv(h2));
        }
        tk.text[n] = 0; tk.len = n; tk.kind = PDT_NAME;
        return PDE_OK;
    }
    /* a word: a number, a keyword or an operator. pdelex.inc's lx_run
     * takes the buffer's run of it in one call (perf) */
    tk.text[0] = (pdb)c; n = 1; total = 1;
    for (;;) {
        if (L->i >= L->n && !lx_fill()) break;
        h = (int)lx_run(L->buf + L->i, (pdw)(L->n - L->i), tk.text + n, (pdw)(255 - n), 0);
        L->i = (pdw)(L->i + h);
        total = (pdw)(total + h);
        if (total >= PD_MAXTOK) return PDE_LIMIT;
        n = (pdw)(n + ((pdw)h < (pdw)(255 - n) ? (pdw)h : (pdw)(255 - n)));
        if (L->i < L->n) break;                         /* it ended at a class */
    }
    tk.text[n] = 0; tk.len = n;
    /* a number: [+-] digits, one '.' at most, a digit somewhere - checked
     * and converted in one walk, pdelex.inc's lx_num (perf: it was a walk,
     * a second walk calling the u32 library per digit, and a full 16.16
     * parse of every integer) */
    h = (int)lx_num(tk.text, n, &tk.iv);
    if (h) {
        tk.kind = PDT_NUM;
        tk.neg = c == '-';
        tk.real = (pdw)((h >> 1) & 1);
        tk.range = (pdw)((h >> 2) & 1);                     /* >= 2^31 */
        if (h == 1 && tk.iv.hi == 0 && tk.iv.lo < 0x8000)
            pdm_fx_int(&tk.fx, tk.neg ? -(pdi)tk.iv.lo : (pdi)tk.iv.lo);
        else if (pdm_fx_parse(&tk.fx, (const char *)tk.text, n) != PDE_OK) tk.fxr = 1;
        return PDE_OK;
    }
    if (str_eq(tk.text, "true")) { tk.kind = PDT_TRUE; return PDE_OK; }
    if (str_eq(tk.text, "false")) { tk.kind = PDT_FALSE; return PDE_OK; }
    if (str_eq(tk.text, "null")) { tk.kind = PDT_NULL; return PDE_OK; }
    tk.kind = PDT_OP;
    return PDE_OK;
}

/* ---- the object parser, in place ---------------------------------------- */

#ifdef PD_HOST
static void ov_clear(void)
{
    ov.kind = PDK_NULL; ov.neg = 0; ov.range = 0; ov.fxr = 0; ov.rnum = 0; ov.rgen = 0; ov.len = 0;
    ov.text[0] = 0;
    pdm_fx_int(&ov.fx, 0);
    u32_zero(&ov.iv);
}
#endif

/* The head of the next value: a scalar whole, an array or dictionary as its
 * opener (the lexer is left just inside), a reference "n g R" as one value. */
static pdw ob_value(void)
{
    pdw rc, k, save_i, save_gen, g, big;
    ov_clear();
    rc = lx_token();
    if (rc) return rc;
    switch (tk.kind) {
    case PDT_EOF:   ov.kind = PDK_EOF; return eof_err();
    case PDT_ARRB:  ov.kind = PDK_ARR; return PDE_OK;
    case PDT_DICTB: ov.kind = PDK_DICT; return PDE_OK;
    case PDT_ARRE:
    case PDT_DICTE: ov.kind = PDK_CLOSE; return PDE_OK;
    case PDT_TRUE:  ov.kind = PDK_BOOL; pdm_fx_int(&ov.fx, 1); return PDE_OK;
    case PDT_FALSE: ov.kind = PDK_BOOL; return PDE_OK;
    case PDT_NULL:  ov.kind = PDK_NULL; return PDE_OK;
    case PDT_NAME:
        ov.kind = PDK_NAME; ov.len = tk.len;
        for (k = 0; k < tk.len && k < PDE_VT - 1; k++) ov.text[k] = tk.text[k];
        ov.text[k] = 0;
        return PDE_OK;
    case PDT_STR:
        ov.kind = PDK_STR;
        ov.len = tk.len;
        for (k = 0; k < tk.len && k < PDE_VT - 1; k++) ov.text[k] = tk.text[k];
        ov.text[k] = 0;
        while (tk.more) {
            rc = lx_token();
            if (rc) return rc;
            if ((pdw)(ov.len + tk.len) >= ov.len) ov.len = (pdw)(ov.len + tk.len);
            else ov.len = 0xFFFF;
        }
        cr_string();
        return PDE_OK;
    case PDT_NUM:
        ov.neg = tk.neg; ov.range = tk.range; ov.fxr = tk.fxr;
        ov.fx.lo = tk.fx.lo; ov.fx.hi = tk.fx.hi;
        u32_cp(&ov.iv, &tk.iv);
        if (tk.real) { ov.kind = PDK_REAL; return PDE_OK; }
        ov.kind = PDK_INT;
        if (tk.neg || tk.range) return PDE_OK;
        /* "n g R" ? look two tokens ahead and put them back if it is not.
         * perf: a token is lexed only where the next byte can begin one of
         * the reference - a digit, then an 'R' - so the number in "[278 333
         * 474" costs two tokens, not three, and one before a name costs one */
        g = tk.iv.lo; big = tk.iv.hi;
        save_i = L->i; save_gen = L->gen;
        lx_tell(&ob_pos);
        k = (pdw)lx_next();
        if ((k >= '0' && k <= '9') || k == '+') {      /* verify-perf: "2 +0 R" is one (qpdf, MuPDF) */
            rc = lx_token();
            if (rc == 0 && tk.kind == PDT_NUM && !tk.neg && !tk.real && !tk.range && tk.iv.hi == 0) {
                k = tk.iv.lo;
                if (lx_next() == 'R') {
                    rc = lx_token();
                    if (rc == 0 && tk.kind == PDT_OP && tk.len == 1 && tk.text[0] == 'R') {
                        if (big) return PDE_LIMIT;      /* an object number over 65535 */
                        ov.kind = PDK_REF; ov.rnum = g; ov.rgen = k;
                        return PDE_OK;
                    }
                }
            }
        }
        if (L->gen == save_gen) L->i = save_i; else lx_seek(&ob_pos);
        return PDE_OK;
    default:
        ov.kind = PDK_WORD; ov.len = tk.len;
        for (k = 0; k < tk.len && k < PDE_VT - 1; k++) ov.text[k] = tk.text[k];
        ov.text[k] = 0;
        return PDE_OK;
    }
}

/* Consume to the closer matching an opener the lexer has just passed. */
static pdw ob_skipc(void)
{
    pdw depth = 1, rc;
    for (;;) {
        rc = lx_token();
        if (rc) return rc;
        switch (tk.kind) {
        case PDT_ARRB: case PDT_DICTB:
            if (++depth > PD_MAXDEPTH) return PDE_DEPTH;
            break;
        case PDT_ARRE: case PDT_DICTE:
            if (--depth == 0) return PDE_OK;
            break;
        case PDT_EOF:
            return eof_err();
        }
    }
}
static pdw ob_skipval(void)
{
    pdw rc = ob_value();
    if (rc) return rc;
    if (ov.kind == PDK_ARR || ov.kind == PDK_DICT) return ob_skipc();
    if (ov.kind == PDK_CLOSE) return PDE_SYNTAX;
    return PDE_OK;
}

/* The lexer is just inside a dictionary. 0 = found, the lexer at its value;
 * PDE_NOTFOUND = the dictionary ended without it. */
static pdw ob_dict_find(const char *key)
{
    pdw rc, items = 0;
    for (;;) {
        rc = lx_token();
        if (rc) return rc;
        if (tk.kind == PDT_DICTE) return PDE_NOTFOUND;
        if (tk.kind == PDT_EOF) return eof_err();
        if (tk.kind != PDT_NAME) return PDE_SYNTAX;
        if (++items > PD_MAXITEMS) return PDE_LIMIT;
        if (str_eq(tk.text, key)) return PDE_OK;
        rc = ob_skipval();
        if (rc) return rc;
    }
}

/* Open object (num, gen): the lexer is left at its value. A free, absent or
 * wrong-generation object sets ob_null and succeeds (a reference to it is a
 * null, PDF 7.3.10). ob_find1 is the half for an object in no object stream:
 * it answers OB_INSTM for a member, which ob_enter hands to ox_enter
 * (pdeostm.c) and an object stream's own geometry refuses. */
#define OB_INSTM 0xFFFE
static pdw xr_find(pdw num);                    /* pdexref.c */
static pdw ox_enter(pdw num, pdw gen);          /* pdeostm.c */
static pdw ob_find1(pdw num, pdw gen)
{
    pdw rc;
    ob_null = 0;
    cr_curn = num; cr_curg = gen;
    rc = xr_find(num);
    if (rc) { err_obj = num; return rc; }
    if (xo_state == 3) return OB_INSTM;
    if (xo_state != 2) { ob_null = 1; return PDE_OK; }
    L = &fobj;
    lx_seek(&xo_off);
    u32_cp(&ob_hdr, &xo_off);
    rc = lx_token();
    if (rc || tk.kind != PDT_NUM || tk.neg || tk.real || tk.iv.hi || tk.iv.lo != num) goto bad;
    rc = lx_token();
    if (rc || tk.kind != PDT_NUM || tk.neg || tk.real || tk.iv.hi) goto bad;
    xo_gen = tk.iv.lo;
    rc = lx_token();
    if (rc || tk.kind != PDT_OP || !str_eq(tk.text, "obj")) goto bad;
    if (xo_gen != gen) { ob_null = 1; return PDE_OK; }
    return PDE_OK;
bad:
    err_obj = num;
    err_detail = xr_rev;                        /* the revision whose entry pointed here */
    return PDE_XREF;
}
static pdw ob_enter(pdw num, pdw gen)
{
    pdw rc = ob_find1(num, gen);
    if (rc == OB_INSTM) {
        rc = ox_enter(num, gen);
        cr_curn = 0;                            /* a member's strings are not encrypted (7.6.3) */
    }
    ob_cur_n = num; ob_cur_g = gen;
    return rc;
}

/* Follow references (ov is a REF) until a value: ov holds its head. */
static pdw ob_follow(void)
{
    pdw rc, hops = 0, sn = cr_curn, sg = cr_curg;
    while (ov.kind == PDK_REF) {
        if (++hops > PDE_RESHOPS) return PDE_CYCLE;
        rc = ob_enter(ov.rnum, ov.rgen);
        if (rc) return rc;
        if (ob_null) { ov_clear(); cr_curn = sn; cr_curg = sg; return PDE_OK; }
        rc = ob_value();
        if (rc) return rc;
    }
    cr_curn = sn; cr_curg = sg;
    return PDE_OK;
}

/* Read the next value as a number (an INT/REAL, or a reference to one). The
 * lexer position afterwards is just past the value as written. */
static pdw ob_num(struct pd_fx *out)
{
    pdw rc;
    rc = ob_value();
    if (rc) return rc;
    if (ov.kind == PDK_REF) {
        lx_tell(&on_pos);
        rc = ob_follow();
        if (rc) return rc;
        L = &fobj;
        lx_seek(&on_pos);
    }
    if (ov.kind != PDK_INT && ov.kind != PDK_REAL) return PDE_SYNTAX;
    if (ov.range || ov.fxr) return PDE_RANGE;
    out->lo = ov.fx.lo; out->hi = ov.fx.hi;
    return PDE_OK;
}
