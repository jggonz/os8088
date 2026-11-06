/* ============================================================================
 * os8088 - apps/pagedesk/pdrd.c     PDREND: display list and band rasteriser
 *
 * #included by pdrend.c. PAGEDESK-SPEC 10.1-10.5, 20.3.2.
 * ==========================================================================*/

/* ---- little-endian field access ----------------------------------------- */
static void w16(pdb *p, pdi v) { p[0] = (pdb)v; p[1] = (pdb)((pdw)v >> 8); }
static pdi  r16(const pdb *p)  { return (pdi)(pdw)(p[0] | (p[1] << 8)); }

/* ---- the display list --------------------------------------------------- */
static pdw g_dls;                   /* DL segment                            */
static pdw g_dlcap, g_dllen;        /* bytes                                  */
static pdw g_dlfull;
static pdw g_ndl;                   /* records, for the status               */

/* the group of canvas rows this pass interprets and draws (10.2, 20.3.2) */
static pdi g_gy0, g_gy1;            /* pixel rows, inclusive                  */
/* the canvas: what the primary asked to be drawn */
static pdi g_vcx0, g_vcy0, g_vcx1, g_vcy1;
static pdi g_pgx0, g_pgy0, g_pgx1, g_pgy1;     /* the displayed page          */
static pdw g_bpp;                   /* 1 or 4: the band depth                 */
static pdw g_bseg, g_bbytes;        /* the band claim                         */
static pdi g_bx0, g_bw;             /* band left pixel (multiple of 8), width */
static pdw g_bstride;
static pdi g_by0;
static pdw g_brows;
static pdi g_wlo, g_whi;            /* the rows this walk may touch: the band, or
                                     * the sub-range of one band (20.3.2)     */

static pdb g_hdr[DLH];
static pdb g_stl[16], g_lst[16];
static pdw g_lstv;

static pdw dl_put(const pdb *h, pdw nh, const pdb *b, pdw nb)
{
    pdw tot = nh + nb;
    if (g_dlfull || g_dllen + tot + DLH > g_dlcap || tot > 0x7FF0) { g_dlfull = 1; return 0; }
    pd_fcopy_out(h, g_dls, g_dllen, nh);
    if (nb) pd_fcopy_out(b, g_dls, g_dllen + nh, nb);
    g_dllen += tot; g_ndl++;
    return 1;
}
static void hdr_set(pdw type, pdw aux, pdw len, pdi y0, pdi y1)
{
    g_hdr[0] = (pdb)type; g_hdr[1] = (pdb)aux; w16(g_hdr + 2, (pdi)len);
    w16(g_hdr + 4, y0); w16(g_hdr + 6, y1);
}

/* the 4x4 ordered dither (PAGEDESK-SPEC 10.5): lit when level > threshold */
static const pdb bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static void pat_make(pdb *pat, pdw lum, pdw solid)
{
    pdw r, c, lev = (pdw)(((lum << 4) + lum) >> 8), nib;
    if (solid) {
        pdb v = lum >= 128 ? 0xFF : 0x00;
        for (r = 0; r < 4; r++) pat[r] = v;
        return;
    }
    for (r = 0; r < 4; r++) {
        nib = 0;
        for (c = 0; c < 4; c++) nib = (nib << 1) | (lev > bayer[r * 4 + c] ? 1u : 0u);
        pat[r] = (pdb)((nib << 4) | nib);
    }
}

/* the STATE record: clip rectangle, active clips, colour (20.3.2) */
static void emit_state(pdw kind)
{
    pdw lum, c4;
    if (kind == PK_STROKE) { lum = GS.slum; c4 = GS.sc4; }
    else                   { lum = GS.flum; c4 = GS.fc4; }
    w16(g_stl + 0, GS.cx0); w16(g_stl + 2, GS.cy0);
    w16(g_stl + 4, GS.cx1); w16(g_stl + 6, GS.cy1);
    g_stl[8] = GS.ncl; g_stl[9] = (pdb)c4;
    pat_make(g_stl + 10, lum, kind != PK_FILL);
    if (g_lstv && pdr_memeq(g_stl, g_lst, 14)) return;
    hdr_set(DLR_STATE, 0, DLH + 14, 0, 0);
    if (dl_put(g_hdr, DLH, g_stl, 14)) { pdr_cpy(g_lst, g_stl, 14); g_lstv = 1; }
}

/* a rectangle in qpx -> pixel rows/columns by the sample-point rule, thin
 * ones widened to one pixel so a table rule never vanishes (20.3.2) */
static void rect_px(pdi xa, pdi ya, pdi xb, pdi yb, pdi *x0, pdi *y0, pdi *x1, pdi *y1)
{
    *x0 = q_lo(xa); *x1 = q_hi(xb);
    *y0 = q_lo(ya); *y1 = q_hi(yb);
    if (*x1 < *x0 && xb > xa) { *x0 = *x1 = (pdi)(((xa + xb) >> 1) >> 2); }
    if (*y1 < *y0 && yb > ya) { *y0 = *y1 = (pdi)(((ya + yb) >> 1) >> 2); }
}
/* does [a,b] intersect [c,d] */
static pdw span_hit(pdi a, pdi b, pdi c, pdi d) { return b >= c && a <= d; }

static void emit_rect_px(pdi x0, pdi y0, pdi x1, pdi y1, pdw kind)
{
    static pdb body[4];
    if (x0 < GS.cx0) x0 = GS.cx0;
    if (x1 > GS.cx1) x1 = GS.cx1;
    if (y0 < GS.cy0) y0 = GS.cy0;
    if (y1 > GS.cy1) y1 = GS.cy1;
    if (x1 < x0 || y1 < y0) return;
    if (!span_hit(y0, y1, g_gy0, g_gy1) || !span_hit(x0, x1, g_vcx0, g_vcx1)) return;
    if (g_mode != PDR_INTERP) return;
    emit_state(kind);
    w16(body, x0); w16(body + 2, x1);
    hdr_set(DLR_RECT, 0, DLH + 4, y0, y1);
    dl_put(g_hdr, DLH, body, 4);
}
static pdi rp_x0, rp_y0, rp_x1, rp_y1;
static void emit_rect_q(pdi xa, pdi ya, pdi xb, pdi yb, pdw kind)
{
    rect_px(xa, ya, xb, yb, &rp_x0, &rp_y0, &rp_x1, &rp_y1);
    emit_rect_px(rp_x0, rp_y0, rp_x1, rp_y1, kind);
}

/* the staged edges as one FILL record */
static void emit_fill_edges(pdw rule, pdw kind)
{
    static pdb body[6];
    pdw i, ne = g_ne;
    pdi xmin = 32000, xmax = -32000, ymin = 32000, ymax = -32000, v;
    if (!ne || g_mode != PDR_INTERP) { g_ne = 0; return; }
    for (i = 0; i < ne; i++) {
        v = g_ed[i][0]; if (v < xmin) xmin = v; if (v > xmax) xmax = v;
        v = g_ed[i][2]; if (v < xmin) xmin = v; if (v > xmax) xmax = v;
        v = g_ed[i][1]; if (v < ymin) ymin = v; if (v > ymax) ymax = v;
        v = g_ed[i][3]; if (v < ymin) ymin = v; if (v > ymax) ymax = v;
    }
    xmin = q_lo(xmin); xmax = q_hi(xmax);
    ymin = q_lo(ymin); ymax = q_hi(ymax);
    if (xmin < GS.cx0) xmin = GS.cx0;
    if (xmax > GS.cx1) xmax = GS.cx1;
    if (ymin < GS.cy0) ymin = GS.cy0;
    if (ymax > GS.cy1) ymax = GS.cy1;
    g_ne = 0;
    if (xmax < xmin || ymax < ymin) return;
    if (!span_hit(ymin, ymax, g_gy0, g_gy1) || !span_hit(xmin, xmax, g_vcx0, g_vcx1)) return;
    emit_state(kind);
    w16(body, xmin); w16(body + 2, xmax); w16(body + 4, (pdi)ne);
    hdr_set(DLR_FILL, rule, DLH + 6 + ne * 8, ymin, ymax);
    dl_put(g_hdr, DLH, body, 6);
    /* the edges follow the body in the same record: append in place */
    if (!g_dlfull) {
        g_dllen -= 0;
        if (g_dllen + ne * 8 + DLH > g_dlcap) g_dlfull = 1;
        else { pd_fcopy_out(g_ed, g_dls, g_dllen, ne * 8); g_dllen += ne * 8; }
    }
}

/* ---- path painting ------------------------------------------------------- */
/* is the whole path one axis-aligned rectangle? bounds in qpx */
static pdi g_rqx0, g_rqy0, g_rqx1, g_rqy1;
static pdw path_is_rect(void)
{
    pdw n = g_nv;
    if (n == 5 && g_vx[4] == g_vx[0] && g_vy[4] == g_vy[0]) n = 4;
    if (n != 4) return 0;
    {
        pdw k;
        for (k = 1; k < 4; k++) if (g_vf[k] & 1) return 0;
    }
    if (g_nv == 5 && (g_vf[4] & 1)) return 0;
    if (!((g_vx[0] == g_vx[1] && g_vy[1] == g_vy[2] && g_vx[2] == g_vx[3] && g_vy[3] == g_vy[0]) ||
          (g_vy[0] == g_vy[1] && g_vx[1] == g_vx[2] && g_vy[2] == g_vy[3] && g_vx[3] == g_vx[0])))
        return 0;
    g_rqx0 = pdr_min(g_vx[0], g_vx[2]); g_rqx1 = pdr_max(g_vx[0], g_vx[2]);
    g_rqy0 = pdr_min(g_vy[0], g_vy[2]); g_rqy1 = pdr_max(g_vy[0], g_vy[2]);
    return 1;
}

/* build the staged edges from the path, implicitly closing every subpath */
static void path_edges(void)
{
    pdw s, e, i, j;
    g_ne = 0;
    for (s = 0; s < g_nv; s = e) {
        for (e = s + 1; e < g_nv && !(g_vf[e] & 1); e++) ;
        if (e - s < 2) continue;
        for (i = s; i < e; i++) {
            j = (i + 1 == e) ? s : i + 1;
            if (g_ne >= PDR_NEDGE) { emit_fill_edges(g_fillrule, PK_FILL); }
            edge_add(g_vx[i], g_vy[i], g_vx[j], g_vy[j]);
        }
    }
}

static void fill_path(pdw rule)
{
    if (g_nv < 2) return;
    if (path_is_rect()) { emit_rect_q(g_rqx0, g_rqy0, g_rqx1, g_rqy1, PK_FILL); return; }
    g_fillrule = rule;
    path_edges();
    emit_fill_edges(rule, PK_FILL);
}

/* W / W*: intersect the clip with the path */
static pdi g_ce[PDR_NCLIPE][4];
static struct pdr_clip { pdi x0, y0, x1, y1; pdw ne, rule, pad0, pad1; } g_cl[PDR_NCLIP];
static pdw g_cetop;

static void clip_apply(pdw rule)
{
    pdi bx0 = 32000, by0 = 32000, bx1 = -32000, by1 = -32000, v;
    pdw s, e, i, j, n0, ne, ci;
    if (g_nv < 2) { GS.cx1 = GS.cx0 - 1; return; }
    if (path_is_rect()) {
        v = q_lo(g_rqx0); if (v > GS.cx0) GS.cx0 = v;
        v = q_hi(g_rqx1); if (v < GS.cx1) GS.cx1 = v;
        v = q_lo(g_rqy0); if (v > GS.cy0) GS.cy0 = v;
        v = q_hi(g_rqy1); if (v < GS.cy1) GS.cy1 = v;
        return;
    }
    ci = GS.ncl;
    n0 = ci ? g_cl[ci - 1].ne : 0;      /* cumulative edge count of the prefix */
    if (ci >= PDR_NCLIP) { inc_note(PDF_INC_LIMIT, "clip depth"); return; }
    ne = n0;
    for (s = 0; s < g_nv; s = e) {
        for (e = s + 1; e < g_nv && !(g_vf[e] & 1); e++) ;
        if (e - s < 2) continue;
        for (i = s; i < e; i++) {
            j = (i + 1 == e) ? s : i + 1;
            if (g_vy[i] == g_vy[j]) continue;
            if (ne >= PDR_NCLIPE) { inc_note(PDF_INC_LIMIT, "clip path size"); return; }
            g_ce[ne][0] = g_vx[i]; g_ce[ne][1] = g_vy[i];
            g_ce[ne][2] = g_vx[j]; g_ce[ne][3] = g_vy[j];
            ne++;
        }
        for (i = s; i < e; i++) {
            if (g_vx[i] < bx0) bx0 = g_vx[i]; if (g_vx[i] > bx1) bx1 = g_vx[i];
            if (g_vy[i] < by0) by0 = g_vy[i]; if (g_vy[i] > by1) by1 = g_vy[i];
        }
    }
    g_cl[ci].ne = ne;                   /* cumulative end, see above */
    g_cl[ci].rule = rule;
    g_cl[ci].x0 = q_lo(bx0); g_cl[ci].x1 = q_hi(bx1);
    g_cl[ci].y0 = q_lo(by0); g_cl[ci].y1 = q_hi(by1);
    GS.ncl = (pdb)(ci + 1);
    if (g_cl[ci].x0 > GS.cx0) GS.cx0 = g_cl[ci].x0;
    if (g_cl[ci].x1 < GS.cx1) GS.cx1 = g_cl[ci].x1;
    if (g_cl[ci].y0 > GS.cy0) GS.cy0 = g_cl[ci].y0;
    if (g_cl[ci].y1 < GS.cy1) GS.cy1 = g_cl[ci].y1;
}

/* ============================================================================
 * THE BAND RASTERISER
 * ==========================================================================*/
static pdi g_scx0, g_scy0, g_scx1, g_scy1;      /* the walker's current state */
static pdw g_sncl;
static pdb g_spat[4];
static pdb g_sc4;
static pdw g_rmode;                 /* 0 paint, 1 mask A, 2 mask B             */
static pdb g_mk[2][PDR_MAXSTRIDE];                 /* one row each (20.3.2) */
static pdw g_mstride;
static pdw g_mkok;

static void mask_clear(pdw m)
{
    pdr_zero(g_mk[m], g_mstride);
}
static void mask_span(pdw m, pdi xa, pdi xb)
{
    pdb *p = g_mk[m];
    pdi ba = xa >> 3, bb = xb >> 3, i;
    pdb ma = (pdb)(0xFF >> (xa & 7)), mb = (pdb)(0xFF << (7 - (xb & 7)));
    if (ba == bb) { p[ba] |= (pdb)(ma & mb); return; }
    p[ba] |= ma;
    for (i = ba + 1; i < bb; i++) p[i] = 0xFF;
    p[bb] |= mb;
}

/* paint one span of pixels (absolute x) on absolute row y */
static void span_out(pdi y, pdi xa, pdi xb)
{
    pdi row;
    if (xa < g_scx0) xa = g_scx0;
    if (xb > g_scx1) xb = g_scx1;
    if (xa < g_bx0) xa = g_bx0;
    if (xb > g_bx0 + g_bw - 1) xb = (pdi)(g_bx0 + g_bw - 1);
    if (xb < xa || y < g_scy0 || y > g_scy1) return;
    if (y < g_wlo || y > g_whi) return;
    row = (pdi)(y - g_by0);
    xa = (pdi)(xa - g_bx0); xb = (pdi)(xb - g_bx0);
    if (g_rmode) { mask_span(g_rmode - 1, xa, xb); return; }
    if (g_bpp == 1) pdr_fill1(g_bseg, (pdw)row * g_bstride, xa, xb, g_spat[y & 3]);
    else            pdr_fill4(g_bseg, (pdw)row * g_bstride, xa, xb, g_sc4);
}

/* scan-convert edges e[0..ne) over pixel rows y0..y1 (absolute) */
static pdw g_act[PDR_NEDGE];
static pdi g_cx[PDR_NSPAN];
static pdb g_cd[PDR_NSPAN];
static void rast_edges(const pdi *e, pdw ne, pdw rule, pdi y0, pdi y1)
{
    pdw i, na = 0, nc, k, j;
    pdi y, ys, ya, yb, xa, xb, x, wind, xl, p0, p1;
    pdb dir;
    pdi qlo = (pdi)(y0 * 4), qhi = (pdi)(y1 * 4 + 3);
    for (i = 0; i < ne; i++) {
        ya = e[i * 4 + 1]; yb = e[i * 4 + 3];
        if (ya < yb ? (yb < qlo || ya > qhi) : (ya < qlo || yb > qhi)) continue;
        g_act[na++] = i;
    }
    if (!na) return;
    for (y = y0; y <= y1; y++) {
        ys = (pdi)(y * 4 + 2);
        nc = 0;
        for (i = 0; i < na; i++) {
            k = g_act[i] * 4;
            ya = e[k + 1]; yb = e[k + 3];
            if (ya < yb) { if (ys < ya || ys >= yb) continue; dir = 1; }
            else         { if (ys < yb || ys >= ya) continue; dir = 0; }
            xa = e[k]; xb = e[k + 2];
            x = (pdi)(xa + pdr_muldiv((pdi)(xb - xa), (pdi)(ys - ya), (pdi)(yb - ya)));
            if (nc >= PDR_NSPAN) break;
            j = nc++;
            while (j > 0 && g_cx[j - 1] > x) { g_cx[j] = g_cx[j - 1]; g_cd[j] = g_cd[j - 1]; j--; }
            g_cx[j] = x; g_cd[j] = dir;
        }
        wind = 0; xl = 0;
        for (j = 0; j < nc; j++) {
            if (rule) {                                  /* even-odd           */
                if (!(j & 1)) { xl = g_cx[j]; continue; }
            } else {                                     /* nonzero            */
                pdi was = wind;
                wind = (pdi)(wind + (g_cd[j] ? 1 : -1));
                if (was == 0) { xl = g_cx[j]; continue; }
                if (wind != 0) continue;
            }
            p0 = q_lo(xl); p1 = q_hi(g_cx[j]);
            if (p1 >= p0) span_out(y, p0, p1);
        }
    }
}

/* A shape under complex clips is drawn ONE SCAN LINE at a time: its spans go
 * into mask 0, each clip polygon's into mask 1 and is ANDed in, and what is
 * left is painted as runs. Rare (a rectangle clip never comes here), so the
 * per-row rescan of the edges is the price of two 96-byte masks instead of two
 * band-sized ones. */
static void mask_paint(pdi y)
{
    pdi x, w = g_bw, xs;
    pdw m, b;
    pdb *p = g_mk[0];
    x = 0;
    while (x < w) {
        b = (pdw)(x >> 3);
        if (p[b] == 0 && !(x & 7)) { x += 8; continue; }
        if (p[b] & (0x80 >> (x & 7))) {
            xs = x;
            while (x < w && (p[x >> 3] & (0x80 >> (x & 7)))) x++;
            m = g_rmode; g_rmode = 0;
            span_out(y, (pdi)(g_bx0 + xs), (pdi)(g_bx0 + x - 1));
            g_rmode = m;
        } else x++;
    }
}
static void clips_and(pdi y)
{
    pdw ci, ne0 = 0, ne1, b;
    for (ci = 0; ci < g_sncl; ci++) {
        ne1 = g_cl[ci].ne;
        mask_clear(1);
        g_rmode = 2;
        rast_edges(&g_ce[ne0][0], ne1 - ne0, (pdw)g_cl[ci].rule, y, y);
        g_rmode = 1;
        for (b = 0; b < g_mstride; b++) g_mk[0][b] &= g_mk[1][b];
        ne0 = ne1;
    }
}
/* paint one row of a shape under the active clips: `kind` 0 a rectangle
 * [x0, x1], 1 the edges staged in g_ed */
static void shape_clipped(pdi y, pdw kind, pdi x0, pdi x1, pdw ne, pdw rule)
{
    mask_clear(0);
    g_rmode = 1;
    if (kind == 0) span_out(y, x0, x1);
    else rast_edges(&g_ed[0][0], ne, rule, y, y);
    clips_and(y);
    g_rmode = 0;
    mask_paint(y);
}

/* ---- glyph drawing ------------------------------------------------------- */
static pdb g_srec[16];
static pdw g_sseg, g_soff;          /* the strike, as seg:off (off < 16)      */
static pdw g_sfirst, g_slast, g_sasc, g_sdesc, g_ssize, g_sflags;

/* far address of a 32-bit bank offset */
static void far_of(pdw base, pdw lo, pdw hi, pdw *seg, pdw *off)
{
    *seg = (pdw)(base + (hi << 12) + (lo >> 4));
    *off = lo & 15;
}
static pdw strike_load(pdw idx)
{
    pdw lo, hi;
    pd_fcopy_in(g_bankseg, 8 + idx * 16, g_srec, 16);
    g_ssize = g_srec[2]; g_sasc = g_srec[3]; g_sdesc = g_srec[4];
    g_sfirst = g_srec[5]; g_slast = g_srec[6]; g_sflags = g_srec[7];
    lo = (pdw)(g_srec[8] | (g_srec[9] << 8)); hi = (pdw)(g_srec[10] | (g_srec[11] << 8));
    far_of(g_bankseg, lo, hi, &g_sseg, &g_soff);
    return 1;
}
/* pick the strike for (family, style) at the largest size <= em; returns
 * the index or 0xFFFF. the smallest strike serves when every one is larger */
static pdw strike_pick(pdw fam, pdw sty, pdw em, pdw *synth)
{
    pdw i, best = 0xFFFF, bsz = 0, small = 0xFFFF, ssz = 0xFFFF, sz, f, s;
    *synth = 0;
    if (!g_bankok) return 0xFFFF;
    if (fam == g_pkfam && sty == g_pksty && em == g_pkem) return g_pkres;
    for (i = 0; i < g_nstrikes; i++) {
        f = pd_fpeekb(g_bankseg, 8 + i * 16);
        s = pd_fpeekb(g_bankseg, 8 + i * 16 + 1);
        if (f != fam || s != sty) continue;
        sz = pd_fpeekb(g_bankseg, 8 + i * 16 + 2);
        if (sz <= em && sz >= bsz) { best = i; bsz = sz; }
        if (sz < ssz) { small = i; ssz = sz; }
    }
    if (best == 0xFFFF) best = small;
    if (best == 0xFFFF && (sty & 2)) {              /* no italic: upright + shear */
        best = strike_pick(fam, sty & 1, em, synth);
        *synth = 1;
    }
    g_pkfam = fam; g_pksty = sty; g_pkem = em; g_pkres = best;
    return best;
}

static pdb g_gl[4];
static pdw g_synth;                 /* strike_pick: italic by shear           */
/* one glyph through a slow, fully clipped path (partial glyphs) */
static void glyph_slow(pdi gx, pdi top, pdw wpx, pdw wb, pdw gseg, pdw goff, pdw nrows, pdw shear)
{
    pdw r, c;
    pdi yy, xx, sh;
    pdb v;
    for (r = 0; r < nrows; r++) {
        yy = (pdi)(top + r);
        if (yy < g_scy0 || yy > g_scy1) continue;
        sh = 0;
        if (shear) sh = (pdi)(((pdi)g_sasc - (pdi)r + 8) >> 2) - 2;
        for (c = 0; c < wpx; c++) {
            v = pd_fpeekb(gseg, (pdw)(goff + r * wb + (c >> 3)));
            if (!(v & (0x80 >> (c & 7)))) continue;
            xx = (pdi)(gx + c + sh);
            if (xx < g_scx0 || xx > g_scx1) continue;
            span_out(yy, xx, xx);
        }
    }
}

/* ---- rotated glyph cache (20.3.4): a direct-mapped table in the glyph claim,
 * keyed by (strike, rotation, code); slot = 8 header bytes + up to 4 x 32 ---- */
#define GCSLOT   144
#define GCBASE   1024
static pdw g_gcn, g_gcoff, g_gcw, g_gcrows;
static pdi g_gcdx, g_gcdy;
/* make (or find) the rotated bitmap of the current glyph; sets g_gcoff etc. */
static pdw glyph_rotated(pdw sidx, pdw rk, pdw code, pdw wpx, pdw wb, pdw boff, pdw rows, pdw bear)
{
    pdw slot, base, j, i, w2, r2, wb2, r, c;
    pdb v;
    if (!g_gseg || g_gkb < 4) return 0;
    if (!g_gcn) g_gcn = (pdw)(((g_gkb * 1024u) - GCBASE) / GCSLOT);
    if (!g_gcn) return 0;
    slot = (pdw)((code + sidx * 7 + rk * 3) % g_gcn);
    base = GCBASE + slot * GCSLOT;
    if (rk == 2) { w2 = wpx; r2 = rows; } else { w2 = rows; r2 = wpx; }
    wb2 = (w2 + 7) >> 3;
    if (wb2 > 4 || r2 > 32) return 0;
    g_gcoff = base + 8; g_gcw = w2; g_gcrows = r2;
    if (rk == 1) { g_gcdx = (pdi)(-(pdi)g_sdesc); g_gcdy = (pdi)bear; }
    else if (rk == 3) { g_gcdx = (pdi)(-(pdi)g_sasc); g_gcdy = (pdi)(-(pdi)bear - (pdi)wpx); }
    else { g_gcdx = (pdi)(-(pdi)bear - (pdi)wpx); g_gcdy = (pdi)(-(pdi)g_sdesc); }
    if (pd_fpeekb(g_gseg, base) == (pdb)(code) && pd_fpeekb(g_gseg, base + 1) == (pdb)(sidx + 1) &&
        pd_fpeekb(g_gseg, base + 2) == (pdb)rk) return 1;
    pd_fpokeb(g_gseg, base, (pdb)code); pd_fpokeb(g_gseg, base + 1, (pdb)(sidx + 1)); pd_fpokeb(g_gseg, base + 2, (pdb)rk);
    pd_ffill(g_gseg, base + 8, 0, wb2 * r2);
    for (j = 0; j < r2; j++) {
        for (i = 0; i < w2; i++) {
            if (rk == 1)      { r = rows - 1 - i; c = j; }
            else if (rk == 3) { r = i; c = wpx - 1 - j; }
            else              { r = rows - 1 - j; c = wpx - 1 - i; }
            v = pd_fpeekb(g_sseg, (pdw)(boff + r * wb + (c >> 3)));
            if (v & (0x80 >> (c & 7)))
                pd_fpokeb(g_gseg, (pdw)(base + 8 + j * wb2 + (i >> 3)),
                          (pdb)(pd_fpeekb(g_gseg, (pdw)(base + 8 + j * wb2 + (i >> 3))) | (0x80 >> (i & 7))));
        }
    }
    return 1;
}

static pdb g_gbuf[PDR_NRUN * 5];
static void draw_glyphs(pdw off, pdw len, pdi ry0, pdi ry1)
{
    pdw fam, sty, px, shear, n, i, idx, code, wb, wpx, boff, rows, skip, nrow, rk;
    pdw gseg, goff, gw, grows;
    pdi x, y, bear, top, first, last, gx, mn, mx;
    static pdb hd[6];
    pdw cnt;
    pd_fcopy_in(g_dls, off + DLH, hd, 6);
    fam = hd[0]; sty = hd[1]; px = hd[2]; rk = hd[3]; n = (pdw)r16(hd + 4);
    idx = strike_pick(fam, sty, px, &g_synth);
    if (idx == 0xFFFF) return;
    strike_load(idx);
    if (g_synth || (g_sflags & 1)) shear = 1; else shear = 0;
    if (rk) shear = 0;
    rows = g_sasc + g_sdesc;
    off += DLH + 6;
    while (n) {
        cnt = n > PDR_NRUN ? PDR_NRUN : n;
        pd_fcopy_in(g_dls, off, g_gbuf, cnt * 5);
        off += cnt * 5; n -= cnt;
        for (i = 0; i < cnt; i++) {
            code = g_gbuf[i * 5];
            x = r16(g_gbuf + i * 5 + 1); y = r16(g_gbuf + i * 5 + 3);
            if (code < g_sfirst || code > g_slast) continue;
            pd_fcopy_in(g_sseg, (pdw)(g_soff + (code - g_sfirst) * 4), g_gl, 4);
            wpx = g_gl[0]; bear = (signed char)g_gl[1];
            if (!wpx) continue;
            wb = (wpx + 7) >> 3;
            boff = (pdw)(g_soff + ((g_slast - g_sfirst + 1) * 4) + (pdw)(g_gl[2] | (g_gl[3] << 8)));
            gseg = g_sseg; goff = boff; gw = wpx; grows = rows;
            if (!rk) { gx = (pdi)(x + bear); top = (pdi)(y - (pdi)g_sasc); }
            else {
                if (!glyph_rotated(idx, rk, code, wpx, wb, boff, rows, (pdw)bear)) continue;
                gseg = g_gseg; goff = g_gcoff; gw = g_gcw; grows = g_gcrows; wb = (gw + 7) >> 3;
                gx = (pdi)(x + g_gcdx); top = (pdi)(y + g_gcdy);
            }
            first = top; if (first < g_wlo) first = g_wlo;
            if (first < g_scy0) first = g_scy0;
            last = (pdi)(top + grows - 1);
            if (last > g_whi) last = g_whi;
            if (last > g_scy1) last = g_scy1;
            if (last < first) continue;
            if (gx > g_scx1 || (pdi)(gx + gw + 6) < g_scx0) continue;
            skip = (pdw)(first - top); nrow = (pdw)(last - first + 1);
            mn = g_bx0; if (g_scx0 > mn) mn = g_scx0;
            mx = (pdi)(g_bx0 + g_bw - 1); if (g_scx1 < mx) mx = g_scx1;
            if ((pdi)(gx - 2) < mn || (pdi)(gx + gw + 6) > mx) {
                glyph_slow(gx, top, gw, wb, gseg, goff, grows, shear);
                continue;
            }
            pdr_gp.bseg = g_bseg;
            pdr_gp.boff = (pdw)(first - g_by0) * g_bstride;
            pdr_gp.stride = g_bstride;
            pdr_gp.rowstep = g_bstride;
            pdr_gp.nrow = nrow; pdr_gp.skip = skip;
            pdr_gp.gseg = gseg; pdr_gp.goff = (pdw)(goff + skip * wb);
            pdr_gp.wbytes = wb; pdr_gp.wpx = gw;
            if (g_bpp == 1) pdr_gp.mode = (g_spat[0] & 1) ? 1 : 0;
            else { pdr_gp.mode = 2; pdr_gp.col = g_sc4; }
            pdr_gp.shear = shear ? g_sasc + 1 : 0;
            if (shear) pdr_gp.x = (pdi)(gx - g_bx0 - 2);
            else       pdr_gp.x = (pdi)(gx - g_bx0);
            pdr_glyph();
        }
    }
    (void)ry0; (void)ry1;
}

/* ---- images (pdri.c supplies the row source) ------------------------------ */
static void draw_image(pdw off, pdw len, pdi ry0, pdi ry1);

/* ---- the DL walker for one band ------------------------------------------ */
static void walk_band(pdi ry0, pdi ry1)
{
    pdw off = 0, type, len;
    pdi y0, y1, a, yy0, yy1, x0, x1;
    static pdb pl[6];
    pdw ne;
    g_scx0 = g_vcx0; g_scy0 = g_vcy0; g_scx1 = g_vcx1; g_scy1 = g_vcy1;
    g_sncl = 0; g_sc4 = 0; pat_make(g_spat, 0, 1);
    while (off < g_dllen) {
        pd_fcopy_in(g_dls, off, g_hdr, DLH);
        type = g_hdr[0]; len = (pdw)r16(g_hdr + 2);
        if (len < DLH) break;
        y0 = r16(g_hdr + 4); y1 = r16(g_hdr + 6);
        if (type == DLR_STATE) {
            pd_fcopy_in(g_dls, off + DLH, g_stl, 14);
            g_scx0 = r16(g_stl); g_scy0 = r16(g_stl + 2);
            g_scx1 = r16(g_stl + 4); g_scy1 = r16(g_stl + 6);
            g_sncl = g_stl[8]; g_sc4 = g_stl[9];
            pdr_cpy(g_spat, g_stl + 10, 4);
        } else if (y1 >= g_wlo && y0 <= g_whi) {
            yy0 = y0 < g_wlo ? g_wlo : y0;
            yy1 = y1 > g_whi ? g_whi : y1;
            if (type == DLR_RECT) {
                pd_fcopy_in(g_dls, off + DLH, pl, 4);
                x0 = r16(pl); x1 = r16(pl + 2);
                if (g_sncl && g_mkok) { for (a = yy0; a <= yy1; a++) shape_clipped(a, 0, x0, x1, 0, 0); }
                else for (a = yy0; a <= yy1; a++) span_out(a, x0, x1);
            } else if (type == DLR_FILL) {
                pd_fcopy_in(g_dls, off + DLH, pl, 6);
                ne = (pdw)r16(pl + 4);
                if (ne > PDR_NEDGE) ne = PDR_NEDGE;
                pd_fcopy_in(g_dls, off + DLH + 6, g_ed, ne * 8);
                if (g_sncl && g_mkok) { for (a = yy0; a <= yy1; a++) shape_clipped(a, 1, 0, 0, ne, g_hdr[1]); }
                else rast_edges(&g_ed[0][0], ne, g_hdr[1], yy0, yy1);
            } else if (type == DLR_GLYPHS) {
                draw_glyphs(off, len, yy0, yy1);
            } else if (type == DLR_IMAGE) {
                draw_image(off, len, yy0, yy1);
            }
        }
        off += len;
    }
}

/* ---- one band: compose, then lock / clip / blit / unlock ----------------- */
static pdw g_winoff;
static pdw g_thumb;                 /* drawing into a thumbnail cell          */
static pdw g_tdseg, g_tdoff, g_tstride; /* the cell                           */

static pdw band_geometry(void)
{
    g_bx0 = (pdi)(g_vcx0 & ~7);
    g_bw = (pdi)(((g_vcx1 | 7) + 1) - g_bx0);
    g_bstride = g_bpp == 1 ? (pdw)(g_bw >> 3) : (pdw)(g_bw >> 1);
    if (g_bstride * 16 > g_bbytes) return 0;
    g_mstride = (pdw)(g_bw >> 3);
    g_mkok = g_mstride <= PDR_MAXSTRIDE;
    return 1;
}

static pdw band_count(void)
{
    pdi h = (pdi)(g_vcy1 - g_vcy0 + 1);
    return (pdw)((h + PD_BANDROWS - 1) / PD_BANDROWS);
}

static void band_init(void)
{
    pdi r, y, xa, xb;
    pdw rowoff;
    for (r = 0; r < (pdi)g_brows; r++) {
        rowoff = (pdw)r * g_bstride;
        y = (pdi)(g_by0 + r);
        if (g_deskwhite) pd_ffill(g_bseg, rowoff, g_bpp == 1 ? 0xFF : 0xFF, g_bstride);
        else if (g_bpp == 1) pd_ffill(g_bseg, rowoff, (y & 1) ? 0x55 : 0xAA, g_bstride);
        else pd_ffill(g_bseg, rowoff, 0x77, g_bstride);
        if (y >= g_pgy0 && y <= g_pgy1) {
            xa = g_pgx0 < g_bx0 ? g_bx0 : g_pgx0;
            xb = g_pgx1 > (pdi)(g_bx0 + g_bw - 1) ? (pdi)(g_bx0 + g_bw - 1) : g_pgx1;
            if (xb >= xa) {
                if (g_bpp == 1) pdr_fill1(g_bseg, rowoff, (pdi)(xa - g_bx0), (pdi)(xb - g_bx0), 0xFF);
                else            pdr_fill4(g_bseg, rowoff, (pdi)(xa - g_bx0), (pdi)(xb - g_bx0), 15);
            }
        }
    }
}

static void band_present(void)
{
    if (g_thumb) {
        /* a thumbnail cell: copy the rows, left-aligned (the cell is the page) */
        pdi r;
        pdw cw = g_tstride;
        for (r = 0; r < (pdi)g_brows; r++) {
            pdi y = (pdi)(g_by0 + r - g_vcy0);
            pd_fcopy(g_tdseg, (pdw)(g_tdoff + (pdw)y * cw), g_bseg,
                     (pdw)r * g_bstride + (pdw)((g_vcx0 - g_bx0) >> 3), cw);
        }
        return;
    }
    os88_gfx_lock();
    if (os88_wm_clip_set((void *)(unsigned)g_winoff) == 0)
        pdr_blit(g_bpp == 1 ? 1 : 4, g_bseg, g_bstride, g_bx0, g_by0, (pdw)g_bw, g_brows);
    os88_gfx_unlock();
}

/* geometry of band k (init / walk / present are separate so that one band can
 * be composed in several sub-passes when even a single band overflows the DL) */
static void band_open(pdw k)
{
    g_by0 = (pdi)(g_vcy0 + (pdi)k * PD_BANDROWS);
    g_brows = (pdw)(g_vcy1 - g_by0 + 1);
    if (g_brows > PD_BANDROWS) g_brows = PD_BANDROWS;
    g_wlo = g_by0; g_whi = (pdi)(g_by0 + g_brows - 1);
}
static void band_draw(pdw k)
{
    band_open(k);
    band_init();
    walk_band(0, (pdi)(g_brows - 1));
    band_present();
}
