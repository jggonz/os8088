; The harness image for hosttest/pdxj_asm.py: pdxjasm.inc and a table of its entry
; points and variables at offset 0, flat, DS = CS.
cpu 8086
bits 16
section .text start=0
section .rodata follows=.text align=2
section .data follows=.rodata align=2
section .bss follows=.data align=2 nobits
section .text
tab:
    dw _xj_mqinit, _xj_mqdec, _xj_cxreset, _xj_lift_n, _xj_lift_f, _xj_l53_n, _xj_l53_f
    dw _xj_scal_n, _xj_scal_f, _xj_inter, _xj_deq, _xj_grey
    dw _xj_cx, _xj_a, _xj_chi, _xj_clo, _xj_ct, _xj_bp
    jmp short $
%include "pagedesk/pdxjasm.inc"
section .data
dummy: dw 0
section .bss
bss_end: resb 64
