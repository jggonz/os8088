/* ============================================================================
 * os8088 - apps/pagedesk/pdxgc.c    colour spaces (20.22.9)
 *
 * A space compiles into a CHAIN of rows (Indexed, Separation and DeviceN each
 * name one base or alternate, so the chain is a list and no routine recurses);
 * cs_conv() walks it. #included by pdxg.c after pdxgf.c.
 * ==========================================================================*/
#define GC_GRAY    1
#define GC_RGB     2
#define GC_CMYK    3
#define GC_CALGRAY 4
#define GC_CALRGB  5
#define GC_LAB     6
#define GC_IDX     7
#define GC_SEP     8
#define GC_DEVN    9
#define GC_PAT     10
#define GCF_NONE   1             /* Separation /None: paints nothing          */
#define CS_ROWS    5
#define CS_TAB     1032

struct gxcs {
    pdb kind, n, flags, pad;
    pdw fn, hival, tab, tablen, nextnode;
    struct pd_fx p[15];          /* wp 0-2; gamma 3-5; matrix 6-14; Lab range 3-6 */
};
static struct gxcs cs_t[CS_ROWS];
static pdw cs_n;
static pdb cs_tab[CS_TAB];
static pdw cs_tused;

static void cs_wp(struct gxcs *g, pdw d)
{
    pdw n;
    n = gx_nums(gx_dres(d, "WhitePoint"), g->p, 3);
    if (n < 3) { fx_raw(&g->p[0], 0, 0xF6D6); fx_i(&g->p[1], 1); fx_raw(&g->p[2], 0, 0xD32C); }
}

/* One row from a node; sets the row's nextnode when it has a base. Answers 0 ok. */
static pdw cs_one(pdw node, pdw first)
{
    struct gxcs *g;
    pdw d, a, nm, t;
    pdw k, got;
    if (cs_n >= CS_ROWS) return 1;
    g = &cs_t[cs_n];
    g->kind = 0; g->n = 0; g->flags = 0; g->fn = FNE_NONE; g->hival = 0; g->tab = 0; g->tablen = 0;
    g->nextnode = GN_NIL;
    node = gx_res(node);
    if (node == GN_NIL) return 1;
    if (gx_ty(node) == GN_NAME) {
        nm = node;
        a = GN_NIL;
    } else if (gx_ty(node) == GN_ARR) {
        a = node;
        nm = gx_ares(a, 0);
    } else return 1;
    if (gx_nameis(nm, "DeviceGray") || gx_nameis(nm, "G") || gx_nameis(nm, "CalGray") && a == GN_NIL) { g->kind = GC_GRAY; g->n = 1; }
    else if (gx_nameis(nm, "DeviceRGB") || gx_nameis(nm, "RGB")) { g->kind = GC_RGB; g->n = 3; }
    else if (gx_nameis(nm, "DeviceCMYK") || gx_nameis(nm, "CMYK")) { g->kind = GC_CMYK; g->n = 4; }
    else if (gx_nameis(nm, "Pattern")) {
        g->kind = GC_PAT; g->n = 0;
        if (gx_alen(a) > 1) g->nextnode = gx_aget(a, 1);
    }
    else if (gx_nameis(nm, "Indexed") || gx_nameis(nm, "I")) {
        pdw lk;
        if (gx_alen(a) < 4) return 1;
        g->kind = GC_IDX; g->n = 1;
        g->hival = (pdw)gx_int(gx_ares(a, 2), 0);
        if (g->hival > 255) g->hival = 255;
        g->nextnode = gx_aget(a, 1);
        lk = gx_ares(a, 3);
        g->tab = cs_tused;
        if (gx_ty(lk) == GN_STR) {
            got = gx_n[lk].b;
            if (got > CS_TAB - cs_tused) got = CS_TAB - cs_tused;
            for (k = 0; k < got; k++) cs_tab[cs_tused + k] = (pdb)gx_heap[gx_n[lk].a + k];
        } else if (gx_ty(lk) == GN_DICT && gx_ro && gx_sopen(gx_ro, gx_rg) == PDE_OK) {
            got = 0;
            while (got < CS_TAB - cs_tused) {
                pdw r = gx_sread(cs_tab + cs_tused + got, CS_TAB - cs_tused - got);
                if (!r) break;
                got += r;
            }
            gx_sclose();
        } else return 1;
        g->tablen = got; cs_tused += got;
    }
    else if (gx_nameis(nm, "ICCBased")) {
        pdw nn, dd = gx_ares(a, 1);
        nn = (pdw)gx_int(gx_dres(dd, "N"), 0);
        if (nn == 1) { g->kind = GC_GRAY; g->n = 1; }
        else if (nn == 3) { g->kind = GC_RGB; g->n = 3; }
        else if (nn == 4) { g->kind = GC_CMYK; g->n = 4; }
        else return 1;
        g->flags = 0;
    }
    else if (gx_nameis(nm, "CalGray")) {
        d = gx_ares(a, 1);
        g->kind = GC_CALGRAY; g->n = 1;
        cs_wp(g, d);
        { static struct pd_fx gm; fx_i(&gm, 1); gx_num(gx_dres(d, "Gamma"), &gm); if (!gm.hi && !gm.lo) fx_i(&gm, 1); fx_cp(&(g->p[3]), &(gm)); }
    }
    else if (gx_nameis(nm, "CalRGB")) {
        pdw ng, nm9;
        d = gx_ares(a, 1);
        g->kind = GC_CALRGB; g->n = 3;
        cs_wp(g, d);
        ng = gx_nums(gx_dres(d, "Gamma"), g->p + 3, 3);
        if (ng < 3) { fx_i(&g->p[3], 1); fx_i(&g->p[4], 1); fx_i(&g->p[5], 1); }
        nm9 = gx_nums(gx_dres(d, "Matrix"), g->p + 6, 9);
        if (nm9 < 9) {
            for (k = 0; k < 9; k++) fx_i(&g->p[6 + k], (k % 4 == 0) ? 1 : 0);
            g->flags = 2;                          /* no matrix: treat as device RGB */
        }
    }
    else if (gx_nameis(nm, "Lab")) {
        pdw nr;
        d = gx_ares(a, 1);
        g->kind = GC_LAB; g->n = 3;
        cs_wp(g, d);
        nr = gx_nums(gx_dres(d, "Range"), g->p + 3, 4);
        if (nr < 4) { fx_i(&g->p[3], -100); fx_i(&g->p[4], 100); fx_i(&g->p[5], -100); fx_i(&g->p[6], 100); }
    }
    else if (gx_nameis(nm, "Separation") || gx_nameis(nm, "DeviceN")) {
        pdw names;
        t = gx_nameis(nm, "Separation");
        if (gx_alen(a) < 4) return 1;
        g->kind = t ? GC_SEP : GC_DEVN;
        if (t) {
            g->n = 1;
            names = gx_ares(a, 1);
            if (gx_nameis(names, "None")) g->flags |= GCF_NONE;
        } else {
            names = gx_ares(a, 1);
            k = gx_alen(names);
            if (!k || k > 8) return 1;
            g->n = (pdb)k;
        }
        g->nextnode = gx_aget(a, 2);
        g->fn = fn_compile(gx_aget(a, 3));
        if (g->fn == FNE_NONE) return 1;
    }
    else return 1;
    (void)first;
    cs_n++;
    return 0;
}

/* the whole chain; 0 on success */
static pdw cs_compile(pdw node)
{
    pdw i, next;
    cs_n = 0; cs_tused = 0;
    if (cs_one(node, 1)) return 1;
    i = 0;
    for (;;) {
        next = cs_t[i].nextnode;
        if (next == GN_NIL) break;
        if (cs_one(next, 0)) return 1;
        i++;
    }
    return 0;
}

/* ---- conversion ------------------------------------------------------------ */
static void cs_xyz_rgb(const struct gxcs *g, const struct pd_fx *xyz, struct pd_fx *rgb)
{
    /* scale to D65 from the white point, then the linear sRGB matrix and the encode */
    static const struct pd_fx m[9] = {
        { 0x3D8E, 3 },
        { 0x767E, -2 },
        { 0x8060, -1 },
        { 0x07DE, -1 },
        { 0xE042, 1 },
        { 0x0AA3, 0 },
        { 0x0E3F, 0 },
        { 0xCBC5, -1 },
        { 0x0EA6, 1 } };
    static const struct pd_fx d65[3] = { { 0xF352, 0 }, { 0x0000, 1 }, { 0x16BE, 1 } };
    static struct pd_fx s[3], t, u;
    pdw i, j;
    for (i = 0; i < 3; i++) {
        fx_mul(&t, &xyz[i], &d65[i]);
        fx_div(&s[i], &t, &g->p[i]);
    }
    for (i = 0; i < 3; i++) {
        fx_raw(&u, 0, 0);
        for (j = 0; j < 3; j++) { fx_mul(&t, &m[i * 3 + j], &s[j]); fx_add(&u, &u, &t); }
        fx_senc(&rgb[i], &u);
    }
}

static void cs_cmyk(const struct pd_fx *v, struct pd_fx *rgb)
{
    static struct pd_fx one, ik, t;
    pdw i;
    fx_i(&one, 1);
    fx_sub(&ik, &one, &v[3]);
    for (i = 0; i < 3; i++) {
        fx_sub(&t, &one, &v[i]);
        fx_mul(&rgb[i], &t, &ik);
    }
}

static pdw cs_cmp_n(pdw row) { return cs_t[row].n; }

/* comps (in the chain's first row's space, nv of them) to rgb 0..1. Answers 1
 * when the colour paints nothing (Separation /None). */
static struct pd_fx cv_v[8];
static struct pd_fx cv_o[8];
static pdw cs_conv(pdw row, const struct pd_fx *comps, struct pd_fx *rgb)
{
    struct gxcs *g;
    pdw i, n = cs_cmp_n(row), nb;
    static struct pd_fx t, z;
    for (i = 0; i < n && i < 8; i++) fx_cp(&(cv_v[i]), &(comps[i]));
    for (;;) {
        g = &cs_t[row];
        if (g->kind == GC_IDX) {
            pdw ix = (pdw)pdm_fx_round(&cv_v[0]), j;
            struct gxcs *b = &cs_t[row + 1];
            if (ix > g->hival) ix = g->hival;
            nb = b->n;
            for (j = 0; j < nb; j++) {
                pdw p = ix * nb + j, byte = p < g->tablen ? cs_tab[g->tab + p] : 0;
                if (b->kind == GC_LAB) {
                    if (j == 0) { fx_raw(&t, 100, 0); fx_raw(&z, 0, (pdw)((byte << 8) + byte)); fx_mul(&cv_o[0], &t, &z); }
                    else {
                        fx_sub(&t, &b->p[3 + 2 * (j - 1) + 1], &b->p[3 + 2 * (j - 1)]);
                        fx_raw(&z, 0, (pdw)((byte << 8) + byte));
                        fx_mul(&t, &t, &z);
                        fx_add(&cv_o[j], &b->p[3 + 2 * (j - 1)], &t);
                    }
                } else { fx_raw(&cv_o[j], 0, (pdw)((byte << 8) + byte)); }
            }
            for (j = 0; j < nb; j++) fx_cp(&(cv_v[j]), &(cv_o[j]));
            row++; continue;
        }
        if (g->kind == GC_SEP || g->kind == GC_DEVN) {
            if (g->flags & GCF_NONE) return 1;
            nb = fn_eval(g->fn, cv_v, cv_o);
            if (!nb) { for (i = 0; i < 3; i++) fx_raw(&rgb[i], 0, 0); return 0; }
            for (i = 0; i < nb; i++) fx_cp(&(cv_v[i]), &(cv_o[i]));
            row++; continue;
        }
        break;
    }
    g = &cs_t[row];
    for (i = 0; i < 4; i++) { if (i < 8 && g->kind != GC_LAB) fx_clamp01(&cv_v[i]); }
    switch (g->kind) {
    case GC_GRAY: fx_cp(&rgb[0], &cv_v[0]); fx_cp(&rgb[1], &cv_v[0]); fx_cp(&rgb[2], &cv_v[0]); break;
    case GC_RGB: fx_cp(&(rgb[0]), &(cv_v[0])); fx_cp(&(rgb[1]), &(cv_v[1])); fx_cp(&(rgb[2]), &(cv_v[2])); break;
    case GC_CMYK: cs_cmyk(cv_v, rgb); break;
    case GC_CALGRAY: {
        static struct pd_fx l;
        fx_pow(&l, &cv_v[0], &g->p[3]);
        fx_senc(&rgb[0], &l);
        fx_cp(&rgb[1], &rgb[0]); fx_cp(&rgb[2], &rgb[0]);
        break; }
    case GC_CALRGB: {
        static struct pd_fx lin[3], xyz[3];
        pdw j;
        if (g->flags & 2) { fx_cp(&(rgb[0]), &(cv_v[0])); fx_cp(&(rgb[1]), &(cv_v[1])); fx_cp(&(rgb[2]), &(cv_v[2])); break; }
        for (i = 0; i < 3; i++) fx_pow(&lin[i], &cv_v[i], &g->p[3 + i]);
        for (i = 0; i < 3; i++) {          /* the matrix is column-major in PDF: [XA YA ZA XB YB ZB XC YC ZC] */
            fx_raw(&xyz[i], 0, 0);
            for (j = 0; j < 3; j++) { fx_mul(&t, &g->p[6 + 3 * j + i], &lin[j]); fx_add(&xyz[i], &xyz[i], &t); }
        }
        cs_xyz_rgb(g, xyz, rgb);
        break; }
    case GC_LAB: {
        static struct pd_fx f[3], xyz[3], c16, c116, c500, c200, u;
        /* clamp a, b to the range */
        fn_clip(&cv_v[1], &g->p[3], &g->p[4]);
        fn_clip(&cv_v[2], &g->p[5], &g->p[6]);
        fx_raw(&z, 0, 0); fx_raw(&t, 100, 0); fn_clip(&cv_v[0], &z, &t);
        fx_i(&c16, 16); fx_i(&c116, 116); fx_i(&c500, 500); fx_i(&c200, 200);
        fx_add(&u, &cv_v[0], &c16); fx_div(&f[1], &u, &c116);
        fx_div(&t, &cv_v[1], &c500); fx_add(&f[0], &f[1], &t);
        fx_div(&t, &cv_v[2], &c200); fx_sub(&f[2], &f[1], &t);
        for (i = 0; i < 3; i++) {
            static struct pd_fx th, q;
            fx_raw(&th, 0, 0x34F7);                 /* 6/29 */
            if (fx_cmp(&f[i], &th) > 0) { fx_mul(&q, &f[i], &f[i]); fx_mul(&q, &q, &f[i]); }
            else {
                static struct pd_fx k, c4;
                fx_raw(&k, 0, 0x20E0);              /* 3 * (6/29)^2 */
                fx_raw(&c4, 0, 0x234F);             /* 4/29 */
                fx_sub(&q, &f[i], &c4); fx_mul(&q, &q, &k);
            }
            fx_mul(&xyz[i], &q, &g->p[i]);
        }
        cs_xyz_rgb(g, xyz, rgb);
        break; }
    default: for (i = 0; i < 3; i++) fx_raw(&rgb[i], 0, 0);
    }
    for (i = 0; i < 3; i++) fx_clamp01(&rgb[i]);
    return 0;
}

/* the first usable row: the chain's own row, or a Pattern's underlying space */
static pdw cs_first(void) { return cs_t[0].kind == GC_PAT ? 1 : 0; }
