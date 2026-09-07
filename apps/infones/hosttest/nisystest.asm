; =============================================================================
; os8088 - apps/infones/hosttest/nisystest.asm    THE WHOLE-EMULATOR ROM GATE
;
; A SKELETON, AND WAVE 2 FILLS IT. The file exists from wave 1 because SPEC.md
; 91.13.1's table names it and because the Makefile's prerequisite lists are
; written once (LESSONS.md 9: a file a later wave adds is a file the build
; does not know about, and the symptom is an edit that reads as if it did
; nothing).
;
; -----------------------------------------------------------------------------
; WHY IT IS A SECOND HARNESS, AND NOT A ROW OF nicputest
; -----------------------------------------------------------------------------
; `make nicputest` IS NASM-ONLY. It %includes apps/infones/nicpu.inc and
; nothing else, because nippu.c, nirun.c and nimap.c are COMPILED C and cannot
; be in a boot-sector image at all. So blargg's `ppu_vbl_nmi` - a test of
; exactly the code that harness excludes - has nowhere to run in it, and
; `cpu_dummy_reads` is a mapper-3 ROM whose CHR bank switching is nimap.c's.
;
; This is therefore a `NITEST=1` HEADLESS DEBUG BUILD OF THE PACKAGE: the same
; INFONES.O88 the user runs, with the bracket and the present compiled out,
; loading a named ROM on the wake, running it with no picture at all, polling
; $6000 through blargg's standard protocol and printing the $6004 string with
; RUNCPM's rc_say shape - a toast AND a line on the console - read back over
; QMP by the shell script beside this file.
;
; WHAT IT WILL CARRY (SPEC.md 91.14.4, and wave 4 is when the list is a gate):
;   ppu_vbl_nmi rom_singles 01-10, of which the SUBSET a scanline model can
;     pass is determined by RUNNING them and each failure is LISTED in SPEC.md
;     91 with its reason - the promise is a scanline PPU and this port does not
;     become a dot-clock one to pass a test (the plan's R3);
;   cpu_dummy_reads, which is mapper 3 and so gates CHR bank switching too;
;   palette_ram and instr_timing.
;
; AND WHAT NO GATE WILL REST ON: `cpu_timing_test6` and the
; `branch_timing_tests` family have NO $6000 status byte - they predate the
; protocol and report only on a rendered screen - so they are screendumped and
; read by eye in wave 4.
;
; WAVE 2 ALSO ADDS THE STACK HIGH-WATER READING (SPEC.md 91.4.4): the shim
; fills the free stack with a pattern before the bracket is entered, and this
; harness reports the deepest scrub after a run that has exercised a mapper
; write, a PPU read, an OAM DMA, a reset, a key poll and a present. The number
; goes in 91.4.4's table, which carries PLANNED rows until it does.
;
; RUN IT:  apps/infones/hosttest/nisystest.sh   (or `make nisystest`)
; =============================================================================

cpu 8086
bits 16

%error "nisystest is wave 2's: it needs the PPU and the frame loop, and a harness written the wave it is needed is a harness nobody has run. See the header."
