/* ============================================================================
 * os8088 - apps/pagedesk/pdxfcm.c      PDXF: CMaps and ToUnicode, /W widths
 *
 * #included by pdxf.c. PAGEDESK-SPEC 20.22.8. A CMap stream is tokenised through
 * a 256-byte window staged in far memory; the result is the compact table the
 * spec describes: +0 u8 code bytes, +1 u8 flags, +2 u16 n, then n x (lo, hi,
 * value) u16, sorted by lo.
 * ==========================================================================*/
static pdw g_cmn, g_cmp, g_cmeof, g_cmflags, g_cmnb, g_cmcnt, g_cmcap, g_cmseg, g_cmoff, g_cmuni;

static pdi cm_getc(void)
{
    if (g_cmp >= g_cmn) {
        if (g_cmeof) return -1;
        g_cmn = pdx_sread(g_stgseg, g_stgoff, 256); g_cmp = 0;
        if (!g_cmn) { g_cmeof = 1; return -1; }
    }
    return (pdi)pd_fpeekb(g_stgseg, (pdw)(g_stgoff + g_cmp++));
}
#define T_EOF  0
#define T_HEX  1
#define T_INT  2
#define T_NAME 3
#define T_KW   4
#define T_LB   5
#define T_RB   6
static pdw g_tv, g_thl;             /* the token's value (first two bytes / the integer), hex length in bytes */
static char g_tk[24];
static pdw g_hexs[4];               /* the hex string's bytes (first 4), for a destination                     */
static pdi g_cmc = -2;              /* one character of lookahead                                              */

static pdi cm_peek(void) { if (g_cmc == -2) g_cmc = cm_getc(); return g_cmc; }
static void cm_eat(void) { g_cmc = -2; }
static pdw cm_tok(void)
{
    pdi c;
    pdw n, h, v, nib;
    for (;;) {
        c = cm_peek();
        if (c < 0) return T_EOF;
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == 0 || c == '\f') { cm_eat(); continue; }
        if (c == '%') { while ((c = cm_peek()) >= 0 && c != '\n' && c != '\r') cm_eat(); continue; }
        break;
    }
    if (c == '<') {
        cm_eat();
        if (cm_peek() == '<') { cm_eat(); return cm_tok(); }                       /* a dict open: skip */
        v = 0; n = 0; h = 0; g_hexs[0] = g_hexs[1] = g_hexs[2] = g_hexs[3] = 0; nib = 0;
        while ((c = cm_peek()) >= 0 && c != '>') {
            cm_eat();
            if (hexv((pdw)c) == 0xFFFF) continue;
            h = (pdw)(h * 16 + hexv((pdw)c)); nib++;
            if (!(nib & 1)) { if (n < 4) g_hexs[n] = (pdw)(h & 255); if (n < 2) v = (pdw)((v << 8) | (h & 255)); n++; h = 0; }
        }
        if (c == '>') cm_eat();
        if (nib & 1) { if (n < 4) g_hexs[n] = (pdw)(h << 4); n++; }
        g_tv = v; g_thl = n;
        /* two bytes: the UTF-16 unit or code; one byte: the byte */
        if (n >= 2) g_tv = (pdw)((g_hexs[0] << 8) | g_hexs[1]); else g_tv = g_hexs[0];
        return T_HEX;
    }
    if (c == '>') { cm_eat(); if (cm_peek() == '>') cm_eat(); return cm_tok(); }
    if (c == '[') { cm_eat(); return T_LB; }
    if (c == ']') { cm_eat(); return T_RB; }
    if (c == '(') {                                                                /* a string: skip */
        pdw depth = 0;
        do { c = cm_peek(); cm_eat(); if (c == '\\') { cm_peek(); cm_eat(); } else if (c == '(') depth++; else if (c == ')') depth--; } while (depth && c >= 0);
        return cm_tok();
    }
    if (c == '/') {
        cm_eat(); n = 0;
        while ((c = cm_peek()) > ' ' && c != '/' && c != '[' && c != ']' && c != '<' && c != '>' && c != '(' && c != '%') { if (n < 22) g_tk[n++] = (char)c; cm_eat(); }
        g_tk[n] = 0; return T_NAME;
    }
    if ((c >= '0' && c <= '9') || c == '-' || c == '+') {
        v = 0; n = 0;
        while ((c = cm_peek()) >= 0 && ((c >= '0' && c <= '9') || c == '-' || c == '.')) { if (c >= '0' && c <= '9') v = (pdw)(v * 10 + (c - '0')); cm_eat(); n++; }
        g_tv = v; return T_INT;
    }
    n = 0;
    while ((c = cm_peek()) > ' ' && c != '/' && c != '[' && c != ']' && c != '<' && c != '>' && c != '(' && c != '%') { if (n < 22) g_tk[n++] = (char)c; cm_eat(); }
    g_tk[n] = 0;
    if (!n) { cm_eat(); return cm_tok(); }
    return T_KW;
}

/* append a range (sorted input is the norm; an out-of-order one is inserted) */
static void cm_add(pdw lo, pdw hi, pdw val)
{
    pdw n = g_cmcnt, a = 0, b = n, m, e;
    if (hi < lo) return;
    if ((pdw)(4 + (n + 1) * 6) > g_cmcap) { g_cmflags |= PDFC_TRUNC; return; }
    while (a < b) {                                                     /* the first entry with lo > ours */
        m = (pdw)((a + b) >> 1);
        if (pd_fpeekw(g_cmseg, (pdw)(g_cmoff + 4 + m * 6)) <= lo) a = (pdw)(m + 1); else b = m;
    }
    if (a < n) pd_fcopy(g_cmseg, (pdw)(g_cmoff + 4 + (a + 1) * 6), g_cmseg, (pdw)(g_cmoff + 4 + a * 6), (pdw)((n - a) * 6));
    e = (pdw)(g_cmoff + 4 + a * 6);
    pd_fpokew(g_cmseg, e, lo); pd_fpokew(g_cmseg, (pdw)(e + 2), hi); pd_fpokew(g_cmseg, (pdw)(e + 4), val);
    g_cmcnt++;
}

static pdw cm_is(const char *a) { return nm_eq(g_tk, a); }

/* parse the open stream. kind PDFC_ENC / PDFC_UNI. 0 = ok */
static pdw cm_parse(pdw kind, pdw dseg, pdw doff, pdw cap)
{
    pdw t, mode = 0, k = 0, ops[3], oh[3], inarr = 0, arrcode = 0;
    g_cmn = g_cmp = g_cmeof = 0; g_cmc = -2; g_cmflags = 0; g_cmnb = 0; g_cmcnt = 0;
    g_cmcap = cap; g_cmseg = dseg; g_cmoff = doff; g_cmuni = (pdw)(kind == PDFC_UNI);
    if (cap < 8) return PDE_LIMIT;
    for (;;) {
        t = cm_tok();
        if (t == T_EOF) break;
        if (t == T_KW) {
            k = 0;
            if (cm_is("begincodespacerange")) mode = 1;
            else if (cm_is("begincidrange")) mode = 2;
            else if (cm_is("begincidchar")) mode = 3;
            else if (cm_is("beginbfrange")) mode = 4;
            else if (cm_is("beginbfchar")) mode = 5;
            else if (cm_is("endcodespacerange") || cm_is("endcidrange") || cm_is("endcidchar") || cm_is("endbfrange") || cm_is("endbfchar")) mode = 0;
            else if (cm_is("usecmap")) g_cmflags |= PDFC_IDENT;      /* a base CMap by name: codes not listed map to themselves */
            inarr = 0;
            continue;
        }
        if (t == T_NAME) {
            if (cm_is("Identity-V")) g_cmflags |= PDFC_VERT;
            continue;
        }
        if (!mode) continue;
        if (t == T_LB) { inarr = 1; arrcode = ops[0]; continue; }
        if (t == T_RB) { inarr = 0; k = 0; continue; }
        if (inarr) {                                                    /* bfrange array: singles from ops[0] */
            if (t == T_HEX) { cm_add(arrcode, arrcode, g_tv); arrcode++; }
            continue;
        }
        if (t != T_HEX && t != T_INT) continue;
        ops[k] = g_tv; oh[k] = g_thl; k++;
        if (mode == 1 && k == 2) {
            if (!g_cmnb) g_cmnb = oh[0]; else if (g_cmnb != oh[0]) g_cmflags |= PDFC_MIXED;
            if (oh[0] > 2) g_cmflags |= PDFC_MIXED;
            k = 0;
        } else if ((mode == 3 || mode == 5) && k == 2) { cm_add(ops[0], ops[0], ops[1]); k = 0; }
        else if ((mode == 2) && k == 3) { cm_add(ops[0], ops[1], ops[2]); k = 0; }
        else if (mode == 4 && k == 3) { cm_add(ops[0], ops[1], ops[2]); k = 0; }
        else if (mode == 4 && k == 2) { /* the third operand may be an array: wait for it */ }
    }
    if (!g_cmnb) g_cmnb = 2;
    pd_fpokeb(dseg, doff, (pdb)(g_cmnb > 1 ? 2 : 1));
    pd_fpokeb(dseg, (pdw)(doff + 1), (pdb)g_cmflags);
    pd_fpokew(dseg, (pdw)(doff + 2), g_cmcnt);
    return 0;
}
