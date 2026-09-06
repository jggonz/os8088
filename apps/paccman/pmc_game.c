/* ============================================================================
 * os8088 - apps/paccman/pmc_game.c    the round, the actors and the rules
 *
 * DERIVED MATERIAL. Part of PACCMAN, a reimplementation of Andre Weissflog's
 * pacman.c (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre
 * Weissflog, at commit 0f5ec5a; this file carries pacman.c 1446-1465
 * (game_init) today and 1467-2215 (round set-up, the tick, the actors and the
 * four ghosts) after wave 2. See apps/paccman/README.md.
 *
 * #included by apps/paccman/paccman.c (one translation unit, SPEC.md 73.1).
 *
 * ---------------------------------------------------------------------------
 * WHAT IS HERE AT WAVE 1, AND WHAT IS NOT
 * ---------------------------------------------------------------------------
 * game_init only: the one-time set-up and the FIRST PICTURE - the maze, HIGH
 * SCORE, PLAYER ONE and READY! exactly where pacman.c 1459-1464 puts them.
 * That picture is what wave 1's screendumps are of, on all three adapters,
 * and it exercises the whole path this wave exists to build: the two RAMs,
 * the damage spans, the tile composer, both packers and all three blits.
 *
 * Wave 2 fills the rest and the shape is decided in
 * docs/PACCMAN-PORT-PLAN.md, not here: game_round_init with the reference's
 * five compound-literal assignments rewritten as stores into PARALLEL ARRAYS
 * (a struct assignment is a rep movsb into KERNEL_SEG - SPEC.md 73.5.1),
 * game_disable_timers, game_tick's freeze/round/life sequencing,
 * game_update_actors calling each ghost's step DIRECTLY so the deepest chain
 * on the tick path is worker -> frame -> game_tick -> game_update_actors ->
 * ghost step -> can_move, the house dot counters, game_update_tiles (with the
 * hiscore drawn only when it is above zero) and game_update_sprites.
 * ==========================================================================*/

static int pmc_round;
static int pmc_lives;
static int pmc_dots_eaten;

/* game_init, pacman.c 1446-1465. The timers, the actors and the freeze state
 * arrive in wave 2; the four drawing calls are the reference's, in the
 * reference's order, and the order matters: game_init_playfield() paints the
 * whole playfield's colour first, so PLAYER ONE and READY! have to follow it
 * or they would be recoloured to COLOR_DOT. */
static void pmc_game_init(void)
{
    pmc_round = 0;
    pmc_lives = PMC_NUM_LIVES;
    pmc_dots_eaten = 0;

    pmc_vid_clear(PMC_TILE_SPACE, PMC_COLOR_DOT);
    pmc_vid_color_text(9, 0, PMC_COLOR_DEFAULT, "HIGH SCORE");
    pmc_init_playfield();
    pmc_vid_color_text(9, 14, 0x05, "PLAYER ONE");
    pmc_vid_color_text(11, 20, 0x09, "READY!");
}
