; =============================================================================
; os8088 - tests/dostrap/dwtv.asm - DWTV.COM, a TOPVIEW-AWARE program (SPEC.md
; 96.54.5): int 10h AH=FEh with ES:DI = B800:0000 asks where to write, and it
; writes `DWTV n` into row n of whatever buffer it was given, every half
; second, counting at PSP:0103h. Odd lines it also calls AH=FFh (ES:DI, CX
; cells) the way a TopView program announces a change; even lines it does
; not, which is the row sums' case. `q` ends it. PSP:0105h holds the segment
; it was given. Under a real IBM DOS 3.30 the answer is B800 unchanged.
; =============================================================================
cpu 8086
org 0x100

    jmp short start
    nop
count: dw 0                         ; PSP:0103h
vseg:  dw 0                         ; PSP:0105h - the buffer's segment
voff:  dw 0                         ; PSP:0107h - ...and offset
start:
    mov ax, 0xB800
    mov es, ax
    xor di, di
    mov ah, 0xFE
    int 0x10
    mov [vseg], es
    mov [voff], di
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
    and ax, 15
    mov bl, 160
    mul bl
    add ax, [voff]
    mov di, ax
    push di
    mov es, [vseg]
    mov al, [count]
    and al, 15
    add al, 'A'
    mov [s_line + 5], al
    mov si, s_line
    mov ah, 0x07
    cld
    xor cx, cx
.ch:
    lodsb
    or al, al
    jz .done
    stosw
    inc cx
    jmp short .ch
.done:
    pop di
    test byte [count], 1
    jz .nomark
    mov ah, 0xFF                    ; ES:DI, CX: these cells changed
    int 0x10
.nomark:
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

s_line: db 'DWTV ?', 0
