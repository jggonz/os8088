/* ---- pdxjt1.c: tier-1 - EBCOT code-block decoding (20.22.13.4) ----------------- */

#define T1_NE  0x0001u
#define T1_SE  0x0002u
#define T1_SW  0x0004u
#define T1_NW  0x0008u
#define T1_N   0x0010u
#define T1_E   0x0020u
#define T1_S   0x0040u
#define T1_W   0x0080u
#define T1_SGN_N 0x0100u
#define T1_SGN_E 0x0200u
#define T1_SGN_S 0x0400u
#define T1_SGN_W 0x0800u
#define T1_SIG   0x1000u
#define T1_REF   0x2000u
#define T1_VIS   0x4000u
#define T1_NEG   0x8000u                /* the sample's own sign                */

static pdw t1_f[66 * 66];               /* flags, a border of one all round     */
static pdw t1_m[64 * 64];               /* magnitudes with one fraction bit     */
static pdb t1_cb[JX_CBDATA + 2];        /* the code-block's bytes, then 0xFF 0xFF */
static pdb t1_zc[3][256];               /* zero-coding contexts by neighbours   */
static pdb t1_sc[256];                  /* sign-coding context | xor << 7       */
static pdb *t1_zcp;
static pdw t1_w, t1_h, t1_fw, t1_vsc, t1_ready;

static void t1_tables(void)
{
    pdw f, h, v, d, hv, c, sn, ss, se, sw, t;
    pdi cn, cs, ce, cw, hh, vv;
    if (t1_ready) return;
    t1_ready = 1;
    for (f = 0; f < 256; f++) {
        h = ((f >> 7) & 1) + ((f >> 5) & 1);
        v = ((f >> 4) & 1) + ((f >> 6) & 1);
        d = (f & 1) + ((f >> 1) & 1) + ((f >> 2) & 1) + ((f >> 3) & 1);
        for (t = 0; t < 3; t++) {
            hh = h; vv = v;
            if (t == 1) { hh = v; vv = h; }
            if (t == 2) {
                hv = h + v;
                if (d >= 3) c = 8;
                else if (d == 2) c = hv >= 1 ? 7 : 6;
                else if (d == 1) c = hv >= 2 ? 5 : (hv == 1 ? 4 : 3);
                else c = hv >= 2 ? 2 : (hv == 1 ? 1 : 0);
            } else {
                if (hh == 2) c = 8;
                else if (hh == 1) c = vv >= 1 ? 7 : (d >= 1 ? 6 : 5);
                else if (vv == 2) c = 4;
                else if (vv == 1) c = 3;
                else c = d >= 2 ? 2 : (d == 1 ? 1 : 0);
            }
            t1_zc[t][f] = (pdb)c;
        }
    }
    for (f = 0; f < 256; f++) {
        sn = f & 1; se = (f >> 1) & 1; ss = (f >> 2) & 1; sw = (f >> 3) & 1;
        cn = (f & 0x10) ? (sn ? -1 : 1) : 0;
        ce = (f & 0x20) ? (se ? -1 : 1) : 0;
        cs = (f & 0x40) ? (ss ? -1 : 1) : 0;
        cw = (f & 0x80) ? (sw ? -1 : 1) : 0;
        hh = ce + cw; vv = cn + cs;
        if (hh > 1) hh = 1;
        if (hh < -1) hh = -1;
        if (vv > 1) vv = 1;
        if (vv < -1) vv = -1;
        /* (H, V) -> context, xor bit */
        if (hh == 1) { c = vv == 1 ? 13 : (vv == 0 ? 12 : 11); t = 0; }
        else if (hh == 0) { c = vv == 0 ? 9 : 10; t = vv == -1; }
        else { c = vv == 1 ? 11 : (vv == 0 ? 12 : 13); t = 1; }
        t1_sc[f] = (pdb)(c | (t << 7));
    }
}

static void t1_sig(pdw pos, pdw neg, pdw y)
{
    pdw fw = t1_fw;
    t1_f[pos] |= neg ? (T1_SIG | T1_NEG) : T1_SIG;
    if (!(t1_vsc && (y & 3) == 0)) {
        t1_f[pos - fw - 1] |= T1_SE;
        t1_f[pos - fw] |= neg ? (T1_S | T1_SGN_S) : T1_S;
        t1_f[pos - fw + 1] |= T1_SW;
    }
    t1_f[pos - 1] |= neg ? (T1_E | T1_SGN_E) : T1_E;
    t1_f[pos + 1] |= neg ? (T1_W | T1_SGN_W) : T1_W;
    t1_f[pos + fw - 1] |= T1_NE;
    t1_f[pos + fw] |= neg ? (T1_N | T1_SGN_N) : T1_N;
    t1_f[pos + fw + 1] |= T1_NW;
}
static pdw t1_sign(pdw f)
{
    pdw sc = t1_sc[(f & 0xF0u) | ((f >> 8) & 0x0Fu)];
    return xj_mqdec(sc & 0x7F) ^ (sc >> 7);
}

static void t1_spp(pdw bp)
{
    pdw k, i, j, pos, f, w = t1_w, h = t1_h, lim;
    for (k = 0; k < h; k += 4) {
        lim = k + 4 < h ? k + 4 : h;
        for (i = 0; i < w; i++)
            for (j = k; j < lim; j++) {
                pos = (j + 1) * t1_fw + i + 1;
                f = t1_f[pos];
                if (!(f & T1_SIG) && (f & 0xFFu)) {
                    if (xj_mqdec(t1_zcp[f & 0xFFu])) {
                        t1_sig(pos, t1_sign(f), j);
                        t1_m[j * w + i] = 3u << bp;
                    }
                    t1_f[pos] |= T1_VIS;
                }
            }
    }
}
static void t1_mrp(pdw bp)
{
    pdw k, i, j, pos, f, w = t1_w, h = t1_h, lim, half = 1u << bp;
    for (k = 0; k < h; k += 4) {
        lim = k + 4 < h ? k + 4 : h;
        for (i = 0; i < w; i++)
            for (j = k; j < lim; j++) {
                pos = (j + 1) * t1_fw + i + 1;
                f = t1_f[pos];
                if ((f & (T1_SIG | T1_VIS)) == T1_SIG) {
                    if (xj_mqdec((f & T1_REF) ? 16 : ((f & 0xFFu) ? 15 : 14)))
                        t1_m[j * w + i] += half;
                    else
                        t1_m[j * w + i] -= half;
                    t1_f[pos] |= T1_REF;
                }
            }
    }
}
static void t1_cup(pdw bp, pdw segsym)
{
    pdw k, i, j, pos, f, w = t1_w, h = t1_h, lim, run, j0, v, all;
    for (k = 0; k < h; k += 4) {
        lim = k + 4 < h ? k + 4 : h;
        for (i = 0; i < w; i++) {
            run = 4;
            if (k + 3 < h) {
                all = 1;
                for (j = k; j < k + 4; j++)
                    if (t1_f[(j + 1) * t1_fw + i + 1] & (T1_SIG | T1_VIS | 0xFFu)) { all = 0; break; }
                if (all) {
                    if (!xj_mqdec(17)) continue;
                    run = xj_mqdec(18) << 1;
                    run |= xj_mqdec(18);
                }
            }
            j0 = run < 4 ? k + run : k;
            for (j = j0; j < lim; j++) {
                pos = (j + 1) * t1_fw + i + 1;
                f = t1_f[pos];
                if (!(f & (T1_SIG | T1_VIS))) {
                    v = (run < 4 && j == j0) ? 1 : xj_mqdec(t1_zcp[f & 0xFFu]);
                    if (v) {
                        t1_sig(pos, t1_sign(f), j);
                        t1_m[j * w + i] = 3u << bp;
                    }
                }
                t1_f[pos] &= (pdw)~T1_VIS;
            }
        }
    }
    if (segsym) { xj_mqdec(18); xj_mqdec(18); xj_mqdec(18); xj_mqdec(18); }
}

/* decode a w x h code-block of `orient` (0 LL, 1 HL, 2 LH, 3 HH) from t1_cb[0..len):
 * P bit-planes, npasses passes. The result is t1_m and t1_f's sign bit. */
static void t1_block(pdw w, pdw h, pdw orient, pdw P, pdw npasses, pdw len, pdw style)
{
    pdw n, pass, type = 2, bp;
    t1_tables();
    t1_w = w; t1_h = h; t1_fw = w + 2; t1_vsc = style & 8;
    n = (h + 2) * (w + 2);
    for (pass = 0; pass < n; pass++) t1_f[pass] = 0;
    for (pass = 0; pass < w * h; pass++) t1_m[pass] = 0;
    t1_zcp = orient == 1 ? t1_zc[1] : (orient == 3 ? t1_zc[2] : t1_zc[0]);
    t1_cb[len] = 0xFF; t1_cb[len + 1] = 0xFF;
    xj_cxreset();
    xj_mqinit(t1_cb);
    bp = P - 1;
    for (pass = 0; pass < npasses; pass++) {
        if (type == 0) t1_spp(bp);
        else if (type == 1) t1_mrp(bp);
        else t1_cup(bp, style & 0x20);
        if (style & 2) xj_cxreset();
        if (++type == 3) {
            type = 0;
            if (bp == 0) break;
            bp--;
        }
    }
}
