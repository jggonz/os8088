; =============================================================================
; os8088 - tests/dosexec/kid.asm
;
; The wave-3 EXEC gate's CHILD (SPEC.md 96.14). Three lines, and each is an
; assertion its parent cannot make for it:
;
;   - it prints, which proves it was loaded, relocated and entered;
;   - it prints its COMMAND TAIL out of its own PSP:0080, which is the far
;     pointer in the parameter block arriving intact;
;   - it reads PSP:0016 and prints it as "has a parent" or not, which is the
;     one PSP field only a child has;
;   - and it exits 7, which the parent reads back through AH=4Dh.
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

    mov ah, 0x09                    ; the tail, straight out of the PSP
    mov dx, msg_tail
    int 0x21
    mov si, 0x80
    lodsb
    mov cl, al
    xor ch, ch
    jcxz .notail
.emit:
    lodsb
    mov dl, al
    mov ah, 0x02
    int 0x21
    loop .emit
.notail:
    call put_crlf

    mov ax, [0x16]                  ; PSP:0016 - the parent's PSP, which is 0
    or ax, ax                       ; only at the top level
    jz .noparent
    mov dx, msg_par
    jmp short .say
.noparent:
    mov dx, msg_nopar
.say:
    mov ah, 0x09
    int 0x21

    mov ax, 0x4C07                  ; ...and 7 is what AH=4Dh has to answer
    int 0x21

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

msg_hi:    db 'CHILD speaking',13,10,'$'
msg_tail:  db 'TAIL:','$'
msg_par:   db 'PARENT yes',13,10,'$'
msg_nopar: db 'PARENT no - FAILED',13,10,'$'
