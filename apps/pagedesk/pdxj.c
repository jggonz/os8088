/* ============================================================================
 * os8088 - apps/pagedesk/pdxj.c                      PDXJ.MOD (PAGEDESK-SPEC 20.22.13)
 *
 * JPEG 2000 at a reduced resolution level. One translation unit (SPEC.md 73.1):
 * the pdxj*.c files are #included in order, compiled by cc8086 for the target
 * (pdxj.asm is the door and the inner loops) and by the host `cc -DPD_HOST` for
 * hosttest/pdxj_test.c, which #includes THIS file so it can reach the statics.
 *
 * Verbs (pdabi.h): PDV_X_INIT 0xC0, PDV_IMG_OPEN 0xE8, PDV_IMG_ROW 0xE9,
 * PDV_IMG_CLOSE 0xEA, through the one function pdxj_verb(verb, bx, cx, dx).
 * ==========================================================================*/
#ifdef PD_HOST
#include "pdxj.h"
#else
#include "os88.h"
#include "pdxj.h"
#endif

pdw pdxj_cf;                            /* CF of this door's answer            */
pdw pdxj_engcf;                         /* CF of the last PDENG call           */
pdw pdxj_engvec[2];                     /* PDENG's door, offset then segment   */
#ifndef PD_HOST
pdw pdxj_engcall(pdw verb, pdw bx, pdw cx, pdw dx);     /* asm */
#endif

/* ---- state ----------------------------------------------------------------- */
static pdw g_xb;                        /* the exchange block                  */
static pdw g_ws, g_wsend;               /* workspace segment, end offset       */
static pdw g_why;                       /* PDJ_R_* of the last refusal         */
static pdw g_open;                      /* an image is open                    */
static pdw g_eng;                       /* its source is a PDENG stream        */

/* the codestream's parameters (20.22.13.1) */
static pdw g_xs, g_ys, g_xts, g_yts;
static pdw g_ntx, g_nty, g_nc, g_nlay, g_ntile;
static pdb g_dep[JX_MAXC], g_sgn[JX_MAXC];
static pdb g_nl, g_xcb, g_ycb, g_sty, g_rev, g_prog, g_mct, g_sop, g_eph, g_grd, g_qsty;
static pdb g_ppx[JX_MAXRES], g_ppy[JX_MAXRES];
static pdb g_qe[40];                    /* eps, per band index                 */
static pdw g_qm[40];                    /* mu                                  */
static pdb g_codref[48];                /* COD's Scod&1 + SPcod, for COC       */
static pdw g_codlen;
static pdb g_qcdref[80];                /* QCD's Sqcd + SPqcd, for QCC         */
static pdw g_qcdlen;
static pdw g_havesiz, g_havecod, g_haveqcd, g_trunc;
static pdw g_alpha;                     /* channel index of the alpha, or 0xFFFF */
static pdw g_jp2;

/* what this open decodes */
static pdw g_nd;                        /* decoded components                  */
static pdw g_dc[4];                     /* their indices                       */
static pdb g_keep[JX_MAXC];             /* component c is decoded              */
static pdw g_mode, g_inv;               /* PDIM_*, PDIMG_INVERT                */
static pdw g_r, g_rtop;                 /* reduction, kept top resolution      */
static pdw g_ow, g_oh, g_rowbytes;
static pdw g_usestrip;

/* ---- the source: PDENG's stream or an inline far buffer -------------------- */
static pdw s_inl, s_seg, s_off, s_left; /* inline source                       */
static pdb s_buf[512];
static pdw s_pos, s_len, s_eof;

static pdw s_fill(void)
{
    pdw n;
    if (s_eof) return 0;
    s_pos = 0; s_len = 0;
    if (s_inl) {
        n = s_left < 512 ? s_left : 512;
        if (!n) { s_eof = 1; return 0; }
        pd_fcopy_in(s_seg, s_off, s_buf, n);
        s_off += n; s_left -= n; s_len = n;
        return 1;
    }
#ifndef PD_HOST
    n = pdxj_engcall(PDV_ENG_STRM_READ, pd_ds(), (pdw)s_buf, 512);
    if (pdxj_engcf || !n) { s_eof = 1; return 0; }
    s_len = n > 512 ? 512 : n;
    return 1;
#else
    s_eof = 1; return 0;
#endif
}
static int s_byte(void)
{
    if (s_pos >= s_len && !s_fill()) return -1;
    return s_buf[s_pos++];
}
/* skip a 32-bit count of bytes; 1 = the stream ended first */
static pdw s_skip32(pdw hi, pdw lo)
{
    pdw k;
    while (hi || lo) {
        if (s_pos >= s_len && !s_fill()) return 1;
        k = s_len - s_pos;
        if (!hi && lo < k) k = lo;
        s_pos += k;
        if (lo < k) hi--;
        lo -= k;
    }
    return 0;
}
/* copy n bytes of the stream to a far address; 1 = the stream ended first */
static pdw s_copy(pdw seg, pdw off, pdw n)
{
    pdw k;
    while (n) {
        if (s_pos >= s_len && !s_fill()) return 1;
        k = s_len - s_pos;
        if (k > n) k = n;
        pd_fcopy_out(s_buf + s_pos, seg, off, k);
        s_pos += k; off += k; n -= k;
    }
    return 0;
}

static pdw rd16(void)
{
    int a = s_byte(), b = s_byte();
    if (a < 0 || b < 0) { g_trunc = 1; return 0; }
    return ((pdw)a << 8) | (pdw)b;
}
static pdw g_sg;                        /* bytes left in the current marker segment */
static pdw sg8(void)
{
    int a;
    if (!g_sg) { g_trunc = 1; return 0; }
    g_sg--;
    a = s_byte();
    if (a < 0) { g_trunc = 1; return 0; }
    return (pdw)a;
}
static pdw sg16(void) { pdw a = sg8(); return (a << 8) | sg8(); }
/* a 32-bit field that must be < 2^15: returns 0xFFFF when it is not */
static pdw sg32s(void)
{
    pdw hi = sg16(), lo = sg16();
    if (hi || lo > JX_MAXAXIS) return 0xFFFF;
    return lo;
}

static pdw refuse(pdw why) { g_why = why; return PDE_FILTER; }

#include "pdxjhdr.c"
#include "pdxjpkt.c"
#include "pdxjt1.c"
#include "pdxjdwt.c"
#include "pdxjimg.c"

/* ---- the verbs --------------------------------------------------------------- */
static pdw v_init(pdw bx)
{
    static struct pdxb_init in;
    g_xb = bx;
    pd_fcopy_in(bx, 0, &in, sizeof in);
    pdxj_engvec[0] = in.eng_off; pdxj_engvec[1] = in.eng_seg;
    if (in.slot_kb < JX_WS_MIN) return PDE_NOMEM;
    g_ws = in.slot_seg;
    g_wsend = in.slot_kb >= 64 ? 0xFFF0u : (pdw)(in.slot_kb << 10);
    g_open = 0;
    return PDE_OK;
}

pdw pdxj_verb(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdw r;
    pdxj_cf = 0;
    switch (verb) {
    case PDV_X_INIT:   r = v_init(bx); break;
    case PDV_IMG_OPEN: r = v_open(); break;
    case PDV_IMG_ROW:  r = v_row(bx, cx, dx); break;
    case PDV_IMG_CLOSE: r = v_close(); break;
    default: r = PDE_SYNTAX; break;
    }
    if (r && r != PDE_BUSY) pdxj_cf = 1;
    return r;
}
