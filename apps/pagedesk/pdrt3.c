/* ============================================================================
 * os8088 - apps/pagedesk/pdrt3.c       PDREND: Type 3 fonts (PAGEDESK-SPEC 20.22.8)
 *
 * #included by pdri.c, before interp_run, when PDR_TYPE3 is defined (pdrf.c defines
 * it unless PDR_NO_TYPE3). A Type 3 glyph is a content stream, run NOT by recursion
 * (pdstk refuses the cycle) but as a FORM: text_glyph QUEUES (pass, block, slot, pen)
 * and rcore's PDR_T3_DRAIN() hook in interp_run, at an operator boundary, calls
 * t3_drain(), which opens the glyph's CharProc through do_form - rcore's Form XObject
 * frame push, the one frame machine - and then sets the CTM to
 * FontMatrix x [fs*Th 0 0 fs 0 0] x [Tm x CTM at the pen]. frame_end pops it like any
 * Form (PDR_T3_FRAME_END() clears g_t3f), and the next queued glyph starts at the next
 * boundary. While a glyph's frame is open no other glyph starts, so a long string
 * runs its procs one after another, never nested.
 * PDXF (a text pass that finds its strike block empty) wrote the CharProc, the matrix
 * and the width into the block (pdxft3.c); the queue lives in the glyph claim's image
 * row buffer [0, 768), which is free while a page is interpreted. An entry carries its
 * pass, so a pass that ended with glyphs queued (a full display list) leaves nothing
 * for the next one.
 * ==========================================================================*/
#define T3_QMAX 64                  /* entries of 12 bytes: pass, block << 8 | slot, pen x 4, pen y 4 */
static pdw g_t3q[6];

static void t3_queue(pdw blk, pdw slot)
{
    if (g_t3n >= T3_QMAX) { inc_note(PDF_INC_LIMIT, "Type 3 text"); return; }
    g_t3q[0] = g_passes; g_t3q[1] = (pdw)((blk << 8) | slot);
    pdr_cpy(&g_t3q[2], &g_penx, 4); pdr_cpy(&g_t3q[4], &g_peny, 4);
    pd_fcopy_out(g_t3q, g_gseg, (pdw)(((g_t3h + g_t3n) & (T3_QMAX - 1)) * 12), 12);
    g_t3n++;
}

/* at an operator boundary with glyphs queued: open the next one's frame; 1 = opened */
static pdw t3_drain(void)
{
    static struct pd_mat fm, dev;
    pdw boff, i, afm;
    if ((g_t3f && g_t3fp == g_passes && g_t3f <= g_nfr) || g_nop || g_arrd || g_inl || g_dictd) return 0;
    while (g_t3n) {
        pd_fcopy_in(g_gseg, (pdw)((g_t3h & (T3_QMAX - 1)) * 12), g_t3q, 12);
        g_t3h++; g_t3n--;
        if (g_t3q[0] != g_passes) continue;                     /* left over by an earlier pass */
        boff = syn_off((pdw)(g_t3q[1] >> 8));
        pd_fcopy_in(g_gseg, (pdw)(boff + PDFB_T3REC + (((g_t3q[1] & 255) - 1) << 2)), g_t3q, 4);   /* the CharProc */
        if (!g_t3q[0]) continue;
        pd_fcopy_in(g_gseg, (pdw)(boff + PDFB_T3FM), &fm, 24);  /* FontMatrix: 6 x (lo, hi), a pd_mat */
        pdm_mat_ident(&dev);                                    /* x diag(fs*Th, fs) */
        pdm_fx_mul(&dev.a, &GS.fs, &g_thk); pdr_cpy(&dev.d, &GS.fs, 4);
        pdm_mat_mul(&fm, &fm, &dev);
        pdr_cpy(&dev.a, &g_M.a, 16); pdr_cpy(&dev.e, &g_t3q[2], 8);   /* the device matrix at the pen */
        afm = g_afm; g_afm = 0; i = g_nfr;                      /* a CharProc is a form with no /Matrix or /BBox */
        do_form(g_t3q[0], g_t3q[1]);
        g_afm = afm;
        if (g_nfr == i) continue;                               /* refused: do_form noted why */
        pdm_mat_mul(&GS.ctm, &fm, &dev);
        g_t3f = g_nfr; g_t3fp = g_passes;
        return 1;
    }
    return 0;
}
