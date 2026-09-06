/* ============================================================================
 * os8088 - apps/paccman/pmc_move.c    the movement rules
 *
 * DERIVED MATERIAL. Part of PACCMAN, a reimplementation of Andre Weissflog's
 * pacman.c (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre
 * Weissflog, at commit 0f5ec5a; this file will carry pacman.c 1260-1361. See
 * apps/paccman/README.md.
 *
 * #included by apps/paccman/paccman.c (one translation unit, SPEC.md 73.1).
 *
 * WAVE 2 FILLS THIS FILE - it exists now so the Makefile's prerequisite line
 * is complete from the first commit (see apps/paccman/pmc_time.c's header).
 *
 * What lands here, per docs/PACCMAN-PORT-PLAN.md: tile_code_at and
 * is_blocking_tile as MACROS rather than functions, so that can_move is ONE
 * call level and not three - the worker declares OS88_STACK_256 and its tick
 * path is flattened to fit it; is_dot / is_pill / is_tunnel / is_redzone;
 * reverse_dir and the direction tables; pixel_to_tile and dist_to_tile_mid;
 * can_move(x, y, dir, cornering); move_actor writing the actor arrays in
 * place, with the tunnel wrap at x < 0 and x >= 224.
 * ==========================================================================*/
