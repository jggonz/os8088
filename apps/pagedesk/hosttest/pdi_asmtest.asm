; pdi_asmtest.asm - pdxidct.inc alone, for the Unicorn differential test (pdi_asm.py).
; A jump table at 0 names the entry points and the three C-owned symbols the
; assembly reads, so the host can find them after `nasm -f bin`.
cpu 8086
bits 16
org 0
section .text   start=0
section .data   follows=.text align=2
section .bss    follows=.data align=2 nobits

section .text
    dw _pdi_hsym, _pdi_bits, _pdi_block, _pdi_idct, _pdi_ebuf, _pdi_pos, _pdi_zz, stop
stop: hlt

section .data
_pdi_zz:
    db 0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5
    db 12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28
    db 35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51
    db 58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
section .bss
_pdi_ebuf: resb 1420
_pdi_pos:  resw 1
section .text
%include "pdxidct.inc"
