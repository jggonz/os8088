/* ============================================================================
 * os8088 - apps/pagedesk/pdxft3.c      PDXF: Type 3 fonts (PAGEDESK-SPEC 20.22.8)
 *
 * #included by pdxf.c. A Type 3 glyph is a content stream, so PDXF draws nothing: for a
 * code it answers the CharProc's object and generation, the width and the Latin byte
 * into the strike block (PDFB_T3REC) and the /FontMatrix once (PDFB_T3FM). PDREND runs
 * the proc through the interpreter's frame machine (pdrt3.c).
 * ==========================================================================*/
static pdw t3_info(struct pdxb_fglyph *g, pdw boff, pdw h, pdw ip)
{
    static struct pd_fx fm[6], wv, k1000;
    pdw i, code = g->code, nm = 0, first = 0, rec = (pdw)(boff + PDFB_T3REC + h * 4), ok = 0, fo, fg;
    const char *name = enc_name(code);
    pdi w;
    pd_fpokew(g->dseg, rec, 0); pd_fpokew(g->dseg, (pdw)(rec + 2), 0);
    fo = g->objnum; fg = g->gen;
    if (name && fd_ref(fo, fg, "CharProcs", name, 0)) { pd_fpokew(g->dseg, rec, g_dg.refnum); pd_fpokew(g->dseg, (pdw)(rec + 2), g_dg.refgen); ok = 1; }
    for (i = 0; i < 6; i++) pdm_fx_int(&fm[i], i == 0 || i == 3 ? 0 : 0);
    fm[0].hi = 0; fm[0].lo = 0x0042; fm[3].hi = 0; fm[3].lo = 0x0042;                 /* the default [0.001 0 0 0.001 0 0] */
    if (!wd_get(fo, fg, "FontMatrix", 0, 0) && g_dg.kind == PDK_ARR && g_dg.count == 6)
        for (i = 0; i < 6; i++)
            if (!wd_get(fo, fg, "FontMatrix", wd_key(0, i), 0) && (g_dg.kind == PDK_INT || g_dg.kind == PDK_REAL)) { fm[i].lo = g_dg.num.lo; fm[i].hi = g_dg.num.hi; }
    for (i = 0; i < 6; i++) { pd_fpokew(g->dseg, (pdw)(boff + PDFB_T3FM + i * 4), fm[i].lo); pd_fpokew(g->dseg, (pdw)(boff + PDFB_T3FM + i * 4 + 2), (pdw)fm[i].hi); }
    /* the width: /Widths[code - /FirstChar] x FontMatrix.a, in 1/1000 em */
    if (!wd_get(fo, fg, "FirstChar", 0, 0)) first = (pdw)pdm_fx_floor(&g_dg.num);
    w = 0;
    if (code >= first && !wd_get(fo, fg, "Widths", wd_key(0, (pdw)(code - first)), 0) && (g_dg.kind == PDK_INT || g_dg.kind == PDK_REAL)) {
        pdm_fx_int(&k1000, 1000);
        pdm_fx_mul(&wv, &g_dg.num, &fm[0]); pdm_fx_mul(&wv, &wv, &k1000);
        w = pdm_fx_round(&wv); if (w < 0) w = 0;
    }
    pd_fpokew(g->dseg, ip, (pdw)w);
    pd_fpokeb(g->dseg, (pdw)(ip + 3), (pdb)(PDFB_F_BIG | PDFB_F_BMP));                 /* nothing to bitmap: the slot is final */
    (void)nm;
    return ok ? 0 : 0;
}
