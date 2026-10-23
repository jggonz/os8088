; Browser Download Manager's optional graphics/presentation part.
; DS is always the primary Browser segment; all imported data symbols are
; offsets into that segment. The three entry offsets lead the part for the
; primary's small far-call-vector table.
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
    dw br_copyselection

%include "brdownload_paint.inc"

; This optional UI command is called only by the foreground package task.
D_LNK1 equ 14
D_CMETA equ 15
D_PARA equ 10
D_BR equ 11
D_ROW equ 12
D_CELL equ 13
D_NBSP equ 3
%include "brcopyselection.inc"
