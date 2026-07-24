; =============================================================================
; keyboard.inc - blocking keyboard input via BIOS int 16h
; =============================================================================

; -----------------------------------------------------------------------------
; getkey - block until a key is pressed
; out: AL = ASCII (0 for keys with no ASCII value), AH = scan code
; -----------------------------------------------------------------------------
getkey:
    xor ah, ah
    int 0x16
    ret

; -----------------------------------------------------------------------------
; readline - read an edited line into DS:DI, echoing as we go
; in:  DS:DI = buffer, CX = buffer size including the terminator
; out: buffer holds a NUL-terminated string
;
; Handles backspace and ignores control characters. There is no history and
; no cursor movement; this is a prompt, not an editor.
; -----------------------------------------------------------------------------
readline:
    push ax
    push bx
    push cx
    push dx
    push di

    mov bx, di                  ; remember where the line starts
    dec cx                      ; leave room for the NUL

.loop:
    call getkey

    cmp al, 13                  ; enter
    je .done
    cmp al, 8                   ; backspace
    je .backspace
    cmp al, 32                  ; anything else below space is not ours
    jb .loop

    mov dx, di                  ; full?
    sub dx, bx
    cmp dx, cx
    jae .loop

    mov [di], al
    inc di
    call putc
    jmp .loop

.backspace:
    cmp di, bx                  ; nothing to erase
    je .loop
    dec di
    mov al, 8                   ; back up, blank the cell, back up again
    call putc
    mov al, ' '
    call putc
    mov al, 8
    call putc
    jmp .loop

.done:
    mov byte [di], 0
    call newline

    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
