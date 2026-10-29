; =============================================================================
; os8088 - apps/win16/w16gdi.inc
;
; GDI, S3'S SUBSET (docs/plans/WIN16-PLAN.md S3): one device context on the
; program's window, the stock objects, solid brushes, and the four drawing
; calls WINDOW.EXE makes - MoveTo/LineTo, Rectangle and TextOut. A colour is
; the nearest of Windows' sixteen, which are os8088's sixteen (CBLACK..
; CWHITE) in another order; S4 brings the System font (SPEC.md 124.7) and the
; rest.
;
; THE CLIP IS TWO CLIPS. The kernel's (OSAPI_WM_CLIP_SET, or the damage inside
; W_PAINT) keeps every pixel inside what is visible of the content; WIN16's
; own, [w16_cl]..[w16_cb] in screen coordinates, keeps it inside rcPaint, so a
; WM_PAINT for a counter's strip repaints the strip and not the window.
; =============================================================================

W16_HDC     equ 0x3002          ; the one DC
W16_STOCK   equ 0x3100          ; GetStockObject(i) = W16_STOCK + i
W16_BRUSH0  equ 0x3200          ; CreateSolidBrush's, W16_BRUSH0 + slot
W16_NBRUSH  equ 8
W16_NOCOL   equ 0xFF            ; a NULL brush or pen: draw nothing

WHITE_BRUSH equ 0
BLACK_PEN   equ 7
SYSTEM_FONT equ 13
STOCK_LAST  equ 16

; -----------------------------------------------------------------------------
; w16_dcopen - open the DC on a window. AL = 1 BeginPaint's (the clip is the
; invalid rectangle, and [w16_prc] its client-coordinate rcPaint) / 0 the
; whole client. BX = the window. Takes the lock and arms the kernel's clip
; unless the resuming callback holds both (SPEC.md 124.8). Keeps every register.
; -----------------------------------------------------------------------------
w16_dcopen:
    push ax
    push bx
    push cx
    push dx
    mov [w16_dcwnd], bx
    mov [w16_dcpaint], al
    cmp byte [w16_dcon], 0
    jne .clip                       ; open already: a new clip, same DC
    mov byte [w16_dcon], 1
    mov word [w16_pen], W16_STOCK + BLACK_PEN
    mov word [w16_brush], W16_STOCK + WHITE_BRUSH
    mov word [w16_font], W16_STOCK + SYSTEM_FONT
    xor ax, ax
    mov [w16_curx], ax
    mov [w16_cury], ax
    mov [w16_dchide], al
    cmp byte [w16_locked], 0
    jne .held
    cmp byte [w16_ltook], 0
    jne .held
    call OSAPI_GFX_LOCK
    mov byte [w16_dctook], 1
.held:
    cmp byte [w16_inpaint], 0       ; inside W_PAINT the damage is the clip
    jne .clip
    mov bx, [w16_win]
    call OSAPI_WM_CLIP_SET
    jnc .armed
    mov byte [w16_dchide], 1        ; not a pixel visible
    jmp short .clip
.armed:
    mov byte [w16_dcclip], 1
.clip:
    mov bx, [w16_win]
    call OSAPI_WM_CONTENT
    mov [w16_ox], ax
    mov [w16_oy], dx
    call OSAPI_WM_GEOM              ; CX/DX = the client's size
    jc .empty
    cmp byte [w16_dchide], 0
    jne .empty
    xor ax, ax
    mov [w16_prc], ax
    mov [w16_prc + 2], ax
    mov [w16_prc + 4], cx
    mov [w16_prc + 6], dx
    cmp byte [w16_dcpaint], 0
    je .screen
    mov bx, [w16_dcwnd]
    test byte [bx + W16W_FLAGS], W16WF_INVAL
    jz .empty                       ; nothing invalid: an empty rcPaint
    mov ax, [bx + W16W_UL]          ; rcPaint = the invalid rectangle cut to
    or ax, ax                       ;   the client
    jl .l
    mov [w16_prc], ax
.l:
    mov ax, [bx + W16W_UT]
    or ax, ax
    jl .t
    mov [w16_prc + 2], ax
.t:
    mov ax, [bx + W16W_UR]
    cmp ax, cx
    jg .r
    mov [w16_prc + 4], ax
.r:
    mov ax, [bx + W16W_UB]
    cmp ax, dx
    jg .screen
    mov [w16_prc + 6], ax
.screen:
    mov ax, [w16_prc]               ; ...and in screen coordinates, inclusive
    add ax, [w16_ox]
    mov [w16_cl], ax
    mov ax, [w16_prc + 2]
    add ax, [w16_oy]
    mov [w16_ct], ax
    mov ax, [w16_prc + 4]
    add ax, [w16_ox]
    dec ax
    mov [w16_cr], ax
    mov ax, [w16_prc + 6]
    add ax, [w16_oy]
    dec ax
    mov [w16_cb], ax
    jmp short .out
.empty:
    xor ax, ax
    mov [w16_prc], ax
    mov [w16_prc + 2], ax
    mov [w16_prc + 4], ax
    mov [w16_prc + 6], ax
    mov word [w16_cl], 1
    mov word [w16_cr], 0
    mov word [w16_ct], 1
    mov word [w16_cb], 0
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_dcclose - give back what w16_dcopen took. Keeps every register.
w16_dcclose:
    cmp byte [w16_dcon], 0
    je .out
    mov byte [w16_dcon], 0
    cmp byte [w16_dcclip], 0
    je .lock
    mov byte [w16_dcclip], 0
    call OSAPI_WM_CLIP_CLEAR
.lock:
    cmp byte [w16_dctook], 0
    je .out
    mov byte [w16_dctook], 0
    call OSAPI_GFX_UNLOCK
.out:
    ret

; w16_gfill - fill AX/BX/CX/DX (screen, inclusive) in [w16_gcol], cut to the
; DC's clip. Keeps every register.
w16_gfill:
    cmp byte [w16_dcon], 0
    je .ret
    push ax
    push bx
    push cx
    push dx
    cmp ax, [w16_cl]
    jge .l
    mov ax, [w16_cl]
.l:
    cmp bx, [w16_ct]
    jge .t
    mov bx, [w16_ct]
.t:
    cmp cx, [w16_cr]
    jle .r
    mov cx, [w16_cr]
.r:
    cmp dx, [w16_cb]
    jle .b
    mov dx, [w16_cb]
.b:
    cmp ax, cx
    jg .out
    cmp bx, dx
    jg .out
    push ax
    mov al, [w16_gcol]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; w16_brcol - AX = a brush or pen handle -> AL = its colour, or W16_NOCOL.
; A small integer is a COLOR_* + 1 class brush. Keeps all but AL.
w16_brcol:
    push bx
    cmp ax, 0x100
    jae .obj
    mov bl, CLGRAY
    cmp ax, 16                      ; COLOR_BTNFACE + 1
    je .have
    mov bl, CWHITE
    or ax, ax
    jnz .have
    mov bl, W16_NOCOL
    jmp short .have
.obj:
    mov bx, ax
    sub bx, W16_STOCK
    jb .none
    cmp bx, 9
    jae .made
    mov bl, [w16_stockcol + bx]
    jmp short .have
.made:
    mov bx, ax
    sub bx, W16_BRUSH0
    jb .none
    cmp bx, W16_NBRUSH
    jae .none
    cmp byte [w16_brused + bx], 0
    je .none
    mov bl, [w16_brc + bx]
    jmp short .have
.none:
    mov bl, W16_NOCOL
.have:
    mov al, bl
    pop bx
    ret

; the stock objects 0-8: WHITE, LTGRAY, GRAY, DKGRAY, BLACK and NULL brushes,
; WHITE, BLACK and NULL pens
w16_stockcol:
    db CWHITE, CLGRAY, CDGRAY, CDGRAY, CBLACK, W16_NOCOL, CWHITE, CBLACK, W16_NOCOL

; w16_rgb2col - AL = red, AH = green, DL = blue -> AL = the nearest of the
; sixteen, by the sum of the three distances. Keeps all but AL.
w16_rgb2col:
    push bx
    push cx
    push dx
    push si
    push di
    mov [w16_rgb], ax
    mov [w16_rgb + 2], dl
    mov si, w16_pal16
    mov di, 0xFFFF                  ; DI = the best distance
    xor cx, cx                      ; CL = this colour, CH = the best
.c:
    xor dx, dx
    xor bx, bx
.ch:
    mov al, [si + bx]
    sub al, [w16_rgb + bx]
    jnb .pos
    neg al
.pos:
    xor ah, ah
    add dx, ax
    inc bx
    cmp bx, 3
    jb .ch
    cmp dx, di
    jae .worse
    mov di, dx
    mov ch, cl
.worse:
    add si, 3
    inc cl
    cmp cl, 16
    jb .c
    mov al, ch
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; Windows' sixteen, in os8088's order (CBLACK .. CWHITE): red, green, blue
w16_pal16:
    db 0,0,0,       0,0,128,     0,128,0,     0,128,128
    db 128,0,0,     128,0,128,   128,128,0,   192,192,192
    db 128,128,128, 0,0,255,     0,255,0,     0,255,255
    db 255,0,0,     255,0,255,   255,255,0,   255,255,255

; =============================================================================
; The objects
; =============================================================================

; GetStockObject (GDI.87) - args: +0 fnObject
w16g_stock:
    call w16_args
    mov ax, [es:bx]
    cmp ax, STOCK_LAST
    ja .no
    add ax, W16_STOCK
    ret
.no:
    xor ax, ax
    ret

; CreateSolidBrush (GDI.66) - args: +0 the COLORREF (red, green; blue)
w16g_solidbrush:
    call w16_args
    mov ax, [es:bx]
    mov dl, [es:bx + 2]
    call w16_rgb2col
    xor bx, bx
.find:
    cmp byte [w16_brused + bx], 0
    je .take
    inc bx
    cmp bx, W16_NBRUSH
    jb .find
    xor ax, ax
    ret
.take:
    mov byte [w16_brused + bx], 1
    mov [w16_brc + bx], al
    lea ax, [bx + W16_BRUSH0]
    ret

; DeleteObject (GDI.69) - args: +0 hObject. A stock object is never deleted.
w16g_delobj:
    call w16_args
    mov ax, [es:bx]
    mov bx, ax
    sub bx, W16_BRUSH0
    jb .stock
    cmp bx, W16_NBRUSH
    jae .no
    mov byte [w16_brused + bx], 0
    mov ax, 1
    ret
.stock:
    sub ax, W16_STOCK
    jb .no
    cmp ax, STOCK_LAST
    ja .no
    mov ax, 1
    ret
.no:
    xor ax, ax
    ret

; SelectObject (GDI.45) - args: +0 hObject, +2 hDC. Answers the one it replaced.
w16g_select:
    call w16_args
    mov ax, [es:bx]
    mov bx, w16_brush
    mov dx, ax
    sub dx, W16_BRUSH0
    jb .stock
    cmp dx, W16_NBRUSH
    jb .swap
    jmp short .no
.stock:
    mov dx, ax
    sub dx, W16_STOCK
    jb .no
    cmp dx, 5
    jbe .swap                       ; 0-5 brushes
    mov bx, w16_pen
    cmp dx, 8
    jbe .swap                       ; 6-8 pens
    mov bx, w16_font
    cmp dx, 9
    je .no
    cmp dx, STOCK_LAST
    jbe .swap                       ; 10-16 fonts (and the palette)
.no:
    xor ax, ax
    ret
.swap:
    xchg ax, [bx]
    ret

; =============================================================================
; Drawing
; =============================================================================

; MoveTo (GDI.20) - args: +0 y, +2 x, +4 hDC. Answers the old point in DX:AX.
w16g_moveto:
    call w16_args
    mov cx, [es:bx + 2]
    mov dx, [es:bx]
    mov ax, [w16_curx]
    mov [w16_curx], cx
    mov cx, [w16_cury]
    mov [w16_cury], dx
    mov dx, cx
    ret

; LineTo (GDI.19) - args: +0 y, +2 x, +4 hDC. Bresenham from the current
; point up to (not including) the new one, plotted through OSAPI_GFX_POINTS a
; buffer at a time.
w16g_lineto:
    push si
    push di
    push bp
    call w16_args
    mov ax, [es:bx + 2]
    mov [w16_lx1], ax
    mov ax, [es:bx]
    mov [w16_ly1], ax
    mov si, [w16_curx]              ; SI, DI = x, y
    mov di, [w16_cury]
    mov ax, [w16_lx1]
    mov [w16_curx], ax
    mov ax, [w16_ly1]
    mov [w16_cury], ax
    cmp byte [w16_dcon], 0
    je .done
    mov ax, [w16_pen]
    call w16_brcol
    cmp al, W16_NOCOL
    je .done
    call OSAPI_SET_COLOR
    mov word [w16_npts], 0
    mov ax, [w16_lx1]               ; dx = |x1 - x|, sx = its sign
    sub ax, si
    mov word [w16_lsx], 1
    jge .dxp
    neg ax
    mov word [w16_lsx], -1
.dxp:
    mov [w16_ldx], ax
    mov ax, [w16_ly1]               ; dy = -|y1 - y|, sy = its sign
    sub ax, di
    mov word [w16_lsy], 1
    jge .dyp
    neg ax
    mov word [w16_lsy], -1
.dyp:
    neg ax
    mov [w16_ldy], ax
    mov bp, [w16_ldx]               ; BP = err = dx + dy
    add bp, ax
.step:
    cmp si, [w16_lx1]
    jne .plot
    cmp di, [w16_ly1]
    je .flush
.plot:
    call w16_lplot
    mov ax, bp
    shl ax, 1                       ; e2 = 2 err
    cmp ax, [w16_ldy]
    jl .noy
    add bp, [w16_ldy]
    add si, [w16_lsx]
.noy:
    cmp ax, [w16_ldx]
    jg .step
    add bp, [w16_ldx]
    add di, [w16_lsy]
    jmp short .step
.flush:
    call w16_lflush
.done:
    mov ax, 1
    pop bp
    pop di
    pop si
    ret

; w16_lplot - SI, DI = a client point: into the buffer if inside the clip
w16_lplot:
    push ax
    push bx
    push dx
    mov ax, si
    add ax, [w16_ox]
    cmp ax, [w16_cl]
    jl .out
    cmp ax, [w16_cr]
    jg .out
    mov dx, di
    add dx, [w16_oy]
    cmp dx, [w16_ct]
    jl .out
    cmp dx, [w16_cb]
    jg .out
    mov bx, [w16_npts]
    shl bx, 1
    shl bx, 1
    mov [w16_pts + bx], ax
    mov [w16_pts + bx + 2], dx
    inc word [w16_npts]
    cmp word [w16_npts], W16_NPTS
    jb .out
    call w16_lflush
.out:
    pop dx
    pop bx
    pop ax
    ret

; w16_lflush - plot the buffered points. Keeps every register.
w16_lflush:
    push cx
    push si
    push es
    mov cx, [w16_npts]
    jcxz .out
    push ds
    pop es
    mov si, w16_pts
    call OSAPI_GFX_POINTS
    mov word [w16_npts], 0
.out:
    pop es
    pop si
    pop cx
    ret

; Rectangle (GDI.27) - args: +0 bottom, +2 right, +4 top, +6 left, +8 hDC.
; The pen's frame on the outside, the brush inside it, no pixel twice.
w16g_rect:
    call w16_args
    mov ax, [es:bx + 6]
    add ax, [w16_ox]
    mov [w16_rl], ax
    mov ax, [es:bx + 4]
    add ax, [w16_oy]
    mov [w16_rt], ax
    mov ax, [es:bx + 2]
    add ax, [w16_ox]
    dec ax
    mov [w16_rr], ax
    mov ax, [es:bx]
    add ax, [w16_oy]
    dec ax
    mov [w16_rb], ax
    cmp byte [w16_dcon], 0
    je .done
    mov ax, [w16_rl]
    cmp ax, [w16_rr]
    jg .done
    mov ax, [w16_rt]
    cmp ax, [w16_rb]
    jg .done
    mov ax, [w16_pen]
    call w16_brcol
    cmp al, W16_NOCOL
    je .inside
    mov [w16_gcol], al
    mov ax, [w16_rl]                ; the top edge
    mov bx, [w16_rt]
    mov cx, [w16_rr]
    mov dx, bx
    call w16_gfill
    mov bx, [w16_rb]                ; the bottom one
    cmp bx, [w16_rt]
    je .sides
    mov dx, bx
    call w16_gfill
.sides:
    mov bx, [w16_rt]                ; the two sides, between them
    inc bx
    mov dx, [w16_rb]
    dec dx
    cmp bx, dx
    jg .shrink
    mov cx, ax
    call w16_gfill
    mov ax, [w16_rr]
    cmp ax, [w16_rl]
    je .shrink
    mov cx, ax
    call w16_gfill
.shrink:
    inc word [w16_rl]               ; the brush's part is inside the frame
    inc word [w16_rt]
    dec word [w16_rr]
    dec word [w16_rb]
.inside:
    mov ax, [w16_brush]
    call w16_brcol
    cmp al, W16_NOCOL
    je .done
    mov [w16_gcol], al
    mov ax, [w16_rl]
    mov bx, [w16_rt]
    mov cx, [w16_rr]
    mov dx, [w16_rb]
    call w16_gfill
.done:
    mov ax, 1
    ret

; TextOut (GDI.33) - args: +0 nCount, +2 lpString, +6 y, +8 x, +10 hDC.
; Black on white, opaque, one font_run (SPEC.md 6.1); the clip is whole cells.

w16g_textout:
    push si
    push di
    cmp byte [w16_dcon], 0
    je .done
    call w16_args
    mov cx, [es:bx]
    cmp cx, W16_TXTMAX
    jbe .n
    mov cx, W16_TXTMAX
.n:
    mov ax, [es:bx + 8]
    add ax, [w16_ox]
    mov [w16_tx], ax
    mov ax, [es:bx + 6]
    add ax, [w16_oy]
    mov [w16_ty], ax
    les si, [es:bx + 2]
    mov di, w16_txt
    jcxz .copied
.cp:
    mov al, [es:si]
    or al, al
    jz .copied
    mov [di], al
    inc si
    inc di
    loop .cp
.copied:
    mov byte [di], 0
    mov ax, [w16_ty]                ; the row: any part inside the clip
    cmp ax, [w16_cb]
    jg .done
    add ax, 7
    cmp ax, [w16_ct]
    jl .done
    mov si, w16_txt
    mov cx, [w16_tx]
.lead:                              ; cells wholly left of the clip
    cmp byte [si], 0
    je .done
    mov ax, cx
    add ax, 7
    cmp ax, [w16_cl]
    jge .first
    add cx, 8
    inc si
    jmp short .lead
.first:
    mov ax, [w16_cr]                ; ...and those wholly right of it
    sub ax, cx
    jl .done
    shr ax, 1
    shr ax, 1
    shr ax, 1
    inc ax                          ; AX = the cells that can show
    mov di, si
.trail:
    cmp byte [di], 0
    je .draw
    inc di
    dec ax
    jnz .trail
    mov byte [di], 0
.draw:
    mov dx, [w16_ty]
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
.done:
    mov ax, 1
    pop di
    pop si
    ret
