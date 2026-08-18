; =============================================================================
; os8088 - apps/runcpm/rcz80.inc       the Z80 core (SPEC.md 71) - WAVE 2
;
; Part of RUNCPM, a reimplementation of RunCPM 6.9 by Marcelo Dantas /
; "Mockba the Borg" (MIT). The instruction semantics and flag results this
; file will reproduce are RunCPM/cpu1.h's - behaviour, not lines - and the
; oracle is ZEXDOC off the master disk. %included by apps/runcpm/runcpm.asm
; and named in the Makefile from wave 1 so that no later wave adds a file the
; build does not know (docs/RUNCPM-PORT-PLAN.md).
;
; WAVE 1: a placeholder. _rc_run(n) is the cdecl entry the slice driver will
; call; it answers RC_RUN_IDLE (0) and runs nothing, so the C above it links
; and the wake loop is exercised end to end before there is a machine to run.
; The register plan the real core follows is in SPEC.md 71 (A/F in AL/AH,
; BC in CX, DE in DX, HL in DI, PC in SI, the Z80 SP in BP with ds:, BX the
; dispatch scratch, DS = the 64KB Z80 claim inside).
; =============================================================================

section .text

; int rc_run(int n) - run up to n instructions; answer the reason it stopped
; (wave 2: 0 = the slice ran out, 1 = RST 08h/10h handoff, 2 = HALT).
_rc_run:
    xor ax, ax
    ret

section .text
