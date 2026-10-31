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
PATINVERT_HI equ 0x005A         ; (PatBlt's, SPEC.md 124.17.1)
DSTINVERT_HI equ 0x0055
RT_BITMAP   equ 2

; -----------------------------------------------------------------------------
; w16_bmslot - AX = a bitmap handle -> BX = its slot * 2. CF=1 not one of ours
; (the stock bitmap included). Keeps the rest.
; -----------------------------------------------------------------------------
; THE CODE IS THE LIBRARY PART'S (SPEC.md 124.19.3): moved out of the image
; in S14 - a call into the image is ICALL, and its data stays in .text
section .w16dlg
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
    mov byte [w16b_mono + bx], 0
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
    push cx                         ; a FIXED block of the global heap, not a
    xor dx, dx                      ;   claim of its own: an owner's claims
    xor cl, cl                      ;   are eight, and Solitaire's bitmaps
    ICALL w16_gnew                  ;   alone are four (SPEC.md 124.20.2).
    pop cx                          ;   The handle IS the segment
    or ax, ax
    jz .no
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
    ICALL w16_dibrow                 ; CF=1: the DIB has not that row
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
W16LIB w16g_cmpdc
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
    mov word [w16m_brush + bx], W16_STOCK + WHITE_BRUSH
    add ax, W16_MDC0
    ret

; DeleteDC (GDI.68) - args: +0 hdc. TRUE for a memory DC; its bitmap stays.
W16LIB w16g_deldc
    ICALL w16_args
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
    mov dl, [w16_objk + bx]
    cmp dl, W16K_BRUSH
    je .brush
    mov dx, W16_STOCK + BLACK_PEN
    cmp dl, W16K_PEN
    je .kind
    jmp short .none
.stock:                             ; a stock one: brushes 0-5, pens 6-8,
    mov dx, ax                      ;   the rest fonts
    sub dx, W16_STOCK
    jb .none
    cmp dx, STOCK_LAST
    ja .none
    cmp dx, 5
    jbe .brush
    mov ax, W16_STOCK + BLACK_PEN
    cmp dx, 8
    jbe .out
    mov ax, W16_STOCK + SYSTEM_FONT
    jmp short .out
.kind:
    mov ax, dx
    jmp short .out
.brush:                             ; a brush is kept: PatBlt into the bitmap
    xchg ax, [w16m_brush + si]      ;   paints with it (SPEC.md 124.20.4)
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
    mov ax, [w16b_seg + bx]         ; the block's handle is its segment
    ICALL w16_gfreeh
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
    mov dx, [w16b_seg + bx]         ; the global heap's arenas go with the
    or dx, dx                       ;   program, and the bitmaps in them
    jz .n
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
W16LIB w16g_credib
    push si
    push di
    ICALL w16_args
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
    ICALL w16_dibhdr
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
; made as CreateDIBitmap makes one, a 1-bit one in its palette's colours
; (SPEC.md 124.15.2). hInstance NULL is one of Windows' own (OBM_*), WIN16's
; drawing of it out of the icon part (124.22.7).
W16LIB w16u_loadbmp
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    cmp word [es:bx + 4], 0
    jne .prog
    or dx, dx
    jnz .no0
    call w16_obm                    ; ES:SI = it
    jc .no0
    call w16_dibmake
    jmp short .out0
.no0:
    xor ax, ax
.out0:
    pop di
    pop si
    ret
.prog:
    mov di, w16_rnamb
    ICALL w16_rkey
    mov [w16_rnk], ax
    mov word [w16_rtk], 0x8000 | RT_BITMAP
    ICALL w16_rfind
    or ax, ax
    jz .no
    mov di, ax                      ; DI = the entry: w16_gfind answers SI,
    ICALL w16_rload                 ;   so the entry may not ride in it
    or ax, ax
    jz .no
    ICALL w16_gfind
    jc .rel
    cmp byte [w16_gdisc], 0
    jne .rel
    mov ax, es
    inc ax
    mov es, ax                      ; ES:0 = the BITMAPINFO
    xor si, si
    call w16_dibmake
    jmp short .relx
.rel:
    xor ax, ax
.relx:
    push es                         ; LoadBitmap frees the resource
    mov es, [w16_rtab]
    cmp word [es:di + RN_USAGE], 0
    je .r0
    dec word [es:di + RN_USAGE]
.r0:
    pop es
    jmp short .out
.no:
    xor ax, ax
.out:
    pop di
    pop si
    ret

; w16_dibmake - ES:SI = a packed DIB (a header, its palette, its bits): a
; bitmap made of it, a 1-bit one keeping its palette's two colours (SPEC.md
; 124.15.2) -> AX = its handle, 0 none. Clobbers BX, CX, DX, SI.
w16_dibmake:
    mov [w16b_rs], es
    push si
    ICALL w16_dibhdr
    pop si
    jc .no
    mov ax, [w16_dwid]
    mov dx, [w16_dhgt]
    push si
    call w16_bmnew
    pop si
    jc .no
    push ax
    mov es, [w16b_rs]               ; the bits follow the header and palette
    mov ax, 2
    cmp byte [w16_dbpp], 1
    je .pn
    mov ax, 16
.pn:
    mul word [w16_dpal]
    add ax, [es:si]
    add ax, si
    mov [w16_dbits], ax
    mov [w16_dbits + 2], es
    call w16_bmfill
    pop ax
    ret
.no:
    xor ax, ax
    ret

; w16_obm - AX = an OBM_* id -> ES:SI = WIN16's drawing of that bitmap, in
; the icon part (124.22.7). CF=1 not one WIN16 draws. Keeps AX, BX, CX, DX.
w16_obm:
    push cx
    call w16_icpart
    jc .out
    mov si, [es:0]                  ; past the icons' table: the bitmaps'
    shl si, 1
    add si, 2
    mov cx, [es:si]
    add si, 2
    jcxz .no
.e:
    cmp [es:si], ax
    je .hit
    add si, 4
    loop .e
.no:
    stc
    jmp short .out
.hit:
    mov si, [es:si + 2]
    clc
.out:
    pop cx
    ret

; GetObject (GDI.82) - args: +0 lpObject, +4 cbBuffer, +6 hObject. A bitmap's
; BITMAP record (14 bytes), cut to cbBuffer; AX = the bytes written
W16LIB w16g_getobj
    push si
    push di
    ICALL w16_args
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
    cmp byte [w16b_mono + si], 0    ; made 1bpp: as it was made (124.22.11)
    je .mono
    mov ax, [w16b_rec + 2]
    add ax, 15
    shr ax, 1
    shr ax, 1
    shr ax, 1
    and al, 0xFE
    mov [w16b_rec + 6], ax          ; bmWidthBytes, a word a 16 pixels
    jmp short .one
.mono:
    cmp byte [w16_mono], 0
    je .bpp
.one:
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
W16LIB w16g_bitblt
    ICALL w16_args
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
W16LIB w16g_stretch
    ICALL w16_args
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
    cmp cx, W16_HDC                 ; the window's DC, or a memory DC's
    je .scr                         ;   bitmap (SPEC.md 124.20.4)
    call w16_bltm
    jmp .out
.scr:
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
    mov dl, W16_XOR
    cmp ax, DSTINVERT_HI
    je .fill
    cmp ax, PATINVERT_HI            ; a black brush leaves it; any other
    jne .pat                        ;   inverts, as a white one does
    mov ax, [w16_brush]
    ICALL w16_brcol
    cmp al, W16_NOCOL
    je .yes
    cmp al, CBLACK
    je .yes
    jmp short .fill
.pat:
    cmp ax, PATCOPY_HI
    jne .srcrop
    mov ax, [w16_brush]
    ICALL w16_brcol
    mov dl, al
    cmp dl, W16_NOCOL
    je .yes
.fill:
    mov [w16_gcol], dl
    mov ax, [w16_rl]
    mov bx, [w16_rt]
    mov cx, [w16_rr]
    mov dx, [w16_rb]
    ICALL w16_gfill
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
    ICALL w16_cliprect               ; the destination cut to the clip
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
.srcrop:                            ; a source ROP the pattern plays no part
    mov dl, al                      ;   in: its index byte's two halves agree
    and dl, 0x0F
    mov dh, al
    shr dh, 1
    shr dh, 1
    shr dh, 1
    shr dh, 1
    cmp dl, dh
    jne .no
    mov [w16b_ropb], al
    mov ax, [w16b_src]
    call w16_mdcslot
    jc .no
    shl bx, 1
    mov ax, [w16m_bmp + bx]
    cmp ax, W16_BMSTK
    je .yes
    call w16_bmslot
    jc .no
    mov [w16b_bm], bx
    mov ax, [w16b_cx]               ; the same size (a stretch under another
    cmp ax, [w16b_cxs]              ;   ROP is not provided)
    jne .no
    mov ax, [w16b_cy]
    cmp ax, [w16b_cys]
    jne .no
    ICALL w16_cliprect
    jc .yes
    call w16_ropscr
    jmp short .yes

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


; =============================================================================
; Drawing INTO a memory DC (SPEC.md 124.20.4): Solitaire's drag is five
; SRCCOPYs a step through three bitmaps - the screen into one, one into
; another, and two back out. A bitmap is 4 bits a pixel, high nibble left.
; =============================================================================

; w16_bltm - w16_blt's when the destination is not the window's DC: AX = the
; source DC, CX = the destination, ES:BX -> dwRop, [w16b_*] the rectangles in
; the two DCs' own coordinates. AX = TRUE drawn (or wholly cut), FALSE not.
w16_bltm:
    mov [w16b_src], ax
    mov dx, [es:bx + 2]             ; DX = the ROP, by its high word
    mov ax, cx
    call w16_mdcslot
    jc .no
    shl bx, 1
    mov ax, [w16m_brush + bx]       ; its own brush, for the pattern ROPs
    mov [w16b_mbr], ax
    mov ax, [w16m_bmp + bx]
    cmp ax, W16_BMSTK
    je .yes                         ; the stock 1 x 1: nothing to draw on
    call w16_bmslot
    jc .no
    mov [w16b_dbm], bx
    mov ax, [w16b_cx]
    or ax, ax
    jle .no                         ; (mirroring is not provided)
    mov ax, [w16b_cy]
    or ax, ax
    jle .no
    mov ax, [w16b_x]                ; the destination, cut to the bitmap
    mov [w16b_dx0], ax
    or ax, ax
    jns .l
    xor ax, ax
.l:
    mov [w16_rl], ax
    mov ax, [w16b_y]
    mov [w16b_dy0], ax
    or ax, ax
    jns .t
    xor ax, ax
.t:
    mov [w16_rt], ax
    mov ax, [w16b_x]
    add ax, [w16b_cx]
    cmp ax, [w16b_w + bx]
    jle .r
    mov ax, [w16b_w + bx]
.r:
    dec ax
    mov [w16_rr], ax
    mov ax, [w16b_y]
    add ax, [w16b_cy]
    cmp ax, [w16b_h + bx]
    jle .b
    mov ax, [w16b_h + bx]
.b:
    dec ax
    mov [w16_rb], ax
    cmp dx, SRCCOPY_HI
    je .copy
    mov al, CBLACK
    cmp dx, BLACKNESS_HI
    je .fill
    mov al, CWHITE
    cmp dx, WHITENESS_HI
    je .fill
    mov al, W16_XOR
    cmp dx, DSTINVERT_HI
    je .fill
    cmp dx, PATINVERT_HI            ; a black brush leaves it; any other
    jne .pat                        ;   inverts, as on the screen
    mov ax, [w16b_mbr]
    ICALL w16_brcol
    cmp al, W16_NOCOL
    je .yes
    cmp al, CBLACK
    je .yes
    mov al, W16_XOR
    jmp short .fill
.pat:
    cmp dx, PATCOPY_HI
    jne .no
    mov ax, [w16b_mbr]
    ICALL w16_brcol
    cmp al, W16_NOCOL
    je .yes
.fill:
    cmp al, W16_GRAY                ; a pattern brush's checker: its grey
    jne .f
    mov al, 7
.f:
    mov [w16b_dfc], al
    call w16_bmfillr
    jmp short .yes
.copy:
    mov ax, [w16b_cx]               ; the same size, or nothing (a stretch
    cmp ax, [w16b_cxs]              ;   into a bitmap is not provided)
    jne .no
    mov ax, [w16b_cy]
    cmp ax, [w16b_cys]
    jne .no
    mov ax, [w16b_src]
    cmp ax, W16_HDC
    jne .frommem
    call w16_scr2bm
    jmp short .yes
.frommem:
    call w16_mdcslot
    jc .no
    shl bx, 1
    mov ax, [w16m_bmp + bx]
    cmp ax, W16_BMSTK
    je .yes
    call w16_bmslot
    jc .no
    mov [w16b_bm], bx
    call w16_bm2bm
.yes:
    mov ax, 1
    ret
.no:
    xor ax, ax
    ret

; w16_bmcut - the source corner for the cut destination, cut again to the
; source's extent: [w16b_sx]/[w16b_sy] = the source's column and row for
; [w16_rl]/[w16_rt], AX/DX = the source's width and height. [w16_rl]..[w16_rb]
; shrink to match. CF=1: nothing is left.
w16_bmcut:
    push ax
    mov ax, [w16_rl]
    sub ax, [w16b_dx0]
    add ax, [w16b_xs]
    jns .l
    sub [w16_rl], ax
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
    pop ax
    sub ax, [w16b_sx]               ; the source's columns left
    jle .none
    add ax, [w16_rl]
    dec ax
    cmp ax, [w16_rr]
    jge .r
    mov [w16_rr], ax
.r:
    mov ax, dx
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
    clc
    ret
.none:
    stc
    ret

; w16_bmaddr - AX = a column, DX = a row of bitmap BX (* 2) -> ES:DI = its
; byte, CF = 1 when the pixel is the byte's low nibble. Keeps BX, CX, SI.
w16_bmaddr:
    push ax
    mov ax, [w16b_str + bx]
    mul dx
    mov di, ax
    pop ax
    mov dx, ax
    shr ax, 1
    add di, ax
    mov es, [w16b_seg + bx]
    shr dl, 1                       ; CF = the column's low bit
    ret

; w16_bmfillr - [w16_rl]..[w16_rb] of bitmap [w16b_dbm] filled with colour
; [w16b_dfc], or inverted when it is W16_XOR. Cut already.
w16_bmfillr:
    mov al, [w16b_dfc]
    cmp al, W16_XOR
    jne .c
    mov al, 0xFF                    ; inverting: every bit of both halves
    jmp short .go
.c:
    mov ah, al
    mov cl, 4
    shl ah, cl
    or al, ah                       ; AL = the colour in both halves
.go:
    mov [w16b_fpat], al
    mov dx, [w16_rt]
.row:
    cmp dx, [w16_rb]
    jg .out
    push dx
    mov bx, [w16b_dbm]
    mov ax, [w16_rl]
    call w16_bmaddr                 ; ES:DI, CF = a low nibble first
    sbb bl, bl
    mov cx, [w16_rr]
    sub cx, [w16_rl]
    inc cx                          ; CX = pixels
    mov al, [w16b_fpat]
    or bl, bl
    jz .mid
    mov ah, 0x0F
    call w16_nibput
    inc di
    dec cx
.mid:
    mov dx, cx
    shr cx, 1
    jcxz .tail
.byte:
    cmp byte [w16b_dfc], W16_XOR
    jne .st
    xor [es:di], al
    jmp short .nx
.st:
    mov [es:di], al
.nx:
    inc di
    loop .byte
.tail:
    test dl, 1
    jz .next
    mov ah, 0xF0                    ; the last pixel a high nibble
    call w16_nibput
.next:
    pop dx
    inc dx
    jmp short .row
.out:
    ret

; w16_nibput - AL = the colour in both halves (FFh: inverting), AH = the half
; of [ES:DI] to write. Keeps every register.
w16_nibput:
    push ax
    and al, ah
    cmp byte [w16b_dfc], W16_XOR
    je .x
    not ah
    and [es:di], ah
    or [es:di], al
    pop ax
    ret
.x:
    xor [es:di], al
    pop ax
    ret

; w16_bm2bm - a same-size SRCCOPY from bitmap [w16b_bm] into [w16b_dbm], the
; destination cut already and the source cut here. Rows top first, so a
; bitmap copied onto itself overlapping downward is not provided.
w16_bm2bm:
    mov bx, [w16b_bm]
    mov ax, [w16b_w + bx]
    mov dx, [w16b_h + bx]
    call w16_bmcut
    jc .out
    mov dx, [w16_rt]
.row:
    cmp dx, [w16_rb]
    jg .out
    push dx
    mov bx, [w16b_dbm]
    mov ax, [w16_rl]
    call w16_bmaddr                 ; ES:DI, CF = the destination starts low
    sbb bl, bl
    and bl, 1
    pop dx
    push dx
    mov ax, dx                      ; the source row
    sub ax, [w16_rt]
    add ax, [w16b_sy]
    push bx
    mov bx, [w16b_bm]
    mul word [w16b_str + bx]
    mov cx, [w16b_sx]
    mov si, cx
    shr si, 1
    add si, ax
    mov ax, [w16b_seg + bx]
    pop bx
    mov bh, cl
    and bh, 1                       ; BH = 1: the source starts low
    mov cx, [w16_rr]
    sub cx, [w16_rl]
    inc cx
    push ds
    mov ds, ax                      ; DS:SI = the source, ES:DI the destination
    call w16_nibrow
    pop ds
    pop dx
    inc dx
    jmp short .row
.out:
    ret

; w16_nibrow - CX pixels (1 or more) from DS:SI to ES:DI, BH = 1 when the
; source's first is a low nibble and BL = 1 the destination's. Byte moves
; when the two agree, a nibble at a time when they do not. Takes AX, CX, SI,
; DI, BX.
w16_nibrow:
    cld
    cmp bh, bl
    jne .n
    or bl, bl
    jz .even
    mov al, [si]                    ; both low: the first pixel alone
    and al, 0x0F
    and byte [es:di], 0xF0
    or [es:di], al
    inc si
    inc di
    dec cx
    jz .ret
.even:
    mov ax, cx
    shr cx, 1
    rep movsb
    test al, 1
    jz .ret
    mov al, [si]                    ; and the last, a high nibble
    and al, 0xF0
    and byte [es:di], 0x0F
    or [es:di], al
.ret:
    ret
.n:
    mov al, [si]
    or bh, bh
    jnz .slo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    jmp short .sg
.slo:
    and al, 0x0F
    inc si
.sg:
    xor bh, 1
    call w16_nibst
    loop .n
    ret

; w16_nibst - AL = a pixel (0-15) to ES:DI's half BL says (1 low, then DI
; moves on), BL toggled. Keeps the rest.
w16_nibst:
    or bl, bl
    jnz .lo
    push ax
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    and byte [es:di], 0x0F
    or [es:di], al
    pop ax
    jmp short .t
.lo:
    and byte [es:di], 0xF0
    or [es:di], al
    inc di
.t:
    xor bl, 1
    ret

; w16_scr2bm - a same-size SRCCOPY from the window's DC into bitmap
; [w16b_dbm]: the screen read back with OSAPI_GFX_SAVE a band at a time (a
; byte a plane a column of 8, plane 0 first) and each pixel's plane bits made
; its colour. Nothing on a hidden DC or a VESA display, whose save is the
; driver's own format.
w16_scr2bm:
    cmp byte [w16_dcon], 0
    je .out
    cmp byte [w16_dchide], 0
    jne .out
    push bx
    mov bx, [w16_win]
    call OSAPI_WM_DISPLAY           ; AX/BX = the size, DL kind, DH planes
    mov cx, bx
    pop bx
    cmp dl, VID_KIND_VESA
    je .out
    or dh, dh
    jz .out
    mov [w16b_pl], dh
    mov dx, [w16_ox]                ; the source in screen coordinates
    add [w16b_xs], dx
    mov dx, [w16_oy]
    add [w16b_ys], dx
    mov dx, cx
    call w16_bmcut                  ; cut to the screen as well
    jc .out
    mov ax, [w16b_sx]
    mov [w16b_sxs], ax
    add ax, [w16_rr]
    sub ax, [w16_rl]
    mov [w16b_sxe], ax
    mov cl, 3
    shr ax, cl
    mov dx, [w16b_sxs]
    shr dx, cl
    sub ax, dx
    inc ax
    mov [w16b_bw], ax               ; bytes a row, a plane
    mul byte [w16b_pl]
    mov bx, ax                      ; BX = bytes a row, all planes
    mov ax, W16_GBUF
    xor dx, dx
    div bx
    or ax, ax
    jz .out
    mov [w16b_band], ax
    mov dx, [w16_rt]
.band:
    cmp dx, [w16_rb]
    jg .out
    mov [w16b_by], dx
    mov cx, [w16_rb]                ; rows this band
    sub cx, dx
    inc cx
    cmp cx, [w16b_band]
    jbe .n
    mov cx, [w16b_band]
.n:
    mov [w16b_n], cx
    push dx
    mov ax, [w16b_sxs]
    mov bx, dx
    sub bx, [w16_rt]
    add bx, [w16b_sy]
    mov dx, bx
    add dx, cx
    dec dx
    mov cx, [w16b_sxe]
    push ds
    pop es
    mov di, w16_gbuf
    call OSAPI_GFX_SAVE
    pop dx
    jc .next
    xor cx, cx                      ; CX = the band's row
.brow:
    cmp cx, [w16b_n]
    jae .next
    push cx
    push dx
    call w16_scrrow
    pop dx
    pop cx
    inc cx
    inc dx
    jmp short .brow
.next:
    mov dx, [w16b_by]
    add dx, [w16b_n]
    jmp short .band
.out:
    ret

; w16_scrrow - the band's row CX into bitmap [w16b_dbm]'s row DX, from column
; [w16_rl] on
w16_scrrow:
    push bp
    push dx                         ; (the multiplies take DX)
    mov ax, [w16b_bw]
    mul cx
    mov si, ax
    add si, w16_gbuf                ; SI = plane 0's byte for this row
    mov ax, [w16b_bw]
    mul word [w16b_n]
    mov bp, ax                      ; BP = one plane's bytes in the band
    pop dx
    mov bx, [w16b_dbm]
    mov ax, [w16_rl]
    call w16_bmaddr                 ; ES:DI, CF = a low nibble first
    sbb bl, bl
    and bl, 1
    mov cx, [w16_rr]
    sub cx, [w16_rl]
    inc cx
    call w16_scrpix
    pop bp
    ret

; w16_scrpix - CX pixels of a saved screen row (DS:SI = plane 0's byte of
; its first, BP = one plane's bytes, [w16b_sxs] its first column, [w16b_pl]
; the planes) as colours to ES:DI from the half BL says (1 low)
w16_scrpix:
    mov cl, [w16b_sxs]
    and cl, 7
    mov ah, 0x80
    shr ah, cl                      ; AH = the first pixel's bit
    mov dl, [w16b_pl]
    mov cx, [w16_rr]
    sub cx, [w16_rl]
    inc cx
.px:
    xor al, al
    mov dh, dl
    mov bh, 1
    push si
.pl:
    test [si], ah
    jz .z
    or al, bh
.z:
    shl bh, 1
    add si, bp
    dec dh
    jnz .pl
    pop si
    cmp dl, 1                       ; one plane: lit is white
    jne .put
    neg al
    and al, 15
.put:
    call w16_nibst
    ror ah, 1
    jnc .same
    inc si
.same:
    loop .px
    ret

; -----------------------------------------------------------------------------
; The other source ROPs onto the screen (SPEC.md 124.20.4): Solitaire's empty
; piles are a monochrome outline put down with SRCAND. A ROP whose answer
; does not depend on the pattern is a truth table over (source bit, screen
; bit), applied to each colour's four bits as Windows' VGA driver applied it
; to each plane: the screen row is read back, combined and put down again.
; -----------------------------------------------------------------------------
W16_ROPROW  equ 400                 ; the combined row, in w16_gbuf past the
                                    ;   saved planes (4 x 92 bytes at 720 px)

; w16_roptab - [w16b_ropb] (the ROP's index byte) -> w16_ropt: the answer
; for each source colour * 16 + screen colour
w16_roptab:
    push ds
    pop es
    mov di, w16_ropt
    xor dx, dx                      ; DL = source * 16 + screen
.e:
    mov bh, dl
    shr bh, 1
    shr bh, 1
    shr bh, 1
    shr bh, 1                       ; BH = the source
    mov bl, dl
    and bl, 15                      ; BL = the screen
    xor al, al
    mov ah, 1                       ; AH = the bit
.b:
    xor cl, cl                      ; the table's index: S * 2 + D
    test bh, ah
    jz .s0
    mov cl, 2
.s0:
    test bl, ah
    jz .d0
    inc cl
.d0:
    mov ch, [w16b_ropb]
    shr ch, cl
    test ch, 1
    jz .z
    or al, ah
.z:
    shl ah, 1
    cmp ah, 16
    jb .b
    stosb
    inc dl
    jnz .e
    ret

; w16_ropscr - from bitmap [w16b_bm] onto the screen under ROP [w16b_ropb]:
; [w16_rl]..[w16_rb] the destination cut to the clip, [w16b_dx0]/[w16b_dy0]
; its uncut corner, [w16b_xs]/[w16b_ys] the source's
w16_ropscr:
    call w16_roptab
    mov bx, [w16b_bm]
    mov ax, [w16b_w + bx]
    mov dx, [w16b_h + bx]
    call w16_bmcut
    jc .out
    mov bx, [w16_win]
    call OSAPI_WM_DISPLAY           ; DL = the kind, DH = the planes
    cmp dl, VID_KIND_VESA
    je .out
    or dh, dh
    jz .out
    mov [w16b_pl], dh
    mov ax, [w16_rl]
    mov [w16b_sxs], ax
    mov cl, 3
    mov dx, ax
    shr dx, cl
    mov ax, [w16_rr]
    shr ax, cl
    sub ax, dx
    inc ax
    mov [w16b_bw], ax               ; one plane's bytes for the row
    mov dx, [w16_rt]
.row:
    cmp dx, [w16_rb]
    jg .out
    push dx
    mov ax, [w16_rl]                ; the screen's row, read back
    mov bx, dx
    mov cx, [w16_rr]
    push ds
    pop es
    mov di, w16_gbuf
    call OSAPI_GFX_SAVE
    jc .next
    push bp
    mov si, w16_gbuf                ; ...as colours, into the row buffer
    mov di, w16_gbuf + W16_ROPROW
    mov bp, [w16b_bw]
    xor bl, bl
    call w16_scrpix
    pop bp
    pop dx
    push dx
    call w16_roprow                 ; ...combined with the source
    push bp
    push ds
    pop es
    mov si, w16_gbuf + W16_ROPROW
    mov cx, [w16_rr]
    sub cx, [w16_rl]
    inc cx
    mov bp, cx
    inc bp
    shr bp, 1
    mov ax, [w16_rl]
    mov bx, dx
    mov dx, 1
    call OSAPI_GFX_BLIT4            ; ...and put down
    pop bp
.next:
    pop dx
    inc dx
    jmp short .row
.out:
    ret

; w16_roprow - the row buffer (screen row DX's colours) combined, pixel by
; pixel through w16_ropt, with the source row that lands on it
w16_roprow:
    push dx
    mov ax, dx                      ; the source row
    sub ax, [w16_rt]
    add ax, [w16b_ys]
    sub ax, [w16b_dy0]
    add ax, [w16_rt]
    mov bx, [w16b_bm]
    mul word [w16b_str + bx]
    mov cx, [w16b_sx]
    mov si, cx
    shr si, 1
    add si, ax
    mov es, [w16b_seg + bx]
    mov dh, cl
    and dh, 1                       ; DH = 1: the source starts low
    xor dl, dl                      ; DL = 1: the row buffer's low half
    mov di, w16_gbuf + W16_ROPROW
    mov cx, [w16_rr]
    sub cx, [w16_rl]
    inc cx
.px:
    mov al, [es:si]                 ; AL = the source's colour
    or dh, dh
    jnz .slo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    jmp short .sg
.slo:
    and al, 0x0F
    inc si
.sg:
    xor dh, 1
    mov ah, [di]                    ; AH = the screen's
    or dl, dl
    jnz .dlo
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    jmp short .dg
.dlo:
    and ah, 0x0F
.dg:
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    or al, ah
    mov bx, w16_ropt
    xlat                            ; AL = the answer
    or dl, dl
    jnz .wlo
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    and byte [di], 0x0F
    or [di], al
    jmp short .wg
.wlo:
    and byte [di], 0xF0
    or [di], al
    inc di
.wg:
    xor dl, 1
    loop .px
    pop dx
    ret

section .text
