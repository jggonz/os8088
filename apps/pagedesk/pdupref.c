/* ============================================================================
 * os8088 - apps/pagedesk/pdupref.c     PREFERENCES: THE RESIDENT HOOKS
 *                                      (PAGEDESK-SPEC 20.18)
 *
 * The settings array lives in pdu.h (pdumod.c reads it, so it is declared
 * first). Everything with a body - the walk to SYSTEM/APPDATA, the read, the
 * dialog, the save - is pdovlpref.c, an overlay tenant: the overlay is loaded
 * by the idle ladder after the first open in any case, so a launch to the
 * empty view pays for none of it (the first load is the first open's, or the
 * dialog's). What stays here is what the open, the render and the popover's
 * close need without calling it.
 * ==========================================================================*/
static char pdp_loaded;                 /* the file has been read (or refused) */
static int  pdp_row;                    /* the dialog's selected setting, 0..4 */
static char pdp_chg;                    /* bit 0 any, 1 panels, 2 marks: since the dialog opened */
static char pdp_owe;                    /* the same bits, owed to the wake */

/* before the first document's claims, and the dialog's first paint */
static void pdp_load(void)
{
    if (pdp_loaded)
        return;
    pdp_loaded = 1;
    ovl_pf_load();
}

/* a document opened: its first view (20.18.1) */
static void pdp_view(void)
{
    pdu_fit = pdp_cfg[5] == 2 ? 0 : pdp_cfg[5] + 1;
    if (!pdu_fit)
        pdu_zoom = 100;
}

/* the status line's "Page drawn.", except that a form's note (20.18.4) is
 * said once over the first long render's, which would otherwise bury it */
static const char *pdp_done(void)
{
    if (pdp_form) {
        pdp_form = 0;
        return pdp_formmsg;
    }
    return "Page drawn.";
}

/* the popover is going (pdu_pop_shut): what changed is owed to the wake */
static void pdp_closed(void)
{
    if (pdu_pop != P_PREFS || !pdp_chg)
        return;
    pdp_owe = pdp_owe | pdp_chg;
    pdp_chg = 0;
    pdu_kick();
}

static void pdp_wake(void)
{
    if (pdp_owe)
        ovl_pf_wake();
}
