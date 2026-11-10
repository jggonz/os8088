/* ============================================================================
 * os8088 - apps/pagedesk/pdrxb.c   PDREND: the interpretation side of transparency
 *
 * PAGEDESK-SPEC 20.22.10.3-4. #included by pdrend.c after pdrx8.c. OWNED BY
 * blend. The `gs` arm, the transparency tuple pool, the XSTATE / GROUP /
 * MASKB / MASKE records and the mark map; the hooks other files call are the
 * `xb_*` prototypes of pdrend.h.
 *
 * The effective transparency is the FOLD of the gs operators so far, so it is
 * a tuple {ca, CA, bm, smask} in a per-pass pool and pdr_gs.pad_ is its index
 * (0 = opaque Normal, no mask): q, Q and a form's push already save it.
 * ==========================================================================*/

#ifdef PDB_DIRECT                       /* the host harness: PDXG's verb linked in, no slot */
static pdw xg_dictget(struct pdxb_dictget *q) { return eng_call(PDV_ENG_DICT_GET, q, sizeof *q, 0, 0, 0); }
#include "pdxgtr.c"
#endif

static pdb g_xt[32][4];                 /* ca, CA, bm, smask (id + 1); index 0 unused */
static pdw g_xtn;
static pdb g_xl[3];                     /* the last XSTATE written: alpha, bm, smask  */
static pdw g_xnt;                       /* the mark being emitted is not opaque Normal */
static pdw g_xmaskpend, g_xmaskid;      /* do_form is about to open a soft mask's /G */
static struct { pdb kind; pdb id; pdi y0, y1; } g_xfr[PDR_NFORM + 1];   /* per frame: 1 group, 2 mask */
static struct pdxb_gs g_gx;

static void xb_trivial(void) { g_xl[0] = 255; g_xl[1] = 0; g_xl[2] = 0; }

static void xb_pass(void)
{
    pdw i;
    g_xtn = 0; g_nmask = 0; g_xmaskpend = 0; g_x8any = 0;
    xb_trivial();
    for (i = 0; i < 32; i++) g_x8map[i] = 0;
    for (i = 0; i <= PDR_NFORM; i++) g_xfr[i].kind = 0;
}

/* the rows a transparent mark touches: its 16-row bands are drawn in 8 bits */
static void xb_rows(pdi y0, pdi y1)
{
    pdw b0, b1, b;
    if (y0 < g_vcy0) y0 = g_vcy0;
    if (y1 < y0) return;
    g_x8any = 1;
    b0 = (pdw)((y0 - g_vcy0) >> 4); b1 = (pdw)((y1 - g_vcy0) >> 4);
    for (b = b0; b <= b1 && b < 256; b++) g_x8map[b >> 3] |= (pdb)(1 << (b & 7));
}
static void xb_mark(pdi y0, pdi y1)
{
    if (g_xnt && g_mode == PDR_INTERP) xb_rows(y0, y1);
}

/* the XSTATE of the mark that is about to be emitted (kind: PK_*) */
static void xb_state(pdw kind)
{
    static pdb b[4];
    pdb *t;
    if (g_mode != PDR_INTERP) { g_xnt = 0; return; }
    if (GS.pad_) {
        t = g_xt[GS.pad_];
        b[0] = kind == PK_STROKE ? t[1] : t[0]; b[1] = t[2]; b[2] = t[3];
    } else { b[0] = 255; b[1] = 0; b[2] = 0; }
    b[3] = 0;
    g_xnt = b[0] != 255 || b[1] || b[2];
    if (g_xl[0] == b[0] && g_xl[1] == b[1] && g_xl[2] == b[2]) return;
    hdr_set(DLR_XSTATE, 0, DLH + 4, 0, 0);
    if (dl_put(g_hdr, DLH, b, 4)) { g_xl[0] = b[0]; g_xl[1] = b[1]; g_xl[2] = b[2]; }
}

/* GS.pad_ = the pool's entry for this tuple (made if new). 1 = done */
static pdw xb_tuple(pdw ca, pdw CA, pdw bm, pdw sm)
{
    pdw i;
    if (ca == 255 && CA == 255 && !bm && !sm) { GS.pad_ = 0; return 1; }
    for (i = 1; i <= g_xtn; i++)
        if (g_xt[i][0] == ca && g_xt[i][1] == CA && g_xt[i][2] == bm && g_xt[i][3] == sm) { GS.pad_ = (pdb)i; return 1; }
    if (g_xtn >= 31) { inc_note(PDF_INC_LIMIT, "transparency states"); return 0; }
    i = ++g_xtn;
    g_xt[i][0] = (pdb)ca; g_xt[i][1] = (pdb)CA; g_xt[i][2] = (pdb)bm; g_xt[i][3] = (pdb)sm;
    GS.pad_ = (pdb)i;
    return 1;
}

/* the gs operator */
static pdw xb_gs(void)
{
    pdw ca = 255, CA = 255, bm = 0, sm = 0, id, n0;
    if (g_mode != PDR_INTERP || !g_havename) return 0;
    if (GS.pad_) { ca = g_xt[GS.pad_][0]; CA = g_xt[GS.pad_][1]; bm = g_xt[GS.pad_][2]; sm = g_xt[GS.pad_][3]; }
    if (res_lookup("ExtGState", g_name)) return 0;
    if (!g_rl.objnum) { inc_note(PDF_INC_TRANSP, "direct ExtGState"); return 0; }
    g_gx.objnum = g_rl.objnum; g_gx.gen = g_rl.gen;
    {
        pdw e = xc_find(XCK_GS, g_gx.objnum, g_gx.gen);
        if (e) pd_fcopy_in(g_xcseg, e, &g_gx, sizeof g_gx);
        else {
#ifdef PDB_DIRECT
            pdr_cpy(&gt_g, &g_gx, sizeof g_gx);
            gt_parse();
            pdr_cpy(&g_gx, &gt_g, sizeof g_gx);
#else
            if (!x_have(PDX_GFX)) { x_need(PDX_GFX); return 0; }
            pd_fcopy_out(&g_gx, g_xb, 0, sizeof g_gx);
            x_call(PDV_GS_PARSE, 0, 0, 0);
            if (x_cf) { inc_note(PDF_INC_TRANSP, "ExtGState"); return 0; }
            pd_fcopy_in(g_xb, 0, &g_gx, sizeof g_gx);
            e = xc_alloc(XCK_GS, g_gx.objnum, g_gx.gen, sizeof g_gx);
            if (e) pd_fcopy_out(&g_gx, g_xcseg, e, sizeof g_gx);
#endif
        }
    }
    if (g_gx.present & GSP_FILL)   ca = g_gx.fill_a;
    if (g_gx.present & GSP_STROKE) CA = g_gx.strk_a;
    if (g_gx.present & GSP_BM)     bm = g_gx.bm;
    if (g_gx.trnon) inc_note(PDF_INC_TRANSP, "soft mask transfer function");
    if (g_gx.present & GSP_SMASK) {
        if (!g_gx.smode || !g_gx.gnum || g_nmask >= 16) {
            if (g_gx.smode && g_nmask >= 16) inc_note(PDF_INC_LIMIT, "soft masks");
            sm = 0;
        } else {                                        /* define the mask: its /G as MASKB ... MASKE */
            id = g_nmask++; sm = (pdw)(id + 1);
            if (!xb_tuple(ca, CA, bm, sm)) { g_nmask--; return 0; }
            g_xmaskpend = 1; g_xmaskid = id;
            g_xmkind = (pdw)(g_gx.smode - 1); g_xmbc = g_gx.bc;
            n0 = g_nfr;
            do_form(g_gx.gnum, g_gx.ggen);
            g_xmaskpend = 0;
            if (g_nfr == n0) { g_nmask--; sm = 0; xb_tuple(ca, CA, bm, 0); }   /* the form would not open */
            return 0;
        }
    }
    xb_tuple(ca, CA, bm, sm);
    return 0;
}

/* a Form XObject begins (do_form's tail, the frame is g_nfr): a soft mask's
 * /G, or a transparency group worth a layer */
static void xb_group_begin(pdw obj, pdw gen)
{
    static pdb b[4];
    pdw f = g_nfr, iso, ko, triv;
    pdi y0, y1;
    g_xfr[f].kind = 0;
    if (g_mode != PDR_INTERP) { g_xmaskpend = 0; return; }
    if (g_xmaskpend) {
        g_xmaskpend = 0;
        b[0] = (pdb)g_xmbc; b[1] = 0;
        hdr_set(DLR_MASKB, g_xmkind, DLH + 2, 0, 0x7FFF);
        g_mboff[g_xmaskid] = g_dllen;
        if (!dl_put(g_hdr, DLH, b, 2)) return;
        g_xfr[f].kind = 2; g_xfr[f].id = (pdb)g_xmaskid;
        g_lstv = 0; GS.pad_ = 0; xb_trivial();           /* the block opens with its own STATE */
        return;
    }
    if (dict_get(obj, gen, "Group", "S", 0) || g_dg.kind != PDK_NAME || !pdr_seq(g_dg.text, "Transparency")) return;
    iso = dict_get(obj, gen, "Group", "I", 0) == 0 && (g_dg.num.hi || g_dg.num.lo);
    ko = dict_get(obj, gen, "Group", "K", 0) == 0 && (g_dg.num.hi || g_dg.num.lo);
    triv = !GS.pad_ || (g_xt[GS.pad_][0] == 255 && !g_xt[GS.pad_][2] && !g_xt[GS.pad_][3]);
    if (triv && !iso && !ko) return;
    y0 = GS.cy0 > g_gy0 ? GS.cy0 : g_gy0; y1 = GS.cy1 < g_gy1 ? GS.cy1 : g_gy1;
    if (y1 < y0 || GS.cx1 < GS.cx0) return;
    if (GS.pad_) { b[0] = g_xt[GS.pad_][0]; b[1] = g_xt[GS.pad_][2]; b[2] = g_xt[GS.pad_][3]; }
    else { b[0] = 255; b[1] = 0; b[2] = 0; }
    b[3] = 0;
    hdr_set(DLR_GROUP, (pdw)(iso | (ko << 1)), DLH + 4, y0, y1);
    if (!dl_put(g_hdr, DLH, b, 4)) return;
    if (!triv) xb_rows(y0, y1);
    g_xfr[f].kind = 1; g_xfr[f].y0 = y0; g_xfr[f].y1 = y1;
    GS.pad_ = 0; xb_trivial();                           /* inside the group: opaque Normal, no mask */
}

/* a Form frame ends (frame_end, before the state is restored) */
static void xb_group_end(void)
{
    static pdb t[2];
    pdw f = g_nfr, k = g_xfr[f].kind, id;
    g_xfr[f].kind = 0;
    if (k == 1) {
        hdr_set(DLR_GROUP, 0x80, DLH, g_xfr[f].y0, g_xfr[f].y1);
        dl_put(g_hdr, DLH, 0, 0);
        xb_trivial();
    } else if (k == 2) {
        id = g_xfr[f].id;
        hdr_set(DLR_MASKE, 0, DLH, 0, 0x7FFF);
        dl_put(g_hdr, DLH, 0, 0);
        w16(t, (pdi)(g_dllen - g_mboff[id]));            /* the block's length: a normal walk jumps it */
        pd_fcopy_out(t, g_dls, (pdw)(g_mboff[id] + 2), 2);
        g_lstv = 0; xb_trivial();
    }
}

/* an image's /SMask or /Mask object (20.22.10.3): the reference, not its value */
static struct pdxb_dictget g_xq;
static pdw xb_ref(pdw obj, pdw gen, const char *key)
{
    pdr_zero(&g_xq, sizeof g_xq); g_xq.objnum = obj; g_xq.gen = gen; pdr_scpy(g_xq.key[0], key, 32);
    if (eng_call(PDV_ENG_DICT_GET, &g_xq, sizeof g_xq, 0, 0, 0)) return 0;
    if (g_xq.kind != PDK_REF) return g_xq.kind == PDK_ARR ? 2 : 0;
    g_imm_o = g_xq.refnum; g_imm_g = g_xq.refgen;
    return 1;
}
static pdw xb_imgmask(pdw obj, pdw gen)
{
    pdw r;
    if (xb_ref(obj, gen, "SMask") == 1) return 0x20;
    r = xb_ref(obj, gen, "Mask");
    if (r == 1) return 0x40;
    if (r == 2) inc_note(PDF_INC_TRANSP, "colour key mask");
    return 0;
}
