/* ============================================================================
 * os8088 - apps/pagedesk/pdxfwd.c      PDXF: a CIDFont's /DW and /W (PDV_FNT_WIDTHS)
 *
 * #included by pdxf.c. PAGEDESK-SPEC 20.22.8. /W is [c [w w ...] c1 c2 w ...]: the
 * array is walked with PDENG's "[n]" paths (the cursor makes each the next
 * item) and written as sorted (first, last, width) triples, equal neighbours
 * merged.
 * ==========================================================================*/
static struct pdxb_dictget g_dg;
static pdw wd_get(pdw obj, pdw gen, const char *k0, const char *k1, const char *k2)
{
    pdw err, n;
    pdx_zero(&g_dg, sizeof g_dg);
    g_dg.objnum = obj; g_dg.gen = gen;
    for (n = 0; k0[n] && n < 31; n++) g_dg.key[0][n] = k0[n];
    if (k1) for (n = 0; k1[n] && n < 31; n++) g_dg.key[1][n] = k1[n];
    if (k2) for (n = 0; k2[n] && n < 31; n++) g_dg.key[2][n] = k2[n];
    pd_fcopy_out(&g_dg, g_xb, 0, sizeof g_dg);
    err = pdx_eng(PDV_ENG_DICT_GET, 0, 0, 0);
    if (!err) pd_fcopy_in(g_xb, 0, &g_dg, sizeof g_dg);
    if (!err && g_dg.kind == PDK_REF) {               /* the FINAL value is not followed by PDENG */
        obj = g_dg.refnum; gen = g_dg.refgen;
        pdx_zero(&g_dg, sizeof g_dg);
        g_dg.objnum = obj; g_dg.gen = gen;
        pd_fcopy_out(&g_dg, g_xb, 0, sizeof g_dg);
        err = pdx_eng(PDV_ENG_DICT_GET, 0, 0, 0);
        if (!err) pd_fcopy_in(g_xb, 0, &g_dg, sizeof g_dg);
    }
    return err;
}
static char g_wk[3][8];
static const char *wd_key(pdw slot, pdw n)           /* "[n]" */
{
    char *t = g_wk[slot];
    pdw k = 0, i;
    char d[6];
    t[0] = '[';
    do { d[k++] = (char)('0' + n % 10); n /= 10; } while (n);
    for (i = 0; i < k; i++) t[1 + i] = d[k - 1 - i];
    t[1 + k] = ']'; t[2 + k] = 0;
    return t;
}
static pdw g_wn, g_wlast;                            /* triples written; the last triple's last cid + width */
static pdw g_wseg, g_woff, g_wcap, g_wover;
static void wd_put(pdw first, pdw last, pdw w)
{
    pdw o;
    if (g_wn) {                                       /* merge into the previous triple when contiguous and equal */
        o = (pdw)(g_woff + (g_wn - 1) * 6);
        if (pd_fpeekw(g_wseg, (pdw)(o + 4)) == w && (pdw)(pd_fpeekw(g_wseg, (pdw)(o + 2)) + 1) == first) { pd_fpokew(g_wseg, (pdw)(o + 2), last); return; }
    }
    if ((pdw)((g_wn + 1) * 6) > g_wcap || g_wn >= 512) { g_wover = 1; return; }
    o = (pdw)(g_woff + g_wn * 6);
    pd_fpokew(g_wseg, o, first); pd_fpokew(g_wseg, (pdw)(o + 2), last); pd_fpokew(g_wseg, (pdw)(o + 4), w);
    g_wn++;
}
static pdw wd_int(void) { pdi v = pdm_fx_round(&g_dg.num); return v < 0 ? 0 : (pdw)v; }

static pdw v_widths(void)
{
    struct pdxb_fwid *wv = &g_a.wi;
    pdw obj = wv->objnum, gen = wv->gen, n, i, j, c1, c2, cnt, items = 0;
    g_wn = 0; g_wover = 0; g_wseg = wv->dseg; g_woff = wv->doff; g_wcap = wv->cap;
    wv->dw = 1000; wv->more = 0; wv->len = 0;
    if (!wd_get(obj, gen, "DW", 0, 0) && (g_dg.kind == PDK_INT || g_dg.kind == PDK_REAL)) wv->dw = wd_int();
    if (wd_get(obj, gen, "W", 0, 0) || g_dg.kind != PDK_ARR) return 0;
    n = g_dg.count;
    for (i = 0; i < n && items < 1024; ) {
        if (wd_get(obj, gen, "W", wd_key(0, i), 0) || (g_dg.kind != PDK_INT && g_dg.kind != PDK_REAL)) { i++; continue; }
        c1 = wd_int(); items++;
        if (i + 1 >= n) break;
        if (wd_get(obj, gen, "W", wd_key(0, (pdw)(i + 1)), 0)) { i += 2; continue; }
        if (g_dg.kind == PDK_ARR) {
            cnt = g_dg.count;
            for (j = 0; j < cnt && items < 1024; j++, items++) {
                if (wd_get(obj, gen, "W", wd_key(0, (pdw)(i + 1)), wd_key(1, j)) ||
                    (g_dg.kind != PDK_INT && g_dg.kind != PDK_REAL)) continue;
                wd_put((pdw)(c1 + j), (pdw)(c1 + j), wd_int());
            }
            i += 2;
        } else if (g_dg.kind == PDK_INT || g_dg.kind == PDK_REAL) {
            c2 = wd_int();
            if (i + 2 >= n || wd_get(obj, gen, "W", wd_key(0, (pdw)(i + 2)), 0)) break;
            wd_put(c1, c2, wd_int());
            i += 3; items += 2;
        } else i += 2;
    }
    wv->len = (pdw)(g_wn * 6); wv->more = g_wover;
    return 0;
}
