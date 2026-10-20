; =============================================================================
; tests/dosargs/args.asm - the arguments gate's DOS program (SPEC.md 96.19)
;
; It prints its own command tail, and it prints it TWICE from the two places
; DOS puts it - because they are two different framings of one thing and a
; shim that writes only one is wrong for half the programs in the world:
;
;   COUNT  PSP:0080 is a LENGTH BYTE, and a program that treats the tail as a
;          counted string reads it. A shim that wrote only the 0Dh leaves this
;          zero and that program sees no arguments at all.
;   TERM   the text after it ends in 0Dh, and a program that PARSES its own
;          arguments scans for that. A shim that wrote only the count leaves
;          this program scanning into the FCB area and beyond.
;
; It also prints the environment's tail - the program's own path, which DOS 3+
; puts after the terminating NUL and a count word (SPEC.md 96.19.3) - because
; that went from a bare 8.3 name to a real path in the same wave and has the
; same shape of failure: it is there, it is wrong, and nothing says so.
;
; NOTHING HERE IS THIRD-PARTY. It is ours, MIT with the rest of the tree.
; =============================================================================

    cpu 8086
    bits 16
    org 0x100

start:
    mov ah, 0x09
    mov dx, msg_hi
    int 0x21

    ; --- the count ----------------------------------------------------------
    mov ah, 0x09
    mov dx, msg_cnt
    int 0x21
    xor ax, ax
    mov al, [0x80]
    call put_dec16
    call put_crlf

    ; --- the text, up to the count ------------------------------------------
    mov ah, 0x09
    mov dx, msg_arg
    int 0x21
    xor cx, cx
    mov cl, [0x80]
    jcxz .noargs
    mov si, 0x81
.ch:
    lodsb
    mov dl, al
    mov ah, 0x02
    int 0x21
    loop .ch
    jmp short .term
.noargs:
    mov ah, 0x09
    mov dx, msg_none
    int 0x21
.term:
    call put_crlf

    ; --- and where the 0Dh actually is --------------------------------------
    ; Scanned INDEPENDENTLY of the count, so a tail whose two framings
    ; disagree reads as two different numbers rather than as one right one.
    mov ah, 0x09
    mov dx, msg_trm
    int 0x21
    mov si, 0x81
    xor cx, cx
.scan:
    lodsb
    cmp al, 0x0D
    je .found
    inc cx
    cmp cx, 127
    jb .scan
    mov cx, 0xFFFF                  ; no terminator inside the PSP's 128
.found:
    mov ax, cx
    call put_dec16
    call put_crlf

    ; --- the whole SET, one '|' between variables ----------------------------
    ; Printed before the path below, out of the same segment, because a shim
    ; that puts a bare NUL in the middle of the set would show here as a SHORT
    ; list and nowhere else - the path after it would still be found, since
    ; the walk to it stops at the first double NUL either way.
    mov ah, 0x09
    mov dx, msg_set
    int 0x21
    mov ax, [0x2C]
    or ax, ax
    jz .noset
    mov es, ax
    xor di, di
.sv:
    cmp byte [es:di], 0
    je .setdone
.sc:
    mov al, [es:di]
    inc di
    or al, al
    jz .sbar
    mov dl, al
    mov ah, 0x02
    int 0x21
    jmp short .sc
.sbar:
    mov dl, '|'
    mov ah, 0x02
    int 0x21
    jmp short .sv
.noset:
    mov ah, 0x09
    mov dx, msg_none
    int 0x21
.setdone:
    call put_crlf

    ; --- the environment's tail: our own path -------------------------------
    mov ah, 0x09
    mov dx, msg_env
    int 0x21
    mov ax, [0x2C]
    or ax, ax
    jz .noenv
    mov es, ax
    xor di, di
.var:                               ; step over the SET: each variable is
    cmp byte [es:di], 0             ; NUL-terminated and a BARE NUL ends the
    je .atcount                     ; lot, so the test is made BEFORE the skip
.skip:                              ; rather than after it - doing it after
    mov al, [es:di]                 ; means stepping back onto the NUL just
    inc di                          ; consumed, and re-reading it for ever
    or al, al
    jnz .skip
    jmp short .var
.atcount:
    inc di                          ; past the set's own terminating NUL...
    add di, 2                       ; ...and the count word
.path:
    mov al, [es:di]
    or al, al
    jz .pdone
    mov dl, al
    mov ah, 0x02
    int 0x21
    inc di
    jmp short .path
.pdone:
    call put_crlf
    jmp short .fin
.noenv:
    mov ah, 0x09
    mov dx, msg_none
    int 0x21
    call put_crlf
.fin:
    mov ah, 0x09
    mov dx, msg_key
    int 0x21
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C23
    int 0x21

; -----------------------------------------------------------------------------
put_dec16:                          ; AX
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
    pop dx
    add dl, '0'
    mov ah, 0x02
    int 0x21
    loop .emit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

put_crlf:
    push ax
    push dx
    mov ah, 0x02
    mov dl, 13
    int 0x21
    mov dl, 10
    int 0x21
    pop dx
    pop ax
    ret

msg_hi:   db 13,10,'os8088 DOS arguments gate - DOSARGS.COM',13,10,13,10,'$'
msg_cnt:  db 'COUNT ','$'
msg_arg:  db 'ARGS ','$'
msg_trm:  db 'TERM ','$'
msg_set:  db 'SET ','$'
msg_env:  db 'MYPATH ','$'
msg_none: db '(none)','$'
msg_key:  db 13,10,'READY - press a key to exit with code 35',13,10,'$'
