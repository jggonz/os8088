/* ============================================================================
 * os8088 - apps/pagedesk/pdxi.h        PDXI's private declarations
 *
 * PDXI (PAGEDESK-SPEC 20.22.11) is ONE translation unit: pdxi.c includes
 * pdxidct.c and, behind -DPDXI_CCIT / -DPDXI_JB2, the bilevel agent's
 * pdxiccit.c and pdxijb2.c. Nothing outside PDXI includes this file. The
 * cross-segment ABI is pdabi.h.
 * ==========================================================================*/
#ifndef PDXI_H
#define PDXI_H
#include "pdabi.h"
#include "pdmath.h"

/* ---- PDENG's door (the far vector from pdxb_init) ----------------------- */
extern pdw pdi_rdx;
extern pdw pdi_engcf;                           /* CF of the last call       */
pdw pdi_engcall(pdw verb, pdw bx, pdw cx, pdw dx); /* AX; asm on the target  */

/* ---- the shell's services to a codec ------------------------------------ */
extern struct pdxb_img g_im;    /* the OPEN request, copied out of the XB. A
                                   codec FILLS out_w out_h out_mode out_ncomp
                                   rowbytes scale in it; the shell copies it
                                   back to the XB                           */
extern pdw g_xb;                /* the exchange block's segment             */
extern pdw g_wseg;              /* the workspace segment                    */
extern pdw g_wbytes;            /* usable workspace bytes (<= 64,496: 65,520 less the 1,024-byte
                                   landing zone that follows it)          */
pdw  pdi_src_open(void);        /* open the coded bytes: PDE_OK or PDE_*    */
pdw  pdi_src_open_obj(pdw obj, pdw gen, pdw accept, pdw codec); /* any stream object */
pdw  pdi_src_read(pdw seg, pdw off, pdw max); /* bytes read; 0 = end        */
pdw  pdi_src_get(void *dst, pdw max);   /* the same into a NEAR buffer, max <= 1024 */
void pdi_src_close(void);

/* ---- the codecs (each returns PDE_OK or a PDE_* code) ------------------- */
pdw  pdi_dct_open(void);
pdw  pdi_dct_row(pdw seg, pdw off, pdw row);
void pdi_dct_close(void);
void pdi_dct_drop(void);        /* PDV_X_INIT: forget warm state            */
#endif
