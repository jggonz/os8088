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

static int jb_getb(void)
{
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
static pdb  gb_r0[GB_MAXRB], gb_r1[GB_MAXRB], gb_r2[GB_MAXRB];
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
static void ht_plane_row(pdw y, const pdb *row) { (void)y; (void)row; }
static int gb_skipf(pdw x, pdw y) { (void)x; (void)y; return 0; }

static void gb_sink(pdw y)
{
    if (gb_sinkmode == 0) {
        cv_row((pdi)(gb_dy + (pdi)y), gb_dx, gb_c, gb_w, gb_op);
        if ((pdi)(gb_dy + (pdi)y + 1) >= (pdi)(cv_y0 + (pdi)cv_h)) gb_stop = 1;
    } else if (gb_sinkmode == 1) ht_plane_row(y, gb_c);
}

/* The arithmetic-coded generic region. Needs gb_w/gb_h/gb_tpl/gb_tpgd/gb_at set and
 * mq_init done. Contexts are cleared here. Returns 0 or PDE_*. */
static pdw gb_arith(void)
{
    pdw x, y, nb = (pdw)((gb_w + 7) >> 3), t = gb_tpl, ltp = 0, w0, w1, w2, ctx, k, i;
    pdb *tp;
    int bit;
    if (gb_w > BL_MAXW) return PDE_LIMIT;
    gb_hash = (pdb)(t == 0);
    if (gb_hash) { jw_zero(JW_GB, (pdw)(JB_HASH * 3)); } else jw_zero(JW_GB, (pdw)(1u << (t == 1 ? 13 : 10)));
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
static void cf_mmr_begin(pdw w)
{
    pdw i;
    cf_build();
    cf_k = 0xFFFFu; cf_cols = w; cf_flags = 0; cf_nb = (pdw)((w + 7) >> 3);
    cf_cur = cf_rowa; cf_ref = cf_rowb;
    for (i = 0; i < 1024; i++) cf_rowa[i] = cf_rowb[i] = 0;
    cf_end = 0; cf_seg = 1;
    cf_binit();
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
    return gb_arith();
}

/* ---- the segment loop for one stream, one band ------------------------------------------ */
static pdw jb_dispatch(void)
{
    pdw r = PDE_OK;
#ifdef JBDBG
    fprintf(stderr, "seg %u type %u left %u:%u\n", jb_sn, jb_st, jb_lh, jb_ll);
#endif
    switch (jb_st) {
    case 38: case 39:
        if (jb_region()) return PDE_TRUNC;
        if (jb_inband()) r = jb_segment_generic();
        break;
    case 48: jb_pageinfo(); break;
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

static pdb jb_srow[1032];
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
