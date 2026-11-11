/* ============================================================================
 * os8088 - apps/pagedesk/pdovlpref.c   THE PREFERENCES DIALOG (20.18.2, 20.18.3)
 *
 * #included by pdovl.c, so every ovl_* here is cut into the overlay (2.2).
 * A popover of eight text rows: a title, the six settings and one line of
 * keys and when changes apply. Its paints run at the wake
 * (pdu_popown), never in the key's chain (t_pdstk).
 * ==========================================================================*/
#define PDP_NAME "PAGEDESK.CFG"
#define PDP_LEN  11                     /* version 2; version 1 is the first 10 */

static struct os88_place pdp_pl;
static struct os88_find  pdp_fd;

/* one directory of the walk: 1 = we stand in it now, 0 = absent, nothing moved */
static int ovl_pf_dive(const char *name)
{
    int ord, i;

    ord = 0;
    for (;;) {
        ord = os88_file_find(ord, &pdp_fd);
        if (ord < 0)
            return 0;
        if (pdp_fd.type != OS88_FT_DIR)
            continue;
        for (i = 0; i < 9; i++) {
            if (pdp_fd.name[i] != name[i])
                break;
            if (name[i] == 0)
                return os88_file_goto_q_mark(pdp_fd.clus, pdu_home.vol) == 0;
        }
    }
}

/* bank where we stand and go to SYSTEM/APPDATA: 1 = there, 0 = nothing moved
 * (SPEC.md 19.9: found by walking, never assumed). On the LAUNCH volume
 * (pdu_home), not the one we stand on: the open and the wake stand in the
 * DOCUMENT's folder (pdu_at_doc), which may be another disk */
static int ovl_pf_enter(void)
{
    os88_file_here(&pdp_pl);
    if (os88_file_goto_q_mark(0, pdu_home.vol) != 0)
        return 0;
    if (!ovl_pf_dive("SYSTEM") || !ovl_pf_dive("APPDATA")) {
        os88_file_goto_q_mark(pdp_pl.clus, pdp_pl.vol);
        return 0;
    }
    return 1;
}

static void ovl_pf_leave(void)
{
    os88_file_goto_q_mark(pdp_pl.clus, pdp_pl.vol);
}

/* the panel wishes, from the settings. 1 = the current layout's changed */
static int ovl_pf_panels(void)
{
    int k, old, c;

    old = pdu_want_side[pdu_L.kind] | (pdu_want_notes[pdu_L.kind] << 1);
    for (k = 0; k < 2; k++) {
        c = pdp_cfg[6 + k];
        pdu_want_side[k] = c & 1;
        pdu_want_notes[k] = (c >> 1) & 1;
    }
    return old != (pdu_want_side[pdu_L.kind] | (pdu_want_notes[pdu_L.kind] << 1));
}

/* read the file; a bad one changes nothing (20.18.1) */
static void ovl_pf_load(void)
{
    int i, n, ok;

    if (!ovl_pf_enter())
        return;
    i = os88_file_read(PDP_NAME, pdu_line, 16);
    ovl_pf_leave();
    n = i;                              /* 10: version 1, 11: version 2 (20.22.6) */
    if ((n != 10 && n != 11) || pdu_line[0] != 'P' || pdu_line[1] != 'D'
        || pdu_line[2] != 'C' || pdu_line[3] != 'F' || pdu_line[4] != n - 9)
        return;
    ok = 1;                             /* each setting in its range */
    for (i = 5; i < n; i++)
        if (pdu_line[i] < 0 || pdu_line[i] > (i == 5 ? 2 : i >= 9 ? 1 : 3))
            ok = 0;
    if (!ok)
        return;
    for (i = 5; i < n; i++)
        pdp_cfg[i] = pdu_line[i];
    ovl_pf_panels();
}

#define PDP_COLS 36

static const char *pdp_label[6] = { "Default view ", "Full panels  ",
                                    "Mid panels   ", "Mono marks   ",
                                    "Flate cache  ", "Module cache " };
static const char *pdp_val[6][4] = {
    { "Fit width", "Fit page", "100%", 0 },
    { "None", "Pages", "Notes", "Both" },
    { "None", "Pages", "Notes", "Both" },
    { "Line+frame", "Line", "Frame", "Hatch" },
    { "Auto", "Off", 0, 0 },
    { "Auto", "Off", 0, 0 }
};
static const char pdp_cnt[6] = { 3, 4, 4, 4, 2, 2 };

/* one row of exactly PDP_COLS cells: the ground comes with the glyphs */
static void ovl_pf_row(int y, const char *a, const char *b, int sel)
{
    struct os88_rect *p;
    int n;

    p = &pdu_poprect;
    os88_strcpy(pdu_line, a, sizeof(pdu_line));
    if (b)
        pdu_cat(pdu_line, b, sizeof(pdu_line));
    n = 0;
    while (pdu_line[n])
        n++;
    while (n < PDP_COLS)
        pdu_line[n++] = ' ';
    pdu_line[n] = 0;
    os88_font_run(p->x1 + 8, y, pdu_line, sel ? OS88_WHITE : OS88_BLACK,
                  sel ? pdu_cact : OS88_WHITE);
}

static void ovl_pf_paint(void)
{
    struct os88_rect *p;
    int k, y;

    if (pdu_au_mode) {                  /* the password prompt (20.22.6) */
        ovl_au_pnt();
        return;
    }
    p = &pdu_poprect;
    pdu_fill(p->x1, p->y1, p->x2, p->y2, OS88_WHITE);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(p->x1, p->y1, p->x2, p->y2);
    os88_gfx_frame(p->x1 + 1, p->y1 + 1, p->x2 - 1, p->y2 - 1);
    y = p->y1 + 6;
    ovl_pf_row(y, "Preferences", 0, 0);
    for (k = 0; k < 6; k++)
        ovl_pf_row(y + (k + 1) * PDU_POP_ROW, pdp_label[k],
                   pdp_val[k][(int)pdp_cfg[5 + k]], k == pdp_row);
    ovl_pf_row(y + 7 * PDU_POP_ROW, "Arrows Esc  View, caches: next open", 0, 0);
}

static void ovl_pf_owe(void)
{
    pdu_popown = 1;                     /* the wake paints it */
    pdu_kick();
}

static void ovl_pf_step(int dir)
{
    int v;

    v = pdp_cfg[5 + pdp_row] + dir;
    if (v < 0)
        v = pdp_cnt[pdp_row] - 1;
    if (v >= pdp_cnt[pdp_row])
        v = 0;
    pdp_cfg[5 + pdp_row] = v;
    pdp_chg = pdp_chg | 1;
    if (pdp_row == 1 || pdp_row == 2)
        pdp_chg = pdp_chg | 2;
    if (pdp_row == 3)
        pdp_chg = pdp_chg | 4;
    ovl_pf_owe();
}

static void ovl_pf_move(int dir)
{
    pdp_row = (pdp_row + dir + 6) % 6;
    ovl_pf_owe();
}

static void ovl_pf_key(int ascii, int scan)
{
    if (pdu_au_mode) {
        ovl_au_ky(ascii);
        return;
    }
    if (ascii == 13 || ascii == ' ')
        ovl_pf_step(1);
    else if (ascii == 9)
        ovl_pf_move(1);
    else if (ascii)
        return;
    else if (scan == K_UP || scan == K_TAB)
        ovl_pf_move(-1);
    else if (scan == K_DOWN)
        ovl_pf_move(1);
    else if (scan == K_LEFT)
        ovl_pf_step(-1);
    else if (scan == K_RIGHT)
        ovl_pf_step(1);
}

static void ovl_pf_click(int x, int y)
{
    int k;

    k = (y - pdu_poprect.y1 - 6) / PDU_POP_ROW - 1;
    if (k < 0 || k > 5 || pdu_au_mode)
        return;
    pdp_row = k;
    ovl_pf_step(1);
}

/* the wake pays the save (20.18.3): the session keeps the choices either way.
 * The reason is the kernel's (os88_ferr, read before the walk back clears
 * it): a write refused, or the launch volume itself unreachable (no disk);
 * only a walk that found no SYSTEM/APPDATA - which goes back and so leaves
 * no error - is "no APPDATA folder" */
static void ovl_pf_save(void)
{
    int e, ok;

    ok = 0;
    if (ovl_pf_enter()) {
        ok = os88_file_write(PDP_NAME, pdp_cfg, PDP_LEN) == 0;
        e = os88_ferr();
        ovl_pf_leave();
    } else
        e = os88_ferr();
    if (ok) {
        pdu_wsay("Preferences saved.");
        return;
    }
    os88_strcpy(pdu_line, "Not saved: ", sizeof(pdu_line));
    if (e)
        os88_ferr_text(e, pdu_line + 11, 40);
    else
        pdu_cat(pdu_line, "no APPDATA folder", sizeof(pdu_line));
    pdu_cat(pdu_line, ". Session only.", sizeof(pdu_line));
    pdu_wsay(pdu_line);
}

/* the wake: the debts the dialog's close left. File I/O is legal here */
static void ovl_pf_wake(void)
{
    int o;

    o = pdp_owe;
    pdp_owe = 0;
    os88_gfx_lock();
    if ((o & 2) && ovl_pf_panels())
        pdu_reflow();
    if ((o & 4) && pdu_doc == D_OPEN)
        pdu_canvas_full();
    os88_gfx_unlock();
    ovl_pf_save();
}
