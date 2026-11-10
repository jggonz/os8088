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
#include "pdxfsf.c"

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
    r = sf_open();
    if (r == 1) g_kind = PDFK_TT;
    return g_kind != PDFK_NONE;
}

/* one glyph's outline -> the recorder; 0 on a refusal */
static pdw fnt_outline(pdw gid)
{
    if (g_kind == PDFK_TT) return sf_glyph(gid);
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
    adv = g_kind == PDFK_TT ? sf_adv(gid) : 0;
    g_gadv = (pdw)pdx_muldiv((pdi)adv, (pdi)em64, (pdi)g_upem);
    return rs_glyph(asc, rows, dseg, doff, cap);
}
