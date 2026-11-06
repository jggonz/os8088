/* ============================================================================
 * os8088 - apps/pagedesk/pdovlpage.c   PAGE OPERATIONS: THE COMMANDS AND
 *                                      THEIR PROMPTS (PAGEDESK-SPEC 20.11)
 *
 * #included by pdovl.c, so every ovl_* here is cut into the overlay (2.2). A
 * page operation is a DELTA of four words {op, a, b, entry} applied to the
 * page map (pdwmap.h) and revertible from itself - which is what makes it a
 * command the undo history can hold (13.6):
 *
 *   1 ROT    page a turns b quarters clockwise (undo: 4 - b)
 *   2 DEL    page a, whose entry was `entry`, leaves the list
 *   3 INS    `entry` becomes page a (a duplicate, a blank)
 *   4 MOVE   page a goes to position b
 *
 * The prompts (extract list, destination, delete, New's size, replace?) live
 * in the status line: no dialog window, so CGA does everything the other
 * adapters do (PD-18).
 * ==========================================================================*/

static unsigned pdu_pgd[4];             /* the delta being made */

/* view page v's map entry, with or without a map */
static unsigned ovl_pg_entry(unsigned v)
{
    return pdu_pgmap ? pdu_pgw(v) : PDM_MK(PDM_SRC, 0, v);
}

/* the page a command is for: the current one, or the Pages panel's selection */
static unsigned ovl_pg_tgt(void)
{
    int s;
    s = pdu_side_sel();                 /* nav: the Pages panel's cell */
    return (s >= 0 && s < (int)pdu_npages) ? (unsigned)s : pdu_page;
}

/* the map exists (identity until something changes it) */
static int ovl_pg_ready(void)
{
    unsigned i, seg;
    if (pdu_pgmap)
        return 1;
    seg = pdu_dcseg(PDU_DC_PAGES);
    if (pdu_doc != D_OPEN || !seg)
        return 0;
    if (pdu_npages > PDM_MAXMAP) {
        pdu_tell("Page operations handle up to 240 pages.");
        return 0;
    }
    for (i = 0; i < pdu_npages; i++)
        pd_fpokew(seg, (unsigned)(2 * i), PDM_MK(PDM_SRC, 0, i));
    pdu_pgmap = 1;
    return 1;
}


static void ovl_pg_ins(unsigned at, unsigned e)
{
    unsigned k, seg;
    seg = pdu_dcseg(PDU_DC_PAGES);
    for (k = pdu_npages; k > at; k--)
        pd_fpokew(seg, (unsigned)(2 * k), pd_fpeekw(seg, (unsigned)(2 * (k - 1))));
    pd_fpokew(seg, (unsigned)(2 * at), e);
    pdu_npages++;
}

static unsigned ovl_pg_del(unsigned at)
{
    unsigned k, e, seg;
    seg = pdu_dcseg(PDU_DC_PAGES);
    e = pdu_pgw(at);
    for (k = at; k + 1 < pdu_npages; k++)
        pd_fpokew(seg, (unsigned)(2 * k), pd_fpeekw(seg, (unsigned)(2 * (k + 1))));
    pdu_npages--;
    return e;
}


/* apply (undo = 0) or revert (undo = 1) a delta (20.11.1) */
static int ovl_pg_apply(const unsigned *d, int undo)
{
    unsigned op, a, b, e, e2, n, k;
    if (!ovl_pg_ready())
        return 0;
    op = d[0]; a = d[1]; b = d[2]; e = d[3];
    n = a;
    if (op == 1) {
        e2 = pdu_pgw(a);
        b = undo ? (4 - b) & 3 : b;
        pd_fpokew(pdu_dcseg(PDU_DC_PAGES), (unsigned)(2 * a),
                  PDM_MK(PDM_KIND(e2), (PDM_ROT(e2) + b) & 3, PDM_IDX(e2)));
    } else if (op == 2 || op == 3) {
        if ((op == 2) != (undo != 0)) {                 /* remove page a */
            ovl_pg_del(a);
            n = a < pdu_npages ? a : pdu_npages - 1;
        } else
            ovl_pg_ins(a, e);
    } else if (op == 5) {                               /* b pages of another file, entries e, e+1.. (20.16) */
        if (undo) {
            for (k = 0; k < b; k++)
                ovl_pg_del(a);
            n = a < pdu_npages ? a : pdu_npages - 1;
        } else
            for (k = 0; k < b; k++)
                ovl_pg_ins(a + k, e + k);
    } else if (op == 4) {
        if (undo) { e = ovl_pg_del(b); ovl_pg_ins(a, e); }
        else { e = ovl_pg_del(a); ovl_pg_ins(b, e); n = b; }
    }
    /* back to the source's own order: nothing to write */
    if (pdu_npages == pdu_srcn) {
        for (e = 0; e < pdu_npages && pdu_pgw(e) == PDM_MK(PDM_SRC, 0, e); e++)
            ;
        if (e == pdu_npages)
            pdu_pgmap = 0;
    }
    /* show view page n: the sidebar now, the page and the rest at the next
     * wake (PAGEINFO is a disk read) */
    pdu_job_abort_render();
    pdu_rwant = 0;
    pdu_page = n;
    pdu_changes = 1;
    pdu_title_set();                    /* the "*" */
    pdu_pgwant = n + 1;
    if (n < (unsigned)pdu_sidetop || n >= (unsigned)pdu_sidetop + 2 * pdu_side_rows())
        pdu_sidetop = n - n % 2;
    pdu_side_paint();
    pdu_menusync();
    pdu_kick();
    return 1;
}

static void ovl_pg_do(unsigned op, unsigned a, unsigned b, unsigned e)
{
    if (pdu_pgow_on) {                  /* one change at a time: the next
                                         * command reads the map it makes */
        pdu_tell("Still applying the last page change.");
        return;
    }
    pdu_pgd[0] = op; pdu_pgd[1] = a; pdu_pgd[2] = b; pdu_pgd[3] = e;
    /* applied at the wake, as undo/redo are (pdu_pg_apply): the apply
     * repaints the Pages panel, and that chain does not fit on a key's
     * UI stack (D21, t_pdstk). Mode 2 = a new change, which edit's undo
     * records when it applies (20.8, 20.11.1) */
    os88_memcpy(pdu_pgow, pdu_pgd, sizeof(pdu_pgow));
    pdu_pgow_undo = 2;
    pdu_pgow_on = 1;
    pdu_kick();
}

/* the prompt line: "<what> <typed>_" */
static void ovl_pg_ask(const char *what)
{
    os88_strcpy(pdu_line, what, sizeof(pdu_line));
    pdu_cat(pdu_line, pdu_pkbuf, sizeof(pdu_line));
    pdu_cat(pdu_line, "_", sizeof(pdu_line));
    pdu_tell(pdu_line);
}

static void ovl_pg_prompt(int kind, const char *what, int seed)
{
    pdu_pk = kind;
    pdu_pklen = 0;
    pdu_pkbuf[0] = 0;
    if (seed) {
        os88_utoa(pdu_page + 1, pdu_pkbuf);
        pdu_pklen = os88_strlen(pdu_pkbuf);
    }
    ovl_pg_ask(what);
}


/* ---- Insert from PDF (20.16): the file, then its pages ---- */
static char pdu_insq[56];               /* the prompt, with the file's name in it */
static int pdu_inslot;                  /* the slot the chosen file has in the row */

/* the slot a file takes: the one that already names it, else the lowest no
 * map entry uses; -1 = three files are in use */
static int ovl_ins_slot(const char *name)
{
    unsigned seg, i, e, used, k, same;
    char c;
    seg = pdu_dcseg(PDU_DC_PAGES);
    used = 0;
    for (i = 0; i < pdu_npages; i++) {
        e = pdu_pgw(i);
        if (PDM_KIND(e) == PDM_FOREIGN)
            used |= 1u << ((PDM_IDX(e) >> 10) & 3);
    }
    for (k = 0; k < PDM_NSLOT; k++) {
        if (!(used & (1u << k)))
            continue;
        same = pd_fpeekw(seg, 1002 + 4 * k) == pdu_tgtplace.clus
               && pd_fpeekw(seg, 1004 + 4 * k) == pdu_tgtplace.vol + 1u;
        for (i = 0; same && i < 14; i++) {
            c = pd_fpeekb(seg, PDM_NAMES + 14 * k + i);
            if (c != name[i])
                same = 0;
            if (!c)
                break;
        }
        if (same)
            return k;
    }
    for (k = 0; k < PDM_NSLOT; k++)
        if (!(used & (1u << k)))
            return k;
    return -1;
}

/* the dialog named the file; the instance stands in its folder */
static int ovl_ins_file(const char *name)
{
    unsigned seg;
    int k;
    if (!ovl_pg_ready())
        return 1;
    os88_file_here(&pdu_tgtplace);
    k = ovl_ins_slot(name);
    if (k < 0) {
        pdu_tell("Three files at most.");
        return 1;
    }
    seg = pdu_dcseg(PDU_DC_PAGES);
    pd_fcopy_out(name, seg, PDM_NAMES + 14 * k, os88_strlen(name) + 1);
    pd_fpokew(seg, 1002 + 4 * k, pdu_tgtplace.clus);
    pd_fpokew(seg, 1004 + 4 * k, pdu_tgtplace.vol + 1u);
    pdu_inslot = k;
    os88_strcpy(pdu_insq, "Insert pages of ", sizeof(pdu_insq));
    pdu_cat(pdu_insq, name, sizeof(pdu_insq));
    pdu_cat(pdu_insq, " (3 or 2-4): ", sizeof(pdu_insq));
    ovl_pg_prompt(PK_INSERT, pdu_insq, 0);
    return 1;
}

/* Enter on the page range: one page operation, so one Undo */
static void ovl_ins_go(void)
{
    unsigned i, lo, hi, n, e, v;
    lo = hi = 0;
    i = 0;
    while (pdu_pkbuf[i] >= '0' && pdu_pkbuf[i] <= '9')
        lo = lo * 10 + (pdu_pkbuf[i++] - '0');
    hi = lo;
    if (pdu_pkbuf[i] == '-') {
        hi = 0;
        i++;
        while (pdu_pkbuf[i] >= '0' && pdu_pkbuf[i] <= '9')
            hi = hi * 10 + (pdu_pkbuf[i++] - '0');
    }
    if (pdu_pkbuf[i] || lo < 1 || hi < lo || hi > PD_MAXPAGES) {
        pdu_tell("Use a page like 3 or 2-4.");
        return;
    }
    n = hi - lo + 1;
    if (pdu_npages + n > PDM_MAXMAP) {
        pdu_tell("Page operations handle up to 240 pages.");
        return;
    }
    for (v = 0; v < pdu_npages; v++) {
        e = pdu_pgw(v);
        if (PDM_KIND(e) == PDM_FOREIGN && (int)((PDM_IDX(e) >> 10) & 3) == pdu_inslot
            && (PDM_IDX(e) & 1023u) + 1 >= lo && (PDM_IDX(e) & 1023u) < hi) {
            pdu_tell("Those pages are already in.");
            return;
        }
    }
    pdu_pk = PK_NONE;
    ovl_pg_do(5, ovl_pg_tgt() + 1, n, PDM_MK(PDM_FOREIGN, 0, PDM_FORE(pdu_inslot, lo - 1)));
    pdu_tell("Inserted; Save As draws them.");
}

static int ovl_pg_cmd(int id)
{
    unsigned t, e;
    if (id == PGC_NEW) {
        pdu_pk = PK_NEWSIZE;
        pdu_tell("New page: 1 Letter, 2 A4, 3 Letter wide, 4 A4 wide");
        return 1;
    }
    if (id == PGC_INSERT) {             /* 20.16: the file first, then which of its pages */
        pdu_pgfd = 3;
        if (os88_file_dlg(OS88_FDLG_OPEN, pdu_win, 0) != 0)
            pdu_tell("The file dialog is already open.");
        return 1;
    }
    if (id == C_P_EXTRACT) {
        pdu_pgfd = 0;
        ovl_pg_prompt(PK_EXTRACT, "Extract pages (e.g. 1,3-5): ", 1);
        return 1;
    }
    if (!ovl_pg_ready())
        return 0;
    t = ovl_pg_tgt();
    switch (id) {
    case C_P_ROTL:
        ovl_pg_do(1, t, 3, 0);
        break;
    case C_P_ROTR:
        ovl_pg_do(1, t, 1, 0);
        break;
    case C_P_DELETE:
        pdu_pk = PK_DELETE;
        os88_utoa(t + 1, pdu_num);
        os88_strcpy(pdu_line, "Delete page ", sizeof(pdu_line));
        pdu_cat(pdu_line, pdu_num, sizeof(pdu_line));
        pdu_cat(pdu_line, "? Earlier bytes stay in the file. Enter confirms", sizeof(pdu_line));
        pdu_tell(pdu_line);
        break;
    case PGC_DUP:
        ovl_pg_do(3, t + 1, 0, pdu_pgw(t));
        break;
    case C_P_BLANK:
        e = PDM_MK(PDM_BLANK, 0, PDM_IDX(pdu_pgw(t)));
        ovl_pg_do(3, t + 1, 0, e);
        break;
    case PGC_UP:
        ovl_pg_do(4, t, t - 1, 0);
        break;
    case PGC_DOWN:
        ovl_pg_do(4, t, t + 1, 0);
        break;
    case PGC_MOVE:
        ovl_pg_prompt(PK_MOVE, "Move page to position: ", 0);
        break;
    }
    return 1;
}

/* "1,3-5" in pdu_pkbuf into the Extract list; the count, 0 for nothing, -1
 * for a list that is not one, -2 for more than PDM_MAXMAP pages */
static int ovl_pg_list(void)
{
    unsigned i, n, lo, hi, v, k, e, seg;
    int cnt, dup;
    seg = pdu_dcseg(PDU_DC_PAGES) + PDM_XLIST / 16;
    pdu_xtn = 0;
    cnt = 0;
    i = 0;
    while (pdu_pkbuf[i]) {
        n = 0;
        if (pdu_pkbuf[i] < '0' || pdu_pkbuf[i] > '9')
            return -1;
        while (pdu_pkbuf[i] >= '0' && pdu_pkbuf[i] <= '9')
            n = n * 10 + (pdu_pkbuf[i++] - '0');
        lo = hi = n;
        if (pdu_pkbuf[i] == '-') {
            i++;
            n = 0;
            if (pdu_pkbuf[i] < '0' || pdu_pkbuf[i] > '9')
                return -1;
            while (pdu_pkbuf[i] >= '0' && pdu_pkbuf[i] <= '9')
                n = n * 10 + (pdu_pkbuf[i++] - '0');
            hi = n;
        }
        if (pdu_pkbuf[i] == ',')
            i++;
        else if (pdu_pkbuf[i])
            return -1;
        if (lo < 1 || hi < lo || hi > pdu_npages)
            return -1;
        for (v = lo - 1; v < hi; v++) {
            e = ovl_pg_entry(v);
            if (PDM_KIND(e) == PDM_FOREIGN)     /* the extract pipeline reads one document */
                return -3;
            dup = 0;
            for (k = 0; k < pdu_xtn; k++)       /* a source page twice would be two page objects */
                if (PDM_KIND(e) == PDM_SRC && pd_fpeekw(seg, (unsigned)(2 * k)) == e)
                    dup = 1;
            if (dup)
                continue;
            if (pdu_xtn >= PDM_MAXMAP)          /* the row's half (pdwmap.h): refuse, never truncate */
                return -2;
            pd_fpokew(seg, (unsigned)(2 * pdu_xtn), e);
            pdu_xtn++;
            cnt++;
        }
    }
    return cnt;
}

static const unsigned pdu_pgsize[4][2] = { { 9792, 12672 }, { 9520, 13472 },
                                            { 12672, 9792 }, { 13472, 9520 } };

/* a key while a prompt is up; 1 = consumed */
static int ovl_pg_key(int ascii, int scan)
{
    int n;
    unsigned v;
    if (ascii == 27) {
        n = pdu_pk;
        pdu_pk = PK_NONE;
        if (n == PK_REPLACE) {          /* keep the target: the temp file goes (14.3) */
            /* as an Escape on a running save: the wake handler aborts the
             * writer, says so and gives PDENG its document back. Not here -
             * a key arrives with the gfx lock held, and saying it takes the
             * lock again (the first build deadlocked in gfx_lock) */
            pdu_job_start(J_SAVE);
            pdu_jcancel = 1;
        } else
            pdu_tell("Cancelled.");
        return 1;
    }
    if (pdu_pk == PK_REPLACE) {         /* Enter: the writer deletes the target and renames */
        if (ascii == 13) {
            pdu_pk = PK_NONE;
            pdu_xsave.state = PDW_ST_REPLACE;
            pdu_job_start(J_SAVE);
        }
        return 1;
    }
    if (pdu_pk == PK_DELETE) {
        if (ascii == 13 || (ascii == 0 && scan == 0x53)) {
            pdu_pk = PK_NONE;
            ovl_pg_do(2, ovl_pg_tgt(), 0, pdu_pgw(ovl_pg_tgt()));
            pdu_tell("Page deleted.");
        }
        return 1;
    }
    if (pdu_pk == PK_NEWSIZE) {
        if (ascii >= '1' && ascii <= '4') {
            v = ascii - '1';
            pdu_newp[0] = pdu_pgsize[v][0];
            pdu_newp[1] = pdu_pgsize[v][1];
            pdu_newp[2] = 0;
            pdu_newp[3] = 0;
            pdu_newp[4] = 0;
            pdu_pk = PK_NONE;
            pdu_pgfd = 1;
            if (os88_file_dlg(OS88_FDLG_SAVE, pdu_win, "UNTITLED.PDF") != 0)
                pdu_tell("The file dialog is already open.");
        }
        return 1;
    }
    if (ascii == 8) {
        if (pdu_pklen > 0)
            pdu_pkbuf[--pdu_pklen] = 0;
    } else if (ascii == 13) {
        if (pdu_pk == PK_EXTRACT) {
            n = ovl_pg_list();
            if (n < 0) {
                pdu_tell(n == -1 ? "Use a list like 1,3-5." : n == -3 ? "Save first, then extract." : "Page operations handle up to 240 pages.");
                return 1;
            }
            if (n == 0)
                return 1;
            pdu_pk = PK_NONE;
            pdu_pgfd = 2;
            if (os88_file_dlg(OS88_FDLG_SAVE, pdu_win, "EXTRACT.PDF") != 0)
                pdu_tell("The file dialog is already open.");
            return 1;
        }
        if (pdu_pk == PK_INSERT) {
            ovl_ins_go();
            return 1;
        }
        n = 0;                          /* PK_MOVE: a position */
        for (v = 0; v < (unsigned)pdu_pklen; v++)
            n = n * 10 + (pdu_pkbuf[v] - '0');
        if (n < 1 || n > (int)pdu_npages) {
            pdu_tell("There is no such page.");
            return 1;
        }
        pdu_pk = PK_NONE;
        v = ovl_pg_tgt();
        if ((unsigned)(n - 1) != v)
            ovl_pg_do(4, v, n - 1, 0);
        return 1;
    } else if (((ascii >= '0' && ascii <= '9') || (pdu_pk == PK_EXTRACT && ascii == ',')
                || (pdu_pk != PK_MOVE && ascii == '-'))
               && pdu_pklen < 20) {
        pdu_pkbuf[pdu_pklen++] = ascii;
        pdu_pkbuf[pdu_pklen] = 0;
    }
    ovl_pg_ask(pdu_pk == PK_EXTRACT ? "Extract pages (e.g. 1,3-5): " : pdu_pk == PK_INSERT ? pdu_insq : "Move page to position: ");
    return 1;
}

/* the Standard File dialog answered for an Extract (2) or a New (1) */
static int ovl_pg_onfile(const char *name)
{
    int kind, i, same;
    kind = pdu_pgfd;
    pdu_pgfd = 0;
    if (kind == 3)
        return ovl_ins_file(name);
    if (kind == 2) {                    /* never over the document being read */
        os88_file_here(&pdu_tgtplace);
        same = pdu_tgtplace.clus == pdu_docplace.clus && pdu_tgtplace.vol == pdu_docplace.vol;
        for (i = 0; name[i] || pdu_name[i]; i++)
            if (name[i] != pdu_name[i])
                same = 0;
        if (same) {
            pdu_tell("Save As writes a new file: choose another name.");
            pdu_at_doc();
            return 1;
        }
    }
    ovl_save_begin(name, kind);
    return 1;
}

/* ---- the writer jobs: start and end (20.11.5) ---- */


/* PAGEDESK-SPEC 20.11.5: the three jobs the writer runs for the primary.
 * kind 0 Save As (14.2), 1 New (14.6), 2 Extract (13.8). The writer reads
 * the SOURCE in its own folder (D14) and writes here, so a job stands in the
 * TARGET's folder: the dialog left the instance there. */
static int ovl_save_begin(const char *name, int kind)
{
    int r;

    pdu_job_abort_render();
    if (kind != 1 && pdu_doc != D_OPEN)
        return 1;
    os88_strcpy(pdu_target, name, sizeof(pdu_target));
    os88_file_here(&pdu_tgtplace);
    if (kind == 0) {                    /* 14.4: the file on the disk is the one we opened */
        pdu_at_doc();
        pdu_mcall(PDU_M_ENG, PDV_ENG_FINGERPRINT, 0, 0, 0);
        if (!pdu_mcf) {
            /* pdxb_fingerprint's size, the first two words */
            if (pd_fpeekw(pdu_xb, 0) != pdu_xopen.size.lo
                || pd_fpeekw(pdu_xb, 2) != pdu_xopen.size.hi) {
                os88_file_goto_q_mark(pdu_tgtplace.clus, pdu_tgtplace.vol);
                pdu_say2("Could not save ", pdu_errtext(PDE_CHANGED));
                pdu_status_paint();
                return 1;
            }
        }
        os88_file_goto_q_mark(pdu_tgtplace.clus, pdu_tgtplace.vol);
    }
    pdu_wkb = PDU_WBUF_KB;
    if (kind == 0 && pdu_pgmap)         /* 20.16: a foreign window carves its bit maps from a big claim */
        for (r = 0; r < (int)pdu_npages; r++)
            if (pdu_pg_kind(r) == PDM_FOREIGN)
                pdu_wkb = 40;
    if (kind == 2) {                    /* 20.11.4: Extract's buffer follows the source's /Size */
        pdu_mcall(PDU_M_ENG, PDV_ENG_TRAILER, 0, 0, 0);
        r = pd_fpeekw(pdu_xb, 0);       /* pdxb_trailer's /Size, its first word */
        pdu_wkb = pdu_mcf ? 40 : 16 + ((unsigned)r >> 7) + ((unsigned)r >> 11);
        if (pdu_wkb > 40)
            pdu_wkb = 40;
    }
    r = pdu_wrt_in_now();
    if (r) {
        pdu_say2("Could not save ", pdu_errtext(r));
        pdu_status_paint();
        return 1;
    }
    for (r = 0; r < (int)sizeof(pdu_xsave); r++)
        ((char *)&pdu_xsave)[r] = 0;
    os88_strcpy(pdu_xsave.target, name, sizeof(pdu_xsave.target));
    /* D9: what an incremental save copies, and the stamp it writes. The
     * source is the open document's 8.3 name; author stays "" (no author
     * preference in wave 1). */
    os88_strcpy(pdu_xsave.source, pdu_name, sizeof(pdu_xsave.source));
    pdu_datestamp(pdu_xsave.date);
    pdu_xsave.mode = pdu_savemode[kind];
    pdu_savekind = kind;
    if (kind == 0) {                    /* edit (13.1.1): the user's marks */
        pdu_xsave.edits_seg = pdu_ed_seg();
        pdu_xsave.edits_count = pdu_ed_count();
    }
    /* the job's pages (20.11.1) and the source's folder (D14), named only
     * when it is not the one we write into */
    if (kind == 1) {
        pdu_xsave.pages_seg = pdu_xb + 32;         /* pd_newpage, 512 bytes into the XB */
        pdu_xsave.pages_count = 1;
    } else {
        if (kind == 2) {
            pdu_xsave.pages_seg = pdu_dcseg(PDU_DC_PAGES) + PDM_XLIST / 16;   /* the Extract list */
            pdu_xsave.pages_count = pdu_xtn;
        } else if (pdu_pgmap) {
            pdu_xsave.pages_seg = pdu_dcseg(PDU_DC_PAGES);
            pdu_xsave.pages_count = pdu_npages;
        }
        if (pdu_tgtplace.clus != pdu_docplace.clus || pdu_tgtplace.vol != pdu_docplace.vol) {
            pdu_xsave.src_clus = pdu_docplace.clus;
            pdu_xsave.src_vol = pdu_docplace.vol + 1;   /* 20.11.3: volume + 1, 0 = one folder */
        }
    }
    pdu_say2(kind == 1 ? "Creating " : kind == 2 ? "Extracting to " : "Saving ", name);
    pdu_status_paint();
    pdu_job_start(J_SAVE);
    return 1;
}

/* the job ended: free the writer, say how it went, and give PDENG back a
 * document (20.11.5) - the writer closed it to read the output through */
static int ovl_save_done(unsigned err)
{
    int reopen, r;
    unsigned err_d;
    static char err_n[14];
    reopen = (pdu_xsave.mode & PDW_OUT_REOPEN) != 0;
    pdu_wrt_out();                      /* 16.2: the writer is freed after */
    if (err) {
        os88_strcpy(pdu_line, "Could not save ", sizeof(pdu_line));
        pdu_cat(pdu_line, pdu_target, sizeof(pdu_line));
        pdu_cat(pdu_line, ": ", sizeof(pdu_line));
        err_d = pdu_xsave.err_detail;
        if (err != PDE_NOMEM && err_d >= 20 && err_d < 20 + PDM_NSLOT) {     /* 20.16: the foreign file at fault */
            for (r = 0; r < 13; r++) {
                err_n[r] = pd_fpeekb(pdu_dcseg(PDU_DC_PAGES), PDM_NAMES + 14 * (err_d - 20) + r);
                if (!err_n[r])
                    break;
            }
            err_n[r] = 0;
            pdu_cat(pdu_line, err_n, sizeof(pdu_line));
            pdu_cat(pdu_line, ": ", sizeof(pdu_line));
        }
        pdu_cat(pdu_line, err == PDE_RANGE && err_d == 17 ? "page past the file's end."
                : err == PDE_LIMIT && err_d == 18 ? "form fields or scripts."
                : pdu_errtext(err), sizeof(pdu_line));
        pdu_wsay(pdu_line);
        if (reopen)
            pdu_reopen_begin();
        return 1;
    }
    if (pdu_savekind == 2) {            /* the source stays; PDENG goes back to it */
        os88_strcpy(pdu_line, "Extracted to ", sizeof(pdu_line));
        pdu_cat(pdu_line, pdu_target, sizeof(pdu_line));
        pdu_wsay(pdu_line);
        if (reopen)
            pdu_reopen_begin();
        return 1;
    }
    pdu_changes = 0;
    pdu_saved();                        /* edit: the save point */
    pdu_pgsaved = 1;                    /* "Saved <target>", once it is open */
    pdu_resume = pdu_savekind == 0 ? pdu_page + 1 : 0;
    pdu_open_begin(pdu_target);         /* the output IS the document now */
    return 1;
}

