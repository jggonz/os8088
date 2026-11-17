; os8088 - tests/pdmath/pdmharness.asm
; The raw harness image tests/pdmath/pdmath_diff.py runs under unicorn: it
; %includes the SHIPPING apps/pagedesk/pdmath.inc and exports a table of entry
; points at offset 0 (so the Python side needs no map file).
cpu 8086
bits 16
org 0
section .text   start=0
section .rodata follows=.text align=2
section .data   follows=.rodata align=2
section .bss    follows=.data align=2 nobits
section .text
        jmp     near pdmh_start
pdmh_table:
%macro T 1
        dw %1
%endmacro
        T pdm_ovfw
        T _pdm_ovf
        T _pdm_ovf_clear
        T _pdm_u32_set
        T _pdm_u32_add
        T _pdm_u32_sub
        T _pdm_u32_add16
        T _pdm_u32_cmp
        T _pdm_u32_mul16
        T _pdm_u32_divmod16
        T _pdm_u32_shr
        T _pdm_u32_shl
        T _pdm_u32_todec
        T _pdm_fx_int
        T _pdm_fx_floor
        T _pdm_fx_round
        T _pdm_fx_add
        T _pdm_fx_sub
        T _pdm_fx_mul
        T _pdm_fx_div
        T _pdm_fx_neg
        T _pdm_fx_cmp
        T _pdm_fx_parse
        T _pdm_fx_todec
        T _pdm_fx_to_pt16
        T _pdm_pt16_to_fx
        T _pdm_mat_ident
        T _pdm_mat_mul
        T _pdm_mat_apply
        T _pdm_view_fwd
        T _pdm_view_inv
        T _pdm_view_size
        T _pd_fcopy_in
        T _pd_fcopy_out
        T _pd_fcopy
        T _pd_fpeekb
        T _pd_fpeekw
        T _pd_fpokeb
        T _pd_fpokew
        T _pd_ffill
        T _pd_ds
        T _pdm_crc16
        T _pdm_crc16_far
pdmh_start:
        hlt
%include "pdmath.inc"
section .text
