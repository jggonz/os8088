/* ============================================================================
 * os8088 - apps/pagedesk/pdxj.h          PDXJ's private declarations
 *
 * PAGEDESK-SPEC 20.22.13 is the contract of everything in pdxj*.c. Private to
 * the JPEG 2000 module (pdxj.c includes every pdxj*.c as ONE translation unit,
 * SPEC.md 73.1). The cross-segment ABI is pdabi.h.
 * ==========================================================================*/
#ifndef PDXJ_H
#define PDXJ_H
#include "pdabi.h"
#include "pdmath.h"

/* limits (20.22.13.1, .3) */
#define JX_MAXC      16
#define JX_MAXNL     12
#define JX_MAXRES    13
#define JX_PTAB      768
#define JX_NCBPKT    384
#define JX_WS_WANT   60               /* the workspace asked for, KiB (20.22.13.3)     */
#define JX_WS_MIN    12               /* the least that still decodes 1600 x 1200 at r5 */
#define JX_CBDATA    4096
#ifndef JX_ARENA_MIN
#define JX_ARENA_MIN 4096u            /* the host fuzz lowers it to reach the arena-full path */
#endif
#define JX_MAXDIM    1024
#define JX_MAXAXIS   32767

/* the reasons an OPEN refuses (20.22.13.1), written to pdxb_img.out_w */
#define PDJ_R_NOTJPX   1
#define PDJ_R_TRUNC    2
#define PDJ_R_GEOM     3
#define PDJ_R_DEPTH    4
#define PDJ_R_STYLE    5
#define PDJ_R_ROI      6
#define PDJ_R_PACKED   7
#define PDJ_R_PERTILE  8
#define PDJ_R_PALETTE  9
#define PDJ_R_PART2    10
#define PDJ_R_ORDER    11
#define PDJ_R_LIMIT    12
#define PDJ_R_MEM      13
#define PDJ_R_SOURCE   14
#define PDJ_R_NOALPHA  15

/* the assembly (pdxjasm.inc) or its host twin (pdxjtw.c). All cdecl. */
void xj_mqinit(pdb *buf);                   /* start a codeword (0xFF 0xFF follow it) */
pdw  xj_mqdec(pdw cx);                      /* one decision in context cx      */
void xj_cxreset(void);                      /* contexts to their initial state */
extern pdb xj_cx[19];
/* 9/7 and 5/3 lifting. NEAR variants work on arrays in this DS, FAR variants on
 * rows of the plane in segment seg (offsets in bytes). n is a count of words. */
void xj_lift_n(pdi *dst, pdi *a, pdi *b, pdw n, pdi k);
void xj_lift_f(pdw seg, pdw dst, pdw a, pdw b, pdw n, pdi k);
void xj_l53_n(pdi *dst, pdi *a, pdi *b, pdw n, pdw mode);
void xj_l53_f(pdw seg, pdw dst, pdw a, pdw b, pdw n, pdw mode);
void xj_scal_n(pdi *dst, pdw n, pdi k);
void xj_scal_f(pdw seg, pdw dst, pdw n, pdi k);
void xj_inter(pdi *out, pdi *e, pdi *o, pdw lw, pdw nh);
void xj_deq(pdw *mag, pdw *flg, pdw seg, pdw off, pdw n, pdw m, pdw sh, pdw rnd);
void xj_grey(pdw sseg, pdw soff, pdw dseg, pdw doff, pdw n, pdi add, pdw sh,
             pdw maxv, pdb *tab, pdw inv);

#endif
