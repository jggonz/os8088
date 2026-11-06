/* ============================================================================
 * os8088 - apps/pagedesk/pdwsave.c     the save and new-document jobs
 *                                      (PAGEDESK-SPEC 14.2, 14.3, 14.6, 20.5.7-9)
 *
 * #included by pdwrt.c. One job at a time, resumable: sv_phase is the state,
 * every call to job_slice() does at most SV_UNITS units of work (a copied
 * chunk, an object group, 16 xref entries) and answers PDE_BUSY until the
 * last. The job's bytes go to PDSAVE.$$$ and only a COMPLETE, self-checked
 * temp file is ever renamed (14.3).
 * ==========================================================================*/
#define SV_UNITS 4

enum { PH_IDLE, PH_COPY, PH_PAGES, PH_CAT, PH_ANNOTS, PH_HEAD, PH_NPAGES, PH_XREF, PH_TRAILER,
       PH_CLOSE, PH_VERIFY, PH_RENAME, PH_DONE };

static pdw sv_active, sv_fresh, sv_phase, sv_i, sv_xi, sv_cnt, sv_base, sv_next, sv_v14, sv_v15;
static pdw sv_catver, sv_xbad, sv_copyw, sv_sub, sv_coff, sv_flushed;
static struct pdxb_save g_sv;
static struct pdxb_trailer g_tr;
static struct pdxb_pageinfo g_pi;
static struct pdxb_rawcopy g_rc;
static struct pdxb_fingerprint g_fp;
static struct pd_u32 sv_prev, sv_xrefpos, sv_total;
static pdw sv_xstart;
                    /* the file to copy (wave 2: pdxb_save.source, 20.5.12) */
static pdw sv_pgidx = 0xFFFF;           /* the page whose pageinfo g_pi holds */
static pdw sv_idw[8];
static pdw sv_npages;
static pdw sv_pgseg;

/* near copies of the fields every scan wants (a record in the far store is
   48 bytes; a scan over 256 of them should not re-read each one) */
static pdw g_epg[PD_MAXEDITS], g_eid[PD_MAXEDITS], g_ekf[PD_MAXEDITS];
static pdw g_esn[PD_MAXEDITS], g_esg[PD_MAXEDITS], g_onum[PD_MAXEDITS], g_oapn[PD_MAXEDITS];

#define EK(i) ((pdw)(g_ekf[i] & 255))
#define EF(i) ((pdw)(g_ekf[i] >> 8))

/* ---- PDENG calls ---------------------------------------------------------- */
static pdw eng_trailer(void)
{
    pdw r = pdw_engcall(PDV_ENG_TRAILER, 0, 0, 0);
    if (pdw_engcf) return r ? r : PDE_XREF;
    pd_fcopy_in(g_xb, 0, &g_tr, sizeof g_tr);
    return PDE_OK;
}
static pdw eng_fingerprint(void)
{
    pdw r = pdw_engcall(PDV_ENG_FINGERPRINT, 0, 0, 0);
    if (pdw_engcf) return r ? r : PDE_IO;
    pd_fcopy_in(g_xb, 0, &g_fp, sizeof g_fp);
    return PDE_OK;
}
static pdw eng_pageinfo(pdw idx)
{
    pdw r;
    if (sv_pgidx == idx) return PDE_OK;
    r = pdw_engcall(PDV_ENG_PAGEINFO, idx, 0, 0);
    if (pdw_engcf) return r ? r : PDE_RANGE;
    pd_fcopy_in(g_xb, 0, &g_pi, sizeof g_pi);
    sv_pgidx = idx;
    return PDE_OK;
}
/* object num gen's bytes into g_pg / sc_n; PDE_LIMIT when it does not fit */
static pdw eng_raw(pdw num, pdw gen)
{
    pdw r;
    g_rc.objnum = num; g_rc.gen = gen; pdm_u32_set(&g_rc.done, 0, 0);
#ifdef PD_HOST
    g_rc.dseg = PDW_HOST_SCRATCH; g_rc.doff = 0;
#else
    g_rc.dseg = pd_ds(); g_rc.doff = (pdw)g_pg;
#endif
    g_rc.dmax = PDW_PGBUF; g_rc.got = 0;
    pd_fcopy_out(&g_rc, g_xb, 0, sizeof g_rc);
    r = pdw_engcall(PDV_ENG_RAW_COPY, 0, 0, 0);
    if (pdw_engcf) return r ? r : PDE_XREF;
    pd_fcopy_in(g_xb, 0, &g_rc, sizeof g_rc);
    sc_n = g_rc.got;
    if (sc_n > PDW_PGBUF) return PDE_LIMIT;
#ifdef PD_HOST
    pd_fcopy_in(PDW_HOST_SCRATCH, 0, g_pg, sc_n);
#endif
    /* is there more? ask for the next 8 bytes into the slack past the buffer.
     * `done` is in/out (pdabi.h): PDENG has advanced it already, so set it
     * rather than add to it */
    pdm_u32_set(&g_rc.done, 0, sc_n);
#ifdef PD_HOST
    g_rc.dseg = PDW_HOST_SCRATCH; g_rc.doff = PDW_PGBUF;
#else
    g_rc.doff = (pdw)(g_pg + PDW_PGBUF);
#endif
    g_rc.dmax = 8; g_rc.got = 0;
    pd_fcopy_out(&g_rc, g_xb, 0, sizeof g_rc);
    r = pdw_engcall(PDV_ENG_RAW_COPY, 0, 0, 0);
    if (pdw_engcf) return r ? r : PDE_XREF;
    pd_fcopy_in(g_xb, 0, &g_rc, sizeof g_rc);
    if (g_rc.got) return PDE_LIMIT;
    g_pg[sc_n] = 0;
    return PDE_OK;
}

/* ---- the scan of the edit store ------------------------------------------- */
static pdw sv_bad(pdw why) { g_sv.err_detail = why; return PDE_LIMIT; }

/* the live note record (not record i) whose id is `id`, else 0xFFFF */
static pdw sv_note(pdw i, pdw id)
{
    pdw j;
    for (j = 0; j < sv_cnt; j++)
        if (j != i && EK(j) == PDA_NOTE && !(EF(j) & PDEF_DELETED) && g_eid[j] == id) return j;
    return 0xFFFF;
}
static pdw sv_scan(void)
{
    pdw i, j, next = sv_base, k, f, irt;
    sv_v14 = sv_v15 = 0;
    /* pass 1: the near copies, and every live record checked */
    for (i = 0; i < sv_cnt; i++) {
        ed_head(i);
        g_ekf[i] = (pdw)(g_ed.kind | (g_ed.flags << 8));
        g_epg[i] = g_ed.page; g_eid[i] = g_ed.id; g_esn[i] = g_ed.src_num; g_esg[i] = g_ed.src_gen;
        g_onum[i] = 0; g_oapn[i] = 0;
        k = g_ed.kind; f = g_ed.flags;
        if (f & PDEF_DELETED) continue;
        if (k == PDA_PAGEOP) return sv_bad(7);                   /* page ops: 13.7, wave 2 */
        if (k < PDA_NOTE || k > PDA_SIGN) { g_sv.err_detail = 10; return PDE_RANGE; }
        if (g_ed.text_len > PDW_TXTMAX) return sv_bad(1);
        if (g_ed.text_off > (pdw)(0xFFFFu - g_ed.text_len)) return sv_bad(2);
        if (g_ed.pts_n > 8000 || g_ed.pts_off > (pdw)(0xFFFFu - 4u * g_ed.pts_n)) return sv_bad(3);
        if (k == PDA_HIGHLIGHT && (g_ed.pts_n < 4 || (g_ed.pts_n & 3))) return sv_bad(4);
        if ((k == PDA_INK || k == PDA_SIGN) && g_ed.pts_n < 1) return sv_bad(5);
        if (k == PDA_LINE && g_ed.pts_n != 2) return sv_bad(6);
        if (!sv_fresh && (g_ed.page & 0x8000)) return sv_bad(7);   /* page ops: wave 2 */
        if (sv_fresh && (g_ed.page & 0x7FFF) >= sv_npages) return PDE_RANGE;
        if (sv_fresh && k == PDA_NOTE && g_ed.irt && (f & PDEF_IRTSRC)) { g_sv.err_detail = 8; return PDE_RANGE; }
    }
    /* pass 2: numbers, in edit order. A review reply whose PageDesk parent is
     * gone (deleted, or never there) is DROPPED - its /IRT would otherwise name
     * whatever object happens to carry the parent's id as its number */
    for (i = 0; i < sv_cnt; i++) {
        k = EK(i); f = EF(i);
        if (f & PDEF_DELETED) continue;
        irt = 0;
        if (k == PDA_NOTE) {
            ed_head(i); irt = g_ed.irt;
            if (irt && !(f & PDEF_IRTSRC) && sv_note(i, irt) == 0xFFFF) continue;
        }
        if (g_esn[i] && !sv_fresh) {
            for (j = 0; j < i; j++)                 /* one record per source object */
                if (g_onum[j] == g_esn[i] && g_esn[j]) return sv_bad(9);
            g_onum[i] = g_esn[i];
        } else g_onum[i] = next++;
        if (k == PDA_NOTE && irt) { g_oapn[i] = 0; sv_v15 = 1; }
        else g_oapn[i] = next++;
        /* 13.5 by key, PDF 1.7 tables 164-182: /NM (13.2, on every annotation)
         * and /LE, /Q, the highlight's /BM are 1.4; /Subj (area mark, drawn
         * signature) and the review state's /IRT /State /StateModel are 1.5.
         * (/RT would be 1.6 and is not written: /R is its default.) */
        sv_v14 = 1;
        if (k == PDA_AREA || k == PDA_SIGN) sv_v15 = 1;
        if (next > 0xFFF0) return PDE_LIMIT;
    }
    sv_next = next;
    return PDE_OK;
}
/* an incremental save keeps the source's page list: a page order that is not
   the identity (a move, a delete) is 13.7's new /Pages tree, which is wave 2 */
static pdw sv_order(void)
{
    pdw i;
    if (!g_sv.pages_count) return PDE_OK;
    if (!g_sv.pages_seg) return PDE_NOMEM;
    for (i = 0; i < g_sv.pages_count; i++)
        if (pd_fpeekw(g_sv.pages_seg, (pdw)(2 * i)) != i) return sv_bad(7);
    if (eng_pageinfo(g_sv.pages_count) == PDE_OK) return sv_bad(7);     /* a page left out */
    sv_pgidx = 0xFFFF;
    return PDE_OK;
}
/* the object a reply points at: the live note record with that id, else irt is
   a source object number (generation 0) */
static void sv_parent(pdw i, pdw irt, pdw irt_src, pdw *n, pdw *g)
{
    pdw j;
    if (irt_src) { *n = irt; *g = 0; return; }
    for (j = 0; j < sv_cnt; j++)
        if (j != i && EK(j) == PDA_NOTE && g_onum[j] && g_eid[j] == irt) {
            *n = g_onum[j]; *g = (sv_fresh || !g_esn[j]) ? 0 : g_esg[j];
            return;
        }
    *n = irt; *g = 0;
}

/* ---- the page-dictionary patch (14.2 step 2) ------------------------------ */
static pdw pg_deleted(pdw page, pdw n, pdw g)
{
    pdw j;
    for (j = 0; j < sv_cnt; j++)
        if (g_epg[j] == page && g_esn[j] == n && g_esg[j] == g && (EF(j) & PDEF_DELETED)) return 1;
    return 0;
}
static void pg_adds(pdw page)           /* " n 0 R" for each new annotation of the page */
{
    pdw j;
    for (j = 0; j < sv_cnt; j++)
        if (g_epg[j] == page && g_onum[j] && !g_esn[j]) { pb_c(' '); pb_u(g_onum[j]); pb_s(" 0 R"); }
}
/* the array value at [vs,ve) of g_pg with deleted refs dropped and the page's
   new refs appended */
static pdw pg_array(pdw vs, pdw ve, pdw page)
{
    static pdw g;
    pdw p = (pdw)(vs + 1), ie, n;
    pb_c('[');
    for (;;) {
        p = sc_skip(p);
        if (p >= ve) return PDE_SYNTAX;
        if (g_pg[p] == ']') break;
        ie = sc_val(p, 2);
        if (ie == SC_BAD) return PDE_SYNTAX;
        n = sc_ref(p, ie, &g);
        if (!(n && pg_deleted(page, n, g))) { pb_c(' '); pb_n(g_pg + p, (pdw)(ie - p)); }
        p = ie;
    }
    pg_adds(page);
    pb_c(']');
    return PDE_OK;
}
/* revise page `page` (its object is in g_pi): /Annots gains the new refs and
   loses the deleted ones. Emits one or two revised objects. */
static pdw pg_patch(pdw page)
{
    static pdw ag;
    pdw r, ds, f, an;
    r = eng_raw(g_pi.objnum, g_pi.gen); if (r) return r;
    ds = sc_body(); if (ds == SC_BAD) return PDE_SYNTAX;
    f = dict_find(ds, "Annots");
    if (f == SC_BAD) return PDE_SYNTAX;
    if (f && g_pg[sc_vs] == '[') {                      /* inline array       */
        pw_obj(g_pi.objnum, g_pi.gen);
        pb_n(g_pg + ds, (pdw)(sc_vs - ds));
        r = pg_array(sc_vs, sc_ve, page); if (r) return r;
        pb_n(g_pg + sc_ve, (pdw)(sc_close + 2 - sc_ve));
        pb_s("\n"); pw_endobj();
        return PDE_OK;
    }
    if (f) {                                            /* an indirect array   */
        an = sc_ref(sc_vs, sc_ve, &ag);
        if (!an) return PDE_SYNTAX;
        r = eng_raw(an, ag); if (r) return r;
        ds = sc_body(); if (ds == SC_BAD || g_pg[ds] != '[') return PDE_SYNTAX;
        f = sc_val(ds, 1); if (f == SC_BAD) return PDE_SYNTAX;
        pw_obj(an, ag);
        r = pg_array(ds, f, page); if (r) return r;
        pb_s("\n"); pw_endobj();
        return PDE_OK;
    }
    pw_obj(g_pi.objnum, g_pi.gen);                      /* no /Annots yet      */
    pb_n(g_pg + ds, (pdw)(sc_close - ds));
    pb_s(" /Annots ["); pg_adds(page); pb_s(" ]");
    pb_n(g_pg + sc_close, 2);
    pb_s("\n"); pw_endobj();
    return PDE_OK;
}
/* a page group is the first live edit of a page that adds or deletes */
static pdw pg_relevant(pdw i)
{
    if (g_epg[i] & 0x8000) return 0;
    if (EK(i) < PDA_NOTE || EK(i) > PDA_SIGN) return 0;
    return (g_onum[i] && !g_esn[i]) || ((EF(i) & PDEF_DELETED) && g_esn[i]);
}
static pdw pg_first(pdw i)
{
    pdw j;
    if (!pg_relevant(i)) return 0;
    for (j = 0; j < i; j++) if (pg_relevant(j) && g_epg[j] == g_epg[i]) return 0;
    return 1;
}
/* the catalog with /Version raised (13.5) */
static pdw cat_patch(void)
{
    pdw r, ds, f;
    r = eng_raw(g_tr.root_num, g_tr.root_gen); if (r) return r;
    ds = sc_body(); if (ds == SC_BAD) return PDE_SYNTAX;
    f = dict_find(ds, "Version");
    if (f == SC_BAD) return PDE_SYNTAX;
    /* PDENG's version is the HEADER's: a catalog that already says /1.x or
       /2.x at least what the output needs is left alone, never lowered */
    if (f == 1 && sc_ve - sc_vs >= 4 && g_pg[sc_vs + 1] >= '1' && g_pg[sc_vs + 1] <= '9'
        && g_pg[sc_vs + 2] == '.' && g_pg[sc_vs + 3] >= '0' && g_pg[sc_vs + 3] <= '9'
        && (pdw)((g_pg[sc_vs + 1] - '0') * 10 + (g_pg[sc_vs + 3] - '0')) >= sv_catver)
        return PDE_OK;
    pw_obj(g_tr.root_num, g_tr.root_gen);
    if (f) {
        pb_n(g_pg + ds, (pdw)(sc_vs - ds));
        pb_s(sv_catver == 15 ? "/1.5" : "/1.4");
        pb_n(g_pg + sc_ve, (pdw)(sc_close + 2 - sc_ve));
    } else {
        pb_n(g_pg + ds, (pdw)(sc_close - ds));
        pb_s(sv_catver == 15 ? " /Version /1.5 " : " /Version /1.4 ");
        pb_n(g_pg + sc_close, 2);
    }
    pb_s("\n"); pw_endobj();
    return PDE_OK;
}

/* ---- one annotation (+ its appearance), as a state machine --------------------
 * Each call does one bounded step (an_run); an annotation with a point list is
 * several steps, so a 500-point stroke never holds a slice for more than ~1.5 KB
 * of output (20.5.5). */
enum { AS_START, AS_LIST, AS_TAIL, AS_COUNT0, AS_COUNT, AS_HEAD, AS_BODY, AS_END, AS_DONE };
static pdw an_sub, an_pn, an_pg;

static const char *const pw_keepk[4] = { "NM", "T", "Popup", "CreationDate" };
static pdw an_prep(pdw i)
{
    static pdw pn, pg;
    pdw r, ds, k, n;
    ed_load(i);
    ed_rect();
    if (sv_fresh) { g_pgnum = (pdw)(7 + 2 * (g_ed.page & 0x7FFF)); g_pggen = 0; }
    else {
        r = eng_pageinfo(g_ed.page); if (r) return r;
        g_pgnum = g_pi.objnum; g_pggen = g_pi.gen;
    }
    g_keepn = 0; g_keepf = 0;       /* a replaced annotation keeps its identity (13.2, 20.5.5) */
    if (!sv_fresh && g_esn[i] && !eng_raw(g_esn[i], g_esg[i])) {
        ds = sc_body();
        for (k = 0; ds != SC_BAD && k < 4; k++) {
            if (dict_find(ds, pw_keepk[k]) != 1) continue;
            n = (pdw)(sc_ve - sc_kpos);                 /* "/Key value"          */
            if (n >= (pdw)(sizeof g_keep - g_keepn)) continue;
            pw_cpy(g_keep + g_keepn, g_pg + sc_kpos, n);
            g_keepn = (pdw)(g_keepn + n); g_keep[g_keepn++] = ' ';
            if (k == 0) g_keepf |= KEEP_NM;
            if (k == 1) g_keepf |= KEEP_T;
        }
    }
    pn = pg = 0;
    if (g_ed.kind == PDA_NOTE && g_ed.irt) sv_parent(i, g_ed.irt, (pdw)(g_ed.flags & PDEF_IRTSRC), &pn, &pg);
    an_pn = pn; an_pg = pg;
    return PDE_OK;
}
static pdw an_run(void)
{
    pdw r, i = sv_i;
    switch (an_sub) {
    case AS_START:
        r = an_prep(i); if (r) return r;
        if (annot_a(g_onum[i], (sv_fresh || !g_esn[i]) ? 0 : g_esg[i], g_oapn[i], an_pn, an_pg)) { bl_reset(); an_sub = AS_LIST; }
        else an_sub = g_oapn[i] ? AS_COUNT0 : AS_DONE;
        break;
    case AS_LIST:
        if (ak_list()) an_sub = AS_TAIL;
        break;
    case AS_TAIL:
        annot_c(g_oapn[i]); an_sub = AS_COUNT0;
        break;
    case AS_COUNT0:                     /* the /Length: one counting pass         */
        pb_flush(); g_mode = 1; pdm_u32_set(&g_cnt, 0, 0); bl_reset(); an_sub = AS_COUNT;
        break;
    case AS_COUNT:
        if (ap_step()) { pb_flush(); u32cp(&ap_len, &g_cnt); g_mode = 0; an_sub = AS_HEAD; }
        break;
    case AS_HEAD:
        ap_head(g_oapn[i]); bl_reset(); an_sub = AS_BODY;
        break;
    case AS_BODY:
        if (ap_step()) an_sub = AS_END;
        break;
    case AS_END:
        ap_tail(); an_sub = AS_DONE;
        break;
    }
    if (an_sub == AS_DONE) { an_sub = AS_START; sv_i++; }
    return PDE_OK;
}

/* ---- ID, trailer, xref ---------------------------------------------------- */
static void id_make(void)
{
    pdw k;
    sv_idw[0] = g_crc; sv_idw[1] = g_nm_hi; sv_idw[2] = g_nm_lo; sv_idw[3] = g_pos.lo;
    sv_idw[4] = g_tn; sv_idw[5] = sv_cnt; sv_idw[6] = pdm_crc16(0xFFFF, pdw_date, pw_len(pdw_date)); sv_idw[7] = g_pos.hi;
    for (k = 0; k < 8; k++)             /* spread: each word sees the others */
        sv_idw[k] = pdm_crc16((pdw)(sv_idw[k] ^ (pdw)(0x9E37u * (k + 1))), sv_idw, 16);
}
static void pb_id(void)
{
    pdw k;
    pb_c('<'); for (k = 0; k < 8; k++) pb_hex4(sv_idw[k]); pb_c('>');
}
static void pb_zu(pdw v, pdw width)     /* zero-padded decimal */
{
    pdw k = pw_utoa(g_nb, v), z;
    for (z = k; z < width; z++) pb_c('0');
    pb_n(g_nb, k);
}
static void xr_entry(pdw k)
{
    pdw at = (pdw)(g_outcap + k * PDW_TENT);
    static struct pd_u32 off;
    off.lo = pd_fpeekw(g_wseg, (pdw)(at + 4)); off.hi = pd_fpeekw(g_wseg, (pdw)(at + 6));
    pb_u32(&off, 10); pb_c(' '); pb_zu(pd_fpeekw(g_wseg, (pdw)(at + 2)), 5); pb_s(" n \n");
}
static pdw tn_num(pdw k) { return pd_fpeekw(g_wseg, (pdw)(g_outcap + k * PDW_TENT)); }

/* ---- the temp file's check (14.2 step 4, the part PDWRT can do alone) ------ */
static char vt[200];
static pdw sv_vstage;
static struct pd_u32 sv_vx;
/* stage 0: the file ends "startxref N %%EOF" and N is where we wrote "xref";
   stage 1: the bytes at N spell "xref". Two reads, two slices. */
static pdw job_verify(void)
{
    static struct pd_u32 at, x, one, a8, a2;
    static pdw got;
    pdw r, k, p, mask = (pdw)(g_clu - 1);
    if (sv_vstage == 0) {
        pdm_u32_set(&one, 0, 1);
        pdm_u32_sub(&at, &g_pos, &one);
        at.lo = (pdw)(at.lo & ~mask);
        /* "startxref N %%EOF" can straddle the last cluster boundary: when the
         * last cluster holds under 190 bytes, read from the one before too
         * (the output region is at least two clusters) */
        if ((pdw)(g_pos.lo - at.lo) < 190 && (at.lo | at.hi)) {
            pdm_u32_set(&one, 0, g_clu); pdm_u32_sub(&at, &at, &one);
        }
        r = pdw_src_back(g_tmp, &at, g_wseg, 0, g_outcap, &got); if (r) return r;
        if (got < 12) return PDE_TRUNC;
        k = got > 190 ? 190 : got;
        pd_fcopy_in(g_wseg, (pdw)(got - k), vt, k);
        if (!(vt[k - 6] == '%' && vt[k - 5] == '%' && vt[k - 4] == 'E' && vt[k - 3] == 'O' && vt[k - 2] == 'F')) return PDE_TRUNC;
        p = (pdw)(k - 7);                       /* the newline before %%EOF   */
        while (p > 0 && vt[p - 1] >= '0' && vt[p - 1] <= '9') p--;
        pdm_u32_set(&x, 0, 0);
        for (; p < (pdw)(k - 7); p++) {         /* x = x * 10 + digit         */
            pdm_u32_shl(&a8, &x, 3); pdm_u32_shl(&a2, &x, 1); pdm_u32_add(&x, &a8, &a2);
            pdm_u32_add16(&x, (pdw)(vt[p] - '0'));
        }
        if (pdm_u32_cmp(&x, &sv_xrefpos) != 0) return PDE_XREF;
        u32cp(&sv_vx, &x); sv_vstage = 1; g_io++;
        return PDE_OK;
    }
    u32cp(&at, &sv_vx); at.lo = (pdw)(at.lo & ~mask);
    r = pdw_src_back(g_tmp, &at, g_wseg, 0, g_outcap, &got); if (r) return r;
    p = (pdw)(sv_vx.lo & mask);
    if (got < (pdw)(p + 4)) return PDE_TRUNC;
    if (!(pd_fpeekb(g_wseg, p) == 'x' && pd_fpeekb(g_wseg, (pdw)(p + 1)) == 'r'
          && pd_fpeekb(g_wseg, (pdw)(p + 2)) == 'e' && pd_fpeekb(g_wseg, (pdw)(p + 3)) == 'f')) return PDE_XREF;
    sv_vstage = 2; g_io++;
    return PDE_OK;
}

/* ---- starting and finishing ------------------------------------------------ */
static void job_clear(void)
{
    sv_active = 0; sv_phase = PH_IDLE; g_err = 0; g_created = 0; g_wpos = 0; g_lnn = 0; sv_pgidx = 0xFFFF; g_mode = 0; an_sub = AS_START;
}
static pdw job_fail(pdw err)
{
    if (g_created || sv_active) pdw_io_delete(g_tmp);
    job_clear();
    return err;
}

static pdw job_begin(pdw fresh)
{
    pdw r, i;
    pd_fcopy_in(g_xb, 0, &g_sv, sizeof g_sv);
    if (!g_wseg) return PDE_NOMEM;
    for (i = 0; i < 14; i++) g_tmp[i] = 0;
    pw_cpy(g_tmp, PDW_TEMPNAME, pw_len(PDW_TEMPNAME));
    pw_cpy(pdw_date, g_sv.date, 23); pdw_date[23] = 0; pw_cpy(pdw_author, g_sv.author, 47); pdw_author[47] = 0;
    r = pw_setup(g_wseg, g_wkb); if (r) { g_sv.err_detail = r == PDE_NOMEM ? g_wantkb : 0; return r; }
    sv_fresh = fresh; g_edseg = g_sv.edits_seg; sv_cnt = g_sv.edits_count;
    if (sv_cnt > PD_MAXEDITS) return PDE_LIMIT;
    if (sv_cnt && !g_edseg) return PDE_NOMEM;           /* D2: a claim of 0 is no claim */
    if (!fresh && !pdw_engvec[1]) return PDE_NOMEM;     /* no PDENG to read the source  */
    sv_xbad = 0; sv_catver = 0; sv_xi = 0; sv_i = 0; sv_xstart = 0; sv_copyw = 0; sv_sub = 0; sv_coff = 0; g_io = 0; an_sub = AS_START; sv_flushed = 0; sv_vstage = 0;
    pdm_u32_set(&sv_total, 0, 0);
    if (!fresh) {
        r = eng_trailer(); if (r) return r;
        r = eng_fingerprint(); if (r) return r;
        g_nm_hi = g_fp.crc_head; g_nm_lo = (pdw)(g_fp.crc_tail ^ g_fp.size.lo);
        sv_base = g_tr.size; u32cp(&sv_prev, &g_tr.startxref);
        if (sv_base < 2 || !g_tr.root_num) return PDE_XREF;
        r = sv_order(); if (r) return r;
        r = pdw_src_open(g_sv.source); if (r) return r;
        sv_phase = PH_COPY;
    } else {
        sv_npages = g_sv.pages_count; sv_pgseg = g_sv.pages_seg;
        if (sv_npages < 1 || sv_npages > PD_MAXPAGES) return PDE_RANGE;
        g_nm_hi = pdm_crc16(0xFFFF, pdw_date, pw_len(pdw_date));
        g_nm_lo = pdm_crc16(g_nm_hi, &sv_cnt, 2);
        g_nm_lo = pdm_crc16(g_nm_lo, &sv_npages, 2);
        sv_base = (pdw)(7 + 2 * sv_npages);
        sv_phase = PH_HEAD;
    }
    r = sv_scan(); if (r) return r;
    if (!fresh) {
        sv_catver = (pdw)(sv_v15 ? 15 : (sv_v14 ? 14 : 0));
        if (sv_catver && g_tr.version >= sv_catver) sv_catver = 0;
        /* the estimate OSAPI_FS_OP is opened with: the source up to its last
           xref, plus a kilobyte and ~600 bytes an edit */
        {   static struct pd_u32 per; u32cp(&sv_total, &g_tr.startxref);
            pdm_u32_mul16(&per, sv_cnt, 600); pdm_u32_add(&sv_total, &sv_total, &per); pdm_u32_add16(&sv_total, 1024); }
    }
    sv_active = 1;
    return PDE_OK;
}

/* ---- the fresh document's three kinds of object --------------------------- */
static void nd_head(void)
{
    pb_s("%PDF-1.5\n%\xE2\xE3\xCF\xD3\n");
    pw_obj(1, 0); pb_s("<< /Type /Catalog /Pages 2 0 R >>\n"); pw_endobj();
    pw_obj(2, 0); pb_s("<< /Type /Pages /Kids ["); {
        pdw k; for (k = 0; k < sv_npages; k++) { pb_u((pdw)(7 + 2 * k)); pb_s(" 0 R "); }
    } pb_s("] /Count "); pb_u(sv_npages); pb_s(" >>\n"); pw_endobj();
    pw_obj(3, 0); pb_s("<< /Producer (PageDesk for os8088)");
    if (pdw_date[0]) { pb_s(" /CreationDate ("); pb_n(pdw_date, pw_len(pdw_date)); pb_c(')'); }
    pb_s(" >>\n"); pw_endobj();
    pw_obj(4, 0); pb_s("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>\n"); pw_endobj();
    pw_obj(5, 0); pb_s("<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>\n"); pw_endobj();
    pw_obj(6, 0); pb_s("<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>\n"); pw_endobj();
}
static struct pdw_newpage g_np;
static pdw nd_page(pdw k)
{
    pdw j, n, rot;
    pd_fcopy_in(sv_pgseg, (pdw)(k * sizeof g_np), &g_np, sizeof g_np);
    if (g_np.w < 16 || g_np.h < 16 || g_np.w > PD_MAXPT || g_np.h > PD_MAXPT) return PDE_RANGE;
    if (g_np.clen > 0xFFF0u) return PDE_RANGE;
    rot = g_np.rotate;
    if (rot != 0 && rot != 90 && rot != 180 && rot != 270) return PDE_RANGE;
    pw_obj((pdw)(7 + 2 * k), 0);
    pb_s("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "); pb_pt((pd_pt)g_np.w); pb_c(' '); pb_pt((pd_pt)g_np.h);
    pb_s("] /Resources << /ProcSet [/PDF /Text] /Font << /Helv 4 0 R /TiRo 5 0 R /Cour 6 0 R >> >> /Contents ");
    pb_u((pdw)(8 + 2 * k)); pb_s(" 0 R");
    if (rot) { pb_s(" /Rotate "); pb_u(rot); }
    n = 0;
    for (j = 0; j < sv_cnt; j++)
        if ((g_epg[j] & 0x7FFF) == k && g_onum[j]) {
            if (!n) { pb_s(" /Annots ["); n = 1; }
            pb_c(' '); pb_u(g_onum[j]); pb_s(" 0 R");
        }
    if (n) pb_s(" ]");
    pb_s(" >>\n"); pw_endobj();
    pw_obj((pdw)(8 + 2 * k), 0);
    pb_s("<< /Length "); pb_u(g_np.clen); pb_s(" >>\nstream\n");
    return PDE_OK;
}
/* the contents' bytes, half an output region a unit, so a large stream is
   several slices and not one long one */
static void nd_content(pdw k)
{
    pdw n;
    pd_fcopy_in(sv_pgseg, (pdw)(k * sizeof g_np), &g_np, sizeof g_np);
    n = (pdw)(g_np.clen - sv_coff);
    if (n > (pdw)(g_outcap / 2)) n = (pdw)(g_outcap / 2);
    pb_flush(); pw_far(g_np.cseg, sv_coff, n);
    sv_coff = (pdw)(sv_coff + n);
    if (sv_coff >= g_np.clen) { pb_s("\nendstream\n"); pw_endobj(); sv_sub = 2; }
}

/* ---- one slice ------------------------------------------------------------ */
static pdw job_slice(void)
{
    static pdw got;
    pdw units = 0, r, k, e, io0 = g_io;
    while (units < SV_UNITS && g_io == io0) {   /* one disk transfer ends a slice (15.2) */
        units++;
        switch (sv_phase) {
        case PH_COPY:                            /* read a chunk, then (next slice) write it */
            if (sv_copyw) { sv_copyw = 0; pw_flush(0); break; }
            k = (pdw)((g_outcap / g_clu) * g_clu);
            r = pdw_src_read(g_wseg, 0, k, &got); if (r) return r;
            if (got == 0) {                      /* the source is copied: new line */
                pb_c('\n'); sv_phase = PH_PAGES; sv_i = 0;
                break;
            }
            g_io++; sv_copyw = 1;
            g_wpos = got; pdm_u32_add16(&g_pos, got);
            break;
        case PH_PAGES:
            while (sv_i < sv_cnt && !pg_first(sv_i)) sv_i++;
            if (sv_i >= sv_cnt) { sv_phase = PH_CAT; break; }
            r = eng_pageinfo(g_epg[sv_i]); if (r) return r;
            r = pg_patch(g_epg[sv_i]); if (r) return r;
            sv_i++;
            break;
        case PH_CAT:
            if (sv_catver) { r = cat_patch(); if (r) return r; }
            sv_phase = PH_ANNOTS; sv_i = 0;
            break;
        case PH_ANNOTS:
            while (sv_i < sv_cnt && !g_onum[sv_i]) sv_i++;
            if (sv_i >= sv_cnt) { sv_phase = PH_XREF; sv_xi = 0; break; }
            r = an_run(); if (r) return r;
            break;
        case PH_HEAD:
            nd_head(); sv_phase = PH_NPAGES; sv_i = 0;
            break;
        case PH_NPAGES:
            if (sv_i >= sv_npages) { sv_phase = PH_ANNOTS; sv_i = 0; break; }
            if (sv_sub == 0) {                   /* the page object and the contents' head */
                r = nd_page(sv_i); if (r) return r;
                sv_coff = 0; sv_sub = g_np.clen ? 1 : 3;
            } else if (sv_sub == 1) nd_content(sv_i);
            if (sv_sub == 3) { pb_s("\nendstream\n"); pw_endobj(); sv_sub = 2; }
            if (sv_sub == 2) { sv_sub = 0; sv_i++; }
            break;
        case PH_XREF:
            if (!sv_xstart) {                    /* "xref", the first subsection */
                sv_xstart = 1;
                if (!g_tn) return PDE_RANGE;     /* nothing to save           */
                pb_flush();
                u32cp(&sv_xrefpos, &g_pos);
                pb_s("xref\n");
                if (sv_fresh) {
                    pb_s("0 "); pb_u((pdw)(g_tn + 1)); pb_s("\n0000000000 65535 f \n");
                }
                if (sv_fresh) { sv_xbad = 0; for (k = 0; k < g_tn; k++) if (tn_num(k) != (pdw)(k + 1)) sv_xbad = 1; }
                if (sv_xbad) return PDE_SYNTAX;
            }
            for (e = 0; e < 16 && sv_xi < g_tn; e++, sv_xi++) {
                if (!sv_fresh && (sv_xi == 0 || tn_num(sv_xi) != (pdw)(tn_num((pdw)(sv_xi - 1)) + 1))) {
                    for (k = sv_xi; (pdw)(k + 1) < g_tn && tn_num((pdw)(k + 1)) == (pdw)(tn_num(k) + 1); k++) ;
                    pb_u(tn_num(sv_xi)); pb_c(' '); pb_u((pdw)(k - sv_xi + 1)); pb_c('\n');
                }
                xr_entry(sv_xi);
            }
            if (sv_xi >= g_tn) sv_phase = PH_TRAILER;
            break;
        case PH_TRAILER:
            {   pdw size = sv_next;
                pb_s("trailer\n<< /Size ");
                if (!sv_fresh && g_tr.size > size) size = g_tr.size;
                pb_u(size);
                if (sv_fresh) pb_s(" /Root 1 0 R /Info 3 0 R");
                else {
                    pb_s(" /Root "); pb_u(g_tr.root_num); pb_c(' '); pb_u(g_tr.root_gen); pb_s(" R");
                    if (g_tr.info_num) { pb_s(" /Info "); pb_u(g_tr.info_num); pb_c(' '); pb_u(g_tr.info_gen); pb_s(" R"); }
                }
                id_make();
                pb_s(" /ID ");
                pb_c('[');
                if (!sv_fresh && g_tr.id0_len) {
                    pb_c('<'); for (k = 0; k < g_tr.id0_len && k < 16; k++) pb_hex2(g_tr.id0[k]); pb_c('>');
                } else pb_id();
                pb_id(); pb_c(']');
                if (!sv_fresh) { pb_s(" /Prev "); pb_u32(&sv_prev, 0); }
                pb_s(" >>\nstartxref\n"); pb_u32(&sv_xrefpos, 0); pb_s("\n%%EOF\n");
                pb_flush();
                sv_phase = PH_CLOSE;
            }
            break;
        case PH_CLOSE:                           /* the last bytes, then (next slice) the close */
            if (!sv_flushed) { sv_flushed = 1; pw_flush(1); if (g_err) return g_err; break; }
            r = pdw_io_close(g_tmp); if (r) return r;
            g_io++;
            sv_phase = PH_VERIFY;
            break;
        case PH_VERIFY:
            r = job_verify(); if (r) return r;
            if (sv_vstage == 2) sv_phase = PH_RENAME;
            break;
        case PH_RENAME:
            return PDE_OK;                      /* the caller finishes (rename) */
        default:
            return PDE_RANGE;
        }
        if (g_err) return g_err;
    }
    return PDE_BUSY;
}

/* 14.3: the verified temp file becomes the target. Returns PDE_EXISTS while the
   target is in the way and the caller has not said REPLACE. */
static pdw sv_tgone;                    /* the target was deleted (14.3)  */
static pdw job_rename(pdw replace)
{
    pdw r = pdw_io_rename(g_tmp, g_sv.target);
    sv_tgone = 0;
    if (r == PDE_EXISTS && replace) {
        r = pdw_io_delete(g_sv.target);
        if (r) return r;                    /* the target is untouched: an ordinary failure */
        /* a failure from here leaves the complete new file as PDSAVE.$$$ */
        sv_tgone = 1;
        r = pdw_io_rename(g_tmp, g_sv.target);
    }
    return r;
}
