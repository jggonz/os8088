; =============================================================================
; vmmabi.inc - the private kernel <-> VMMOUSE.DRV ABI (SPEC.md 9.11.2)
;
; INCLUDED BY BOTH SIDES, as source and never as a copy: kernel/vmmouse.inc
; and drivers/vmmouse/vmmouse.asm. XMEM.DRV mirrors its one constant by hand
; and says so in a comment at each end; there are four here and a register
; contract, which is more than a comment can keep honest.
;
; IT LIVES IN kernel/ AND THE DRIVER REACHES IT, rather than the other way
; round, and that is a build fact rather than a statement about ownership.
; THREE TOOLS re-assemble kernel.asm with include paths of their own -
; kernsize.py, os88boot.py and os88sym.py - so a new kernel include DIRECTORY
; has to be added in four places and is silently wrong in the three that get
; forgotten (os88sym then refuses every address, saying the map describes a
; different kernel). `kernel/*.inc` is a path all four already have, and
; $(KERNEL_INC)'s wildcard picks this file up as a kernel dependency for
; free. The driver's own rule carries the one extra `-I kernel/`.
;
; The kernel checks VMM_ABI_VER at DRVV_ATTACH (it goes in AH) and the image
; refuses a mismatch, so a stale VMMOUSE.DRV beside a new kernel is a refused
; load with a Drivers-page line, not a wrong answer at the first poll.
; =============================================================================

VMM_ABI_VER equ 1               ; bump on ANY change to the contract below

; --- the verbs, past DRVV_* -------------------------------------------------
; DRVV_ATTACH (0) and DRVV_DETACH (1) are the shared ones and mean what
; SPEC.md 51.2 says. This image implements no DRVV_TIER and no DRVV_READY:
; it has one tier and nothing to do once it is up.
VMMV_READ   equ 16              ; one report out of the queue. Well clear of
                                ; the DRVV_* range so a verb sent to the wrong
                                ; image is refused rather than obeyed

; --- what VMMV_READ answers with --------------------------------------------
;   CF = 1                nothing queued; stop asking this pass
;   CF = 0                a report:
;     AX = x, BX = y      RAW, 0..0xFFFF, unscaled - the kernel owns [vid_w]
;     CL                  buttons, in mouse_btn's own bits: 1 = left, 2 = right
;     CH                  0 = AX/BX are an absolute POSITION
;                         1 = ...a signed relative DELTA (v86 sends these while
;                             the host pointer is locked)

VMM_DRAIN   equ 24              ; reports the kernel takes in one poll - a
                                ; bound like ui_task's EVQ_CAP, so a host that
                                ; queues faster than we drain cannot starve
                                ; the caller. THE KERNEL'S number, not the
                                ; image's: it is the caller that has to bound
                                ; its own pass
