/* ============================================================================
 * os8088 - apps/pagedesk/pdwin.c       Insert from another PDF: the foreign
 *                                      windows (PAGEDESK-SPEC 13.8, 20.16)
 *
 * #included by pdwrt.c after pdwgc.c. After PH_TREE (the root /Pages node is
 * written, its /Kids naming every foreign page by the number pm_prep gave it)
 * the job visits each foreign file the map names, one WINDOW each:
 *
 *   PH_FOPEN   close PDENG's document, stand in the file's folder, OPEN it,
 *              read its trailer, carve the bit maps of 20.11.4 from the END
 *              of the object table
 *   PH_XROOT / PH_XDISC / PH_XCOPY   Extract's discovery and copy, run with
 *              gc_ins set: numbers start where the last window ended, the
 *              chosen pages leave the set before ranking
 *   PH_FROOTS  the chosen pages, with their pre-assigned numbers
 *
 * and the last window puts the source's trailer back. The row's layout is
 * pdwmap.h's: slot k's name at PDM_NAMES + 14k, its folder at 1002 + 4k.
 * ==========================================================================*/
#define FW_PLACE 1002u

static struct pdxb_open fw_op;

/* the next slot at or after `from` that a map entry names, else PDM_NSLOT */
static pdw fw_scan(pdw from)
{
    static pdw i, e, k, best;
    best = PDM_NSLOT;
    for (i = 0; i < pm_n; i++) {
        e = pd_fpeekw(g_sv.pages_seg, (pdw)(2 * i));
        if (PDM_KIND(e) != PDM_FOREIGN) continue;
        k = (pdw)((PDM_IDX(e) >> 10) & 3u);
        if (k >= from && k < best) best = k;
    }
    return best;
}

static pdw fw_open(void)
{
    static pdw r, k, i, e;
    if (fg_stage == 0) {                                 /* the next file, or the end */
        k = fw_scan(fg_next);
        if (!fg_closed) {                                /* the first: PDENG leaves the source */
            fg_closed = 1; fg_cur = sv_next;
            g_sv.mode = (pdw)(g_sv.mode | PDW_OUT_REOPEN);
            pdw_engcall(PDV_ENG_CLOSE, 0, 0, 0);
            pw_cpy((char *)&sv_srctr, (const char *)&g_tr, (pdw)sizeof g_tr);
        }
        if (k >= PDM_NSLOT) {                            /* every window done */
            pw_cpy((char *)&g_tr, (const char *)&sv_srctr, (pdw)sizeof g_tr);
            sv_next = fg_cur; gc_ins = 0; sv_pgidx = 0xFFFF;
            sv_phase = PH_XREF; sv_xi = 0;
            return PDE_OK;
        }
        gc_slot = k; gc_ins = 1;
        for (i = 0; i < (pdw)sizeof fw_op; i++) ((char *)&fw_op)[i] = 0;
        pd_fcopy_in(g_sv.pages_seg, (pdw)(PDM_NAMES + 14u * k), fw_op.name, 13);
        if (!fw_op.name[0]) return PDE_RANGE;
        pdw_src_dir(pd_fpeekw(g_sv.pages_seg, (pdw)(FW_PLACE + 4u * k)), pd_fpeekw(g_sv.pages_seg, (pdw)(FW_PLACE + 4u * k + 2)));
        fg_stage = 1;
        return PDE_OK;
    }
    if (fg_stage == 1) {                                 /* OPEN is resumable (D11) */
        pd_fcopy_out(&fw_op, g_xb, 0, sizeof fw_op);
        pdw_in_src(); r = pdw_engcall(PDV_ENG_OPEN, 0, 0, 0); pdw_in_tgt();
        if (pdw_engcf) return r ? r : PDE_SYNTAX;
        pd_fcopy_in(g_xb, 0, &fw_op, sizeof fw_op);
        if (r == PDE_BUSY) return PDE_OK;
        fg_stage = 2;
        return PDE_OK;
    }
    sv_pgidx = 0xFFFF;                                   /* stage 2: this file's facts */
    r = eng_trailer(); if (r) return r;
    gc_nb = 0;
    for (i = 0; i < pm_n; i++) {
        e = pd_fpeekw(g_sv.pages_seg, (pdw)(2 * i));
        if (!gc_mine(e)) continue;
        gc_nb++;
        if ((pdw)(PDM_IDX(e) & 1023u) >= fw_op.npages) { g_sv.err_detail = 17; return PDE_RANGE; }
    }
    gc_nw = (pdw)(((g_tr.size >> 7) + 1) << 3);          /* the maps: 4.25 bytes x gc_nw, at the table's end */
    i = (pdw)(4u * gc_nw + gc_nw / 4u);
    r = (pdw)(g_tb + (pdw)(g_tn + gc_nb + 64) * PDW_TENT);
    if (g_tail < i || (pdw)(g_tail - i) < r) {
        g_sv.err_detail = (pdw)((pdw)(r + i + TB_BYTES) / 1024u + 2);
        return PDE_NOMEM;
    }
    gc_seen = (pdw)(g_tail - i); gc_scan = (pdw)(gc_seen + 2 * gc_nw); gc_pref = (pdw)(gc_scan + 2 * gc_nw);
    fg_tmax0 = g_tmax;
    g_tmax = (pdw)((pdw)(gc_seen - g_tb) / PDW_TENT);
    gc_cap = (pdw)(g_tmax - g_tn - gc_nb - 8);
    pd_ffill(g_wseg, gc_seen, 0, i);
    gc_w = 0; gc_dirty = 0; gc_cnt = 0; gc_over = 0; gc_total = 0; gc_sub = 0; gc_n = 0; gc_blk = 0;
    gc_k = fg_cur; gc_b0 = fg_cur;
    tclr(T_PMDONE, 64); tclr(T_FSRC, 256);
    pdm_u32_set(&gc_zero, 0, 0);
    sv_npages = pm_n; sv_pgseg = g_sv.pages_seg; sv_i = 0;
    fg_stage = 0;
    sv_phase = PH_XROOT;
    return PDE_OK;
}

/* PH_FROOTS: one chosen page a call; the last closes the window */
static pdw fw_roots(void)
{
    static pdw e, r, ds;
    while (sv_i < pm_n && !gc_mine(pd_fpeekw(g_sv.pages_seg, (pdw)(2 * sv_i)))) sv_i++;
    if (sv_i >= pm_n) {
        g_tmax = fg_tmax0; fg_cur = gc_k; fg_next = (pdw)(gc_slot + 1); sv_pgidx = 0xFFFF;
        sv_phase = PH_FOPEN;
        return PDE_OK;
    }
    e = pd_fpeekw(g_sv.pages_seg, (pdw)(2 * sv_i));
    r = eng_pageinfo(gc_pg(e)); if (r) return r;
    r = pg_fetch(g_pi.objnum, g_pi.gen, 1, 0); if (r) return r;
    ds = sc_body(); if (ds == SC_BAD) return PDE_SYNTAX;
    rs_mode = 1;
    pw_obj(tg(T_PMNUM, sv_i), 0);
    r = dk_copy(ds, PGK_PARENT | PGK_ROTATE, 1); if (r) return r;
    pb_s(" /Parent "); pb_u(pm_root); pb_s(" 0 R /Rotate "); pb_u(pm_rotfinal(PDM_ROT(e)));
    /* the page now sits under the SOURCE's root /Pages, whose own inheritable
       /CropBox and /Resources it must not take up: both are always written
       (verify-insert) - pg_geo writes a CropBox only where it differs */
    if (!pf_hascrop) { pb_s(" /CropBox "); pb_rect(&g_pi.crop); pf_hascrop = 1; }
    pg_geo();
    if (ih_a) { pb_s(" /Resources "); rs_walk(ih_a, ih_b, 1); }
    else if (!pf_hasres) pb_s(" /Resources << >>");
    pb_s(" >>\n"); pw_endobj();
    sv_i++;
    return PDE_OK;
}
