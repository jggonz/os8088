; =============================================================================
; os8088 - tests/multiseg/msegbig.asm
;
; MSEGBIG - MSEG's REFUSAL TWIN (SPEC.md 20.12). The same three file-backed
; parts and one more: a REQUIRED scratch part of 640KB, which is the whole of
; the biggest machine in this tree. It must be refused, and the launch with it.
;
; THE POINT IS WHAT THE REFUSAL COSTS. op_load sizes from the table, which is
; in the image the kernel has already read - so this package is refused with
; NO DISK READ OF ITS OWN, before a sector of its parts has been asked for.
; That is the third thing the whole design exists for, and tests/msegnomem.py
; measures it against MSEG on the same disk with a DISKCNT kernel: the two
; launches differ by exactly op_read's int 13h calls.
;
; 640KB and not 512: the first figure was 512 and a 640KB XT GRANTED it - the
; heap ladder leaves about 500KB there - so the row passed on a mechanism it
; had never run. A part that might or might not be refused makes a row report
; the MACHINE rather than the mechanism.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MSEGBIG', mb_entry, OS88_F_PARTS

%include "os88parts.inc"

MB_PARTS  equ 4
MB_KB     equ 640

    OS88_PARTS_BEGIN MB_PARTS
      OS88_PART OP_SEG                      ; 0 ...the same three MSEG has, so
      OS88_PART OP_SEG                      ; 1 the two launches cost the same
      OS88_PART OP_ASSET                    ; 2 up to the moment of refusal
      OS88_PART OP_ASSET, OP_ZERO, MB_KB    ; 3 and this cannot be had
    OS88_PARTS_END

; -----------------------------------------------------------------------------
; mb_entry - package entry (SPEC.md 20.2)
; in:  SI = the name of the file we came out of, ES = KERNEL_SEG
; out: CF=1 always, in practice: op_load cannot grant 640KB anywhere
;
; A window is created ONLY if op_load succeeded, and that is the row's
; discriminator rather than a comment: a machine that granted 640KB is one
; where nothing below means anything, and it says so on the glass.
; -----------------------------------------------------------------------------
mb_entry:
    call op_load
    jc .refuse
    mov si, mb_tpl
    call OSAPI_WM_CREATE
    ret
.refuse:
    stc                         ; ...and the loader tears down what exists,
    ret                         ; including whatever op_load claimed before it
                                ; gave up: ld_unreserve frees by [ld_base],
                                ; which is what a package's own claims carry

mb_paint:
    ret

mb_tpl:
    dw 140, 100, 200, 60
    dw mb_title, mb_paint, 0, 0
mb_title:  db 'MSEGBIG GRANTED', 0

; The image is padded to the same seven sectors MSEG's is, so the two launches
; read the same number of sectors before the refusal and the only difference
; tests/msegnomem.py can measure is op_read.
    times 3584 - ($ - $$) db 0

    OS88_BSS OP_BSS
    OS88_IMAGE_END
