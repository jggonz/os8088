/* ============================================================================
 * os8088 - apps/pagedesk/pdrf.c      PDREND: PDENG plumbing, fonts, widths
 *
 * #included by pdrend.c. PAGEDESK-SPEC 11.1-11.4, 20.3.4.
 * ==========================================================================*/
#include "pdrenc.h"

/* ---- the exchange block -------------------------------------------------- */
static pdw g_xb;                    /* XB segment (PDV_REND_INIT)             */

static pdw pdr_slen(const char *s) { pdw n = 0; while (s[n]) n++; return n; }
static void pdr_scpy(char *d, const char *s, pdw cap)
{
    pdw i;
    for (i = 0; i + 1 < cap && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}
static pdw pdr_seq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* copy a struct to the XB, call a PDENG verb, copy the answer back */
static pdw eng_call(pdw verb, void *st, pdw n, pdw bx, pdw cx, pdw dx)
{
    pdw err;
    if (n) pd_fcopy_out(st, g_xb, 0, n);
    err = pdr_eng(verb, bx, cx, dx);
    if (!err && n) pd_fcopy_in(g_xb, 0, st, n);
    return err;
}

/* dictionary query: object (0/0 = none) and a key path of up to three names.
 * A path element "#n" (decimal) selects element n of the array reached so
 * far (PAGEDESK-SPEC 20.3.3, agreed with PDENG).                           */
static struct pdxb_dictget g_dg;
static pdw dict_get(pdw obj, pdw gen, const char *k0, const char *k1, const char *k2)
{
    pdr_zero(&g_dg, sizeof g_dg);
    g_dg.objnum = obj; g_dg.gen = gen;
    pdr_scpy(g_dg.key[0], k0, 32);
    if (k1) pdr_scpy(g_dg.key[1], k1, 32);
    if (k2) pdr_scpy(g_dg.key[2], k2, 32);
    return eng_call(PDV_ENG_DICT_GET, &g_dg, sizeof g_dg, 0, 0, 0);
}
/* "#n" with n in 0..9999 */
static char g_idx[8];
static const char *idx_key(pdw n)
{
    static char t[6]; pdw k = 0, i;
    g_idx[0] = '#';
    do { t[k++] = (char)('0' + n % 10); n /= 10; } while (n);
    for (i = 0; i < k; i++) g_idx[1 + i] = t[k - 1 - i];
    g_idx[1 + k] = 0;
    return g_idx;
}

/* ---- the PDRES bank: strikes (11.2) and the extension (20.3.4) ----------- */
static pdw g_wid_off, g_nam_off;    /* from the 'PDFX' record, 0 = absent     */

/* index_seg holds a PDRES-format file's INDEX from byte 0: 'PDRS', the
 * directory, then the PDFB bank at the offset D of directory kind 1 (a
 * multiple of 16, 20.4.1). Returns the bank's segment, or 0 */
static pdw index_bank(pdw index_seg, pdw *nstrikes)
{
    pdw n, i, k, d = 0, seg;
    if (!index_seg || pd_fpeekw(index_seg, 0) != 0x4450 || pd_fpeekw(index_seg, 2) != 0x5352) return 0;   /* "PDRS" */
    n = pd_fpeekw(index_seg, 6);
    for (i = 0; i < n && i < 16; i++) {
        k = pd_fpeekw(index_seg, 8 + i * 12);
        if (k == 1) { d = pd_fpeekw(index_seg, 8 + i * 12 + 4); break; }
    }
    if (!d || (d & 15)) return 0;
    seg = (pdw)(index_seg + (d >> 4));
    if (pd_fpeekw(seg, 0) != 0x4450 || pd_fpeekw(seg, 2) != 0x4246) return 0;                       /* "PDFB" */
    n = pd_fpeekw(seg, 6);
    if (n == 0 || n > 120) return 0;
    *nstrikes = n;
    return seg;
}

/* the PDRES core bank: the primary's INDEX claim (PDV_REND_FONTBANK). The
 * widths ('PDWD', 20.4.3) and names ('PDFX' record) live in it */
static pdw g_bn_tmp;
static void bank_attach(pdw index_seg)
{
    pdw n, x, seg;
    g_bankseg = 0; g_bankok = 0; g_bk_seg[0] = 0; g_bk_n[0] = 0; g_wid_off = g_nam_off = 0;
    g_pkfam = 0xFF;
    seg = index_bank(index_seg, &g_bn_tmp);
    n = g_bn_tmp;
    if (!seg) return;
    g_bankseg = seg; g_bankok = 1; g_bk_seg[0] = seg; g_bk_n[0] = n;
    x = 8 + n * 16;
    if (pd_fpeekw(seg, x) == 0x4450 && pd_fpeekw(seg, x + 2) == 0x5846) {                            /* "PDFX" */
        g_wid_off = pd_fpeekw(seg, x + 4);
        g_nam_off = pd_fpeekw(seg, x + 6);
    }
}

/* Adobe glyph name -> Windows-1252 byte through the sorted names block;
 * 0 = unknown */
static pdw glyph_code(const char *name)
{
    pdw n = pdr_slen(name), off, len, k, c;
    pdi cmp;
    if (n >= 4 && name[0] == 'u' && name[1] == 'n' && name[2] == 'i') {      /* uniXXXX */
        pdw v = 0;
        for (k = 3; k < 7 && k < n; k++) {
            c = (pdb)name[k];
            v = (pdw)(v * 16 + (c >= 'A' ? c - 'A' + 10 : c - '0'));
        }
        if (v >= 32 && v < 127) return v;
    }
    if (!g_nam_off) return 0;
    off = g_nam_off;
    for (;;) {                                  /* linear scan: once per font */
        len = pd_fpeekb(g_bankseg, off);
        if (!len) return 0;
        cmp = 0;
        if (len == n) {
            for (k = 0; k < n; k++)
                if (pd_fpeekb(g_bankseg, off + 1 + k) != (pdb)name[k]) { cmp = 1; break; }
            if (!cmp) return pd_fpeekb(g_bankseg, off + 1 + len);
        }
        off += len + 2;
    }
}

/* ---- the font cache ------------------------------------------------------ */
struct pdr_font {
    pdw objnum, gen;
    pdb used, fam, sty, ok;         /* ok: 0 unsupported (Type0): draws nothing */
    pdw first, nw;                  /* /FirstChar, /Widths length (0 = none)   */
    pdb enc[224];                   /* code 32..255 -> PD Latin (0 = none)     */
    pdw wid[224];                   /* 1/1000 em, 0xFFFF = not fetched         */
};
static struct pdr_font g_fn[PDR_NFONT];
static pdw g_fnnext;

static pdw ci_has(const char *s, const char *sub)
{
    pdw n = pdr_slen(s), m = pdr_slen(sub), i, j;
    for (i = 0; i + m <= n; i++) {
        for (j = 0; j < m; j++) {
            char a = s[i + j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (a != sub[j]) break;
        }
        if (j == m) return 1;
    }
    return 0;
}

static void font_classify(struct pdr_font *f, const char *base)
{
    const char *b = base;
    pdw i, known = 0;
    for (i = 0; b[i] && i < 8; i++) if (b[i] == '+') { b += i + 1; break; }
    f->fam = 0; f->sty = 0;
    if (ci_has(b, "courier") || ci_has(b, "mono") || ci_has(b, "consolas") ||
        ci_has(b, "typewriter") || ci_has(b, "fixed")) { f->fam = 2; known = ci_has(b, "courier"); }
    else if (ci_has(b, "times") || ci_has(b, "serif") || ci_has(b, "roman") ||
             ci_has(b, "georgia") || ci_has(b, "garamond") || ci_has(b, "palatino") ||
             ci_has(b, "bookman") || ci_has(b, "century")) { f->fam = 1; known = ci_has(b, "times"); }
    else if (ci_has(b, "helvetica") || ci_has(b, "arial")) known = 1;
    if (ci_has(b, "bold") || ci_has(b, "black") || ci_has(b, "heavy") || ci_has(b, "demi")) f->sty |= 1;
    if (ci_has(b, "italic") || ci_has(b, "oblique") || ci_has(b, "slanted")) f->sty |= 2;
    if (!known) inc_note(PDF_INC_FONT, "font substituted");
}

/* build the code -> PD Latin table from /Encoding */
static void font_encoding(struct pdr_font *f, pdw obj, pdw gen)
{
    pdw c, mode = 0, nd, i, code, v;       /* 0 standard, 1 winansi, 2 macroman */
    static char nm[32];
    if (dict_get(obj, gen, "Encoding", 0, 0) == 0) {
        if (g_dg.kind == PDK_NAME) {
            if (ci_has(g_dg.text, "winansi")) mode = 1;
            else if (ci_has(g_dg.text, "macroman")) mode = 2;
        } else if (g_dg.kind == PDK_DICT || g_dg.kind == PDK_REF) {
            if (dict_get(obj, gen, "Encoding", "BaseEncoding", 0) == 0 && g_dg.kind == PDK_NAME) {
                if (ci_has(g_dg.text, "winansi")) mode = 1;
                else if (ci_has(g_dg.text, "macroman")) mode = 2;
            } else mode = 1;                /* a dict with no base: the font's own - WinAnsi is the kinder guess */
        }
    }
    for (c = 32; c < 256; c++) {
        v = c;
        if (mode == 0) {
            if (c == 0x27) v = 0x92; else if (c == 0x60) v = 0x91;
            else if (c >= 0xA0) v = enc_std_hi[c - 0xA0];
            else if (c >= 0x80) v = 0;
        } else if (mode == 2) {
            if (c >= 0x80) v = enc_mac_hi[c - 0x80];
        }
        f->enc[c - 32] = (pdb)v;
    }
    /* /Differences: [ code name name ... code name ... ] */
    if (dict_get(obj, gen, "Encoding", "Differences", 0) == 0 && g_dg.kind == PDK_ARR) {
        nd = g_dg.count; code = 0;
        for (i = 0; i < nd && i < 400; i++) {
            if (dict_get(obj, gen, "Encoding", "Differences", idx_key(i))) break;
            if (g_dg.kind == PDK_INT || g_dg.kind == PDK_REAL) code = (pdw)pdm_fx_floor(&g_dg.num);
            else if (g_dg.kind == PDK_NAME) {
                pdr_scpy(nm, g_dg.text, 32);
                if (code >= 32 && code < 256) f->enc[code - 32] = (pdb)glyph_code(nm);
                code++;
            }
        }
    }
}

/* returns the cache index, 0xFF when the font cannot be drawn at all */
static pdw font_get(pdw obj, pdw gen)
{
    pdw i, slot;
    struct pdr_font *f;
    for (i = 0; i < PDR_NFONT; i++)
        if (g_fn[i].used && g_fn[i].objnum == obj && g_fn[i].gen == gen && obj) return i;
    slot = g_fnnext; g_fnnext = (g_fnnext + 1) % PDR_NFONT;
    f = &g_fn[slot];
    pdr_zero(f, sizeof *f);
    f->used = 1; f->objnum = obj; f->gen = gen; f->ok = 1;
    for (i = 0; i < 224; i++) f->wid[i] = 0xFFFF;
    if (!obj) { f->fam = 0; inc_note(PDF_INC_FONT, "direct font object"); }
    else {
        static char base[64];
        base[0] = 0;
        if (dict_get(obj, gen, "BaseFont", 0, 0) == 0 && g_dg.kind == PDK_NAME) pdr_scpy(base, g_dg.text, 64);
        if (dict_get(obj, gen, "Subtype", 0, 0) == 0 && g_dg.kind == PDK_NAME) {
            if (ci_has(g_dg.text, "type0")) {
                f->ok = 0; inc_note(PDF_INC_FONT, "CID font (Type0)");
            } else if (ci_has(g_dg.text, "type3")) inc_note(PDF_INC_FONT, "Type3 font");
        }
        font_classify(f, base);
        if (ci_has(base, "symbol") || ci_has(base, "dingbat")) inc_note(PDF_INC_FONT, "Symbol font");
        font_encoding(f, obj, gen);
        if (dict_get(obj, gen, "FirstChar", 0, 0) == 0) f->first = (pdw)pdm_fx_floor(&g_dg.num);
        if (dict_get(obj, gen, "Widths", 0, 0) == 0 && g_dg.kind == PDK_ARR) f->nw = g_dg.count;
    }
    if (!obj) { for (i = 0; i < 224; i++) f->enc[i] = (pdb)(i + 32); }
    return slot;
}

/* advance width of a CODE in 1/1000 em: /Widths, else the base-14 table of
 * the glyph the code maps to (11.3), else a flat guess */
static pdw font_width(pdw fi, pdw code)
{
    struct pdr_font *f = &g_fn[fi];
    pdw w, face, lat;
    if (code < 32) return 0;
    w = f->wid[code - 32];
    if (w != 0xFFFF) return w;
    w = 0xFFFF;
    if (f->nw && code >= f->first && code - f->first < f->nw && f->objnum) {
        if (dict_get(f->objnum, f->gen, "Widths", idx_key((pdw)(code - f->first)), 0) == 0 &&
            (g_dg.kind == PDK_INT || g_dg.kind == PDK_REAL)) {
            pdi v = pdm_fx_round(&g_dg.num);
            w = v < 0 ? 0 : (pdw)v;
        }
    }
    if (w == 0xFFFF) {
        lat = f->enc[code - 32];
        if (lat < 32) lat = 32;
        face = (pdw)(f->fam * 4 + (f->sty & 3));
        if (f->fam == 2) w = 600;
        else if (g_wid_off) w = pd_fpeekw(g_bankseg, g_wid_off + 8 + face * 448 + (lat - 32) * 2);
        else w = 556;
    }
    f->wid[code - 32] = w;
    return w;
}
