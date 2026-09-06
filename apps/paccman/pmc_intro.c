/* ============================================================================
 * os8088 - apps/paccman/pmc_intro.c    the attract screen
 *
 * DERIVED MATERIAL. Part of PACCMAN, a reimplementation of Andre Weissflog's
 * pacman.c (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre
 * Weissflog, at commit 0f5ec5a; this file will carry its intro_tick
 * (pacman.c 2326-2399). See apps/paccman/README.md.
 *
 * #included by apps/paccman/paccman.c (one translation unit, SPEC.md 73.1).
 *
 * WAVE 3 FILLS THIS FILE - it exists now so the Makefile's prerequisite line
 * is complete from the first commit (see apps/paccman/pmc_time.c's header).
 *
 * What lands here, per docs/PACCMAN-PORT-PLAN.md: the CHARACTER / NICKNAME
 * reveal at the reference's own event ticks (60, 120, 150, ... 630), the 2x3
 * ghost block drawn as three vid_color_tile rows, the hiscore field shown
 * ONLY when the hiscore is above zero, the 10/50 PTS legend, and the blinking
 * PRESS ANY KEY TO START!.
 *
 * IT IS ALSO THE FIRST ovl_* CANDIDATE (SPEC.md 73.14): it is once-per-attract
 * code that a keystroke never touches, so it is what moves out of the resident
 * image the day os88pkg's line passes 50,000. Nothing here may take the
 * address of a function, for that reason among others.
 * ==========================================================================*/
