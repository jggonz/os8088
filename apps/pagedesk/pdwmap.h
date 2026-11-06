/* ============================================================================
 * os8088 - apps/pagedesk/pdwmap.h     the page map PDWRT and the primary share
 *                                     (PAGEDESK-SPEC 20.11.1)
 *
 * The page map is the order of the pages the user SEES: one 16-bit word a
 * view page, in the claim PDU_DC_PAGES (1 KiB) and, unchanged, in the
 * `pages_seg` of PDV_WRT_SAVE / PDV_WRT_EXTRACT (pdxb_save.pages_count words
 * from pages_seg:0). pages_count = 0 means "the source's own order".
 *
 *   bits 15..14  kind   PDM_SRC     a page of the open document (idx = its
 *                                   0-based index IN THE SOURCE)
 *                       PDM_BLANK   a new empty page the size of source page
 *                                   idx (13.7: "the MediaBox of the current
 *                                   page")
 *                       PDM_FOREIGN a page of another PDF (13.8): idx =
 *                                   slot << 10 | the page's 0-based index
 *   bits 13..12  rot    quarter turns clockwise ADDED to the page's own
 *                       /Rotate
 *   bits 11..0   idx
 *
 * The SECOND time a PDM_SRC entry names one source page it is a duplicate
 * (13.7): a new page object sharing /Contents and /Resources, annotations
 * deep-copied. The first is the original and keeps the page's edit records.
 * The three foreign file names (8.3, NUL, 14 bytes each) sit at byte
 * PDM_NAMES of the same segment, which is why a map holds at most
 * PDM_MAXMAP = 400 pages.
 *
 * Shared by pdwsave.c/pdwpg.c (the writer) and pdupage.c (the primary).
 * ==========================================================================*/
#ifndef PDWMAP_H
#define PDWMAP_H
#define PDM_MAXMAP   400u
#define PDM_NAMES    960u
#define PDM_NSLOT    3u
#define PDM_SRC      0u
#define PDM_BLANK    1u
#define PDM_FOREIGN  2u
#define PDM_MK(kind, rot, idx) ((unsigned)(((unsigned)(kind) << 14) | (((unsigned)(rot) & 3u) << 12) | ((unsigned)(idx) & 0x0FFFu)))
#define PDM_KIND(e)  ((unsigned)((unsigned)(e) >> 14))
#define PDM_ROT(e)   ((unsigned)(((unsigned)(e) >> 12) & 3u))
#define PDM_IDX(e)   ((unsigned)((unsigned)(e) & 0x0FFFu))
#define PDM_FORE(slot, page) ((unsigned)(((slot) << 10) | ((page) & 1023u)))

/* the writer's answer, ORed into pdxb_save.mode when a job ends (20.11.5):
 * PDENG's document was closed to verify the output - re-open it */
#define PDW_OUT_REOPEN  0x0100u
#endif
