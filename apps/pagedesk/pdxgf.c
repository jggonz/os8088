/* ============================================================================
 * os8088 - apps/pagedesk/pdxgf.c    PDF functions, types 0 2 3 4 (20.22.9)
 *
 * fn_compile() turns a function object into a row of the table below, fn_eval()
 * evaluates it. No recursion: a type 3 chain is a loop, a type 4 program is a
 * byte code with forward jumps run on an explicit operand stack. #included by
 * pdxg.c after pdxgp.c. Every static array is bss, so no frame passes 96 bytes.
 * ==========================================================================*/
#define FN_MAX   12
#define FN_SAMAX 3072
#define FN_CODE  1024
#define FN_STACK 100
#define FNE_NONE 0xFFu
#ifdef GXDBG
#include <stdio.h>
#define FNFAIL() (fprintf(stderr, "fn fail line %d\n", __LINE__), FNE_NONE)
#else
#define FNFAIL() FNE_NONE
#endif

struct gxf {
    pdb type, nin, nout, nsub, bps, ok, pad0, pad1;
    pdw sub[8];
    pdw soff, slen, coff, clen, size[4];
    struct pd_fx dom[8], rng[16], p1[16], p2[16];
    pdw nrng;
};
static struct gxf fn_t[FN_MAX];
static pdw fn_n;
static pdb fn_sam[FN_SAMAX];
static pdw fn_sused;
static pdb fn_code[FN_CODE];
static pdw fn_cused;

/* ---- the calculator's byte code ------------------------------------------ */
#define FC_PUSH 1
#define FC_JZ   2
#define FC_JMP  3
static const char fc_names[] =
    "abs\0add\0and\0atan\0bitshift\0ceiling\0copy\0cos\0cvi\0cvr\0div\0dup\0eq\0exch\0"
    "exp\0false\0floor\0ge\0gt\0idiv\0index\0le\0ln\0log\0lt\0mod\0mul\0ne\0neg\0not\0or\0"
    "pop\0roll\0round\0sin\0sqrt\0sub\0true\0truncate\0xor\0";
/* operator i (0-based in the list above) is byte code 10 + i */

static int fc_find(const char *s, pdw len)
{
    const char *t = fc_names;
    int idx = 0;
    pdw k;
    while (*t) {
        k = 0;
        while (t[k] && k < len && t[k] == s[k]) k++;
        if (!t[k] && k == len) return 10 + idx;
        while (*t) t++;
        t++; idx++;
    }
    return -1;
}

static pdw fc_blk0[8], fc_blk1[8];      /* closed blocks (start, end) not yet consumed */
static pdw fc_open[GX_DEPTH + 8];

static int fc_ins(pdw at, pdw n)        /* open n bytes at `at` */
{
    pdw i;
    if (fn_cused + n > FN_CODE) return 0;
    for (i = fn_cused; i > at; i--) fn_code[i - 1 + n] = fn_code[i - 1];
    fn_cused += n;
    return 1;
}

/* Compile the "{ ... }" program held in the calculator stream into fn_code.
 * Answers the code length or 0 on a malformed program. */
static pdw fc_compile(pdw len)
{
    pdw i = 0, h, k, base = fn_cused, nb = 0, nopen = 0, a0, a1, b0, b1;
    int c, op;
    static struct pd_fx v;
    while (i < len) {
        c = (pdb)gx_tb[i];
        if (gx_isws(c)) { i++; continue; }
        if (c == '%') { while (i < len && gx_tb[i] != 10 && gx_tb[i] != 13) i++; continue; }
        if (c == '{') {
            if (nopen >= 8) return 0;
            fc_open[nopen++] = fn_cused;
            i++; continue;
        }
        if (c == '}') {
            if (!nopen) return 0;
            nopen--;
            if (nopen > 0) {               /* an inner block closed: remember it */
                if (nb >= 8) return 0;
                fc_blk0[nb] = fc_open[nopen]; fc_blk1[nb] = fn_cused; nb++;
            }
            i++; continue;
        }
        h = i;
        while (i < len && !gx_isws((pdb)gx_tb[i]) && gx_tb[i] != '{' && gx_tb[i] != '}') i++;
        k = i - h;
        c = (pdb)gx_tb[h];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
            if (fn_cused + 5 > FN_CODE) return 0;
            if (c == '+') { h++; k--; }
            if (pdm_fx_parse(&v, gx_tb + h, k) != PDE_OK) { v.hi = 0; v.lo = 0; }
            fn_code[fn_cused++] = FC_PUSH;
            fn_code[fn_cused++] = (pdb)(v.lo & 255); fn_code[fn_cused++] = (pdb)(v.lo >> 8);
            fn_code[fn_cused++] = (pdb)((pdw)v.hi & 255); fn_code[fn_cused++] = (pdb)((pdw)v.hi >> 8);
            continue;
        }
        if (k == 2 && c == 'i' && gx_tb[h + 1] == 'f') {
            if (!nb) return 0;
            nb--;
            a0 = fc_blk0[nb]; a1 = fc_blk1[nb];
            if (!fc_ins(a0, 3)) return 0;
            fn_code[a0] = FC_JZ; fn_code[a0 + 1] = (pdb)((a1 - a0) & 255); fn_code[a0 + 2] = (pdb)((a1 - a0) >> 8);
            continue;
        }
        if (k == 6 && c == 'i' && gx_tb[h + 1] == 'f' && gx_tb[h + 2] == 'e') {
            if (nb < 2) return 0;
            nb -= 2;
            a0 = fc_blk0[nb]; a1 = fc_blk1[nb]; b0 = fc_blk0[nb + 1]; b1 = fc_blk1[nb + 1];
            (void)b0;
            /* A [a0,a1) then B [a1,b1): JZ over A+JMP, A, JMP over B, B */
            if (!fc_ins(a1, 3)) return 0;
            fn_code[a1] = FC_JMP; fn_code[a1 + 1] = (pdb)((b1 - a1) & 255); fn_code[a1 + 2] = (pdb)((b1 - a1) >> 8);
            if (!fc_ins(a0, 3)) return 0;
            fn_code[a0] = FC_JZ; fn_code[a0 + 1] = (pdb)((a1 - a0 + 3) & 255); fn_code[a0 + 2] = (pdb)((a1 - a0 + 3) >> 8);
            continue;
        }
        op = fc_find(gx_tb + h, k);
        if (op < 0 || fn_cused + 1 > FN_CODE) return 0;
        fn_code[fn_cused++] = (pdb)op;
    }
    if (nopen) return 0;
    return fn_cused - base;
}

/* ---- compile a function object ------------------------------------------- */
static void fn_fill_fx(pdw arr, struct pd_fx *dst, pdw max, pdw *count)
{
    *count = gx_nums(arr, dst, max);
}

static pdw fn_subnode[FN_MAX][8];       /* a type 3 row's sub-function nodes, until the driver compiles them */

/* one function, not its sub-functions; answers the table index or FNE_NONE */
static pdw fn_one(pdw node)
{
    pdw d, t, arr, f, i, onum, ogen, got;
    static pdw n, np;
    struct gxf *g;
    static struct pd_fx tmp[1];
    if (fn_n >= FN_MAX) return FNFAIL();
    d = gx_res(node);
    onum = gx_ro; ogen = gx_rg;
    if (gx_ty(d) != GN_DICT) return FNFAIL();
    t = gx_dres(d, "FunctionType");
    if (gx_ty(t) != GN_NUM) return FNFAIL();
    f = fn_n++;
    g = &fn_t[f];
    g->type = (pdb)gx_int(t, 9); g->ok = 0; g->nsub = 0; g->nrng = 0;
    g->soff = 0; g->slen = 0; g->coff = 0; g->clen = 0;
    arr = gx_dres(d, "Domain");
    np = 0; fn_fill_fx(arr, g->dom, 8, &np);
    if (np < 2) return FNFAIL();
    g->nin = (pdb)(np / 2);
    arr = gx_dres(d, "Range");
    fn_fill_fx(arr, g->rng, 16, &g->nrng);
    g->nout = (pdb)(g->nrng / 2);
    if (g->type == 2) {
        static pdw n0, n1;
        fn_fill_fx(gx_dres(d, "C0"), g->p1, 16, &n0);
        fn_fill_fx(gx_dres(d, "C1"), g->p2, 16, &n1);
        if (!n0) { n0 = 1; g->p1[0].hi = 0; g->p1[0].lo = 0; }
        if (!n1) { n1 = 1; g->p2[0].hi = 1; g->p2[0].lo = 0; }
        if (n1 < n0) n0 = n1;
        g->nout = (pdb)n0;
        gx_num(gx_dres(d, "N"), &tmp[0]);
        g->sub[0] = (pdw)tmp[0].hi; g->sub[1] = tmp[0].lo;      /* N, as hi:lo */
        g->ok = 1;
        return f;
    }
    if (g->type == 3) {
        static pdw bn, en;
        pdw fa = gx_dres(d, "Functions"), k, s;
        k = gx_alen(fa);
        if (!k || k > 8 || g->nin != 1) return FNFAIL();
        fn_fill_fx(gx_dres(d, "Bounds"), g->p1, 7, &bn);
        fn_fill_fx(gx_dres(d, "Encode"), g->p2, 16, &en);
        if (bn + 1 < k || en < 2 * k) return FNFAIL();
        g->nsub = (pdb)k;
        for (i = 0; i < k; i++) { fn_subnode[f][i] = gx_aget(fa, i); g->sub[i] = 0; }
        g->ok = 1;
        return f;
    }
    if (g->type == 0) {
        static pdw sz[4];
        pdw bps, tot, nbytes;   /* tot <= 4096 */
        static struct pd_fx sizes[4];
        if (g->nin > 4 || !g->nout || g->nout > 8) return FNFAIL();
        fn_fill_fx(gx_dres(d, "Size"), sizes, 4, &n);
        if (n < g->nin) return FNFAIL();
        bps = (pdw)gx_int(gx_dres(d, "BitsPerSample"), 8);
        if (bps < 1 || bps > 32) return FNFAIL();
        g->bps = (pdb)bps;
        tot = 1;
        for (i = 0; i < g->nin; i++) {
            sz[i] = (pdw)sizes[i].hi;
            if (!sz[i] || sz[i] > 4096) return FNFAIL();
            g->size[i] = sz[i];
            tot = (pdw)(tot * sz[i]);
            if (tot > 4096) return FNFAIL();
        }
        fn_fill_fx(gx_dres(d, "Encode"), g->p1, 8, &n);
        if (n < 2 * g->nin) for (i = 0; i < g->nin; i++) { g->p1[2 * i].hi = 0; g->p1[2 * i].lo = 0; g->p1[2 * i + 1].hi = (pdi)(sz[i] - 1); g->p1[2 * i + 1].lo = 0; }
        fn_fill_fx(gx_dres(d, "Decode"), g->p2, 16, &n);
        if (n < 2 * g->nout) for (i = 0; i < 2 * (pdw)g->nout; i++) fx_cp(&(g->p2[i]), &(g->rng[i]));
        { static struct pd_u32 u;
          pdm_u32_mul16(&u, (pdw)(tot * g->nout), (pdw)bps);
          if (u.hi >= 8) return FNFAIL();
          nbytes = (pdw)((u.hi << 13) | (u.lo >> 3)) + ((u.lo & 7) ? 1 : 0); }
        if (nbytes > FN_SAMAX - fn_sused) return FNFAIL();
        if (!onum || gx_sopen(onum, ogen) != PDE_OK) return FNFAIL();
        g->soff = fn_sused;
        got = 0;
        while (got < nbytes) {
            pdw r = gx_sread(fn_sam + fn_sused + got, nbytes - got);
            if (!r) break;
            got += r;
        }
        gx_sclose();
        for (i = got; i < nbytes; i++) fn_sam[fn_sused + i] = 0;
        g->slen = nbytes; fn_sused += nbytes;
        g->ok = 1;
        return f;
    }
    if (g->type == 4) {
        pdw len;
        if (!g->nout || g->nout > 8 || g->nin > 8) return FNFAIL();
        if (!onum || gx_sopen(onum, ogen) != PDE_OK) return FNFAIL();
        len = 0;
        while (len < GX_TB) {
            pdw r = gx_sread((pdb *)gx_tb + len, GX_TB - len);
            if (!r) break;
            len += r;
        }
        gx_sclose();
        g->coff = fn_cused;
        n = fc_compile(len);
        if (!n) { fn_cused = g->coff; return FNFAIL(); }
        g->clen = n; g->ok = 1;
        return f;
    }
    return FNFAIL();
}

/* the driver: a type 3 chain compiles by a loop over the rows it creates */
static pdw fn_compile(pdw node)
{
    pdw root, i, k, s;
    struct gxf *g;
    root = fn_one(node);
    if (root == FNE_NONE) return FNE_NONE;
    for (i = root; i < fn_n; i++) {
        g = &fn_t[i];
        if (g->type != 3) continue;
        for (k = 0; k < g->nsub; k++) {
            s = fn_one(fn_subnode[i][k]);
            if (s == FNE_NONE) { fn_n = root; return FNE_NONE; }
            fn_t[i].sub[k] = s;
        }
    }
    for (i = fn_n; i > root; i--) {
        g = &fn_t[i - 1];
        if (g->type == 3 && !g->nout) g->nout = fn_t[g->sub[0]].nout;
    }
    return root;
}

/* ---- evaluation ------------------------------------------------------------ */
static void fn_clip(struct pd_fx *x, const struct pd_fx *lo, const struct pd_fx *hi)
{
    if (fx_cmp(x, lo) < 0) fx_cp(&(*x), &(*lo));
    else if (fx_cmp(x, hi) > 0) fx_cp(&(*x), &(*hi));
}

/* the sample at linear index i (component j folded in) as 0..1 */
static void fn_sample(const struct gxf *g, pdw lin, struct pd_fx *r)
{
    static struct pd_u32 bit, q;
    pdw byte, sh, v, bps = g->bps, take, nb, got = 0, left;
    pdm_u32_mul16(&bit, lin, bps);           /* bit offset of the sample */
    byte = (pdw)((bit.hi << 13) | (bit.lo >> 3));
    sh = bit.lo & 7;
    v = 0; take = bps > 16 ? 16 : bps;
    left = take;
    /* read `take` bits starting at bit `sh` of byte `byte` */
    while (left) {
        pdw avail = 8 - sh, use = left < avail ? left : avail, b;
        b = g->soff + byte;
        b = b < FN_SAMAX && byte < g->slen ? fn_sam[b] : 0;
        b = (b >> (avail - use)) & ((1u << use) - 1u);
        v = (pdw)((v << use) | b);
        left -= use; sh = 0; byte++;
    }
    (void)got; (void)nb;
    if (take == 16) { r->hi = 0; r->lo = v; return; }
    {
        pdw mx = (pdw)((1u << take) - 1u);
        pdm_u32_mul16(&q, v, 65535u);
        v = pdm_u32_divmod16(&q, &q, mx), (void)v;
        r->hi = 0; r->lo = q.lo;
    }
}

static struct pd_fx fe_st[FN_STACK];
static pdb fe_bool[FN_STACK];
static struct pd_fx fe_tmp[FN_STACK];
static struct pd_fx fe_x[8];

static void fe_lerp(struct pd_fx *r, const struct pd_fx *a, const struct pd_fx *b, const struct pd_fx *f)
{
    static struct pd_fx t;
    fx_sub(&t, b, a); fx_mul(&t, &t, f); fx_add(r, a, &t);
}

static pdw fn_eval_sampled(const struct gxf *g, const struct pd_fx *in, struct pd_fx *out)
{
    static pdw idx[4], idx1[4];
    pdw i, j, nin = g->nin, nout = g->nout;
    static struct pd_fx e[4], fr[4], s0, s1, one, t, den;
    fx_i(&one, 1);
    for (i = 0; i < nin; i++) {
        static struct pd_fx x, d, num, mx;
        fx_cp(&(x), &(in[i]));
        fn_clip(&x, &g->dom[2 * i], &g->dom[2 * i + 1]);
        fx_sub(&d, &g->dom[2 * i + 1], &g->dom[2 * i]);
        fx_sub(&num, &x, &g->dom[2 * i]);
        fx_sub(&den, &g->p1[2 * i + 1], &g->p1[2 * i]);
        if (d.hi == 0 && d.lo == 0) { fx_cp(&(e[i]), &(g->p1[2 * i])); }
        else { fx_div(&t, &num, &d); fx_mul(&t, &t, &den); fx_add(&e[i], &g->p1[2 * i], &t); }
        fx_i(&mx, (pdi)(g->size[i] - 1));
        { static struct pd_fx z; fx_raw(&z, 0, 0); fn_clip(&e[i], &z, &mx); }
        idx[i] = (pdw)e[i].hi;
        fr[i].hi = 0; fr[i].lo = e[i].lo;
        idx1[i] = idx[i] + 1 < g->size[i] ? idx[i] + 1 : idx[i];
    }
    for (j = 0; j < nout; j++) {
        static struct pd_fx v;
        pdw lin;
        if (nin == 1) {
            fn_sample(g, idx[0] * nout + j, &s0);
            fn_sample(g, idx1[0] * nout + j, &s1);
            fe_lerp(&v, &s0, &s1, &fr[0]);
        } else if (nin == 2) {
            static struct pd_fx a, b, c, d2, m0, m1;
            pdw w0 = g->size[0];
            fn_sample(g, (idx[0] + w0 * idx[1]) * nout + j, &a);
            fn_sample(g, (idx1[0] + w0 * idx[1]) * nout + j, &b);
            fn_sample(g, (idx[0] + w0 * idx1[1]) * nout + j, &c);
            fn_sample(g, (idx1[0] + w0 * idx1[1]) * nout + j, &d2);
            fe_lerp(&m0, &a, &b, &fr[0]);
            fe_lerp(&m1, &c, &d2, &fr[0]);
            fe_lerp(&v, &m0, &m1, &fr[1]);
        } else {
            pdw mul = 1;
            lin = 0;
            for (i = 0; i < nin; i++) {
                pdw ix = idx[i];
                if (fr[i].lo >= 0x8000u && idx1[i] != idx[i]) ix = idx1[i];
                lin += mul * ix; mul *= g->size[i];
            }
            fn_sample(g, lin * nout + j, &v);
        }
        /* decode */
        fx_sub(&t, &g->p2[2 * j + 1], &g->p2[2 * j]);
        fx_mul(&t, &t, &v);
        fx_add(&out[j], &g->p2[2 * j], &t);
    }
    return nout;
}

static void fe_pop2(struct pd_fx *a, struct pd_fx *b, pdw *sp)
{
    fx_cp(&(*b), &(fe_st[--(*sp)])); fx_cp(&(*a), &(fe_st[--(*sp)]));
}
static int fe_isint(const struct pd_fx *a) { return a->lo == 0; }

/* one program run; the operand stack starts with the inputs */
static pdw fn_eval_ps(const struct gxf *g, const struct pd_fx *in, struct pd_fx *out)
{
    static pdw sp;
    pdw pc = g->coff, end = g->coff + g->clen, i, n2;
    static struct pd_fx a, b, r, one;
    int op;
    sp = 0;
    fx_i(&one, 1);
    for (i = 0; i < g->nin; i++) {
        static struct pd_fx x;
        fx_cp(&(x), &(in[i]));
        fn_clip(&x, &g->dom[2 * i], &g->dom[2 * i + 1]);
        fx_cp(&(fe_st[sp]), &(x)); fe_bool[sp] = 0; sp++;
    }
    while (pc < end) {
        op = fn_code[pc++];
        if (op == FC_PUSH) {
            if (sp >= FN_STACK) return 0;
            fe_st[sp].lo = (pdw)(fn_code[pc] | (fn_code[pc + 1] << 8));
            fe_st[sp].hi = (pdi)(pdw)(fn_code[pc + 2] | (fn_code[pc + 3] << 8));
            fe_bool[sp] = 0; sp++; pc += 4; continue;
        }
        if (op == FC_JZ || op == FC_JMP) {
            n2 = (pdw)(fn_code[pc] | (fn_code[pc + 1] << 8)); pc += 2;
            if (op == FC_JMP) { pc += n2; continue; }
            if (!sp) return 0;
            fx_cp(&(a), &(fe_st[--sp]));
            if (a.hi == 0 && a.lo == 0) pc += n2;
            continue;
        }
        switch (op) {
        case 10: if (!sp) return 0;                        /* abs */
            if (fe_st[sp - 1].hi < 0) { fx_raw(&a, 0, 0); fx_sub(&fe_st[sp - 1], &a, &fe_st[sp - 1]); } break;
        case 11: if (sp < 2) return 0; fe_pop2(&a, &b, &sp); fx_add(&r, &a, &b); fx_cp(&(fe_st[sp]), &(r)); fe_bool[sp++] = 0; break;
        case 12: case 40: case 49:                         /* and or xor */
            if (sp < 2) return 0;
            { pdb bl = fe_bool[sp - 1];
              fe_pop2(&a, &b, &sp);
              if (op == 12) { r.hi = a.hi & b.hi; r.lo = a.lo & b.lo; }
              else if (op == 40) { r.hi = a.hi | b.hi; r.lo = a.lo | b.lo; }
              else { r.hi = a.hi ^ b.hi; r.lo = a.lo ^ b.lo; }
              fx_cp(&(fe_st[sp]), &(r)); fe_bool[sp++] = bl; }
            break;
        case 13: if (sp < 2) return 0; fe_pop2(&a, &b, &sp); fx_atan2(&r, &a, &b); fx_cp(&(fe_st[sp]), &(r)); fe_bool[sp++] = 0; break;
        case 14: if (sp < 2) return 0; fe_pop2(&a, &b, &sp);
            { pdi sh = b.hi; fx_cp(&(r), &(a));
              if (sh > 0) { while (sh-- > 0) { r.hi = (pdi)((pdw)r.hi << 1); } }
              else { while (sh++ < 0) r.hi >>= 1; }
              r.lo = 0; }
            fx_cp(&(fe_st[sp]), &(r)); fe_bool[sp++] = 0; break;
        case 15: if (!sp) return 0; if (fe_st[sp - 1].lo) { fe_st[sp - 1].hi++; fe_st[sp - 1].lo = 0; } break;   /* ceiling */
        case 26: if (!sp) return 0; fe_st[sp - 1].lo = 0; break;                                                   /* floor */
        case 48: case 18: if (!sp) return 0;                                                                       /* truncate cvi */
            if (fe_st[sp - 1].hi < 0 && fe_st[sp - 1].lo) fe_st[sp - 1].hi++;
            fe_st[sp - 1].lo = 0; break;
        case 43: if (!sp) return 0; { pdi h = pdm_fx_round(&fe_st[sp - 1]); fx_i(&fe_st[sp - 1], h); } break;      /* round */
        case 19: break;                                                                                            /* cvr */
        case 16: if (!sp) return 0;                                                                                /* copy */
            { pdw nn = (pdw)fe_st[--sp].hi, k;
              if (nn > sp || sp + nn > FN_STACK) return 0;
              for (k = 0; k < nn; k++) { fx_cp(&(fe_st[sp + k]), &(fe_st[sp - nn + k])); fe_bool[sp + k] = fe_bool[sp - nn + k]; }
              sp += nn; }
            break;
        case 17: if (!sp) return 0; fx_cos(&fe_st[sp - 1], &fe_st[sp - 1]); fe_bool[sp - 1] = 0; break;
        case 44: if (!sp) return 0; fx_sin(&fe_st[sp - 1], &fe_st[sp - 1]); fe_bool[sp - 1] = 0; break;
        case 20: if (sp < 2) return 0; fe_pop2(&a, &b, &sp); fx_div(&r, &a, &b); fx_cp(&(fe_st[sp]), &(r)); fe_bool[sp++] = 0; break;
        case 21: if (!sp || sp >= FN_STACK) return 0; fx_cp(&(fe_st[sp]), &(fe_st[sp - 1])); fe_bool[sp] = fe_bool[sp - 1]; sp++; break;
        case 22: case 37: case 27: case 28: case 31: case 34:   /* eq ne ge gt le lt */
            if (sp < 2) return 0; fe_pop2(&a, &b, &sp);
            { pdi c = fx_cmp(&a, &b), v;
              v = op == 22 ? c == 0 : op == 37 ? c != 0 : op == 27 ? c >= 0 : op == 28 ? c > 0 : op == 31 ? c <= 0 : c < 0;
              fx_i(&r, v); fx_cp(&(fe_st[sp]), &(r)); fe_bool[sp++] = 1; }
            break;
        case 23: if (sp < 2) return 0; fx_cp(&(a), &(fe_st[sp - 1])); fx_cp(&(fe_st[sp - 1]), &(fe_st[sp - 2])); fx_cp(&(fe_st[sp - 2]), &(a));
            { pdb t = fe_bool[sp - 1]; fe_bool[sp - 1] = fe_bool[sp - 2]; fe_bool[sp - 2] = t; } break;
        case 24: if (sp < 2) return 0; fe_pop2(&a, &b, &sp); fx_pow(&r, &a, &b); fx_cp(&(fe_st[sp]), &(r)); fe_bool[sp++] = 0; break;
        case 25: if (sp >= FN_STACK) return 0; fx_i(&fe_st[sp], 0); fe_bool[sp++] = 1; break;
        case 47: if (sp >= FN_STACK) return 0; fx_i(&fe_st[sp], 1); fe_bool[sp++] = 1; break;
        case 29: case 35: if (sp < 2) return 0; fe_pop2(&a, &b, &sp);                           /* idiv mod */
            if (!b.hi) return 0;
            { pdi q = a.hi / b.hi, m = a.hi % b.hi; fx_i(&r, op == 29 ? q : m); }
            fx_cp(&(fe_st[sp]), &(r)); fe_bool[sp++] = 0; break;
        case 30: if (!sp) return 0; { pdw nn = (pdw)fe_st[--sp].hi; if (nn >= sp) return 0; fx_cp(&(fe_st[sp]), &(fe_st[sp - 1 - nn])); fe_bool[sp] = fe_bool[sp - 1 - nn]; sp++; } break;
        case 32: case 33: if (!sp) return 0;                                                     /* ln log */
            { static struct pd_fx l, k; fx_log2(&l, &fe_st[sp - 1]);
              if (op == 32) fx_raw(&k, 0, 0xB172); else fx_raw(&k, 0, 0x4D10);                    /* ln2, log10(2) */
              fx_mul(&fe_st[sp - 1], &l, &k); fe_bool[sp - 1] = 0; }
            break;
        case 36: if (sp < 2) return 0; fe_pop2(&a, &b, &sp); fx_mul(&r, &a, &b); fx_cp(&(fe_st[sp]), &(r)); fe_bool[sp++] = 0; break;
        case 38: if (!sp) return 0; fx_raw(&a, 0, 0); fx_sub(&fe_st[sp - 1], &a, &fe_st[sp - 1]); break;
        case 39: if (!sp) return 0;                                                              /* not */
            if (fe_bool[sp - 1]) { fx_i(&fe_st[sp - 1], fe_st[sp - 1].hi ? 0 : 1); }
            else { fe_st[sp - 1].hi = ~fe_st[sp - 1].hi; fe_st[sp - 1].lo = ~fe_st[sp - 1].lo; }
            break;
        case 41: if (!sp) return 0; sp--; break;
        case 42: if (sp < 2) return 0; fe_pop2(&a, &b, &sp);                                      /* roll */
            { pdi nn = a.hi, j = b.hi; pdw k;
              if (nn < 0 || (pdw)nn > sp) return 0;
              if (nn > 1) {
                  j %= nn; if (j < 0) j += nn;
                  for (k = 0; k < (pdw)nn; k++) fx_cp(&(fe_tmp[(k + (pdw)j) % (pdw)nn]), &(fe_st[sp - nn + k]));
                  for (k = 0; k < (pdw)nn; k++) fx_cp(&(fe_st[sp - nn + k]), &(fe_tmp[k]));
              } }
            break;
        case 45: if (!sp) return 0; fx_sqrt(&fe_st[sp - 1], &fe_st[sp - 1]); fe_bool[sp - 1] = 0; break;
        case 46: if (sp < 2) return 0; fe_pop2(&a, &b, &sp); fx_sub(&r, &a, &b); fx_cp(&(fe_st[sp]), &(r)); fe_bool[sp++] = 0; break;
        default: return 0;
        }
    }
    if (sp < g->nout) return 0;
    for (i = 0; i < g->nout; i++) fx_cp(&(out[i]), &(fe_st[sp - g->nout + i]));
    return g->nout;
}

/* Evaluate function f at in[0..nin). Answers the number of outputs, 0 on failure. */
static pdw fn_eval(pdw f, const struct pd_fx *in, struct pd_fx *out)
{
    const struct gxf *g;
    static struct pd_fx x, lo, hi, d, num, t;
    pdw depth = 0, i, k, n = 0;
    g = &fn_t[f];
    if (f >= fn_n || !g->ok) return 0;
    if (g->type == 3) {
        const struct gxf *top = g;
        fx_cp(&(x), &(in[0]));
        fn_clip(&x, &g->dom[0], &g->dom[1]);
        while (g->type == 3) {
            if (depth++ >= 8) return 0;
            k = 0;
            while (k + 1 < g->nsub && fx_cmp(&x, &g->p1[k]) >= 0) k++;
            if (k == 0) fx_cp(&lo, &g->dom[0]); else fx_cp(&lo, &g->p1[k - 1]);
            if (k + 1 == g->nsub) fx_cp(&hi, &g->dom[1]); else fx_cp(&hi, &g->p1[k]);
            fx_sub(&d, &hi, &lo);
            if (d.hi == 0 && d.lo == 0) fx_cp(&(x), &(g->p2[2 * k]));
            else {
                fx_sub(&num, &x, &lo); fx_div(&t, &num, &d);
                fx_sub(&num, &g->p2[2 * k + 1], &g->p2[2 * k]); fx_mul(&t, &t, &num);
                fx_add(&x, &g->p2[2 * k], &t);
            }
            g = &fn_t[g->sub[k]];
        }
        fx_cp(&(fe_x[0]), &(x));
        n = g->type == 0 ? fn_eval_sampled(g, fe_x, out) :
            g->type == 2 ? 0 : fn_eval_ps(g, fe_x, out);
        if (g->type == 2) {
            static struct pd_fx xx, pw;
            fx_cp(&(xx), &(x));
            fn_clip(&xx, &g->dom[0], &g->dom[1]);
            { static struct pd_fx nn; fx_raw(&nn, (pdi)g->sub[0], g->sub[1]); fx_pow(&pw, &xx, &nn); }
            n = g->nout;
            for (i = 0; i < n; i++) { fx_sub(&t, &g->p2[i], &g->p1[i]); fx_mul(&t, &t, &pw); fx_add(&out[i], &g->p1[i], &t); }
        }
        g = top;
    } else if (g->type == 0) n = fn_eval_sampled(g, in, out);
    else if (g->type == 4) n = fn_eval_ps(g, in, out);
    else if (g->type == 2) {
        static struct pd_fx xx, pw, nn;
        fx_cp(&(xx), &(in[0]));
        fn_clip(&xx, &g->dom[0], &g->dom[1]);
        fx_raw(&nn, (pdi)g->sub[0], g->sub[1]);
        fx_pow(&pw, &xx, &nn);
        n = g->nout;
        for (i = 0; i < n; i++) { fx_sub(&t, &g->p2[i], &g->p1[i]); fx_mul(&t, &t, &pw); fx_add(&out[i], &g->p1[i], &t); }
    }
    if (!n) return 0;
    if (g->nrng)
        for (i = 0; i < n && 2 * i + 1 < g->nrng; i++) fn_clip(&out[i], &g->rng[2 * i], &g->rng[2 * i + 1]);
    return n;
}
