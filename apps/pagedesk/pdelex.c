/* ============================================================================
 * pdelex.c - the tokenizer and the in-place object parser (PAGEDESK-SPEC 7.1)
 *
 * One tokenizer, two sources: the file (the object parser's lexer, `fobj`) and
 * a decoded stream (a stream context's own lexer, for PDV_ENG_TOKEN). The
 * lexer reads through a 256-byte near buffer; the object parser works IN
 * PLACE on the file - it skips what it does not need and builds no tree, so
 * the "arena" is not where objects live (see 20.2).
 * ==========================================================================*/

static int is_ws(int c) { return c == 32 || c == 10 || c == 13 || c == 9 || c == 12 || c == 0; }
static int is_delim(int c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '/' || c == '%';
}
static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static pdw st_decode(struct sctx *c);          /* pdestrm.c */

static pdw lx_fill(void)
{
    if (L->mode == 0) {
        L->n = fl_read(&L->fpos, L->buf, PDE_IB);
        L->gen++;
    } else {
        L->n = st_decode((struct sctx *)L);
        L->gen++;
    }
    L->i = 0;
    return L->n;
}
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

/* tk.iv = tk.iv * 10 + d, saturating; sets tk.range at 2^31 */
static void iv_acc(int d)
{
    pdm_ovf_clear();
    pdm_u32_mul16(&m_a, tk.iv.lo, 10);
    pdm_u32_mul16(&m_b, tk.iv.hi, 10);
    if (m_b.hi) { tk.range = 1; pdm_u32_set(&tk.iv, 0xFFFF, 0xFFFF); return; }
    pdm_u32_set(&m_c, m_b.lo, 0);
    pdm_u32_add(&tk.iv, &m_a, &m_c);
    pdm_u32_add16(&tk.iv, (pdw)d);
    if (pdm_ovf()) { tk.range = 1; pdm_u32_set(&tk.iv, 0xFFFF, 0xFFFF); }
}

static int is_number(const pdb *t, pdw len)
{
    pdw i = 0, digits = 0, dots = 0;
    if (len == 0) return 0;
    if (t[0] == '+' || t[0] == '-') i = 1;
    for (; i < len; i++) {
        if (t[i] >= '0' && t[i] <= '9') digits++;
        else if (t[i] == '.') dots++;
        else return 0;
    }
    return digits > 0 && dots <= 1;
}

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
    int c, d, h, h2;
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
            d = lx_peek();
            if (d < 0 || is_ws(d) || is_delim(d)) break;
            L->i++;
            if (d == '#') {
                h = lx_getc(); h2 = lx_getc();
                if (hexv(h) < 0 || hexv(h2) < 0) return PDE_SYNTAX;
                d = hexv(h) * 16 + hexv(h2);
            }
            if (n >= PD_MAXNAME) return PDE_LIMIT;
            tk.text[n++] = (pdb)d;
        }
        tk.text[n] = 0; tk.len = n; tk.kind = PDT_NAME;
        return PDE_OK;
    }
    /* a word: a number, a keyword or an operator */
    n = 0; total = 0;
    tk.text[n++] = (pdb)c; total = 1;
    for (;;) {
        d = lx_peek();
        if (d < 0 || is_ws(d) || is_delim(d)) break;
        L->i++;
        total++;
        if (total >= PD_MAXTOK) return PDE_LIMIT;
        if (n < 255) tk.text[n++] = (pdb)d;
    }
    tk.text[n] = 0; tk.len = n;
    if (is_number(tk.text, n)) {
        pdw i = 0, real = 0;
        tk.kind = PDT_NUM;
        u32_zero(&tk.iv);
        if (tk.text[0] == '-') { tk.neg = 1; i = 1; }
        else if (tk.text[0] == '+') i = 1;
        for (; i < n; i++) {
            if (tk.text[i] == '.') real = 1;
            else if (!real) iv_acc(tk.text[i] - '0');
        }
        tk.real = real;
        if (tk.iv.hi >= 0x8000) tk.range = 1;          /* >= 2^31 */
        if (pdm_fx_parse(&tk.fx, (const char *)tk.text, n) != PDE_OK) tk.fxr = 1;
        return PDE_OK;
    }
    if (str_eq(tk.text, "true")) { tk.kind = PDT_TRUE; return PDE_OK; }
    if (str_eq(tk.text, "false")) { tk.kind = PDT_FALSE; return PDE_OK; }
    if (str_eq(tk.text, "null")) { tk.kind = PDT_NULL; return PDE_OK; }
    tk.kind = PDT_OP;
    return PDE_OK;
}

/* ---- the object parser, in place ---------------------------------------- */

static void ov_clear(void)
{
    ov.kind = PDK_NULL; ov.neg = 0; ov.range = 0; ov.fxr = 0; ov.rnum = 0; ov.rgen = 0; ov.len = 0;
    ov.text[0] = 0;
    pdm_fx_int(&ov.fx, 0);
    u32_zero(&ov.iv);
}

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
        return PDE_OK;
    case PDT_NUM:
        ov.neg = tk.neg; ov.range = tk.range; ov.fxr = tk.fxr;
        ov.fx.lo = tk.fx.lo; ov.fx.hi = tk.fx.hi;
        u32_cp(&ov.iv, &tk.iv);
        if (tk.real) { ov.kind = PDK_REAL; return PDE_OK; }
        ov.kind = PDK_INT;
        if (tk.neg || tk.range) return PDE_OK;
        /* "n g R" ? look two tokens ahead and put them back if it is not */
        g = tk.iv.lo; big = tk.iv.hi;
        save_i = L->i; save_gen = L->gen;
        lx_tell(&ob_pos);
        rc = lx_token();
        if (rc == 0 && tk.kind == PDT_NUM && !tk.neg && !tk.real && !tk.range && tk.iv.hi == 0) {
            k = tk.iv.lo;
            rc = lx_token();
            if (rc == 0 && tk.kind == PDT_OP && tk.len == 1 && tk.text[0] == 'R') {
                if (big) return PDE_LIMIT;              /* an object number over 65535 */
                ov.kind = PDK_REF; ov.rnum = g; ov.rgen = k;
                return PDE_OK;
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
 * null, PDF 7.3.10). */
static pdw xr_find(pdw num);                    /* pdexref.c */
static pdw ob_enter(pdw num, pdw gen)
{
    pdw rc;
    ob_null = 0;
    rc = xr_find(num);
    if (rc) { err_obj = num; return rc; }
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
    err_detail = nrev;
    return PDE_XREF;
}

/* Follow references (ov is a REF) until a value: ov holds its head. */
static pdw ob_follow(void)
{
    pdw rc, hops = 0;
    while (ov.kind == PDK_REF) {
        if (++hops > PDE_RESHOPS) return PDE_CYCLE;
        rc = ob_enter(ov.rnum, ov.rgen);
        if (rc) return rc;
        if (ob_null) { ov_clear(); return PDE_OK; }
        rc = ob_value();
        if (rc) return rc;
    }
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
