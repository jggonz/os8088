; =============================================================================
; tests/dostrap/a20pub.asm - every far address the DOS core PUBLISHES survives
; a shut A20 (KERN-MEGA-DOS-PLAN 10.5 defect 4)
;
;   nasm -f bin -w+error -o A20PUB.COM tests/dostrap/a20pub.asm
;
; kern_mega's kern_dos runs at FFFF:xxxx, which is memory only while A20 is
; open, so every address it hands a program must reach a low trampoline that
; re-proves the gate. A20SHUT covers the vectors kern_dos takes at entry; this
; covers what the core publishes LATER, which were direct FFFF addresses:
;
;   bit 0  int 2Fh AX=4310h's XMS entry is not in segment FFFFh
;   bit 1  the PSP's terminate address (PSP:000Ah) is not in segment FFFFh
;   bit 2  int 08h after int 33h AX=0Ch arms the mouse's tick is not FFFFh
;   bit 6  ...and with port 92h's gate SHUT, three ticks went by and the XMS
;          entry answered AH=00h - which a direct FFFF address cannot survive
;
; So the exit code is 071 (47h) on a fixed HMA run. Under a box that is not
; in the HMA, bits 0-2 are set by construction and bit 6 still holds. A box
; that sends any of them astray hangs instead of exiting.
; =============================================================================
    cpu 8086
    bits 16
    org 0x100

start:
    xor bp, bp                      ; the result bits
    mov ax, 0x4300
    int 0x2F
    cmp al, 0x80
    jne .noxms
    mov ax, 0x4310
    int 0x2F
    mov [xms], bx
    mov [xms+2], es
    mov ax, es
    inc ax
    jz .noxms
    or bp, 1
.noxms:
    push cs
    pop es
    cmp word [0x0C], 0xFFFF         ; the PSP's terminate segment
    je .p
    or bp, 2
.p:
    xor ax, ax
    int 0x33                        ; reset...
    mov ax, 0x0C
    mov cx, 1
    mov dx, mhand
    int 0x33                        ; ...and a handler, which arms the tick
    mov ax, 0x3508
    int 0x21
    mov ax, es
    inc ax
    jz .t
    or bp, 4
.t:
    cli
    in al, 0x92
    and al, 0xFC                    ; A20 off; bit 0 is a reset, left clear
    out 0x92, al
    sti
    mov ax, 0x40
    mov es, ax
    mov bx, [es:0x6C]
.w:
    mov ax, [es:0x6C]
    sub ax, bx
    cmp ax, 3                       ; three ticks with the gate shut - one
    jb .w                           ; is enough to reach a direct FFFF:08h
    cmp word [xms+2], 0
    je .done                        ; no XMS: bit 6 is the ticks alone
    mov ah, 0
    call far [xms]                  ; through the entry, from the shut gate
.done:
    or bp, 0x40
    xor ax, ax
    mov cx, ax
    mov ax, 0x0C
    int 0x33                        ; the handler back
    mov ax, bp
    mov ah, 0x4C
    int 0x21

mhand:
    retf

xms:    dw 0, 0
