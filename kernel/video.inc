; =============================================================================
; video.inc - text and pixel output via BIOS int 10h
;
; Everything here goes through the BIOS rather than banging the CRTC directly.
; It is slower, but it works on every machine that can boot us, and scrolling
; comes for free with the teletype call.
; =============================================================================

; -----------------------------------------------------------------------------
; putc - write one character at the cursor and advance it
; in:  AL = character
; -----------------------------------------------------------------------------
putc:
    push ax
    push bx
    mov ah, 0x0E
    mov bh, 0x00                ; page 0
    mov bl, 0x07                ; colour, only consulted in graphics modes
    int 0x10
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; puts - write the NUL-terminated string at DS:SI
; -----------------------------------------------------------------------------
puts:
    push ax
    push si
.next:
    lodsb
    test al, al
    jz .done
    call putc
    jmp .next
.done:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; puts_far - write the NUL-terminated string at ES:SI
;
; The syscall version: callers live in their own segment, so they hand us a
; segment along with the offset instead of hoping DS happens to be right.
; -----------------------------------------------------------------------------
puts_far:
    push ax
    push si
.next:
    mov al, [es:si]
    inc si
    test al, al
    jz .done
    call putc
    jmp .next
.done:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; newline - CR/LF
; -----------------------------------------------------------------------------
newline:
    push ax
    mov al, 13
    call putc
    mov al, 10
    call putc
    pop ax
    ret

; -----------------------------------------------------------------------------
; clear_screen - blank the 80x25 window and park the cursor at the top left
; -----------------------------------------------------------------------------
clear_screen:
    push ax
    push bx
    push cx
    push dx
    mov ax, 0x0600              ; scroll up, AL=0 means blank the whole window
    mov bh, 0x07                ; grey on black
    xor cx, cx                  ; from (0,0)
    mov dx, 0x184F              ; to (24,79)
    int 0x10
    mov ah, 0x02                ; move cursor
    xor bh, bh
    xor dx, dx
    int 0x10
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; set_mode - switch BIOS video mode
; in:  AL = mode (0x03 text 80x25, 0x13 graphics 320x200x256)
; -----------------------------------------------------------------------------
set_mode:
    push ax
    xor ah, ah
    int 0x10
    pop ax
    ret

; -----------------------------------------------------------------------------
; put_pixel - plot a single pixel through the BIOS
; in:  CX = x, DX = y, AL = colour
;
; Fine for a handful of pixels. Anything that fills the screen should write
; to A000:0000 directly, the way gfx_demo does.
; -----------------------------------------------------------------------------
put_pixel:
    push ax
    push bx
    mov ah, 0x0C
    xor bh, bh
    int 0x10
    pop bx
    pop ax
    ret
