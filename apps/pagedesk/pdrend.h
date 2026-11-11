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
/* OSAPI_DECOMP (PAGEDESK-SPEC 20.22.14): the LZ4 stream of plen bytes at sseg:soff
 * expanded onto dseg:0, exactly ulen bytes; 0 = done, nonzero = refused */
pdw  pdr_lzdec(pdw sseg, pdw soff, pdw plen, pdw dseg, pdw ulen);
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
    pdw smode;                  /* 0 1bpp, 1 grey8, 2 rgb8, 3 stencil, 4 LUT */
    pdw inv;                    /* 1 = invert the sample / stencil sense    */
    pdw dmode;                  /* 1 = 1bpp band, 4 = 4bpp band             */
    pdw dyrow;                  /* y & 3, the dither row                    */
    pdw col;                    /* stencil: 1bpp 0 black / 1 white; 4bpp c4 */
    /* smode 4 (PAGEDESK-SPEC 20.22.7): nc samples of bpc bits (1 2 4 8 16) a
     * pixel; grey = the saturating sum over c of T[c][sample], where T is
     * lseg:loff's 256-byte tables (gfx's struct pdgx_lut body) or, lseg 0,
     * sample x weight / 256 for nc 1 (256), 3 (77 150 29), 4 (77 150 29 256);
     * lflags bit 0 then takes 255 - the sum (CMYK-shaped)                    */
    pdw nc, bpc;
    pdw lseg, loff, lflags;
};
extern struct pdr_ip pdr_ip;
typedef char pdr_ip_size[sizeof(struct pdr_ip) == 42 ? 1 : -1];
void pdr_imgrow(void);

/* the quantiser (pdrx8.inc, 20.22.10): n pixels of 8-bit grey at sseg:soff into a
 * 4bpp (bpp 4) or 1bpp band row at dseg:doff, which is the byte holding pixel x0.
 * 4bpp: the seven-level checker of 20.19.2, `par` = (x0 + y - page_y0) & 1; 1bpp: the
 * 4x4 Bayer, `par` = (y & 3) << 2. A pixel outside [x0, x0+n) in a byte it shares keeps
 * its bits. The assembly's offsets are pdrx8.inc's XQ_*.                                 */
struct pdr_xq { pdw sseg, soff, dseg, doff, x0, n, par, bpp; };
extern struct pdr_xq pdr_xq;
typedef char pdr_xq_size[sizeof(struct pdr_xq) == 16 ? 1 : -1];
void pdr_x8quant(void);

/* globals the asm shares */
extern pdw pdr_eng_ax, pdr_eng_dx;
extern pdw pdr_eng_off, pdr_eng_seg;

/* ---- tunables (PAGEDESK-SPEC 20.3.1) ------------------------------------ */
#define PDR_QMAX        16000   /* |device coordinate| clamp, 1/4 pixel       */
#define PDR_NOPS        24      /* operand stack                              */
#define PDR_NNAME       32
#define PDR_NSTR        512
#define PDR_NPATH       352     /* flattened path vertices                    */
#define PDR_WSB         6912u   /* workspace at the tail of dl_seg: saved gstates + font cache */
#define PDR_MINDL       256u    /* the least list room BEGIN accepts (D13): 7 KiB of dl_seg */
#define PDR_NEDGE       256     /* staged edges                               */
#define PDR_NCLIP       255     /* non-rectangular clips a pass (DLR_CLIP records, 20.3.2); clid 0xFF is none */
#define PDR_NFONT       4
#define PDR_NRUN        64      /* glyphs per DL run record                   */
#define PDR_NSPAN       128     /* crossings per scanline; a row with more is NOT
                                 * painted (a dropped crossing would pair the rest
                                 * wrongly and paint a false span) and is noted */
#define PDR_MAXSTRIDE   96      /* band stride the masks are sized for        */
#define PDR_NFORM       5       /* nested stream frames: page + 4 forms       */
#define PDR_MAXRES      4
#define PDR_XCKB        16      /* PDMEM_XC asked for (20.22.7); any grant >= 1 KiB is used */
#ifdef PD_HOST
extern int pdrh_xasks;                  /* hosttest PDR_XASKS: a small budget exercises the exhausted path */
#define PDR_XASKS       pdrh_xasks
#else
#define PDR_XASKS       8       /* NEEDMOD/NEEDMEM answers a page view may make (20.22.7) */
#endif
#define PDR_XFREE       16      /* ...more, each after a load that answered a verb (x_answer) */

/* ---- display list (PAGEDESK-SPEC 20.3.2) -------------------------------- */
#define DLR_END     0
#define DLR_STATE   1
#define DLR_RECT    2
#define DLR_FILL    3
#define DLR_GLYPHS  4
#define DLR_IMAGE   5
#define DLR_XSTATE  6           /* transparency of the marks that follow (20.22.10.3) */
#define DLR_GROUP   7           /* a transparency group begins or ends (aux 0x80)     */
#define DLR_MASKB   8           /* a soft-mask definition: len spans to its MASKE     */
#define DLR_MASKE   9
#define DLR_CLIP    16          /* [rcore] a polygon clip: rule in aux, its rows; parent, ne, edges (20.3.2) */
#define DLH         8           /* header: type aux len y0 y1                 */
#define DLI_BODY    28          /* DLR_IMAGE's body (20.3.2, 20.22.7); inline rows follow */

/* gfx (PAGEDESK-SPEC 20.22.9): a device colour operator ends a non-device colour space (the GS.pad_ nibbles) */
#ifndef XG_DEVCOL
#define XG_DEVCOL(st) (GS.pad_ &= (pdb)((st) ? 0x0F : 0xF0))
#endif
/* ---- transparency (PAGEDESK-SPEC 20.22.10); the hooks are pdrx8.c / pdrxb.c, the compositor PDX8 ---- */
#ifdef PDR_X8   /* the transparency build: the host harness's, and the target's once the Makefile passes it */
#ifdef PD_HOST
extern pdw pdx8_verb(pdw verb, pdw bx, pdw cx, pdw dx);
#define x8_call(v, b, c, d)  pdx8_verb(v, b, c, d)
#else
pdw pdr_x8far(pdw verb, pdw bx, pdw cx, pdw dx);    /* pdrx8h.inc: AX; CF, CX, DX in x8_cf, x8_ocx, x8_odx */
void pdr_x8bind(pdw seg);
void pdr_x8pack(void);                              /* the band geometry into XB as pdxb_x8band */                           /* the door's code selector, once a region is held */
#define x8_call(v, b, c, d)  pdr_x8far(v, b, c, d)
#endif
extern pdw x8_cf, x8_ocx, x8_odx;
#define x8_span(y, a, b)     ((void)x8_call(PDV_X8_SPAN, (pdw)(y), (pdw)(a), (pdw)(b)))
static void x8_imgrow(pdi y);                       /* PDX8's twin of pdr_imgrow (reads pdr_ip) */
static void x8_imgset(const pdb *b, pdw flags, pdi xl, pdi yt, pdi wq, pdi hq, pdi x0, pdi x1, pdi ry0, pdi ry1);
static pdw g_xs;                                    /* composing an 8-bit strip                */
static void xb_state(pdw kind);                     /* emit_state's hook: the XSTATE record    */
static void xb_mark(pdi y0, pdi y1);                /* dl_put's hook: rows a transparent mark touches */
static void xb_pass(void);                          /* pass_begin: empty the tuple pool        */
static pdw g_x8rdy;                                 /* PDX8 is in the region and has been told */
static pdw g_x8any;                                 /* a transparent mark in this pass         */
static void x8_bits(pdw v);                         /* X8F_* into the incomplete bits          */
static pdw  xb_gs(void);                            /* exec_op's gs arm                        */
static void xb_group_begin(pdw obj, pdw gen);       /* do_form's tail                          */
static void xb_group_end(void);                     /* frame_end's form branch                 */
static void xb_needmem(void);                       /* asks for the region when the page needs it */
static void x8_init(pdw seg, pdw kb);               /* verb_init: take the region              */
#define XB_IMAGE_MASK(obj, gen, kind) do { if ((kind) == 128) inc_note(PDF_INC_TRANSP, "colour key mask"); else g_x8any = 1; } while (0)
static void xb_rows(pdi y0, pdi y1);                /* mark rows for the 8-bit path            */
static void x8_view(void);                          /* verb_begin: size the planes             */
#else
#define xb_state(k)          ((void)0)
#define xb_mark(a, b)        ((void)0)
#define xb_pass()            ((void)0)
#define xb_gs()              ((void)0)
#define xb_group_begin(o, g) ((void)0)
#define xb_group_end()       ((void)0)
#define xb_needmem()         ((void)0)
#define x8_init(s, k)        ((void)0)
#define x8_view()            ((void)0)
#define xb_rows(a, b)        ((void)0)
#endif

/* ---- modes -------------------------------------------------------------- */
#define PDR_IDLE    0
#define PDR_INTERP  1           /* interpreting into the DL                   */
#define PDR_RAST    2           /* drawing bands of the DL                    */
#define PDR_DONE    3
#define PDR_TEXT    4           /* text map                                   */
#define PDR_THUMB   5
#define PDR_XWAIT   6           /* a NEEDMOD/NEEDMEM was answered: what came of it (20.22.7) */

/* ---- font bank (PAGEDESK-SPEC 11.2, 20.3.4) ----------------------------- */
#define PDFB_MAGIC0 0x4450      /* 'PD' little-endian of "PDFB"               */

#endif
