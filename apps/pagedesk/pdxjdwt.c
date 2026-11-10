/* ---- pdxjdwt.c: the inverse wavelet transform, in place (20.22.13.4, .5) ------- */

#define K97_ALPHA (-25987)
#define K97_BETA  (-868)
#define K97_GAMMA 14466
#define K97_DELTA 7266
#define K97_K     20155
#define K97_IK    13318

/* the transform's row work overlays the scratch array after the packet lists, and the
 * interleave buffer overlays the tier-1 flags (both dead while a stage runs) */
#define jx_E ((pdi *)(jx_scr + 3 * JX_NCBPKT))
#define jx_O (jx_E + (JX_MAXDIM / 2 + 2))
#define jx_R ((pdi *)t1_f)
static pdw jx_rowmap[JX_MAXDIM];
static pdw g_pstride;                   /* bytes in a row of the plane          */

static pdw prow(pdw y) { return w_plane + jx_rowmap[y] * g_pstride; }

/* one row of a stage: the low half then the high half in, the interleaved row out */
static void hrow(pdw off, pdw rw, pdw lw)
{
    pdw nh = rw - lw;
    pd_fcopy_in(g_ws, off, jx_E + 1, lw * 2);
    pd_fcopy_in(g_ws, off + 2 * lw, jx_O + 1, nh * 2);
    if (g_rev) {
        jx_O[0] = jx_O[1]; jx_O[nh + 1] = jx_O[nh];
        xj_l53_n(jx_E + 1, jx_O, jx_O + 1, lw, 0);
        jx_E[lw + 1] = jx_E[lw];
        xj_l53_n(jx_O + 1, jx_E + 1, jx_E + 2, nh, 1);
    } else {
        xj_scal_n(jx_E + 1, lw, K97_K);
        xj_scal_n(jx_O + 1, nh, K97_IK);
        jx_O[0] = jx_O[1]; jx_O[nh + 1] = jx_O[nh];
        xj_lift_n(jx_E + 1, jx_O, jx_O + 1, lw, K97_DELTA);
        jx_E[lw + 1] = jx_E[lw];
        xj_lift_n(jx_O + 1, jx_E + 1, jx_E + 2, nh, K97_GAMMA);
        jx_O[0] = jx_O[1]; jx_O[nh + 1] = jx_O[nh];
        xj_lift_n(jx_E + 1, jx_O, jx_O + 1, lw, K97_BETA);
        jx_E[lw + 1] = jx_E[lw];
        xj_lift_n(jx_O + 1, jx_E + 1, jx_E + 2, nh, K97_ALPHA);
    }
    xj_inter(jx_R, jx_E + 1, jx_O + 1, lw, nh);
    pd_fcopy_out(jx_R, g_ws, off, rw * 2);
}

/* the vertical pass of a stage over rows 0..rh-1, lh of them low: whole rows,
 * neighbours chosen by index, and the interleave is a permutation of the row map */
static void vstep(pdw lh, pdw nh, pdw rw, pdi k, pdw mode, pdw odd)
{
    pdw j, a, b;
    if (!odd) {                                         /* update the low rows from the high ones */
        for (j = 0; j < lh; j++) {
            a = prow(lh + (j ? j - 1 : 0)); b = prow(lh + (j < nh ? j : nh - 1));
            if (g_rev) xj_l53_f(g_ws, prow(j), a, b, rw, mode);
            else xj_lift_f(g_ws, prow(j), a, b, rw, k);
        }
    } else {                                            /* update the high rows from the low ones */
        for (j = 0; j < nh; j++) {
            a = prow(j); b = prow(j + 1 < lh ? j + 1 : lh - 1);
            if (g_rev) xj_l53_f(g_ws, prow(lh + j), a, b, rw, mode);
            else xj_lift_f(g_ws, prow(lh + j), a, b, rw, k);
        }
    }
}
static void vcol(pdw rw, pdw rh, pdw lh)
{
    pdw nh = rh - lh, j;
    if (!g_rev) {
        for (j = 0; j < lh; j++) xj_scal_f(g_ws, prow(j), rw, K97_K);
        for (j = 0; j < nh; j++) xj_scal_f(g_ws, prow(lh + j), rw, K97_IK);
        vstep(lh, nh, rw, K97_DELTA, 0, 0);
        vstep(lh, nh, rw, K97_GAMMA, 0, 1);
        vstep(lh, nh, rw, K97_BETA, 0, 0);
        vstep(lh, nh, rw, K97_ALPHA, 0, 1);
    } else {
        vstep(lh, nh, rw, 0, 0, 0);
        vstep(lh, nh, rw, 0, 1, 1);
    }
    for (j = 0; j < lh; j++) {
        jx_R[2 * j] = (pdi)jx_rowmap[j];
        if (j < nh) jx_R[2 * j + 1] = (pdi)jx_rowmap[lh + j];
    }
    for (j = 0; j < rh; j++) jx_rowmap[j] = (pdw)jx_R[j];
}

/* the rows of the plane are addressed through the row map for the whole of a
 * tile-component: stage lev reads the rows its predecessor interleaved, and the
 * bands of resolution lev are placed in the same logical rows (decode_comp). */
static void rowmap_id(void) { pdw y; for (y = 0; y < JX_MAXDIM; y++) jx_rowmap[y] = y; }

/* synthesise resolution lev from lev-1 and the three bands placed at lev */
static void idwt_stage(pdw lev)
{
    pdw rw, rh, lw, lh, y;
    rw = g_rx1[lev] - g_rx0[lev]; rh = g_ry1[lev] - g_ry0[lev];
    lw = g_rx1[lev - 1] - g_rx0[lev - 1]; lh = g_ry1[lev - 1] - g_ry0[lev - 1];
    if (rw > 1) for (y = 0; y < rh; y++) hrow(prow(y), rw, lw);
    if (rh > 1) vcol(rw, rh, lh);
}
