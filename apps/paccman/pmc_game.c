/* ============================================================================
 * os8088 - apps/paccman/pmc_game.c    the round, the actors and the rules
 *
 * DERIVED MATERIAL. Part of PACCMAN, a reimplementation of Andre Weissflog's
 * pacman.c (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre
 * Weissflog, at commit 0f5ec5a; this file carries pacman.c 1134-1228
 * (the sprite animation selectors), 1434-1637 (game_init, game_round_init,
 * game_update_tiles), 1639-2215 (the four ghosts, the dot counters and
 * game_update_actors) and 2217-2322 (game_tick). See apps/paccman/README.md.
 *
 * #included by apps/paccman/paccman.c (one translation unit, SPEC.md 73.1).
 *
 * ---------------------------------------------------------------------------
 * WHAT THE PORT CHANGED
 * ---------------------------------------------------------------------------
 *  - THE FIVE COMPOUND-LITERAL ASSIGNMENTS OF game_round_init ARE FIELD
 *    STORES INTO PARALLEL ARRAYS. `state.game.ghost[i] = (ghost_t){...}` is a
 *    struct assignment, which the compiler implements as a rep movsb into
 *    ES:DI - and ES is the kernel's (SPEC.md 73.5.1). The same is true of
 *    every `sprite_t` and `pacman_t` literal.
 *
 *  - THE GHOSTS' TWO TRIGGERS LIVE IN THE TRIGGER TABLE, at PMC_T_FRIGHT0 + i
 *    and PMC_T_EATEN0 + i, so `disable(&ghost->frightened)` is
 *    pmc_disable(PMC_T_FRIGHT0 + i) and nothing has to hold a pointer into a
 *    struct.
 *
 *  - levelspec() RETURNS AN INDEX, not a struct: the three columns are three
 *    const arrays in the generated pmc_rom.c, so the round's fruit, bonus and
 *    fright ticks are three indexed reads rather than a by-value return.
 *
 *  - THE TICK PATH IS FLAT BY DESIGN. The worker declares OS88_STACK_256
 *    (SPEC.md 91), so the deepest chain here is pmc_update_actors -> a ghost
 *    step -> pmc_can_move, and the ghost's state, target and direction are
 *    three siblings called from pmc_update_actors rather than a nest.
 * ==========================================================================*/

/* --- the reference's own constants, pacman.c 181-193 ---------------------- */
#define PMC_ANTEPORTAS_X   (14 * 8)
#define PMC_ANTEPORTAS_Y   (14 * 8 + 4)
#define PMC_GHOST_EATEN_FREEZE 60
#define PMC_PACMAN_EATEN_TICKS 60
#define PMC_PACMAN_DEATH_TICKS 150
#define PMC_GAMEOVER_TICKS     (3 * 60)
#define PMC_ROUNDWON_TICKS     (4 * 60)
#define PMC_FRUITACTIVE_TICKS  (10 * 60)
#define PMC_NUM_DOTS           244
#define PMC_FADE_TICKS         30
#define PMC_MAX_LEVELSPEC      21

/* freeze reasons, pacman.c 249-256 */
#define PMC_FZ_PRELUDE  1
#define PMC_FZ_READY    2
#define PMC_FZ_EATGHOST 4
#define PMC_FZ_DEAD     8
#define PMC_FZ_WON      16

/* ghost AI states, pacman.c 300-310 */
#define PMC_GS_NONE       0
#define PMC_GS_CHASE      1
#define PMC_GS_SCATTER    2
#define PMC_GS_FRIGHTENED 3
#define PMC_GS_EYES       4
#define PMC_GS_HOUSE      5
#define PMC_GS_LEAVE      6
#define PMC_GS_ENTER      7

/* the 'hardware' sprites, pacman.c 279-286 */
#define PMC_NSPR   6
#define PMC_SP_PAC 0
#define PMC_SP_GH0 1
#define PMC_SP_FRT 5

#define PMC_SPR_INVISIBLE 30
#define PMC_SPR_SCORE_200 40
#define PMC_SPR_PAC_CLOSED 48
#define PMC_TILE_LIFE      0x20

#define PMC_COLOR_PACMAN     0x09
#define PMC_COLOR_FRIGHTENED 0x11
#define PMC_COLOR_FRIGHT_BLINK 0x12
#define PMC_COLOR_GHOST_SCORE  0x18
#define PMC_COLOR_EYES         0x19
#define PMC_COLOR_WHITE_BORDER 0x1F

/* scatter targets in TILE coords, starting and house-target positions in
 * PIXEL coords - pacman.c 542-561, split into x and y for the same reason
 * int2_t is gone. */
static const unsigned char pmc_scat_x[4] = { 25,  2, 27,  0 };
static const unsigned char pmc_scat_y[4] = {  0,  0, 34, 34 };
static const int pmc_gstart_x[4] = { 14*8, 14*8, 12*8, 16*8 };
static const int pmc_gstart_y[4] = { 14*8+4, 17*8+4, 17*8+4, 17*8+4 };
static const int pmc_ghouse_x[4] = { 14*8, 14*8, 12*8, 16*8 };
static const int pmc_ghouse_y[4] = { 17*8+4, 17*8+4, 17*8+4, 17*8+4 };

/* --- the game's state ----------------------------------------------------- */
static int pmc_round;
static int pmc_lives;

/* What the reserve-life row and the fruit list were last DRAWN from. See
 * pmc_update_tiles: neither strip can change except at a game_init or a
 * round_init, and rewriting them on every game tick is 210 near calls that
 * provably mark nothing. */
static int pmc_shlives;
static int pmc_shround;

/* ...and what the ROUND-WON FLASH last recoloured the playfield to. Its own
 * reason, one order of magnitude worse than the strips': the flash colour is
 * bit 4 of since(WON), so it changes once every SIXTEEN game ticks, and
 * pmc_vid_color_playfield is 31 rows x 28 columns = 868 calls to
 * pmc_vid_color, each of which calls pmc_ok again. Ungated that is ~1,736
 * near calls a tick - 19 ms of call-and-ret alone at PERFORMANCE.md's 11 us,
 * nearer 25-30 with the loop and the index arithmetic - on every one of the
 * 180 ticks from WON+60 to READY, of which fifteen in sixteen prove nothing
 * changed. About 4.5 s of XT added to the most expensive animation the
 * program has. Compare-then-write keeps it out of the DAMAGE, so neither the
 * cost table nor a screendump can see it. */
static int pmc_shflash;

/* ...and how many times it actually ran, WHICH ONLY THE HARNESS CAN SEE.
 * Compare-then-write keeps the recolour out of the damage, so a wasted pass
 * costs no band, no gfx call and no pixel: the cost table prices 868 near
 * calls at exactly zero and no screendump shows them. Host-only, so the 8086
 * image carries neither the word nor the increment. */
#ifdef PMC_HOST
static unsigned pmc_n_flash;
#define PMC_FLASHRAN() (pmc_n_flash++)
#else
#define PMC_FLASHRAN() ((void) 0)
#endif

static int pmc_dots_eaten;
static int pmc_freeze;
static int pmc_nghosts_eaten;
static int pmc_gdc_active;              /* the GLOBAL dot counter is in use  */
static int pmc_gdc;
static int pmc_fruit;                   /* the active bonus fruit, 0 = none  */
static int pmc_input_on = 1;
/* WHICH SCREEN IS RUNNING, and it is PMC_MODE_ and not PMC_GS_. The ghost AI
 * states above are PMC_GS_NONE 0 .. PMC_GS_ENTER 7, so a second PMC_GS_
 * enumeration in the same file would make PMC_GS_GAME == PMC_GS_CHASE == 1
 * and PMC_GS_INTRO == PMC_GS_NONE == 0: `pmc_gstate[i] == PMC_GS_GAME` and
 * `pmc_mode == PMC_GS_CHASE` would both compile and both read as correct.
 * Both variables are live in this file and wave 3's attract screen is exactly
 * the code that touches the two. The name also matches the precedent's own
 * `pm_mode` (apps/pacman/pacman.asm). */
static int pmc_mode;                    /* 0 = intro (wave 3), 1 = game      */

#define PMC_MODE_INTRO 0
#define PMC_MODE_GAME  1

static unsigned char pmc_score[PMC_SCORE_DIGITS];
static unsigned char pmc_hiscore[PMC_SCORE_DIGITS];

static unsigned char pmc_gstate[4];
static unsigned char pmc_gnext[4];      /* the ghost AI looks one tile ahead */
static int           pmc_gtx[4];        /* the current target, tile coords   */
static int           pmc_gty[4];
static unsigned char pmc_gdots[4];
static unsigned char pmc_glimit[4];

/* the 'hardware' sprites, pacman.c 385-391 */
static unsigned char pmc_sp_on[PMC_NSPR];
static unsigned char pmc_sp_tile[PMC_NSPR];
static unsigned char pmc_sp_col[PMC_NSPR];
static unsigned char pmc_sp_flip[PMC_NSPR];     /* bit 0 flipx, bit 1 flipy  */
static int           pmc_sp_x[PMC_NSPR];        /* the sprite's TOP-LEFT     */
static int           pmc_sp_y[PMC_NSPR];

/* levelspec(), pacman.c 831-838: an INDEX into the three const columns. */
static int pmc_lvl(int round)
{
    return round >= PMC_MAX_LEVELSPEC ? PMC_MAX_LEVELSPEC - 1 : round;
}

static void pmc_disable_timers(void)     /* pacman.c 1434-1444 */
{
    pmc_disable(PMC_T_WON);
    pmc_disable(PMC_T_OVER);
    pmc_disable(PMC_T_DOT);
    pmc_disable(PMC_T_PILL);
    pmc_disable(PMC_T_GEATEN);
    pmc_disable(PMC_T_PMEATEN);
    pmc_disable(PMC_T_FRUITEAT);
    pmc_disable(PMC_T_LEAVE);
    pmc_disable(PMC_T_FRUIT);
}

/* --- the sprite animation selectors, pacman.c 1134-1228 ------------------- */
static void pmc_spr_pac(int dir, unsigned tick)
{
    static const unsigned char t[8] = { 44, 46, 48, 46,      /* horizontal */
                                        45, 47, 48, 47 };    /* vertical   */
    pmc_sp_tile[PMC_SP_PAC] = t[((dir & 1) << 2) + (int) ((tick >> 1) & 3)];
    pmc_sp_col[PMC_SP_PAC]  = PMC_COLOR_PACMAN;
    pmc_sp_flip[PMC_SP_PAC] = (dir == PMC_DIR_LEFT ? 1 : 0)
                            | (dir == PMC_DIR_UP   ? 2 : 0);
}

static void pmc_spr_pac_death(unsigned tick)
{
    unsigned t = 52 + (tick >> 3);
    if (t > 63)
        t = 63;
    pmc_sp_tile[PMC_SP_PAC] = (unsigned char) t;
    pmc_sp_flip[PMC_SP_PAC] = 0;
}

static void pmc_spr_ghost(int i, int dir, unsigned tick)
{
    static const unsigned char t[8] = { 32, 33, 34, 35, 36, 37, 38, 39 };
    pmc_sp_tile[PMC_SP_GH0 + i] = t[(dir << 1) + (int) ((tick >> 3) & 1)];
    pmc_sp_col[PMC_SP_GH0 + i]  = (unsigned char) (1 + 2 * i);
    pmc_sp_flip[PMC_SP_GH0 + i] = 0;
}

static void pmc_spr_ghost_fright(int i, unsigned tick)
{
    int fr = pmc_lvl_fright[pmc_lvl(pmc_round)];

    pmc_sp_tile[PMC_SP_GH0 + i] = (tick >> 2) & 1 ? 29 : 28;
    if (fr > 60 && tick > (unsigned) (fr - 60))
        pmc_sp_col[PMC_SP_GH0 + i] = (tick & 0x10) ? PMC_COLOR_FRIGHTENED
                                                   : PMC_COLOR_FRIGHT_BLINK;
    else
        pmc_sp_col[PMC_SP_GH0 + i] = PMC_COLOR_FRIGHTENED;
    pmc_sp_flip[PMC_SP_GH0 + i] = 0;
}

static void pmc_spr_ghost_eyes(int i, int dir)
{
    static const unsigned char t[4] = { 32, 34, 36, 38 };
    pmc_sp_tile[PMC_SP_GH0 + i] = t[dir];
    pmc_sp_col[PMC_SP_GH0 + i]  = PMC_COLOR_EYES;
    pmc_sp_flip[PMC_SP_GH0 + i] = 0;
}

/* spr_clear, pacman.c 1134-1136 - a memset there and a loop here, because a
 * `rep stosb` addresses ES:DI and ES is the kernel's (SPEC.md 73.5.1). */
static void pmc_spr_clear(void)
{
    int i;

    for (i = 0; i < PMC_NSPR; i++) {
        pmc_sp_on[i] = 0;
        pmc_sp_tile[i] = 0;
        pmc_sp_col[i] = 0;
        pmc_sp_flip[i] = 0;
        pmc_sp_x[i] = 0;
        pmc_sp_y[i] = 0;
    }
}

/* --- game_init, pacman.c 1446-1465 ----------------------------------------
 * The one-time set-up and the first picture. The order matters:
 * pmc_init_playfield() paints the whole playfield's colour, so PLAYER ONE and
 * READY! have to follow it or they would be recoloured to COLOR_DOT. */
static void pmc_game_init(void)
{
    pmc_input_on = 1;
    pmc_disable_timers();
    pmc_round = 0;
    pmc_freeze = PMC_FZ_PRELUDE;
    pmc_lives = PMC_NUM_LIVES;
    pmc_gdc_active = 0;
    pmc_gdc = 0;
    pmc_dots_eaten = 0;
    pmc_score_zero(pmc_score);

    /* AND THE SPRITES, WHICH THE REFERENCE GETS FOR FREE AND THIS PORT DOES
     * NOT. pacman.c only ever enters game_init from a fresh process or from
     * the attract screen, so its sprite array is always already zero; here
     * `N` and Game > New Game reach it MID-ROUND, and without this the two
     * seconds of prelude before game_round_init runs are drawn with the dead
     * game's Pac-Man and ghosts still standing in the maze. Seen on the glass
     * as a white ghost loose in the top-left corner under PLAYER ONE. */
    pmc_spr_clear();

    pmc_vid_clear(PMC_TILE_SPACE, PMC_COLOR_DOT);
    pmc_shlives = -1;                   /* the two bottom strips were just
                                         * cleared away: impossible shadows,
                                         * so the first tick paints them */
    pmc_shround = -1;
    pmc_vid_color_text(9, 0, PMC_COLOR_DEFAULT, "HIGH SCORE");
    pmc_init_playfield();
    pmc_shflash = PMC_COLOR_DOT;        /* ...and init_playfield just PAINTED
                                         * the field that colour, so the flash
                                         * shadow is a fact rather than an
                                         * impossible value: the first flip
                                         * after a round always repaints */
    pmc_vid_color_text(9, 14, pmc_text_ink(0x05), "PLAYER ONE");
    pmc_vid_color_text(11, 20, 0x09, "READY!");
}

/* --- game_round_init, pacman.c 1467-1582 ---------------------------------- */
static void pmc_round_init(void)
{
    int i;

    pmc_spr_clear();
    pmc_vid_color_text(9, 14, 0x10, "          ");   /* clear PLAYER ONE */

    if (pmc_dots_eaten == PMC_NUM_DOTS) {
        pmc_round++;
        pmc_dots_eaten = 0;
        pmc_init_playfield();
        pmc_shflash = PMC_COLOR_DOT;    /* as in game_init: the field is now
                                         * genuinely that colour again */
        pmc_gdc_active = 0;
    } else {
        if (pmc_lives != PMC_NUM_LIVES) {
            pmc_gdc_active = 1;
            pmc_gdc = 0;
        }
        pmc_lives--;
    }

    pmc_fruit = 0;
    pmc_freeze = PMC_FZ_READY;
    pmc_rndstate = 0x1234;
    pmc_nghosts_eaten = 0;
    pmc_disable_timers();

    pmc_vid_color_text(11, 20, 0x09, "READY!");
    pmc_start(PMC_T_LEAVE);

    /* Pac-Man starts running to the left, pacman.c 1508-1516 */
    pmc_adir[PMC_A_PAC] = PMC_DIR_LEFT;
    pmc_ax[PMC_A_PAC] = 14 * 8;
    pmc_ay[PMC_A_PAC] = 26 * 8 + 4;
    pmc_aanim[PMC_A_PAC] = 0;
    pmc_sp_on[PMC_SP_PAC] = 1;
    pmc_sp_col[PMC_SP_PAC] = PMC_COLOR_PACMAN;

    /* Blinky outside the house looking left and scattering; the other three
     * in the house, Pinky moving down and Inky and Clyde up. The dot limits
     * are the reference's, which does not vary them by round. */
    for (i = 0; i < 4; i++) {
        pmc_ax[PMC_A_GH0 + i] = pmc_gstart_x[i];
        pmc_ay[PMC_A_GH0 + i] = pmc_gstart_y[i];
        pmc_aanim[PMC_A_GH0 + i] = 0;
        pmc_disable(PMC_T_FRIGHT0 + i);
        pmc_disable(PMC_T_EATEN0 + i);
        pmc_gdots[i] = 0;
        pmc_sp_on[PMC_SP_GH0 + i] = 1;
        pmc_sp_col[PMC_SP_GH0 + i] = (unsigned char) (1 + 2 * i);
    }
    pmc_adir[PMC_A_GH0 + 0] = PMC_DIR_LEFT;
    pmc_gnext[0] = PMC_DIR_LEFT;
    pmc_gstate[0] = PMC_GS_SCATTER;
    pmc_glimit[0] = 0;

    pmc_adir[PMC_A_GH0 + 1] = PMC_DIR_DOWN;
    pmc_gnext[1] = PMC_DIR_DOWN;
    pmc_gstate[1] = PMC_GS_HOUSE;
    pmc_glimit[1] = 0;

    pmc_adir[PMC_A_GH0 + 2] = PMC_DIR_UP;
    pmc_gnext[2] = PMC_DIR_UP;
    pmc_gstate[2] = PMC_GS_HOUSE;
    pmc_glimit[2] = 30;

    pmc_adir[PMC_A_GH0 + 3] = PMC_DIR_UP;
    pmc_gnext[3] = PMC_DIR_UP;
    pmc_gstate[3] = PMC_GS_HOUSE;
    pmc_glimit[3] = 60;
}

/* --- game_update_tiles, pacman.c 1584-1637 --------------------------------
 * EVERY WRITE HERE HAPPENS ON EVERY GAME TICK, and almost none of them change
 * anything: pmc_vid.c's compare-then-write rule is what keeps the score strip,
 * the lives row and the fruit list out of the frame's damage (SPEC.md 91).
 * The harness's "identical rewrite" row is the check. */
static void pmc_update_tiles(void)
{
    static const unsigned char px[4] = {  1, 26,  1, 26 };
    static const unsigned char py[4] = {  6,  6, 26, 26 };
    int i, x, fr, c;

    pmc_vid_score(6, 1, PMC_COLOR_DEFAULT, pmc_score);
    if (!pmc_score_zerop(pmc_hiscore))
        pmc_vid_score(16, 1, PMC_COLOR_DEFAULT, pmc_hiscore);

    for (i = 0; i < 4; i++) {
        if (pmc_freeze)
            pmc_vid_color(px[i], py[i], PMC_COLOR_DOT);
        else
            pmc_vid_color(px[i], py[i], (pmc_tick_lo & 8) ? PMC_COLOR_DOT : 0);
    }

    if (pmc_after_once(PMC_T_FRUITEAT, 2 * 60))
        pmc_vid_fruit_score(0);

    /* THE TWO BOTTOM STRIPS ARE GATED ON THEIR OWN STATE, and this is the one
     * place in the port where the reference's structure is not carried
     * verbatim. pacman.c rewrites the reserve-life row and the fruit list on
     * every one of its 60 ticks a second, and neither can change except at a
     * game_init or a round_init. Compare-then-write keeps them out of the
     * DAMAGE, so they cost no band and no gfx call and the harness's cost
     * table prices them at exactly zero - but they still cost the CALLS.
     * Three life quads plus up to seven fruit quads is 10 x 21 = 210 near
     * calls, ~2.3 ms of call-and-ret alone at PERFORMANCE.md's 11 us, three
     * to six times a drawn frame: 7-15 ms a frame that provably marks
     * nothing. The score, the hiscore and the pill blink stay per-tick
     * because those really do change.
     *
     * The shadows start impossible, so the first tick after game_init's
     * pmc_vid_clear paints both strips back. */
    if (pmc_lives != pmc_shlives || pmc_round != pmc_shround) {
        pmc_shlives = pmc_lives;
        pmc_shround = pmc_round;

        for (i = 0; i < PMC_NUM_LIVES; i++)
            pmc_vid_quad(2 + 2 * i, 34, i < pmc_lives ? PMC_COLOR_PACMAN : 0,
                         PMC_TILE_LIFE);

        x = 24;
        for (i = pmc_round - 6; i <= pmc_round; i++) {
            if (i >= 0) {
                fr = pmc_lvl_fruit[pmc_lvl(i)];
                pmc_vid_quad(x, 34, pmc_fruit_tc[fr * 3 + 2],
                             pmc_fruit_tc[fr * 3 + 0]);
                x -= 2;
            }
        }
    }

    /* THE ROUND-WON FLASH IS SHADOWED for pmc_shflash's reason: the colour
     * flips once every sixteen ticks and the recolour is 868 pmc_vid_color
     * calls, so fifteen ticks in sixteen would spend ~25 ms proving nothing
     * moved. */
    if (pmc_after(PMC_T_WON, 60)) {
        c = (pmc_since_lo(PMC_T_WON) & 0x10) ? PMC_COLOR_DOT
                                             : PMC_COLOR_WHITE_BORDER;
        if (c != pmc_shflash) {
            pmc_shflash = c;
            PMC_FLASHRAN();
            pmc_vid_color_playfield(c);
        }
    }
}

/* --- game_update_sprites, pacman.c 1639-1731 ------------------------------ */
static void pmc_update_sprites(void)
{
    int i;

    if (pmc_sp_on[PMC_SP_PAC]) {
        pmc_sp_x[PMC_SP_PAC] = pmc_ax[PMC_A_PAC] - 8;
        pmc_sp_y[PMC_SP_PAC] = pmc_ay[PMC_A_PAC] - 8;
        if (pmc_freeze & PMC_FZ_EATGHOST)
            pmc_sp_tile[PMC_SP_PAC] = PMC_SPR_INVISIBLE;
        else if (pmc_freeze & (PMC_FZ_PRELUDE | PMC_FZ_READY))
            pmc_sp_tile[PMC_SP_PAC] = PMC_SPR_PAC_CLOSED;
        else if (pmc_freeze & PMC_FZ_DEAD) {
            if (pmc_after(PMC_T_PMEATEN, PMC_PACMAN_EATEN_TICKS))
                pmc_spr_pac_death(pmc_since(PMC_T_PMEATEN)
                                  - PMC_PACMAN_EATEN_TICKS);
        } else
            pmc_spr_pac(pmc_adir[PMC_A_PAC], pmc_aanim[PMC_A_PAC]);
    }

    for (i = 0; i < 4; i++) {
        if (!pmc_sp_on[PMC_SP_GH0 + i])
            continue;
        pmc_sp_x[PMC_SP_GH0 + i] = pmc_ax[PMC_A_GH0 + i] - 8;
        pmc_sp_y[PMC_SP_GH0 + i] = pmc_ay[PMC_A_GH0 + i] - 8;
        if (pmc_freeze & PMC_FZ_DEAD) {
            if (pmc_after(PMC_T_PMEATEN, PMC_PACMAN_EATEN_TICKS))
                pmc_sp_tile[PMC_SP_GH0 + i] = PMC_SPR_INVISIBLE;
        } else if (pmc_freeze & PMC_FZ_WON) {
            pmc_sp_tile[PMC_SP_GH0 + i] = PMC_SPR_INVISIBLE;
        } else if (pmc_gstate[i] == PMC_GS_EYES) {
            if (pmc_before(PMC_T_EATEN0 + i, PMC_GHOST_EATEN_FREEZE)) {
                pmc_sp_tile[PMC_SP_GH0 + i] =
                    (unsigned char) (PMC_SPR_SCORE_200 + pmc_nghosts_eaten - 1);
                pmc_sp_col[PMC_SP_GH0 + i] = PMC_COLOR_GHOST_SCORE;
            } else
                pmc_spr_ghost_eyes(i, pmc_gnext[i]);
        } else if (pmc_gstate[i] == PMC_GS_ENTER) {
            pmc_spr_ghost_eyes(i, pmc_adir[PMC_A_GH0 + i]);
        } else if (pmc_gstate[i] == PMC_GS_FRIGHTENED) {
            pmc_spr_ghost_fright(i, pmc_since(PMC_T_FRIGHT0 + i));
        } else {
            pmc_spr_ghost(i, pmc_gnext[i], pmc_aanim[PMC_A_GH0 + i]);
        }
    }

    if (pmc_fruit == 0)
        pmc_sp_on[PMC_SP_FRT] = 0;
    else {
        pmc_sp_on[PMC_SP_FRT] = 1;
        pmc_sp_x[PMC_SP_FRT] = 13 * 8;
        pmc_sp_y[PMC_SP_FRT] = 19 * 8 + 4;
        pmc_sp_tile[PMC_SP_FRT] = pmc_fruit_tc[pmc_fruit * 3 + 1];
        pmc_sp_col[PMC_SP_FRT] = pmc_fruit_tc[pmc_fruit * 3 + 2];
    }
}

/* --- speeds and the phase schedule, pacman.c 1733-1786 -------------------- */
static int pmc_pac_should_move(void)
{
    if (pmc_now(PMC_T_DOT))
        return 0;                       /* a dot costs one tick     */
    if (pmc_before(PMC_T_PILL, 3))
        return 0;                       /* ...and a pill three      */
    return (pmc_tick_lo & 7) != 0;      /* seven ticks of eight, pacman.c 1746 */
}

static int pmc_ghost_speed(int i)
{
    int a = PMC_A_GH0 + i;

    if (pmc_gstate[i] == PMC_GS_HOUSE || pmc_gstate[i] == PMC_GS_LEAVE
        || pmc_gstate[i] == PMC_GS_FRIGHTENED)
        return (int) (pmc_tick_lo & 1);
    if (pmc_gstate[i] == PMC_GS_EYES || pmc_gstate[i] == PMC_GS_ENTER)
        return (pmc_tick_lo & 1) ? 1 : 2;
    if (pmc_is_tunnel(pmc_ax[a] >> 3, pmc_ay[a] >> 3))
        return (pmc_tick_lo & 1) ? 1 : 0;   /* `(tick*2) % 4` without a div */
    return (pmc_tick_lo % 7) ? 1 : 0;
}

static int pmc_phase(void)              /* pacman.c 1776-1786 */
{
    unsigned t = pmc_since(PMC_T_ROUND);

    if (t == PMC_NEVER)  return PMC_GS_SCATTER;
    if (t <  7 * 60)     return PMC_GS_SCATTER;
    if (t < 27 * 60)     return PMC_GS_CHASE;
    if (t < 34 * 60)     return PMC_GS_SCATTER;
    if (t < 54 * 60)     return PMC_GS_CHASE;
    if (t < 59 * 60)     return PMC_GS_SCATTER;
    if (t < 79 * 60)     return PMC_GS_CHASE;
    if (t < 84 * 60)     return PMC_GS_SCATTER;
    return PMC_GS_CHASE;
}

/* --- game_update_ghost_state, pacman.c 1788-1885 -------------------------- */
static void pmc_ghost_state(int i)
{
    int a = PMC_A_GH0 + i;
    int ns = pmc_gstate[i];

    if (pmc_gstate[i] == PMC_GS_EYES) {
        if (pmc_ax[a] >= PMC_ANTEPORTAS_X - 1 && pmc_ax[a] <= PMC_ANTEPORTAS_X + 1
            && pmc_ay[a] >= PMC_ANTEPORTAS_Y - 1 && pmc_ay[a] <= PMC_ANTEPORTAS_Y + 1)
            ns = PMC_GS_ENTER;
    } else if (pmc_gstate[i] == PMC_GS_ENTER) {
        if (pmc_ax[a] >= pmc_ghouse_x[i] - 1 && pmc_ax[a] <= pmc_ghouse_x[i] + 1
            && pmc_ay[a] >= pmc_ghouse_y[i] - 1 && pmc_ay[a] <= pmc_ghouse_y[i] + 1)
            ns = PMC_GS_LEAVE;
    } else if (pmc_gstate[i] == PMC_GS_HOUSE) {
        if (pmc_after_once(PMC_T_LEAVE, 4 * 60)) {
            ns = PMC_GS_LEAVE;
            pmc_start(PMC_T_LEAVE);
        } else if (pmc_gdc_active) {
            if (i == 1 && pmc_gdc == 7)
                ns = PMC_GS_LEAVE;
            else if (i == 2 && pmc_gdc == 17)
                ns = PMC_GS_LEAVE;
            else if (i == 3 && pmc_gdc == 32) {
                ns = PMC_GS_LEAVE;
                pmc_gdc_active = 0;
            }
        } else if (pmc_gdots[i] == pmc_glimit[i])
            ns = PMC_GS_LEAVE;
    } else if (pmc_gstate[i] == PMC_GS_LEAVE) {
        if (pmc_ay[a] == PMC_ANTEPORTAS_Y)
            ns = PMC_GS_SCATTER;
    } else {
        if (pmc_before(PMC_T_FRIGHT0 + i, pmc_lvl_fright[pmc_lvl(pmc_round)]))
            ns = PMC_GS_FRIGHTENED;
        else
            ns = pmc_phase();
    }

    if (ns != pmc_gstate[i]) {
        if (pmc_gstate[i] == PMC_GS_LEAVE) {
            pmc_gnext[i] = PMC_DIR_LEFT;
            pmc_adir[a] = PMC_DIR_LEFT;
        } else if (pmc_gstate[i] == PMC_GS_ENTER) {
            pmc_disable(PMC_T_FRIGHT0 + i);
        } else if (pmc_gstate[i] == PMC_GS_SCATTER
                   || pmc_gstate[i] == PMC_GS_CHASE) {
            pmc_gnext[i] = (unsigned char) pmc_reverse_dir(pmc_adir[a]);
        }
        pmc_gstate[i] = (unsigned char) ns;
    }
}

/* --- game_update_ghost_target, pacman.c 1887-1959 ------------------------- */
static void pmc_ghost_target(int i)
{
    int a = PMC_A_GH0 + i;
    int px, py, dx, dy, bx, by, tx, ty;

    if (pmc_gstate[i] == PMC_GS_SCATTER) {
        pmc_gtx[i] = pmc_scat_x[i];
        pmc_gty[i] = pmc_scat_y[i];
    } else if (pmc_gstate[i] == PMC_GS_CHASE) {
        px = pmc_ax[PMC_A_PAC] >> 3;
        py = pmc_ay[PMC_A_PAC] >> 3;
        dx = pmc_dx[pmc_adir[PMC_A_PAC]];
        dy = pmc_dy[pmc_adir[PMC_A_PAC]];
        if (i == 0) {                           /* Blinky chases directly */
            pmc_gtx[i] = px;
            pmc_gty[i] = py;
        } else if (i == 1) {                    /* Pinky, four tiles ahead */
            pmc_gtx[i] = px + 4 * dx;
            pmc_gty[i] = py + 4 * dy;
        } else if (i == 2) {                    /* Inky, through Blinky    */
            bx = pmc_ax[PMC_A_GH0] >> 3;
            by = pmc_ay[PMC_A_GH0] >> 3;
            tx = px + 2 * dx;
            ty = py + 2 * dy;
            pmc_gtx[i] = bx + 2 * (tx - bx);
            pmc_gty[i] = by + 2 * (ty - by);
        } else {                                /* Clyde, unless close     */
            if (pmc_sqdist(pmc_ax[a] >> 3, pmc_ay[a] >> 3, px, py) > 64) {
                pmc_gtx[i] = px;
                pmc_gty[i] = py;
            } else {
                pmc_gtx[i] = pmc_scat_x[3];
                pmc_gty[i] = pmc_scat_y[3];
            }
        }
    } else if (pmc_gstate[i] == PMC_GS_FRIGHTENED) {
        pmc_gtx[i] = pmc_rand_tx();
        pmc_gty[i] = pmc_rand_ty();
    } else if (pmc_gstate[i] == PMC_GS_EYES) {
        pmc_gtx[i] = 13;
        pmc_gty[i] = 14;
    }
}

/* --- game_update_ghost_dir, pacman.c 1961-2058 ---------------------------
 * Answers 1 when the movement must happen whatever is in the way, which is
 * how the reference walks a ghost through the house's own walls. */
static int pmc_ghost_dir(int i)
{
    static const unsigned char order[4] = { PMC_DIR_UP, PMC_DIR_LEFT,
                                            PMC_DIR_DOWN, PMC_DIR_RIGHT };
    int a = PMC_A_GH0 + i;
    int j, d, rd, lx, ly, tx, ty, best, dist, mid;

    if (pmc_gstate[i] == PMC_GS_HOUSE) {
        if (pmc_ay[a] <= 17 * 8)
            pmc_gnext[i] = PMC_DIR_DOWN;
        else if (pmc_ay[a] >= 18 * 8)
            pmc_gnext[i] = PMC_DIR_UP;
        pmc_adir[a] = pmc_gnext[i];
        return 1;
    }
    if (pmc_gstate[i] == PMC_GS_LEAVE) {
        if (pmc_ax[a] == PMC_ANTEPORTAS_X) {
            if (pmc_ay[a] > PMC_ANTEPORTAS_Y)
                pmc_gnext[i] = PMC_DIR_UP;
        } else {
            mid = 17 * 8 + 4;
            if (pmc_ay[a] > mid)
                pmc_gnext[i] = PMC_DIR_UP;
            else if (pmc_ay[a] < mid)
                pmc_gnext[i] = PMC_DIR_DOWN;
            else
                pmc_gnext[i] = (unsigned char)
                    (pmc_ax[a] > PMC_ANTEPORTAS_X ? PMC_DIR_LEFT
                                                  : PMC_DIR_RIGHT);
        }
        pmc_adir[a] = pmc_gnext[i];
        return 1;
    }
    if (pmc_gstate[i] == PMC_GS_ENTER) {
        if ((pmc_ay[a] >> 3) == 14) {
            if (pmc_ax[a] != PMC_ANTEPORTAS_X)
                pmc_gnext[i] = (unsigned char)
                    (pmc_ax[a] < PMC_ANTEPORTAS_X ? PMC_DIR_RIGHT
                                                  : PMC_DIR_LEFT);
            else
                pmc_gnext[i] = PMC_DIR_DOWN;
        } else if (pmc_ay[a] == pmc_ghouse_y[i]) {
            pmc_gnext[i] = (unsigned char)
                (pmc_ax[a] < pmc_ghouse_x[i] ? PMC_DIR_RIGHT : PMC_DIR_LEFT);
        }
        pmc_adir[a] = pmc_gnext[i];
        return 1;
    }

    /* scatter / chase / frightened: turn only on a tile midpoint */
    if ((4 - (pmc_ax[a] & 7)) != 0 || (4 - (pmc_ay[a] & 7)) != 0)
        return 0;

    pmc_adir[a] = pmc_gnext[i];
    lx = (pmc_ax[a] >> 3) + pmc_dx[pmc_adir[a]];
    ly = (pmc_ay[a] >> 3) + pmc_dy[pmc_adir[a]];

    best = 0x7FFF;
    for (j = 0; j < 4; j++) {
        d = order[j];
        if (d == PMC_DIR_UP && pmc_gstate[i] != PMC_GS_EYES
            && pmc_is_redzone(lx, ly))
            continue;
        rd = pmc_reverse_dir(d);
        if (rd == pmc_adir[a])
            continue;
        tx = pmc_clamp_tx(lx + pmc_dx[d]);
        ty = pmc_clamp_ty(ly + pmc_dy[d]);
        if (PMC_BLOCKS(tx, ty))
            continue;
        dist = pmc_sqdist(tx, ty, pmc_gtx[i], pmc_gty[i]);
        if (dist < best) {
            best = dist;
            pmc_gnext[i] = (unsigned char) d;
        }
    }
    return 0;
}

/* --- the two dot counters, pacman.c 2060-2107 ----------------------------- */
static void pmc_house_counters(void)
{
    int i;

    if (pmc_gdc_active) {
        pmc_gdc++;
        return;
    }
    for (i = 0; i < 4; i++)
        if (pmc_gdots[i] < pmc_glimit[i]) {
            pmc_gdots[i]++;
            return;
        }
}

static void pmc_dots_eaten_update(void)
{
    pmc_dots_eaten++;
    if (pmc_dots_eaten == PMC_NUM_DOTS) {
        pmc_start(PMC_T_WON);
        pmc_snd_clear();                /* the round is over: everything stops,
                                         * siren included (pacman.c 2094) */
    } else if (pmc_dots_eaten == 70 || pmc_dots_eaten == 170)
        pmc_start(PMC_T_FRUIT);

    /* THE CRUNCH ALTERNATES BETWEEN TWO EFFECTS, one falling and one rising,
     * which is what gives the arcade its two-note munch (pacman.c 2100-2107).
     * Both are five game ticks long and the speaker is sampled once per OS
     * tick - about every 3.3 game ticks - so a crunch lands as one or two
     * tones rather than five: stated in SPEC.md 91, not tuned. */
    if (pmc_dots_eaten & 1)
        pmc_snd_start(2, PMC_SK_EATDOT1);
    else
        pmc_snd_start(2, PMC_SK_EATDOT2);
}

/* --- game_update_actors, pacman.c 2109-2214 -------------------------------
 * The whole tick path's widest function, and it is deliberately ONE level
 * above the ghost steps: the chain is worker -> pmc_frame -> pmc_game_tick ->
 * here -> a ghost step -> pmc_can_move, which is what OS88_STACK_256 buys
 * (SPEC.md 91). */
static void pmc_update_actors(void)
{
    int i, want, tx, ty, gtx, gty, n, force, sc;

    if (pmc_pac_should_move()) {
        want = pmc_input_dir(pmc_adir[PMC_A_PAC]);
        if (pmc_can_move(pmc_ax[PMC_A_PAC], pmc_ay[PMC_A_PAC], want, 1))
            pmc_adir[PMC_A_PAC] = (unsigned char) want;
        if (pmc_can_move(pmc_ax[PMC_A_PAC], pmc_ay[PMC_A_PAC],
                         pmc_adir[PMC_A_PAC], 1)) {
            pmc_move_actor(PMC_A_PAC, pmc_adir[PMC_A_PAC], 1);
            pmc_aanim[PMC_A_PAC]++;
        }

        tx = pmc_ax[PMC_A_PAC] >> 3;
        ty = pmc_ay[PMC_A_PAC] >> 3;
        if (PMC_TILE_AT(tx, ty) == PMC_TILE_DOT) {
            pmc_vid_tile(tx, ty, PMC_TILE_SPACE);
            pmc_score_add(pmc_score, 1);
            pmc_start(PMC_T_DOT);
            pmc_start(PMC_T_LEAVE);
            pmc_dots_eaten_update();
            pmc_house_counters();
        }
        if (PMC_TILE_AT(tx, ty) == PMC_TILE_PILL) {
            pmc_vid_tile(tx, ty, PMC_TILE_SPACE);
            pmc_score_add(pmc_score, 5);
            pmc_dots_eaten_update();
            pmc_start(PMC_T_PILL);
            pmc_nghosts_eaten = 0;
            for (i = 0; i < 4; i++)
                pmc_start(PMC_T_FRIGHT0 + i);
            pmc_snd_start(1, PMC_SK_FRIGHT);    /* the warble replaces the
                                                 * siren on voice 1 until the
                                                 * fright timer runs out */
        }

        if (pmc_fruit != 0) {
            if (((pmc_ax[PMC_A_PAC] + 4) >> 3) == 14 && ty == 20) {
                pmc_start(PMC_T_FRUITEAT);
                pmc_score_add(pmc_score,
                              (int) pmc_lvl_bonus[pmc_lvl(pmc_round)]);
                pmc_vid_fruit_score(pmc_fruit);
                pmc_fruit = 0;
                pmc_snd_start(2, PMC_SK_EATFRUIT);
            }
        }

        for (i = 0; i < 4; i++) {
            gtx = pmc_ax[PMC_A_GH0 + i] >> 3;
            gty = pmc_ay[PMC_A_GH0 + i] >> 3;
            if (tx != gtx || ty != gty)
                continue;
            if (pmc_gstate[i] == PMC_GS_FRIGHTENED) {
                pmc_gstate[i] = PMC_GS_EYES;
                pmc_start(PMC_T_EATEN0 + i);
                pmc_start(PMC_T_GEATEN);
                pmc_nghosts_eaten++;
                sc = 10;            /* 10 * (1 << n): 20, 40, 80, 160,   */
                n = pmc_nghosts_eaten;  /* which is 200..1600 on the glass   */
                while (n-- > 0)
                    sc += sc;
                pmc_score_add(pmc_score, sc);
                pmc_freeze |= PMC_FZ_EATGHOST;
                pmc_snd_start(2, PMC_SK_EATGHOST);
            } else if (pmc_gstate[i] == PMC_GS_CHASE
                       || pmc_gstate[i] == PMC_GS_SCATTER) {
                pmc_snd_clear();        /* everything stops the instant he is
                                         * caught; the death tune starts sixty
                                         * ticks later (pacman.c 2178-2180) */
                pmc_start(PMC_T_PMEATEN);
                pmc_freeze |= PMC_FZ_DEAD;
                if (pmc_lives > 0)
                    pmc_start_after(PMC_T_READY, PMC_PACMAN_EATEN_TICKS
                                                 + PMC_PACMAN_DEATH_TICKS);
                else
                    pmc_start_after(PMC_T_OVER, PMC_PACMAN_EATEN_TICKS
                                                + PMC_PACMAN_DEATH_TICKS);
            }
        }
    }

    for (i = 0; i < 4; i++) {
        pmc_ghost_state(i);
        pmc_ghost_target(i);
        n = pmc_ghost_speed(i);
        while (n-- > 0) {
            force = pmc_ghost_dir(i);
            if (force || pmc_can_move(pmc_ax[PMC_A_GH0 + i],
                                      pmc_ay[PMC_A_GH0 + i],
                                      pmc_adir[PMC_A_GH0 + i], 0)) {
                pmc_move_actor(PMC_A_GH0 + i, pmc_adir[PMC_A_GH0 + i], 0);
                pmc_aanim[PMC_A_GH0 + i]++;
            }
        }
    }
}

/* --- game_tick, pacman.c 2217-2322 ---------------------------------------- */
static void pmc_game_tick(void)
{
    if (pmc_now(PMC_T_GAME)) {
        pmc_start(PMC_T_FADEIN);
        pmc_start_after(PMC_T_READY, 2 * 60);
        pmc_snd_start(0, PMC_SK_PRELUDE);   /* the two-second opening tune -
                                             * its MELODY is voice 1 of the
                                             * dump and its bass voice 0 */
        pmc_game_init();
    }
    if (pmc_now(PMC_T_READY)) {
        pmc_round_init();
        pmc_start_after(PMC_T_ROUND, 2 * 60 + 10);
    }
    if (pmc_now(PMC_T_ROUND)) {
        pmc_freeze &= ~PMC_FZ_READY;
        pmc_vid_color_text(11, 20, 0x10, "      ");
        pmc_snd_start(1, PMC_SK_WEEOOH);    /* ...and play begins with the
                                             * siren, which never stops */
    }

    if (pmc_now(PMC_T_FRUIT))
        pmc_fruit = pmc_lvl_fruit[pmc_lvl(pmc_round)];
    else if (pmc_after_once(PMC_T_FRUIT, PMC_FRUITACTIVE_TICKS))
        pmc_fruit = 0;

    /* the fright timer running out puts the siren back over the warble
     * (pacman.c 2251-2256) - the same level table the ghosts' own blink
     * uses */
    if (pmc_after_once(PMC_T_PILL, pmc_lvl_fright[pmc_lvl(pmc_round)]))
        pmc_snd_start(1, PMC_SK_WEEOOH);

    if (pmc_freeze & PMC_FZ_EATGHOST)
        if (pmc_after_once(PMC_T_GEATEN, PMC_GHOST_EATEN_FREEZE))
            pmc_freeze &= ~PMC_FZ_EATGHOST;

    /* the death tune, sixty ticks after he was caught - which is where the
     * death ANIMATION starts too (pacman.c 2266-2268) */
    if (pmc_after_once(PMC_T_PMEATEN, PMC_PACMAN_EATEN_TICKS))
        pmc_snd_start(2, PMC_SK_DEAD);

    if (!pmc_freeze)
        pmc_update_actors();
    pmc_update_tiles();
    pmc_update_sprites();

    if (pmc_score_above(pmc_score, pmc_hiscore))
        pmc_score_copy(pmc_hiscore, pmc_score);

    if (pmc_now(PMC_T_WON)) {
        pmc_freeze |= PMC_FZ_WON;
        pmc_start_after(PMC_T_READY, PMC_ROUNDWON_TICKS);
    }
    if (pmc_now(PMC_T_OVER)) {
        /* THE ONE MESSAGE THE PLAYER MOST NEEDS TO READ, and the reference
         * writes it in BLINKY's red 1 - a colour that resolves to the mono
         * class table's 50% dither, so on either 1bpp adapter it was a
         * full-size checkerboard smear. pmc_text_ink is the rule and this is
         * the second of the two LABELS on the game screen it governs (the
         * first is PLAYER ONE, in INKY's cyan 5); the ghost PICTURES keep the
         * arcade colour everywhere. */
        pmc_vid_color_text(9, 20, pmc_text_ink(0x01), "GAME  OVER");
        pmc_input_dis();        /* input_disable(): the three seconds of GAME
                                 * OVER and the fade after it belong to nobody,
                                 * and the key that ends them is the ATTRACT
                                 * screen's any-key, not this one's */
        pmc_start_after(PMC_T_FADEOUT, PMC_GAMEOVER_TICKS);
        pmc_start_after(PMC_T_INTRO, PMC_GAMEOVER_TICKS + PMC_FADE_TICKS);
    }
}

/* pmc_new_game - what N and Game > New Game do, and what os88_main arms.
 *
 * The reference reaches game_init through `now(state.game.started)` inside
 * game_tick, and so does this: PMC_T_GAME fires on the NEXT game tick and
 * game_init draws the first picture then. But os88_main has no worker yet and
 * a window with an empty field until the first frame is a window that looks
 * broken, so the picture is drawn here as well - which is idempotent, because
 * every pmc_vid_* write compares first. */
static void pmc_new_game(void)
{
    int i;

    for (i = 0; i < PMC_T_N; i++)
        pmc_disable(i);
    pmc_acc = 0;
    pmc_paused = 0;                     /* apps/pacman's pm_new does this too:
                                         * Pause is a state the user set on the
                                         * game being discarded, and leaving it
                                         * set draws a fresh maze that then
                                         * never moves (paccman.c) */
    pmc_pause_item();                   /* ...and the item label is the only
                                         * place that state is readable, so it
                                         * follows every write of the byte */
    pmc_snd_clear();                    /* New Game taken during the prelude,
                                         * the death tune or the siren: the old
                                         * game's voices are the old game's */
    pmc_black = 0;                      /* ...and New Game taken mid-FADE must
                                         * not leave the field black with a
                                         * fresh maze underneath it, which is
                                         * what a fade-out with no fade-in
                                         * behind it would do */
    pmc_shblack = 0;
    pmc_mode = PMC_MODE_GAME;
    pmc_game_init();
    pmc_start(PMC_T_GAME);
}
