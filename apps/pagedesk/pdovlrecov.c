/* ============================================================================
 * os8088 - apps/pagedesk/pdovlrecov.c   THE RECOVERY JOURNAL, OVERLAY PART
 *                                       (PAGEDESK-SPEC 14.7, 14.3, 20.17)
 *
 * #included by pdovl.c: every ovl_* here is cut into the overlay. The format,
 * the debounce and the offers are 20.17; in one line: a command's AFTER state
 * is what is journaled - each record whose CRC differs from the remembered
 * one is written whole - so undo, redo, move and typing cost nothing extra and
 * replay needs only pdu_ed_new.
 *
 * Scratch it borrows, because the claim count and the bss are spent:
 *   - PDU_DC_UNDO bytes 3,584..3,839: 128 words, a record's CRC while writing
 *     and its place in the file while replaying (20.8.1 uses 0..3,327);
 *   - ed_v / ed_w (pduedit.c): the record in hand, and a find record;
 *   - pdu_tbuf: a replayed record's text (a write never runs in a typing
 *     session, and the wake does not run with a popover up).
 * ==========================================================================*/

#define EO_ID     16                    /* pd_edit.id */
#define RC_TAB    3584
#define RC_HDR    36
#define RC_CAP    8192                  /* a fresh file past this (20.17.2) */

static unsigned rc_ss, rc_su, rc_st, rc_tok;
static unsigned rc_js, rc_jl, rc_cnt, rc_new;

/* the journal in its claim, and the CRC table in the undo claim's tail */
static unsigned ovl_rc_w(unsigned o) { return pd_fpeekw(rc_js, o); }
static unsigned ovl_rc_tab(unsigned k)
{
    return pd_fpeekw(pdu_dcseg(PDU_DC_UNDO), RC_TAB + 2 * k);
}
static void ovl_rc_tabset(unsigned k, unsigned v)
{
    pd_fpokew(pdu_dcseg(PDU_DC_UNDO), RC_TAB + 2 * k, v);
}

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

/* stand there; 0 = there is no SYSTEM/APPDATA. rc_cl is its cluster in bytes */
static int ovl_rc_enter(void)
{
    os88_file_here(&rc_here);
    rc_cl = (unsigned)os88_disk_cluster_sectors() << 9;
    rc_k = os88_vol_sys();
    if (rc_dclus && os88_file_goto_q_mark(rc_dclus, rc_k) == 0)
        return 1;
    if (os88_file_goto_q_mark(0, rc_k) == 0
        && ovl_rc_find("SYSTEM")
        && os88_file_goto_q_mark(rc_fclus, rc_k) == 0
        && ovl_rc_find("APPDATA")
        && os88_file_goto_q_mark(rc_fclus, rc_k) == 0) {
        rc_dclus = rc_fclus;
        return 1;
    }
    ovl_rc_leave();
    return 0;
}

/* ---- writing ------------------------------------------------------------ */
/* the staging claim (8 KiB, taken for the write), APPDATA and, for a fresh
 * file, the header. 0; 1 = no memory (try again); -1 = no journal here */
static int ovl_rc_open(void)
{
    rc_ss = os88_mem_claim_p(512);
    if (!rc_ss)
        return 1;
    rc_su = 0;
    if (!ovl_rc_enter() || rc_cl > 4096)
        return -1;
    if (rc_new) {
        pd_ffill(rc_ss, 0, 0, RC_HDR);
        pd_fpokew(rc_ss, 0, 0x4450);                    /* "PD" "J1" */
        pd_fpokew(rc_ss, 2, 0x314A);
        pd_fcopy_out(pdu_name, rc_ss, 4, 14);
        pdu_mcall(PDU_M_ENG, PDV_ENG_FINGERPRINT, 0, 0, 0);
        if (pdu_mcf)
            return -1;
        pd_fcopy(rc_ss, 20, pdu_xb, 0, 8);
        pd_fpokew(rc_ss, 28, rc_seed);
        pd_fpokew(rc_ss, 34, pdm_crc16_far(0xFFFF, rc_ss, 0, 34));
        rc_su = RC_HDR;
    }
    return 0;
}

/* start a record of n body bytes: the staging claim on the first, and -1 when
 * it will not fit (one write per flush, 20.17.3) */
static int ovl_rc_beg(int type, unsigned n)
{
    int r;
    r = 0;
    if (!rc_ss)
        r = ovl_rc_open();
    if (r)
        return r;
    if (rc_su + n + 7 > 8192u - rc_cl)
        return -1;
    rc_st = rc_su;
    pd_fpokeb(rc_ss, rc_su + 2, type);
    rc_su += 3;
    return 0;
}

static void ovl_rc_end(void)
{
    rc_k = rc_su - rc_st - 2;
    pd_fpokew(rc_ss, rc_st, rc_k);
    pd_fpokew(rc_ss, rc_su, pdm_crc16_far(0xFFFF, rc_ss, rc_st + 2, rc_k));
    rc_su += 2;
}

/* the CRC of record i, which is left in ed_v */
static unsigned ovl_rc_crc(int i)
{
    unsigned c;
    pd_fcopy_in(ed_seg(), (unsigned)i * ED_REC, &ed_v, ED_REC);
    c = pdm_crc16(0xFFFF, &ed_v, ED_REC);
    c = pdm_crc16_far(c, ed_seg(), ed_v.text_off, ed_v.text_len);
    return pdm_crc16_far(c, ed_seg(), ed_v.pts_off, ed_v.pts_n << 2);
}

/* T: the store's new count */
static int ovl_rc_small(int type)
{
    int r;
    r = ovl_rc_beg(type, 2);
    if (r)
        return r;
    if (type == 'T') {
        pd_fpokew(rc_ss, rc_su, pdu_ed_n);
        rc_su += 2;
    }
    ovl_rc_end();
    return 0;
}

/* One write, if anything changed: the records whose CRC moved (all of them in
 * a fresh file), a T for a store that shrank,
 * (20.17.1). */
static void ovl_rc_flush(void)
{
    int i, n, r;
    unsigned c;

    n = (int)pdu_ed_n;
    rc_new = !(rc_f & RCF_LIVE) || rc_sz > RC_CAP;
    rc_ss = 0;
    r = 0;
    for (i = 0; i < n && !r; i++) {
        c = ovl_rc_crc(i);                      /* leaves record i in ed_v */
        if (ed_v.page & 0x8000u) {
            continue;
        }
        if (!rc_new && (unsigned)i < rc_n && c == ovl_rc_tab(i))
            continue;
        r = ovl_rc_beg('R', 2 + ED_REC + ed_v.text_len + (ed_v.pts_n << 2));
        if (r)
            break;
        pd_fpokew(rc_ss, rc_su, i);
        pd_fcopy_out(&ed_v, rc_ss, rc_su + 2, ED_REC);
        rc_su += 2 + ED_REC;
        pd_fcopy(rc_ss, rc_su, ed_seg(), ed_v.text_off, ed_v.text_len);
        rc_su += ed_v.text_len;
        pd_fcopy(rc_ss, rc_su, ed_seg(), ed_v.pts_off, ed_v.pts_n << 2);
        rc_su += ed_v.pts_n << 2;
        ovl_rc_end();
        ovl_rc_tabset(i, c);
    }
    if (!r && !rc_new && (unsigned)n < rc_n)
        r = ovl_rc_small('T');
    if (!r && rc_ss) {                          /* padded to the cluster, and two zeros more */
        i = (rc_su + 2 + rc_cl - 1) / rc_cl * rc_cl;
        pd_ffill(rc_ss, rc_su, 0, i - rc_su);
        if (rc_new) {
            r = os88_file_write_seg(RC_JRN, rc_ss, i);
            rc_sz = 0;
        } else
            r = os88_file_write_seq(RC_JRN, rc_ss, 0, i, 0, &rc_tok);
        rc_sz += i;
        if (r)
            r = -1;
    }
    if (rc_ss) {
        os88_mem_free(rc_ss);
        ovl_rc_leave();
    }
    if (r > 0) {                                /* no memory: soon, again */
        rc_f |= RCF_DIRTY;
        rc_t = os88_ticks();
    } else if (r < 0) {
        rc_f |= RCF_OFF;
        pdu_wsay("Recovery journal off.");
    } else if (rc_ss) {
        rc_f |= RCF_LIVE;
        rc_n = n;
    }
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
    rc_n = rc_sz = 0;
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

/* Walk the records, stopping at the first bad one. The table gets, per index,
 * where the LAST valid R's body is; rc_cnt is the store's count after the last
 * T or R. Returns the live
 * R count. */
static int ovl_rc_scan(void)
{
    unsigned o, l, t, k;
    int v;

    for (k = 0; k < ED_MAX; k++)
        ovl_rc_tabset(k, 0);
    rc_cnt = 0;
    for (o = RC_HDR; o + 4 <= rc_jl; o += 4 + l) {
        l = ovl_rc_w(o);
        if (l == 0) {                           /* the padding: on to the cluster's end */
            o = (o / rc_cl + 1) * rc_cl - 4;
            continue;
        }
        if (o + 4 + l > rc_jl
            || pdm_crc16_far(0xFFFF, rc_js, o + 2, l) != ovl_rc_w(o + 2 + l))
            break;
        t = ovl_rc_w(o + 2) & 0xFF;
        k = ovl_rc_w(o + 3);
        if (t == 'R') {
            pd_fcopy_in(rc_js, o + 5, &ed_v, ED_REC);
            if (k >= ED_MAX || l < 3 + ED_REC || ed_v.text_len > ED_TMAX)
                break;
            ovl_rc_tabset(k, o + 3);
            if (k >= rc_cnt)
                rc_cnt = k + 1;
        } else if (t == 'T') {
            for (rc_cnt = k; k < ED_MAX; k++)
                ovl_rc_tabset(k, 0);
        }
    }
    v = 0;
    for (k = 0; k < rc_cnt; k++)
        if (ovl_rc_tab(k))
            v++;
    return v;
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

    rc_f &= ~RCF_JRN;
    if (ovl_rc_enter()) {
        v = ovl_rc_has(RC_JRN);
        ovl_rc_leave();
        if (v)
            rc_f |= RCF_JRN;
    }

    if (ovl_rc_ftemp()) {
        pdu_say2("Finish save of ", pdu_tbuf + 4);
        rc_pk = 2;
        ovl_rc_leave();
    } else if (ovl_rc_load()) {
        v = ovl_rc_scan();
        if (v && ovl_rc_match()) {
            os88_utoa(v, pdu_num);
            pdu_say2("Recover ", pdu_num);
            pdu_cat(pdu_msg, " marks", sizeof(pdu_msg));
            rc_pk = 1;
        }
        ovl_rc_free();
    }
    if (rc_pk) {
        pdu_cat(pdu_msg, "? Enter: yes, Esc: no.", sizeof(pdu_msg));
        pdu_wsay(pdu_msg);
    }
}

/* make the records, in index order, as ONE command (20.17.5) */
static int ovl_rc_replay(void)
{
    unsigned k, o, id, maxid, tl, np;
    int r;

    r = 0;
    maxid = 0;
    pdu_undo_begin(UL_NONE);
    for (k = 0; k < rc_cnt && !r; k++) {
        o = ovl_rc_tab(k);
        tl = np = 0;
        if (o) {
            pd_fcopy_in(rc_js, o + 2, &ed_v, ED_REC);
            tl = ed_v.text_len;
            np = ed_v.pts_n;
            if (!ovl_ed_room(tl, np << 2))
                r = -1;
            else {                              /* points in place: 13.1.1's captured stroke */
                pd_fcopy_in(rc_js, o + 2 + ED_REC, pdu_tbuf, tl);
                pd_fcopy(ed_seg(), pdu_ed_pt - (np << 2), rc_js,
                         o + 2 + ED_REC + tl, np << 2);
            }
        } else {                                /* a hole: keep the indices */
            os88_memset(&ed_v, 0, ED_REC);
            ed_v.kind = PDA_AREA;
            ed_v.flags = PDEF_DELETED;
        }
        id = ed_v.id;
        if (!r && pdu_ed_new(&ed_v, pdu_tbuf, tl, (const pd_pt *)0, np) < 0)
            r = -1;
        if (!r) {
            pd_fpokew(ed_seg(), k * ED_REC + EO_ID, id);
            if (id > maxid)
                maxid = id;
        }
    }
    pdu_undo_end();
    if (maxid > pdu_ed_idc)
        pdu_ed_idc = maxid;
    return r;
}

/* Enter or Esc at the prompt (the wake). The journal is deleted unless the
 * marks it holds are only in memory now, or nothing was done with it. */
static void ovl_rc_answer(int yes)
{
    int keep;

    keep = 0;
    pdu_say("");
    if (yes) {
        keep = 1;
        pdu_say("Could not recover.");
        if (rc_pk == 2) {
            if (ovl_rc_ftemp() && os88_file_rename("PDSAVE.$$$", pdu_tbuf + 4) == 0) {
                pdu_say2("Saved ", pdu_tbuf + 4);
                keep = 0;
            }
            ovl_rc_leave();
        } else if (!pdu_ed_n && ovl_rc_load()) {        /* marks made meanwhile: not now */
            ovl_rc_scan();
            if (ovl_rc_replay() == 0) {
                pdu_say("Recovered.");
                rc_f &= ~RCF_LIVE;              /* the next write is a fresh file */
            }
            ovl_rc_free();
        }
    }
    if (!keep)
        ovl_rc_retire();
    pdu_wsay(pdu_msg);
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
