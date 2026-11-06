; os8088 - apps/pagedesk/hosttest/pdr_primharness.asm
; The raw image hosttest/pdr_primtest.py runs under Unicorn: it %includes the
; SHIPPING pdrprim.inc and exports a table of entry points at offset 3 (after
; the jump), so the Python side needs no map file. OSAPI_* blit slots are
; stubbed: pdr_blit is not differentially tested (it is a register shuffle onto
; a kernel slot; tests/pdrend* rows cover it on the glass).
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
        dw _pdr_muldiv, _pdr_fill1, _pdr_fill4, _pdr_glyph, _pdr_imgrow, _pdr_gp, _pdr_ip
ph_start:
        hlt
%define OSAPI_GFX_BLIT1 ph_blit
%define OSAPI_GFX_BLIT4 ph_blit
ph_blit:
        clc
        ret
%include "pdrprim.inc"
section .text
