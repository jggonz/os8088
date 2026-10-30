; =============================================================================
; os8088 - apps/win16/w16bmp.inc
;
; BITMAPS AND MEMORY DEVICE CONTEXTS (SPEC.md 124.15.2): what WinMine 3.10
; draws its cells with - CreateDIBitmap once, a memory DC holding it, and a
; BitBlt a cell. A bitmap is converted ONCE, when it is made, into os8088's
; sixteen colours at 4 bits a pixel, top row first, in a claim of its own, so
; a BitBlt whose source starts on a byte is one OSAPI_GFX_BLIT4 straight from
; the claim. Anything else (an odd column, StretchBlt) is built a row at a
; time into WIN16's band, w16_gbuf.
;
; A memory DC is only ever a SOURCE here: nothing draws into one.
; =============================================================================

W16_BMP0    equ 0x3300          ; a bitmap: W16_BMP0 + slot
W16_BMSTK   equ 0x3180          ; the 1 x 1 stock bitmap a new memory DC holds
W16_MDC0    equ 0x3010          ; a memory DC: W16_MDC0 + slot
W16_HDESKDC equ 0x3003          ; the desktop's DC: read, never drawn on
W16_HDESK   equ 0x1FFC          ; the desktop window (no record of ours)
W16_BMMAX   equ 64000           ; a bitmap's converted bytes, at most

CBM_INIT    equ 4
SRCCOPY_HI  equ 0x00CC          ; the ROPs, by their high word
BLACKNESS_HI equ 0x0000
WHITENESS_HI equ 0x00FF
PATCOPY_HI  equ 0x00F0
RT_BITMAP   equ 2

; -----------------------------------------------------------------------------
; w16_bmslot - AX = a bitmap handle -> BX = its slot * 2. CF=1 not one of ours
; (the stock bitmap included). Keeps the rest.
; -----------------------------------------------------------------------------
w16_bmslot:
    mov bx, ax
    sub bx, W16_BMP0
    jb .no
    cmp bx, W16_NBMP
    jae .no
    shl bx, 1
    cmp word [w16b_seg + bx], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; w16_mdcslot - AX = a DC handle -> BX = its memory DC's slot. CF=1 not one.
; Keeps the rest.
w16_mdcslot:
    mov bx, ax
    sub bx, W16_MDC0
    jb .no
    cmp bx, W16_NMDC
    jae .no
    cmp byte [w16m_on + bx], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; w16_bmnew - AX = width, DX = height -> a new black bitmap: AX = its handle,
; BX = its slot * 2. CF=1 (AX = 0): no slot, too large, or no memory.
; -----------------------------------------------------------------------------
w16_bmnew:
    push cx
    push dx
    push di
    push es
    or ax, ax
    jle .no
    or dx, dx
    jle .no
    xor bx, bx
.find:
    cmp word [w16b_seg + bx], 0
    je .take
    add bx, 2
    cmp bx, 2 * W16_NBMP
    jb .find
    jmp short .no
.take:
    mov [w16b_w + bx], ax
    mov [w16b_h + bx], dx
    inc ax
    shr ax, 1
    mov [w16b_str + bx], ax         ; 4bpp bytes a row
    mul dx                          ; DX:AX = its bytes
    or dx, dx
    jnz .no
    cmp ax, W16_BMMAX
    ja .no
    mov cx, ax
    add ax, 1023
    jc .no
    push cx
    mov cl, 10
    shr ax, cl                      ; KB
    pop cx
    push ax
    call OSAPI_MEM_CLAIM
    pop ax
    jc .no
    mov [w16b_seg + bx], dx
    mov es, dx                      ; black: CBLACK in both halves
    xor di, di
    xor al, al
    cld
    rep stosb
    mov ax, bx
    shr ax, 1
    add ax, W16_BMP0
    clc
    jmp short .out
.no:
    xor ax, ax
    stc
.out:
    pop es
    pop di
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; w16_bmfill - the DIB that w16_dibhdr has read ([w16_dmap] and the rest) and
; whose bits are at [w16_dbits] into bitmap slot BX (* 2), row by row through
; w16_dibrow and the band. Keeps BX.
; -----------------------------------------------------------------------------
w16_bmfill:
    push bx
    push si
    push di
    push es
    xor ax, ax
    mov [w16_rl], ax                ; w16_dibrow's frame: the DIB's top left
    mov [w16_dx0], ax               ;   at row and column 0
    mov [w16_dsx], ax
    mov [w16_dy0], ax
    mov [w16_dstart], ax
    mov ax, [w16_dhgt]
    mov [w16_dlines], ax
    dec ax
    mov [w16_dtop], ax
    mov ax, [w16_dwid]              ; the narrower of the DIB and the bitmap
    cmp ax, [w16b_w + bx]
    jbe .w
    mov ax, [w16b_w + bx]
.w:
    mov [w16_dvw], ax
    or ax, ax
    jz .out
    inc ax
    shr ax, 1
    mov [w16b_n], ax                ; bytes a row converted
    mov [w16b_bm], bx
    xor dx, dx                      ; DX = the row
.row:
    mov bx, [w16b_bm]
    cmp dx, [w16b_h + bx]
    jae .out
    mov di, w16_gbuf
    call w16_dibrow                 ; CF=1: the DIB has not that row
    mov bx, [w16b_bm]               ; (it keeps neither AX nor BX)
    jc .next
    mov ax, [w16b_str + bx]
    push dx
    mul dx
    pop dx
    mov di, ax
    mov es, [w16b_seg + bx]
    mov si, w16_gbuf
    mov cx, [w16b_n]
    cld
    rep movsb
.next:
    inc dx
    jmp short .row
.out:
    pop es
    pop di
    pop si
    pop bx
    ret

; =============================================================================
; As Windows exports them
; =============================================================================

; CreateCompatibleDC (GDI.52) - args: +0 hdc. A memory DC holding the stock
; 1 x 1 bitmap; 0 when four are open.
w16g_cmpdc:
    xor bx, bx
.find:
    cmp byte [w16m_on + bx], 0
    je .take
    inc bx
    cmp bx, W16_NMDC
    jb .find
    xor ax, ax
    ret
.take:
    mov byte [w16m_on + bx], 1
    mov ax, bx
    shl bx, 1
    mov word [w16m_bmp + bx], W16_BMSTK
    add ax, W16_MDC0
    ret

; DeleteDC (GDI.68) - args: +0 hdc. TRUE for a memory DC; its bitmap stays.
w16g_deldc:
    call w16_args
    mov ax, [es:bx]
    call w16_mdcslot
    jc .no
    mov byte [w16m_on + bx], 0
    shl bx, 1
    mov ax, [w16m_bmp + bx]
    call w16_bmsel0
    mov ax, 1
    ret
.no:
    xor ax, ax
    ret

; w16_bmsel0 - AX = a bitmap no memory DC holds any more. Keeps the rest.
w16_bmsel0:
    push bx
    call w16_bmslot
    jc .out
    mov byte [w16b_sel + bx], 0
.out:
    pop bx
    ret

; w16_bmselect - SelectObject into a memory DC: AX = the object, BX = the DC's
; slot -> AX = the bitmap it replaced. A pen, brush or font is not used there:
; it answers the stock object of its kind. Anything else 0.
w16_bmselect:
    push bx
    push si
    mov si, bx
    shl si, 1                       ; SI = the DC's slot * 2
    cmp ax, W16_BMSTK
    je .bm
    call w16_bmslot
    jnc .bm
    mov dx, ax                      ; a made pen or brush?
    sub dx, W16_OBJ0
    jb .stock
    cmp dx, W16_NOBJ
    jae .none
    mov bx, dx
    mov al, [w16_objk + bx]
    mov dx, W16_STOCK + BLACK_PEN
    cmp al, W16K_PEN
    je .kind
    mov dx, W16_STOCK + WHITE_BRUSH
    cmp al, W16K_BRUSH
    je .kind
    jmp short .none
.stock:                             ; a stock one: brushes 0-5, pens 6-8,
    mov dx, ax                      ;   the rest fonts
    sub dx, W16_STOCK
    jb .none
    cmp dx, STOCK_LAST
    ja .none
    mov ax, W16_STOCK + WHITE_BRUSH
    cmp dx, 5
    jbe .out
    mov ax, W16_STOCK + BLACK_PEN
    cmp dx, 8
    jbe .out
    mov ax, W16_STOCK + SYSTEM_FONT
    jmp short .out
.kind:
    mov ax, dx
    jmp short .out
.none:
    xor ax, ax
    jmp short .out
.bm:
    xchg ax, [w16m_bmp + si]        ; AX = the one it replaced
    call w16_bmsel0
    push ax
    mov ax, [w16m_bmp + si]
    call w16_bmslot
    jc .sel
    mov byte [w16b_sel + bx], 1
.sel:
    pop ax
.out:
    pop si
    pop bx
    ret

; w16_bmdelete - DeleteObject on a bitmap: AX = it -> AX = TRUE freed, FALSE
; selected into a memory DC (Windows' answer). CF=1: not a bitmap of ours.
w16_bmdelete:
    push bx
    push dx
    call w16_bmslot
    jc .out
    xor ax, ax
    cmp byte [w16b_sel + bx], 0
    jne .done
    mov dx, [w16b_seg + bx]
    call OSAPI_MEM_FREE
    mov word [w16b_seg + bx], 0
    mov ax, 1
.done:
    clc
.out:
    pop dx
    pop bx
    ret

; w16_bmend - the program has ended: its bitmaps freed, its memory DCs gone
w16_bmend:
    push bx
    push dx
    xor bx, bx
.b:
    mov dx, [w16b_seg + bx]
    or dx, dx
    jz .n
    call OSAPI_MEM_FREE
    mov word [w16b_seg + bx], 0
    mov byte [w16b_sel + bx], 0
.n:
    add bx, 2
    cmp bx, 2 * W16_NBMP
    jb .b
    xor bx, bx
.m:
    mov byte [w16m_on + bx], 0
    inc bx
    cmp bx, W16_NMDC
    jb .m
    pop dx
    pop bx
    ret

; CreateDIBitmap (GDI.442) - args: +0 fuUsage, +2 lpbmi, +6 lpbBits,
; +10 fdwInit, +14 lpbih, +18 hdc
w16g_credib:
    push si
    push di
    call w16_args
    push es
    push bx
    les si, [es:bx + 14]            ; the size, from the header
    mov ax, [es:si + 4]
    mov dx, [es:si + 6]
    cmp word [es:si], 12
    je .core
    mov dx, [es:si + 8]
.core:
    call w16_bmnew                  ; AX = it, BX = its slot
    mov di, bx
    pop bx
    pop es
    jc .out
    test byte [es:bx + 10], CBM_INIT
    jz .out
    cmp word [es:bx], 0             ; DIB_RGB_COLORS
    jne .out
    push ax
    mov ax, [es:bx + 6]
    mov [w16_dbits], ax
    mov ax, [es:bx + 8]
    mov [w16_dbits + 2], ax
    les si, [es:bx + 2]
    call w16_dibhdr
    mov bx, di
    jc .noinit
    call w16_bmfill
.noinit:
    pop ax
.out:
    pop di
    pop si
    ret

; LoadBitmap (USER.175) - args: +0 lpName, +4 hInstance: a bitmap resource,
; made as CreateDIBitmap makes one. A 1-bit one is a monochrome bitmap, its
; 0 and 1 black and white whatever its palette says (SPEC.md 124.15.2)
w16u_loadbmp:
    push si
    push di
    call w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    mov di, w16_rnamb
    call w16_rkey
    mov [w16_rnk], ax
    mov word [w16_rtk], 0x8000 | RT_BITMAP
    call w16_rfind
    or ax, ax
    jz .no
    mov si, ax                      ; SI = the entry
    call w16_rload
    or ax, ax
    jz .no
    call w16_gfind
    jc .rel
    cmp byte [w16_gdisc], 0
    jne .rel
    mov ax, es
    inc ax
    mov es, ax                      ; ES:0 = the BITMAPINFO
    mov [w16b_rs], ax
    push si
    xor si, si
    call w16_dibhdr
    pop si
    jc .rel
    mov ax, [w16_dwid]
    mov dx, [w16_dhgt]
    call w16_bmnew
    jc .rel
    push ax
    cmp byte [w16_dbpp], 1          ; monochrome: black and white
    jne .bits
    push bx
    xor ax, ax
    xor dl, dl
    call w16_rgb2col
    mov [w16_dmap], al
    mov ax, 0xFFFF
    mov dl, 0xFF
    call w16_rgb2col
    mov [w16_dmap + 1], al
    pop bx
.bits:
    mov es, [w16b_rs]               ; the bits follow the header and palette
    mov ax, 2
    cmp byte [w16_dbpp], 1
    je .pn
    mov ax, 16
.pn:
    mul word [w16_dpal]
    add ax, [es:0]
    mov [w16_dbits], ax
    mov [w16_dbits + 2], es
    call w16_bmfill
    pop ax
    jmp short .relx
.rel:
    xor ax, ax
.relx:
    push es                         ; LoadBitmap frees the resource
    mov es, [w16_rtab]
    cmp word [es:si + RN_USAGE], 0
    je .r0
    dec word [es:si + RN_USAGE]
.r0:
    pop es
    jmp short .out
.no:
    xor ax, ax
.out:
    pop di
    pop si
    ret

; GetObject (GDI.82) - args: +0 lpObject, +4 cbBuffer, +6 hObject. A bitmap's
; BITMAP record (14 bytes), cut to cbBuffer; AX = the bytes written
w16g_getobj:
    push si
    push di
    call w16_args
    mov ax, [es:bx + 6]
    mov cx, [es:bx + 4]
    les di, [es:bx]
    push bx
    call w16_bmslot
    mov si, bx
    pop bx
    jc .no
    cmp cx, 14
    jbe .n
    mov cx, 14
.n:
    mov word [w16b_rec], 0          ; bmType
    mov ax, [w16b_w + si]
    mov [w16b_rec + 2], ax          ; bmWidth
    mov ax, [w16b_h + si]
    mov [w16b_rec + 4], ax          ; bmHeight
    mov ax, [w16b_str + si]
    inc ax
    and al, 0xFE
    mov [w16b_rec + 6], ax          ; bmWidthBytes, even
    mov word [w16b_rec + 8], 0x0401 ; bmPlanes 1, bmBitsPixel 4
    cmp byte [w16_mono], 0
    je .bpp
    mov byte [w16b_rec + 9], 1
.bpp:
    xor ax, ax
    mov [w16b_rec + 10], ax         ; bmBits
    mov [w16b_rec + 12], ax
    mov ax, cx
    mov si, w16b_rec
    cld
    rep movsb
    jmp short .out
.no:
    xor ax, ax
.out:
    pop di
    pop si
    ret

; BitBlt (GDI.34) - args: +0 dwRop, +4 ySrc, +6 xSrc, +8 hdcSrc, +10 nHeight,
; +12 nWidth, +14 y, +16 x, +18 hdcDest
w16g_bitblt:
    call w16_args
    mov ax, [es:bx + 4]
    mov [w16b_ys], ax
    mov ax, [es:bx + 6]
    mov [w16b_xs], ax
    mov ax, [es:bx + 10]
    mov [w16b_cy], ax
    mov [w16b_cys], ax
    mov ax, [es:bx + 12]
    mov [w16b_cx], ax
    mov [w16b_cxs], ax
    mov ax, [es:bx + 14]
    mov [w16b_y], ax
    mov ax, [es:bx + 16]
    mov [w16b_x], ax
    mov ax, [es:bx + 8]
    mov cx, [es:bx + 18]
    jmp short w16_blt

; StretchBlt (GDI.35) - args: +0 dwRop, +4 nHeightSrc, +6 nWidthSrc, +8 ySrc,
; +10 xSrc, +12 hdcSrc, +14 nHeight, +16 nWidth, +18 y, +20 x, +22 hdcDest
w16g_stretch:
    call w16_args
    mov ax, [es:bx + 4]
    mov [w16b_cys], ax
    mov ax, [es:bx + 6]
    mov [w16b_cxs], ax
    mov ax, [es:bx + 8]
    mov [w16b_ys], ax
    mov ax, [es:bx + 10]
    mov [w16b_xs], ax
    mov ax, [es:bx + 14]
    mov [w16b_cy], ax
    mov ax, [es:bx + 16]
    mov [w16b_cx], ax
    mov ax, [es:bx + 18]
    mov [w16b_y], ax
    mov ax, [es:bx + 20]
    mov [w16b_x], ax
    mov ax, [es:bx + 12]
    mov cx, [es:bx + 22]
    ; fall into w16_blt

; w16_blt - AX = the source DC, CX = the destination's, ES:BX -> dwRop, and
; [w16b_*] the rectangles. AX = TRUE drawn (or wholly clipped), FALSE not.
w16_blt:
    push si
    push di
    push bp
    cmp cx, W16_HDC                 ; only ever onto the window's DC
    jne .no
    cmp byte [w16_dcon], 0
    je .no
    mov [w16b_src], ax
    mov ax, [w16b_cx]               ; the destination, on the screen
    or ax, ax
    jle .no                         ; (mirroring is not provided)
    mov ax, [w16b_cy]
    or ax, ax
    jle .no
    mov ax, [w16b_x]
    add ax, [w16_ox]
    mov [w16_rl], ax
    mov [w16b_dx0], ax
    add ax, [w16b_cx]
    dec ax
    mov [w16_rr], ax
    mov ax, [w16b_y]
    add ax, [w16_oy]
    mov [w16_rt], ax
    mov [w16b_dy0], ax
    add ax, [w16b_cy]
    dec ax
    mov [w16_rb], ax
    mov ax, [es:bx + 2]             ; the ROP
    cmp ax, SRCCOPY_HI
    je .copy
    mov dl, CBLACK
    cmp ax, BLACKNESS_HI
    je .fill
    mov dl, CWHITE
    cmp ax, WHITENESS_HI
    je .fill
    cmp ax, PATCOPY_HI
    jne .no
    mov ax, [w16_brush]
    call w16_brcol
    mov dl, al
    cmp dl, W16_NOCOL
    je .yes
.fill:
    mov [w16_gcol], dl
    mov ax, [w16_rl]
    mov bx, [w16_rt]
    mov cx, [w16_rr]
    mov dx, [w16_rb]
    call w16_gfill
    jmp .yes
.copy:
    mov ax, [w16b_src]              ; the source: a memory DC's bitmap
    call w16_mdcslot
    jc .no
    shl bx, 1
    mov ax, [w16m_bmp + bx]
    cmp ax, W16_BMSTK
    je .yes                         ; the stock 1 x 1: nothing to copy
    call w16_bmslot
    jc .no
    mov [w16b_bm], bx
    mov ax, [w16b_cxs]
    or ax, ax
    jle .no
    mov ax, [w16b_cys]
    or ax, ax
    jle .no
    call w16_cliprect               ; the destination cut to the clip
    jc .yes
    mov ax, [w16b_cx]               ; the same size: a copy
    cmp ax, [w16b_cxs]
    jne .stretch
    mov ax, [w16b_cy]
    cmp ax, [w16b_cys]
    jne .stretch
    call w16_bmcopy
    jmp short .yes
.stretch:
    call w16_bmscale
.yes:
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    pop bp
    pop di
    pop si
    ret

; w16_bmcopy - a same-size SRCCOPY: [w16_rl]..[w16_rb] (cut) from bitmap
; [w16b_bm] at [w16b_xs], [w16b_ys] for the uncut [w16b_dx0], [w16b_dy0].
; The source cut to the bitmap as well. An even source column is one BLIT4
; from the claim; an odd one goes through w16_bmscale.
w16_bmcopy:
    mov bx, [w16b_bm]
    mov ax, [w16_rl]                ; the source's left for the cut left
    sub ax, [w16b_dx0]
    add ax, [w16b_xs]
    jns .l
    sub [w16_rl], ax                ; left of the bitmap: cut
    xor ax, ax
.l:
    mov [w16b_sx], ax
    mov ax, [w16_rt]
    sub ax, [w16b_dy0]
    add ax, [w16b_ys]
    jns .t
    sub [w16_rt], ax
    xor ax, ax
.t:
    mov [w16b_sy], ax
    mov ax, [w16b_w + bx]           ; right of the bitmap: cut
    sub ax, [w16b_sx]
    jle .none
    add ax, [w16_rl]
    dec ax
    cmp ax, [w16_rr]
    jge .r
    mov [w16_rr], ax
.r:
    mov ax, [w16b_h + bx]
    sub ax, [w16b_sy]
    jle .none
    add ax, [w16_rt]
    dec ax
    cmp ax, [w16_rb]
    jge .b
    mov [w16_rb], ax
.b:
    mov ax, [w16_rl]
    cmp ax, [w16_rr]
    jg .none
    mov ax, [w16_rt]
    cmp ax, [w16_rb]
    jg .none
    test byte [w16b_sx], 1
    jnz .odd
    mov ax, [w16b_str + bx]         ; on a byte: straight from the claim
    mov bp, ax
    mul word [w16b_sy]
    mov si, [w16b_sx]
    shr si, 1
    add si, ax
    push es
    mov es, [w16b_seg + bx]
    mov ax, [w16_rl]
    mov bx, [w16_rt]
    mov cx, [w16_rr]
    sub cx, ax
    inc cx
    mov dx, [w16_rb]
    sub dx, bx
    inc dx
    call OSAPI_GFX_BLIT4
    pop es
.none:
    ret
.odd:                               ; through the band: w16_bmscale's frame,
    mov ax, [w16b_sx]               ;   a scale of one, from the cut corner
    mov [w16b_xs], ax
    mov ax, [w16b_sy]
    mov [w16b_ys], ax
    mov ax, [w16_rl]
    mov [w16b_dx0], ax
    mov ax, [w16_rt]
    mov [w16b_dy0], ax
    mov ax, [w16_rr]
    sub ax, [w16_rl]
    inc ax
    mov [w16b_cx], ax
    mov [w16b_cxs], ax
    mov ax, [w16_rb]
    sub ax, [w16_rt]
    inc ax
    mov [w16b_cy], ax
    mov [w16b_cys], ax
    ; fall into w16_bmscale

; w16_bmscale - [w16_rl]..[w16_rb] (cut) as the [w16b_cx] x [w16b_cy] block at
; [w16b_dx0], [w16b_dy0], each pixel the source pixel it falls on in the
; [w16b_cxs] x [w16b_cys] block at [w16b_xs], [w16b_ys] of bitmap [w16b_bm]
; (a source pixel off the bitmap is its nearest edge's). Rows into the band,
; one BLIT4 a band.
w16_bmscale:
    mov ax, [w16_rr]
    sub ax, [w16_rl]
    inc ax
    mov [w16b_n], ax                ; pixels a row
    inc ax
    shr ax, 1
    mov [w16b_ow], ax               ; the band's stride
    mov bx, ax
    mov ax, W16_GBUF
    xor dx, dx
    div bx
    or ax, ax
    jz .done
    mov [w16b_band], ax
    mov ax, [w16_rl]                ; the first column's source, and the
    sub ax, [w16b_dx0]              ;   error the DDA carries from it
    mul word [w16b_cxs]
    div word [w16b_cx]
    add ax, [w16b_xs]
    mov [w16b_sx0], ax
    mov [w16b_err0], dx
    mov dx, [w16_rt]                ; DX = the screen row
.band:
    cmp dx, [w16_rb]
    jg .done
    mov [w16b_by], dx
    mov di, w16_gbuf
    xor cx, cx                      ; rows in this band
.brow:
    cmp dx, [w16_rb]
    jg .put
    cmp cx, [w16b_band]
    jae .put
    call w16_bmrow
    add di, [w16b_ow]
    inc dx
    inc cx
    jmp short .brow
.put:
    push dx
    push es
    push ds
    pop es
    mov si, w16_gbuf
    mov bp, [w16b_ow]
    mov ax, [w16_rl]
    mov bx, [w16b_by]
    mov dx, cx
    mov cx, [w16b_n]
    call OSAPI_GFX_BLIT4
    pop es
    pop dx
    jmp short .band
.done:
    ret

; w16_bmrow - screen row DX of w16_bmscale's block into the band at DS:DI.
; Keeps CX, DX, DI.
w16_bmrow:
    push cx
    push dx
    push di
    push es
    mov ax, dx                      ; the source row
    sub ax, [w16b_dy0]
    mul word [w16b_cys]
    div word [w16b_cy]
    add ax, [w16b_ys]
    mov bx, [w16b_bm]
    call w16_bmclamp_h
    mul word [w16b_str + bx]
    mov si, ax                      ; SI = the source row's first byte
    mov es, [w16b_seg + bx]
    mov ax, [w16b_sx0]
    mov [w16b_sx], ax
    mov ax, [w16b_err0]
    mov [w16b_err], ax
    mov cx, [w16b_n]
    xor dh, dh                      ; DH: 0 the next pixel is a byte's high
.px:                                ;   half, 1 its low
    mov ax, [w16b_sx]
    or ax, ax
    jns .c0
    xor ax, ax
.c0:
    cmp ax, [w16b_w + bx]
    jb .c1
    mov ax, [w16b_w + bx]
    dec ax
.c1:
    push bx
    mov bx, ax
    shr bx, 1
    mov dl, [es:si + bx]
    pop bx
    test al, 1
    jnz .lo
    shr dl, 1
    shr dl, 1
    shr dl, 1
    shr dl, 1
.lo:
    and dl, 15
    or dh, dh
    jnz .second
    shl dl, 1
    shl dl, 1
    shl dl, 1
    shl dl, 1
    mov [di], dl
    inc dh
    jmp short .step
.second:
    or [di], dl
    inc di
    xor dh, dh
.step:
    mov ax, [w16b_err]              ; the DDA: cxs / cx of a source pixel
    add ax, [w16b_cxs]
.carry:
    cmp ax, [w16b_cx]
    jb .stepd
    sub ax, [w16b_cx]
    inc word [w16b_sx]
    jmp short .carry
.stepd:
    mov [w16b_err], ax
    loop .px
    pop es
    pop di
    pop dx
    pop cx
    ret

; w16_bmclamp_h - AX = a source row -> AX inside bitmap BX's rows
w16_bmclamp_h:
    or ax, ax
    jns .t
    xor ax, ax
.t:
    cmp ax, [w16b_h + bx]
    jb .out
    mov ax, [w16b_h + bx]
    dec ax
.out:
    ret
