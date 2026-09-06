/* ============================================================================
 * os8088 - apps/paccman/pmc_snd.c    three arcade voices into one speaker
 *
 * DERIVED MATERIAL. Part of PACCMAN, a reimplementation of Andre Weissflog's
 * pacman.c (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre
 * Weissflog, at commit 0f5ec5a; this file will carry its sound machinery
 * (pacman.c 634-673 and 3192-3380). The two register dumps the players read
 * were captured from an arcade emulator and are decoded on the host into
 * pmc_rom.c. See apps/paccman/README.md.
 *
 * #included by apps/paccman/paccman.c (one translation unit, SPEC.md 73.1).
 *
 * WAVE 3 FILLS THIS FILE - it exists now so the Makefile's prerequisite line
 * is complete from the first commit (see apps/paccman/pmc_time.c's header).
 *
 * What lands here, per docs/PACCMAN-PORT-PLAN.md: three voice registers
 * (frequency and volume; the waveform is dropped), three sound slots as a
 * SWITCH and not a function-pointer table - nothing here may take the address
 * of a function - the six procedural effects with the reference's exact
 * registers and stop ticks, the two dump players, and once per OS tick one
 * os88_snd_tone() for the highest-priority non-silent voice: effects, then the
 * siren / frightened tone / prelude MELODY, then the prelude bass.
 *
 * THE REDUCTION IS STATED, NOT TUNED. The PC speaker is one square wave with
 * no volume and no waveform, and the arcade's three-voice wavetable does not
 * fit through it; SPEC.md 91 and the README say so and the About card does
 * not. Sampling once per OS tick also means an effect shorter than about
 * three game ticks can fall between two samples - stated for the same reason.
 * ==========================================================================*/
