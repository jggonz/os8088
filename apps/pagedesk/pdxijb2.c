/* ============================================================================
 * pdxijb2.c - JBIG2Decode (PAGEDESK-SPEC 20.22.12, AP-09)
 *
 * #included by PDXI's pdxi.c AFTER pdxiccit.c (which it uses for MMR and for
 * the shared reducer and bl_* results). The embedded stream format (ISO/IEC
 * 14492 D.3): segments only, /JBIG2Globals first. The coded bytes come from
 * ix_get / ix_open of the shell (see pdxiccit.c).
 *
 * THE SHAPE. The page cannot be resident (A4 at 300 dpi is 1 MB). The page is
 * decoded in horizontal BANDS of full-resolution rows held in the workspace;
 * each band re-runs the segment stream and applies only what touches it:
 *  - a symbol or pattern dictionary and a custom table are decoded ONCE and
 *    kept (their segment numbers are in a registry), later bands skip their
 *    bytes;
 *  - a region wholly outside the band is skipped by byte count;
 *  - a generic region decodes its rows from its top and stops at the band's
 *    last row (the arithmetic decoder is sequential, so rows above the band
 *    are decoded and dropped);
 *  - a text region walks its instances and draws the symbols that touch the
 *    band.
 * Each finished band is averaged down to the delivered scale (pdxiccit.c).
 *
 * THE WORKSPACE (64 KiB, far, reached only through jw_*):
 *   0000-2FFF  generic-region contexts (a 4,096-entry hash for template 0,
 *              direct arrays for 1-3)
 *   3000-4FFF  refinement contexts
 *   5000-69FF  the 13 integer decoders' contexts (512 each)
 *   6A00-71FF  IAID contexts
 *   7200-      the band, JB_BANDMAX bytes
 *   then       symbol and pattern bitmaps, growing up; their 6-byte records
 *              (offset, width, height), growing down from the top.
 * ==========================================================================*/

/* ---- the workspace -------------------------------------------------------- */
#ifndef JW_SHELL
/* host stand-in: the shell defines these over pd_f* on the target */
static pdb jw_mem[65536];
#define jw_rb(o)     (jw_mem[(pdw)(o)])
#define jw_wb(o, v)  (jw_mem[(pdw)(o)] = (pdb)(v))
static pdw jw_rw(pdw o) { return (pdw)(jw_mem[o] | (jw_mem[(pdw)(o + 1)] << 8)); }
static void jw_ww(pdw o, pdw v) { jw_mem[o] = (pdb)v; jw_mem[(pdw)(o + 1)] = (pdb)(v >> 8); }
static void jw_get(pdb *dst, pdw o, pdw n) { pdw i; for (i = 0; i < n; i++) dst[i] = jw_mem[(pdw)(o + i)]; }
static void jw_put(pdw o, const pdb *src, pdw n) { pdw i; for (i = 0; i < n; i++) jw_mem[(pdw)(o + i)] = src[i]; }
static void jw_zero(pdw o, pdw n) { pdw i; for (i = 0; i < n; i++) jw_mem[(pdw)(o + i)] = 0; }
static void jw_move(pdw d, pdw sr, pdw n) { pdw i; if (d < sr) for (i = 0; i < n; i++) jw_mem[(pdw)(d + i)] = jw_mem[(pdw)(sr + i)]; else for (i = n; i; i--) jw_mem[(pdw)(d + i - 1)] = jw_mem[(pdw)(sr + i - 1)]; }
#endif

#define JW_GB     0x0000u
#define JW_GBHS   0x2000u       /* hash states; tags are words at JW_GB          */
#define JW_GR     0x3000u
#define JW_INT    0x5000u
#define JW_IAID   0x6A00u
#define JW_BAND   0x7200u
#ifndef JB_BANDMAX
#define JB_BANDMAX 16384u
#endif
#define JW_SYM    ((pdw)(JW_BAND + JB_BANDMAX))
#define JB_HASH   4096u

#define JBE_OK      0
#define JBF_UNSUP   1           /* jb_inc: something was skipped (intermediate region, too large, ...) */
#define JBF_BAD     2           /* jb_inc: a segment was damaged                                      */

static pdw jb_inc;

/* ---- the segment-bounded byte source ---------------------------------------- */
static pdw jb_lh, jb_ll;        /* bytes left in the segment (32 bits)            */
static pdb jb_unb;              /* the segment's length is unknown (unbounded)    */

static pdb jb_sub;              /* a sub-limit inside the segment (MMR / refinement data of a stated size) */
static pdw jb_subn;

static int jb_getb(void)
{
    if (jb_sub) { if (!jb_subn) return -1; jb_subn--; }
    if (!jb_unb) {
        if (!jb_lh && !jb_ll) return -1;
        if (!jb_ll) { jb_lh--; jb_ll = 0xFFFF; } else jb_ll--;
    }
    return ix_get();
}

/* ---- the MQ decoder (T.88 annex E, hardware convention) -------------------- */
static const pdw mq_qe[47] = {
    0x5601, 0x3401, 0x1801, 0x0AC1, 0x0521, 0x0221, 0x5601, 0x5401, 0x4801, 0x3801,
    0x3001, 0x2401, 0x1C01, 0x1601, 0x5601, 0x5401, 0x5101, 0x4801, 0x3801, 0x3401,
    0x3001, 0x2801, 0x2401, 0x2201, 0x1C01, 0x1801, 0x1601, 0x1401, 0x1201, 0x1101,
    0x0AC1, 0x09C1, 0x08A1, 0x0521, 0x0441, 0x02A1, 0x0221, 0x0141, 0x0111, 0x0085,
    0x0049, 0x0025, 0x0015, 0x0009, 0x0005, 0x0001, 0x5601 };
static const pdb mq_nm[47] = {
    1, 2, 3, 4, 5, 38, 7, 8, 9, 10, 11, 12, 13, 29, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 45, 46 };
static const pdb mq_nl[47] = {
    1, 6, 9, 12, 29, 33, 6, 14, 14, 14, 17, 18, 20, 21, 14, 14, 15, 16, 17, 18, 19, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 46 };

static pdw mq_chi, mq_clo, mq_a, mq_ct;
static pdw mq_cur, mq_nxt;

static pdw mq_fetch(void)
{
    int c = jb_getb();
    return c < 0 ? 0xFFu : (pdw)c;
}
static void mq_add(pdw vhi, pdw vlo)
{
    mq_clo = (pdw)(mq_clo + vlo);
    if (mq_clo < vlo) mq_chi++;
    mq_chi = (pdw)(mq_chi + vhi);
}
static void mq_bytein(void)
{
    if (mq_cur == 0xFF) {
        if (mq_nxt > 0x8F) { mq_add(0, 0xFF00u); mq_ct = 8; return; }
        mq_cur = mq_nxt; mq_nxt = mq_fetch();
        mq_add((pdw)(mq_cur >> 7), (pdw)(mq_cur << 9)); mq_ct = 7;
        return;
    }
    mq_cur = mq_nxt; mq_nxt = mq_fetch();
    mq_add(0, (pdw)(mq_cur << 8)); mq_ct = 8;
}
/* begin an arithmetic-coded stream at the next byte of the segment */
static void mq_init(void)
{
    mq_cur = mq_fetch(); mq_nxt = mq_fetch();
    mq_chi = mq_cur; mq_clo = 0;
    mq_bytein();
    mq_chi = (pdw)((mq_chi << 7) | (mq_clo >> 9)) & 0xFFFFu;
    mq_clo = (pdw)(mq_clo << 7);
    mq_ct -= 7;
    mq_a = 0x8000u;
}
/* one decision with the context whose state byte is at workspace offset o */
static int mq_bit(pdw o)
{
    pdw st = jw_rb(o), ix = (pdw)((st & 0x7F) >> 1), mps = st & 1, qe = mq_qe[ix], a = (pdw)(mq_a - qe), d;
    if (mq_chi < qe) {
        if (a < qe) { d = mps; ix = mq_nm[ix]; }
        else { d = mps ^ 1; if (ix == 0 || ix == 6 || ix == 14) mps = d; ix = mq_nl[ix]; }
        a = qe;
    } else {
        mq_chi = (pdw)(mq_chi - qe);
        if (a & 0x8000u) { mq_a = a; return (int)mps; }
        if (a < qe) { d = mps ^ 1; if (ix == 0 || ix == 6 || ix == 14) mps = d; ix = mq_nl[ix]; }
        else { d = mps; ix = mq_nm[ix]; }
    }
    do {
        if (!mq_ct) mq_bytein();
        a = (pdw)(a << 1);
        mq_chi = (pdw)(((mq_chi << 1) | (mq_clo >> 15)) & 0xFFFFu);
        mq_clo = (pdw)(mq_clo << 1);
        mq_ct--;
    } while (!(a & 0x8000u));
    mq_a = a;
    jw_wb(o, (st & 0x80) | (ix << 1) | mps);
    return (int)d;
}

/* ---- integer and IAID decoders (annex A) ------------------------------------ */
#define IA_DH   0
#define IA_DW   1
#define IA_EX   2
#define IA_AI   3
#define IA_DT   4
#define IA_FS   5
#define IA_DS   6
#define IA_IT   7
#define IA_RI   8
#define IA_RDW  9
#define IA_RDH  10
#define IA_RDX  11
#define IA_RDY  12
#define IA_N    13

static pdi  jb_ival;            /* the last integer decoded                     */
static pdb  jb_ovf;             /* a value too large for 16 bits                */

/* returns 1 for OOB, else 0 with the value in jb_ival */
static int ia_dec(pdw which)
{
    pdw prev = 1, base = (pdw)(JW_INT + which * 512u), v = 0, n, off;
    int s, b, k;
    #define IA_BIT() (b = mq_bit((pdw)(base + prev)), prev = (prev < 256) ? (pdw)((prev << 1) | b) : (pdw)((((prev << 1) | b) & 511) | 256), b)
    s = IA_BIT();
    if (!IA_BIT()) { n = 2; off = 0; }
    else if (!IA_BIT()) { n = 4; off = 4; }
    else if (!IA_BIT()) { n = 6; off = 20; }
    else if (!IA_BIT()) { n = 8; off = 84; }
    else if (!IA_BIT()) { n = 12; off = 340; }
    else { n = 32; off = 4436; }
    for (k = 0; (pdw)k < n; k++) {
        b = IA_BIT();
        if (n == 32 && k < 16) { if (b) jb_ovf = 1; }
        else v = (pdw)((v << 1) | b);
    }
    if (v > (pdw)(32000 - off)) { jb_ovf = 1; v = 0; }
    v = (pdw)(v + off);
    if (s) { if (!v) return 1; jb_ival = (pdi)(-(pdi)v); }
    else jb_ival = (pdi)v;
    return 0;
    #undef IA_BIT
}

static pdw jb_idlen;            /* SBSYMCODELEN                                 */
static pdw ia_id(void)
{
    pdw prev = 1, k;
    for (k = 0; k < jb_idlen; k++) prev = (pdw)((prev << 1) | mq_bit((pdw)(JW_IAID + prev)));
    return (pdw)(prev - (1u << jb_idlen));
}

/* ---- bitmaps and the canvas --------------------------------------------------- */
static pdw jb_sp, jb_hp;        /* bitmap bump (up), record bump (down)          */

/* The destination the regions are drawn on: the band, or a symbol bitmap */
static pdw cv_off, cv_rb, cv_w, cv_h;
static pdi cv_y0;               /* absolute row of canvas row 0                  */

static pdw jb_srcbyte(const pdb *src, pdi k)
{
    pdi b;
    pdw r;
    if (k <= -8) return 0;
    if (k < 0) return (pdw)(src[0] >> (-k));
    b = (pdi)(k >> 3); r = (pdw)(k & 7);
    if (!r) return src[b];
    return (pdw)(((src[b] << r) | (src[b + 1] >> (8 - r))) & 0xFF);
}

/* draw one source row (w bits, 1 = black, zero beyond) at absolute row y,
 * column dx, combined with op (0 OR 1 AND 2 XOR 3 XNOR 4 REPLACE) */
static void cv_row(pdi y, pdi dx, const pdb *src, pdw w, pdw op)
{
    pdi yy = (pdi)(y - cv_y0), x0 = dx, x1 = (pdi)(dx + (pdi)w), bi, last;
    pdw row, lo, hi, m, d, s, n;
    if (yy < 0 || yy >= (pdi)cv_h) return;
    if (x0 < 0) x0 = 0;
    if (x1 > (pdi)cv_w) x1 = (pdi)cv_w;
    if (x0 >= x1) return;
    row = (pdw)(cv_off + (pdw)yy * cv_rb);
    last = (pdi)((x1 - 1) >> 3);
    for (bi = (pdi)(x0 >> 3); bi <= last; bi++) {
        pdi b0 = (pdi)(bi * 8);
        lo = (pdw)(x0 > b0 ? x0 - b0 : 0);
        hi = (pdw)(x1 < b0 + 8 ? x1 - b0 : 8);
        m = (pdw)((0xFF >> lo) & ~(0xFF >> hi) & 0xFF);
        s = jb_srcbyte(src, (pdi)(b0 - dx));
        d = jw_rb((pdw)(row + bi));
        if (op == 0) n = d | s;
        else if (op == 1) n = d & s;
        else if (op == 2) n = d ^ s;
        else if (op == 3) n = ~(d ^ s);
        else n = s;
        jw_wb((pdw)(row + bi), (d & ~m) | (n & m));
    }
}

/* ---- generic region decoding (6.2) ---------------------------------------------- */
#define GB_MAXRB 1032
#define gb_r0 bl_pool[0]
#define gb_r1 bl_pool[1]
#define gb_r2 bl_pool[2]
static pdb *gb_c, *gb_p1, *gb_p2;
static pdw  gb_tpl, gb_tpgd;
static pdi  gb_at[4][2];
static pdb  gb_atok;            /* every AT pixel is nominal                       */
static pdw  gb_w, gb_h;
static pdb  gb_hash;            /* template 0 uses the hash                        */
static pdb  gb_stop;            /* the sink asks for no more rows                  */
static pdw  gb_sinkmode;        /* 0 canvas, 1 plane, 2 none                       */
static pdi  gb_dx, gb_dy;       /* where a canvas sink draws                       */
static pdw  gb_op;
static pdw  gb_skip;            /* halftone skip in use                            */

/* template shapes: bits of the row-y window, row y-1 window (with its shift
 * and look-ahead), row y-2 window */
static const pdb gb_m0[4] = { 15, 7, 3, 15 };
static const pdb gb_s1[4] = { 4, 3, 2, 4 };
static const pdb gb_m1[4] = { 127, 63, 31, 63 };
static const pdb gb_l1[4] = { 3, 3, 2, 2 };
static const pdb gb_s2[4] = { 11, 9, 7, 0 };
static const pdb gb_m2[4] = { 31, 15, 7, 0 };
static const pdb gb_l2[4] = { 2, 2, 1, 0 };
static const pdw gb_tctx[4] = { 0x9B25u, 0x0795u, 0x00E5u, 0x0195u };
/* the context bit each AT pixel sits at, and its nominal position */
static const pdb gb_atbit[4][4] = { { 4, 10, 11, 15 }, { 3, 0, 0, 0 }, { 2, 0, 0, 0 }, { 4, 0, 0, 0 } };
static const signed char gb_atnomv[4][8] = {
    { 3, -1, -3, -1, 2, -2, -2, -2 }, { 3, -1, 0, 0, 0, 0, 0, 0 },
    { 2, -1, 0, 0, 0, 0, 0, 0 }, { 2, -1, 0, 0, 0, 0, 0, 0 } };

static int gb_px(const pdb *r, pdi x)
{
    if (x < 0 || x >= (pdi)gb_w) return 0;
    return (r[x >> 3] >> (7 - (x & 7))) & 1;
}

/* the state byte for context ctx; the hash for template 0 */
static pdw gb_cx(pdw ctx)
{
    pdw h = (pdw)((ctx ^ (ctx >> 6) ^ (ctx << 3)) & (JB_HASH - 1)), n;
    for (n = 0; n < JB_HASH; n++) {
        pdw st = jw_rb((pdw)(JW_GBHS + h));
        if (!st) { jw_ww((pdw)(JW_GB + h * 2), ctx); jw_wb((pdw)(JW_GBHS + h), 0x80); return (pdw)(JW_GBHS + h); }
        if (jw_rw((pdw)(JW_GB + h * 2)) == ctx) return (pdw)(JW_GBHS + h);
        h = (pdw)((h + 1) & (JB_HASH - 1));
    }
    return 0xFFFFu;             /* full: the caller fails the region */
}

static int gb_fail;

static int gb_bit(pdw ctx)
{
    pdw o;
    if (gb_hash) { o = gb_cx(ctx); if (o == 0xFFFFu) { gb_fail = 1; return 0; } }
    else o = (pdw)(JW_GB + ctx);
    return mq_bit(o);
}

/* the sink for each finished row */
static void ht_plane_row(pdw y, const pdb *row);
static int gb_skipf(pdw x, pdw y);

static pdw gb_boff, gb_brb;     /* sink 3: a bitmap in the workspace            */
static void gb_sink(pdw y)
{
    if (gb_sinkmode == 3) { jw_put((pdw)(gb_boff + y * gb_brb), gb_c, gb_brb); return; }
    if (gb_sinkmode == 0) {
        cv_row((pdi)(gb_dy + (pdi)y), gb_dx, gb_c, gb_w, gb_op);
        if ((pdi)(gb_dy + (pdi)y + 1) >= (pdi)(cv_y0 + (pdi)cv_h)) gb_stop = 1;
    } else if (gb_sinkmode == 1) ht_plane_row(y, gb_c);
}

/* The arithmetic-coded generic region. Needs gb_w/gb_h/gb_tpl/gb_tpgd/gb_at set and
 * mq_init done. Contexts are cleared here. Returns 0 or PDE_*. */
static void gb_cinit(void)
{
    gb_hash = (pdb)(gb_tpl == 0);
    if (gb_hash) jw_zero(JW_GB, (pdw)(JB_HASH * 3)); else jw_zero(JW_GB, (pdw)(1u << (gb_tpl == 1 ? 13 : 10)));
}
static pdw gb_arith(void)
{
    pdw x, y, nb = (pdw)((gb_w + 7) >> 3), t = gb_tpl, ltp = 0, w0, w1, w2, ctx, k, i;
    pdb *tp;
    int bit;
    if (gb_w > BL_MAXW) return PDE_LIMIT;
    gb_hash = (pdb)(t == 0);
    gb_fail = 0; gb_stop = 0;
    gb_c = gb_r0; gb_p1 = gb_r1; gb_p2 = gb_r2;
    for (i = 0; i < GB_MAXRB; i++) gb_r0[i] = gb_r1[i] = gb_r2[i] = 0;
    gb_atok = 1;
    for (k = 0; k < (t ? 1u : 4u); k++)
        if (gb_at[k][0] != gb_atnomv[t][k * 2] || gb_at[k][1] != gb_atnomv[t][k * 2 + 1]) gb_atok = 0;
    for (y = 0; y < gb_h && !gb_stop; y++) {
        if (gb_tpgd) {
            ltp ^= (pdw)gb_bit(gb_tctx[t]);
            if (ltp) {
                for (i = 0; i < nb; i++) gb_c[i] = gb_p1[i];
                goto rowdone;
            }
        }
        for (i = 0; i < nb; i++) gb_c[i] = 0;
        /* windows at x = 0 */
        w0 = 0; w1 = 0; w2 = 0;
        for (k = 0; k <= gb_l1[t]; k++) w1 = (pdw)((w1 << 1) | gb_px(gb_p1, (pdi)k));
        if (gb_m2[t]) for (k = 0; k <= gb_l2[t]; k++) w2 = (pdw)((w2 << 1) | gb_px(gb_p2, (pdi)k));
        for (x = 0; x < gb_w; x++) {
            if (gb_skip && gb_skipf(x, y)) bit = 0;
            else {
                ctx = (pdw)(w0 | (w1 << gb_s1[t]) | (w2 << gb_s2[t]));
                if (!gb_atok) {
                    for (k = 0; k < (t ? 1u : 4u); k++) {
                        pdi ax = (pdi)((pdi)x + gb_at[k][0]), ay = gb_at[k][1];
                        pdw b = gb_atbit[t][k], v;
                        v = (pdw)(ay == 0 ? gb_px(gb_c, ax) : ay == -1 ? gb_px(gb_p1, ax) : gb_px(gb_p2, ax));
                        ctx = (pdw)((ctx & ~(1u << b)) | (v << b));
                    }
                }
                bit = gb_bit(ctx);
                if (gb_fail) return PDE_LIMIT;
            }
            if (bit) gb_c[x >> 3] |= (pdb)(0x80 >> (x & 7));
            w0 = (pdw)(((w0 << 1) | bit) & gb_m0[t]);
            w1 = (pdw)(((w1 << 1) | gb_px(gb_p1, (pdi)(x + gb_l1[t] + 1))) & gb_m1[t]);
            if (gb_m2[t]) w2 = (pdw)(((w2 << 1) | gb_px(gb_p2, (pdi)(x + gb_l2[t] + 1))) & gb_m2[t]);
        }
    rowdone:
        gb_sink(y);
        tp = gb_p2; gb_p2 = gb_p1; gb_p1 = gb_c; gb_c = tp;
    }
    return PDE_OK;
}

/* ---- MMR (Group 4) regions: pdxiccit.c's rows over the segment's bytes --------- */
static pdb gb_mmr_cont;         /* a later bitplane of a halftone: the bit reader's window carries over */
static void cf_mmr_begin(pdw w)
{
    pdw i;
    cf_build();
    cf_k = 0xFFFFu; cf_cols = w; cf_flags = 0; cf_nb = (pdw)((w + 7) >> 3);
    cf_cur = cf_rowa; cf_ref = cf_rowb;
    for (i = 0; i < 1024; i++) cf_rowa[i] = cf_rowb[i] = 0;
    cf_end = 0; cf_seg = 1;
    if (!gb_mmr_cont) cf_binit();
}
/* EOFB if present, then the next byte boundary */
static void cf_mmr_end(void)
{
    if (!cf_done() && cf_peek(12) == 1) { cf_adv(12); if (cf_peek(12) == 1) cf_adv(12); }
    if (cf_bit) cf_adv((pdw)(8 - cf_bit));
}
static pdw gb_mmr(void)
{
    pdw y;
    if (gb_w > BL_MAXW) return PDE_LIMIT;
    cf_mmr_begin(gb_w);
    gb_stop = 0;
    for (y = 0; y < gb_h && !gb_stop; y++) {
        cf_decode();
        gb_c = cf_ref;
        gb_sink(y);
    }
    return PDE_OK;
}


/* ---- symbol and pattern storage --------------------------------------------------- */
#define JB_MAXSYMS 4096u
#define JB_RECSZ   6

static pdw jb_alloc(pdw w, pdw h)       /* a zeroed bitmap, or 0 */
{
    pdw rb = (pdw)((w + 7) >> 3), size, o;
    if (w > BL_MAXW) return 0;
    if (h && rb > (pdw)(0xFFFFu / h)) return 0;
    size = (pdw)(rb * h);
    if ((pdw)(jb_hp - jb_sp) < (pdw)(size + 64) || (pdw)(jb_hp - jb_sp) > 0xF000u) return 0;
    o = jb_sp; jw_zero(o, size); jb_sp = (pdw)(jb_sp + size);
    return o;
}
static pdw jb_recs(pdw n)               /* n records at the top, or 0 */
{
    pdw sz;
    if (n > JB_MAXSYMS) return 0;
    sz = (pdw)(n * JB_RECSZ);
    if ((pdw)(jb_hp - jb_sp) < (pdw)(sz + 64)) return 0;
    jb_hp = (pdw)(jb_hp - sz);
    return jb_hp;
}
static void jb_setrec(pdw base, pdw i, pdw off, pdw w, pdw h)
{
    pdw o = (pdw)(base + i * JB_RECSZ);
    jw_ww(o, off); jw_ww((pdw)(o + 2), w); jw_ww((pdw)(o + 4), h);
}
static pdw jb_rec_off, jb_rec_w, jb_rec_h;
static void jb_getrec(pdw base, pdw i)
{
    pdw o = (pdw)(base + i * JB_RECSZ);
    jb_rec_off = jw_rw(o); jb_rec_w = jw_rw((pdw)(o + 2)); jb_rec_h = jw_rw((pdw)(o + 4));
}

/* ---- the segment registry (decoded once, kept across bands) ------------------------------ */
#define JB_NRG 24
#define RGK_SYM   1     /* symbol dictionary: ofs = records, cnt = exported symbols */
#define RGK_PAT   2     /* pattern dictionary: ofs = bitmaps, cnt = patterns, a = w, b = h */
#define RGK_TAB   3     /* custom Huffman table: ofs = the table in the workspace    */
#define RGK_BAD   4     /* decoded and unusable                                      */
static pdw jb_rg_num[JB_NRG], jb_rg_ofs[JB_NRG], jb_rg_cnt[JB_NRG], jb_rg_a[JB_NRG], jb_rg_b[JB_NRG];
static pdb jb_rg_kind[JB_NRG];
static pdw jb_nrg;
static pdw jb_rg_find(pdw num)
{
    pdw i;
    for (i = 0; i < jb_nrg; i++) if (jb_rg_num[i] == num) return i;
    return 0xFFFFu;
}

/* ---- generic refinement (6.3) ---------------------------------------------------------- */
/* current rows live in jb_srow (the row being made) and tr_row (the row above); the
 * reference rows y-1, y, y+1 in gb_r0..2. None of the three is in use while a
 * refinement runs. */
#define jb_srow bl_pool[3]
#define tr_row bl_pool[4]
#define RF_MAXW 8192
static pdw rf_tpl, rf_tpgr;
static pdi rf_at[2][2];
static pdw rf_w, rf_rw;

static int rf_cur(const pdb *r, pdi x)
{
    if (x < 0 || x >= (pdi)rf_w) return 0;
    return (r[x >> 3] >> (7 - (x & 7))) & 1;
}
static int rf_ref(const pdb *r, pdi x)
{
    if (x < 0 || x >= (pdi)rf_rw) return 0;
    return (r[x >> 3] >> (7 - (x & 7))) & 1;
}

/* dst: a bitmap of w x h in the workspace; ref at (x - dx, y - dy) of the reference.
 * mq_init done and the refinement contexts cleared by the caller. */
static pdw rf_decode(pdw dst, pdw w, pdw h, pdw ref, pdw rw, pdw rh, pdi dx, pdi dy)
{
    pdw rb = (pdw)((w + 7) >> 3), rrb = (pdw)((rw + 7) >> 3), x, y, i, ctx, ltp = 0, cen;
    pdb *cur = jb_srow, *up = tr_row;
    pdb *r0 = gb_r0, *r1 = gb_r1, *r2 = gb_r2;
    pdi ry;
    int bit, a, b, c, d, e, f, g, hh, ii;
    if (w > RF_MAXW || rw > RF_MAXW) return PDE_LIMIT;
    rf_w = w; rf_rw = rw;
    cen = rf_tpl ? 0x80u : 0x100u;
    for (i = 0; i < GB_MAXRB; i++) up[i] = 0;
    for (y = 0; y < h; y++) {
        for (i = 0; i < GB_MAXRB; i++) { cur[i] = 0; r0[i] = r1[i] = r2[i] = 0; }
        ry = (pdi)((pdi)y - dy);
        if (ry - 1 >= 0 && ry - 1 < (pdi)rh) jw_get(r0, (pdw)(ref + (pdw)(ry - 1) * rrb), rrb);
        if (ry >= 0 && ry < (pdi)rh) jw_get(r1, (pdw)(ref + (pdw)ry * rrb), rrb);
        if (ry + 1 >= 0 && ry + 1 < (pdi)rh) jw_get(r2, (pdw)(ref + (pdw)(ry + 1) * rrb), rrb);
        if (rf_tpgr) ltp ^= (pdw)mq_bit((pdw)(JW_GR + cen));
        for (x = 0; x < w; x++) {
            pdi rx = (pdi)((pdi)x - dx);
            a = rf_ref(r1, rx);
            if (ltp) {          /* typical prediction: a uniform 3 x 3 reference neighbourhood is the pixel */
                b = rf_ref(r0, (pdi)(rx - 1)); c = rf_ref(r0, rx); d = rf_ref(r0, (pdi)(rx + 1));
                e = rf_ref(r1, (pdi)(rx - 1)); f = rf_ref(r1, (pdi)(rx + 1));
                g = rf_ref(r2, (pdi)(rx - 1)); hh = rf_ref(r2, rx); ii = rf_ref(r2, (pdi)(rx + 1));
                if (b == a && c == a && d == a && e == a && f == a && g == a && hh == a && ii == a) {
                    if (a) cur[x >> 3] |= (pdb)(0x80 >> (x & 7));
                    continue;
                }
            }
            if (rf_tpl == 0) {
                pdi ax = (pdi)((pdi)x + rf_at[0][0]), ay = rf_at[0][1];
                ctx = (pdw)(rf_cur(cur, (pdi)(x - 1)));
                ctx |= (pdw)(rf_cur(up, (pdi)(x + 1)) << 1);
                ctx |= (pdw)(rf_cur(up, (pdi)x) << 2);
                ctx |= (pdw)((ay == 0 ? rf_cur(cur, ax) : rf_cur(up, ax)) << 3);
                ctx |= (pdw)(rf_ref(r2, (pdi)(rx + 1)) << 4);
                ctx |= (pdw)(rf_ref(r2, rx) << 5);
                ctx |= (pdw)(rf_ref(r2, (pdi)(rx - 1)) << 6);
                ctx |= (pdw)(rf_ref(r1, (pdi)(rx + 1)) << 7);
                ctx |= (pdw)(a << 8);
                ctx |= (pdw)(rf_ref(r1, (pdi)(rx - 1)) << 9);
                ctx |= (pdw)(rf_ref(r0, (pdi)(rx + 1)) << 10);
                ctx |= (pdw)(rf_ref(r0, rx) << 11);
                {
                    pdi qx = (pdi)(rx + rf_at[1][0]), qy = rf_at[1][1];
                    ctx |= (pdw)((qy == -1 ? rf_ref(r0, qx) : qy == 0 ? rf_ref(r1, qx) : rf_ref(r2, qx)) << 12);
                }
            } else {
                ctx = (pdw)(rf_cur(cur, (pdi)(x - 1)));
                ctx |= (pdw)(rf_cur(up, (pdi)(x + 1)) << 1);
                ctx |= (pdw)(rf_cur(up, (pdi)x) << 2);
                ctx |= (pdw)(rf_cur(up, (pdi)(x - 1)) << 3);
                ctx |= (pdw)(rf_ref(r2, (pdi)(rx + 1)) << 4);
                ctx |= (pdw)(rf_ref(r2, rx) << 5);
                ctx |= (pdw)(rf_ref(r1, (pdi)(rx + 1)) << 6);
                ctx |= (pdw)(a << 7);
                ctx |= (pdw)(rf_ref(r1, (pdi)(rx - 1)) << 8);
                ctx |= (pdw)(rf_ref(r0, rx) << 9);
            }
            bit = mq_bit((pdw)(JW_GR + ctx));
            if (bit) cur[x >> 3] |= (pdb)(0x80 >> (x & 7));
        }
        jw_put((pdw)(dst + y * rb), cur, rb);
        for (i = 0; i < rb; i++) up[i] = cur[i];
    }
    return PDE_OK;
}

/* ---- Huffman (annex B) is added below; the text region and the dictionary take their
 * numbers through jb_idec, which picks the integer decoder or a table. ---------------- */
static pdb jb_huff;
static int hf_dec(pdw role);            /* returns 1 for OOB, 0 with jb_ival set */
static pdw hf_ids(void);                /* a symbol ID in Huffman mode           */
static pdw hf_refsize(void);
static pdw hf_uval;                     /* the last value as an unsigned 16-bit (BMSIZE)  */
static void hb_align(void);
static pdw jb_b(void);
#define HF_BM 13
static int jb_idec(pdw role)
{
    if (jb_huff) return hf_dec(role);
    return ia_dec(role);
}

/* ---- text regions (6.4) ------------------------------------------------------------------ */
static pdw tr_recs, tr_nsyms;           /* SBSYMS: records                                      */
static pdw tr_ninst, tr_strips, tr_logs, tr_corner, tr_comb, tr_defpix, tr_refine, tr_transp;
static pdi tr_dsoff, tr_ox, tr_oy;      /* the region's origin on the canvas                    */
static pdw tr_rtpl;
static pdi tr_rat[2][2];

static void tr_draw(pdw off, pdw w, pdw h, pdi x, pdi y)
{
    pdw rb = (pdw)((w + 7) >> 3), sy;
    if (!w || !h) return;
    if (rb > GB_MAXRB - 2) return;
    for (sy = 0; sy < h; sy++) {
        pdi yy = (pdi)(y + (pdi)sy);
        if (yy < cv_y0 || yy >= (pdi)(cv_y0 + (pdi)cv_h)) continue;
        jw_get(tr_row, (pdw)(off + sy * rb), rb);
        tr_row[rb] = 0; tr_row[rb + 1] = 0;
        cv_row(yy, x, tr_row, w, tr_comb);
    }
}

static pdw tr_run(void)
{
    pdi stript, firsts, curs, curt, ti;
    pdw n = 0, id, ri, e;
    int first;
    if (jb_idec(IA_DT)) return PDE_SYNTAX;
    stript = (pdi)(-jb_ival * (pdi)tr_strips); firsts = 0;
    while (n < tr_ninst) {
        if (jb_idec(IA_DT)) return PDE_SYNTAX;
        stript = (pdi)(stript + jb_ival * (pdi)tr_strips);
        first = 1;
        for (;;) {
            if (first) {
                if (jb_idec(IA_FS)) return PDE_SYNTAX;
                firsts = (pdi)(firsts + jb_ival); curs = firsts; first = 0;
            } else {
                if (jb_idec(IA_DS)) break;
                curs = (pdi)(curs + jb_ival + tr_dsoff);
            }
            if (n >= tr_ninst) return PDE_SYNTAX;
            if (tr_strips == 1) curt = 0;
            else { if (jb_idec(IA_IT)) return PDE_SYNTAX; curt = jb_ival; }
            ti = (pdi)(stript + curt);
            id = jb_huff ? hf_ids() : ia_id();
            if (jb_ovf || id >= tr_nsyms) return PDE_SYNTAX;
            ri = 0;
            if (tr_refine) { if (jb_idec(IA_RI)) return PDE_SYNTAX; ri = (pdw)jb_ival; }
            jb_getrec(tr_recs, id);
            {
                pdw so = jb_rec_off, sw = jb_rec_w, sh = jb_rec_h, mark = jb_sp;
                pdi x, y;
                if (ri) {
                    pdi rdw, rdh, rdx, rdy;
                    pdw nw, nh, no;
                    if (jb_idec(IA_RDW)) return PDE_SYNTAX; rdw = jb_ival;
                    if (jb_idec(IA_RDH)) return PDE_SYNTAX; rdh = jb_ival;
                    if (jb_idec(IA_RDX)) return PDE_SYNTAX; rdx = jb_ival;
                    if (jb_idec(IA_RDY)) return PDE_SYNTAX; rdy = jb_ival;
                    if (jb_huff) { (void)hf_refsize(); }
                    if ((pdi)(sw + rdw) < 0 || (pdi)(sh + rdh) < 0) return PDE_SYNTAX;
                    nw = (pdw)(sw + rdw); nh = (pdw)(sh + rdh);
                    no = jb_alloc(nw, nh);
                    if (!no) return PDE_LIMIT;
                    if (jb_huff) mq_init();
                    e = rf_decode(no, nw, nh, so, sw, sh, (pdi)((rdw >> 1) + rdx), (pdi)((rdh >> 1) + rdy));
                    if (e) return e;
                    so = no; sw = nw; sh = nh;
                }
                /* the corner and the advance before the draw */
                if (!tr_transp && (tr_corner == 2 || tr_corner == 3)) curs = (pdi)(curs + (pdi)sw - 1);
                if (tr_transp && (tr_corner == 0 || tr_corner == 2)) curs = (pdi)(curs + (pdi)sh - 1);
                if (!tr_transp) {
                    x = curs; y = ti;
                    if (tr_corner == 3 || tr_corner == 2) x = (pdi)(curs - (pdi)sw + 1);
                    if (tr_corner == 0 || tr_corner == 2) y = (pdi)(ti - (pdi)sh + 1);
                } else {
                    x = ti; y = curs;
                    if (tr_corner == 3 || tr_corner == 2) x = (pdi)(ti - (pdi)sw + 1);
                    if (tr_corner == 0 || tr_corner == 2) y = (pdi)(curs - (pdi)sh + 1);
                }
                tr_draw(so, sw, sh, (pdi)(x + tr_ox), (pdi)(y + tr_oy));
                if (!tr_transp && (tr_corner == 0 || tr_corner == 1)) curs = (pdi)(curs + (pdi)sw - 1);
                if (tr_transp && (tr_corner == 1 || tr_corner == 3)) curs = (pdi)(curs + (pdi)sh - 1);
                jb_sp = mark;
            }
            n++;
            if (jb_ovf) return PDE_SYNTAX;
        }
    }
    return PDE_OK;
}

/* ---- symbol dictionaries (6.5) ----------------------------------------------------------------- */
static pdw sd_huff, sd_refagg, sd_tpl, sd_rtpl, sd_nin, sd_numnew, sd_numex, sd_all;
static pdi sd_at[4][2];

static void sd_contexts(void)
{
    jw_zero(JW_INT, (pdw)(IA_N * 512u));
    jw_zero(JW_IAID, (pdw)(1u << jb_idlen));
    jw_zero(JW_GR, 8192);
}
static pdw jb_codelen(pdw n)            /* ceil(log2(n)), at least 0 */
{
    pdw k = 0;
    while ((1u << k) < n && k < 15) k++;
    return k;
}

/* decodes the new symbols into sd_all (records, input symbols first) and exports */
static pdw sd_run(void)
{
    pdw ndec = 0, e, symw, hch = 0, totw, o, k, i, j, total = (pdw)(sd_nin + sd_numnew), runs, keep, cur, cs = 0, ctw = 0;
    pdi hc = 0, sw = 0;
    int first;
    (void)first; (void)totw; (void)hch; (void)k;
    jb_idlen = jb_codelen(total);
    if (jb_idlen > 11) return PDE_LIMIT;
    if (!jb_huff) { sd_contexts(); mq_init(); }
    else { jw_zero(JW_GR, 8192); hb_align(); }
    gb_tpl = sd_tpl; gb_tpgd = 0; gb_sinkmode = 3; gb_skip = 0;
    for (i = 0; i < 4; i++) { gb_at[i][0] = sd_at[i][0]; gb_at[i][1] = sd_at[i][1]; }
    if (!jb_huff || sd_refagg) { if (!jb_huff || sd_refagg) gb_cinit(); }
    while (ndec < sd_numnew) {
        if (jb_idec(IA_DH)) return PDE_SYNTAX;
        hc = (pdi)(hc + jb_ival); if (hc < 0 || hc > 8192) return PDE_SYNTAX;
        sw = 0; cs = ndec; ctw = 0;
        for (;;) {
            if (jb_idec(IA_DW)) break;
            if (ndec >= sd_numnew) return PDE_SYNTAX;
            sw = (pdi)(sw + jb_ival); if (sw < 0 || sw > RF_MAXW) return PDE_SYNTAX;
            symw = (pdw)sw; hch = (pdw)hc;
            o = jb_alloc(symw, (pdw)hc);
            if (!o) return PDE_LIMIT;
            if (!jb_huff && !sd_refagg) {
                if (symw && hc) {
                    gb_w = symw; gb_h = (pdw)hc; gb_boff = o; gb_brb = (pdw)((symw + 7) >> 3);
                    e = gb_arith(); if (e) return e;
                }
            } else if (sd_refagg) {
                if (jb_idec(IA_AI)) return PDE_SYNTAX;
                if (jb_ival < 1) return PDE_SYNTAX;
                if (jb_ival == 1) {
                    pdw id; pdi rdx, rdy;
                    id = jb_huff ? hf_ids() : ia_id();
                    if (id >= (pdw)(sd_nin + ndec)) return PDE_SYNTAX;
                    if (jb_idec(IA_RDX)) return PDE_SYNTAX; rdx = jb_ival;
                    if (jb_idec(IA_RDY)) return PDE_SYNTAX; rdy = jb_ival;
                    if (jb_huff) { (void)hf_refsize(); mq_init(); }
                    jb_getrec(sd_all, id);
                    rf_tpl = sd_rtpl; rf_tpgr = 0;
                    e = rf_decode(o, symw, (pdw)hc, jb_rec_off, jb_rec_w, jb_rec_h, rdx, rdy);
                    if (e) return e;
                } else {
                    /* an aggregate: a small text region over the symbols so far */
                    pdw saved[8], mark = jb_sp;
                    saved[0] = cv_off; saved[1] = cv_rb; saved[2] = cv_w; saved[3] = cv_h; saved[4] = (pdw)cv_y0;
                    cv_off = o; cv_rb = (pdw)((symw + 7) >> 3); cv_w = symw; cv_h = (pdw)hc; cv_y0 = 0;
                    tr_recs = sd_all; tr_nsyms = (pdw)(sd_nin + ndec);
                    tr_ninst = (pdw)jb_ival; tr_strips = 1; tr_corner = 1; tr_comb = 0; tr_defpix = 0;
                    tr_refine = 1; tr_transp = 0; tr_dsoff = 0; tr_ox = 0; tr_oy = 0; tr_rtpl = sd_rtpl;
                    rf_tpl = sd_rtpl; rf_tpgr = 0;
                    e = tr_run();
                    cv_off = saved[0]; cv_rb = saved[1]; cv_w = saved[2]; cv_h = saved[3]; cv_y0 = (pdi)saved[4];
                    jb_sp = mark;
                    if (e) return e;
                    gb_tpl = sd_tpl; gb_sinkmode = 3; gb_tpgd = 0;
                }
            }
            jb_setrec(sd_all, (pdw)(sd_nin + ndec), o, symw, (pdw)hc);
            ctw = (pdw)(ctw + symw);
            ndec++;
        }
        if (jb_ovf) return PDE_SYNTAX;
        if (jb_huff && !sd_refagg && ndec > cs) {       /* the height class's collective bitmap (6.5.9) */
            pdw bm, tmp, rbt, yy, b, x0, mark = jb_sp, rbs;
            if (jb_idec(HF_BM)) return PDE_SYNTAX;
            bm = hf_uval; hb_align();
            rbt = (pdw)((ctw + 7) >> 3);
            tmp = jb_alloc(ctw, (pdw)hc);
            if (!tmp) return PDE_LIMIT;
            if (!bm) { for (k = 0; k < (pdw)(rbt * (pdw)hc); k++) jw_wb((pdw)(tmp + k), jb_b()); }
            else {
                jb_sub = 1; jb_subn = bm;
                gb_w = ctw; gb_h = (pdw)hc; gb_sinkmode = 3; gb_boff = tmp; gb_brb = rbt;
                e = gb_mmr();
                while (jb_subn) jb_getb();
                jb_sub = 0;
                if (e) return e;
            }
            x0 = 0;
            for (k = cs; k < ndec; k++) {
                jb_getrec(sd_all, (pdw)(sd_nin + k));
                rbs = (pdw)((jb_rec_w + 7) >> 3);
                for (yy = 0; yy < (pdw)hc; yy++) {
                    jw_get(tr_row, (pdw)(tmp + yy * rbt), rbt);
                    tr_row[rbt] = 0; tr_row[rbt + 1] = 0;
                    for (b = 0; b < rbs; b++) {
                        pdw v = jb_srcbyte(tr_row, (pdi)(x0 + b * 8));
                        if (b == (pdw)(rbs - 1) && (jb_rec_w & 7)) v &= (pdw)(0xFF00 >> (jb_rec_w & 7)) & 0xFF;
                        jw_wb((pdw)(jb_rec_off + yy * rbs + b), v);
                    }
                }
                x0 = (pdw)(x0 + jb_rec_w);
            }
            jb_sp = mark;
        }
    }
    /* the export flags (6.5.10) */
    if (jb_idlen > 11) return PDE_LIMIT;
    keep = 0; cur = 0; runs = 0;
    for (i = 0; i < total; ) {
        pdw run;
        if (jb_idec(IA_EX)) return PDE_SYNTAX;
        if (jb_ival < 0 || (pdw)jb_ival > (pdw)(total - i)) return PDE_SYNTAX;
        run = (pdw)jb_ival;
        if (cur) for (j = 0; j < run; j++) {
            jb_getrec(sd_all, (pdw)(i + j));
            jb_setrec(sd_all, keep, jb_rec_off, jb_rec_w, jb_rec_h);
            keep++;
        }
        i = (pdw)(i + run); cur ^= 1;
        if (++runs > (pdw)(total * 2 + 4)) return PDE_SYNTAX;
    }
    if (keep != sd_numex) { /* the declared count is advisory: the flags decide */ }
    sd_numex = keep;
    return PDE_OK;
}

/* ---- segment headers (7.2) ------------------------------------------------------- */
static pdw jb_hi, jb_lo;        /* the last 32-bit field read                      */
static pdb jb_eof;
static pdw jb_sn, jb_st, jb_nref;
static pdw jb_ref[32];

static int jb_raw(void)
{
    int c = ix_get();
    if (c < 0) { jb_eof = 1; return 0; }
    return c;
}
static void jb_r32(void)
{
    pdw a = (pdw)jb_raw(), b = (pdw)jb_raw(), c = (pdw)jb_raw(), d = (pdw)jb_raw();
    jb_hi = (pdw)((a << 8) | b); jb_lo = (pdw)((c << 8) | d);
}
static pdw jb_b(void)           /* in-segment byte; 0 past the end */
{
    int c = jb_getb();
    if (c < 0) { jb_eof = 1; return 0; }
    return (pdw)c;
}
static void jb_b32(void)
{
    pdw a = jb_b(), b = jb_b(), c = jb_b(), d = jb_b();
    jb_hi = (pdw)((a << 8) | b); jb_lo = (pdw)((c << 8) | d);
}

/* Reads one segment header; the data length is left in jb_lh/jb_ll.
 * Returns 0, or 1 at the end of the stream / on a header that cannot be used. */
static int jb_hdr(void)
{
    pdw fl, rt, i, nbytes, extra;
    jb_eof = 0;
    jb_r32();
    if (jb_eof) return 1;
    if (jb_hi) { jb_sn = 0xFFFFu; } else jb_sn = jb_lo;
    fl = (pdw)jb_raw();
    jb_st = (pdw)(fl & 63);
    rt = (pdw)jb_raw();
    if ((rt >> 5) == 7) {
        pdw a = (pdw)jb_raw(), b = (pdw)jb_raw(), c = (pdw)jb_raw();
        if ((rt & 0x1F) || a) return 1;
        jb_nref = (pdw)((b << 8) | c);
        extra = (pdw)((jb_nref + 8) >> 3);
        for (i = 0; i < extra; i++) jb_raw();
    } else jb_nref = (pdw)(rt >> 5);
    nbytes = jb_sn <= 256 ? 1u : jb_sn <= 65535u ? 2u : 4u;
    for (i = 0; i < jb_nref; i++) {
        pdw v = 0, k;
        for (k = 0; k < nbytes; k++) v = (pdw)((v << 8) | (pdw)jb_raw());     /* the low 16 bits */
        if (i < 32) jb_ref[i] = v;
    }
    if (jb_nref > 32) return 1;
    if (fl & 0x40) { jb_raw(); jb_raw(); jb_raw(); jb_raw(); } else jb_raw();
    jb_r32();
    if (jb_eof) return 1;
    jb_lh = jb_hi; jb_ll = jb_lo;
    jb_unb = (pdb)(jb_hi == 0xFFFFu && jb_lo == 0xFFFFu);
    return 0;
}
static void jb_skip(void)
{
    if (jb_unb) return;
    while (jb_lh || jb_ll) { if (jb_getb() < 0) { jb_lh = jb_ll = 0; break; } }
}

/* ---- the page ------------------------------------------------------------------------- */
static pdw jb_pw, jb_ph, jb_pdef, jb_pcomb, jb_gl, jb_ih;
static pdw jb_by0, jb_brows, jb_rb;
static pdw jb_rx, jb_ry, jb_rw, jb_rh, jb_rop;

static pdw jb_satadd(pdw a, pdw b) { pdw s = (pdw)(a + b); return s < a ? 0xFFFFu : s; }

static void jb_pageinfo(void)
{
    pdw f;
    jb_b32(); jb_pw = jb_lo; if (jb_hi) jb_pw = 0;
    jb_b32(); jb_ph = jb_lo; if (jb_hi == 0xFFFFu) jb_ph = jb_ih; else if (jb_hi) jb_ph = 0;
    jb_b32(); jb_b32();         /* resolution */
    f = jb_b();
    jb_pdef = (pdw)((f >> 2) & 1);
    jb_pcomb = (pdw)((f >> 3) & 3);
    jb_b(); jb_b();             /* striping: height is known from the page or the image */
    if (!jb_ph) jb_ph = jb_ih;
}

/* region segment information field (7.4.1) */
static int jb_region(void)
{
    pdw f;
    jb_b32(); jb_rw = jb_lo; if (jb_hi) jb_rw = 0xFFFFu;
    jb_b32(); jb_rh = jb_lo; if (jb_hi) jb_rh = 0xFFFFu;
    jb_b32(); jb_rx = jb_lo; if (jb_hi) jb_rx = 0xFFFFu;
    jb_b32(); jb_ry = jb_lo; if (jb_hi) jb_ry = 0xFFFFu;
    f = jb_b();
    jb_rop = (pdw)(f & 7);
    return jb_eof ? 1 : 0;
}
static int jb_inband(void)
{
    return jb_ry < (pdw)(jb_by0 + jb_brows) && jb_satadd(jb_ry, jb_rh) > jb_by0 && jb_rx < jb_pw;
}

static void jb_cv_band(void)
{
    cv_off = JW_BAND; cv_rb = jb_rb; cv_w = jb_pw; cv_h = jb_brows; cv_y0 = (pdi)jb_by0;
}

static pdw jb_segment_generic(void)
{
    pdw f, k;
    if (!jb_rw || !jb_rh) return PDE_OK;
    f = jb_b();
    gb_tpl = (pdw)((f >> 1) & 3); gb_tpgd = (pdw)((f >> 3) & 1);
    if (f & 16) { jb_inc |= JBF_UNSUP; return PDE_OK; }
    if (!(f & 1)) {
        for (k = 0; k < (gb_tpl ? 1u : 4u); k++) {
            gb_at[k][0] = (pdi)(signed char)jb_b(); gb_at[k][1] = (pdi)(signed char)jb_b();
            if (gb_at[k][1] < -2 || gb_at[k][1] > 0 || (gb_at[k][1] == 0 && gb_at[k][0] >= 0)) { jb_inc |= JBF_UNSUP; return PDE_OK; }
        }
    }
    gb_w = jb_rw; gb_h = jb_rh; gb_sinkmode = 0; gb_dx = (pdi)jb_rx; gb_dy = (pdi)jb_ry; gb_op = jb_rop; gb_skip = 0;
    jb_cv_band();
    if (f & 1) return gb_mmr();
    mq_init();
    gb_cinit();
    return gb_arith();
}


/* ---- Huffman (T.88 annex B) ---------------------------------------------------------------- */
static const pdi hf_s_low[202] = { 0, 16, 272, -1, 32767, 0, 0, 1, 2, 3, 11, -1, 75, 0, -256, 0, 1, 2, 3, 11, -257, 75, 0, 1, 2, 3, 4, 12, -1, 76, 0, -255, 1, 2, 3, 4, 12, -256, 76, 0, -2048, -1024, -512, -256, -128, -64, -32, 0, 128, 256, 512, 1024, -2049, 2048, 0, -1024, -512, -256, -128, -64, -32, 0, 32, 64, 128, 256, 512, 1024, -1025, 2048, 0, -15, -7, -5, -3, -2, -1, 0, 2, 3, 4, 20, 22, 38, 70, 134, 262, 390, 646, -16, 1670, 0, -31, -15, -11, -7, -5, -3, -1, 1, 3, 5, 7, 39, 43, 75, 139, 267, 523, 779, 1291, -32, 3339, 0, -21, -5, -4, -3, -2, 2, 3, 4, 5, 6, 70, 102, 134, 198, 326, 582, 1094, 2118, -22, 4166, 0, 1, 2, 4, 5, 7, 9, 13, 17, 21, 29, 45, 77, -1, 141, 0, 1, 2, 3, 5, 6, 8, 10, 11, 13, 17, 25, 41, -1, 73, 0, 1, 2, 3, 4, 5, 7, 15, 17, 21, 29, 45, 77, -1, 141, 0, -2, -1, 0, 1, 2, -1, 3, 0, -24, -8, -4, -2, -1, 0, 1, 2, 3, 5, 9, -25, 25, 0 };
static const pdb hf_s_pl[202] = { 1, 2, 3, 0, 3, 0, 1, 2, 3, 4, 5, 0, 6, 6, 8, 1, 2, 3, 4, 5, 8, 7, 6, 1, 2, 3, 4, 5, 0, 5, 0, 7, 1, 2, 3, 4, 5, 7, 6, 0, 5, 4, 4, 4, 5, 5, 4, 2, 3, 3, 4, 4, 6, 6, 0, 4, 3, 4, 5, 5, 4, 4, 5, 5, 4, 3, 3, 3, 5, 5, 0, 8, 9, 8, 9, 7, 4, 2, 5, 6, 3, 6, 4, 4, 5, 5, 6, 7, 6, 9, 9, 2, 8, 9, 8, 9, 7, 4, 3, 3, 5, 6, 3, 6, 4, 4, 5, 5, 6, 7, 6, 9, 9, 2, 7, 8, 7, 5, 2, 5, 6, 7, 8, 2, 5, 6, 6, 6, 6, 6, 6, 7, 8, 8, 2, 1, 2, 4, 4, 5, 5, 6, 7, 7, 7, 7, 7, 0, 7, 0, 1, 2, 3, 5, 5, 6, 7, 7, 7, 7, 7, 8, 0, 8, 0, 1, 3, 4, 5, 4, 3, 6, 6, 6, 6, 6, 7, 0, 7, 0, 3, 3, 1, 3, 3, 0, 0, 0, 7, 6, 5, 4, 3, 1, 3, 4, 5, 6, 7, 7, 7, 0 };
static const pdb hf_s_rl[202] = { 4, 8, 16, 32, 32, 0, 0, 0, 0, 3, 6, 32, 32, 0, 8, 0, 0, 0, 3, 6, 32, 32, 0, 0, 0, 0, 3, 6, 32, 32, 0, 8, 0, 0, 0, 3, 6, 32, 32, 0, 10, 9, 8, 7, 6, 5, 5, 7, 7, 8, 9, 10, 32, 32, 0, 9, 8, 7, 6, 5, 5, 5, 5, 6, 7, 8, 9, 10, 32, 32, 0, 3, 1, 1, 0, 0, 0, 1, 0, 0, 4, 1, 4, 5, 6, 7, 7, 8, 10, 32, 32, 0, 4, 2, 2, 1, 1, 1, 1, 1, 1, 1, 5, 2, 5, 6, 7, 8, 8, 9, 11, 32, 32, 0, 4, 0, 0, 0, 2, 0, 0, 0, 0, 6, 5, 5, 6, 7, 8, 9, 10, 11, 32, 32, 0, 0, 1, 0, 1, 1, 2, 2, 2, 3, 4, 5, 6, 32, 32, 0, 0, 0, 1, 0, 1, 1, 0, 1, 2, 3, 4, 5, 32, 32, 0, 0, 0, 0, 0, 1, 3, 1, 2, 3, 4, 5, 6, 32, 32, 0, 0, 0, 0, 0, 0, 32, 32, 0, 4, 2, 1, 0, 0, 0, 0, 0, 1, 2, 4, 32, 32, 0 };
static const pdb hf_s_nn[16] = { 0, 3, 5, 6, 5, 6, 12, 13, 18, 19, 18, 12, 12, 12, 5, 11 };
static const pdw hf_s_start[17] = { 0, 0, 6, 14, 23, 31, 40, 55, 71, 92, 114, 135, 150, 165, 180, 188, 202 };


static void jb_rg_add(pdw kind, pdw ofs, pdw cnt, pdw a, pdw b);

#define HF_NS 8                 /* simultaneous tables (text region: FS DS DT RDW RDH RDX RDY RSIZE) */
#define HF_ML 43
static pdi hf_low[HF_NS][HF_ML];
static pdb hf_pl[HF_NS][HF_ML], hf_rl[HF_NS][HF_ML];
static pdw hf_cd[HF_NS][HF_ML];
static pdb hf_n[HF_NS];
static pdb hf_slot[14];
static pdw hf_logs;

/* the bit reader over the segment */
static pdw hb_byte, hb_n;
static pdw hb_bit(void)
{
    if (!hb_n) { int c = jb_getb(); hb_byte = c < 0 ? 0 : (pdw)c; hb_n = 8; if (c < 0) jb_eof = 1; }
    hb_n--;
    return (pdw)((hb_byte >> hb_n) & 1);
}
static pdw hb_bits(pdw n)       /* n <= 16 */
{
    pdw v = 0;
    while (n--) v = (pdw)((v << 1) | hb_bit());
    return v;
}
static void hb_align(void) { hb_n = 0; }

/* B.3: codes from the prefix lengths, in table order */
static void hf_assign(pdw t)
{
    pdw n = (pdw)(hf_n[t] + 3), cnt[18], first = 0, len, i, code, mx = 0;
    for (i = 0; i < 18; i++) cnt[i] = 0;
    for (i = 0; i < n; i++) { if (hf_pl[t][i] > 16) hf_pl[t][i] = 0; cnt[hf_pl[t][i]]++; if (hf_pl[t][i] > mx) mx = hf_pl[t][i]; }
    cnt[0] = 0;
    for (len = 1; len <= mx; len++) {
        first = (pdw)((first + cnt[len - 1]) << 1);
        code = first;
        for (i = 0; i < n; i++) if (hf_pl[t][i] == len) hf_cd[t][i] = code++;
    }
}
static void hf_std(pdw t, pdw k)
{
    pdw a = hf_s_start[k], n = hf_s_nn[k], i;
    hf_n[t] = (pdb)n;
    for (i = 0; i < n + 3; i++) { hf_low[t][i] = hf_s_low[a + i]; hf_pl[t][i] = hf_s_pl[a + i]; hf_rl[t][i] = hf_s_rl[a + i]; }
    hf_assign(t);
}
/* a custom table in the workspace: one byte of normal-line count, then 4 bytes a line */
static void hf_custom(pdw t, pdw rg)
{
    pdw o = jb_rg_ofs[rg], n = jb_rg_cnt[rg], i;
    hf_n[t] = (pdb)n;
    for (i = 0; i < n + 3; i++) {
        hf_low[t][i] = (pdi)jw_rw((pdw)(o + i * 4)); hf_pl[t][i] = jw_rb((pdw)(o + i * 4 + 2)); hf_rl[t][i] = jw_rb((pdw)(o + i * 4 + 3));
    }
    hf_assign(t);
}

/* one value from table t: 1 for OOB; else jb_ival and hf_uval */
static pdw jb_tabs[8], jb_ntabs;
static pdb hf_big;
static int hf_dec_t(pdw t)
{
    pdw code = 0, len, l, n = (pdw)(hf_n[t] + 3), rl, mag, hi, lo;
    int neg = 0;
    for (len = 1; len <= 16; len++) {
        code = (pdw)((code << 1) | hb_bit());
        for (l = 0; l < n; l++) if (hf_pl[t][l] == len && hf_cd[t][l] == code) goto found;
    }
    jb_ovf = 1; return 1;
found:
    if (l == (pdw)(n - 1)) return 1;                    /* the OOB line */
    rl = hf_rl[t][l];
    if (l < n - 3) {                                    /* a normal line */
        pdw off = hb_bits(rl);
        if (hf_low[t][l] >= 0) { mag = (pdw)((pdw)hf_low[t][l] + off); if (mag < off) jb_ovf = 1; }
        else if (off >= (pdw)(-hf_low[t][l])) mag = (pdw)(off + hf_low[t][l]);
        else { mag = (pdw)(-hf_low[t][l] - off); neg = 1; }
    } else {                                            /* lower (l = n - 3) or upper (l = n - 2): a 32-bit offset */
        hi = hb_bits(16); lo = hb_bits(16);
        if (hi) { jb_ovf = 1; return 1; }
        if (l == (pdw)(n - 3)) {                        /* low - offset */
            mag = (pdw)(lo + (pdw)(-hf_low[t][l])); if (mag < lo) jb_ovf = 1; neg = 1;
            if (hf_low[t][l] >= 0) { jb_ovf = 1; }
        } else { mag = (pdw)((pdw)hf_low[t][l] + lo); if (mag < lo) jb_ovf = 1; }
    }
    hf_uval = mag; hf_big = 0;
    if (mag > 0x7FFF) { hf_big = 1; jb_ival = 0; }
    else jb_ival = (pdi)(neg ? -(pdi)mag : (pdi)mag);
    return 0;
}

static int hf_dec(pdw role)
{
    if (role == IA_IT) { jb_ival = (pdi)hb_bits(hf_logs); return 0; }
    if (role == IA_RI) { jb_ival = (pdi)hb_bit(); return 0; }
    {
        int r = hf_dec_t(hf_slot[role]);
        if (hf_big && role != HF_BM) jb_ovf = 1;
        return r;
    }
}

/* the symbol-ID codes (7.4.3.1.7): canonical codes over lengths kept in the workspace */
static pdw hf_cnt[18], hf_first[18], hf_off[18], hf_sorted, hf_nsym;
static void hf_canon(pdw lens, pdw n, pdw sorted)       /* lens: n bytes; sorted: n words */
{
    pdw i, len, pos[18], first = 0;
    for (i = 0; i < 18; i++) hf_cnt[i] = 0;
    for (i = 0; i < n; i++) hf_cnt[jw_rb((pdw)(lens + i)) & 15]++;
    hf_cnt[0] = 0;
    hf_off[1] = 0;
    for (len = 1; len < 17; len++) {
        first = (pdw)((first + hf_cnt[len - 1]) << 1);
        hf_first[len] = first;
        if (len > 1) hf_off[len] = (pdw)(hf_off[len - 1] + hf_cnt[len - 1]);
        pos[len] = hf_off[len];
    }
    for (i = 0; i < n; i++) {
        len = jw_rb((pdw)(lens + i)) & 15;
        if (len) { jw_ww((pdw)(sorted + pos[len] * 2), i); pos[len]++; }
    }
}
static int hf_canon_dec(pdw sorted)                      /* a symbol, or -1 */
{
    pdw code = 0, len;
    for (len = 1; len < 17; len++) {
        code = (pdw)((code << 1) | hb_bit());
        if (hf_cnt[len] && code >= hf_first[len] && (pdw)(code - hf_first[len]) < hf_cnt[len])
            return (int)jw_rw((pdw)(sorted + (pdw)(hf_off[len] + code - hf_first[len]) * 2));
    }
    return -1;
}
static pdw hf_ids(void)
{
    int v = hf_canon_dec(hf_sorted);
    if (v < 0) { jb_ovf = 1; return 0; }
    return (pdw)v;
}
static pdw hf_refsize(void) { jb_ovf = 1; return 0; }

/* reads the symbol ID table of a Huffman text region; nsyms symbols */
static pdw hf_idtable(pdw nsyms)
{
    pdw rl, lens, rsorted, i, c, run, prev = 0, n;
    int v;
    rl = jb_alloc(40 * 8, 1);                       /* 35 run-code lengths */
    rsorted = jb_alloc(40 * 16, 1);
    lens = jb_alloc((pdw)(nsyms * 8), 1);
    hf_sorted = jb_alloc((pdw)(nsyms * 16), 1);
    if (!rl || !rsorted || !lens || !hf_sorted) return PDE_LIMIT;
    hb_align();
    for (i = 0; i < 35; i++) jw_wb((pdw)(rl + i), hb_bits(4));
    hf_canon(rl, 35, rsorted);
    i = 0;
    while (i < nsyms) {
        v = hf_canon_dec(rsorted);
        if (v < 0) return PDE_SYNTAX;
        c = (pdw)v;
        if (c < 32) { jw_wb((pdw)(lens + i), c); prev = c; i++; continue; }
        if (c == 32) { run = (pdw)(hb_bits(2) + 3); if (!i) return PDE_SYNTAX; c = prev; }
        else if (c == 33) { run = (pdw)(hb_bits(3) + 3); c = 0; }
        else { run = (pdw)(hb_bits(7) + 11); c = 0; }
        if (i + run > nsyms) return PDE_SYNTAX;
        for (n = 0; n < run; n++) jw_wb((pdw)(lens + i + n), c);
        i = (pdw)(i + run);
    }
    hb_align();
    for (i = 0; i < nsyms; i++) if (jw_rb((pdw)(lens + i)) > 15) return PDE_LIMIT;
    hf_canon(lens, nsyms, hf_sorted);
    return PDE_OK;
}

/* a custom table segment (type 53) */
static pdw jb_segment_table(void)
{
    pdw f, ps, rs, o, nn = 0, n;
    pdi cur, high, lowv;
    f = jb_b(); ps = (pdw)(((f >> 1) & 7) + 1); rs = (pdw)(((f >> 4) & 7) + 1);
    jb_b32(); cur = (pdi)jb_lo; if (jb_hi != 0 && jb_hi != 0xFFFFu) return PDE_LIMIT;
    jb_b32(); high = (pdi)jb_lo; if (jb_hi != 0 && jb_hi != 0xFFFFu) return PDE_LIMIT;
    lowv = cur;
    o = jb_alloc((pdw)(8 * 4 * HF_ML), 1);
    if (!o) return PDE_LIMIT;
    hb_align();
    while (cur < high) {
        pdw pl = hb_bits(ps), rl = hb_bits(rs);
        if (nn >= HF_ML - 3 || rl > 15) return PDE_LIMIT;
        jw_ww((pdw)(o + nn * 4), (pdw)cur); jw_wb((pdw)(o + nn * 4 + 2), pl); jw_wb((pdw)(o + nn * 4 + 3), rl);
        cur = (pdi)(cur + (pdi)(1u << rl)); nn++;
    }
    n = nn;
    jw_ww((pdw)(o + n * 4), (pdw)(pdi)(lowv - 1)); jw_wb((pdw)(o + n * 4 + 2), hb_bits(ps)); jw_wb((pdw)(o + n * 4 + 3), 32);
    n++;
    jw_ww((pdw)(o + n * 4), (pdw)high); jw_wb((pdw)(o + n * 4 + 2), hb_bits(ps)); jw_wb((pdw)(o + n * 4 + 3), 32);
    n++;
    jw_ww((pdw)(o + n * 4), 0); jw_wb((pdw)(o + n * 4 + 2), (f & 1) ? hb_bits(ps) : 0); jw_wb((pdw)(o + n * 4 + 3), 0);
    jb_sp = (pdw)(o + (pdw)((n + 1) * 4));
    jb_rg_add(RGK_TAB, o, nn, 0, 0);
    return PDE_OK;
}


/* ---- pattern dictionaries (6.7) and halftone regions (6.6) -------------------------------- */
#define RGK_HT 5            /* decoded grid values: ofs = one byte a cell, a = columns, b = rows */

static pdw ht_vals, ht_gw, ht_gh, ht_first, ht_hbw, ht_hbh, ht_pw, ht_ph, ht_hrx, ht_hry;
static struct pd_u32 ht_gx, ht_gy, ht_t1, ht_t2;
static pdi ht_px, ht_py;

/* the position of grid cell (row m, column n) (6.6.5.2): x = (HGX + m*HRY + n*HRX) >> 8,
 * y = (HGY + m*HRX - n*HRY) >> 8, as signed 16 bit; returns 0 when it does not fit */
static pdb ht_gxn, ht_gyn;          /* HGX / HGY are negative; ht_gx / ht_gy then hold the magnitude */
static struct pd_u32 ht_zero;

/* (P - N) >> 8 as a signed 16-bit value (arithmetic, so a negative difference rounds down);
 * P and N are non-negative, so the pdmath unsigned operations never wrap. 0 = does not fit. */
static int ht_diff(const struct pd_u32 *P, const struct pd_u32 *N, pdi *out)
{
    struct pd_u32 *d = &ht_t2;
    if (pdm_u32_cmp(P, N) >= 0) {
        pdm_u32_sub(d, P, N); pdm_u32_shr(d, d, 8);
        if (d->hi || d->lo > 32767u) return 0;
        *out = (pdi)d->lo; return 1;
    }
    pdm_u32_sub(d, N, P); pdm_u32_add16(d, 255); pdm_u32_shr(d, d, 8);
    if (d->hi || d->lo > 32767u) return 0;
    *out = (pdi)(-(pdi)d->lo); return 1;
}
static struct pd_u32 ht_p, ht_n;
static int ht_cell(pdw m, pdw n)
{
    pdm_u32_mul16(&ht_p, m, ht_hry);
    pdm_u32_mul16(&ht_t1, n, ht_hrx);
    pdm_u32_add(&ht_p, &ht_p, &ht_t1);
    ht_n = ht_zero;
    if (ht_gxn) ht_n = ht_gx; else pdm_u32_add(&ht_p, &ht_p, &ht_gx);
    if (!ht_diff(&ht_p, &ht_n, &ht_px)) return 0;
    pdm_u32_mul16(&ht_p, m, ht_hrx);
    pdm_u32_mul16(&ht_n, n, ht_hry);
    if (ht_gyn) pdm_u32_add(&ht_n, &ht_n, &ht_gy); else pdm_u32_add(&ht_p, &ht_p, &ht_gy);
    return ht_diff(&ht_p, &ht_n, &ht_py);
}
static int gb_skipf(pdw x, pdw y)                  /* HSKIP[m = y, n = x]: the pattern lies wholly outside the region */
{
    if (!ht_cell(y, x)) return 1;
    return (int)((ht_px + (pdi)ht_pw <= 0) || ht_px >= (pdi)ht_hbw || (ht_py + (pdi)ht_ph <= 0) || ht_py >= (pdi)ht_hbh);
}
static void ht_plane_row(pdw y, const pdb *row)    /* gray coding folded in: value = value << 1 | (bit ^ last bit) */
{
    pdw x, o = (pdw)(ht_vals + y * ht_gw), v, bit;
    for (x = 0; x < ht_gw; x++) {
        v = jw_rb((pdw)(o + x));
        bit = (pdw)((row[x >> 3] >> (7 - (x & 7))) & 1);
        if (!ht_first) bit ^= (pdw)(v & 1);
        jw_wb((pdw)(o + x), (v << 1) | bit);
    }
}

/* columns [x0, x0 + w) of a w2 x h bitmap at src (rows of rbt bytes) into the bitmap dst */
static void jb_split(pdw src, pdw rbt, pdw x0, pdw w, pdw h, pdw dst)
{
    pdw yy, b, rbs = (pdw)((w + 7) >> 3), v;
    for (yy = 0; yy < h; yy++) {
        jw_get(tr_row, (pdw)(src + yy * rbt), rbt);
        tr_row[rbt] = 0; tr_row[rbt + 1] = 0;
        for (b = 0; b < rbs; b++) {
            v = jb_srcbyte(tr_row, (pdi)(x0 + b * 8));
            if (b == (pdw)(rbs - 1) && (w & 7)) v &= (pdw)((0xFF00 >> (w & 7)) & 0xFF);
            jw_wb((pdw)(dst + yy * rbs + b), v);
        }
    }
}

static pdw jb_segment_pattern(void)
{
    pdw f, pw, ph, np, tmp, pats, mark = jb_sp, e, k;
    f = jb_b(); pw = jb_b(); ph = jb_b();
    jb_b32(); if (jb_hi || jb_lo > 255) return PDE_LIMIT;
    np = (pdw)(jb_lo + 1);
    if (!pw || !ph) return PDE_SYNTAX;
    pats = jb_alloc(pw, (pdw)(ph * np));
    tmp = jb_alloc((pdw)(np * pw), ph);
    if (!pats || !tmp) { jb_sp = mark; return PDE_LIMIT; }
    gb_tpl = (pdw)((f >> 1) & 3); gb_tpgd = 0; gb_w = (pdw)(np * pw); gb_h = ph; gb_sinkmode = 3; gb_boff = tmp;
    gb_brb = (pdw)((gb_w + 7) >> 3); gb_skip = 0;
    gb_at[0][0] = (pdi)-(pdi)pw; gb_at[0][1] = 0; gb_at[1][0] = -3; gb_at[1][1] = -1;
    gb_at[2][0] = 2; gb_at[2][1] = -2; gb_at[3][0] = -2; gb_at[3][1] = -2;
    if (f & 1) { gb_mmr_cont = 0; e = gb_mmr(); }
    else { mq_init(); gb_cinit(); e = gb_arith(); }
    if (e) { jb_sp = mark; return e; }
    for (k = 0; k < np; k++) jb_split(tmp, gb_brb, (pdw)(k * pw), pw, ph, (pdw)(pats + k * ((pw + 7) >> 3) * ph));
    jb_sp = tmp;
    jb_rg_add(RGK_PAT, pats, np, pw, ph);
    return PDE_OK;
}

static pdw jb_segment_halftone(void)
{
    pdw f, tpl, g, pat, npats, bpp, j, e, direct, hcomb, defpix, tmp = 0, trows = 0, y0r = 0, rbr = 0, msp = jb_sp, m, n, rbp, k;
    pdi sx, sy;
    f = jb_b();
    tpl = (pdw)((f >> 1) & 3); hcomb = (pdw)((f >> 4) & 7); defpix = (pdw)((f >> 7) & 1);
    jb_b32(); if (jb_hi || jb_lo > 8192) return PDE_LIMIT; ht_gw = jb_lo;
    jb_b32(); if (jb_hi || jb_lo > 8192) return PDE_LIMIT; ht_gh = jb_lo;
    jb_b32(); ht_gx.hi = jb_hi; ht_gx.lo = jb_lo; ht_gxn = (pdb)((jb_hi & 0x8000u) != 0);
    if (ht_gxn) { ht_gx.lo = (pdw)(~jb_lo + 1); ht_gx.hi = (pdw)(~jb_hi + (jb_lo ? 0 : 1)); }
    jb_b32(); ht_gy.hi = jb_hi; ht_gy.lo = jb_lo; ht_gyn = (pdb)((jb_hi & 0x8000u) != 0);
    if (ht_gyn) { ht_gy.lo = (pdw)(~jb_lo + 1); ht_gy.hi = (pdw)(~jb_hi + (jb_lo ? 0 : 1)); }
    ht_hrx = (pdw)(jb_b() << 8); ht_hrx |= jb_b();
    ht_hry = (pdw)(jb_b() << 8); ht_hry |= jb_b();
    (void)sx; (void)sy;
    pat = 0xFFFFu;
    for (j = 0; j < jb_nref && j < 32; j++) { g = jb_rg_find(jb_ref[j]); if (g != 0xFFFFu && jb_rg_kind[g] == RGK_PAT) pat = g; }
    if (pat == 0xFFFFu) return PDE_SYNTAX;
    npats = jb_rg_cnt[pat]; ht_pw = jb_rg_a[pat]; ht_ph = jb_rg_b[pat]; rbp = (pdw)((ht_pw + 7) >> 3);
    ht_hbw = jb_rw; ht_hbh = jb_rh;
    for (bpp = 0; (1u << bpp) < npats; bpp++) {}
    if (!bpp) bpp = 1;
    /* the grid values: decoded once, kept */
    g = jb_rg_find(jb_sn);
    if (g != 0xFFFFu && jb_rg_kind[g] == RGK_BAD) return PDE_LIMIT;
    if (g == 0xFFFFu) {
        pdw cells;
        if (ht_gw && ht_gh > (pdw)(0xFFFFu / ht_gw)) return PDE_LIMIT;
        cells = (pdw)(ht_gw * ht_gh);
        ht_vals = jb_alloc((pdw)(cells ? cells : 1), 8);        /* 8 bits wide: a byte a cell */
        if (!ht_vals) { jb_rg_add(RGK_BAD, 0, 0, 0, 0); return PDE_LIMIT; }
        ht_vals = (pdw)(ht_vals);
        /* jb_alloc(w, h) takes w bits: ask for cells x 8 bits = cells bytes */
        jb_sp = (pdw)(ht_vals + cells);
        gb_tpl = tpl; gb_tpgd = 0; gb_w = ht_gw; gb_h = ht_gh; gb_sinkmode = 1;
        gb_skip = (pdw)((f >> 3) & 1);
        gb_at[0][0] = tpl <= 1 ? 3 : 2; gb_at[0][1] = -1; gb_at[1][0] = -3; gb_at[1][1] = -1;
        gb_at[2][0] = 2; gb_at[2][1] = -2; gb_at[3][0] = -2; gb_at[3][1] = -2;
        e = PDE_OK;
        if (!(f & 1)) { mq_init(); gb_cinit(); }
        for (j = bpp; j > 0 && !e; j--) {
            ht_first = (pdw)(j == bpp);
            if (f & 1) { gb_mmr_cont = (pdb)!ht_first; e = gb_mmr(); cf_mmr_end(); }
            else e = gb_arith();
        }
        gb_mmr_cont = 0; gb_skip = 0;
        if (e) { jb_sp = msp; jb_rg_add(RGK_BAD, 0, 0, 0, 0); return e; }
        jb_rg_add(RGK_HT, ht_vals, cells, ht_gw, ht_gh);
        g = (pdw)(jb_nrg - 1);
    }
    ht_vals = jb_rg_ofs[g];
    /* the canvas */
    direct = (pdw)(!defpix && jb_rop == 0 && hcomb == 0);
    msp = jb_sp;
    if (direct) { jb_cv_band(); tr_ox = (pdi)jb_rx; tr_oy = (pdi)jb_ry; }
    else {
        pdw by1 = (pdw)(jb_by0 + jb_brows), ye = jb_satadd(jb_ry, jb_rh), y1r;
        y0r = jb_ry > jb_by0 ? jb_ry : jb_by0; y1r = ye < by1 ? ye : by1;
        trows = (pdw)(y1r - y0r); rbr = (pdw)((jb_rw + 7) >> 3);
        tmp = jb_alloc(jb_rw, trows);
        if (!tmp) { jb_sp = msp; return PDE_LIMIT; }
        if (defpix) for (k = 0; k < (pdw)(rbr * trows); k++) jw_wb((pdw)(tmp + k), 0xFF);
        cv_off = tmp; cv_rb = rbr; cv_w = jb_rw; cv_h = trows; cv_y0 = (pdi)(y0r - jb_ry);
        tr_ox = 0; tr_oy = 0;
    }
    tr_comb = hcomb;
    for (m = 0; m < ht_gh; m++) {
        for (n = 0; n < ht_gw; n++) {
            pdw v;
            if (!ht_cell(m, n)) continue;
            if ((pdi)(ht_py + (pdi)ht_ph + tr_oy) <= cv_y0 || (pdi)(ht_py + tr_oy) >= (pdi)(cv_y0 + (pdi)cv_h)) continue;
            if (ht_px + (pdi)ht_pw <= 0 || ht_px >= (pdi)ht_hbw) continue;
            v = jw_rb((pdw)(ht_vals + m * ht_gw + n));
            if (v >= npats) v = (pdw)(npats - 1);
            tr_draw((pdw)(jb_rg_ofs[pat] + v * rbp * ht_ph), ht_pw, ht_ph, (pdi)(ht_px + tr_ox), (pdi)(ht_py + tr_oy));
        }
    }
    if (!direct) {
        jb_cv_band();
        for (k = 0; k < trows; k++) {
            jw_get(tr_row, (pdw)(tmp + k * rbr), rbr);
            tr_row[rbr] = 0; tr_row[rbr + 1] = 0;
            cv_row((pdi)(y0r + k), (pdi)jb_rx, tr_row, jb_rw, jb_rop);
        }
    }
    jb_sp = msp;
    return PDE_OK;
}

/* the table for a slot: a standard one, or the next custom table the segment refers to */
static pdw jb_tnext;
static pdw hf_select(pdw slot, pdw std)
{
    if (std) { hf_std(slot, std); return PDE_OK; }
    if (jb_tnext >= jb_ntabs) return PDE_SYNTAX;
    hf_custom(slot, jb_tabs[jb_tnext++]);
    return PDE_OK;
}


/* ---- segment-level wrappers ------------------------------------------------------------------ */
static void jb_rg_add(pdw kind, pdw ofs, pdw cnt, pdw a, pdw b)
{
    if (jb_nrg >= JB_NRG) { jb_inc |= JBF_UNSUP; return; }
    jb_rg_num[jb_nrg] = jb_sn; jb_rg_kind[jb_nrg] = (pdb)kind; jb_rg_ofs[jb_nrg] = ofs;
    jb_rg_cnt[jb_nrg] = cnt; jb_rg_a[jb_nrg] = a; jb_rg_b[jb_nrg] = b;
    jb_nrg++;
}

static void jb_collect_tabs(void)
{
    pdw i, g;
    jb_ntabs = 0; jb_tnext = 0;
    for (i = 0; i < jb_nref && i < 32; i++) {
        g = jb_rg_find(jb_ref[i]);
        if (g != 0xFFFFu && jb_rg_kind[g] == RGK_TAB && jb_ntabs < 8) jb_tabs[jb_ntabs++] = g;
    }
}

static pdw jb_segment_symdict(void)
{
    pdw f, k, nin = 0, i, j, e, msp = jb_sp, mhp = jb_hp, total, M;
    f = (pdw)(jb_b() << 8); f |= jb_b();
    sd_huff = (pdw)(f & 1); sd_refagg = (pdw)((f >> 1) & 1);
    sd_tpl = (pdw)((f >> 10) & 3); sd_rtpl = (pdw)((f >> 12) & 1);
    jb_huff = (pdb)sd_huff;
    if (f & 0x300) { jb_inc |= JBF_UNSUP; return PDE_LIMIT; }   /* bitmap contexts retained / used */
    if (sd_huff && sd_refagg) return PDE_LIMIT;                 /* Huffman with aggregation: not supported */
    if (sd_huff) {
        pdw sel;
        jb_collect_tabs();
        sel = (pdw)((f >> 2) & 3); if (sel == 2 || hf_select(0, sel == 0 ? 4u : sel == 1 ? 5u : 0)) return PDE_LIMIT;
        sel = (pdw)((f >> 4) & 3); if (sel == 2 || hf_select(1, sel == 0 ? 2u : sel == 1 ? 3u : 0)) return PDE_LIMIT;
        if (hf_select(2, (pdw)(((f >> 6) & 1) ? 0 : 1))) return PDE_LIMIT;
        if (hf_select(3, (pdw)(((f >> 7) & 1) ? 0 : 1))) return PDE_LIMIT;
        hf_std(4, 1);
        hf_slot[IA_DH] = 0; hf_slot[IA_DW] = 1; hf_slot[HF_BM] = 2; hf_slot[IA_AI] = 3; hf_slot[IA_EX] = 4;
    }
    if (!sd_huff)
        for (k = 0; k < (sd_tpl ? 1u : 4u); k++) { sd_at[k][0] = (pdi)(signed char)jb_b(); sd_at[k][1] = (pdi)(signed char)jb_b();
            if (sd_at[k][1] < -2 || sd_at[k][1] > 0 || (sd_at[k][1] == 0 && sd_at[k][0] >= 0)) return PDE_LIMIT; }
    else for (k = 0; k < 4; k++) { sd_at[k][0] = gb_atnomv[0][k * 2]; sd_at[k][1] = gb_atnomv[0][k * 2 + 1]; }
    if (sd_refagg && !sd_rtpl) {
        for (k = 0; k < 2; k++) { rf_at[k][0] = (pdi)(signed char)jb_b(); rf_at[k][1] = (pdi)(signed char)jb_b(); }
        if (rf_at[0][1] < -1 || rf_at[0][1] > 0 || rf_at[1][1] < -1 || rf_at[1][1] > 1) return PDE_LIMIT;
    }
    jb_b32(); sd_numex = jb_lo; if (jb_hi) return PDE_LIMIT;
    jb_b32(); sd_numnew = jb_lo; if (jb_hi || sd_numnew > JB_MAXSYMS) return PDE_LIMIT;
    for (i = 0; i < jb_nref && i < 32; i++) {
        j = jb_rg_find(jb_ref[i]);
        if (j != 0xFFFFu && jb_rg_kind[j] == RGK_SYM) nin = (pdw)(nin + jb_rg_cnt[j]);
        else if (j != 0xFFFFu && jb_rg_kind[j] == RGK_BAD) return PDE_LIMIT;
    }
    total = (pdw)(nin + sd_numnew);
    if (total > JB_MAXSYMS) return PDE_LIMIT;
    sd_all = jb_recs(total ? total : 1);
    if (!sd_all) return PDE_LIMIT;
    sd_nin = nin; j = 0;
    for (i = 0; i < jb_nref && i < 32; i++) {
        pdw g = jb_rg_find(jb_ref[i]);
        if (g != 0xFFFFu && jb_rg_kind[g] == RGK_SYM)
            for (k = 0; k < jb_rg_cnt[g]; k++) {
                jb_getrec(jb_rg_ofs[g], k);
                jb_setrec(sd_all, j, jb_rec_off, jb_rec_w, jb_rec_h); j++;
            }
    }
    e = sd_run();
    if (e) { jb_sp = msp; jb_hp = mhp; return e; }
    M = sd_numex;
    if (M < total) {
        jw_move((pdw)(sd_all + (pdw)((total - M) * JB_RECSZ)), sd_all, (pdw)(M * JB_RECSZ));
        sd_all = (pdw)(sd_all + (pdw)((total - M) * JB_RECSZ)); jb_hp = sd_all;
    }
    jb_rg_add(RGK_SYM, sd_all, M, 0, 0);
    return PDE_OK;
}

static pdw jb_segment_text(void)
{
    pdw f, k, i, j, g, nsy = 0, e, msp = jb_sp, mhp = jb_hp, direct, trows = 0, y0r = 0, rbr, tmp = 0;
    f = (pdw)(jb_b() << 8); f |= jb_b();
    jb_huff = (pdb)(f & 1); tr_refine = (pdw)((f >> 1) & 1); tr_strips = (pdw)(1u << ((f >> 2) & 3));
    tr_corner = (pdw)((f >> 4) & 3); tr_transp = (pdw)((f >> 6) & 1); tr_comb = (pdw)((f >> 7) & 3);
    tr_defpix = (pdw)((f >> 9) & 1); tr_dsoff = (pdi)((f >> 10) & 31); if (tr_dsoff > 15) tr_dsoff = (pdi)(tr_dsoff - 32);
    tr_rtpl = (pdw)((f >> 15) & 1);
    if (jb_huff && tr_refine) return PDE_LIMIT;                 /* Huffman with refinement: not supported */
    if (jb_huff) {
        pdw hf = (pdw)(jb_b() << 8), sel;
        hf |= jb_b();
        jb_collect_tabs();
        sel = (pdw)(hf & 3); if (sel == 2 || hf_select(0, sel == 0 ? 6u : sel == 1 ? 7u : 0)) return PDE_LIMIT;
        sel = (pdw)((hf >> 2) & 3); if (hf_select(1, sel == 0 ? 8u : sel == 1 ? 9u : sel == 2 ? 10u : 0)) return PDE_LIMIT;
        sel = (pdw)((hf >> 4) & 3); if (sel == 3 ? hf_select(2, 0) : hf_select(2, (pdw)(11 + sel))) return PDE_LIMIT;
        sel = (pdw)((hf >> 6) & 3); if (sel == 2 || hf_select(3, sel == 0 ? 14u : sel == 1 ? 15u : 0)) return PDE_LIMIT;
        sel = (pdw)((hf >> 8) & 3); if (sel == 2 || hf_select(4, sel == 0 ? 14u : sel == 1 ? 15u : 0)) return PDE_LIMIT;
        sel = (pdw)((hf >> 10) & 3); if (sel == 2 || hf_select(5, sel == 0 ? 14u : sel == 1 ? 15u : 0)) return PDE_LIMIT;
        sel = (pdw)((hf >> 12) & 3); if (sel == 2 || hf_select(6, sel == 0 ? 14u : sel == 1 ? 15u : 0)) return PDE_LIMIT;
        if (hf_select(7, (pdw)(((hf >> 14) & 1) ? 0 : 1))) return PDE_LIMIT;
        hf_slot[IA_FS] = 0; hf_slot[IA_DS] = 1; hf_slot[IA_DT] = 2; hf_slot[IA_RDW] = 3; hf_slot[IA_RDH] = 4;
        hf_slot[IA_RDX] = 5; hf_slot[IA_RDY] = 6;
        hf_logs = (pdw)((f >> 2) & 3);
    }
    if (tr_refine && !tr_rtpl) {
        for (k = 0; k < 2; k++) { rf_at[k][0] = (pdi)(signed char)jb_b(); rf_at[k][1] = (pdi)(signed char)jb_b(); }
        if (rf_at[0][1] < -1 || rf_at[0][1] > 0 || rf_at[1][1] < -1 || rf_at[1][1] > 1) return PDE_LIMIT;
    }
    jb_b32(); if (jb_hi) return PDE_LIMIT; tr_ninst = jb_lo;
    for (i = 0; i < jb_nref && i < 32; i++) {
        j = jb_rg_find(jb_ref[i]);
        if (j != 0xFFFFu && jb_rg_kind[j] == RGK_SYM) nsy = (pdw)(nsy + jb_rg_cnt[j]);
        else if (j != 0xFFFFu && jb_rg_kind[j] == RGK_BAD) return PDE_LIMIT;
    }
    if (nsy > JB_MAXSYMS) return PDE_LIMIT;
    if (!nsy) return PDE_OK;
    tr_recs = jb_recs(nsy);
    if (!tr_recs) return PDE_LIMIT;
    tr_nsyms = nsy; j = 0;
    for (i = 0; i < jb_nref && i < 32; i++) {
        g = jb_rg_find(jb_ref[i]);
        if (g != 0xFFFFu && jb_rg_kind[g] == RGK_SYM)
            for (k = 0; k < jb_rg_cnt[g]; k++) {
                jb_getrec(jb_rg_ofs[g], k);
                jb_setrec(tr_recs, j, jb_rec_off, jb_rec_w, jb_rec_h); j++;
            }
    }
    jb_idlen = jb_codelen(nsy);
    if (jb_idlen > 11 && !jb_huff) { jb_sp = msp; jb_hp = mhp; return PDE_LIMIT; }
    direct = (pdw)(!tr_defpix && jb_rop == 0 && tr_comb == 0);
    if (direct) { jb_cv_band(); tr_ox = (pdi)jb_rx; tr_oy = (pdi)jb_ry; }
    else {
        pdw by1 = (pdw)(jb_by0 + jb_brows), ye = jb_satadd(jb_ry, jb_rh), y1r;
        y0r = jb_ry > jb_by0 ? jb_ry : jb_by0; y1r = ye < by1 ? ye : by1;
        trows = (pdw)(y1r - y0r); rbr = (pdw)((jb_rw + 7) >> 3);
        tmp = jb_alloc(jb_rw, trows);
        if (!tmp) { jb_sp = msp; jb_hp = mhp; return PDE_LIMIT; }
        if (tr_defpix) for (k = 0; k < (pdw)(rbr * trows); k++) jw_wb((pdw)(tmp + k), 0xFF);
        cv_off = tmp; cv_rb = rbr; cv_w = jb_rw; cv_h = trows; cv_y0 = (pdi)(y0r - jb_ry);
        tr_ox = 0; tr_oy = 0;
    }
    if (jb_huff) {
        e = hf_idtable(nsy);
        if (e) { jb_sp = msp; jb_hp = mhp; return e; }
    } else { sd_contexts(); mq_init(); }
    rf_tpl = tr_rtpl; rf_tpgr = 0;
    e = tr_run();
    if (!e && !direct) {
        jb_cv_band();
        for (k = 0; k < trows; k++) {
            jw_get(tr_row, (pdw)(tmp + k * rbr), rbr);
            tr_row[rbr] = 0; tr_row[rbr + 1] = 0;
            cv_row((pdi)(y0r + k), (pdi)jb_rx, tr_row, jb_rw, jb_rop);
        }
    }
    jb_sp = msp; jb_hp = mhp;
    return e;
}

/* ---- the segment loop for one stream, one band ------------------------------------------ */
static pdw jb_dispatch(void)
{
    pdw r = PDE_OK;
    jb_ovf = 0;
#ifdef JBDBG
    fprintf(stderr, "seg %u type %u left %u:%u\n", jb_sn, jb_st, jb_lh, jb_ll);
#endif
    switch (jb_st) {
    case 38: case 39:
        if (jb_region()) return PDE_TRUNC;
        if (jb_inband()) r = jb_segment_generic();
        break;
    case 48: jb_pageinfo(); break;
    case 0:
        if (jb_rg_find(jb_sn) == 0xFFFFu) {
            r = jb_segment_symdict();
            if (r) { jb_rg_add(RGK_BAD, 0, 0, 0, 0); jb_inc |= JBF_UNSUP; r = PDE_OK; }
        }
        break;
    case 6: case 7:
        if (jb_region()) return PDE_TRUNC;
        if (jb_inband()) r = jb_segment_text();
        break;
    case 53:
        if (jb_rg_find(jb_sn) == 0xFFFFu) {
            r = jb_segment_table();
            if (r) { jb_rg_add(RGK_BAD, 0, 0, 0, 0); jb_inc |= JBF_UNSUP; r = PDE_OK; }
        }
        break;
    case 16:
        if (jb_rg_find(jb_sn) == 0xFFFFu) {
            r = jb_segment_pattern();
            if (r) { jb_rg_add(RGK_BAD, 0, 0, 0, 0); jb_inc |= JBF_UNSUP; r = PDE_OK; }
        }
        break;
    case 22: case 23:
        if (jb_region()) return PDE_TRUNC;
        if (jb_inband()) r = jb_segment_halftone();
        break;
    case 4: case 20: case 36: case 40: jb_inc |= JBF_UNSUP; break;
    default: break;
    }
    return r;
}

static pdw jb_pass(void)
{
    pdw r;
    for (;;) {
        if (jb_hdr()) break;
        r = jb_dispatch();
        if (r == PDE_LIMIT) jb_inc |= JBF_UNSUP;
        else if (r) jb_inc |= JBF_BAD;
        jb_skip();
    }
    return PDE_OK;
}

/* ---- open / rows ----------------------------------------------------------------------------- */
static pdw jb_globals, jb_bandok, jb_gnext;

static void jb_band_start(pdw by0)
{
    pdw n;
    jb_by0 = by0;
    jb_cv_band();
    for (n = 0; n < (pdw)(jb_rb * jb_brows); n++) jw_wb((pdw)(JW_BAND + n), jb_pdef ? 0xFF : 0);
    if (jb_globals) { ix_open(1); jb_pass(); }
    ix_open(0);
    jb_pass();
}

static pdw jb_open(pdw globals, pdw ih, pdw want_w)
{
    pdw e, n, s;
    jb_globals = globals; jb_ih = ih; jb_inc = 0; jb_pw = 0;
    ix_open(0);
    jb_by0 = 0; jb_brows = 0; jb_rb = 0;
    for (;;) {                  /* find the page information */
        if (jb_hdr()) return PDE_SYNTAX;
        if (jb_st == 48) { jb_pageinfo(); break; }
        jb_skip();
    }
    if (!jb_pw || !jb_ph) return PDE_SYNTAX;
    e = bl_pick(jb_pw, jb_ph, want_w);
    if (e) return e;
    jb_rb = (pdw)((jb_pw + 7) >> 3);
    n = (pdw)(1u << bl_scale);
    s = (pdw)(JB_BANDMAX / jb_rb);
    s = (pdw)(s - s % n);
    if (!s) return PDE_LIMIT;
    jb_brows = s > jb_ph ? (pdw)(((jb_ph + n - 1) / n) * n) : s;
    jb_sp = JW_SYM; jb_hp = 0;      /* the record bump starts at the top (0 = 65536) */
    jb_gnext = 0xFFFFu;             /* no band decoded yet                           */
    return PDE_OK;
}

static pdw jb_getrow(pdw r)
{
    pdw n = (pdw)(1u << bl_scale), first = (pdw)(r << bl_scale), by, y, i, last;
    if (r >= bl_oh) return PDE_RANGE;
    by = (pdw)((first / jb_brows) * jb_brows);
    if (jb_gnext != by) { jb_band_start(by); jb_gnext = by; }
    last = (pdw)(first + n); if (last > jb_ph) last = jb_ph;
    for (y = first; y < last; y++) {
        pdw o = (pdw)(JW_BAND + (pdw)(y - by) * jb_rb);
        jw_get(jb_srow, o, jb_rb);
        for (i = 0; i < jb_rb; i++) jb_srow[i] = (pdb)~jb_srow[i];
        if (jb_pw & 7) jb_srow[jb_rb - 1] |= (pdb)~(0xFF00 >> (jb_pw & 7));
        if (bl_scale) bl_arow(jb_srow);
        else for (i = 0; i < jb_rb; i++) bl_out[i] = jb_srow[i];
    }
    if (bl_scale) bl_aemit(n);
    return PDE_OK;
}
