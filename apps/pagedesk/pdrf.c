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
 * A path element "[n]" selects element n of the array reached so far; a final
 * reference is followed here (PDENG does not: PAGEDESK-SPEC 20.2.6).      */
static struct pdxb_dictget g_dg;
static pdw dict_get(pdw obj, pdw gen, const char *k0, const char *k1, const char *k2)
{
    pdw err;
    pdr_zero(&g_dg, sizeof g_dg);
    g_dg.objnum = obj; g_dg.gen = gen;
    pdr_scpy(g_dg.key[0], k0, 32);
    if (k1) pdr_scpy(g_dg.key[1], k1, 32);
    if (k2) pdr_scpy(g_dg.key[2], k2, 32);
    err = eng_call(PDV_ENG_DICT_GET, &g_dg, sizeof g_dg, 0, 0, 0);
    if (!err && g_dg.kind == PDK_REF) {          /* the FINAL value is not followed by PDENG (20.2.6) */
        obj = g_dg.refnum; gen = g_dg.refgen;
        pdr_zero(&g_dg, sizeof g_dg);
        g_dg.objnum = obj; g_dg.gen = gen;       /* no names: the object's own value */
        err = eng_call(PDV_ENG_DICT_GET, &g_dg, sizeof g_dg, 0, 0, 0);
    }
    return err;
}
/* "[n]" with n in 0..9999 */
static char g_idx[10];
static const char *idx_key(pdw n)
{
    static char t[6]; pdw k = 0, i;
    g_idx[0] = '[';
    do { t[k++] = (char)('0' + n % 10); n /= 10; } while (n);
    for (i = 0; i < k; i++) g_idx[1 + i] = t[k - 1 - i];
    g_idx[1 + k] = ']'; g_idx[2 + k] = 0;
    return g_idx;
}

/* ---- the PDRES bank: strikes (11.2) and the extension (20.3.4) ----------- */
static pdw g_wid_off, g_nam_off;    /* from the 'PDFX' record, 0 = absent     */

/* index_seg holds a PDRES-format file's INDEX from byte 0: 'PDRS', the
 * directory, then the PDFB bank at the offset D of directory kind 1 (a
 * multiple of 16, 20.4.1). Returns the bank's segment, or 0 */
static pdw index_bank(pdw index_seg, pdw have, pdw *nstrikes)
{
    pdw n, i, k, d = 0, ilen = 0, seg;
    if (!index_seg || have < 96 || pd_fpeekw(index_seg, 0) != 0x4450 || pd_fpeekw(index_seg, 2) != 0x5352) return 0;   /* "PDRS" */
    n = pd_fpeekw(index_seg, 6);
    for (i = 0; i < n && i < 7; i++) {         /* 7 entries end at 92: inside the 96 checked */
        k = pd_fpeekw(index_seg, 8 + i * 12);
        if (k == 1 && !d) d = pd_fpeekw(index_seg, 8 + i * 12 + 4);
        if (k == 7) ilen = pd_fpeekw(index_seg, 8 + i * 12 + 8);
    }
    /* D from kind 1 and the INDEX length from kind 7 (D10), never constants; the
     * INDEX must be what was read, and the strike records and PDFX inside it */
    if (!d || (d & 15) || !ilen || ilen > have || d > ilen - 8) return 0;
    seg = (pdw)(index_seg + (d >> 4));
    if (pd_fpeekw(seg, 0) != 0x4450 || pd_fpeekw(seg, 2) != 0x4246) return 0;                       /* "PDFB" */
    n = pd_fpeekw(seg, 6);
    if (n == 0 || n > 120 || 8 + n * 16 > ilen - d) return 0;
    *nstrikes = n;
    return seg;
}

/* the PDRES core bank: the primary's INDEX claim (PDV_REND_FONTBANK). The
 * widths ('PDWD', 20.4.3) and names ('PDFX' record) live in it */
static pdw g_bn_tmp;
static void bank_attach(pdw index_seg, pdw index_len)
{
    pdw n, x, seg;
    g_bankseg = 0; g_bankok = 0; g_bk_seg[0] = 0; g_bk_n[0] = 0; g_wid_off = g_nam_off = 0;
    g_pkfam = 0xFF;
    seg = index_bank(index_seg, index_len, &g_bn_tmp);
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
/* the cache's 4 entries live in the workspace (WS_FN); one is resident at a time */
static struct pdr_font g_fw;
static struct { pdw objnum, gen; pdb used; } g_fk[PDR_NFONT];
static pdw g_fwi = 0xFF, g_fwdirty;
static pdw g_fnnext;
static void font_flush(void)
{
    if (g_fwi != 0xFF && g_fwdirty)
        pd_fcopy_out(&g_fw, g_ws, (pdw)(WS_FN + g_fwi * sizeof(struct pdr_font)), sizeof(struct pdr_font));
    g_fwdirty = 0;
}
static struct pdr_font *font_sel(pdw fi)
{
    if (g_fwi != fi) {
        font_flush();
        pd_fcopy_in(g_ws, (pdw)(WS_FN + fi * sizeof(struct pdr_font)), &g_fw, sizeof(struct pdr_font));
        g_fwi = fi;
    }
    return &g_fw;
}

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

/* word lists for font_classify: NUL separated, a 0x01 byte starts the next
 * group. Families: 0 mono (courier first), 1 serif (times first), 2 sans.
 * Styles: 0 bold, 1 italic */
static const char fam_words[] = "courier\0mono\0consolas\0typewriter\0fixed\0\1times\0serif\0roman\0georgia\0garamond\0palatino\0bookman\0century\0\1helvetica\0arial\0";
static const char sty_words[] = "bold\0black\0heavy\0demi\0\1italic\0oblique\0slanted\0";
static pdw words_hit(const char *w, const char *b)        /* 0xFFFF none, else group << 8 | index */
{
    pdw g = 0, k = 0;
    while (*w) {
        if (*w == 1) { g++; k = 0; w++; continue; }
        if (ci_has(b, w)) return (pdw)((g << 8) | k);
        while (*w) w++;
        w++; k++;
    }
    return 0xFFFF;
}
static void font_classify(struct pdr_font *f, const char *base)
{
    const char *b = base;
    pdw i, r, known = 0;
    for (i = 0; b[i] && i < 8; i++) if (b[i] == '+') { b += i + 1; break; }
    f->fam = 0; f->sty = 0;
    r = words_hit(fam_words, b);
    if (r != 0xFFFF) {
        f->fam = (pdb)((r >> 8) == 0 ? 2 : (r >> 8) == 1 ? 1 : 0);
        known = (r >> 8) == 2 || (r & 255) == 0;
    }
    r = words_hit(sty_words, b);
    if (r != 0xFFFF) f->sty |= (pdb)((r >> 8) ? 2 : 1);
    if (ci_has(b, "italic") || ci_has(b, "oblique")) f->sty |= 2;     /* both may be present */
    if (ci_has(b, "bold")) f->sty |= 1;
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
        if (g_fk[i].used && g_fk[i].objnum == obj && g_fk[i].gen == gen && obj) return i;
    slot = g_fnnext; g_fnnext = (g_fnnext + 1) % PDR_NFONT;
    font_flush();
    g_fwi = slot; g_fwdirty = 1;
    g_fk[slot].used = 1; g_fk[slot].objnum = obj; g_fk[slot].gen = gen;
    f = &g_fw;
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
    struct pdr_font *f = font_sel(fi);
    pdw w, face, lat;
    if (code < 32) {
        /* a subset font's control codes (TeX's ligatures at 11..15, a symbolic
         * font's 1..31) are not drawn (no PD Latin for them) but still ADVANCE
         * by their /Widths entry, uncached: there is no table room for them */
        if (f->nw && code >= f->first && code - f->first < f->nw && f->objnum &&
            dict_get(f->objnum, f->gen, "Widths", idx_key((pdw)(code - f->first)), 0) == 0 &&
            (g_dg.kind == PDK_INT || g_dg.kind == PDK_REAL)) {
            pdi v = pdm_fx_round(&g_dg.num);
            return v < 0 ? 0 : (pdw)v;
        }
        return 0;
    }
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
    f->wid[code - 32] = w; g_fwdirty = 1;
    return w;
}
