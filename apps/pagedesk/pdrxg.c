/* ============================================================================
 * os8088 - apps/pagedesk/pdrxg.c    PDREND: colour spaces and shading (20.22.9)
 *
 * #included by pdri.c just before exec_op. The interpretation half (cs CS sc SC
 * scn SCN sh) asks PDXG, through x_call, for a compiled colour space or
 * shading, keeps the result in the XC cache, and emits display-list records
 * from the cache; the raster half (pdrxgr.c) draws DLR_SHADE and DLR_TRI. While
 * PDXG is absent an operator calls x_need(PDX_GFX) and is skipped (L2): the
 * ladder loads the module and the pass runs again.
 * ==========================================================================*/
static pdw g_xcsk[16][2];           /* colour-space slot -> its cache key (k0, k1); slot 0 = a device space */
static pdw g_xcsn;                  /* slots in use (1..15)                                                */
struct xg_csh { pdb kind, n, flags, pad; pdw hival, srcobj, srcgen, srclen, rsv[2]; };   /* the 16-byte head of a pdgx_cs */
static struct pdxb_gfx g_gx;

/* XG_SHADING 0 compiles the shading half out (cs, sc and the colour LUTs stay): the core's cost of
 * the rasteriser is measured with it on and off in the any-PDF report (gfx-report.md) */
#ifndef XG_SHADING
#define XG_SHADING 0
#endif
#if XG_SHADING
/* ---- the paint seam of the rasteriser (blend's 8-bit band branches here) ---- */
static void xg_emit(pdi y, pdi xa, pdi xb, pdw lum, pdw c4)
{
    g_stl[9] = (pdb)c4; g_stl[10] = 0;
    g_sc4 = (pdb)c4;
    pat_make(g_spat, lum, 0);
    if (g_bpp != 1) {
        g_spat[0] = 0;
        grey_mix(lum, c4);
        g_sc4 = g_stl[9]; g_spat[0] = g_stl[10];
    }
    if (g_sclid < PDR_NCLIP && g_mkok) shape_clipped(y, 0, xa, xb, 0, 0);
    else span_out(y, xa, xb);
}
static void xg_load(pdw off, void *dst, pdw n) { pd_fcopy_in(g_xcseg, off, dst, n); }

#include "pdrxgr.c"
#endif

/* ---- keys ------------------------------------------------------------------ */
static pdw xg_ctx(void) { return g_reso ? g_reso : (pdw)(0x8000u | g_resp); }
static pdw xg_hash(const char *s)
{
    pdw h = 0x5A5Au;
    while (*s) h = (pdw)(((h << 5) | (h >> 11)) ^ (pdb)*s++);
    return h;
}

/* One request to PDXG for a resource: category "ColorSpace" or "Shading", the current name.
 * The result goes to xc:off (rcap bytes). Answers the PDE code (CF clear) or 0xFFFF. */
static pdw xg_ask(pdw verb, const char *cat, pdw rseg, pdw roff, pdw rcap, pdw state)
{
    static char txt[48];
    pdw n = 0, c = 0, r;
    pdr_zero(&g_gx, sizeof g_gx);
    while (cat[c]) txt[n++] = cat[c++];
    txt[n++] = 0;
    c = 0;
    while (g_name[c] && n < 46) txt[n++] = g_name[c++];
    txt[n++] = 0;
    pd_fcopy_out(txt, g_xb, 128, n);
    g_gx.objnum = g_reso; g_gx.gen = g_resg; g_gx.bpc = g_resp == 0xFFFFu ? 0 : g_resp;
    g_gx.flags = PDGXI_RES;
    g_gx.tseg = g_xb; g_gx.toff = 128; g_gx.tlen = n;
    g_gx.rseg = rseg; g_gx.roff = roff; g_gx.rcap = rcap; g_gx.state = state;
    pd_fcopy_out(&g_gx, g_xb, 0, sizeof g_gx);
    r = x_call(verb, g_xb, 0, 0);
    pd_fcopy_in(g_xb, 0, &g_gx, sizeof g_gx);
    return x_cf && r != PDE_BUSY ? 0xFFFFu : r;
}

/* ---- cs / CS ----------------------------------------------------------------- */
/* answers 1 when the operator was handled here (a non-device space), 0 to fall through to the device code */
static int xg_cs(pdw st)
{
    pdw k0, k1, e, slot, i;
    static struct xg_csh hd;
    if (!g_havename || ci_has(g_name, "device")) return 0;
    k0 = xg_ctx(); k1 = xg_hash(g_name);
    for (slot = 1; slot <= g_xcsn; slot++) if (g_xcsk[slot][0] == k0 && g_xcsk[slot][1] == k1) break;
    e = xc_find(XCK_CS, k0, k1);
    if (!e) {
        if (!x_have(PDX_GFX)) { x_need(PDX_GFX); return 1; }
        e = xc_alloc(XCK_CS, k0, k1, PDGX_CSBYTES);
        if (!e || xg_ask(PDV_GFX_CS, "ColorSpace", g_xcseg, e, PDGX_CSBYTES, 0) != PDE_OK) {
            inc_note(PDF_INC_COLOUR, "colour space");
            return 1;
        }
    }
    if (slot > g_xcsn) {
        if (g_xcsn >= 15) { inc_note(PDF_INC_LIMIT, "colour spaces"); return 1; }
        slot = ++g_xcsn; g_xcsk[slot][0] = k0; g_xcsk[slot][1] = k1;
    }
    pd_fcopy_in(g_xcseg, e, &hd, 16);
    if (st) { GS.pad_ = (pdb)((GS.pad_ & 0x0F) | (slot << 4)); GS.sncs = hd.n; }
    else    { GS.pad_ = (pdb)((GS.pad_ & 0xF0) | slot);        GS.fncs = hd.n; }
    /* the initial colour: a table space starts at its first (Indexed) or its full tint (Separation) */
    i = 0;
    if (hd.flags & PDGX_F_TABLE) {
        static pdb rgb[3];
        if (hd.kind == PDGX_K_SEP || hd.kind == PDGX_K_DEVN) i = 255;
        pd_fcopy_in(g_xcseg, (pdw)(e + 496 + i * 3), rgb, 3);
        col_set(st, rgb[0], rgb[1], rgb[2]);
    } else col_set(st, 0, 0, 0);
    return 1;
}

/* ---- sc SC scn SCN ------------------------------------------------------------- */
static int xg_sc(pdw st, pdw nop)
{
    pdw slot = st ? (pdw)(GS.pad_ >> 4) : (pdw)(GS.pad_ & 15), e, base, i, h;
    static struct xg_csh hd;
    static pdb rgb[4];
    static struct pd_fx t;
    if (!slot) return 0;
    if (g_havename) { inc_note(PDF_INC_COLOUR, "pattern colour"); return 1; }      /* tiling and shading patterns: not yet */
    e = xc_find(XCK_CS, g_xcsk[slot][0], g_xcsk[slot][1]);
    if (!e || g_nop < nop) return 1;
    pd_fcopy_in(g_xcseg, e, &hd, 16);
    base = g_nop - nop;
    if (hd.flags & PDGX_F_PATTERN) return 1;
    if (hd.flags & PDGX_F_TABLE) {
        if (hd.kind == PDGX_K_INDEXED) i = (pdw)pdm_fx_round(&g_opn[base]);
        else { pdm_fx_int(&t, 255); pdm_fx_mul(&t, &t, &g_opn[base]); i = (pdw)pdm_fx_round(&t); }
        if (g_opn[base].hi < 0) i = 0;
        if (i > 255) i = 255;
        pd_fcopy_in(g_xcseg, (pdw)(e + 496 + i * 3), rgb, 3);
        col_set(st, rgb[0], rgb[1], rgb[2]);
        return 1;
    }
    if (hd.flags & (PDGX_F_RGBLIKE | PDGX_F_CMYKLIKE)) {
        col_apply(st, hd.n);
        GS.pad_ = (pdb)(st ? ((GS.pad_ & 0x0F) | (slot << 4)) : ((GS.pad_ & 0xF0) | slot));
        return 1;
    }
    /* PDGX_F_MODULE: Lab, CalRGB, DeviceN: the answer is cached, keyed by the space and the operands */
    h = 0;
    for (i = 0; i < nop; i++) h = (pdw)(((h << 3) | (h >> 13)) ^ g_opn[base + i].lo ^ (pdw)g_opn[base + i].hi);
    i = xc_find(XCK_LUT, e, h);
    if (!i) {
        if (!x_have(PDX_GFX)) { x_need(PDX_GFX); return 1; }
        pdr_zero(&g_gx, sizeof g_gx);
        g_gx.cseg = g_xcseg; g_gx.coff = e;
        for (i = 0; i < nop && i < 8; i++) pdr_cpy(&g_gx.comp[i], &g_opn[base + i], 4);
        pd_fcopy_out(&g_gx, g_xb, 0, sizeof g_gx);
        if (x_call(PDV_GFX_COLOR, g_xb, 0, 0) != PDE_OK || x_cf) { inc_note(PDF_INC_COLOUR, "colour space"); return 1; }
        pd_fcopy_in(g_xb, 68, rgb, 4);
        i = xc_alloc(XCK_LUT, e, h, 4);
        if (i) pd_fcopy_out(rgb, g_xcseg, i, 4);
    } else pd_fcopy_in(g_xcseg, i, rgb, 4);
    col_set(st, rgb[0], rgb[1], rgb[2]);
    return 1;
}

#if XG_SHADING
/* ---- sh ------------------------------------------------------------------------- */
/* device pixel -> user space: the inverse of the CTM with the 4 qpx a pixel is wide folded in */
static pdw xg_pixmap(struct pd_fx *m)
{
    static struct pd_fx det, t, u, four;
    pdm_fx_mul(&det, &GS.ctm.a, &GS.ctm.d); pdm_fx_mul(&t, &GS.ctm.b, &GS.ctm.c); pdm_fx_sub(&det, &det, &t);
    if (det.hi == 0 && det.lo == 0) return 0;
    pdm_fx_int(&four, 4);
    pdm_fx_div(&t, &GS.ctm.d, &det); pdm_fx_mul(&m[0], &t, &four);               /* a' = d/det   */
    pdm_fx_div(&t, &GS.ctm.b, &det); pdm_fx_neg(&t, &t); pdm_fx_mul(&m[1], &t, &four);   /* b' = -b/det */
    pdm_fx_div(&t, &GS.ctm.c, &det); pdm_fx_neg(&t, &t); pdm_fx_mul(&m[2], &t, &four);   /* c' = -c/det */
    pdm_fx_div(&t, &GS.ctm.a, &det); pdm_fx_mul(&m[3], &t, &four);               /* d' = a/det   */
    pdm_fx_mul(&t, &GS.ctm.c, &GS.ctm.f); pdm_fx_mul(&u, &GS.ctm.d, &GS.ctm.e); pdm_fx_sub(&t, &t, &u); pdm_fx_div(&m[4], &t, &det);   /* e' = (c f - d e)/det */
    pdm_fx_mul(&t, &GS.ctm.b, &GS.ctm.e); pdm_fx_mul(&u, &GS.ctm.a, &GS.ctm.f); pdm_fx_sub(&t, &t, &u); pdm_fx_div(&m[5], &t, &det);   /* f' = (b e - a f)/det */
    return 1;
}

static void xg_sh(void)
{
    pdw k0, k1, e, state = 0, i, n;
    pdi rec_y0, rec_y1;
    static struct pdgx_shade hd;
    static pdb body[XG_SHBODY];
    static struct pd_fx m[6];
    static pdb trb[XG_TRIBODY];
    static pdb tri[PDGX_TRI_BYTES];
    if (!g_havename || g_mode != PDR_INTERP) return;
    k0 = xg_ctx(); k1 = xg_hash(g_name);
    e = xc_find(XCK_SHADE, k0, k1);
    if (!e) {
        if (!x_have(PDX_GFX)) { x_need(PDX_GFX); return; }
        e = xc_alloc(XCK_SHADE, k0, k1, PDGX_SHBYTES);
        if (!e) { inc_note(PDF_INC_SHADE, "shading"); return; }
        n = xg_ask(PDV_GFX_SHADE, "Shading", g_xcseg, e, PDGX_SHBYTES, 0);
        if (n != PDE_OK && n != PDE_BUSY) { inc_note(PDF_INC_SHADE, "shading"); return; }
    }
    pd_fcopy_in(g_xcseg, e, &hd, 4);
    if (!xg_pixmap(m)) return;
    rec_y0 = GS.cy0 > g_gy0 ? GS.cy0 : g_gy0; rec_y1 = GS.cy1 < g_gy1 ? GS.cy1 : g_gy1;
    if (rec_y1 < rec_y0) return;
    emit_state(PK_FILL);
    if (hd.type == 2 || hd.type == 3) {
        w16(body, (pdi)e);
        for (i = 0; i < 6; i++) pdr_cpy(body + 2 + i * 4, &m[i], 4);
        hdr_set(DLR_SHADE, 0, DLH + XG_SHBODY, rec_y0, rec_y1);
        dl_put(g_hdr, DLH, body, XG_SHBODY);
        return;
    }
    /* a mesh: the module tessellates into the workspace's staging area and each triangle becomes a record */
    if (!x_have(PDX_GFX)) { x_need(PDX_GFX); return; }
    for (;;) {
        n = xg_ask(PDV_GFX_SHADE, "Shading", g_ws, WS_ED, 2048, state);
        if (n != PDE_OK && n != PDE_BUSY) break;
        for (i = 0; i < g_gx.ntri; i++) {
            static pdi vx[3], vy[3];
            pdw v, yl, yh;
            for (v = 0; v < 3; v++) {
                pd_fcopy_in(g_ws, (pdw)(WS_ED + PDGX_SHBYTES + i * PDGX_TRI_BYTES + v * PDGX_VTX_BYTES), tri, PDGX_VTX_BYTES);
                pdr_cpy(&ta, tri, 4); pdr_cpy(&tb, tri + 4, 4);
                to_dev(&ta, &tb);
                vx[v] = g_dx; vy[v] = g_dy;
                w16(trb + v * 4, vx[v]); w16(trb + v * 4 + 2, vy[v]);
                trb[12 + v * 2] = tri[8]; trb[13 + v * 2] = tri[9];
            }
            yl = (pdw)vy[0]; yh = yl;
            for (v = 1; v < 3; v++) { if ((pdi)vy[v] < (pdi)yl) yl = (pdw)vy[v]; if ((pdi)vy[v] > (pdi)yh) yh = (pdw)vy[v]; }
            if (q_lo((pdi)yl) > rec_y1 || q_hi((pdi)yh) < rec_y0) continue;
            hdr_set(DLR_TRI, 0, DLH + XG_TRIBODY, (pdi)(q_lo((pdi)yl) < rec_y0 ? rec_y0 : q_lo((pdi)yl)), (pdi)(q_hi((pdi)yh) > rec_y1 ? rec_y1 : q_hi((pdi)yh)));
            if (!dl_put(g_hdr, DLH, trb, XG_TRIBODY)) return;
        }
        if (n == PDE_OK) break;
        state = g_gx.state;
    }
    if (g_gx.kind) inc_note(PDF_INC_LIMIT, "shading triangles");
}

/* ---- the walker's arms (pdrd.c's walk_band calls this) ---------------------------- */
static void xg_walk(pdw type, pdw off, pdi yy0, pdi yy1)
{
    static pdb sv[5];
    static pdb body[XG_SHBODY];
    static struct pd_fx m[6];
    pdw i;
    sv[0] = g_sc4; pdr_cpy(sv + 1, g_spat, 4);
    if (type == DLR_SHADE) {
        pd_fcopy_in(g_dls, off + DLH, body, XG_SHBODY);
        for (i = 0; i < 6; i++) pdr_cpy(&m[i], body + 2 + i * 4, 4);
        xg_shade_rows((pdw)r16(body), m, g_scx0, g_scx1, yy0, yy1);
    } else if (type == DLR_TRI) {
        pd_fcopy_in(g_dls, off + DLH, body, XG_TRIBODY);
        for (i = 0; i < 6; i++) xg_tq[i] = r16(body + i * 2);
        for (i = 0; i < 6; i++) xg_tl[i] = body[12 + i];
        xg_tcx0 = g_scx0; xg_tcx1 = g_scx1;
        xg_tri_rows(yy0, yy1);
    }
    g_sc4 = sv[0]; pdr_cpy(g_spat, sv + 1, 4);
}
#else
static void xg_sh(void) { inc_note(PDF_INC_SHADE, "shading"); }
static void xg_walk(pdw type, pdw off, pdi yy0, pdi yy1) { (void)type; (void)off; (void)yy0; (void)yy1; }
#endif
