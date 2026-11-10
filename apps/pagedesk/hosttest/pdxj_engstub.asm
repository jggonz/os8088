; A stand-in PDENG door for hosttest/pdxj_uc.py --eng: the three stream verbs PDXJ
; uses (PDV_ENG_STRM_OPEN 6, STRM_READ 7, STRM_CLOSE 8) over a buffer in memory.
; Loaded at 7000:0000. The harness writes `src_len` and `src_seg` (offsets below)
; and reads `opens`/`closes` to see that the module called what it should.
cpu 8086
bits 16
org 0
door:
    cmp al, 6
    je .open
    cmp al, 7
    je .read
    cmp al, 8
    je .close
    mov ax, 17                      ; PDE_FILTER
    stc
    retf
.open:
    push ds
    push ax
    mov ax, 0x2400                  ; the exchange block
    mov ds, ax
    cmp word [0], 5                 ; objnum must be what the harness asked for
    jne .bad
    cmp word [14], 1                ; accept = PDSA_CODEC
    jne .bad
    mov word [16], 4                ; codec = PDCD_JPX
    push cs
    pop ds
    mov word [pos], 0
    inc word [opens]
    pop ax
    pop ds
    xor ax, ax
    clc
    retf
.bad:
    pop ax
    pop ds
    mov ax, 17
    stc
    retf
.read:
    push ds
    push si
    push di
    push es
    push cs
    pop ds
    mov es, bx
    mov di, cx
    mov cx, [src_len]
    sub cx, [pos]                   ; left
    cmp cx, dx
    jbe .n
    mov cx, dx
.n:
    mov ax, cx
    push ax
    mov bx, ds
    mov si, [pos]
    add [pos], cx
    mov ds, [src_seg]
    cld
    rep movsb
    pop ax
    pop es
    pop di
    pop si
    pop ds
    clc
    retf
.close:
    push ds
    push cs
    pop ds
    inc word [closes]
    pop ds
    xor ax, ax
    clc
    retf
align 2
src_len: dw 0
src_seg: dw 0x4000
pos:     dw 0
opens:   dw 0
closes:  dw 0
