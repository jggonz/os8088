/* ============================================================================
 * os8088 - apps/pagedesk/pdrv.c      PDREND: images at raster time, the pass
 *                                    machine, and the verbs
 *
 * #included by pdrend.c. PAGEDESK-SPEC 10.2, 10.7, 10.8, 15.2, 20.3.
 * ==========================================================================*/

/* ---- images, drawn from the engine's stream as the bands go by ------------ */
static pdi g_icur;                  /* source row held in the buffer            */
static pdw g_iobj, g_igen, g_ibytes, g_iopen;
static const pdb dpal4[4] = { 0, 8, 7, 15 };

static void img_close(void) { if (g_iopen) { strm_close(); g_iopen = 0; } }
static pdw img_reopen(void)
{
    img_close();
    if (strm_open(g_iobj, g_igen, 0, 0)) return 0;
    g_iopen = 1; g_icur = -1;
    return 1;
}
/* read the next source row into the row buffer; 0 on a short stream */
static pdw img_row(void)
{
    pdw got = 0, r;
    while (got < g_ibytes) {
        if (pdr_eng(PDV_ENG_STRM_READ, g_gseg, got, g_ibytes - got)) return 0;
        r = pdr_eng_ax;
        if (!r) return 0;
        got += r;
    }
    g_icur++;
    return 1;
}

static void draw_image(pdw off, pdw len, pdi ry0, pdi ry1)
{
#ifdef PDR_X8
    static pdb b[24];
#else
    static pdb b[20];
#endif
    static struct pd_u32 u, q;
    pdi xl, yt, xr, yb, wq, hq, y, sr, x0, x1, delta;
    pdw W, H, bpc, ncomp, flags, rb, stencil, sm, obj, gen, inl;
    (void)len;
    pd_fcopy_in(g_dls, off + DLH, b, 20);
#ifdef PDR_X8
    if (b[18] & 0x60) pd_fcopy_in(g_dls, off + DLH + 20, b + 20, 4);      /* blend: the mask object (20.22.10.3) */
#endif
    xl = r16(b); yt = r16(b + 2); xr = r16(b + 4); yb = r16(b + 6);
    W = (pdw)r16(b + 8); H = (pdw)r16(b + 10);
    obj = (pdw)r16(b + 12); gen = (pdw)r16(b + 14);
    bpc = b[16]; ncomp = b[17]; flags = b[18];
    stencil = flags & 1;
    inl = flags & 16;                          /* an inline image: its rows follow the body in the list */
    if (!inl && (!g_gseg || g_gkb < 2)) { inc_note(PDF_INC_IMAGE, "no image buffer"); return; }
    wq = (pdi)(xr - xl); hq = (pdi)(yb - yt);
    if (wq < 1) wq = 1;
    if (hq < 1) hq = 1;
    if (!W || !H || W > 4096 || H > 0x7FFF) return;
    /* the row in bytes, without a 16-bit product that can wrap (do_image allows
     * 8 bits of 1 or 3 components, or 1 bit of one) */
    rb = bpc == 8 ? (pdw)(W * ncomp) : (pdw)((W + 7) >> 3);
    if (rb > 1024) { inc_note(PDF_INC_LIMIT, "image row size"); return; }
    /* the stream stays open from band to band while the same image is drawn
     * further down: each band reads on from the row the last one stopped at,
     * instead of decoding the image from its first row again */
    if (!inl && (!g_iopen || g_iobj != obj || g_igen != gen || g_ibytes != rb)) {
        g_iobj = obj; g_igen = gen; g_ibytes = rb;
        if (!img_reopen()) { inc_note(PDF_INC_FILTER, "image stream"); return; }
    }
    sm = stencil ? 3 : (ncomp == 3 ? 2 : (bpc == 1 ? 0 : 1));
    /* destination columns: first/last pixel the rectangle and the clip share */
    x0 = q_lo(xl); x1 = q_hi(xr);
    if (x1 < x0) x1 = x0;
    if (x0 < g_scx0) x0 = g_scx0;
    if (x1 > g_scx1) x1 = g_scx1;
    if (x0 < g_bx0) x0 = g_bx0;
    if (x1 > (pdi)(g_bx0 + g_bw - 1)) x1 = (pdi)(g_bx0 + g_bw - 1);
    if (x1 < x0) return;
    delta = (pdi)(x0 * 4 + 2 - xl);
    if (delta < 0) delta = 0;
    pdm_u32_set(&u, (pdw)(4 * W), 0);
    pdm_u32_divmod16(&q, &u, (pdw)wq);
#ifdef PDR_X8
    if (g_xs) x8_imgset(b, flags, xl, yt, wq, hq, x0, x1, ry0, ry1);   /* blend: this strip's image mask */
#endif
    for (y = ry0; y <= ry1; y++) {
        pdi qy = (pdi)(y * 4 + 2);
        if (y < g_scy0 || y > g_scy1) continue;
        if (qy <= yt) sr = 0;
        else if (qy >= yb) sr = (pdi)(H - 1);
        else sr = pdr_muldiv((pdi)(qy - yt), (pdi)H, hq);
        if (sr >= (pdi)H) sr = (pdi)(H - 1);
        if (!inl) {
            if (sr < g_icur) { if (!img_reopen()) return; }
            while (g_icur < sr) if (!img_row()) { img_close(); return; }
            if (g_icur < 0 && !img_row()) { img_close(); return; }
        }
        {
            pdi s0 = pdr_muldiv(delta, (pdi)W, wq);
            pdr_ip.bseg = g_bseg;
            pdr_ip.boff = (pdw)(y - g_by0) * g_bstride;
            pdr_ip.x0 = (pdi)(x0 - g_bx0); pdr_ip.x1 = (pdi)(x1 - g_bx0);
            if (inl) { pdr_ip.sseg = g_dls; pdr_ip.soff = (pdw)(off + DLH + 20 + (pdw)sr * rb); }
            else { pdr_ip.sseg = g_gseg; pdr_ip.soff = 0; }
            pdr_ip.sw = W;
            pdr_ip.xlo = 0; pdr_ip.xhi = (pdw)s0;
            pdr_ip.slo = q.lo; pdr_ip.shi = q.hi;
            pdr_ip.smode = sm | ((flags & 4) ? 0x100 : 0);
            pdr_ip.inv = (flags & 2) ? 1 : 0;
            pdr_ip.dmode = g_bpp;
            pdr_ip.dyrow = (pdw)(y & 3);
            pdr_ip.col = g_bpp == 1 ? (g_spat[0] & 1) : g_sc4;
#ifdef PDR_X8
            if (g_xs) x8_imgrow(y); else
#endif
            pdr_imgrow();                                   /* blend: the 8-bit strip's twin (20.22.10.8) */
        }
    }
}

/* ============================================================================
 * THE PASS MACHINE
 * ==========================================================================*/
static pdw g_ph;                    /* PDR_*                                    */
static pdw g_nb, g_b0, g_b1, g_gw, g_bcur;
static struct pdxb_render g_rq;
static struct pdxb_init g_ini;
static struct pdxb_textmap g_tmq;
static struct pdxb_thumb g_thq;
static struct pdxb_pageinfo g_pi;
static pdw g_flatscale;

static void close_streams(void)
{
    img_close();
    while (g_nfr) { strm_close(); g_nfr--; }
    if (g_pgopen) strm_close();
    g_pgopen = 0; g_idone = 1; g_await = 0;
}

/* a pass interprets the content once for the rows of bands [g_b0, g_b1) */
static pdw g_subm, g_subm_w;            /* sub-row mode: one band, composed in pieces */
static pdw pass_begin(void)
{
    img_close();                        /* a held image is not the top stream any more */
    g_eylo = (pdi)(g_gy0 * 4); g_eyhi = (pdi)(g_gy1 * 4 + 3);
    g_dllen = 0; g_dlfull = 0; g_lstv = 0; g_ndl = 0; g_cetop = 0; g_ncl = 0;
    g_passes++;
    gs_init();
    xb_pass();
    g_ph = PDR_INTERP;
    return interp_start(g_rq.page);
}
static pdw pass_start(void)
{
    pdi last = (pdi)(g_vcy0 + (pdi)g_b1 * PD_BANDROWS - 1);
    g_subm = 0;
    g_gy0 = (pdi)(g_vcy0 + (pdi)g_b0 * PD_BANDROWS);
    g_gy1 = last > g_vcy1 ? g_vcy1 : last;
    return pass_begin();
}

static void view_base(const struct pd_view *v)
{
    static struct pd_fx sx, sy, a, b;
    pdm_fx_int(&a, (pdi)(v->zoom * 4)); pdm_fx_int(&b, 100);
    pdm_fx_div(&sx, &a, &b);
    pdm_fx_int(&a, (pdi)v->asp_num); pdm_fx_int(&b, (pdi)v->asp_den);
    pdm_fx_mul(&sy, &sx, &a);
    pdm_fx_div(&sy, &sy, &b);
    base_build(&v->box, v->rot, &sx, &sy, v->ox, v->oy);
}

static void mode_reset(void)
{
    g_inc = 0; g_incname[0] = 0; g_ops = 0; g_passes = 0;
    g_ierr = 0; g_ndl = 0;
    g_pkfam = 0xFF; g_aflags = 0;
}

static pd_px vb_w, vb_h;
static pdw verb_begin(void)
{
    pdw err;
    pd_fcopy_in(g_xb, 0, &g_rq, sizeof g_rq);
    if (!g_dls || !g_bseg) return PDE_NOMEM;
    close_streams();
    mode_reset();
    g_mode = PDR_INTERP; g_thumb = 0; g_bpp = g_ini.bpp == 4 ? 4 : 1;
    g_aflags = g_rq.rflags;
    g_winoff = g_rq.win_off;
    g_vcx0 = g_rq.view.clip_x0; g_vcy0 = g_rq.view.clip_y0;
    g_vcx1 = g_rq.view.clip_x1; g_vcy1 = g_rq.view.clip_y1;
    g_ydone = g_vcy0;
    if (g_vcx1 < g_vcx0 || g_vcy1 < g_vcy0) { g_ph = PDR_DONE; return PDE_OK; }
    pdm_view_size(&vb_w, &vb_h, &g_rq.view);
    g_pgx0 = g_rq.view.ox; g_pgy0 = g_rq.view.oy;
    g_pgx1 = (pdi)(g_pgx0 + vb_w - 1); g_pgy1 = (pdi)(g_pgy0 + vb_h - 1);
    if (!band_geometry()) return PDE_NOMEM;
    x8_view();
    view_base(&g_rq.view);
    g_nb = band_count();
    g_gw = g_nb; g_b0 = 0; g_b1 = g_nb;
    g_deskwhite = 0;
    err = pass_start();
    if (err) { g_ph = PDR_DONE; return err; }
    return PDE_OK;
}

/* the DL filled up: narrow the group and rescan (PAGEDESK-SPEC 20.3.2) */
static void group_narrow(void)
{
    pdw n = g_b1 - g_b0;
    close_streams();
    if (n > 1) {
        g_gw = (n + 1) >> 1;
        g_b1 = g_b0 + g_gw;
        pass_start();
    } else if (g_subm ? g_subm_w > 1 : (g_gy1 - g_gy0) > 0) {
        /* one band and still too much: compose it in sub-passes of fewer rows,
         * onto the same band buffer, and blit once (PAGEDESK-SPEC 20.3.2) */
        if (!g_subm) { g_subm = 1; g_subm_w = (pdw)(g_gy1 - g_gy0 + 1); band_open(g_b0); band_init(); }
        g_subm_w = (g_subm_w + 1) >> 1;
        g_gy1 = (pdi)(g_gy0 + g_subm_w - 1);
        pass_begin();
    } else {
        inc_note(PDF_INC_LIMIT, "display list full");
        g_ph = PDR_RAST; g_bcur = g_b0;
    }
}

static pdw verb_step(void)
{
    pdw done, err;
    if (g_ph == PDR_DONE || g_ph == PDR_IDLE) return PDE_OK;
    if (g_ph == PDR_INTERP) {
        done = interp_run(PD_SLICE_OPS);
        if (g_dlfull) { group_narrow(); return PDE_BUSY; }
        if (done) {
            close_streams();
#ifdef PDR_X8
            if (xb_needmem()) { g_needcx = PDMEM_X8; g_needdx = g_x8need; return PDE_NEEDMEM; }   /* 20.22.10.2 */
#endif
            g_ph = PDR_RAST; g_bcur = g_b0;
        }
        return PDE_BUSY;
    }
    if (g_ph == PDR_RAST && g_subm) {
        /* one sub-range of the band is interpreted: draw its rows into the band */
            g_wlo = g_gy0; g_whi = g_gy1;
        walk_band(0, 0);
        if (g_gy1 >= (pdi)(g_by0 + g_brows - 1)) {
            g_wlo = g_by0; g_whi = (pdi)(g_by0 + g_brows - 1);
            band_present();
            g_subm = 0;
            g_bcur = g_b1;                      /* the group (one band) is done */
        } else {
            pdi hi = (pdi)(g_by0 + g_brows - 1);
            g_gy0 = (pdi)(g_gy1 + 1);
            g_gy1 = (pdi)(g_gy0 + g_subm_w - 1);
            if (g_gy1 > hi) g_gy1 = hi;
            err = pass_begin();
            if (err) { g_ph = PDR_DONE; return err; }
            return PDE_BUSY;
        }
        if (g_bcur >= g_b1) goto groupend;
        return PDE_BUSY;
    }
    if (g_ph == PDR_RAST) {
        band_draw(g_bcur);
        g_bcur++;
        if (g_bcur >= g_b1) {
        groupend:
            if (g_b1 >= g_nb) { img_close(); g_ph = PDR_DONE; return PDE_OK; }
            g_b0 = g_b1;
            g_b1 = g_b0 + g_gw; if (g_b1 > g_nb) g_b1 = g_nb;
            err = pass_start();
            if (err) { g_ph = PDR_DONE; return err; }
        }
        return PDE_BUSY;
    }
    return PDE_OK;
}

/* 20.19.1: CX = 1 also writes, as XB word 0, the first view row the render
 * did not blit (its clip's top when none was): the primary lays paper from
 * there instead of leaving the old pixels under a cancelled page */
static pdw verb_abort(pdw cx)
{
    if (cx) pd_fcopy_out(&g_ydone, g_xb, 0, 2);
    close_streams();
    g_ph = PDR_IDLE; g_mode = PDR_IDLE;
    return PDE_OK;
}

static pdw verb_status(void)
{
    static struct pdxb_rstatus rs;
    pdr_zero(&rs, sizeof rs);
    rs.incomplete = g_inc;
    pdr_scpy(rs.first, g_incname, 32);
    rs.ops = g_ops; rs.passes = g_passes;
    pd_fcopy_out(&rs, g_xb, 0, sizeof rs);
    return PDE_OK;
}

/* ---- the text map ----------------------------------------------------------- */
static pdw verb_textmap(void)
{
    static struct pd_fx s16, z;
    pdw n, room, err;
    pd_fcopy_in(g_xb, 0, &g_tmq, sizeof g_tmq);
    if (!g_dls) return PDE_NOMEM;
    if (g_tmq.dmax < 12) return PDE_RANGE;          /* no room for one record: it would never finish */
    if (g_tmq.state == 0) {
        close_streams();
        mode_reset();
        g_mode = PDR_TEXT; g_ph = PDR_TEXT;
        g_tmwr = g_tmrd = g_tmrun = 0; g_tmoverflow = 0; g_tmfirst = 1;
        pdm_fx_int(&s16, 16); pdm_fx_int(&z, 0);
        pdr_zero(&g_base, sizeof g_base);
        pdr_cpy(&g_base.a, &s16, 4); pdr_cpy(&g_base.d, &s16, 4);
        g_vcx0 = 0; g_vcy0 = 0; g_vcx1 = 32000; g_vcy1 = 32000;
        g_pgx0 = g_pgy0 = -32000; g_pgx1 = g_pgy1 = 32000;
        g_gy0 = -32000; g_gy1 = 32000;
        gs_init();
        GS.cx0 = GS.cy0 = -32000; GS.cx1 = GS.cy1 = 32000;
        err = interp_start(g_tmq.page);
        g_tmq.state = 1;
        if (err) { g_tmq.done = 1; g_tmq.count = 0; pd_fcopy_out(&g_tmq, g_xb, 0, sizeof g_tmq); return err; }
        (void)z;
    }
    if (g_ph != PDR_TEXT) return PDE_CANCEL;
    /* drain what is waiting, else interpret one more slice */
    if (g_tmrd >= g_tmwr && !g_idone) {
        g_tmrd = g_tmwr = 0;
        interp_run(PD_SLICE_OPS);
    }
    room = g_tmq.dmax / 12;
    n = g_tmwr - g_tmrd;
    if (n > room) n = room;
    if (n) {
        pd_fcopy(g_tmq.dseg, g_tmq.doff, g_dls, (pdw)((g_tmrd << 3) + (g_tmrd << 2)), (pdw)((n << 3) + (n << 2)));
        g_tmrd += n;
    }
    g_tmq.count = n;
    g_tmq.done = (g_idone && g_tmrd >= g_tmwr) ? 1 : 0;
    if (g_tmq.done) { close_streams(); g_ph = PDR_IDLE; g_mode = PDR_IDLE; }
    if (g_tmoverflow) inc_note(PDF_INC_LIMIT, "text map");
    pd_fcopy_out(&g_tmq, g_xb, 0, sizeof g_tmq);
    return PDE_OK;
}

/* ---- thumbnails ------------------------------------------------------------- */
static struct pd_view g_tv;
static pdw verb_thumb(void)
{
    pdw err, dw, dh, zw, zh;
    pdi pw, ph;
    pd_fcopy_in(g_xb, 0, &g_thq, sizeof g_thq);
    if (g_thq.state == 0) {
        if (!g_dls || !g_bseg || !g_thq.w || !g_thq.h || g_thq.w > 720 || g_thq.h > 720) return PDE_NOMEM;
        close_streams();
        mode_reset();
        err = eng_call(PDV_ENG_PAGEINFO, &g_pi, sizeof g_pi, g_thq.page, 0, 0);
        if (err) return err;
        pdr_zero(&g_tv, sizeof g_tv);
        g_tv.box.x0 = pdr_max(g_pi.media.x0, g_pi.crop.x0); g_tv.box.y0 = pdr_max(g_pi.media.y0, g_pi.crop.y0);
        g_tv.box.x1 = pdr_min(g_pi.media.x1, g_pi.crop.x1); g_tv.box.y1 = pdr_min(g_pi.media.y1, g_pi.crop.y1);
        g_tv.rot = g_pi.rotate;
        if (g_tv.box.x1 - g_tv.box.x0 < 160 || g_tv.box.y1 - g_tv.box.y0 < 160) return PDE_RANGE;
        dw = (pdw)(g_tv.box.x1 - g_tv.box.x0); dh = (pdw)(g_tv.box.y1 - g_tv.box.y0);
        if (g_tv.rot == 90 || g_tv.rot == 270) { zw = dw; dw = dh; dh = zw; }
        /* the zoom, in percent, at which the page fits the cell: px / (pt16 / 16) * 100 */
        zw = (pdw)pdr_muldiv((pdi)g_thq.w, 1600, (pdi)dw);
        zh = (pdw)pdr_muldiv((pdi)g_thq.h, 1600, (pdi)dh);
        g_tv.zoom = zw < zh ? zw : zh;
        if (g_tv.zoom < 1) g_tv.zoom = 1;
        g_tv.asp_num = g_tv.asp_den = 1;
        pdm_view_size(&vb_w, &vb_h, &g_tv);
        pw = vb_w; ph = vb_h;
        if (pw < 1) pw = 1;
        if (ph < 1) ph = 1;
        if (pw > (pdi)g_thq.w) pw = (pdi)g_thq.w;
        if (ph > (pdi)g_thq.h) ph = (pdi)g_thq.h;
        g_tv.ox = (pdi)(((pdi)g_thq.w - pw) >> 1); g_tv.oy = (pdi)(((pdi)g_thq.h - ph) >> 1);
        view_base(&g_tv);
        g_mode = PDR_INTERP; g_thumb = 1; g_bpp = 1; g_deskwhite = 1;
        g_winoff = 0;
        g_vcx0 = 0; g_vcy0 = 0; g_vcx1 = (pdi)(g_thq.w - 1); g_vcy1 = (pdi)(g_thq.h - 1);
        g_pgx0 = g_tv.ox; g_pgy0 = g_tv.oy; g_pgx1 = (pdi)(g_tv.ox + pw - 1); g_pgy1 = (pdi)(g_tv.oy + ph - 1);
        g_tdseg = g_thq.dseg; g_tdoff = g_thq.doff; g_tstride = (pdw)((g_thq.w + 7) >> 3);
        if (!band_geometry()) return PDE_NOMEM;
        g_rq.page = g_thq.page;
        g_nb = band_count(); g_gw = g_nb; g_b0 = 0; g_b1 = g_nb;
        err = pass_start();
        g_thq.state = 1;
        pd_fcopy_out(&g_thq, g_xb, 0, sizeof g_thq);
        if (err) { g_ph = PDR_DONE; return err; }
        return PDE_BUSY;
    }
    err = verb_step();
    if (err == PDE_OK) { g_thumb = 0; g_thq.state = 2; }
    pd_fcopy_out(&g_thq, g_xb, 0, sizeof g_thq);
    return err;
}

/* ---- INIT and the dispatcher -------------------------------------------------- */
static pdw verb_init(pdw xbseg)
{
    pdw bound = pdr_eng_seg;            /* 0 on the FIRST INIT: the bss */
    g_xb = xbseg;
    /* the engine's vector first, before anything can call the engine. The
     * first INIT then closes nothing: its g_idone = 0 is the bss, not an open
     * content stream, and close_streams() through the 0000:0000 it found was
     * rendfix's launch hang (pdr_eng now refuses a 0 vector as well) */
    pd_fcopy_in(g_xb, 0, &g_ini, sizeof g_ini);
    pdr_eng_off = g_ini.eng_off; pdr_eng_seg = g_ini.eng_seg;
    if (bound) close_streams();
    g_ph = PDR_IDLE; g_mode = PDR_IDLE;
    g_dls = g_ini.dl_seg;
    g_dlcap = g_ini.dl_kb ? (pdw)(g_ini.dl_kb >= 63 ? 0xFC00u : (pdw)(g_ini.dl_kb << 10)) : 0;
    /* the workspace is the last PDR_WSB bytes of the claim: the DL gets the rest.
     * A claim with no room for both is NO claim (D13): BEGIN, TEXTMAP and THUMB
     * refuse PDE_NOMEM rather than put the workspace past its end */
    if (g_dls && g_dlcap >= PDR_WSB + PDR_MINDL) { g_dlcap -= PDR_WSB; g_ws = (pdw)(g_dls + (g_dlcap >> 4)); }
    else { g_dlcap = 0; g_dls = 0; g_ws = 0; }
    g_bseg = g_ini.band_seg; g_bbytes = g_ini.band_bytes;
    g_gseg = g_ini.glyph_seg; g_gkb = g_ini.glyph_kb > 63 ? 63 : g_ini.glyph_kb;   /* << 10 fits 16 bits */
    if (!g_gseg) g_gkb = 0;
    g_bankok = 0; g_bankseg = 0; g_bk_seg[0] = g_bk_seg[1] = 0; g_pkfam = 0xFF;
    g_scn = 0; g_scbump = SC_BASE; g_fxok = 0;
    g_bpp = g_ini.bpp == 4 ? 4 : 1;
    x8_init(g_ini.x8_seg, g_ini.x8_kb);
    g_nfr = 0; g_idone = 1; g_pgopen = 0;
    /* perf: an INIT is a new document (D2): its fonts and Tf names are new */
    g_fnnext = 0; pdr_zero(g_fk, sizeof g_fk); g_fwi = 0xFF; g_fwdirty = 0; g_rcn = 0;
    return PDE_OK;
}

/* PDV_REND_FONTBANK (D3): the primary's INDEX claim of PDRES, where the package
 * is, and where the PDRES part starts in it. The optional PDFONTX.RES beside
 * the package gets its own INDEX read into the glyph claim (D5). The primary
 * sends this after every PDV_REND_INIT that changed the claims. */
static struct pdxb_fontbank g_fbq;
static pdw verb_fontbank(void)
{
    pdw cb, cap, got, n, seg;
    pd_fcopy_in(g_xb, 0, &g_fbq, sizeof g_fbq);
    bank_attach(g_fbq.index_seg, g_fbq.index_len);
    pdr_scpy(g_pkgname, g_fbq.pkgname, 14);
    g_pclus = g_fbq.place_clus; g_pvol = g_fbq.place_vol;
    g_poff_lo = g_fbq.part_off.lo; g_poff_hi = g_fbq.part_off.hi;
    g_scn = 0; g_scbump = SC_BASE; g_bk_seg[1] = 0; g_bk_n[1] = 0; g_fxok = 0;
    if (!g_bankok) return PDE_FONT;
    if (g_gseg && g_gkb >= 8) {
        cb = file_enter();
        if (cb) {
            cap = (pdw)((2048 + cb - 1) & ~(cb - 1));
            got = file_read("PDFONTX.RES", 0, 0, (pdw)(g_gseg + (GC_IDX >> 4)), cap);
            file_leave();
            if (got > cap) got = cap;
            if (got > GC_XBASE - GC_IDX) got = GC_XBASE - GC_IDX;     /* what the region holds */
            if (got >= 96) {
                seg = index_bank((pdw)(g_gseg + (GC_IDX >> 4)), got, &g_bn_tmp);
                n = g_bn_tmp;
                if (seg) { g_bk_seg[1] = seg; g_bk_n[1] = n; g_fxok = 1; }
            }
        }
    }
    /* the transformed-glyph slots hold whatever the new claim held: empty them
     * wherever glyph_xform may use them (a claim of 5 KiB or more) */
    if (g_gseg && g_gkb >= 5) pd_ffill(g_gseg, GC_XBASE, 0, SC_BASE - GC_XBASE);
    g_pkfam = 0xFF;
    return PDE_OK;
}

pdw pdr_verb(pdw verb, pdw bx, pdw cx, pdw dx)
{
    (void)dx;
    switch (verb) {
    case PDV_REND_INIT:     return verb_init(bx);
    case PDV_REND_BEGIN:    return verb_begin();
    case PDV_REND_STEP:     return verb_step();
    case PDV_REND_ABORT:    return verb_abort(cx);
    case PDV_REND_TEXTMAP:  return verb_textmap();
    case PDV_REND_THUMB:    return verb_thumb();
    case PDV_REND_STATUS:   return verb_status();
    case PDV_REND_FONTBANK: return verb_fontbank();
    }
    return PDE_RANGE;
}

#ifdef PD_HOST
/* host test hook: the base matrix applied to a page point (pt16), qpx result */
void pdr_test_fwd(pd_pt x, pd_pt y, pdi *qx, pdi *qy)
{
    static struct pd_fx fx, fy;
    pdm_pt16_to_fx(&fx, x); pdm_pt16_to_fx(&fy, y);
    pdm_mat_apply(&fx, &fy, &g_base);
    *qx = pdm_fx_round(&fx); *qy = pdm_fx_round(&fy);
}
void pdr_test_base(const struct pd_view *v) { view_base(v); }
#endif
