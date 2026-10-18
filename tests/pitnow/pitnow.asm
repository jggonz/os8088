; tests/pitnow/pitnow.asm - apps/os88pit.inc's pit_now, hammered from a boot
; sector (tests/pitnow.py). Consecutive reads must never go backwards and never
; jump a whole tick. Results at 0000:0600: dw reads, dw bad, dw state, the last
; bad pair as prev lo/hi, now lo/hi at 0608.
    cpu 8086
    org 0x7C00
    jmp 0:start
start:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov al, 0x34                ; ch0 mode 2, divisor 65536 - the kernel's
    out 0x43, al
    xor al, al
    out 0x40, al
    out 0x40, al
    mov word [0x600], 0
    mov word [0x602], 0
    mov word [0x604], 0x5A5A    ; running marker
    call pit_now
    mov [pv], ax
    mov [pv+2], dx
.loop:
    call pit_now
    mov bx, ax
    mov cx, dx
    sub bx, [pv]
    sbb cx, [pv+2]              ; CX:BX = now - prev, must be >= 0 and small
    jc .bad                     ; went backwards
    cmp cx, 0
    jne .bad                    ; jumped forward by more than a tick
    jmp short .ok
.bad:
    inc word [0x602]
    mov si, [pv]
    mov [0x608], si
    mov si, [pv+2]
    mov [0x60A], si
    mov [0x60C], ax
    mov [0x60E], dx
.ok:
    mov [pv], ax
    mov [pv+2], dx
    inc word [0x600]
    cmp word [0x600], 60000
    jb .loop
    mov word [0x604], 0xD0E0    ; done
.h: hlt
    jmp .h
pv: dd 0
%include "os88pit.inc"
    times 510-($-$$) db 0
    dw 0xAA55
