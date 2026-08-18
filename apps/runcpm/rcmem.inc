; =============================================================================
; os8088 - apps/runcpm/rcmem.inc       Z80-RAM movers (SPEC.md 71) - WAVE 2
;
; Part of RUNCPM, a reimplementation of RunCPM 6.9 by Marcelo Dantas /
; "Mockba the Borg" (MIT). %included by apps/runcpm/runcpm.asm and named in
; the Makefile from wave 1 (docs/RUNCPM-PORT-PLAN.md).
;
; WAVE 2 puts here the near cdecl accessors into the 64KB Z80 claim -
; rc_rd(a), rc_rd16(a), rc_wr(a, v) - and the ES-loading block movers
; rc_zcopy_in / rc_zcopy_out / rc_zzcopy / rc_zfill for the 128-byte DMA
; buffer, the 36-byte FCB and the CCP image, each marked cc8086:allow with its
; reason, each restoring ES, and all of them tested by hosttest/rcmemtest (a
; boot-sector harness with SS != DS and negative controls, the cwmovetest
; precedent). Until then this file is empty on purpose: a byte mover written
; before its test exists is the defect LESSONS.md 4 describes.
; =============================================================================

section .text
