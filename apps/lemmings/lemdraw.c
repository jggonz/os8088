/* ============================================================================
 * os8088 - apps/lemmings/lemdraw.c   (#included by lemmings.c)
 *
 * THE FRAME (SPEC.md 116.4). What is drawn and when; lemblit.inc, lemmask.inc
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
 * and the 40-character status line in MAIN.DAT's own green font. WAVE 3 adds
 * the lemmings, the objects, the panel's live counts, the selection frame, the
 * minimap's lemming dots and the app's own 14x14 cursor - and wave 2 did NOT
 * bench a sprite path, whatever an earlier version of this paragraph said
 * (tests/lembench/lembench.c says so in its own words); the sprite rows arrive
 * in tests/lembench with the sprites, in wave 3.
 *
 * THE MINIMAP HAS A SHADOW OF ITS OWN, 104 x 20 bits = 260 bytes, and it is
 * not an optimisation waiting to happen: the VIEW RECTANGLE is drawn INSIDE
 * the minimap and has to be erased when it moves, and what is under it is the
 * minimap's own cells. Re-sampling them off the world would be sixteen probes
 * a cell, which is what sampling the whole minimap costs and is 4.0 s on a
 * 4.77 MHz 8088 (SPEC.md 116.7.1: 7,128 lem_probe4 calls at 563 us) - per
 * scroll step.
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
/* THE LOADING BAR'S SHARES OF THE WELL: the compose walks 0..79 and the
 * minimap sample the 18 cells after it, so the bar runs to the end of the
 * whole wait rather than stopping a quarter of it early. */
#define LEM_LB_SAMPLE   82
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
 * 1,584-pixel level. §116.4.4. */
#define LEM_C_MAP1BPP    8

/* ...and the LEMMINGS' DOTS are entry 2, green - lemmings_3ds src/draw.c's
 * draw_lemmings_minimap (`u8 color = 2;`, "green (or blue)") and Lemmix's
 * Color32(0, 255, 0) in DrawLemmings. On the two paletteless cards 2 is the
 * solid class, the opposite of the map's, so a dot reads on both. */
#define LEM_C_DOT        2

static int ovl_c_map(void)
{
    return lem_kind_id() == LEM_RKIND_VGA ? LEM_C_MAP : LEM_C_MAP1BPP;
}

static unsigned char lem_mm[LEM_MM_H * LEM_MM_STRIDE];
static unsigned char lem_mmd[LEM_MM_H * LEM_MM_STRIDE];   /* the dots on it */
static unsigned char lem_mmn[LEM_MM_H * LEM_MM_STRIDE];   /* ...and this
                                                           * frame's, before
                                                           * they are drawn */
static int lem_view;                /* the view's left edge, in world pixels */
static int lem_mx, lem_my;          /* the pointer, in SCREEN pixels: 0..319,
                                     * and 160.. is the panel (ovl_mouse_map) */
#define LEM_VIEWH_PX   160          /* the screen's world rows; the panel
                                     * is the forty below them */
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

/* ovl_level_load - the banks this level needs, into their claims.
 *
 * IT IS THE FILE HALF AND NOTHING ELSE. On a launch it runs OUTSIDE the
 * bracket (ovl_launch); since wave 5 it also runs INSIDE it, for the session's
 * next level or retry (lemmings.c ovl_next_level, SPEC.md 116.4.3) - legal,
 * because the file and mem slots are legal throughout a bracket (SPEC.md
 * 53.7). Either way a refusal DRAWS NOTHING, because nothing can be said in a
 * foreign mode: it sets lem_load_err and returns 0, and the window says it
 * after the bracket (ovl_launch's LEM_X_LOAD arm -> ovl_said_load).
 *
 * 0 = something is missing, and the caller says which in the window - WHICH is
 * lem_load_err, because the two refusals are different sentences (SPEC.md 47:
 * a refusal names the fact it refused on) and a bare status cannot carry that.
 *
 * IT IS THE ONLY COPY OF THIS SEQUENCE. ovl_launch() used to open-code the
 * identical three calls, so the harness exercised this function and the package
 * exercised that copy, and this one was emitted resident and never called -
 * SmallerC does not drop an unreferenced static. Two copies of one sequence
 * drift the moment either grows a step (a sixth claim, a retry, a refusal). */
/* (LEM_LE_* and lem_load_err are lemload.c's: ovl_main_read sets the third) */

static int ovl_level_load(int level)
{
    int spec;

    lem_load_err = LEM_LE_MEM;
    if (!ovl_claim_all())
        return 0;
    lem_load_err = LEM_LE_BANK;
    if (!ovl_main_read())
        return 0;
    /* A SPECIAL LEVEL READS ITS PICTURE HERE AND ITS STYLE BANK LATER: both
     * go in the one bank claim, in that order (ovl_spec_read) */
    spec = lem_ent_u8(level, LEM_E_SPECIAL);
    if (spec != 0xFF) {
        if (!ovl_spec_read(spec))
            return 0;
    } else if (!ovl_bank_read(lem_ent_u8(level, LEM_E_STYLE))) {
        return 0;
    }
    /* ...AND THE RECORD, which wave 2 read inside the bracket (ovl_compose):
     * a refusal there could not be said (finding (1) of its last review) */
    if (!ovl_rec_read(level))
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
 * down between them (ovl_frame_panel), and on the two shadow backends the
 * present that puts them there composes the world window - which has to be a
 * cleared world and not a fresh claim's contents. */
static void lem_world_clear(int vram)
{
    lem_m_setup(lem_cl_mask);
    lem_m_clear();
    lem_r_clearworld(vram);
}

/* 1 = composed; 0 = the caller leaves the bracket and the window says so */
static int lem_compose_level(int level)
{
    return ovl_compose(level);
}

/* ============================================================================
 * The minimap (lemmings_3ds/doc/data/minimap.txt)
 * ==========================================================================*/

/* ovl_mm_sample - the whole minimap into its own 260-byte shadow.
 *
 * 99 cells by 18 rows, sixteen probes each - which through lem_probe4() is
 * 7,128 calls rather than 28,512, and THAT is what the batched probe entry was
 * built for (lemmask.inc's header). It is 7,128 x 563 us = 4.0 s on a
 * 4.77 MHz 8088, once per level, against 28,512 single probes at 192 us =
 * 5.5 s (SPEC.md 116.7.1 conclusion 1: the batch saves 27%, not 75%). */
static void ovl_mm_sample(void)
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
            if (mx == 0)
                ovl_load_bar(LEM_LB_SAMPLE + my);   /* the sample's own share
                                                     * of the well: 4.0 s on
                                                     * an XT and the longest
                                                     * wait after the compose
                                                     * (wave 2's review) */
            n += lem_pop4[lem_probe4(wx + 4, wy, lem_off4)];
            n += lem_pop4[lem_probe4(wx + 8, wy, lem_off4)];
            n += lem_pop4[lem_probe4(wx + 12, wy, lem_off4)];
            if (n >= LEM_MM_THRESH)
                lem_mm[my * LEM_MM_STRIDE + (mx >> 3)] |=
                    (unsigned char)(0x80 >> (mx & 7));
        }
    }
}

/* ovl_mm_bit - what minimap cell (mx, my) shows: 0 black, 1 terrain, 2 a
 * lemming's dot. THE DOTS ARE OVER THE TERRAIN AND UNDER THE VIEW RECTANGLE,
 * which is Lemmix's order (DrawLemmings paints them into the minimap buffer,
 * DrawMinimap frames the rectangle over it) - so every routine that puts a
 * cell back asks this one question and gets the dot back with it. */
static int ovl_mm_bit(int mx, int my)
{
    int b;

    if (mx < 0 || mx >= LEM_MM_W || my < 0 || my >= LEM_MM_ROWS)
        return 0;
    b = my * LEM_MM_STRIDE + (mx >> 3);
    if ((lem_mmd[b] >> (7 - (mx & 7))) & 1)
        return 2;
    return (lem_mm[b] >> (7 - (mx & 7))) & 1;
}

/* ovl_mm_ink - the colour of a ovl_mm_bit() answer. */
static int ovl_mm_ink(int v, int on)
{
    if (v == 2)
        return LEM_C_DOT;
    return v ? on : LEM_C_BLACK;
}

/* ovl_mm_row - one minimap row's cells, from column c0 to c1 inclusive, out of
 * the shadow. It is what erases the view rectangle when it moves: what is
 * under a view rectangle is exactly this.
 *
 * `my` MAY BE -1 OR LEM_MM_ROWS..: the rectangle is twenty rows and the sample
 * is eighteen inset by one (LEM_MM_YOFF), so its top and bottom edges sit on
 * panel rows no cell was ever drawn on. ovl_mm_bit() answers 0 outside the
 * sampled rows, which is what those two rows are: black. */
static void ovl_mm_row(int my, int c0, int c1)
{
    int mx, run0, cur, v, on;

    /* ONCE PER ROW AND NOT ONCE PER RUN. ovl_c_map() is two near calls and a
     * compare, which is nothing beside a run's own lem_r_rect - but a run is
     * 150-550 us (§116.7.1) and a per-run call would put a fifth of that back
     * on the one path this wave measured. */
    on = ovl_c_map();
    if (c0 < 0)
        c0 = 0;
    if (c1 > LEM_MM_W - 1)
        c1 = LEM_MM_W - 1;
    if (c1 < c0)
        return;
    run0 = c0;
    cur = ovl_mm_bit(c0, my);
    for (mx = c0 + 1; mx <= c1 + 1; mx++) {
        v = (mx <= c1) ? ovl_mm_bit(mx, my) : -1;
        if (v == cur)
            continue;
        lem_r_rect(LEM_MM_X + run0, LEM_MM_Y + LEM_MM_YOFF + my,
                   mx - run0, 1, ovl_mm_ink(cur, on), 1);
        run0 = mx;
        cur = v;
    }
}

/* ovl_mm_col - the same thing down a COLUMN, in RECTANGLE rows.
 *
 * IT IS A RUN AND NOT A CELL, AND THE BENCH IS WHY. SPEC.md 116.7.1 measured a
 * bracketed one-cell `lem_r_rect` at 548 us and a plot INSIDE one call at
 * 153 us: the SmallerC cdecl boundary is three quarters of a minimap cell, so
 * the batch that took the VGA's ten `out`s a pixel down to two barely moved a
 * per-cell caller. Coalescing runs of one colour is what actually pays - a
 * minimap column of eighteen cells is typically three or four runs, so the
 * view rectangle's two erased columns go from 36 calls to about 8. */
static void ovl_mm_col(int mx, int r0, int r1)
{
    int r, run0, cur, v, on;

    on = ovl_c_map();               /* once per column - see ovl_mm_row */
    if (mx < 0 || mx >= LEM_MM_W || r1 < r0)
        return;
    run0 = r0;
    cur = ovl_mm_bit(mx, r0 - LEM_MM_YOFF);
    for (r = r0 + 1; r <= r1 + 1; r++) {
        v = (r <= r1) ? ovl_mm_bit(mx, r - LEM_MM_YOFF) : -1;
        if (v == cur)
            continue;
        lem_r_rect(LEM_MM_X + mx, LEM_MM_Y + run0, 1, r - run0,
                   ovl_mm_ink(cur, on), 1);
        run0 = r;
        cur = v;
    }
}

static void ovl_mm_draw(void)
{
    int my;

    lem_r_batch(1);
    for (my = 0; my < LEM_MM_ROWS; my++)
        ovl_mm_row(my, 0, LEM_MM_W - 1);
    lem_r_batch(0);
}

/* ovl_view_rect - the view rectangle inside the minimap, moved.
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
static void ovl_vr_erase_span(int r, int c0, int c1)
{
    if (c1 >= c0)
        ovl_mm_row(r - LEM_MM_YOFF, c0, c1);
}

static void ovl_vr_draw_span(int r, int c0, int c1)
{
    if (c1 >= c0)
        lem_r_rect(LEM_MM_X + c0, LEM_MM_Y + r, c1 - c0 + 1, 1,
                   LEM_C_VIEW, 1);
}

static void ovl_view_rect(int erase_x0)
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
            ovl_mm_col(erase_x0, 1, LEM_MM_H - 2);
        if (er)
            ovl_mm_col(erase_x0 + LEM_VR_W - 1, 1, LEM_MM_H - 2);
        /* ...and the part of the old top and bottom rows the new rectangle no
         * longer reaches, CLAMPED to the old rectangle so that a jump bigger
         * than the rectangle does not erase cells the draw below covers. */
        if (x0 > erase_x0) {
            c1 = x0 - 1;
            if (c1 > erase_x0 + LEM_VR_W - 1)
                c1 = erase_x0 + LEM_VR_W - 1;
            ovl_vr_erase_span(0, erase_x0, c1);
            ovl_vr_erase_span(LEM_MM_H - 1, erase_x0, c1);
        } else {
            c0 = x0 + LEM_VR_W;
            if (c0 < erase_x0)
                c0 = erase_x0;
            ovl_vr_erase_span(0, c0, erase_x0 + LEM_VR_W - 1);
            ovl_vr_erase_span(LEM_MM_H - 1, c0, erase_x0 + LEM_VR_W - 1);
        }
    }

    /* the two new columns, one call each rather than one per cell */
    lem_r_rect(LEM_MM_X + x0, LEM_MM_Y + 1, 1, LEM_MM_H - 2, LEM_C_VIEW, 1);
    lem_r_rect(LEM_MM_X + x0 + LEM_VR_W - 1, LEM_MM_Y + 1, 1, LEM_MM_H - 2,
               LEM_C_VIEW, 1);
    if (erase_x0 < 0) {
        ovl_vr_draw_span(0, x0, x0 + LEM_VR_W - 1);
        ovl_vr_draw_span(LEM_MM_H - 1, x0, x0 + LEM_VR_W - 1);
    } else if (x0 > erase_x0) {
        c0 = erase_x0 + LEM_VR_W;
        if (c0 < x0)
            c0 = x0;
        ovl_vr_draw_span(0, c0, x0 + LEM_VR_W - 1);
        ovl_vr_draw_span(LEM_MM_H - 1, c0, x0 + LEM_VR_W - 1);
    } else {
        c1 = erase_x0 - 1;
        if (c1 > x0 + LEM_VR_W - 1)
            c1 = x0 + LEM_VR_W - 1;
        ovl_vr_draw_span(0, x0, c1);
        ovl_vr_draw_span(LEM_MM_H - 1, x0, c1);
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

static void ovl_status_template(void)
{
    unsigned seg, off;

    if (!ovl_item(LEM_ITEM_STATFONT))
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

/* ovl_status_field - `n` cells at column `col`, delta-drawn.
 *
 * ONE CALL PER FIELD AND NOT ONE PER CELL: lem_f_run() exists for the same
 * reason os88_font_run() does on the desktop side, and a cell is ~900 us of
 * plotting whoever draws it (lemfont.inc's header). */
static void ovl_status_field(int col, const char *s, int n)
{
    if (!lem_stat_have)
        return;
    if (!ovl_item(LEM_ITEM_STATFONT))
        return;
    lem_f_run(lem_far_seg, lem_far_off, col, LEM_STAT_ROW, s, n);
}

/* ============================================================================
 * The panel, and the whole first frame
 * ==========================================================================*/

static void ovl_panel_draw(void)
{
    if (!ovl_item(LEM_ITEM_PANEL))
        return;
    lem_r_panel(lem_far_seg, lem_far_off);
}

/* ovl_frame_panel - THE PANEL AND A WORD, before the level is composed.
 *
 * COMPOSING A LEVEL WAS ~25 SECONDS ON AN XT (SPEC.md 116.7.1 conclusion 2: 400
 * terrain pieces at 8.8 ms into the mask and 53.8 ms into four VGA planes) and
 * wave 3's RAM composer took that to 22.0 ms a piece and ~8.9 s for Fun 1
 * whole (SPEC.md 116.7.2), plus
 * ~4.0 s to sample the minimap and ~0.4 s to draw it (conclusion 7). Inside SPEC.md 53's
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
static void ovl_frame_panel(void)
{
    /* THE VGA'S WINDOW AT THE LEVEL'S OWN VIEW, BEFORE A PIXEL OF TERRAIN.
     * lem_r_prep() pointed the CRTC Start Address at the world (lemblit.inc) -
     * without that the card displays VRAM 0 with a 200-byte row, which is the
     * panel twice - but it ran before lem_fsx_main() had read the LVL's
     * ScreenPosition, so it left the window at world x 0. On VGA the world IS
     * video memory and the compose is watched through this window, so leaving
     * it there would be twenty-five seconds of the level's far left followed
     * by a snap to where it actually opens.
     *
     * GUARDED ON THE KIND, and that is not tidiness. lem_r_scroll() CACHES on
     * the two shadow backends (lb_scroll) and skips the 12,800-byte compose
     * when the view has not moved; scrolling here AND in ovl_frame_first()
     * there meant the second call did nothing and the shadow still held the
     * world as it was BEFORE the compose - a correct panel, a correct minimap
     * and a completely black world (build/port-shots/wave2f-cga-fun1.png,
     * before this). On VGA the scroll has no cache and no copy: it is four
     * `out`s. */
    if (lem_kind_id() == LEM_RKIND_VGA)
        lem_r_scroll(lem_view);
    ovl_panel_draw();
    lem_stat_have = 0;
    ovl_status_template();
    ovl_status_field(LEM_STAT_F0, lem_str(LEMS_LOADING), LEM_STAT_N0);
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

/* ovl_load_bar - the compose's own progress, one cell at a time.
 *
 * ovl_compose() calls it every fourth terrain slot, which is ~100 cells over
 * the 400 - and a cell is one lb_rpix, against 22.0 ms for the piece beside it
 * (SPEC.md 116.7.2).
 * It is drawn in the minimap's own well, which is black at this point and which
 * ovl_mm_draw() covers completely afterwards, so it needs no erase of its own.
 *
 * IT DOES NOT PRESENT. On VGA the well IS video memory and the bar is live; on
 * the two shadow backends a present is 178 ms (SPEC.md 116.7.1 row 7) and the
 * compose there is five seconds rather than twenty-five, so the panel and the
 * word above are what those cards show and the bar arrives with the first
 * frame. Paying 100 presents to animate a five-second wait is the trade this
 * refuses. */
static void ovl_load_bar(int n)
{
    int b0, b1;

    /* ON THE PREVIEW SCREEN (wave 5) the compose runs under the original's
     * own screen, and the bar is in the level picture's black well, which the
     * picture covers afterwards: four rows across the middle of it in the
     * font's lightest purple, a byte column at a time (lem_p_rect is byte
     * aligned), the minimap well's 104 steps scaled to the screen's 80 bytes.
     * Every card draws it - the surface is the framebuffer, there is no
     * shadow to present - so a CGA and a Hercules have a bar now too. */
    if (lem_onscr) {
        if (n <= lem_load_n || n > LEM_MM_W)
            return;
        b0 = (lem_load_n * 80) / LEM_MM_W;
        b1 = (n * 80) / LEM_MM_W;
        if (b1 > b0)
            lem_p_rect(b0 << 3, 38, (b1 - b0) << 3, 4, LEM_SCR_INK);
        lem_load_n = n;
        return;
    }
    /* VGA ONLY, which is finding (a) of wave 2's last review: on the two
     * shadow backends the bar's cells went into the shadow and ovl_mm_draw()
     * covered every one of them before the first present, so ~100 plots a
     * level reached no glass at all. The panel and LOADING are what those
     * cards show, and the compose there is the mask alone. */
    if (lem_kind_id() != LEM_RKIND_VGA) {
        lem_load_n = n;
        return;
    }
    if (n <= lem_load_n || n >= LEM_MM_W)
        return;
    lem_r_rect(LEM_MM_X + lem_load_n, LEM_MM_Y + LEM_MM_H / 2,
               n - lem_load_n, 3, LEM_C_MAP, 1);
    lem_load_n = n;
}

/* ============================================================================
 * WAVE 3: THE LIVE FRAME (SPEC.md 116.4.6)
 * ==========================================================================*/

/* the per-frame cost counters the host harness prints (LESSONS.md 6) */
#ifdef LEM_HOST
static int lem_c_sprites;           /* sprites drawn this frame */
#endif
static int lem_c_objects;           /* objects drawn again this frame */
static int lem_scrolled = -1;       /* the view the glass shows */

/* --- the destruction a game frame did, put right at its end ---------------
 * lemact.c writes the MASK the instant a lemming digs, and records the
 * rectangle here; the PICTURE is put right when the frame is drawn (the
 * frame's compose, ovl_vframe, or the shadow's rebuild of W). Sixteen
 * rectangles, and the seventeenth is FOLDED into the one whose union grows
 * least (ovl_dmg_fold) - a nuke on a full level is eighty, and one union is a
 * few more background bytes, never a lost crater. It used to fold into slot 15
 * whatever the distance, so a digger at x 40 and one at x 1,500 became one
 * rectangle the width of the level, synced byte by byte. */
#define LEM_NDMG 16
static int lem_dmg[LEM_NDMG * 4];
static int lem_ndmg;

static void lem_damage(int x, int y, int w, int h)
{
    int *d;

    if (lem_ndmg >= LEM_NDMG) {
        ovl_dmg_fold(x, y, w, h);
        return;
    }
    d = &lem_dmg[lem_ndmg * 4];
    d[0] = x;
    d[1] = y;
    d[2] = w;
    d[3] = h;
    lem_ndmg++;
}

/* ovl_mm_vr - is minimap cell (mx, my) on the view rectangle's OUTLINE, which
 * stays on top of everything the minimap shows? */
static int ovl_mm_vr(int mx, int my)
{
    int x0 = lem_view / LEM_MM_XSTEP;
    int r = my + LEM_MM_YOFF;

    if (mx < x0 || mx > x0 + LEM_VR_W - 1)
        return 0;
    return mx == x0 || mx == x0 + LEM_VR_W - 1 || r == 0 || r == LEM_MM_H - 1;
}

/* ovl_mm_cell - one minimap cell redrawn from what it now shows. */
static void ovl_mm_cell(int mx, int my)
{
    if (mx < 0 || mx >= LEM_MM_W || my < 0 || my >= LEM_MM_ROWS)
        return;
    if (ovl_mm_vr(mx, my))
        return;
    lem_r_rect(LEM_MM_X + mx, LEM_MM_Y + LEM_MM_YOFF + my, 1, 1,
               ovl_mm_ink(ovl_mm_bit(mx, my), ovl_c_map()), 1);
}

/* ovl_mm_clear - Lemmix's "fake draw mask in fMiniMap": the cell under a
 * digger, basher or miner loses its terrain, and an explosion's 2x2 around it
 * (FillRectS(X - 1, Y - 1, X + 1, Y + 1), exclusive). The cell is the
 * minimap's own convention (lemdraw.c LEM_MM_YOFF): world row y is minimap row
 * y / 8 - 2.
 *
 * IT DRAWS NOTHING, and it used to. It is called from the MECHANICS
 * (lemact.c), mid-frame, before ovl_tick_draw() has decided anything about the
 * pointer - so a plot under a lit pointer went into the pointer's save-under
 * window and the restore put the old cell back, for good: the bit was already
 * clear and nothing would ever draw that cell again. And a digger called it on
 * every row of one 16x8 cell, a 721 us plot each time that changed nothing.
 * Now a cell whose bit actually CHANGES is marked in lem_mmc, and ovl_mm_dots()
 * draws the marked cells with the dots, at the end of the frame, inside the
 * pointer's own bracket (SPEC.md 116.4.6). */
static unsigned char lem_mmc[LEM_MM_H * LEM_MM_STRIDE];   /* cells owed */
static int lem_mmpend;              /* ...and whether any is */

static void ovl_mm_clear(int x, int y, int big)
{
    int mx, my, i, j, k;
    unsigned char bit;

    if (x < 0)
        x = 0;
    if (y < 16)
        y = 16;
    mx = x / LEM_MM_XSTEP;
    my = y / LEM_MM_YSTEP - 2;
    for (j = big ? -1 : 0; j <= 0; j++)
        for (i = big ? -1 : 0; i <= 0; i++) {
            if (mx + i < 0 || mx + i >= LEM_MM_W || my + j < 0 ||
                my + j >= LEM_MM_ROWS)
                continue;
            k = (my + j) * LEM_MM_STRIDE + ((mx + i) >> 3);
            bit = (unsigned char)(0x80 >> ((mx + i) & 7));
            if (!(lem_mm[k] & bit))
                continue;           /* already bare: nothing to draw */
            lem_mm[k] &= (unsigned char)~bit;
            lem_mmc[k] |= bit;
            lem_mmpend = 1;
        }
}

/* ovl_mm_dots - the lemmings' dots, delta-drawn: this frame's set is built in
 * lem_mmn, and only the cells where it differs from lem_mmd are drawn - and
 * with them every cell ovl_mm_clear() marked in lem_mmc. A walker crosses a
 * cell every sixteen frames, so a frame of sixty lemmings is a handful of
 * plots rather than sixty. */
static void ovl_mm_dots(void)
{
    ovl_mm_build();
    ovl_mm_cells();
}

/* ovl_mm_build - this frame's dots into lem_mmn, drawing nothing: the
 * pointer's panel test (ovl_pan_hit) has to know which cells are about to be
 * drawn BEFORE they are */
static void ovl_mm_build(void)
{
    int i, mx, my, x, y;
    struct lem_l *p;

    for (i = 0; i < LEM_MM_H * LEM_MM_STRIDE; i++)
        lem_mmn[i] = 0;
    for (i = 0; i < lem_released; i++) {
        p = &lem_lm[i];
        if (p->fl & LF_GONE)
            continue;
        x = p->x;
        y = p->y;
        if (y >= LEM_WORLD_H || x >= 1664)
            continue;               /* lemmings_3ds: "or: 1584?" - off the map */
        if (x < 0)
            x = 0;
        if (y < 16)
            y = 16;
        mx = x / LEM_MM_XSTEP;
        my = y / LEM_MM_YSTEP - 2;
        if (mx >= LEM_MM_W || my >= LEM_MM_ROWS)
            continue;
        lem_mmn[my * LEM_MM_STRIDE + (mx >> 3)] |=
            (unsigned char)(0x80 >> (mx & 7));
    }
}

/* ovl_mm_cells - every cell whose dot came or went, and every cell the
 * mechanics cleared (lem_mmc), drawn */
static void ovl_mm_cells(void)
{
    int b, bit;
    unsigned char d;

    lem_r_batch(1);
    for (b = 0; b < LEM_MM_ROWS * LEM_MM_STRIDE; b++) {
        d = (unsigned char)((lem_mmn[b] ^ lem_mmd[b]) | lem_mmc[b]);
        if (d == 0)
            continue;
        lem_mmd[b] = lem_mmn[b];
        lem_mmc[b] = 0;
        for (bit = 0; bit < 8; bit++)
            if (d & (0x80 >> bit))
                ovl_mm_cell(((b % LEM_MM_STRIDE) << 3) + bit,
                            b / LEM_MM_STRIDE);
    }
    lem_r_batch(0);
    lem_mmpend = 0;
}

/* --- the panel's live parts (Lemmix src/Game.SkillPanel.pas) --------------- */

/* the counts and the selection frame that have to be drawn again */
static unsigned int lem_cdirty;     /* bit b = button b's count */
static int lem_seldrawn = -1;       /* the button the frame is on the glass at */
static int lem_tmin, lem_tsec;      /* the clock AS SHOWN: it moves on the
                                     * frame after the second does (:3517) */
static char lem_stnew[LEM_STAT_CELLS + 1];
static char lem_stold[LEM_STAT_CELLS + 1];
static unsigned char lem_dig8[8];

static void ovl_dirty_count(int b)
{
    lem_cdirty |= 1 << b;
}

static void ovl_dirty_sel(int b)
{
    (void)b;                        /* ovl_panel_live() compares the two */
}

static void lem_dirty_stat(void)
{
}

static void ovl_stat_time(void)
{
    lem_tmin = lem_min;
    lem_tsec = lem_sec;
}

/* ovl_count_draw - DrawSkillCount (:295): two digits, the left half of the
 * left one's glyph and the right half of the right one's, OR'd into one 8x8
 * cell at (16b + 4, 17); a zero count is a solid block of colour 3. The digit
 * glyphs are MAIN section 2's twenty, in right/left pairs: glyph 2d is digit
 * d's right half and 2d + 1 its left. */
static void ovl_count_draw(int b)
{
    int n, lo, hi, r;
    unsigned seg, off;

    if (b == LB_SLOWER)
        n = lem_rrmin;
    else if (b == LB_FASTER)
        n = lem_rr;
    else if (b >= LB_CLIMBER && b <= LB_DIGGER)
        n = lem_cnt[b - LB_CLIMBER];
    else
        return;
    if (n < 0)
        n = 0;
    if (n > 99)
        n = 99;
    if (n == 0) {
        lem_r_rect(b * 16 + 4, 17, 8, 8, 3, 1);
        return;
    }
    if (!ovl_item(LEM_ITEM_DIGITS))
        return;
    seg = lem_far_seg;
    off = lem_far_off;
    hi = n / 10;
    lo = n % 10;
    for (r = 0; r < 8; r++)
        lem_dig8[r] = (unsigned char)
            ((ovl_pk(seg, off + ((hi * 2 + 1) << 3) + r) & 0xF0) |
             (ovl_pk(seg, off + ((lo * 2) << 3) + r) & 0x0F));
    lem_r_glyph8(lem_dig8, b * 16 + 4, 17, 8, 3, 0);
}

/* ovl_sel_draw - DrawButtonSelector (:192): the frame is the button's rect
 * one wider and two taller, in the panel's white; taking it down copies the
 * original's four edges back. */
static void ovl_sel_draw(int b, int on)
{
    if (b < 0)
        return;
    if (on) {
        lem_r_rect(b * 16 + 1, 16, 15, 24, 3, 0);
        return;
    }
    if (ovl_item(LEM_ITEM_PANEL))
        lem_r_panrect(lem_far_seg, lem_far_off, b * 16 + 1, 16, 15, 24);
}

/* lem_pad - n characters of `s` into dst, padded RIGHT with spaces. */
static void ovl_padr(char *dst, const char *s, int n)
{
    int i = 0;

    while (i < n && s[i]) {
        dst[i] = s[i];
        i++;
    }
    while (i < n)
        dst[i++] = ' ';
}

/* ovl_stat_build - the forty cells as the five setters leave them (Lemmix
 * src/Game.SkillPanel.pas:501-563), over the template: the word and the hit
 * count PadRight 14 at 0, OUT PadRight 5 at 18, IN as a per cent PadRight 5
 * at 26, the minutes PadLeft 2 at 35 and the seconds PadLeft 2 with '0' at
 * 38. Everything stays upper case, because the font has nothing else. */
static void ovl_stat_build(void)
{
    const char *t;
    int n, i;

    t = lem_str(LEMS_STATUS_TEMPLATE);
    for (i = 0; i < LEM_STAT_CELLS; i++)
        lem_stnew[i] = t[i] ? t[i] : '.';
    lem_stnew[LEM_STAT_CELLS] = 0;
    if (lem_hit_n > 0 && lem_hit_l1 >= 0) {
        t = lem_str(ovl_word(lem_hit_l1));
        i = 0;
        while (t[i] && i < 12) {
            lem_line[i] = t[i];
            i++;
        }
        lem_line[i++] = ' ';
        os88_utoa((unsigned)lem_hit_n, lem_line + i);
        ovl_padr(lem_stnew, lem_line, 14);
    } else {
        ovl_padr(lem_stnew, "", 14);
    }
    os88_utoa((unsigned)lem_out, lem_num);
    ovl_padr(lem_stnew + 18, lem_num, 5);
    /* IN AS LEMMIX LAST SET IT, not as it would read now: SetInfoLemmingsSaved
     * is called at the level's start (Game.pas:1635, 0 of 1) and when an
     * exiter is removed (HandleExiting :3308) and nowhere else, so a nuke -
     * which moves MaxNumLemmings (the NUKE GLITCH, R8) - leaves the field
     * where the last exit put it. lem_inpct is those two setters' answer. */
    os88_utoa((unsigned)lem_inpct, lem_num);
    n = (int)os88_strlen(lem_num);
    lem_num[n] = '%';
    lem_num[n + 1] = 0;
    ovl_padr(lem_stnew + 26, lem_num, 5);
    lem_stnew[35] = (char)(lem_tmin >= 10 ? '0' + lem_tmin / 10 : ' ');
    lem_stnew[36] = (char)('0' + lem_tmin % 10);
    lem_stnew[38] = (char)('0' + lem_tsec / 10);
    lem_stnew[39] = (char)('0' + lem_tsec % 10);
}

/* ovl_stat_delta - DrawNewStr's rule: only the cells that CHANGED are drawn,
 * one lem_f_run per changed RUN. An ordinary tick changes the clock or a
 * count and nothing else. It draws lem_stnew AS BUILT - ovl_tick_draw() has
 * already built it once this tick to ask whether the panel changed, and the
 * second build was three divisions and a 40-byte copy for nothing. */
static void ovl_stat_delta(void)
{
    int i, j;

    i = 0;
    while (i < LEM_STAT_CELLS) {
        if (lem_stnew[i] == lem_stold[i]) {
            i++;
            continue;
        }
        j = i;
        while (j < LEM_STAT_CELLS && lem_stnew[j] != lem_stold[j])
            j++;
        ovl_status_field(i, lem_stnew + i, j - i);
        while (i < j) {
            lem_stold[i] = lem_stnew[i];
            i++;
        }
    }
}

/* ovl_panel_live - the counts that changed, the frame if it moved, the line
 * (lem_stnew, as the caller built it). */
static void ovl_panel_live(void)
{
    int b;

    for (b = 0; b < LB_COUNT; b++)
        if (lem_cdirty & (1 << b))
            ovl_count_draw(b);
    lem_cdirty = 0;
    if (lem_seldrawn != lem_skill) {
        ovl_sel_draw(lem_seldrawn, 0);
        ovl_sel_draw(lem_skill, 1);
        lem_seldrawn = lem_skill;
    }
    ovl_stat_delta();
}

/* --- the sprites ------------------------------------------------------------ */

/* ovl_sprite_one - lemming i's frame, and its countdown over its head
 * (TLemming.GetCountDownDigitBounds: x - 1, y + FrameTopDy - 12; the digit is
 * 5 for 65..79 down to 1 for 0..16 - DrawLemmings :2620). */
static void ovl_sprite_one(int i)
{
    int row, d, t;
    struct lem_l *p = &lem_lm[i];

    if (!lem_anim_at((int)p->act, p->fl & LF_RTL, (int)p->frame))
        return;
    row = (p->fl & LF_RTL) ? lem_an[p->act].rowl : lem_an[p->act].rowr;
    lem_r_sprite(lem_far_seg, lem_far_off,
                 p->x - (int)lem_an[p->act].footx,
                 p->y - (int)lem_an[p->act].footy,
                 (int)lem_ar[row].w, (int)lem_ar[row].h,
                 (int)lem_ar[row].bpp);
    LEM_COUNT(lem_c_sprites);
    if (p->timer == 0)
        return;
    t = p->timer;
    d = t >= 65 ? 5 : t >= 49 ? 4 : t >= 33 ? 3 : t >= 17 ? 2 : 1;
    if (!lem_mask_at(LEM_MK_DIGIT, 9 - d))
        return;
    lem_r_sprite(lem_far_seg, lem_far_off, p->x - 1,
                 p->y - (int)lem_an[p->act].footy - 12, 8, 8, 1 | (3 << 4));
    LEM_COUNT(lem_c_sprites);
}

/* --- THE FRAME'S RECTANGLES (SPEC.md 116.4.6) --------------------------------
 * Everything a game frame changes, as rectangles of WORLD BYTES (0..199) and
 * ROWS (0..159), inclusive: each destruction, each object whose frame moved,
 * and on VGA each lemming's box as the glass showed it last frame joined with
 * the box it has now. On VGA the list is what the compose builds and latches
 * on (ovl_vframe); on the two shadow cards only the first two kinds go in, and
 * each is rebuilt in W and shown once (ovl_frame_draw).
 *
 * TWO WAYS IN. The destruction and the objects are few and merge with EVERY
 * rectangle they overlap, until none does - so two overlapping water tiles,
 * or an exit and its torch, are one rectangle and each object over it is
 * drawn ONCE a frame (wave 3's review: each tile was drawn ~3 times, and on a
 * shadow card a whole object into W each time). A lemming's box merges with
 * the LAST rectangle only, when it touches it: the pool's order is the
 * crowd's order, so a hatch's stream folds into one rectangle at one compare
 * a lemming - against all forty it would be the frame's largest C cost on an
 * XT. Two rectangles may overlap on VGA, and that is only work, never a wrong
 * pixel: each is composed whole (T, then every object over it) before any
 * lemming is drawn. A full list folds into the rectangle whose half-perimeter
 * grows least. */
#define LEM_NRC     40
#define LEM_VCOMPH 125              /* lemblit.inc's compose band, in rows */
#define LEM_VRAM_W 1600             /* the VGA world's 200-byte row, in pixels */
static unsigned char lem_rc[LEM_NRC * 4];
static int lem_nrc;
static int lem_cr0, lem_cr1;        /* ovl_rc_clip's rows */

/* each lemming's box as the VGA glass shows it - b0, b1, r0, r1, and b0 = 0xFF
 * for none - which is what next frame's rectangle for it starts from. IT LIVES
 * IN lemsprite.inc's SPRITE RECORDS (lb_sprec, 1,008 bytes), which only the
 * shadow cards use: VGA keeps no records any more, and LEM_MAXLEM x 4 = 320
 * of those bytes cost no bss of their own (the .inc asserts the fit). The
 * kind of card cannot change inside a bracket, and ovl_frame_first() writes
 * every box before a frame reads one. */
#define lem_obox lem_sprec
static int lem_bb0, lem_bb1, lem_br0, lem_br1;     /* ovl_sbox's answer */

static int lem_cur_on;              /* the pointer is on the glass */

#ifdef LEM_HOST
static int lem_c_rcadd;             /* ovl_rc_add calls - the host harness's
                                     * count alone (lemtest's spray_area), so
                                     * the package carries no byte of it */
#endif

static void ovl_rc_add(int b0, int b1, int r0, int r1, int near)
{
    unsigned char *q;
    int k, g, best, bg;

    LEM_COUNT(lem_c_rcadd);
    if (near) {
        if (lem_nrc > 0) {
            q = &lem_rc[(lem_nrc - 1) * 4];
            if (b0 <= q[1] + 1 && q[0] <= b1 + 1 && r0 <= q[3] + 4 &&
                q[2] <= r1 + 4) {
                if (b0 < q[0])
                    q[0] = (unsigned char)b0;
                if (b1 > q[1])
                    q[1] = (unsigned char)b1;
                if (r0 < q[2])
                    q[2] = (unsigned char)r0;
                if (r1 > q[3])
                    q[3] = (unsigned char)r1;
                return;
            }
        }
    } else {
        k = 0;
        while (k < lem_nrc) {
            q = &lem_rc[k * 4];
            if (b0 <= q[1] && q[0] <= b1 && r0 <= q[3] && q[2] <= r1) {
                if (q[0] < b0)
                    b0 = q[0];
                if (q[1] > b1)
                    b1 = q[1];
                if (q[2] < r0)
                    r0 = q[2];
                if (q[3] > r1)
                    r1 = q[3];
                lem_nrc--;
                q[0] = lem_rc[lem_nrc * 4];
                q[1] = lem_rc[lem_nrc * 4 + 1];
                q[2] = lem_rc[lem_nrc * 4 + 2];
                q[3] = lem_rc[lem_nrc * 4 + 3];
                k = 0;              /* the union may reach one already passed */
                continue;
            }
            k++;
        }
    }
    if (lem_nrc < LEM_NRC) {
        q = &lem_rc[lem_nrc * 4];
        lem_nrc++;
    } else {
        best = 0;
        bg = 32767;
        for (k = 0; k < LEM_NRC; k++) {
            q = &lem_rc[k * 4];
            g = (b0 < q[0] ? q[0] - b0 : 0) + (b1 > q[1] ? b1 - q[1] : 0) +
                (r0 < q[2] ? q[2] - r0 : 0) + (r1 > q[3] ? r1 - q[3] : 0);
            if (g < bg) {
                bg = g;
                best = k;
            }
        }
        q = &lem_rc[best * 4];
        if (q[0] < b0)
            b0 = q[0];
        if (q[1] > b1)
            b1 = q[1];
        if (q[2] < r0)
            r0 = q[2];
        if (q[3] > r1)
            r1 = q[3];
    }
    q[0] = (unsigned char)b0;
    q[1] = (unsigned char)b1;
    q[2] = (unsigned char)r0;
    q[3] = (unsigned char)r1;
}

/* ovl_rc_px - a rectangle in world PIXELS, into bytes and rows, clipped */
static void ovl_rc_px(int x, int y, int w, int h, int near)
{
    int x1 = x + w - 1, y1 = y + h - 1;

    if (w <= 0 || h <= 0 || x1 < 0 || y1 < 0 || x >= LEM_VRAM_W ||
        y >= LEM_WORLD_H)
        return;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > LEM_VRAM_W - 1)
        x1 = LEM_VRAM_W - 1;
    if (y1 > LEM_WORLD_H - 1)
        y1 = LEM_WORLD_H - 1;
    ovl_rc_add(x >> 3, x1 >> 3, y, y1, near);
}

/* ovl_rc_clip - rectangle k's rows inside the band y0 .. y0+h-1 (lem_cr0,
 * lem_cr1), and the raster's clip set to them. 0 = it has none there. */
static int ovl_rc_clip(int k, int y0, int h)
{
    unsigned char *q = &lem_rc[k * 4];

    lem_cr0 = q[2] > y0 ? q[2] : y0;
    lem_cr1 = q[3] < y0 + h - 1 ? q[3] : y0 + h - 1;
    if (lem_cr0 > lem_cr1)
        return 0;
    lem_r_cclip(lem_cr0, lem_cr1 - lem_cr0 + 1, q[0], q[1]);
    return 1;
}

/* ovl_obj_due - object i moved a frame since it was drawn, and touches the
 * view: one off the glass keeps the frame it had, which is still a frame of
 * its own animation when it scrolls in */
static int ovl_obj_due(int i)
{
    struct lem_o *o = &lem_ob[i];

    if (o->drawn == o->frame && !o->dirty)
        return 0;
    return o->x + (int)lem_ty[o->id].w >= lem_view &&
           o->x <= lem_view + LEM_VIEW_W;
}

/* ovl_rc_world - the frame's destruction and its moved objects, as
 * rectangles. On VGA each destruction rectangle SYNCHRONISES T with the mask
 * first (lem_r_bgrect's sync 2, which copies nothing): the compose then copies
 * a T that already has the crater in it. */
static void ovl_rc_world(void)
{
    int i;
    int *d;
    struct lem_o *o;

    for (i = 0; i < lem_ndmg; i++) {
        d = &lem_dmg[i * 4];
        if (lem_kind_id() == LEM_RKIND_VGA)
            lem_r_bgrect(d[0], d[1], d[2], d[3], 2);
        ovl_rc_px(d[0], d[1], d[2], d[3], 0);
    }
    lem_ndmg = 0;
    for (i = 0; i < lem_nob; i++) {
        if (!ovl_obj_due(i))
            continue;
        o = &lem_ob[i];
        ovl_rc_px(o->x, o->y, (int)lem_ty[o->id].w, (int)lem_ty[o->id].h, 0);
        lem_c_objects++;
    }
}

/* ovl_objs_mark - every object ovl_rc_world() put in is drawn WHOLE now, even
 * where the band cut it in two or a rectangle only reached part of it
 * (ovl_obj_redraw_rect puts a part-drawn object's flag back) */
static void ovl_objs_mark(void)
{
    int i;

    for (i = 0; i < lem_nob; i++)
        if (ovl_obj_due(i)) {
            lem_ob[i].drawn = lem_ob[i].frame;
            lem_ob[i].dirty = 0;
        }
}

/* ovl_obj_vis - the objects that touch the union of this frame's rectangles,
 * in drawing order, so a rectangle walks a handful rather than every object
 * four times over */
static void ovl_obj_vis(void)
{
    int k, i, b0, b1, r0, r1, x;
    unsigned char *q;
    struct lem_o *o;

    b0 = 255;
    b1 = 0;
    r0 = 255;
    r1 = 0;
    for (k = 0; k < lem_nrc; k++) {
        q = &lem_rc[k * 4];
        if (q[0] < b0)
            b0 = q[0];
        if (q[1] > b1)
            b1 = q[1];
        if (q[2] < r0)
            r0 = q[2];
        if (q[3] > r1)
            r1 = q[3];
    }
    ovl_obj_order();
    lem_nvis = 0;
    for (k = 0; k < lem_nord; k++) {
        i = lem_oord[k];
        o = &lem_ob[i];
        x = o->x;
        if (x <= b1 * 8 + 7 && x + (int)lem_ty[o->id].w > b0 * 8 &&
            o->y <= r1 && o->y + (int)lem_ty[o->id].h > r0)
            lem_ovis[lem_nvis++] = (unsigned char)i;
    }
}

/* ovl_inmargin - is lemming p inside the view's margin, the +- 32 pixels VRAM
 * keeps lemmings in (and the shadow cards draw them in)? The two compares
 * ovl_sbox opens with, alone: ovl_vframe(0) asks it of every lemming BEFORE
 * paying for a box (wave 4 review). The margin need not cover a scroll's step:
 * every compose runs at the NEW view before the Start Address shows it
 * (ovl_tick_draw). */
static int ovl_inmargin(struct lem_l *p)
{
    return !(p->fl & LF_GONE) && p->x >= lem_view - 32 &&
           p->x <= lem_view + LEM_VIEW_W + 32;
}

/* ovl_sbox - lemming i's box as it is drawn this frame: its sprite and its
 * countdown digit, in whole bytes, clipped to the VRAM world. 0 = it is not
 * drawn at all (gone, outside the view's margin, or no frame in the bank). */
static int ovl_sbox(int i)
{
    struct lem_l *p = &lem_lm[i];
    int row, x0, y0, x1, y1;

    if (!ovl_inmargin(p))
        return 0;
    if (!lem_gfx_ok)
        return 0;
    row = (p->fl & LF_RTL) ? lem_an[p->act].rowl : lem_an[p->act].rowr;
    if (lem_ar[row].w == 0)
        return 0;
    x0 = p->x - (int)lem_an[p->act].footx;
    y0 = p->y - (int)lem_an[p->act].footy;
    x1 = x0 + (int)lem_ar[row].w - 1;
    y1 = y0 + (int)lem_ar[row].h - 1;
    if (p->timer) {                 /* the digit: (x - 1, y0 - 12), 8 x 8 */
        if (p->x - 1 < x0)
            x0 = p->x - 1;
        if (p->x + 6 > x1)
            x1 = p->x + 6;
        y0 -= 12;
    }
    if (x1 < 0 || y1 < 0 || x0 >= LEM_VRAM_W || y0 >= LEM_WORLD_H)
        return 0;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > LEM_VRAM_W - 1)
        x1 = LEM_VRAM_W - 1;
    if (y1 > LEM_WORLD_H - 1)
        y1 = LEM_WORLD_H - 1;
    lem_bb0 = x0 >> 3;
    lem_bb1 = x1 >> 3;
    lem_br0 = y0;
    lem_br1 = y1;
    return 1;
}

/* ovl_obox_forget - nothing of any lemming is on the VGA glass (the T load of
 * the first frame and of 'f' covers the world) */
static void ovl_obox_forget(void)
{
    int i;

    for (i = 0; i < LEM_MAXLEM; i++)
        lem_obox[i * 4] = 0xFF;
}

/* --- THE EXPLOSION'S SPRAY (decision 5, SPEC.md 116.8, wave 6) -------------
 * Eighty single pixels a frame for the 52 frames after a bomber goes off -
 * the table's 51 rows, the last one twice, as Lemmix draws them
 * (LEM_PTIME) - at the foot it left: LEMMAIN item 10 is PARTICLES.DAT's
 * table, one 160-byte row of signed (dx, dy) pairs a frame, and after it each
 * row's BOUNDS, so the C never walks the 80 to know where a frame is. The
 * record carries the frame the glass shows (dpf, 1 .. LEM_PTIME) and the box
 * it was drawn in (pbox), because the box is what the next frame has to put
 * back:
 *
 *   VGA      a game frame makes each spray ONE RECTANGLE A 16-ROW BAND IT
 *            OCCUPIES - the old frame's pixels and the new frame's in that
 *            band, which lem_r_spray's bins (erase 2, 3) find in one walk of
 *            the 80 pairs each (wave 6's second review: one box over the
 *            whole cloud recomposed 3.6x the area, 2,961 byte-rows a frame
 *            against 828 on the real table, for at most 160 changed
 *            pixels) - and the band loop draws it into the scratch
 *            in the pool's order with the lemmings, so the latch puts the
 *            erasure and the new pixels down together and the pointer stays
 *            lit over it, the compose's whole contract (SPEC.md 116.4.6). A
 *            compose with no game frame draws it as the glass shows it
 *   CGA/Herc each frame puts the old pixels' bytes back out of W before the
 *            sprites come off, and the sprite pass draws the new ones in the
 *            solid class every sprite is drawn in
 *
 * A spray is culled to the VGA's margin (the shadow cards' view) like a
 * lemming: pixels nobody can see are not drawn, and the old box is erased
 * wherever it was. */

/* ovl_psb - one signed byte of the spray item */
static int ovl_psb(unsigned o)
{
    int v = os88_peek(lem_ptseg, o) & 0xFF;

    return v > 127 ? v - 256 : v;
}

/* ovl_prow - the table row spray frame n (1 .. LEM_PTIME) draws: the last
 * row twice (Lemmix's ParticleFrame stops at 50, its timer at 52) */
static int ovl_prow(int n)
{
    return n > LEM_PFR ? LEM_PFR - 1 : n - 1;
}

/* ovl_pbox - spray p's box at frame n (1 .. LEM_PTIME), from its row's own
 * bounds, offset by the foot and clipped to the world and the view's margin,
 * into lem_bb0 .. lem_br1. 0 = none of it is there, or n 0: no spray. */
static int ovl_pbox(struct lem_l *p, int n)
{
    unsigned o;
    int x0, x1, y0, y1, lo, hi;

    if (!n || !lem_ptseg)
        return 0;
    o = lem_ptoff + LEM_PFR * LEM_PROW + ((unsigned)ovl_prow(n) << 2);
    x0 = ovl_psb(o);
    x1 = ovl_psb(o + 1);
    if (x1 < x0)
        return 0;                   /* a frame with no particle drawn */
    x0 += p->x;
    x1 += p->x;
    y0 = p->y + ovl_psb(o + 2);
    y1 = p->y + ovl_psb(o + 3);
    lo = lem_view - 32;
    hi = lem_view + LEM_VIEW_W + 31;
    if (lo < 0)
        lo = 0;
    if (hi > LEM_VRAM_W - 1)
        hi = LEM_VRAM_W - 1;
    if (x0 < lo)
        x0 = lo;
    if (x1 > hi)
        x1 = hi;
    if (y0 < 0)
        y0 = 0;
    if (y1 > LEM_WORLD_H - 1)
        y1 = LEM_WORLD_H - 1;
    if (x0 > x1 || y0 > y1)
        return 0;
    lem_bb0 = x0 >> 3;
    lem_bb1 = x1 >> 3;
    lem_br0 = y0;
    lem_br1 = y1;
    return 1;
}

/* ovl_spray_rows - spray p as its record says it is drawn: frame dpf, inside
 * pbox's byte columns and the rows r0 .. r1 of it; `erase` 1 puts it back
 * (the shadow cards), 2 and 3 bin it (the VGA's rectangles, lemsprite.inc).
 * The caller narrows the rows to a compose band's (ovl_vframe), so a pair
 * outside the band fails lem_r_spray's first compare - its row - and never
 * reaches the three shifts, the byte compares and the band test behind them
 * (wave 6's third review: a box straddling the band's edge paid the whole
 * walk twice) */
static void ovl_spray_rows(struct lem_l *p, int erase, int r0, int r1)
{
    lem_r_spray(lem_ptseg, lem_ptoff + (unsigned)ovl_prow(p->dpf) * LEM_PROW,
                p->x, p->y, (int)p->pbox[0], (int)p->pbox[1], r0, r1, erase);
}

/* ovl_spray_one - ...over the whole of pbox */
static void ovl_spray_one(struct lem_l *p, int erase)
{
    ovl_spray_rows(p, erase, (int)p->pbox[2], (int)p->pbox[3]);
}

/* ovl_spray_keep - the box ovl_pbox answered, as the one frame pf is drawn
 * in; `k` 0: nothing of it is drawn */
static void ovl_spray_keep(struct lem_l *p, int k)
{
    if (!k) {
        p->dpf = 0;
        return;
    }
    p->dpf = p->pf;
    p->pbox[0] = (unsigned char)lem_bb0;
    p->pbox[1] = (unsigned char)lem_bb1;
    p->pbox[2] = (unsigned char)lem_br0;
    p->pbox[3] = (unsigned char)lem_br1;
}

/* ovl_spray_new - spray p at its current frame, straight in: the first frame
 * and 'f' onto the VGA world, the shadow cards' sprite pass into the shadow */
static void ovl_spray_new(struct lem_l *p)
{
    ovl_spray_keep(p, ovl_pbox(p, (int)p->pf));
    if (p->dpf)
        ovl_spray_one(p, 0);
}

/* ovl_spray_forget - nothing of any spray is where its record says: the T
 * load, a shadow card's scroll (lem_r_scroll recomposes the window from W) */
static void ovl_spray_forget(void)
{
    int i;

    for (i = 0; i < lem_released; i++)
        lem_lm[i].dpf = 0;
}

/* ovl_spray_erase - a shadow card's game frame: every spray's pixels off,
 * out of W, before the sprites come off and the new frame goes in */
static void ovl_spray_erase(void)
{
    int i;
    struct lem_l *p;

    for (i = 0; i < lem_released; i++) {
        p = &lem_lm[i];
        if (!p->dpf)
            continue;
        ovl_spray_one(p, 1);
        p->dpf = 0;
    }
}

/* ovl_spray_bins - a VGA game frame's rectangles for spray p as BINS: ONE A
 * 16-ROW BAND, each the span of the pixels the glass shows there joined with
 * the span of the pixels its new frame puts there (lem_r_spray's bins, the
 * old frame emptying them and the new adding to them), and the record moved
 * on to the new frame. Bands are 16 rows so two never share a row and
 * ovl_rc_add keeps them apart; on the real table a frame is ~7.6 of them.
 * WHAT IT COSTS: two 80-pair walks and up to LEM_PBINS adds, which is why
 * ovl_spray_rects reaches it only where that buys something. */
#define lem_pbins (lem_sprec + LEM_MAXLEM * 4)
static void ovl_spray_bins(struct lem_l *p)
{
    int m = 2;                      /* the first binning empties the bins */
    unsigned char *q;

    if (p->dpf) {
        ovl_spray_one(p, 2);        /* the pixels the glass shows */
        m = 3;
    }
    ovl_spray_keep(p, ovl_pbox(p, (int)p->pf));
    if (p->dpf) {
        ovl_spray_one(p, m);        /* ...and the ones about to be drawn */
        m = 3;
    }
    if (m == 3)
        for (q = lem_pbins; q < lem_pbins + LEM_PBINS * 4; q += 4)
            if (q[0] != 0xFF)
                ovl_rc_add(q[0], q[1], q[2], q[3], 0);
}

/* ovl_spray_box - ...as ONE rectangle: the box the glass frame was drawn in
 * joined with the new frame's, both read from the stored bounds (pbox and
 * ovl_pbox's row bounds after the table), no walk and one add. */
static void ovl_spray_box(struct lem_l *p)
{
    int b0 = 255, b1 = -1, r0 = 255, r1 = -1;   /* an empty box */

    if (p->dpf) {
        b0 = p->pbox[0];
        b1 = p->pbox[1];
        r0 = p->pbox[2];
        r1 = p->pbox[3];
    }
    ovl_spray_keep(p, ovl_pbox(p, (int)p->pf));
    if (p->dpf) {
        if (lem_bb0 < b0)
            b0 = lem_bb0;
        if (lem_bb1 > b1)
            b1 = lem_bb1;
        if (lem_br0 < r0)
            r0 = lem_br0;
        if (lem_br1 > r1)
            r1 = lem_br1;
    }
    if (b1 >= 0)
        ovl_rc_add(b0, b1, r0, r1, 0);
}

/* ovl_spray_rects - a VGA game frame's rectangles for every spray in flight,
 * BINS ONLY WHERE THEY PAY (wave 6's third review): a lone spray on a list
 * with room for its LEM_PBINS rectangles short of ovl_rc_add's best-fit
 * loop takes ovl_spray_bins, the case lemtest's spray_area measured (828
 * byte-rows a frame against one box's 2,961 on the real table). Anything
 * else takes ovl_spray_box. A nuke lights a fuse a frame and a spray flies
 * 52, so a nuked crowd keeps up to 52 in flight with every lemming in the
 * margin already a rectangle: there the boxes overlap and fold into the
 * capped list whichever way they are cut, the area saving is gone, and the
 * bins' two walks a spray (~2 x 44K cycles, ~19 ms of a 4.77 MHz 8088) and
 * their up-to-ten full-list adds would have been paid N times over for
 * nothing. The first spray's choice is made after the count, so the loop
 * defers it. */
static void ovl_spray_rects(void)
{
    int i, more = 0;
    struct lem_l *p, *first = 0;

    for (i = 0; i < lem_released; i++) {
        p = &lem_lm[i];
        if (!(p->pf | p->dpf))
            continue;
        if (!first) {
            first = p;
            continue;
        }
        ovl_spray_box(p);
        more = 1;
    }
    if (!first)
        return;
    if (!more && lem_nrc + LEM_PBINS <= LEM_NRC)
        ovl_spray_bins(first);
    else
        ovl_spray_box(first);
}

/* ovl_sprites_all - every lemming that could be on the glass, in the list's
 * order (Lemmix DrawLemmings), straight onto it: the first frame, 'f', and the
 * two shadow cards' frames, whose shadow is off the glass until the present.
 * One wholly outside the view is not drawn at all on any card: on VGA it would
 * be VRAM nobody sees, and the next frame draws it where it is by then. On VGA
 * each one drawn leaves its box behind for the next frame's compose. */
static void ovl_sprites_all(void)
{
    int i, vga;
    unsigned char *b;

    vga = lem_kind_id() == LEM_RKIND_VGA;
    for (i = 0; i < lem_released; i++) {
        if (lem_lm[i].fl & LF_GONE) {
            ovl_spray_new(&lem_lm[i]);  /* in the pool's order, as the
                                         * original draws its particles */
            continue;
        }
        if (vga) {
            if (!ovl_sbox(i))
                continue;
            b = &lem_obox[i * 4];
            b[0] = (unsigned char)lem_bb0;
            b[1] = (unsigned char)lem_bb1;
            b[2] = (unsigned char)lem_br0;
            b[3] = (unsigned char)lem_br1;
        } else {
            if (!ovl_inmargin(&lem_lm[i]))
                continue;
        }
        ovl_sprite_one(i);
    }
}

/* --- the pointer -------------------------------------------------------------
 * Lemmix's own two 14x14 cursors, hotspot (7, 7) (src/GameScreen.Player.pas
 * InitializeCursor; the bitmaps are src/Data/Cursors/Cursors.zip's
 * CursorDefault / CursorHighlight and their masks, under Lemmix's own
 * Licence.txt, zlib-style with an added clause): a dashed
 * cross, and the boxed form shown while a lemming is under it. IN THREE
 * COLOURS, as both references draw them - palette 1 the blue centre, 2 green
 * and 3 white on the arms and the box corners (the left arm green over white,
 * the right white over green, the bar white | green) - stored as TWO PLANES of
 * fourteen 2-byte rows, plane 0 first: a pixel's colour is p0 | p1 << 1 and it
 * is opaque where either is set. The VGA draws the colours; the two shadow
 * cards draw the OR, their solid class (lemsprite.inc lb_score). Nothing is
 * taken from lemmings_3ds's src/data/cursor.c, which that project's LICENCE.txt
 * excludes from its dedication (SPEC.md 116.1) - it agrees with these. Wave 3
 * drew one colour, the panel's white. */
static const unsigned char lem_cur0[56] = {
    0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x03, 0x54, 0xAB, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0xA8, 0x54, 0xA8, 0x54, 0x00, 0x00, 0x03, 0x00,
    0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00
};
static const unsigned char lem_cur1[56] = {
    0x33, 0x30, 0x00, 0x00, 0x80, 0x04, 0x80, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x04, 0x80, 0x04, 0x00, 0x00, 0x00, 0x00, 0x80, 0x04, 0x80, 0x04,
    0x00, 0x00, 0x33, 0x30, 0xF0, 0x3C, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0xF0, 0x3C
};


static void ovl_cursor_off(void)
{
    if (lem_cur_on)
        lem_r_cursor(0, 0, 0);
    lem_cur_on = 0;
}

/* ovl_cur_shape - the boxed form only when the status line names a lemming:
 * Lemmix HitTest (Game.pas:3641-3668) sets GAME_CURSOR_LEMMING on HitCount > 0
 * AND Lemming1 <> nil, and with the right button held over nothing but
 * working lemmings Lemming1 is nil - the default cross, and a blank word. */
static int ovl_cur_shape(void)
{
    return lem_hit_n > 0 && lem_hit_l1 >= 0;
}

static void ovl_cursor_on(void)
{
    lem_r_cursor(ovl_cur_shape() ? lem_cur1 : lem_cur0, lem_mx - 7,
                 lem_my - 7);
    lem_cur_on = 1;
}

static int lem_curx = -1, lem_cury = -1, lem_curshape = -1;

/* ovl_vframe - ONE VGA GAME FRAME, COMPOSED OFF THE GLASS (SPEC.md 116.4.6).
 *
 *   1. the rectangles: the destruction (synced into T) and the moved objects,
 *      then each lemming's last box joined with its new one;
 *   2. for each BAND of LEM_VCOMPH rows the rectangles reach (one, unless the
 *      frame changed rows more than 125 apart): every rectangle's T and the
 *      objects over it into the scratch, then EVERY lemming whose box is in
 *      the band - once, in the pool's order, whichever rectangle it is in -
 *      then the pointer's save-under brought up to date and the pointer drawn
 *      in, then each rectangle latch-copied onto the glass.
 *
 * Nothing is drawn onto the glass but the latch, which copies a finished
 * picture: no lemming, object or pointer pixel is missing between two frames.
 * Wave 3 restored every save-under onto the glass and drew the frame there,
 * and a lemming was off the glass from its restore to its draw - the whole
 * frame on an XT - with the pointer off with it whenever it was near one,
 * which is to say while the player aimed (wave 3's second review). */
static int lem_cmv;                 /* this frame moves the pointer too */

/* THE POINTER'S COMPOSE IS BOUNDED (wave 4's third review). Between the
 * passes of a part-stepped frame (lem_infr) a pointer move that rode
 * ovl_vframe(0) could make ovl_infr_whole ride across a stepped crowd: each
 * stepped lemming under the pointer becomes a rectangle of its two boxes, a
 * rectangle meets more of the crowd, and nothing bounded the ride or counted
 * it - on the XT's many aiming ticks, which is exactly when the player is
 * looking at the pointer. So a pointer-only tick (no game frame, no scroll)
 * rides the compose only while at most LEM_CMV_CAP stepped lemmings meet its
 * two boxes, and the cap is ZERO: none, so the cascade cannot start - a
 * lemming that has not stepped is in the state the glass shows and composes
 * clipped like any other. Past the cap the tick takes the constant-cost glass
 * pair instead (ovl_cursor_off/on, 14x14 restored and drawn, which is what
 * wave 3 did on every tick): correct whatever the crowd, one erase-then-draw
 * of the pointer where it meets lemmings that moved this frame. The scroll
 * keeps the cascade, which correctness needs there and which a scroll pays for
 * anyway. lem_cmv_cap is a variable so the harness can lift it for its
 * negative control, and the two counters are what lemtest prints. */
#define LEM_CMV_CAP 0
static int lem_cmv_cap = LEM_CMV_CAP;
#ifdef LEM_HOST
static int lem_c_infr;              /* lemmings ovl_infr_whole made rectangles
                                     * of, this compose */
static int lem_c_cmvfb;             /* pointer moves the cap sent to the glass
                                     * pair (cumulative) */
#endif

/* ovl_rc_meets - does a byte box (b0, b1, r0, r1) meet any of the frame's
 * rectangles? A lemming that meets none is not latched anywhere, so composing
 * its sprite into the scratch is work for no pixel (ovl_vframe(0)). */
static int ovl_rc_meets(unsigned char *b)
{
    int k;
    unsigned char *q;

    for (k = 0; k < lem_nrc; k++) {
        q = &lem_rc[k * 4];
        if (b[0] <= q[1] && q[0] <= b[1] && b[2] <= q[3] && q[2] <= b[3])
            return 1;
    }
    return 0;
}

/* ovl_rc_holds - is the byte box (b0, b1, r0, r1) wholly inside ONE of the
 * frame's rectangles? Then it is erased and drawn whole by the band loop. */
static int ovl_rc_holds(int b0, int b1, int r0, int r1)
{
    int k;
    unsigned char *q;

    for (k = 0; k < lem_nrc; k++) {
        q = &lem_rc[k * 4];
        if (q[0] <= b0 && b1 <= q[1] && q[2] <= r0 && r1 <= q[3])
            return 1;
    }
    return 0;
}

/* ovl_infr_whole - A COMPOSE BETWEEN THE TICKS OF A PART-STEPPED FRAME
 * (lem_infr: more than LEM_STEP_MAX lemmings out, so one game frame is
 * several passes). Lemmings before lem_cursor have STEPPED - a new x and a
 * new frame - while lem_obox still holds the box the glass shows. One of
 * those whose old box meets a rectangle would be drawn in its NEW state
 * clipped to the rectangle over an erasure of its OLD one clipped the same
 * way: half a lemming in each place, under the pointer, while the player
 * aims (wave 4's second review). So each such lemming becomes a crosser:
 * the union of its two boxes is a rectangle, and its box is the new one -
 * erased and drawn whole. A union can meet more of the stepped crowd, so
 * this runs until a pass adds nothing; it ends, because every rectangle
 * only grows inside the world. One not yet stepped is in the state the glass
 * shows, and drawing it clipped draws what is there. */
static void ovl_infr_whole(void)
{
    int i, k, b0, b1, r0, r1, grew;
    unsigned char *b;

    do {
        grew = 0;
        for (i = 0; i < lem_cursor && i < lem_released; i++) {
            b = &lem_obox[i * 4];
            if (b[0] == 0xFF || !ovl_rc_meets(b))
                continue;
            b0 = b[0];
            b1 = b[1];
            r0 = b[2];
            r1 = b[3];
            k = ovl_sbox(i);
            if (k) {
                if (lem_bb0 < b0)
                    b0 = lem_bb0;
                if (lem_bb1 > b1)
                    b1 = lem_bb1;
                if (lem_br0 < r0)
                    r0 = lem_br0;
                if (lem_br1 > r1)
                    r1 = lem_br1;
                b[0] = (unsigned char)lem_bb0;
                b[1] = (unsigned char)lem_bb1;
                b[2] = (unsigned char)lem_br0;
                b[3] = (unsigned char)lem_br1;
            } else {
                b[0] = 0xFF;
            }
            if (!ovl_rc_holds(b0, b1, r0, r1)) {
                ovl_rc_add(b0, b1, r0, r1, 0);
                LEM_COUNT(lem_c_infr);
                grew = 1;
            }
        }
    } while (grew);
}

static void ovl_vframe(int step)
{
    int i, k, y0, h, ymin, ymax, vmoved;
    unsigned char *b, *q;

    lem_nrc = 0;
    LEM_CLEAR(lem_c_infr);
    if (step)
        ovl_rc_world();
    /* NOT A GAME FRAME (a scroll, a minimap jump or a pointer move while
     * nothing stepped, ovl_tick_draw): the membership loop below runs only
     * when the VIEW moved - a pointer move alone cannot change who is in the
     * margin (wave 4's second review: it paid an intra-module far call a
     * lemming on every aiming tick). This is the VGA's arm, which pans a
     * pixel at a time, so the view moved exactly when it is not the one on
     * the glass (ovl_scroll_due with no snap). */
    vmoved = step || lem_view != lem_scrolled;
    for (i = 0; i < lem_released && vmoved; i++) {
        b = &lem_obox[i * 4];
        /* ...and then only the lemmings newly IN the view's margin, asked with
         * ovl_inmargin's two compares BEFORE a box is paid for (wave 4
         * review: ovl_sbox over the whole crowd was ~20 ms of an XT tick on
         * every scroll step). One newly OUT keeps its box: composing its
         * erasure is a whole compose-and-latch of VRAM 32 pixels beyond the
         * glass, the next game frame's step erases a box whose ovl_sbox
         * answers 0 anyway, and a paused view that comes back finds it still
         * drawn where it stands (wave 4's second review). One that stayed is
         * on the glass where its box says. */
        if (!step && (b[0] != 0xFF || !ovl_inmargin(&lem_lm[i])))
            continue;
        k = ovl_sbox(i);
        if (b[0] == 0xFF && !k)
            continue;
        if (b[0] == 0xFF) {
            ovl_rc_add(lem_bb0, lem_bb1, lem_br0, lem_br1, 1);
        } else if (!k) {
            ovl_rc_add(b[0], b[1], b[2], b[3], 1);
        } else {
            ovl_rc_add(b[0] < lem_bb0 ? b[0] : lem_bb0,
                       b[1] > lem_bb1 ? b[1] : lem_bb1,
                       b[2] < lem_br0 ? b[2] : lem_br0,
                       b[3] > lem_br1 ? b[3] : lem_br1, 1);
        }
        if (k) {
            b[0] = (unsigned char)lem_bb0;
            b[1] = (unsigned char)lem_bb1;
            b[2] = (unsigned char)lem_br0;
            b[3] = (unsigned char)lem_br1;
        } else {
            b[0] = 0xFF;
        }
    }
    if (step)
        ovl_spray_rects();
    if (lem_cmv) {
        /* THE POINTER MOVES INSIDE THE COMPOSE (wave 3's third review): its
         * old box and its new one are rectangles of this frame, and the asm
         * is told the new place before anything is drawn - so the latch puts
         * the erasure and the new pointer down in ONE write a byte, where the
         * frame used to compose it at the old place and the tick then took it
         * off and drew it again (rule 2's erase-then-draw on every aiming
         * move). ovl_tick_draw only asks for this when both boxes are wholly
         * in the world, so the panel piece never moves here.
         *
         * THE NEW BOX IS ITS SAVE-UNDER'S WHOLE-BYTE SPAN, NOT ITS 14 PIXELS
         * (wave 6, carried finding A): lem_r_cursor saves and restores three
         * planar bytes a row from x & ~7 - lb_clipx always adds the tail byte
         * the shift spills into - and lem_r_csave brings up to date only what
         * a rectangle covers. A 14-pixel box at x & 7 of 0, 1 or 2 left the
         * tail column of the save holding the LAST place saved, and the glass
         * pair that took the pointer into the panel put an 8x14 block of
         * that place on the glass (wave 5's verifier, VGA). The old box needs
         * only its 14: no pointer pixel is outside it, and the compose
         * rebuilds it from T. */
        ovl_rc_px(lem_view + lem_curx - 7, lem_cury - 7, 14, 14, 0);
        ovl_rc_px((lem_view + lem_mx - 7) & ~7, lem_my - 7, 24, 14, 0);
        lem_r_cplace(ovl_cur_shape() ? lem_cur1 : lem_cur0, lem_mx - 7,
                     lem_my - 7);
    }
    if (!step && lem_infr && lem_nrc)
        ovl_infr_whole();
    if (lem_nrc == 0)
        return;
    ovl_obj_vis();
    ymin = 255;
    ymax = 0;
    for (k = 0; k < lem_nrc; k++) {
        q = &lem_rc[k * 4];
        if (q[2] < ymin)
            ymin = q[2];
        if (q[3] > ymax)
            ymax = q[3];
    }
    for (y0 = ymin; y0 <= ymax; y0 += LEM_VCOMPH) {
        h = ymax - y0 + 1;
        if (h > LEM_VCOMPH)
            h = LEM_VCOMPH;
        if (!lem_r_cband(y0, h))
            break;
        for (k = 0; k < lem_nrc; k++) {
            if (!ovl_rc_clip(k, y0, h))
                continue;
            q = &lem_rc[k * 4];
            lem_r_bgrect(q[0] << 3, lem_cr0, (q[1] - q[0] + 1) << 3,
                         lem_cr1 - lem_cr0 + 1, 0);
            ovl_obj_redraw_rect(q[0] << 3, lem_cr0, (q[1] - q[0] + 1) << 3,
                                lem_cr1 - lem_cr0 + 1);
        }
        for (i = 0; i < lem_released; i++) {
            /* a spray in the pool's order, where its bomber was - only a
             * removed lemming has one, and a removed lemming has no box */
            if (lem_lm[i].dpf) {
                b = lem_lm[i].pbox;
                if (b[3] < y0 || b[2] >= y0 + h || (!step && !ovl_rc_meets(b)))
                    continue;
                /* only the band's rows of it: a box across the band's edge
                 * is walked once a band either way, but the pairs outside
                 * fail the row compare and cost nothing else */
                ovl_spray_rows(&lem_lm[i], 0, b[2] > y0 ? b[2] : y0,
                               b[3] < y0 + h - 1 ? b[3] : y0 + h - 1);
                continue;
            }
            b = &lem_obox[i * 4];
            if (b[0] == 0xFF || b[3] < y0 || b[2] >= y0 + h)
                continue;
            /* a game frame made every lemming a rectangle of its own; with
             * no step only the crossers are, and a lemming that meets no
             * rectangle is latched nowhere (wave 4 review: one crossing
             * recomposed the whole crowd's sprites) */
            if (!step && !ovl_rc_meets(b))
                continue;
            ovl_sprite_one(i);
        }
        if (lem_cur_on) {
            for (k = 0; k < lem_nrc; k++)
                if (ovl_rc_clip(k, y0, h))
                    lem_r_csave();
            lem_r_cdraw();
        }
        for (k = 0; k < lem_nrc; k++)
            if (ovl_rc_clip(k, y0, h))
                lem_r_clatch();
        lem_r_cend();
    }
    lem_nvis = -1;
    if (step)
        ovl_objs_mark();
}

/* ovl_bg_redraw - a world rectangle's background put back and every object
 * over it drawn again: COMPOSED OFF THE GLASS on VGA, a band of LEM_VCOMPH
 * rows at a time (a rectangle taller than the band used to be drawn onto the
 * glass, bare terrain first), with a lit pointer kept lit through it the way
 * the frame keeps it; on the two shadow backends W is off the glass already
 * and lem_r_show() moves the rectangle into the shadow. */
static void ovl_bg_redraw(int x, int y, int w, int h, int sync)
{
    int y0, y1, n, b0, b1;

    if (lem_kind_id() != LEM_RKIND_VGA) {
        lem_r_bgrect(x, y, w, h, sync);
        ovl_obj_redraw_rect(x, y, w, h);
        lem_r_show(x, y, w, h);
        return;
    }
    if (x + w <= 0 || x >= LEM_VRAM_W || w <= 0)
        return;
    b0 = x < 0 ? 0 : x >> 3;
    b1 = (x + w - 1) >> 3;
    if (b1 > LEM_VRAM_W / 8 - 1)
        b1 = LEM_VRAM_W / 8 - 1;
    y0 = y < 0 ? 0 : y;
    y1 = y + h > LEM_WORLD_H ? LEM_WORLD_H : y + h;
    if (sync)
        lem_r_bgrect(x, y, w, h, 2);
    for (; y0 < y1; y0 += n) {
        n = y1 - y0;
        if (n > LEM_VCOMPH)
            n = LEM_VCOMPH;
        if (!lem_r_cbegin(y0, n, b0, b1))
            return;
        lem_r_bgrect(x, y0, w, n, 0);
        ovl_obj_redraw_rect(x, y0, w, n);
        if (lem_cur_on) {
            lem_r_csave();
            lem_r_cdraw();
        }
        lem_r_clatch();
        lem_r_cend();
    }
}

/* ovl_dmg_fold - the seventeenth destruction rectangle and after, into the
 * slot whose union grows least (half-perimeter, which cannot overflow an int
 * the way an area of a 1,584-pixel union can) */
static void ovl_dmg_fold(int x, int y, int w, int h)
{
    int k, best, bg, g, x1, y1;
    int *d;

    best = 0;
    bg = 32767;
    for (k = 0; k < LEM_NDMG; k++) {
        d = &lem_dmg[k * 4];
        g = (x < d[0] ? d[0] - x : 0) + (y < d[1] ? d[1] - y : 0) +
            (x + w > d[0] + d[2] ? x + w - d[0] - d[2] : 0) +
            (y + h > d[1] + d[3] ? y + h - d[1] - d[3] : 0);
        if (g < bg) {
            bg = g;
            best = k;
        }
    }
    d = &lem_dmg[best * 4];
    x1 = d[0] + d[2];
    y1 = d[1] + d[3];
    if (x < d[0])
        d[0] = x;
    if (y < d[1])
        d[1] = y;
    if (x + w > x1)
        x1 = x + w;
    if (y + h > y1)
        y1 = y + h;
    d[2] = x1 - d[0];
    d[3] = y1 - d[1];
}

/* --- the whole frame --------------------------------------------------------- */

/* ovl_frame_first - everything that is drawn once, in the order it has to be:
 * the background (T into VRAM, or W out of the mask), every object on it, the
 * minimap sampled and drawn, the view rectangle, the line, the counts and the
 * selection frame - and the same again when 'f' brings the level back. The
 * pointer is OWED afterwards: its cached position is forgotten, so the first
 * tick puts it back even if the mouse has not moved (after 'f' and resume the
 * loop's own ovl_cursor_off() had left it off, and a still mouse kept it off). */
static void ovl_frame_first(int resume)
{
    int b;

    if (lem_kind_id() == LEM_RKIND_VGA) {
        if (!resume)
            lem_r_tmask();
        lem_r_tload();
    } else if (!resume) {
        lem_r_derive();
    }
    lem_r_sforget();
    ovl_obox_forget();
    ovl_spray_forget();
    lem_nvis = -1;
    ovl_obj_all();
    if (!resume)
        ovl_mm_sample();
    else {
        ovl_panel_draw();
        lem_stat_have = 0;
        ovl_status_template();
    }
    ovl_mm_draw();
    for (b = 0; b < LEM_MM_H * LEM_MM_STRIDE; b++)
        lem_mmc[b] = 0;             /* ovl_mm_draw() drew every cell */
    lem_mmpend = 0;
    lem_view_drawn = -1;
    ovl_view_rect(-1);
    lem_view_drawn = lem_view / LEM_MM_XSTEP;
    for (b = 0; b < LEM_STAT_CELLS; b++)
        lem_stold[b] = (char)0xFF;  /* every cell differs: the line is drawn */
    lem_stold[LEM_STAT_CELLS] = 0;
    lem_cdirty = 0xFFFF;
    lem_seldrawn = -1;
    ovl_stat_build();
    ovl_panel_live();
    lem_r_scroll(lem_view);
    lem_scrolled = lem_view;
    ovl_sprites_all();
    lem_curx = -1;
    lem_cury = -1;
    lem_curshape = -1;
    lem_r_present();
}

/* ovl_frame_draw - one game frame's WORLD. VGA: composed off the glass
 * (ovl_vframe). The two shadow cards: the sprites taken off the shadow out of
 * W, each changed rectangle rebuilt in W and shown into the shadow ONCE, the
 * sprites drawn again - all of it in a shadow the glass does not see until
 * lem_r_present(). The minimap and the panel are drawn after it
 * (ovl_tick_draw). */
static void ovl_frame_draw(void)
{
    int k;
    unsigned char *q;

    LEM_CLEAR(lem_c_sprites);
    lem_c_objects = 0;
    if (lem_kind_id() == LEM_RKIND_VGA) {
        ovl_vframe(1);
        return;
    }
    ovl_spray_erase();
    lem_r_srest();
    lem_nrc = 0;
    ovl_rc_world();
    for (k = 0; k < lem_nrc; k++) {
        q = &lem_rc[k * 4];
        ovl_bg_redraw(q[0] << 3, q[2], (q[1] - q[0] + 1) << 3,
                      q[3] - q[2] + 1, 0);
    }
    ovl_objs_mark();
    ovl_sprites_all();
}

/* ovl_view_follow - the scroll, and on a shadow backend everything a scroll
 * recomposes away. 1 = the view moved. `drew`: a game frame is drawn this
 * tick too, and THAT draws the sprites - a scroll that drew them as well
 * drew every sprite twice and restored it once on the most expensive tick
 * there is (SPEC.md 116.7.1's 178 ms scrolling frame). On VGA the scroll is
 * the Start Address now and the pel pan after the retrace (ovl_tick_draw). */
/* ovl_scroll_due - will the scroll move a pixel? VGA pans one pixel at a
 * time; CGA snaps the view to 4 pixels and Hercules to 8 (lemblit.inc .snap),
 * so a Ctrl+arrow's one-pixel step there scrolls nothing until the snap moves
 * - and the shadow arm below used to drop and redraw every sprite and present
 * their rows for it, 3 passes of 4 on CGA and 7 of 8 on Hercules, showing
 * nothing (wave 4 review). lem_scrolled keeps the last view that DID scroll,
 * whose snap is the one on the glass. */
static int ovl_scroll_due(void)
{
    int m;

    m = lem_kind_id() == LEM_RKIND_VGA ? 0
      : lem_kind_id() == LEM_RKIND_CGA ? 3 : 7;
    return (lem_view & ~m) != (lem_scrolled & ~m);
}

static int ovl_view_follow(int drew)
{
    if (!ovl_scroll_due())
        return 0;
    lem_scrolled = lem_view;
    if (lem_kind_id() == LEM_RKIND_VGA) {
        lem_r_vstart(lem_view);
        return 1;
    }
    /* THE WINDOW IS RECOMPOSED OUT OF W, which has no sprites in it: the
     * records are dropped rather than restored (they describe the old view)
     * and the sprites are drawn again on the new one - here only when no game
     * frame will draw them below */
    lem_r_sforget();
    ovl_spray_forget();
    lem_r_scroll(lem_view);
    if (!drew)
        ovl_sprites_all();
    return 1;
}

/* ovl_rhit - do two inclusive rectangles meet? */
static int ovl_rhit(int x0, int y0, int x1, int y1, int bx0, int by0, int bx1,
                    int by1)
{
    return x0 <= bx1 && bx0 <= x1 && y0 <= by1 && by0 <= y1;
}

/* ovl_cur_hit - THE SHADOW CARDS' WORLD TEST: will this tick write anything
 * into the shadow under the pointer's 14x14 box at screen (cx, cy) - the box
 * it is lit at now, the only one a write can land under? A record
 * lem_r_srest() puts back, a lemming's new sprite (its box hung generously off
 * the foot: 16 either side, 40 up for the countdown digit), a destruction
 * rectangle, an object about to be rebuilt. One rectangle test per write list.
 * It is a question about the save-under and not about the glass: the shadow is
 * off the glass until the present, so a pointer taken off here is never seen
 * off. VGA never asks it - its frame keeps the pointer lit (ovl_vframe). */
static int ovl_cur_hit(int cx, int cy, int drew)
{
    int sx0, sy0, sx1, sy1, i, w, h, wx;
    int *d;
    unsigned char *b;
    struct lem_l *p;
    struct lem_o *o;

    sx0 = cx - 7 - 8;               /* a byte either side: every write here */
    sx1 = cx + 7 + 8;               /* is whole bytes */
    sy0 = cy - 7;
    sy1 = cy + 7;
    if (!drew || sy0 >= LEM_VIEWH_PX)
        return 0;
    if (lem_r_shit(sx0, sy0, sx1, sy1))
        return 1;
    wx = lem_view;
    for (i = 0; i < lem_released; i++) {
        p = &lem_lm[i];
        if (p->fl & LF_GONE) {
            /* a spray writes the box its glass frame was drawn in (the
             * erase) and the box of the frame about to be drawn - exactly,
             * and not a +-128 square round the foot, which covered the whole
             * 320 x 160 view and took the save-under off on every drawn
             * frame of every explosion (wave 6's second review) */
            b = p->pbox;
            if (p->dpf && ovl_rhit(sx0, sy0, sx1, sy1, (b[0] << 3) - wx,
                                   b[2], (b[1] << 3) + 7 - wx, b[3]))
                return 1;
            if (ovl_pbox(p, (int)p->pf) &&
                ovl_rhit(sx0, sy0, sx1, sy1, (lem_bb0 << 3) - wx, lem_br0,
                         (lem_bb1 << 3) + 7 - wx, lem_br1))
                return 1;
            continue;
        }
        if (ovl_rhit(sx0, sy0, sx1, sy1, p->x - wx - 16, p->y - 40,
                     p->x - wx + 16, p->y + 8))
            return 1;
    }
    for (i = 0; i < lem_ndmg; i++) {
        d = &lem_dmg[i * 4];
        if (ovl_rhit(sx0, sy0, sx1, sy1, d[0] - wx - 8, d[1],
                     d[0] + d[2] - wx + 8, d[1] + d[3]))
            return 1;
    }
    for (i = 0; i < lem_nob; i++) {
        o = &lem_ob[i];
        if (o->drawn == o->frame && !o->dirty)
            continue;
        w = (int)lem_ty[o->id].w;
        h = (int)lem_ty[o->id].h;
        if (ovl_rhit(sx0, sy0, sx1, sy1, o->x - wx - 8, o->y,
                     o->x + w - wx + 8, o->y + h))
            return 1;
    }
    return 0;
}

/* ovl_pan_hit - THE PANEL SIDE'S TEST, on every card: will this tick draw a
 * panel pixel inside a BYTE the pointer's save-under at screen (cx, cy) holds?
 * Only what actually changes is asked about, which is wave 3's second review:
 * "any panel change at all" took a pointer resting on the skill buttons or
 * the minimap off for every frame - the clock, a held release rate, the
 * minimap's dots - and that is exactly where it rests when it is not aiming.
 *
 *   the status line   each CHANGED cell under the box (8x16 at row 0)
 *   the counts        each dirty count's 8x8 cell (b * 16 + 4, 17)
 *   the frame         the old and the new selection frame, (b * 16 + 1, 16)
 *                     15 x 24, when the selection moved
 *   the minimap       each cell under the box whose dot came or went or that
 *                     the mechanics cleared ((lem_mmn ^ lem_mmd) | lem_mmc -
 *                     ovl_mm_build() has run), and the view rectangle's old
 *                     and new outline when it moved (`ox` the old column)
 *
 * Panel coordinates: rows 0..39 below screen row 160. */
static int ovl_pan_hit(int cx, int cy, int vr, int ox)
{
    int x0, x1, y0, y1, b, c, r, m0, m1;
    unsigned char d;

    y0 = cy - 7 - LEM_VIEWH_PX;
    y1 = cy + 6 - LEM_VIEWH_PX;
    if (y1 < 0)
        return 0;
    if (y0 < 0)
        y0 = 0;
    x0 = (cx - 7) & ~7;             /* the save holds whole bytes */
    x1 = (cx + 6) | 7;
    if (x0 < 0)
        x0 = 0;
    if (x1 > LEM_VIEW_W - 1)
        x1 = LEM_VIEW_W - 1;
    if (y0 <= LEM_STAT_ROW + 15)
        for (c = x0 >> 3; c <= x1 >> 3; c++)
            if (lem_stnew[c] != lem_stold[c])
                return 1;
    for (b = 0; b < LB_COUNT; b++)
        if ((lem_cdirty & (1 << b)) &&
            ovl_rhit(x0, y0, x1, y1, b * 16 + 4, 17, b * 16 + 11, 24))
            return 1;
    if (lem_seldrawn != lem_skill &&
        ((lem_seldrawn >= 0 &&
          ovl_rhit(x0, y0, x1, y1, lem_seldrawn * 16 + 1, 16,
                   lem_seldrawn * 16 + 15, 39)) ||
         ovl_rhit(x0, y0, x1, y1, lem_skill * 16 + 1, 16,
                  lem_skill * 16 + 15, 39)))
        return 1;
    if (!ovl_rhit(x0, y0, x1, y1, LEM_MM_X, LEM_MM_Y,
                  LEM_MM_X + LEM_MM_W - 1, LEM_MM_Y + LEM_MM_H - 1))
        return 0;
    if (vr) {
        m0 = lem_view / LEM_MM_XSTEP;
        m1 = m0;
        if (ox >= 0 && ox < m0)
            m0 = ox;
        if (ox > m1)
            m1 = ox;
        if (ovl_rhit(x0, y0, x1, y1, LEM_MM_X + m0, LEM_MM_Y,
                     LEM_MM_X + m1 + LEM_VR_W - 1, LEM_MM_Y + LEM_MM_H - 1))
            return 1;
    }
    m0 = x0 - LEM_MM_X;
    m1 = x1 - LEM_MM_X;
    if (m0 < 0)
        m0 = 0;
    if (m1 > LEM_MM_W - 1)
        m1 = LEM_MM_W - 1;
    for (r = y0 - LEM_MM_Y - LEM_MM_YOFF; r <= y1 - LEM_MM_Y - LEM_MM_YOFF;
         r++) {
        if (r < 0 || r >= LEM_MM_ROWS)
            continue;
        for (c = m0; c <= m1; c++) {
            b = r * LEM_MM_STRIDE + (c >> 3);
            d = (unsigned char)((lem_mmn[b] ^ lem_mmd[b]) | lem_mmc[b]);
            if (d & (0x80 >> (c & 7)))
                return 1;
        }
    }
    return 0;
}

/* ovl_tick_draw - ONE TICK ONTO THE GLASS, and nothing when nothing changed.
 *
 * WHAT IS OFF THE GLASS, AND FOR HOW LONG (SPEC.md 116.4.6, wave 3's two
 * reviews):
 *   the lemmings  never, on VGA: the frame is composed off the glass and
 *                 latched on (ovl_vframe). On the shadow cards the frame is
 *                 drawn in a shadow the glass sees only at lem_r_present().
 *   the pointer   a game frame never takes it off on VGA - the compose brings
 *                 its save-under up to date and draws it into the scratch.
 *                 It comes off only (a) at a scroll's retrace, and goes
 *                 straight back on at its new place, because its world piece
 *                 moves with the Start Address; (b) around a PANEL write
 *                 inside the bytes it holds (ovl_pan_hit), right before the
 *                 minimap and the panel are drawn; (c) as one adjacent pair
 *                 at the very end when it moved or changed shape with a box
 *                 reaching the panel - a move wholly in the world, drawn
 *                 frame or not, rides a compose instead (lem_cmv).
 *   the beam      VGA waits for a vertical retrace ONLY to scroll - the Start
 *                 Address goes out before the wait and the pel pan after it,
 *                 so both land on one refresh. A frame that does not scroll
 *                 does not wait: the compose needs no retrace, and the wait
 *                 was up to 14.3 ms of every 59 ms frame (wave 3's review).
 *
 * `right` is the right button, for the hit test's priority. The hit test here
 * RECORDS Lemmix's fLastNonPrioritizedLemming only where Lemmix's own HitTest
 * calls run - at a frame boundary (UpdateLemmings :2646) and while paused
 * (Img_MouseMove :654); on every other tick it refreshes the status word and
 * the pointer's shape and leaves the right-click glitch's lemming alone. */
/* ovl_cmv_stepped - how many STEPPED lemmings of a part-stepped frame (the
 * pool before lem_cursor) have a drawn box meeting either of the pointer's
 * two boxes - the old 14x14 one where its save-under is, and the new one as
 * the compose takes it, its save-under's three-byte span. One
 * compare a lemming and no box computed (ovl_sbox is what costs); it is asked
 * only on a pointer-only tick while lem_infr holds. */
static int ovl_cmv_stepped(void)
{
    static unsigned char pb[8];
    int i, k, n, x, x1, y;
    unsigned char *b;

    for (k = 0; k < 2; k++) {
        /* EXACTLY THE SPANS ovl_vframe COMPOSES, clipped the way ovl_rc_px
         * clips them (wave 6's review): the old box is its 14 pixels and the
         * new one its save-under's 24 from x & ~7 - rounded BEFORE the clamp,
         * so a box hanging off the left edge counts bytes 0..1 as the compose
         * does and not a phantom third, and one at the right edge stops at
         * byte 199 */
        if (k) {
            x = (lem_view + lem_mx - 7) & ~7;
            x1 = x + 23;
        } else {
            x = lem_view + lem_curx - 7;
            x1 = x + 13;
        }
        y = (k ? lem_my : lem_cury) - 7;
        if (x < 0)
            x = 0;
        if (x1 > LEM_VRAM_W - 1)
            x1 = LEM_VRAM_W - 1;
        if (y < 0)
            y = 0;
        pb[k * 4] = (unsigned char)(x >> 3);
        pb[k * 4 + 1] = (unsigned char)(x1 >> 3);
        pb[k * 4 + 2] = (unsigned char)y;
        pb[k * 4 + 3] = (unsigned char)(y + 13);
    }
    n = 0;
    for (i = 0; i < lem_cursor && i < lem_released; i++) {
        b = &lem_obox[i * 4];
        if (b[0] == 0xFF)
            continue;
        for (k = 0; k < 8; k += 4)
            if (b[0] <= pb[k + 1] && pb[k] <= b[1] && b[2] <= pb[k + 3] &&
                pb[k + 2] <= b[3]) {
                n++;
                break;
            }
    }
    return n;
}

static void ovl_tick_draw(int drew, int right)
{
    int pd, shape, i, scroll, vr, moved, mm, ox;

    if (lem_my < LEM_VIEWH_PX)
        ovl_hittest(lem_view + lem_mx - 3, lem_my + 2, right,
                    drew || lem_paused);
    else
        lem_hit_n = 0;
    ovl_stat_build();
    pd = lem_cdirty != 0 || lem_seldrawn != lem_skill;
    for (i = 0; i < LEM_STAT_CELLS && !pd; i++)
        if (lem_stnew[i] != lem_stold[i])
            pd = 1;
    shape = ovl_cur_shape();
    scroll = ovl_scroll_due();
    ox = lem_view_drawn;
    vr = lem_view / LEM_MM_XSTEP != lem_view_drawn;
    moved = lem_mx != lem_curx || lem_my != lem_cury || shape != lem_curshape;
    mm = drew || lem_mmpend;
    if (!drew && !pd && !scroll && !vr && !moved && !mm && lem_cur_on)
        return;
    if (lem_kind_id() == LEM_RKIND_VGA) {
        /* A MOVING POINTER RIDES A COMPOSE when both its boxes are wholly in
         * the world and the view stays put: on a game frame's, and on a tick
         * with no frame through ovl_vframe(0), whose rectangles are then the
         * pointer's two boxes and whose sprites are only the lemmings that
         * touch them - so the latch puts the erasure and the new pointer down
         * in one write a byte, on the XT's many aiming ticks between drawn
         * frames as well as on the drawn one (wave 4 review). A scroll's tick
         * leaves it to the scroll's own pair below. */
        lem_cmv = !scroll && lem_cur_on && moved &&
                  lem_cury + 7 <= LEM_VIEWH_PX && lem_my + 7 <= LEM_VIEWH_PX;
        if (lem_cmv && !drew && lem_infr && ovl_cmv_stepped() > lem_cmv_cap) {
            lem_cmv = 0;            /* the glass pair below (LEM_CMV_CAP) */
            LEM_COUNT(lem_c_cmvfb);
        }
        /* THE COMPOSE GOES FIRST, AT THE NEW VIEW, and the Start Address
         * after it: ovl_sbox already reads the new lem_view, and every byte
         * composed is off the visible window until the pan shows it - so a
         * lemming a step brings into view is on the glass the moment its
         * terrain is (wave 4 review: composed after the pan, it was bare
         * terrain for the length of the compose). The pointer is composed at
         * the old offset, where its save-under is, and the pair after the
         * pan moves it. */
        if (drew) {
            ovl_frame_draw();
        } else if (scroll || lem_cmv) {
            /* A SCROLL WITH NO GAME FRAME - paused, or a frame still being
             * stepped on an XT - shows world the margin cull left without
             * lemmings: the ones that crossed into it are composed, and only
             * those (wave 3's third review) - or the pointer moved */
            LEM_CLEAR(lem_c_sprites);
            lem_c_objects = 0;
            ovl_vframe(0);
        }
        if (lem_cmv) {
            lem_cmv = 0;
            lem_curx = lem_mx;
            lem_cury = lem_my;
            lem_curshape = shape;
            moved = 0;
        }
        if (scroll) {
            ovl_view_follow(drew);  /* the Start Address */
            os88_fsx_wait(OS88_FSXW_VSYNC);
            lem_r_vpan();
            /* its world piece moved with the world: back at its place on the
             * same retrace - but ONLY when it reaches the world. Wholly in
             * the panel it is at an unpanned offset the Start Address does not
             * move, and the pair would put back and redraw the same pixels:
             * a flash on every tick of a scroll while it rests on the skill
             * buttons (wave 3's third review) */
            if (lem_cur_on && lem_cury - 7 < LEM_VIEWH_PX) {
                ovl_cursor_off();
                ovl_cursor_on();
                lem_curx = lem_mx;
                lem_cury = lem_my;
                lem_curshape = shape;
                moved = 0;
            }
        }
    } else {
        /* the pointer's save-under is in the shadow: off before anything
         * writes under it there, which no glass ever shows */
        if (lem_cur_on && (scroll || ovl_cur_hit(lem_curx, lem_cury, drew)))
            ovl_cursor_off();
        ovl_view_follow(drew);
        if (drew)
            ovl_frame_draw();
    }
    if (mm)
        ovl_mm_build();
    if (lem_cur_on && (pd || mm || vr) &&
        ovl_pan_hit(lem_curx, lem_cury, vr, ox))
        ovl_cursor_off();
    if (vr) {
        ovl_view_rect(lem_view_drawn);
        lem_view_drawn = lem_view / LEM_MM_XSTEP;
    }
    if (mm)
        ovl_mm_cells();
    ovl_panel_live();
    if (lem_cur_on && moved)
        ovl_cursor_off();
    if (!lem_cur_on)
        ovl_cursor_on();
    lem_curx = lem_mx;
    lem_cury = lem_my;
    lem_curshape = shape;
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

/* ovl_mouse_map - the desktop pointer into game raster coordinates.
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
static void ovl_mouse_map(void)
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
