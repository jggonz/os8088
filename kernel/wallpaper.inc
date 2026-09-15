; Wallpaper renderer (SPEC.md 76.2.2). Included in vga12.inc, kern_big only.
; Persistent bitmap data: repainting a vacated window must never read a disk.
WP_COUNT equ 18
WP_DIE equ 17
wp_colors: db 0, 15, 1, 3, 5, 9, 7, 2
wp_patterns:
    db 0xEE,0xFF,0xBF,0xF9,0xF9,0xFF,0xDF,0xFF ; Confetti
    db 0x7E,0xBD,0xDB,0xE7,0x7E,0xBD,0xDB,0xE7 ; Zigzag
    db 0xF0,0xF0,0xF0,0xF0,0x0F,0x0F,0x0F,0x0F ; Checker
    db 0x77,0xBB,0xDD,0xEE,0x77,0xBB,0xDD,0xEE ; Weave
    db 0x00,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F ; Grid
    db 0x7F,0xFF,0xFF,0xFF,0xF7,0xFF,0xFF,0xFF ; Dots
    db 0xE3,0xDD,0xDD,0xE3,0xEF,0xEF,0xEF,0x0F ; Circuit
    db 0x7F,0xBF,0xDF,0xEF,0xF7,0xFB,0xFD,0xFE ; Hatch
wp_black: times 8 db 0
%include "kernel/wallpaper-die.inc"

; Same register/clip contract as thm_desk. Resolve each display before
; centering the image; nested fills retain that display's coordinate space.
wp_draw:
    GFXCLIP wp_display
wp_display:
    GFXDISP wp_raw
wp_raw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push word [gfx_pat]
    mov si, wp_kind
    cmp byte [si], WP_DIE
    je .die
    mov al, [si]
    xor ah, ah
    mov si, ax
    cmp si, 9
    jae .pattern
    mov al, [wp_colors+si-1]
    mov [gfx_color], al
    pop word [gfx_pat]
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    jmp gfx_fill_raw
.pattern:
    sub si, 9
    shl si, 1
    shl si, 1
    shl si, 1
    add si, wp_patterns
    mov [gfx_pat], si
    ; Original x1 was saved before AL became the wallpaper id.
    mov bp, sp
    mov ax, [ss:bp+14]
    call gfx_fill_pat_raw
    jmp .out
.die:
    mov [wp_x1], ax
    mov [wp_y1], bx
    mov [wp_x2], cx
    mov [wp_y2], dx
    mov di, [vid_cw]
    sub di, 128
    shr di, 1
    and di, 0xFFF8
    mov bp, [vid_ch]
    sub bp, 128
    shr bp, 1
    and bp, 0xFFF8
    ; Four disjoint strips around the bitmap, intersected with the damage.
    mov word [gfx_pat], wp_black
    mov dx, bp
    dec dx
    call wp_piece
    mov bx, bp
    add bx, 128
    mov dx, [wp_y2]
    call wp_piece
    mov bx, bp
    mov dx, bp
    add dx, 127
    mov cx, di
    dec cx
    call wp_piece
    mov ax, di
    add ax, 128
    mov cx, [wp_x2]
    call wp_piece
    ; Tile-major storage lets the existing masked pattern fill handle even
    ; one-pixel damage edges. No pixel outside the requested rect is written.
    mov si, wp_die_bits
    mov bx, bp
.row:
    mov ax, di
.col:
    mov [gfx_pat], si
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call wp_piece
    add si, 8
    add ax, 8
    mov cx, di
    add cx, 128
    cmp ax, cx
    jb .col
    add bx, 8
    mov dx, bp
    add dx, 128
    cmp bx, dx
    jb .row
.out:
    pop word [gfx_pat]
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Intersect a bitmap tile/background strip with the caller's damage rect.
; gfx_fill_pat supplies the window clipping and adapter's masked byte edges.
wp_piece:
    push ax
    push bx
    push cx
    push dx
    cmp ax, [wp_x1]
    jge .x1
    mov ax, [wp_x1]
.x1:
    cmp bx, [wp_y1]
    jge .y1
    mov bx, [wp_y1]
.y1:
    cmp cx, [wp_x2]
    jle .x2
    mov cx, [wp_x2]
.x2:
    cmp dx, [wp_y2]
    jle .y2
    mov dx, [wp_y2]
.y2:
    cmp ax, cx
    jg .out
    cmp bx, dx
    jg .out
    call gfx_fill_pat_raw
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
section .bss
wp_x1: resw 1
wp_y1: resw 1
wp_x2: resw 1
wp_y2: resw 1
section .text
