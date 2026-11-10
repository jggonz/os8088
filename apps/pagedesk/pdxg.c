/* ============================================================================
 * os8088 - apps/pagedesk/pdxg.c                      PDXG.MOD (PAGEDESK-SPEC 20.22.9)
 *
 * The graphics slot occupant: colour spaces, functions, shadings, patterns.
 * One translation unit (SPEC.md 73.1) - the pdxg*.c files are #included in
 * order - compiled by cc8086 for the target (pdxg.asm is the door) and by the
 * host `cc -DPD_HOST` for hosttest/pdxg_test.c, which #includes THIS file so it
 * can reach the statics and supplies pdxgh_* in place of PDENG.
 *
 * Verbs 0xC8-0xCF, calling convention PAGEDESK-SPEC 3.1: the door pushes AL, BX,
 * CX, DX and calls pdx_verb; the answer is AX, with CF = 1 on a failure.
 * ==========================================================================*/
#ifdef PD_HOST
#include "pdabi.h"
#include "pdmath.h"
#else
#include "os88.h"
#include "pdabi.h"
#include "pdmath.h"
#endif

pdw pdx_cf;                             /* CF of this door's answer            */
pdw pdx_engcf;                          /* CF of the last PDENG call           */
pdw pdx_engvec[2];                      /* PDENG's door, offset then segment   */
static pdw g_xbseg;                     /* the exchange block                  */
static pdw gw_seg;                      /* the slot workspace (pdxb_init.slot_seg): parse arrays, function tables */

/* ---- the object source: PDENG on the target, the harness on the host ------- */
static pdw gx_fetch(pdw obj, pdw gen, char *buf, pdw cap);
static pdw gx_sopen(pdw obj, pdw gen);
static pdw gx_sread(pdb *buf, pdw cap);
static void gx_sclose(void);
/* read the open stream into far memory: through a 128-byte near window */
static pdw gx_sreadf(pdw seg, pdw off, pdw cap)
{
    static pdb win[128];
    pdw got = 0, n, r;
    while (got < cap) {
        n = cap - got < 128 ? cap - got : 128;
        r = gx_sread(win, n);
        if (!r) break;
        pd_fcopy_out(win, seg, (pdw)(off + got), r);
        got += r;
    }
    return got;
}

#include "pdxgm.c"
#include "pdxgp.c"
#include "pdxgf.c"
#include "pdxgc.c"

#ifdef PD_HOST
pdw pdxgh_fetch(pdw obj, pdw gen, char *buf, pdw cap);
pdw pdxgh_sopen(pdw obj, pdw gen);
pdw pdxgh_sread(pdb *buf, pdw cap);
static pdw gx_fetch(pdw obj, pdw gen, char *buf, pdw cap) { return pdxgh_fetch(obj, gen, buf, cap); }
static pdw gx_sopen(pdw obj, pdw gen) { return pdxgh_sopen(obj, gen); }
static pdw gx_sread(pdb *buf, pdw cap) { return pdxgh_sread(buf, cap); }
static void gx_sclose(void) { }
#else
pdw pdx_engcall(pdw verb, pdw bx, pdw cx, pdw dx);     /* AX; asm on the target */
static pdw gx_fetch(pdw obj, pdw gen, char *buf, pdw cap)
{
    static struct pdxb_rawcopy rc;
    rc.objnum = obj; rc.gen = gen; rc.done.lo = 0; rc.done.hi = 0;
    rc.dseg = pd_ds(); rc.doff = (pdw)buf; rc.dmax = cap; rc.got = 0; rc.mode = PDRC_PLAIN;
    pd_fcopy_out(&rc, g_xbseg, 0, sizeof rc);
    pdx_engcall(PDV_ENG_RAW_COPY, g_xbseg, 0, 0);
    if (pdx_engcf) return 0;
    pd_fcopy_in(g_xbseg, 0, &rc, sizeof rc);
    return rc.got;
}
static pdw gx_sopen(pdw obj, pdw gen)
{
    static struct pdxb_strm st;
    st.objnum = obj; st.gen = gen; st.index = 0; st.page = 0; st.accept = 0;
    pd_fcopy_out(&st, g_xbseg, 0, sizeof st);
    return pdx_engcall(PDV_ENG_STRM_OPEN, g_xbseg, 0, 0);
}
static pdw gx_sread(pdb *buf, pdw cap)
{
    pdw r = pdx_engcall(PDV_ENG_STRM_READ, pd_ds(), (pdw)buf, cap);
    return pdx_engcf ? 0 : r;
}
static void gx_sclose(void) { pdx_engcall(PDV_ENG_STRM_CLOSE, 0, 0, 0); }
#endif

/* The verbs of 0xC8-0xCF are this module's own except 0xCE (rcore's dash walker, pdxdash.c) and 0xCF (rcore's
 * edit-store marks, pdxedit.c); blend's 0xD0-0xD7 (pdxgtr.c) follow. Those branches add their #include and a
 * PDX_OTHER_VERB(verb) that dispatches them (answering PDE_RANGE for any other) - the lead merges the three. */
#ifndef PDX_LAST_VERB
#define PDX_LAST_VERB 0xCD
#endif
#ifndef PDX_OTHER_VERB
#define PDX_OTHER_VERB(v) PDE_RANGE
#endif

/* ---- the verbs -------------------------------------------------------------- */
static struct pdxb_gfx G;
/* one overlay for the verbs' big temporaries: a verb uses one of them at a time (22.9's workspace note) */
static pdb gx_ov[1264];
#define R_cs  (*(struct pdgx_cs *)gx_ov)
#define R_lut (*(struct pdgx_lut *)gx_ov)

/* the node the request names: an object, or the text, parsed */
/* PDGXI_RES: the value of Resources/<category>/<name> of a Form or a page, following /Parent for
 * inherited resources (at most 8 levels). The text is "Category\0Name". */
static char gx_txt[64];
/* the dictionary text that /key of dict text p[0..len) names: inline (a slice of p) or a reference
 * fetched into `buf`. Answers 1 and the slice in *dp, *dl. */
static int gx_sub(const char *p, pdw len, const char *key, char *buf, const char **dp, pdw *dl)
{
    static pdw s, e, o, g;
    pdw got;
    if (!gx_find(p, len, key, &s, &e)) return 0;
    if (gx_refat(p, s, e, &o, &g)) {
        got = gx_fetch(o, g, buf, GX_TB);
        if (!got) return 0;
        *dp = buf; *dl = got; return 1;
    }
    *dp = p + s; *dl = e - s; return 1;
}
/* PDGXI_RES: the value of Resources/<category>/<name> of a Form or a page, following /Parent for
 * inherited resources (8 levels at most). txt is "Category\0Name". No dictionary is parsed whole:
 * only the value found is. */
static pdw gx_res_lookup(const char *txt, pdw len)
{
    static char cat[20], nm[34];
    pdw obj = G.objnum, gen = G.gen, i = 0, j = 0, hops, got;
    static pdw s, e, o, g, rl, cl;
    static const char *rp, *cp;
    while (i < len && txt[i] && j < 19) cat[j++] = txt[i++];
    cat[j] = 0; i++; j = 0;
    while (i < len && txt[i] && j < 33) nm[j++] = txt[i++];
    nm[j] = 0;
    if (!obj) {
#ifdef PD_HOST
        return GN_NIL;
#else
        static struct pdxb_pageinfo pi;
        if (pdx_engcall(PDV_ENG_PAGEINFO, G.bpc, 0, 0) != PDE_OK || pdx_engcf) return GN_NIL;
        pd_fcopy_in(g_xbseg, 0, &pi, 4);
        obj = pi.objnum; gen = pi.gen;
#endif
    }
    for (hops = 0; hops < 8; hops++) {
        got = gx_fetch(obj, gen, gx_tb, GX_TB);
        if (!got) return GN_NIL;
        if (gx_sub(gx_tb, got, "Resources", gx_tb2, &rp, &rl) && gx_sub(rp, rl, cat, rp == gx_tb2 ? gx_tb : gx_tb2, &cp, &cl)
            && gx_find(cp, cl, nm, &s, &e))
            return gx_parse(cp + s, e - s);
        if (!gx_find(gx_tb, got, "Parent", &s, &e) || !gx_refat(gx_tb, s, e, &o, &g)) return GN_NIL;
        obj = o; gen = g;
    }
    return GN_NIL;
}

/* the full "Category\0Name" text of a PDGXI_RES request: the category by code (flags bits 8-11), the name at tseg:toff */
static pdw gx_fulltxt(char *dst)
{
    static const char cats[] = "ColorSpace\0Shading\0Pattern\0";
    const char *c = cats;
    pdw k = (pdw)((G.flags >> 8) & 15), n = 0;
    if (!k || k > 3 || !G.tlen || G.tlen > 40) return 0;
    while (--k) { while (*c) c++; c++; }
    while (*c) dst[n++] = *c++;
    dst[n++] = 0;
    pd_fcopy_in(G.tseg, G.toff, dst + n, G.tlen);
    return (pdw)(n + G.tlen);
}

static pdw gx_source(void)
{
    gx_reset();
    fn_n = 0; fn_sused = 0; fn_cused = 0;
    if (G.flags & PDGXI_RES) {
        pdw n = gx_fulltxt(gx_txt);
        return n ? gx_res_lookup(gx_txt, n) : GN_NIL;
    }
    if (G.objnum) return gx_fetch_parse(G.objnum, G.gen);
    if (!G.tlen || G.tlen > GX_TB) return GN_NIL;
    pd_fcopy_in(G.tseg, G.toff, gx_tb, G.tlen);
    return gx_parse(gx_tb, G.tlen);
}

#include "pdxgs.c"

static pdb gx_flags_of(pdw row)
{
    switch (cs_t[row].kind) {
    case GC_RGB: return PDGX_F_RGBLIKE;
    case GC_CMYK: return PDGX_F_CMYKLIKE;
    default: break;
    }
    return cs_t[row].n > 1 ? PDGX_F_MODULE : 0;
}

static pdw v_cs(void)
{
    pdw node, row, i, k, n;
    static struct pd_fx c, rgb[3];
    node = gx_source();
    if (node == GN_NIL || cs_compile(node)) return PDE_GRAPHICS;
    for (i = 0; i < sizeof R_cs; i++) ((pdb *)&R_cs)[i] = 0;
    row = cs_first();
    R_cs.kind = cs_t[0].kind; R_cs.n = cs_t[row].n;
    if (cs_t[0].kind == GC_PAT) {
        R_cs.flags |= PDGX_F_PATTERN;
        if (cs_n < 2) R_cs.n = 0;
    }
    R_cs.hival = cs_t[row].hival;
    if (G.flags & PDGXI_RES) {
        R_cs.pad = 1; R_cs.srcobj = G.objnum; R_cs.srcgen = G.gen; R_cs.rsv[0] = G.bpc;
        R_cs.srclen = gx_fulltxt((char *)R_cs.src);
        if (!R_cs.srclen) return PDE_GRAPHICS;
    } else if (G.objnum) { R_cs.srcobj = G.objnum; R_cs.srcgen = G.gen; }
    else {
        R_cs.srclen = G.tlen;
        if (G.tlen > 480) return PDE_GRAPHICS;
        pd_fcopy_in(G.tseg, G.toff, R_cs.src, G.tlen);   /* gx_tb was reused by the fetches */
    }
    if (cs_t[0].kind == GC_PAT && cs_n < 2) goto done;
    n = cs_t[row].n;
    if (n == 1) {
        R_cs.flags |= PDGX_F_TABLE;
        for (k = 0; k < 256; k++) {
            if (cs_t[row].kind == GC_IDX) fx_i(&c, (pdi)k);
            else if (k == 255) fx_i(&c, 1);
            else fx_raw(&c, 0, (pdw)((k << 8) + k));
            if (cs_conv(row, &c, rgb)) { R_cs.flags |= PDGX_F_NONE; break; }
            R_cs.rgb[k * 3] = (pdb)fx_to255(&rgb[0]);
            R_cs.rgb[k * 3 + 1] = (pdb)fx_to255(&rgb[1]);
            R_cs.rgb[k * 3 + 2] = (pdb)fx_to255(&rgb[2]);
        }
    } else R_cs.flags |= gx_flags_of(row);
done:
    if (G.rcap < sizeof R_cs) return PDE_NOMEM;
    pd_fcopy_out(&R_cs, G.rseg, G.roff, sizeof R_cs);
    G.rlen = sizeof R_cs; G.ncomp = R_cs.n; G.kind = R_cs.kind; G.ok = 1;
    return PDE_OK;
}

/* PDV_GFX_COLOR: cseg:coff names an XCK_CS entry; ncomp operands in comp[] (0 = the space's initial colour:
 * tint 1 for a Separation or DeviceN, index 0 for an Indexed, otherwise 0). Answers rgb[0..2]. A table space
 * (PDGX_F_TABLE) is looked up in the entry; any other is compiled again from its source and converted. */
static pdw v_color(void)
{
    static struct pdgx_cs hdr;
    pdw node, row, i, k;
    static struct pd_fx rgb[3], t;
    pd_fcopy_in(G.cseg, G.coff, &hdr, 16 + 480);
    if (hdr.flags & PDGX_F_TABLE) {
        k = (hdr.kind == PDGX_K_SEP || hdr.kind == PDGX_K_DEVN) ? 255 : 0;
        if (G.ncomp) {
            if (hdr.kind == PDGX_K_INDEXED) k = (pdw)pdm_fx_round(&G.comp[0]);
            else { fx_i(&t, 255); fx_mul(&t, &t, &G.comp[0]); k = (pdw)pdm_fx_round(&t); }
            if (G.comp[0].hi < 0) k = 0;
            if (k > 255) k = 255;
        }
        pd_fcopy_in(G.cseg, (pdw)(G.coff + 496 + k * 3), G.rgb, 3);
        G.rgb[3] = (hdr.flags & PDGX_F_NONE) ? 1 : 0;
        G.ok = 1;
        return PDE_OK;
    }
    G.objnum = hdr.srcobj; G.gen = hdr.srcgen; G.tlen = hdr.srclen;
    gx_reset(); fn_n = 0; fn_sused = 0; fn_cused = 0;
    if (hdr.pad) {
        if (hdr.srclen > 64) return PDE_GRAPHICS;
        for (i = 0; i < hdr.srclen && i < 64; i++) gx_txt[i] = (char)hdr.src[i];
        G.bpc = hdr.rsv[0];
        node = gx_res_lookup(gx_txt, hdr.srclen);
    }
    else if (G.objnum) node = gx_fetch_parse(G.objnum, G.gen);
    else {
        for (i = 0; i < hdr.srclen && i < 480; i++) gx_tb[i] = (char)hdr.src[i];
        node = gx_parse(gx_tb, hdr.srclen);
    }
    if (node == GN_NIL || cs_compile(node)) return PDE_GRAPHICS;
    row = cs_first();
    if (!G.ncomp) for (i = 0; i < 8; i++) { if (cs_t[row].kind == GC_DEVN) fx_i(&G.comp[i], 1); else fx_i(&G.comp[i], 0); }
    G.rgb[3] = (pdb)cs_conv(row, G.comp, rgb);
    G.rgb[0] = (pdb)fx_to255(&rgb[0]); G.rgb[1] = (pdb)fx_to255(&rgb[1]); G.rgb[2] = (pdb)fx_to255(&rgb[2]);
    G.ok = 1;
    return PDE_OK;
}

static pdw gx_grey(pdw row, const struct pd_fx *comps)
{
    static struct pd_fx rgb[3];
    pdw r, g, b;
    if (cs_conv(row, comps, rgb)) return 255;
    r = fx_to255(&rgb[0]); g = fx_to255(&rgb[1]); b = fx_to255(&rgb[2]);
    return (pdw)((r * 77u + g * 150u + b * 29u) >> 8);
}

/* PDV_GFX_LUT: the tables of an image's space (20.22.9) */
static pdw v_lut(void)
{
    pdw node, row, n, i, v, bpc = G.bpc ? G.bpc : 8, maxv, ix;
    static struct pd_fx comps[4], dmin[4], dmax[4], t, f;
    pdb inv = 0;
    node = gx_source();
    if (node == GN_NIL || cs_compile(node)) return PDE_GRAPHICS;
    row = cs_first();
    n = cs_t[row].n;
    if (!n || n > 4) return PDE_GRAPHICS;
    for (i = 0; i < sizeof R_lut; i++) ((pdb *)&R_lut)[i] = 0;
    R_lut.ncomp = (pdb)n;
    maxv = (pdw)((1u << (bpc > 15 ? 15 : bpc)) - 1u);
    if (bpc == 16) maxv = 65535u;
    for (i = 0; i < n; i++) {
        if (cs_t[row].kind == GC_IDX) { fx_i(&dmin[i], 0); fx_i(&dmax[i], (pdi)maxv); }
        else if (cs_t[row].kind == GC_LAB) {
            if (i == 0) { fx_i(&dmin[0], 0); fx_i(&dmax[0], 100); }
            else { fx_cp(&(dmin[i]), &(cs_t[row].p[3 + 2 * (i - 1)])); fx_cp(&(dmax[i]), &(cs_t[row].p[4 + 2 * (i - 1)])); }
        } else { fx_i(&dmin[i], 0); fx_i(&dmax[i], 1); }
        if ((G.flags & PDGXI_DECODE) && 2 * i + 1 < 8) { fx_cp(&(dmin[i]), &(G.comp[2 * i])); fx_cp(&(dmax[i]), &(G.comp[2 * i + 1])); }
    }
    if (n == 1) {
        for (v = 0; v < 256; v++) {
            if (cs_t[row].kind == GC_IDX && bpc < 8) {
                pdw r = (pdw)((v * maxv + 127u) / 255u);
                fx_i(&f, (pdi)r); fx_raw(&f, f.hi, 0);
                /* index = dmin + r * (dmax - dmin) / maxv */
                fx_sub(&t, &dmax[0], &dmin[0]); fx_mul(&t, &t, &f);
                { static struct pd_fx mv; fx_i(&mv, (pdi)maxv); fx_div(&t, &t, &mv); }
                fx_add(&comps[0], &dmin[0], &t);
            } else if (cs_t[row].kind == GC_IDX) {
                fx_i(&f, (pdi)v);
                fx_sub(&t, &dmax[0], &dmin[0]);
                fx_i(&comps[0], 255);
                fx_div(&t, &t, &comps[0]); fx_mul(&t, &t, &f);
                fx_add(&comps[0], &dmin[0], &t);
            } else {
                fx_raw(&f, v == 255 ? 1 : 0, v == 255 ? 0 : (pdw)((v << 8) + v));
                fx_sub(&t, &dmax[0], &dmin[0]); fx_mul(&t, &t, &f);
                fx_add(&comps[0], &dmin[0], &t);
            }
            R_lut.t[v] = (pdb)gx_grey(row, comps);
        }
    } else {
        /* additive or subtractive: does the zero vector paint white? */
        for (i = 0; i < n; i++) fx_cp(&(comps[i]), &(dmin[i]));
        if (cs_t[row].kind == GC_CMYK || cs_t[row].kind == GC_DEVN || gx_grey(row, comps) >= 200) inv = PDGX_LUT_INVERT;
        for (i = 0; i < n; i++) {
            for (v = 0; v < 256; v++) {
                pdw j;
                for (j = 0; j < n; j++) fx_cp(&(comps[j]), &(dmin[j]));
                fx_raw(&f, v == 255 ? 1 : 0, v == 255 ? 0 : (pdw)((v << 8) + v));
                fx_sub(&t, &dmax[i], &dmin[i]); fx_mul(&t, &t, &f);
                fx_add(&comps[i], &dmin[i], &t);
                ix = gx_grey(row, comps);
                if (cs_t[row].kind == GC_LAB && i == 0) { R_lut.t[v] = (pdb)ix; continue; }
                R_lut.t[i * 256 + v] = (pdb)(inv ? 255 - ix : (cs_t[row].kind == GC_LAB ? 0 : ix));
            }
        }
        R_lut.flags = inv;
    }
    if (G.rcap < sizeof R_lut) return PDE_NOMEM;
    pd_fcopy_out(&R_lut, G.rseg, G.roff, sizeof R_lut);
    G.rlen = sizeof R_lut; G.ncomp = n; G.ok = 1;
    return PDE_OK;
}

/* PDV_GFX_FN: evaluate one function; the inputs in comp[], ncomp of them */
static pdw v_fn(void)
{
    pdw node, f, n, i;
    node = gx_source();
    if (node == GN_NIL) return PDE_GRAPHICS;
    f = fn_compile(node);
    if (f == FNE_NONE) return PDE_GRAPHICS;
    n = fn_eval(f, G.comp, fe_x);
    for (i = 0; i < n && i < 8; i++) fx_cp(&(G.comp[i]), &(fe_x[i]));
    G.ncomp = n; G.ok = n ? 1 : 0;
    return PDE_OK;
}

static pdw v_init(pdw bx)
{
    static struct pdxb_init in;
    g_xbseg = bx;
    pd_fcopy_in(bx, 0, &in, sizeof in);
    pdx_engvec[0] = in.eng_off; pdx_engvec[1] = in.eng_seg;
    gw_seg = in.slot_seg;
    return in.slot_kb >= GX_WS_KB ? PDE_OK : PDE_NOMEM;
}

/* the door's entry: verb, then BX/CX/DX; the answer in AX, CF in pdx_cf */
pdw pdx_verb(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdw r;
    (void)cx; (void)dx;
    pdx_cf = 0;
    if (verb == PDV_X_INIT) return v_init(bx);
    if (verb < PDV_GFX_CS || verb > PDX_LAST_VERB) { pdx_cf = 1; return PDE_RANGE; }
    pd_fcopy_in(g_xbseg, 0, &G, sizeof G);
    G.ok = 0;
    switch (verb) {
    case PDV_GFX_CS: r = v_cs(); break;
    case PDV_GFX_COLOR: r = v_color(); break;
    case PDV_GFX_LUT: r = v_lut(); break;
    case PDV_GFX_SHADE: r = v_shade(); break;
    case PDV_GFX_FN: r = v_fn(); break;
    case PDV_GFX_PAT: r = v_pat(); break;
    default: r = PDX_OTHER_VERB(verb); break;
    }
    pd_fcopy_out(&G, g_xbseg, 0, sizeof G);
    if (r != PDE_OK && r != PDE_BUSY) pdx_cf = 1;
    return r;
}
