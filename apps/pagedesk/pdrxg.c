/* ============================================================================
 * os8088 - apps/pagedesk/pdrxg.c    PDREND: colour spaces, shading and patterns (20.22.9)
 *
 * #included by pdri.c just before paint; it defines the hooks pdri.c calls (XG_CS, XG_SC, XG_SH, XG_FILL,
 * XG_TILE_NEXT) and pdri.c's defaults stand in when PDR_NOXG is defined. The operators cs CS sc SC scn SCN sh
 * ask PDXG, through x_call, for a compiled colour space or pattern (kept in the XC cache), a colour, or a
 * shading; a shading comes back as FLAT POLYGONS that this file appends as ordinary fills, so the core has no
 * shading rasteriser. A tiling pattern replays its content stream once a tile, through the form frame. While
 * PDXG is absent an operator calls x_need(PDX_GFX) and is skipped (L2): the ladder loads it, the pass reruns.
 * ==========================================================================*/
static pdw g_xcsk[16][2];           /* colour-space slot -> its cache key (k0, k1); slot 0 = a device space */
static pdw g_xcsn;                  /* slots in use (1..15)                                                */
static pdw g_xslot;                 /* xg_entry's slot                                                     */
static pdw g_xcrr;                  /* the slot a 16th space takes (round robin)                           */
static pdw g_xcrcap;                /* xg_alloc's room                                                      */
static struct pdxb_gfx g_gx;
#define XG_RES   256                /* the XB offset of a shading's records, and their room */
#define XG_RCAP  760

#define XG_CS(st)         xg_cs(st)
#define XG_SC(st, nop)    xg_sc(st, nop)
#define XG_SH()           xg_sh()
#define XG_FILL(rule, st) xg_fill(rule, st)
#define XG_TILE_NEXT()    xg_tile_next()

/* ---- one request to PDXG: begin (the block is zeroed, the CTM and clip go in), set fields, send --------------- */
/* [gfx far-candidate: per operator, once per cs/sc/scn/sh] */
static void xg_begin(pdw cat)
{
    pdw n = 0;
    pdr_zero(&g_gx, sizeof g_gx);
    if (cat) {                                                    /* the resource of this category and the current name */
        while (g_name[n]) n++;
        pd_fcopy_out(g_name, g_xb, 128, n + 1);
        g_gx.objnum = g_reso; g_gx.gen = g_resg; g_gx.bpc = g_resp == 0xFFFFu ? 0 : g_resp;
        g_gx.flags = (pdw)(PDGXI_RES | cat); g_gx.tseg = g_xb; g_gx.toff = 128; g_gx.tlen = n + 1;
    }
    pdr_cpy(&g_gx.ctm, &GS.ctm, sizeof GS.ctm);
    g_gx.clip[0] = GS.cx0; g_gx.clip[1] = GS.cy0; g_gx.clip[2] = GS.cx1; g_gx.clip[3] = GS.cy1;
}
/* the PDE code (CF clear), or 0xFFFF; g_gx is the block as PDXG left it */
/* [gfx far-candidate: per operator, once per cs/sc/scn/sh] */
static pdw xg_req(pdw verb)
{
    pdw r;
    pd_fcopy_out(&g_gx, g_xb, 0, sizeof g_gx);
    r = x_call(verb, g_xb, 0, 0);
    if (x_cf && r != PDE_BUSY && strm_park()) {     /* its stream wanted our Flate slot (20.22.7.12) */
        pd_fcopy_out(&g_gx, g_xb, 0, sizeof g_gx);
        r = x_call(verb, g_xb, 0, 0);
    }
    pd_fcopy_in(g_xb, 0, &g_gx, sizeof g_gx);
    return x_cf && r != PDE_BUSY ? 0xFFFFu : r;
}
/* [gfx far-candidate: per operator, once per cs/sc/scn/sh] */
static pdw xg_ask(pdw verb, pdw cat, pdw rseg, pdw roff, pdw rcap, pdw state)
{
    xg_begin(cat);
    g_gx.rseg = rseg; g_gx.roff = roff; g_gx.rcap = rcap; g_gx.state = state;
    return xg_req(verb);
}
#define xg_nc() inc_note(PDF_INC_COLOUR, "colour space")
#define xg_ncx(w) inc_note(PDF_INC_COLOUR, w)

/* the colour of cache entry e's space for the last `nop` operands (0: its initial colour) */
/* [gfx far-candidate: per operator, once per cs/sc/scn/sh] */
static void xg_color(pdw st, pdw e, pdw nop)
{
    pdw i;
    if (!x_have(PDX_GFX)) { x_need(PDX_GFX); return; }
    xg_begin(0);
    g_gx.cseg = g_xcseg; g_gx.coff = e; g_gx.ncomp = nop;
    for (i = 0; i < nop && i < 8; i++) pdr_cpy(&g_gx.comp[i], &g_opn[g_nop - nop + i], 4);
    if (xg_req(PDV_GFX_COLOR) != PDE_OK) { xg_ncx("colour value"); return; }
    col_set(st, g_gx.rgb[0], g_gx.rgb[1], g_gx.rgb[2]);
}

/* ---- slots: a gstate names its fill and stroke space by a 4-bit slot (GS.pad_) ----------------------------- */
/* an XC entry of up to `size` bytes: when the cache has less room, what it has (>= 128), which a small entry fits */
static pdw xg_alloc(pdw kind, pdw k0, pdw k1, pdw size)
{
    pdw room = g_xccap > g_xctop + 8 ? (pdw)(g_xccap - g_xctop - 8) : 0;
    if (!g_xcseg) return xc_alloc(kind, k0, k1, size);
    g_xcrcap = size;
    if (room < size && kind == XCK_CS && room >= 128) g_xcrcap = room;
    else if (room < size) { g_xcfull = 1; return 0; }
    return xc_alloc(kind, k0, k1, g_xcrcap);
}
/* the cache entry of a resource of `kind` (XCK_CS / XCK_PAT), asked for when it is not there; g_xslot is its
 * slot. Answers the entry's offset, 0 for none (skipped: the ladder, or a note) */
/* [gfx far-candidate: per operator, once per cs/sc/scn/sh] */
static pdw xg_entry(pdw kind, pdw verb, pdw cat, pdw size)
{
    pdw k0 = g_reso ? g_reso : (pdw)(0x8000u | g_resp), k1 = 0x5A5Au, e, ra;
    const char *s = g_name;
    while (*s) k1 = (pdw)(((k1 << 5) | (k1 >> 11)) ^ (pdb)*s++);
    if (kind == XCK_PAT) k1 ^= 0x5555u;
    e = xc_find(kind, k0, k1);
    if (!e) {
        if (!x_have(PDX_GFX)) { x_need(PDX_GFX); return 0; }
        e = xg_alloc(kind, k0, k1, size);
        if (!e) { if (g_xcseg) xg_ncx("colour cache"); return 0; }       /* no cache yet: x_mem asked, the pass reruns */
        ra = xg_ask(verb, cat, g_xcseg, e, g_xcrcap, 0);
        if (ra != PDE_OK) { if (ra == PDE_NOMEM) g_xcfull = 1; xg_ncx(ra == PDE_NOMEM ? "colour space size" : "colour space source"); return 0; }
        if (kind == XCK_CS) xc_trim(e, g_gx.rlen);                 /* only a table space keeps its 768 bytes */
    }
    for (g_xslot = 1; g_xslot <= g_xcsn; g_xslot++) if (g_xcsk[g_xslot][0] == k0 && g_xcsk[g_xslot][1] == k1) return e;
    if (g_xcsn < 15) g_xslot = ++g_xcsn;                              /* a 16th space reuses the oldest slot: the gstates   */
    else { g_xslot = (pdw)(g_xcrr % 15 + 1); g_xcrr++; }              /* that named it are long gone on any real page       */
    g_xcsk[g_xslot][0] = k0; g_xcsk[g_xslot][1] = k1;
    return e;
}
/* [gfx far-candidate: per operator, once per cs/sc/scn/sh] */
static void xg_setslot(pdw st, pdw slot)
{
    GS.pad_ = (pdb)(st ? ((GS.pad_ & 0x0F) | (slot << 4)) : ((GS.pad_ & 0xF0) | slot));
}

/* ---- cs / CS: answers 1 when handled here (a non-device space), 0 to fall through to the device code ----------- */
/* [gfx far-candidate: per operator, once per cs/sc/scn/sh] */
static pdw xg_cs(pdw st)
{
    pdw e;
    if (!g_havename || ci_has(g_name, "device")) return 0;
    e = xg_entry(XCK_CS, PDV_GFX_CS, PDGXI_CAT_CS, PDGX_CSBYTES);
    if (!e) return 1;
    xg_setslot(st, g_xslot);
    GS.sncs = GS.fncs = pd_fpeekb(g_xcseg, (pdw)(e + 1));              /* the space's component count (struct pdgx_cs.n) */
    xg_color(st, e, 0);
    return 1;
}

/* ---- sc SC scn SCN ------------------------------------------------------------- */
/* [gfx far-candidate: per operator, once per cs/sc/scn/sh] */
static pdw xg_sc(pdw st, pdw nop)
{
    pdw slot = st ? (pdw)(GS.pad_ >> 4) : (pdw)(GS.pad_ & 15), e, fl;
    if (!slot) return 0;
    e = xc_find(XCK_CS, g_xcsk[slot][0], g_xcsk[slot][1]);
    if (!e || g_nop < nop) return 1;
    fl = pd_fpeekb(g_xcseg, (pdw)(e + 2));
    if (fl & PDGX_F_PATTERN) {                                 /* scn /Name: a pattern (an uncoloured one has components first) */
        if (!g_havename) return 1;
        if (nop == 1 || nop == 3 || nop == 4) col_apply(st, nop);
        e = xg_entry(XCK_PAT, PDV_GFX_PAT, PDGXI_CAT_PAT, PDGX_PATBYTES);
        if (e) xg_setslot(st, g_xslot);
        return 1;
    }
    if (g_havename) { xg_ncx("colour name"); return 1; }
    if (fl & (PDGX_F_RGBLIKE | PDGX_F_CMYKLIKE)) {            /* ICC N=3 / N=4: the device operator */
        col_apply(st, pd_fpeekb(g_xcseg, (pdw)(e + 1)));
        xg_setslot(st, slot);
        return 1;
    }
    xg_color(st, e, nop);
    return 1;
}

/* ---- an image's colour space: its LUT (struct pdgx_lut) in the XC cache, asked of PDXG when it is not there ---- */
/* do_image's hook: g_dg still holds the image's /ColorSpace (a NAME: a resource of the current scope; otherwise
 * PDXG reads the value out of the image dictionary). Answers the XC offset, or 0 (asked for, or noted) */
/* [gfx far-candidate: per image] */
static pdw xg_image_lut(pdw obj, pdw gen, pdw bpc)
{
    pdw e, k1 = (pdw)(((gen << 5) | bpc) ^ 0x4C55u), n = 0;
    e = xc_find(XCK_LUT, obj, k1);
    if (e) return e;
    if (!x_have(PDX_GFX)) { x_need(PDX_GFX); return 0; }
    if (!(e = xg_alloc(XCK_LUT, obj, k1, PDGX_LUTBYTES))) { if (g_xcseg) xg_ncx("colour cache"); return 0; }
    xg_begin(0);
    if (g_dg.kind == PDK_NAME) {                        /* a resource of this scope: "ColorSpace\0Name" */
        while (g_dg.text[n] && n < 32) n++;
        pd_fcopy_out(g_dg.text, g_xb, 128, n);
        pd_fcopy_out("", g_xb, (pdw)(128 + n), 1);
        g_gx.objnum = g_reso; g_gx.gen = g_resg; g_gx.bpc = g_resp == 0xFFFFu ? 0 : g_resp;
        g_gx.flags = (pdw)(PDGXI_RES | PDGXI_CAT_CS); g_gx.tseg = g_xb; g_gx.toff = 128; g_gx.tlen = (pdw)(n + 1);
    } else { g_gx.objnum = obj; g_gx.gen = gen; g_gx.flags = PDGXI_IMGCS; }
    g_gx.kind = bpc;                                    /* the sample depth (PDV_GFX_LUT's input) */
    g_gx.rseg = g_xcseg; g_gx.roff = e; g_gx.rcap = g_xcrcap;
    if (xg_req(PDV_GFX_LUT) != PDE_OK) { xg_nc(); return 0; }
    xc_trim(e, g_gx.rlen);                              /* the header and the tables the space has */
    return e;
}

/* QA R15: a shading paints nothing outside the clip, so one whose clip
 * misses this pass's rows costs no module call at all - SYNTH.PDF asked
 * PDXG for every knob on the page in every band's pass - and one in a
 * small clip is cut into no more bands than the clip has pixels across
 * (PDXG's floor is 4): 32 levels of a 3-pixel knob were 32 STATEs for 9
 * pixels. Answers the steps (0: PDXG's 32), or 0xFFFF: nothing to paint */
#ifdef PD_HOST
extern int pdrh_noshcull;                   /* hosttest PDR_NOSHCULL: the A/B */
#endif
static pdw xg_steps(void)
{
    pdi d;
#ifdef PD_HOST
    if (pdrh_noshcull) return 0;
#endif
    if (GS.cx1 < GS.cx0 || GS.cy1 < GS.cy0 || !span_hit(GS.cy0, GS.cy1, g_gy0, g_gy1) || !span_hit(GS.cx0, GS.cx1, g_vcx0, g_vcx1))
        return 0xFFFFu;
    d = GS.cx1 - GS.cx0;
    if (GS.cy1 - GS.cy0 > d) d = GS.cy1 - GS.cy0;
    return d < 31 ? (pdw)(d + 1) : 0;
}
/* ---- sh: the polygons PDXG answers, each an ordinary fill (a /BBox record is a clip) ------------------------ */
/* the shading `obj gen` (0: the resource of the current name), under the current CTM */
/* [gfx far-candidate: per operator, once per cs/sc/scn/sh] */
static void xg_shade(pdw obj, pdw gen)
{
    pdw n, st = 0, p, np, k, n1;
    static pdb rec[PDGX_REC_HDR];
    static pdi pt[2];
    pdw steps = xg_steps();
    if (steps == 0xFFFFu) return;
    gs_push();
    do {
        xg_begin(obj ? 0 : PDGXI_CAT_SH);
        g_gx.steps = steps;
        g_gx.rseg = g_xb; g_gx.roff = XG_RES; g_gx.rcap = XG_RCAP; g_gx.state = st;
        if (obj) { g_gx.objnum = obj; g_gx.gen = gen; }
        n = xg_req(PDV_GFX_SHADE);
        if (n != PDE_OK && n != PDE_BUSY) { inc_note(PDF_INC_SHADE, "shading"); break; }
        for (p = XG_RES; p < XG_RES + g_gx.rlen && !g_dlfull; p += PDGX_REC_HDR + np * 4) {
            pd_fcopy_in(g_xb, p, rec, PDGX_REC_HDR);
            np = rec[4]; n1 = rec[5];
            path_reset();
            for (k = 0; k < np; k++) {
                pd_fcopy_in(g_xb, (pdw)(p + PDGX_REC_HDR + k * 4), pt, 4);
                if (k == n1 && n1) path_close();
                if (!k || k == n1) path_move(pt[0], pt[1]); else path_line(pt[0], pt[1]);
            }
            path_close();
            if (rec[3] & PDGX_REC_CLIP) clip_apply(0);
            else { col_set(0, rec[0], rec[1], rec[2]); fill_path(0); }
            path_reset();
        }
        st = g_gx.state;
    } while (n == PDE_BUSY && !g_dlfull);
    if (g_gx.kind & 1) inc_note(PDF_INC_LIMIT, "shading size");
    gs_pop();
}
/* [gfx far-candidate: per operator, once per cs/sc/scn/sh] */
static void xg_sh(void)
{
    if (!g_havename || g_mode != PDR_INTERP) return;
    if (xg_steps() == 0xFFFFu) return;          /* nothing on this pass's rows: no PDXG either */
    if (!x_have(PDX_GFX)) { x_need(PDX_GFX); return; }
    xg_shade(0, 0);
}

/* ---- patterns: a fill with a pattern colour -------------------------------------------------------------------- */
static struct {
    pdw on, nfr, ci, cj, ni, nj, e;        /* the running fill: its frame, the tile counters and extent, the cache entry */
    struct pd_mat c;                       /* the current tile's matrix                                              */
    struct pd_fx s[4];                     /* the device step of one tile along i (x y) and along j (x y)            */
    struct pd_fx r[2];                     /* the row's first tile origin                                            */
    struct pd_fx bb[4];                    /* /BBox                                                                  */
    pdw base;                              /* g_gsn before the fill's two saved states                               */
} g_tl;

/* the current state is the fill's S1 (clipped to the path); put the current tile in it */
/* [gfx near: per fill/record/tile (xg_tile_next per form frame)] */
static void xg_tile(void)
{
    pdr_cpy(&GS.ctm, &g_tl.c, sizeof GS.ctm);
    rect_clip_user(&g_tl.bb[0], &g_tl.bb[1], &g_tl.bb[2], &g_tl.bb[3]);
}
/* [gfx near: per fill/record/tile (xg_tile_next per form frame)] */
static void xg_pfill(pdw rule, pdw e)
{
    pdw n, obj = pd_fpeekw(g_xcseg, (pdw)(e + PDGX_PAT_OBJ)), gen = pd_fpeekw(g_xcseg, (pdw)(e + PDGX_PAT_OBJ + 2));
    pdw shading = pd_fpeekb(g_xcseg, e) == 2, paint = pd_fpeekb(g_xcseg, (pdw)(e + 1));
    if (!obj || !x_have(PDX_GFX)) { if (obj) x_need(PDX_GFX); else inc_note(PDF_INC_SHADE, "shading pattern"); return; }
    xg_begin(0);
    g_gx.cseg = g_xcseg; g_gx.coff = e; g_gx.flags = PDGXI_TILES;
    pdr_cpy(&g_gx.ctm, &g_base, sizeof g_base);
    if (xg_req(PDV_GFX_PAT) != PDE_OK) { inc_note(PDF_INC_SHADE, "pattern"); return; }
    gs_push(); clip_apply(rule);                                 /* S0 saved; this is S1: clipped to the path */
    if (shading) { pdr_cpy(&GS.ctm, &g_gx.ctm, sizeof GS.ctm); xg_shade(obj, gen); gs_pop(); return; }
    if (!g_gx.ncomp || !g_gx.kind) {                              /* too many tiles: a flat approximation */
        gs_pop();
        if (paint == 1) col_set(0, 128, 128, 128);
        inc_note(PDF_INC_APPROX, "pattern");
        fill_path(rule);
        return;
    }
    if (g_tl.on || g_nfr + 1 >= PDR_NFORM || g_gsn + 2 > PD_MAXGSTACK || strm_open(obj, gen, 0, 0)) {
        gs_pop(); inc_note(PDF_INC_LIMIT, "pattern"); return;
    }
    g_tl.on = 1; g_tl.base = g_gsn - 1; g_tl.e = e;
    g_tl.ni = g_gx.ncomp; g_tl.nj = g_gx.kind; g_tl.ci = 0; g_tl.cj = 0;
    pdr_cpy(&g_tl.c, &g_gx.ctm, sizeof g_tl.c); pdr_cpy(g_tl.s, g_gx.comp, 16);
    pdr_cpy(g_tl.r, &g_tl.c.e, 8);
    pd_fcopy_in(g_xcseg, (pdw)(e + PDGX_PAT_BBOX), g_tl.bb, 16);
    g_nfr++; g_tl.nfr = g_nfr;                                    /* the frame, as do_form makes it */
    g_fr[g_nfr].isform = 1; g_fr[g_nfr].gsbase = g_tl.base; g_fr[g_nfr].gsover = g_gsover;
    g_fr[g_nfr].resp = g_resp; g_fr[g_nfr].reso = g_reso; g_fr[g_nfr].resg = g_resg;
    g_gsover = 0;
    g_resp = 0xFFFF; g_reso = obj; g_resg = gen;
    GS.pad_ = 0;                                                  /* inside a tile: no pattern; black unless uncoloured */
    if (paint == 1) { col_set(0, 0, 0, 0); col_set(1, 0, 0, 0); }
    gs_push();                                                    /* S1 saved */
    g_gsfloor = g_gsn;
    xg_tile();
}
/* frame_end: the pattern's frame ended; true when it was restarted for the next tile */
/* [gfx near: per fill/record/tile (xg_tile_next per form frame)] */
static pdw xg_tile_next(void)
{
    if (!g_tl.on || g_nfr != g_tl.nfr) return 0;
    if (++g_tl.ci >= g_tl.ni) {
        g_tl.ci = 0;
        if (++g_tl.cj >= g_tl.nj) { g_tl.on = 0; return 0; }
        pdm_fx_add(&g_tl.r[0], &g_tl.r[0], &g_tl.s[2]); pdm_fx_add(&g_tl.r[1], &g_tl.r[1], &g_tl.s[3]);
        pdr_cpy(&g_tl.c.e, g_tl.r, 8);
    } else {
        pdm_fx_add(&g_tl.c.e, &g_tl.c.e, &g_tl.s[0]); pdm_fx_add(&g_tl.c.f, &g_tl.c.f, &g_tl.s[1]);
    }
    while (g_gsn > g_tl.base + 2) { g_gsn--; gs_load(g_gsn); }
    gs_load(g_tl.base + 1);
    if (strm_open(pd_fpeekw(g_xcseg, (pdw)(g_tl.e + PDGX_PAT_OBJ)), pd_fpeekw(g_xcseg, (pdw)(g_tl.e + PDGX_PAT_OBJ + 2)), 0, 0)) { g_tl.on = 0; return 0; }
    xg_tile();
    return 1;
}
/* paint's fill: answers 1 when it also did the stroke (a pattern fill's frame runs after the operator) */
/* [gfx near: per fill/record/tile (xg_tile_next per form frame)] */
static pdw xg_fill(pdw rule, pdw stroke)
{
    pdw slot = GS.pad_ & 15, e;
    if (slot && g_mode == PDR_INTERP && (e = xc_find(XCK_PAT, g_xcsk[slot][0], g_xcsk[slot][1])) != 0) {
        if (stroke) stroke_path();
        xg_pfill(rule, e);
        return 1;
    }
    fill_path(rule);
    return 0;
}
