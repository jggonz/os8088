/* ============================================================================
 * os8088 - apps/pagedesk/pdovlauth.c   THE PASSWORD PROMPT: PAINT AND KEYS
 *                                      (PAGEDESK-SPEC 20.22.6)
 *
 * #included by pdovl.c, so every ovl_* here is cut into the overlay. The
 * state is pduauth.c's. Painted at the wake (pdu_popown), never in the key's
 * chain (t_pdstk).
 * ==========================================================================*/
static const char pdu_au_hint[] = "Enter: open  Esc: cancel";

/* pdu_line, padded to AU_COLS - 2 cells, drawn as row k */
static void ovl_au_put(int k)
{
    int n;

    n = 0;
    while (pdu_line[n])
        n++;
    while (n < AU_COLS - 2)
        pdu_line[n++] = ' ';
    pdu_line[n] = 0;
    os88_font_run(pdu_poprect.x1 + 8, pdu_poprect.y1 + 6 + k * PDU_POP_ROW,
                  pdu_line, OS88_BLACK, OS88_WHITE);
}

static void ovl_au_row(int k, const char *a)
{
    pdu_line[0] = 0;
    if (a)
        os88_strcpy(pdu_line, a, sizeof(pdu_line));
    ovl_au_put(k);
}

static void ovl_au_pnt(void)
{
    struct os88_rect *p;
    int m, n;

    p = &pdu_poprect;
    pdu_fill(p->x1, p->y1, p->x2, p->y2, OS88_WHITE);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(p->x1, p->y1, p->x2, p->y2);
    os88_gfx_frame(p->x1 + 1, p->y1 + 1, p->x2 - 1, p->y2 - 1);
    m = pdu_au_mode;
    if (m == AU_R6) {
        ovl_au_row(0, "This file's encryption takes");
        os88_strcpy(pdu_line, "about ", sizeof(pdu_line));
        os88_utoa(pdu_au_val ? pdu_au_val : 1, pdu_num);
        pdu_cat(pdu_line, pdu_num, sizeof(pdu_line));
        pdu_cat(pdu_line, " minutes on this machine.", sizeof(pdu_line));
        ovl_au_put(1);
        ovl_au_row(2, "Open anyway?");
    } else if (m == AU_CHECK) {
        os88_strcpy(pdu_line, "Checking...  ", sizeof(pdu_line));
        os88_utoa(pdu_au_val / 10, pdu_num);
        pdu_cat(pdu_line, pdu_num, sizeof(pdu_line));
        pdu_cat(pdu_line, "%", sizeof(pdu_line));
        ovl_au_put(0);
        ovl_au_row(1, 0);
        ovl_au_row(2, 0);
    } else {
        os88_strcpy(pdu_line, "Password: ", sizeof(pdu_line));
        for (n = 0; n < pdu_au_len && n < 22; n++)
            pdu_line[10 + n] = '*';
        pdu_line[10 + n] = '_';
        pdu_line[11 + n] = 0;
        ovl_au_put(0);
        if (pdu_pfl && pdu_pfk < PDM_NSLOT) {   /* the pre-flight's file is not the one being saved: say which (F13) */
            os88_strcpy(pdu_line, "For ", sizeof(pdu_line));
            pdu_cat(pdu_line, pdu_xopen.name, sizeof(pdu_line));
            ovl_au_put(1);
        } else
            ovl_au_row(1, 0);
        ovl_au_row(2, m == AU_PWBAD ? "Wrong password." : 0);
    }
    ovl_au_row(3, m == AU_CHECK ? pdu_au_hint + 13 : pdu_au_hint);
}

static void ovl_au_wipe(void)
{
    while (pdu_au_len > 0)
        pdu_au_pw[--pdu_au_len] = 0;
}

static void ovl_au_ky(int ascii)
{
    int m, ok;

    m = pdu_au_mode;
    if (ascii == 27 || (m == AU_R6 && (ascii == 'n' || ascii == 'N'))) {
        ovl_au_cancel();
        pdu_pop_close_soon();
        return;
    }
    if (m == AU_CHECK)
        return;
    ok = 0;
    if (m == AU_R6) {
        ok = ascii == 13 || ascii == 'y' || ascii == 'Y';
    } else if (ascii == 13)
        ok = pdu_au_len > 0;
    else if (ascii == 8 && pdu_au_len)
        pdu_au_pw[--pdu_au_len] = 0;
    else if (ascii >= 32 && ascii < 127 && pdu_au_len < 127) {
        pdu_au_pw[pdu_au_len++] = ascii;
        pdu_au_pw[pdu_au_len] = 0;
    } else
        return;
    if (m == AU_R6 && !ok)
        return;
    if (ok) {
        pdu_au_ans = 1;
        pdu_au_mode = AU_CHECK;
        pdu_au_val = 0;
    }
    pdu_popown = 1;
    pdu_kick();
}

/* cancelled by some other road (pdu_pop_shut's hook, or N): a leaf, so the
 * shut path does not reach the code that opens popovers (D21) */
static void ovl_au_cancel(void)
{
    ovl_au_wipe();
    pdu_au_mode = 0;
    pdu_au_ans = -1;
}

/* show the popover in this mode, or repaint it in place */
static int ovl_au_shw(int mode, int val)
{
    int was;

    was = pdu_au_mode;                  /* already up: repaint in place */
    if (!was && pdu_pop != P_NONE)
        pdu_pop_close();                /* before the mode is set: not a cancel */
    pdu_au_mode = mode;
    pdu_au_ans = 0;
    pdu_au_val = val;
    if (was) {
        pdu_popown = 1;
        pdu_kick();
        return 1;
    }
    pdu_pop_open(P_PREFS, C_NONE);      /* its second face (20.22.6) */
    if (pdu_pop != P_PREFS)
        pdu_au_mode = 0;
    return pdu_pop == P_PREFS;
}

/* THE ONE ENTRY (one thunk in the primary): op 1..4 is the mode to show
 * (AU_PW, AU_PWBAD, AU_R6, AU_CHECK; val is the minutes), 5 the progress in
 * permille, 6 the close. The paint and the
 * keys are Preferences' own entries, which hand over while pdu_au_mode is set. A refused fetch answers 0 */
static int ovl_au_do(int op, int val)
{
    if (op < 5) {
        if (op < 3)
            ovl_au_wipe();
        return ovl_au_shw(op, val);
    }
    if (op == 5) {
        pdu_au_val = val;
        if (pdu_au_mode) {
            pdu_popown = 1;
            pdu_kick();
        }
    } else if (op == 6) {
        ovl_au_wipe();
        pdu_au_mode = 0;
        if (pdu_pop == P_PREFS)
            pdu_pop_close_soon();
    }
    return 1;
}
