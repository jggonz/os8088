/* ============================================================================
 * os8088 - apps/pagedesk/pdrend.h                  PDREND's private header
 *
 * PAGEDESK-SPEC 20.3 is the authority. Everything here is private to the
 * renderer segment; the only cross-segment contract is pdabi.h.
 *
 * ONE TRANSLATION UNIT: pdrend.c #includes pdr*.c in dependency order
 * (SPEC.md 73.1: a C package or module is one .c plus runtime).
 *
 * C RULES THAT BIND THIS FILE (SPEC.md 73, tools/cc8086.py):
 *   - no local arrays, no struct locals, no &local: every object that is
 *     not a scalar is a file-scope or function-scope STATIC;
 *   - no long/float, no struct assignment (pdr_cpy), no struct by value;
 *   - an `int` is 16 bits on the target and 32 on the host: every
 *     intermediate that can pass 15 bits goes through pdr_muldiv or pdm_*.
 * ==========================================================================*/
#ifndef PDREND_H
#define PDREND_H
#include "pdabi.h"
#include "pdmath.h"

/* ---- assembly primitives (pdrprim.inc on the target, pdr_host.c twin) ---- */
/* trunc(a*b/c) with a 32-bit intermediate; c != 0; result must fit pdi.     */
pdi  pdr_muldiv(pdi a, pdi b, pdi c);
/* one band row: set pixels x0..x1 (inclusive, band relative, x0<=x1).
 * 1bpp: pat = the 8-bit periodic pattern, bit 7 leftmost, 1 = lit (white).
 * 4bpp: col = the colour index 0..15.                                       */
void pdr_fill1(pdw seg, pdw rowoff, pdi x0, pdi x1, pdw pat);
void pdr_fill4(pdw seg, pdw rowoff, pdi x0, pdi x1, pdw col);
/* far call into PDENG's door: AL=verb BX CX DX. Returns 0, or the PDE_ code
 * when CF was set. AX/DX of a success are left in pdr_eng_ax / pdr_eng_dx. */
pdw  pdr_eng(pdw verb, pdw bx, pdw cx, pdw dx);
/* the kernel's blit: kind 1 = BLIT1, 4 = BLIT4. Returns 0 taken, else -1.   */
pdi  pdr_blit(pdw kind, pdw seg, pdw stride, pdi x, pdi y, pdw w, pdw rows);
/* glyph and image row composers read this block (offsets in pdrprim.inc,
 * pinned by the assertion in pdrend.c).                                     */
struct pdr_gp {
    pdw bseg, boff, stride;     /* band segment, offset of the first row    */
    pdi x;                      /* band-relative left edge of the bitmap    */
    pdw nrow, skip;             /* rows to compose, rows to skip at the top */
    pdw gseg, goff;             /* glyph bitmap, row `skip` first           */
    pdw wbytes, wpx;            /* bitmap row bytes (1..3), width in pixels */
    pdw mode;                   /* 0 clear bits, 1 set bits, 2 nibble       */
    pdw col;                    /* mode 2 colour                            */
    pdw shear;                  /* 0, or ascent rows above the baseline + 1 */
    pdw rowstep;                /* bytes from one band row to the next      */
};
extern struct pdr_gp pdr_gp;
/* pdrprim.inc's GP_* offsets are this order, word by word: the size pins it
 * (hosttest/pdr_primtest.py writes the block in this order for both sides) */
typedef char pdr_gp_size[sizeof(struct pdr_gp) == 28 ? 1 : -1];
void pdr_glyph(void);
/* the edge scan (pdrprim.inc): edges are 8-byte records (x0 y0 x1 y1) at eseg:eoff.
 * pdr_act lists the edges that can touch qpx rows qlo..qhi in act[] and returns
 * the count; pdr_scan fills cx[] (ascending) and cd[] with the crossings of the
 * line ys, up to nspan of them, and returns how many. pdr_fcopy is far to far. */
pdw pdr_act(pdw eseg, pdw eoff, pdw ne, pdi qlo, pdi qhi, pdw *act);
pdw pdr_scan(pdw eseg, pdw eoff, const pdw *act, pdw na, pdi ys, pdi *cx, pdb *cd, pdw nspan);
void pdr_fcopy(pdw dseg, pdw doff, pdw sseg, pdw soff, pdw n);
struct pdr_ip {
    pdw bseg, boff;             /* band row start                           */
    pdi x0, x1;                 /* destination pixels, band relative        */
    pdw sseg, soff;             /* source row                               */
    pdw sw;                     /* source width in pixels                   */
    pdw xlo, xhi;               /* DDA: source x = (xhi:xlo) at pixel x0    */
    pdw slo, shi;               /* DDA step per destination pixel           */
    pdw smode;                  /* 0 1bpp, 1 grey8, 2 rgb8, 3 stencil       */
    pdw inv;                    /* 1 = invert the sample / stencil sense    */
    pdw dmode;                  /* 1 = 1bpp band, 4 = 4bpp band             */
    pdw dyrow;                  /* y & 3, the dither row                    */
    pdw col;                    /* stencil: 1bpp 0 black / 1 white; 4bpp c4 */
};
extern struct pdr_ip pdr_ip;
typedef char pdr_ip_size[sizeof(struct pdr_ip) == 32 ? 1 : -1];
void pdr_imgrow(void);

/* globals the asm shares */
extern pdw pdr_eng_ax, pdr_eng_dx;
extern pdw pdr_eng_off, pdr_eng_seg;

/* ---- tunables (PAGEDESK-SPEC 20.3.1) ------------------------------------ */
#define PDR_QMAX        16000   /* |device coordinate| clamp, 1/4 pixel       */
#define PDR_NOPS        24      /* operand stack                              */
#define PDR_NNAME       32
#define PDR_NSTR        512
#define PDR_NPATH       200     /* flattened path vertices                    */
#define PDR_WSB         6912u   /* workspace at the tail of dl_seg: saved gstates + font cache */
#define PDR_MINDL       256u    /* the least list room BEGIN accepts (D13): 7 KiB of dl_seg */
#define PDR_NEDGE       256     /* staged edges                               */
#define PDR_NCLIPE      96      /* edges in the clip pool                     */
#define PDR_NCLIP       8       /* non-rectangular clips in the pool          */
#define PDR_NFONT       4
#define PDR_NRUN        64      /* glyphs per DL run record                   */
#define PDR_NSPAN       128     /* crossings per scanline; a row with more is NOT
                                 * painted (a dropped crossing would pair the rest
                                 * wrongly and paint a false span) and is noted */
#define PDR_MAXSTRIDE   96      /* band stride the masks are sized for        */
#define PDR_NFORM       5       /* nested stream frames: page + 4 forms       */
#define PDR_MAXRES      4

/* ---- display list (PAGEDESK-SPEC 20.3.2) -------------------------------- */
#define DLR_END     0
#define DLR_STATE   1
#define DLR_RECT    2
#define DLR_FILL    3
#define DLR_GLYPHS  4
#define DLR_IMAGE   5
#define DLR_SHADE   6           /* gfx: axial / radial shading (pdrxg.c, 20.22.9)  */
#define DLR_TRI     7           /* gfx: a Gouraud triangle; blend's records take 8 upward */
#define DLH         8           /* header: type aux len y0 y1                 */

/* ---- modes -------------------------------------------------------------- */
#define PDR_IDLE    0
#define PDR_INTERP  1           /* interpreting into the DL                   */
#define PDR_RAST    2           /* drawing bands of the DL                    */
#define PDR_DONE    3
#define PDR_TEXT    4           /* text map                                   */
#define PDR_THUMB   5

/* ---- font bank (PAGEDESK-SPEC 11.2, 20.3.4) ----------------------------- */
#define PDFB_MAGIC0 0x4450      /* 'PD' little-endian of "PDFB"               */

#endif
