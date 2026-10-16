; =============================================================================
; apps/deskclok/dkdraw.inc - THE COMPOSER: everything the Desk Clock draws is
; laid down here first, in its own RAM, and put on the glass with ONE blit a
; slab (SPEC.md 110.14.4) - OSAPI_GFX_BLITP's four planes on a colour display,
; OSAPI_GFX_BLIT1's one on a 1bpp one. Nothing is written to the glass twice.
;
; THE BAND is dk_band: [ck_np] planes (1 or 4), each [ck_sh] rows of [ck_st]
; bytes, plane-major, bit 7 the leftmost pixel - the layout BLITP takes with
; DI = the plane step. It holds rows [ck_y0] .. [ck_y0] + [ck_sh] - 1 of a
; CANVAS (the widget's card, or the settings window's content), and every
; primitive takes canvas coordinates plus the origin [ck_ox], [ck_oy] (a face
; drawn inside a tile), clipped to the slab's rows and to the columns
; [ck_cx0] .. [ck_cx1].
;
; INK IS A PATTERN, not a colour: [ck_pp] names an 8-byte table, a byte per
; plane per row parity, and a span writes its plane's byte under a mask. So a
; solid colour is FF/00 per plane, a two-colour dither is AA/55 - the amber
; LED is yellow and light red on alternate pixels - and on a 1bpp display a
; colour becomes black, white or one of three greys (dk_mpat). The DESKTOP'S
; GROUND is the same kind of table, sampled off the glass (dkwid.inc), with
; the parity of ABSOLUTE screen rows, so a transparent corner continues the
; desktop's dither with no seam.
;
; Registers: ck_span and ck_spanc clobber AX, BX, CX, DX; everything else
; here keeps every register but its documented outputs.
; =============================================================================

; dk_lc - the LOGICAL colours: VGA colour, its dither partner (the same for a
; solid one), and the 1bpp rendering (DKM_*). A face names these, never an
; adapter's colour, so one program draws both worlds
DKM_BLK     equ 0
DKM_WHT     equ 1
DKM_50      equ 2
DKM_LITE    equ 3               ; mostly white
DKM_DARK    equ 4               ; mostly black
%macro DKLC 4                   ; name, c1, c2, mono
%1 equ ($ - dk_lc) / 3
    db %2, %3, %4
%endmacro
dk_lc:
    DKLC LC_BLK,  CBLACK,    CBLACK,   DKM_BLK
    DKLC LC_WHT,  CWHITE,    CWHITE,   DKM_WHT
    DKLC LC_DGRY, CDGRAY,    CDGRAY,   DKM_BLK    ; a bezel
    DKLC LC_LGRY, CLGRAY,    CLGRAY,   DKM_WHT    ; a flip panel's edge
    DKLC LC_GRYD, CLGRAY,    CLGRAY,   DKM_50     ; a grey that must stay grey
    DKLC LC_RED,  CLRED,     CLRED,    DKM_BLK    ; Classic's second hand
    DKLC LC_CYND, CLCYAN,    CLCYAN,   DKM_50     ; Memphis on white
    DKLC LC_YLWD, CYELLOW,   CYELLOW,  DKM_LITE
    DKLC LC_MAGB, CLMAGENTA, CLMAGENTA, DKM_BLK
    DKLC LC_NAVY, CBLUE,     CBLUE,    DKM_BLK    ; Neon's dial, Casio VGA
    DKLC LC_CYNW, CLCYAN,    CLCYAN,   DKM_WHT    ; ...their markings
    DKLC LC_MAGW, CLMAGENTA, CLMAGENTA, DKM_WHT
    DKLC LC_LCD,  CLGREEN,   CLGREEN,  DKM_WHT    ; Casio LCD's glass
    DKLC LC_YLWW, CYELLOW,   CYELLOW,  DKM_WHT    ; Casio VGA's digits
    DKLC LC_LRED, CLRED,     CLRED,    DKM_WHT    ; red LED, lit...
    DKLC LC_RDIM, CRED,      CBLACK,   DKM_DARK   ; ...and dark
    DKLC LC_AMB,  CYELLOW,   CLRED,    DKM_WHT    ; amber: yellow + red
    DKLC LC_ADIM, CBROWN,    CBLACK,   DKM_DARK
    DKLC LC_SEL,  CBLACK,    CBLACK,   DKM_BLK    ; the settings' selection
dk_mpat:    dw 00000h, 0FFFFh, 055AAh, 077FFh, 08800h   ; row 0 low byte

; -----------------------------------------------------------------------------
; ck_ink - AL = a logical colour: the next spans' pattern. Keeps everything
; -----------------------------------------------------------------------------
ck_ink:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, al
    xor bh, bh
    mov si, bx
    shl si, 1
    add si, bx
    add si, dk_lc
    mov word [ck_pp], ck_pat
    mov byte [ck_par], 0
    cmp byte [ck_np], 1
    je .mono
    mov dl, [si]
    mov dh, [si+1]
    mov bx, ck_pat
    mov cx, 4
.p:
    xor ax, ax                  ; AL = row 0, AH = row 1: c1 on the even
    test dl, 1                  ; pixels of an even row, c2 on the odd
    jz .p2
    or ax, 055AAh
.p2:
    test dh, 1
    jz .p3
    or ax, 0AA55h
.p3:
    mov [bx], ax
    shr dl, 1
    shr dh, 1
    add bx, 2
    loop .p
    jmp short .x
.mono:
    mov bl, [si+2]
    xor bh, bh
    shl bx, 1
    mov ax, [dk_mpat+bx]
    mov [ck_pat], ax
.x:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ck_ground - the next spans lay the DESKTOP (ck_gnd, absolute parity)
ck_ground:
    push ax
    mov word [ck_pp], ck_gnd
    mov al, [ck_gpar]
    mov [ck_par], al
    pop ax
    ret

; ck_gwhite - the ground table is plain white (a titled fallback, a window)
ck_gwhite:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, ck_gnd
    mov ax, 0FFFFh
    mov cx, 4
    cld
    rep stosw
    mov byte [ck_gpar], 0
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ck_setband - BX = the canvas's width in px (a multiple of 8, <= DK_BANDW):
; the stride, the planes ([ck_np] already set) and the slab's height, the
; most rows the buffer holds (at most DK_SLABMAX). Keeps everything
; -----------------------------------------------------------------------------
ck_setband:
    push ax
    push bx
    push cx
    push dx
    mov cl, 3
    shr bx, cl
    mov [ck_st], bx
    mov al, [ck_np]
    xor ah, ah
    mul bx                      ; bytes a row, all planes
    mov cx, ax
    mov ax, DK_BANDSZ
    xor dx, dx
    div cx
    cmp ax, DK_SLABMAX
    jbe .s
    mov ax, DK_SLABMAX
.s:
    mov [ck_sh], ax
    mul bx
    mov [ck_ps], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ck_clear - every row of the slab in the current pattern (the ground first,
; under everything). Keeps everything
ck_clear:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, dk_band
    mov si, [ck_pp]
    xor dx, dx                  ; DL = the plane
.pl:
    xor bx, bx                  ; BX = the slab row
.row:
    mov ax, bx
    add ax, [ck_y0]
    add al, [ck_par]
    and ax, 1
    push si
    add si, ax
    mov al, [si]
    pop si
    mov cx, [ck_st]
    rep stosb
    inc bx
    cmp bx, [ck_sh]
    jb .row
    add si, 2
    inc dl
    cmp dl, [ck_np]
    jb .pl
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ck_span - one row of pixels in the current pattern: AX = x0, CX = x1, DX = y
; (inclusive, canvas less the origin). ck_spanc takes them with the origin
; already added. Clipped to the slab and to [ck_cx0]..[ck_cx1]. Clobbers AX,
; BX, CX, DX
; -----------------------------------------------------------------------------
ck_span:
    add ax, [ck_ox]
    add cx, [ck_ox]
    add dx, [ck_oy]
ck_spanc:
    cmp ax, [ck_cx0]
    jge .a
    mov ax, [ck_cx0]
.a:
    cmp cx, [ck_cx1]
    jle .b
    mov cx, [ck_cx1]
.b:
    cmp ax, cx
    jg .r
    sub dx, [ck_y0]
    jl .r
    cmp dx, [ck_sh]
    jl .in
.r:
    ret
.in:
    push si
    push di
    push bp
    push es
    push ds
    pop es
    mov bx, dx                  ; BX = the slab row
    push ax
    mov ax, dx
    mul word [ck_st]
    mov di, ax
    add di, dk_band             ; DI = the row in plane 0
    pop ax
    add bx, [ck_y0]
    add bl, [ck_par]
    and bx, 1
    add bx, [ck_pp]             ; BX = the pattern, plane 0, this parity
    mov bp, cx                  ; BP = x1
    mov dx, ax                  ; DX = x0
    mov cl, 3
    shr ax, cl
    add di, ax                  ; DI = the first byte
    mov si, bp
    shr si, cl
    sub si, ax                  ; SI = bytes - 1
    mov cl, dl
    and cl, 7
    mov ah, 0FFh
    shr ah, cl                  ; AH = the first byte's mask
    mov cx, bp
    and cl, 7
    mov al, 80h
    sar al, cl                  ; AL = the last byte's
    or si, si
    jnz .m
    and ah, al                  ; one byte: both
.m:
    mov cl, [ck_np]
    xor ch, ch
    cld
.pl:
    push cx
    push di
    mov dl, [bx]
    mov dh, [di]                ; [di] ^= ([di] ^ pattern) & mask
    xor dh, dl
    and dh, ah
    xor [di], dh
    or si, si
    jz .nx
    inc di
    mov cx, si
    dec cx
    jcxz .last
    push ax
    mov al, dl
    rep stosb
    pop ax
.last:
    mov dh, [di]
    xor dh, dl
    and dh, al
    xor [di], dh
.nx:
    pop di
    pop cx
    add di, [ck_ps]
    add bx, 2
    loop .pl
    pop es
    pop bp
    pop di
    pop si
    ret

; ck_rect - AX = x, BX = y, CX = w, DX = h (canvas less the origin), filled.
; Keeps everything
ck_rect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    or cx, cx
    jle .x
    mov si, ax                  ; SI = x0
    mov di, ax
    add di, cx
    dec di                      ; DI = x1
    add dx, bx                  ; DX = the end row
.r:
    cmp bx, dx
    jge .x
    push bx
    push dx
    mov ax, si
    mov cx, di
    mov dx, bx
    call ck_span
    pop dx
    pop bx
    inc bx
    jmp short .r
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ck_rows - CF = 1 when rows BX .. BX + DX - 1 (canvas less the origin) miss
; the slab altogether. Keeps everything
ck_rows:
    push ax
    push bx
    add bx, [ck_oy]
    mov ax, [ck_y0]
    add ax, [ck_sh]
    cmp bx, ax
    jge .no                     ; starts below it
    add bx, dx
    cmp bx, [ck_y0]
    jle .no                     ; ends above it
    clc
    jmp short .x
.no:
    stc
.x:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; THE FONT: the kernel's 8x8 table (OSAPI_FONT_GLYPHS), asked for at the top
; of every paint - a resume may have moved it (SPEC.md 5.12.8)
; -----------------------------------------------------------------------------
ck_font:
    push ax
    push cx
    push dx
    push si
    call OSAPI_FONT_GLYPHS
    mov [ck_fseg], dx
    mov [ck_foff], si
    mov [ck_ffst], al
    mov [ck_flst], ah
    pop si
    pop dx
    pop cx
    pop ax
    ret

; ck_glyph - AL = a character at CX = x, DX = y (canvas less the origin), x
; [ck_gsx] wide a bit and [ck_gsy] tall, [ck_gbold] = 1 bold. Out CX past it.
; One span a RUN of lit bits a row
ck_glyph:
    push ax
    push bx
    push dx
    push si
    push di
    push bp
    push cx
    push ax                     ; the character
    mov bx, dx                  ; the rows it covers
    mov al, [ck_gsy]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    push dx
    mov dx, ax
    call ck_rows
    pop dx
    pop ax
    pop cx
    jc .adv
    cmp al, [ck_ffst]           ; off the table: a blank
    jb .adv
    cmp al, [ck_flst]
    ja .adv
    sub al, [ck_ffst]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [ck_foff]
    mov si, ax
    push ds
    push es
    push ds
    pop es
    mov di, dk_gl
    mov ds, [ck_fseg]
    movsw
    movsw
    movsw
    movsw
    pop es
    pop ds
..@ck_gldraw:                    ; (dk_bmp's way in: dk_gl filled, the
    mov si, dk_gl               ; registers pushed as above)
    mov bp, dx                  ; BP = this glyph row's first canvas row
.row:
    mov bl, [si]
    cmp byte [ck_gbold], 0
    je .nb
    mov al, bl
    shr al, 1
    or bl, al
.nb:
    xor di, di                  ; DI = the bit (0 = leftmost)
.bit:
    cmp di, 8
    jae .rd
    test bl, 80h
    jnz .run
    shl bl, 1
    inc di
    jmp short .bit
.run:
    mov ax, di                  ; AX = the run's first bit
.rr:
    shl bl, 1
    inc di
    cmp di, 8
    jae .re
    test bl, 80h
    jnz .rr
.re:
    push bx
    push cx
    push si
    push di
    mov bl, [ck_gsx]
    xor bh, bh
    push dx
    mul bx
    pop dx
    add ax, cx                  ; x0 = x + first * sx
    mov si, ax
    mov ax, di
    push dx
    mul bx
    pop dx
    add ax, cx
    dec ax                      ; x1 = x + end * sx - 1
    mov di, ax
    mov cl, [ck_gsy]
    xor ch, ch
    mov dx, bp
.ry:
    push cx
    push dx
    mov ax, si
    mov cx, di
    call ck_span
    pop dx
    pop cx
    inc dx
    loop .ry
    pop di
    pop si
    pop cx
    pop bx
    jmp short .bit
.rd:
    mov al, [ck_gsy]
    xor ah, ah
    add bp, ax
    inc si
    cmp si, dk_gl + 8
    jb .row
.adv:
    pop bp
    pop di
    pop si
    pop dx
    pop bx
    mov al, [ck_gsx]            ; CX past the cell
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add cx, ax
    pop ax
    ret

; dk_bmp - AL = a digit 0..9 in dk_dfont, the CHUNKY face Mono Digital and
; the flip clock letter in (the ROM's slashed zero scales into a blot): as
; ck_glyph, at CX, DX, [ck_gsx] x [ck_gsy], never bold. CX past 8 cells.
dk_bmp:
    push bx
    push si
    push di
    push es
    push ax
    push cx
    push ds
    pop es
    xor ah, ah
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1
    sub si, ax                  ; x 7 rows
    add si, dk_dfont
    mov di, dk_gl
    mov cx, 7
    cld
    rep movsb
    xor al, al
    stosb
    pop cx
    pop ax
    mov bl, [ck_gbold]
    mov byte [ck_gbold], 0
    call .d                     ; CX past the cell
    mov [ck_gbold], bl
    pop es
    pop di
    pop si
    pop bx
    ret
.d:                             ; ck_glyph's frame at ck_gldraw: six pushed,
    push ax                     ; CX = x and DX = y live
    push bx
    push dx
    push si
    push di
    push bp
    jmp ..@ck_gldraw

;              a 6 x 7 face, two-pixel strokes: bit 7 the leftmost column
dk_dfont:
    db 78h, 0CCh, 0CCh, 0CCh, 0CCh, 0CCh, 78h       ; 0
    db 30h, 70h, 30h, 30h, 30h, 30h, 78h            ; 1
    db 78h, 0CCh, 0Ch, 18h, 30h, 60h, 0FCh          ; 2
    db 78h, 0CCh, 0Ch, 38h, 0Ch, 0CCh, 78h          ; 3
    db 18h, 38h, 78h, 0D8h, 0FCh, 18h, 18h          ; 4
    db 0FCh, 0C0h, 0F8h, 0Ch, 0Ch, 0CCh, 78h        ; 5
    db 78h, 0C0h, 0F8h, 0CCh, 0CCh, 0CCh, 78h       ; 6
    db 0FCh, 0Ch, 18h, 30h, 60h, 60h, 60h           ; 7
    db 78h, 0CCh, 0CCh, 78h, 0CCh, 0CCh, 78h        ; 8
    db 78h, 0CCh, 0CCh, 7Ch, 0Ch, 18h, 70h          ; 9

; ck_text - SI = a NUL string at CX = x, DX = y: CX past it. Keeps the rest
ck_text:
    push ax
    push si
.c:
    lodsb
    or al, al
    jz .x
    call ck_glyph
    jmp short .c
.x:
    pop si
    pop ax
    ret

; ck_tw - SI = a NUL string: AX = its width at [ck_gsx]. Keeps the rest
ck_tw:
    push cx
    push dx
    push si
    xor cx, cx
.c:
    lodsb
    or al, al
    jz .x
    inc cx
    jmp short .c
.x:
    mov al, [ck_gsx]
    xor ah, ah
    mul cx
    shl ax, 1
    shl ax, 1
    shl ax, 1
    pop si
    pop dx
    pop cx
    ret

; ck_textc - SI = a string centred on CX = x, top DX = y. Keeps everything
ck_textc:
    push ax
    push cx
    call ck_tw
    shr ax, 1
    sub cx, ax
    call ck_text
    pop cx
    pop ax
    ret

; ck_sc1 - glyphs at x1, plain. Keeps everything
ck_sc1:
    mov byte [ck_gsx], 1
    mov byte [ck_gsy], 1
    mov byte [ck_gbold], 0
    ret

; -----------------------------------------------------------------------------
; ck_isqrt - AX -> AX = floor(sqrt(AX)). Keeps the rest
; -----------------------------------------------------------------------------
ck_isqrt:
    push bx
    push cx
    push dx
    mov cx, ax
    xor ax, ax
    mov bx, 4000h
.b:
    cmp bx, cx
    jbe .l
    shr bx, 1
    shr bx, 1
    jnz .b
.l:
    or bx, bx
    jz .x
    mov dx, ax
    add dx, bx
    cmp cx, dx
    jb .s
    sub cx, dx
    shr ax, 1
    add ax, bx
    jmp short .n
.s:
    shr ax, 1
.n:
    shr bx, 1
    shr bx, 1
    jmp short .l
.x:
    pop dx
    pop cx
    pop bx
    ret

; ck_ehw - an ellipse's half-width at a row: BX = rx, CX = ry (> 0), DX = j,
; the row from the centre (-ry .. ry - 1; the centre lies BETWEEN rows). Out
; AX = the half-width: the row is cx - AX .. cx + AX - 1. Keeps the rest
ck_ehw:
    push bx
    push cx
    push dx
    push si
    shl dx, 1
    inc dx                      ; t = 2j + 1, odd
    mov ax, dx
    imul dx                     ; AX = t^2 (fits: |t| < 256)
    mov si, ax
    mov ax, cx
    shl ax, 1
    mul ax                      ; AX = (2 ry)^2
    cmp si, ax
    jae .z
    mov cx, ax                  ; CX = R2
    sub ax, si                  ; AX = R2 - t^2
    push ax
    mov ax, bx
    mul bx
    add ax, bx                  ; rx^2 + rx
    pop dx
    mul dx                      ; DX:AX = (rx^2 + rx)(R2 - t^2)
    div cx
    call ck_isqrt
    jmp short .x
.z:
    xor ax, ax
.x:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; ck_ell - a filled ellipse centred BETWEEN pixels at AX = cx, BX = cy, rx =
; CX, ry = DX (canvas less the origin). Keeps everything
ck_ell:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    or cx, cx
    jz .x
    or dx, dx
    jz .x
    mov si, ax                  ; SI = cx
    mov di, cx                  ; DI = rx
    mov bp, dx                  ; BP = ry
    push bx
    sub bx, dx                  ; the rows: cy - ry ..
    shl dx, 1
    call ck_rows
    pop bx
    jc .x
    mov cx, bp
    neg cx                      ; CX = j
.j:
    cmp cx, bp
    jge .x
    push cx                     ; j
    push bx                     ; cy
    mov dx, cx
    mov bx, di
    mov cx, bp
    call ck_ehw                 ; AX = the half-width
    pop bx
    pop cx
    push bx
    push cx
    mov dx, bx
    add dx, cx                  ; the row
    mov cx, si
    add cx, ax
    dec cx
    neg ax
    add ax, si
    cmp ax, cx
    jg .e
    call ck_span
.e:
    pop cx
    pop bx
    inc cx
    jmp short .j
.x:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ck_rrect - a rounded rectangle: AX = x, BX = y, CX = w, DX = h, [ck_rx] /
; [ck_ry] the corner's radii (0: square). Keeps everything
ck_rrect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    or cx, cx
    jle .x
    or dx, dx
    jle .x
    call ck_rows
    jc .x
    mov si, ax                  ; SI = x
    mov di, cx                  ; DI = w
    mov bp, dx                  ; BP = h
    xor cx, cx                  ; CX = the row in it
.r:
    cmp cx, bp
    jge .x
    xor ax, ax                  ; AX = the inset
    mov dx, cx                  ; the row's distance into the nearer corner
    push bp
    sub bp, cx
    dec bp
    cmp bp, dx
    jge .n
    mov dx, bp
.n:
    pop bp
    cmp dx, [ck_ry]
    jge .in
    push bx
    push cx
    sub dx, [ck_ry]             ; j = -ry .. -1
    mov bx, [ck_rx]
    mov cx, [ck_ry]
    call ck_ehw
    neg ax
    add ax, [ck_rx]             ; inset = rx - hw
    pop cx
    pop bx
.in:
    push bx
    push cx
    push dx
    mov dx, bx
    add dx, cx                  ; the canvas row
    mov cx, si
    add cx, di
    dec cx
    sub cx, ax
    add ax, si
    call ck_span
    pop dx
    pop cx
    pop bx
    inc cx
    jmp short .r
.x:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ck_line - AX,BX to CX,DX (canvas less the origin) with a [ck_bw] x [ck_bh]
; brush centred on each point (Bresenham). Keeps everything
; -----------------------------------------------------------------------------
ck_line:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [dk_lx], cx
    mov [dk_ly], dx
    push dx                     ; the rows it may touch
    push bx
    cmp bx, dx
    jle .o
    xchg bx, dx
.o:
    sub dx, bx
    add dx, [ck_bh]
    inc dx
    push ax
    mov ax, [ck_bh]
    shr ax, 1
    sub bx, ax
    pop ax
    call ck_rows
    pop bx
    pop dx
    jc .x
    mov si, cx
    sub si, ax                  ; SI = dx (signed)
    mov word [dk_lsx], 1
    jge .px
    neg si
    mov word [dk_lsx], -1
.px:
    mov di, dx
    sub di, bx                  ; DI = dy
    mov word [dk_lsy], 1
    jge .py
    neg di
    mov word [dk_lsy], -1
.py:
    neg di                      ; DI = -|dy|
    mov bp, si
    add bp, di                  ; err = dx - dy
.pt:
    call ck_dab                 ; the brush at AX, BX
    cmp ax, [dk_lx]
    jne .st
    cmp bx, [dk_ly]
    je .x
.st:
    mov dx, bp
    shl dx, 1
    cmp dx, di
    jl .sy
    add bp, di
    add ax, [dk_lsx]
.sy:
    cmp dx, si
    jg .pt
    add bp, si
    add bx, [dk_lsy]
    jmp short .pt
.x:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ck_dab - the brush centred on AX, BX. Keeps everything
ck_dab:
    push ax
    push bx
    push cx
    push dx
    mov cx, [ck_bw]
    mov dx, cx
    shr dx, 1
    sub ax, dx
    mov dx, [ck_bh]
    push dx
    shr dx, 1
    sub bx, dx
    pop dx
    call ck_rect
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ck_brush - CX = the brush's width in px: [ck_bw], and [ck_bh] the rows
; that make it square on this screen (at least 1). Keeps everything
ck_brush:
    push ax
    push dx
    or cx, cx
    jnz .w
    inc cx
.w:
    mov [ck_bw], cx
    mov ax, cx
    mul word [dk_ys]
    add ax, 128
    adc dx, 0
    mov al, ah
    mov ah, dl
    or ax, ax
    jnz .h
    inc ax
.h:
    mov [ck_bh], ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ck_tri - the triangle at dk_tp (three x,y word pairs, canvas less the
; origin), filled, one span a row. Keeps everything
; -----------------------------------------------------------------------------
ck_tri:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov si, dk_tp               ; sort the three by y
    call .cmp01
    add si, 4
    call .cmp01                 ; (1, 2)
    sub si, 4
    call .cmp01
    mov bx, [dk_tp+2]           ; BX = the top row
    mov dx, [dk_tp+10]
    sub dx, bx
    inc dx
    call ck_rows
    jc .x
    mov bp, [dk_tp+2]
.row:
    cmp bp, [dk_tp+10]
    jg .x
    mov si, dk_tp               ; the long edge, 0 -> 2
    mov di, dk_tp+8
    call .edge
    mov cx, ax
    cmp bp, [dk_tp+6]
    jge .low
    mov di, dk_tp+4             ; 0 -> 1 above the middle vertex...
    jmp short .e2
.low:
    mov si, dk_tp+4             ; ...1 -> 2 below it
    mov di, dk_tp+8
.e2:
    call .edge
    cmp ax, cx
    jle .o
    xchg ax, cx
.o:
    mov dx, bp
    call ck_span
    inc bp
    jmp short .row
.x:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
; the edge from [si] to [di] at row BP: AX = its x
.edge:
    push bx
    push cx
    push dx
    mov cx, [di+2]
    sub cx, [si+2]              ; CX = its rows
    mov ax, [si]
    or cx, cx
    jz .ex
    mov ax, [di]
    sub ax, [si]
    mov bx, bp
    sub bx, [si+2]
    imul bx
    idiv cx
    add ax, [si]
.ex:
    pop dx
    pop cx
    pop bx
    ret
; swap the pairs at [si] and [si+4] when the first is lower
.cmp01:
    mov ax, [si+2]
    cmp ax, [si+6]
    jle .cr
    xchg ax, [si+6]
    mov [si+2], ax
    mov ax, [si]
    xchg ax, [si+4]
    mov [si], ax
.cr:
    ret
