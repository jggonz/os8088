; os8088 DOS read gate (SPEC.md 96.11) - OURS, MIT with the rest of the tree.
;
; DOES A DOS PROGRAM GET THE BYTES THAT ARE ON THE DISK? Every other file row
; here asks whether a call SUCCEEDS; this one asks whether it was TOLD THE
; TRUTH, which is the failure mode a program reports as its own data being
; corrupt and never as a read error.
;
; It takes a file name on the command line, and for each of three windows -
; the first 512 bytes, 512 bytes at offset 1024, and the last 512 - prints a
; rotating 16-bit sum and the first eight bytes. The host computes the same
; three from the real file and compares. A sum alone would pass on data that
; is right in aggregate and wrong in order, so the rotate makes it
; position-sensitive, and the eight bytes make a mismatch legible.
    org 0x100
    cpu 8086

BUF     equ 0x2000              ; well clear of the PSP and this code

start:
    mov si, 0x80                ; the command tail: a length byte, then text
    xor ch, ch
    mov cl, [si]
    inc si
    jcxz .toarg                 ; no argument: fall back to TEST.DAT, so the
                                ; row needs no UI to drive it
.skipsp:
    cmp byte [si], ' '
    jne .name
    inc si
    dec cx
    jnz .skipsp
.toarg:                             ; a trampoline: .noarg is past a short
    jmp .noarg                      ; jump's reach from here
.name:
    mov di, fname
.cp:
    mov al, [si]
    cmp al, ' '
    je .cpend
    cmp al, 13
    je .cpend
    mov [di], al
    inc di
    inc si
    dec cx
    jnz .cp
.cpend:
    mov byte [di], 0

.open:
    mov dx, fname
    mov ax, 0x3D00
    int 0x21
    jc .noopen
    mov [fh], ax

    ; --- the size, from a seek to the end --------------------------------
    mov bx, [fh]
    xor cx, cx
    xor dx, dx
    mov ax, 0x4202
    int 0x21
    mov [sz_lo], ax
    mov [sz_hi], dx
    mov si, s_size
    call puts
    mov ax, [sz_hi]
    call hex4
    mov ax, [sz_lo]
    call hex4
    call crlf

    xor cx, cx
    xor dx, dx
    call window                 ; ...the first 512
    mov cx, 0
    mov dx, 1024
    call window                 ; ...512 at 1024
    mov ax, [sz_lo]             ; ...and the last 512
    mov dx, [sz_hi]
    sub ax, 512
    sbb dx, 0
    jc .close
    mov cx, dx
    mov dx, ax
    call window
.close:
    mov bx, [fh]
    mov ah, 0x3E
    int 0x21
    mov si, s_done
    call puts
.wait:
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C00
    int 0x21

.noarg:
    mov si, s_noarg             ; ...the default name is already in fname
    call puts
    jmp .open
.noopen:
    mov si, s_noopen
    call puts
    jmp short .wait

; --- window - seek to CX:DX, read 512, print the sum and the first eight ----
window:
    push cx
    push dx
    mov si, s_at
    call puts
    pop dx
    pop cx
    push cx
    push dx
    mov ax, cx
    call hex4
    pop ax
    push ax
    call hex4
    pop dx
    pop cx
    mov bx, [fh]
    mov ax, 0x4200
    int 0x21

    mov bx, [fh]
    mov cx, 512
    mov dx, BUF
    mov ah, 0x3F
    int 0x21
    jc .werr
    mov [got], ax
    mov si, s_got
    call puts
    mov ax, [got]
    call hex4

    mov si, s_sum
    call puts
    mov cx, [got]
    jcxz .nosum
    mov si, BUF
    xor ax, ax
.sum:
    mov bl, [si]
    xor bh, bh
    add ax, bx
    rol ax, 1                   ; position-sensitive: the same bytes in a
    inc si                      ; different order do not sum alike
    loop .sum
.nosum:
    call hex4

    mov si, s_head
    call puts
    mov cx, 8
    mov si, BUF
.head:
    lodsb
    call hex2
    mov al, ' '
    call putc
    loop .head
    call crlf
    ret
.werr:
    mov si, s_rderr
    call puts
    call crlf
    ret

putc:
    push ax
    push dx
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop dx
    pop ax
    ret
puts:
    push ax
.l:
    lodsb
    or al, al
    jz .done
    call putc
    jmp short .l
.done:
    pop ax
    ret
crlf:
    mov al, 13
    call putc
    mov al, 10
    call putc
    ret
hexd:
    add al, '0'
    cmp al, '9'
    jbe .o
    add al, 7
.o:
    ret
hex2:
    push ax
    push ax
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call hexd
    call putc
    pop ax
    and al, 0x0F
    call hexd
    call putc
    pop ax
    ret
hex4:
    push ax
    mov al, ah
    call hex2
    pop ax
    call hex2
    ret

s_size:  db 'SIZE ', 0
s_at:    db 'AT ', 0
s_got:   db ' GOT ', 0
s_sum:   db ' SUM ', 0
s_head:  db ' HEAD ', 0
s_done:  db 'READY - press a key', 13, 10, 0
s_noarg: db 'no name given - using TEST.DAT', 13, 10, 0
s_noopen:db 'could not open it', 13, 10, 0
s_rderr: db ' READ FAILED', 0

fh:      dw 0
sz_lo:   dw 0
sz_hi:   dw 0
got:     dw 0
fname:   db 'TEST.DAT', 0
         times 71 db 0
