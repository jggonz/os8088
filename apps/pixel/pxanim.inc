; =============================================================================
; os8088 - apps/pixel/pxanim.inc
;
; A GIF THAT PLAYS (SPEC.md 106.25): the numbers the resident package and the
; GIF part (apps/pixel/pxgif.asm) both read - pure equates, so the two cannot
; disagree about a verb, a flag or the worker's job. The GIF part plays the
; images after the first on the 1/1 master, a frame a job-step on the WORKER
; (JOB_ANIM), and its UI half - the frame painted, the timer's pace, the
; claims - runs on the UI task through its INFO vector (AV_*); the resident
; keeps the hooks: the timer's tick, the wake, the stop, the menu item.
; =============================================================================

%ifndef PXANIM_INC
%define PXANIM_INC

JOB_ANIM    equ 5                   ; the worker's job: the next frame (and
                                    ; the next, while the UI says go)

AV_INIT     equ 0                   ; THE GIF PART'S UI VERBS (CL, through its
AV_TICK     equ 1                   ; PXV_INFO vector): a visible decode ended
AV_WAKE     equ 2                   ; - the animation's facts; the timer; the
AV_TOGGLE   equ 3                   ; worker's wake or answer; Play/Stop

ANF_LOCAL0  equ 1                   ; [px_anflg]: frame 0 had a local table
ANF_GLOB    equ 2                   ; ...the GIF has a global one
ANF_GREAD   equ 4                   ; ...which the next job reads into the
ANF_GHAVE   equ 8                   ; animation's claim, and it holds it
ANF_DISPD   equ 16                  ; the frame at [px_anpos]: the previous
ANF_BKD     equ 32                  ; frame's disposal applied, its own
                                    ; backup taken - so a job stopped inside
                                    ; it does neither twice
ANF_END     equ 64                  ; the frame shown ended its pass

AN_GTAB     equ 768                 ; THE ANIMATION'S CLAIM: the global table,
                                    ; then a disposal 3's backup

%endif
