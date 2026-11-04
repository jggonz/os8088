; =============================================================================
; os8088 - apps/win16/w16rgn.inc
;
; WHAT PAINTBRUSH DRAWS ITS PICTURE WITH (SPEC.md 124.25.6): a bitmap's bits
; written as well as read, a DIB's both ways, a flood fill, and regions -
; which WIN16 keeps as their bounding rectangle, so a clip region is a cut of
; the DC's clip and nothing finer. Part 9, the system-library part, with
; COMMDLG (SPEC.md 124.24.1): the library part's routines are SLCALLs from
; here, the image's ICALLs, and the image's data is DS-relative.
; =============================================================================

section .w16sys

W16RGN_RECT equ 8                   ; a region: left, top, right, bottom
NULLREGION  equ 1
SIMPLEREGION equ 2
COMPLEXREGION equ 3
FLOODFILLBORDER equ 0

; -----------------------------------------------------------------------------
; Bitmap bits
; -----------------------------------------------------------------------------

; SetBitmapBits (GDI.106) - args: +0 lpBits, +4 dwCount, +8 hBitmap.
; GetBitmapBits backwards: a bitmap made 1bpp takes Windows' monochrome bits
; (1 white, 0 black, each row a whole number of words), any other its 4bpp
; rows, each padded to a word. At most dwCount bytes are read; the answer is
; how many were. 0 for a handle that is not a bitmap.
W16SYS w16g_setbmbits
    push si
    push di
    push bp
    ICALL w16_args
    mov ax, [es:bx + 8]
    mov cx, [es:bx + 4]             ; at most 64KB of it: one segment
    cmp word [es:bx + 6], 0
    je .cnt
    mov cx, 0xFFFF
.cnt:
    les si, [es:bx]                 ; ES:SI = the program's bits
    SLCALL w16_bmslot               ; BX = its slot * 2
    jc .none
    mov [w16_scratch], cx           ; bytes left to take
    mov bp, si                      ; BP = where they began
    call w16_bmcaps                 ; (a tiled bitmap: the first tile's rows,
    mov dx, ax                      ;   SPEC.md 124.25.17)
    mov [w16_scratch + 2], dx       ; rows to go
    xor di, di                      ; DI = the row in the bitmap
    cmp byte [w16b_mono + bx], 0
    jne .mrow
.crow:                              ; 4bpp: each row its bytes, then to even
    mov cx, [w16b_str + bx]
    push di
.cb:
    call .room
    jc .cend
    mov al, [es:si]
    inc si
    push es
    W16SEG es, [w16b_seg + bx]
    stosb
    pop es
    loop .cb
.cend:
    pop di
    jc .done
    add di, [w16b_str + bx]
    test byte [w16b_str + bx], 1
    jz .cnext
    call .room
    jc .done
    inc si
.cnext:
    dec word [w16_scratch + 2]
    jnz .crow
    jmp short .done
.mrow:                              ; 1bpp: eight pixels a byte, a word's
    mov cx, [w16b_w + bx]           ;   worth a 16
    add cx, 15
    shr cx, 1
    shr cx, 1
    shr cx, 1
    and cl, 0xFE                    ; CX = the row's bytes
    xor dx, dx                      ; DX = the pixel
.mb:
    call .room
    jc .done
    mov ah, [es:si]
    inc si
    push cx
    mov cx, 8
.mp:
    shl ah, 1                       ; CF = the pixel's bit
    jnc .blk
    mov al, CWHITE
    jmp short .put
.blk:
    mov al, CBLACK
.put:
    cmp dx, [w16b_w + bx]
    jae .mz
    push di
    push dx
    push es
    push ax
    shr dx, 1                       ; (CF = the low nibble)
    pushf
    add di, dx
    W16SEG es, [w16b_seg + bx]
    popf
    pop ax
    jc .lo
    mov ah, al
    and byte [es:di], 0x0F
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    or [es:di], al
    mov al, ah
    jmp short .pd
.lo:
    and byte [es:di], 0xF0
    or [es:di], al
.pd:
    pop es
    pop dx
    pop di
.mz:
    inc dx
    loop .mp
    pop cx
    loop .mb
    add di, [w16b_str + bx]
    dec word [w16_scratch + 2]
    jnz .mrow
.done:
    mov ax, si
    sub ax, bp
    jmp short .out
.none:
    xor ax, ax
.out:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret
; .room - one more byte may be read: CF=1 dwCount is reached
.room:
    cmp word [w16_scratch], 0
    je .full
    dec word [w16_scratch]
    clc
    ret
.full:
    stc
    ret

; SetDIBits (GDI.440) - args: +0 wUsage, +2 lpbi, +6 lpBits, +10 nNumScans,
; +12 nStartScan, +14 hBitmap, +16 hDC. The DIB's scans nStartScan to
; nStartScan + nNumScans - 1 (counted from its bottom) into the bitmap, as
; CreateDIBitmap's CBM_INIT puts a whole DIB (SPEC.md 124.15.2): 1 or 4 bits
; a pixel, DIB_RGB_COLORS or DIB_PAL_COLORS (124.25.8). The answer is
; nNumScans; 0 for anything else.
W16SYS w16g_setdibits
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx]                 ; DIB_PAL_COLORS: through the selected
    call w16_dibuse                 ;   palette (SPEC.md 124.25.8)
    jc .no
    mov ax, [es:bx + 12]
    mov [w16_dstart], ax
    mov ax, [es:bx + 10]
    mov [w16_dlines], ax
    mov ax, [es:bx + 6]
    mov [w16_dbits], ax
    mov ax, [es:bx + 8]
    mov [w16_dbits + 2], ax
    les si, [es:bx + 2]
    ICALL w16_dibhdr                ; (AX is not kept)
    jc .no
    ICALL w16_args
    mov ax, [es:bx + 14]
    SLCALL w16_bmslot
    jc .no
    SLCALL w16_bmfills
    ICALL w16_args
    mov ax, [es:bx + 10]
    jmp short .out
.no:
    xor ax, ax
.out:
    pop di
    pop si
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
; Part 9's, beside SetDIBits: it fetched the part for w16_dibuse already.
; =============================================================================
W16SYS w16g_setdib
    push si
    push di
    push bp
    xor ax, ax
    mov [w16_dres], ax
    cmp byte [w16_dcon], 0
    je .ret
    ICALL w16_args
    cmp word [es:bx + 26], W16_HDC  ; another DC's: nothing drawn
    jne .ret
    mov ax, [es:bx]                 ; DIB_PAL_COLORS: through the selected
    call w16_dibuse                 ;   palette (SPEC.md 124.25.8)
    jc .ret
    push es
    push bx
    les si, [es:bx + 2]             ; the header
    ICALL w16_dibhdr
    pop bx
    pop es
    jc .ret
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
    ICALL w16_cliprect              ; [w16_rl]..[w16_rb] cut to the clip
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
    ICALL w16_dibrow                ; CF=1: lpBits has not that row
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
    cmp byte [w16_mtgt], 0
    jne .mem
    call OSAPI_GFX_BLIT4
.put:
    pop es
    pop dx
    jmp short .band
.skip:
    inc dx                          ; a row lpBits has not: not drawn
    jmp short .band
.mem:                               ; a memory DC's bitmap (SPEC.md 124.25.3)
    SLCALL w16_mblit4
    jmp short .put
.ret:
    mov ax, [w16_dres]
    pop bp
    pop di
    pop si
    ret

; GetDIBits (GDI.441) - args: +0 wUsage, +2 lpbi, +6 lpBits, +10 nNumScans,
; +12 nStartScan, +14 hBitmap, +16 hDC. The header filled in - the bitmap's
; size, one plane, biBitCount as the program asked (1, 4, 8 or 24) or, when
; 0, 4 (1 for a bitmap made 1bpp), BI_RGB and biSizeImage - and the colour
; table after it, os8088's sixteen (white and black for 1 bit). With lpBits
; the scans nStartScan on, counted from the bottom, are written there in
; that format, every row a whole number of dwords, a pixel light grey or
; brighter 1 at one bit. The answer is the scans written (with no lpBits,
; the bitmap's height); 0 for a handle that is not a bitmap or a bit count
; WIN16 does not write.
W16SYS w16g_getdibits
    push si
    push di
    push bp
    ICALL w16_args
    cmp word [es:bx], 0
    jne .zero
    mov ax, [es:bx + 12]
    mov [w16_gdstart], ax
    mov ax, [es:bx + 10]
    mov [w16_gdnum], ax
    mov ax, [es:bx + 6]
    mov [w16_gdbits], ax
    mov ax, [es:bx + 8]
    mov [w16_gdbits + 2], ax
    cld
    les di, [es:bx + 2]             ; ES:DI = the BITMAPINFO
    push es
    ICALL w16_args
    mov ax, [es:bx + 14]
    pop es
    SLCALL w16_bmslot
    jc .zero
    mov [w16_gdslot], bx
    mov ax, [w16b_w + bx]
    mov [es:di + 4], ax
    xor dx, dx
    mov [es:di + 6], dx
    mov ax, [w16b_h + bx]
    mov [es:di + 8], ax
    mov [es:di + 10], dx
    mov word [es:di + 12], 1
    mov ax, [es:di + 14]            ; biBitCount, or WIN16's own
    or ax, ax
    jnz .bpp
    mov al, 4
    cmp byte [w16b_mono + bx], 0
    je .b0
    mov al, 1
.b0:
    mov [es:di + 14], ax
.bpp:
    cmp ax, 1
    je .bok
    cmp ax, 4
    je .bok
    cmp ax, 8
    je .bok
    cmp ax, 24
    jne .zero
.bok:
    mov [w16_gdbpp], al
    xor dx, dx
    mov [es:di + 16], dx            ; BI_RGB
    mov [es:di + 18], dx
    mov [es:di + 32], dx            ; biClrUsed, biClrImportant
    mov [es:di + 34], dx
    mov [es:di + 36], dx
    mov [es:di + 38], dx
    mul word [w16b_w + bx]          ; the row's bits, to whole dwords
    add ax, 31
    mov cl, 5
    shr ax, cl
    shl ax, 1
    shl ax, 1
    mov [w16_gdstr], ax
    mul word [w16b_h + bx]
    mov [es:di + 20], ax            ; biSizeImage
    mov [es:di + 22], dx
    ; --- the colour table, after the header ---
    add di, [es:di]
    mov cl, [w16_gdbpp]
    cmp cl, 24
    je .bits
    cmp cl, 1
    jne .pal
    xor ax, ax                      ; 1 bit: black and white
    stosw
    stosw
    dec ax
    stosw
    mov ax, 0x00FF
    stosw
    jmp short .bits
.pal:
    mov si, w16_pal16               ; os8088's sixteen, as RGBQUADs
    mov cx, 16
.pe:
    mov al, [si + 2]
    stosb
    mov al, [si + 1]
    stosb
    mov al, [si]
    stosb
    xor al, al
    stosb
    add si, 3
    loop .pe
    cmp byte [w16_gdbpp], 8
    jne .bits
    mov cx, (256 - 16) * 2          ; 8 bits: the other 240 unused
    xor ax, ax
    rep stosw
.bits:
    mov bx, [w16_gdslot]
    mov ax, [w16_gdbits]
    or ax, [w16_gdbits + 2]
    jnz .rows
    mov ax, [w16b_h + bx]           ; no lpBits: the header alone
    jmp .out
.rows:
    les di, [w16_gdbits]
    xor bp, bp                      ; BP = the scans written
.scan:
    cmp bp, [w16_gdnum]
    jae .done
    mov ax, [w16_gdstart]
    add ax, bp                      ; AX = the DIB's scan, from the bottom
    cmp ax, [w16b_h + bx]
    jae .done
    mov dx, [w16b_h + bx]
    dec dx
    sub dx, ax                      ; DX = the bitmap's row
    call w16_gdrow
    inc bp
%ifndef PKG_MEGA                    ; (the arm: a selector is not a paragraph;
    mov ax, di                      ;   a DIB past 64KB is S22 batch 7's)
    mov cl, 4                       ; the next row: lpBits may run past a
    shr ax, cl                      ;   segment (a huge pointer), so ES:DI
    mov cx, es                      ;   is made the smallest offset again
    add cx, ax
    mov es, cx
    and di, 15
%endif
    jmp short .scan
.done:
    mov ax, bp
    jmp short .out
.zero:
    xor ax, ax
.out:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; A TILED bitmap (SPEC.md 124.25.17): past W16_BMMAX, one fixed block of the
; global heap cut into tiles of 65535 / stride rows, each 64KB (1000h
; paragraphs) after the last, so no row straddles two and the next tile is
; the block's paragraph + 1000h on both arms. Part 9's, out of the library
; part's 64KB: w16_bmnew's .huge comes here first, which fetches the part,
; and the part stays until the program ends, so every later call finds it.
; -----------------------------------------------------------------------------

; w16_bmtrow - BX = bitmap slot * 2 -> AX = the rows a tile holds. Keeps all
; but AX
w16_bmtrow:
    push dx
    mov ax, 0xFFFF
    xor dx, dx
    div word [w16b_str + bx]
    pop dx
    ret

; w16_bmrps - w16_bmrp, here: AX = a row, BX = bitmap slot * 2 -> AX = its
; byte offset, DX = the segment (the arm: the virtual paragraph) it is in.
; Keeps all but AX and DX
w16_bmrps:
    test byte [w16b_mono + bx + 1], 2
    jnz .tiled
    mul word [w16b_str + bx]
    mov dx, [w16b_seg + bx]
    ret
.tiled:
    push cx
    xchg ax, cx                     ; CX = the row
    call w16_bmtrow
    xchg ax, cx                     ; AX = the row, CX = rows a tile
    xor dx, dx
    div cx                          ; AX = its tile, DX = its row in it
    xchg ax, dx
    push dx
    mul word [w16b_str + bx]        ; AX = the offset in the tile
    pop dx
    mov cl, 12
    shl dx, cl                      ; the tile's paragraphs in
    add dx, [w16b_seg + bx]
    pop cx
    ret

; w16_bmcaps - w16_bmcap, here: BX = bitmap slot * 2 -> AX = the rows one
; tile holds, at most the bitmap's. Keeps all but AX
w16_bmcaps:
    call w16_bmtrow
    test byte [w16b_mono + bx + 1], 2
    jz .h
    cmp ax, [w16b_h + bx]
    jbe .out
.h:
    mov ax, [w16b_h + bx]
.out:
    ret

; w16_bmhuge - w16_bmnew's .huge: slot BX, its w16b_w, w16b_h and w16b_str
; written -> CF=0 AX = the handle; CF=1 AX = 1 the heap is short, 0 too large.
; ONE fixed block, (tiles - 1) x 64KB + the last tile's bytes, zeroed by
; w16_gnew (black: CBLACK in both halves) through the arm's walk descriptor,
; which holds no LDT entry past the call. Keeps all but AX
w16_bmhuge:
    push cx
    push dx
    call w16_bmtrow
    mov cx, ax
    mov ax, [w16b_h + bx]
    xor dx, dx
    div cx                          ; AX = the whole tiles, DX = rows beyond
    xchg ax, dx
    push dx
    mul word [w16b_str + bx]        ; AX = the last tile's bytes (DX = 0)
    pop dx                          ; DX:AX = whole tiles x 64KB + that
    cmp dx, 15                      ; (w16_gnew's arena: under 1023KB)
    jae .big
    mov cl, GMEM_ZEROINIT
    ICALL w16_gnew                  ; AX = the handle, DX = the segment
    or ax, ax
    mov ax, 1
    stc
    jz .out
    mov byte [w16b_mono + bx + 1], 2
    mov [w16b_seg + bx], dx
    lea ax, [bx + W16_BMP0]
    clc
    jmp short .out
.big:
    xor ax, ax
    stc
.out:
    pop dx
    pop cx
    ret

; w16_bmtiles - w16_bmcopy's for a tiled source on a byte: one BLIT4 a tile,
; the source's rows from [w16b_sy], to the screen rect [w16_rl..rr] x
; [w16_rt..rb], BX = the slot, BP = the stride. Takes AX, CX, DX, SI, ES
w16_bmtiles:
    mov ax, [w16b_sy]
    mov [w16b_ty], ax               ; the next source row
    mov ax, [w16_rt]
    mov [w16b_tdy], ax              ; ...and the screen row it lands on
.tile:
    mov ax, [w16b_ty]
    call w16_bmrps                  ; AX = the offset, DX = the tile's segment
    mov si, [w16b_sx]
    shr si, 1
    add si, ax
    W16SEG es, dx
    call w16_bmtrow
    mov cx, ax
    mov ax, [w16b_ty]
    xor dx, dx
    div cx                          ; DX = its row in the tile
    sub cx, dx                      ; CX = rows left in the tile
    mov dx, [w16_rb]
    sub dx, [w16b_tdy]
    inc dx                          ; DX = screen rows still to draw
    cmp dx, cx
    jbe .tl
    mov dx, cx
.tl:
    push dx
    mov ax, [w16_rl]
    mov cx, [w16_rr]
    sub cx, ax
    inc cx
    push bx
    mov bx, [w16b_tdy]
    call OSAPI_GFX_BLIT4
    pop bx
    pop dx
    add [w16b_tdy], dx
    add [w16b_ty], dx
    mov ax, [w16b_tdy]
    cmp ax, [w16_rb]
    jle .tile
    ret

; w16_gdrow - bitmap [w16_gdslot]'s row DX at ES:DI in [w16_gdbpp] bits a
; pixel, [w16_gdstr] bytes; DI past it. Keeps all but AX, CX, DX, SI.
w16_gdrow:
    push bx
    push bp
    mov bx, [w16_gdslot]
    mov ax, dx
    call w16_bmrps                  ; (a tiled row: SPEC.md 124.25.17)
    mov si, ax                      ; SI = the row in the bitmap
    mov [w16_gdseg], dx             ; ...and the segment it is in
    mov bp, di
    add bp, [w16_gdstr]             ; BP = where the row ends
    xor dx, dx                      ; DX = the pixel
    mov ah, 1                       ; AH = 1 bit's byte, a sentinel at its top
.px:
    cmp dx, [w16b_w + bx]
    jae .pad
    push ds                         ; AL = the pixel's colour
    push si
    push dx
    shr dx, 1
    pushf
    add si, dx
    W16SEG ds, [w16_gdseg]
    mov al, [si]
    popf
    pop dx
    pop si
    pop ds
    jc .lo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lo:
    and al, 0x0F
    mov cl, [w16_gdbpp]
    cmp cl, 4
    je .p4
    cmp cl, 8
    je .p8
    cmp cl, 24
    je .p24
    cmp al, 7                       ; 1 bit: light grey and brighter lit
    cmc
    rcl ah, 1
    jnc .next
    mov al, ah
    stosb
    mov ah, 1
    jmp short .next
.p4:
    test dl, 1
    jnz .p4lo
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    mov [es:di], al
    jmp short .next
.p4lo:
    or [es:di], al
    inc di
    jmp short .next
.p8:
    stosb
    jmp short .next
.p24:
    push bx
    mov bl, al
    xor bh, bh
    mov cx, bx
    shl bx, 1
    add bx, cx                      ; its R, G, B in w16_pal16
    mov al, [w16_pal16 + bx + 2]
    stosb
    mov al, [w16_pal16 + bx + 1]
    stosb
    mov al, [w16_pal16 + bx]
    stosb
    pop bx
.next:
    inc dx
    jmp .px
.pad:
    cmp byte [w16_gdbpp], 1         ; a part-filled byte of bits, shifted up
    jne .p4end
    cmp ah, 1
    je .zero
.sh:
    shl ah, 1
    jnc .sh
    mov al, ah
    stosb
    jmp short .zero
.p4end:
    cmp byte [w16_gdbpp], 4
    jne .zero
    test dl, 1                      ; an odd width's last byte is written
    jz .zero
    inc di
.zero:
    xor al, al                      ; the rest of the row 0
.z:
    cmp di, bp
    jae .out
    stosb
    jmp short .z
.out:
    pop bp
    pop bx
    ret

; -----------------------------------------------------------------------------
; ExtFloodFill (GDI.372) - args: +0 wFillType, +2 crColor, +6 y, +8 x, +10
; hDC (SPEC.md 124.25.6): from the point, every pixel joined to it across its
; four sides that is not crColor (FLOODFILLBORDER) or that is crColor
; (FLOODFILLSURFACE), painted in the DC's brush, a row at a time. crColor is
; the nearest of the sixteen. In a memory DC the bitmap is read; on the
; window's DC the clip's rectangle of the screen is read back first into a
; mask, a bit a pixel set where the fill may go (w16r_snap), and each row
; painted is cleared in it, so the brush's look on the adapter is never
; asked. TRUE, or FALSE when the point is not one to fill, the brush is not
; one colour, or the screen could not be read back.
; -----------------------------------------------------------------------------
W16SYS w16g_xflood
    push si
    push di
    push bp
    xor ax, ax
    mov [w16_fbx], ax               ; a memory DC's bitmap, at its own 0, 0
    mov [w16_fby], ax
    mov [w16_fwin], al
    mov ax, [w16_mbm]
    mov [w16_fbm], ax
    ICALL w16_args
    mov al, [es:bx]
    mov [w16_fmode], al
    mov ax, [es:bx + 2]
    mov dl, [es:bx + 4]
    ICALL w16_rgb2col
    mov [w16_fcol], al
    mov ax, [w16_brush]
    ICALL w16_brcol
    cmp al, 16                      ; NULL, a pattern or an inversion: no
    jae .no
    cmp byte [w16_mtgt], 0
    jne .mem
    mov [w16_gcol], al              ; the window's: painted as it is
    call w16r_snap                  ; ...and the screen read back
    jc .no
    jmp short .seed
.mem:
    SLCALL w16_mcol                 ; a memory DC's: as its bitmap keeps it
    mov [w16_gcol], al
    mov [w16_ffill], al
    mov al, [w16_fcol]
    SLCALL w16_mcol
    mov [w16_fcol], al
.seed:
    ICALL w16_args
    mov ax, [es:bx + 8]
    add ax, [w16_ox]
    mov dx, [es:bx + 6]
    add dx, [w16_oy]
    call w16r_can                   ; Windows' answer for a point already
    jc .no                          ;   filled, or a border, is FALSE
    mov word [w16_fsp], 0
    call w16r_push
.loop:
    call w16r_pop
    jc .yes
    call w16r_can
    jc .loop
    mov [w16_fy], dx
    mov [w16_fxl], ax
    mov [w16_fxr], ax
    mov bx, ax
.l:                                 ; as far left as it goes ...
    dec bx
    mov ax, bx
    call w16r_can
    jc .r0
    mov [w16_fxl], bx
    jmp short .l
.r0:
    mov bx, [w16_fxr]
.r:                                 ; ... and right
    inc bx
    mov ax, bx
    call w16r_can
    jc .fill
    mov [w16_fxr], bx
    jmp short .r
.fill:
    mov ax, [w16_fxl]
    mov cx, [w16_fxr]
    mov bx, dx
    ICALL w16_gfill
    call w16r_mark
    dec dx                          ; a seed for each run above and below
    call .scan
    mov dx, [w16_fy]
    inc dx
    call .scan
    jmp short .loop
.yes:
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    call w16r_unsnap
    xor dx, dx
    pop bp
    pop di
    pop si
    ret
; .scan - row DX: a seed where each run of pixels to fill starts in
; [w16_fxl]..[w16_fxr]
.scan:
    mov bx, [w16_fxl]
    xor bp, bp                      ; BP = 1 in a run
.s:
    cmp bx, [w16_fxr]
    jg .sret
    mov ax, bx
    call w16r_can
    jc .sout
    or bp, bp
    jnz .snext
    inc bp
    call w16r_push
    jmp short .snext
.sout:
    xor bp, bp
.snext:
    inc bx
    jmp short .s
.sret:
    ret

; w16r_can - AX, DX = a pixel of the bitmap: CF=0 one ExtFloodFill paints -
; inside the clip, not the fill's colour already, and not the border
; (FLOODFILLBORDER) or the surface's colour (FLOODFILLSURFACE). Keeps every
; register.
w16r_can:
    cmp ax, [w16_cl]
    jl .no
    cmp ax, [w16_cr]
    jg .no
    cmp dx, [w16_ct]
    jl .no
    cmp dx, [w16_cb]
    jg .no
    cmp byte [w16_fwin], 0
    jne .mask
    push ax
    push bx
    push dx
    push es
    mov bx, [w16_fbm]
    push ax
    mov ax, dx
    call w16_bmrps                  ; (a tiled row: SPEC.md 124.25.17)
    mov [w16_gdseg], dx
    mov dx, ax
    pop ax
    shr ax, 1                       ; CF = a low nibble
    pushf
    add ax, dx
    W16SEG es, [w16_gdseg]
    mov bx, ax
    mov al, [es:bx]
    popf
    jc .lo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lo:
    and al, 0x0F
    cmp al, [w16_ffill]
    je .not
    cmp byte [w16_fmode], FLOODFILLBORDER
    jne .surf
    cmp al, [w16_fcol]
    je .not
    jmp short .yes
.surf:
    cmp al, [w16_fcol]
    jne .not
.yes:
    pop es
    pop dx
    pop bx
    pop ax
    clc
    ret
.not:
    pop es
    pop dx
    pop bx
    pop ax
.no:
    stc
    ret
.mask:                              ; the window's: its bit in the mask
    push ax
    push bx
    push cx
    push dx
    push es
    call w16r_bit                   ; ES:BX, CH = it
    test [es:bx], ch
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    jz .no
    clc
    ret

; w16r_bit - AX, DX = a device pixel in the window's read-back -> ES:BX its
; byte in the mask, CH its bit. Takes AX, CL, DX.
w16r_bit:
    sub ax, [w16_fbx]
    sub dx, [w16_fby]
    mov bx, ax
    mov cl, al
    and cl, 7
    mov ch, 0x80
    shr ch, cl
    mov cl, 3
    shr bx, cl
    mov ax, [w16_fstr]
    mul dx
    add bx, ax
    mov es, [w16_fmseg]
    ret

; w16r_snap - the window's DC: its clip's rectangle of the screen read back
; into a mask of its own (an os8088 claim at [w16_fmseg], [w16_fstr] bytes a
; row, its 0, 0 the device's [w16_fbx]/[w16_fby]) with a bit set for each
; pixel the fill may go to, and [w16_fwin] set. A bit a pixel because that is
; all the fill asks, and a canvas of four bits a pixel would not be had out
; of the room Paintbrush leaves. CF=1: none - no memory, or a screen that
; cannot be read (hidden, VESA). Keeps SI, DI, BP.
w16r_snap:
    push bx
    mov bx, [w16_win]
    call OSAPI_WM_DISPLAY           ; DH = the planes
    pop bx
    cmp dh, 1                       ; one: what it reads is black or white,
    jne .col                        ;   and so is the colour sought
    mov byte [w16_mmono], 1
    mov al, [w16_fcol]
    SLCALL w16_mcol
    mov [w16_fcol], al
    mov byte [w16_mmono], 0
.col:
    mov ax, [w16_cr]
    sub ax, [w16_cl]
    add ax, 8
    mov cl, 3
    shr ax, cl
    mov [w16_fstr], ax
    mov dx, [w16_cb]
    sub dx, [w16_ct]
    inc dx
    mul dx                          ; DX:AX = its bytes
    or dx, dx
    jnz .no
    push ax
    add ax, 15                      ; a claim of os8088's own, out of the
    rcr ax, 1                       ;   room WIN16's heap leaves (W16_ARKEEP):
    shr ax, 1                       ;   an allocation in the global heap could
    shr ax, 1                       ;   discard a block of the program's while
    shr ax, 1                       ;   it holds it unlocked, which Windows'
    push bx                         ;   GDI never did. In PARAGRAPHS, a byte
    xor cx, cx                      ;   count's door (SPEC.md 50.3.7)
    mov bl, MEMP_CLAIM
    call OSAPI_MEM_P
    pop bx
    pop cx
    jc .no
    mov [w16_fmseg], dx
    push di
    push es
    mov es, dx                      ; none to fill, until it is read: a part
    xor di, di                      ;   off the screen stays that way
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    mov byte [w16_fwin], 1
    xor ax, ax
    mov [w16_rl], ax
    mov [w16_rt], ax
    mov [w16b_dx0], ax
    mov [w16b_dy0], ax
    mov ax, [w16_cr]
    sub ax, [w16_cl]
    mov [w16_rr], ax
    mov ax, [w16_cb]
    sub ax, [w16_ct]
    mov [w16_rb], ax
    mov ax, [w16_cl]
    mov [w16_fbx], ax
    sub ax, [w16_ox]
    mov [w16b_xs], ax               ; the source, logical (w16_scr2bm adds
    mov ax, [w16_ct]                ;   the origin back)
    mov [w16_fby], ax
    sub ax, [w16_oy]
    mov [w16b_ys], ax
    mov byte [w16b_pl], 0
    SLCALL w16_scr2bm               ; ...into the mask, w16_scrmrow's way
    cmp byte [w16b_pl], 0           ; no planes: it read nothing
    je .none
    clc
    ret
.none:
    call w16r_unsnap
.no:
    stc
    ret

; w16r_unsnap - the read-back's mask freed, if there is one. Keeps AX.
w16r_unsnap:
    cmp byte [w16_fwin], 0
    je .ret
    push dx
    mov byte [w16_fwin], 0
    mov dx, [w16_fmseg]
    call OSAPI_MEM_FREE
    pop dx
.ret:
    ret

; w16r_mark - the window's: row [w16_fy], [w16_fxl]..[w16_fxr] cleared in the
; mask, painted now. Keeps every register.
w16r_mark:
    cmp byte [w16_fwin], 0
    je .ret
    push ax
    push bx
    push cx
    push dx
    push es
    mov ax, [w16_fxl]
.p:
    cmp ax, [w16_fxr]
    jg .out
    push ax
    mov dx, [w16_fy]
    call w16r_bit
    not ch
    and [es:bx], ch
    pop ax
    inc ax
    jmp short .p
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; w16r_push - AX, DX onto the fill's stack in w16_gbuf; when it is full the
; seed is dropped and that part is not filled. w16r_pop - AX, DX off it, CF=1
; empty. Both keep the rest.
w16r_push:
    push bx
    mov bx, [w16_fsp]
    cmp bx, W16_GBUF - 4
    ja .full
    mov [w16_gbuf + bx], ax
    mov [w16_gbuf + bx + 2], dx
    add bx, 4
    mov [w16_fsp], bx
.full:
    pop bx
    ret
w16r_pop:
    push bx
    mov bx, [w16_fsp]
    or bx, bx
    jz .e
    sub bx, 4
    mov [w16_fsp], bx
    mov ax, [w16_gbuf + bx]
    mov dx, [w16_gbuf + bx + 2]
    pop bx
    clc
    ret
.e:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; Regions: a made object of their own kind, W16K_RGN, holding a rectangle
; (SPEC.md 124.25.6). DeleteObject frees one as it frees a brush.
; -----------------------------------------------------------------------------

; CreateRectRgn (GDI.64) - args: +0 bottom, +2 right, +4 top, +6 left
W16SYS w16g_rectrgn
    push si
    push di
    ICALL w16_args
    mov cx, [es:bx + 6]
    mov dx, [es:bx + 4]
    mov si, [es:bx + 2]
    mov di, [es:bx]
    jmp short w16_newrgn

; CreatePolygonRgn (GDI.63) - args: +0 nPolyFillMode, +2 nCount, +4
; lpPoints. Its bounding rectangle, the right and bottom one past the
; points: the region is the box the polygon is drawn in, not the polygon.
W16SYS w16g_polyrgn
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 2]
    les bx, [es:bx + 4]
    or ax, ax
    jle .none
    push bp
    mov bp, ax
    mov cx, [es:bx]                 ; CX, DX = the least x and y, SI, DI the
    mov si, cx                      ;   most
    mov dx, [es:bx + 2]
    mov di, dx
.pt:
    mov ax, [es:bx]
    cmp ax, cx
    jge .x1
    mov cx, ax
.x1:
    cmp ax, si
    jle .x2
    mov si, ax
.x2:
    mov ax, [es:bx + 2]
    cmp ax, dx
    jge .y1
    mov dx, ax
.y1:
    cmp ax, di
    jle .y2
    mov di, ax
.y2:
    add bx, 4
    dec bp
    jnz .pt
    pop bp
    inc si
    inc di
    jmp short w16_newrgn
.none:
    xor ax, ax
    pop di
    pop si
    ret

; CreateRectRgnIndirect (GDI.65) - args: +0 lpRect: CreateRectRgn's, of the
; RECT
W16SYS w16g_rectrgni
    push si
    push di
    ICALL w16_args
    les bx, [es:bx]
    mov cx, [es:bx]
    mov dx, [es:bx + 2]
    mov si, [es:bx + 4]
    mov di, [es:bx + 6]
    ; fall into w16_newrgn

; w16_newrgn - CX, DX, SI, DI = left, top, right, bottom, SI and DI pushed
; by the handler: a region made of them -> AX = its handle, 0 when W16_NOBJ
; objects are made already. The handler's tail: it pops them and returns.
w16_newrgn:
    push bx
    mov ah, W16K_RGN
    ICALL w16_newobj
    or ax, ax
    jz .out
    mov bx, ax
    sub bx, W16_OBJ0
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov [w16_orgn + bx], cx
    mov [w16_orgn + bx + 2], dx
    mov [w16_orgn + bx + 4], si
    mov [w16_orgn + bx + 6], di
.out:
    pop bx
    xor dx, dx
    pop di
    pop si
    ret

; w16_rgnbx - AX = a handle -> BX = its rectangle's offset in w16_orgn. CF=1
; it is not a region's. Keeps the rest.
w16_rgnbx:
    mov bx, ax
    sub bx, W16_OBJ0
    jb .no
    cmp bx, W16_NOBJ
    jae .no
    cmp byte [w16_objk + bx], W16K_RGN
    jne .no
    shl bx, 1
    shl bx, 1
    shl bx, 1
    clc
    ret
.no:
    stc
    ret

; GetUpdateRgn (USER.237) - args: +0 bErase, +2 hRgn, +4 hWnd. The window's
; invalid rectangle, in its client and cut to it, put into the region (a
; region is its bounding rectangle here): SIMPLEREGION, or NULLREGION and an
; empty region when nothing is invalid; ERROR (0) for a window or region that
; is not one. bErase is not acted on, as GetUpdateRect's is not (SPEC.md
; 124.25.15)
W16SYS w16g_getupdrgn
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 4]
    mov di, [es:bx + 2]             ; DI = the region
    ICALL w16_hwnd2rec              ; BX = the window's record
    jc .err
    push bx
    mov ax, di
    call w16_rgnbx                  ; BX = its rectangle in w16_orgn
    pop si                          ; SI = the record
    jc .err
    mov di, bx
    test byte [si + W16W_FLAGS], W16WF_INVAL
    jz .null
    mov bx, si
    ICALL w16_csize                 ; CX, DX = the client
    mov ax, [si + W16W_UR]
    cmp ax, cx
    jle .r
    mov ax, cx
.r:
    mov cx, ax                      ; CX = right
    mov ax, [si + W16W_UB]
    cmp ax, dx
    jle .b
    mov ax, dx
.b:
    mov dx, ax                      ; DX = bottom
    mov ax, [si + W16W_UL]
    or ax, ax
    jge .l
    xor ax, ax
.l:
    mov bx, [si + W16W_UT]
    or bx, bx
    jge .t
    xor bx, bx
.t:
    cmp ax, cx
    jge .null
    cmp bx, dx
    jge .null
    mov [w16_orgn + di], ax
    mov [w16_orgn + di + 2], bx
    mov [w16_orgn + di + 4], cx
    mov [w16_orgn + di + 6], dx
    mov ax, SIMPLEREGION
    jmp short .out
.null:
    xor ax, ax
    mov [w16_orgn + di], ax
    mov [w16_orgn + di + 2], ax
    mov [w16_orgn + di + 4], ax
    mov [w16_orgn + di + 6], ax
    mov ax, NULLREGION
    jmp short .out
.err:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; w16_rgnnull - BX = a rectangle's offset in w16_orgn: CF=1 it is empty.
; Keeps all but AX.
w16_rgnnull:
    mov ax, [w16_orgn + bx]
    cmp ax, [w16_orgn + bx + 4]
    jge .y
    mov ax, [w16_orgn + bx + 2]
    cmp ax, [w16_orgn + bx + 6]
    jge .y
    clc
    ret
.y:
    stc
    ret

; w16_rgneq - SI = a rectangle's offset in w16_orgn: ZF=1 it is the one in
; w16_scratch. Keeps every register.
w16_rgneq:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    add si, w16_orgn
    mov di, w16_scratch
    mov cx, 4
    cld
    repe cmpsw
    pop es
    pop di
    pop si
    pop cx
    ret

; w16_rgnget - BX = a rectangle's offset in w16_orgn: it into w16_scratch.
; Keeps all but AX.
w16_rgnget:
    mov ax, [w16_orgn + bx]
    mov [w16_scratch], ax
    mov ax, [w16_orgn + bx + 2]
    mov [w16_scratch + 2], ax
    mov ax, [w16_orgn + bx + 4]
    mov [w16_scratch + 4], ax
    mov ax, [w16_orgn + bx + 6]
    mov [w16_scratch + 6], ax
    ret

; one edge of two rectangles, at SI and BX in w16_orgn, into w16_scratch: %2
; is the jump that keeps the first's - jge the larger, jle the smaller
%macro W16RGNEDGE 2
    mov ax, [w16_orgn + si + %1]
    cmp ax, [w16_orgn + bx + %1]
    %2 %%k
    mov ax, [w16_orgn + bx + %1]
%%k:
    mov [w16_scratch + %1], ax
%endmacro

RGN_AND     equ 1
RGN_OR      equ 2
RGN_XOR     equ 3
RGN_DIFF    equ 4
RGN_COPY    equ 5

; CombineRgn (GDI.47) - args: +0 nCombineMode, +2 hrgnSrc2, +4 hrgnSrc1, +6
; hrgnDest. A region is a rectangle (SPEC.md 124.25.6), so: RGN_COPY the
; first; RGN_AND the two's intersection; RGN_OR the box that holds both,
; COMPLEXREGION when that is more than either is; RGN_XOR that box too,
; COMPLEXREGION; RGN_DIFF nothing when the second covers the first, else the
; first, COMPLEXREGION when the second cuts into it. AX = the type, 0
; (ERROR) for a handle that is not a region's or a mode that is not one
W16SYS w16g_combrgn
    push si
    push di
    push bp
    ICALL w16_args
    mov bp, [es:bx]                 ; BP = the mode
    mov cx, [es:bx + 2]
    mov dx, [es:bx + 4]
    mov ax, [es:bx + 6]
    call w16_rgnbx
    jc .err
    mov di, bx                      ; DI = the destination's rectangle
    mov ax, dx
    call w16_rgnbx
    jc .err
    mov si, bx                      ; SI = the first source's
    cmp bp, RGN_COPY
    je .first
    ja .err
    mov ax, cx
    call w16_rgnbx                  ; BX = the second's
    jc .err
    cmp bp, RGN_AND
    je .and
    jb .err
    cmp bp, RGN_DIFF
    je .diff
    call w16_rgnnull                ; OR, XOR: one empty is the other
    jc .first
    xchg bx, si
    call w16_rgnnull
    xchg bx, si
    jc .second
    W16RGNEDGE 0, jle
    W16RGNEDGE 2, jle
    W16RGNEDGE 4, jge
    W16RGNEDGE 6, jge
    mov ax, COMPLEXREGION
    cmp bp, RGN_XOR
    je .store
    call w16_rgneq
    je .simple
    xchg bx, si
    call w16_rgneq
    xchg bx, si
    jne .store
.simple:
    mov ax, SIMPLEREGION
    jmp short .store
.diff:
    call .meet
    jc .first                       ; they do not meet: the first
    call w16_rgneq
    jne .cut
    xor ax, ax                      ; covered: nothing
    mov [w16_scratch], ax
    mov [w16_scratch + 2], ax
    mov [w16_scratch + 4], ax
    mov [w16_scratch + 6], ax
    mov al, NULLREGION
    jmp short .store
.cut:
    mov bx, si
    call w16_rgnget
    mov ax, COMPLEXREGION
    jmp short .store
.and:
    call .meet
    jmp short .typed
.first:
    mov bx, si
.second:
    call w16_rgnget
.typed:                             ; w16_scratch's own type
    mov bx, w16_scratch - w16_orgn
    call w16_rgnnull
    mov ax, SIMPLEREGION
    jnc .store
    mov al, NULLREGION
.store:                             ; w16_scratch is the destination's
    mov dx, [w16_scratch]
    mov [w16_orgn + di], dx
    mov dx, [w16_scratch + 2]
    mov [w16_orgn + di + 2], dx
    mov dx, [w16_scratch + 4]
    mov [w16_orgn + di + 4], dx
    mov dx, [w16_scratch + 6]
    mov [w16_orgn + di + 6], dx
    jmp short .out
.err:
    xor ax, ax
.out:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret
.meet:                              ; the intersection into w16_scratch: CF=1
    W16RGNEDGE 0, jge               ;   it is empty
    W16RGNEDGE 2, jge
    W16RGNEDGE 4, jle
    W16RGNEDGE 6, jle
    push bx
    mov bx, w16_scratch - w16_orgn
    call w16_rgnnull
    pop bx
    ret

; PtInRegion (GDI.161) - args: +0 y, +2 x, +4 hRgn: TRUE when its rectangle
; holds the point, the right and bottom edges outside it
W16SYS w16g_ptinrgn
    ICALL w16_args
    mov cx, [es:bx + 2]
    mov dx, [es:bx]
    mov ax, [es:bx + 4]
    call w16_rgnbx
    jc .no
    cmp cx, [w16_orgn + bx]
    jl .no
    cmp cx, [w16_orgn + bx + 4]
    jge .no
    cmp dx, [w16_orgn + bx + 2]
    jl .no
    cmp dx, [w16_orgn + bx + 6]
    jge .no
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    ret

; FillRgn (GDI.40) - args: +0 hBrush, +2 hRgn, +4 hDC: FillRect's, of the
; region's rectangle in logical coordinates. TRUE, FALSE for a DC or region
; that is not one
W16SYS w16g_fillrgn
    ICALL w16_args
    cmp word [es:bx + 4], W16_HDC
    jne .no
    mov ax, [es:bx + 2]
    push word [es:bx]
    call w16_rgnbx
    pop ax                          ; AX = hBrush
    jc .no
    push bx
    ICALL w16_brcol
    pop bx
    cmp al, W16_NOCOL
    je .yes
    mov [w16_gcol], al
    add bx, w16_orgn
    push ds
    pop es
    ICALL w16_rcin
    jc .yes
    mov ax, [w16_rl]
    mov bx, [w16_rt]
    mov cx, [w16_rr]
    mov dx, [w16_rb]
    ICALL w16_gfill
.yes:
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    ret

; GetClipBox (GDI.77) - args: +0 lpRect, +4 hDC: the clip's rectangle,
; logical - 0, 0, 0, 0 when it is empty - and its type, SIMPLEREGION or
; NULLREGION; 0 (ERROR) for a DC that is not one
W16SYS w16g_clipbox
    push si
    push di
    ICALL w16_args
    cmp word [es:bx + 4], W16_HDC
    jne .err
    cmp byte [w16_dcon], 0
    je .err
    call w16_cliptype               ; AX = the type
    push ax
    les di, [es:bx]
    xor cx, cx
    xor dx, dx
    xor si, si
    xor bx, bx
    cmp ax, NULLREGION
    je .put
    mov cx, [w16_cl]
    sub cx, [w16_ox]
    mov dx, [w16_ct]
    sub dx, [w16_oy]
    mov si, [w16_cr]
    sub si, [w16_ox]
    inc si
    mov bx, [w16_cb]
    sub bx, [w16_oy]
    inc bx
.put:
    cld
    mov ax, cx
    stosw
    mov ax, dx
    stosw
    mov ax, si
    stosw
    mov ax, bx
    stosw
    pop ax
    jmp short .out
.err:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; SelectClipRgn (GDI.44) - args: +0 hRgn, +2 hDC. The DC's clip is what it
; was made with (the window's, or the bitmap's: [w16_bcl]..[w16_bcb]) cut to
; the region, which is in the DC's device coordinates; hRgn 0 is the clip as
; made. SIMPLEREGION, NULLREGION when nothing is left, 0 (ERROR) for a DC or
; region that is not one.
W16SYS w16g_selclip
    push si
    push di
    call .go
    pop di
    pop si
    ret
.go:
    ICALL w16_args
    cmp word [es:bx + 2], W16_HDC
    jne .err
    cmp byte [w16_dcon], 0
    je .err
    mov ax, [es:bx]
    mov si, w16_bcl                 ; the clip as made
    mov di, w16_cl
    mov cx, 4
.cp:
    mov dx, [si]
    mov [di], dx
    add si, 2
    add di, 2
    loop .cp
    or ax, ax
    jz w16_cliptype
    mov bx, ax
    sub bx, W16_OBJ0
    jb .err
    cmp bx, W16_NOBJ
    jae .err
    cmp byte [w16_objk + bx], W16K_RGN
    jne .err
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov ax, [w16_ox]                ; the device origin, on the screen: the
    sub ax, [w16_vorg]              ;   DC's own, before SetViewportOrg and
    add ax, [w16_worg]              ;   SetWindowOrg moved it
    mov cx, [w16_oy]
    sub cx, [w16_vorg + 2]
    add cx, [w16_worg + 2]
    mov dx, [w16_orgn + bx]
    add dx, ax
    cmp dx, [w16_cl]
    jle .l
    mov [w16_cl], dx
.l:
    mov dx, [w16_orgn + bx + 2]
    add dx, cx
    cmp dx, [w16_ct]
    jle .t
    mov [w16_ct], dx
.t:
    mov dx, [w16_orgn + bx + 4]
    add dx, ax
    dec dx
    cmp dx, [w16_cr]
    jge .r
    mov [w16_cr], dx
.r:
    mov dx, [w16_orgn + bx + 6]
    add dx, cx
    dec dx
    cmp dx, [w16_cb]
    jge w16_cliptype
    mov [w16_cb], dx
    jmp short w16_cliptype
.err:
    xor ax, ax
    xor dx, dx
    ret

; w16_cliptype - AX = SIMPLEREGION, or NULLREGION when the clip is empty
w16_cliptype:
    mov ax, SIMPLEREGION
    mov dx, [w16_cl]
    cmp dx, [w16_cr]
    jg .null
    mov dx, [w16_ct]
    cmp dx, [w16_cb]
    jle .out
.null:
    mov ax, NULLREGION
.out:
    xor dx, dx
    ret

; ExcludeClipRect (GDI.21) - args: +0 bottom, +2 right, +4 top, +6 left, +8
; hDC. The rectangle (logical) taken out of the clip as far as a rectangle
; can say it: one that misses the clip leaves it, one that covers it leaves
; nothing, one that covers it from one edge across its whole height or width
; moves that edge. A hole in the middle is not cut - COMPLEXREGION is the
; answer, and drawing there is not stopped.
W16SYS w16g_exclclip
    push si
    push di
    ICALL w16_args
    cmp word [es:bx + 8], W16_HDC
    jne .err
    cmp byte [w16_dcon], 0
    je .err
    mov cx, [es:bx + 6]             ; CX, DX, SI, DI = the rectangle, screen,
    add cx, [w16_ox]                ;   inclusive
    mov dx, [es:bx + 4]
    add dx, [w16_oy]
    mov si, [es:bx + 2]
    add si, [w16_ox]
    dec si
    mov di, [es:bx]
    add di, [w16_oy]
    dec di
    cmp cx, si
    jg .type                        ; empty: nothing taken
    cmp dx, di
    jg .type
    cmp cx, [w16_cr]                ; ...or missing the clip
    jg .type
    cmp si, [w16_cl]
    jl .type
    cmp dx, [w16_cb]
    jg .type
    cmp di, [w16_ct]
    jl .type
    cmp dx, [w16_ct]                ; across the whole height?
    jg .wide
    cmp di, [w16_cb]
    jl .wide
    cmp cx, [w16_cl]
    jg .right
    inc si                          ; from the left edge (or all of it)
    mov [w16_cl], si
    jmp short .type
.right:
    cmp si, [w16_cr]
    jl .wide
    dec cx                          ; from the right edge
    mov [w16_cr], cx
    jmp short .type
.wide:
    cmp cx, [w16_cl]                ; across the whole width?
    jg .hole
    cmp si, [w16_cr]
    jl .hole
    cmp dx, [w16_ct]
    jg .bottom
    inc di
    mov [w16_ct], di
    jmp short .type
.bottom:
    cmp di, [w16_cb]
    jl .hole
    dec dx
    mov [w16_cb], dx
.type:
    call w16_cliptype
    jmp short .out
.hole:
    mov ax, COMPLEXREGION
    xor dx, dx
    jmp short .out
.err:
    xor ax, ax
    xor dx, dx
.out:
    pop di
    pop si
    ret


; w16_rrwide - RoundRect's and Ellipse's shape under a pen over one pixel
; wide (SPEC.md 124.25.7), from w16g_roundrect with the box, the radii and
; w16_rrin made for it, [w16_rpen] the pen's colour and [w16_rwide] its
; width. The pen is the band between that shape and the same shape inside
; it by the width, both radii less the width; the brush is the inner one.
; Drawn as three batches of rows through w16_span, as RoundRect's own: the
; band's left part (and its top and bottom rows whole), its right part, and
; the brush. Clobbers AX, BX, CX, DX, SI, DI, BP.
w16_rrwide:
    push ds
    pop es
    mov si, w16_rrin                ; the outer shape's insets and box
    mov di, w16_rrin2
    mov cx, W16_RRMAX
    cld
    rep movsb
    mov si, w16_rl
    mov di, w16_rro
    movsw
    movsw
    movsw
    movsw
    mov ax, [w16_rry]
    stosw
    mov al, [w16_rwide]             ; the inner one: the box less the width
    xor ah, ah
    add [w16_rl], ax
    add [w16_rt], ax
    sub [w16_rr], ax
    sub [w16_rb], ax
    sub [w16_rrx], ax
    jnb .ix
    mov word [w16_rrx], 0
.ix:
    sub [w16_rry], ax
    jnb .iy
    mov word [w16_rry], 0
.iy:
    mov ax, [w16_rl]                ; a box with no inside has no rows
    cmp ax, [w16_rr]
    jle .iok
    mov word [w16_rt], 1
    mov word [w16_rb], 0
.iok:
    ICALL w16_rrtable
    mov si, w16_rl
    mov di, w16_rri
    movsw
    movsw
    movsw
    movsw
    mov ax, [w16_rry]
    stosw
    mov al, [w16_rpen]
    mov [w16_gcol], al
    mov dx, [w16_rro + 2]           ; the left part, every row of the shape
.left:
    call w16_rro_row                ; AX = the outer left, CX its right
    mov bp, ax
    call w16_rri_row                ; AX = the inner left, or CF: outside it
    jc .lput
    dec ax
    mov cx, ax
.lput:
    mov ax, bp
    ICALL w16_span
    inc dx
    cmp dx, [w16_rro + 6]
    jle .left
    ICALL w16_spflush
    mov dx, [w16_rri + 2]           ; the right part, the inner shape's rows
.right:
    cmp dx, [w16_rri + 6]
    jg .rdone
    call w16_rro_row
    push cx
    call w16_rri_row
    mov ax, cx
    inc ax
    pop cx
    ICALL w16_span
    inc dx
    jmp short .right
.rdone:
    ICALL w16_spflush
    mov ax, [w16_brush]             ; the brush, the inner shape
    ICALL w16_brcol
    ICALL w16_ropcol
    cmp al, W16_NOCOL
    je .out
    mov [w16_gcol], al
    mov dx, [w16_rri + 2]
.fill:
    cmp dx, [w16_rri + 6]
    jg .fdone
    call w16_rri_row
    ICALL w16_span
    inc dx
    jmp short .fill
.fdone:
    ICALL w16_spflush
.out:
    ret

; w16_rro_row, w16_rri_row - DX = a row: AX..CX the outer (inner) shape's
; run on it, from its box and insets; CF=1 when the row is not in the box.
; Keep DX, BP.
w16_rro_row:
    mov si, w16_rro
    mov di, w16_rrin2
    jmp short w16_rrk
w16_rri_row:
    mov si, w16_rri
    mov di, w16_rrin
w16_rrk:
    mov ax, dx                      ; k = the rows to the nearer edge
    sub ax, [si + 2]
    jl .no
    mov bx, [si + 6]
    sub bx, dx
    jl .no
    cmp ax, bx
    jbe .k
    mov ax, bx
.k:
    xor bx, bx                      ; BX = that row's inset
    cmp ax, [si + 8]
    jae .in
    mov bx, ax
    mov bl, [bx + di]
    xor bh, bh
.in:
    mov ax, [si]
    add ax, bx
    mov cx, [si + 4]
    sub cx, bx
    clc
    ret
.no:
    stc
    ret


; =============================================================================
; Logical palettes (SPEC.md 124.25.8). A display here has the colours it has,
; so a palette changes nothing on the screen; what it holds is what the
; program asks it back for - GetPaletteEntries, GetObject's count, the
; nearest entry - and what DIB_PAL_COLORS indexes: a colour table of words,
; each an entry of the palette SelectPalette chose, mapped to the nearest of
; the sixteen as a DIB's RGB entries are. A made palette is a fixed global
; block, its entry count and then its PALETTEENTRYs; W16_NPAL at once, and
; past them CreatePalette answers the stock DEFAULT_PALETTE, as it did before
; WIN16 kept any. One palette is selected for the program, not one a DC.
; =============================================================================

; CreatePalette (GDI.360) - args: +0 lpLogPalette: palVersion, palNumEntries
; (at most 256 are kept) and the entries. AX = W16_PAL0 + 2 * its slot
W16SYS w16g_crepal
    push si
    push di
    xor si, si
.slot:
    cmp word [w16_pals + si], 0
    je .free
    inc si
    inc si
    cmp si, 2 * W16_NPAL
    jb .slot
    jmp short .stock
.free:
    ICALL w16_args
    les bx, [es:bx]
    mov cx, [es:bx + 2]
    cmp cx, 256
    jbe .n
    mov cx, 256
.n:
    mov ax, cx                      ; 2 + 4 bytes an entry, fixed
    shl ax, 1
    shl ax, 1
    add ax, 2
    xor dx, dx
    push cx
    xor cx, cx
    ICALL w16_gnew                  ; AX = the block's segment
    pop cx
    or ax, ax
    jz .stock
    mov [w16_pals + si], ax
    push ds
    push es
    pop ds
    lea dx, [bx + 4]
    W16SEG es, ax
    xchg dx, si                     ; DS:SI = the entries, DX = the slot
    xor di, di
    mov ax, cx
    cld
    stosw
    shl cx, 1
    rep movsw
    pop ds
    mov ax, dx
    add ax, W16_PAL0
    jmp short .out
.stock:
    mov ax, W16_STOCK + DEFAULT_PALETTE
.out:
    xor dx, dx
    pop di
    pop si
    ret

; SelectPalette (USER.282) - args: +0 bForceBackground, +2 hPal, +4 hDC: a
; made palette is the one DIB_PAL_COLORS reads from now on. Answers the one
; it replaced, the stock palette when it was none of ours
W16SYS w16g_selpal
    ICALL w16_args
    mov ax, [es:bx + 2]
    call w16_palseg
    jnc .made
    xor ax, ax
.made:
    xchg ax, [w16_selpal]
    or ax, ax
    jnz .out
    mov ax, W16_STOCK + DEFAULT_PALETTE
.out:
    xor dx, dx
    ret

; w16_palseg - AX = a handle -> CF=0 a made palette: DX = its block, BX =
; its slot * 2; CF=1 not one. Keeps AX
w16_palseg:
    mov bx, ax
    sub bx, W16_PAL0
    jb .no
    cmp bx, 2 * W16_NPAL
    jae .no
    test bl, 1
    jnz .no
    mov dx, [w16_pals + bx]
    or dx, dx
    jz .no
    clc
    ret
.no:
    stc
    ret

; w16_paldel - DeleteObject's: AX = a handle. A made palette's block is
; freed, AX = 1, CF=0; CF=1 not one, AX kept
w16_paldel:
    push bx
    push dx
    call w16_palseg
    jc .out
    mov word [w16_pals + bx], 0
    cmp ax, [w16_selpal]
    jne .sel
    mov word [w16_selpal], 0
.sel:
    mov ax, dx
    ICALL w16_gfreeh
    mov ax, 1
    clc
.out:
    pop dx
    pop bx
    ret

; w16_palobj - GetObject's: AX = a handle, CX = cbBuffer, ES:DI = the
; buffer. A palette, made or the stock one: its entry count there as a word,
; AX = 2 (0 when CX is short of one), CF=0; CF=1 not one, all kept
w16_palobj:
    push bx
    push dx
    push ax
    call w16_palseg
    jc .stock
    push es
    mov es, dx
    mov dx, [es:0]
    pop es
    jmp short .put
.stock:
    cmp ax, W16_STOCK + DEFAULT_PALETTE
    stc
    jne .not
    mov dx, 16
.put:
    pop ax
    xor ax, ax
    cmp cx, 2
    jb .out
    mov [es:di], dx
    mov al, 2
.out:
    clc
    pop dx
    pop bx
    ret
.not:
    pop ax
    pop dx
    pop bx
    ret

; w16_dibuse - AX = a DIB call's wUsage -> CF=0 it can be read: DIB_RGB_COLORS,
; or DIB_PAL_COLORS with a made palette selected, which [w16_dpalc] names for
; w16_dibhdr; CF=1 anything else. Keeps all
w16_dibuse:
    mov word [w16_dpalc], 0
    cmp ax, 1
    jb .ok
    jne .no
    push bx
    push dx
    push ax
    mov ax, [w16_selpal]
    call w16_palseg
    pop ax
    jc .none
    mov [w16_dpalc], dx
.none:
    pop dx
    pop bx
    jc .no
.ok:
    clc
    ret
.no:
    stc
    ret

; GetPaletteEntries (GDI.363) - args: +0 lppe, +4 wNum, +6 wStart, +8 hPal.
; A made palette's own entries; any other handle is the display's sixteen,
; in the order GetDIBits writes a colour table, so a BMP WIN16 saved reads
; back as the colours it was drawn in, and past the sixteen black. The count
; copied; with lppe NULL, the palette's entries
W16SYS w16g_getpalent
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 8]
    call w16_palseg
    jnc .made
    mov cx, [es:bx + 4]
    mov ax, [es:bx + 6]
    les di, [es:bx]
    mov dx, es
    or dx, di
    jz .count
    push cx
    cld
    jcxz .done
.e:
    xor dx, dx                      ; DX = R, G (B and the flags 0 below)
    xor si, si
    cmp ax, 16
    jae .put
    mov si, ax
    shl si, 1
    add si, ax
    add si, w16_pal16
    mov dx, [si]
    mov si, [si + 2]
    and si, 0xFF
.put:
    xchg ax, dx
    stosw                           ; peRed, peGreen
    xchg ax, dx
    xchg ax, si
    stosw                           ; peBlue, peFlags 0
    xchg ax, si
    inc ax
    loop .e
.done:
    pop ax
    jmp short .out
.count:
    mov ax, 16
.out:
    xor dx, dx
    pop di
    pop si
    ret
.made:
    ICALL w16_args                  ; (w16_palseg answered in BX)
    mov cx, [es:bx + 4]
    mov si, [es:bx + 6]
    les di, [es:bx]
    push ds
    mov ds, dx
    mov ax, [0]                     ; the entries it has
    mov dx, es
    or dx, di
    jz .mout
    sub ax, si
    jbe .none
    cmp cx, ax
    jae .all
    mov ax, cx
.all:
    mov cx, ax
    shl si, 1
    shl si, 1
    inc si
    inc si
    shl cx, 1
    cld
    rep movsw
    jmp short .mout
.none:
    xor ax, ax
.mout:
    pop ds
    jmp short .out

; GetNearestPaletteIndex (GDI.370) - args: +0 crColor, +4 hPal: a made
; palette's entry nearest it, by the sum of the three distances; of any other
; handle, the nearest of GetPaletteEntries' sixteen
W16SYS w16g_nearpal
    ICALL w16_args
    mov ax, [es:bx + 4]
    call w16_palseg
    jnc .made
    mov ax, [es:bx]
    mov dl, [es:bx + 2]
    ICALL w16_rgb2col
    xor ah, ah
    jmp short .out
.made:
    push si
    push di
    push bp
    ICALL w16_args                  ; (w16_palseg answered in BX)
    mov cx, [es:bx]                 ; CL = red, CH = green, AH = blue
    mov ah, [es:bx + 2]
    mov es, dx
    mov bx, 0xFFFF                  ; BX = the best distance, DX its entry
    xor dx, dx
    xor di, di
    mov si, 2
.e:
    cmp di, [es:0]
    jae .done
    xor bp, bp
    mov al, [es:si]
    sub al, cl
    call w16_nadd
    mov al, [es:si + 1]
    sub al, ch
    call w16_nadd
    mov al, [es:si + 2]
    sub al, ah
    call w16_nadd
    cmp bp, bx
    jae .n
    mov bx, bp
    mov dx, di
.n:
    add si, 4
    inc di
    jmp short .e
.done:
    mov ax, dx
    pop bp
    pop di
    pop si
.out:
    xor dx, dx
    ret

; w16_nadd - BP += |AL|, AL the difference a SUB just left (CF its borrow).
; Keeps all but AL and the flags
w16_nadd:
    jnc .p
    neg al
.p:
    push ax
    xor ah, ah
    add bp, ax
    pop ax
    ret

; GetFreeSystemResources (USER.284) - args: +0 wFlags: the percentage free
; of the tables WIN16 keeps for what Windows' GDI and USER heaps hold -
; GFSR_GDIRESOURCES (1) the made objects' (W16_NOBJ), GFSR_USERRESOURCES (2)
; the window records' (W16_MAXWND), GFSR_SYSTEMRESOURCES (0) the less
W16SYS w16u_freesysres
    push si
    ICALL w16_args
    mov si, [es:bx]                 ; SI = which
    call w16u_fsr
    pop si
    ret

; w16u_fsr - SI = GetFreeSystemResources' wFlags -> AX = the percentage, DX
; = 0 (GetHeapSpaces too, SPEC.md 124.27.1). Keeps SI and DI
w16u_fsr:
    xor cx, cx                      ; CX = the free objects
    xor bx, bx
.o:
    cmp byte [w16_objk + bx], 0
    jne .o1
    inc cx
.o1:
    inc bx
    cmp bx, W16_NOBJ
    jb .o
    mov ax, 100
    mul cx
    mov cx, W16_NOBJ
    div cx                          ; AX = GDI's
    cmp si, 1
    je .out
    push ax
    xor cx, cx                      ; CX = the free records
    mov bx, w16_wnd
    mov dx, W16_MAXWND
.w:
    test byte [bx + W16W_FLAGS], W16WF_USED
    jnz .w1
    inc cx
.w1:
    add bx, W16W_SIZE
    dec dx
    jnz .w
    mov ax, 100
    mul cx
    mov cx, W16_MAXWND
    div cx                          ; AX = USER's
    pop dx
    cmp si, 2
    je .out
    cmp ax, dx
    jbe .out
    mov ax, dx
.out:
    xor dx, dx
    ret

; -----------------------------------------------------------------------------
; The calls Visual Basic 3.0 imports (SPEC.md 124.25.14)
; -----------------------------------------------------------------------------

; OffsetWindowOrg (GDI.15) - args: +0 y, +2 x, +4 hDC: SetWindowOrg of the
; origin plus the offsets, so the answer is the old one
W16SYS w16g_offworg
    ICALL w16_args
    mov ax, [w16_worg]
    add [es:bx + 2], ax
    mov ax, [w16_worg + 2]
    add [es:bx], ax
    SLCALL w16g_setworg
    ret

; SetRectRgn (GDI.172) - args: +0 bottom, +2 right, +4 top, +6 left, +8 hRgn:
; the region's rectangle replaced. TRUE, FALSE for a handle that is no region
W16SYS w16g_setrectrgn
    push si
    ICALL w16_args
    mov si, bx
    mov ax, [es:si + 8]
    call w16_rgnbx
    jc .no
    mov ax, [es:si + 6]
    mov [w16_orgn + bx], ax
    mov ax, [es:si + 4]
    mov [w16_orgn + bx + 2], ax
    mov ax, [es:si + 2]
    mov [w16_orgn + bx + 4], ax
    mov ax, [es:si]
    mov [w16_orgn + bx + 6], ax
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    pop si
    ret

; CreatePenIndirect (GDI.62) - args: +0 lpLogPen {style, width x, width y,
; colour}: CreatePen's {colour, width, style} built in the band, which nothing
; else is using between two calls
W16SYS w16g_penind
    push si
    ICALL w16_args
    les si, [es:bx]
    mov ax, [es:si + 6]
    mov [w16_gbuf], ax
    mov ax, [es:si + 8]
    mov [w16_gbuf + 2], ax
    mov ax, [es:si + 2]
    mov [w16_gbuf + 4], ax
    mov ax, [es:si]
    mov [w16_gbuf + 6], ax
    push ds
    pop es
    mov bx, w16_gbuf
    ICALL w16g_createpen.body
    xor dx, dx
    pop si
    ret

; CreateBrushIndirect (GDI.50) - args: +0 lpLogBrush {style, colour, hatch}
BS_NULL equ 1
W16SYS w16g_brind
    push si
    ICALL w16_args
    les si, [es:bx]
    cmp word [es:si], BS_NULL
    jne .solid
    mov ax, W16_STOCK + 5           ; NULL_BRUSH
    jmp short .out
.solid:
    mov ax, [es:si + 2]
    mov [w16_gbuf], ax
    mov ax, [es:si + 4]
    mov [w16_gbuf + 2], ax
    push ds
    pop es
    mov bx, w16_gbuf
    ICALL w16g_solidbrush.body
.out:
    xor dx, dx
    pop si
    ret

section .text
