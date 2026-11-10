/* ============================================================================
 * os8088 - apps/pagedesk/pdovlplat.c   THE KEY OF AN ENCRYPTED OPEN (PAGEDESK-
 *                                      SPEC 20.22.2.6), an overlay tenant
 *
 * The open's AUTH sub-state, run once a slice from the open job (pdujob.c)
 * when PDENG's OPEN pauses at its state 7 with CF = 1, PDE_PASSWORD (crypt's
 * 20.22.5). PDXK comes into the slot, PDV_KEY_COST reads the /Encrypt
 * parameters and prices one run, the empty password is tried silently, and
 * ui's popover (20.22.6, the pdu_au_* calls) asks for the password - and on
 * an 8088 asks first whether an R6 derivation of that length is wanted. The
 * key goes to PDENG with PDV_ENG_SETKEY and the job calls OPEN again.
 *
 * pdxb_crypt lives in the exchange block between the slices: nothing else
 * uses the XB while the open job is the job. It is here, in the overlay, and
 * not in the primary, because it runs at open frequency (CLAUDE.md, 73.14).
 * ==========================================================================*/

static unsigned char pdu_au_st;         /* 0 start, 1 R6 answer, 3 deriving,
                                         * 4 the password asked */
static unsigned char pdu_au_ui;         /* ui's popover is up */
static unsigned char pdu_au_typed;      /* a typed password has been tried */
#define PDU_CRY(f) ((unsigned)&((struct pdxb_crypt *)0)->f)

/* one slice: PDE_BUSY (again next slice), 0 (the key is set: OPEN again) or
 * the PDE_* the open fails with. `start` on the first */
static unsigned ovl_au_slice(int start)
{
    unsigned r;

    if (start) {
        pdu_au_ui = 0;
        pdu_au_typed = 0;
        r = pdu_slot_load(PDX_KEY);
        if (r)
            return r;
        pd_ffill(pdu_xb, 0, 0, sizeof(struct pdxb_crypt));
        r = pdu_mcall(PDU_M_SLOT, PDV_KEY_COST, pdu_xb, 0, 0);
        if (pdu_mcf)
            return r ? r : PDE_ENCRYPT;
        pdu_au_st = 3;
        if (r) {                        /* R6: seconds a run; the empty
                                         * password that works is 2 runs */
            pdu_au_ui = 1;
            if (pdu_xinit.cpu == OS88_CPU_8086) {
                if (!pdu_au_askr6(r * 2))
                    return PDE_PASSWORD;    /* no question: no derivation */
                pdu_au_st = 1;
            } else if (!pdu_au_busy())
                pdu_au_ui = 0;
        }
        return PDE_BUSY;
    }
    if (pdu_au_st == 1 || pdu_au_st == 4) {     /* an answer owed */
        r = pdu_au_poll();
        if (r == 0)
            return PDE_BUSY;
        if (r != 1)
            return PDE_CANCEL;          /* the popover is gone */
        if (pdu_au_st == 4) {           /* the typed password, into the XB */
            pd_fpokew(pdu_xb, PDU_CRY(pw_len), pdu_au_len);
            pd_fcopy_out(pdu_au_pw, pdu_xb, PDU_CRY(pw), sizeof(pdu_au_pw));
            os88_memset(pdu_au_pw, 0, sizeof(pdu_au_pw));
            pdu_au_typed = 1;
        }
        pd_fpokew(pdu_xb, PDU_CRY(state), 0);
        pdu_au_st = 3;
    }
    r = pdu_mcall(PDU_M_SLOT, PDV_KEY_AUTH, pdu_xb, 0, 0);
    if (!pdu_mcf && r == PDE_BUSY) {
        if (pdu_au_ui)
            pdu_au_prog(pd_fpeekw(pdu_xb, PDU_CRY(progress)));
        return PDE_BUSY;
    }
    if (!pdu_mcf || r != PDE_PASSWORD) {
        if (!pdu_mcf) {                 /* the key: PDENG resumes its OPEN */
            r = pdu_mcall(PDU_M_ENG, PDV_ENG_SETKEY, pdu_xb, 0, 0);
            if (!pdu_mcf)
                r = 0;
            else if (!r)
                r = PDE_PASSWORD;
        }
        if (pdu_au_ui)
            pdu_au_close();
        return r;
    }
    if (!pdu_au_ask(pdu_au_typed))      /* wrong, or the empty one was */
        return PDE_PASSWORD;
    pdu_au_ui = 1;
    pdu_au_st = 4;
    return PDE_BUSY;
}
