/* ============================================================================
 * os8088 - apps/pagedesk/pdxf.c        PDXF.MOD: font programs (PAGEDESK-SPEC 20.22.8)
 *
 * One translation unit: this file #includes the readers. The door (pdxf.asm)
 * calls pdxf_verb(). On the host, hosttest/pdx_host.c / pdx_real.c include this
 * file with PD_HOST and supply pdx_muldiv(), pdx_eng() and the far-memory arena
 * (pdmath.c).
 *
 * The program lives in the WORKSPACE (slot_seg, 20.22.2.3):
 *   [0, pcap)               the font program (one at a time)
 *   [pcap, pcap + 4096)     the CIDToGIDMap window (CIDs 0..2047)
 *   [pcap + 4096, + 512)    the staging window of the CMap tokeniser
 * ==========================================================================*/
#include "pdabi.h"
#include "pdmath.h"

/* trunc-to-nearest(a*b/c), 32-bit intermediate, c != 0. Assembly in pdxf.asm */
pdi pdx_muldiv(pdi a, pdi b, pdi c);
/* one far call into PDENG's door; 0 or the PDE_ code, AX in pdx_eng_ax */
pdw pdx_eng(pdw verb, pdw bx, pdw cx, pdw dx);
extern pdw pdx_eng_ax;

static pdw g_upem, g_em64;          /* units per em; the size in 1/64 px         */

#define PDXF_WS_KB   48             /* the workspace this module asks for (20.22.2.3) */
#define PDXF_CIDWIN  4096
#define PDXF_STG     512

#include "pdxfol.c"
static pdw g_cf_base;
#include "pdxfnm.c"
#include "pdxfsf.c"
#include "pdxfcf.c"
#include "pdxft1.c"

/* ---- the engine: streams and dictionaries ------------------------------------ */
static pdw g_xb;                    /* the exchange block                         */
static pdw g_ws, g_wslen, g_pcap, g_cidwin;
static pdw g_kind, g_pobj, g_pgen, g_cmaplen;
static pdw g_stgseg, g_stgoff;      /* the staging window                          */

static pdw pdx_zero(void *p, pdw n) { pdw i; pdb *b = (pdb *)p; for (i = 0; i < n; i++) b[i] = 0; return n; }
static struct pdxb_strm g_st;
static pdw pdx_sopen(pdw obj, pdw gen)
{
    pdx_zero(&g_st, sizeof g_st);
    g_st.objnum = obj; g_st.gen = gen;
    pd_fcopy_out(&g_st, g_xb, 0, sizeof g_st);
    return pdx_eng(PDV_ENG_STRM_OPEN, 0, 0, 0);
}
static pdw pdx_sread(pdw seg, pdw off, pdw max)
{
    if (pdx_eng(PDV_ENG_STRM_READ, seg, off, max)) return 0;
    return pdx_eng_ax;
}
/* the whole stream into far memory; 0xFFFF when it is larger than cap or unreadable */
static pdw pdx_slurp(pdw obj, pdw gen, pdw seg, pdw off, pdw cap)
{
    pdw got, tot = 0;
    if (pdx_sopen(obj, gen)) return 0xFFFF;
    for (;;) {
        got = pdx_sread(seg, (pdw)(off + tot), (pdw)(cap - tot > 4096 ? 4096 : cap - tot));
        if (!got) break;
        tot = (pdw)(tot + got);
        if (tot >= cap) {                                  /* the next read decides: more data = too big */
            got = pdx_sread(g_stgseg, g_stgoff, 1);
            pdx_eng(PDV_ENG_STRM_CLOSE, 0, 0, 0);
            return got ? 0xFFFF : tot;
        }
    }
    pdx_eng(PDV_ENG_STRM_CLOSE, 0, 0, 0);
    return tot;
}

#include "pdxfcm.c"

/* ---- the encoding context (a simple font) ------------------------------------- */
static pdb g_elat[224];
static pdw g_eflags;
static pdw g_edcode[64], g_edoff[64], g_ednd;
static char g_epool[1024];
static pdw g_epn;

static void enc_set(const struct pdxb_fenc *e)
{
    pdw i, k;
    if (e->flags & PDFE_FIRST) { g_ednd = 0; g_epn = 0; }
    g_eflags = (pdw)(e->flags & (PDFE_SYMBOLIC | PDFE_HASENC));
    for (i = 0; i < 224; i++) g_elat[i] = e->lat[i];
    for (i = 0; i < e->ndiff && i < 12; i++) {
        pdw n = 0;
        while (n < 21 && e->diff[i].name[n]) n++;
        if (g_ednd >= 64 || g_epn + n + 1 > sizeof g_epool) break;
        g_edcode[g_ednd] = e->diff[i].code; g_edoff[g_ednd] = g_epn;
        for (k = 0; k < n; k++) g_epool[g_epn++] = e->diff[i].name[k];
        g_epool[g_epn++] = 0;
        g_ednd++;
    }
}
static const char *enc_name(pdw code)
{
    pdw i;
    pdw lat;
    for (i = 0; i < g_ednd; i++) if (g_edcode[i] == code) return g_epool + g_edoff[i];
    lat = code >= 32 ? g_elat[code - 32] : 0;
    return lat >= 32 ? nm_nth(pdx_latnames, (pdw)(lat - 32)) : 0;
}
/* a name of the forms gNN, cidNN, glyphNN, indexNN -> the number (0xFFFF: no) */
static pdw nm_numbered(const char *n)
{
    pdw v = 0, i = 0;
    if (n[0] == 'g' && n[1] == 'l') i = 5;
    else if (n[0] == 'c' && n[1] == 'i') i = 3;
    else if (n[0] == 'g') i = 1;
    else return 0xFFFF;
    if (!n[i]) return 0xFFFF;
    for (; n[i]; i++) { if (n[i] < '0' || n[i] > '9') return 0xFFFF; v = (pdw)(v * 10 + (n[i] - '0')); }
    return v;
}
static pdw enc_gid(pdw code)
{
    const char *nm = enc_name(code);
    pdw g = 0, u, c;
    if (g_kind == PDFK_TT) {
        if (g_eflags & PDFE_SYMBOLIC || (!(g_eflags & PDFE_HASENC) && !nm)) {
            if (g_cm_sym) { g = cm_get(g_cm_sym, (pdw)(0xF000 + code)); if (!g) g = cm_get(g_cm_sym, code); if (!g) g = cm_get(g_cm_sym, (pdw)(0xF100 + code)); }
            if (!g && g_cm_mac) g = cm_get(g_cm_mac, code);
            if (!g && g_cm_uni) g = cm_get(g_cm_uni, lat_uni(code));
            if (!g && nm && g_cm_uni) { u = nm_uni(nm); if (u) g = cm_get(g_cm_uni, u); }
            return g;
        }
        if (nm) {
            u = nm_uni(nm);
            if (u && g_cm_uni) g = cm_get(g_cm_uni, u);
            if (!g && u && g_cm_sym) g = cm_get(g_cm_sym, (pdw)(0xF000 + (u & 255)));
            if (!g && g_cm_mac) { c = nm_lat(nm); g = cm_get(g_cm_mac, c ? c : code); }
            if (!g && (c = nm_numbered(nm)) != 0xFFFF && c < g_sf_ng) g = c;
        }
        if (!g && g_cm_sym) g = cm_get(g_cm_sym, (pdw)(0xF000 + code));
        return g;
    }
    if (g_kind == PDFK_CFF) {
        if (g_cf_cid) return (pdw)(code < g_cf_ng ? code : 0);
        if (nm) {
            g = cf_find_name(nm);
            if (!g && (c = nm_numbered(nm)) != 0xFFFF && c < g_cf_ng) g = c;
        }
        if (!g && (!nm || !(g_eflags & PDFE_HASENC))) g = cf_enc_gid(code);
        if (!g && !nm) g = cf_enc_gid(code);
        return g;
    }
    if (g_kind == PDFK_T1) {
        if (nm) g = t1_find_name(nm);
        if (!g && (!nm || !(g_eflags & PDFE_HASENC))) g = t1_enc_gid(code);
        return g;
    }
    return 0;
}
static pdw cid_gid(pdw cid)
{
    if (g_kind == PDFK_TT) {
        if (g_cmaplen) { if (cid >= (pdw)(g_cmaplen >> 1) || cid >= PDXF_CIDWIN / 2) return 0; return pd_fpeekw(g_ws, (pdw)(g_pcap + cid * 2)) >> 0 ? (pdw)((pd_fpeekb(g_ws, (pdw)(g_pcap + cid * 2)) << 8) | pd_fpeekb(g_ws, (pdw)(g_pcap + cid * 2 + 1))) : 0; }
        return cid < g_sf_ng ? cid : 0;
    }
    if (g_kind == PDFK_CFF) return cf_cid_gid(cid);
    return 0;
}

/* ---- the resident program -------------------------------------------------------- */
static pdw fnt_open(void)
{
    pdw r;
    g_kind = PDFK_NONE; g_cf_base = 0;
    r = sf_open();
    if (r == 1) g_kind = PDFK_TT;
    else if (r == 2 && cf_open()) g_kind = PDFK_CFF;
    else if (!r && P8(0) == 1 && P8(1) == 0 && cf_open()) g_kind = PDFK_CFF;
    else if (!r && ((P8(0) == '%' && P8(1) == '!') || P8(0) == 0x80) && t1_open()) g_kind = PDFK_T1;
    return g_kind != PDFK_NONE;
}

/* one glyph's outline -> the recorder; 0 on a refusal */
static pdw fnt_outline(pdw gid)
{
    if (g_kind == PDFK_TT) return sf_glyph(gid);
    if (g_kind == PDFK_CFF) return cf_glyph(gid);
    if (g_kind == PDFK_T1) return t1_glyph(gid);
    return 0;
}

static pdw g_gadv;                  /* the last glyph's advance, 1/64 px         */
/* rasterise: em64 = size in 1/64 px. Returns bytes, 0 = empty, 0xFFFF = refused */
static pdw fnt_glyph(pdw gid, pdw em64, pdw asc, pdw rows, pdw dseg, pdw doff, pdw cap)
{
    pdw adv;
    g_em64 = em64;
    if (!fnt_outline(gid)) return 0xFFFF;
    ol_close();
    adv = g_kind == PDFK_TT ? sf_adv(gid) : g_kind == PDFK_CFF ? (pdw)g_wid : (pdw)g_t1_wid;
    g_gadv = (pdw)pdx_muldiv((pdi)adv, (pdi)em64, (pdi)g_upem);
    return rs_glyph(asc, rows, dseg, doff, cap);
}

/* ---- PDV_FNT_PATH: the recorded outline as PDFP_* ops (quadratics raised to cubics) --- */
static pdw pdx_path(pdw dseg, pdw doff, pdw cap)
{
    pdw i = 0, n = 0, k, nc;
    pdi cx = 0, cy = 0;
    #define PW(v) do { if (n + 1 > cap) return 0xFFFF; pd_fpokew(dseg, (pdw)(doff + n * 2), (pdw)(v)); n++; } while (0)
    ol_close();
    while (i < g_oln) {
        k = (pdw)g_ol[i++];
        if (k == OLM) { PW(PDFP_M); PW(g_ol[i]); PW(g_ol[i + 1]); cx = g_ol[i]; cy = g_ol[i + 1]; i += 2; }
        else if (k == OLL) { PW(PDFP_L); PW(g_ol[i]); PW(g_ol[i + 1]); cx = g_ol[i]; cy = g_ol[i + 1]; i += 2; }
        else if (k == OLQ) {
            pdi qx = g_ol[i], qy = g_ol[i + 1], ex = g_ol[i + 2], ey = g_ol[i + 3];
            PW(PDFP_C);
            PW((pdi)(cx + pdx_muldiv((pdi)(qx - cx), 2, 3))); PW((pdi)(cy + pdx_muldiv((pdi)(qy - cy), 2, 3)));
            PW((pdi)(ex + pdx_muldiv((pdi)(qx - ex), 2, 3))); PW((pdi)(ey + pdx_muldiv((pdi)(qy - ey), 2, 3)));
            PW(ex); PW(ey); cx = ex; cy = ey; i += 4;
        } else if (k == OLC) { PW(PDFP_C); for (nc = 0; nc < 6; nc++) PW(g_ol[i + nc]); cx = g_ol[i + 4]; cy = g_ol[i + 5]; i += 6; }
        else if (k == OLZ) PW(PDFP_Z);
        else break;
    }
    #undef PW
    return n;
}

/* ---- the verbs ---------------------------------------------------------------------- */
static union {
    struct pdxb_fload ld; struct pdxb_fenc en; struct pdxb_fgid gi; struct pdxb_fglyph gl;
    struct pdxb_fpath pa; struct pdxb_fcmap cm; struct pdxb_fwid wi; struct pdxb_init in;
} g_a;
static pdw g_cf;                    /* the carry flag of the last verb (the door reads it) */
static pdw g_pdetail;

#include "pdxfwd.c"

static pdw v_load(void)
{
    struct pdxb_fload *l = &g_a.ld;
    pdw n, k, bot;
    g_kind = PDFK_NONE; g_cmaplen = 0; g_pobj = g_pgen = 0;
    l->flags = 0; l->kind = 0; l->nglyphs = 0; l->upem = 0; l->detail = 0;
    n = pdx_slurp(l->objnum, l->gen, g_ws, 0, g_pcap);
    if (n == 0xFFFF) {
        l->detail = PDXF_E_SIZE; l->flags |= PDFKF_CHOP;
        return PDE_LIMIT;
    }
    if (!n) { l->detail = PDXF_E_READ; return PDE_FONT; }
    g_pseg = g_ws; g_plen = n;
    if (l->cidmap_obj) {
        k = pdx_slurp(l->cidmap_obj, l->cidmap_gen, g_ws, g_pcap, PDXF_CIDWIN);
        if (k != 0xFFFF) { g_cmaplen = k; l->flags |= PDFKF_MAPPED; }
        else { g_cmaplen = PDXF_CIDWIN; l->flags |= PDFKF_MAPPED; }          /* the first 2,048 CIDs */
    }
    if (!fnt_open()) { l->detail = PDXF_E_FORMAT; return PDE_FONT; }
    g_pobj = l->objnum; g_pgen = l->gen;
    l->kind = g_kind;
    l->upem = g_upem;
    if (g_kind == PDFK_TT) l->nglyphs = g_sf_ng;
    else if (g_kind == PDFK_CFF) { l->nglyphs = g_cf_ng; if (g_cf_cid) l->flags |= PDFKF_CID; if (g_cf_base) l->flags |= PDFKF_OTTO; }
    else l->nglyphs = (pdw)(g_t1_ng + 1);
    l->bbox_top = 900; bot = 250;
    if (g_kind == PDFK_TT && g_upem) {
        pdi t = pdx_muldiv(g_sf_asc, 1000, (pdi)g_upem), b = pdx_muldiv((pdi)-g_sf_desc, 1000, (pdi)g_upem);
        if (t > 0 && t < 2000) l->bbox_top = (pdw)t;
        if (b >= 0 && b < 1000) bot = (pdw)b;
    }
    l->bbox_bot = bot;
    return 0;
}

static pdw v_glyph(void)
{
    struct pdxb_fglyph *g = &g_a.gl;
    pdw r;
    if (g_kind == PDFK_NONE) return PDE_NOTFOUND;
    r = fnt_glyph(g->gid, g->em64, g->asc, g->rows, g->dseg, g->doff, g->cap);
    g->adv64 = g_gadv;
    if (r == 0xFFFF) { g->w = 0xFFFF; g->lb = 0; g->bytes = 0; return 0; }
    g->w = g_rwpx; g->lb = g_rlb; g->bytes = r;
    return 0;
}

static pdw v_path(void)
{
    struct pdxb_fpath *p = &g_a.pa;
    pdw n;
    if (g_kind == PDFK_NONE) return PDE_NOTFOUND;
    if (!fnt_outline(p->gid)) { p->len = 0; return PDE_FONT; }
    n = pdx_path(p->dseg, p->doff, p->cap);
    if (n == 0xFFFF) return PDE_LIMIT;
    p->len = n; p->upem = g_upem;
    p->adv = (pdw)pdx_muldiv((pdi)(g_kind == PDFK_TT ? sf_adv(p->gid) : g_kind == PDFK_CFF ? (pdw)g_wid : (pdw)g_t1_wid), 1000, (pdi)g_upem);
    return 0;
}

static pdw v_cmap(void)
{
    struct pdxb_fcmap *c = &g_a.cm;
    pdw r;
    c->len = 0; c->flags = 0;
    if (!c->objnum) {                                              /* a predefined CMap: Identity-H/V only */
        if (nm_eq(c->name, "Identity-H") || nm_eq(c->name, "Identity-V")) { c->flags = (pdw)(PDFC_IDENT | (c->name[9] == 'V' ? PDFC_VERT : 0)); return 0; }
        return PDE_NOTFOUND;
    }
    if (pdx_sopen(c->objnum, c->gen)) return PDE_FONT;
    r = cm_parse(c->kind, c->dseg, c->doff, c->cap);
    pdx_eng(PDV_ENG_STRM_CLOSE, 0, 0, 0);
    if (r) return r;
    c->len = (pdw)(4 + g_cmcnt * 6); c->flags = g_cmflags;
    return 0;
}

/* the module's entry point: the door passes AL, BX, CX, DX. Returns AX; g_cf = the carry */
pdw pdxf_verb(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdw r = 0;
    (void)cx; (void)dx;
    g_cf = 0;
    if (verb == PDV_X_INIT) {
        pd_fcopy_in(bx, 0, &g_a.in, sizeof g_a.in);
        g_xb = g_a.in.xb_seg;
        g_ws = g_a.in.slot_seg;
        g_wslen = (pdw)(g_a.in.slot_kb >= 64 ? 0xFFF0u : (pdw)(g_a.in.slot_kb << 10));
        if (!g_ws || g_wslen < PDXF_CIDWIN + PDXF_STG + 4096) { g_cf = 1; return PDE_NOSLOT; }
        g_pcap = (pdw)(g_wslen - PDXF_CIDWIN - PDXF_STG);
        g_stgseg = g_ws; g_stgoff = (pdw)(g_pcap + PDXF_CIDWIN);
        g_kind = PDFK_NONE;
        return 0;
    }
    if (!g_ws) { g_cf = 1; return PDE_NOSLOT; }
    pdx_zero(&g_a, sizeof g_a);
    switch (verb) {
    case PDV_FNT_LOAD:   pd_fcopy_in(g_xb, 0, &g_a.ld, sizeof g_a.ld); r = v_load(); pd_fcopy_out(&g_a.ld, g_xb, 0, sizeof g_a.ld); break;
    case PDV_FNT_ENC:    pd_fcopy_in(g_xb, 0, &g_a.en, sizeof g_a.en); enc_set(&g_a.en); break;
    case PDV_FNT_GID:
        pd_fcopy_in(g_xb, 0, &g_a.gi, sizeof g_a.gi);
        if (g_kind == PDFK_NONE) { r = PDE_NOTFOUND; break; }
        g_a.gi.gid = g_a.gi.kind ? cid_gid(g_a.gi.code) : enc_gid(g_a.gi.code);
        pd_fcopy_out(&g_a.gi, g_xb, 0, sizeof g_a.gi); break;
    case PDV_FNT_GLYPH:  pd_fcopy_in(g_xb, 0, &g_a.gl, sizeof g_a.gl); r = v_glyph(); pd_fcopy_out(&g_a.gl, g_xb, 0, sizeof g_a.gl); break;
    case PDV_FNT_PATH:   pd_fcopy_in(g_xb, 0, &g_a.pa, sizeof g_a.pa); r = v_path(); pd_fcopy_out(&g_a.pa, g_xb, 0, sizeof g_a.pa); break;
    case PDV_FNT_CMAP:   pd_fcopy_in(g_xb, 0, &g_a.cm, sizeof g_a.cm); r = v_cmap(); pd_fcopy_out(&g_a.cm, g_xb, 0, sizeof g_a.cm); break;
    case PDV_FNT_WIDTHS: pd_fcopy_in(g_xb, 0, &g_a.wi, sizeof g_a.wi); r = v_widths(); pd_fcopy_out(&g_a.wi, g_xb, 0, sizeof g_a.wi); break;
    default: r = PDE_RANGE; break;
    }
    if (r) g_cf = 1;
    return r;
}
