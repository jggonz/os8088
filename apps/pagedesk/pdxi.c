/* ============================================================================
 * os8088 - apps/pagedesk/pdxi.c                       PDXI.MOD (PAGEDESK-SPEC 20.22.11)
 *
 * The image-decoder occupant of the module slot: DCT here, CCITT and JBIG2 by
 * the bilevel agent's files. One translation unit (SPEC.md 73.1) compiled twice:
 * by cc8086 for the target (pdxi.asm is the door) and by the host
 * `cc -DPD_HOST` for hosttest/pdi_test.c.
 *
 * The door (pdxi.asm) pushes AL, BX, CX, DX and calls pdi_verb; the answer is AX
 * with CF = 1 when it is a failure.
 * ==========================================================================*/
#ifdef PD_HOST
#include "pdxi.h"
#else
#include "os88.h"
#include "pdxi.h"
#endif

pdw pdi_engcf;                          /* CF of the last PDENG call          */
pdw pdi_cf;                             /* CF of this door's answer           */
pdw pdi_engvec[2];                      /* PDENG's door, offset then segment  */

struct pdxb_img g_im;
pdw g_xb, g_wseg, g_wbytes;

static pdw s_codec;                     /* the open image's codec, 0 = none   */
static pdw s_src;                       /* 0 none, 1 PDENG stream, 2 inline   */
static pdw s_ipos;                      /* inline: bytes taken                */
static struct pdxb_strm s_sm;

/* ---- the coded bytes ------------------------------------------------------ */
pdw pdi_src_open_obj(pdw obj, pdw gen, pdw accept, pdw codec)
{
    pdw r;
    if (s_src) pdi_src_close();
    s_sm.objnum = obj; s_sm.gen = gen;
    s_sm.index = 0; s_sm.page = 0;
    s_sm.declen.lo = 0; s_sm.declen.hi = 0;
    s_sm.filters = 0; s_sm.accept = accept; s_sm.codec = 0;
    pd_fcopy_out(&s_sm, g_xb, 0, sizeof s_sm);
    r = pdi_engcall(PDV_ENG_STRM_OPEN, 0, 0, 0);
    if (pdi_engcf) return r;
    pd_fcopy_in(g_xb, 0, &s_sm, sizeof s_sm);
    if (accept && s_sm.codec != codec) {
        pdi_engcall(PDV_ENG_STRM_CLOSE, 0, 0, 0);
        return PDE_FILTER;
    }
    s_src = 1;
    return PDE_OK;
}

pdw pdi_src_open(void)
{
    if (s_src) pdi_src_close();
    if (g_im.objnum == 0 && g_im.gen == 0) {
        if (g_im.ilen == 0) return PDE_SYNTAX;
        s_src = 2; s_ipos = 0;
        return PDE_OK;
    }
    return pdi_src_open_obj(g_im.objnum, g_im.gen, PDSA_CODEC, g_im.codec);
}

pdw pdi_src_get(void *dst, pdw max)
{
    /* into a NEAR buffer, through the landing zone that follows the usable
     * workspace (g_wbytes .. g_wbytes + 1024): the engine writes far */
    pdw n;
    if (max > 1024) max = 1024;
    n = pdi_src_read(g_wseg, g_wbytes, max);
    if (n) pd_fcopy_in(g_wseg, g_wbytes, dst, n);
    return n;
}

pdw pdi_src_read(pdw seg, pdw off, pdw max)
{
    pdw n;
    if (s_src == 2) {
        n = g_im.ilen - s_ipos;
        if (n > max) n = max;
        if (n) pd_fcopy(seg, off, g_im.iseg, (pdw)(g_im.ioff + s_ipos), n);
        s_ipos = (pdw)(s_ipos + n);
        return n;
    }
    if (s_src != 1) return 0;
    n = pdi_engcall(PDV_ENG_STRM_READ, seg, off, max);
    if (pdi_engcf) return 0;
    return n;
}

void pdi_src_close(void)
{
    if (s_src == 1) pdi_engcall(PDV_ENG_STRM_CLOSE, 0, 0, 0);
    s_src = 0;
}

/* ---- the bilevel agent's input (PAGEDESK-SPEC 20.22.12): ix_get / ix_open ----- */
#if defined(PDXI_CCIT) || defined(PDXI_JB2)
static struct pdxb_imgx s_imgx;         /* follows pdxb_img in the XB                    */
static pdb  ix_b[512];
static pdw  ix_p, ix_e;
static int ix_get(void)
{
    if (ix_p >= ix_e) {
        ix_e = pdi_src_get(ix_b, 512); ix_p = 0;
        if (ix_e == 0) return -1;
    }
    return ix_b[ix_p++];
}
/* 0 = the coded stream again from its start; 1 = /JBIG2Globals, whose OBJECT NUMBER pdxb_imgx.globals
 * holds (generation 0), read through the whole filter chain */
static pdw ix_open(pdw which)
{
    pdw r;
    ix_p = 0; ix_e = 0;
    if (which == 0) r = pdi_src_open();
    else r = pdi_src_open_obj(s_imgx.globals, 0, 0, 0);
    return r;
}
#endif
#ifdef PDXI_CCIT
#include "pdxiccit.c"
#endif
#ifdef PDXI_JB2
#include "pdxijb2.c"
#endif
#if defined(PDXI_CCIT) || defined(PDXI_JB2)
static void ix_report(void)
{
    g_im.out_w = bl_ow; g_im.out_h = bl_oh; g_im.out_mode = bl_omode;
    g_im.out_ncomp = 1; g_im.rowbytes = bl_orb; g_im.scale = bl_scale;
}
#endif

#ifndef PDXI_NODCT                      /* -DPDXI_NODCT: the size A/B (the shell without DCT) */
#include "pdxidct.c"
#if defined(PD_HOST) && !defined(PDI_ASM)
#include "pdxitwin.c"
#endif
#else
pdw pdi_dct_open(void) { return PDE_FILTER; }
pdw pdi_dct_row(pdw seg, pdw off, pdw row) { (void)seg; (void)off; (void)row; return PDE_RANGE; }
void pdi_dct_close(void) { }
void pdi_dct_drop(void) { }
#endif
/* ---- the verbs ------------------------------------------------------------ */
static pdw v_init(pdw bx)
{
    static struct pdxb_init in;
    g_xb = bx;
    pd_fcopy_in(bx, 0, &in, sizeof in);
    pdi_engvec[0] = in.eng_off; pdi_engvec[1] = in.eng_seg;
    g_wseg = in.slot_seg;
    g_wbytes = in.slot_kb >= 64 ? (pdw)0xFFF0 : (pdw)(in.slot_kb << 10);
    g_wbytes = g_wbytes > 1024 ? (pdw)(g_wbytes - 1024) : 0;   /* the landing zone */
    if (s_codec) pdi_src_close();
    s_codec = 0;
    pdi_dct_drop();
    return PDE_OK;
}

static void v_close(void)
{
    if (s_codec == PDCD_DCT) pdi_dct_close();
    pdi_src_close();
    s_codec = 0;
}

static pdw v_open(void)
{
    pdw r, c;
    if (s_codec) v_close();
    if (g_wbytes == 0) return PDE_NOMEM;
    pd_fcopy_in(g_xb, 0, &g_im, sizeof g_im);
    c = g_im.codec;
    r = PDE_FILTER;
    g_im.out_w = 0; g_im.out_h = 0; g_im.out_mode = 0;
    g_im.out_ncomp = 0; g_im.rowbytes = 0; g_im.scale = 0;
    if (c == PDCD_DCT) r = pdi_dct_open();
#if defined(PDXI_CCIT) || defined(PDXI_JB2)
    pd_fcopy_in(g_xb, sizeof(struct pdxb_img), &s_imgx, sizeof s_imgx);
#endif
#ifdef PDXI_CCIT
    if (c == PDCD_CCITT) {
        r = ix_open(0);
        if (!r) r = cf_open(s_imgx.k, s_imgx.cols, s_imgx.rows, s_imgx.flags, g_im.h, g_im.want_w);
        if (!r) ix_report();
    }
#endif
#ifdef PDXI_JB2
    if (c == PDCD_JBIG2) {
        r = jb_open(s_imgx.globals, g_im.h, g_im.want_w);
        if (!r) ix_report();
    }
#endif
    if (r) { pdi_src_close(); return r; }
    s_codec = c;
    pd_fcopy_out(&g_im, g_xb, 0, sizeof g_im);
    return PDE_OK;
}

static pdw v_row(pdw seg, pdw off, pdw row)
{
    if (s_codec == PDCD_DCT) return pdi_dct_row(seg, off, row);
#ifdef PDXI_CCIT
    if (s_codec == PDCD_CCITT) {
        pdw r = cf_getrow(row);
        if (!r) pd_fcopy_out(bl_out, seg, off, bl_orb);
        return r;
    }
#endif
#ifdef PDXI_JB2
    if (s_codec == PDCD_JBIG2) {
        pdw r = jb_getrow(row);
        if (!r) pd_fcopy_out(bl_out, seg, off, bl_orb);
        return r;
    }
#endif
    return PDE_RANGE;
}

unsigned pdi_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx)
{
    pdw r;
    switch (verb) {
    case PDV_X_INIT:    r = v_init(bx); break;
    case PDV_IMG_OPEN:  r = v_open(); break;
    case PDV_IMG_ROW:   r = v_row(bx, cx, dx); break;
    case PDV_IMG_CLOSE: v_close(); r = PDE_OK; break;
    default:            r = PDE_RANGE; break;
    }
    pdi_cf = (pdw)(r != PDE_OK && r != PDE_BUSY);
    return r;
}
