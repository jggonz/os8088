/* ============================================================================
 * os8088 - apps/pagedesk/pduslot.c   THE SLOT, ITS CACHE, THE REGIONS AND THE
 *                                    LADDER (PAGEDESK-SPEC 20.22.2)
 *
 * One claim S holds ONE occupant at a time - PDXK, PDXG, PDXF, PDXI, PDXJ or
 * PDWRT - its image + bss at S's own base and its workspace after it. One
 * claim X holds the four regions PDENG and PDREND ask for with PDE_NEEDMEM.
 * Both are taken when a document first needs them and given back at close.
 * Every load is a READ_AT of the package's own file and an OSAPI_DECOMP onto
 * S's base, or a copy back from the XMS/EMS cache of unpacked images when the
 * machine has one. The ladder is driven from the job slices alone (pdujob.c,
 * and the text, find and thumbnail slices), never from a click's chain, so a
 * disk read never sits under D22's 384.
 *
 * THE MACHINERY IS pduslot.inc, in assembly (20.22.2.9: in C it was 2,800
 * bytes of a segment that had 877). What stays here is what an OWNER edits -
 * the occupant table and each occupant's workspace - and ui's stand-ins.
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

/* EACH OCCUPANT'S WORKSPACE, in KiB (20.22.2.3): its owner edits ITS line and
 * the matching row of PAGEDESK-SPEC 20.22.2.3's table, nothing else. The
 * comment lines between them keep five agents' edits from merging into one
 * hunk. PDWRT's is not here: it is pdu_wkb, per job. */
static const unsigned char pdu_occws[PDU_NOCC] = {
    0,
    /* --- */
    0,          /* PDX_KEY  PDXK  (crypt)        */
    /* --- */
    0,          /* PDX_GFX  PDXG  (gfx, blend)   */
    /* --- */
    0,          /* PDX_FNT  PDXF  (fonts)        */
    /* --- */
    0,          /* PDX_IMG  PDXI  (dct, bilevel) */
    /* --- */
    0,          /* PDX_JPX  PDXJ  (jpx)          */
    /* --- */
    0           /* PDWRT: pdu_wkb                */
};

/* pduslot.inc's state that the C reads */
extern int pdu_socc;                    /* the slot's occupant, 0 = empty */
extern int pdu_ladkey;                  /* the ladder just loaded PDXK for OPEN */

/* ui's (20.22.2.6, 20.22.2.7): stand-ins until pduauth.c / pdupref.c land
 * and pagedesk.c defines PDU_AUTH_UI / PDP_XCACHE_UI */
#ifndef PDU_AUTH_UI
static void pdu_auth_reset(void)
{
}
static unsigned pdu_auth_step(void)
{
    return PDE_PASSWORD;                /* an encrypted file: its sentence */
}
#endif
#ifndef PDP_XCACHE_UI
int pdp_xcache(void)
{
    return 1;                           /* Auto */
}
#endif
