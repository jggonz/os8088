/* ============================================================================
 * os8088 - apps/pagedesk/pdw.h      PDWRT's private declarations
 *
 * PAGEDESK-SPEC 20.5 is the contract of everything in pdw*.c. This header is
 * private to the writer module (pdwrt.c includes every pdw*.c as ONE
 * translation unit, SPEC.md 73.1) - nothing outside PDWRT includes it. The
 * cross-segment ABI is pdabi.h, which this file never edits.
 * ==========================================================================*/
#ifndef PDW_H
#define PDW_H
#include "pdabi.h"
#include "pdmath.h"
#include "pdwmap.h"

/* ---- writer-private numbers (PAGEDESK-SPEC 20.5.1, 20.5.2) -------------- */
#define PDW_OUTMIN   4096u   /* the output region of the writer buffer, bytes
                                (2 x the cluster when that is larger)      */
#define PDW_TENT     8u      /* object table entry: num, gen, off.lo, off.hi */
#define PDW_MAXLN    256u    /* a staged line                                */
#define PDW_TXTMAX   1024u   /* PD_MAXTEXT                                   */
#define PDW_PGBUF    3072u   /* a page / catalog / array object being patched */
#define PDW_TEMPNAME "PDSAVE.$$$"

/* the pen-up marker between ink strokes in the point heap (20.5.4) */
#define PDW_PENUP    ((pd_pt)(-32767 - 1))

/* a fresh document's page (the PDV_WRT_NEWDOC description, 20.5.7) */
#define pdw_newpage pd_newpage   /* pdabi.h's, D11 */

/* PDV_WRT_SAVE / NEWDOC progress words in pdxb_save.state (20.5.9) */
#define PDW_ST_START    0
#define PDW_ST_CONFIRM  0x0100  /* out: the target exists - ask, then ...   */
#define PDW_ST_REPLACE  0x0200  /* in:  ... call again with this set        */
#define PDW_ST_DONE     0x7FFF

/* ---- the PDENG door (the far vector from pdxb_init), 20.5.8 ------------- */
extern pdw pdw_engcf;                           /* CF of the last call      */
pdw pdw_engcall(pdw verb, pdw bx, pdw cx, pdw dx); /* AX; asm on the target   */

/* ---- the file layer (pdwio.c) ------------------------------------------- */
pdw  pdw_io_clu(void);                          /* bytes in a cluster       */
pdw  pdw_io_create(const char *name, pdw seg, pdw off, pdw n);
pdw  pdw_io_append(const char *name, pdw seg, pdw off, pdw n);
pdw  pdw_io_close(const char *name);
pdw  pdw_io_delete(const char *name);
pdw  pdw_io_rename(const char *from, const char *to); /* PDE_EXISTS if taken */
pdw  pdw_src_open(const char *name);
pdw  pdw_src_read(pdw seg, pdw off, pdw cap, pdw *got);
pdw  pdw_src_back(const char *name, const struct pd_u32 *at, pdw seg, pdw off,
                  pdw cap, pdw *got);           /* read a range of any file  */
pdw  pdw_ferr(void);                            /* last FERR mapped to PDE_* */

/* ---- Save As into another folder (D14, 20.11.3) ----------------------- */
void pdw_cross(pdw clus, pdw vol);              /* job start: remember "here" as the target's folder;
                                                   0,0 = one folder, the calls below do nothing   */
void pdw_in_src(void);                          /* stand in the source's folder (PDENG, source reads) */
void pdw_in_tgt(void);                          /* ...and back in the target's                    */
void pdw_src_dir(pdw clus, pdw vol1);           /* Insert (20.16): the "source" folder is now (clus, volume + 1) */

/* ---- the date and the author (set by the primary, 20.5.10) -------------- */
extern char pdw_date[24];                       /* "D:YYYYMMDDHHmmSS", "" = omit */
extern char pdw_author[48];                     /* /T on a note, "" = omit   */
#endif
