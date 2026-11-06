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

#include "pdrg.c"
#include "pdrd.c"
#include "pdrf.c"
#include "pdri.c"
#include "pdrv.c"
