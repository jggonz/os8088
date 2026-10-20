; =============================================================================
; os8088 - tests/dostrap/cx0.asm
;
; CX0.COM - what AH=40h with CX=0 actually does (SPEC.md 96.11.6.2). A
; MEASUREMENT and not a gate: it PRINTS what it saw rather than asserting, and
; it runs unchanged under a real DOS, which is the only way to know what DOS
; does rather than what the books say (docs/DOS-DEBUGGING.md). It is what the
; table in 96.11.6.2 was taken with - two files, one shortened and one
; extended, and the SIZE read back through a fresh handle each time because
; the call answers CF=0 with AX=0 either way and there is nothing else to
; look at.
;
;   nasm -f bin -w+error -o CX0.COM tests/dostrap/cx0.asm
;   python3 tools/os88disk.py -o cx0.img --size 360 CX0.COM
;
; ...then double-click it off B: in the box, and boot your own DOS floppy with
; the same image in B: for the other column. NOTHING HERE IS THIRD-PARTY: it
; is ours, MIT with the rest of the tree.
; =============================================================================
    cpu 8086
    bits 16
    org 0x100

INIT   equ 1000                  ; the file's starting size
BACK   equ 400                   ; ...where A seeks back to
OVER   equ 1000                  ; ...and how far past the end B seeks

start:
    mov ah, 0x09
    mov dx, msg_hi
    int 0x21

    ; --- A. CX=0 at a position INSIDE the file --------------------------
    call make
    mov ax, 0x3D02
    mov dx, fnamea
    int 0x21
    jc .bad
    mov [handle], ax
    mov ax, 0x4200
    mov bx, [handle]
    xor cx, cx
    mov dx, BACK
    int 0x21
    jc .bad
    mov ah, 0x40                  ; the call under test
    mov bx, [handle]
    xor cx, cx
    mov dx, buf
    int 0x21
    mov dx, msg_a
    call report
    mov ax, 0x3D02
    mov dx, fnamea
    call size_of
    mov dx, msg_asz
    call sayn

    ; --- B. CX=0 at a position PAST the end -----------------------------
    call makeb
    mov ax, 0x3D02
    mov dx, fnameb
    int 0x21
    jc .bad
    mov [handle], ax
    mov ax, 0x4200
    mov bx, [handle]
    xor cx, cx
    mov dx, INIT + OVER
    int 0x21
    jc .bad
    mov ah, 0x40
    mov bx, [handle]
    xor cx, cx
    mov dx, buf
    int 0x21
    mov dx, msg_b
    call report
    mov ax, 0x3D02
    mov dx, fnameb
    call size_of
    mov dx, msg_bsz
    call sayn

    mov ah, 0x41
    mov dx, fnamea
    int 0x21
    mov ah, 0x41
    mov dx, fnameb
    int 0x21
    jmp .done
.bad:
    mov ah, 0x09
    mov dx, msg_bad
    int 0x21
.done:
    mov ah, 0x09
    mov dx, msg_key
    int 0x21
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C00
    int 0x21

; report - print DX$ then CF and AX from the call just made
report:
    pushf
    push ax
    push ax
    mov ah, 0x09
    int 0x21
    pop ax
    pop bx
    popf
    jc .cf1
    mov al, '0'
    jmp short .put
.cf1:
    mov al, '1'
.put:
    call put_chr
    mov ah, 0x09
    mov dx, msg_ax
    int 0x21
    mov ax, bx
    call put_dec16
    call put_crlf
    ret

; sayn - print DX$ then the word in [fsize]
sayn:
    mov ah, 0x09
    int 0x21
    mov ax, [fsize]
    call put_dec16
    call put_crlf
    ret

; make/makeb - a fresh INIT-byte file
make:
    mov dx, fnamea
    jmp short mkgo
makeb:
    mov dx, fnameb
mkgo:
    mov ah, 0x3C
    xor cx, cx
    int 0x21
    jc start.bad
    mov [handle], ax
    mov ah, 0x40
    mov bx, [handle]
    mov cx, INIT
    mov dx, buf
    int 0x21
    mov ah, 0x3E
    mov bx, [handle]
    int 0x21
    ret

; size_of - close the open handle, reopen DX (mode in AX) and stat it
size_of:
    push ax
    push dx
    mov ah, 0x3E
    mov bx, [handle]
    int 0x21
    pop dx
    pop ax
    int 0x21
    jc start.bad
    mov [handle], ax
    mov ax, 0x4202
    mov bx, [handle]
    xor cx, cx
    xor dx, dx
    int 0x21
    mov [fsize], ax
    mov ah, 0x3E
    mov bx, [handle]
    int 0x21
    ret

put_chr:
    push ax
    push dx
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop dx
    pop ax
    ret
put_crlf:
    mov al, 13
    call put_chr
    mov al, 10
    call put_chr
    ret
put_dec16:
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.emit:
    pop ax
    add al, '0'
    call put_chr
    loop .emit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

handle: dw 0
fsize:  dw 0
fnamea: db 'CX0A.TMP', 0
fnameb: db 'CX0B.TMP', 0

msg_hi:  db 13,10,'AH=40h CX=0 probe',13,10,'$'
msg_a:   db 'A inside  CF=','$'
msg_b:   db 'B pastend CF=','$'
msg_ax:  db ' AX=','$'
msg_asz: db 'A size=','$'
msg_bsz: db 'B size=','$'
msg_bad: db 'probe FAILED at a setup call',13,10,'$'
msg_key: db 'READY - press a key',13,10,'$'

    align 16
buf:
