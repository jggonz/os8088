/* ============================================================================
 * os8088 - apps/pagedesk/pdxf.c        PDXF.MOD: font programs (PAGEDESK-SPEC 20.22.8)
 *
 * One translation unit: this file #includes the readers. The door (pdxf.asm)
 * calls pdxf_verb(). On the host, hosttest/pdx_host.c includes this file with
 * PD_HOST and supplies pdx_muldiv() and the far-memory arena (pdmath.c).
 * ==========================================================================*/
#include "pdabi.h"
#include "pdmath.h"

/* trunc-to-nearest(a*b/c), 32-bit intermediate, c != 0. Assembly in pdxf.asm */
pdi pdx_muldiv(pdi a, pdi b, pdi c);

static pdw g_upem, g_em64;          /* units per em; the size in 1/64 px         */

#include "pdxfol.c"
static pdw g_cf_base;
#include "pdxfnm.c"
#include "pdxfsf.c"
#include "pdxfcf.c"
#include "pdxft1.c"

/* ---- the resident program ---------------------------------------------------- */
#define PDFK_NONE 0
#define PDFK_TT   1
#define PDFK_CFF  2
#define PDFK_T1   3
static pdw g_kind;

/* open the program already in (g_pseg, g_plen): detect the format */
static pdw fnt_open(void)
{
    pdw r;
    g_kind = PDFK_NONE;
    g_cf_base = 0;
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
