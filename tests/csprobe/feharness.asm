; tests/csprobe/feharness.asm - REDLINE's chipset WRITE side (csfe.inc,
; csapply.inc) on its own, for tests/csprobe.py. Segment 0, org 100h. The
; test parks IP on a routine with h_ret's address pushed, so every call ends
; on the hlt there. od_read/od_write are stubs over h_file, which the test
; fills; OSAPI_CPU_INFO answers [h_tier].
cpu 8086
bits 16
org 0x100
CPU_8086 equ 0
CPU_286  equ 1
CPU_386  equ 2
FERR_NOENT equ 4
%define OSAPI_CPU_INFO h_cpuinfo
%define CS_LOADER                   ; csapply.inc's load and apply, RLAPPLY's
h_ret:
    hlt
h_tier:     db 1
h_flen:     dw 0                    ; 0 = no file
h_file:     times 64 db 0
h_wlen:     dw 0
h_cpuinfo:
    mov al, [h_tier]
    ret
od_read:                            ; ES:BX = buffer, CX = capacity
    mov ax, [h_flen]
    or ax, ax
    jz .none
    cmp ax, cx
    ja .big
    push cx
    push si
    push di
    mov cx, ax
    mov si, h_file
    mov di, bx
    cld
    rep movsb
    pop di
    pop si
    pop cx
    xor dx, dx
    clc
    ret
.big:
    mov ax, 5
    stc
    ret
.none:
    mov ax, FERR_NOENT
    stc
    ret
od_write:                           ; ES:BX = bytes, CX = count
    push si
    push di
    push cx
    mov [h_wlen], cx
    mov si, bx
    mov di, h_file
    cld
    rep movsb
    pop cx
    pop di
    pop si
    xor ax, ax
    clc
    ret
h_home:     db 12                   ; the FE3600's home: the model's R4
h_sweep:                            ; the FE3600's sweep, the test's ROM at
    xor bx, bx                      ; C000 and no PicoMEM buffer
    jmp short h_sweep_fe
h_sweep_pm:                         ; ...and with a PicoMEM's buffer at D000
    mov bx, 0xD000
h_sweep_fe:
    mov ax, 0xA000
    mov cx, 0xC000
    call cs_sw_refs
    mov si, cs_klist_fe             ; REDLINE's own path: the row's list
    mov al, [h_home]
    jmp cs_sw_tune
h_tune_ne:                          ; every NEAT knob, as REDLINE's Tune
    mov ax, 0xA000
    xor bx, bx
    mov cx, 0xC000
    call cs_sw_refs
    mov si, cs_klist_ne
    jmp cs_sw_tune
h_sweep_ne:                         ; the NEAT's, the same references; home
    mov ax, 0xA000                  ; is what the chip says it runs
    xor bx, bx
    mov cx, 0xC000
    call cs_sw_refs
    call cs_ne_get
    mov bx, cs_desc_ne
    jmp cs_sw_sweep
cs_sw_measure:                      ; REDLINE times each step; the model has
    ret                             ; no clock, so the harness does not
%include "redline/csrows.inc"
%include "redline/csfe.inc"
%include "redline/csne.inc"
%include "redline/csapply.inc"
%include "redline/csweep.inc"
cs_sw_ref:  times 1024 db 0
