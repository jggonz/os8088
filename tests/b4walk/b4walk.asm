; =============================================================================
; os8088 - tests/b4walk/b4walk.asm
;
; THE FIXTURE FOR SPEC.md 5.4.1.5: one window whose W_PAINT is ONE
; OSAPI_GFX_BLIT4 of a 200 x 112 block of packed pixels, every pixel a colour
; drawn at random out of eight - so the block is about a run a pixel, which is
; the case a covered blit used to pay a whole gfx_fill per pixel for, and the
; case a walk that silently fell back to that would be caught on.
;
; tests/b4walk.py drives it: a Disk window is moved over part of the block and
; then moved again, so the heal's region is in pieces, and the row reads the
; heal's cost, its gfx_fill calls, every piece's rect and the pixels.
;
; IT ASSERTS NOTHING, tests/filler/filler.asm's rule: an instrument that can
; fail its own assertions cannot be used to set up somebody else's.
;
; The pattern is a fixed LCG, so every run - and both kernels of an A/B - blits
; the same bytes. The window sits at the right of the screen, so a Disk window
; to its left ends INSIDE the block - and a window edge is an EVEN number of
; pixels off the content grid (SPEC.md 11.94 snaps a frame's x to put its
; content on 8, and a frame is 2 wider than its content with its shadow), so
; the block's x is the content origin + 9, ODD: the Disk window's edge then
; starts its piece on a LOW nibble and the block's own left edge on a high one,
; and the row asserts it met both.
;
; NOT SHIPPED and nothing in `all` builds it: $(BUILD)/b4walk.o88 is the row's
; `wants=`, and the row writes its own scratch disk.
;
; Prefix bw_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'B4WALK', bw_entry, 0, OS88_STACK_256

BW_W        equ 200                 ; the block, in pixels...
BW_H        equ 112
BW_STRIDE   equ BW_W / 2            ; ...and its row, two pixels a byte
BW_OFF      equ 9                   ; its corner, from the content origin: ODD

; -----------------------------------------------------------------------------
; bw_entry - the pattern, then the window
; -----------------------------------------------------------------------------
bw_entry:
    push ax                         ; (NOT BX: the loader takes the window
    push cx                         ; from it, kernel/loader.inc step 9)
    push dx                         ; DX is the instance index: WM_CREATE's
    push si
    push di
    push dx
    mov di, bw_pix
    mov cx, BW_STRIDE * BW_H
    mov ax, 0x1D2B                  ; the LCG's seed
.pat:
    mov dx, 25173
    mul dx
    add ax, 13849                   ; x = x * 25173 + 13849 (mod 65536)
    mov bl, ah
    and bx, 7
    mov dl, [bw_set + bx]           ; the high nibble: one of the eight...
    mov bl, ah
    shr bl, 1
    shr bl, 1
    shr bl, 1
    and bx, 7
    mov dh, [bw_set + bx]           ; ...and the low, off three other bits
    shl dl, 1
    shl dl, 1
    shl dl, 1
    shl dl, 1
    or dl, dh
    mov [di], dl
    inc di
    loop .pat
    pop dx
    mov si, bw_tpl
    call OSAPI_WM_CREATE
    jc .out
    OS88_REGION_MOVABLE             ; nothing here points into the image
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bw_paint - W_PAINT: the block, one call
; in:  SI = window ptr; the gfx lock is held
; -----------------------------------------------------------------------------
bw_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    add ax, BW_OFF
    mov bx, dx
    add bx, BW_OFF
    ; THE BLOCK IS CLEARED FIRST, ON PURPOSE - the one place in the tree a
    ; pixel is written twice (CLAUDE.md, Performance rule 2), because this is
    ; an instrument and not an application. The clear is clipped exactly by
    ; gfx_fill, so a pixel of the region a walk fails to draw is left in a
    ; colour the pattern never uses - where without it the pixel would keep
    ; whatever the last paint left there, which is usually right
    push ax
    mov cx, ax
    add cx, BW_W - 1
    mov dx, bx
    add dx, BW_H - 1
    mov al, 12
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    push ds
    pop es                          ; the block is OURS (SPEC.md 5.4)
    mov si, bw_pix
    mov bp, BW_STRIDE
    mov cx, BW_W
    mov dx, BW_H
    call OSAPI_GFX_BLIT4
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bw_set:     db 0, 1, 2, 4, 7, 9, 14, 15    ; never 12, the clear's

bw_tpl:                             ; the FRAME, so the title and the border
    dw 400, 120, BW_W + 2 * BW_OFF + 8, BW_H + 2 * BW_OFF + 32  ; are on top
    dw bw_title, bw_paint, 0, 0

bw_title:   db 'B4 Walk', 0

    OS88_BSS BW_STRIDE * BW_H
    OS88_IMAGE_END

bw_pix      equ os88_image_end      ; the block, BW_STRIDE x BW_H bytes
