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

/* room for `n` more bytes and the END reserve? (wrap-safe: g_dllen <= g_dlcap) */
static pdw dl_room(pdw n)
{
    return n <= 0x7FF0 && g_dlcap - g_dllen >= DLH && n <= g_dlcap - g_dllen - DLH;
}
static pdw dl_put(const pdb *h, pdw nh, const pdb *b, pdw nb)
{
    pdw tot = nh + nb;
    if (g_dlfull || !dl_room(tot)) { g_dlfull = 1; return 0; }
#ifdef PDR_X8
    if (h[0] >= DLR_RECT && h[0] <= DLR_IMAGE) xb_mark(r16(h + 4), r16(h + 6));   /* 20.22.10.3 */
#endif
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
        pdb v = lum >= 240 ? 0xFF : 0x00;   /* 20.19: only near-white ink is paper; a coloured or grey stroke is black */
        for (r = 0; r < 4; r++) pat[r] = v;
        return;
    }
    for (r = 0; r < 4; r++) {
        nib = 0;
        for (c = 0; c < 4; c++) nib = (nib << 1) | (lev > bayer[r * 4 + c] ? 1u : 0u);
        pat[r] = (pdb)((nib << 4) | nib);
    }
}

/* 20.19.2: a 4bpp GREY fill between two of the palette's four greys
 * (0, 8, 7, 15 = luminance 0, 85, 170, 255) is a checkerboard of the pair
 * that brackets it - seven levels, not four, and 0.85 grey (217) is the
 * 7/15 mix instead of white. Fills only; the state's colour becomes the
 * pair's first and g_stl[10] its second + 1 (g_spat is unused at 4bpp). */
static void grey_mix(pdw lum, pdw c4)
{
    pdb a, b;
    if (c4 != 0 && c4 != 7 && c4 != 8 && c4 != 15) return;
    if (lum >= 21 && lum < 64)        { a = 0; b = 8; }
    else if (lum >= 106 && lum < 149) { a = 8; b = 7; }
    else if (lum >= 191 && lum < 234) { a = 7; b = 15; }
    else return;
    g_stl[9] = a; g_stl[10] = (pdb)(b + 1);
}

/* the STATE record: clip rectangle, active clips, colour (20.3.2) */
static void emit_state(pdw kind)
{
    pdw lum, c4;
#ifdef PDR_X8
    xb_state(kind);                     /* DLR_XSTATE, when the transparency changed (20.22.10.3) */
#endif
    if (kind == PK_STROKE) { lum = GS.slum; c4 = GS.sc4; }
    else                   { lum = GS.flum; c4 = GS.fc4; }
    w16(g_stl + 0, GS.cx0); w16(g_stl + 2, GS.cy0);
    w16(g_stl + 4, GS.cx1); w16(g_stl + 6, GS.cy1);
    g_stl[8] = GS.clid; g_stl[9] = (pdb)c4;
    pat_make(g_stl + 10, lum, kind != PK_FILL);
    if (g_bpp != 1) {                   /* 20.19.2: spat[0] = the checker's other colour + 1 */
        g_stl[10] = 0;
        if (kind == PK_FILL)
            grey_mix(lum, c4);
    }
#ifdef PDR_X8
    g_stl[14] = (pdb)lum;               /* the colour's luminosity rides in aux (20.22.10.3) */
    if (g_lstv && pdr_memeq(g_stl, g_lst, 15)) return;
    hdr_set(DLR_STATE, lum, DLH + 14, 0, 0);
    if (dl_put(g_hdr, DLH, g_stl, 14)) { pdr_cpy(g_lst, g_stl, 15); g_lstv = 1; }
#else
    if (g_lstv && pdr_memeq(g_stl, g_lst, 14)) return;
    hdr_set(DLR_STATE, 0, DLH + 14, 0, 0);
    if (dl_put(g_hdr, DLH, g_stl, 14)) { pdr_cpy(g_lst, g_stl, 14); g_lstv = 1; }
#endif
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
        pd_fcopy_in(g_ws, (pdw)(WS_ED + i * 8), g_etmp, 8);
        v = g_etmp[0]; if (v < xmin) xmin = v; if (v > xmax) xmax = v;
        v = g_etmp[2]; if (v < xmin) xmin = v; if (v > xmax) xmax = v;
        v = g_etmp[1]; if (v < ymin) ymin = v; if (v > ymax) ymax = v;
        v = g_etmp[3]; if (v < ymin) ymin = v; if (v > ymax) ymax = v;
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
    /* the WHOLE record (header, body, edges) is reserved before any of it is
     * written: a list that fills up holds only complete records, so the walker
     * of a "draws what fits" pass (D4) never reads edges that are not there */
    if (g_dlfull || !dl_room(DLH + 6 + ne * 8)) { g_dlfull = 1; return; }
    w16(body, xmin); w16(body + 2, xmax); w16(body + 4, (pdi)ne);
    hdr_set(DLR_FILL, rule, DLH + 6 + ne * 8, ymin, ymax);
    dl_put(g_hdr, DLH, body, 6);
    /* the edges follow the body in the same record: append in place */
    pdr_fcopy(g_dls, g_dllen, g_ws, WS_ED, ne * 8);
    g_dllen += ne * 8;
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

/* W / W*: intersect the clip with the path. A polygon clip goes in a POOL that
 * lives for the pass (8 polygons, 96 edges): an entry is never rewritten while
 * the pass's list can name it, because the list is walked after the whole pass
 * is interpreted - a slot reused after a Q would cut the earlier records by the
 * later polygon. The state names the innermost entry (`clid`, 0xFF none) and
 * each entry its parent, so q/Q nesting is a chain, not a prefix. */
static struct pdr_clip { pdi x0, y0, x1, y1; pdw e0, ne, rule, parent; } g_cl[PDR_NCLIP];
static pdw g_cetop, g_ncl;          /* edges and entries used this pass (pass_begin) */

/* rule: bit 0 even-odd; bit 1 (rect_clip_user) a form's /BBox, see below */
static void clip_apply(pdw rule)
{
    pdi bx0 = 32000, by0 = 32000, bx1 = -32000, by1 = -32000, v, g = (pdi)(rule >> 1);
    pdw s, e, i, j, ne, ci, ok;
    if (g_nv < 2) { GS.cx1 = GS.cx0 - 1; return; }
    if (path_is_rect()) {
        /* OUTWARD (20.3.2): every pixel the rectangle touches, PDF's own rule for a clip. A
         * thin shape is widened to one pixel whose centre can lie outside a clip its centre
         * line is inside - a 1 pt stroke half a point inside a form's BBox, which is how
         * PageDesk saves a mark: the sample-point clip dropped two edges of a saved /Square.
         * A shape LARGER than the clip can gain the touched pixel too (up to one a side).
         * A BBox's low sides also give back the rounding to qpx (g = 1): the stroke and the
         * edge can round to the same qpx, a pixel boundary, and the stroke's pixel is then
         * the one below it (25-33% on VGA, the y axis of Hercules and CGA below ~80/120%).
         * A rectangle with no area clips everything, as the sample-point rule did. */
        if (g_rqx0 == g_rqx1 || g_rqy0 == g_rqy1) { GS.cx1 = GS.cx0 - 1; return; }
        v = (pdi)((g_rqx0 - g) >> 2); if (v > GS.cx0) GS.cx0 = v;
        v = (pdi)(((g_rqx1 + 3) >> 2) - 1); if (v < GS.cx1) GS.cx1 = v;
        v = (pdi)((g_rqy0 - g) >> 2); if (v > GS.cy0) GS.cy0 = v;
        v = (pdi)(((g_rqy1 + 3) >> 2) - 1); if (v < GS.cy1) GS.cy1 = v;
        return;
    }
    ok = g_ncl < PDR_NCLIP;
    if (!ok) inc_note(PDF_INC_LIMIT, "clip depth");
    ne = g_cetop;
    for (s = 0; s < g_nv; s = e) {
        for (e = s + 1; e < g_nv && !(g_vf[e] & 1); e++) ;
        if (e - s < 2) continue;
        for (i = s; i < e; i++) {
            j = (i + 1 == e) ? s : i + 1;
            if (g_vx[i] < bx0) bx0 = g_vx[i]; if (g_vx[i] > bx1) bx1 = g_vx[i];
            if (g_vy[i] < by0) by0 = g_vy[i]; if (g_vy[i] > by1) by1 = g_vy[i];
            if (!ok || g_vy[i] == g_vy[j]) continue;
            if (ne >= PDR_NCLIPE) { inc_note(PDF_INC_LIMIT, "clip path size"); ok = 0; continue; }
            g_etmp[0] = g_vx[i]; g_etmp[1] = g_vy[i];
            g_etmp[2] = g_vx[j]; g_etmp[3] = g_vy[j];
            pd_fcopy_out(g_etmp, g_ws, (pdw)(WS_CE + ne * 8), 8);
            ne++;
        }
    }
    /* the bounding box always cuts (a polygon the pool cannot hold is its box) */
    v = q_lo(bx0); if (v > GS.cx0) GS.cx0 = v;
    v = q_hi(bx1); if (v < GS.cx1) GS.cx1 = v;
    v = q_lo(by0); if (v > GS.cy0) GS.cy0 = v;
    v = q_hi(by1); if (v < GS.cy1) GS.cy1 = v;
    if (!ok) return;
    ci = g_ncl++;
    g_cl[ci].e0 = g_cetop; g_cl[ci].ne = ne - g_cetop; g_cetop = ne;
    g_cl[ci].rule = rule & 1; g_cl[ci].parent = GS.clid;
    GS.clid = (pdb)ci;
}

/* ============================================================================
 * THE BAND RASTERISER
 * ==========================================================================*/
static pdi g_scx0, g_scy0, g_scx1, g_scy1;      /* the walker's current state */
static pdw g_sclid;               /* the walker's innermost polygon clip, 0xFF none */
static pdb g_spat[4];
static pdb g_sc4;
static pdw g_rmode;                 /* 0 paint, 1 mask A, 2 mask B             */
/* two mask rows of PDR_MAXSTRIDE (20.3.2), lent by the path (pdrg.c, 20.22.7) */
#define g_mk ((pdb (*)[PDR_MAXSTRIDE])g_vy)
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

/* 20.19.2: a span of the checker g_sc4 / g_spat[0]-1 (band-relative x; the
 * band starts on a multiple of 8, so x parity is absolute parity). The ROW
 * parity is the page's, not the screen's: a scroll moves the pixels by dy and
 * renders only the strip it exposes, so a screen-parity checker would flip
 * phase at that seam after every odd scroll (verify-polish) */
static void fill4_mix(pdw rowoff, pdi xa, pdi xb, pdi y)
{
    pdw ca = g_sc4, cb = (pdw)(g_spat[0] - 1), ce, co;
    if ((y - g_pgy0) & 1) { ce = cb; co = ca; } else { ce = ca; co = cb; }
    if (xa & 1) { pdr_fill4(g_bseg, rowoff, xa, xa, co); xa++; }
    if (xb >= xa && !(xb & 1)) { pdr_fill4(g_bseg, rowoff, xb, xb, ce); xb--; }
    if (xb > xa) pd_ffill(g_bseg, (pdw)(rowoff + (xa >> 1)), (pdb)((ce << 4) | co), (pdw)((xb - xa + 1) >> 1));
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
#ifdef PDR_X8
    if (g_xs) { x8_span(y, xa, xb); return; }                                    /* the 8-bit strip */
#endif
    if (g_bpp == 1) pdr_fill1(g_bseg, (pdw)row * g_bstride, xa, xb, g_spat[y & 3]);
    else if (g_spat[0]) fill4_mix((pdw)row * g_bstride, xa, xb, y);
    else            pdr_fill4(g_bseg, (pdw)row * g_bstride, xa, xb, g_sc4);
}

/* scan-convert edges e[0..ne) over pixel rows y0..y1 (absolute) */
static pdw g_fe;                    /* offset of the current FILL record's edges in dl_seg */
/* lent by the interpreter's arrays (pdrg.c, PAGEDESK-SPEC 20.22.7's room) */
#define g_act ((pdw *)g_str)                     /* PDR_NEDGE entries          */
#define g_cx  g_vx                               /* PDR_NSPAN crossings        */
#define g_cd  ((pdb *)(g_vx + PDR_NSPAN))        /* PDR_NSPAN directions       */
static void rast_edges(pdw eseg, pdw eoff, pdw ne, pdw rule, pdi y0, pdi y1)
{
    pdw na, nc, j;
    pdi y, wind, xl, p0, p1;
    pdi qlo = (pdi)(y0 * 4), qhi = (pdi)(y1 * 4 + 3);
    na = pdr_act(eseg, eoff, ne, qlo, qhi, g_act);
    if (!na) return;
    for (y = y0; y <= y1; y++) {
        nc = pdr_scan(eseg, eoff, g_act, na, (pdi)(y * 4 + 2), g_cx, g_cd, PDR_NSPAN);
        if (nc >= PDR_NSPAN) { inc_note(PDF_INC_LIMIT, "crossings per row"); continue; }
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
    pdw ci, b;
    for (ci = g_sclid; ci < PDR_NCLIP; ci = g_cl[ci].parent) {     /* the chain ends at 0xFF */
        mask_clear(1);
        g_rmode = 2;
        rast_edges(g_ws, (pdw)(WS_CE + g_cl[ci].e0 * 8), g_cl[ci].ne, (pdw)g_cl[ci].rule, y, y);
        g_rmode = 1;
        for (b = 0; b < g_mstride; b++) g_mk[0][b] &= g_mk[1][b];
    }
}
/* paint one row of a shape under the active clips: `kind` 0 a rectangle
 * [x0, x1], 1 the edges staged in g_ed */
static void shape_clipped(pdi y, pdw kind, pdi x0, pdi x1, pdw ne, pdw rule)
{
    mask_clear(0);
    g_rmode = 1;
    if (kind == 0) span_out(y, x0, x1);
    else rast_edges(g_dls, g_fe, ne, rule, y, y);
    clips_and(y);
    g_rmode = 0;
    mask_paint(y);
}

/* ---- glyph drawing ------------------------------------------------------- */
static pdb g_srec[16];
static pdw g_sseg, g_soff;          /* the loaded strike: seg:off, off = 0     */
static pdw g_sfirst, g_slast, g_sasc, g_sdesc, g_ssize, g_sflags, g_sid;

/* ---- the strike cache (PAGEDESK-SPEC 20.3.4, DECISIONS D3) -----------------
 * The glyph claim is carved: [0,1024) the image row buffer, [1024,3072) the
 * INDEX of PDFONTX.RES, [3072,5120) the transformed-glyph cache (14 slots) and
 * [SC_BASE, claim end) the STRIKE CACHE: strikes are read from the package or
 * PDFONTX.RES on first use. Entries are bump-allocated 512-aligned; when the
 * region is full everything is dropped and the one wanted is read again. */
#define GC_IDX    1024
#define GC_XBASE  3072
#define GC_NSLOT  14
#define SC_BASE   5120
#define SC_NENT   8
static pdb g_scb[SC_NENT];
static pdw g_scolo[SC_NENT], g_scohi[SC_NENT], g_scoff[SC_NENT], g_sclen[SC_NENT];
static pdw g_scn, g_scbump;
static pdw g_pclus, g_pvol, g_poff_lo, g_poff_hi, g_fxok;
static char g_pkgname[14];
static unsigned g_cur8[8];
static struct os88_place g_savpl;
static pdw g_fioerr;

/* Stand in the package's folder (the primary gave its place, D3) and answer
 * the cluster size of the volume it is on. PDENG reads the document BY NAME in
 * the working folder, and the shell stands back in the document's folder only
 * BETWEEN wakes - so every strike read is bracketed here, inside the verb:
 * file_leave puts back the place file_enter found (os88_file_here, a word, no
 * I/O), before PDENG's next read in the same slice. 0 = could not, and the
 * folder is put back on that path too */
static void file_leave(void) { os88_file_goto_q_mark(g_savpl.clus, g_savpl.vol); }
static pdw file_enter(void)
{
    pdw cb;
    os88_file_here(&g_savpl);
    if (os88_file_goto_q_mark(g_pclus, g_pvol)) { file_leave(); return 0; }
    cb = (pdw)os88_disk_cluster_sectors();
    cb = (pdw)(cb << 9);
    if (cb < 512 || cb > 4096 || (cb & (cb - 1))) { file_leave(); return 0; }
    return cb;
}
/* `cap` bytes (a cluster multiple) from byte (ahi:alo) (a cluster multiple) */
static pdw file_read(const char *name, pdw alo, pdw ahi, pdw seg, pdw cap)
{
    pdr_zero(g_cur8, sizeof g_cur8);
    g_cur8[6] = alo; g_cur8[7] = ahi;
    return os88_file_read_seq(name, seg, 0, cap, g_cur8);
}

/* ---- the synthetic strikes (PAGEDESK-SPEC 20.22.8): embedded fonts. The TOP of the glyph
 * claim holds up to SYN_MAX blocks of PDFB_SIZE (pdabi.h's [fonts] block), one a (font, size);
 * PDXF fills them (PDV_FNT_GLYPH with PDFG_BLOCK), the walker reads them like any strike
 * (bank 2). (glyph_kb - 24) / 4 blocks: a 24 KiB claim has none and embedded fonts are not
 * drawn from their programs. A block is reused only when it was not used in THIS pass: the
 * display list being built refers to the ones it used. ------------------------------------ */
#define SYN_MAX PDFB_NBLK
static pdw syn_cap(void)
{
    pdw n = (pdw)((g_gkb - 24) >> 2);
    return g_gkb < 28 ? 0 : n > SYN_MAX ? SYN_MAX : n;
}
static pdw syn_hdr(void) { return (pdw)(((g_gkb << 10) - syn_cap() * PDFB_SIZE) - PDFB_HDR); }     /* PDXF keeps the header (PDFB_HDR) */
static pdw syn_off(pdw i) { return (pdw)(syn_hdr() + PDFB_HDR + i * PDFB_SIZE); }
/* the block of (font, px), or 0xFFFF: PDXF makes it (PDV_FNT_GLYPH with PDFG_BLOCK) */
static pdw syn_find(pdw obj, pdw gen, pdw px)
{
    pdw h = syn_hdr(), i, e;
    if (!syn_cap() || !g_gseg || pd_fpeekw(g_gseg, (pdw)(h + 62)) != PDFB_MAGIC) return 0xFFFF;
    for (i = pd_fpeekw(g_gseg, (pdw)(h + 60)); i--; ) {
        e = (pdw)(h + (i << 3));
        if (pd_fpeekw(g_gseg, e) == obj && pd_fpeekw(g_gseg, (pdw)(e + 2)) == gen && pd_fpeekw(g_gseg, (pdw)(e + 4)) == px) {
            pd_fcopy_out(&g_pstamp, g_gseg, (pdw)(e + 6), 2);   /* used in this pass: not to be reused (PDREND has no pd_fpokew) */
            return i;
        }
    }
    return 0xFFFF;
}

/* a text pass (px 0): the next block from `i` up of (font, ANY size) the page view has drawn, or
 * 0xFFFF. Its slots carry each code's Latin byte, so Find and Copy need no font module for the
 * codes the page drew (QA R17: under the overlay, a 640 KB machine cannot load PDXF) */
static pdw syn_next(pdw obj, pdw gen, pdw i)
{
    pdw h = syn_hdr(), e;
    if (!syn_cap() || !g_gseg || pd_fpeekw(g_gseg, (pdw)(h + 62)) != PDFB_MAGIC) return 0xFFFF;
    for (; i < pd_fpeekw(g_gseg, (pdw)(h + 60)); i++) {
        e = (pdw)(h + (i << 3));
        if (pd_fpeekw(g_gseg, e) == obj && pd_fpeekw(g_gseg, (pdw)(e + 2)) == gen && pd_fpeekw(g_gseg, (pdw)(e + 4))) return i;
    }
    return 0xFFFF;
}

/* the glyph claim was REGROWN for the strike blocks (PDMEM_GLYPH, 20.22.8): the page view goes on in
 * the claim at its new place and size. The bank's PDFONTX.RES index lives in it, so its segment moves
 * with it; the strike cache keeps its entries unless the blocks' header now covers them */
static void syn_adopt(pdw seg, pdw kb)
{
    pdw og = g_gseg;
    g_gseg = seg; g_gkb = kb > 63 ? 63 : kb;
    if (!g_gseg) g_gkb = 0;
    if (g_bk_seg[1]) g_bk_seg[1] = (pdw)(g_bk_seg[1] + (g_gseg - og));
    if (g_scbump > syn_hdr()) { g_scn = 0; g_scbump = SC_BASE; }
}

/* PAGEDESK-SPEC 20.22.14: `plen` != 0 is a PACKED strike, an LZ4 stream of plen
 * bytes that expands to len. It is read into its own slot at rr = ceil16(len - plen
 * - head) (0 when negative), so the stream starts at or above len - plen from the
 * slot's base (SPEC.md 20.13.7: the decoder then never overtakes its source), and
 * OSAPI_DECOMP expands it onto the base; the slot's room is the larger of the two */
static pdw strike_fetch(pdw b, pdw olo, pdw ohi, pdw len, pdw plen)
{
    static struct pd_u32 u, p;
    pdw cb, head, cap, room, seg, got, i, rr, need, n;
    room = syn_cap() ? syn_hdr() : (pdw)(g_gkb << 10);         /* g_gkb <= 63 (INIT); the synthetic blocks sit above */
    if (!g_gseg || room <= SC_BASE + 512u || !len || len > 0xE000u || plen >= len || g_fioerr) return 0;   /* cap below cannot wrap; a failed read ends the page view's (QA F21) */
    pdm_u32_set(&u, ohi, olo);
    if (b == 0) { pdm_u32_set(&p, g_poff_hi, g_poff_lo); pdm_u32_add(&u, &u, &p); }
    cb = file_enter();
    if (!cb) { g_fioerr = 1; return 0; }
    head = u.lo & (cb - 1);
    n = plen ? plen : len;                      /* the bytes on disk */
    cap = (pdw)((head + n + cb - 1) & ~(cb - 1));   /* n < 60K and cb <= 4K: no wrap */
    rr = 0;
    if (plen && len - plen > head) rr = (pdw)((len - plen - head + 15) & ~15u);
    need = (pdw)(rr + cap);                     /* <= len + 14 + cb: no wrap */
    if (need < len) need = len;
    /* every comparison by subtraction: g_scbump <= room, so none of them wraps */
    if (need > room - g_scbump || g_scn >= SC_NENT) { g_scn = 0; g_scbump = SC_BASE; }
    if (need > room - g_scbump) { file_leave(); return 0; }      /* a strike larger than the cache */
    seg = (pdw)(g_gseg + (g_scbump >> 4));
    got = file_read(b ? "PDFONTX.RES" : g_pkgname, (pdw)(u.lo & ~(cb - 1)), u.hi, (pdw)(seg + (rr >> 4)), cap);
    file_leave();
    if (got < head + n) { g_fioerr = 1; return 0; }
    if (plen) {
        if (pdr_lzdec((pdw)(seg + (rr >> 4)), head, plen, seg, len)) { g_fioerr = 1; return 0; }
    } else
        pd_fcopy(seg, 0, seg, head, len);      /* the strike down to the paragraph */
    i = g_scn++;
    g_scb[i] = (pdb)b; g_scolo[i] = olo; g_scohi[i] = ohi;
    g_scoff[i] = g_scbump; g_sclen[i] = len;
    g_scbump = (pdw)(g_scbump + ((len + 511) & ~511u));
    return 1;
}

/* make strike `id` (bank << 8 | index) resident; sets g_sseg/g_soff and the
 * metrics. 0 = unavailable (bank absent, read failed, larger than the cache) */
static pdw strike_load(pdw id)
{
    pdw b = id >> 8, idx = id & 255, i, lo, hi, len;
    if (b == 2) {                                   /* a synthetic strike: (font, size) block idx */
        if (idx >= pd_fpeekw(g_gseg, (pdw)(syn_hdr() + 60))) return 0;
        g_sseg = (pdw)(g_gseg + (syn_off(idx) >> 4)); g_soff = 0;
        g_ssize = pd_fpeekw(g_gseg, (pdw)(syn_hdr() + (idx << 3) + 4)); g_sasc = g_ssize; g_sdesc = (pdw)((g_ssize >> 2) + 1);
        g_sfirst = 1; g_slast = 128; g_sflags = 0; g_sid = id;
        return 1;
    }
    if (b > 1 || !g_bk_seg[b] || idx >= g_bk_n[b]) return 0;
    pd_fcopy_in(g_bk_seg[b], 8 + idx * 16, g_srec, 16);
    g_ssize = g_srec[2]; g_sasc = g_srec[3]; g_sdesc = g_srec[4];
    g_sfirst = g_srec[5]; g_slast = g_srec[6]; g_sflags = g_srec[7];
    lo = (pdw)(g_srec[8] | (g_srec[9] << 8)); hi = (pdw)(g_srec[10] | (g_srec[11] << 8));
    len = (pdw)(g_srec[12] | (g_srec[13] << 8));
    g_sid = id;
    for (i = 0; i < g_scn; i++)
        if (g_scb[i] == b && g_scolo[i] == lo && g_scohi[i] == hi) goto found;
    if (!strike_fetch(b, lo, hi, len, (pdw)(g_srec[14] | (g_srec[15] << 8)))) return 0;     /* 20.22.14: packed */
    i = g_scn - 1;
found:
    g_sseg = (pdw)(g_gseg + (g_scoff[i] >> 4)); g_soff = 0;
    return 1;
}

/* pick the strike for (family, style) at the largest size <= em, over both
 * banks; the smallest serves when every one is larger. Text larger than the
 * largest strike is a strike of em/n or less SCALED n times (n <= PDR_MAXSC),
 * when that comes out at least a quarter larger than the crisp strike: with
 * only the core sizes (8, 10, 12) that is D5's doubling - 16 from 8, 20 from
 * 10, 24 from 12 - and above 24 it is a tripled 12 and so on, never a glyph
 * a fraction of its size. g_pkscale says n. Returns an id or 0xFFFF */
#define PDR_MAXSC 12
static pdw g_pksz;                  /* pick_in: the size of what it found        */
static pdw pick_in(pdw fam, pdw sty, pdw em, pdw maxb)
{
    pdw b, i, best = 0xFFFF, bsz = 0, small = 0xFFFF, ssz = 0xFFFF, sz, f, s;
    for (b = 0; b <= maxb; b++) {
        if (!g_bk_seg[b]) continue;
        for (i = 0; i < g_bk_n[b]; i++) {
            f = pd_fpeekb(g_bk_seg[b], 8 + i * 16);
            s = pd_fpeekb(g_bk_seg[b], 8 + i * 16 + 1);
            if (f != fam || s != sty) continue;
            sz = pd_fpeekb(g_bk_seg[b], 8 + i * 16 + 2);
            if (sz <= em && sz >= bsz) { best = (pdw)((b << 8) | i); bsz = sz; }
            if (sz < ssz) { small = (pdw)((b << 8) | i); ssz = sz; }
        }
    }
    if (best != 0xFFFF) { g_pksz = bsz; return best; }
    g_pksz = ssz | 0x8000;                      /* bit 15: larger than asked */
    return small;
}
static pdw strike_pick(pdw fam, pdw sty, pdw em, pdw *synth)
{
    pdw id, id2, n, v, bestv;
    *synth = 0;
    if (!g_bankok) return 0xFFFF;
    if (fam == g_pkfam && sty == g_pksty && em == g_pkem) { *synth = 0; return g_pkres; }
    g_pkscale = 1;
    id = pick_in(fam, sty, em, 1);
    if (id != 0xFFFF && em >= 16 && !(g_pksz & 0x8000)) {
        bestv = g_pksz + (g_pksz >> 2);          /* a scaled strike must beat the crisp one by a quarter */
        for (n = 2; n <= PDR_MAXSC && em / n >= 6; n++) {
            id2 = pick_in(fam, sty, em / n, 1);
            if (id2 == 0xFFFF || (g_pksz & 0x8000)) continue;
            v = g_pksz * n;
            if (v > bestv) { id = id2; bestv = v; g_pkscale = n; }
        }
    }
    g_pkfam = fam; g_pksty = sty; g_pkem = em; g_pkres = id;
    return id;
}

static pdb g_gl[4];
static pdw g_synth;                 /* strike_pick: italic by shear           */
/* one glyph through a slow, fully clipped path (partial glyphs) */
#ifdef PDR_X8   /* runs of ink, not pixels: the 8-bit strip pays a span's setup each */
static void glyph_slow(pdi gx, pdi top, pdw wpx, pdw wb, pdw gseg, pdw goff, pdw nrows, pdw shear)
{
    pdw r, c, ink;
    pdi yy, xa, xb, sh, rs;
    for (r = 0; r < nrows; r++) {
        yy = (pdi)(top + r);
        if (yy < g_scy0 || yy > g_scy1) continue;
        sh = 0;
        if (shear) sh = (pdi)(((pdi)g_sasc - (pdi)r + 8) >> 2) - 2;
        rs = -1;
        for (c = 0; c <= wpx; c++) {            /* one span a run of ink pixels */
            ink = c < wpx && (pd_fpeekb(gseg, (pdw)(goff + r * wb + (c >> 3))) & (0x80 >> (c & 7)));
            if (ink) { if (rs < 0) rs = (pdi)c; continue; }
            if (rs < 0) continue;
            xa = (pdi)(gx + rs + sh); xb = (pdi)(gx + (pdi)c - 1 + sh); rs = -1;
            if (xa < g_scx0) xa = g_scx0;
            if (xb > g_scx1) xb = g_scx1;
            if (xb >= xa) span_out(yy, xa, xb);
        }
    }
}
#else
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
#endif

/* ---- the transformed-glyph cache (20.3.4): a direct-mapped table of 14
 * slots in the glyph claim, keyed by (strike, rotation, scale, code); a slot
 * is 8 header bytes + up to 4 bytes x 32 rows. A glyph that does not fit a
 * slot (a large scale, a rotated 24 px strike) is drawn by glyph_big. ---- */
#define GCSLOT   144
static pdw g_gcoff, g_gcw, g_gcrows;
static pdi g_gcdx, g_gcdy;
/* the transformed glyph's geometry: size and top-left offset from the pen */
static void glyph_geom(pdw rk, pdw sc, pdw wpx, pdw rows, pdw bear)
{
    pdw W = sc * wpx, R = sc * rows;
    pdi Bs = (pdi)((pdi)sc * (pdi)(signed char)bear), As = (pdi)(sc * g_sasc), Ds = (pdi)(sc * g_sdesc);
    if (rk == 0 || rk == 2) { g_gcw = W; g_gcrows = R; } else { g_gcw = R; g_gcrows = W; }
    if (rk == 0)      { g_gcdx = Bs;                 g_gcdy = (pdi)(-As); }
    else if (rk == 1) { g_gcdx = (pdi)(-Ds);         g_gcdy = Bs; }
    else if (rk == 3) { g_gcdx = (pdi)(-As);         g_gcdy = (pdi)(-Bs - (pdi)W); }
    else              { g_gcdx = (pdi)(-Bs - (pdi)W); g_gcdy = (pdi)(-Ds); }
}
/* make (or find) the rotated and/or scaled bitmap of the current glyph in a
 * slot (glyph_geom first); 0 = it does not fit one, or there is no claim */
static pdw glyph_xform(pdw sidx, pdw rk, pdw sc, pdw code, pdw wpx, pdw wb, pdw boff, pdw rows)
{
    pdw slot, base, j, i, W, R, w2, r2, wb2, r, c, tag;
    pdb v;
    w2 = g_gcw; r2 = g_gcrows; W = sc * wpx; R = sc * rows;
    wb2 = (w2 + 7) >> 3;
    if (!g_gseg || g_gkb < 5 || wb2 > 4 || r2 > 32) return 0;
    slot = (pdw)((code + sidx * 7 + rk * 3 + sc) % GC_NSLOT);
    base = GC_XBASE + slot * GCSLOT;
    g_gcoff = base + 8;
    tag = rk | (sc << 2);
    if (pd_fpeekb(g_gseg, base) == (pdb)code && pd_fpeekb(g_gseg, base + 1) == (pdb)(sidx + 1) &&
        pd_fpeekb(g_gseg, base + 2) == (pdb)tag && pd_fpeekb(g_gseg, base + 3) == (pdb)(g_sid >> 8) + 1) return 1;
    pd_fpokeb(g_gseg, base, (pdb)code); pd_fpokeb(g_gseg, base + 1, (pdb)(sidx + 1));
    pd_fpokeb(g_gseg, base + 2, (pdb)tag); pd_fpokeb(g_gseg, base + 3, (pdb)((g_sid >> 8) + 1));
    pd_ffill(g_gseg, base + 8, 0, wb2 * r2);
    for (j = 0; j < r2; j++) {
        for (i = 0; i < w2; i++) {
            if (rk == 0)      { r = j; c = i; }
            else if (rk == 1) { r = R - 1 - i; c = j; }
            else if (rk == 3) { r = i; c = W - 1 - j; }
            else              { r = R - 1 - j; c = W - 1 - i; }
            if (sc == 2) { r >>= 1; c >>= 1; }
            else if (sc > 2) { r /= sc; c /= sc; }
            v = pd_fpeekb(g_sseg, (pdw)(boff + r * wb + (c >> 3)));
            if (v & (0x80 >> (c & 7)))
                pd_fpokeb(g_gseg, (pdw)(base + 8 + j * wb2 + (i >> 3)),
                          (pdb)(pd_fpeekb(g_gseg, (pdw)(base + 8 + j * wb2 + (i >> 3))) | (0x80 >> (i & 7))));
        }
    }
    return 1;
}
/* a transformed glyph no slot can hold: every ink pixel of the strike is an
 * sc x sc block, put where glyph_xform's mapping puts it, through span_out
 * (which clips) - slow, and only for text far larger than any strike */
static void glyph_big(pdi x, pdi y, pdw rk, pdw sc, pdw wpx, pdw wb, pdw boff, pdw rows)
{
    pdw r, c, t, W = sc * wpx, R = sc * rows;
    pdi i0, j0, bx, by;
    for (r = 0; r < rows; r++) {
        for (c = 0; c < wpx; c++) {
            if (!(pd_fpeekb(g_sseg, (pdw)(boff + r * wb + (c >> 3))) & (0x80 >> (c & 7)))) continue;
            if (rk == 0)      { i0 = (pdi)(c * sc);           j0 = (pdi)(r * sc); }
            else if (rk == 1) { i0 = (pdi)(R - sc * (r + 1)); j0 = (pdi)(c * sc); }
            else if (rk == 3) { i0 = (pdi)(r * sc);           j0 = (pdi)(W - sc * (c + 1)); }
            else              { i0 = (pdi)(W - sc * (c + 1)); j0 = (pdi)(R - sc * (r + 1)); }
            bx = (pdi)(x + g_gcdx + i0); by = (pdi)(y + g_gcdy + j0);
            for (t = 0; t < sc; t++) span_out((pdi)(by + t), bx, (pdi)(bx + sc - 1));
        }
    }
}
/* floor(v / 4) for either sign */
static pdi pdr_fl4(pdi v) { return v >= 0 ? (pdi)(v >> 2) : (pdi)-((3 - v) >> 2); }

/* the glyph-run buffer: the interpreter builds a run in it, the walker reads one back; the two
 * never overlap (interpretation of a group is over before its bands are drawn), so one serves */
static pdb g_rbody[8 + PDR_NRUN * 5];
#define g_gbuf g_rbody
static void draw_glyphs(pdw off, pdw len, pdi ry0, pdi ry1)
{
    pdw shear, n, i, idx, code, wb, swb, wpx, boff, rows, skip, nrow, rk, sc, big;
    pdw gseg, goff, gw, grows;
    pdi x, y, bear, top, first, last, gx, mn, mx, shl, shr;
    static pdb hd[8];
    pdw cnt;
    pd_fcopy_in(g_dls, off + DLH, hd, 8);
    rk = hd[3] & 3; sc = (pdw)(((hd[3] >> 2) & 15) + 1); n = (pdw)r16(hd + 4);
    idx = (pdw)r16(hd + 6);                    /* the run's strike id (bank << 8 | index) */
    if (idx == 0xFFFF || !strike_load(idx)) return;
    if (g_sflags & 1) shear = 1; else shear = 0;
    if (rk || sc > 1) shear = 0;
    rows = g_sasc + g_sdesc;
    /* the synthesised slant moves row r by floor((ascent - r) / 4): its extent */
    shl = shear ? pdr_fl4((pdi)(1 - (pdi)g_sdesc)) : 0;
    shr = shear ? pdr_fl4((pdi)g_sasc) : 0;
    mn = g_bx0; if (g_scx0 > mn) mn = g_scx0;
    mx = (pdi)(g_bx0 + g_bw - 1); if (g_scx1 < mx) mx = g_scx1;
    off += DLH + 8;
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
            swb = wb = (wpx + 7) >> 3;
            boff = (pdw)(g_soff + ((g_slast - g_sfirst + 1) * 4) + (pdw)(g_gl[2] | (g_gl[3] << 8)));
            gseg = g_sseg; goff = boff; gw = wpx; grows = rows; big = 0;
            if (!rk && sc == 1) { gx = (pdi)(x + bear); top = (pdi)(y - (pdi)g_sasc); }
            else {
                glyph_geom(rk, sc, wpx, rows, (pdw)bear);
                big = !glyph_xform(idx, rk, sc, code, wpx, wb, boff, rows);
                gw = g_gcw; grows = g_gcrows;
                if (!big) { gseg = g_gseg; goff = g_gcoff; wb = (gw + 7) >> 3; }
                gx = (pdi)(x + g_gcdx); top = (pdi)(y + g_gcdy);
            }
            first = top; if (first < g_wlo) first = g_wlo;
            if (first < g_scy0) first = g_scy0;
            last = (pdi)(top + grows - 1);
            if (last > g_whi) last = g_whi;
            if (last > g_scy1) last = g_scy1;
            if (last < first) continue;
            if ((pdi)(gx + shl) > g_scx1 || (pdi)(gx + (pdi)gw - 1 + shr) < g_scx0) continue;
            if (big) { glyph_big(x, y, rk, sc, wpx, swb, boff, rows); continue; }
            skip = (pdw)(first - top); nrow = (pdw)(last - first + 1);
            /* the composer writes the bytes under [gx + shl, gx + gw - 1 + shr]
             * and reads at most 4 bitmap bytes a row: anything else is clipped
             * pixel by pixel */
#ifdef PDR_X8
            if (g_xs) { glyph_slow(gx, top, gw, wb, gseg, goff, grows, shear); continue; }
#endif
            if (wb > 4 || (pdi)(gx + shl) < mn || (pdi)(gx + (pdi)gw - 1 + shr) > mx) {
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
    (void)ry0; (void)ry1; (void)len;
}

/* ---- images (pdri.c supplies the row source) ------------------------------ */
static void draw_image(pdw off, pdw len, pdi ry0, pdi ry1);

#ifdef PDR_X8
/* ---- the DL walker for one band: pdrx8.c (it grew the transparency records) ----- */
static void walk_band(pdi ry0, pdi ry1);
#else
/* ---- the DL walker for one band ------------------------------------------ */
static void walk_band(pdi ry0, pdi ry1)
{
    pdw off = 0, type, len;
    pdi y0, y1, a, yy0, yy1, x0, x1;
    static pdb pl[6];
    pdw ne;
    g_scx0 = g_vcx0; g_scy0 = g_vcy0; g_scx1 = g_vcx1; g_scy1 = g_vcy1;
    g_sclid = 0xFF; g_sc4 = 0; pat_make(g_spat, 0, 1);
    while (off < g_dllen) {
        pd_fcopy_in(g_dls, off, g_hdr, DLH);
        type = g_hdr[0]; len = (pdw)r16(g_hdr + 2);
        if (len < DLH || len > g_dllen - off) break;     /* only whole records */
        y0 = r16(g_hdr + 4); y1 = r16(g_hdr + 6);
        if (type == DLR_STATE) {
            pd_fcopy_in(g_dls, off + DLH, g_stl, 14);
            g_scx0 = r16(g_stl); g_scy0 = r16(g_stl + 2);
            g_scx1 = r16(g_stl + 4); g_scy1 = r16(g_stl + 6);
            g_sclid = g_stl[8]; g_sc4 = g_stl[9];
            pdr_cpy(g_spat, g_stl + 10, 4);
        } else if (y1 >= g_wlo && y0 <= g_whi) {
            yy0 = y0 < g_wlo ? g_wlo : y0;
            yy1 = y1 > g_whi ? g_whi : y1;
            if (type == DLR_RECT) {
                pd_fcopy_in(g_dls, off + DLH, pl, 4);
                x0 = r16(pl); x1 = r16(pl + 2);
                if (g_sclid < PDR_NCLIP && g_mkok) { for (a = yy0; a <= yy1; a++) shape_clipped(a, 0, x0, x1, 0, 0); }
                else for (a = yy0; a <= yy1; a++) span_out(a, x0, x1);
            } else if (type == DLR_FILL) {
                pd_fcopy_in(g_dls, off + DLH, pl, 6);
                ne = (pdw)r16(pl + 4);
                if (ne > PDR_NEDGE) ne = PDR_NEDGE;
                g_fe = (pdw)(off + DLH + 6);       /* the edges are scanned in the list itself */
                if (g_sclid < PDR_NCLIP && g_mkok) { for (a = yy0; a <= yy1; a++) shape_clipped(a, 1, 0, 0, ne, g_hdr[1]); }
                else rast_edges(g_dls, g_fe, ne, g_hdr[1], yy0, yy1);
            } else if (type == DLR_GLYPHS) {
                draw_glyphs(off, len, yy0, yy1);
            } else if (type == DLR_IMAGE) {
                draw_image(off, len, yy0, yy1);
            }
        }
        off += len;
    }
}
#endif

/* ---- one band: compose, then lock / clip / blit / unlock ----------------- */
static pdw g_winoff;
static pdi g_ydone;                 /* 20.19.1: the first row not yet blitted */
static pdw g_thumb;                 /* drawing into a thumbnail cell          */
static pdw g_tdseg, g_tdoff, g_tstride; /* the cell                           */

static pdw band_geometry(void)
{
    g_bx0 = (pdi)(g_vcx0 & ~7);
    g_bw = (pdi)(((g_vcx1 | 7) + 1) - g_bx0);
    if (g_bw <= 0 || g_bw > 2048) return 0;
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
        else pd_ffill(g_bseg, rowoff, 0x88, g_bstride);   /* the shell's desk, 8 */
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
    g_ydone = (pdi)(g_by0 + g_brows);   /* 20.19.1: ABORT's CX=1 answer */
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
