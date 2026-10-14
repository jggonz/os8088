; tests/picomem/khx.asm - the KERNEL's two PicoMEM checks, alone, for
; tests/picomem.py (SPEC.md 18.97.6, 18.100.1). dsk_fdd_pmemu and
; dsk_fdd_park_x are CUT OUT of kernel/disk.inc by the row (kslice.inc) rather
; than copied here; everything around them is the minimum they read.
cpu 8086
bits 16
org 0x100
%define KERN_BIG
%define COLD_SEG 0
entry_emu0:
    mov al, 0
    call dsk_fdd_pmemu
    jmp short emu_done
entry_emu1:
    mov al, 1
    call dsk_fdd_pmemu
emu_done:
    pushf
    pop ax
    mov [res_flags], ax
    hlt
entry_park:
    jmp dsk_fdd_park_x
%include "kslice.inc"
res_flags:   dw 0
fdd_dbg_eqp: db 2
