; =============================================================================
; os8088 - apps/win16/w16gdi.inc
;
; GDI (SPEC.md 124.8.1, docs/plans/WIN16-PLAN.md S3 and S4): one device context
; on the program's window, the stock objects, solid brushes and pens, SetROP2,
; the drawing calls WinMine and CALC make - MoveTo/LineTo, Rectangle,
; RoundRect, SetPixel, FillRect - text in the System font Windows showed on
; this display (SPEC.md 124.7), SetDIBitsToDevice, and what GetDeviceCaps
; answered there. A colour is the nearest of Windows' sixteen, which are
; os8088's sixteen (CBLACK..CWHITE) in another order.
;
; THE CLIP IS TWO CLIPS. The kernel's (OSAPI_WM_CLIP_SET, or the damage inside
; W_PAINT) keeps every pixel inside what is visible of the content; WIN16's
; own, [w16_cl]..[w16_cb] in screen coordinates, keeps it inside rcPaint, so a
; WM_PAINT for a counter's strip repaints the strip and not the window.
;
; PRICED IN CALLS (PERFORMANCE.md): a string is ONE OSAPI_GFX_BLIT1 however
; many glyphs it has, a RoundRect is a fill for each run of equal rows in its
; pen's two edges and its brush (17 for a CALC key), and a DIB is one
; OSAPI_GFX_BLIT4 per W16_GBUF of pixels - one for any of WinMine's cells.
; =============================================================================

W16_HDC     equ 0x3002          ; the one DC
W16_STOCK   equ 0x3100          ; GetStockObject(i) = W16_STOCK + i
W16_OBJ0    equ 0x3200          ; a made brush or pen, W16_OBJ0 + slot
W16_NOCOL   equ 0xFF            ; a NULL brush or pen, or R2_NOP: draw nothing
W16_XOR     equ 0xFE            ; ...or invert what is there (w16_ropcol)

W16K_BRUSH  equ 1               ; [w16_objk]
W16K_PEN    equ 2

WHITE_BRUSH equ 0
BLACK_PEN   equ 7
SYSTEM_FONT equ 13
STOCK_LAST  equ 16
PS_NULL     equ 5

R2_BLACK      equ 1
R2_NOTCOPYPEN equ 4
R2_NOT        equ 6
R2_XORPEN     equ 7
R2_NOTXORPEN  equ 10
R2_NOP        equ 11
R2_COPYPEN    equ 13
R2_WHITE      equ 16

VID_KIND_VGA  equ 0            ; OSAPI_WM_DISPLAY's DL
VID_KIND_HERC equ 1
VID_KIND_CGA  equ 2
VID_KIND_EGA  equ 3
VID_KIND_VESA equ 4

; --- the face part (SPEC.md 124.7.1, tools/w16font.py) -------------------------
W16F_ROWS   equ 0
W16F_ASC    equ 1
W16F_AVG    equ 2
W16F_MAX    equ 3
W16F_IL     equ 4
W16F_EL     equ 5
W16F_FIRST  equ 6
W16F_COUNT  equ 7
W16F_ADV    equ 8
W16F_OFF    equ 104             ; 8 + 95 advances, to even
W16F_DEFCH  equ '?'             ; what a character the face has not is drawn as

; -----------------------------------------------------------------------------
; w16_dcopen - open the DC on a window. AL = 1 BeginPaint's (the clip is the
; invalid rectangle, and [w16_prc] its client-coordinate rcPaint) / 0 the
; whole client. BX = the window: any of the tree (SPEC.md 124.8), its client
; found by w16_origin and cut by its ancestors' (w16_wclip). Takes the lock
; and arms the kernel's clip on its top-level's os8088 window unless the
; resuming callback holds both - inside W_PAINT the damage is that clip, and
; a window in another os8088 window than the one painting has no pixel to
; draw. Keeps every register.
; -----------------------------------------------------------------------------
w16_dcopen:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [w16_dcwnd], bx
    mov [w16_dcpaint], al
    cmp byte [w16_dcon], 0
    jne .clip                       ; open already: a new clip, same DC
    mov byte [w16_dcon], 1
    mov word [w16_pen], W16_STOCK + BLACK_PEN
    mov word [w16_brush], W16_STOCK + WHITE_BRUSH
    mov word [w16_font], W16_STOCK + SYSTEM_FONT
    mov byte [w16_rop], R2_COPYPEN
    xor ax, ax
    mov [w16_tcol], al              ; black text, its COLORREF 0
    mov byte [w16_tpap], CWHITE     ; ...on white paper (SPEC.md 124.13)
    mov [w16_trgb], ax
    mov [w16_trgb + 2], ax
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
    push bx
    call w16_toplevel
    mov bx, [bx + W16W_OSW]         ; BX = the os8088 window it is in
    or bx, bx
    jz .hide
    cmp byte [w16_inpaint], 0       ; inside W_PAINT the damage is the clip
    je .arm
    cmp bx, [w16_pfosw]
    je .armed0
.hide:
    mov byte [w16_dchide], 1        ; not a pixel visible
    jmp short .armed0
.arm:
    call OSAPI_WM_CLIP_SET
    jc .hide
    mov byte [w16_dcclip], 1
.armed0:
    pop bx
.clip:
    mov bx, [w16_dcwnd]
    call w16_origin                 ; AX/DX = the client's origin, CX/SI size
    jc .empty
    mov [w16_ox], ax
    mov [w16_oy], dx
    mov dx, si
    or dx, dx
    jle .empty
    or cx, cx
    jle .empty
    cmp byte [w16_dchide], 0
    jne .empty
    call w16_wclip                  ; [w16_kl]..[w16_kb]: cut by its ancestors
    jc .empty
    xor ax, ax
    mov [w16_prc], ax
    mov [w16_prc + 2], ax
    mov [w16_prc + 4], cx
    mov [w16_prc + 6], dx
    cmp byte [w16_dcpaint], 0
    je .screen
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
    mov ax, [w16_prc]               ; ...and in screen coordinates, inclusive,
    add ax, [w16_ox]                ;   cut by the ancestors
    cmp ax, [w16_kl]
    jge .sl
    mov ax, [w16_kl]
.sl:
    mov [w16_cl], ax
    mov ax, [w16_prc + 2]
    add ax, [w16_oy]
    cmp ax, [w16_kt]
    jge .st
    mov ax, [w16_kt]
.st:
    mov [w16_ct], ax
    mov ax, [w16_prc + 4]
    add ax, [w16_ox]
    dec ax
    cmp ax, [w16_kr]
    jle .sr
    mov ax, [w16_kr]
.sr:
    mov [w16_cr], ax
    mov ax, [w16_prc + 6]
    add ax, [w16_oy]
    dec ax
    cmp ax, [w16_kb]
    jle .sb
    mov ax, [w16_kb]
.sb:
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
    pop si
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

; -----------------------------------------------------------------------------
; w16_ink - AL = a colour: make it the kernel's ink. On a 1bpp display it is
; first put in one of the three classes Windows' own mono drivers drew it in
; (w16_monotab); the kernel then draws black, its 50% dither, or white
; (SPEC.md 39.4). Keeps every register.
; -----------------------------------------------------------------------------
w16_ink:
    push ax
    push bx
    cmp byte [w16_mono], 0
    je .set
    mov bx, w16_monotab
    and al, 15
    xlat
.set:
    call OSAPI_SET_COLOR
    pop bx
    pop ax
    ret

; Each colour's class on a 1bpp display, by its luminance (0.30 R + 0.59 G +
; 0.11 B) as Windows' mono drivers judged it: under 64 black, 64-207 the
; kernel's 50% dither (CLGRAY's class), 208 and over white. The kernel's own
; map (SPEC.md 39.4) is os8088's UI's and turns bright red WHITE; a Windows
; program's red is dark, so it is reduced here first.
w16_monotab:
    ;  black   navy    green   teal    maroon  purple  olive   lgray
    db CBLACK, CBLACK, CLGRAY, CLGRAY, CBLACK, CBLACK, CLGRAY, CLGRAY
    ;  dgray   blue    lime    cyan    red     magenta yellow  white
    db CLGRAY, CBLACK, CLGRAY, CLGRAY, CLGRAY, CLGRAY, CWHITE, CWHITE

; ...and TEXT, which is lit or not (a dithered glyph loses half its strokes,
; the kernel's rule too): lit from luminance 160 - light grey, cyan, yellow
; and white.
w16_monolit:
    db 0, 0, 0, 0, 0, 0, 0, 1,  0, 0, 0, 1, 0, 0, 1, 1

; w16_gfill - fill AX/BX/CX/DX (screen, inclusive) in [w16_gcol] - a colour,
; or W16_XOR to invert - cut to the DC's clip. Keeps every register.
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
    cmp byte [w16_gcol], W16_XOR
    je .xor
    push ax
    mov al, [w16_gcol]
    call w16_ink
    pop ax
    call OSAPI_GFX_FILL
    jmp short .out
.xor:
    call OSAPI_GFX_XOR_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; -----------------------------------------------------------------------------
; w16_span - one row of a shape: AX = x1, CX = x2, DX = y (screen, inclusive),
; in [w16_gcol], the rows of one layer coming top to bottom. A row the same
; as the one above it grows that rectangle instead of starting one, so a
; RoundRect's straight edges and the runs of equal rows in its corners are a
; fill each (w16_spflush). OSAPI_GFX_SPANS would be one call for the lot,
; but it refuses under a clip region (SPEC.md 5.10), and a DC always has
; one. x1 > x2 is an empty row: it ends the rectangle. Keeps every register.
; -----------------------------------------------------------------------------
w16_span:
    cmp ax, cx
    jg w16_spflush
    cmp byte [w16_son], 0
    je .new
    cmp ax, [w16_sx1]
    jne .flush
    cmp cx, [w16_sx2]
    jne .flush
    push dx
    dec dx
    cmp dx, [w16_sy2]
    pop dx
    jne .flush
    mov [w16_sy2], dx               ; the same span, the next row: grow
    ret
.flush:
    call w16_spflush
.new:
    mov [w16_sx1], ax
    mov [w16_sx2], cx
    mov [w16_sy1], dx
    mov [w16_sy2], dx
    mov byte [w16_son], 1
    ret

; w16_spflush - draw the rectangle w16_span is growing. Keeps every register.
w16_spflush:
    cmp byte [w16_son], 0
    je .ret
    mov byte [w16_son], 0
    push ax
    push bx
    push cx
    push dx
    mov ax, [w16_sx1]
    mov bx, [w16_sy1]
    mov cx, [w16_sx2]
    mov dx, [w16_sy2]
    call w16_gfill
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; -----------------------------------------------------------------------------
; w16_brcol - AX = a brush or pen handle -> AL = its colour, or W16_NOCOL.
; A small integer is a class's COLOR_* + 1 brush. Keeps all but AL.
; -----------------------------------------------------------------------------
w16_brcol:
    push bx
    cmp ax, 0x100
    jae .obj
    mov bl, W16_NOCOL
    or ax, ax
    jz .have
    dec ax
    cmp ax, W16_NSYSCOL
    jae .have
    call w16_syscol
    mov bl, al
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
    sub bx, W16_OBJ0
    jb .none
    cmp bx, W16_NOBJ
    jae .none
    cmp byte [w16_objk + bx], 0
    je .none
    mov bl, [w16_objc + bx]
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

; w16_ropcol - AL = a pen's or brush's colour -> AL as [w16_rop] draws it: a
; colour, W16_XOR or W16_NOCOL. The planes' bit operations are on the
; hardware's IRGB index, which os8088's colour numbers are, so R2_NOT is the
; kernel's XOR fill and R2_NOTCOPYPEN is the index complemented. A pen R2_XORPEN
; or R2_NOTXORPEN would leave alone draws nothing; any other colour under
; them inverts, which is exact for the white and black pens programs use them
; with (CALC's pressed key) and the nearest the kernel has for the rest.
; Keeps all but AL.
w16_ropcol:
    cmp al, W16_NOCOL
    je .ret
    push bx
    mov bl, [w16_rop]
    cmp bl, R2_COPYPEN
    je .out
    cmp bl, R2_WHITE
    jne .nw
    mov al, CWHITE
    jmp short .out
.nw:
    cmp bl, R2_BLACK
    jne .nb
    mov al, CBLACK
    jmp short .out
.nb:
    cmp bl, R2_NOTCOPYPEN
    jne .nn
    xor al, 15
    jmp short .out
.nn:
    cmp bl, R2_NOP
    je .nop
    cmp bl, R2_NOT
    je .xor
    cmp bl, R2_XORPEN
    jne .nx
    cmp al, CBLACK
    je .nop
    jmp short .xor
.nx:
    cmp bl, R2_NOTXORPEN
    jne .out                        ; the rest copy, the nearest there is
    cmp al, CWHITE
    je .nop
.xor:
    mov al, W16_XOR
    jmp short .out
.nop:
    mov al, W16_NOCOL
.out:
    pop bx
.ret:
    ret

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
; The display, and what Windows 3.0 answered on it
; =============================================================================

; w16_dispinit - read the display our window is on (SPEC.md 39.16.4) into
; [w16_disp], [w16_mono], [w16_scrw]/[w16_scrh] and the caption height that
; CreateWindow's sizing takes off. Clobbers AX, BX, CX, DX, SI.
w16_dispinit:
    push di
    mov bx, [w16_win]
    call OSAPI_WM_DISPLAY           ; AX/BX = size, DL = kind, DH = depth
    pop di
    mov [w16_scrw], ax
    mov [w16_scrh], bx
    xor ax, ax
    cmp dh, 1
    jne .colour
    inc ax
.colour:
    mov [w16_mono], al
    mov al, VID_KIND_VGA            ; a VESA display is VGA's to a Windows
    cmp dl, VID_KIND_VESA           ;   program: its desktop is the sixteen
    je .k
    mov al, dl
.k:
    mov [w16_disp], al
    ret

; w16_facepart - AL = the face part this display's System font is (D4):
; VGA 16 px, EGA and Hercules 12 (SETUP.INF's resolution class), CGA 8.
w16_facepart:
    mov al, W16P_SYS16
    cmp byte [w16_disp], VID_KIND_VGA
    je .out
    mov al, W16P_SYS08
    cmp byte [w16_disp], VID_KIND_CGA
    je .out
    mov al, W16P_SYS12
.out:
    ret

; GetDeviceCaps (GDI.80) - args: +0 nIndex, +2 hDC. What Windows 3.0's own
; display driver for this adapter answers: its GDIINFO, read off the
; driver files (VGA's was checked against GDICARD.EXE on Windows 3.0).
; HORZRES/VERTRES are the display's own size.
GDC_HORZRES   equ 8
GDC_VERTRES   equ 10
GDC_LOGPIXX   equ 88
GDC_LOGPIXY   equ 90
GDC_NWORDS    equ 23            ; DRIVERVERSION (0) .. ASPECTXY (44)

w16g_devcaps:
    call w16_args
    mov ax, [es:bx]
    cmp ax, GDC_HORZRES
    jne .nh
    mov ax, [w16_scrw]
    ret
.nh:
    cmp ax, GDC_VERTRES
    jne .nv
    mov ax, [w16_scrh]
    ret
.nv:
    mov bl, [w16_disp]
    xor bh, bh
    cmp ax, GDC_LOGPIXX
    jne .nx
    mov ax, 96
    ret
.nx:
    cmp ax, GDC_LOGPIXY
    jne .ny
    mov al, [w16_logpy + bx]
    xor ah, ah
    ret
.ny:
    test al, 1
    jnz .zero
    cmp ax, GDC_NWORDS * 2
    jae .zero
    push ax
    mov ax, GDC_NWORDS * 2
    mul bx
    pop bx
    add bx, ax
    mov ax, [w16_gdiinfo + bx]
    ret
.zero:
    xor ax, ax
    ret

; GDIINFO's first 23 words for each VID_* kind: DRIVERVERSION, TECHNOLOGY,
; HORZSIZE, VERTSIZE, HORZRES, VERTRES, BITSPIXEL, PLANES, NUMBRUSHES,
; NUMPENS, NUMMARKERS, NUMFONTS, NUMCOLORS, PDEVICESIZE, CURVECAPS,
; LINECAPS, POLYGONALCAPS, TEXTCAPS, CLIPCAPS, RASTERCAPS, ASPECTX, ASPECTY,
; ASPECTXY - from Windows 3.0's VGA.DRV, HERCULES.DRV, CGA.DRV and EGA.DRV
w16_gdiinfo:
    dw 0x300, 1, 208, 156, 640, 480, 1, 4, 0xFFFF, 80, 0, 0, 16, 35, 0, 34, 8, 8708, 1, 1753, 36, 36, 51
    dw 0x300, 1, 225, 145, 720, 348, 1, 1, 77, 10, 0, 0, 2, 35, 0, 34, 8, 8196, 0, 665, 11, 16, 19
    dw 0x300, 1, 240, 180, 640, 200, 1, 1, 77, 8, 0, 0, 2, 35, 0, 34, 8, 8196, 0, 665, 5, 12, 13
    dw 0x300, 1, 240, 175, 640, 350, 1, 4, 0xFFFF, 80, 0, 0, 16, 35, 0, 34, 8, 8708, 1, 1753, 38, 48, 61
w16_logpy:
    db 96, 72, 48, 72               ; LOGPIXELSY; LOGPIXELSX is 96 on all four

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

; w16_newobj - AL = colour, AH = W16K_* -> AX = the handle, 0 = none free
w16_newobj:
    push bx
    xor bx, bx
.find:
    cmp byte [w16_objk + bx], 0
    je .take
    inc bx
    cmp bx, W16_NOBJ
    jb .find
    xor ax, ax
    pop bx
    ret
.take:
    mov [w16_objk + bx], ah
    mov [w16_objc + bx], al
    lea ax, [bx + W16_OBJ0]
    pop bx
    ret

; CreateSolidBrush (GDI.66) - args: +0 the COLORREF (red, green; blue)
w16g_solidbrush:
    call w16_args
    mov ax, [es:bx]
    mov dl, [es:bx + 2]
    call w16_rgb2col
    mov ah, W16K_BRUSH
    jmp w16_newobj

; CreatePen (GDI.61) - args: +0 the COLORREF, +4 nWidth, +6 nPenStyle. Every
; style but PS_NULL draws solid, and every width one pixel (SPEC.md 124.8.1).
w16g_createpen:
    call w16_args
    mov ax, [es:bx]
    mov dl, [es:bx + 2]
    call w16_rgb2col
    cmp word [es:bx + 6], PS_NULL
    jne .pen
    mov al, W16_NOCOL
.pen:
    mov ah, W16K_PEN
    jmp w16_newobj

; DeleteObject (GDI.69) - args: +0 hObject. A stock object is never deleted.
w16g_delobj:
    call w16_args
    mov ax, [es:bx]
    mov bx, ax
    sub bx, W16_OBJ0
    jb .stock
    cmp bx, W16_NOBJ
    jae .no
    mov byte [w16_objk + bx], 0
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
    mov dx, ax
    sub dx, W16_OBJ0
    jb .stock
    cmp dx, W16_NOBJ
    jae .no
    mov bx, dx
    mov dl, [w16_objk + bx]
    mov bx, w16_brush
    cmp dl, W16K_BRUSH
    je .swap
    mov bx, w16_pen
    cmp dl, W16K_PEN
    je .swap
    jmp short .no
.stock:
    mov bx, w16_brush
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

; SetROP2 (GDI.4) - args: +0 nDrawMode, +2 hDC. Answers the old one.
w16g_setrop2:
    call w16_args
    mov cx, [es:bx]
    xor ax, ax
    mov al, [w16_rop]
    cmp cx, R2_BLACK
    jb .out
    cmp cx, R2_WHITE
    ja .out
    mov [w16_rop], cl
.out:
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
; buffer at a time - or, under an inverting ROP2, a point at a time.
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
    call w16_ropcol
    cmp al, W16_NOCOL
    je .done
    mov [w16_gcol], al
    cmp al, W16_XOR
    je .xor
    call w16_ink
.xor:
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

; w16_lplot - SI, DI = a client point: into the buffer if inside the clip, or
; inverted now under W16_XOR
w16_lplot:
    push ax
    push bx
    push cx
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
    cmp byte [w16_gcol], W16_XOR
    jne .buf
    mov bx, dx
    mov cx, ax
    call OSAPI_GFX_XOR_FILL
    jmp short .out
.buf:
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
    pop cx
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

; w16_rectin - ES:BX -> left, top, right, bottom (client, Windows' order on
; the stack: +6 left, +4 top, +2 right, +0 bottom) into [w16_rl]..[w16_rb],
; screen and inclusive. CF=1: empty, or the DC is not open.
w16_rectin:
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
    je .no
    mov ax, [w16_rl]
    cmp ax, [w16_rr]
    jg .no
    mov ax, [w16_rt]
    cmp ax, [w16_rb]
    jg .no
    clc
    ret
.no:
    stc
    ret

; w16_nullpen - AL = the pen's colour under the ROP: with no pen, Windows'
; Rectangle and RoundRect fill one pixel short of the right and bottom edges
; (GDICARD.EXE's bevel box on Windows 3.0), so take the pixel off. Keeps AX.
w16_nullpen:
    cmp al, W16_NOCOL
    jne .ret
    dec word [w16_rr]
    dec word [w16_rb]
.ret:
    ret

; Rectangle (GDI.27) - args: +0 bottom, +2 right, +4 top, +6 left, +8 hDC.
; The pen's frame on the outside, the brush inside it, no pixel twice.
w16g_rect:
    call w16_args
    call w16_rectin
    jc .done
    mov ax, [w16_pen]
    call w16_brcol
    call w16_ropcol
    call w16_nullpen
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
    call w16_ropcol
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

; -----------------------------------------------------------------------------
; RoundRect (GDI.28) - args: +0 ellipse height, +2 ellipse width, +4 bottom,
; +6 right, +8 top, +10 left, +12 hDC. Each corner is a quarter of an ellipse
; whose radii are half those (each at most W16_RRMAX); w16_rrin holds how far
; in the edge is on each of the corner's rows. Drawn as three batches of rows
; through w16_span: the pen's left edge (with the top and bottom rows whole),
; its right edge, and the brush between them.
; -----------------------------------------------------------------------------
w16g_roundrect:
    push si
    push di
    push bp
    call w16_args
    mov ax, [es:bx + 2]             ; the radii, before the rectangle moves BX
    shr ax, 1
    mov [w16_rrx], ax
    mov ax, [es:bx]
    shr ax, 1
    mov [w16_rry], ax
    add bx, 4
    call w16_rectin
    jc .done
    mov ax, [w16_pen]
    call w16_brcol
    call w16_ropcol
    call w16_nullpen
    mov [w16_rpen], al
    mov ax, [w16_rr]                ; the radii fit the box, and the table
    sub ax, [w16_rl]
    inc ax
    shr ax, 1
    mov cx, [w16_rrx]
    call w16_rrclamp
    mov [w16_rrx], cx
    mov ax, [w16_rb]
    sub ax, [w16_rt]
    inc ax
    shr ax, 1
    mov cx, [w16_rry]
    call w16_rrclamp
    mov [w16_rry], cx
    call w16_rrtable
    ; --- the pen ---
    mov al, [w16_rpen]
    cmp al, W16_NOCOL
    je .brush
    mov [w16_gcol], al
    mov dx, [w16_rt]                ; the left edge, and the top and bottom
.left:                              ;   rows whole
    call w16_rrrow                  ; AX..CX the frame's left run, SI..DI its
    cmp dx, [w16_rt]                ;   right run, BP = the row's inset
    je .whole
    cmp dx, [w16_rb]
    jne .lrun
.whole:
    mov cx, di
.lrun:
    call w16_span
    inc dx
    cmp dx, [w16_rb]
    jle .left
    call w16_spflush
    mov dx, [w16_rt]                ; the right edge, between them
    inc dx
.right:
    cmp dx, [w16_rb]
    jge .rdone
    call w16_rrrow
    mov ax, si
    mov cx, di
    call w16_span
    inc dx
    jmp short .right
.rdone:
    call w16_spflush
.brush:
    mov ax, [w16_brush]
    call w16_brcol
    call w16_ropcol
    cmp al, W16_NOCOL
    je .done
    mov [w16_gcol], al
    mov dx, [w16_rt]
    cmp byte [w16_rpen], W16_NOCOL
    je .fill
    inc dx                          ; inside the pen's top and bottom rows
.fill:
    mov bx, [w16_rb]
    cmp byte [w16_rpen], W16_NOCOL
    je .frow
    dec bx
.frow:
    cmp dx, bx
    jg .fdone
    call w16_rrrow
    cmp byte [w16_rpen], W16_NOCOL
    je .fspan                       ; no pen: the brush is the whole row
    mov ax, cx
    inc ax
    mov cx, si
    dec cx
.fspan:
    cmp byte [w16_rpen], W16_NOCOL
    jne .fput
    mov cx, di
.fput:
    call w16_span
    inc dx
    jmp short .frow
.fdone:
    call w16_spflush
.done:
    mov ax, 1
    pop bp
    pop di
    pop si
    ret

; w16_rrclamp - CX = a radius, AX = half the box: CX = the smallest of the
; two and W16_RRMAX
w16_rrclamp:
    cmp cx, ax
    jbe .t
    mov cx, ax
.t:
    cmp cx, W16_RRMAX
    jbe .out
    mov cx, W16_RRMAX
.out:
    ret

; w16_rrtable - [w16_rrin + k] = how far in from the side the edge is on the
; corner's row k (0 = its outermost), for k < ry: rx less the ellipse's
; half-width at the middle of that row, rounded,
;     x(k)^2 = rx^2 (4 ry^2 - (2 (ry - k) - 1)^2) / (4 ry^2)
; Clobbers AX, BX, CX, DX, DI.
w16_rrtable:
    xor di, di
.row:
    cmp di, [w16_rry]
    jae .out
    mov ax, [w16_rry]               ; t = 2 (ry - k) - 1
    sub ax, di
    shl ax, 1
    dec ax
    mul ax
    mov cx, ax                      ; CX = t^2
    mov ax, [w16_rry]
    shl ax, 1
    mul ax                          ; AX = 4 ry^2 (ry <= W16_RRMAX: a word)
    mov bx, ax
    sub ax, cx                      ; 4 ry^2 - t^2
    mov cx, ax
    mov ax, [w16_rrx]
    mul ax                          ; rx^2
    mul cx                          ; DX:AX = rx^2 (4 ry^2 - t^2)
    div bx                          ; AX = x^2, at most rx^2
    mov cx, ax
    xor bx, bx                      ; BX = the root, rounded:
.sq:                                ;   the last b with b^2 - b <= x^2
    inc bx
    mov ax, bx
    mul ax
    sub ax, bx
    cmp ax, cx
    jbe .sq
    dec bx
    mov ax, [w16_rrx]
    sub ax, bx
    jnb .in
    xor ax, ax
.in:
    mov [w16_rrin + di], al
    inc di
    jmp short .row
.out:
    ret

; w16_rrrow - DX = a row of the shape (screen): AX..CX = the frame's left
; run, SI..DI its right run, from the row's inset and the next row out's
; (the edge is joined where it steps). Keeps BX, DX.
w16_rrrow:
    push bx
    mov ax, dx                      ; k = the rows to the nearer edge
    sub ax, [w16_rt]
    mov bx, [w16_rb]
    sub bx, dx
    cmp ax, bx
    jbe .k
    mov ax, bx
.k:
    call w16_rrinset                ; BX = this row's inset
    mov cx, bx                      ; CX = the run's far end: the next row
    or ax, ax                       ;   out's inset less one, or this one's
    jz .edge
    dec ax
    push bx
    call w16_rrinset
    mov ax, bx
    pop bx
    dec ax
    cmp ax, bx
    jl .edge
    mov cx, ax
.edge:
    mov ax, [w16_rl]                ; the left run, l + inset .. l + far
    add ax, bx
    mov si, [w16_rr]
    sub si, cx                      ; the right run, r - far .. r - inset
    mov di, [w16_rr]
    sub di, bx
    mov bx, cx
    mov cx, [w16_rl]
    add cx, bx
    pop bx
    ret

; w16_rrinset - AX = rows from the nearer edge -> BX = the inset. Keeps AX.
w16_rrinset:
    xor bx, bx
    cmp ax, [w16_rry]
    jae .out
    mov bx, ax
    mov bl, [w16_rrin + bx]
    xor bh, bh
.out:
    ret

; FillRect (USER.81) - args: +0 hBrush, +2 lpRect, +6 hDC. The brush alone,
; and not under the ROP: Windows' FillRect is PATCOPY.
w16u_fillrect:
    call w16_args
    mov ax, [es:bx]
    call w16_brcol
    cmp al, W16_NOCOL
    je .out
    mov [w16_gcol], al
    les bx, [es:bx + 2]
    call w16_rcin
    jc .out
    mov ax, [w16_rl]
    mov bx, [w16_rt]
    mov cx, [w16_rr]
    mov dx, [w16_rb]
    call w16_gfill
.out:
    mov ax, 1
    ret

; w16_rcin - ES:BX -> a RECT (left, top, right, bottom, client): w16_rectin's
; answer for it. Clobbers AX, BX, ES.
w16_rcin:
    mov ax, [es:bx]
    mov [w16_scratch + 6], ax
    mov ax, [es:bx + 2]
    mov [w16_scratch + 4], ax
    mov ax, [es:bx + 4]
    mov [w16_scratch + 2], ax
    mov ax, [es:bx + 6]
    mov [w16_scratch], ax
    push ds
    pop es
    mov bx, w16_scratch
    jmp w16_rectin

; SetPixel (GDI.31) - args: +0 the COLORREF, +4 y, +6 x, +8 hDC. Not under
; the ROP. Answers the colour it drew, as a COLORREF.
w16g_setpixel:
    call w16_args
    mov ax, [es:bx]
    mov dl, [es:bx + 2]
    call w16_rgb2col
    mov [w16_gcol], al
    mov cx, [es:bx + 6]
    add cx, [w16_ox]
    mov dx, [es:bx + 4]
    add dx, [w16_oy]
    mov ax, cx
    mov bx, dx
    call w16_gfill
    mov al, [w16_gcol]              ; the answer: that colour's RGB
    mov bl, 3
    mul bl
    mov bx, ax
    mov ax, [w16_pal16 + bx]
    mov dl, [w16_pal16 + bx + 2]
    xor dh, dh
    ret

; =============================================================================
; Text: the System font (SPEC.md 124.7), set into a band and drawn in ONE
; OSAPI_GFX_BLIT1 a string (SPEC.md 6.3's method; os88type.inc's own faces
; are at most 8 pixels wide with even advances, and Windows' System font is
; neither, so the composer is WIN16's own)
; =============================================================================

; w16_glyph - AL = a character -> BX = its index in the face. A character the
; face has not is W16F_DEFCH. ES = the face. Keeps all but BX.
w16_glyph:
    push ax
    sub al, [es:W16F_FIRST]
    jb .def
    cmp al, [es:W16F_COUNT]
    jb .ok
.def:
    mov al, W16F_DEFCH
    sub al, [es:W16F_FIRST]
.ok:
    xor ah, ah
    mov bx, ax
    pop ax
    ret

; w16_textw - SI -> CX characters in our segment: AX = their width in the
; System font. Keeps all but AX.
w16_textw:
    push bx
    push cx
    push si
    push es
    xor ax, ax
    mov es, [w16_face]
    jcxz .out
.c:
    push ax
    mov al, [si]
    call w16_glyph
    pop ax
    add al, [es:W16F_ADV + bx]
    adc ah, 0
    inc si
    loop .c
.out:
    pop es
    pop si
    pop cx
    pop bx
    ret

; w16_txtcopy - ES:BX -> the args, lpString at +SI, nCount at +DI: copy at
; most W16_TXTMAX of them into w16_txt; CX = how many. Clobbers AX, SI, DI.
w16_txtcopy:
    mov cx, [es:bx + di]
    or cx, cx
    jge .pos
    xor cx, cx
.pos:
    cmp cx, W16_TXTMAX
    jbe .n
    mov cx, W16_TXTMAX
.n:
    push cx
    push es
    les si, [es:bx + si]
    mov di, w16_txt
    jcxz .done
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
.done:
    pop es
    pop cx
    ret

; SetTextColor (GDI.9) - args: +0 the COLORREF, +4 hDC. Answers the old one.
w16g_settextcol:
    call w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    xchg ax, [w16_trgb]
    xchg dx, [w16_trgb + 2]
    push ax
    push dx
    mov ax, [w16_trgb]
    mov dl, [w16_trgb + 2]
    call w16_rgb2col
    mov [w16_tcol], al
    pop dx
    pop ax
    ret

; GetTextExtent (GDI.91) - args: +0 nCount, +2 lpString, +6 hDC. DX:AX =
; height:width.
w16g_textext:
    call w16_args
    mov si, 2
    xor di, di
    call w16_txtcopy
    mov si, w16_txt
    call w16_textw
    mov es, [w16_face]
    mov dl, [es:W16F_ROWS]
    xor dh, dh
    ret

; GetTextMetrics (GDI.93) - args: +0 lpMetrics, +4 hDC. Windows 3.0's
; TEXTMETRIC for its System font on this display, the .FON's own header (D4).
TM_SIZE equ 31

w16g_textmet:
    call w16_args
    les di, [es:bx]
    push ds
    mov ds, [w16_face]
    xor ah, ah
    mov al, [W16F_ROWS]
    stosw                           ; tmHeight
    mov al, [W16F_ASC]
    stosw                           ; tmAscent
    mov al, [W16F_ROWS]
    sub al, [W16F_ASC]
    stosw                           ; tmDescent
    mov al, [W16F_IL]
    stosw                           ; tmInternalLeading
    mov al, [W16F_EL]
    stosw                           ; tmExternalLeading
    mov al, [W16F_AVG]
    stosw                           ; tmAveCharWidth
    mov al, [W16F_MAX]
    stosw                           ; tmMaxCharWidth
    pop ds
    mov ax, 700
    stosw                           ; tmWeight: bold
    xor ax, ax
    stosw                           ; tmItalic, tmUnderlined
    mov ax, 32 << 8                 ; tmStruckOut, tmFirstChar
    stosw
    mov ax, 0x80FF                  ; tmLastChar, tmDefaultChar
    stosw
    mov ax, 0x2120                  ; tmBreakChar, tmPitchAndFamily
    stosw                           ;   (FF_SWISS, variable pitch)
    xor ax, ax
    stosb                           ; tmCharSet: ANSI
    stosw                           ; tmOverhang
    mov al, 96
    stosw                           ; tmDigitizedAspectX
    mov bl, [w16_disp]
    xor bh, bh
    mov al, [w16_logpy + bx]
    stosw                           ; tmDigitizedAspectY
    mov ax, 1
    ret

; TextOut (GDI.33) - args: +0 nCount, +2 lpString, +6 y, +8 x, +10 hDC.
; Opaque, in the text colour on white, the System font, cut to the exact
; pixel of the clip.
w16g_textout:
    push si
    push di
    push bp
    cmp byte [w16_dcon], 0
    je .done
    call w16_args
    mov ax, [es:bx + 8]
    add ax, [w16_ox]
    mov [w16_tx], ax
    mov ax, [es:bx + 6]
    add ax, [w16_oy]
    mov [w16_ty], ax
    mov si, 2
    xor di, di
    call w16_txtcopy
    mov [w16_tn], cx
    call w16_txtdraw
.done:
    mov ax, 1
    pop bp
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; w16_txtdraw - [w16_tn] characters of w16_txt at [w16_tx], [w16_ty] (screen)
;
; L is the clip's left edge or the string's, whichever is further right, and
; the band's bit 0 is screen x (L AND NOT 7) - 16: OSAPI_GFX_BLIT1 takes any x
; on kern_big but reads the band's first byte as the screen's byte column
; holding it, and leaves its first x AND 7 pixels alone (SPEC.md 5.4.2.8). A
; glyph straddling L composes its left part into the two guard bytes or those
; pixels, none of which is drawn, so the cut is to the pixel. The blit starts
; two bytes in, at L, and is the clipped width wide.
; Clobbers AX, BX, CX, DX, SI, DI, BP.
; -----------------------------------------------------------------------------
w16_txtdraw:
    push es
    mov es, [w16_face]
    mov si, w16_txt
    mov cx, [w16_tn]
    call w16_textw                  ; AX = the string's width
    or ax, ax
    jz .out
    add ax, [w16_tx]
    dec ax                          ; R = its right edge, cut to the clip
    cmp ax, [w16_cr]
    jle .r
    mov ax, [w16_cr]
.r:
    mov [w16_rr], ax
    mov ax, [w16_tx]                ; L
    cmp ax, [w16_cl]
    jge .l
    mov ax, [w16_cl]
.l:
    mov [w16_rl], ax
    cmp ax, [w16_rr]
    jg .out
    mov al, [es:W16F_ROWS]
    xor ah, ah
    add ax, [w16_ty]
    dec ax                          ; B
    cmp ax, [w16_cb]
    jle .b
    mov ax, [w16_cb]
.b:
    sub ax, [w16_ty]
    mov [w16_r1], ax                ; ...as the last glyph row drawn
    mov ax, [w16_ty]                ; T
    cmp ax, [w16_ct]
    jge .t
    mov ax, [w16_ct]
.t:
    sub ax, [w16_ty]
    mov [w16_r0], ax                ; ...as the first
    cmp ax, [w16_r1]
    jg .out
    ; --- clear the rows of the band the blit takes ---
    mov ax, [w16_rl]
    and ax, 0xFFF8
    sub ax, 16
    mov [w16_tb0], ax               ; the band's bit 0, on the screen
    mov ax, [w16_rr]
    sub ax, [w16_tb0]
    add ax, 8 + 7
    mov cl, 3
    shr ax, cl                      ; the guard, the width, the spill
    mov [w16_tnb], ax
    push es
    push ds
    pop es
    mov ax, W16_BS
    mul word [w16_r0]
    add ax, w16_gbuf
    mov di, ax
    mov dx, [w16_r1]
    sub dx, [w16_r0]
    inc dx
.clr:
    push di
    mov cx, [w16_tnb]
    xor al, al
    rep stosb
    pop di
    add di, W16_BS
    dec dx
    jnz .clr
    pop es
    ; --- the glyphs ---
    mov dx, [w16_tx]                ; DX = the pen
    mov si, w16_txt
    mov cx, [w16_tn]
.g:
    push cx
    mov al, [si]
    call w16_glyph                  ; BX = the glyph
    mov cl, [es:W16F_ADV + bx]
    xor ch, ch                      ; CX = its advance
    mov ax, dx
    add ax, cx
    cmp ax, [w16_rl]
    jle .next                       ; wholly left of L
    cmp dx, [w16_rr]
    jg .stop                        ; ...or right of R: the string is done
    call w16_compose
.next:
    add dx, cx
    inc si
    pop cx
    loop .g
    jmp short w16_bandblit
.stop:
    pop cx
    jmp short w16_bandblit
.out:
    pop es
    ret

; w16_bandblit - the tail w16_txtdraw and w16_bmp share: blit the band's
; rows [w16_r0]..[w16_r1] from screen x [w16_rl] to [w16_rr], row [w16_ty]
; + [w16_r0], in [w16_tcol] on [w16_tpap]. The caller pushed ES.
w16_bandblit:
    ; --- the colours, and on a 1bpp display what lit means ---
    mov al, [w16_tcol]
    mov ah, [w16_tpap]
    cmp byte [w16_mono], 0
    je .pen
    xor bh, bh                      ; a 1bpp display: the band's set bits
    mov bl, ah                      ;   are what is lit
    mov cl, [w16_monolit + bx]      ; CL = the paper lit
    mov bl, al
    mov ch, [w16_monolit + bx]      ; CH = the ink lit
    or cl, cl
    jz .dark
    or ch, ch
    jnz .lit                        ; lit ink on lit paper: all lit
    call w16_bandnot                ; dark ink on lit paper: the ink is the
    jmp short .pen                  ;   band's clear bits
.lit:
    call w16_bandfill
    jmp short .pen
.dark:
    or ch, ch                       ; lit ink on dark paper: the glyphs as
    jnz .pen                        ;   they are; dark on dark: none lit
    call w16_bandclr
.pen:
    call OSAPI_GFX_BLIT1_PEN        ; AL = ink, AH = paper (colour only)
    push ds
    pop es
    mov ax, W16_BS
    mul word [w16_r0]
    add ax, w16_gbuf + 2
    mov si, ax
    mov bp, W16_BS
    mov ax, [w16_rl]
    mov cx, [w16_rr]
    sub cx, ax
    inc cx
    mov bx, [w16_ty]
    add bx, [w16_r0]
    mov dx, [w16_r1]
    sub dx, [w16_r0]
    inc dx
    call OSAPI_GFX_BLIT1
.out:
    pop es
    ret

; -----------------------------------------------------------------------------
; w16_bmp - a small 1bpp picture: CX rows of two bytes at DS:SI (the left
; pixel the first byte's top bit), DX wide (at most 16), at most 20 rows, its top left
; at [w16_tx], [w16_ty] (screen), in [w16_tcol] on [w16_tpap], cut to the
; DC's clip - one band and one blit, as text is (SPEC.md 124.13: a check
; box, a radio button, an arrow). Clobbers AX, BX, CX, DX, SI, DI, BP.
; -----------------------------------------------------------------------------
w16_bmp:
    push es
    mov [w16_bmpp], si
    mov ax, [w16_tx]                ; R, cut to the clip
    add ax, dx
    dec ax
    cmp ax, [w16_cr]
    jle .r
    mov ax, [w16_cr]
.r:
    mov [w16_rr], ax
    mov ax, [w16_tx]                ; L
    cmp ax, [w16_cl]
    jge .l
    mov ax, [w16_cl]
.l:
    mov [w16_rl], ax
    cmp ax, [w16_rr]
    jg .out
    mov ax, cx
    add ax, [w16_ty]
    dec ax                          ; B
    cmp ax, [w16_cb]
    jle .b
    mov ax, [w16_cb]
.b:
    sub ax, [w16_ty]
    mov [w16_r1], ax
    mov ax, [w16_ty]                ; T
    cmp ax, [w16_ct]
    jge .t
    mov ax, [w16_ct]
.t:
    sub ax, [w16_ty]
    mov [w16_r0], ax
    cmp ax, [w16_r1]
    jg .out
    mov ax, [w16_rl]
    and ax, 0xFFF8
    sub ax, 16
    mov [w16_tb0], ax
    mov ax, [w16_rr]
    sub ax, [w16_tb0]
    add ax, 8 + 7
    mov cl, 3
    shr ax, cl
    mov [w16_tnb], ax
    push ds
    pop es
    mov ax, W16_BS                  ; DI = the band's first row drawn
    mul word [w16_r0]
    add ax, w16_gbuf
    mov di, ax
    mov si, [w16_r0]                ; SI = that row of the picture
    shl si, 1
    add si, [w16_bmpp]
    mov ax, [w16_tx]
    sub ax, [w16_tb0]
    mov cl, al
    and cl, 7
    mov ch, 8
    sub ch, cl                      ; CL = the shift, CH = 8 less it
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov bx, ax                      ; BX = the picture's byte in a row
    mov dx, [w16_r1]
    sub dx, [w16_r0]
    inc dx
.row:
    push di
    push cx
    mov cx, [w16_tnb]
    xor al, al
    cld
    rep stosb
    pop cx
    pop di
    mov ah, [si]
    mov al, [si + 1]
    push dx
    mov dl, al
    shr ax, cl
    xchg cl, ch
    shl dl, cl
    xchg cl, ch
    or [di + bx], ah
    or [di + bx + 1], al
    or [di + bx + 2], dl
    pop dx
    inc si
    inc si
    add di, W16_BS
    dec dx
    jnz .row
    jmp w16_bandblit
.out:
    pop es
    ret

; w16_compose - glyph BX at the pen DX into the band's rows [w16_r0]..[w16_r1].
; ES = the face. Keeps BX, CX, DX, SI.
w16_compose:
    push bx
    push cx
    push dx
    push si
    mov al, [es:W16F_ADV + bx]
    shl bx, 1
    mov si, [es:W16F_OFF + bx]      ; ES:SI = its first row
    cmp al, 8
    mov al, 1
    jbe .one
    inc ax
.one:
    mov [w16_gbpr], al              ; bytes a row: 1 or 2
    mul byte [w16_r0]
    add si, ax                      ; ...the first row drawn
    mov ax, dx
    sub ax, [w16_tb0]               ; the bit in the band
    mov cl, al
    and cl, 7
    mov ch, 8
    sub ch, cl                      ; CL = the shift, CH = 8 less it
    mov bx, ax
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov ax, W16_BS
    mul word [w16_r0]
    add bx, ax
    add bx, w16_gbuf                ; DS:BX = the band's byte
    mov dx, [w16_r1]
    sub dx, [w16_r0]
    inc dx                          ; DX = rows
.row:
    mov ah, [es:si]
    xor al, al
    cmp byte [w16_gbpr], 1
    je .w
    mov al, [es:si + 1]
.w:
    push dx
    mov dl, al                      ; the 24 bits: AH, AL, then DL's
    shr ax, cl
    xchg cl, ch
    shl dl, cl
    xchg cl, ch
    or [bx], ah
    or [bx + 1], al
    or [bx + 2], dl
    pop dx
    add si, [w16_gbpr]              ; (the high byte is [w16_gbpr + 1] = 0)
    add bx, W16_BS
    dec dx
    jnz .row
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16_bandnot / w16_bandfill - every byte of the rows the blit takes,
; complemented / set: a 1bpp display reads a band as lit and unlit, so a dark
; glyph on white paper is the band's clear bits, and ink as light as its
; paper is no glyph at all. Keeps every register.
w16_bandnot:
    push cx
    mov cl, 0xFF
    jmp short w16_bandop
w16_bandfill:
    push cx
    mov cl, 0
    jmp short w16_bandop
w16_bandclr:
    push cx
    mov cl, 1
w16_bandop:
    push ax
    push dx
    push di
    mov ax, W16_BS
    mul word [w16_r0]
    add ax, w16_gbuf
    mov di, ax
    mov dx, [w16_r1]
    sub dx, [w16_r0]
    inc dx
.row:
    push di
    push dx
    mov dx, [w16_tnb]
.b:
    or cl, cl
    jz .set
    cmp cl, 1
    je .clr
    not byte [di]
    jmp short .n
.clr:
    mov byte [di], 0
    jmp short .n
.set:
    mov byte [di], 0xFF
.n:
    inc di
    dec dx
    jnz .b
    pop dx
    pop di
    add di, W16_BS
    dec dx
    jnz .row
    pop di
    pop dx
    pop ax
    pop cx
    ret

; =============================================================================
; SetDIBitsToDevice (GDI.443) - args: +0 wUsage, +2 lpbmi, +6 lpBits,
; +10 cScanLines, +12 uStartScan, +14 ySrc, +16 xSrc, +18 cy, +20 cx,
; +22 yDest, +24 xDest, +26 hDC
;
; A DIB's rows run bottom up and ySrc counts from its bottom; lpBits holds
; cScanLines of them starting at uStartScan. The visible part - the clip, and
; the rows lpBits has - is converted into a 4bpp block in os8088's colours
; (each palette entry the nearest of the sixteen) and put down with one
; OSAPI_GFX_BLIT4 per W16_GBUF of it. 1 and 4 bits a pixel, BITMAPINFOHEADER
; or the older BITMAPCOREHEADER, DIB_RGB_COLORS. Answers the rows drawn.
; =============================================================================
w16g_setdib:
    push si
    push di
    push bp
    xor ax, ax
    mov [w16_dres], ax
    cmp byte [w16_dcon], 0
    je .ret
    call w16_args
    cmp word [es:bx], 0             ; DIB_PAL_COLORS: no palette here
    jne .ret
    push es
    push bx
    les si, [es:bx + 2]             ; the header
    mov ax, [es:si + 4]             ; width and height: words of a core
    mov dx, [es:si + 6]             ;   header, dwords of an info one
    mov cl, [es:si + 10]            ; ...and the bits a pixel
    mov di, 3                       ; ...and the palette's bytes an entry
    cmp word [es:si], 12
    je .core
    mov dx, [es:si + 8]
    mov cl, [es:si + 14]
    mov di, 4
.core:
    mov [w16_dwid], ax
    mov [w16_dhgt], dx
    mov [w16_dbpp], cl
    mov [w16_dpal], di
    ; --- the palette, mapped: 2 or 16 entries ---
    mov bx, 2
    cmp cl, 1
    je .pal
    mov bx, 16
    cmp cl, 4
    jne .refused
.pal:
    add si, [es:si]                 ; the entries follow the header
    xor di, di
.pe:
    mov dl, [es:si]                 ; blue, green, red
    mov ah, [es:si + 1]
    mov al, [es:si + 2]
    call w16_rgb2col
    cmp byte [w16_mono], 0          ; BLIT4 would put the kernel's own map
    je .pm                          ;   on it (SPEC.md 39.4): reduce it as
    push bx                         ;   w16_ink does
    mov bx, w16_monotab
    xlat
    pop bx
.pm:
    mov [w16_dmap + di], al
    add si, [w16_dpal]
    inc di
    cmp di, bx
    jb .pe
    ; --- the source stride: DWORD-aligned rows ---
    mov ax, [w16_dwid]
    mov cl, [w16_dbpp]
    xor ch, ch
    mul cx                          ; bits a row
    add ax, 31
    mov cl, 5
    shr ax, cl
    shl ax, 1
    shl ax, 1
    mov [w16_dstride], ax
    pop bx
    pop es
    ; --- the visible rectangle, on the screen ---
    mov ax, [es:bx + 24]
    add ax, [w16_ox]
    mov [w16_rl], ax                ; the destination's left ...
    mov [w16_dx0], ax
    mov ax, [es:bx + 22]
    add ax, [w16_oy]
    mov [w16_rt], ax
    mov [w16_dy0], ax
    mov ax, [w16_rl]
    add ax, [es:bx + 20]
    dec ax
    mov [w16_rr], ax
    mov ax, [w16_rt]
    add ax, [es:bx + 18]
    dec ax
    mov [w16_rb], ax
    mov ax, [es:bx + 16]
    mov [w16_dsx], ax
    mov ax, [es:bx + 14]
    add ax, [es:bx + 18]
    dec ax
    mov [w16_dtop], ax              ; the source row at the top, bottom up
    mov ax, [es:bx + 12]
    mov [w16_dstart], ax
    mov ax, [es:bx + 10]
    mov [w16_dlines], ax
    les si, [es:bx + 6]
    mov [w16_dbits], si
    mov [w16_dbits + 2], es
    call w16_cliprect               ; [w16_rl]..[w16_rb] cut to the clip
    jc .ret
    ; --- band by band: as many rows as W16_GBUF holds ---
    mov ax, [w16_rr]
    sub ax, [w16_rl]
    inc ax
    mov [w16_dvw], ax
    inc ax
    shr ax, 1
    mov [w16_d4s], ax               ; 4bpp bytes a row
    mov bx, ax
    mov ax, W16_GBUF
    xor dx, dx
    div bx
    or ax, ax
    jz .ret                         ; a row wider than the buffer (it is not)
    mov [w16_dband], ax
    mov dx, [w16_rt]                ; DX = the screen row
.band:
    cmp dx, [w16_rb]
    jg .ret
    mov [w16_dby], dx
    mov di, w16_gbuf
    xor cx, cx                      ; CX = rows in this band
.brow:
    cmp dx, [w16_rb]
    jg .bput
    cmp cx, [w16_dband]
    jae .bput
    call w16_dibrow                 ; CF=1: lpBits has not that row
    jc .bput
    add di, [w16_d4s]
    inc dx
    inc cx
    jmp short .brow
.bput:
    jcxz .skip
    push dx
    push es
    push ds
    pop es
    mov si, w16_gbuf
    mov bp, [w16_d4s]
    mov ax, [w16_rl]
    mov bx, [w16_dby]
    mov dx, cx
    add [w16_dres], cx
    mov cx, [w16_dvw]
    call OSAPI_GFX_BLIT4
    pop es
    pop dx
    jmp short .band
.skip:
    inc dx                          ; a row lpBits has not: not drawn
    jmp short .band
.refused:
    pop bx
    pop es
.ret:
    mov ax, [w16_dres]
    pop bp
    pop di
    pop si
    ret

; w16_dibrow - screen row DX of the DIB into 4bpp at DS:DI, [w16_dvw] pixels
; from [w16_rl]. CF=1: lpBits does not hold that row. Keeps all but AX, BX.
w16_dibrow:
    push cx
    push dx
    push si
    push di
    push es
    mov ax, dx                      ; the source row, bottom up
    sub ax, [w16_dy0]
    mov bx, [w16_dtop]
    sub bx, ax
    sub bx, [w16_dstart]            ; ...and in lpBits
    jb .none
    cmp bx, [w16_dlines]
    jae .none
    mov ax, [w16_dstride]
    mul bx
    les si, [w16_dbits]
    add si, ax                      ; ES:SI = the row
    mov cx, [w16_rl]                ; the first source column
    sub cx, [w16_dx0]
    add cx, [w16_dsx]
    mov dx, [w16_dvw]
    xor bx, bx                      ; BH = the 4bpp byte being built, BL = 0
.px:                                ;   its first half, 1 its second
    push bx
    push cx
    cmp byte [w16_dbpp], 1
    je .b1
    mov bx, cx                      ; 4 bits
    shr bx, 1
    mov al, [es:si + bx]
    test cl, 1
    jnz .lo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lo:
    and al, 15
    jmp short .map
.b1:
    mov bx, cx                      ; 1 bit
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov al, [es:si + bx]
    and cl, 7
    shl al, cl
    rol al, 1
    and al, 1
.map:
    mov bx, w16_dmap
    xlat
    pop cx
    pop bx
    or bl, bl
    jnz .second
    mov bh, al
    shl bh, 1
    shl bh, 1
    shl bh, 1
    shl bh, 1
    mov bl, 1
    jmp short .nx
.second:
    or bh, al
    mov [di], bh
    inc di
    xor bx, bx
.nx:
    inc cx
    dec dx
    jnz .px
    or bl, bl
    jz .ok
    mov [di], bh                    ; an odd width's last half-byte
.ok:
    clc
    jmp short .out
.none:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    ret

; w16_cliprect - [w16_rl]..[w16_rb] cut to the DC's clip. CF=1: nothing left.
w16_cliprect:
    mov ax, [w16_cl]
    cmp [w16_rl], ax
    jge .l
    mov [w16_rl], ax
.l:
    mov ax, [w16_ct]
    cmp [w16_rt], ax
    jge .t
    mov [w16_rt], ax
.t:
    mov ax, [w16_cr]
    cmp [w16_rr], ax
    jle .r
    mov [w16_rr], ax
.r:
    mov ax, [w16_cb]
    cmp [w16_rb], ax
    jle .b
    mov [w16_rb], ax
.b:
    mov ax, [w16_rl]
    cmp ax, [w16_rr]
    jg .no
    mov ax, [w16_rt]
    cmp ax, [w16_rb]
    jg .no
    clc
    ret
.no:
    stc
    ret
