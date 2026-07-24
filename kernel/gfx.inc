; =============================================================================
; gfx.inc - mode 13h demo
;
; This is deliberately the crudest thing that proves the graphics path works:
; switch to 320x200x256, fill the framebuffer by hand, wait, switch back.
; The C layer will replace this, and it will do it the same way - map A000
; as a 64000-byte array and write to it.
; =============================================================================

; -----------------------------------------------------------------------------
; gfx_demo - diagonal gradient plus a palette strip, any key returns to text
; -----------------------------------------------------------------------------
gfx_demo:
    push ax
    push bx
    push cx
    push dx
    push di
    push es

    mov al, 0x13                ; 320x200, 256 colours, one byte per pixel
    call set_mode

    mov ax, VGA_SEG
    mov es, ax
    xor di, di                  ; the whole frame is 64000 bytes, so DI never wraps

    xor dx, dx                  ; y
.row:
    xor cx, cx                  ; x
.col:
    mov ax, cx                  ; colour = (x + y) / 2, a diagonal ramp
    add ax, dx
    shr ax, 1
    stosb
    inc cx
    cmp cx, 320
    jb .col
    inc dx
    cmp dx, 200
    jb .row

    ; a strip of all 256 colours across the bottom, so the palette is visible
    mov di, 180 * 320           ; start of row 180
    mov dx, 20                  ; rows tall
.strip_row:
    xor cx, cx
.strip_col:
    mov ax, cx                  ; 320 pixels across 256 colours, near enough
    stosb
    inc cx
    cmp cx, 320
    jb .strip_col
    dec dx
    jnz .strip_row

    call getkey

    mov al, 0x03                ; back to 80x25 text
    call set_mode
    call clear_screen

    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
