/* ============================================================================
 * os8088 - apps/paccman/pmc_vid.c      the video and colour RAM (SPEC.md 91)
 *
 * DERIVED MATERIAL. Ported from Andre Weissflog's pacman.c
 * (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre Weissflog, at
 * commit 0f5ec5a: this file carries lines 993-1132 (vid_clear ..
 * vid_fruit_score) and 1377-1432 (game_init_playfield). The playfield map and
 * the tile codes it decodes to are the arcade board's; see
 * apps/paccman/README.md for the full provenance.
 *
 * #included by apps/paccman/paccman.c, which is one translation unit
 * (SPEC.md 73.1).
 *
 * ---------------------------------------------------------------------------
 * WHAT IS THE SAME AND WHAT IS NOT
 * ---------------------------------------------------------------------------
 * The reference holds the screen the way the Namco board holds it: a 28x36
 * byte video_ram of tile codes and a byte-for-byte color_ram beside it, and
 * every drawing routine in the whole program writes only those two. That model
 * is carried WHOLE, because it is what makes a damage-only repaint possible
 * here at all: the set of tiles a frame changed IS the set of bytes these
 * writers changed.
 *
 * Three departures, each forced:
 *
 *  - int2_t IS GONE. The reference passes tile positions as a two-int struct
 *    BY VALUE, 59 times; a struct by value is an address of an automatic and a
 *    rep movsb, which are rules 1 and 2 (SPEC.md 73.5, 73.5.1). Every position
 *    is two plain ints here.
 *
 *  - THE STRIDE IS 32, NOT 28. `y * 28` is an imul the gate can refuse
 *    outright (it needs a scratch register it cannot always prove dead), and
 *    the fix the LESSONS file names is to make the stride a power of two so
 *    the row is a shift. It costs 4 unused columns a row in each of two
 *    arrays - 288 bytes of bss, which is the cheap half of the ceiling
 *    (SPEC.md 73.9) - and buys a shift on every one of these writers.
 *
 *  - EVERY WRITE COMPARES FIRST. The reference redraws the whole tile buffer
 *    into a GPU texture each frame and does not care; here a tile that is
 *    written with the value it already holds must mark NOTHING, or the score
 *    strip, the lives row and the fruit list - all rewritten every game tick
 *    by game_update_tiles - would put four bands into every frame's damage
 *    for nothing. That is the compare-then-write rule of SPEC.md 91, and the
 *    harness's per-frame call count is what checks it.
 * ==========================================================================*/

/* --- the damage model -----------------------------------------------------
 * TWO column spans per tile ROW, each of which becomes one blit of one
 * 8-pixel BAND of the field. Clean is dmin > dmax, and the sentinels are
 * chosen so that any marked column lands inside: a clean row is (28, 0) and
 * 28 > 0. The two are kept SORTED and DISJOINT - span 1 is left of span 2 -
 * and span 2 is dirty only when span 1 is, so one test still answers "is this
 * band owed anything".
 *
 * WHY TWO AND NOT ONE, WHICH IS A NUMBER OFF THE 1bpp ADAPTERS AND NOT OFF
 * VGA. The first draft carried one span and justified it with "on the
 * OSAPI_GFX_BLITP path width is nearly free" - and BLITP is unreachable on
 * the two adapters an XT actually boots, because pmc_pick_path answers
 * PMC_P_BLIT1 on bpp 1 before the probe is asked. Priced off the measured
 * terms (apps/paccman/build.sh), ONE wasted column of a band costs
 *
 *   CGA           551 us composed + (4/28) x 2255 pack_1 +  (4/28) x 57 blit1
 *                 = ~880 us          (rowstep 2, and the tile is the MERGED
 *                                     one - a CGA tile is its own term)
 *   Hercules      703 us composed + (8/28) x 2255 pack_1 +  (8/28) x 57 blit1
 *                 = ~1,360 us
 *   VGA (BLITP)   703 us composed + (8/28) x 5222 pack_pl + (8/28) x 826
 *                 = ~2,430 us
 *
 * against 814 us (756 + the thunk) for the extra gfx call a second span
 * costs. So a gap of two clean columns already pays for the split on BOTH
 * paths and a gap of twenty-four - which is what the energizer blink makes of
 * rows 6 and 26, columns 1 and 26 - was ~33 ms of a ~110 ms 1bpp play frame,
 * spent recomposing maze tiles that had not changed.
 *
 * PMC_DGAP is where the two arms cross: a ONE-column gap is 1,050 us against
 * 814 on the 1bpp path, near enough a wash, so it is swallowed and the band
 * stays single; two or more opens the second span. Marking a RANGE is
 * pmc_mark_span and marking a tile is pmc_mark - a caller that means a
 * rectangle MUST use the range form, because two point marks two columns
 * apart now leave the column between them undrawn. */
#define PMC_DGAP  1

static unsigned char pmc_dmin[PMC_TILES_Y];
static unsigned char pmc_dmax[PMC_TILES_Y];
static unsigned char pmc_dmin2[PMC_TILES_Y];
static unsigned char pmc_dmax2[PMC_TILES_Y];

static void pmc_clean_row(int y)
{
    pmc_dmin[y]  = PMC_TILES_X;
    pmc_dmax[y]  = 0;
    pmc_dmin2[y] = PMC_TILES_X;
    pmc_dmax2[y] = 0;
}

static void pmc_clean(void)
{
    int y;
    for (y = 0; y < PMC_TILES_Y; y++)
        pmc_clean_row(y);
}

static void pmc_dirty_all(void)
{
    int y;
    for (y = 0; y < PMC_TILES_Y; y++) {
        pmc_dmin[y]  = 0;
        pmc_dmax[y]  = PMC_TILES_X - 1;
        pmc_dmin2[y] = PMC_TILES_X;
        pmc_dmax2[y] = 0;
    }
}

/* pmc_mark_span - columns c0..c1 of row y are owed.
 *
 * Three regions cannot be held in two spans, so one pair is joined, and the
 * pair joined is the one with the SMALLER clean gap between it: swallowing
 * g columns costs g x (a tile + its share of the pack and the blit) and the
 * alternative costs one gfx call, so the cheaper join is the narrower gap
 * every time. */
static void pmc_mark_span(int c0, int c1, int y)
{
    int a0, a1, b0, b1, gl, gr;

    if (c0 < 0)
        c0 = 0;
    if (c1 > PMC_TILES_X - 1)
        c1 = PMC_TILES_X - 1;
    if (c0 > c1)
        return;

    a0 = pmc_dmin[y];
    a1 = pmc_dmax[y];
    b0 = pmc_dmin2[y];
    b1 = pmc_dmax2[y];

    if (a0 > a1) {                              /* nothing owed yet */
        a0 = c0;
        a1 = c1;
    } else if (c0 <= a1 + 1 + PMC_DGAP && c1 + 1 + PMC_DGAP >= a0) {
        if (c0 < a0) a0 = c0;                   /* joins span 1 */
        if (c1 > a1) a1 = c1;
    } else if (b0 > b1) {                       /* one span: open the second */
        if (c1 < a0) {                          /* ...to the LEFT of it */
            b0 = a0; b1 = a1;
            a0 = c0; a1 = c1;
        } else {
            b0 = c0; b1 = c1;
        }
    } else if (c0 <= b1 + 1 + PMC_DGAP && c1 + 1 + PMC_DGAP >= b0) {
        if (c0 < b0) b0 = c0;                   /* joins span 2 */
        if (c1 > b1) b1 = c1;
    } else if (c1 < a0) {                       /* a third region, LEFT of both */
        if (a0 - c1 <= b0 - a1) {               /* cheaper to join span 1 */
            a0 = c0;
        } else {
            b0 = a0;                            /* ...than to join the pair */
            a0 = c0;
            a1 = c1;
        }
    } else if (c0 > b1) {                       /* ...RIGHT of both */
        if (c0 - b1 <= b0 - a1) {
            b1 = c1;
        } else {
            a1 = b1;
            b0 = c0;
            b1 = c1;
        }
    } else {                                    /* ...in the gap BETWEEN them */
        gl = c0 - a1;
        gr = b0 - c1;
        if (gl <= gr)
            a1 = c1;
        else
            b0 = c0;
    }

    if (b0 <= b1 && b0 <= a1 + 1 + PMC_DGAP) {  /* they met: one band is one
                                                 * call fewer than two */
        if (b1 > a1)
            a1 = b1;
        b0 = PMC_TILES_X;
        b1 = 0;
    }

    pmc_dmin[y]  = (unsigned char) a0;
    pmc_dmax[y]  = (unsigned char) a1;
    pmc_dmin2[y] = (unsigned char) b0;
    pmc_dmax2[y] = (unsigned char) b1;
}

static void pmc_mark(int x, int y)
{
    pmc_mark_span(x, x, y);
}

/* --- the two RAMs ---------------------------------------------------------
 * pmc_vram holds tile codes, pmc_cram colour codes, at PMC_VSTRIDE apart.
 * Nothing outside this file writes either of them. */
static unsigned char pmc_vram[PMC_TILES_Y * PMC_VSTRIDE];
static unsigned char pmc_cram[PMC_TILES_Y * PMC_VSTRIDE];

static int pmc_ok(int x, int y)                  /* pacman.c's valid_tile_pos */
{
    return x >= 0 && x < PMC_TILES_X && y >= 0 && y < PMC_TILES_Y;
}

/* vid_tile / vid_color / vid_color_tile, pacman.c 1013-1028. */
static void pmc_vid_tile(int x, int y, int tile)
{
    int i;
    if (!pmc_ok(x, y))
        return;
    i = (y << PMC_VSHIFT) + x;
    if (pmc_vram[i] != (unsigned char) tile) {
        pmc_vram[i] = tile;
        pmc_mark(x, y);
    }
}

static void pmc_vid_color(int x, int y, int color)
{
    int i;
    if (!pmc_ok(x, y))
        return;
    i = (y << PMC_VSHIFT) + x;
    if (pmc_cram[i] != (unsigned char) color) {
        pmc_cram[i] = color;
        pmc_mark(x, y);
    }
}

static void pmc_vid_color_tile(int x, int y, int color, int tile)
{
    pmc_vid_tile(x, y, tile);
    pmc_vid_color(x, y, color);
}

/* vid_clear, pacman.c 993-997 - a memset of both buffers there, and a loop
 * here, because a `rep stosb` addresses ES:DI and ES is the kernel's
 * (SPEC.md 73.5.1). The four unused columns of every row are cleared too:
 * they are never composed, and leaving them stale would be one more thing
 * that is true only until somebody widens the field. */
static void pmc_vid_clear(int tile, int color)
{
    int x, y;
    for (y = 0; y < PMC_TILES_Y; y++)
        for (x = 0; x < PMC_VSTRIDE; x++) {
            int i = (y << PMC_VSHIFT) + x;
            if (pmc_vram[i] == (unsigned char) tile
                && pmc_cram[i] == (unsigned char) color)
                continue;
            pmc_vram[i] = tile;
            pmc_cram[i] = color;
            if (x < PMC_TILES_X)        /* the 4 columns past the field are
                                         * cleared but are never composed, so
                                         * they may not mark a band dirty */
                pmc_mark(x, y);
        }
}

/* vid_color_playfield, pacman.c 999-1005: rows 3..33 of the colour buffer. */
static void pmc_vid_color_playfield(int color)
{
    int x, y;
    for (y = 3; y < PMC_TILES_Y - 2; y++)
        for (x = 0; x < PMC_TILES_X; x++)
            pmc_vid_color(x, y, color);
}

/* conv_char, pacman.c 1031-1042: ASCII into the arcade ROM's own character
 * set. Everything else passes through, which is why 'A'..'Z' and '0'..'9'
 * need no table: the Namco font is ASCII where it matters. */
static int pmc_conv(int c)
{
    if (c == ' ')  return 0x40;
    if (c == '/')  return 58;
    if (c == '-')  return 59;
    if (c == '"')  return 38;
    if (c == '!')  return 'Z' + 1;
    return c;
}

/* vid_color_text / vid_text, pacman.c 1056-1085. */
static void pmc_vid_color_text(int x, int y, int color, const char *s)
{
    while (*s && x < PMC_TILES_X) {
        pmc_vid_color_tile(x, y, color, pmc_conv((unsigned char) *s));
        s++;
        x++;
    }
}

static void pmc_vid_text(int x, int y, const char *s)
{
    while (*s && x < PMC_TILES_X) {
        pmc_vid_tile(x, y, pmc_conv((unsigned char) *s));
        s++;
        x++;
    }
}

/* vid_color_char, pacman.c 1046-1054. */
static void pmc_vid_color_char(int x, int y, int color, int c)
{
    pmc_vid_color_tile(x, y, color, pmc_conv(c));
}

/* vid_color_score, pacman.c 1092-1108: the digits right to left with a
 * trailing zero, so the score really is the arcade's own "score / 10 then a
 * 0". A zero score prints "00", which is what the machine does.
 *
 * The reference walks a uint32 down by tens and stops when it reaches zero;
 * here the number is already a digit array (pmc_time.c), so the same stopping
 * rule is "print up to the highest non-zero digit, and always digit 0". */
static void pmc_vid_score(int x, int y, int color, const unsigned char *d)
{
    int i, top;

    pmc_vid_color_char(x, y, color, '0');
    x--;
    top = 0;
    for (i = PMC_SCORE_DIGITS - 1; i > 0; i--)
        if (d[i]) {
            top = i;
            break;
        }
    for (i = 0; i <= top && x >= 0; i++) {
        pmc_vid_color_char(x, y, color, '0' + d[i]);
        x--;
    }
}

/* vid_fruit_score, pacman.c 1122-1131: the four tiles of a bonus number,
 * drawn where READY! and GAME OVER also live. */
static void pmc_vid_fruit_score(int fruit)
{
    int i, color;

    color = fruit == 0 ? PMC_COLOR_DOT : 0x03;      /* COLOR_FRUIT_SCORE */
    for (i = 0; i < 4; i++)
        pmc_vid_color_tile(12 + i, 20, color,
                           pmc_fruit_score_tiles[fruit * 4 + i]);
}

/* vid_draw_tile_quad, pacman.c 1111-1120: the 2x2 arrangement
 *     | t+1 | t+0 |
 *     | t+3 | t+2 |
 * that draws one life and one fruit symbol along the bottom border. */
static void pmc_vid_quad(int x, int y, int color, int tile)
{
    int xx, yy;
    for (yy = 0; yy < 2; yy++)
        for (xx = 0; xx < 2; xx++)
            pmc_vid_color_tile(x + xx, y + yy, color,
                               tile + yy * 2 + (1 - xx));
}

/* --- the playfield --------------------------------------------------------
 * game_init_playfield, pacman.c 1377-1432. The reference decodes a 31x28
 * ASCII map through a 128-entry char table at run time; both are gone, because
 * tools/paccman_assets.py did that on the host and pmc_rom.c carries the tile
 * codes themselves (SPEC.md 91). Rows 3..33, then the ghost house's gate
 * colour on the two door tiles. */
static void pmc_init_playfield(void)
{
    int x, y, i;
    pmc_vid_color_playfield(PMC_COLOR_DOT);
    i = 0;
    for (y = 3; y <= 33; y++)
        for (x = 0; x < PMC_TILES_X; x++)
            pmc_vid_tile(x, y, pmc_maze[i++]);
    pmc_vid_color(13, 15, 0x18);
    pmc_vid_color(14, 15, 0x18);
}
