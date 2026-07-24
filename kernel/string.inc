; =============================================================================
; string.inc - the small amount of string handling the shell needs
; =============================================================================

; -----------------------------------------------------------------------------
; strcmp - compare the NUL-terminated strings at DS:SI and DS:DI
; out: ZF set if equal
; -----------------------------------------------------------------------------
strcmp:
    push si
    push di
    push ax
.next:
    mov al, [si]
    cmp al, [di]
    jne .differ
    test al, al                 ; both ended together
    jz .equal
    inc si
    inc di
    jmp .next
.differ:
    ; leave ZF clear; the cmp above already did that
    pop ax
    pop di
    pop si
    ret
.equal:
    cmp al, al                  ; force ZF set without disturbing anything else
    pop ax
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; skip_spaces - advance SI past any leading blanks
; -----------------------------------------------------------------------------
skip_spaces:
    push ax
.next:
    mov al, [si]
    cmp al, ' '
    je .skip
    cmp al, 9                   ; tab
    je .skip
    pop ax
    ret
.skip:
    inc si
    jmp .next

; -----------------------------------------------------------------------------
; print_dec - print AX as unsigned decimal
;
; Divide by ten, push the remainders, pop them back out. The digits come off
; the stack in the order we want to print them.
; -----------------------------------------------------------------------------
print_dec:
    push ax
    push bx
    push cx
    push dx

    mov bx, 10
    xor cx, cx                  ; digit count

.divide:
    xor dx, dx
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .divide

.emit:
    pop ax
    add al, '0'
    call putc
    loop .emit

    pop dx
    pop cx
    pop bx
    pop ax
    ret
