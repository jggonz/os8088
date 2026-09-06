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
 * One column span per tile ROW, which is exactly one 8-pixel BAND of the
 * field. Clean is dmin > dmax, and the sentinels are chosen so that any marked
 * column lands inside: a clean row is (28, 0) and 28 > 0.
 *
 * A span rather than a per-tile mask, deliberately (SPEC.md 91): the pill
 * blink alone touches columns 1 and 26 of two rows, so a mask would save
 * composition and nothing else - and on the OSAPI_GFX_BLITP path width is
 * nearly free, being bytes at ~4 us each against a 756 us call floor. */
static unsigned char pmc_dmin[PMC_TILES_Y];
static unsigned char pmc_dmax[PMC_TILES_Y];

static void pmc_clean(void)
{
    int y;
    for (y = 0; y < PMC_TILES_Y; y++) {
        pmc_dmin[y] = PMC_TILES_X;
        pmc_dmax[y] = 0;
    }
}

static void pmc_dirty_all(void)
{
    int y;
    for (y = 0; y < PMC_TILES_Y; y++) {
        pmc_dmin[y] = 0;
        pmc_dmax[y] = PMC_TILES_X - 1;
    }
}

static void pmc_mark(int x, int y)
{
    if (x < (int) pmc_dmin[y])
        pmc_dmin[y] = x;
    if (x > (int) pmc_dmax[y])
        pmc_dmax[y] = x;
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
