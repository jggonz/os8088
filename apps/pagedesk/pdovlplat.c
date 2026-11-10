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
 * THE HELD KEYS (20.22.2.10). A key that opened the document is kept, with
 * the first 16 bytes of its /ID[0], in one 512-byte claim (pdu_kseg, made on
 * the first key): a save keeps the source's /Encrypt and /ID[0], so the file
 * re-opened after it - and the document re-opened after the Insert's
 * pre-flight - takes it with SETKEY and no question. The same claim carries
 * the pre-flight's pdxb_wkeys, which pduslot.inc's sl_bind hands PDWRT with
 * PDV_WRT_KEYS after every INIT.
 *
 * THE PRE-FLIGHT (20.22.4.5). PDWRT is a slot occupant, so PDXK cannot load
 * while a save runs: before a Save As whose page map holds foreign pages the
 * save job first CLOSEs the document, OPENs each foreign file (with this
 * auth when it asks), keeps an encrypted one's key from CRYPTINFO, CLOSEs it,
 * and OPENs the document again with its held key. Then PDWRT comes in.
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
#define PDU_WK(f)  ((unsigned)&((struct pdxb_wkey *)0)->f)

/* CRYPTINFO of a keyed file into the pdxb_wkey at kseg:off (and, for the
 * document's, its /ID[0]); 0 = no claim or no answer */
static int ovl_key_get(unsigned off)
{
    if (!pdu_kseg && (pdu_kseg = os88_mem_claim_p(512 / 16)) == 0)
        return 0;
    pdu_mcall(PDU_M_ENG, PDV_ENG_CRYPTINFO, pdu_xb, 0, 0);
    if (pdu_mcf)
        return 0;
    pd_fpokew(pdu_kseg, off + PDU_WK(key_len), pd_fpeekw(pdu_xb, PDU_CRY(key_len)));
    pd_fcopy(pdu_kseg, off + PDU_WK(stm), pdu_xb, PDU_CRY(stm), 8);  /* stm str eff encmeta */
    pd_fcopy(pdu_kseg, off + PDU_WK(key), pdu_xb, PDU_CRY(key), 32);
    if (off == PDU_KS_OWN)
        pd_fcopy(pdu_kseg, PDU_KS_OWNID, pdu_xb, PDU_CRY(id0), 16);
    return 1;
}

/* the held key, when this half-open file's /ID[0] is the one it was kept
 * with: SETKEY, and OPEN resumes with no PDXK and no question */
static int ovl_key_try(void)
{
    unsigned i;
    if (!pdu_kseg || !pd_fpeekw(pdu_kseg, PDU_KS_OWN + PDU_WK(key_len)))
        return 0;
    pdu_mcall(PDU_M_ENG, PDV_ENG_CRYPTINFO, pdu_xb, 0, 0);
    if (pdu_mcf)
        return 0;
    for (i = 0; i < 16; i += 2)
        if (pd_fpeekw(pdu_xb, PDU_CRY(id0) + i) != pd_fpeekw(pdu_kseg, PDU_KS_OWNID + i))
            return 0;
    pd_ffill(pdu_xb, 0, 0, sizeof(struct pdxb_crypt));
    pd_fpokew(pdu_xb, PDU_CRY(key_len), pd_fpeekw(pdu_kseg, PDU_KS_OWN + PDU_WK(key_len)));
    pd_fcopy(pdu_xb, PDU_CRY(stm), pdu_kseg, PDU_KS_OWN + PDU_WK(stm), 8);
    pd_fcopy(pdu_xb, PDU_CRY(key), pdu_kseg, PDU_KS_OWN + PDU_WK(key), 32);
    pd_fpokew(pdu_xb, PDU_CRY(auth), PDCA_USER);
    pdu_mcall(PDU_M_ENG, PDV_ENG_SETKEY, pdu_xb, 0, 0);
    return !pdu_mcf;
}

/* the file being opened is a foreign one (the pre-flight's) */
#define pdu_aufor() (pdu_pfl && pdu_pfk < PDM_NSLOT)

/* one slice: PDE_BUSY (again next slice), 0 (the key is set: OPEN again) or
 * the PDE_* the open fails with. `start` on the first */
static unsigned ovl_au_slice(int start)
{
    unsigned r;

    if (start) {
        if (!pdu_aufor() && ovl_key_try())
            return 0;                   /* the held key: OPEN again */
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
            if (!pdu_mcf) {
                r = 0;
                if (!pdu_aufor())
                    ovl_key_get(PDU_KS_OWN);    /* held for the re-opens */
            } else if (!r)
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

/* ---- the pre-flight of an Insert (20.22.4.5) ---- */
static int ovl_save_done(unsigned err);

/* the foreign files the page map names: a mask of their slots */
static unsigned char ovl_ifl_need(void)
{
    unsigned i, e, m;
    m = 0;
    for (i = 0; i < pdu_npages; i++) {
        e = pdu_pgw(i);
        if (PDM_KIND(e) == PDM_FOREIGN)
            m |= 1u << ((PDM_IDX(e) >> 10) & 3);
    }
    return (unsigned char)m;
}

/* one slice of the save job while pdu_pfl: 1 = the job is still ours (or
 * ended, said), 0 = PDWRT is in, its keys handed over: the save goes on */
static int ovl_ifl_step(void)
{
    unsigned r, k, seg, n;
    seg = pdu_dcseg(PDU_DC_PAGES);
    if (pdu_pfl == 1) {                 /* the next file: PDENG let go first */
        while (pdu_pfk < PDM_NSLOT && !(pdu_pfm & (1u << pdu_pfk)))
            pdu_pfk++;
        os88_memset(&pdu_xopen, 0, sizeof(pdu_xopen));
        if (pdu_pfk < PDM_NSLOT)
            pd_fcopy_in(seg, PDM_NAMES + 14 * pdu_pfk, pdu_xopen.name, 13);
        else
            os88_strcpy(pdu_xopen.name, pdu_name, sizeof(pdu_xopen.name));
        pdu_mcall(PDU_M_ENG, PDV_ENG_CLOSE, 0, 0, 0);    /* the last file */
        pdu_opauth = 0;
        pdu_pfl = 2;
        return 1;
    }
    k = pdu_pfk;
    if (k < PDM_NSLOT)                  /* 20.16: the slot's folder, volume + 1 */
        os88_file_goto_q_mark(pd_fpeekw(seg, 1002 + 4 * k), pd_fpeekw(seg, 1004 + 4 * k) - 1);
    else
        pdu_at_doc();
    if (pdu_opauth == 1) {
        r = ovl_au_slice(0);
        if (r == PDE_BUSY)
            return 1;
        pdu_opauth = 2;
        if (r)
            goto failed;
    }
    pd_fcopy_out(&pdu_xopen, pdu_xb, 0, sizeof(pdu_xopen));
    r = pdu_mcall(PDU_M_ENG, PDV_ENG_OPEN, 0, 0, 0);
    pd_fcopy_in(pdu_xb, 0, &pdu_xopen, sizeof(pdu_xopen));
    r = pdu_ladck(PDU_M_ENG, r);
    if (r == 0xFFFF)
        return 1;
    if (pdu_mcf && r == PDE_PASSWORD && !pdu_opauth) {
        pdu_opauth = 1;
        r = ovl_au_slice(1);
        if (r == PDE_BUSY)
            return 1;
        pdu_opauth = 2;
        if (!r)
            return 1;                   /* the key is set: OPEN again */
        goto failed;
    }
    if (!pdu_mcf && r == PDE_BUSY) {
        pdu_progress("Checking ", pdu_xopen.name);
        return 1;
    }
    if (pdu_mcf)
        goto failed;
    if (k < PDM_NSLOT) {                /* a foreign file: its key, when it has one */
        if (pdu_xopen.flags & PDO_ENCRYPTED) {
            n = pdu_kseg ? pd_fpeekw(pdu_kseg, PDU_KS_WKEYS) : 0;
            r = PDU_KS_WKEYS + 2 + sizeof(struct pdxb_wkey) * n;
            if (!ovl_key_get(r)) {
                r = PDE_NOMEM;
                goto failed;
            }
            pd_fpokew(pdu_kseg, r + PDU_WK(slot), k);
            pd_fpokew(pdu_kseg, PDU_KS_WKEYS, n + 1);
        }
        pdu_pfk++;
        pdu_pfl = 1;
        return 1;
    }
    pdu_xsave.mode &= ~PDW_OUT_REOPEN;  /* the document is back */
    pdu_pfl = 0;
    os88_file_goto_q_mark(pdu_tgtplace.clus, pdu_tgtplace.vol);
    r = pdu_wrt_in_now();               /* sl_bind hands it the keys */
    if (!r)
        return 0;
    k = PDM_NSLOT;
failed:
    pdu_pfl = 0;
    pdu_job = J_NONE;
    if (k < PDM_NSLOT)
        pdu_xsave.err_detail = 20 + k;  /* the foreign file at fault (20.16) */
    if (pdu_xsave.mode & PDW_OUT_REOPEN)
        pdu_mcall(PDU_M_ENG, PDV_ENG_CLOSE, 0, 0, 0);
    ovl_save_done(r ? r : PDE_PASSWORD);
    return 1;
}
