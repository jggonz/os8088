/* ============================================================================
 * os8088 - apps/pagedesk/pdrend.c                 PDREND.MOD's C (one unit)
 *
 * The rendering segment (PAGEDESK-SPEC 10, 11, 20.3): content interpreter,
 * display list, band rasteriser, glyph and image drawing, text map and
 * thumbnails. pdrend.asm is the header, the door and the assembly primitives;
 * this file is everything that decides.
 * ==========================================================================*/
#ifdef PD_HOST
#include "hosttest/pdr_host.h"
#else
#include "os88.h"
#endif
#include "pdrend.h"
#ifndef PD_HOST
/* DECISIONS D1: eng adds this thunk (and its prototype) to apps/cc; declared here
 * with the exact prototype until that is merged - a repeated identical
 * declaration is harmless once it is */
unsigned os88_file_read_seq(const char *name, unsigned seg, unsigned off,
                            unsigned cap, unsigned *cursor);
#endif

#include "pdrg.c"
#include "pdrxc.c"   /* the slot and the extension cache (20.22.2, 20.22.7) */
#include "pdrd.c"
#include "pdrf.c"
#include "pdri.c"
#include "pdrv.c"
#include "pdra.c"
