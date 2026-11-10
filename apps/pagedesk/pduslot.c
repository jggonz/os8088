/* ============================================================================
 * os8088 - apps/pagedesk/pduslot.c   THE SLOT, ITS CACHE, THE REGIONS AND THE
 *                                    LADDER (PAGEDESK-SPEC 20.22.2)
 *
 * One claim S holds ONE occupant at a time - PDXK, PDXG, PDXF, PDXI, PDXJ or
 * PDWRT - its image + bss at S's own base and its workspace after it. One
 * claim X holds the five regions PDENG, PDREND and PDXF ask for with
 * PDE_NEEDMEM.
 * Both are taken when a document first needs them and given back at close.
 * Every load is a READ_AT of the package's own file and an OSAPI_DECOMP onto
 * S's base, or a copy back from the XMS/EMS cache of unpacked images when the
 * machine has one. The ladder is driven from the job slices alone (pdujob.c,
 * and the text, find and thumbnail slices), never from a click's chain, so a
 * disk read never sits under D22's 384.
 *
 * THE MACHINERY IS pduslot.inc, in assembly (20.22.2.9: in C it was 2,800
 * bytes of a segment that had 877). What stays here is what an OWNER edits -
 * the occupant table and each occupant's workspace range.
 * ==========================================================================*/

/* the parts row and the magic of each occupant, by id */
static const unsigned char pdu_occrow[PDU_NOCC] = {
    0, PDU_PART_XK, PDU_PART_XG, PDU_PART_XF, PDU_PART_XI, PDU_PART_XJ,
    PDU_PART_WRT
};
static const unsigned pdu_occmagic[PDU_NOCC] = {
    0, PDM_MAGIC_KEY, PDM_MAGIC_GFX, PDM_MAGIC_FNT, PDM_MAGIC_IMG,
    PDM_MAGIC_JPX, PDM_MAGIC_WRT
};

/* EACH OCCUPANT'S WORKSPACE, in KiB (20.22.2.3): a RANGE, WANT and MIN. S is
 * sized at `want` while PD-29's 32 KiB stays free beside it, else at the most
 * of at least `min` that fits, and slot_kb says what was granted; a verb that
 * needs more for one stream answers PDE_NEEDMEM with CX = PDMEM_SLOT (the
 * ladder regrows S up to `want`). Its owner edits ITS two lines and the
 * matching row of 20.22.2.3's table, nothing else; the comment lines between
 * them keep the edits from merging into one hunk. PDWRT's is not here: it is
 * pdu_wkb, per job, want = min. */
const unsigned char pdu_occws[PDU_NOCC] = {         /* want */
    0,
    /* --- */
    0,          /* PDX_KEY  PDXK  (crypt)        */
    /* --- */
    13,         /* PDX_GFX  PDXG  (gfx, blend)   */
    /* --- */
    0,          /* PDX_FNT  PDXF  (fonts)        */
    /* --- */
    64,         /* PDX_IMG  PDXI  (dct, bilevel) */
    /* --- */
    60,         /* PDX_JPX  PDXJ  (jpx)          */
    /* --- */
    0           /* PDWRT: pdu_wkb                */
};
const unsigned char pdu_occwmin[PDU_NOCC] = {       /* min */
    0,
    /* --- */
    0,          /* PDX_KEY  PDXK  (crypt)        */
    /* --- */
    13,         /* PDX_GFX  PDXG  (gfx, blend)   */
    /* --- */
    0,          /* PDX_FNT  PDXF  (fonts)        */
    /* --- */
    12,         /* PDX_IMG  PDXI  (dct, bilevel) */
    /* --- */
    12,         /* PDX_JPX  PDXJ  (jpx)          */
    /* --- */
    0           /* PDWRT: pdu_wkb                */
};

/* pduslot.inc's state that the C reads */
extern int pdu_socc;                    /* the slot's occupant, 0 = empty */
