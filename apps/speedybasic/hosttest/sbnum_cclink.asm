cpu 8086
bits 16
org 0
section .text start=0
section .rodata follows=.text align=2
section .data follows=.rodata align=2
section .bss follows=.data align=2 nobits
section .modc follows=.data align=1 vstart=0
section .text
; Minimal stand-ins for crt0's real overlay loader.  This file is only an
; assembly/link gate; the raw machine differential uses SBN_FLAT separately.
cc_ovneed:
    clc
    ret
section .bss
cc_ovseg: resw 1
section .text
%include "sbnum_ccprobe.gen.asm"
%include "speedybasic/sbnum.inc"
