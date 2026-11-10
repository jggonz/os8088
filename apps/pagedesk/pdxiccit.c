/* ============================================================================
 * pdxiccit.c - CCITTFaxDecode and the shared bilevel row reducer
 *                                       (PAGEDESK-SPEC 20.22.12, AP-08)
 *
 * #included by PDXI's pdxi.c (dct's shell), BEFORE pdxijb2.c. Follows the C
 * rules of SPEC.md 73: no long, no &local, no string instructions.
 *
 * What it decodes: Group 3 one-dimensional (K = 0), Group 3 two-dimensional
 * (K > 0, a tag bit after each EOL), Group 4 (K < 0), with /Columns, /Rows,
 * /EncodedByteAlign, /EndOfLine, /EndOfBlock, /BlackIs1. /DamagedRowsBeforeError
 * is accepted and ignored: a damaged row never fails the image, it ends the
 * row where the fault is (the rest of the row is white) and decoding goes on
 * at the next EOL (K >= 0) or stops with the rest white (K < 0), which is what
 * MuPDF does and is a superset of every N.
 *
 * It STREAMS: one row at a time, from a byte source the shell provides.
 *
 *   static int  ix_get(void);      next coded byte, -1 at the end
 *
 * The rows delivered are the PDF SAMPLES (BlackIs1 false: black = 0, white = 1).
 * With a reduction (scale > 0) a group of 2^s rows is averaged into one
 * PDIM_GREY8 row (0 black .. 255 white) whose columns are groups of 2^s
 * pixels; nothing is held but the reference row and one row of counters.
 *
 *   pdw cf_open(k, cols, rows, flags, ih, want_w)   ->  PDE_*; sets bl_*
 *   pdw cf_getrow(r)                                ->  PDE_*; row in bl_out
 * ==========================================================================*/

#define BL_MAXW   8192          /* widest bilevel image: rowbytes <= 1024     */

/* what a bilevel decoder reports at open, and the row it fills */
static pdb  bl_out[1024];
static pdw  bl_w, bl_h;         /* source width and height                    */
static pdw  bl_scale;           /* 0..3                                       */
static pdw  bl_ow, bl_oh;       /* delivered width and height                 */
static pdw  bl_omode;           /* PDIM_BITS1 or PDIM_GREY8                   */
static pdw  bl_orb;             /* delivered bytes a row                      */

static pdb  bl_pop[256];        /* popcounts                                  */
static pdw  bl_acc[1032];       /* white counters, one per output column      */
static pdw  bl_arows;           /* rows accumulated into the group            */

/* CCF_* (the flags) are in pdabi.h */

/* The reduction: the largest 2^s with w >> s >= want, then more if the
 * grey row would not fit the 1,024-byte IMG_ROW. */
static pdw bl_pick(pdw w, pdw h, pdw want)
{
    pdw s = 0;
    pdw i;
    if (w == 0 || h == 0 || w > BL_MAXW) return PDE_LIMIT;
    if (!bl_pop[255]) for (i = 0; i < 256; i++) {
        pdw v = i, n = 0;
        while (v) { n += v & 1; v >>= 1; }
        bl_pop[i] = (pdb)n;
    }
    while (want && s < 3 && (w >> (s + 1)) >= want) s++;
    while (s && s < 3 && (pdw)((w + (1u << s) - 1) >> s) > 1024) s++;
    if (s && (pdw)((w + (1u << s) - 1) >> s) > 1024) return PDE_LIMIT;
    bl_w = w; bl_h = h; bl_scale = s;
    bl_ow = (pdw)((w + (1u << s) - 1) >> s);
    bl_oh = (pdw)((h + (1u << s) - 1) >> s);
    bl_omode = s ? PDIM_GREY8 : PDIM_BITS1;
    bl_orb = s ? bl_ow : (pdw)((w + 7) >> 3);
    bl_arows = 0;
    for (i = 0; i < 1032; i++) bl_acc[i] = 0;
    return PDE_OK;
}

/* Add one sample row (1 = white, pad bits white) into the group. */
static void bl_arow(const pdb *r)
{
    pdw nb = (pdw)((bl_w + 7) >> 3), i, j, b;
    if (bl_scale == 1) {
        for (i = 0, j = 0; i < nb; i++, j += 4) {
            b = r[i];
            bl_acc[j]     += bl_pop[b >> 6];
            bl_acc[j + 1] += bl_pop[(b >> 4) & 3];
            bl_acc[j + 2] += bl_pop[(b >> 2) & 3];
            bl_acc[j + 3] += bl_pop[b & 3];
        }
    } else if (bl_scale == 2) {
        for (i = 0, j = 0; i < nb; i++, j += 2) {
            b = r[i];
            bl_acc[j]     += bl_pop[b >> 4];
            bl_acc[j + 1] += bl_pop[b & 15];
        }
    } else {
        for (i = 0; i < nb; i++) bl_acc[i] += bl_pop[r[i]];
    }
    bl_arows++;
}

/* The group's average, into bl_out; counters cleared. `miss` rows of the
 * group were off the bottom and count as white. */
static void bl_aemit(pdw n)
{
    pdw j, sh = (pdw)(bl_scale * 2), miss = (pdw)((1u << bl_scale) - bl_arows);
    pdw add = (pdw)(miss << bl_scale);  /* a missing row is n whites per column */
    (void)n;
    for (j = 0; j < bl_ow; j++) {
        pdw a = (pdw)(bl_acc[j] + add);
        bl_out[j] = (pdb)(((pdw)(a << 8) - a) >> sh);
        bl_acc[j] = 0;
    }
    for (; j < 1032; j++) bl_acc[j] = 0;
    bl_arows = 0;
}

/* ---- the code tables, generated from tools/os88pdfx_bilevel.py ---------- */
static const pdw cf_white_code[104] = {
    0x35, 0x7, 0x7, 0x8, 0xB, 0xC, 0xE, 0xF, 0x13, 0x14, 0x7, 0x8, 0x8, 0x3, 0x34, 0x35, 0x2A, 0x2B, 0x27, 0xC, 0x8, 0x17, 0x3, 0x4, 0x28, 0x2B, 0x13, 0x24, 0x18, 0x2, 0x3, 0x1A, 0x1B, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x4, 0x5, 0xA, 0xB, 0x52, 0x53, 0x54, 0x55, 0x24, 0x25, 0x58, 0x59, 0x5A, 0x5B, 0x4A, 0x4B, 0x32, 0x33, 0x34, 0x1B, 0x12, 0x17, 0x37, 0x36, 0x37, 0x64, 0x65, 0x68, 0x67, 0xCC, 0xCD, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0x98, 0x99, 0x9A, 0x18, 0x9B, 0x8, 0xC, 0xD, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1C, 0x1D, 0x1E, 0x1F };
static const pdb cf_white_len[104] = {
    8, 6, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 5, 5, 6, 7, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 6, 9, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 };
static const pdw cf_black_code[104] = {
    0x37, 0x2, 0x3, 0x2, 0x3, 0x3, 0x2, 0x3, 0x5, 0x4, 0x4, 0x5, 0x7, 0x4, 0x7, 0x18, 0x17, 0x18, 0x8, 0x67, 0x68, 0x6C, 0x37, 0x28, 0x17, 0x18, 0xCA, 0xCB, 0xCC, 0xCD, 0x68, 0x69, 0x6A, 0x6B, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0x6C, 0x6D, 0xDA, 0xDB, 0x54, 0x55, 0x56, 0x57, 0x64, 0x65, 0x52, 0x53, 0x24, 0x37, 0x38, 0x27, 0x28, 0x58, 0x59, 0x2B, 0x2C, 0x5A, 0x66, 0x67, 0xF, 0xC8, 0xC9, 0x5B, 0x33, 0x34, 0x35, 0x6C, 0x6D, 0x4A, 0x4B, 0x4C, 0x4D, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x52, 0x53, 0x54, 0x55, 0x5A, 0x5B, 0x64, 0x65, 0x8, 0xC, 0xD, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1C, 0x1D, 0x1E, 0x1F };
static const pdb cf_black_len[104] = {
    10, 3, 2, 2, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8, 9, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 10, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 };
static const pdw cf_run_of[104] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960, 1024, 1088, 1152, 1216, 1280, 1344, 1408, 1472, 1536, 1600, 1664, 1728, 1792, 1856, 1920, 1984, 2048, 2112, 2176, 2240, 2304, 2368, 2432, 2496, 2560 };


/* Primary lookup on 8 bits; entry = run << 4 | length. A code longer than 8
 * bits points (length field 15) into a second table on the next 5 bits. */
#define CF_NSEC 24
static pdw cf_pri[2][256];
static pdw cf_sec[2][CF_NSEC * 32];
static pdw cf_nsec[2];
static pdb cf_built;

static void cf_build(void)
{
    pdw c, i, k, ln, code, run, e, base, ex, fill;
    if (cf_built) return;
    for (c = 0; c < 2; c++) {
        cf_nsec[c] = 0;
        for (i = 0; i < 256; i++) cf_pri[c][i] = 0;
        for (i = 0; i < CF_NSEC * 32; i++) cf_sec[c][i] = 0;
        for (i = 0; i < 104; i++) {
            code = c ? cf_black_code[i] : cf_white_code[i];
            ln = c ? cf_black_len[i] : cf_white_len[i];
            run = cf_run_of[i];
            e = (pdw)((run << 4) | ln);
            if (ln <= 8) {
                fill = (pdw)(1u << (8 - ln));
                base = (pdw)(code << (8 - ln));
                for (k = 0; k < fill; k++) cf_pri[c][base + k] = e;
            } else {
                pdw pre = (pdw)(code >> (ln - 8));
                if ((cf_pri[c][pre] & 15) != 15) {
                    cf_pri[c][pre] = (pdw)((cf_nsec[c] * 32) << 4 | 15);
                    cf_nsec[c]++;
                }
                base = (pdw)(cf_pri[c][pre] >> 4);
                ex = (pdw)(ln - 8);
                fill = (pdw)(1u << (5 - ex));
                base += (pdw)((code & ((1u << ex) - 1)) << (5 - ex));
                for (k = 0; k < fill; k++) cf_sec[c][base + k] = e;
            }
        }
    }
    cf_built = 1;
}

/* ---- the bit reader ------------------------------------------------------ */
static pdb  cf_b[3];
static pdw  cf_bit;
static pdw  cf_eofn;            /* bytes of padding shifted into the window   */

static void cf_shift(void)
{
    int c = ix_get();
    cf_b[0] = cf_b[1]; cf_b[1] = cf_b[2];
    if (c < 0) { cf_b[2] = 0; if (cf_eofn < 9) cf_eofn++; } else cf_b[2] = (pdb)c;
}
static void cf_binit(void)
{
    cf_bit = 0; cf_eofn = 0;
    cf_b[0] = cf_b[1] = cf_b[2] = 0;
    cf_shift(); cf_shift(); cf_shift();
}
static pdw cf_peek(pdw n)       /* n <= 16 */
{
    pdw v = (pdw)(((pdw)cf_b[0] << 8) | cf_b[1]);
    v = (pdw)((v << cf_bit) | (cf_b[2] >> (8 - cf_bit)));
    return (pdw)(v >> (16 - n));
}
static void cf_adv(pdw n)
{
    cf_bit += n;
    while (cf_bit >= 8) { cf_bit -= 8; cf_shift(); }
}
/* no real bit left to read */
static int cf_done(void)
{
    return (int)((3 - cf_eofn) * 8) <= (int)cf_bit;
}

/* ---- run lengths ---------------------------------------------------------- */
/* One run of colour c (0 white, 1 black): makeups then a terminating code.
 * -1 for an invalid code or a run longer than the row. */
static int cf_run(pdw c, pdw cols)
{
    pdw total = 0, e, v;
    for (;;) {
        v = cf_peek(13);
        e = cf_pri[c][v >> 5];
        if ((e & 15) == 15) e = cf_sec[c][(e >> 4) + (v & 31)];
        if (!(e & 15)) return -1;
        total += (e >> 4);
        cf_adv(e & 15);
        if ((e >> 4) < 64) return (int)total;
        if (total > cols) return -1;
    }
}

/* ---- rows ----------------------------------------------------------------- */
static pdb  cf_rowa[1024], cf_rowb[1024];
static pdb *cf_cur, *cf_ref;
static pdw  cf_k, cf_cols, cf_flags, cf_nb;
static pdw  cf_next;            /* source rows decoded so far                 */
static pdb  cf_end;             /* no more coded rows: the rest are white     */
static pdw  cf_gdone;           /* output groups delivered                    */
static pdb  cf_samp[1024];

static void cf_fill(pdb *r, pdw a, pdw b)       /* set bits [a, b) */
{
    pdw ia, ib, i;
    pdb ma, mb;
    if (a >= b) return;
    ia = (pdw)(a >> 3); ib = (pdw)(b >> 3);
    ma = (pdb)(0xFF >> (a & 7));
    mb = (pdb)((0xFF00 >> (b & 7)) & 0xFF);
    if (ia == ib) { r[ia] |= (pdb)(ma & mb); return; }
    r[ia] |= ma;
    for (i = (pdw)(ia + 1); i < ib; i++) r[i] = 0xFF;
    if (b & 7) r[ib] |= mb;
}

static int cf_pix(const pdb *r, pdw p)
{
    return (r[p >> 3] >> (7 - (p & 7))) & 1;
}

/* first index >= p of ref whose pixel is val, or cols */
static pdw cf_find(const pdb *r, pdw p, int val)
{
    pdb skip = val ? 0x00 : 0xFF;
    while (p < cf_cols) {
        if (!(p & 7))
            while ((pdw)(p + 8) <= cf_cols && r[p >> 3] == skip) p += 8;
        if (p >= cf_cols) break;
        if (cf_pix(r, p) == val) return p;
        p++;
    }
    return cf_cols;
}

/* Align, skip fill and EOLs. Returns 1 for a 2-D row, 0 for a 1-D row, -1 at
 * the end of the data (an EOFB / RTC, or nothing left). */
static int cf_rowstart(void)
{
    pdw eols = 0, v;
    int tag = 0;
    if ((cf_flags & CCF_ALIGN) && cf_bit) cf_adv((pdw)(8 - cf_bit));
    if ((pdi)cf_k < 0) {
        if (cf_done()) return -1;
        if (cf_peek(12) == 1) return -1;                /* EOFB */
        return 1;
    }
    for (;;) {
        if (cf_done()) return eols ? -1 : -1;
        v = cf_peek(12);
        if (v == 0) { cf_adv(1); continue; }            /* fill */
        if (v == 1) {
            cf_adv(12); eols++;
            if (cf_k > 0) { tag = (int)cf_peek(1); cf_adv(1); }
            else tag = 0;
            if (eols >= 2) return -1;                   /* RTC */
            continue;
        }
        break;
    }
    if (cf_k == 0) return 0;
    if (!eols) { tag = (int)cf_peek(1); cf_adv(1); }    /* tag with no EOL: take it as written */
    return tag ? 0 : 1;
}

/* skip to the next EOL (K >= 0) after a fault */
static void cf_resync(void)
{
    while (!cf_done() && cf_peek(12) != 1) cf_adv(1);
}

/* one 2-D row; returns 0 ok, -1 fault (row left as far as it got) */
static int cf_row2d(void)
{
    pdi a0 = -1;
    pdw c = 0, st, b1, b2, a1, r1, r2, s0;
    int m, d;
    pdw v;
    while (a0 < (pdi)cf_cols) {
        if (cf_done()) return -1;
        v = cf_peek(7);
        m = 99; d = 0;
        if (v & 0x40) { m = 2; cf_adv(1); }                         /* 1: V0 */
        else if ((v >> 4) == 3) { m = 2; d = 1; cf_adv(3); }        /* 011 */
        else if ((v >> 4) == 2) { m = 2; d = -1; cf_adv(3); }       /* 010 */
        else if ((v >> 4) == 1) { m = 1; cf_adv(3); }               /* 001: H */
        else if ((v >> 3) == 1) { m = 0; cf_adv(4); }               /* 0001: P */
        else if ((v >> 1) == 3) { m = 2; d = 2; cf_adv(6); }        /* 000011 */
        else if ((v >> 1) == 2) { m = 2; d = -2; cf_adv(6); }       /* 000010 */
        else if (v == 3) { m = 2; d = 3; cf_adv(7); }               /* 0000011 */
        else if (v == 2) { m = 2; d = -3; cf_adv(7); }              /* 0000010 */
        else return -1;                                             /* extension, EOL, garbage */
        s0 = (pdw)(a0 < 0 ? 0 : a0);
        if (m == 1) {                                               /* horizontal */
            int x = cf_run(c, cf_cols), y;
            if (x < 0) return -1;
            y = cf_run(c ^ 1, cf_cols);
            if (y < 0) { if (c) cf_fill(cf_cur, s0, (pdw)(s0 + x > cf_cols ? cf_cols : s0 + x)); return -1; }
            r1 = (pdw)(s0 + x); if (r1 > cf_cols) r1 = cf_cols;
            r2 = (pdw)(r1 + y); if (r2 > cf_cols) r2 = cf_cols;
            if (c) cf_fill(cf_cur, s0, r1); else cf_fill(cf_cur, r1, r2);
            a0 = (pdi)r2;
            continue;
        }
        /* b1, b2 */
        st = (pdw)(a0 + 1);
        if (a0 >= 0 && cf_pix(cf_ref, (pdw)a0) != (int)c) st = cf_find(cf_ref, st, (int)c);
        b1 = cf_find(cf_ref, st, (int)(c ^ 1));
        b2 = b1 < cf_cols ? cf_find(cf_ref, (pdw)(b1 + 1), (int)c) : cf_cols;
        if (m == 0) {                                               /* pass */
            if (c) cf_fill(cf_cur, s0, b2);
            a0 = (pdi)b2;
            continue;
        }
        a1 = (pdw)((pdi)b1 + d);
        if ((pdi)a1 < (pdi)s0 || (pdi)a1 > (pdi)cf_cols) return -1;
        if (c) cf_fill(cf_cur, s0, a1);
        a0 = (pdi)a1; c ^= 1;
    }
    return 0;
}

static int cf_row1d(void)
{
    pdw a = 0, c = 0, b;
    int r;
    while (a < cf_cols) {
        if (cf_done()) return -1;
        r = cf_run(c, cf_cols);
        if (r < 0) return -1;
        b = (pdw)(a + r); if (b > cf_cols) b = cf_cols;
        if (c) cf_fill(cf_cur, a, b);
        a = b; c ^= 1;
    }
    return 0;
}

/* the next source row into cf_cur (black = 1); cf_end when the data is over */
static void cf_decode(void)
{
    pdw i;
    int t, r;
    pdb *x;
    for (i = 0; i < cf_nb; i++) cf_cur[i] = 0;
    if (!cf_end) {
        t = cf_rowstart();
        if (t < 0) cf_end = 1;
        else {
            r = t ? cf_row2d() : cf_row1d();
            if (r < 0) {
                if ((pdi)cf_k < 0) cf_end = 1; else cf_resync();
            }
        }
    }
    /* the row just made is the next reference */
    x = cf_cur; cf_cur = cf_ref; cf_ref = x;
    /* cf_ref is now the new row; cf_cur is scratch to be cleared */
}

/* the row cf_ref as samples into cf_samp */
static void cf_sample(void)
{
    pdw i;
    pdb m = (pdb)(0xFF00 >> (cf_cols & 7));
    for (i = 0; i < cf_nb; i++) cf_samp[i] = (cf_flags & CCF_BLACK1) ? cf_ref[i] : (pdb)~cf_ref[i];
    if (cf_cols & 7)  /* pad bits count as 1 = 255 grey */ cf_samp[cf_nb - 1] |= (pdb)~m;
}

static pdw cf_open(pdw k, pdw cols, pdw rows, pdw flags, pdw ih, pdw want_w)
{
    pdw e, i;
    (void)rows;
    if (!cols) cols = 1728;
    e = bl_pick(cols, ih, want_w);
    if (e) return e;
    cf_build();
    cf_k = k; cf_cols = cols; cf_flags = flags; cf_nb = (pdw)((cols + 7) >> 3);
    cf_cur = cf_rowa; cf_ref = cf_rowb;
    for (i = 0; i < cf_nb; i++) cf_rowa[i] = cf_rowb[i] = 0;
    cf_next = 0; cf_end = 0; cf_gdone = 0;
    cf_binit();
    return PDE_OK;
}

/* output row r (ascending; a skipped row is decoded and dropped) */
static pdw cf_getrow(pdw r)
{
    pdw n = (pdw)(1u << bl_scale), last, g;
    if (r < cf_gdone || r >= bl_oh) return PDE_RANGE;
    for (g = cf_gdone; g <= r; g++) {
        pdw first = (pdw)(g << bl_scale);
        last = (pdw)(first + n);
        if (last > bl_h) last = bl_h;
        while (cf_next < last) {
            cf_decode(); cf_next++;
            if (g == r) {
                cf_sample();
                if (bl_scale) bl_arow(cf_samp);
                else {
                    pdw i;
                    for (i = 0; i < cf_nb; i++) bl_out[i] = cf_samp[i];
                }
            }
        }
    }
    if (bl_scale) bl_aemit(n);
    cf_gdone = (pdw)(r + 1);
    return PDE_OK;
}
