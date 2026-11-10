/* ============================================================================
 * os8088 - apps/pagedesk/pdrt3.c       PDREND: Type 3 fonts (PAGEDESK-SPEC 20.22.8)
 *
 * #included by pdri.c, before interp_run, when PDR_TYPE3 is defined. A Type 3 glyph
 * is a content stream, run NOT by recursion (pdstk refuses the cycle) but through the
 * frame machine: text_glyph QUEUES (strike block, slot, pen) and interp_run, at an
 * operator boundary, calls t3_go(), which opens the glyph's CharProc as a frame (the
 * shape of do_form) with CTM = FontMatrix x [fs*Th 0 0 fs 0 0] x [Tm x CTM at the pen];
 * frame_end pops it like a Form and the next queued glyph starts at the next boundary.
 * PDXF (a text pass that finds its strike block empty) wrote the CharProc, the matrix
 * and the width into the block (pdxft3.c); the queue lives in the glyph claim's image
 * row buffer [0, 1024), which is free while a group is interpreted.
 * ==========================================================================*/
#define T3_QMAX 64                  /* entries of 12 bytes: slot's block u16, slot u16, pen x 4, pen y 4 */
static pdw g_t3h, g_t3n;

static void t3_queue(pdw blk, pdw slot)
{
    pdw o;
    if (g_t3n >= T3_QMAX) { inc_note(PDF_INC_LIMIT, "Type 3 text"); return; }
    o = (pdw)(((g_t3h + g_t3n) & (T3_QMAX - 1)) * 12);
    pd_fpokew(g_gseg, o, blk); pd_fpokew(g_gseg, (pdw)(o + 2), slot);
    pd_fcopy_out(&g_penx, g_gseg, (pdw)(o + 4), 4); pd_fcopy_out(&g_peny, g_gseg, (pdw)(o + 8), 4);
    g_t3n++;
}

/* start the next queued glyph's frame; 1 = a frame was opened */
static pdw t3_go(void)
{
    static struct pd_mat fm, dev;
    static struct pd_fx sx;
    pdw o, boff, obj, gen, i, rp;
    struct pd_fx *p;
    o = (pdw)((g_t3h & (T3_QMAX - 1)) * 12); g_t3h++; g_t3n--;
    boff = syn_off(pd_fpeekw(g_gseg, o));
    rp = (pdw)(boff + PDFB_T3REC + ((pd_fpeekw(g_gseg, (pdw)(o + 2)) - 1) << 2));
    obj = pd_fpeekw(g_gseg, rp); gen = pd_fpeekw(g_gseg, (pdw)(rp + 2));
    if (!obj) return 0;
    if (g_nfr + 1 >= PDR_NFORM || g_gsn + 1 > PD_MAXGSTACK) { inc_note(PDF_INC_LIMIT, "form nesting"); return 0; }
    p = &fm.a;
    for (i = 0; i < 6; i++) {                               /* FontMatrix x diag(fs*Th, fs) */
        p[i].lo = pd_fpeekw(g_gseg, (pdw)(boff + PDFB_T3FM + (i << 2))); p[i].hi = (pdi)pd_fpeekw(g_gseg, (pdw)(boff + PDFB_T3FM + (i << 2) + 2));
        if (i & 1) pdm_fx_mul(&p[i], &p[i], &GS.fs);
        else { pdm_fx_mul(&sx, &GS.fs, &g_thk); pdm_fx_mul(&p[i], &p[i], &sx); }
    }
    pdr_cpy(&dev.a, &g_M.a, 16); pd_fcopy_in(g_gseg, (pdw)(o + 4), &dev.e, 4); pd_fcopy_in(g_gseg, (pdw)(o + 8), &dev.f, 4);
    if (strm_open(obj, gen, 0, 0)) { inc_note(PDF_INC_FILTER, "Type 3 glyph stream"); return 0; }
    g_nfr++;
    g_fr[g_nfr].isform = 1; g_fr[g_nfr].gsbase = g_gsn; g_fr[g_nfr].gsover = g_gsover; g_gsover = 0;
    g_fr[g_nfr].resp = g_resp; g_fr[g_nfr].reso = g_reso; g_fr[g_nfr].resg = g_resg;
    gs_push();
    g_gsfloor = g_gsn;
    pdm_mat_mul(&GS.ctm, &fm, &dev);
    return 1;
}
