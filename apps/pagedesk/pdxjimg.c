/* ---- pdxjimg.c: the image verbs - plan, tile decode, rows (20.22.13.2-.7) ------- */

static pdw g_trow_y0, g_trow_y1, g_nexttr, g_lasty;
static pdw g_sth;                       /* strip rows (the tallest tile's)       */
static pdb g_gtab[4][128];                 /* depth < 8: sample -> 0..255          */
static pdi g_ga[4]; static pdw g_gs[4], g_gmax[4];
static pdb *g_gt[4];

static pdw a2(pdw v) { return (pdw)((v + 1u) & 0xFFFEu); }

/* the memory plan for reduction r (20.22.13.3): 0 = fits */
static pdw plan(pdw r)
{
    pdw pw, ph, ow, e, t, nd = g_nd;
    g_r = r; g_rtop = g_nl - r;
    g_persist = g_nlay > 1;
    ow = cdiv(g_xs, r);
    if (ow * (g_mode == PDIM_N8 ? nd : 1) > 1024) return PDJ_R_MEM;
    tile_geom(0, 0);
    pw = g_rx1[g_rtop] - g_rx0[g_rtop]; ph = g_ry1[g_rtop] - g_ry0[g_rtop];
    if (pw > JX_MAXDIM || ph > JX_MAXDIM || !pw || !ph) return PDJ_R_MEM;
    if (pw > 32767u / ph) return PDJ_R_MEM;
    if (tile_layout()) return g_why ? g_why : PDJ_R_LIMIT;
    if (w_statesz + 1u < w_statesz || w_nrec > 10000) return PDJ_R_MEM;
    w_state = 0;
    w_rec = a2(w_statesz);
    if (w_nrec > (0xFFF0u - w_rec) / 6) return PDJ_R_MEM;
    w_plane = a2(w_rec + w_nrec * 6);
    e = w_plane + 2u * pw * ph;
    if (e < w_plane) return PDJ_R_MEM;
    g_usestrip = (g_ntx > 1 || nd > 1);
    g_sth = ph;
    w_strip = a2(e);
    if (g_usestrip) {
        if (ow > 65535u / ph / nd) return PDJ_R_MEM;
        t = ow * ph * nd;
        e = w_strip + t;
        if (e < w_strip) return PDJ_R_MEM;
    } else e = w_strip;
    w_arena = a2(e);
    if (w_arena < e || w_arena >= g_wsend || g_wsend - w_arena < JX_ARENA_MIN) return PDJ_R_MEM;
    return 0;
}

/* sample parameters of component c for xj_grey and xj_deq */
static pdw comp_f(pdw c) { pdw d = g_dep[c]; return g_rev ? 0 : (d >= 11 ? 0 : (d <= 8 ? 3 : 11 - d)); }
static void grey_params(pdw k, pdw c)
{
    pdw d = g_dep[c], f = comp_f(c), s, i, mx;
    s = f + (d > 8 ? d - 8 : 0);
    g_gs[k] = s;
    g_ga[k] = (pdi)((1u << (d - 1)) << f) + (s ? (pdi)(1u << (s - 1)) : 0);
    g_gmax[k] = d >= 8 ? 255 : (1u << d) - 1;
    g_gt[k] = 0;
    if (d < 8) {
        mx = g_gmax[k];
        for (i = 0; i <= mx; i++) g_gtab[k][i] = (pdb)((i * 255u + mx / 2) / mx);
        g_gt[k] = g_gtab[k];
    }
}

/* decode every kept code-block of component c into the plane */
static void decode_comp(pdw c)
{
    pdw res, p, np, b, nbands, i, j, e, rec0, ro, head, npass, zbp, mb, P, len, seg, l;
    pdw cx, cy, x0, y0, x1, y1, w, h, px, py, bi, eps, rb, f, m, sh, rnd, yy, d;
    d = g_dep[c]; f = comp_f(c);
    pd_ffill(g_ws, w_plane, 0, (pdw)(2u * (g_rx1[g_rtop] - g_rx0[g_rtop]) * (g_ry1[g_rtop] - g_ry0[g_rtop])));
    rowmap_id();
    for (res = 0; res <= g_rtop; res++) {
        np = g_npx[res] * g_npy[res];
        nbands = res ? 3 : 1;
        for (p = 0; p < np; p++) {
            e = g_pstart[c * JX_MAXRES + res] + p;
            rec0 = g_ptab[2 * e + 1];
            blk_geom(res, p);
            for (b = 0; b < nbands; b++) {
                bi = res ? 3 * res - 2 + b : 0;
                eps = g_qe[bi];
                mb = g_grd + eps - 1;
                for (j = 0; j < g_pbs[b].ncy; j++)
                    for (i = 0; i < g_pbs[b].ncx; i++) {
                        ro = w_rec + (rec0 + g_bcb[b] + j * g_pbs[b].ncx + i) * 6;
                        head = pd_fpeekw(g_ws, ro);
                        npass = pd_fpeekb(g_ws, ro + 4) & 0x7F;
                        zbp = pd_fpeekb(g_ws, ro + 5);
                        if (!head || !npass || zbp >= mb || mb - zbp > 15) continue;
                        P = mb - zbp;
                        len = 0; seg = head;
                        while (seg && len < JX_CBDATA) {
                            l = pd_fpeekw(g_ws, seg + 2);
                            if (l > JX_CBDATA - len) l = JX_CBDATA - len;
                            pd_fcopy_in(g_ws, seg + 4, t1_cb + len, l);
                            len += l; seg = pd_fpeekw(g_ws, seg);
                        }
                        cx = g_pbs[b].gx0 + i; cy = g_pbs[b].gy0 + j;
                        x0 = cx << g_pbs[b].cw; if (x0 < g_pbs[b].bx0) x0 = g_pbs[b].bx0;
                        y0 = cy << g_pbs[b].ch; if (y0 < g_pbs[b].by0) y0 = g_pbs[b].by0;
                        x1 = (cx + 1) << g_pbs[b].cw; if (x1 > g_pbs[b].bx1) x1 = g_pbs[b].bx1;
                        y1 = (cy + 1) << g_pbs[b].ch; if (y1 > g_pbs[b].by1) y1 = g_pbs[b].by1;
                        w = x1 - x0; h = y1 - y0;
                        t1_block(w, h, g_pbs[b].orient, P, npass, len, g_sty);
                        px = g_pbs[b].ox + x0 - g_pbs[b].tbx0;
                        py = g_pbs[b].oy + y0 - g_pbs[b].tby0;
                        if (g_rev) { m = 1; sh = 1; rnd = 0; }
                        else {
                            rb = d + (g_pbs[b].orient == 0 ? 0 : (g_pbs[b].orient == 3 ? 2 : 1));
                            m = 2048 + g_qm[bi];
                            sh = 12 + eps; rb += f;      /* sh = 12 + eps - Rb - F, floored at 0 */
                            sh = sh > rb ? sh - rb : 0;
                            rnd = sh ? 1u << (sh - 1) : 0;
                        }
                        for (yy = 0; yy < h; yy++)
                            xj_deq(t1_m + yy * w, t1_f + (yy + 1) * (w + 2) + 1, g_ws,
                                   prow(py + yy) + 2 * px, w, m, sh, rnd);
                    }
            }
        }
        if (res) idwt_stage(res);
    }
}

/* read and decode tile (tx, ty) of tile-row ty; the plane or the strip gets it */
static pdw decode_tile(pdw tx, pdw ty)
{
    pdw s, ci, pw, ph, y, xoff, sbase;
    tile_geom(tx, ty);
    pw = g_rx1[g_rtop] - g_rx0[g_rtop]; ph = g_ry1[g_rtop] - g_ry0[g_rtop];
    g_pstride = 2 * pw;
    if (tile_layout()) return PDE_FILTER;
    if (g_persist) pd_ffill(g_ws, w_state, 0, w_statesz);
    pd_ffill(g_ws, w_rec, 0, (pdw)(w_nrec * 6u));
    g_arenatop = w_arena; g_arenafull = 0;
    s = tile_packets();
    if (s == 2) return PDE_FILTER;
    for (ci = 0; ci < g_nd; ci++) {
        decode_comp(g_dc[ci]);
        if (g_usestrip) {
            grey_params(ci, g_dc[ci]);
            xoff = cdiv(g_tx0, g_r);
            sbase = w_strip + ci * (g_ow * g_sth);
            for (y = 0; y < ph; y++)
                xj_grey(g_ws, prow(y), g_ws, sbase + y * g_ow + xoff, pw, g_ga[ci], g_gs[ci],
                        g_gmax[ci], g_gt[ci], (g_nd == 1 && g_mode == PDIM_GREY8) ? g_inv : 0);
        }
    }
    return PDE_OK;
}

/* decode tile-row g_nexttr: every tile that touches it */
static pdw decode_trow(void)
{
    pdw tx, t, ty = g_nexttr, m, err, y1;
    if (g_usestrip) pd_ffill(g_ws, w_strip, 0x80, (pdw)(g_ow * g_sth * g_nd));
    else pd_ffill(g_ws, w_plane, 0, (pdw)(2u * g_ow * g_sth));
    tile_geom(0, ty);
    for (tx = 0; tx < g_ntx; tx++) {
        t = ty * g_ntx + tx;
        if (!g_pend && !g_eoc) {
            m = rd16();
            if (g_trunc || m == 0xFFD9 || m != 0xFF90 || sot_read()) g_eoc = 1;
        }
        if (g_pend && g_pt < t) { g_why = PDJ_R_ORDER; return PDE_FILTER; }
        if (g_pend && g_pt == t) {
            g_cur = t;
            err = tp_begin();
            if (err == 2) return PDE_FILTER;
            if (!err) {
                err = decode_tile(tx, ty);
                if (err) return err;
                tile_finish();
            }
        } else if (!g_usestrip) {
            tile_geom(tx, ty);                  /* a missing tile: the cleared plane */
            g_pstride = 2 * (g_rx1[g_rtop] - g_rx0[g_rtop]);
            rowmap_id();
        }
    }
    tile_geom(0, ty);
    g_trow_y0 = (ty * g_yts) >> g_r;
    y1 = ty * g_yts + g_yts; if (y1 > g_ys) y1 = g_ys;
    g_trow_y1 = cdiv(y1, g_r);
    g_nexttr++;
    return PDE_OK;
}

/* ---- verbs --------------------------------------------------------------------- */
static void close_src(void)
{
#ifndef PD_HOST
    if (g_eng) pdxj_engcall(PDV_ENG_STRM_CLOSE, 0, 0, 0);
#endif
    g_eng = 0;
    g_open = 0;
}

static pdw v_close(void) { close_src(); g_asked = 0; return PDE_OK; }

static pdw v_open(void)
{
    static struct pdxb_img im;
    static struct pdxb_strm sm;
    pdw r, rq, wantw, wanth, n, k;
    if (g_open) close_src();
    pd_fcopy_in(g_xb, 0, &im, sizeof im);
    g_why = 0; s_pos = s_len = s_eof = 0; g_nback = 0;
    s_inl = (im.objnum == 0 && im.gen == 0);
    if (s_inl) { s_seg = im.iseg; s_off = im.ioff; s_left = im.ilen; g_eng = 0; }
    else {
#ifdef PD_HOST
        g_why = PDJ_R_SOURCE; goto fail;
#else
        sm.objnum = im.objnum; sm.gen = im.gen; sm.index = 0; sm.page = 0;
        sm.declen.lo = sm.declen.hi = 0; sm.filters = 0; sm.accept = PDSA_CODEC; sm.codec = 0;
        pd_fcopy_out(&sm, g_xb, 0, sizeof sm);
        r = pdxj_engcall(PDV_ENG_STRM_OPEN, 0, 0, 0);
        if (pdxj_engcf || r) { g_why = PDJ_R_SOURCE; goto fail; }
        g_eng = 1;
        pd_fcopy_in(g_xb, 0, &sm, sizeof sm);
        if (sm.codec != PDCD_JPX) { g_why = PDJ_R_NOTJPX; goto fail; }
#endif
    }
    if (open_container() || parse_main()) goto fail;
    /* what to decode and how it comes out */
    g_inv = (im.flags & PDIMG_INVERT) ? 1 : 0;
    g_nd = 1; g_dc[0] = 0; g_mode = PDIM_GREY8;
    for (k = 0; k < g_nc; k++) g_keep[k] = 0;
    if (im.flags & PDJ_ALPHA) {
        k = g_alpha != 0xFFFF ? g_alpha : ((g_nc == 2 || g_nc == 4) ? g_nc - 1 : 0xFFFF);
        if (k >= g_nc) { g_why = PDJ_R_NOALPHA; goto fail; }
        g_dc[0] = k; g_inv = 0;
    } else if (g_nc >= 3 && g_mct) {
        g_dc[0] = 0;
    } else {
        n = im.ncomp ? im.ncomp : (g_nc == 2 ? 1 : g_nc);
        if (n > g_nc) n = g_nc;
        if (n > 4) n = 4;
        if (n > 1) {
            g_nd = n; g_mode = PDIM_N8; g_inv = 0;
            for (k = 0; k < n; k++) g_dc[k] = k;
        }
    }
    for (k = 0; k < g_nd; k++) g_keep[g_dc[k]] = 1;
    /* the reduction (20.22.13.2) */
    wantw = im.want_w ? im.want_w : g_xs; wanth = im.want_h ? im.want_h : g_ys;
    rq = 0;
    for (r = 1; r <= g_nl; r++) {
        if (cdiv(g_xs, r) < wantw || cdiv(g_ys, r) < wanth) break;
        rq = r;
    }
    /* the level we would choose needs more than the slot has: ask once for the KiB (20.22.2),
     * and when the primary could not regrow it (same slot_kb) fall through to a coarser level */
    {
        pdw sv = g_wsend, need;
        g_wsend = 0xFFF0u;
        for (r = rq; r <= g_nl; r++) { k = plan(r); if (!k) break; }
        g_wsend = sv;
        if (!k) {
            need = (pdw)((w_arena + JX_ARENA_MIN + 1023u) >> 10);
            if (need > 63) need = 63;
            if (need > g_slotkb && g_asked != g_slotkb) {
                g_asked = g_slotkb; pdxj_rdx = need;
                close_src();
                return PDE_NEEDMEM;
            }
        }
    }
    k = PDJ_R_MEM;
    for (r = rq; r <= g_nl; r++) {
        k = plan(r);
        if (!k) break;
    }
    if (k) { g_why = k; goto fail; }
    g_ow = cdiv(g_xs, g_r); g_oh = cdiv(g_ys, g_r);
    g_rowbytes = g_mode == PDIM_N8 ? g_ow * g_nd : g_ow;
    g_nexttr = 0; g_trow_y0 = g_trow_y1 = 0; g_lasty = 0xFFFF;
    t1_tables();
    grey_params(0, g_dc[0]);
    im.out_w = g_ow; im.out_h = g_oh; im.out_mode = g_mode;
    im.out_ncomp = g_mode == PDIM_N8 ? g_nd : 1;
    im.rowbytes = g_rowbytes; im.scale = g_r;
    pd_fcopy_out(&im, g_xb, 0, sizeof im);
    g_open = 1; g_asked = 0;
    return PDE_OK;
fail:
    g_asked = 0;
    im.out_w = g_why;
    pd_fcopy_out(&im, g_xb, 0, sizeof im);
    close_src();
    return g_why == PDJ_R_MEM ? PDE_LIMIT : PDE_FILTER;
}

static pdw v_row(pdw dseg, pdw doff, pdw y)
{
    pdw err, yy, ci, x;
    pdb *tmp, *orow;
    if (!g_open) return PDE_SYNTAX;
    if (y >= g_oh || (g_lasty != 0xFFFF && y < g_lasty)) return PDE_RANGE;
    while (y >= g_trow_y1) {
        err = decode_trow();
        if (err) return err;
    }
    g_lasty = y;
    yy = y - g_trow_y0;
    if (!g_usestrip) {
        g_pstride = 2 * (g_rx1[g_rtop] - g_rx0[g_rtop]);
        xj_grey(g_ws, prow(yy), dseg, doff, g_ow, g_ga[0], g_gs[0], g_gmax[0], g_gt[0], g_inv);
    } else if (g_mode == PDIM_GREY8) {
        pd_fcopy(dseg, doff, g_ws, w_strip + yy * g_ow, g_ow);
    } else {
        tmp = (pdb *)jx_R; orow = tmp + 1024;
        for (ci = 0; ci < g_nd; ci++) {
            pd_fcopy_in(g_ws, w_strip + ci * (g_ow * g_sth) + yy * g_ow, tmp, g_ow);
            for (x = 0; x < g_ow; x++) orow[x * g_nd + ci] = tmp[x];
        }
        pd_fcopy_out(orow, dseg, doff, g_rowbytes);
    }
    return PDE_OK;
}
