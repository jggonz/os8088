/* ============================================================================
 * os8088 - apps/pagedesk/pdxgs.c    shading types 1-7 (20.22.9)
 *
 * Types 2 and 3 compile to a 256-entry RAMP of (luminance, VGA index); types
 * 1, 4, 5, 6 and 7 are tessellated into Gouraud triangles of
 * (x, y: 16.16 shading space; luminance; VGA index). The core rasterises both
 * (pdrxg.c). #included by pdxg.c after pdxgc.c. No recursion; every static.
 * ==========================================================================*/
#define SH_MAXTRI 4096u
#define SH_VTX    10            /* x y (16.16), lum, c4                        */
#define SH_TRI    30

static struct pdgx_shade R_sh;
static pdw sh_cs_row;           /* the shading's colour space (first usable row) */
static pdw sh_ncomp;
static pdw sh_f0, sh_nf;        /* the shading's function(s) in fn_t: first, count */

/* ---- a colour to (lum, c4) --------------------------------------------------- */
static pdb sh_c4(pdw r, pdw g, pdw b)
{
    pdw mx = r, mn = r, lum, idx;
    if (g > mx) mx = g;
    if (b > mx) mx = b;
    if (g < mn) mn = g;
    if (b < mn) mn = b;
    lum = (pdw)((r * 77u + g * 150u + b * 29u) >> 8);
    if (mx - mn < 40) {
        if (lum < 48) return 0;
        if (lum < 128) return 8;
        if (lum < 208) return 7;
        return 15;
    }
    idx = 0;
    if (r * 2 >= mx) idx |= 4;
    if (g * 2 >= mx) idx |= 2;
    if (b * 2 >= mx) idx |= 1;
    if (mx >= 192) idx |= 8;
    return (pdb)idx;
}
static pdb sh_lum, sh_cc4;
/* comps of the shading's space -> sh_lum / sh_cc4 */
static void sh_color(const struct pd_fx *comps)
{
    static struct pd_fx rgb[3];
    pdw r, g, b;
    if (cs_conv(sh_cs_row, comps, rgb)) { sh_lum = 255; sh_cc4 = 15; return; }
    r = fx_to255(&rgb[0]); g = fx_to255(&rgb[1]); b = fx_to255(&rgb[2]);
    sh_lum = (pdb)((r * 77u + g * 150u + b * 29u) >> 8);
    sh_cc4 = sh_c4(r, g, b);
}

/* the shading function(s) at `in` (nin inputs) -> comps (sh_ncomp of them) */
static struct pd_fx sh_cv[8];
static pdw sh_fn_at(const struct pd_fx *in)
{
    static struct pd_fx o[8];
    pdw i, n;
    if (sh_nf == 1) {
        n = fn_eval(sh_f0, in, sh_cv);
        return n;
    }
    for (i = 0; i < sh_nf && i < 8; i++) {
        n = fn_eval(sh_f0 + i, in, o);
        if (!n) return 0;
        fx_cp(&sh_cv[i], &o[0]);
    }
    return sh_nf;
}

/* compile "Function": a function or an array of functions; true on success */
static pdw sh_functions(pdw d)
{
    pdw fn = gx_dget(d, "Function"), r, i, k;
    pdw res = gx_res(fn);
    sh_f0 = 0; sh_nf = 0;
    if (res == GN_NIL) return 0;
    if (gx_ty(res) == GN_ARR) {
        k = gx_alen(res);
        if (!k || k > 8) return 0;
        sh_f0 = fn_n;
        for (i = 0; i < k; i++) {
            r = fn_compile(gx_aget(res, i));
            if (r == FNE_NONE) return 0;
            if (i == 0) sh_f0 = r;
        }
        sh_nf = k;
        return 1;
    }
    r = fn_compile(fn);
    if (r == FNE_NONE) return 0;
    sh_f0 = r; sh_nf = 1;
    return 1;
}

/* ---- the header every type shares -------------------------------------------- */
static pdw sh_header(pdw d)
{
    static struct pd_fx bb[4], bg[8];
    pdw n, i, cs;
    for (i = 0; i < sizeof R_sh; i++) ((pdb *)&R_sh)[i] = 0;
    cs = gx_dres(d, "ColorSpace");
    if (cs == GN_NIL) return 0;
    {
        /* cs may be a name or an array: cs_compile takes a node */
        if (cs_compile(gx_dget(d, "ColorSpace"))) return 0;
    }
    sh_cs_row = cs_first();
    sh_ncomp = cs_t[sh_cs_row].n;
    if (!sh_ncomp || sh_ncomp > 8) return 0;
    n = gx_nums(gx_dres(d, "BBox"), bb, 4);
    if (n == 4) {
        R_sh.bbox_ok = 1;
        for (i = 0; i < 4; i++) fx_cp(&R_sh.bbox[i], &bb[i]);
    }
    n = gx_nums(gx_dres(d, "Background"), bg, 8);
    if (n >= sh_ncomp) {
        sh_color(bg);
        R_sh.bg_ok = 1; R_sh.bg_lum = sh_lum; R_sh.bg_c4 = sh_cc4;
    }
    return 1;
}

/* ---- types 2 and 3 ----------------------------------------------------------- */
static pdw sh_axial_radial(pdw d, pdw type)
{
    static struct pd_fx co[6], dom[2], one, t, f, tt;
    pdw n, i, ex, k, nc = type == 2 ? 4 : 6;
    n = gx_nums(gx_dres(d, "Coords"), co, 6);
    if (n < nc) return 0;
    for (i = 0; i < nc; i++) fx_cp(&R_sh.coords[i], &co[i]);
    fx_raw(&dom[0], 0, 0); fx_i(&dom[1], 1);
    gx_nums(gx_dres(d, "Domain"), dom, 2);
    fx_cp(&R_sh.t0, &dom[0]); fx_cp(&R_sh.t1, &dom[1]);
    ex = gx_dres(d, "Extend");
    if (gx_ty(ex) == GN_ARR) {
        if (gx_ty(gx_ares(ex, 0)) == GN_BOOL && GN_A(gx_ares(ex, 0))) R_sh.flags |= PDGX_SH_EXT0;
        if (gx_ty(gx_ares(ex, 1)) == GN_BOOL && GN_A(gx_ares(ex, 1))) R_sh.flags |= PDGX_SH_EXT1;
    }
    if (!sh_functions(d)) return 0;
    R_sh.type = (pdb)type; R_sh.nramp = 256;
    fx_i(&one, 1);
    for (k = 0; k < 256; k++) {
        /* t = t0 + (t1 - t0) * k / 255 */
        if (k == 255) fx_cp(&f, &one); else fx_raw(&f, 0, (pdw)((k << 8) + k));
        fx_sub(&tt, &dom[1], &dom[0]); fx_mul(&tt, &tt, &f); fx_add(&t, &dom[0], &tt);
        if (!sh_fn_at(&t)) { R_sh.ramp[k * 2] = 0; R_sh.ramp[k * 2 + 1] = 0; continue; }
        sh_color(sh_cv);
        R_sh.ramp[k * 2] = sh_lum; R_sh.ramp[k * 2 + 1] = sh_cc4;
    }
    return 1;
}

/* ---- triangles ----------------------------------------------------------------- */
static pdw sh_total, sh_skip, sh_out, sh_cap, sh_full;
static pdb sh_tb[SH_TRI];
struct shv { pdb lum, c4; struct pd_fx x, y; };
static struct shv sh_v[3];
static void shv_cp(struct shv *d, const struct shv *s) { d->lum = s->lum; d->c4 = s->c4; fx_cp(&d->x, &s->x); fx_cp(&d->y, &s->y); }
static pdw sh_off(pdw n) { return (pdw)((n << 5) - (n << 1)); }   /* n * SH_TRI */

static void sh_put(pdw o, const struct shv *v)
{
    sh_tb[o] = (pdb)(v->x.lo & 255); sh_tb[o + 1] = (pdb)(v->x.lo >> 8);
    sh_tb[o + 2] = (pdb)((pdw)v->x.hi & 255); sh_tb[o + 3] = (pdb)((pdw)v->x.hi >> 8);
    sh_tb[o + 4] = (pdb)(v->y.lo & 255); sh_tb[o + 5] = (pdb)(v->y.lo >> 8);
    sh_tb[o + 6] = (pdb)((pdw)v->y.hi & 255); sh_tb[o + 7] = (pdb)((pdw)v->y.hi >> 8);
    sh_tb[o + 8] = v->lum; sh_tb[o + 9] = v->c4;
}
/* one triangle from sh_v[0..2] */
static void sh_tri(void)
{
    if (sh_full) return;
    if (sh_total >= SH_MAXTRI) { sh_full = 2; return; }
    if (sh_total < sh_skip) { sh_total++; return; }
    if (sh_off(sh_out + 1) > sh_cap) { sh_full = 1; return; }
    sh_put(0, &sh_v[0]); sh_put(10, &sh_v[1]); sh_put(20, &sh_v[2]);
    pd_fcopy_out(sh_tb, G.rseg, (pdw)(G.roff + PDGX_SHBYTES + sh_off(sh_out)), SH_TRI);
    sh_out++; sh_total++;
}

/* ---- type 1: a grid of the domain --------------------------------------------- */
#define SH_GRID 8
#define grid ((struct shv (*)[SH_GRID + 1])gx_ov)
static pdw sh_function_based(pdw d)
{
    static struct pd_fx dom[4], mat[6], one, fa, fb, in[2], px, py, tx, ty, tmp;
    pdw n, i, j, k;
    fx_raw(&dom[0], 0, 0); fx_i(&dom[1], 1); fx_raw(&dom[2], 0, 0); fx_i(&dom[3], 1);
    gx_nums(gx_dres(d, "Domain"), dom, 4);
    for (i = 0; i < 6; i++) fx_i(&mat[i], (i == 0 || i == 3) ? 1 : 0);
    n = gx_nums(gx_dres(d, "Matrix"), mat, 6); (void)n;
    if (!sh_functions(d)) return 0;
    R_sh.type = 1; R_sh.flags |= PDGX_SH_MESH;
    for (j = 0; j <= SH_GRID; j++)
        for (i = 0; i <= SH_GRID; i++) {
            /* x = x0 + (x1 - x0) * i / SH_GRID */
            fx_i(&tmp, (pdi)i); fx_i(&fa, SH_GRID); fx_div(&fb, &tmp, &fa);
            fx_sub(&tx, &dom[1], &dom[0]); fx_mul(&tx, &tx, &fb); fx_add(&in[0], &dom[0], &tx);
            fx_i(&tmp, (pdi)j); fx_div(&fb, &tmp, &fa);
            fx_sub(&ty, &dom[3], &dom[2]); fx_mul(&ty, &ty, &fb); fx_add(&in[1], &dom[2], &ty);
            /* the matrix: [a b c d e f] -> x' = a x + c y + e, y' = b x + d y + f */
            fx_mul(&px, &mat[0], &in[0]); fx_mul(&tmp, &mat[2], &in[1]); fx_add(&px, &px, &tmp); fx_add(&px, &px, &mat[4]);
            fx_mul(&py, &mat[1], &in[0]); fx_mul(&tmp, &mat[3], &in[1]); fx_add(&py, &py, &tmp); fx_add(&py, &py, &mat[5]);
            fx_cp(&grid[j][i].x, &px); fx_cp(&grid[j][i].y, &py);
            if (!sh_fn_at(in)) { grid[j][i].lum = 255; grid[j][i].c4 = 15; }
            else { sh_color(sh_cv); grid[j][i].lum = sh_lum; grid[j][i].c4 = sh_cc4; }
        }
    (void)one; (void)k;
    for (j = 0; j < SH_GRID; j++)
        for (i = 0; i < SH_GRID; i++) {
            shv_cp(&sh_v[0], &grid[j][i]); shv_cp(&sh_v[1], &grid[j][i + 1]); shv_cp(&sh_v[2], &grid[j + 1][i]); sh_tri();
            shv_cp(&sh_v[0], &grid[j][i + 1]); shv_cp(&sh_v[1], &grid[j + 1][i + 1]); shv_cp(&sh_v[2], &grid[j + 1][i]); sh_tri();
        }
    return 1;
}

/* ---- the mesh bit reader ------------------------------------------------------- */
static pdb sb_buf[512];
static pdw sb_len, sb_pos, sb_bit, sb_eof;
static int sb_getbit(void)
{
    int b;
    if (sb_pos >= sb_len) {
        sb_len = gx_sread(sb_buf, 512); sb_pos = 0;
        if (!sb_len) { sb_eof = 1; return 0; }
    }
    b = (sb_buf[sb_pos] >> (7 - sb_bit)) & 1;
    if (++sb_bit == 8) { sb_bit = 0; sb_pos++; }
    return b;
}
static void sb_align(void) { if (sb_bit) { sb_bit = 0; sb_pos++; } }
static pdw sb_int(pdw n)            /* up to 16 significant bits; the rest skipped */
{
    pdw v = 0, i, m = n > 16 ? 16 : n;
    for (i = 0; i < n; i++) { int b = sb_getbit(); if (i < m) v = (pdw)((v << 1) | (pdw)b); }
    return v;
}
/* n bits -> 0..1 */
static void sb_norm(pdw n, struct pd_fx *r)
{
    pdw m = n > 16 ? 16 : n, v = sb_int(n);
    static struct pd_u32 q;
    if (m == 16) { r->hi = 0; r->lo = v; return; }
    pdm_u32_mul16(&q, v, 65535u);
    pdm_u32_divmod16(&q, &q, (pdw)((1u << m) - 1u));
    r->hi = 0; r->lo = q.lo;
}

static pdw sm_bpc, sm_bpcomp, sm_bpf, sm_ncv;       /* BitsPerCoordinate/Component/Flag, colour values a vertex */
static struct pd_fx sm_dec[16];
static pdw sm_hasfn;

/* decode raw 0..1 -> dec[2i] + raw * (dec[2i+1] - dec[2i]) */
static void sm_map(pdw i, const struct pd_fx *raw, struct pd_fx *r)
{
    static struct pd_fx t;
    fx_sub(&t, &sm_dec[2 * i + 1], &sm_dec[2 * i]); fx_mul(&t, &t, raw); fx_add(r, &sm_dec[2 * i], &t);
}

struct smv { struct pd_fx x, y; struct pd_fx c[4]; };
static struct smv sm_a, sm_b, sm_c, sm_cur;          /* the triangle machine's last vertices */

/* read a vertex's x, y (flag read by the caller) and colour values */
static void sm_vertex(struct smv *v)
{
    static struct pd_fx raw;
    pdw i;
    sb_norm(sm_bpc, &raw); sm_map(0, &raw, &v->x);
    sb_norm(sm_bpc, &raw); sm_map(1, &raw, &v->y);
    for (i = 0; i < sm_ncv; i++) { sb_norm(sm_bpcomp, &raw); sm_map(2 + i, &raw, &v->c[i]); }
}
/* the colour of a vertex: through the function, or directly */
static void sm_color(const struct smv *v, struct shv *o)
{
    if (sm_hasfn) { if (sh_fn_at(v->c)) sh_color(sh_cv); else { sh_lum = 255; sh_cc4 = 15; } }
    else sh_color(v->c);
    o->lum = sh_lum; o->c4 = sh_cc4;
}
static void sm_emit3(const struct smv *a, const struct smv *b, const struct smv *c)
{
    fx_cp(&sh_v[0].x, &a->x); fx_cp(&sh_v[0].y, &a->y); sm_color(a, &sh_v[0]);
    fx_cp(&sh_v[1].x, &b->x); fx_cp(&sh_v[1].y, &b->y); sm_color(b, &sh_v[1]);
    fx_cp(&sh_v[2].x, &c->x); fx_cp(&sh_v[2].y, &c->y); sm_color(c, &sh_v[2]);
    sh_tri();
}
static void sm_cp(struct smv *d, const struct smv *s)
{
    pdw i;
    fx_cp(&d->x, &s->x); fx_cp(&d->y, &s->y);
    for (i = 0; i < 4; i++) fx_cp(&d->c[i], &s->c[i]);
}

/* ---- type 4: free-form triangles ------------------------------------------------ */
static pdw sh_freeform(void)
{
    pdw flag, state = 0;
    while (!sb_eof && !sh_full) {
        flag = sb_int(sm_bpf);
        sm_vertex(&sm_cur); sb_align();
        if (sb_eof) break;
        if (state == 0 || flag == 0) {
            /* a new triangle: this vertex and the next two (their flags are ignored) */
            sm_cp(&sm_a, &sm_cur);
            sb_int(sm_bpf); sm_vertex(&sm_b); sb_align();
            sb_int(sm_bpf); sm_vertex(&sm_c); sb_align();
            if (sb_eof) break;
            state = 1;
        } else if (flag == 1) {
            sm_cp(&sm_a, &sm_b); sm_cp(&sm_b, &sm_c); sm_cp(&sm_c, &sm_cur);
        } else {
            sm_cp(&sm_b, &sm_c); sm_cp(&sm_c, &sm_cur);
        }
        sm_emit3(&sm_a, &sm_b, &sm_c);
    }
    return 1;
}

/* ---- type 5: a lattice -------------------------------------------------------------- */
#define sm_prev ((struct smv *)gx_ov)
#define sm_row  ((struct smv *)(gx_ov + 24 * sizeof(struct smv)))
static pdw sh_lattice(pdw d)
{
    pdw vpr = (pdw)gx_int(gx_dres(d, "VerticesPerRow"), 0), i, have = 0;
    if (vpr < 2 || vpr > 24) return 0;
    while (!sb_eof && !sh_full) {
        for (i = 0; i < vpr; i++) { sm_vertex(&sm_row[i]); sb_align(); if (sb_eof) return 1; }
        if (have) {
            for (i = 0; i + 1 < vpr; i++) {
                sm_emit3(&sm_prev[i], &sm_prev[i + 1], &sm_row[i]);
                sm_emit3(&sm_prev[i + 1], &sm_row[i + 1], &sm_row[i]);
            }
        }
        for (i = 0; i < vpr; i++) sm_cp(&sm_prev[i], &sm_row[i]);
        have = 1;
    }
    return 1;
}

/* ---- types 6 and 7: patches ----------------------------------------------------------- */
static struct pd_fx pc_x[16], pc_y[16];       /* a patch's boundary points in stream order (+4 tensor interiors) */
static struct pd_fx pp_x[12], pp_y[12];       /* the previous patch's boundary points */
static struct smv pc_c[4], pp_c[4];           /* corner colours */
static struct pd_fx pg_x[4][4], pg_y[4][4];   /* the 4x4 grid */
static const pdb pc_ij[32] = { 0,0, 0,1, 0,2, 0,3, 1,3, 2,3, 3,3, 3,2, 3,1, 3,0, 2,0, 1,0, 1,1, 1,2, 2,2, 2,1 };
/* (i, j) of stream point k is pc_ij[2k], pc_ij[2k+1] */
static const pdw pc_bern[5][4] = {   /* B0..B3 at t = 0, 1/4, 1/2, 3/4, 1 as 16.16 low words (hi = 0 except t=0/1) */
    { 0xFFFF, 0, 0, 0 }, { 27648, 27648, 9216, 1024 }, { 8192, 24576, 24576, 8192 },
    { 1024, 9216, 27648, 27648 }, { 0, 0, 0, 0xFFFF } };

static void pp_grid_fill(pdw tensor)
{
    pdw k, a, b;
    static struct pd_fx t, s, c9;
    for (k = 0; k < (tensor ? 16u : 12u); k++) {
        a = pc_ij[2 * k]; b = pc_ij[2 * k + 1];
        fx_cp(&pg_x[a][b], &pc_x[k]); fx_cp(&pg_y[a][b], &pc_y[k]);
    }
    if (!tensor) {
        /* the four interior points of a Coons patch (ISO 32000-1 8.7.4.5.7) */
        pdw w, ii, jj;
        static const pdb cf[4][2] = { {1, 1}, {1, 2}, {2, 1}, {2, 2} };
        fx_i(&c9, 9);
        for (w = 0; w < 4; w++) {
            /* corner k0 (the nearest), edge neighbours, far corners ... as formulas on the grid */
            pdw e0, e1, c0, c1, m0, m1, f0, f1, g0, g1, h0, h1, p0, p1, q0, q1;
            ii = cf[w][0]; jj = cf[w][1];
            e0 = ii == 1 ? 0 : 3; e1 = jj == 1 ? 0 : 3;            /* the nearest corner (e0, e1) */
            c0 = e0; c1 = e1 == 0 ? 3 : 0;                         /* the corner across the row  */
            m0 = e0 == 0 ? 3 : 0; m1 = e1;                         /* the corner across the column */
            f0 = m0; f1 = c1;                                       /* the far corner */
            g0 = e0; g1 = jj;                                       /* edge point beside, along j */
            h0 = ii; h1 = e1;                                       /* edge point beside, along i */
            p0 = m0; p1 = jj;                                       /* far-row edge, same column  */
            q0 = ii; q1 = c1;                                       /* far-column edge, same row  */
            /* P = (-4 P_near + 6 (P_g + P_h) - 2 (P_c + P_m) + 3 (P_p + P_q) - P_far) / 9 */
            fx_raw(&s, 0, 0);
            #define ACC(coef, arr, ra, rb) do { static struct pd_fx cc; fx_i(&cc, coef); fx_mul(&t, &cc, &arr[ra][rb]); fx_add(&s, &s, &t); } while (0)
            ACC(-4, pg_x, e0, e1); ACC(6, pg_x, g0, g1); ACC(6, pg_x, h0, h1); ACC(-2, pg_x, c0, c1); ACC(-2, pg_x, m0, m1);
            ACC(3, pg_x, p0, p1); ACC(3, pg_x, q0, q1); ACC(-1, pg_x, f0, f1);
            fx_div(&pg_x[ii][jj], &s, &c9);
            fx_raw(&s, 0, 0);
            ACC(-4, pg_y, e0, e1); ACC(6, pg_y, g0, g1); ACC(6, pg_y, h0, h1); ACC(-2, pg_y, c0, c1); ACC(-2, pg_y, m0, m1);
            ACC(3, pg_y, p0, p1); ACC(3, pg_y, q0, q1); ACC(-1, pg_y, f0, f1);
            fx_div(&pg_y[ii][jj], &s, &c9);
            #undef ACC
        }
    }
}

/* tessellate the grid in 4x4 cells with the corner colours */
static struct shv pt_v[5][5];
static void pt_tessellate(void)
{
    static struct pd_fx bu, bv, t, s, ux, uy, u, v, one, cu, cv_, w;
    static struct smv col;
    pdw i, j, a, b, k;
    fx_i(&one, 1);
    for (i = 0; i < 5; i++)
        for (j = 0; j < 5; j++) {
            fx_raw(&ux, 0, 0); fx_raw(&uy, 0, 0);
            for (a = 0; a < 4; a++)
                for (b = 0; b < 4; b++) {
                    if (pc_bern[i][a] == 0 || pc_bern[j][b] == 0) continue;
                    if (pc_bern[i][a] == 0xFFFF) fx_cp(&bu, &one); else fx_raw(&bu, 0, pc_bern[i][a]);
                    if (pc_bern[j][b] == 0xFFFF) fx_cp(&bv, &one); else fx_raw(&bv, 0, pc_bern[j][b]);
                    fx_mul(&w, &bu, &bv);
                    fx_mul(&t, &w, &pg_x[a][b]); fx_add(&ux, &ux, &t);
                    fx_mul(&t, &w, &pg_y[a][b]); fx_add(&uy, &uy, &t);
                }
            fx_cp(&pt_v[i][j].x, &ux); fx_cp(&pt_v[i][j].y, &uy);
            /* colour: bilinear on the corner values (c11 = (0,0), c14 = (0,1), c44 = (1,1), c41 = (1,0)) */
            fx_raw(&u, i == 4 ? 1 : 0, i == 4 ? 0 : (pdw)(i << 14));
            fx_raw(&v, j == 4 ? 1 : 0, j == 4 ? 0 : (pdw)(j << 14));
            for (k = 0; k < sm_ncv; k++) {
                static struct pd_fx m0, m1;
                fx_sub(&t, &pc_c[1].c[k], &pc_c[0].c[k]); fx_mul(&t, &t, &v); fx_add(&m0, &pc_c[0].c[k], &t);
                fx_sub(&t, &pc_c[2].c[k], &pc_c[3].c[k]); fx_mul(&t, &t, &v); fx_add(&m1, &pc_c[3].c[k], &t);
                fx_sub(&t, &m1, &m0); fx_mul(&t, &t, &u); fx_add(&col.c[k], &m0, &t);
            }
            (void)s; (void)cu; (void)cv_;
            sm_color(&col, &pt_v[i][j]);
        }
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            shv_cp(&sh_v[0], &pt_v[i][j]); shv_cp(&sh_v[1], &pt_v[i][j + 1]); shv_cp(&sh_v[2], &pt_v[i + 1][j]); sh_tri();
            shv_cp(&sh_v[0], &pt_v[i][j + 1]); shv_cp(&sh_v[1], &pt_v[i + 1][j + 1]); shv_cp(&sh_v[2], &pt_v[i + 1][j]); sh_tri();
        }
}

static pdw sh_patches(pdw tensor)
{
    static struct pd_fx raw;
    pdw flag, np, nc, k, base, have = 0, i;
    static const pdb edge0[4] = { 0, 3, 6, 9 };      /* flag 0..3: which previous boundary point the new patch starts at */
    while (!sb_eof && !sh_full) {
        flag = sb_int(sm_bpf);
        if (sb_eof) break;
        if (flag > 3) break;
        if (flag == 0 || !have) { np = tensor ? 16 : 12; nc = 4; base = 0; flag = 0; }
        else { np = tensor ? 12 : 8; nc = 2; base = 4;
               for (k = 0; k < 4; k++) {
                   pdw s = (pdw)((edge0[flag] + k) % 12);
                   fx_cp(&pc_x[k], &pp_x[s]); fx_cp(&pc_y[k], &pp_y[s]);
               }
               if (flag == 1) { sm_cp(&pc_c[0], &pp_c[1]); sm_cp(&pc_c[1], &pp_c[2]); }
               else if (flag == 2) { sm_cp(&pc_c[0], &pp_c[2]); sm_cp(&pc_c[1], &pp_c[3]); }
               else { sm_cp(&pc_c[0], &pp_c[3]); sm_cp(&pc_c[1], &pp_c[0]); } }
        for (k = 0; k < np; k++) {
            if (flag != 0 && k >= np) break;
            sb_norm(sm_bpc, &raw); sm_map(0, &raw, &pc_x[(flag ? 4 : 0) + k]);
            sb_norm(sm_bpc, &raw); sm_map(1, &raw, &pc_y[(flag ? 4 : 0) + k]);
        }
        for (k = 0; k < nc; k++) {
            for (i = 0; i < sm_ncv; i++) { sb_norm(sm_bpcomp, &raw); sm_map(2 + i, &raw, &pc_c[(flag ? 2 : 0) + k].c[i]); }
        }
        (void)base;
        sb_align();
        if (sb_eof) break;
        pp_grid_fill(tensor);
        pt_tessellate();
        for (k = 0; k < 12; k++) { fx_cp(&pp_x[k], &pc_x[k]); fx_cp(&pp_y[k], &pc_y[k]); }
        for (k = 0; k < 4; k++) sm_cp(&pp_c[k], &pc_c[k]);
        have = 1;
    }
    return 1;
}

/* ---- the verb ------------------------------------------------------------------------------ */
static pdw v_shade(void)
{
    static struct pd_fx dec[16];
    pdw d, ty, i, nd, hasfn, r = PDE_OK;
    d = gx_source();
    if (d == GN_NIL || gx_ty(d) != GN_DICT) return PDE_GRAPHICS;
    ty = (pdw)gx_int(gx_dres(d, "ShadingType"), 0);
    sh_total = 0; sh_skip = G.state; sh_out = 0; sh_full = 0;
    sh_cap = G.rcap > PDGX_SHBYTES ? (pdw)(G.rcap - PDGX_SHBYTES) : 0;
    if (!sh_header(d)) return PDE_GRAPHICS;
    if (ty == 2 || ty == 3) { if (!sh_axial_radial(d, ty)) return PDE_GRAPHICS; }
    else if (ty == 1) { if (!sh_function_based(d)) return PDE_GRAPHICS; }
    else if (ty >= 4 && ty <= 7) {
        R_sh.type = (pdb)ty; R_sh.flags |= PDGX_SH_MESH;
        if (!G.objnum) return PDE_GRAPHICS;
        hasfn = gx_dget(d, "Function") != GN_NIL;
        if (hasfn && !sh_functions(d)) return PDE_GRAPHICS;
        sm_hasfn = hasfn;
        sm_ncv = hasfn ? 1 : sh_ncomp;
        if (sm_ncv > 4) return PDE_GRAPHICS;
        sm_bpc = (pdw)gx_int(gx_dres(d, "BitsPerCoordinate"), 0);
        sm_bpcomp = (pdw)gx_int(gx_dres(d, "BitsPerComponent"), 0);
        sm_bpf = (pdw)gx_int(gx_dres(d, "BitsPerFlag"), 0);
        if (!sm_bpc || sm_bpc > 32 || !sm_bpcomp || sm_bpcomp > 16 || (ty != 5 && (!sm_bpf || sm_bpf > 8))) return PDE_GRAPHICS;
        nd = gx_nums(gx_dres(d, "Decode"), dec, 16);
        if (nd < 4 + 2 * sm_ncv) return PDE_GRAPHICS;
        for (i = 0; i < nd; i++) fx_cp(&sm_dec[i], &dec[i]);
        sm_dec[2 * (2 + 0)].lo += 0;
        sb_len = 0; sb_pos = 0; sb_bit = 0; sb_eof = 0;
        if (gx_sopen(G.objnum, G.gen) != PDE_OK) return PDE_GRAPHICS;
        if (ty == 4) sh_freeform();
        else if (ty == 5) { if (!sh_lattice(d)) { gx_sclose(); return PDE_GRAPHICS; } }
        else sh_patches(ty == 7);
        gx_sclose();
    } else return PDE_GRAPHICS;
    if (G.rcap < PDGX_SHBYTES) return PDE_NOMEM;
    pd_fcopy_out(&R_sh, G.rseg, G.roff, sizeof R_sh);
    G.rlen = (pdw)(PDGX_SHBYTES + sh_off(sh_out)); G.ntri = sh_out; G.ok = 1;
    if (sh_full == 1) { G.state = sh_total; r = PDE_BUSY; }
    else G.state = 0;
    if (sh_full == 2) G.kind = 1;                    /* the triangle limit: the core notes PDF_INC_LIMIT */
    return r;
}
