; os8088 - apps/pagedesk/hosttest/pdb_primharness.asm
; The raw image hosttest/pdb_primtest.py runs under Unicorn: the SHIPPING pdrx8.inc
; with a table of entry points at offset 3 (pdr_primharness.asm's shape).
cpu 8086
bits 16
org 0
section .text   start=0
section .rodata follows=.text align=2
section .data   follows=.rodata align=2
section .bss    follows=.data align=2 nobits
section .text
        jmp     near ph_start
ph_table:
        dw _pdr_x8xlat, _pdr_x8quant, _pdr_xq
ph_start:
        hlt
%include "pdrx8.inc"
section .text
