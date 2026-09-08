/* ============================================================================
 * os8088 - apps/cword/cwchrome.c      the menu bar, the ribbon, the ruler and
 *                                     the status line
 *
 * #included by cword.c - one translation unit (SPEC.md 73.1). Drawing only:
 * nothing here changes the document, and everything here is called with the
 * gfx lock already held.
 *
 * THE CHROME IS THE APP'S OWN because MENU_APPMAX is five and Word's bar is
 * nine (SPEC.md 12.2, 65.2) - which is the same constraint that made the real
 * product draw its menus inside its own frame. Layout, top to bottom:
 *
 *     menu bar   14 px   File Edit View Insert Format Utilities Macro Window Help
 *     ribbon     16 px   Font:/Pts: boxes, B I K | U W D | paragraph mark
 *     ruler      18 px   Style: box, alignment, spacing, indents, inch scale
 *     text        *      the document
 *     status     14 px   Pg Sec n/n At Ln Col + EXT CAPS NUM OVR
 *
 * The three lower strips toggle from the View menu, exactly as they do in the
 * product. The ribbon and ruler layouts follow `Opus/ibdefs.h` and the status
 * line's field ORDER follows `Opus/status.h`.
 *
 * WHAT IT COSTS. The bar is one opaque run of 56 cells plus nine one-pixel
 * mnemonic rules; the ribbon is a run and eleven small boxes; the ruler a run,
 * nine boxes and a scale. That is about 50 calls and 120 cells - ~90 ms on the
 * target 8088 - and it is paid ONCE, on a full repaint. Everything after that
 * is incremental: a ribbon button that lit redraws one box, a status field
 * whose text changed redraws that field, and a keystroke touches neither.
 * ==========================================================================*/

/* cw_box - a control's frame, and its fill. `lit` inverts it, which is how
 * every toggle in this chrome says it is on: the real product used a pressed
 * bitmap, and an inverse cell is this port's equivalent that costs one call
 * and reads correctly on all three adapters. */
static void cw_box(int x1, int y1, int x2, int y2, int lit)
{
    os88_set_color(lit ? OS88_BLACK : OS88_WHITE);
    os88_gfx_fill(x1, y1, x2, y2);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(x1, y1, x2, y2);
}

/* cw_btn - one lettered toggle: a 12x12 box with an 8x8 glyph in it. */
static void cw_btn(int x, int y, int ch, int lit)
{
    cw_box(x, y, x + 11, y + 11, lit);
    cw_rt[0] = (char)ch;
    cw_rt[1] = 0;
    os88_font_run(x + 2, y + 2, cw_rt,
                  lit ? OS88_WHITE : OS88_BLACK,
                  lit ? OS88_BLACK : OS88_WHITE);
}

/* THE PARAGRAPH MARK, WHICH THIS MACHINE HAS NO GLYPH FOR. The kernel's face
 * is characters 32..126 (SPEC.md 6): there is no pilcrow in it, and asking for
 * one draws NOTHING - which is what the first build of the ribbon did, an
 * empty box where the Show-paragraph-marks button belongs.
 *
 * So it is drawn, in three calls: the bowl, the stem, and the second stem.
 * os88_gfx_blit1() would put it on the glass in one, and was tried first - but
 * it requires x to be a multiple of 8 and the ribbon's buttons are 14 apart,
 * so the mark would have had to move inside its own box to suit the primitive.
 * Three calls is 2.3 ms on the target machine and the mark is drawn once per
 * ribbon repaint and once per paragraph on a screen that has asked to see
 * them; that is the right way round. */
static void cw_pilcrow(int x, int y, int ink)
{
    os88_set_color(ink ? OS88_WHITE : OS88_BLACK);
    os88_gfx_fill(x + 1, y, x + 5, y + 3);
    os88_gfx_vline(x + 5, y, y + 7);
    os88_gfx_vline(x + 3, y + 4, y + 7);
}

/* cw_combo - a drop-down box with a caption and the down-arrow well at its
 * right end.
 *
 * THE FONT BOX DROPS DOWN AND THE OTHER TWO DO NOT, which is the honest state
 * of this port rather than an inconsistency: there is a list of typefaces on
 * the system disk (SPEC.md 19.8) and there is exactly one size and one style.
 * The arrow is drawn on all three because a control that lies about being
 * interactive is worse than one that is honest about being the only choice
 * (SPEC.md 47 - grey a fact), and the Pts and Style boxes say what is in force
 * and do nothing when clicked. */
static void cw_combo(int x, int y, int w, const char *s)
{
    cw_box(x, y, x + w - 1, y + 11, 0);
    cw_pad(cw_rt, s, (w - 16) / 8);
    os88_font_run(x + 2, y + 2, cw_rt, OS88_BLACK, OS88_WHITE);
    os88_gfx_vline(x + w - 13, y + 1, y + 10);
    os88_set_color(OS88_BLACK);
    os88_gfx_hline(x + w - 10, x + w - 4, y + 4);
    os88_gfx_hline(x + w - 9, x + w - 5, y + 5);
    os88_gfx_hline(x + w - 8, x + w - 6, y + 6);
    os88_gfx_pixel(x + w - 7, y + 7);
}

/* ==========================================================================
 * THE MENU BAR
 * ========================================================================*/

/* The bar's own strip, and the nine titles as ONE opaque run: 56 cells in a
 * single call rather than nine. The mnemonic rules are nine hlines after it -
 * menus.cmd's '&' is an underline here, not a printed character. */
static void cw_menubar(void)
{
    int i;
    int x;
    int y;

    if (cw_by == 0)
        return;
    y = cw_by + 3;

    os88_set_color(OS88_WHITE);
    os88_gfx_fill(cw_org.x, cw_by, cw_org.x + cw_sz.w - 1, cw_by + CW_H_BAR - 1);

    cw_pad(cw_rt, cw_mbar, cw_bcols);
    os88_font_run(cw_bx, y, cw_rt, OS88_BLACK, OS88_WHITE);

    os88_set_color(OS88_BLACK);
    for (i = 0; i < CW_NMENU; i++) {
        x = cw_bx + (cw_mn_cell[i] + cw_mn_tmn[i]) * 8;
        if (x + 7 > cw_bx + cw_bcols * 8 - 1)
            break;                      /* a narrow window: the bar is clipped
                                         * by the run above and the rules must
                                         * stop where it did */
        os88_gfx_hline(x, x + 7, y + 8);
    }
    os88_gfx_hline(cw_org.x, cw_org.x + cw_sz.w - 1, cw_by + CW_H_BAR - 1);
}

/* ==========================================================================
 * THE RIBBON (View > Ribbon)
 *
 * Font: [Pica] Pts: [10]   B I K | U W D | (paragraph mark)
 *
 * The six letters are Word's own: B bold, I italic, K small caps, U
 * underline, W word underline, D double underline - `ibdefs.h`'s ibBold,
 * ibItalic, ibSmallCaps, ibUnderline, ibWordUnderline, ibDoubleUnderline. They
 * light from the caret's formatting, or from the selection's when there is
 * one, and clicking one applies it.
 * ========================================================================*/

#define CW_RB_BX     264                /* where the six letter buttons start */
#define CW_RB_STEP    14
/* ...and the Font box, which is the one control on this ribbon that opens. */
#define CW_RB_FX      48
#define CW_RB_FW      96

static int cw_rib_attr(void)
{
    int i;
    int a;
    int n;

    if (!cw_sel_on)
        return cw_attr;
    /* the selection's COMMON formatting: a bit lights only if every character
     * in the span has it, which is what makes the ribbon describe the
     * selection rather than one end of it */
    cw_sel_bounds();
    a = CW_A_CHP;
    n = 0;
    for (i = cw_w_end; i < cw_w_next; i++) {
        if (cw_buf[i] == '\n')
            continue;
        a = a & cw_att[i];
        n++;
    }
    return n ? a : cw_attr;
}

static void cw_ribbon(void)
{
    int x;
    int y;
    int a;

    if (!cw_vrib || cw_ry < 0)
        return;
    y = cw_ry + 2;
    a = cw_rib_attr();

    os88_set_color(OS88_WHITE);
    os88_gfx_fill(cw_org.x, cw_ry, cw_org.x + cw_sz.w - 1, cw_ry + CW_H_RIB - 1);

    os88_font_run(cw_bx, y + 2, "Font:", OS88_BLACK, OS88_WHITE);
    cw_combo(cw_bx + CW_RB_FX, y, CW_RB_FW, cw_fcap);
    os88_font_run(cw_bx + 152, y + 2, "Pts:", OS88_BLACK, OS88_WHITE);
    cw_combo(cw_bx + 192, y, 56, "10");

    x = cw_bx + CW_RB_BX;
    if (x + 8 * CW_RB_STEP < cw_org.x + cw_sz.w) {
        cw_btn(x, y, 'B', a & CW_A_BOLD);
        cw_btn(x + CW_RB_STEP, y, 'I', a & CW_A_ITAL);
        cw_btn(x + 2 * CW_RB_STEP, y, 'K', a & CW_A_SCAP);
        cw_btn(x + 3 * CW_RB_STEP + 4, y, 'U', a & CW_A_ULINE);
        cw_btn(x + 4 * CW_RB_STEP + 4, y, 'W', a & CW_A_WUL);
        cw_btn(x + 5 * CW_RB_STEP + 4, y, 'D', a & CW_A_DUL);
        /* the Show-paragraph-marks button, which needs a glyph the face does
         * not have - see cw_pilcrow() */
        cw_box(x + 6 * CW_RB_STEP + 8, y, x + 6 * CW_RB_STEP + 19, y + 11,
               cw_showp);
        cw_pilcrow(x + 6 * CW_RB_STEP + 10, y + 2, cw_showp);
    }
    os88_set_color(OS88_BLACK);
    os88_gfx_hline(cw_org.x, cw_org.x + cw_sz.w - 1, cw_ry + CW_H_RIB - 1);
}

/* ==========================================================================
 * THE FONT LIST (SPEC.md 19.8)
 *
 * The list is the MACHINE'S and not this program's: `SYSTEM/FONTS` sits with
 * the rest of the machine's own furniture on the system disk because a
 * typeface is the machine's the way the kernel's own 8x8 cell is, and a second
 * application wanting the same family should find it already there rather than
 * carry a copy (SPEC.md 19.8.1). So this panel is
 * built from what apps/os88type.inc's ty_scan() found and its length is
 * whatever that disk holds - a disk carrying more faces needs no edit here.
 *
 * ITEM 0 IS ALWAYS `Pica`, which is the kernel's cell and is not on the disk at
 * all: it is face 0 (SPEC.md 6.5), it is what the document is set in until
 * somebody chooses otherwise, and it is how they choose to come BACK.
 * ========================================================================*/

#define CW_FL_H      10                 /* an item's height, the dropdown's */

/* The panel's rectangle, computed in ONE place and read by the painter, the hit
 * test and the close - which is cwdrop.c's discipline in a second dropdown, and
 * for its reason: a hit test that recomputes the layout is a second
 * implementation of it, and the symptom is a list that highlights one row and
 * opens another. */
static void ovl_font_geom(void)
{
    int n;
    int avail;
    int ncol;

    n = cw_nfam + 1;
    cw_fl_x1 = cw_bx + CW_RB_FX;
    cw_fl_y1 = cw_ry + 14;

    /* IT WRAPS INTO COLUMNS RATHER THAN BEING CUT, and that is not tidiness.
     * The first version was one column clamped to the content box, which is
     * what cwdrop.c's menus do - and a menu that loses its last item on a short
     * screen is what the real product did, so it was defensible there. Here it
     * is not: the items are the machine's typefaces and losing one means a face
     * on the disk that cannot be chosen at all. Measured on CGA (640x200,
     * SPEC.md 39), where the window is 150 rows of content and the tenth face
     * was the last one the panel could show - `Times` was on the disk, in the
     * scan, and unreachable.
     *
     * So the height is what the window has and the WIDTH grows instead. On VGA
     * every face fits in one column and this is the geometry it always was. */
    avail = (cw_org.y + cw_sz.h - 1) - cw_fl_y1 - 3;
    cw_fl_rows = avail / CW_FL_H;
    if (cw_fl_rows < 1)
        cw_fl_rows = 1;
    if (cw_fl_rows > n)
        cw_fl_rows = n;
    ncol = (n + cw_fl_rows - 1) / cw_fl_rows;

    cw_fl_x2 = cw_fl_x1 + ncol * CW_RB_FW - 1;
    cw_fl_y2 = cw_fl_y1 + cw_fl_rows * CW_FL_H + 3;
    if (cw_fl_x2 > cw_org.x + cw_sz.w - 2) {
        cw_fl_x1 = cw_org.x + cw_sz.w - 2 - ncol * CW_RB_FW;
        cw_fl_x2 = cw_fl_x1 + ncol * CW_RB_FW - 1;
    }
    if (cw_fl_x1 < cw_org.x)
        cw_fl_x1 = cw_org.x;
}

static void ovl_font_paint(void)
{
    int i;
    int x;
    int y;

    os88_set_color(OS88_WHITE);
    os88_gfx_fill(cw_fl_x1, cw_fl_y1, cw_fl_x2, cw_fl_y2);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(cw_fl_x1, cw_fl_y1, cw_fl_x2, cw_fl_y2);

    for (i = 0; i <= cw_nfam; i++) {
        x = cw_fl_x1 + (i / cw_fl_rows) * CW_RB_FW;
        y = cw_fl_y1 + 2 + (i % cw_fl_rows) * CW_FL_H;
        /* THE NAME IS FETCHED INTO A BUFFER OF OURS and never held as a
         * pointer into the library's table - see cw_ty_name() in
         * apps/cword/cwtype.inc for the hard hang that rule is written from. */
        if (i == 0)
            cw_pad(cw_rt, "Pica", (CW_RB_FW - 8) / 8);
        else {
            cw_ty_name(i - 1, cw_tmp);
            cw_pad(cw_rt, cw_tmp, (CW_RB_FW - 8) / 8);
        }
        os88_font_run(x + 4, y, cw_rt, OS88_BLACK, OS88_WHITE);
        if (i - 1 == cw_fam)
            os88_gfx_xor_fill(x + 1, y - 1, x + CW_RB_FW - 2, y + 8);
    }
}

/* The item under a point, or -1. It reads the same two divisions the painter
 * does, which is why they are here and not in two places. */
static int ovl_font_hit(int mx, int my)
{
    int i;
    int c;
    int r;

    if (mx < cw_fl_x1 || mx > cw_fl_x2 || my < cw_fl_y1 || my > cw_fl_y2)
        return -1;
    c = (mx - cw_fl_x1) / CW_RB_FW;
    r = (my - cw_fl_y1 - 2) / CW_FL_H;
    if (r < 0 || r >= cw_fl_rows)
        return -1;
    i = c * cw_fl_rows + r;
    if (i > cw_nfam)
        return -1;
    return i;
}

/* The hit test, sharing the numbers above rather than repeating them: the
 * ribbon is laid out in ONE place and read in two.
 * out: the CW_A_* bit clicked, -1 for the paragraph-mark button, -2 for the
 *      Font box, 0 for a miss */
static int cw_rib_hit(int mx, int my)
{
    int x;
    int i;

    if (!cw_vrib || cw_ry < 0)
        return 0;
    if (my < cw_ry + 2 || my > cw_ry + 13)
        return 0;
    if (mx >= cw_bx + CW_RB_FX && mx < cw_bx + CW_RB_FX + CW_RB_FW)
        return -2;
    x = cw_bx + CW_RB_BX;
    for (i = 0; i < 7; i++) {
        int bx;

        bx = x + i * CW_RB_STEP;
        if (i >= 3)
            bx = bx + 4;
        if (i >= 6)
            bx = bx + 4;
        if (mx >= bx && mx <= bx + 11) {
            if (i == 0)
                return CW_A_BOLD;
            if (i == 1)
                return CW_A_ITAL;
            if (i == 2)
                return CW_A_SCAP;
            if (i == 3)
                return CW_A_ULINE;
            if (i == 4)
                return CW_A_WUL;
            if (i == 5)
                return CW_A_DUL;
            return -1;
        }
    }
    return 0;
}

/* ==========================================================================
 * THE RULER (View > Ruler)
 *
 * Style: [Normal]  L C R J | 1 5 2 | O C          and the inch scale under it
 *
 * The four alignment buttons and the three line-spacing buttons are Word's
 * ruler, lettered rather than pictured because an 8x8 cell is what this
 * machine draws in: L C R J for left/centre/right/justified, 1 / 5 / 2 for
 * single, one-and-a-half and double spacing, O and C for opening and closing
 * the space before a paragraph. The scale is one dot a cell and a number every
 * ten - a cell is 1/10 inch at pica pitch, so the numbers ARE inches.
 * ========================================================================*/

#define CW_RU_BX     160                /* clear of "Style:" and its box */

/* The ruler's scale, composed here and blitted in one call - four pixel rows,
 * one byte a cell plus one for the mark at the right margin. */
#define CW_SCALE_ROWS  4
static unsigned char cw_scale[CW_SCALE_ROWS * (CW_RULE_MAX + 1)];
#define CW_RU_STEP    14

static void cw_ruler(void)
{
    int x;
    int y;
    int i;
    int p;
    int sx;
    int nc;

    if (!cw_vrul || cw_uy < 0)
        return;
    y = cw_uy + 2;
    p = cw_pap_of(cw_cur);

    os88_set_color(OS88_WHITE);
    os88_gfx_fill(cw_org.x, cw_uy, cw_org.x + cw_sz.w - 1, cw_uy + CW_H_RUL - 1);

    os88_font_run(cw_bx, y + 2, "Style:", OS88_BLACK, OS88_WHITE);
    cw_combo(cw_bx + 56, y, 88, "Normal");

    x = cw_bx + CW_RU_BX;
    if (x + 9 * CW_RU_STEP + 20 < cw_org.x + cw_sz.w) {
        cw_btn(x, y, 'L', cw_pap_al[p] == CW_AL_LEFT);
        cw_btn(x + CW_RU_STEP, y, 'C', cw_pap_al[p] == CW_AL_CENTER);
        cw_btn(x + 2 * CW_RU_STEP, y, 'R', cw_pap_al[p] == CW_AL_RIGHT);
        cw_btn(x + 3 * CW_RU_STEP, y, 'J', cw_pap_al[p] == CW_AL_JUST);
        cw_btn(x + 4 * CW_RU_STEP + 4, y, '1', cw_pap_sp[p] == 0);
        cw_btn(x + 5 * CW_RU_STEP + 4, y, '5', cw_pap_sp[p] == 1);
        cw_btn(x + 6 * CW_RU_STEP + 4, y, '2', cw_pap_sp[p] == 2);
        cw_btn(x + 7 * CW_RU_STEP + 8, y, 'O', cw_pap_op[p] != 0);
        cw_btn(x + 8 * CW_RU_STEP + 8, y, 'C', cw_pap_op[p] == 0);
    }

    /* The scale, on its own row UNDER the buttons: a dot a cell, a taller tick
     * every five, and the inch numbers every ten - a cell is 1/10 inch at pica
     * pitch, so the numbers ARE inches. The first version drew the numbers 11
     * pixels above the tick row, which on an 18-pixel strip put them inside the
     * buttons; the strip is 26 now and the two rows do not meet.
     *
     * COMPOSED AND BLITTED, NOT DRAWN DOT BY DOT. A dot a cell is 71 drawing
     * calls on a 71-column window, and a drawing call is 756 us WHATEVER IT
     * DRAWS (PERFORMANCE.md) - so the scale alone was 54 ms and a third of
     * everything a full repaint did. Building the four pixel rows in this
     * package's own memory and handing them to os88_gfx_blit1() is ONE call
     * (SPEC.md 5.4.2), which is the same trade apps/os88type.inc makes for a
     * row of proportional text. blit1 can refuse - a kern_small machine
     * carries the slot without the body - so the dot-by-dot loop stays as the
     * fallback rather than as the method. */
    /* THE SCALE IS IN 1/10 INCH AND NOT IN CELLS, which stopped being the same
     * thing the day a face could be chosen (SPEC.md 73.12.2): a ruler measures
     * the SHEET, so a tick is 8 pixels in every face and `nc` is the column's
     * width in ticks - cw_wpx / 8 - rather than cw_cols, which is now a count
     * of however many glyphs a row may hold. */
    nc = cw_wpx / 8;
    if (nc > CW_RULE_MAX)
        nc = CW_RULE_MAX;
    sx = cw_uy + CW_H_RUL - 3;
    os88_set_color(OS88_BLACK);
    for (i = 0; i < CW_SCALE_ROWS * (CW_RULE_MAX + 1); i++)
        cw_scale[i] = 0;
    for (i = 0; i <= nc; i++) {
        int b;

        b = i;                          /* tick i's pixel is bit 7 of byte i:
                                         * the scale is 8 px a tick and the
                                         * band starts at cw_tx */
        cw_scale[3 * (CW_RULE_MAX + 1) + b] = 0x80;
        if ((i % 5) == 0)
            cw_scale[2 * (CW_RULE_MAX + 1) + b] = 0x80;
        if ((i % 10) == 0) {
            cw_scale[b] = 0x80;
            cw_scale[(CW_RULE_MAX + 1) + b] = 0x80;
        }
    }
    if (os88_gfx_blit1(cw_scale, CW_RULE_MAX + 1, cw_tx, sx - 3,
                       nc * 8, CW_SCALE_ROWS) != 0) {
        for (i = 0; i <= nc; i++) {
            if ((i % 10) == 0)
                os88_gfx_vline(cw_tx + i * 8, sx - 3, sx);
            else if ((i % 5) == 0)
                os88_gfx_vline(cw_tx + i * 8, sx - 1, sx);
            else
                os88_gfx_pixel(cw_tx + i * 8, sx);
        }
    }
    for (i = 10; i <= nc; i = i + 10) {
        if (i / 10 >= 10)
            break;
        cw_rt[0] = (char)('0' + i / 10);
        cw_rt[1] = 0;
        os88_font_run(cw_tx + i * 8 + 2, sx - 11, cw_rt,
                      OS88_BLACK, OS88_WHITE);
    }
    os88_set_color(OS88_BLACK);
    /* the indent markers: the left indent as a full mark, the first line's as
     * a mark above it, the right indent at the other end */
    i = cw_pap_li[p];
    os88_gfx_fill(cw_tx + i * 8 - 2, sx - 3, cw_tx + i * 8 + 2, sx - 2);
    i = cw_pap_li[p] + cw_pap_fi[p];
    if (i >= 0)
        os88_gfx_fill(cw_tx + i * 8 - 2, sx - 6, cw_tx + i * 8 + 2, sx - 5);
    i = nc - cw_pap_ri[p];
    os88_gfx_fill(cw_tx + i * 8 - 2, sx - 3, cw_tx + i * 8 + 2, sx - 2);

    os88_gfx_hline(cw_org.x, cw_org.x + cw_sz.w - 1, cw_uy + CW_H_RUL - 1);
}

/* The ruler's hit test. out: 1..4 alignment, 5..7 spacing, 8 open, 9 close */
static int cw_rul_hit(int mx, int my)
{
    int x;
    int i;
    int bx;

    if (!cw_vrul || cw_uy < 0)
        return 0;
    if (my < cw_uy + 2 || my > cw_uy + 13)
        return 0;
    x = cw_bx + CW_RU_BX;
    for (i = 0; i < 9; i++) {
        bx = x + i * CW_RU_STEP;
        if (i >= 4)
            bx = bx + 4;
        if (i >= 7)
            bx = bx + 8;
        if (mx >= bx && mx <= bx + 11)
            return i + 1;
    }
    return 0;
}

/* ==========================================================================
 * THE STATUS LINE (View > Status Bar)
 *
 *     Pg 1  Sec 1  1/1  At 1li  Ln 1  Col 1                 EXT CAPS NUM OVR
 *
 * status.h's field order, and `At` wears the `li` unit because a line is the
 * honest draft-view measure of where you are - a page is 54 lines here, so At
 * and Ln agree by construction rather than by a promise about paper.
 *
 * TWO SHADOWS AND NOT SIX. The left group is built into one string and drawn
 * as one run when it differs from what is on the glass; the lamps are a second.
 * Six separate fields would be six comparisons and up to six calls for a
 * keystroke that moved the column by one; one run of 40 cells is one call, and
 * the comparison means an arrow key that stays on its line draws nothing.
 * ========================================================================*/

/* The BIOS keyboard flag byte, which is where CapsLock and NumLock live: bit 6
 * caps, bit 5 num (0040:0017). Read on every status update rather than polled
 * by a worker task, which is a real difference and worth stating: pressing
 * CapsLock ALONE emits no key event, so this port's lamp changes on the next
 * keystroke or click rather than at the moment the key went down. The
 * assembly port hires a worker for that poll; this one does not have a worker
 * at all, which is also what makes SPEC.md 73.14's UI-task-only overlay rule
 * free here. */
#define CW_BIOS_KFLAG 0x0017

/* The two halves of building the status string, appending onto cw_tmp. */
static void cw_st_add(const char *s)
{
    os88_strcpy(cw_tmp + os88_strlen(cw_tmp), s,
                sizeof(cw_tmp) - os88_strlen(cw_tmp));
}

static void cw_st_num(int v)
{
    os88_utoa((unsigned)v, cw_num);
    cw_st_add(cw_num);
}

static void cw_status(void)
{
    int i;
    int lamp;
    int x;
    int n;

    if (!cw_vsta || cw_sy == 0 || cw_bcols < 24)
        return;

    /* BUILT BY APPENDING AND NOT BY WRITING NUMBERS INTO A TEMPLATE AT FIXED
     * OFFSETS, which is what the first version did: `Pg 1  Sec 1  1/1` came
     * out as `Pg 1  Sec 1  1 1` because the page number's second digit landed
     * on the slash, and the offsets had to be recounted by hand every time a
     * field changed width. Appending cannot be off by one. */
    cw_tmp[0] = 0;
    cw_st_add("Pg ");
    cw_st_num(cw_pg);
    cw_st_add("  Sec 1  ");
    cw_st_num(cw_pg);
    cw_st_add("/");
    cw_st_num((cw_ln - 1) / CW_PGLINES + 1);

    /* A window too narrow to show every cell DROPS the At field first: it
     * duplicates Ln by construction, so losing it keeps the two numbers that
     * do not repeat each other (SPEC.md 68.2). */
    n = cw_bcols - 19;
    if (n >= 40) {
        cw_st_add("  At ");
        cw_st_num(cw_ln);
        cw_st_add("li");
    }
    cw_st_add("  Ln ");
    cw_st_num(cw_ln);
    cw_st_add("  Col ");
    cw_st_num(cw_col);

    if (n > 44)
        n = 44;
    if (n < 20)
        n = 20;
    /* DELTA-DRAWN, the same way a text row is. `Col n` changes on EVERY
     * keystroke, so a status line redrawn whole costs 44 glyph cells - 40 ms
     * on the target machine, ten times the cost of the character that was
     * typed, to say that the column went from 30 to 31. Comparing against the
     * shadow and lettering only the differing span costs the two or three
     * cells that actually changed.
     *
     * The demonstrator solved this by not showing the column at all. This is
     * Word's status line and status.h says the column is on it, so the answer
     * has to be the cheap redraw rather than the missing field. */
    cw_pad(cw_rt, cw_tmp, n);
    if (!cw_streq(cw_rt, cw_st_left)) {
        int a;
        int b;

        a = 0;
        while (a < n && cw_rt[a] == cw_st_left[a])
            a++;
        b = n - 1;
        while (b > a && cw_rt[b] == cw_st_left[b])
            b--;
        if (cw_st_left[0] == 0) {       /* nothing known: the whole field */
            a = 0;
            b = n - 1;
        }
        os88_strcpy(cw_st_left, cw_rt, sizeof(cw_st_left));
        cw_rt[b + 1] = 0;
        os88_font_run(cw_bx + a * 8, cw_sy, cw_rt + a, OS88_BLACK, OS88_WHITE);
    }

    /* the four lamps, inverse-video when lit */
    lamp = os88_peek(0x0040, CW_BIOS_KFLAG);
    cw_pad(cw_rt, "", 19);
    if (cw_ext) {
        cw_rt[0] = 'E'; cw_rt[1] = 'X'; cw_rt[2] = 'T';
    }
    if (lamp & 0x40) {
        cw_rt[4] = 'C'; cw_rt[5] = 'A'; cw_rt[6] = 'P'; cw_rt[7] = 'S';
    }
    if (lamp & 0x20) {
        cw_rt[9] = 'N'; cw_rt[10] = 'U'; cw_rt[11] = 'M';
    }
    if (cw_ovr) {
        cw_rt[13] = 'O'; cw_rt[14] = 'V'; cw_rt[15] = 'R';
    }
    if (cw_bcols < 44)
        return;                         /* the lamps stand down whole rather
                                         * than collide with the text */
    if (!cw_streq(cw_rt, cw_st_lamp)) {
        x = cw_bx + (cw_bcols - 17) * 8;
        os88_font_run(x, cw_sy, cw_rt, OS88_BLACK, OS88_WHITE);
        os88_strcpy(cw_st_lamp, cw_rt, sizeof(cw_st_lamp));
        /* inverse where a lamp is lit: one XOR per lit lamp, and a lamp
         * changes about as often as the user presses Ins */
        for (i = 0; i < 4; i++) {
            int on;
            int c0;

            on = (i == 0) ? cw_ext :
                 (i == 1) ? (lamp & 0x40) :
                 (i == 2) ? (lamp & 0x20) : cw_ovr;
            if (!on)
                continue;
            c0 = (i == 0) ? 0 : (i == 1) ? 4 : (i == 2) ? 9 : 13;
            n = (i == 1) ? 4 : 3;
            os88_gfx_xor_fill(x + c0 * 8, cw_sy, x + (c0 + n) * 8 - 1,
                              cw_sy + 7);
        }
    }
    os88_set_color(OS88_BLACK);
    os88_gfx_hline(cw_org.x, cw_org.x + cw_sz.w - 1, cw_sy - 4);
}
