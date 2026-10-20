; =============================================================================
; os8088 - tests/dosxms/xms.asm
;
; The wave-3 XMS gate's DOS program (SPEC.md 96.15). On an 8088 with no
; extended memory the whole assertion is a REFUSAL, and it is worth a row
; because getting it wrong is silent both ways:
;
;   - int 2Fh AX=4300h must answer AL != 80h. A shim that says "yes" here and
;     then refuses every call leaves the program worse off than one that says
;     no, because the program has already committed to XMS by then;
;   - every OTHER multiplex number must answer AL = 0, "nobody is here". That
;     is a thing an UNHOOKED vector cannot say, and it is most of what hooking
;     int 2Fh buys (SPEC.md 96.15.1);
;   - and asking has to RETURN. An unhooked 2Fh on a machine whose ROM does
;     not implement it is how a probe becomes a hang.
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

    mov ax, 0x4300                  ; is an XMS driver there?
    int 0x2F
    push ax
    mov ah, 0x09
    mov dx, msg_xms
    int 0x21
    pop ax
    xor ah, ah
    call put_hex8
    call put_crlf

    mov ax, 0x1600                  ; ...and somebody ELSE's multiplex number,
    int 0x2F                        ; which nobody here is
    push ax
    mov ah, 0x09
    mov dx, msg_other
    int 0x21
    pop ax
    xor ah, ah
    call put_hex8
    call put_crlf

    mov ah, 0x09
    mov dx, msg_alive
    int 0x21                        ; ...and we got here, which is the third

    mov ah, 0x09
    mov dx, msg_key
    int 0x21
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C21
    int 0x21

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

put_hex8:
    push ax
    push cx
    mov cl, al
    mov al, cl
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call .dig
    mov al, cl
    and al, 0x0F
    call .dig
    pop cx
    pop ax
    ret
.dig:
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 7
.emit:
    call put_chr
    ret

msg_hi:    db 13,10,'os8088 DOS xms gate - DOSXMS.COM',13,10,13,10,'$'
msg_xms:   db 'XMS4300 ','$'
msg_other: db 'MUX1600 ','$'
msg_alive: db 'ALIVE - int 2Fh returned',13,10,'$'
msg_key:   db 13,10,'READY - press a key to exit with code 33',13,10,'$'
