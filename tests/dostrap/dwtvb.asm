; =============================================================================
; os8088 - tests/dostrap/dwtvb.asm - DWTVB.COM, a TopView-aware program that
; DRAWS THROUGH THE BIOS WHEN THERE IS NO MULTITASKER, which is what
; WordPerfect 4.1 does (SPEC.md 96.54.5, 96.54.13). It asks int 10h AH=FEh
; with ES:DI = B800:0000 once. Given another buffer, it writes `DWTB n`
; straight into row n of it every half second and says so with AH=FFh; given
; B800 back - no multitasker, or a window that took a capture card - it
; writes the same line with int 10h AH=02h and AH=09h instead. Counting at
; PSP:0103h; PSP:0105h holds the segment it was given. `q` ends it. Under a
; real IBM DOS 3.30 the answer is B800 unchanged and the lines are the BIOS's.
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
    mov al, [count]
    and al, 15
    mov dh, al                      ; DH = the row
    add al, 'A'
    mov [s_line + 5], al
    mov si, s_line
    cld
    cmp word [vseg], 0xB800
    je .bios
    mov al, 160                     ; --- TopView's buffer -----------------
    mul dh
    add ax, [voff]
    mov di, ax
    push di
    mov es, [vseg]
    mov ah, 0x07
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
    mov ah, 0xFF                    ; ES:DI, CX: these cells changed
    int 0x10
    jmp short .next
.bios:                              ; --- no multitasker: the video BIOS ---
    xor dl, dl                      ; DH = the row, DL the column
.bch:
    mov ah, 0x02
    xor bh, bh
    int 0x10
    lodsb
    or al, al
    jz .next
    mov ah, 0x09
    mov bx, 0x0007
    mov cx, 1
    int 0x10
    inc dl
    jmp short .bch
.next:
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

s_line: db 'DWTB ?', 0
