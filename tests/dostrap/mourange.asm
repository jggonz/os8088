; =============================================================================
; os8088 - tests/dostrap/mourange.asm
;
; **WHAT DOES A REAL DRIVER DO WITH AX=7 AND AX=8?** (SPEC.md 96.10.6)
;
; `dos_int33` answers both as no-ops - "we clamp nothing, the host's pointer
; is already inside the screen" - and Battle Chess is the report that says
; otherwise: it sets `AX=7 CX=0 DX=013Fh` and `AX=8 CX=0 DX=00C7h`, a
; 320x200 window, and then polls `AX=3` for ever. This box answered x=320 on
; the very first read, one past the range the program had just asked for.
;
; Like every probe in this directory it runs under OUR box and under a real
; IBM DOS 3.30 with CuteMouse UNCHANGED, and the reference is the point: the
; question is not what the documentation says, it is what the driver every
; one of these programs was written against actually answers.
;
; The steps, and what each one settles:
;
;   1. AX=0 reset, then AX=3            - where a driver starts (measured at
;                                         320,96 on CuteMouse 1.9.1)
;   2. AX=7/AX=8 for 0..319 and 0..199  - the window Battle Chess asks for
;   3. AX=3 again, WITHOUT MOVING       - does setting the range move the
;                                         pointer that is now outside it?
;   4. move the mouse hard right/down,  - is the answer CAPPED at the range,
;      then AX=3                          or does it run on to 639/199?
;   5. AX=4 to (600,190), then AX=3     - does an out-of-range WARP clamp?
;   6. AX=7/AX=8 back to 0..639/0..199,
;      then AX=3                        - and does widening it again leave
;                                         the pointer where it was?
;
; NOTHING HERE IS THIRD-PARTY: ours, MIT with the rest of the tree, under
; tests/ because it is not shipped software (CLAUDE.md, Layout).
; =============================================================================

    cpu 8086
    bits 16
    org 0x100

start:
    mov ah, 0x09
    mov dx, msg_hi
    int 0x21

    ; --- 1. reset, and where it starts --------------------------------------
    xor ax, ax
    int 0x33
    push bx
    push ax
    mov ah, 0x09
    mov dx, msg_rst
    int 0x21
    pop ax
    call put_hex16
    mov ah, 0x09
    mov dx, msg_bx
    int 0x21
    pop ax
    call put_dec16
    call put_crlf

    mov ax, 3
    int 0x33
    mov si, msg_pos1
    call put_pos

    ; --- 2. the window Battle Chess asks for --------------------------------
    mov ax, 7
    xor cx, cx
    mov dx, 319
    int 0x33
    mov ax, 8
    xor cx, cx
    mov dx, 199
    int 0x33

    ; --- 3. ...and the same read again, with NOTHING moved ------------------
    mov ax, 3
    int 0x33
    mov si, msg_pos2
    call put_pos

    ; --- 4. now move it hard right and down ---------------------------------
    mov dx, msg_p1
    call prompt
    mov ax, 3
    int 0x33
    mov si, msg_pos3
    call put_pos

    ; --- 5. a WARP to a point outside the window ----------------------------
    mov ax, 4
    mov cx, 600
    mov dx, 190
    int 0x33
    mov ax, 3
    int 0x33
    mov si, msg_pos4
    call put_pos

    ; --- 6. and the window opened up again ----------------------------------
    mov ax, 7
    xor cx, cx
    mov dx, 639
    int 0x33
    mov ax, 8
    xor cx, cx
    mov dx, 199
    int 0x33
    mov ax, 3
    int 0x33
    mov si, msg_pos5
    call put_pos

    mov dx, msg_key
    call prompt
    mov ax, 0x4C00
    int 0x21

; -----------------------------------------------------------------------------
prompt:
    mov ah, 0x09
    int 0x21
    mov ah, 0x08
    int 0x21
    ret

; put_pos - SI = the "$"-terminated label; BX/CX/DX are INT 33h's answer
put_pos:
    push bx
    push cx
    push dx
    mov dx, si
    mov ah, 0x09
    int 0x21
    pop dx
    pop cx
    pop bx
    push bx
    push dx
    mov ax, cx
    call put_dec16
    mov ah, 0x09
    mov dx, msg_y
    int 0x21
    pop ax
    call put_dec16
    mov ah, 0x09
    mov dx, msg_b
    int 0x21
    pop ax
    call put_dec16
    call put_crlf
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

put_hex16:
    push ax
    push bx
    push cx
    mov bx, ax
    mov cx, 4
.next:
    mov ax, bx
    rol ax, 1
    rol ax, 1
    rol ax, 1
    rol ax, 1
    mov bx, ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 7
.emit:
    call put_chr
    loop .next
    pop cx
    pop bx
    pop ax
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

; -----------------------------------------------------------------------------
msg_hi:   db 13,10,'INT 33h range probe - MOURANGE.COM',13,10,13,10,'$'
msg_rst:  db 'RESET ax=','$'
msg_bx:   db ' bx=','$'
; **A SPACE BEFORE EVERY `x=`**, which is not cosmetic: every reader of these
; lines splits on whitespace and takes the `k=v` tokens, so `319x199)x=82`
; parses as a field called `319x199)x` and the row fails saying it could not
; find a position - a sentence about the box, for a label.
msg_pos1: db 'POS1 (reset)         x=','$'
msg_pos2: db 'POS2 (range 319x199) x=','$'
msg_pos3: db 'POS3 (moved right)   x=','$'
msg_pos4: db 'POS4 (warp 600,190)  x=','$'
msg_pos5: db 'POS5 (range 639x199) x=','$'
msg_y:    db ' y=','$'
msg_b:    db ' b=','$'
msg_p1:   db 'MOVE the mouse right and down, then press a key',13,10,'$'
msg_key:  db 13,10,'READY - press a key to exit',13,10,'$'
