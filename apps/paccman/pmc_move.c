/* ============================================================================
 * os8088 - apps/paccman/pmc_move.c    the movement rules
 *
 * DERIVED MATERIAL. Part of PACCMAN, a reimplementation of Andre Weissflog's
 * pacman.c (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre
 * Weissflog, at commit 0f5ec5a; this file carries pacman.c 1229-1361 (the
 * geometry helpers, can_move and move) and the tile predicates at 1268-1300.
 * See apps/paccman/README.md.
 *
 * #included by apps/paccman/paccman.c (one translation unit, SPEC.md 73.1).
 *
 * ---------------------------------------------------------------------------
 * WHAT THE PORT CHANGED, AND WHY EACH ONE WAS FORCED
 * ---------------------------------------------------------------------------
 *  - int2_t IS GONE. The reference passes and returns a two-int struct BY
 *    VALUE at 59 sites; a struct by value is the address of an automatic and a
 *    rep movsb, which are rules 1 and 2 (SPEC.md 73.5, 73.5.1). Positions are
 *    two plain ints, and the actors live in PARALLEL ARRAYS indexed 0 for
 *    Pac-Man and 1..4 for the ghosts, so `move_actor` writes them in place
 *    instead of returning a pair.
 *
 *  - tile_code_at and is_blocking_tile ARE MACROS. can_move is on the tick
 *    path - worker -> pmc_frame -> pmc_game_tick -> pmc_update_actors ->
 *    a ghost step -> can_move - and the worker declares OS88_STACK_256
 *    (SPEC.md 91). Three call levels where one will do is ~72 bytes of stack
 *    for nothing, and the two are one memory read each.
 *
 *  - `% TILE_WIDTH` IS `& 7`. Every position these see is inside the field
 *    (0..223 by x, 0..287 by y) because move() wraps before storing, so the
 *    two spellings agree - and `%` on a signed int is an `idiv`.
 * ==========================================================================*/

/* directions, pacman.c 261-267. Bit 0 clear is horizontal movement, which is
 * a property the animation tables and can_move both rely on. */
#define PMC_DIR_RIGHT 0
#define PMC_DIR_DOWN  1
#define PMC_DIR_LEFT  2
#define PMC_DIR_UP    3

/* the actors, pacman.c 342-352: 0 is Pac-Man and 1 + i is ghost i. */
#define PMC_NACT   5
#define PMC_A_PAC  0
#define PMC_A_GH0  1

static int           pmc_ax[PMC_NACT];      /* sprite CENTRE, field pixels */
static int           pmc_ay[PMC_NACT];
static unsigned char pmc_adir[PMC_NACT];
static unsigned      pmc_aanim[PMC_NACT];   /* bumped on every tick moved  */

static const signed char pmc_dx[4] = {  1,  0, -1,  0 };
static const signed char pmc_dy[4] = {  0,  1,  0, -1 };

/* tile_code_at / is_blocking_tile, pacman.c 1268-1276. The caller has already
 * clamped, exactly as the reference's asserts require. */
#define PMC_TILE_AT(tx, ty)   pmc_vram[((ty) << PMC_VSHIFT) + (tx)]
#define PMC_BLOCKS(tx, ty)    (PMC_TILE_AT(tx, ty) >= 0xC0)

static int pmc_reverse_dir(int dir)         /* pacman.c 1262-1266 */
{
    if (dir == PMC_DIR_RIGHT) return PMC_DIR_LEFT;
    if (dir == PMC_DIR_DOWN)  return PMC_DIR_UP;
    if (dir == PMC_DIR_LEFT)  return PMC_DIR_RIGHT;
    return PMC_DIR_DOWN;
}

/* is_tunnel / is_redzone, pacman.c 1288-1300. */
static int pmc_is_tunnel(int tx, int ty)
{
    return ty == 17 && (tx <= 5 || tx >= 22);
}

static int pmc_is_redzone(int tx, int ty)
{
    return tx >= 11 && tx <= 16 && (ty == 14 || ty == 26);
}

/* clamped_tile_pos, pacman.c 1234-1249: the playfield's own bounds, so a
 * lookahead off the top or the bottom border reads a real tile. */
static int pmc_clamp_tx(int tx)
{
    if (tx < 0) return 0;
    if (tx >= PMC_TILES_X) return PMC_TILES_X - 1;
    return tx;
}

static int pmc_clamp_ty(int ty)
{
    if (ty < 3) return 3;
    if (ty >= PMC_TILES_Y - 2) return PMC_TILES_Y - 3;
    return ty;
}

/* can_move, pacman.c 1302-1327. `cornering` is Pac-Man's diagonal shortcut
 * around a corner; a ghost is never given it. */
static int pmc_can_move(int x, int y, int dir, int cornering)
{
    int dx, dy, mmid, pmid, cx, cy;

    dx = pmc_dx[dir];
    dy = pmc_dy[dir];
    if (dy != 0) {
        mmid = 4 - (y & 7);
        pmid = 4 - (x & 7);
    } else {
        mmid = 4 - (x & 7);
        pmid = 4 - (y & 7);
    }
    cx = pmc_clamp_tx((x >> 3) + dx);
    cy = pmc_clamp_ty((y >> 3) + dy);
    if ((!cornering && pmid != 0) || (PMC_BLOCKS(cx, cy) && mmid == 0))
        return 0;
    return 1;
}

/* move, pacman.c 1329-1355, writing the actor in place. The tunnel wrap is
 * the only place x leaves the field, and it is why every caller of the tile
 * predicates above may assume a position inside it. */
static void pmc_move_actor(int a, int dir, int cornering)
{
    int x, y, m;

    x = pmc_ax[a] + pmc_dx[dir];
    y = pmc_ay[a] + pmc_dy[dir];

    if (cornering) {
        if (pmc_dx[dir] != 0) {
            m = 4 - (y & 7);
            if (m < 0) y--;
            else if (m > 0) y++;
        } else if (pmc_dy[dir] != 0) {
            m = 4 - (x & 7);
            if (m < 0) x--;
            else if (m > 0) x++;
        }
    }

    if (x < 0)
        x = PMC_FIELD_W - 1;
    else if (x >= PMC_FIELD_W)
        x = 0;

    pmc_ax[a] = x;
    pmc_ay[a] = y;
}

/* squared_distance_i2 in TILE coordinates, pacman.c 965-969. The field is
 * 28 x 36, so the largest square is 27*27 + 35*35 = 1,954 and an int holds
 * it; the reference's int32 and its 100,000 sentinel are not needed. */
static int pmc_sqdist(int x0, int y0, int x1, int y1)
{
    int dx, dy;

    dx = x1 - x0;
    dy = y1 - y0;
    return dx * dx + dy * dy;
}
