; =============================================================================
; tests/diaprobe/diaprobe.asm - the smallest DESKTOP INTEGRATED APPLICATION
; (SPEC.md 107)
;
; Not shipped software: the gate package `make diatest` puts in SYSTEM\DIA\
; of its fixture system disks. It is apps/os88dia.inc and a window, nothing
; else, so every row that boots it asks about the MECHANISM and not about an
; application:
;
;   diaboot    SYSTEM.CFG's DIA row launches it at boot, where DIAPROBE.DAT
;              says its window was, and it holds no desktop cell
;   diamiss    a row beside a missing one still launches
;   diahiber   ...and is NOT launched under a pending resume question
;
; It registers its row on its first tick (a byte-identical row is a no-op
; that writes nothing) and removes it BY NAME on close. Its payload is EMPTY:
; the DIA header alone is its state, DIAPROBE.DAT is 16 bytes.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'DiaProbe', dp_entry, OS88_F_ICON

    OS88_ICON16
    dw 0xFFFF                       ; 16 mask rows: the whole square white
    times 15 dw 0xFFFF
    dw 0xFFFF                       ; 16 data rows: a framed box with a dot
    dw 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8181, 0x83C1
    dw 0x83C1, 0x8181, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001
    dw 0xFFFF
    OS88_ICON16_END

%define DIA_DATNAME 'DIAPROBE.DAT'
%define DIA_CAP     'DiaProbe'
DIA_PAYLEN  equ 0
DIA_PAYVER  equ 1
DIA_ICON    equ 32
DIA_BSS     equ 0

dp_entry:
    push si
    push di
    mov di, dp_tpl
    call dia_load               ; SI = the loader's name; places the template
    jc .out                     ; ...or another instance is up: say no
    mov si, dp_tpl
    call OSAPI_WM_CREATE
    jc .out
    call dia_made
    clc
.out:
    pop di
    pop si
    ret

dp_paint:                       ; SI = the window, lock held: one opaque run
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = left, DX = top
    mov cx, ax
    add cx, 8
    add dx, 8
    mov si, dp_msg
    mov ax, 0x0F00              ; ink 0 on 15: two colours, every adapter
    call OSAPI_FONT_RUN
    call dia_painted            ; kern_small: hire the worker (SPEC.md 107.5.2)
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dp_tpl:
    dw 40, 60, 160, 50          ; the TEMPLATE place: a valid state file
    dw dp_ttl, dp_paint, 0, 0   ; moves it

dp_ttl:     db 'DiaProbe', 0
dp_msg:     db 'DIA probe', 0

%include "os88dia.inc"

    align 16, db 0              ; os88dia.inc's read lands on a paragraph
    OS88_BSS DIA_BSS_END
    OS88_IMAGE_END
