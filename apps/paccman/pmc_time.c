/* ============================================================================
 * os8088 - apps/paccman/pmc_time.c    the two-word tick and the triggers
 *
 * DERIVED MATERIAL. Part of PACCMAN, a reimplementation of Andre Weissflog's
 * pacman.c (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre
 * Weissflog, at commit 0f5ec5a; this file will carry its time-trigger
 * vocabulary (pacman.c 322-325, 420-428, 744-780, 844-915). See
 * apps/paccman/README.md.
 *
 * #included by apps/paccman/paccman.c (one translation unit, SPEC.md 73.1).
 *
 * WAVE 2 FILLS THIS FILE. It exists at wave 1 so that the Makefile's
 * prerequisite line for build/paccman.raw.asm is complete from the first
 * commit: make cannot see through a #include, and an undeclared one leaves a
 * stale .o88 that reads exactly like a change that did nothing
 * (docs/C-TOOLCHAIN.md, LESSONS.md 9).
 *
 * What lands here, and the shape each takes, is decided in
 * docs/PACCMAN-PORT-PLAN.md and is not re-opened:
 *
 *   - the tick as tick_lo/tick_hi and the trigger table as trg_lo[]/trg_hi[]
 *     indexed by a PMC_T_* enum, because there is no 32-bit integer here
 *     (SPEC.md 73.7) and the reference counts 60 Hz ticks in a uint32_t;
 *   - start / start_after (an add with carry) / disable / now / after /
 *     after_once / before / between;
 *   - TWO since() helpers on purpose: one SATURATED at 0x7FFF for compares,
 *     and since_lo() wrapping in 16 bits for the blink masks. A blink written
 *     against the saturating one freezes after nine minutes of one attract
 *     screen, which is what the harness's 40,000-tick drive exists to catch;
 *   - xorshift16 seeded 0x1234, and the BCD score digits;
 *   - the accumulator: acc += 600 * min(elapsed OS ticks, PMC_CATCHUP_MAX = 2),
 *     one game tick per 182 of it. 18.2 Hz out here, 60 Hz in there.
 * ==========================================================================*/
