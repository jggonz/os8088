/* ============================================================================
 * os8088 - apps/pagedesk/pdmath.h      PageDesk arithmetic (PAGEDESK-SPEC 4)
 *
 * The ONLY arithmetic on pd_u32 / pd_fx / pd_mat, the ONLY view transform
 * (PAGEDESK-SPEC 5.3) and the ONLY far-memory movers (4.3). On the target the
 * bodies are assembly in apps/pagedesk/pdmath.inc, %included by each module's
 * shim; on the host they are apps/pagedesk/pdmath.c. The two are held to each
 * other by a differential host test. OWNED BY THE LEAD (signatures); the
 * `plat` agent owns pdmath.inc and the differential test.
 *
 * Every argument that is a struct is a POINTER (SPEC.md 73: no struct by
 * value), and every pointer is a near pointer into the CALLER's DS. A result
 * pointer may equal an operand pointer.
 * ==========================================================================*/
#ifndef PDMATH_H
#define PDMATH_H
#include "pdabi.h"

/* sticky overflow: set by any operation whose exact result does not fit,
 * cleared only by pdm_ovf_clear(). */
pdw  pdm_ovf(void);
void pdm_ovf_clear(void);

/* ---- unsigned 32 -------------------------------------------------------- */
void pdm_u32_set(struct pd_u32 *r, pdw hi, pdw lo);
void pdm_u32_add(struct pd_u32 *r, const struct pd_u32 *a, const struct pd_u32 *b);
void pdm_u32_sub(struct pd_u32 *r, const struct pd_u32 *a, const struct pd_u32 *b);
void pdm_u32_add16(struct pd_u32 *r, pdw n);
pdi  pdm_u32_cmp(const struct pd_u32 *a, const struct pd_u32 *b); /* -1,0,1 */
void pdm_u32_mul16(struct pd_u32 *r, pdw a, pdw b);               /* a*b    */
pdw  pdm_u32_divmod16(struct pd_u32 *q, const struct pd_u32 *a, pdw d);
                                    /* q = a/d, returns a%d; d=0 sets ovf  */
void pdm_u32_shr(struct pd_u32 *r, const struct pd_u32 *a, pdw n);
void pdm_u32_shl(struct pd_u32 *r, const struct pd_u32 *a, pdw n);
/* decimal: writes up to 10 digits + NUL into buf, returns digits written;
 * width > 0 zero-pads to width (the xref's 10-digit offsets). */
pdw  pdm_u32_todec(char *buf, const struct pd_u32 *a, pdw width);

/* ---- signed 16.16 ------------------------------------------------------- */
void pdm_fx_int(struct pd_fx *r, pdi n);
pdi  pdm_fx_floor(const struct pd_fx *a);
pdi  pdm_fx_round(const struct pd_fx *a);
void pdm_fx_add(struct pd_fx *r, const struct pd_fx *a, const struct pd_fx *b);
void pdm_fx_sub(struct pd_fx *r, const struct pd_fx *a, const struct pd_fx *b);
void pdm_fx_mul(struct pd_fx *r, const struct pd_fx *a, const struct pd_fx *b);
void pdm_fx_div(struct pd_fx *r, const struct pd_fx *a, const struct pd_fx *b);
void pdm_fx_neg(struct pd_fx *r, const struct pd_fx *a);
pdi  pdm_fx_cmp(const struct pd_fx *a, const struct pd_fx *b);
/* PDF number syntax ("-12", "3.25", ".5", "4.") into 16.16, rounded to
 * nearest; returns PDE_OK or PDE_RANGE (|value| >= 32768). len bytes, no NUL
 * needed. */
pdw  pdm_fx_parse(struct pd_fx *r, const char *s, pdw len);
/* 16.16 -> decimal for the writer: at most 4 fraction digits, trailing zeros
 * and a bare '.' dropped ("12", "0.5", "-3.1416"). Returns length. */
pdw  pdm_fx_todec(char *buf, const struct pd_fx *a);
/* points <-> pt16 (1/16 pt); to_pt16 saturates and sets ovf out of range. */
pd_pt pdm_fx_to_pt16(const struct pd_fx *a);
void  pdm_pt16_to_fx(struct pd_fx *r, pd_pt p);

/* ---- matrices ----------------------------------------------------------- */
void pdm_mat_ident(struct pd_mat *m);
void pdm_mat_mul(struct pd_mat *r, const struct pd_mat *a, const struct pd_mat *b);
                                    /* r = a x b in PDF order (a applied first) */
void pdm_mat_apply(struct pd_fx *x, struct pd_fx *y, const struct pd_mat *m);
                                    /* (x,y) <- (x,y) x m, in place        */

/* ---- the view transform (PAGEDESK-SPEC 5.3), the ONLY one --------------- */
void pdm_view_fwd(pd_px *px, pd_px *py, const struct pd_view *v, pd_pt x, pd_pt y);
void pdm_view_inv(pd_pt *x, pd_pt *y, const struct pd_view *v, pd_px px, pd_px py);
/* the displayed page's size in pixels at this view */
void pdm_view_size(pd_px *w, pd_px *h, const struct pd_view *v);

/* ---- far memory (PAGEDESK-SPEC 4.3); refuse (return 0) rather than wrap - */
pdw  pd_fcopy_in(pdw seg, pdw off, void *dst, pdw n);
pdw  pd_fcopy_out(const void *src, pdw seg, pdw off, pdw n);
pdw  pd_fcopy(pdw dseg, pdw doff, pdw sseg, pdw soff, pdw n);
pdb  pd_fpeekb(pdw seg, pdw off);
pdw  pd_fpeekw(pdw seg, pdw off);
void pd_fpokeb(pdw seg, pdw off, pdb v);
void pd_fpokew(pdw seg, pdw off, pdw v);
pdw  pd_ffill(pdw seg, pdw off, pdb v, pdw n);
pdw  pd_ds(void);                   /* this segment's DS, for a far address */

/* ---- CRC-16/CCITT (fingerprint, journal) --------------------------------- */
pdw  pdm_crc16(pdw crc, const void *p, pdw n);
pdw  pdm_crc16_far(pdw crc, pdw seg, pdw off, pdw n);

#endif /* PDMATH_H */
