/* ============================================================================
 * os8088 - apps/pagedesk/pdxf.c        PDXF.MOD: font programs (PAGEDESK-SPEC 20.22.8)
 *
 * One translation unit: this file #includes the readers. The door (pdxf.asm)
 * calls pdxf_verb(). On the host, hosttest/pdx_host.c / pdx_real.c include this
 * file with PD_HOST and supply pdx_muldiv(), pdx_eng() and the far-memory arena
 * (pdmath.c).
 *
 * The program lives in the WORKSPACE (slot_seg, 20.22.2.3):
 *   [0, pcap)               the font program (one at a time), pcap = 48 KiB - 10,752
 *   [pcap, + 4096)          the CIDToGIDMap window (CIDs 0..2047)
 *   [.., + 512)             the staging window of the CMap tokeniser
 *   [.., + 3 x 2048)        the ToUnicode, Encoding-CMap and /W tables
 * ==========================================================================*/
#ifndef PD_HOST
#include "os88.h"
#endif
#include "pdabi.h"
#include "pdmath.h"

/* trunc-to-nearest(a*b/c), 32-bit intermediate, c != 0. Assembly in pdxf.asm */
pdi pdx_muldiv(pdi a, pdi b, pdi c);
/* one far call into PDENG's door; 0 or the PDE_ code, AX in pdx_eng_ax */
pdw pdx_eng(pdw verb, pdw bx, pdw cx, pdw dx);
#ifdef PD_HOST
extern pdw pdx_eng_ax;
#else
pdw pdx_eng_ax;                     /* the AX of the last successful pdx_eng (the door sets it) */
#endif

static pdw g_upem, g_em64;          /* units per em; the size in 1/64 px         */

#define PDXF_WS_KB   48             /* the workspace this module asks for (20.22.2.3)  */
#define PDXF_CIDWIN  4096           /* the CIDToGIDMap window (CIDs 0..2047)             */
#define PDXF_STG     512            /* the CMap tokeniser's staging window               */
#define PDXF_TAB     2048           /* each of the ToUnicode, Encoding-CMap and /W tables */

#include "pdxfol.c"
static pdw g_cf_base;
#include "pdxfnm.c"
#include "pdxfsf.c"
#include "pdxfcf.c"
#include "pdxft1.c"

/* CMap table flags (a PDXF-private table: +1 u8 flags) */
#define PDFC_ENC   0
#define PDFC_UNI   1
#define PDFC_IDENT 0x0001       /* codes not listed map to themselves (usecmap of an identity CMap) */
#define PDFC_VERT  0x0002
#define PDFC_TRUNC 0x0004       /* the table did not fit: covered codes are right                    */
#define PDFC_MIXED 0x0008       /* a mixed-length codespace: read as 2 bytes                         */
#define EC_SYM 0x0001           /* the descriptor says symbolic                                      */
#define EC_HAS 0x0002           /* the font dictionary has an /Encoding                              */

/* ---- the engine: streams and dictionaries ------------------------------------ */
static pdw g_xb;                    /* the exchange block                         */
static pdw g_ws, g_wslen, g_pcap;
static pdw g_kind, g_cmaplen;
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
#include "pdxfwd.c"
#include "pdrenc.h"

/* ---- the encoding context (a simple font) ---------------------------------------- */
static pdb g_elat[224];
static pdw g_eflags;
static pdw g_edcode[64], g_edoff[64], g_ednd;
static char g_epool[1024];
static pdw g_epn;

static const char *enc_name(pdw code)
{
    pdw i, lat;
    for (i = 0; i < g_ednd; i++) if (g_edcode[i] == code) return g_epool + g_edoff[i];
    lat = code >= 32 ? g_elat[code - 32] : 0;
    return lat >= 32 ? nm_nth(pdx_latnames, (pdw)(lat - 32)) : 0;
}
/* a name of the forms gNN, cidNN, glyphNN -> the number (0xFFFF: no) */
static pdw nm_numbered(const char *n)
{
    pdw v = 0, i = 0;
    if (n[0] == 'g' && n[1] == 'l') i = 5;
    else if (n[0] == 'c' && n[1] == 'i') i = 3;
    else if (n[0] == 'g') i = 1;
    else return 0xFFFF;
    if (!n[i]) return 0xFFFF;
    for (; n[i]; i++) { if (n[i] < '0' || n[i] > '9') return 0xFFFF; v = (pdw)(X10(v) + (n[i] - '0')); }
    return v;
}
static pdw enc_gid(pdw code)
{
    const char *nm = enc_name(code);
    pdw g = 0, u, c;
    if (g_kind == PDFK_TT) {
        if (g_eflags & EC_SYM || (!(g_eflags & EC_HAS) && !nm)) {
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
        if (!g && (!nm || !(g_eflags & EC_HAS))) g = cf_enc_gid(code);
        return g;
    }
    if (g_kind == PDFK_T1) {
        if (nm) g = t1_find_name(nm);
        if (!g && (!nm || !(g_eflags & EC_HAS))) g = t1_enc_gid(code);
        return g;
    }
    return 0;
}
static pdw cid_gid(pdw cid)
{
    if (g_kind == PDFK_TT) {
        if (g_cmaplen) {
            if (cid >= (pdw)(g_cmaplen >> 1) || cid >= PDXF_CIDWIN / 2) return 0;
            return (pdw)((pd_fpeekb(g_ws, (pdw)(g_pcap + cid * 2)) << 8) | pd_fpeekb(g_ws, (pdw)(g_pcap + cid * 2 + 1)));
        }
        return cid < g_sf_ng ? cid : 0;
    }
    if (g_kind == PDFK_CFF) return cf_cid_gid(cid);
    return 0;
}

/* ---- the resident program ------------------------------------------------------------ */
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
static pdw fnt_outline(pdw gid)
{
    if (g_kind == PDFK_TT) return sf_glyph(gid);
    if (g_kind == PDFK_CFF) return cf_glyph(gid);
    if (g_kind == PDFK_T1) return t1_glyph(gid);
    return 0;
}
static pdw g_gadv;                  /* the last glyph's advance, 1/64 px         */
static pdw fnt_adv(pdw gid)         /* the program's advance in font units        */
{
    return g_kind == PDFK_TT ? sf_adv(gid) : g_kind == PDFK_CFF ? (pdw)g_wid : (pdw)g_t1_wid;
}
/* rasterise: em64 = size in 1/64 px. Returns bytes, 0 = empty, 0xFFFF = refused */
static pdw fnt_glyph(pdw gid, pdw em64, pdw asc, pdw rows, pdw dseg, pdw doff, pdw cap)
{
    g_em64 = em64;
    if (!fnt_outline(gid)) return 0xFFFF;
    ol_close();
    g_gadv = (pdw)pdx_muldiv((pdi)fnt_adv(gid), (pdi)em64, (pdi)g_upem);
    return rs_glyph(asc, rows, dseg, doff, cap);
}

/* ---- PDV_FNT_PATH: the recorded outline as PDFP_* ops (quadratics raised to cubics) ---- */
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

/* ---- the font dictionary: activate a font (read its program and its tables) ------------ */
static pdw g_fobj, g_fgen;          /* the font whose program is resident (objnum 0 = none) */
static pdw g_ft0, g_fvert, g_ftu, g_fcm, g_fwl, g_fdw, g_fwrc, g_ferr;
static pdw g_tuoff, g_cmoff2, g_wdoff, g_dobj, g_dgen, g_ffobj, g_ffgen, g_cmapobj, g_cmapgen, g_nmode;
static struct pdxb_dictget g_dg2;

/* a dictionary value that is a name: compare it to s */
static pdw fd_name_is(const char *s) { return g_dg.kind == PDK_NAME && nm_eq(g_dg.text, s); }
static pdw fd_ref(pdw obj, pdw gen, const char *a, const char *b, const char *c)      /* 1 + the reference in g_ro/g_rg */
{
    pdw r;
    g_wdraw = 1; r = wd_get(obj, gen, a, b, c); g_wdraw = 0;
    return !r && g_dg.kind == PDK_REF;
}
static pdw g_ro, g_rg;
static pdw fd_refs(pdw obj, pdw gen, const char *a, const char *b, const char *c)
{
    if (!fd_ref(obj, gen, a, b, c)) return 0;
    g_ro = g_dg.refnum; g_rg = g_dg.refgen;
    return 1;
}
/* the base encoding: 0 Standard, 1 WinAnsi, 2 MacRoman, applied to g_elat (PD Latin, as pdrf.c's font_encoding) */
static void fd_baseenc(pdw mode)
{
    pdw c, v;
    for (c = 32; c < 256; c++) {
        v = c;
        if (mode == 0) {
            if (c == 0x27) v = 0x92; else if (c == 0x60) v = 0x91;
            else if (c >= 0xA0) v = enc_std_hi[c - 0xA0];
            else if (c >= 0x80) v = 0;
        } else if (mode == 2) { if (c >= 0x80) v = enc_mac_hi[c - 0x80]; }
        g_elat[c - 32] = (pdb)v;
    }
}
static pdw fd_enc_name(void)                       /* g_dg is a name: its mode, 3 = something else */
{
    if (nm_eq(g_dg.text, "WinAnsiEncoding")) return 1;
    if (nm_eq(g_dg.text, "MacRomanEncoding")) return 2;
    if (nm_eq(g_dg.text, "StandardEncoding")) return 0;
    return 3;
}
static void fd_simple_enc(pdw fobj, pdw fgen, pdw symbolic)
{
    pdw mode = 0, nd, i, code = 0, n, k;
    g_eflags = (pdw)(symbolic ? EC_SYM : 0); g_ednd = 0; g_epn = 0;
    if (!wd_get(fobj, fgen, "Encoding", 0, 0)) {
        if (g_dg.kind == PDK_NAME) { g_eflags |= EC_HAS; mode = fd_enc_name(); if (mode == 3) mode = 0; }
        else if (g_dg.kind == PDK_DICT || g_dg.kind == PDK_REF) {
            g_eflags |= EC_HAS; mode = 1;
            if (!wd_get(fobj, fgen, "Encoding", "BaseEncoding", 0) && g_dg.kind == PDK_NAME) { mode = fd_enc_name(); if (mode == 3) mode = 1; }
        }
    }
    fd_baseenc(mode);
    if (wd_get(fobj, fgen, "Encoding", "Differences", 0) || g_dg.kind != PDK_ARR) return;
    nd = g_dg.count;
    for (i = 0; i < nd && i < 400; i++) {
        if (wd_get(fobj, fgen, "Encoding", "Differences", wd_key(0, i))) break;
        if (g_dg.kind == PDK_INT || g_dg.kind == PDK_REAL) code = (pdw)pdm_fx_floor(&g_dg.num);
        else if (g_dg.kind == PDK_NAME) {
            if (g_ednd < 64) {
                n = 0; while (n < 30 && g_dg.text[n]) n++;
                if (g_epn + n + 1 <= sizeof g_epool) {
                    g_edcode[g_ednd] = code; g_edoff[g_ednd] = g_epn;
                    for (k = 0; k < n; k++) g_epool[g_epn++] = g_dg.text[k];
                    g_epool[g_epn++] = 0; g_ednd++;
                }
            }
            code++;
        }
    }
}

/* load the font (its dictionary, program and tables); 0 ok, else a PDFE_* in g_ferr */
static pdw fd_activate(pdw fobj, pdw fgen)
{
    pdw n, sym = 0, cidobj = 0, cidgen = 0, tuobj = 0, tugen = 0;
    g_fobj = 0; g_kind = PDFK_NONE; g_cmaplen = 0; g_ft0 = 0; g_fvert = 0; g_ftu = 0; g_fcm = 0; g_fdw = 1000; g_fwl = 0; g_ferr = 0;
    g_dobj = fobj; g_dgen = fgen; g_cmapobj = 0;
    if (!wd_get(fobj, fgen, "Subtype", 0, 0) && fd_name_is("Type0")) {
        g_ft0 = 1;
        if (!fd_refs(fobj, fgen, "DescendantFonts", "[0]", 0)) { g_ferr = PDFE_NOPROG; return PDE_FONT; }
        g_dobj = g_ro; g_dgen = g_rg;
        if (fd_refs(fobj, fgen, "Encoding", 0, 0)) { g_cmapobj = g_ro; g_cmapgen = g_rg; }
        else if (!wd_get(fobj, fgen, "Encoding", 0, 0) && g_dg.kind == PDK_NAME) {
            if (nm_eq(g_dg.text, "Identity-H") || nm_eq(g_dg.text, "Identity-V")) { if (g_dg.text[9] == 'V') g_fvert = 1; }
            else { g_ferr = PDFE_CMAP; }                      /* a predefined CJK CMap: codes read as 2-byte identity */
        }
        if (fd_refs(g_dobj, g_dgen, "CIDToGIDMap", 0, 0)) { cidobj = g_ro; cidgen = g_rg; }
    }
    if (fd_refs(fobj, fgen, "ToUnicode", 0, 0)) { tuobj = g_ro; tugen = g_rg; }
    if (!wd_get(g_dobj, g_dgen, "FontDescriptor", "Flags", 0) && (g_dg.kind == PDK_INT)) sym = (pdw)((pdm_fx_floor(&g_dg.num) & 4) != 0);
    g_ffobj = 0;
    if (fd_refs(g_dobj, g_dgen, "FontDescriptor", "FontFile2", 0) || fd_refs(g_dobj, g_dgen, "FontDescriptor", "FontFile3", 0) ||
        fd_refs(g_dobj, g_dgen, "FontDescriptor", "FontFile", 0)) { g_ffobj = g_ro; g_ffgen = g_rg; }
    if (!g_ffobj) { g_ferr = PDFE_NOPROG; return PDE_FONT; }
    n = pdx_slurp(g_ffobj, g_ffgen, g_ws, 0, g_pcap);
    if (n == 0xFFFF) { g_ferr = PDFE_SIZE; return PDE_LIMIT; }
    if (!n) { g_ferr = PDFE_READ; return PDE_FONT; }
    g_pseg = g_ws; g_plen = n;
    if (cidobj) {
        n = pdx_slurp(cidobj, cidgen, g_ws, g_pcap, PDXF_CIDWIN);
        g_cmaplen = n == 0xFFFF ? PDXF_CIDWIN : n;
    }
    if (!fnt_open()) { g_ferr = PDFE_FORMAT; return PDE_FONT; }
    /* the tables */
    g_tuoff = (pdw)(g_pcap + PDXF_CIDWIN + PDXF_STG); g_cmoff2 = (pdw)(g_tuoff + PDXF_TAB); g_wdoff = (pdw)(g_cmoff2 + PDXF_TAB);
    if (tuobj && !pdx_sopen(tuobj, tugen)) {
        if (!cm_parse(PDFC_UNI, g_ws, g_tuoff, PDXF_TAB)) g_ftu = 1;
        pdx_eng(PDV_ENG_STRM_CLOSE, 0, 0, 0);
    }
    if (g_ft0) {
        if (g_cmapobj && !pdx_sopen(g_cmapobj, g_cmapgen)) {
            if (!cm_parse(PDFC_ENC, g_ws, g_cmoff2, PDXF_TAB)) g_fcm = 1;
            pdx_eng(PDV_ENG_STRM_CLOSE, 0, 0, 0);
        }
        g_fwl = wd_ranges(g_dobj, g_dgen, g_ws, g_wdoff, PDXF_TAB); g_fdw = g_wdw;
    } else fd_simple_enc(fobj, fgen, sym);
    g_fobj = fobj; g_fgen = fgen;
    return 0;
}

/* value of the table at (off) for `code`: found in g_tfound */
static pdw g_tfound;
static pdw tab_find(pdw off, pdw code)
{
    pdw n = pd_fpeekw(g_ws, (pdw)(off + 2)), lo = 0, hi = n, m, e;
    g_tfound = 0;
    if (n > PDXF_TAB / 6) n = hi = (pdw)(PDXF_TAB / 6 - 1);
    while (lo < hi) {
        m = (pdw)((lo + hi) >> 1);
        if (pd_fpeekw(g_ws, (pdw)(off + 4 + X6(m))) <= code) lo = (pdw)(m + 1); else hi = m;
    }
    if (!lo) return 0;
    e = (pdw)(off + 4 + X6(lo - 1));
    if (code <= pd_fpeekw(g_ws, (pdw)(e + 2))) { g_tfound = 1; return (pdw)(pd_fpeekw(g_ws, (pdw)(e + 4)) + (code - pd_fpeekw(g_ws, e))); }
    return 0;
}
/* the /W range table: (first, last, width) triples, sorted */
static pdw cid_width(pdw cid)
{
    pdw n = (pdw)(g_fwl / 6), k;
    for (k = 0; k < n; k++)
        if (cid >= pd_fpeekw(g_ws, (pdw)(g_wdoff + X6(k))) && cid <= pd_fpeekw(g_ws, (pdw)(g_wdoff + X6(k) + 2))) return pd_fpeekw(g_ws, (pdw)(g_wdoff + X6(k) + 4));
    return g_fdw;
}

/* the common front of GLYPH and PATH: the font resident, the code mapped to a glyph index */
static pdw g_cid, g_adv1000, g_lat, g_gid;
static pdw fd_map(pdw fobj, pdw fgen, pdw code)
{
    pdw gid, v;
    if (g_fobj != fobj || g_fgen != fgen) { pdw r = fd_activate(fobj, fgen); if (r) return r; }
    g_cid = 0; g_adv1000 = 0; g_lat = 0xFFFF;
    if (g_ft0) {
        v = code;
        if (g_fcm) { v = tab_find(g_cmoff2, code); if (!g_tfound) v = (pdw)((pd_fpeekb(g_ws, (pdw)(g_cmoff2 + 1)) & PDFC_IDENT) ? code : 0); }
        g_cid = v; g_adv1000 = cid_width(v);
        gid = cid_gid(v);
    } else gid = enc_gid(code);
    if (g_ftu) { v = tab_find(g_tuoff, code); g_lat = g_tfound ? uni_lat(v) : 0; }
    g_gid = gid;
    return 0;
}

/* ---- the verbs --------------------------------------------------------------------------- */
pdw pdx_cf;                         /* the carry flag of the last verb (the door reads it)   */
pdw pdx_engvec[2];                  /* PDENG's door, offset then segment (PDV_X_INIT)         */
static union { struct pdxb_fglyph gl; struct pdxb_fpath pa; struct pdxb_init in; } g_a;

/* PDFG_BLOCK, step 1: the block of (font, size) in the header at (dseg, doff): found, or made (a free one, else one
 * not used in this pass); returns its index, 0xFFFF when none can be had */
static pdw blk_find(struct pdxb_fglyph *g)
{
    pdw seg = g->dseg, hdr = g->doff, n, i, e, px = (pdw)(g->em64 >> 6), cap = g->cap, b;
    if (pd_fpeekw(seg, (pdw)(hdr + 62)) != PDFB_MAGIC) { pd_ffill(seg, hdr, 0, PDFB_HDR); pd_fpokew(seg, (pdw)(hdr + 62), PDFB_MAGIC); }
    n = pd_fpeekw(seg, (pdw)(hdr + 60));
    if (n > cap) n = cap;
    for (i = 0; i < n; i++) {
        e = (pdw)(hdr + (i << 3));
        if (pd_fpeekw(seg, e) == g->objnum && pd_fpeekw(seg, (pdw)(e + 2)) == g->gen && pd_fpeekw(seg, (pdw)(e + 4)) == px) {
            pd_fpokew(seg, (pdw)(e + 6), g->pass); return i;
        }
    }
    if (n < cap) { i = n; pd_fpokew(seg, (pdw)(hdr + 60), (pdw)(n + 1)); }
    else {
        for (i = 0; i < cap && pd_fpeekw(seg, (pdw)(hdr + (i << 3) + 6)) == g->pass; i++) ;
        if (i == cap) return 0xFFFF;
    }
    e = (pdw)(hdr + (i << 3));
    pd_fpokew(seg, e, g->objnum); pd_fpokew(seg, (pdw)(e + 2), g->gen); pd_fpokew(seg, (pdw)(e + 4), px); pd_fpokew(seg, (pdw)(e + 6), g->pass);
    b = (pdw)(hdr + PDFB_HDR + i * PDFB_SIZE);
    pd_ffill(seg, b, 0, PDFB_SIZE);
    pd_ffill(seg, (pdw)(b + PDFB_KEYS), 0xFF, 256);
    return i;
}
/* step 2: the code's slot in the block at `boff`, its info and (px != 0) its bitmap */
static pdw blk_slot(struct pdxb_fglyph *g, pdw boff)
{
    pdw h = (pdw)((g->code ^ (g->code >> 7)) & 127), i, k, kp, ip, flags, bump, rp, n, rows, px = (pdw)(g->em64 >> 6), seg = g->dseg;
    g->slot = 0;
    if (g->code == 0xFFFF) return 0;
    for (i = 0; i < 128; i++, h = (pdw)((h + 1) & 127)) {
        kp = (pdw)(boff + PDFB_KEYS + h * 2); k = pd_fpeekw(seg, kp);
        if (k == g->code) break;
        if (k == 0xFFFF) { pd_fpokew(seg, kp, g->code); break; }
    }
    if (i == 128) return PDE_LIMIT;                                   /* the block's keys are full */
    g->slot = (pdw)(h + 1);
    ip = (pdw)(boff + PDFB_INFO + h * 4);
    flags = pd_fpeekb(seg, (pdw)(ip + 3));
    pd_fpokew(seg, ip, g->adv1000);
    pd_fpokeb(seg, (pdw)(ip + 2), (pdb)(g_lat == 0xFFFF ? 255 : g_lat));          /* 255: no /ToUnicode */
    if (!px || (flags & (PDFB_F_BMP | PDFB_F_BIG))) return 0;
    if (!g_gid) { pd_fpokeb(seg, (pdw)(ip + 3), (pdb)(flags | PDFB_F_BMP)); return 0; }
    rp = (pdw)(boff + h * 4);                                          /* the record of slot (h + 1) */
    bump = pd_fpeekw(seg, (pdw)(boff + PDFB_KEYS - 4));
    rows = (pdw)(px + (px >> 2) + 1);
    n = fnt_glyph(g_gid, g->em64, px, rows, seg, (pdw)(boff + 512 + bump), (pdw)(PDFB_KEYS - 4 - 512 - bump));
    g->adv64 = g_gadv;
    if (n == 0xFFFF) { pd_fpokeb(seg, (pdw)(ip + 3), (pdb)(flags | PDFB_F_BIG)); return 0; }
    pd_fpokeb(seg, rp, (pdb)g_rwpx); pd_fpokeb(seg, (pdw)(rp + 1), (pdb)g_rlb);
    pd_fpokew(seg, (pdw)(rp + 2), bump);
    pd_fpokew(seg, (pdw)(boff + PDFB_KEYS - 4), (pdw)(bump + ((n + 1) & 0xFFFE)));
    pd_fpokeb(seg, (pdw)(ip + 3), (pdb)(flags | PDFB_F_BMP));
    g->w = g_rwpx; g->lb = g_rlb; g->nb = n;
    return 0;
}

static pdw v_glyph(void)
{
    struct pdxb_fglyph *g = &g_a.gl;
    pdw r, n;
    g->w = 0; g->lb = 0; g->nb = 0; g->adv64 = 0; g->err = 0; g->oflags = 0;
    r = fd_map(g->objnum, g->gen, g->code);
    if (r) { g->err = g_ferr; return r; }
    g->gid = g_gid; g->cid = g_cid; g->adv1000 = g_adv1000; g->kind = g_kind;
    g->lat = g_lat == 0xFFFF ? 0 : g_lat;
    g->oflags = (pdw)((g_ftu ? PDFG_LATIN : 0) | (g_ft0 ? PDFG_T0 : 0) | (g_fvert ? PDFG_VERT : 0));
    if (g->flags & PDFG_BLOCK) {
        g->blk = blk_find(g);
        if (g->blk == 0xFFFF) { g->slot = 0; return 0; }
        return blk_slot(g, (pdw)(g->doff + PDFB_HDR + g->blk * PDFB_SIZE));
    }
    if (g->flags & PDFG_INFO) return 0;
    if (!g_gid) { g->w = 0; return 0; }                         /* no glyph: nothing to draw */
    n = fnt_glyph(g_gid, g->em64, g->asc, g->rows, g->dseg, g->doff, g->cap);
    g->adv64 = g_gadv;
    if (n == 0xFFFF) { g->w = 0xFFFF; return 0; }
    g->w = g_rwpx; g->lb = g_rlb; g->nb = n;
    return 0;
}
static pdw v_path(void)
{
    struct pdxb_fpath *p = &g_a.pa;
    pdw r, n;
    p->len = 0; p->err = 0;
    r = fd_map(p->objnum, p->gen, p->code);
    if (r) { p->err = g_ferr; return r; }
    if (!g_gid || !fnt_outline(g_gid)) return 0;
    n = pdx_path(p->dseg, p->doff, p->cap);
    if (n == 0xFFFF) return PDE_LIMIT;
    p->len = n; p->upem = g_upem;
    p->adv1000 = (pdw)pdx_muldiv((pdi)fnt_adv(g_gid), 1000, (pdi)g_upem);
    return 0;
}

/* the module's entry point: the door passes AL, BX, CX, DX. Returns AX; pdx_cf = the carry */
pdw pdxf_verb(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdw r = 0;
    (void)cx; (void)dx;
    pdx_cf = 0;
    if (verb == PDV_X_INIT) {
        pd_fcopy_in(bx, 0, &g_a.in, sizeof g_a.in);
        g_xb = g_a.in.xb_seg;
        pdx_engvec[0] = g_a.in.eng_off; pdx_engvec[1] = g_a.in.eng_seg;
        g_ws = g_a.in.slot_seg;
        g_wslen = (pdw)(g_a.in.slot_kb >= 64 ? 0xFFF0u : (pdw)(g_a.in.slot_kb << 10));
        if (!g_ws || g_wslen < PDXF_CIDWIN + PDXF_STG + 3 * PDXF_TAB + 4096) { pdx_cf = 1; return PDE_NOSLOT; }
        g_pcap = (pdw)(g_wslen - PDXF_CIDWIN - PDXF_STG - 3 * PDXF_TAB);
        g_stgseg = g_ws; g_stgoff = (pdw)(g_pcap + PDXF_CIDWIN);
        g_kind = PDFK_NONE; g_fobj = 0;
        return 0;
    }
    if (!g_ws) { pdx_cf = 1; return PDE_NOSLOT; }
    switch (verb) {
    case PDV_FNT_GLYPH: pd_fcopy_in(g_xb, 0, &g_a.gl, sizeof g_a.gl); r = v_glyph(); pd_fcopy_out(&g_a.gl, g_xb, 0, sizeof g_a.gl); break;
    case PDV_FNT_PATH:  pd_fcopy_in(g_xb, 0, &g_a.pa, sizeof g_a.pa); r = v_path(); pd_fcopy_out(&g_a.pa, g_xb, 0, sizeof g_a.pa); break;
    default: r = PDE_RANGE; break;
    }
    if (r) pdx_cf = 1;
    return r;
}
