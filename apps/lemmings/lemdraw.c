/* ============================================================================
 * os8088 - apps/lemmings/lemdraw.c   (#included by lemmings.c)
 *
 * THE FRAME (SPEC.md 92.4). What is drawn and when; lemblit.inc, lemmask.inc
 * and lemfont.inc touch the pixels. Nothing in this file writes one, and that
 * is the rule SPEC.md 73.11 states as "the inner loop is never C": a per-pixel
 * loop in C costs 3-5x what the same loop costs in assembly, and inside
 * SPEC.md 53's bracket every kernel drawing slot is refused anyway, so there is
 * no third option.
 *
 * WHAT WAVE 2 DRAWS. The composed level - terrain from the style bank, in the
 * level's own sixteen colours on VGA and by colour CLASS on the two 1bpp
 * adapters - the skill panel out of MAIN.DAT's own bitmap, the minimap sampled
 * by lemmings_3ds/doc/data/minimap.txt's rule, the view rectangle inside it,
 * and the 40-character status line in MAIN.DAT's own green font. The lemmings,
 * their eighteen actions and the panel's live counts are wave 3; the sprite
 * path they will use is exercised and BENCHED here (tests/lembench) so that
 * wave 3 builds on a measured number rather than a hope.
 *
 * THE MINIMAP HAS A SHADOW OF ITS OWN, 104 x 20 bits = 260 bytes, and it is
 * not an optimisation waiting to happen: the VIEW RECTANGLE is drawn INSIDE
 * the minimap and has to be erased when it moves, and what is under it is the
 * minimap's own cells. Re-sampling them off the world would be 16 probes a
 * cell, which is what drawing the whole minimap costs and is ~0.2 s on a
 * 4.77 MHz 8088 - per scroll step.
 *
 * ATTRIBUTION. The minimap's sampling rule, the panel's geometry and the
 * status line's five field offsets are the original's, cited at the line that
 * carries each; Lemmings is (C) 1991 DMA Design / Psygnosis and README.TXT
 * carries the full attribution.
 * ==========================================================================*/

/* --- the panel, in the original's own coordinates ---------------------------
 * Lemmix src/Dos.Consts.pas: DOS_MINIMAP_WIDTH 104, DOS_MINIMAP_HEIGHT 20 and
 * DosMiniMapCorners (208,18)-(311,37), all relative to the 320x40 panel
 * bitmap. The status line is the panel's top 16 rows, which is where
 * Game.SkillPanel.pas draws it. */
#define LEM_MM_X       208
#define LEM_MM_Y        18
#define LEM_MM_W       104
#define LEM_MM_H        20
#define LEM_MM_STRIDE   13          /* 104 bits */

/* The minimap's sampling rule, from lemmings_3ds/doc/data/minimap.txt: a cell
 * covers SIXTEEN world pixels across and samples ONE world row per cell row,
 * rows 16, 24, ... 152; nine of the sixteen non-zero lights the cell.
 *
 * ...AND IT IS INSET BY ONE ROW INSIDE THE RECTANGLE'S WELL. lemmings_3ds/src/
 * draw.c:992 draws world row yi at `minimap_y = y + 17 + yi/8`, so rows
 * 16..152 land on panel rows 19..36 while draw.c:1023 draws the view rectangle
 * at panel rows 18..37 - one clear row above the sample and one below. Drawing
 * the first sampled row AT LEM_MM_Y put it under the rectangle's own top edge
 * and left rows 36 and 37 permanently black, which is the same eighteen rows
 * one pixel out of place. LEM_MM_YOFF is that inset. */
#define LEM_MM_XSTEP    16
#define LEM_MM_YSTEP     8
#define LEM_MM_Y0       16
#define LEM_MM_THRESH    9
#define LEM_MM_YOFF      1          /* lemmings_3ds/src/draw.c:992 */
#define LEM_MM_ROWS     18          /* world rows 16..152 step 8 - the rows
                                     * actually SAMPLED, which is two fewer
                                     * than the rectangle's twenty */

#define LEM_VIEW_W     320          /* what the view shows of the 1,584 */
#define LEM_STAT_ROW     0          /* the status line's pixel row in the panel */
#define LEM_STAT_CELLS  40

/* The three colour ids this file names, AND THEY ARE THE ORIGINAL'S OWN, not a
 * choice. On VGA they are palette entries and on the two 1bpp adapters
 * lb_spix() turns them into the three classes SPEC.md 39.4 requires - solid,
 * dithered, black - because grey rounds to black there and a shade is not a
 * distinction.
 *
 * ENTRY 7 IS THE MINIMAP'S AND ENTRY 3 IS THE RECTANGLE'S, and the first build
 * used 11 and 15 - both of which are in the level style's CUSTOM half.
 * lemmings_3ds/doc/data/lemmings_main_dat_file_format.txt section 2 says it
 * outright ("Color idx 7 is the color used to render the build bricks and the
 * mini-map at the bottom right") and lemmings_3ds/src/draw.c:990 is
 * `solid = (solid>8?7:0)`; the rectangle is Lemmix Game.SkillPanel.pas:127,
 * `DosInLevelPalettes[False][3]; // white`, and lemmings_3ds/src/draw.c:1010's
 * `highperf_palette[3]`.
 *
 * IT IS NOT A SHADE QUESTION, IT IS A CORRECTNESS ONE, twice over: 11 and 15
 * are the style's own custom colours, so the minimap changed hue per graphic
 * set and could land ON the terrain's own colour (7 is a COPY of custom[0],
 * which is exactly why the original chose it); and on the two 1bpp adapters 11
 * and 15 fall in the 8..15 half, so the minimap and the rectangle were DITHERED
 * where the panel around them is solid. lem_r_pal() already loads DAC entry 7
 * as that copy and 0..6 from the standard palette (lemblit.inc), so both are
 * right on the glass with no other change. */
#define LEM_C_BLACK      0
#define LEM_C_MAP        7
#define LEM_C_VIEW       3

/* ...AND ON THE SHADOW BACKENDS THE MAP IS THE TERRAIN CLASS, WHICH IS NOT
 * ENTRY 7. Everything above is about the VGA's palette, where 7 and 3 are two
 * different colours. The two cards with no palette read a NIBBLE'S HALF and
 * nothing else (lb_spix): 1..7 is the sprites/panel/text class and 8..15 the
 * terrain class, so 7 and 3 land in the SAME class there and the view
 * rectangle disappears into every lit minimap cell it crosses - which
 * build/port-shots/wave2f-herc-fun1.png showed, both vertical edges swallowed
 * by the solid mass while the top and bottom survived only because
 * LEM_MM_YOFF insets the sample off them. It also drew the map's terrain in
 * the SPRITE class two inches under a level drawn in the terrain class.
 *
 * So the map's set cells take a nibble of 8..15 on those two cards. The
 * rectangle keeps entry 3 and is therefore the other class on all three: white
 * over magenta on CGA, solid over a 50% dither on Hercules, and byte for byte
 * what it already was on VGA. In the original the rectangle is white against
 * the build-brick colour and is always readable, which is the whole of what it
 * is for - it is the only thing telling the player where they are in a
 * 1,584-pixel level. §92.4.4. */
#define LEM_C_MAP1BPP    8

static int lem_c_map(void)
{
    return lem_kind_id() == LEM_RKIND_VGA ? LEM_C_MAP : LEM_C_MAP1BPP;
}

static unsigned char lem_mm[LEM_MM_H * LEM_MM_STRIDE];
static int lem_view;                /* the view's left edge, in world pixels */
static int lem_view_drawn;          /* ...and the MINIMAP CELL the view
                                     * rectangle is drawn at, which is not the
                                     * same number: a cell is 16 world pixels */
static int lem_lvl_ok;              /* a level is composed and on the glass */

/* The four-offset table lem_probe4() takes: four adjacent pixels of one row.
 * `static const` because the address of an automatic is refused by name
 * (SPEC.md 73.5) and because a shared table costs nothing. */
static const char lem_off4[8] = { 0, 0, 1, 0, 2, 0, 3, 0 };

/* lem_bits4 - how many of the four bits lem_probe4() answered are set. A
 * four-entry table beats a shift loop and this is called 7,128 times when a
 * minimap is sampled. */
static const unsigned char lem_pop4[16] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4
};

/* ============================================================================
 * The level, composed
 * ==========================================================================*/

/* lem_level_load - the banks this level needs, into their claims.
 *
 * IT IS THE FILE HALF AND NOTHING ELSE, so it can run OUTSIDE the bracket
 * where a refusal can be said in a window. Reading a file mid-bracket is legal
 * (SPEC.md 53.1) and composing has to be, because on VGA the picture IS video
 * memory and there is none until the mode is set - but a REFUSAL cannot be
 * said in a foreign mode, so everything that can refuse happens here.
 *
 * 0 = something is missing, and the caller says which in the window - WHICH is
 * lem_load_err, because the two refusals are different sentences (SPEC.md 47:
 * a refusal names the fact it refused on) and a bare status cannot carry that.
 *
 * IT IS THE ONLY COPY OF THIS SEQUENCE. lem_launch() used to open-code the
 * identical three calls, so the harness exercised this function and the package
 * exercised that copy, and this one was emitted resident and never called -
 * SmallerC does not drop an unreferenced static. Two copies of one sequence
 * drift the moment either grows a step (a sixth claim, a retry, a refusal). */
#define LEM_LE_MEM   1              /* a claim refused */
#define LEM_LE_BANK  2              /* a band file did not read */

static int lem_load_err;

static int lem_level_load(int level)
{
    lem_load_err = LEM_LE_MEM;
    if (!lem_claim_all())
        return 0;
    lem_load_err = LEM_LE_BANK;
    if (!lem_main_read())
        return 0;
    if (!lem_bank_read(lem_ent_u8(level, LEM_E_STYLE)))
        return 0;
    lem_load_err = 0;
    return 1;
}

/* lem_compose_level - the terrain, into the mask AND the picture.
 *
 * The walk itself is ovl_compose()'s, in the module: it runs ONCE PER LEVEL and
 * never during play, which is SPEC.md 73.14's split by frequency exactly. What
 * is here is the frame around it - clearing both surfaces first, and deriving
 * the 1bpp world from the mask afterwards. */
/* lem_world_clear - both surfaces, BEFORE anything is put on the glass.
 *
 * It is separate from the compose because the panel and the LOADING message go
 * down between them (lem_frame_panel), and on the two shadow backends the
 * present that puts them there composes the world window - which has to be a
 * cleared world and not a fresh claim's contents. */
static void lem_world_clear(void)
{
    lem_m_setup(lem_cl_mask);
    lem_m_clear();
    lem_r_clearworld();
}

static void lem_compose_level(int level)
{
    if (lem_ovl == 1)
        ovl_compose(level);
    lem_r_derive();                 /* a no-op on VGA: its world is in VRAM */
}

/* ============================================================================
 * The minimap (lemmings_3ds/doc/data/minimap.txt)
 * ==========================================================================*/

/* lem_mm_sample - the whole minimap into its own 260-byte shadow.
 *
 * 99 cells by 18 rows, sixteen probes each - which through lem_probe4() is
 * 7,128 calls rather than 28,512, and THAT is what the batched probe entry was
 * built for (lemmask.inc's header). It is ~0.2 s on a 4.77 MHz 8088, once per
 * level, against ~0.8 s one probe at a time. */
static void lem_mm_sample(void)
{
    int mx, my, wx, wy, n, i;

    for (i = 0; i < LEM_MM_H * LEM_MM_STRIDE; i++)
        lem_mm[i] = 0;
    for (my = 0; my < LEM_MM_ROWS; my++) {
        wy = LEM_MM_Y0 + my * LEM_MM_YSTEP;
        if (wy >= LEM_WORLD_H)
            break;
        for (mx = 0; mx < LEM_MM_W; mx++) {
            wx = mx * LEM_MM_XSTEP;
            if (wx >= LEM_WORLD_W)
                break;
            n = lem_pop4[lem_probe4(wx, wy, lem_off4)];
            n += lem_pop4[lem_probe4(wx + 4, wy, lem_off4)];
            n += lem_pop4[lem_probe4(wx + 8, wy, lem_off4)];
            n += lem_pop4[lem_probe4(wx + 12, wy, lem_off4)];
            if (n >= LEM_MM_THRESH)
                lem_mm[my * LEM_MM_STRIDE + (mx >> 3)] |=
                    (unsigned char)(0x80 >> (mx & 7));
        }
    }
}

static int lem_mm_bit(int mx, int my)
{
    if (mx < 0 || mx >= LEM_MM_W || my < 0 || my >= LEM_MM_ROWS)
        return 0;
    return (lem_mm[my * LEM_MM_STRIDE + (mx >> 3)] >> (7 - (mx & 7))) & 1;
}

/* lem_mm_row - one minimap row's cells, from column c0 to c1 inclusive, out of
 * the shadow. It is what erases the view rectangle when it moves: what is
 * under a view rectangle is exactly this.
 *
 * `my` MAY BE -1 OR LEM_MM_ROWS..: the rectangle is twenty rows and the sample
 * is eighteen inset by one (LEM_MM_YOFF), so its top and bottom edges sit on
 * panel rows no cell was ever drawn on. lem_mm_bit() answers 0 outside the
 * sampled rows, which is what those two rows are: black. */
static void lem_mm_row(int my, int c0, int c1)
{
    int mx, run0, cur, v, on;

    /* ONCE PER ROW AND NOT ONCE PER RUN. lem_c_map() is two near calls and a
     * compare, which is nothing beside a run's own lem_r_rect - but a run is
     * 150-550 us (§92.7.1) and a per-run call would put a fifth of that back
     * on the one path this wave measured. */
    on = lem_c_map();
    if (c0 < 0)
        c0 = 0;
    if (c1 > LEM_MM_W - 1)
        c1 = LEM_MM_W - 1;
    if (c1 < c0)
        return;
    run0 = c0;
    cur = lem_mm_bit(c0, my);
    for (mx = c0 + 1; mx <= c1 + 1; mx++) {
        v = (mx <= c1) ? lem_mm_bit(mx, my) : -1;
        if (v == cur)
            continue;
        lem_r_rect(LEM_MM_X + run0, LEM_MM_Y + LEM_MM_YOFF + my,
                   mx - run0, 1, cur ? on : LEM_C_BLACK, 1);
        run0 = mx;
        cur = v;
    }
}

/* lem_mm_col - the same thing down a COLUMN, in RECTANGLE rows.
 *
 * IT IS A RUN AND NOT A CELL, AND THE BENCH IS WHY. SPEC.md 92.7.1 measured a
 * bracketed one-cell `lem_r_rect` at 548 us and a plot INSIDE one call at
 * 153 us: the SmallerC cdecl boundary is three quarters of a minimap cell, so
 * the batch that took the VGA's ten `out`s a pixel down to two barely moved a
 * per-cell caller. Coalescing runs of one colour is what actually pays - a
 * minimap column of eighteen cells is typically three or four runs, so the
 * view rectangle's two erased columns go from 36 calls to about 8. */
static void lem_mm_col(int mx, int r0, int r1)
{
    int r, run0, cur, v, on;

    on = lem_c_map();               /* once per column - see lem_mm_row */
    if (mx < 0 || mx >= LEM_MM_W || r1 < r0)
        return;
    run0 = r0;
    cur = lem_mm_bit(mx, r0 - LEM_MM_YOFF);
    for (r = r0 + 1; r <= r1 + 1; r++) {
        v = (r <= r1) ? lem_mm_bit(mx, r - LEM_MM_YOFF) : -1;
        if (v == cur)
            continue;
        lem_r_rect(LEM_MM_X + mx, LEM_MM_Y + run0, 1, r - run0,
                   cur ? on : LEM_C_BLACK, 1);
        run0 = r;
        cur = v;
    }
}

static void lem_mm_draw(void)
{
    int my;

    lem_r_batch(1);
    for (my = 0; my < LEM_MM_ROWS; my++)
        lem_mm_row(my, 0, LEM_MM_W - 1);
    lem_r_batch(0);
}

/* lem_view_rect - the view rectangle inside the minimap, moved.
 *
 * THE RECTANGLE IS 25 CELLS AND NOT 320/16, and that is the original's own
 * fudge rather than arithmetic. Lemmix src/Dos.Consts.pas:71-79 documents it in
 * so many words - "we cannot get a nice minimapscale 1/16 so instead we chose
 * the following: image width of game = 1584, imagewidth of minimap = 104, width
 * of white rectangle in minimap = 25" - and Game.SkillPanel.pas:584 draws
 * `FrameRectS(208 + X, 18, 208 + X + 20 + 5, 38)`, which is 25 columns by 20
 * rows. (lemmings_3ds/src/draw.c:1005 computes 24 for a 320-wide screen from a
 * different constant; Lemmix's is the documented DOS number and is the one
 * followed here. The two references disagree by one column.) Derived as
 * 320/16 = 20 the rectangle is a fifth narrower than the original's and
 * under-reports how much of the level is on screen; 79 + 24 = 103 is also
 * exactly the last minimap column, which is what makes 25 fit.
 *
 * IT ERASES ONLY WHAT THE NEW OUTLINE WILL NOT COVER, out of the minimap's
 * shadow, which is the whole reason that shadow exists (see the file header).
 * The first version erased the WHOLE old outline - 2*(W + H) - 4 = 86 cells -
 * and then drew the whole new one, so on a one-cell step 84 of those cells were
 * written twice: PERFORMANCE.md's double-draw flash, on the rectangle's top and
 * bottom edges, every fourth tick of a scroll. What actually has to change for
 * a move of d cells is
 *
 *   erased   both old COLUMNS (the left one leaves the rectangle, the right one
 *            becomes its interior - and the interior is not drawn, so a stale
 *            line would sit inside it), plus the d cells of the top and bottom
 *            rows the new rectangle no longer reaches;
 *   drawn    both new columns, plus the d cells of the top and bottom rows it
 *            newly reaches.
 *
 * which for d = 1 is 38 cells erased and 38 drawn, none of them twice, against
 * 86 + 86. (The review that found this priced the erase at 18 cells by counting
 * only the vacated column; the old RIGHT column has to go too, and that is the
 * half that would have been left behind as a line inside the new rectangle.) */
#define LEM_VR_W  25    /* Lemmix Dos.Consts.pas:79, Game.SkillPanel.pas:584 -
                         * NOT LEM_VIEW_W / LEM_MM_XSTEP */

/* lem_vr_edge - the outline's four pieces, drawn or erased.
 * `r` is a RECTANGLE row 0..LEM_MM_H-1; the sample is inset by LEM_MM_YOFF, so
 * cell row = r - LEM_MM_YOFF and the two edge rows fall outside the sample. */
static void lem_vr_erase_span(int r, int c0, int c1)
{
    if (c1 >= c0)
        lem_mm_row(r - LEM_MM_YOFF, c0, c1);
}

static void lem_vr_draw_span(int r, int c0, int c1)
{
    if (c1 >= c0)
        lem_r_rect(LEM_MM_X + c0, LEM_MM_Y + r, c1 - c0 + 1, 1,
                   LEM_C_VIEW, 1);
}

static void lem_view_rect(int erase_x0)
{
    int x0, c0, c1, el, er;

    x0 = lem_view / LEM_MM_XSTEP;
    lem_r_batch(1);

    if (erase_x0 == x0) {
        lem_r_batch(0);
        return;                     /* it has not moved: nothing at all */
    }

    if (erase_x0 >= 0) {
        /* BOTH old columns, whichever way it went (see the header) - unless
         * one of them is where a NEW column is about to go, which happens on a
         * jump of exactly LEM_VR_W-1 cells and would be 18 cells written
         * twice. */
        el = (erase_x0 != x0 + LEM_VR_W - 1);
        er = (erase_x0 + LEM_VR_W - 1 != x0);
        if (el)
            lem_mm_col(erase_x0, 1, LEM_MM_H - 2);
        if (er)
            lem_mm_col(erase_x0 + LEM_VR_W - 1, 1, LEM_MM_H - 2);
        /* ...and the part of the old top and bottom rows the new rectangle no
         * longer reaches, CLAMPED to the old rectangle so that a jump bigger
         * than the rectangle does not erase cells the draw below covers. */
        if (x0 > erase_x0) {
            c1 = x0 - 1;
            if (c1 > erase_x0 + LEM_VR_W - 1)
                c1 = erase_x0 + LEM_VR_W - 1;
            lem_vr_erase_span(0, erase_x0, c1);
            lem_vr_erase_span(LEM_MM_H - 1, erase_x0, c1);
        } else {
            c0 = x0 + LEM_VR_W;
            if (c0 < erase_x0)
                c0 = erase_x0;
            lem_vr_erase_span(0, c0, erase_x0 + LEM_VR_W - 1);
            lem_vr_erase_span(LEM_MM_H - 1, c0, erase_x0 + LEM_VR_W - 1);
        }
    }

    /* the two new columns, one call each rather than one per cell */
    lem_r_rect(LEM_MM_X + x0, LEM_MM_Y + 1, 1, LEM_MM_H - 2, LEM_C_VIEW, 1);
    lem_r_rect(LEM_MM_X + x0 + LEM_VR_W - 1, LEM_MM_Y + 1, 1, LEM_MM_H - 2,
               LEM_C_VIEW, 1);
    if (erase_x0 < 0) {
        lem_vr_draw_span(0, x0, x0 + LEM_VR_W - 1);
        lem_vr_draw_span(LEM_MM_H - 1, x0, x0 + LEM_VR_W - 1);
    } else if (x0 > erase_x0) {
        c0 = erase_x0 + LEM_VR_W;
        if (c0 < x0)
            c0 = x0;
        lem_vr_draw_span(0, c0, x0 + LEM_VR_W - 1);
        lem_vr_draw_span(LEM_MM_H - 1, c0, x0 + LEM_VR_W - 1);
    } else {
        c1 = erase_x0 - 1;
        if (c1 > x0 + LEM_VR_W - 1)
            c1 = x0 + LEM_VR_W - 1;
        lem_vr_draw_span(0, x0, c1);
        lem_vr_draw_span(LEM_MM_H - 1, x0, c1);
    }
    lem_r_batch(0);
}

/* ============================================================================
 * The status line (Lemmix src/Game.SkillPanel.pas, and its FIVE setters)
 * ==========================================================================*/

/* The 40-character template, and the five field WRITE offsets, are the
 * authority table's - Base.Strings.pas:351 for the template and
 * Game.SkillPanel.pas:501-563 for the offsets, which are NOT the label-
 * inclusive spans the comment block at :250-255 gives. In 0-based columns:
 *
 *   0..13   the word for the lemming under the cursor   PadRight 14
 *   18..22  OUT                                          PadRight 5
 *   26..30  IN                                           PadRight 5
 *   35..36  minutes                                      PadLeft  2
 *   38..39  seconds                                      PadLeft  2, '0'
 *
 * COLUMN 26 IS THE IN FIELD'S FIRST CELL AND COLUMN 31 IS NOT IN IT. The draft
 * plan said 26-30 for a field the setter writes at [26+i] for i = 1..5, which
 * is 27..31 one-based and 26..30 here - one column left of where a reader
 * would put it from the comment block. */
#define LEM_STAT_F0      0          /* the word field's column... */
#define LEM_STAT_N0     14          /* ...and its width (PadRight 14) */

static int lem_stat_have;           /* the template is on the glass */
static int lem_load_n;              /* how far the loading bar has got */

static void lem_status_template(void)
{
    unsigned seg, off;

    if (!lem_item(LEM_ITEM_STATFONT))
        return;
    seg = lem_far_seg;
    off = lem_far_off;
    /* THE TEMPLATE'S OWN '.' IS NOT A GLYPH. lem_f_index() has no arm for it,
     * so every one of those cells draws the fourth arm's 8x16 BLACK CELL -
     * which is exactly what Lemmix's DrawNewStr does with it, and is why the
     * status line reads as five fields with gaps rather than as dots. */
    lem_f_run(seg, off, 0, LEM_STAT_ROW, lem_str(LEMS_STATUS_TEMPLATE),
              LEM_STAT_CELLS);
    lem_stat_have = 1;
}

/* lem_status_field - `n` cells at column `col`, delta-drawn.
 *
 * ONE CALL PER FIELD AND NOT ONE PER CELL: lem_f_run() exists for the same
 * reason os88_font_run() does on the desktop side, and a cell is ~900 us of
 * plotting whoever draws it (lemfont.inc's header). */
static void lem_status_field(int col, const char *s, int n)
{
    if (!lem_stat_have)
        return;
    if (!lem_item(LEM_ITEM_STATFONT))
        return;
    lem_f_run(lem_far_seg, lem_far_off, col, LEM_STAT_ROW, s, n);
}

/* ============================================================================
 * The panel, and the whole first frame
 * ==========================================================================*/

static void lem_panel_draw(void)
{
    if (!lem_item(LEM_ITEM_PANEL))
        return;
    lem_r_panel(lem_far_seg, lem_far_off);
}

/* lem_frame_panel - THE PANEL AND A WORD, before the level is composed.
 *
 * COMPOSING A LEVEL IS ~25 SECONDS ON AN XT (SPEC.md 92.7.1 conclusion 2: 400
 * terrain pieces at 8.8 ms into the mask and 53.8 ms into four VGA planes),
 * plus ~4.0 s to sample the minimap and ~1.2 s to draw it. Inside SPEC.md 53's
 * bracket every kernel slot is refused, so nothing can be said and no toast can
 * appear: the first build pressed Enter and the machine looked dead for half a
 * minute. The panel is loaded and drawable before any of that and
 * lem_r_clearworld() covers only the view's height, so the bottom forty rows
 * can carry a message the whole time - which is what this does.
 *
 * The status TEMPLATE goes down here and once only; LOADING is written into the
 * first field (the 14-cell one the lemming-under-the-cursor word uses, Lemmix
 * Game.SkillPanel.pas:501) and blanked when the level is up, which is two
 * 14-cell field writes rather than a second 40-cell template.
 *
 * The font has no lowercase and no space glyph (lemfont.inc), so the word is
 * LOADING in capitals and the padding is the fourth arm's black cells - which
 * is what the original's own status line does with every short word. */
static void lem_frame_panel(void)
{
    /* THE VGA'S WINDOW AT THE LEVEL'S OWN VIEW, BEFORE A PIXEL OF TERRAIN.
     * lem_r_prep() pointed the CRTC Start Address at the world (lemblit.inc) -
     * without that the card displays VRAM 0 with a 200-byte row, which is the
     * panel twice - but it ran before os88_fsx_main() had read the LVL's
     * ScreenPosition, so it left the window at world x 0. On VGA the world IS
     * video memory and the compose is watched through this window, so leaving
     * it there would be twenty-five seconds of the level's far left followed
     * by a snap to where it actually opens.
     *
     * GUARDED ON THE KIND, and that is not tidiness. lem_r_scroll() CACHES on
     * the two shadow backends (lb_scroll) and skips the 12,800-byte compose
     * when the view has not moved; scrolling here AND in lem_frame_first()
     * there meant the second call did nothing and the shadow still held the
     * world as it was BEFORE the compose - a correct panel, a correct minimap
     * and a completely black world (build/port-shots/wave2f-cga-fun1.png,
     * before this). On VGA the scroll has no cache and no copy: it is four
     * `out`s. */
    if (lem_kind_id() == LEM_RKIND_VGA)
        lem_r_scroll(lem_view);
    lem_panel_draw();
    lem_stat_have = 0;
    lem_status_template();
    lem_status_field(LEM_STAT_F0, lem_str(LEMS_LOADING), LEM_STAT_N0);
    lem_load_n = 0;
    /* ...and PRESENT it, because on the two shadow backends everything above
     * went into the shadow and the glass is still the BIOS's black; the panel's
     * forty rows are marked dirty by the plotter that wrote them, so the blit
     * carries exactly them. On VGA the panel is already in VRAM and this is a
     * no-op.
     *
     * IT MUST NOT SCROLL ON THOSE TWO, which is the guard at the top of this
     * function and the whole reason it is a guard. */
    lem_r_present();
}

/* lem_load_bar - the compose's own progress, one cell at a time.
 *
 * ovl_compose() calls it every fourth terrain slot, which is ~100 cells over
 * the 400 - and a cell is one lb_rpix, against 53.8 ms for the piece beside it.
 * It is drawn in the minimap's own well, which is black at this point and which
 * lem_mm_draw() covers completely afterwards, so it needs no erase of its own.
 *
 * IT DOES NOT PRESENT. On VGA the well IS video memory and the bar is live; on
 * the two shadow backends a present is 178 ms (SPEC.md 92.7.1 row 7) and the
 * compose there is five seconds rather than twenty-five, so the panel and the
 * word above are what those cards show and the bar arrives with the first
 * frame. Paying 100 presents to animate a five-second wait is the trade this
 * refuses. */
static void lem_load_bar(int n)
{
    if (n <= lem_load_n || n >= LEM_MM_W)
        return;
    lem_r_rect(LEM_MM_X + lem_load_n, LEM_MM_Y + LEM_MM_H / 2,
               n - lem_load_n, 3, LEM_C_MAP, 1);
    lem_load_n = n;
}

/* lem_frame_first - everything that is drawn once, in the order it has to be.
 *
 * The panel went down in lem_frame_panel() before the compose; the minimap is
 * drawn INSIDE the panel's own bitmap, so it and the view rectangle and the
 * status line come after it. Nothing here is redrawn per frame - what a frame
 * costs is in lem_frame_step(). */
static void lem_frame_first(void)
{
    lem_mm_sample();
    lem_mm_draw();
    lem_view_drawn = -1;
    lem_view_rect(-1);
    lem_view_drawn = lem_view / LEM_MM_XSTEP;
    lem_status_field(LEM_STAT_F0, "", LEM_STAT_N0);   /* LOADING, taken down */
    lem_r_scroll(lem_view);
    lem_r_present();
}

/* lem_frame_step - one displayed frame.
 *
 * On VGA a scroll is four `out`s and this costs nothing at all; on the two
 * 1bpp adapters it is a windowed copy into the shadow and a blit of the rows
 * that changed (lemblit.inc). The view rectangle is redrawn only when the view
 * actually moved, which on a scroll-by-mouse-edge is most frames and on a
 * still screen is none. */
static void lem_frame_step(void)
{
    /* IT IS COMPARED IN MINIMAP CELLS AND NOT IN WORLD PIXELS. A cell is
     * sixteen world pixels, so a four-pixel scroll step moves the rectangle
     * one cell in four - and redrawing it on the other three would erase and
     * re-draw 76 cells to put them back exactly where they were. */
    if (lem_view / LEM_MM_XSTEP != lem_view_drawn) {
        lem_view_rect(lem_view_drawn);
        lem_view_drawn = lem_view / LEM_MM_XSTEP;
    }
    lem_r_scroll(lem_view);
    lem_r_present();
}

/* lem_view_move - the view, clamped to the world. The step is what the SCROLL
 * costs on this adapter, not what the game wants: VGA pans one pixel at a time
 * in hardware and the other two snap to a byte (lemblit.inc's header says why),
 * so asking for one pixel there simply does nothing until the fourth or eighth
 * ask. Passing the raw number through keeps that in ONE place. */
static void lem_view_move(int dx)
{
    lem_view += dx;
    if (lem_view < 0)
        lem_view = 0;
    if (lem_view > LEM_WORLD_W - LEM_VIEW_W)
        lem_view = LEM_WORLD_W - LEM_VIEW_W;
}

/* lem_mouse_map - the desktop pointer into game raster coordinates.
 *
 * The mouse ISR keeps running inside the bracket and os88_mouse() keeps
 * answering in DESKTOP pixels - the kernel's [vid_*] block is never touched by
 * a mode change (kernel/fsx.inc's own header) - so the mapping is per adapter
 * and it is this program's to do:
 *
 *   VGA        640x480 desktop over a 320x200 mode      x/2, y*5/12
 *   CGA        640x200 desktop over a 320x200 mode      x/2, y
 *   Hercules   720x348 desktop, the box at (40, 74)     (x-40)/2, y-74
 *
 * It answers through two statics, because half this API is out-parameters and
 * every one of them is a static (rule 1). */
static int lem_mx, lem_my;

static void lem_mouse_map(void)
{
    static struct os88_mouse m;

    os88_mouse(&m);
    if (lem_vidkind == OS88_VID_HERC) {
        lem_mx = (m.x - 40) >> 1;
        lem_my = m.y - 74;
    } else if (lem_vidkind == OS88_VID_CGA) {
        lem_mx = m.x >> 1;
        lem_my = m.y;
    } else {
        lem_mx = m.x >> 1;
        lem_my = (m.y * 5) / 12;
    }
    if (lem_mx < 0)
        lem_mx = 0;
    if (lem_mx > LEM_VIEW_W - 1)
        lem_mx = LEM_VIEW_W - 1;
    if (lem_my < 0)
        lem_my = 0;
    if (lem_my > 199)
        lem_my = 199;
}
