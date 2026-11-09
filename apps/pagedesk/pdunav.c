/* ============================================================================
 * os8088 - apps/pagedesk/pdunav.c   NAVIGATION, GESTURES, LINKS
 *                                   (PAGEDESK-SPEC 9.1, 9.4, 17.5; 20.10)
 *
 *   history   Back / Forward over the pages a JUMP left (Alt+Left, Alt+Right,
 *             Document menu): a link, the outline, Go to, Home/End, a Find
 *             hit. Prev/Next and PgUp/PgDn step by one and are not history.
 *   gestures  the C SDK has no W_ONDRAG callback (the kernel's tracking
 *             edge, OSAPI_WM_ONDRAG, has no crt0 thunk), so a press that needs
 *             one (the scroll bar's thumb, a text selection) arms the
 *             window's one-shot timer and POLLS os88_mouse() every tick until
 *             the button is up. Nothing is drawn by an idle window.
 *             While the button is down no page render runs (the rubber band
 *             is XORed on the glass, and a band redrawn under it would leave
 *             the second XOR as garbage).
 *   links     a page's /Link annotations, fetched when it loads into the
 *             SCRATCH claim; a click that hits one goes to its page. PageDesk
 *             never runs an action: a /URI is shown on the status line, a
 *             /Launch or JavaScript action is named and refused (1.4).
 *   work      the idle ladder: when no job runs and no page is owed, the
 *             next piece of nav's work starts, in the order the user waits
 *             for it.
 * ==========================================================================*/

/* ---- history ---------------------------------------------------------------- */
#define PDU_HIST 8
static unsigned pdu_hb[PDU_HIST], pdu_hf[PDU_HIST];
static int pdu_hbn, pdu_hfn, pdu_hnav;

static void ovl_hpush(unsigned *s, int *n, unsigned v)
{
    int i;
    if (*n && s[*n - 1] == v)
        return;
    if (*n == PDU_HIST) {
        for (i = 1; i < PDU_HIST; i++)
            s[i - 1] = s[i];
        (*n)--;
    }
    s[(*n)++] = v;
}

/* pdu_goto(n) is about to leave the current page: remember it if n is a jump */
static void pdu_nav_noted(int n)
{
    int d;
    if (pdu_hnav)
        return;
    d = n - (int)pdu_page;
    if (d >= -1 && d <= 1 && !pdu_hjump)
        return;
    pdu_hjump = 0;
    ovl_hpush(pdu_hb, &pdu_hbn, pdu_page);
    pdu_hfn = 0;
}

static void ovl_back(void)
{
    if (pdu_hbn == 0) {
        pdu_tell("There is no page to go back to.");
        return;
    }
    ovl_hpush(pdu_hf, &pdu_hfn, pdu_page);
    pdu_hnav = 1;
    pdu_goto(pdu_hb[--pdu_hbn]);
    pdu_hnav = 0;
}

static void ovl_forward(void)
{
    if (pdu_hfn == 0) {
        pdu_tell("There is no page to go forward to.");
        return;
    }
    ovl_hpush(pdu_hb, &pdu_hbn, pdu_page);
    pdu_hnav = 1;
    pdu_goto(pdu_hf[--pdu_hfn]);
    pdu_hnav = 0;
}

/* ---- links -------------------------------------------------------------------- */
/* pdxb_annot's offsets (pdabi.h): rect 20, nm 28, state 58, contents_len 60,
 * contents 62 - read out of the exchange block, no copy of the record kept */
#define PDA_O_SUB  4
#define PDA_O_RECT 20
#define PDA_O_NM   28
#define PDA_O_ST   58
#define PDA_O_CLEN 60
#define PDA_O_CONT 62

static int pdu_lkn;                     /* links of this page in scratch */
static int pdu_lkact;                   /* a link's action to report: idx + 1 */

static void ovl_lk_load(void)
{
    int k, n, o, w;
    unsigned s;

    pdu_lkn = 0;
    pdu_lkact = 0;
    s = pdu_dcseg(PDU_DC_SCRATCH);
    if (!(pdu_pinfo.flags & PDP_HASLINKS) || !s)
        return;
    n = pdu_pinfo.nannots;
    if (n > 64)
        n = 64;
    for (k = 0; k < n && pdu_lkn < PDU_SC_LMAX; k++) {
        pdu_mcall(PDU_M_ENG, PDV_ENG_ANNOTS, pdu_page, k, 0);
        if (pdu_mcf)
            break;
        if (pd_fpeekb(pdu_xb, PDA_O_SUB) != 'L' || pd_fpeekb(pdu_xb, PDA_O_SUB + 1) != 'i'
            || pd_fpeekb(pdu_xb, PDA_O_SUB + 2) != 'n' || pd_fpeekb(pdu_xb, PDA_O_SUB + 3) != 'k')
            continue;
        o = PDU_SC_LINKS + pdu_lkn * 12;
        for (w = 0; w < 4; w++)
            pd_fpokew(s, o + w * 2, pd_fpeekw(pdu_xb, PDA_O_RECT + w * 2));
        pd_fpokew(s, o + 8, pd_fpeekw(pdu_xb, PDA_O_ST));
        pd_fpokew(s, o + 10, k);
        pdu_lkn++;
    }
}

/* did a press at (x, y) land on a link? Then go there, or say why not. */
static pd_pt pdu_lkx, pdu_lky;
static int ovl_lk_press(int x, int y)
{
    int i, o;
    unsigned s, d;

    if (!pdu_lkn)
        return 0;
    s = pdu_dcseg(PDU_DC_SCRATCH);
    pdm_view_inv(&pdu_lkx, &pdu_lky, &pdu_view, x, y);
    for (i = 0; i < pdu_lkn; i++) {
        o = PDU_SC_LINKS + i * 12;
        if (pdu_lkx >= (pd_pt)pd_fpeekw(s, o) && pdu_lky >= (pd_pt)pd_fpeekw(s, o + 2)
            && pdu_lkx <= (pd_pt)pd_fpeekw(s, o + 4) && pdu_lky <= (pd_pt)pd_fpeekw(s, o + 6)) {
            d = pd_fpeekw(s, o + 8);
            if (d && d <= pdu_npages) {
                os88_utoa(d, pdu_num);
                os88_strcpy(pdu_line, "Link to page ", sizeof(pdu_line));
                pdu_cat(pdu_line, pdu_num, sizeof(pdu_line));
                pdu_tell(pdu_line);
                pdu_hjump = 1;
                pdu_goto(d - 1);
            } else {
                pdu_lkact = pd_fpeekw(s, o + 10) + 1;
                pdu_kick();
            }
            return 1;
        }
    }
    return 0;
}

/* the wake handler: what a link that goes nowhere in this file says */
static void ovl_lk_say(void)
{
    int k;
    unsigned r;

    k = pdu_lkact - 1;
    pdu_lkact = 0;
    r = pdu_mcall(PDU_M_ENG, PDV_ENG_ANNOTS, pdu_page, k, 0);
    if (pdu_mcf) {
        pdu_wsay(pdu_errtext(r));
        return;
    }
    if (pd_fpeekb(pdu_xb, PDA_O_NM) == 'U' && pd_fpeekw(pdu_xb, PDA_O_CLEN)) {
        os88_strcpy(pdu_line, "Link: ", sizeof(pdu_line));
        pd_fcopy_in(pdu_xb, PDA_O_CONT, pdu_line + 6, 44);
        pdu_line[50] = 0;
        pdu_cat(pdu_line, " - PageDesk never opens links.", sizeof(pdu_line));
    } else if (pd_fpeekb(pdu_xb, PDA_O_NM) == 'N')
        os88_strcpy(pdu_line, "This link names a destination PageDesk cannot find.", sizeof(pdu_line));
    else if (pd_fpeekb(pdu_xb, PDA_O_NM) == 'G')
        os88_strcpy(pdu_line, "This link goes to a page that is not in the file.", sizeof(pdu_line));
    else if (pd_fpeekb(pdu_xb, PDA_O_NM))
        os88_strcpy(pdu_line, "This link runs an action; PageDesk never runs one.", sizeof(pdu_line));
    else
        os88_strcpy(pdu_line, "This link has no destination.", sizeof(pdu_line));
    pdu_wsay(pdu_line);
}

/* ---- the gestures ---------------------------------------------------------------- */
static int pdu_gx, pdu_gy, pdu_gmx, pdu_gmy, pdu_gmoved, pdu_gband, pdu_gdbl;
static int pdu_gcx, pdu_gcy;
static unsigned pdu_gt;

static void pdu_arm(void)
{
    os88_wm_timer(pdu_win, 1);
}

static void ovl_band(int x0, int y0, int x1, int y1)
{
    int t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (x0 < pdu_L.canvas.x1) x0 = pdu_L.canvas.x1;
    if (y0 < pdu_L.canvas.y1) y0 = pdu_L.canvas.y1;
    if (x1 > pdu_L.canvas.x2) x1 = pdu_L.canvas.x2;
    if (y1 > pdu_L.canvas.y2) y1 = pdu_L.canvas.y2;
    if (x0 <= x1 && y0 <= y1)
        os88_gfx_xor_rect(x0, y0, x1, y1);
}

static int pdu_abs(int v) { return v < 0 ? -v : v; }

/* the scroll bar's thumb was pressed (pdu_sbhit said so) */
static int pdu_nav_thumb(int y)
{
    if (!pdu_sbgrab(pdu_sb, y))
        return 0;
    pdu_gs = 1;
    pdu_arm();
    return 1;
}

/* a press on the page with the Select tool: a link, else a selection starts */
static int pdu_nav_press(int x, int y)
{
    unsigned t;

    if (pdu_job == J_FIND || pdu_job == J_SAVE || pdu_job == J_OPEN) {
        pdu_tell("Busy - Esc cancels.");
        return 1;
    }
    if (pdu_job == J_TEXT || pdu_job == J_THUMB)
        pdu_job_abort();
    if (pdu_lkn && pdu_tool == C_T_SELECT && ovl_lk_press(x, y))
        return 1;
    t = os88_ticks();
    pdu_gdbl = (unsigned)(t - pdu_gt) < 8 && pdu_abs(x - pdu_gcx) < 5
               && pdu_abs(y - pdu_gcy) < 5;
    pdu_gt = pdu_gdbl ? t - 100 : t;
    pdu_gcx = x;
    pdu_gcy = y;
    ovl_sel_clear();
    pdu_job_abort_render();
    pdu_gs = 2;
    pdu_gx = x;
    pdu_gy = y;
    pdu_gmoved = 0;
    pdu_gband = 0;
    pdu_arm();
    return 1;
}

static void ovl_gmove(int x, int y)
{
    int p;
    if (pdu_gs == 1) {
        p = pdu_sbtrack(pdu_sb, y);
        if (p >= 0)
            pdu_scroll_to(pdu_sx, p);
        return;
    }
    if (!pdu_gmoved && pdu_abs(x - pdu_gx) < 4 && pdu_abs(y - pdu_gy) < 4)
        return;
    pdu_gmoved = 1;
    if (os88_wm_clip_set(pdu_win) != 0)
        return;
    if (pdu_gband)
        ovl_band(pdu_gx, pdu_gy, pdu_gmx, pdu_gmy);
    ovl_band(pdu_gx, pdu_gy, x, y);
    pdu_gband = 1;
    pdu_gmx = x;
    pdu_gmy = y;
}

static void ovl_gend(int x, int y)
{
    int p, g;
    g = pdu_gs;
    pdu_gs = 0;
    if (g == 1) {
        p = pdu_sbdrop(pdu_sb);
        if (p >= 0)
            pdu_scroll_to(pdu_sx, p);
    } else if (g == 2) {
        if (pdu_gband && os88_wm_clip_set(pdu_win) == 0)
            ovl_band(pdu_gx, pdu_gy, pdu_gmx, pdu_gmy);
        pdu_gband = 0;
        if (pdu_gmoved || pdu_abs(x - pdu_gx) >= 4 || pdu_abs(y - pdu_gy) >= 4)
            ovl_sel_points(pdu_gx, pdu_gy, x, y, 0);   /* a flick the poll missed */
        else if (pdu_gdbl)
            ovl_sel_points(x, y, x, y, 1);
    }
    pdu_kick();                         /* the render the gesture held back */
}

/* polish (20.19.5): Esc while a press is being tracked (the Select tool's
 * rubber band and the scroll thumb poll from os88_ontimer, so a key reaches
 * os88_onkey meanwhile). The band comes off, nothing is selected, and the
 * button's release finds pdu_gs = 0 and does nothing. */
static void ovl_gcancel(void)
{
    int g;
    g = pdu_gs;
    pdu_gs = 0;
    if (g == 1) {                       /* the thumb goes back to the view: */
        pdu_sbdrop(pdu_sb);             /* the hand may have moved it further */
        g = pdu_sb[6];                  /* than the view went (os88ui's rate) */
        pdu_sb_fill();
        pdu_sbmove(pdu_sb, g);
    } else if (pdu_gband && os88_wm_clip_set(pdu_win) == 0)
        ovl_band(pdu_gx, pdu_gy, pdu_gmx, pdu_gmy);
    pdu_gband = 0;
    pdu_kick();                         /* the render the gesture held back */
}

void os88_onmouseup(int x, int y, void *win)
{
    if (pdu_gs)
        ovl_gend(x, y);
}

void os88_ontimer(void *win)
{
    static struct os88_mouse m;
    if (!pdu_gs)
        return;
    os88_mouse(&m);
    if (!(m.btn & 1)) {                 /* the release was lost: it is over */
        ovl_gend(m.x, m.y);
        return;
    }
    ovl_gmove(m.x, m.y);
    pdu_arm();
}

static void pdu_nav_reset(void)
{
    pdu_hbn = pdu_hfn = 0;
    pdu_lkn = 0;
    pdu_lkact = 0;
    pdu_gs = 0;
    pdu_txwant = 0;
    pdu_fwant = 0;
    pdu_sel_drop();
    pdu_th_reset();
}

/* ---- page change and the idle ladder ---------------------------------------------- */
/* a page was just loaded (the wake handler, no lock): what follows it */
static void pdu_nav_page(void)
{
    if (pdu_selpg != (int)pdu_page)
        pdu_sel_drop();
    else if (pdu_selq > 0)
        pdu_selvis = 1;                 /* the render's hook XORs it back */
    pdu_lkn = 0;
    pdu_lkact = 0;
    if (pdu_pinfo.flags & PDP_HASLINKS)
        ovl_lk_load();
}

/* anything nav still owes (the wake handler keeps going while it is true) */
static int pdu_nav_owed(void)
{
    if (pdu_doc != D_OPEN || pdu_gs)
        return 0;
    /* a selection owed to the glass under a popover waits for the popover's
     * repair render: counting it here re-posted the wake on every wake while
     * the popover stayed open - a window spinning with nothing to do */
    return pdu_txwant || pdu_fwant || pdu_lkact
           || (pdu_selshow && pdu_pop == P_NONE && !pdu_about)
           || (pdu_selq < 0 && pdu_selpg >= 0)
           || (pdu_sbtab == 0 && pdu_thwant)
           || (pdu_sbtab == 1 && (pdu_olxp || (pdu_oln == 0 && (pdu_xopen.flags & PDO_OUTLINE))));
}

/* the idle ladder (job NONE, no page owed): the next piece of work */
static void ovl_nav_work(void)
{
    int pg;
    if (pdu_doc != D_OPEN || pdu_gs)
        return;
    if (pdu_txwant) {
        ovl_txwant_start();
        return;
    }
    if (pdu_fwant) {
        ovl_find_begin();
        return;
    }
    if (pdu_selq < 0 && pdu_selpg == (int)pdu_page) {
        ovl_sel_quads_owed();
        return;
    }
    if (pdu_selshow)
        ovl_sel_show();
    if (pdu_lkact) {
        ovl_lk_say();
        return;
    }
    if (pdu_sbtab == 1) {
        if (pdu_olxp || pdu_oln == 0)
            ovl_ol_work();
        return;
    }
    pg = ovl_th_pick();
    if (pg >= 0)
        ovl_th_begin(pg);
    else
        pdu_thwant = 0;
}

/* a job of nav's kind is running: one slice */
static int pdu_job_nav(void)
{
    if (pdu_job == J_FIND)
        ovl_find_step();
    else if (pdu_job == J_TEXT)
        ovl_tx_step();
    else if (pdu_job == J_THUMB)
        ovl_th_step();
    else
        return 0;
    return 1;
}
