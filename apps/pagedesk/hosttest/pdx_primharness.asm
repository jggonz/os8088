; os8088 - apps/pagedesk/hosttest/pdx_primharness.asm
; The raw image hosttest/pdx_primtest.py runs under Unicorn: it %includes the SHIPPING
; pdxfrs.inc (muldiv, accline, pack) with the accumulator's data, and exports a table of
; entry points at offset 3 so the Python side needs no map file.
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
        dw _pdx_muldiv, _pdx_accline, _pdx_pack, _pdx_acc, _pdx_acw, _pdx_ach
ph_start:
        hlt
%define PDXF_THR_ASM 128
%include "pagedesk/pdxfrs.inc"
section .data
_pdx_acw: dw 0
_pdx_ach: dw 0
_pdx_acc: times 34 * 48 dw 0
section .text
