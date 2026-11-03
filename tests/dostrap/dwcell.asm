; =============================================================================
; os8088 - tests/dostrap/dwcell.asm - DWCELL.COM, the band-paint probe
; (SPEC.md 96.33.12, docs/plans/DOS-WINDOWED-PLAN.md 10)
;
; A program that changes ONE CELL at a time, the way a clock or a counter in
; a corner of a DOS screen does, so a row can price what one cell costs the
; window's band. Two phases of DC_N updates each, two BIOS ticks apart:
;
;   1  int 10h AH=09h at a parked cursor (row 12, column 40): a digit written
;      in place, the cursor never moving - a clock's shape
;   2  the ROM teletype (AH=0Eh) printing a digit and then a BACKSPACE, so
;      the cursor walks one cell right and back - a spinner's shape, and the
;      cursor-underline case: the cell the caret leaves must lose it
;
; The phase and the update count are in the words at PSP:0103h/0105h, which
; the row reads rather than the band. Then it asks for a key (INT 21h AH=08h)
; and exits with it as its exit code. It runs under a real DOS unchanged.
; =============================================================================
cpu 8086
org 0x100

DC_N equ 24

    jmp short start
    nop
count: dw 0                         ; PSP:0103h - updates in this phase
phase: dw 0                         ; PSP:0105h - 1 or 2, 3 = waiting a key
start:
    mov dx, s_hello
    mov ah, 0x09
    int 0x21
    mov word [phase], 1
    mov ah, 0x02                    ; park the cursor at 12,40
    xor bh, bh
    mov dx, 0x0C28
    int 0x10
    mov cx, DC_N
    mov byte [n], '0'
.p1:
    call wait2
    push cx
    mov al, [n]
    mov ah, 0x09
    mov bx, 0x0007
    mov cx, 1
    int 0x10
    pop cx
    call bump
    loop .p1

    mov word [count], 0
    mov word [phase], 2
    mov ah, 0x02                    ; a row of its own for the teletype
    xor bh, bh
    mov dx, 0x0E28
    int 0x10
    mov cx, DC_N
.p2:
    call wait2
    mov al, [n]
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    mov al, 8
    mov ah, 0x0E
    int 0x10
    call bump
    loop .p2

    mov word [phase], 3
    mov ah, 0x02
    xor bh, bh
    mov dx, 0x1000
    int 0x10
    mov dx, s_key
    mov ah, 0x09
    int 0x21
    mov ah, 0x08
    int 0x21
    mov ah, 0x4C
    int 0x21

bump:
    inc word [count]
    inc byte [n]
    cmp byte [n], '9'
    jbe .r
    mov byte [n], '0'
.r:
    ret

wait2:                              ; two BIOS ticks, spun: never yields
    push ax
    push bx
    push es
    mov ax, 0x40
    mov es, ax
    mov bx, [es:0x6C]
    add bx, 2
.s:
    mov ax, [es:0x6C]
    sub ax, bx
    js .s
    pop es
    pop bx
    pop ax
    ret

n:       db '0'
s_hello: db 'DWCELL START', 13, 10, '$'
s_key:   db 'DWCELL PRESS A KEY$'
