; =============================================================================
; os8088 - tests/dostrap/dwdirect.asm - DWDIRECT.COM, a program that WRITES
; THE SCREEN ITSELF (SPEC.md 96.54.6), as Microsoft Works does: it asks the
; mode with int 10h AH=0Fh, takes B000 for mode 7 and B800 otherwise, and
; writes `DWDIRECT n` straight into row n every half second (spinning on the
; BIOS tick), counting at PSP:0103h. `q` (INT 16h AH=01h/00h) ends it.
; It runs under a real IBM DOS 3.30 unchanged.
; =============================================================================
cpu 8086
org 0x100

    jmp short start
    nop
count: dw 0                         ; PSP:0103h
vseg:  dw 0xB800
start:
    mov ah, 0x0F
    int 0x10
    cmp al, 7
    jne .line
    mov word [vseg], 0xB000
.line:
    mov ax, 0x40
    mov es, ax
    mov bx, [es:0x6C]
    add bx, 9
.spin:
    mov ax, [es:0x6C]
    sub ax, bx
    js .spin
    mov ax, [count]
    and ax, 15                      ; rows 0..15, over and over
    mov bl, 160
    mul bl
    mov di, ax
    mov es, [vseg]
    mov si, s_line
    mov al, [count]
    and al, 15
    add al, 'A'
    mov [s_line + 9], al            ; DWDIRECT A, B, ... - the row's letter
    mov ah, 0x07
    cld
.ch:
    lodsb
    or al, al
    jz .done
    stosw
    jmp short .ch
.done:
    inc word [count]
    mov ah, 0x01
    int 0x16
    jz .line
    xor ah, ah
    int 0x16
    cmp al, 'q'
    jne .line
    mov ah, 0x4C
    int 0x21

s_line: db 'DWDIRECT ?', 0
