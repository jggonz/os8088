; =============================================================================
; os8088 - apps/pixel/pxtools.inc
;
; THE TOOLS (SPEC.md 106.24): what a press on the picture does with each of
; the tool column's six - Hand (pxview.inc's pan), Zoom, Marquee, Crop,
; Eyedropper, Rotate - and THE SELECTION they make.
;
; THE SELECTION IS IN MASTER PIXELS ([px_selr], inclusive) and is shown as
; an XOR outline: four one-pixel XOR fills, each cut to the canvas, so an
; edge outside it is not drawn rather than drawn along the canvas's border.
; What is on the glass is recorded ([px_mqr], kept from the content's corner
; like every record, SPEC.md 106.15) and taken off before anything moves
; the canvas's pixels - a render, a scroll, a band moved across - and put
; back at the view's new place after (px_mqhide / px_mqshow, nested by
; [px_mqhold] so a pan's own renders do not put it back half way). A press
; outside a selection starts another; Esc, or a click that does not drag,
; deselects; the arrows nudge it a master pixel at a time (eight with
; Shift) while Marquee or Crop is the tool.
; =============================================================================

KSC_LSHIFT  equ 0x2A
KSC_RSHIFT  equ 0x36
PX_DCLK     equ 9                   ; ticks between the releases of a double
                                    ; click (half a second)
PX_TOOL_ZOOM equ 1                  ; the tools' places in the column
PX_TOOL_SEL equ 2
PX_TOOL_CROP equ 3
PX_TOOL_EYE equ 4
PX_TOOL_ROT equ 5
PX_PR_TOOL  equ 6                   ; px_press: a tool's drag on the picture

; px_shift - CF = 1 when a Shift key is down. Preserves all
px_shift:
    push ax
    mov al, KSC_LSHIFT
    call OSAPI_KEY_DOWN
    jc .y
    mov al, KSC_RSHIFT
    call OSAPI_KEY_DOWN
.y:
    pop ax
    ret

; =============================================================================
; SCREEN AND MASTER
; =============================================================================

; px_s2m - CX, DX = a screen point: CX, DX = the master pixel under it,
; clamped to the master. CF = 1 the point is off the picture (the clamped
; pixel is still returned). Preserves all else
px_s2m:
    push ax
    push bx
    push si
    xor si, si                      ; SI = off the picture
    mov ax, cx
    sub ax, [px_vcan + VW_IX]
    jge .x0
    inc si
    xor ax, ax
.x0:
    cmp ax, [px_vcan + VW_DW]
    jb .x1
    inc si
.x1:
    push dx
    mov cx, ax
    mov ax, [px_vcan + VW_HS]
    mov dx, [px_vcan + VW_HS + 2]
    call px_mulx                    ; BX:DX:AX = x hs: the master x is DX
    mov cx, dx
    or bx, bx
    jz .xc
    mov cx, 0xFFFF
.xc:
    pop dx
    cmp cx, [px_cur + PXR_MW]
    jb .y
    mov cx, [px_cur + PXR_MW]
    dec cx
.y:
    push cx
    mov ax, dx
    sub ax, [px_vcan + VW_IY]
    jge .y0
    inc si
    xor ax, ax
.y0:
    cmp ax, [px_vcan + VW_DH]
    jb .y1
    inc si
.y1:
    mov cx, ax
    mov ax, [px_vcan + VW_VS]
    mov dx, [px_vcan + VW_VS + 2]
    call px_mulx
    or bx, bx
    jz .yc
    mov dx, 0xFFFF
.yc:
    cmp dx, [px_cur + PXR_MH]
    jb .yo
    mov dx, [px_cur + PXR_MH]
    dec dx
.yo:
    pop cx
    cmp si, 1                       ; CF = 1 when SI is 0? no: off when > 0
    cmc
    pop si
    pop bx
    pop ax
    ret

; px_m2sx - AX = a master column count: AX = the screen columns before the
; first that shows it, ceil(AX 2^16 / hs), at most the picture's width.
; Preserves all but AX (px_m2s is the rows')
px_m2sx:
    push bx
    push cx
    push dx
    mov bx, [px_vcan + VW_HS]
    mov cx, [px_vcan + VW_HS + 2]
    mov dx, ax
    xor ax, ax
    call px_cdiv32
    or dx, dx
    jnz .w
    cmp ax, [px_vcan + VW_DW]
    jbe .o
.w:
    mov ax, [px_vcan + VW_DW]
.o:
    pop dx
    pop cx
    pop bx
    ret

; px_selscr - the selection's screen rect: AX/BX/CX/DX. Preserves all else
px_selscr:
    push si
    mov ax, [px_selr + 4]
    inc ax
    call px_m2sx
    mov cx, ax
    mov ax, [px_selr + 6]
    inc ax
    call px_m2s
    mov dx, ax
    mov ax, [px_selr]
    call px_m2sx
    mov si, ax
    mov ax, [px_selr + 2]
    call px_m2s
    mov bx, ax
    mov ax, si
    dec cx                          ; the last screen pixel of the last
    dec dx                          ; master pixel, and at least the first
    cmp cx, ax
    jge .c
    mov cx, ax
.c:
    cmp dx, bx
    jge .d
    mov dx, bx
.d:
    add ax, [px_vcan + VW_IX]
    add cx, [px_vcan + VW_IX]
    add bx, [px_vcan + VW_IY]
    add dx, [px_vcan + VW_IY]
    pop si
    ret

; =============================================================================
; THE OUTLINE
; =============================================================================

; px_xfill - AX/BX/CX/DX: XOR-filled where it meets the canvas. Preserves all
px_xfill:
    push ax
    push bx
    push cx
    push dx
    cmp ax, [px_cvx1]
    jge .a
    mov ax, [px_cvx1]
.a:
    cmp cx, [px_cvx2]
    jle .c
    mov cx, [px_cvx2]
.c:
    cmp bx, [px_midy1]
    jge .b
    mov bx, [px_midy1]
.b:
    cmp dx, [px_midy2]
    jle .d
    mov dx, [px_midy2]
.d:
    cmp ax, cx
    jg .x
    cmp bx, dx
    jg .x
    call OSAPI_GFX_XOR_FILL
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_mqxor - the outline of AX/BX/CX/DX, XOR, cut to the canvas: the top
; and bottom rows whole, the sides between them, so no pixel is turned
; twice. Preserves all
px_mqxor:
    push dx
    push bx
    mov dx, bx
    call px_xfill                   ; the top
    pop bx
    pop dx
    cmp dx, bx
    je .x
    push bx
    mov bx, dx
    call px_xfill                   ; the bottom
    pop bx
    push dx
    push bx
    inc bx
    dec dx
    cmp bx, dx
    jg .s
    push cx
    mov cx, ax
    call px_xfill                   ; the left
    pop cx
    cmp cx, ax
    je .s
    push ax
    mov ax, cx
    call px_xfill                   ; the right
    pop ax
.s:
    pop bx
    pop dx
.x:
    ret

; px_mqshow - the selection's outline on the glass where the view puts it,
; and recorded. Lock held, layout done. Preserves all
px_mqshow:
    cmp byte [px_mqhold], 0
    jne .r
    cmp byte [px_mqon], 0
    jne .r
    cmp byte [px_sel], 0
    je .r
    cmp byte [px_tdrag], 1          ; (a zoom's band owns the record)
    je .r
    call px_haspic
    jc .r
    push ax
    push bx
    push cx
    push dx
    call px_selscr
    call px_mqput
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; px_mqput - AX/BX/CX/DX (screen) XORed on and recorded. Preserves all
px_mqput:
    push ax
    push bx
    push cx
    push dx
    call px_mqxor
    sub ax, [px_cx0]
    sub cx, [px_cx0]
    sub bx, [px_cy0]
    sub dx, [px_cy0]
    mov [px_mqr], ax
    mov [px_mqr + 2], bx
    mov [px_mqr + 4], cx
    mov [px_mqr + 6], dx
    mov byte [px_mqon], 1
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_mqhide - the outline taken back off the glass (XOR again), where the
; record says it is. Preserves all
px_mqhide:
    cmp byte [px_mqon], 0
    je .r
    push ax
    push bx
    push cx
    push dx
    mov ax, [px_mqr]
    mov bx, [px_mqr + 2]
    mov cx, [px_mqr + 4]
    mov dx, [px_mqr + 6]
    add ax, [px_cx0]
    add cx, [px_cx0]
    add bx, [px_cy0]
    add dx, [px_cy0]
    call px_mqxor
    mov byte [px_mqon], 0
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; px_mqhidec / px_mqshowc - the same from a callback with no clip armed:
; the window laid out and its clip armed for them. Preserve all
px_mqhidec:
    cmp byte [px_mqon], 0
    je .r
    push bx
    call px_layout
    jc .x
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET
    jc .x
    call px_mqhide
    call OSAPI_WM_CLIP_CLEAR
.x:
    mov byte [px_mqon], 0           ; (not visible: nothing to take back)
    pop bx
.r:
    ret
px_mqshowc:
    push bx
    call px_layout
    jc .x
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET
    jc .x
    call px_mqshow
    call OSAPI_WM_CLIP_CLEAR
.x:
    pop bx
    ret

; px_seloff - no selection, its outline off the glass. Preserves all
px_seloff:
    call px_mqhidec
    mov byte [px_sel], 0
    ret

; px_eseltrim - the selection kept inside the master (an operation changed
; its size: px_efin drops it then; a palette changes nothing). Preserves all
px_eseltrim:
    cmp byte [px_sel], 0
    je .r
    push ax
    mov ax, [px_cur + PXR_MW]
    cmp [px_selr + 4], ax
    jae .off
    mov ax, [px_cur + PXR_MH]
    cmp [px_selr + 6], ax
    jb .ok
.off:
    mov byte [px_sel], 0
.ok:
    pop ax
.r:
    ret

; px_selall - Edit > Select All (Ctrl+A). Lock held
px_selall:
    cmp byte [px_cur + PXR_HAVE], 0
    je .r
    push ax
    call px_mqhidec
    xor ax, ax
    mov [px_selr], ax
    mov [px_selr + 2], ax
    mov ax, [px_cur + PXR_MW]
    dec ax
    mov [px_selr + 4], ax
    mov ax, [px_cur + PXR_MH]
    dec ax
    mov [px_selr + 6], ax
    mov byte [px_sel], 1
    call px_selnew
    pop ax
.r:
    ret

; px_selnew - the selection changed: its outline where it is now, Edit's
; items, a key's rect for a pin. Lock held. Preserves all
px_selnew:
    push ax
    push bx
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET
    jc .x
    call px_mqshow
    call OSAPI_WM_CLIP_CLEAR
.x:
    call px_menulive
    pop bx
    pop ax
    ret

; px_crop - Edit > Crop to Selection, the Crop tool's Enter or double click
px_crop:
    cmp byte [px_sel], 0
    je .r
    push ax
    push si
    push di
    push cx
    push es
    push ds
    pop es
    mov si, px_selr
    mov di, px_ep
    mov cx, 4
    cld
    rep movsw
    pop es
    pop cx
    pop di
    pop si
    mov al, EV_CROP
    call px_edo
    pop ax
.r:
    ret

; px_selnudge - AX, BX = a move in master pixels: the selection moved,
; kept inside the master. Lock held. Preserves all
px_selnudge:
    push ax
    push bx
    push cx
    push dx
    call px_shift
    jnc .n
    mov cl, 3
    shl ax, cl
    shl bx, cl
.n:
    mov cx, [px_selr]               ; x1 + dx, held to 0..mw - width
    add cx, ax
    jns .x1
    sub ax, cx
    xor cx, cx
.x1:
    mov dx, [px_selr + 4]
    add dx, ax
    cmp dx, [px_cur + PXR_MW]
    jb .x2
    sub dx, [px_cur + PXR_MW]
    inc dx
    sub cx, dx
    mov dx, [px_cur + PXR_MW]
    dec dx
.x2:
    mov [px_selr], cx
    mov [px_selr + 4], dx
    mov cx, [px_selr + 2]
    add cx, bx
    jns .y1
    sub bx, cx
    xor cx, cx
.y1:
    mov dx, [px_selr + 6]
    add dx, bx
    cmp dx, [px_cur + PXR_MH]
    jb .y2
    sub dx, [px_cur + PXR_MH]
    inc dx
    sub cx, dx
    mov dx, [px_cur + PXR_MH]
    dec dx
.y2:
    mov [px_selr + 2], cx
    mov [px_selr + 6], dx
    call px_layout
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET
    jc .x
    call px_mqhide
    call px_mqshow
    call OSAPI_WM_CLIP_CLEAR
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE GESTURES
; =============================================================================

; px_tpress - a left press on the picture with a tool other than the Hand:
; CX, DX. AL = PX_PR_TOOL when it is a drag that follows, else PX_PR_NONE.
; Lock held, layout done
px_tpress:
    mov [px_tx0], cx
    mov [px_ty0], dx
    mov al, [px_tool]
    cmp al, PX_TOOL_ZOOM
    je .zoom
    cmp al, PX_TOOL_SEL
    je .sel
    cmp al, PX_TOOL_CROP
    je .sel
    cmp al, PX_TOOL_EYE
    je .eye
    mov al, PX_PR_TOOL              ; Rotate: on the release
    ret
.zoom:
    call px_mqhidec                 ; the band takes the record
    mov byte [px_tdrag], 1
    mov al, PX_PR_TOOL
    ret
.eye:
    call px_eyeread
    mov al, PX_PR_TOOL
    ret
.sel:
    push cx                         ; a press inside the selection, the
    push dx                         ; Crop tool's, may be a double click's
    call px_s2m                     ; second: the release decides
    mov [px_tmx], cx
    mov [px_tmy], dx
    pop dx
    pop cx
    call px_mqhidec
    mov byte [px_tdrag], 2
    mov byte [px_tmoved], 0
    mov al, PX_PR_TOOL
    ret

; px_tdragto - CX, DX = the pointer during a tool's drag (a tick at most):
; the band, the selection, or the readout follows it. Lock held, layout done
px_tdragto:
    push ax
    push bx
    push cx
    push dx
    mov al, [px_tool]
    cmp al, PX_TOOL_EYE
    jne .z
    call px_eyeread
    mov al, PX_R_SDIRTY             ; (the fields it changed, drawn)
    call px_regdraw
    jmp .x0
.z:
    push bx
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET
    pop bx
    jc .x0
    cmp byte [px_tdrag], 1
    jne .s
    call px_mqhide                  ; THE BAND: screen pixels, cut to the
    mov ax, [px_tx0]                ; canvas, normalised
    mov bx, [px_ty0]
    call px_rnorm
    call px_mqput
    jmp short .x
.s:
    cmp byte [px_tdrag], 2
    jne .x
    call px_s2m                     ; THE SELECTION: master pixels from the
    mov ax, [px_tmx]                ; anchor to here
    mov bx, [px_tmy]
    cmp cx, ax
    jne .mv
    cmp dx, bx
    je .x
.mv:
    mov byte [px_tmoved], 1
    call px_rnorm
    call px_mqhide
    mov [px_selr], ax
    mov [px_selr + 2], bx
    mov [px_selr + 4], cx
    mov [px_selr + 6], dx
    mov byte [px_sel], 1
    call px_mqshow
.x:
    call OSAPI_WM_CLIP_CLEAR
.x0:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_rnorm - AX/BX and CX/DX two corners: AX/BX the top-left, CX/DX the
; bottom-right. Preserves all else
px_rnorm:
    cmp ax, cx
    jle .a
    xchg ax, cx
.a:
    cmp bx, dx
    jle .b
    xchg bx, dx
.b:
    ret

; px_trelease - the left button up after a tool's press: CX, DX. Lock held,
; layout done
px_trelease:
    push ax
    push bx
    push cx
    push dx
    call px_tdragto                 ; the last of the drag
    mov al, [px_tool]
    cmp al, PX_TOOL_ROT
    je .rot
    cmp al, PX_TOOL_EYE
    je .eye
    cmp al, PX_TOOL_ZOOM
    je .zoom
    ; MARQUEE AND CROP: a drag made the selection; a click did not
    mov byte [px_tdrag], 0
    cmp byte [px_tmoved], 0
    jne .menus
    call OSAPI_GET_TICKS            ; a click: the second of a double click
    mov bx, ax                      ; inside the selection crops (Crop)
    sub ax, [px_dclk]
    mov [px_dclk], bx
    cmp ax, PX_DCLK
    ja .desel
    cmp byte [px_tool], PX_TOOL_CROP
    jne .desel
    cmp byte [px_sel], 0
    je .desel
    call px_crop
    jmp short .x
.desel:
    call px_seloff
.menus:
    call px_mqshowc
    call px_menulive
    jmp short .x
.eye:
    call px_eyepin                  ; the pixel under it, pinned in Info
    jmp short .x
.rot:
    mov al, EV_ROTCW                ; Rotate: a click turns it, Shift the
    call px_shift                   ; other way
    jnc .r1
    mov al, EV_ROTCCW
.r1:
    call px_edo
    jmp short .x
.zoom:
    mov byte [px_tdrag], 0
    call px_mqhidec                 ; the band off
    mov ax, cx                      ; a click (moved four pixels or less)
    sub ax, [px_tx0]                ; zooms a step about it; a drag zooms
    jns .ax                         ; to the rect
    neg ax
.ax:
    mov bx, dx
    sub bx, [px_ty0]
    jns .ay
    neg bx
.ay:
    cmp ax, 4
    ja .rect
    cmp bx, 4
    ja .rect
    mov al, 1
    call px_shift
    jnc .zs
    xor al, al
.zs:
    call px_zat
    jmp short .x
.rect:
    mov ax, [px_tx0]
    mov bx, [px_ty0]
    call px_rnorm
    call px_zrect
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_trclick - W_ONRCLICK: the right button on the picture - the Zoom tool
; out a step about the point, the Rotate tool the other way. CX, DX, SI
px_trclick:
    mov [px_win], si
    call px_acted
    jc .r
    call px_carddown
    jc .r
    call px_layout
    jc .r
    cmp cx, [px_cvx1]
    jl .r
    cmp cx, [px_cvx2]
    jg .r
    cmp dx, [px_midy1]
    jl .r
    cmp dx, [px_midy2]
    jg .r
    cmp byte [px_cur + PXR_HAVE], 0
    je .r
    push ax
    cmp byte [px_tool], PX_TOOL_ZOOM
    jne .rot
    xor al, al
    call px_zat
    jmp short .x
.rot:
    cmp byte [px_tool], PX_TOOL_ROT
    jne .x
    mov al, EV_ROTCCW
    call px_edo
.x:
    pop ax
.r:
    ret

; px_zat - AL = 1 in, 0 out: a zoom step, and the master pixel at CX, DX
; brought to the canvas's centre. Lock held. Preserves all
px_zat:
    push ax
    push bx
    push cx
    push dx
    push ax
    call px_s2m                     ; CX, DX = the master pixel
    mov [px_tmx], cx
    mov [px_tmy], dx
    pop ax
    call px_mqhidec
    inc byte [px_mqhold]
    call px_zstep
    call px_zcentre
    dec byte [px_mqhold]
    call px_mqshowc
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_zcentre - the master pixel [px_tmx], [px_tmy] to the canvas's centre
; (px_panto clamps). Lock held. Preserves all
px_zcentre:
    push ax
    push bx
    mov ax, [px_tmx]
    call px_m2sx
    neg ax
    mov bx, [px_cw]
    shr bx, 1
    add ax, bx
    push ax
    mov ax, [px_tmy]
    call px_m2s
    neg ax
    mov bx, [px_ch]
    shr bx, 1
    add bx, ax
    pop ax
    call px_panto
    pop bx
    pop ax
    ret

; px_zrect - AX/BX/CX/DX a screen rect: the zoom that shows that part of
; the picture filling the canvas, and it centred. Lock held. Preserves all
px_zrect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push cx
    push dx
    mov cx, ax                      ; the corners as master pixels
    mov dx, bx
    call px_s2m
    mov si, cx
    mov di, dx
    pop dx
    pop cx
    call px_s2m                     ; SI/DI .. CX/DX
    sub cx, si
    inc cx                          ; CX = its master columns
    sub dx, di
    inc dx                          ; DX = its rows
    mov ax, cx                      ; its centre, for after
    shr ax, 1
    add ax, si
    mov [px_tmx], ax
    mov ax, dx
    shr ax, 1
    add ax, di
    mov [px_tmy], ax
    ; Z = min(cw / (cols << s), ch ad / (rows << s) an), 16.16 per SOURCE
    ; pixel: the master's columns are 2^s of them
    push dx
    mov ax, [px_cw]
    mov si, cx
    call px_zfit1                   ; DX:AX = cw 2^16 / (cols << s)
    pop cx
    push dx
    push ax
    mov bl, [px_rkind]              ; ...the rows, through the aspect
    xor bh, bh
    shl bx, 1
    mov ax, [px_ch]
    mul word [px_aspd + bx]
    div word [px_aspn + bx]         ; ch ad / an (the screen rows, as
    mov si, cx                      ; columns of the same width)
    call px_zfit1
    pop bx
    pop cx                          ; CX:BX = the columns' zoom
    cmp dx, cx
    jb .m
    ja .o
    cmp ax, bx
    jbe .m
.o:
    mov ax, bx
    mov dx, cx
.m:
    call px_mqhidec
    inc byte [px_mqhold]
    call px_zoomto
    call px_zcentre
    dec byte [px_mqhold]
    call px_mqshowc
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_zfit1 - AX = screen pixels, SI = master pixels: DX:AX = AX 2^16 /
; (SI << s), at least 1. Preserves all else
px_zfit1:
    push bx
    push cx
    mov dx, ax                      ; DX:AX = screen << 16
    xor ax, ax
    mov bx, si
    xor cx, cx
    push ax
    mov al, [px_cur + PXR_SCL]
    or al, al
    jz .s
.sh:
    shl bx, 1
    rcl cx, 1
    dec al
    jnz .sh
.s:
    pop ax
    call px_div32
    mov bx, ax
    or bx, dx
    jnz .o
    inc ax
.o:
    pop cx
    pop bx
    ret

; =============================================================================
; THE EYEDROPPER: x,y in the size field, #RRGGBB in the zoom's, the index in
; the format's - live under the pointer (a press and drag, and the timer's
; look at the pointer while it is the tool), pinned in Image Info by a click
; =============================================================================

; px_eyeread - CX, DX = the pointer: the readout, or the fields' own values
; back when it is off the picture. Preserves all
px_eyeread:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call px_haspic
    jc .back
    cmp byte [px_cur + PXR_HAVE], 0
    je .back
    cmp cx, [px_cvx1]
    jl .back
    cmp cx, [px_cvx2]
    jg .back
    cmp dx, [px_midy1]
    jl .back
    cmp dx, [px_midy2]
    jg .back
    call px_s2m
    jc .back
    mov [px_eyex], cx
    mov [px_eyey], dx
    mov byte [px_eyeon], 1
    mov di, px_cline                ; "123,45"
    call px_xy
    mov bx, PX_SF_DIMS
    mov si, px_cline
    call px_sval
    call px_eyeidx                  ; AL = the index, SI = its colour
    push ax
    mov di, px_cline                ; "#A0B0C0"
    mov byte [di], '#'
    inc di
    mov cx, 3
.h:
    mov al, [si]
    call px_hex2
    inc si
    loop .h
    mov byte [di], 0
    mov bx, PX_SF_ZOOM
    mov si, px_cline
    call px_sval
    pop ax
    xor ah, ah                      ; the index
    mov di, px_cline
    call px_dec
    mov bx, PX_SF_FMT
    mov si, px_cline
    call px_sval
    jmp short .x
.back:
    cmp byte [px_eyeon], 0
    je .x
    mov byte [px_eyeon], 0
    call px_compose                 ; the fields' own values again
    call px_zfield
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_eyeidx - AL = the index at [px_eyex], [px_eyey]; SI = its palette
; entry. Preserves all but AX, SI
px_eyeidx:
    push bx
    push cx
    push dx
    push es
    mov ax, [px_eyey]
    mul word [px_cur + PXR_MW]
    add ax, [px_eyex]
    adc dx, 0
    mov bx, ax
    and bx, 15
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, [px_cur + PXR_MSEG]
    mov es, ax
    mov al, [es:bx]
    xor ah, ah
    mov si, ax
    shl si, 1
    add si, ax
    add si, px_pal
    pop es
    pop dx
    pop cx
    pop bx
    ret

; px_xy - "x,y" of the readout at DI, DI left on its NUL. Preserves all
; but DI
px_xy:
    push ax
    mov ax, [px_eyex]
    call px_dec
    mov byte [di], ','
    inc di
    mov ax, [px_eyey]
    call px_dec
    pop ax
    ret

; px_dec - AX in decimal at DI, no commas (a coordinate is not a count),
; NUL after; DI left on the NUL. Preserves all but DI
px_dec:
    push ax
    push bx
    push dx
    push si
    mov si, px_digs + 10            ; backwards into px_u32's buffer (a
    mov bx, 10                      ; loop that pushes is one stkbalance
.d:                                 ; cannot follow)
    xor dx, dx
    div bx
    add dl, '0'
    dec si
    mov [si], dl
    or ax, ax
    jnz .d
.p:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    cmp si, px_digs + 10
    jb .p
    mov byte [di], 0
    pop si
    pop dx
    pop bx
    pop ax
    ret

; px_hex2 - AL as two hex digits at DI. Preserves all but DI (advanced)
px_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 15
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .s
    add al, 'A' - '9' - 1
.s:
    mov [di], al
    inc di
    ret

; px_eyepin - the pixel under the last readout pinned in Image Info's last
; line: "123,45 #A0B0C0 17". Lock held. Preserves all
px_eyepin:
    cmp byte [px_eyeon], 0
    je .r
    push ax
    push bx
    push dx
    push si
    push di
    mov di, px_cline
    call px_xy
    mov byte [di], ' '
    inc di
    call px_eyeidx
    push ax
    mov byte [di], '#'
    inc di
    mov al, [si]
    call px_hex2
    mov al, [si + 1]
    call px_hex2
    mov al, [si + 2]
    call px_hex2
    mov byte [di], ' '
    inc di
    pop ax
    xor ah, ah
    call px_dec
    mov bx, PX_IPICK
    mov si, px_cline
    call px_ivset
    call px_update
    pop di
    pop si
    pop dx
    pop bx
    pop ax
.r:
    ret

; px_eyetick - the timer's look at the pointer while the Eyedropper is the
; tool and our window is in front (nothing covers the canvas, so the point
; is ours). Lock held. Preserves all
px_eyetick:
    cmp byte [px_tool], PX_TOOL_EYE
    jne .r
    push ax
    push bx
    push cx
    push dx
    call OSAPI_WM_TOP
    cmp bx, [px_win]
    jne .x
    call px_layout
    jc .x
    call OSAPI_MOUSE                ; CX, DX
    call px_eyeread
.x:
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; px_edkey - AL/AH = a key: editing's (SPEC.md 106.24) - Ctrl+S Save As,
; Ctrl+Z Undo, Ctrl+C Copy, Ctrl+A Select All; Enter crops with the Crop
; tool and a selection; the arrows nudge the selection while Marquee or Crop
; is the tool. CF = 0 it was one. Lock held. Preserves all
px_edkey:
    push ax
    push bx
    cmp al, 0x13                    ; Ctrl+S
    jne .z
    call px_saveas
    jmp short .y
.z:
    cmp al, 0x1A                    ; Ctrl+Z
    jne .c
    cmp byte [px_ukind], UK_NONE
    je .y
    call px_eundo
    jmp short .y
.c:
    cmp al, 0x03                    ; Ctrl+C
    jne .a
    call px_copy
    jmp short .y
.a:
    cmp al, 0x01                    ; Ctrl+A
    jne .e
    call px_selall
    jmp short .y
.e:
    cmp byte [px_sel], 0            ; the selection's keys
    je .no
    mov bl, [px_tool]
    cmp bl, PX_TOOL_SEL
    je .k
    cmp bl, PX_TOOL_CROP
    jne .no
    cmp ah, KSC_ENTER
    jne .k
    call px_crop
    jmp short .y
.k:
    push dx
    mov dl, ah
    xor ax, ax
    xor bx, bx
    cmp dl, KSC_LEFT
    jne .r
    dec ax
    jmp short .n
.r:
    cmp dl, KSC_RIGHT
    jne .u
    inc ax
    jmp short .n
.u:
    cmp dl, KSC_UP
    jne .d
    dec bx
    jmp short .n
.d:
    cmp dl, KSC_DOWN
    jne .nd
    inc bx
.n:
    pop dx
    call px_selnudge
    jmp short .y
.nd:
    pop dx
    jmp short .no
.y:
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret

; px_eyeoff - the readout's fields their own values again (the tool is
; left). Preserves all
px_eyeoff:
    cmp byte [px_eyeon], 0
    je .r
    mov byte [px_eyeon], 0
    call px_compose
    call px_zfield
.r:
    ret
