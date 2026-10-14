; tests/picomem/sbhx.asm - SOUND.DRV's PicoMEM attach and SB IRQ choice,
; alone, for tests/picomem.py (SPEC.md 34.10, 34.10.1). picomem.inc is
; %included AS IT SHIPS; sbl_f_irqdisc and its candidate tables are CUT OUT
; of sb.inc by the row (sbslice.inc) rather than copied here, so the code
; under test is the code that ships. Everything else is the minimum they
; read - and every DSP or tick primitive is a stub that RECORDS being
; called, because on a PicoMEM the probe must not run at all.
cpu 8086
bits 16
org 0x100
%define PICOMEM
%macro CLC_OR_STC 1
    db 0xA8
%1: stc
%endmacro
start:
    call pm_init
    hlt
entry_shut:                     ; the stream's window spent: asks nothing
    mov byte [pm_rchk], 0
    call pm_runaway
    hlt
entry_race:                     ; a stream just started (sb.inc loads it)
    mov byte [pm_rchk], PM_RCHK
    call pm_runaway
    hlt
entry_keep:                     ; pm_tick, once (SPEC.md 34.10.6)
    call pm_tick
    hlt
entry_pub:                      ; pm_tickpub against a table of our own
    mov si, res_svc
    call pm_tickpub
    hlt
entry_disc:
    mov byte [sbl_verhi], 2     ; the DSP 2.01 the firmware reports
    mov byte [res_ticks], 0     ; pm_wait's deadline reads it too: count
                                ; discovery's alone
    call sbl_f_irqdisc
    mov [res_al], al
    hlt
%include "picomem.inc"
%include "sbslice.inc"

; --- stubs: any call here is the F2h probe running, which it must not ------
sbl_dsp_wr:
    inc byte [res_dspwr]
    stc
    ret
OSAPI_GET_TICKS:
    inc byte [res_ticks]
    xor ax, ax
    ret
sbl_isr:    iret
sbl_stub7:  iret
sbl_stub5:  iret
sbl_stub3:  iret
sbl_stub2:  iret

sbl_tick:                       ; pm_tick's tail: counted, so the row sees
    inc byte [res_sbt]          ; the Sound Blaster's duties still run
    ret
OPL_NCH     equ 18
DSV_TICK    equ 6
res_svc:     times 16 db 0
res_sbt:     db 0
opl_b0:      times OPL_NCH db 0
res_al:      db 0
res_dspwr:   db 0
res_ticks:   db 0
sbl_verhi:   db 0
sbl_base:    dw 0x220
sbl_irq:     db 0xFF
sbl_hooked:  db 0
sbl_drqmask: db 0
sbl_dsc_fired: db 0
sbl_oldvec:  dw 0, 0
sbl_dsc_mask: db 0
