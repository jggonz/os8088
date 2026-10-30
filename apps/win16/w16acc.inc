; =============================================================================
; os8088 - apps/win16/w16acc.inc
;
; WHAT CLOCK AND REVERSI CALL (SPEC.md 124.17): Windows 3.0's two 8086
; accessories that S11 runs on a stock 8088 beside Calculator. A background
; colour, mode and text alignment for the DC, ExtTextOut, Ellipse, PatBlt,
; a monochrome bitmap and a pattern brush made from one, two rectangle
; calls, the keyboard's shift state - and the font calls answered as a
; machine with only the System font answers them.
; =============================================================================

OPAQUE      equ 2               ; SetBkMode's default
TA_RIGHT    equ 2               ; SetTextAlign: TA_CENTER is both bits
TA_CENTER   equ 6
TA_BOTTOM   equ 8
TA_BASELINE equ 24
ETO_OPAQUE  equ 2
ETO_CLIPPED equ 4

; the one-line answers: AddFontResource, RemoveFontResource and
; GetInstanceData answer 0, UnrealizeObject and GetModuleUsage 1, and
; SetCursorPos does nothing (os8088 lets no package move the pointer)
; THE CODE IS THE LIBRARY PART'S (SPEC.md 124.19.3): moved out of the image
; in S14 - a call into the image is ICALL, and its data stays in .text
section .w16dlg
W16LIB w16_ans0
    xor ax, ax
    cwd
    ret
W16LIB w16_ans1
    mov ax, 1
    xor dx, dx
W16LIB w16u_setcurpos
    ret

; CreateFontIndirect (GDI.57) - the System font: the only face there is, so
; the one Windows' mapper would fall back to (SPEC.md 124.17.1)
W16LIB w16g_crefont
    mov ax, W16_STOCK + SYSTEM_FONT
    ret

; CreateDC (GDI.53) - an information context for the display: the desktop's
; DC, which GetDeviceCaps answers for and nothing draws on
W16LIB w16g_credc
    mov ax, W16_HDESKDC
    ret

; SetBkColor (GDI.1) - args: +0 the COLORREF, +4 hDC. The paper text is
; drawn on, and ExtTextOut's opaque rectangle. Answers the old one.
W16LIB w16g_setbkcol
    mov si, w16_brgb
    mov di, w16_tpap
    ; fall into w16_cref

; w16_cref - SetTextColor's and SetBkColor's body: the COLORREF at +0 into
; the dword at SI, its nearest colour into the byte at DI; DX:AX = the old
w16_cref:
    ICALL w16_args
    xor ax, ax
    xor dx, dx
    cmp word [es:bx + 4], W16_HDC   ; another DC's: kept nowhere
    jne .out
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    xchg ax, [si]
    xchg dx, [si + 2]
    push ax
    push dx
    mov ax, [si]
    mov dl, [si + 2]
    ICALL w16_rgb2col
    mov [di], al
    pop dx
    pop ax
.out:
    ret

; SetBkMode (GDI.2), SetTextAlign (GDI.346) - args: +0 the value, +2 hDC.
; Answer the old one. TRANSPARENT is kept and answered, but text is drawn on
; its paper either way (SPEC.md 124.17.1).
W16LIB w16g_setbkmode
    mov si, w16_bkmode
    jmp short w16_dcbyte
W16LIB w16g_settalign
    mov si, w16_tal
w16_dcbyte:
    ICALL w16_args
    xor ax, ax
    cmp word [es:bx + 2], W16_HDC
    jne .out
    mov al, [es:bx]
    xchg al, [si]
.out:
    ret

; GetNearestColor (GDI.154) - args: +0 the COLORREF, +4 hDC: the colour of
; the sixteen it is drawn in, as a COLORREF
W16LIB w16g_nearcol
    ICALL w16_args
    mov ax, [es:bx]
    mov dl, [es:bx + 2]
    ICALL w16_rgb2col
    mov bl, al
    xor bh, bh
    mov ax, bx
    shl bx, 1
    add bx, ax
    mov ax, [w16_pal16 + bx]
    mov dl, [w16_pal16 + bx + 2]
    xor dh, dh
    ret

; w16_talign - move [w16_tx], [w16_ty] from where [w16_tal] says the
; reference point is to the text's top left. Clobbers AX, CX, SI.
w16_talign:
    test byte [w16_tal], TA_RIGHT
    jz .v
    push es
    mov es, [w16_face]
    mov si, w16_txt
    mov cx, [w16_tn]
    ICALL w16_textw
    pop es
    test byte [w16_tal], TA_CENTER & ~TA_RIGHT
    jz .r
    shr ax, 1                       ; centred: half of it
.r:
    sub [w16_tx], ax
.v:
    mov cl, [w16_tal]
    and cl, TA_BASELINE
    jz .ret
    push es
    mov es, [w16_face]
    mov al, [es:W16F_ASC]           ; the baseline: the ascent above it
    cmp cl, TA_BASELINE
    je .up
    mov al, [es:W16F_ROWS]          ; the bottom: the whole cell
.up:
    pop es
    xor ah, ah
    sub [w16_ty], ax
.ret:
    ret

; -----------------------------------------------------------------------------
; ExtTextOut (GDI.351) - args: +0 lpDx, +4 nCount, +6 lpString, +10 lpRect,
; +14 wOptions, +16 y, +18 x, +20 hDC. ETO_OPAQUE fills the rectangle in the
; background colour first, ETO_CLIPPED cuts the text to it; lpDx's spacing
; is not followed - each character keeps its own advance (SPEC.md 124.17.1).
; -----------------------------------------------------------------------------
W16LIB w16g_exttext
    push si
    push di
    push bp
    cmp byte [w16_dcon], 0
    je .yes
    ICALL w16_args
    cmp word [es:bx + 20], W16_HDC
    jne .no
    push word [w16_cl]              ; the clip, which ETO_CLIPPED narrows
    push word [w16_ct]
    push word [w16_cr]
    push word [w16_cb]
    mov ax, [es:bx + 10]
    or ax, [es:bx + 12]
    jz .text                        ; no rectangle
    push es
    push bx
    mov bp, [es:bx + 14]            ; the options
    les bx, [es:bx + 10]
    ICALL w16_rcin                   ; [w16_rl].. = it, on the screen
    pop bx
    pop es
    jc .gone                        ; empty: nothing filled, nothing cut
    test bp, ETO_OPAQUE
    jz .clip
    mov al, [w16_tpap]
    mov [w16_gcol], al
    push bx
    mov ax, [w16_rl]
    mov bx, [w16_rt]
    mov cx, [w16_rr]
    mov dx, [w16_rb]
    ICALL w16_gfill
    pop bx
.clip:
    test bp, ETO_CLIPPED
    jz .text
    mov ax, [w16_rl]
    cmp ax, [w16_cl]
    jle .c1
    mov [w16_cl], ax
.c1:
    mov ax, [w16_rt]
    cmp ax, [w16_ct]
    jle .c2
    mov [w16_ct], ax
.c2:
    mov ax, [w16_rr]
    cmp ax, [w16_cr]
    jge .c3
    mov [w16_cr], ax
.c3:
    mov ax, [w16_rb]
    cmp ax, [w16_cb]
    jge .text
    mov [w16_cb], ax
.text:
    mov ax, [es:bx + 18]
    add ax, [w16_ox]
    mov [w16_tx], ax
    mov ax, [es:bx + 16]
    add ax, [w16_oy]
    mov [w16_ty], ax
    mov si, 6
    mov di, 4
    ICALL w16_txtcopy
    mov [w16_tn], cx
    call w16_talign
    ICALL w16_txtdraw
.gone:
    pop word [w16_cb]
    pop word [w16_cr]
    pop word [w16_ct]
    pop word [w16_cl]
.yes:
    mov ax, 1
.out:
    pop bp
    pop di
    pop si
    ret
.no:
    xor ax, ax
    jmp short .out

; Ellipse (GDI.24) - args: +0 bottom, +2 right, +4 top, +6 left, +8 hDC:
; RoundRect's shape with each corner's radii half the box's, so the four
; quarters meet (radii past W16_RRMAX are cut to it, as RoundRect's are)
W16LIB w16g_ellipse
    push si
    push di
    push bp
    ICALL w16_args
    mov ax, [es:bx + 2]
    sub ax, [es:bx + 6]
    sar ax, 1
    mov [w16_rrx], ax
    mov ax, [es:bx]
    sub ax, [es:bx + 4]
    sar ax, 1
    mov [w16_rry], ax
    pop bp                          ; RoundRect's own frame, from the image
    pop di
    pop si
    ICALL w16g_rrbox
    ret

; PatBlt (GDI.29) - args: +0 dwRop, +4 nHeight, +6 nWidth, +8 y, +10 x,
; +12 hDC: BitBlt with no source (PATCOPY, PATINVERT, DSTINVERT, BLACKNESS,
; WHITENESS)
W16LIB w16g_patblt
    ICALL w16_args
    mov ax, [es:bx + 4]
    mov [w16b_cy], ax
    mov ax, [es:bx + 6]
    mov [w16b_cx], ax
    mov ax, [es:bx + 8]
    mov [w16b_y], ax
    mov ax, [es:bx + 10]
    mov [w16b_x], ax
    mov cx, [es:bx + 12]
    xor ax, ax
    call w16_blt
    ret

; -----------------------------------------------------------------------------
; CreateBitmap (GDI.48) - args: +0 lpBits, +4 cBitsPixel, +6 cPlanes,
; +8 nHeight, +10 nWidth. A monochrome bitmap's bits (one plane, one bit,
; rows a word apart, top row first) are converted as a DIB's are: a set bit
; white, a clear one black. Any other format is made black (SPEC.md 124.17.1).
; -----------------------------------------------------------------------------
W16LIB w16g_crebmp
    push si
    push di
    push bp
    ICALL w16_args
    mov ax, [es:bx + 10]
    mov dx, [es:bx + 8]
    push bx
    call w16_bmnew                  ; AX = the handle, BX = its slot * 2
    mov bp, bx
    pop bx
    jc .out
    push ax
    mov ax, [es:bx + 4]
    mul word [es:bx + 6]
    cmp ax, 1
    jne .done
    les si, [es:bx]
    mov ax, es
    or ax, si
    jz .done
    mov ax, [ds:w16b_w + bp]
    mov [w16_scratch], ax           ; the width
    add ax, 15
    mov cl, 4
    shr ax, cl
    shl ax, 1
    mov [w16_scratch + 2], ax       ; the bits' bytes a row
    mov ax, [ds:w16b_str + bp]
    mov [w16_scratch + 4], ax       ; the bitmap's
    mov ax, [ds:w16b_h + bp]
    mov [w16_scratch + 6], ax       ; rows to go
    mov dx, [ds:w16b_seg + bp]
    xor di, di
.row:
    xor bx, bx                      ; BX = the column
.px:
    cmp bx, [w16_scratch]
    jae .nrow
    mov ax, bx
    mov cl, 3
    shr ax, cl
    push si
    add si, ax
    mov al, [es:si]
    pop si
    mov cl, bl
    and cl, 7
    shl al, cl
    test al, 0x80
    jz .next
    push es
    push di
    mov es, dx
    mov ax, bx
    shr ax, 1
    add di, ax
    mov al, 0xF0                    ; CWHITE, the high nibble leftmost
    test bl, 1
    jz .hi
    mov al, 0x0F
.hi:
    or [es:di], al
    pop di
    pop es
.next:
    inc bx
    jmp short .px
.nrow:
    add si, [w16_scratch + 2]
    add di, [w16_scratch + 4]
    dec word [w16_scratch + 6]
    jnz .row
.done:
    pop ax
.out:
    pop bp
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; CreatePatternBrush (GDI.60) - args: +0 hBitmap. The brush keeps no copy of
; the pattern: half its top left 8 x 8 bright (a bright pixel white, a dark
; one black) is the kernel's 50% checker, which is REVERSI's board, and
; anything else is the colour of the sixteen nearest the grey it averages
; (SPEC.md 124.17.1). The bitmap may be deleted at once, as REVERSI does.
; -----------------------------------------------------------------------------
W16LIB w16g_patbrush
    push si
    push di
    push bp
    ICALL w16_args
    mov ax, [es:bx]
    call w16_bmslot                 ; BX = its slot * 2
    mov ax, 0
    jc .out
    mov es, [w16b_seg + bx]
    mov cx, [w16b_h + bx]
    cmp cx, 8
    jbe .h
    mov cx, 8
.h:
    mov bp, [w16b_w + bx]
    cmp bp, 8
    jbe .w
    mov bp, 8
.w:
    mov ax, bp
    mul cx
    mov [w16_scratch], ax           ; pixels counted
    mov ax, [w16b_str + bx]
    mov [w16_scratch + 2], ax
    xor dx, dx                      ; DX = the bright ones
    xor bx, bx                      ; BX = the row's offset
.row:
    xor si, si
.px:
    cmp si, bp
    jae .nrow
    push si
    shr si, 1
    mov al, [es:bx + si]
    pop si
    test si, 1
    jnz .lo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lo:
    test al, 8                      ; IRGB's I, or white's
    jz .dark
    inc dx
.dark:
    inc si
    jmp short .px
.nrow:
    add bx, [w16_scratch + 2]
    loop .row
    mov ax, dx                      ; exactly half: Windows' 50% checker,
    shl ax, 1                       ;   which the kernel's dither fill is
    cmp ax, [w16_scratch]
    mov al, W16_GRAY
    je .made
    mov ax, 255
    mul dx
    div word [w16_scratch]          ; AL = the grey
    mov ah, al
    mov dl, al
    ICALL w16_rgb2col
.made:
    mov ah, W16K_BRUSH
    ICALL w16_newobj
.out:
    pop bp
    pop di
    pop si
    ret

; OffsetRect (USER.77), InflateRect (USER.78) - args: +0 dy, +2 dx, +4 lpRect
W16LIB w16u_offrect
    ICALL w16_args
    mov ax, [es:bx + 2]
    mov dx, [es:bx]
    les bx, [es:bx + 4]
    add [es:bx], ax
    add [es:bx + 2], dx
    jmp short w16_rbr
W16LIB w16u_infrect
    ICALL w16_args
    mov ax, [es:bx + 2]
    mov dx, [es:bx]
    les bx, [es:bx + 4]
    sub [es:bx], ax
    sub [es:bx + 2], dx
w16_rbr:
    add [es:bx + 4], ax
    add [es:bx + 6], dx
    ret

; GetKeyState (USER.106) - args: +0 nVirtKey. Shift, Ctrl and Alt are read
; off the BIOS's shift flags, down answering 8000h; any other key is up, as
; os8088 delivers no key's release (SPEC.md 124.17.1)
W16LIB w16u_getkeystate
    ICALL w16_args
    mov al, [es:bx]
    mov ah, 3                       ; either Shift
    cmp al, 0x10
    je .flag
    mov ah, 4                       ; Ctrl
    cmp al, 0x11
    je .flag
    mov ah, 8                       ; Alt
    cmp al, 0x12
    jne .up
.flag:
    push ds
    mov bx, 0x40
    mov ds, bx
    test [0x17], ah
    pop ds
    jz .up
    mov ax, 0x8000
    ret
.up:
    xor ax, ax
    ret

section .text
