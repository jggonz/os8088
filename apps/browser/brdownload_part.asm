; Browser Download Manager's optional graphics/presentation part.
; DS is always the primary Browser segment; all imported data symbols are
; offsets into that segment. The three entry offsets lead the part for the
; primary's small far-call-vector table.
cpu 8086                        ; THE TARGET IS AN 8088 (SPEC.md 1). The part
                                ; is not an OS88_HEADER image, so it does not
                                ; get that macro's `cpu 8086` - and without it
                                ; nasm relaxes a far Jcc into the 386's 0F 8x
                                ; form, which an 8088 runs as POP CS: Copy
                                ; Selection jumped into segment 0 on the XT
bits 16
org 0

BR_UBUF     equ 160
BR_HOSTMAX  equ 64
BR_PATHMAX  equ 96

%include "os88api.inc"
%include "brdownload_const.inc"
%include "build/brdownload_part_symbols.inc"

    dw br_dl_part_paint
    dw br_dl_part_redraw_doc
    dw br_dl_part_paint_live
    dw br_dl_part_copy              ; FAR-CALLED like the three above, so it
                                    ; needs their retf shim: br_copyselection
                                    ; ends in a near `ret`

%include "brdownload_paint.inc"

; This optional UI command is called only by the foreground package task.
%include "brdocmarks.inc"
%include "brcopyselection.inc"

br_dl_part_copy:
    call br_copyselection
    retf
