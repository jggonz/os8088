/* ============================================================================
 * os8088 - apps/pagedesk/hosttest/pdx_xmod.c   PDXF in the slot, in-process (host)
 *
 * PAGEDESK-SPEC 20.22.8. hosttest/pdx_page.py links this in place of rcore's
 * pdr_xmod.c, so PDREND runs its REAL slot code (pdrxc.c: x_have, x_need, x_mem,
 * x_call) and pdr_test.c's ladder loads "the font module" on PDE_NEEDMOD: the far
 * call to the occupant's door is pdxf_verb, the real PDXF compiled for the host.
 *
 * The primary re-hands the glyph claim to the occupant at PDV_X_INIT (20.22.8); this
 * twin does it the way plat does, by re-INITing PDXF whenever the claim PDREND names
 * in a GLYPH (its dseg and cap) is not the one PDXF was last handed.
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "../pdabi.h"
#include "../pdmath.h"

extern unsigned char pd_host_mem[];
extern pdw pdrh_xbseg(void);
extern pdw pdxf_verb(pdw verb, pdw bx, pdw cx, pdw dx);
extern pdw pdx_cf, pdx_odx;
/* pdrv.c's verb_init skips words 13 and 14 of pdxb_init (INI_GLYW) on a NEEDMEM re-INIT */
typedef char pdx_ck_lat[offsetof(struct pdxb_fglyph, lat) == 30 && offsetof(struct pdxb_fglyph, oflags) == 38 && offsetof(struct pdxb_fglyph, blk) == 44 ? 1 : -1];   /* pdri.c emb_miss reads them by offset */
typedef char pdx_ck_glyw[offsetof(struct pdxb_init, glyph_seg) == 26 && offsetof(struct pdxb_init, glyph_kb) == 28 ? 1 : -1];
#define XMEM(seg, off) (pd_host_mem + (((unsigned long)(seg) << 4) + (off)))

pdw pdrh_slot;                      /* the occupant the harness "loaded"     */
long pdrh_xcalls;
pdw pdr_x_off, pdr_x_seg, pdr_x_cf, pdr_x_dx, pdr_out_cx, pdr_out_dx;
static pdw g_igs = 0xFFFF, g_igk;   /* the glyph claim PDXF was last handed    */
void pdrh_xload(void) { g_igs = 0xFFFF; }   /* a load: the next GLYPH INITs PDXF (pdr_test.c) */
int pdrh_xgrant;                    /* pdr_test.c's PDR_XNEEDMEM ladder (pdr_xmod.c's; unread here) */
long pdrh_xshades, pdrh_xstepmax, pdrh_xshfail;   /* pdr_test.c's PDR_XSHADE report (pdr_xmod.c's; no PDXG here) */
static pdw g_slotkb;                /* a PDMEM_SLOT grant (PDX_SLOTGROW), else PDX_SLOTKB or 48 */

static void xinit(pdw gseg, pdw gkb)
{
    static struct pdxb_init in;
    static unsigned char keep[512];
    pdw xb = pdrh_xbseg();
    memcpy(keep, XMEM(xb, 0), sizeof keep);
    memset(&in, 0, sizeof in);
    in.xb_seg = xb; in.slot_seg = 0xB000;
    in.slot_kb = g_slotkb ? g_slotkb : (pdw)(getenv("PDX_SLOTKB") ? atoi(getenv("PDX_SLOTKB")) : 48);
    in.glyph_seg = gseg; in.glyph_kb = gkb;
    memcpy(XMEM(xb, 0), &in, sizeof in);
    if (pdxf_verb(PDV_X_INIT, xb, 0, 0)) { fprintf(stderr, "XMOD: PDXF refused X_INIT\n"); exit(8); }
    memcpy(XMEM(xb, 0), keep, sizeof keep);
    g_igs = gseg; g_igk = gkb;
}

/* pdrprim.inc's pdr_xfar, twinned: the far call to the occupant's door */
pdw pdr_xfar(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdw r, xb = pdrh_xbseg(), gseg, gkb;
    pdrh_xcalls++;
    if (pdrh_slot != PDX_FNT) {
        fprintf(stderr, "XMOD: verb 0x%x sent to occupant %u (only PDXF is here)\n", verb, pdrh_slot);
        pdr_x_cf = 1; pdr_x_dx = 0; return PDE_RANGE;
    }
    if (verb == PDV_FNT_GLYPH) {
        memcpy(&gseg, XMEM(xb, offsetof(struct pdxb_fglyph, dseg)), 2);
        memcpy(&gkb, XMEM(xb, offsetof(struct pdxb_fglyph, cap)), 2);
        if (gseg != g_igs || gkb != g_igk) xinit(gseg, gkb);
        if (getenv("PDX_NOPROG")) {             /* --nopr: every font's program unusable (QA R13) */
            pdw fl;
            memcpy(&fl, XMEM(xb, offsetof(struct pdxb_fglyph, flags)), 2); fl |= PDFG_NOPROG;
            memcpy(XMEM(xb, offsetof(struct pdxb_fglyph, flags)), &fl, 2);
        }
    }
    pdx_odx = 0;
    r = pdxf_verb(verb, bx, cx, dx);
    /* PDX_SLOTGROW: grant a PDMEM_SLOT answer at once, as plat's ladder does (regrow S, re-INIT,
     * the same verb again), so PDXF's side of the growth is exercised; without it the answer goes
     * back to PDREND and pdr_test.c's ladder refuses it */
    if (!pdx_cf && r == PDE_NEEDMEM && getenv("PDX_SLOTGROW") && pdx_odx <= 64) {
        static unsigned char gl[sizeof(struct pdxb_fglyph)];
        memcpy(gl, XMEM(xb, 0), sizeof gl);
        g_slotkb = pdx_odx; xinit(g_igs, g_igk);
        memcpy(XMEM(xb, 0), gl, sizeof gl);
        pdx_odx = 0;
        r = pdxf_verb(verb, bx, cx, dx);
    }
    pdr_x_cf = pdx_cf; pdr_x_dx = pdx_odx;
    return r;
}
