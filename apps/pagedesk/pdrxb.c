/* ============================================================================
 * os8088 - apps/pagedesk/pdrxb.c   PDREND: the interpretation hooks of transparency
 *
 * PAGEDESK-SPEC 20.22.10.3-4. #included by pdrend.c after pdrx8.c. OWNED BY
 * blend. The work is PDX8's (pdx8i.c: the ExtGState parse, the tuple pool, the
 * XSTATE / GROUP / MASKB / MASKE records, the mark map); PDREND asks and puts the
 * record PDX8 answers in its list. With no PDX8 in the X8 region (the first pass
 * of a view that needs it) a gs only decides whether the page needs it at all.
 * ==========================================================================*/

static pdw g_xnt;                       /* the mark being emitted is not opaque Normal */
static pdw g_xlive;                     /* transparency is in force (a tuple, or the last XSTATE)  */
static pdw g_xmaskpend;                 /* do_form is about to open a soft mask's /G  */

/* the record PDX8 left at XB+0, n bytes, into the list */
static void xb_emit(pdw n)
{
    static pdb r[DLH + 4];
    pd_fcopy_in(g_xb, 0, r, n);
    dl_put(r, DLH, r + DLH, (pdw)(n - DLH));
}

static void xb_pass(void)
{
    g_xnt = 0; g_xlive = 0; g_xmaskpend = 0; g_x8any = 0;
    if (g_x8rdy) x8_call(PDV_X8_CTL, X8C_PASS, (pdw)g_vcy0, 0);
}

/* the rows a transparent mark touches: its 16-row bands are drawn in 8 bits */
static void xb_rows(pdi y0, pdi y1)
{
    g_x8any = 1;
    if (g_x8rdy) x8_call(PDV_X8_DEF, X8D_MARK, (pdw)y0, (pdw)y1);
}
static void xb_mark(pdi y0, pdi y1)
{
    if (g_xnt && g_mode == PDR_INTERP) xb_rows(y0, y1);
}

/* the XSTATE of the mark that is about to be emitted (kind: PK_*) */
static void xb_state(pdw kind)
{
    pdw n;
    g_xnt = 0;
    if (!g_x8rdy || g_mode != PDR_INTERP || (!GS.pad_ && !g_xlive)) return;
    n = x8_call(PDV_X8_DEF, X8D_STATE, kind == PK_STROKE, GS.pad_);
    g_xnt = x8_odx & 1; g_xlive = (x8_odx >> 1) & 1;
    if (n) xb_emit(n);
}

/* no PDX8 yet: does this ExtGState ask for anything transparent? Then the view needs the region */
static const char *const xb_keys[4] = { "ca", "CA", "BM", "SMask" };
static void xb_detect(pdw obj, pdw gen)
{
    pdw i;
    for (i = 0; i < 4; i++) {
        if (dict_get(obj, gen, xb_keys[i], 0, 0)) continue;
        if (i < 2) { if (g_dg.num.hi < 1 && g_dg.num.lo < 0xFF80u) g_x8any = 1; }
        else if (i == 2) { if (g_dg.kind == PDK_ARR || (g_dg.text[0] != 'N' && g_dg.text[0] != 'C')) g_x8any = 1; }
        else if (g_dg.kind == PDK_DICT) g_x8any = 1;
    }
}

/* the gs operator */
static pdw xb_gs(void)
{
    pdw n0, t;
    if (g_mode != PDR_INTERP || !g_havename) return 0;
    if (res_lookup("ExtGState", g_name)) return 0;
    if (!g_rl.objnum) { inc_note(PDF_INC_TRANSP, "direct ExtGState"); return 0; }
    if (!g_x8rdy) { xb_detect(g_rl.objnum, g_rl.gen); return 0; }
    t = x8_call(PDV_X8_GS, GS.pad_, g_rl.objnum, g_rl.gen);
    x8_bits(x8_odx);
    if (x8_cf) return 0;
    GS.pad_ = (pdb)t;
    if (x8_ocx) {                                       /* a soft mask: its /G is a Form, interpreted into a block */
        g_xmaskpend = 1; n0 = g_nfr;
        do_form(pd_fpeekw(g_xb, 0), pd_fpeekw(g_xb, 2));
        g_xmaskpend = 0;
        if (g_nfr == n0) GS.pad_ = (pdb)x8_call(PDV_X8_DEF, X8D_NOMASK, 0, 0);   /* the form would not open */
    }
    return 0;
}

/* a Form XObject begins (do_form's tail, the frame is g_nfr): a soft mask's /G,
 * or a transparency group worth a layer */
static void xb_group_begin(pdw obj, pdw gen)
{
    static struct pdxb_x8grp q;
    pdw n;
    q.maskpend = g_xmaskpend; g_xmaskpend = 0;
    if (!g_x8rdy || g_mode != PDR_INTERP) return;
    q.frame = g_nfr; q.obj = obj; q.gen = gen; q.tuple = GS.pad_; q.dllen = g_dllen;
    q.y0 = GS.cy0 > g_gy0 ? GS.cy0 : g_gy0; q.y1 = GS.cy1 < g_gy1 ? GS.cy1 : g_gy1;
    if (GS.cx1 < GS.cx0) q.y1 = (pdi)(q.y0 - 1);
    pd_fcopy_out(&q, g_xb, 0, sizeof q);
    n = x8_call(PDV_X8_DEF, X8D_GROUP, 0, 0);
    if (!n) return;
    xb_emit(n);
    if (x8_ocx & 1) g_lstv = 0;                         /* a mask's block opens with its own STATE */
    GS.pad_ = 0;                                        /* inside: opaque Normal, no mask */
    g_xlive = 0;
}

/* a Form frame ends (frame_end, before the state is restored) */
static void xb_group_end(void)
{
    static pdb t[2];
    pdw n, at;
    if (!g_x8rdy || g_mode != PDR_INTERP) return;
    n = x8_call(PDV_X8_DEF, X8D_GEND, g_nfr, g_dllen);
    if (!n) return;
    at = x8_ocx; w16(t, (pdi)x8_odx);
    xb_emit(n);
    g_xlive = 0;
    if (at) { pd_fcopy_out(t, g_dls, at, 2); g_lstv = 0; }     /* the mask block's length: a normal walk jumps it */
}

/* an image's /SMask or /Mask (20.22.10.3): 0x20, 0x40 or 0; PDX8 finds the object itself */
static pdw xb_imgmask(pdw obj, pdw gen)
{
    if (dict_get(obj, gen, "SMask", 0, 0) == 0 && g_dg.kind != PDK_NULL) { g_x8any = 1; return 0x20; }
    if (dict_get(obj, gen, "Mask", 0, 0) == 0 && g_dg.kind != PDK_NULL) {
        if (g_dg.kind == PDK_ARR) { inc_note(PDF_INC_TRANSP, "colour key mask"); return 0; }
        g_x8any = 1; return 0x40;
    }
    return 0;
}
