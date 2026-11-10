/* ============================================================================
 * os8088 - apps/pagedesk/pdrxgr.c   PDREND's shading rasteriser (20.22.9)
 *
 * DLR_SHADE (axial, radial) and DLR_TRI (a Gouraud triangle) as scan-line
 * runs. A run of one class is ONE call of the sink xg_emit(), so a smooth
 * gradient costs a few runs a row, not a call a pixel. #included by pdrxg.c;
 * the host harness (hosttest/pdxgr_test.c) includes it alone, with its own
 * sink xg_emit() and loader xg_load(). 16.16 throughout (pdm_fx_*), no
 * recursion, every object static, no struct assignment (SPEC.md 73).
 *
 * The record bodies (PAGEDESK-SPEC 20.22.9):
 *   DLR_SHADE  xoff(2) m[6](24): the XC offset of the pdgx_shade entry and the
 *              affine device-pixel -> shading-space map [a b c d e f]
 *   DLR_TRI    x0 y0 x1 y1 x2 y2 (qpx, 2 each) lum0 c40 lum1 c41 lum2 c42
 * ==========================================================================*/
#define XG_SHBODY  26
#define XG_TRIBODY 18

static struct pdgx_shade xg_e;      /* the entry being drawn                   */
static pdb xg_cls[256];             /* ramp index -> run class                  */
static struct pd_fx xg_m[6];
static struct pd_fx xg_q0, xg_q1, xg_q2, xg_q3, xg_q4, xg_q5, xg_q6, xg_q7;   /* scratch */

static void xg_cp(struct pd_fx *d, const struct pd_fx *s) { d->hi = s->hi; d->lo = s->lo; }
static int xg_isgrey(pdw c4) { return c4 == 0 || c4 == 7 || c4 == 8 || c4 == 15; }
static pdw xg_grey_c4(pdw lum) { return lum < 48 ? 0 : (lum < 128 ? 8 : (lum < 208 ? 7 : 15)); }
static void xg_classes(void)
{
    pdw i, lum, c4;
    for (i = 0; i < 256; i++) {
        lum = xg_e.ramp[2 * i]; c4 = xg_e.ramp[2 * i + 1];
        xg_cls[i] = (pdb)(xg_isgrey(c4) ? (lum >> 4) : (16 + c4));
    }
}
static void xg_shr(struct pd_fx *r, pdw k)      /* r >>= k (arithmetic), k < 16 */
{
    if (!k) return;
    r->lo = (pdw)((r->lo >> k) | ((pdw)r->hi << (16 - k)));
    r->hi = (pdi)(r->hi >> k);
}

/* ---- axial: U = 255 s is linear in the pixel ------------------------------------- */
static struct pd_fx xg_sa, xg_sb, xg_sc;    /* U(X, Y) = sa X + sb Y + sc          */

static void xg_axial_row(pdi y, pdi rx0, pdi rx1)
{
    pdi x = rx0, xe, dir;
    pdw idx, c, j, ext0 = xg_e.flags & PDGX_SH_EXT0, ext1 = xg_e.flags & PDGX_SH_EXT1;
    dir = (xg_sa.hi < 0) ? -1 : ((xg_sa.hi == 0 && xg_sa.lo == 0) ? 0 : 1);
    while (x <= rx1) {
        pdm_fx_int(&xg_q0, x); pdm_fx_mul(&xg_q0, &xg_q0, &xg_sa); pdm_fx_add(&xg_q0, &xg_q0, &xg_q6);
        if (xg_q0.hi < 0) {                                 /* before the first stop */
            if (!ext0) {
                if (dir <= 0) return;
                pdm_fx_neg(&xg_q1, &xg_q0); pdm_fx_div(&xg_q1, &xg_q1, &xg_sa);
                x = (pdi)(x + pdm_fx_floor(&xg_q1) + 1); continue;
            }
            idx = 0;
        } else if (xg_q0.hi >= 255) {
            if (!ext1) {
                if (dir >= 0) return;
                pdm_fx_int(&xg_q1, 255); pdm_fx_sub(&xg_q1, &xg_q0, &xg_q1);
                pdm_fx_neg(&xg_q2, &xg_sa); pdm_fx_div(&xg_q1, &xg_q1, &xg_q2);
                x = (pdi)(x + pdm_fx_floor(&xg_q1) + 1); continue;
            }
            idx = 255;
        } else idx = (pdw)xg_q0.hi;
        c = xg_cls[idx];
        if (!dir) { xg_emit(y, x, rx1, xg_e.ramp[2 * idx], xg_e.ramp[2 * idx + 1]); return; }
        j = idx;
        if (dir > 0) { while (j < 255 && xg_cls[j + 1] == c) j++; }
        else { while (j > 0 && xg_cls[j - 1] == c) j--; }
        if ((dir > 0 && j == 255 && ext1) || (dir < 0 && j == 0 && ext0)) xe = rx1;
        else {
            /* U leaves the class at index j+1 (rising) or j (falling): the pixels until then */
            pdm_fx_int(&xg_q1, (pdi)(dir > 0 ? j + 1 : j));
            pdm_fx_sub(&xg_q1, &xg_q1, &xg_q0);
            if (dir > 0) pdm_fx_div(&xg_q1, &xg_q1, &xg_sa);
            else { pdm_fx_neg(&xg_q2, &xg_sa); pdm_fx_neg(&xg_q1, &xg_q1); pdm_fx_div(&xg_q1, &xg_q1, &xg_q2); }
            xe = (pdi)(x + pdm_fx_floor(&xg_q1));
            if (xg_q1.lo == 0 && xe > x) xe--;
            if (xe < x) xe = x;
            if (xe > rx1) xe = rx1;
        }
        j = (pdw)((idx + j) >> 1);                              /* the run's middle entry stands for it */
        xg_emit(y, x, xe, xg_e.ramp[2 * j], xg_e.ramp[2 * j + 1]);
        x = (pdi)(xe + 1);
    }
}

/* ---- radial ---------------------------------------------------------------------- */
static struct pd_fx xg_rc[6];       /* x0 y0 r0 dx dy dr, in circle units (the coordinates / 2^k) */
static struct pd_fx xg_ra;          /* dx^2 + dy^2 - dr^2 */

static void xg_rsqrt(struct pd_fx *r, const struct pd_fx *v)
{
    pdi k;
    if (v->hi < 0 || (v->hi == 0 && v->lo == 0)) { r->hi = 0; r->lo = 0; return; }
    pdm_fx_int(&xg_q7, 1); pdm_fx_add(r, v, &xg_q7); xg_shr(r, 1);        /* seed (v + 1) / 2 */
    for (k = 0; k < 14; k++) {
        if (r->hi == 0 && r->lo == 0) return;
        pdm_fx_div(&xg_q7, v, r);
        pdm_fx_add(r, r, &xg_q7);
        xg_shr(r, 1);
    }
}

/* the ramp index at the centre of pixel (X, Y), or -1 when nothing is painted there */
static pdi xg_radial_at(pdi X, pdi Y)
{
    static struct pd_fx px, py, b, cc, disc, s1, s2, t, s, rr;
    pdw ext0 = xg_e.flags & PDGX_SH_EXT0, ext1 = xg_e.flags & PDGX_SH_EXT1, k;
    pdm_fx_int(&px, X); px.lo = 0x8000;
    pdm_fx_int(&py, Y); py.lo = 0x8000;
    pdm_fx_mul(&t, &xg_m[0], &px); pdm_fx_mul(&xg_q0, &xg_m[2], &py); pdm_fx_add(&t, &t, &xg_q0); pdm_fx_add(&t, &t, &xg_m[4]);
    pdm_fx_mul(&xg_q1, &xg_m[1], &px); pdm_fx_mul(&xg_q0, &xg_m[3], &py); pdm_fx_add(&xg_q1, &xg_q1, &xg_q0); pdm_fx_add(&xg_q1, &xg_q1, &xg_m[5]);
    pdm_fx_sub(&px, &t, &xg_rc[0]); pdm_fx_sub(&py, &xg_q1, &xg_rc[1]);
    if (px.hi > 150) px.hi = 150;
    if (px.hi < -150) px.hi = -150;
    if (py.hi > 150) py.hi = 150;
    if (py.hi < -150) py.hi = -150;
    pdm_fx_mul(&b, &px, &xg_rc[3]); pdm_fx_mul(&t, &py, &xg_rc[4]); pdm_fx_add(&b, &b, &t);
    pdm_fx_mul(&t, &xg_rc[2], &xg_rc[5]); pdm_fx_add(&b, &b, &t);
    pdm_fx_mul(&cc, &px, &px); pdm_fx_mul(&t, &py, &py); pdm_fx_add(&cc, &cc, &t);
    pdm_fx_mul(&t, &xg_rc[2], &xg_rc[2]); pdm_fx_sub(&cc, &cc, &t);
    if ((xg_ra.hi == 0 && xg_ra.lo < 0x40) || (xg_ra.hi == -1 && xg_ra.lo > 0xFFC0)) {   /* a ~ 0: linear */
        if (b.hi == 0 && b.lo == 0) return -1;
        pdm_fx_add(&t, &b, &b); pdm_fx_div(&s1, &cc, &t);
        xg_cp(&s2, &s1);
    } else {
        pdm_fx_mul(&disc, &b, &b); pdm_fx_mul(&t, &xg_ra, &cc); pdm_fx_sub(&disc, &disc, &t);
        if (disc.hi < 0) return -1;
        xg_rsqrt(&t, &disc);
        pdm_fx_add(&s1, &b, &t); pdm_fx_div(&s1, &s1, &xg_ra);
        pdm_fx_sub(&s2, &b, &t); pdm_fx_div(&s2, &s2, &xg_ra);
        if (pdm_fx_cmp(&s1, &s2) < 0) { xg_cp(&t, &s1); xg_cp(&s1, &s2); xg_cp(&s2, &t); }
    }
    for (k = 0; k < 2; k++) {                   /* the larger root first (ISO 32000-1 8.7.4.5.4) */
        xg_cp(&s, k == 0 ? &s1 : &s2);
        pdm_fx_mul(&rr, &xg_rc[5], &s); pdm_fx_add(&rr, &rr, &xg_rc[2]);
        if (rr.hi < 0) continue;
        if (s.hi < 0) { if (!ext0) continue; return 0; }
        if (s.hi > 0 && (s.hi > 1 || s.lo)) { if (!ext1) continue; return 255; }
        pdm_fx_int(&t, 255); pdm_fx_mul(&t, &t, &s);
        k = (pdw)pdm_fx_round(&t);
        return (pdi)(k > 255 ? 255 : k);
    }
    return -1;
}

#define XG_RSTEP 4              /* radial rows are sampled every 4 pixels and held between */
static void xg_radial_row(pdi y, pdi rx0, pdi rx1)
{
    pdi runx = rx0, idx, last = -2, x, xs;
    for (xs = (pdi)(rx0 & ~(XG_RSTEP - 1)); xs <= rx1; xs = (pdi)(xs + XG_RSTEP)) {
        idx = xg_radial_at((pdi)(xs + XG_RSTEP / 2), y);
        x = xs < rx0 ? rx0 : xs;
        if (last == -2) { last = idx; runx = x; continue; }
        if (idx < 0 || last < 0 ? idx != last : xg_cls[idx] != xg_cls[last]) {
            if (last >= 0) xg_emit(y, runx, (pdi)(x - 1), xg_e.ramp[2 * last], xg_e.ramp[2 * last + 1]);
            last = idx; runx = x;
        }
    }
    if (last >= 0) xg_emit(y, runx, rx1, xg_e.ramp[2 * last], xg_e.ramp[2 * last + 1]);
}

/* ---- the entry point for DLR_SHADE -------------------------------------------------- */
static void xg_shade_rows(pdw xoff, const struct pd_fx *m, pdi rx0, pdi rx1, pdi y0, pdi y1)
{
    static struct pd_fx dx, dy, ux, uy, K, big, t;
    pdw i;
    pdi y;
    xg_load(xoff, &xg_e, sizeof xg_e);
    if (xg_e.type != 2 && xg_e.type != 3) return;
    for (i = 0; i < 6; i++) xg_cp(&xg_m[i], &m[i]);
    xg_classes();
    pdm_fx_sub(&dx, &xg_e.coords[2], &xg_e.coords[0]); pdm_fx_sub(&dy, &xg_e.coords[3], &xg_e.coords[1]);
    if (xg_e.type == 2) {
        /* U = 255 ((P - p0) . d) / |d|^2, with d scaled by its larger part so no square overflows 16.16 */
        xg_cp(&ux, &dx); if (ux.hi < 0) pdm_fx_neg(&ux, &ux);
        xg_cp(&uy, &dy); if (uy.hi < 0) pdm_fx_neg(&uy, &uy);
        xg_cp(&big, pdm_fx_cmp(&ux, &uy) >= 0 ? &ux : &uy);
        if (big.hi == 0 && big.lo == 0) return;
        pdm_fx_div(&ux, &dx, &big); pdm_fx_div(&uy, &dy, &big);
        pdm_fx_mul(&K, &ux, &ux); pdm_fx_mul(&t, &uy, &uy); pdm_fx_add(&K, &K, &t);
        pdm_fx_mul(&K, &K, &big);                                   /* |d|^2 / big */
        pdm_fx_int(&t, 255); pdm_fx_div(&K, &K, &t);                /* ... / 255 */
        pdm_fx_mul(&xg_sa, &xg_m[0], &ux); pdm_fx_mul(&t, &xg_m[1], &uy); pdm_fx_add(&xg_sa, &xg_sa, &t); pdm_fx_div(&xg_sa, &xg_sa, &K);
        pdm_fx_mul(&xg_sb, &xg_m[2], &ux); pdm_fx_mul(&t, &xg_m[3], &uy); pdm_fx_add(&xg_sb, &xg_sb, &t); pdm_fx_div(&xg_sb, &xg_sb, &K);
        pdm_fx_sub(&xg_sc, &xg_m[4], &xg_e.coords[0]); pdm_fx_mul(&xg_sc, &xg_sc, &ux);
        pdm_fx_sub(&t, &xg_m[5], &xg_e.coords[1]); pdm_fx_mul(&t, &t, &uy); pdm_fx_add(&xg_sc, &xg_sc, &t);
        pdm_fx_div(&xg_sc, &xg_sc, &K);
        xg_q5.hi = 0; xg_q5.lo = 0x8000;                            /* 0.5: the pixel centre */
        for (y = y0; y <= y1; y++) {
            /* q6 = sb (y + .5) + sc + sa .5 */
            pdm_fx_int(&xg_q6, y); pdm_fx_add(&xg_q6, &xg_q6, &xg_q5); pdm_fx_mul(&xg_q6, &xg_q6, &xg_sb);
            pdm_fx_add(&xg_q6, &xg_q6, &xg_sc);
            pdm_fx_mul(&t, &xg_sa, &xg_q5); pdm_fx_add(&xg_q6, &xg_q6, &t);
            xg_axial_row(y, rx0, rx1);
        }
        return;
    }
    {
        /* radial: scale the circles and the map by 2^-k so every square fits */
        pdw k = 0;
        pdi h, hm = 1;
        pdm_fx_sub(&dx, &xg_e.coords[3], &xg_e.coords[0]); pdm_fx_sub(&dy, &xg_e.coords[4], &xg_e.coords[1]);
        pdm_fx_sub(&ux, &xg_e.coords[5], &xg_e.coords[2]);          /* dr */
        h = xg_e.coords[2].hi; if (h > hm) hm = h;
        h = xg_e.coords[5].hi; if (h > hm) hm = h;
        h = dx.hi < 0 ? (pdi)-dx.hi : dx.hi; if (h > hm) hm = h;
        h = dy.hi < 0 ? (pdi)-dy.hi : dy.hi; if (h > hm) hm = h;
        h = ux.hi < 0 ? (pdi)-ux.hi : ux.hi; if (h > hm) hm = h;
        while ((hm >> k) > 0 && k < 14) k++;
        for (i = 0; i < 6; i++) xg_shr(&xg_m[i], k);
        xg_cp(&xg_rc[0], &xg_e.coords[0]); xg_cp(&xg_rc[1], &xg_e.coords[1]); xg_cp(&xg_rc[2], &xg_e.coords[2]);
        xg_cp(&xg_rc[3], &dx); xg_cp(&xg_rc[4], &dy); xg_cp(&xg_rc[5], &ux);
        for (i = 0; i < 6; i++) xg_shr(&xg_rc[i], k);
        pdm_fx_mul(&xg_ra, &xg_rc[3], &xg_rc[3]); pdm_fx_mul(&t, &xg_rc[4], &xg_rc[4]); pdm_fx_add(&xg_ra, &xg_ra, &t);
        pdm_fx_mul(&t, &xg_rc[5], &xg_rc[5]); pdm_fx_sub(&xg_ra, &xg_ra, &t);
        for (y = y0; y <= y1; y++) xg_radial_row(y, rx0, rx1);
    }
}

/* ---- a Gouraud triangle ------------------------------------------------------------- */
static pdi xg_tq[6];                /* vertices, qpx */
static pdi xg_tcx0, xg_tcx1;        /* the columns the caller will keep (scissor) */
static pdb xg_tl[6];                /* lum, c4 a vertex */
static void xg_tri_rows(pdi y0, pdi y1)
{
    static struct pd_fx cx[2], cl[2], t, fy, fl, dl, ll;
    pdi y, yq, i, j, n, ymin, ymax, pa, pb, p;
    pdw chroma = !(xg_isgrey(xg_tl[1]) && xg_isgrey(xg_tl[3]) && xg_isgrey(xg_tl[5]));
    pdi runa, lq, cls, lastc = -1, lastlum = 0;
    ymin = ymax = xg_tq[1];
    for (i = 1; i < 3; i++) { if (xg_tq[2 * i + 1] < ymin) ymin = xg_tq[2 * i + 1]; if (xg_tq[2 * i + 1] > ymax) ymax = xg_tq[2 * i + 1]; }
    if (y0 < (pdi)((ymin + 1) >> 2)) y0 = (pdi)((ymin + 1) >> 2);
    if (y1 > (pdi)(((ymax + 1) >> 2) - 1)) y1 = (pdi)(((ymax + 1) >> 2) - 1);
    for (y = y0; y <= y1; y++) {
        yq = (pdi)(4 * y + 2);
        n = 0;
        for (i = 0; i < 3 && n < 2; i++) {
            pdi ax, ay, bx, by, lo, hi;
            j = (pdi)((i + 1) % 3);
            ax = xg_tq[2 * i]; ay = xg_tq[2 * i + 1]; bx = xg_tq[2 * j]; by = xg_tq[2 * j + 1];
            lo = ay < by ? ay : by; hi = ay < by ? by : ay;
            if (yq < lo || yq >= hi) continue;
            pdm_fx_int(&fy, (pdi)(yq - ay)); pdm_fx_int(&t, (pdi)(by - ay)); pdm_fx_div(&fy, &fy, &t);      /* t along the edge */
            pdm_fx_int(&t, (pdi)(bx - ax)); pdm_fx_mul(&t, &t, &fy); pdm_fx_int(&cx[n], ax); pdm_fx_add(&cx[n], &cx[n], &t);
            pdm_fx_int(&t, (pdi)((pdi)xg_tl[2 * j] - (pdi)xg_tl[2 * i])); pdm_fx_mul(&t, &t, &fy);
            pdm_fx_int(&cl[n], (pdi)xg_tl[2 * i]); pdm_fx_add(&cl[n], &cl[n], &t);
            n++;
        }
        if (n < 2) continue;
        if (pdm_fx_cmp(&cx[0], &cx[1]) > 0) {
            xg_cp(&t, &cx[0]); xg_cp(&cx[0], &cx[1]); xg_cp(&cx[1], &t);
            xg_cp(&t, &cl[0]); xg_cp(&cl[0], &cl[1]); xg_cp(&cl[1], &t);
        }
        /* pixels p with 4p + 2 in [cx0, cx1) */
        pa = (pdi)((cx[0].hi + 1 + (cx[0].lo ? 1 : 0)) >> 2);
        pb = (pdi)(((cx[1].hi + 1 + (cx[1].lo ? 1 : 0)) >> 2) - 1);
        if (pa > xg_tcx1 || pb < xg_tcx0) continue;
        /* lum at the first sample, and per pixel */
        pdm_fx_int(&t, (pdi)(4 * pa + 2)); pdm_fx_sub(&t, &t, &cx[0]);          /* offset of the first sample in qpx */
        pdm_fx_sub(&dl, &cl[1], &cl[0]);
        pdm_fx_sub(&fl, &cx[1], &cx[0]);
        if (fl.hi == 0 && fl.lo == 0) { dl.hi = 0; dl.lo = 0; xg_cp(&ll, &cl[0]); }
        else {
            pdm_fx_div(&dl, &dl, &fl);                                       /* lum per qpx */
            pdm_fx_mul(&ll, &dl, &t); pdm_fx_add(&ll, &ll, &cl[0]);          /* lum at the first sample */
            pdm_fx_int(&t, 4); pdm_fx_mul(&dl, &dl, &t);                     /* lum per pixel */
        }
        runa = pa;
        for (p = pa; p <= pb; p++) {
            lq = ll.hi < 0 ? 0 : (ll.hi > 255 ? 255 : ll.hi);
            if (chroma) {                                                   /* the vertex nearest in luminance */
                pdi best = 0, d0 = 999, d;
                for (i = 0; i < 3; i++) { d = (pdi)xg_tl[2 * i] - lq; if (d < 0) d = -d; if (d < d0) { d0 = d; best = i; } }
                cls = (pdi)(32 * (best + 1) + (lq >> 4));
            } else cls = lq >> 4;
            if (cls != lastc) {
                if (lastc >= 0 && p > runa) {
                    if (chroma) xg_emit(y, runa, (pdi)(p - 1), lastlum, xg_tl[2 * (lastc / 32 - 1) + 1]);
                    else xg_emit(y, runa, (pdi)(p - 1), lastlum, xg_grey_c4(lastlum));
                }
                lastc = cls; runa = p; lastlum = lq;
            }
            pdm_fx_add(&ll, &ll, &dl);
            if (p == pb) break;
        }
        if (lastc >= 0) {
            if (chroma) xg_emit(y, runa, pb, lastlum, xg_tl[2 * (lastc / 32 - 1) + 1]);
            else xg_emit(y, runa, pb, lastlum, xg_grey_c4(lastlum));
        }
        lastc = -1;
    }
}
