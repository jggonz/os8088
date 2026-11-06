/* ============================================================================
 * os8088 - apps/pagedesk/pdovlnav.c   THE OUTLINE (PAGEDESK-SPEC 17.1, 20.10.4)
 *
 * Nav's overlay tenant, #included from pdovl.c: the sidebar's Outline tab.
 * The list is a flat array of entries in the THUMB claim (the Pages tab's
 * pictures and the Outline are never both on screen), in display order:
 * an expanded entry is followed by its children, one level deeper. Nothing
 * but the first level is read until an entry is expanded - a click on its
 * twisty, which the wake handler does (a disk read is not a callback's to
 * do under the lock) - and a collapsed entry gives its children back.
 *
 *   entry (42 bytes)   0 objnum  2 gen  4 first.num  6 first.gen
 *                      8 next.num 10 next.gen 12 dest page (0xFFFF = none)
 *                      14 depth (byte)  15 flags (byte, 1 = expanded)
 *                      16 title, 25 characters and a NUL
 * ==========================================================================*/

#define OL_SZ   42
#define OL_MAX  97
#define OL_SLACK 24                     /* children read at one expansion */

static unsigned ovl_ol_seg(void) { return pdu_dcseg(PDU_DC_THUMB); }
static unsigned ovl_ol_w(int i, int f) { return pd_fpeekw(ovl_ol_seg(), i * OL_SZ + f); }

/* one OUTLINE call into pdu_xout; 0 or a PDE_* */
static unsigned ovl_ol_fetch(unsigned num, unsigned gen)
{
    unsigned r;
    os88_memset(&pdu_xout, 0, sizeof(pdu_xout));
    pdu_xout.objnum = num;
    pdu_xout.gen = gen;
    pd_fcopy_out(&pdu_xout, pdu_xb, 0, sizeof(pdu_xout));
    r = pdu_mcall(PDU_M_ENG, PDV_ENG_OUTLINE, 0, 0, 0);
    if (pdu_mcf)
        return r ? r : PDE_LIMIT;
    pd_fcopy_in(pdu_xb, 0, &pdu_xout, sizeof(pdu_xout));
    return 0;
}

/* pdu_xout becomes entry i at `depth` */
static void ovl_ol_store(int i, int depth)
{
    unsigned s;
    int o;
    s = ovl_ol_seg();
    o = i * OL_SZ;
    pd_fpokew(s, o, pdu_xout.objnum);
    pd_fpokew(s, o + 2, pdu_xout.gen);
    pd_fpokew(s, o + 4, pdu_xout.first_num);
    pd_fpokew(s, o + 6, pdu_xout.first_gen);
    pd_fpokew(s, o + 8, pdu_xout.next_num);
    pd_fpokew(s, o + 10, pdu_xout.next_gen);
    pd_fpokew(s, o + 12, pdu_xout.dest_page);
    pd_fpokeb(s, o + 14, depth);
    pd_fpokeb(s, o + 15, 0);
    pdu_xout.title[24] = 0;
    pd_fcopy_out(pdu_xout.title, s, o + 16, 26);
}

/* entries [from, from + n) to `to`, overlap-safe */
static void ovl_ol_mv(int from, int to, int n)
{
    int k;
    if (n <= 0 || from == to)
        return;
    if (to > from)
        for (k = n - 1; k >= 0; k--)
            pd_fcopy(ovl_ol_seg(), (to + k) * OL_SZ, ovl_ol_seg(), (from + k) * OL_SZ, OL_SZ);
    else
        for (k = 0; k < n; k++)
            pd_fcopy(ovl_ol_seg(), (to + k) * OL_SZ, ovl_ol_seg(), (from + k) * OL_SZ, OL_SZ);
}

/* read the siblings that start at (num, gen) into entries at `at`, one level
 * `depth`; at most `max`. Returns how many. */
static int ovl_ol_siblings(unsigned num, unsigned gen, int at, int depth, int max)
{
    int n;
    n = 0;
    while (n < max && (num || n == 0)) {          /* (0, 0) is the root's first */
        if (ovl_ol_fetch(num, gen))
            break;
        ovl_ol_store(at + n, depth);
        n++;
        num = pdu_xout.next_num;
        gen = pdu_xout.next_gen;
    }
    return n;
}

/* the first level, or an expansion/collapse that is owed (the wake handler) */
static void ovl_ol_work(void)
{
    int i, j, d, n;
    unsigned fnum, fgen;

    if (!ovl_ol_seg())
        return;
    if (pdu_oln == 0) {
        n = ovl_ol_siblings(0, 0, 0, 0, OL_MAX - OL_SLACK);
        pdu_oln = n ? n : -1;
        pdu_oltop = 0;
    } else if (pdu_olxp) {
        i = pdu_olxp - 1;
        pdu_olxp = 0;
        if (i >= pdu_oln)
            return;
        d = pd_fpeekb(ovl_ol_seg(), i * OL_SZ + 14);
        if (pd_fpeekb(ovl_ol_seg(), i * OL_SZ + 15) & 1) {
            for (j = i + 1; j < pdu_oln && (int)pd_fpeekb(ovl_ol_seg(), j * OL_SZ + 14) > d; j++)
                ;
            ovl_ol_mv(j, i + 1, pdu_oln - j);
            pdu_oln -= j - i - 1;
            pd_fpokeb(ovl_ol_seg(), i * OL_SZ + 15, 0);
        } else {
            fnum = ovl_ol_w(i, 4);
            fgen = ovl_ol_w(i, 6);
            if (!fnum || pdu_oln + OL_SLACK > OL_MAX) {
                pdu_wsay(fnum ? "The outline is too long to open further."
                              : "This entry has no sub-entries.");
                return;
            }
            ovl_ol_mv(i + 1, i + 1 + OL_SLACK, pdu_oln - i - 1);
            n = ovl_ol_siblings(fnum, fgen, i + 1, d + 1, OL_SLACK);
            ovl_ol_mv(i + 1 + OL_SLACK, i + 1 + n, pdu_oln - i - 1);
            pdu_oln += n;
            pd_fpokeb(ovl_ol_seg(), i * OL_SZ + 15, 1);
        }
    }
    os88_gfx_lock();
    if (pdu_sbtab == 1)
        pdu_side_paint();
    os88_gfx_unlock();
}

/* entry i's row in the list rect o, when it is on screen. Lock held. */
static char ol_row[34];
static void ovl_ol_row(struct os88_rect *o, int i)
{
    int r, d, x, maxc, act;
    unsigned s;

    r = i - pdu_oltop;
    if (i < 0 || i >= pdu_oln || r < 0 || r >= (o->y2 - o->y1 - 4) / 12)
        return;
    s = ovl_ol_seg();
    d = pd_fpeekb(s, i * OL_SZ + 14);
    x = o->x1 + 4 + d * 8;
    maxc = (o->x2 - x - 2) / PDU_CW;
    if (maxc > 30)
        maxc = 30;
    if (maxc < 3)
        return;
    ol_row[0] = ovl_ol_w(i, 4) ? ((pd_fpeekb(s, i * OL_SZ + 15) & 1) ? '-' : '+') : ' ';
    ol_row[1] = ' ';
    pd_fcopy_in(s, i * OL_SZ + 16, ol_row + 2, 26);
    ol_row[maxc] = 0;
    act = (i == pdu_olsel);
    os88_font_run(x, o->y1 + 4 + r * 12, ol_row,
                  act ? OS88_WHITE : OS88_BLACK,
                  act ? pdu_cact : (pdu_mono ? OS88_WHITE : pdu_cbar));
}

/* the rows that fit in the list rect o, from pdu_oltop. Lock held. */
static void ovl_ol_paint(struct os88_rect *o)
{
    int r, rows;
    rows = (o->y2 - o->y1 - 4) / 12;
    for (r = 0; r < rows && pdu_oltop + r < pdu_oln; r++)
        ovl_ol_row(o, pdu_oltop + r);
}

/* a click in the list: the twisty toggles, the title goes to the page */
static void ovl_ol_click(int x, int y)
{
    struct os88_rect *o;
    int i, d, tx;
    unsigned dest;

    o = &pdu_tr;
    pdu_side_list(o);
    i = pdu_oltop + (y - o->y1 - 4) / 12;
    if (y < o->y1 + 4 || i < 0 || i >= pdu_oln)
        return;
    d = pd_fpeekb(ovl_ol_seg(), i * OL_SZ + 14);
    tx = o->x1 + 4 + d * 8;
    if (ovl_ol_w(i, 4) && x < tx + 2 * PDU_CW) {
        pdu_olxp = i + 1;
        pdu_kick();
        return;
    }
    dest = ovl_ol_w(i, 12);
    d = pdu_olsel;                      /* two rows change, not the panel */
    pdu_olsel = i;
    ovl_ol_row(o, d);
    ovl_ol_row(o, i);
    if (dest != 0xFFFF && dest < pdu_npages) {
        pdu_hjump = 1;
        pdu_goto(dest);
    }
    else
        pdu_tell("This entry does not go to a page in this file.");
}
