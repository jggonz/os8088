/* ============================================================================
 * os8088 - apps/pagedesk/pdxitwin.c     the C twins of pdxidct.inc (HOST ONLY)
 *
 * pdi_hsym, pdi_bits, pdi_block and pdi_idct as C, with the exact semantics of
 * the assembly: every pre-multiply sum is wrapped to 16 bits, every product is
 * 32 bits, a descaled result is the low 16 bits of the 32-bit shift. For a
 * valid image these equal libjpeg's jidctint / jidctred; for a corrupt one they
 * are what the 8086 does. Compiled only with -DPD_HOST (int is 32 bits there).
 * The differential test hosttest/pdi_asm.py holds the assembly to these.
 * ==========================================================================*/
#include <stdint.h>

static pdw tw_peek(void)
{
    pdw b = pdi_pos >> 3, o = pdi_pos & 7;
    uint32_t v = ((uint32_t)pdi_ebuf[b] << 16) | ((uint32_t)pdi_ebuf[b + 1] << 8) | pdi_ebuf[b + 2];
    return (pdw)((v >> (8 - o)) & 0xFFFF);
}

pdw pdi_hsym(struct dj_huff *t)
{
    pdw v = tw_peek(), e = t->look[v >> 8], L, code;
    if (e) { pdi_pos = (pdw)(pdi_pos + (e >> 8)); return e & 0xFF; }
    for (L = 9; L <= 16; L++) {
        code = (pdw)(v >> (16 - L));
        if (code < t->limit[L]) {
            pdi_pos = (pdw)(pdi_pos + L);
            return t->vals[(pdw)(code + t->valoff[L]) & 0xFF];
        }
    }
    pdi_pos = (pdw)(pdi_pos + 16);
    return 0;
}

pdw pdi_bits(pdw n)
{
    pdw v = tw_peek();
    pdi_pos = (pdw)(pdi_pos + n);
    return n >= 16 ? v : (pdw)(v >> (16 - n));
}

static pdi tw_ext(pdw v, pdw s)
{
    if (s == 0) return 0;
    if (v < (pdw)(1u << (s - 1))) return (pdi)(int16_t)(v + 1 - (1u << s));
    return (pdi)(int16_t)v;
}

pdi pdi_block(struct dj_huff *dc, struct dj_huff *ac, pdi *coef)
{
    pdw s, rs, r, k;
    pdi diff;
    for (k = 0; k < 64; k++) coef[k] = 0;
    s = pdi_hsym(dc) & 15;
    diff = s ? tw_ext(pdi_bits(s), s) : 0;
    k = 1;
    while (k < 64) {
        rs = pdi_hsym(ac); r = rs >> 4; s = rs & 15;
        if (s == 0) {
            if (r == 15) { k += 16; continue; }
            break;
        }
        k += r;
        if (k > 63) break;
        coef[pdi_zz[k]] = tw_ext(pdi_bits(s), s);
        k++;
    }
    return diff;
}

/* ---- the transforms -------------------------------------------------------- */
static int16_t tw_ws[64];
static int16_t tw_i[8];
static uint32_t tw_o[8];
#define W16(x) ((int16_t)(x))

static void tw_k8(void)
{
    uint32_t z1, tmp0, tmp1, tmp2, tmp3, tmp10, tmp11, tmp12, tmp13, t0, t1, t2, t3, z2, z3, z4, z5;
    z1 = (uint32_t)((int32_t)W16(tw_i[2] + tw_i[6]) * 4433);
    tmp2 = z1 + (uint32_t)((int32_t)tw_i[6] * -15137);
    tmp3 = z1 + (uint32_t)((int32_t)tw_i[2] * 6270);
    tmp0 = (uint32_t)((int32_t)W16(tw_i[0] + tw_i[4]) * 8192);
    tmp1 = (uint32_t)((int32_t)W16(tw_i[0] - tw_i[4]) * 8192);
    tmp10 = tmp0 + tmp3; tmp13 = tmp0 - tmp3; tmp11 = tmp1 + tmp2; tmp12 = tmp1 - tmp2;
    t0 = (uint32_t)(int32_t)tw_i[7]; t1 = (uint32_t)(int32_t)tw_i[5]; t2 = (uint32_t)(int32_t)tw_i[3]; t3 = (uint32_t)(int32_t)tw_i[1];
    z1 = (uint32_t)(int32_t)W16(tw_i[7] + tw_i[1]); z2 = (uint32_t)(int32_t)W16(tw_i[5] + tw_i[3]);
    z3 = (uint32_t)(int32_t)W16(tw_i[7] + tw_i[3]); z4 = (uint32_t)(int32_t)W16(tw_i[5] + tw_i[1]);
    z5 = (uint32_t)((int32_t)W16((int32_t)z3 + (int32_t)z4) * 9633);
    t0 = (uint32_t)((int32_t)W16((int32_t)t0) * 2446); t1 = (uint32_t)((int32_t)W16((int32_t)t1) * 16819); t2 = (uint32_t)((int32_t)W16((int32_t)t2) * 25172); t3 = (uint32_t)((int32_t)W16((int32_t)t3) * 12299);
    z1 = (uint32_t)((int32_t)W16((int32_t)z1) * -7373); z2 = (uint32_t)((int32_t)W16((int32_t)z2) * -20995); z3 = (uint32_t)((int32_t)W16((int32_t)z3) * -16069); z4 = (uint32_t)((int32_t)W16((int32_t)z4) * -3196);
    z3 += z5; z4 += z5;
    t0 += z1 + z3; t1 += z2 + z4; t2 += z2 + z3; t3 += z1 + z4;
    tw_o[0] = tmp10 + t3; tw_o[7] = tmp10 - t3;
    tw_o[1] = tmp11 + t2; tw_o[6] = tmp11 - t2;
    tw_o[2] = tmp12 + t1; tw_o[5] = tmp12 - t1;
    tw_o[3] = tmp13 + t0; tw_o[4] = tmp13 - t0;
}
static void tw_k4(void)
{
    uint32_t tmp0, tmp2, tmp10, tmp12;
    tmp0 = (uint32_t)((int32_t)tw_i[0] * 16384);
    tmp2 = (uint32_t)((int32_t)tw_i[2] * 15137) + (uint32_t)((int32_t)tw_i[6] * -6270);
    tmp10 = tmp0 + tmp2; tmp12 = tmp0 - tmp2;
    tmp0 = (uint32_t)((int32_t)tw_i[7] * -1730) + (uint32_t)((int32_t)tw_i[5] * 11893) + (uint32_t)((int32_t)tw_i[3] * -17799) + (uint32_t)((int32_t)tw_i[1] * 8697);
    tmp2 = (uint32_t)((int32_t)tw_i[7] * -4176) + (uint32_t)((int32_t)tw_i[5] * -4926) + (uint32_t)((int32_t)tw_i[3] * 7373) + (uint32_t)((int32_t)tw_i[1] * 20995);
    tw_o[0] = tmp10 + tmp2; tw_o[3] = tmp10 - tmp2;
    tw_o[1] = tmp12 + tmp0; tw_o[2] = tmp12 - tmp0;
}
static void tw_k2(void)
{
    uint32_t tmp10, tmp0;
    tmp10 = (uint32_t)((int32_t)tw_i[0] * 32768);
    tmp0 = (uint32_t)((int32_t)tw_i[7] * -5906) + (uint32_t)((int32_t)tw_i[5] * 6967) + (uint32_t)((int32_t)tw_i[3] * -10426) + (uint32_t)((int32_t)tw_i[1] * 29692);
    tw_o[0] = tmp10 + tmp0; tw_o[1] = tmp10 - tmp0;
}
static int16_t tw_desc(uint32_t x, int n) { return W16((int32_t)(x + ((uint32_t)1 << (n - 1))) >> n); }

void pdi_idct(pdi *coef, pdw *qt, pdw mode, pdw dseg, pdw doff, pdw stride)
{
    int c, r, k, n, nout, sh1, sh2, zero;
    static const int cols2[5] = { 0, 1, 3, 5, 7 };
    for (k = 0; k < 64; k++) tw_ws[k] = W16((int32_t)coef[k] * (int32_t)(int16_t)qt[k]);
    nout = mode == 0 ? 8 : (mode == 1 ? 4 : 2);
    sh1 = mode == 0 ? 11 : (mode == 1 ? 12 : 13);
    sh2 = sh1 + 7;
    for (c = 0; c < 8; c++) {
        if (mode == 1 && c == 4) continue;
        if (mode == 2 && !(c == 0 || c == 1 || c == 3 || c == 5 || c == 7)) continue;
        for (k = 0; k < 8; k++) tw_i[k] = tw_ws[k * 8 + c];
        zero = 1;
        for (k = 1; k < 8; k++) if (tw_i[k]) zero = 0;
        if (zero) {
            for (r = 0; r < nout; r++) tw_ws[r * 8 + c] = W16(tw_i[0] * 4);
            continue;
        }
        if (mode == 0) tw_k8(); else if (mode == 1) tw_k4(); else tw_k2();
        for (r = 0; r < nout; r++) tw_ws[r * 8 + c] = tw_desc(tw_o[r], sh1);
    }
    (void)cols2;
    for (r = 0; r < nout; r++) {
        for (k = 0; k < 8; k++) tw_i[k] = tw_ws[r * 8 + k];
        if (mode == 0) tw_k8(); else if (mode == 1) tw_k4(); else tw_k2();
        for (n = 0; n < nout; n++) {
            int v = tw_desc(tw_o[n], sh2) + 128;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            pd_fpokeb(dseg, (pdw)(doff + r * stride + n), (pdb)v);
        }
    }
}
