; =============================================================================
; os8088 - tests/dosexec/parent.asm
;
; The wave-3 EXEC gate's parent (SPEC.md 96.14). It does what DOS requires of
; any program that wants to run another, and asserts the two things that can
; only be seen from here:
;
;   - a 4Bh BEFORE the parent shrinks itself must answer 8, because the
;     launched program was given the whole arena and there is no free block;
;   - after AH=4Ah, the child runs, prints, and its exit code comes back
;     through AH=4Dh - and then the PARENT is still running, which is the
;     whole point and the thing a wrong stack restore destroys.
;
; The child is passed a COMMAND TAIL, and prints it, so the parameter block's
; far pointer is exercised rather than assumed.
;
; NOTHING HERE IS THIRD-PARTY. It is ours, MIT with the rest of the tree, and
; it is under tests/ because it is not shipped software (CLAUDE.md, Layout).
; =============================================================================

    cpu 8086
    bits 16
    org 0x100

start:
    mov [parm+4], ds                ; the tail's SEGMENT, which only exists at
                                    ; run time - the parameter block carries a
                                    ; FAR pointer and a .COM cannot assemble
                                    ; its own segment into one
    mov ah, 0x09
    mov dx, msg_hi
    int 0x21

    ; --- 1. EXEC before shrinking: DOS's own refusal ------------------------
    mov ax, 0x4B00
    mov dx, cname
    mov bx, parm
    push ds
    pop es
    int 0x21
    jnc .noref
    cmp ax, 8
    jne .wrongerr
    mov ah, 0x09
    mov dx, msg_ref
    int 0x21
    jmp short .shrink
.noref:
    mov ah, 0x09
    mov dx, msg_noref
    int 0x21
    jmp .done
.wrongerr:
    push ax
    mov ah, 0x09
    mov dx, msg_wrong
    int 0x21
    pop ax
    call put_dec16
    call put_crlf
    jmp .done

    ; --- 2. shrink to 8KB, then run it --------------------------------------
.shrink:
    mov ah, 0x4A
    mov bx, 0x200                   ; 512 paragraphs = 8KB, which is plenty
    push ds                         ; for this program and leaves the rest
    pop es                          ; for the child
    int 0x21
    jc .shfail

    mov ah, 0x09
    mov dx, msg_run
    int 0x21

    mov ax, 0x4B00
    mov dx, cname
    mov bx, parm
    push ds
    pop es
    int 0x21
    jc .exfail

    ; --- 3. we are still here, which is the assertion ----------------------
    mov ah, 0x09
    mov dx, msg_back
    int 0x21
    mov ah, 0x4D
    int 0x21
    push ax
    mov ah, 0x09
    mov dx, msg_code
    int 0x21
    pop ax
    xor ah, ah
    call put_dec16
    call put_crlf
    jmp short .done

.shfail: mov dx, msg_esh
         jmp short .say
.exfail: push ax
         mov ah, 0x09
         mov dx, msg_eex
         int 0x21
         pop ax
         call put_dec16
         call put_crlf
         jmp short .done
.say:
    mov ah, 0x09
    int 0x21
.done:
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
cname:    db 'DOSKID.COM', 0
tail:     db 7, ' HELLO', 13
parm:     dw 0                      ; inherit the environment
          dw tail                   ; ...and the command tail, far - its
          dw 0                      ; segment is patched in at start:
          dd 0                      ; the two FCBs, which nothing here parses
          dd 0

msg_hi:   db 13,10,'os8088 DOS exec gate - DOSEXEC.COM',13,10,13,10,'$'
msg_ref:  db 'REFUSED 8 before the shrink, as DOS does',13,10,'$'
msg_noref: db 'FAILED - 4Bh worked with no free memory',13,10,'$'
msg_wrong: db 'FAILED - the wrong refusal code: ','$'
msg_run:  db 'RUNNING the child...',13,10,'$'
msg_back: db 'BACK in the parent',13,10,'$'
msg_code: db 'CHILD ','$'
msg_esh:  db 'FAILED at AH=4Ah',13,10,'$'
msg_eex:  db 'FAILED at AH=4Bh, code ','$'
msg_key:  db 13,10,'READY - press a key to exit with code 33',13,10,'$'
