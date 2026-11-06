/* ============================================================================
 * os8088 - apps/pagedesk/pdri.c      PDREND: the content interpreter
 *
 * #included by pdrend.c. PAGEDESK-SPEC 10.1, 10.6, 11, 20.3.
 *
 * The interpreter pulls tokens from PDENG's content lexer (PDV_ENG_TOKEN)
 * and executes the operators of the supported subset. It draws nothing
 * itself: painting operators append display-list records (pdrd.c); in the
 * text-map mode (PDR_TEXT) the same code instead appends pdtm_glyph records.
 * ==========================================================================*/

/* ---- the operand stack ---------------------------------------------------- */
static struct pd_fx g_opn[PDR_NOPS];
static pdw g_nop;
static char g_name[PDR_NNAME];
static pdw g_havename;
static pdb g_str[PDR_NSTR];
static pdw g_slen, g_smore;
static pdw g_arrd, g_dictd, g_arr0, g_tjlive, g_tjany;
static struct pdxb_token g_tk;

static void op_clear(void)
{
    g_nop = 0; g_havename = 0; g_slen = 0; g_smore = 0;
    g_arrd = 0; g_arr0 = 0;
}

/* ---- streams: a stack of frames (page contents, then nested Forms) ------- */
struct pdr_frame {
    pdw isform, gsbase;             /* a Form frame; the gs index it started at */
    pdw resp, reso, resg;           /* resource context to restore               */
    pdw cidx;                       /* /Contents element (page frame)             */
    pdw pad0, pad1, pad2;           /* 16 bytes: an index is a shift              */
};
static struct pdr_frame g_fr[PDR_NFORM];
static pdw g_nfr;
static pdw g_resp, g_reso, g_resg;  /* current resource context                  */
static pdw g_page;
static pdw g_idone;                 /* interpretation finished                    */
static pdw g_ierr;                  /* a stream error stopped it                  */

static struct pdxb_strm g_sm;
static pdw strm_open(pdw obj, pdw gen, pdw page, pdw index)
{
    pdr_zero(&g_sm, sizeof g_sm);
    g_sm.objnum = obj; g_sm.gen = gen; g_sm.page = page; g_sm.index = index;
    return eng_call(PDV_ENG_STRM_OPEN, &g_sm, sizeof g_sm, 0, 0, 0);
}
static void strm_close(void) { pdr_eng(PDV_ENG_STRM_CLOSE, 0, 0, 0); }

/* the next content token into g_tk; returns 0 or a PDE code */
static pdw tok_next(void)
{
    pdw err = pdr_eng(PDV_ENG_TOKEN, 0, 0, 0);
    if (err) return err;
    pd_fcopy_in(g_xb, 0, &g_tk, 8);
    if (g_tk.len > 255) g_tk.len = 255;
    if (g_tk.len && (g_tk.kind == PDT_NAME || g_tk.kind == PDT_OP || g_tk.kind == PDT_STR))
        pd_fcopy_in(g_xb, 8, g_tk.text, g_tk.len);
    g_tk.text[g_tk.len] = 0;
    g_tk.more = g_tk.kind == PDT_STR ? pd_fpeekw(g_xb, 264) : 0;
    return 0;
}

/* ---- resources ------------------------------------------------------------ */
static struct pdxb_reslookup g_rl;
static pdw res_lookup(const char *cat, const char *name)
{
    pdr_zero(&g_rl, sizeof g_rl);
    g_rl.page = g_resp; g_rl.resobj = g_reso; g_rl.resgen = g_resg;
    pdr_scpy(g_rl.category, cat, 16);
    pdr_scpy(g_rl.name, name, 32);
    return eng_call(PDV_ENG_RES_LOOKUP, &g_rl, sizeof g_rl, 0, 0, 0);
}

/* ---- colour --------------------------------------------------------------- */
static void col_set(pdw stroke, pdw r, pdw g, pdw b)
{
    pdw lum = (pdw)((r * 77u + g * 150u + b * 29u) >> 8);
    pdw c4 = rgb_c4(r, g, b);
    if (stroke) { GS.slum = (pdb)lum; GS.sc4 = (pdb)c4; }
    else        { GS.flum = (pdb)lum; GS.fc4 = (pdb)c4; }
}
static void col_apply(pdw stroke, pdw nop)
{
    pdw a, b, c, k, base = g_nop - nop;
    if (nop == 1) { a = fx_u8(&g_opn[base]); col_set(stroke, a, a, a); }
    else if (nop == 3) col_set(stroke, fx_u8(&g_opn[base]), fx_u8(&g_opn[base + 1]), fx_u8(&g_opn[base + 2]));
    else if (nop == 4) {
        a = fx_u8(&g_opn[base]); b = fx_u8(&g_opn[base + 1]);
        c = fx_u8(&g_opn[base + 2]); k = fx_u8(&g_opn[base + 3]);
        col_set(stroke, (pdw)(((255 - a) * (255 - k)) / 255u), (pdw)(((255 - b) * (255 - k)) / 255u),
                (pdw)(((255 - c) * (255 - k)) / 255u));
    }
}

/* ---- gstate ---------------------------------------------------------------- */
static pdw g_gsfloor;                /* Q cannot pop below this                   */
static void gs_push(void)
{
    if (g_gsn + 1 > PD_MAXGSTACK) { inc_note(PDF_INC_LIMIT, "graphics state depth"); return; }
    pdr_cpy(&g_gs[g_gsn + 1], &g_gs[g_gsn], sizeof(struct pdr_gs));
    gs_set(g_gsn + 1);
}
static void gs_pop(void)
{
    if (g_gsn > g_gsfloor) gs_set(g_gsn - 1);
}

static pdw g_pagex0, g_pagey0, g_pagex1, g_pagey1;
static void gs_init(void)
{
    struct pdr_gs *g;
    pdr_zero(g_gs, sizeof g_gs);
    gs_set(0); g_gsfloor = 0;
    g = &g_gs[0];
    pdr_cpy(&g->ctm, &g_base, sizeof g_base);
    pdm_fx_int(&g->lw, 1); pdm_fx_int(&g->miter, 10); pdm_fx_int(&g->tz, 100);
    g->font = 255;
    g->flum = g->slum = 0; g->fc4 = g->sc4 = 0;
    g->cx0 = g_vcx0 > g_pgx0 ? g_vcx0 : g_pgx0;
    g->cy0 = g_vcy0 > g_pgy0 ? g_vcy0 : g_pgy0;
    g->cx1 = g_vcx1 < g_pgx1 ? g_vcx1 : g_pgx1;
    g->cy1 = g_vcy1 < g_pgy1 ? g_vcy1 : g_pgy1;
    g_lstv = 0;
}

/* ============================================================================
 * TEXT
 * ==========================================================================*/
static struct pd_mat g_tm, g_tlm, g_M, g_tr;
static struct pd_fx g_penx, g_peny, g_fsk, g_thk, g_tadv, g_xx, g_yy;
static pdw g_tdirty;
static pdw g_inbt;
static pdw g_em;                    /* device em height of the current font, px  */
static pdw g_tupright;
static pdb g_tmchan;

/* the textmap output ring lives in the DL claim */
static pdw g_tmn, g_tmrd, g_tmwr, g_tmrun, g_tmfirst;
static pdw g_tmoverflow;

static void text_ident(void)
{
    pdm_mat_ident(&g_tm); pdm_mat_ident(&g_tlm);
    g_tdirty = 1;
}
static struct pd_fx fk;
static void fs_changed(void)
{
    pdm_fx_int(&fk, 1000);
    pdm_fx_div(&g_fsk, &GS.fs, &fk);
    pdm_fx_int(&fk, 100);
    pdm_fx_div(&g_thk, &GS.tz, &fk);
    g_tdirty = 1;
}

/* M = Tm x CTM, the pen at (0, rise), and the device em height */
static void text_sync(void)
{
    pdi h;
    pdm_mat_mul(&g_M, &g_tm, &GS.ctm);
    pdm_fx_int(&g_xx, 0);
    pdr_cpy(&g_yy, &GS.rise, 4);
    pdm_mat_apply(&g_xx, &g_yy, &g_M);
    pdr_cpy(&g_penx, &g_xx, 4); pdr_cpy(&g_peny, &g_yy, 4);
    /* em height: fs * |(M.c, M.d)| / 4 pixels */
    pdr_cpy(&ta, &g_M.c, 4); pdr_cpy(&tb, &g_M.d, 4);
    if (ta.hi < 0) pdm_fx_neg(&ta, &ta);
    if (tb.hi < 0) pdm_fx_neg(&tb, &tb);
    if (ta.hi < 120 && tb.hi < 120) {
        pdi fa = (pdi)(ta.hi * 256 + (ta.lo >> 8)), fb = (pdi)(tb.hi * 256 + (tb.lo >> 8));
        h = pdr_hypot(fa, fb);                          /* qpx per text unit x256 */
        pdm_fx_int(&ta, 0); ta.hi = (pdi)(h >> 8); ta.lo = (pdw)((h & 255) << 8);
    } else { h = pdr_hypot(ta.hi, tb.hi); pdm_fx_int(&ta, h); }
    pdr_cpy(&tb, &GS.fs, 4);
    if (tb.hi < 0) pdm_fx_neg(&tb, &tb);
    pdm_fx_mul(&ta, &ta, &tb);
    h = pdm_fx_round(&ta);
    g_em = h <= 0 ? 0 : (pdw)((h + 2) >> 2);
    /* upright? (b and c negligible against a and d) */
    {
        pdi b = (pdi)pdr_abs(pdm_fx_round(&g_M.b)), a = (pdi)pdr_abs(pdm_fx_round(&g_M.a));
        pdi c = (pdi)pdr_abs(pdm_fx_round(&g_M.c)), d = (pdi)pdr_abs(pdm_fx_round(&g_M.d));
        g_tupright = (pdw)((b * 4 <= a || (a == 0 && b == 0)) && (c * 4 <= d || (c == 0 && d == 0)));
    }
    g_tdirty = 0;
}

/* ---- glyph runs ------------------------------------------------------------ */
static pdw g_rn;                    /* glyphs in the open run                     */
static pdb g_rfam, g_rsty, g_rpx, g_ropen;
static pdb g_rbody[6 + PDR_NRUN * 5];
static pdw g_rasc, g_rdesc, g_rrot;
/* the text direction as a quadrant: 0 right, 1 down (90 cw), 2 left, 3 up */
static pdw text_quadrant(void)
{
    pdi a = pdm_fx_round(&g_M.a), b = pdm_fx_round(&g_M.b);
    if (pdr_abs(a) >= pdr_abs(b)) return a >= 0 ? 0 : 2;
    return b > 0 ? 1 : 3;
}

static void run_flush(void)
{
    pdw i;
    pdi y0 = 32000, y1 = -32000, t;
    if (!g_rn) { g_ropen = 0; return; }
    for (i = 0; i < g_rn; i++) {
        t = (pdi)(r16(g_rbody + 6 + i * 5 + 3) - (pdi)g_rasc); if (t < y0) y0 = t;
        t = (pdi)(r16(g_rbody + 6 + i * 5 + 3) + (pdi)g_rdesc); if (t > y1) y1 = t;
    }
    g_rbody[0] = g_rfam; g_rbody[1] = g_rsty; g_rbody[2] = g_rpx; g_rbody[3] = (pdb)g_rrot;
    w16(g_rbody + 4, (pdi)g_rn);
    if (y0 < g_gy0) y0 = g_gy0;
    if (y1 > g_gy1) y1 = g_gy1;
    emit_state(PK_TEXT);
    hdr_set(DLR_GLYPHS, 0, DLH + 6 + g_rn * 5, y0, y1);
    dl_put(g_hdr, DLH, g_rbody, 6 + g_rn * 5);
    g_rn = 0; g_ropen = 0;
}

/* choose the strike for the current font and size; false = nothing to draw */
static pdw run_begin(void)
{
    struct pdr_font *f;
    pdw idx, sty;
    if (g_ropen) return 1;
    if (GS.font == 255) return 0;
    f = &g_fn[GS.font];
    if (!f->ok) return 0;
    sty = f->sty;
    idx = strike_pick(f->fam, sty, g_em, &g_synth);
    if (idx == 0xFFFF) return 0;
    strike_load(idx);
    g_rfam = f->fam; g_rsty = (pdb)sty; g_rpx = (pdb)g_ssize;
    g_rrot = text_quadrant();
    g_rasc = g_sasc; g_rdesc = g_sdesc;
    g_rn = 0; g_ropen = 1;
    return 1;
}

/* text too small for a bitmap (em < 5 px) is GREEKED: one hairline per show
 * operator across its extent at 0.3 em above the baseline, in half the ink */
static pdi g_gkx0, g_gkx1, g_gky;
static pdw g_gkhave;
static void greek_flush(void)
{
    pdb sl, sc, fl, fc;
    pdw l;
    if (!g_gkhave) return;
    g_gkhave = 0;
    if (g_mode != PDR_INTERP) return;
    fl = GS.flum; fc = GS.fc4;
    l = (pdw)((GS.flum + 255u) >> 1);
    GS.flum = (pdb)l; GS.fc4 = rgb_c4(l, l, l);
    emit_rect_px(g_gkx0, g_gky, g_gkx1, g_gky, PK_FILL);
    GS.flum = fl; GS.fc4 = fc;
    (void)sl; (void)sc;
}
static void glyph_emit(pdw code, pdw lat, pdi px, pdi py)
{
    if (g_mode == PDR_TEXT) {
        /* a text-map record in page space: the pen is in pt16 (the base is x16) */
        static pdb rec[12];
        static pdi x0, x1, y0, y1, fa, ad, de, ex;
        pdi ax = (pdi)pdm_fx_round(&g_penx), ay = (pdi)pdm_fx_round(&g_peny);
        (void)px; (void)py;
        /* glyph box: the advance wide, ascent .75 em above, descent .25 below,
         * in text space, then the bounding box of the transformed quad       */
        {
            static struct pd_fx wd;
            pdm_fx_int(&wd, (pdi)font_width(GS.font, code));
            pdm_fx_mul(&wd, &wd, &g_fsk);
            pdm_fx_mul(&wd, &wd, &g_thk);
            /* corners offset by the advance vector (M.a, M.b) and the up vector (M.c, M.d)*0.75 fs */
            pdm_fx_mul(&ta, &wd, &g_M.a); ex = pdm_fx_round(&ta);
            fa = ex;
            pdm_fx_mul(&ta, &wd, &g_M.b); ad = pdm_fx_round(&ta);
            pdr_cpy(&tb, &GS.fs, 4);
            pdm_fx_mul(&ta, &tb, &g_M.c); de = pdm_fx_round(&ta);   /* up x */
            pdm_fx_mul(&ta, &tb, &g_M.d); ex = pdm_fx_round(&ta);   /* up y */
            x0 = ax; x1 = ax; y0 = ay; y1 = ay;
            {
                pdi ux = (pdi)((de * 3) >> 2), uy = (pdi)((ex * 3) >> 2);
                pdi dx = (pdi)(de >> 2), dy = (pdi)(ex >> 2);
                pdi p0x = ax, p0y = ay;
                pdi p1x = (pdi)(ax + fa), p1y = (pdi)(ay + ad);
                pdi p2x = (pdi)(p1x + ux), p2y = (pdi)(p1y + uy);
                pdi p3x = (pdi)(p0x + ux), p3y = (pdi)(p0y + uy);
                pdi p4x = (pdi)(p0x - dx), p4y = (pdi)(p0y - dy);
                pdi p5x = (pdi)(p1x - dx), p5y = (pdi)(p1y - dy);
                x0 = pdr_min(pdr_min(pdr_min(p0x, p1x), pdr_min(p2x, p3x)), pdr_min(p4x, p5x));
                x1 = pdr_max(pdr_max(pdr_max(p0x, p1x), pdr_max(p2x, p3x)), pdr_max(p4x, p5x));
                y0 = pdr_min(pdr_min(pdr_min(p0y, p1y), pdr_min(p2y, p3y)), pdr_min(p4y, p5y));
                y1 = pdr_max(pdr_max(pdr_max(p0y, p1y), pdr_max(p2y, p3y)), pdr_max(p4y, p5y));
            }
        }
        if (g_tmoverflow || ((g_tmwr + 1) << 3) + ((g_tmwr + 1) << 2) > g_dlcap) { g_tmoverflow = 1; return; }
        rec[0] = (pdb)lat; rec[1] = g_tmfirst ? 1 : 0;
        w16(rec + 2, x0); w16(rec + 4, y0); w16(rec + 6, x1); w16(rec + 8, y1);
        w16(rec + 10, (pdi)g_tmrun);
        pd_fcopy_out(rec, g_dls, (pdw)((g_tmwr << 3) + (g_tmwr << 2)), 12);
        g_tmwr++; g_tmrun++; g_tmfirst = 0;
        return;
    }
    if (!g_ropen && !run_begin()) return;
    {
        pdi top = (pdi)(py - (pdi)g_rasc), bot = (pdi)(py + (pdi)g_rdesc);
        if (bot < g_gy0 || top > g_gy1) return;
        if (px < (pdi)(g_vcx0 - 2 * (pdi)g_rpx) || px > g_vcx1) return;
        if (px > GS.cx1 || (pdi)(px + 2 * (pdi)g_rpx) < GS.cx0 || bot < GS.cy0 || top > GS.cy1) return;
    }
    if (g_rn >= PDR_NRUN) { run_flush(); if (!run_begin()) return; }
    g_rbody[6 + g_rn * 5] = (pdb)lat; w16(g_rbody + 6 + g_rn * 5 + 1, px); w16(g_rbody + 6 + g_rn * 5 + 3, py); g_rn++;
}

/* one glyph: draw it (unless invisible), advance the pen */
static void text_glyph(pdw code)
{
    pdw lat, w;
    struct pdr_font *f;
    pdi px, py;
    if (GS.font == 255) { w = 556; lat = code; }
    else {
        f = &g_fn[GS.font];
        lat = code >= 32 ? f->enc[code - 32] : 0;
        w = font_width(GS.font, code);
    }
    if (GS.tr != 3 && lat >= 32 && g_em >= 1) {
        px = q2p(pdr_clampq(pdm_fx_round(&g_penx)));
        py = q2p(pdr_clampq(pdm_fx_round(&g_peny)));
        if (g_mode == PDR_TEXT || g_em >= 5) glyph_emit(code, lat, px, py);
        else {
            if (!g_gkhave) { g_gkhave = 1; g_gkx0 = px; g_gky = (pdi)(py - (pdi)((g_em + 2) / 3)); }
            g_gkx1 = (pdi)(px + (pdi)((w * g_em) / 1000));
        }
    } else if (g_mode == PDR_TEXT && lat >= 32) glyph_emit(code, lat, 0, 0);
    /* advance: ((w/1000) fs + Tc + Tw if space) * Th */
    pdm_fx_int(&t0, (pdi)w);
    pdm_fx_mul(&t0, &t0, &g_fsk);
    pdm_fx_add(&t0, &t0, &GS.tc);
    if (code == 32) pdm_fx_add(&t0, &t0, &GS.tw);
    if (GS.tz.hi != 100 || GS.tz.lo) pdm_fx_mul(&t0, &t0, &g_thk);
    pdm_fx_add(&g_tadv, &g_tadv, &t0);
    pdm_fx_mul(&t1, &t0, &g_M.a); pdm_fx_add(&g_penx, &g_penx, &t1);
    pdm_fx_mul(&t1, &t0, &g_M.b); pdm_fx_add(&g_peny, &g_peny, &t1);
}

static void text_begin_show(void)
{
    if (g_tdirty) text_sync();
    g_gkhave = 0;
    pdm_fx_int(&g_tadv, 0);
    g_tmfirst = 1;
}
static void text_end_show(void)
{
    if (g_mode != PDR_TEXT) { run_flush(); greek_flush(); }
    /* Tm advances by the text-space displacement */
    pdm_fx_mul(&t0, &g_tadv, &g_tm.a); pdm_fx_add(&g_tm.e, &g_tm.e, &t0);
    pdm_fx_mul(&t0, &g_tadv, &g_tm.b); pdm_fx_add(&g_tm.f, &g_tm.f, &t0);
    g_tdirty = 1;
}
static void text_show(const pdb *s, pdw n)
{
    pdw i;
    for (i = 0; i < n; i++) text_glyph(s[i]);
}
/* a TJ number: move left by n/1000 em */
static void text_adjust(const struct pd_fx *n)
{
    pdm_fx_int(&fk, 1000);
    pdm_fx_div(&t0, n, &fk);
    pdm_fx_mul(&t0, &t0, &GS.fs);
    pdm_fx_neg(&t0, &t0);
    pdm_fx_mul(&t0, &t0, &g_thk);
    pdm_fx_add(&g_tadv, &g_tadv, &t0);
    pdm_fx_mul(&t1, &t0, &g_M.a); pdm_fx_add(&g_penx, &g_penx, &t1);
    pdm_fx_mul(&t1, &t0, &g_M.b); pdm_fx_add(&g_peny, &g_peny, &t1);
}
static void text_td(const struct pd_fx *tx, const struct pd_fx *ty)
{
    static struct pd_mat tr;
    pdm_mat_ident(&tr);
    pdr_cpy(&tr.e, tx, 4); pdr_cpy(&tr.f, ty, 4);
    pdm_mat_mul(&g_tlm, &tr, &g_tlm);
    pdr_cpy(&g_tm, &g_tlm, sizeof g_tm);
    g_tdirty = 1;
}

/* ============================================================================
 * XOBJECTS
 * ==========================================================================*/
static pdw g_fobj, g_fgen;
static struct pd_mat g_fm;

static void rect_clip_user(const struct pd_fx *x0, const struct pd_fx *y0,
                           const struct pd_fx *x1, const struct pd_fx *y1)
{
    static struct pd_fx ax, ay;
    pdw save = g_nv;
    (void)save;
    path_reset();
    to_dev(x0, y0); path_move(g_dx, g_dy);
    to_dev(x1, y0); path_line(g_dx, g_dy);
    to_dev(x1, y1); path_line(g_dx, g_dy);
    to_dev(x0, y1); path_line(g_dx, g_dy);
    path_close();
    clip_apply(0);
    path_reset();
    (void)ax; (void)ay;
}

/* read a numeric array element of an object's key into out; 0 on success */
static pdw dict_num(pdw obj, pdw gen, const char *k0, pdw idx, struct pd_fx *out)
{
    pdw e;
    if (idx == 0xFFFF) e = dict_get(obj, gen, k0, 0, 0);
    else e = dict_get(obj, gen, k0, idx_key(idx), 0);
    if (e) return e;
    if (g_dg.kind != PDK_INT && g_dg.kind != PDK_REAL && g_dg.kind != PDK_BOOL) return PDE_SYNTAX;
    pdr_cpy(out, &g_dg.num, 4);
    return 0;
}

static void do_form(pdw obj, pdw gen)
{
    static struct pd_mat m;
    static struct pd_fx bx0, by0, bx1, by1;
    pdw i, hasbox = 1, e, ressave_p, ressave_o, ressave_g, hasres;
    if (g_nfr + 1 >= PDR_NFORM) { inc_note(PDF_INC_LIMIT, "form nesting"); return; }
    pdm_mat_ident(&m);
    e = dict_get(obj, gen, "Matrix", 0, 0);
    if (!e && g_dg.kind == PDK_ARR && g_dg.count == 6) {
        struct pd_fx *p = &m.a;
        for (i = 0; i < 6; i++) if (dict_num(obj, gen, "Matrix", i, p + i)) { pdm_mat_ident(&m); break; }
    }
    for (i = 0; i < 4; i++) {
        struct pd_fx *p = i == 0 ? &bx0 : i == 1 ? &by0 : i == 2 ? &bx1 : &by1;
        if (dict_num(obj, gen, "BBox", i, p)) { hasbox = 0; break; }
    }
    hasres = (dict_get(obj, gen, "Resources", 0, 0) == 0 && g_dg.kind != PDK_NULL);
    if (strm_open(obj, gen, 0, 0)) { inc_note(PDF_INC_FILTER, "form stream"); return; }
    ressave_p = g_resp; ressave_o = g_reso; ressave_g = g_resg;
    g_nfr++;
    g_fr[g_nfr].isform = 1; g_fr[g_nfr].gsbase = g_gsn;
    g_fr[g_nfr].resp = ressave_p; g_fr[g_nfr].reso = ressave_o; g_fr[g_nfr].resg = ressave_g;
    if (hasres) { g_resp = 0xFFFF; g_reso = obj; g_resg = gen; }
    gs_push();
    g_gsfloor = g_gsn;
    pdm_mat_mul(&GS.ctm, &m, &GS.ctm);
    if (hasbox) rect_clip_user(&bx0, &by0, &bx1, &by1);
}

/* ---- images ---------------------------------------------------------------- */
static void do_image(pdw obj, pdw gen)
{
    static pdb body[20];
    static struct pd_fx w, h, bpc, one, zero, dec0;
    pdw ncomp = 1, stencil = 0, inv = 0, flags = 0, bpcv;
    pdi x00, y00, x10, y10, x01, y01, xl, xr, yt, yb, t;
    static pdi py0, py1, px0, px1;
    pdi pa, pb, pc, pd;
    if (dict_num(obj, gen, "Width", 0xFFFF, &w) || dict_num(obj, gen, "Height", 0xFFFF, &h)) return;
    if (dict_get(obj, gen, "ImageMask", 0, 0) == 0 && g_dg.kind == PDK_BOOL && (g_dg.num.hi || g_dg.num.lo)) stencil = 1;
    bpcv = 1;
    if (!stencil) {
        if (dict_num(obj, gen, "BitsPerComponent", 0xFFFF, &bpc)) { inc_note(PDF_INC_IMAGE, "image bit depth"); return; }
        bpcv = (pdw)pdm_fx_floor(&bpc);
        if (dict_get(obj, gen, "ColorSpace", 0, 0)) { inc_note(PDF_INC_COLOUR, "image colour space"); return; }
        if (g_dg.kind == PDK_NAME) {
            if (ci_has(g_dg.text, "rgb")) ncomp = 3;
            else if (ci_has(g_dg.text, "gray")) ncomp = 1;
            else { inc_note(PDF_INC_COLOUR, "image colour space"); return; }
        } else { inc_note(PDF_INC_COLOUR, "image colour space"); return; }
        if (!(bpcv == 8 || (bpcv == 1 && ncomp == 1))) { inc_note(PDF_INC_IMAGE, "image bit depth"); return; }
    }
    if (dict_get(obj, gen, "SMask", 0, 0) == 0 && g_dg.kind != PDK_NULL) inc_note(PDF_INC_TRANSP, "soft mask");
    if (dict_get(obj, gen, "Mask", 0, 0) == 0 && g_dg.kind != PDK_NULL) inc_note(PDF_INC_TRANSP, "image mask");
    if (dict_num(obj, gen, "Decode", 0, &dec0) == 0 && dec0.hi >= 1) inv = 1;
    pdm_fx_int(&zero, 0); pdm_fx_int(&one, 1);
    to_dev(&zero, &zero); x00 = g_dx; y00 = g_dy;
    to_dev(&one, &zero);  x10 = g_dx; y10 = g_dy;
    to_dev(&zero, &one);  x01 = g_dx; y01 = g_dy;
    pa = (pdi)pdr_abs((pdi)(y10 - y00)); pb = (pdi)pdr_abs((pdi)(x01 - x00));
    pc = (pdi)pdr_abs((pdi)(x10 - x00)); pd = (pdi)pdr_abs((pdi)(y01 - y00));
    if (pa > 1 || pb > 1) {
        /* rotated by 90 degrees (or more): not drawn in this release */
        inc_note(PDF_INC_IMAGE, "rotated image");
        (void)pc; (void)pd;
        return;
    }
    xl = pdr_min(x00, x10); xr = pdr_max(x00, x10);
    yt = pdr_min(y00, y01); yb = pdr_max(y00, y01);
    if (x10 < x00) flags |= 4;                 /* mirrored left-right           */
    if (y01 > y00) flags |= 8;                 /* upside down                   */
    if (flags & 8) { inc_note(PDF_INC_IMAGE, "flipped image"); return; }
    if (stencil) flags |= 1;
    if (inv) flags |= 2;
    rect_px(xl, yt, xr, yb, &px0, &py0, &px1, &py1);
    if (px1 < px0 || py1 < py0) return;
    t = py1; (void)t;
    if (py1 < GS.cy0 || py0 > GS.cy1 || px1 < GS.cx0 || px0 > GS.cx1) return;
    if (py1 < g_gy0 || py0 > g_gy1 || px1 < g_vcx0 || px0 > g_vcx1) return;
    if (g_mode != PDR_INTERP) return;
    emit_state(PK_TEXT);
    w16(body, xl); w16(body + 2, yt); w16(body + 4, xr); w16(body + 6, yb);
    w16(body + 8, (pdi)pdm_fx_floor(&w)); w16(body + 10, (pdi)pdm_fx_floor(&h));
    w16(body + 12, (pdi)obj); w16(body + 14, (pdi)gen);
    body[16] = (pdb)bpcv; body[17] = (pdb)ncomp; body[18] = (pdb)flags; body[19] = 0;
    hdr_set(DLR_IMAGE, 0, DLH + 20, py0 < GS.cy0 ? GS.cy0 : py0, py1 > GS.cy1 ? GS.cy1 : py1);
    dl_put(g_hdr, DLH, body, 20);
}

static void do_xobject(void)
{
    pdw obj, gen;
    if (!g_havename) return;
    if (res_lookup("XObject", g_name)) { inc_note(PDF_INC_OPER, "missing XObject"); return; }
    obj = g_rl.objnum; gen = g_rl.gen;
    if (!obj) { inc_note(PDF_INC_OPER, "direct XObject"); return; }
    if (dict_get(obj, gen, "Subtype", 0, 0)) return;
    if (ci_has(g_dg.text, "form")) do_form(obj, gen);
    else if (ci_has(g_dg.text, "image")) do_image(obj, gen);
    else inc_note(PDF_INC_OPER, "XObject subtype");
}

/* ============================================================================
 * THE OPERATOR TABLE
 * ==========================================================================*/
static struct pd_fx *opn(pdw back)    /* the operand `back` from the end (1 = last) */
{
    static struct pd_fx zero;
    if (g_nop < back) { pdm_fx_int(&zero, 0); return &zero; }
    return &g_opn[g_nop - back];
}

static void paint(pdw fill, pdw rule, pdw stroke, pdw close)
{
    if (close) path_close();
    if (g_plimit) { inc_note(PDF_INC_LIMIT, "path size"); g_plimit = 0; }
    if (g_mode == PDR_INTERP) {
        if (fill) fill_path(rule);
        if (stroke) stroke_path();
    }
    if (g_pclip) clip_apply(g_pclip - 1);
    path_reset();
}

static void op_re(void)
{
    static struct pd_fx x, y, x2, y2;
    pdr_cpy(&x, opn(4), 4); pdr_cpy(&y, opn(3), 4);
    pdm_fx_add(&x2, &x, opn(2)); pdm_fx_add(&y2, &y, opn(1));
    to_dev(&x, &y);   path_move(g_dx, g_dy);
    to_dev(&x2, &y);  path_line(g_dx, g_dy);
    to_dev(&x2, &y2); path_line(g_dx, g_dy);
    to_dev(&x, &y2);  path_line(g_dx, g_dy);
    path_close();
}

/* returns 1 when the operator was the inline-image data marker */
static void exec_op(const char *s, pdw n)
{
    pdw k = (pdw)((pdb)s[0] | (n > 1 ? ((pdb)s[1] << 8) : 0));
    pdw c2 = n > 2 ? (pdb)s[2] : 0;
    static struct pd_mat m;
    g_ops++;
    if (n == 1) {
        switch (s[0]) {
        case 'q': gs_push(); break;
        case 'Q': gs_pop(); break;
        case 'w': pdr_cpy(&GS.lw, opn(1), 4); break;
        case 'J': GS.cap = (pdb)pdm_fx_floor(opn(1)); break;
        case 'j': GS.join = (pdb)pdm_fx_floor(opn(1)); break;
        case 'M': pdr_cpy(&GS.miter, opn(1), 4); break;
        case 'd': {
            pdw i, cnt = g_nop > 1 ? g_nop - 1 : 0;      /* operands are the array then phase */
            if (cnt > 4) cnt = 4;
            GS.ndash = 0;
            for (i = 0; i < cnt; i++) {
                pdr_cpy(&GS.dash[i], &g_opn[g_arr0 + i], 4);
            }
            if (cnt) {
                pdw nz = 0;
                for (i = 0; i < cnt; i++) if (GS.dash[i].hi || GS.dash[i].lo) nz = 1;
                GS.ndash = nz ? (pdb)cnt : 0;
                if (nz && cnt & 1) { pdr_cpy(&GS.dash[cnt], &GS.dash[0], 4); GS.ndash = (pdb)(cnt + 1 > 4 ? 4 : cnt + 1); }
            }
            pdr_cpy(&GS.dphase, opn(1), 4);
            break; }
        case 'i': break;
        case 'm': { to_dev(opn(2), opn(1)); path_move(g_dx, g_dy); break; }
        case 'l': { to_dev(opn(2), opn(1)); path_line(g_dx, g_dy); break; }
        case 'c': {
            pdi x1, y1, x2, y2;
            to_dev(opn(6), opn(5)); x1 = g_dx; y1 = g_dy;
            to_dev(opn(4), opn(3)); x2 = g_dx; y2 = g_dy;
            to_dev(opn(2), opn(1));
            path_curve(x1, y1, x2, y2, g_dx, g_dy);
            break; }
        case 'v': {
            pdi x2, y2;
            to_dev(opn(4), opn(3)); x2 = g_dx; y2 = g_dy;
            to_dev(opn(2), opn(1));
            path_curve(g_cpx, g_cpy, x2, y2, g_dx, g_dy);
            break; }
        case 'y': {
            pdi x1, y1;
            to_dev(opn(4), opn(3)); x1 = g_dx; y1 = g_dy;
            to_dev(opn(2), opn(1));
            path_curve(x1, y1, g_dx, g_dy, g_dx, g_dy);
            break; }
        case 'h': path_close(); break;
        case 'S': paint(0, 0, 1, 0); break;
        case 's': paint(0, 0, 1, 1); break;
        case 'f': case 'F': paint(1, 0, 0, 0); break;
        case 'B': paint(1, 0, 1, 0); break;
        case 'b': paint(1, 0, 1, 1); break;
        case 'n': paint(0, 0, 0, 0); break;
        case 'W': g_pclip = 1; break;
        case 'G': col_apply(1, 1); GS.sncs = 1; break;
        case 'g': col_apply(0, 1); GS.fncs = 1; break;
        case 'K': col_apply(1, 4); GS.sncs = 4; break;
        case 'k': col_apply(0, 4); GS.fncs = 4; break;
        case '\'': goto showq;
        case '"': goto showdq;
        default: goto unknown;
        }
        return;
    }
    if (n == 2) {
        switch (k) {
        case 'c' | ('m' << 8):
            pdr_cpy(&m.a, opn(6), 4); pdr_cpy(&m.b, opn(5), 4); pdr_cpy(&m.c, opn(4), 4);
            pdr_cpy(&m.d, opn(3), 4); pdr_cpy(&m.e, opn(2), 4); pdr_cpy(&m.f, opn(1), 4);
            pdm_mat_mul(&GS.ctm, &m, &GS.ctm);
            break;
        case 'r' | ('e' << 8): op_re(); break;
        case 'g' | ('s' << 8): case 'r' | ('i' << 8): break;
        case 'f' | ('*' << 8): paint(1, 1, 0, 0); break;
        case 'B' | ('*' << 8): paint(1, 1, 1, 0); break;
        case 'b' | ('*' << 8): paint(1, 1, 1, 1); break;
        case 'W' | ('*' << 8): g_pclip = 2; break;
        case 'R' | ('G' << 8): col_apply(1, 3); GS.sncs = 3; break;
        case 'r' | ('g' << 8): col_apply(0, 3); GS.fncs = 3; break;
        case 'C' | ('S' << 8): case 'c' | ('s' << 8): {
            pdw st = (s[0] == 'C');
            if (g_havename && ci_has(g_name, "devicegray")) { if (st) GS.sncs = 1; else GS.fncs = 1; }
            else if (g_havename && ci_has(g_name, "devicergb")) { if (st) GS.sncs = 3; else GS.fncs = 3; }
            else if (g_havename && ci_has(g_name, "devicecmyk")) { if (st) GS.sncs = 4; else GS.fncs = 4; }
            else inc_note(PDF_INC_COLOUR, "colour space");
            if (st) col_set(1, 0, 0, 0); else col_set(0, 0, 0, 0);
            break; }
        case 'S' | ('C' << 8): case 's' | ('c' << 8): {
            pdw st = (s[0] == 'S');
            if (g_nop == 1 || g_nop == 3 || g_nop == 4) col_apply(st, g_nop);
            else inc_note(PDF_INC_COLOUR, "colour space");
            break; }
        case 'B' | ('T' << 8): text_ident(); g_inbt = 1; break;
        case 'E' | ('T' << 8): g_inbt = 0; break;
        case 'T' | ('c' << 8): pdr_cpy(&GS.tc, opn(1), 4); break;
        case 'T' | ('w' << 8): pdr_cpy(&GS.tw, opn(1), 4); break;
        case 'T' | ('z' << 8): pdr_cpy(&GS.tz, opn(1), 4); fs_changed(); break;
        case 'T' | ('L' << 8): pdr_cpy(&GS.tl, opn(1), 4); break;
        case 'T' | ('r' << 8): GS.tr = (pdb)pdm_fx_floor(opn(1));
            if (GS.tr >= 4) inc_note(PDF_INC_OPER, "text clip mode"); break;
        case 'T' | ('s' << 8): pdr_cpy(&GS.rise, opn(1), 4); g_tdirty = 1; break;
        case 'T' | ('f' << 8): {
            pdw fi = 0xFF;
            pdr_cpy(&GS.fs, opn(1), 4);
            if (g_havename && res_lookup("Font", g_name) == 0 && g_rl.objnum) fi = font_get(g_rl.objnum, g_rl.gen);
            else inc_note(PDF_INC_FONT, "missing font resource");
            if (fi == 0xFF) fi = font_get(0, 0);
            GS.font = (pdb)fi;
            fs_changed();
            break; }
        case 'T' | ('d' << 8): text_td(opn(2), opn(1)); break;
        case 'T' | ('D' << 8):
            pdm_fx_neg(&GS.tl, opn(1)); text_td(opn(2), opn(1)); break;
        case 'T' | ('m' << 8):
            pdr_cpy(&g_tm.a, opn(6), 4); pdr_cpy(&g_tm.b, opn(5), 4); pdr_cpy(&g_tm.c, opn(4), 4);
            pdr_cpy(&g_tm.d, opn(3), 4); pdr_cpy(&g_tm.e, opn(2), 4); pdr_cpy(&g_tm.f, opn(1), 4);
            pdr_cpy(&g_tlm, &g_tm, sizeof g_tm); g_tdirty = 1; break;
        case 'T' | ('*' << 8): {
            static struct pd_fx zero, nl;
            pdm_fx_int(&zero, 0); pdm_fx_neg(&nl, &GS.tl); text_td(&zero, &nl); break; }
        case 'T' | ('j' << 8):
            text_begin_show(); text_show(g_str, g_slen); text_end_show(); break;
        case 'T' | ('J' << 8):
            if (g_tjlive) text_end_show();
            g_tjlive = 0; break;
        case 'D' | ('o' << 8): do_xobject(); break;
        case 'B' | ('I' << 8): case 'I' | ('D' << 8): case 'E' | ('I' << 8): break;   /* pdri handles */
        case 'E' | ('X' << 8): case 'B' | ('X' << 8): case 'M' | ('P' << 8): case 'D' | ('P' << 8): break;
        case 's' | ('h' << 8): inc_note(PDF_INC_SHADE, "shading"); break;
        case 'd' | ('0' << 8): case 'd' | ('1' << 8): break;
        default: goto unknown;
        }
        return;
    }
    if (n == 3) {
        if (k == ('B' | ('M' << 8)) && c2 == 'C') return;
        if (k == ('B' | ('D' << 8)) && c2 == 'C') return;
        if (k == ('E' | ('M' << 8)) && c2 == 'C') return;
        if ((k == ('S' | ('C' << 8)) || k == ('s' | ('c' << 8))) && c2 == 'N') {
            pdw st = (s[0] == 'S');
            if (g_nop == 1 || g_nop == 3 || g_nop == 4) col_apply(st, g_nop);
            else inc_note(PDF_INC_COLOUR, "pattern colour");
            return;
        }
    }
unknown:
    {
        static char nm[8];
        pdw i;
        for (i = 0; i < n && i < 7; i++) nm[i] = s[i];
        nm[i] = 0;
        inc_note(PDF_INC_OPER, nm);
    }
    return;
showq:
    {
        static struct pd_fx zero, nl;
        pdm_fx_int(&zero, 0); pdm_fx_neg(&nl, &GS.tl); text_td(&zero, &nl);
        text_begin_show(); text_show(g_str, g_slen); text_end_show();
    }
    return;
showdq:
    {
        static struct pd_fx zero, nl;
        pdr_cpy(&GS.tw, opn(3), 4); pdr_cpy(&GS.tc, opn(2), 4);
        pdm_fx_int(&zero, 0); pdm_fx_neg(&nl, &GS.tl); text_td(&zero, &nl);
        text_begin_show(); text_show(g_str, g_slen); text_end_show();
    }
}

/* ============================================================================
 * THE MAIN LOOP
 * ==========================================================================*/
static pdw g_inl;                   /* skipping an inline image to EI */

/* a frame ended: pop to its parent, or move to the next /Contents element */
static pdw frame_end(void)
{
    strm_close();
    if (g_nfr > 0 && g_fr[g_nfr].isform) {
        gs_set(g_fr[g_nfr].gsbase);
        g_resp = g_fr[g_nfr].resp; g_reso = g_fr[g_nfr].reso; g_resg = g_fr[g_nfr].resg;
        g_nfr--;
        g_gsfloor = g_nfr > 0 && g_fr[g_nfr].isform ? g_fr[g_nfr].gsbase + 1 : 0;
        return 1;
    }
    /* the page: next element of /Contents */
    g_fr[0].cidx++;
    if (strm_open(0, 0, g_page, g_fr[0].cidx) == 0) return 1;
    g_idone = 1;
    return 0;
}

/* run at most `maxops` operators. Returns 1 when the page is finished */
static pdw interp_run(pdw maxops)
{
    pdw err, ops = 0, kind;
    while (ops < maxops) {
        if (g_dlfull) return 0;
        err = tok_next();
        if (err) { g_ierr = err; inc_note(PDF_INC_LIMIT, "content stream error"); g_idone = 1; while (g_nfr) { strm_close(); g_nfr--; } strm_close(); return 1; }
        kind = g_tk.kind;
        if (g_inl) {                              /* inline image: skip to EI */
            if (kind == PDT_OP && g_tk.len == 2 && g_tk.text[0] == 'E' && g_tk.text[1] == 'I') g_inl = 0;
            continue;
        }
        if (g_dictd) {
            if (kind == PDT_DICTB) g_dictd++;
            else if (kind == PDT_DICTE) g_dictd--;
            else if (kind == PDT_EOF) goto eof;
            continue;
        }
        switch (kind) {
        case PDT_NUM:
            if (g_tjlive && g_arrd) { text_adjust(&g_tk.num); break; }
            if (g_nop < PDR_NOPS) { pdr_cpy(&g_opn[g_nop], &g_tk.num, 4); g_nop++; }
            break;
        case PDT_TRUE: case PDT_FALSE: case PDT_NULL: case PDT_REF:
            if (g_nop < PDR_NOPS) { pdm_fx_int(&g_opn[g_nop], kind == PDT_TRUE); g_nop++; }
            break;
        case PDT_NAME:
            pdr_scpy(g_name, g_tk.text, PDR_NNAME); g_havename = 1;
            break;
        case PDT_STR:
            if (g_arrd && g_inbt) {
                pdw j;
                if (!g_tjlive) {                   /* the array is a TJ: apply what came before */
                    g_tjlive = 1;
                    text_begin_show();
                    for (j = g_arr0; j < g_nop; j++) text_adjust(&g_opn[j]);
                    g_nop = g_arr0;
                }
                text_show((const pdb *)g_tk.text, g_tk.len);
            } else {
                pdw j;
                if (!g_smore) g_slen = 0;
                for (j = 0; j < g_tk.len && g_slen < PDR_NSTR; j++) g_str[g_slen++] = (pdb)g_tk.text[j];
                g_smore = g_tk.more;
            }
            break;
        case PDT_ARRB: g_arrd++; if (g_arrd == 1) g_arr0 = g_nop; break;
        case PDT_ARRE: if (g_arrd) g_arrd--; break;
        case PDT_DICTB: g_dictd = 1; break;
        case PDT_DICTE: break;
        case PDT_OP:
            if (g_tk.len == 2 && g_tk.text[0] == 'B' && g_tk.text[1] == 'I') {
                inc_note(PDF_INC_IMAGE, "inline image");
                g_inl = 1; op_clear(); break;
            }
            exec_op(g_tk.text, g_tk.len);
            op_clear();
            g_tjlive = 0;
            ops++;
            break;
        case PDT_EOF:
        eof:
            if (!frame_end()) return 1;
            op_clear();
            break;
        default: break;
        }
    }
    return 0;
}

/* start interpreting page `page`; g_mode is set by the caller */
static pdw interp_start(pdw page)
{
    pdw err;
    op_clear(); g_tjlive = 0; g_inl = 0; g_dictd = 0; g_inbt = 0;
    path_reset(); g_plimit = 0; g_nfr = 0; g_idone = 0; g_ierr = 0;
    g_page = page; g_resp = page; g_reso = 0; g_resg = 0;
    g_fr[0].isform = 0; g_fr[0].cidx = 0;
    g_ops = 0;
    g_rn = 0; g_ropen = 0;
    g_fnnext = 0; pdr_zero(g_fn, sizeof g_fn);
    g_pkfam = 0xFF;
    text_ident();
    err = strm_open(0, 0, page, 0);
    if (err) { g_idone = 1; return err == PDE_RANGE ? 0 : err; }
    return 0;
}
