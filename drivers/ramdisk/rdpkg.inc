; =============================================================================
; RAMDISK.DRV's package verbs - the ABI behind OSAPI_DRV_CALL (SPEC.md 20.11)
;
; Included by BOTH ends: the driver that answers these and the package that
; asks. That is the shape every driver with a package ABI should copy, and it
; is why the kernel has no networking cell of its own - a driver's verbs are
; the DRIVER's contract, they live beside the driver, and os8088 neither knows
; nor owes anything about them. NET.DRV's socket verbs will be
; drivers/net/netpkg.inc, exactly this file with different numbers in it.
;
; Reached as:
;
;       mov  bh, RD_CLASS           ; the class - and NET.DRV's too
;       mov  bl, RDPV_IDENT         ; ...ALWAYS FIRST: a class is a
;                                   ; publication SLOT, not a driver
;       call OSAPI_DRV_CALL
;       jc   .no_driver             ; not loaded, or it published no door
;
; CF=1 with AX=0 is the kernel's refusal and means "there is no driver of that
; class published, or the one that is publishes no DSV_PKGCALL". Any other
; CF=1 is the verb's own.
;
; **ES IS THE CALLING PACKAGE'S SEGMENT** on the way in, which is what makes
; RDPV_UPCASE possible at all: it is the one driver entry point in the machine
; where ES is not KERNEL_SEG (SPEC.md 20.11).
;
; **WORKER SAFETY IS STATED PER VERB**, because SPEC.md 20.6 rule 7's
; allowlist covers the CELL and not what is behind it. Both of these are
; register work in the driver's own segment: no lock, no disk, no kernel call.
; Both are worker-safe.
; =============================================================================

RDPV_IDENT   equ PKGV_IDENT     ; = 0, and it is the UNIVERSAL verb 0 rather
                                ; than this driver's own idea (SPEC.md
                                ; 20.11.1). out AX = RD_SIG, CF=0. Takes
                                ; nothing, touches nothing, and answers a
                                ; value a wrong driver could not - which is
                                ; the proof that a package reached A DRIVER AT
                                ; ALL, and, since NET.DRV is DRVC_FILE too,
                                ; the only way to know WHICH
RDPV_UPCASE  equ 1              ; in  ES:SI = a NUL-terminated string in YOUR
                                ;     segment, CX = the most bytes to touch
                                ; out AX = the length it found, and the string
                                ;     is upper-cased IN PLACE.
                                ;
                                ; It exists to prove the half of this cell
                                ; that nothing else can: the driver READ AND
                                ; WROTE the package's own memory, which is
                                ; only possible because the X stub put the
                                ; package's segment in ES. Any other driver
                                ; entry point gets KERNEL_SEG there and could
                                ; not have done it
RD_CLASS     equ DRVC_FILE      ; the publication slot this driver sits in -
                                ; SHARED with NET.DRV, which is what makes
                                ; RDPV_IDENT load-bearing rather than tidy
RD_SIG       equ 'DR'           ; ...and what RDPV_IDENT answers

RDPV_MAX     equ 1              ; ...and anything above this is refused. A
                                ; verb table indexed by an unchecked verb is
                                ; a wild near call, which is the same hazard
                                ; FSV_SIZE guards on the other table
