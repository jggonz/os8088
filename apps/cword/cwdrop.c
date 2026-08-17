/* ============================================================================
 * os8088 - apps/cword/cwdrop.c        the dropdown menus
 *
 * #included by cword.c - one translation unit (SPEC.md 70.1).
 *
 * Both period gestures, because Word 1.1a supported both and a user of either
 * habit finds the other one broken: PRESS-DRAG-RELEASE, where the button going
 * down opens the menu, moving highlights an item and letting go chooses it;
 * and CLICK-OPEN ("sticky"), where a click opens the menu and it stays open
 * until a second click chooses, Esc closes, or the arrows and Enter walk it.
 * Alt+mnemonic opens one from the keyboard and a letter fires an item.
 *
 * ONE GEOMETRY, BANKED. cw_menu_geom() computes the panel's rectangle once
 * into cw_m_x1..cw_m_y2, and the painter, the hit test, the highlight and the
 * close repaint all read those four words. This is the fm_hit discipline of
 * SPEC.md 22, and the reason for it is that a hit test which recomputes the
 * layout is a second implementation of it: the two drift, and the symptom is a
 * menu that highlights one item and fires another.
 *
 * CLOSING REPAINTS EXACTLY WHAT THE PANEL COVERED, which on this machine is
 * the whole reason this file is not four lines long. A full repaint is over a
 * second on the target 8088 (PERFORMANCE.md); the strips the panel overlapped
 * plus the text rows it overlapped are a tenth of that, and the rows are found
 * from the panel's own rectangle rather than assumed.
 * ==========================================================================*/

#define CW_MI_H    10                   /* an item's height */
#define CW_MI_SEP   5                   /* ...and a separator's */

/* Is this item's check mark showing? Only CWF_CHK items are asked. */
static int cw_item_chk(int it)
{
    int act;

    act = cw_it[it].act;
    if (act == CWA_DRAFT)
        return !cw_page;
    if (act == CWA_PAGE)
        return cw_page;
    if (act == CWA_VRIB)
        return cw_vrib;
    if (act == CWA_VRUL)
        return cw_vrul;
    if (act == CWA_VSTA)
        return cw_vsta;
    if (act == CWA_WIN1)
        return 1;                       /* the one document, and it is current */
    return 0;
}

/* Greyed for a reason that is TRUE RIGHT NOW, as against the permanent
 * greying CWF_DIS records for things this platform cannot do at all. Both end
 * up in the same pen; only this one changes while the program runs. */
static int cw_item_dis(int it)
{
    int act;

    act = cw_it[it].act;
    if (cw_it[it].flag & CWF_DIS)
        return 1;
    if (act == CWA_SAVE)
        return !cw_mod;
    if (act == CWA_CUT || act == CWA_COPY)
        return !cw_sel_on;
    if (act == CWA_PASTE)
        return os88_clip_size() <= 0;
    if (act == CWA_UNDO)
        return 1;                       /* no undo yet: greyed as a FACT about
                                         * this build, not as decoration */
    return 0;
}

/* The label, which is the table's except for the Window menu's item 1: that
 * one carries the live document name, exactly as it does in the product. */
static const char *cw_item_label(int it)
{
    if (cw_it[it].act == CWA_WIN1)
        return cw_wname;
    return cw_it[it].label;
}

/* cw_menu_geom - the open panel's rectangle, into the four banked words.
 * Clamped onto the content box: a menu near the right edge slides left rather
 * than drawing over the window frame, and a menu taller than the window is
 * cut at the bottom by the fill it draws (its items below that simply do not
 * appear, which is what the real product did on a short screen). */
static void cw_menu_geom(int m)
{
    int i;
    int h;
    int n;
    int first;

    first = cw_mn_first[m];
    n = cw_mn_count[m];
    h = 4;
    for (i = 0; i < n; i++)
        h = h + ((cw_it[first + i].flag & CWF_SEP) ? CW_MI_SEP : CW_MI_H);

    cw_m_x1 = cw_bx + cw_mn_cell[m] * 8 - 4;
    cw_m_y1 = cw_by + CW_H_BAR;
    cw_m_x2 = cw_m_x1 + cw_mn_width[m];
    cw_m_y2 = cw_m_y1 + h;

    if (cw_m_x2 > cw_org.x + cw_sz.w - 2) {
        cw_m_x1 = cw_org.x + cw_sz.w - 2 - cw_mn_width[m];
        cw_m_x2 = cw_m_x1 + cw_mn_width[m];
    }
    if (cw_m_x1 < cw_org.x)
        cw_m_x1 = cw_org.x;
    if (cw_m_y2 > cw_org.y + cw_sz.h - 1)
        cw_m_y2 = cw_org.y + cw_sz.h - 1;
}

/* The top of item k of the open menu, in screen pixels. The painter and the
 * hit test both walk this, which is why it is a function. */
static int cw_menu_item_y(int k)
{
    int i;
    int y;
    int first;

    first = cw_mn_first[cw_m_open];
    y = cw_m_y1 + 2;
    for (i = 0; i < k; i++)
        y = y + ((cw_it[first + i].flag & CWF_SEP) ? CW_MI_SEP : CW_MI_H);
    return y;
}

static void cw_menu_paint(void)
{
    int i;
    int y;
    int n;
    int first;
    int it;
    int dis;
    const char *cap;

    first = cw_mn_first[cw_m_open];
    n = cw_mn_count[cw_m_open];

    os88_set_color(OS88_WHITE);
    os88_gfx_fill(cw_m_x1, cw_m_y1, cw_m_x2, cw_m_y2);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(cw_m_x1, cw_m_y1, cw_m_x2, cw_m_y2);

    for (i = 0; i < n; i++) {
        it = first + i;
        y = cw_menu_item_y(i);
        if (y + 8 > cw_m_y2)
            break;
        if (cw_it[it].flag & CWF_SEP) {
            os88_set_color(OS88_BLACK);
            os88_gfx_hline(cw_m_x1 + 2, cw_m_x2 - 2, y + 2);
            continue;
        }
        dis = cw_item_dis(it);
        if (dis) {
            /* THE WHOLE ITEM in the disabled pen, not just the caption
             * (SPEC.md 47): on the two 1bpp adapters the colour alone rounds
             * to solid black and only the pen's flag dithers it. */
            os88_gfx_pen(1);
            os88_font_str(cw_m_x1 + 18, y, cw_item_label(it));
        } else {
            os88_font_run(cw_m_x1 + 18, y, cw_item_label(it),
                          OS88_BLACK, OS88_WHITE);
        }
        cap = cw_it[it].cap;
        if (cap != 0) {
            n = (int)os88_strlen(cap);
            if (!dis)
                os88_set_color(OS88_BLACK);
            os88_font_str(cw_m_x2 - 6 - n * 8, y, cap);
            n = cw_mn_count[cw_m_open];
        }
        if (dis)
            os88_gfx_pen(0);
        if ((cw_it[it].flag & CWF_CHK) && cw_item_chk(it)) {
            /* THE CHECK IS DRAWN, BECAUSE THIS MACHINE HAS NO GLYPH FOR ONE.
             * This asked os88_font_char() for 0xFB, the IBM ROM's check - and
             * the kernel's face is characters 32..126 (SPEC.md 6), so the call
             * drew NOTHING and no menu item in this program has ever carried a
             * check. It is the same defect cw_pilcrow() exists for, in a second
             * place, and it hid because every checkable item here was one whose
             * state the user could see elsewhere: the ribbon lit, the strip was
             * on the screen or it was not. View > Draft / Page is the first pair
             * where the check IS the answer (SPEC.md 70.12.1), which is what
             * turned it up.
             *
             * Two strokes, which is what apps/word draws - a short down-stroke
             * and a long up-stroke - and two calls per checked item, never one
             * per pixel. */
            os88_set_color(OS88_BLACK);
            os88_gfx_line(cw_m_x1 + 2, y + 5, cw_m_x1 + 3, y + 7, 0);
            os88_gfx_line(cw_m_x1 + 3, y + 7, cw_m_x1 + 7, y + 3, 0);
        }
        /* the mnemonic, underlined where menus.cmd put the '&' */
        if (!dis) {
            os88_set_color(OS88_BLACK);
            os88_gfx_hline(cw_m_x1 + 18 + cw_it[it].mn * 8,
                           cw_m_x1 + 25 + cw_it[it].mn * 8, y + 8);
        }
    }
}

/* The item under a point, or -1. Separators and anything past the cut bottom
 * answer -1, which is what makes dragging over one leave the previous
 * highlight alone rather than flickering. */
static int cw_menu_hit(int mx, int my)
{
    int i;
    int y;
    int n;
    int first;

    if (cw_m_open < 0)
        return -1;
    if (mx < cw_m_x1 || mx > cw_m_x2 || my < cw_m_y1 || my > cw_m_y2)
        return -1;
    first = cw_mn_first[cw_m_open];
    n = cw_mn_count[cw_m_open];
    for (i = 0; i < n; i++) {
        if (cw_it[first + i].flag & CWF_SEP)
            continue;
        y = cw_menu_item_y(i);
        if (y + 8 > cw_m_y2)
            break;
        if (my >= y && my < y + CW_MI_H)
            return i;
    }
    return -1;
}

/* The XOR highlight, which is its own inverse - so putting it on and taking it
 * off are one routine and cannot disagree about where it was. */
static void cw_menu_hl(int k)
{
    int y;

    if (k < 0 || cw_m_open < 0)
        return;
    y = cw_menu_item_y(k);
    if (y + 8 > cw_m_y2)
        return;
    os88_gfx_xor_fill(cw_m_x1 + 1, y - 1, cw_m_x2 - 1, y + 8);
}

static void cw_menu_move(int k)
{
    if (k == cw_m_item)
        return;
    cw_menu_hl(cw_m_item);
    cw_m_item = k;
    cw_menu_hl(cw_m_item);
}

/* Which bar title a point is over, or -1. */
static int cw_bar_hit(int mx, int my)
{
    int i;
    int x;

    if (my < cw_by || my >= cw_by + CW_H_BAR)
        return -1;
    for (i = 0; i < CW_NMENU; i++) {
        x = cw_bx + cw_mn_cell[i] * 8;
        if (mx >= x - 4 && mx < x + cw_mn_cells[i] * 8 + 4)
            return i;
    }
    return -1;
}

static void cw_menu_open(int m)
{
    cw_caret_off();
    cw_m_open = m;
    cw_m_item = -1;
    cw_menu_geom(m);
    cw_menu_paint();
    /* the bar title inverts while its menu is down, which is how the product
     * showed which one was open */
    os88_gfx_xor_fill(cw_bx + cw_mn_cell[m] * 8 - 4, cw_by + 1,
                      cw_bx + (cw_mn_cell[m] + cw_mn_cells[m]) * 8 + 3,
                      cw_by + CW_H_BAR - 2);
}

/* cw_menu_close - take the panel off the glass by repainting what it covered,
 * and nothing else. The rows are worked out from the panel's own rectangle. */
static void cw_menu_close(void *win)
{
    int r;
    int r0;
    int r1;
    int i;
    int base;
    int y;

    (void)win;
    if (cw_m_open < 0)
        return;

    os88_gfx_xor_fill(cw_bx + cw_mn_cell[cw_m_open] * 8 - 4, cw_by + 1,
                      cw_bx + (cw_mn_cell[cw_m_open] + cw_mn_cells[cw_m_open])
                          * 8 + 3, cw_by + CW_H_BAR - 2);
    cw_m_open = -1;
    cw_m_item = -1;

    os88_set_color(OS88_WHITE);
    os88_gfx_fill(cw_m_x1, cw_m_y1, cw_m_x2, cw_m_y2);

    if (cw_m_y1 < cw_chrome_top) {
        cw_ribbon();
        cw_ruler();
    }
    cw_sheet();                         /* the panel stood over the margins the
                                         * sheet's rules are drawn in, and the
                                         * fill above has just whitened them */

    /* the text rows the panel's rectangle touched */
    r0 = (cw_m_y1 - cw_ty) / cw_pitch;
    r1 = (cw_m_y2 - cw_ty) / cw_pitch;
    if (r0 < 0)
        r0 = 0;
    if (r1 >= cw_rows)
        r1 = cw_rows - 1;
    for (r = r0; r <= r1; r++) {
        base = r * CW_SH_STRIDE;
        for (i = 0; i < cw_cols; i++) {
            cw_sh[base + i] = (char)0xFF;   /* a cell no text can equal, so the
                                             * whole row is damaged */
            cw_sha[base + i] = 0xFF;
        }
        cw_sh_ind[r] = -128;
    }
    if (cw_sh_ok) {
        for (r = r0; r <= r1; r++)
            cw_render_row(r);
    }

    /* the status line, if the panel reached it */
    y = cw_vsta ? cw_sy - 4 : 0;
    if (y != 0 && cw_m_y2 >= y) {
        cw_st_left[0] = 0;
        cw_st_lamp[0] = 0;
        cw_status();
    }
    cw_sel_xor();
    cw_caret_on();
}
