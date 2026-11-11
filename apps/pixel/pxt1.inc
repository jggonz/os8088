; os8088 - apps/pixel/pxt1.inc - a colour's 1bpp threshold and its luma
; (SPEC.md 106.11, pixelsim's thresh1), shared with the Browser (71.26)

; px_t1for - the 1bpp threshold of the colour at DS:SI: AL = (64 L + 127)
; div 255, L = (Y + (Y^2 + 127) div 255) >> 1, Y the luma. Preserves all
; but AX
px_t1for:
    push bx
    push dx
    call px_luma                    ; AX = Y
    mov bx, ax
    mul ax
    add ax, 127
    adc dx, 0
    push cx
    mov cx, 255
    div cx                          ; AX = (Y^2 + 127) div 255
    add ax, bx
    shr ax, 1                       ; L
    mov dx, 64
    mul dx
    add ax, 127
    adc dx, 0
    div cx
    pop cx
    pop dx
    pop bx
    ret

; px_luma - AX = (77 R + 150 G + 29 B) >> 8 of DS:SI. Preserves all but AX
px_luma:
    push bx
    push dx
    mov al, [si]
    mov bl, 77
    mul bl
    mov bx, ax
    mov al, [si + 1]
    mov dl, 150
    mul dl
    add bx, ax
    mov al, [si + 2]
    mov dl, 29
    mul dl
    add ax, bx
    mov al, ah
    xor ah, ah
    pop dx
    pop bx
    ret
