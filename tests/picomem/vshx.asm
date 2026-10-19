; tests/picomem/vshx.asm - VIDSND's PicoMEM path, alone, for tests/picomem.py
; (its vs leg; tests/vidbench/vidsnd.asm's header). picomem.inc is %included
; AS IT SHIPS under PM_CORE, as vidsnd.asm does, and vs_pmup, vs_reset,
; vs_hook, vs_dma and vs_race are CUT OUT of vidsnd.asm by the row (vsslice.inc)
; rather than copied here, so the code under test is the bench's own.
cpu 8086
bits 16
org 0x100
%define PM_CORE
start:
    call vs_pmup
    mov [res_al], al
    hlt
entry_hook:
    call vs_hook
    hlt
entry_dma:
    mov cx, 1470
    call vs_dma
    hlt
entry_race:
    call vs_race
    hlt
entry_off:
    call pm_undo
    hlt
%include "picomem.inc"
%include "vsslice.inc"

OSAPI_GET_TICKS:
    xor ax, ax
    ret
vs_isr:     iret
res_al:     db 0
vs_port:    dw 0x220
vs_irq:     db 0
vs_buf:     dw 0x3000
vs_ovec:    dw 0, 0
vs_omask:   db 0
