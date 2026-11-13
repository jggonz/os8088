/* ============================================================================
 * os8088 - apps/pagedesk/pdovlrecov.c   THE RECOVERY JOURNAL, OVERLAY PART
 *                                       (PAGEDESK-SPEC 14.7, 14.3, 20.17)
 *
 * #included by pdovl.c: every ovl_* here is cut into the overlay. The format,
 * the debounce and the offers are 20.17; in one line: the journal is the
 * store's AFTER state - every record, whole, in index order, one file written
 * after a burst of commands - so undo, redo, move and typing cost nothing
 * extra and replay needs only pdu_ed_new.
 *
 * Scratch it borrows, because the claim count and the bss are spent:
 *   - ed_v / ed_w (pduedit.c): the record in hand, and a find record;
 *   - pdu_tbuf: a replayed record's text (a write never runs in a typing
 *     session, and the wake does not run with a popover up).
 * ==========================================================================*/

#define EO_ID     16                    /* pd_edit.id */
#define RC_HDR    36

static unsigned rc_js, rc_jl;           /* the journal's claim and length... */
#define rc_ss rc_js                     /* ...and the staging claim's: a flush */
#define rc_su rc_jl                     /* and a load never overlap */

static unsigned ovl_rc_w(unsigned o) { return pd_fpeekw(rc_js, o); }

/* the folder `nm` where we stand: its cluster in rc_fclus */
static int ovl_rc_find(const char *nm)
{
    int ord, i;
    ord = 0;
    while ((ord = os88_file_find(ord, rc_ff)) >= 0) {
        if (rc_ff->type != OS88_FT_DIR)
            continue;
        for (i = 0; i < 13 && rc_ff->name[i] == nm[i] && nm[i]; i++)
            ;
        if (rc_ff->name[i] == nm[i]) {
            rc_fclus = rc_ff->clus;
            return 1;
        }
    }
    return 0;
}

/* is there a file of that name here? (a one-byte read says FERR_BIG for a
 * file and FERR_NOENT for none) */
static int ovl_rc_has(const char *nm)
{
    os88_file_read(nm, &ed_v, 1);
    return os88_ferr() != OS88_FERR_NOENT;
}

static void ovl_rc_leave(void)
{
    os88_file_goto_q_mark(rc_here.clus, rc_here.vol);
}

/* stand there; 0 = there is no SYSTEM/APPDATA. WALKED every time, never
 * banked: this visit WRITES, and a banked cluster on a boot floppy that was
 * swapped since is a cluster of somebody else's disk (SPEC.md 19.9.1: "a
 * banked visit never creates") */
static int ovl_rc_enter(void)
{
    os88_file_here(&rc_here);
    rc_k = os88_vol_sys();
    if (os88_file_goto_q_mark(0, rc_k) == 0
        && ovl_rc_find("SYSTEM")
        && os88_file_goto_q_mark(rc_fclus, rc_k) == 0
        && ovl_rc_find("APPDATA")
        && os88_file_goto_q_mark(rc_fclus, rc_k) == 0)
        return 1;
    ovl_rc_leave();
    return 0;
}

/* ---- writing ------------------------------------------------------------ */
/* The whole store, every record, in index order, as one file (20.17.2). A
 * page the journal cannot rebuild is written as a deleted placeholder so the
 * indices hold. 8 KiB is the most one write stages: more, and the journal is
 * off for the session (said). */
static void ovl_rc_flush(void)
{
    int i, r;

    if (!pdu_ed_n && !(rc_f & RCF_LIVE))
        return;
    rc_ss = os88_mem_claim_p(512);
    if (!rc_ss) {                               /* no memory: soon, again */
        rc_f |= RCF_DIRTY;
        rc_t = os88_ticks();
        return;
    }
    r = -1;
    pd_ffill(rc_ss, 0, 0, RC_HDR);
    pd_fpokew(rc_ss, 0, 0x4450);                /* "PD" "J1" */
    pd_fpokew(rc_ss, 2, 0x314A);
    pd_fcopy_out(pdu_name, rc_ss, 4, 14);
    i = 0;
    if (!(rc_f & RCF_LIVE)) {                   /* the document's identity, ONCE: */
        pdu_mcall(PDU_M_ENG, PDV_ENG_FINGERPRINT, 0, 0, 0); /* it re-reads 2 KiB */
        i = pdu_mcf;                            /* and drops PDENG's cache */
        pd_fcopy_in(pdu_xb, 0, rc_fp, 8);
    }
    if (!i && ovl_rc_enter()) {
        pd_fcopy_out(rc_fp, rc_ss, 20, 8);
        pd_fpokew(rc_ss, 34, pdm_crc16_far(0xFFFF, rc_ss, 0, 34));
        rc_su = RC_HDR;
        r = 0;
        for (i = 0; i < (int)pdu_ed_n && !r; i++) {
            pd_fcopy_in(ed_seg(), (unsigned)i * ED_REC, &ed_v, ED_REC);
            if (ed_v.page & 0x8000u) {          /* a hole */
                os88_memset(&ed_v, 0, ED_REC);
                ed_v.kind = PDA_AREA;
                ed_v.flags = PDEF_DELETED;
            }
            rc_k = ED_REC + ed_v.text_len + (ed_v.pts_n << 2);
            if (rc_su + rc_k + 4 > 8192u) {
                r = -1;
                break;
            }
            pd_fpokew(rc_ss, rc_su, rc_k);
            pd_fcopy_out(&ed_v, rc_ss, rc_su + 2, ED_REC);
            rc_su += 2 + ED_REC;
            pd_fcopy(rc_ss, rc_su, ed_seg(), ed_v.text_off, ed_v.text_len);
            rc_su += ed_v.text_len;
            pd_fcopy(rc_ss, rc_su, ed_seg(), ed_v.pts_off, ed_v.pts_n << 2);
            rc_su += ed_v.pts_n << 2;
            pd_fpokew(rc_ss, rc_su, pdm_crc16_far(0xFFFF, rc_ss, rc_su - rc_k, rc_k));
            rc_su += 2;
        }
        if (!r && os88_file_write_seg(RC_JRN, rc_ss, rc_su))
            r = -1;
        ovl_rc_leave();
    }
    os88_mem_free(rc_ss);
    if (r) {
        rc_f |= RCF_OFF;
        pdu_wsay("Recovery off.");
    } else
        rc_f |= RCF_LIVE;
}

/* ---- reading ------------------------------------------------------------ */
static void ovl_rc_retire(void)
{
    if (ovl_rc_enter()) {
        os88_file_delete(RC_JRN);
        os88_file_delete(RC_TGT);
        ovl_rc_leave();
    }
    rc_f &= ~(RCF_LIVE | RCF_FIN);
}

static void ovl_rc_free(void)
{
    os88_mem_free(rc_js);
    rc_js = 0;
}

/* the journal into a claim (rc_js, rc_jl), its header valid; 0 = none */
static int ovl_rc_load(void)
{
    rc_js = 0;
    if (!ovl_rc_enter())
        return 0;
    if (ovl_rc_has(RC_JRN)) {
        rc_js = os88_mem_claim_p(1024);                 /* 16 KiB: a journal is never more */
        if (rc_js) {
            rc_jl = os88_file_read_seg(RC_JRN, rc_js, 16384);
            if (rc_jl < RC_HDR || ovl_rc_w(0) != 0x4450 || ovl_rc_w(2) != 0x314A
                || pdm_crc16_far(0xFFFF, rc_js, 0, 34) != ovl_rc_w(34))
                ovl_rc_free();
        }
    }
    ovl_rc_leave();
    return rc_js != 0;
}

/* Walk the records, stopping at the first bad one (a bad CRC, a length that
 * does not fit, a torn tail). Returns how many are good; with apply, makes them
 * in index order as ONE command (20.17.5): the points go in place first, as a
 * captured stroke's do (13.1.1), then each record's id is put back. */
static int ovl_rc_walk(int apply)
{
    unsigned o, l, id, maxid, tl, np;
    int cnt;

    cnt = 0;
    maxid = 0;
    if (apply && ovl_ed_need())         /* the points go in before the record */
        return 0;
    if (apply)
        pdu_undo_begin(UL_NONE);
    for (o = RC_HDR; o + 4 <= rc_jl; o += 4 + l) {
        l = ovl_rc_w(o);
        if (l < ED_REC || o + 4 + l > rc_jl || cnt >= ED_MAX
            || pdm_crc16_far(0xFFFF, rc_js, o + 2, l) != ovl_rc_w(o + 2 + l))
            break;
        pd_fcopy_in(rc_js, o + 2, &ed_v, ED_REC);
        tl = ed_v.text_len;
        np = ed_v.pts_n;
        if (tl > ED_TMAX || np > ED_PMAX || l != ED_REC + tl + (np << 2))
            break;                      /* np << 2 must not wrap (hostile file) */
        if (apply) {
            if (!ovl_ed_room(tl, np << 2))
                break;
            pd_fcopy_in(rc_js, o + 2 + ED_REC, pdu_tbuf, tl);
            pd_fcopy(ed_seg(), pdu_ed_pt - (np << 2), rc_js, o + 2 + ED_REC + tl, np << 2);
            id = ed_v.id;
            if (pdu_ed_new(&ed_v, pdu_tbuf, tl, (const pd_pt *)0, np) < 0)
                break;
            pd_fpokew(ed_seg(), cnt * ED_REC + EO_ID, id);
            if (id > maxid)
                maxid = id;
        }
        cnt++;
    }
    if (apply) {
        pdu_undo_end();
        if (maxid > pdu_ed_idc)
            pdu_ed_idc = maxid;
    }
    return cnt;
}

/* the interrupted save's note (20.17.6): its temp file is in the folder it
 * names and its target is not. 1 = finishing it makes sense, and we stand in
 * that folder with the target's name in pdu_tbuf + 4. A note that points at
 * nothing is deleted: it would send every open to the overlay */
static int ovl_rc_ftemp(void)
{
    int k;
    k = 0;
    if (ovl_rc_enter()) {
        if (os88_file_read(RC_TGT, pdu_tbuf, 32) == 18
            && !(pdu_tbuf[17] = 0)      /* a name, whatever the file says */
            && os88_file_goto_q_mark(((pdw *)pdu_tbuf)[1],
                                     ((pdw *)pdu_tbuf)[0]) == 0)
            k = ovl_rc_has("PDSAVE.$$$") && !ovl_rc_has(pdu_tbuf + 4);
        if (!k) {
            ovl_rc_leave();
            if (ovl_rc_enter())
                os88_file_delete(RC_TGT);
            ovl_rc_leave();
        }
    }
    return k;
}

/* the journal's source is the document that is open: the engine's fingerprint
 * (6.2) of it is the one in the header (20.17.5). The name is not compared: a
 * renamed or copied file with the same bytes is the same source */
static int ovl_rc_match(void)
{
    unsigned k;
    if (pdu_doc != D_OPEN)
        return 0;
    pdu_mcall(PDU_M_ENG, PDV_ENG_FINGERPRINT, 0, 0, 0);
    if (pdu_mcf)
        return 0;
    for (k = 0; k < 8; k += 2)
        if (pd_fpeekw(pdu_xb, k) != ovl_rc_w(20 + k))
            return 0;
    return 1;
}

/* the offers (20.17.5, 20.17.6) */
static void ovl_rc_check(void)
{
    int v;

    rc_f &= ~RCF_JRN;                   /* set again below while there is
                                         * something a later open may want */
    if (ovl_rc_ftemp()) {
        pdu_say2("Finish save of ", pdu_tbuf + 4);
        rc_pk = 2;
        rc_f |= RCF_JRN;
        ovl_rc_leave();
    } else if (ovl_rc_load()) {
        rc_f |= RCF_JRN;                /* perhaps another document's */
        v = ovl_rc_walk(0);
        if (v && ovl_rc_match()) {
            os88_utoa(v, pdu_num);
            pdu_say2("Recover ", pdu_num);
            pdu_cat(pdu_msg, " marks", sizeof(pdu_msg));
            rc_pk = 1;
        }
        ovl_rc_free();
    }
    if (rc_pk) {
        pdu_cat(pdu_msg, "? Enter yes, Esc no.", sizeof(pdu_msg));
        pdu_wsay(pdu_msg);
    }
}

/* Enter or Esc at the prompt (the wake). The journal is deleted unless the
 * marks it holds are only in memory now, or nothing was done with it. */
static void ovl_rc_answer(int yes)
{
    int keep;

    keep = 0;
    pdu_say("");                        /* Esc: the prompt goes */
    if (yes) {
        keep = 1;
        pdu_say("Not recovered.");
        if (rc_pk == 2) {
            if (ovl_rc_ftemp() && os88_file_rename("PDSAVE.$$$", pdu_tbuf + 4) == 0) {
                pdu_say2("Saved ", pdu_tbuf + 4);
                keep = 0;
            }
            ovl_rc_leave();
        } else if (!pdu_ed_n && ovl_rc_load()) {        /* marks made meanwhile: not now */
            os88_gfx_lock();            /* the store's flush paints Undo: the wake's paints take it */
            if (ovl_rc_walk(1))
                pdu_say("Recovered.");
            os88_gfx_unlock();
            ovl_rc_free();
        }
    }
    if (!keep)
        ovl_rc_retire();
    pdu_wsay(pdu_msg);
}

/* a save is starting, and the finish offer is still up, unanswered: its note
 * goes. This save is about to write PDSAVE.$$$ in its own folder, and if it
 * died mid-copy, the next launch would otherwise offer the PARTIAL file under
 * the old note's name (recov-verify 5). The old temp file stays on the disk;
 * only the offer goes */
static void ovl_rc_drop(void)
{
    if (rc_pk != 2)
        return;
    rc_pk = 0;
    if (ovl_rc_enter()) {               /* it leaves by itself when it fails */
        os88_file_delete(RC_TGT);
        ovl_rc_leave();
    }
}

/* a save is about to delete its target: the note that lets the next launch
 * finish it (20.17.6). Not a flush - a folder and a name, and it must not
 * depend on there being any marks */
static void ovl_rc_fin(void)
{
    if (rc_f & RCF_FIN)
        return;
    rc_f |= RCF_FIN;
    if (ovl_rc_enter()) {
        ((pdw *)pdu_tbuf)[0] = pdu_tgtplace.vol;
        ((pdw *)pdu_tbuf)[1] = pdu_tgtplace.clus;
        os88_strcpy(pdu_tbuf + 4, pdu_target, 14);
        os88_file_write(RC_TGT, pdu_tbuf, 18);
        ovl_rc_leave();
    }
}

/* the wake's work, one piece a slice, in the order that matters */
static void ovl_rc_wake(void)
{
    if (rc_f & RCF_RETIRE) {
        rc_f &= ~RCF_RETIRE;
        ovl_rc_retire();
    } else if ((rc_f & RCF_CHECK) && !rc_pk) {
        rc_f &= ~RCF_CHECK;
        ovl_rc_check();
    } else if (rc_f & RCF_ANS) {
        ovl_rc_answer(rc_f & RCF_YES);
        rc_f &= ~(RCF_ANS | RCF_YES);
        rc_pk = 0;
    } else if ((rc_f & RCF_DIRTY) && !rc_pk && pdu_doc == D_OPEN) {
        if ((unsigned)(os88_ticks() - rc_t) < RC_QUIET)
            pdu_kick();
        else {
            rc_f &= ~RCF_DIRTY;
            if (!(rc_f & RCF_OFF))
                ovl_rc_flush();
        }
    }
}
