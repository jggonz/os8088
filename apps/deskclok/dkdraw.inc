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
; ck_ourband - the composer draws into dk_band again. Keeps everything
ck_ourband:
    mov [ck_bseg], ds
    mov word [ck_buf], dk_band
    mov word [ck_lrow], 8000h   ; ck_span's row cache: no row
    ret

; ck_setband - BX = the canvas's width in px (a multiple of 8, <= DK_BANDW):
; the stride, the planes ([ck_np] already set) and the slab's height, the
; most rows the buffer holds (at most DK_SLABMAX). Keeps everything
; -----------------------------------------------------------------------------
ck_setband:
    push ax
    push bx
    push cx
    push dx
    call ck_ourband
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
    mov [dk_shf], ax
    mov [ck_crow], al
    mul bx
    mov [ck_ps], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ck_clear - the slab's live rows ([ck_crow]) in the current pattern, in the
; byte columns [ck_cx0]..[ck_cx1] reach (the ground first, under everything):
; a hand's box clears its own columns, not the card's. Keeps everything
ck_clear:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov es, [ck_bseg]
    cld
    mov ax, [ck_cx0]
    sar ax, 1
    sar ax, 1
    sar ax, 1
    mov di, ax                  ; DI = the first byte column...
    mov dx, [ck_cx1]
    sar dx, 1
    sar dx, 1
    sar dx, 1
    sub dx, ax
    jl .x
    inc dx                      ; DX = ...and the bytes a row
    add di, [ck_buf]
    mov bp, [ck_st]
    sub bp, dx                  ; BP = the step past a row's bytes
    mov si, [ck_pp]
    mov bl, [ck_np]
.pl:
    push di
    mov al, [si]                ; AL = the first row's byte, AH = the next's
    mov ah, [si+1]
    mov cx, [ck_y0]
    add cl, [ck_par]
    test cl, 1
    jz .go
    xchg al, ah
.go:
    mov bh, [ck_crow]
.row:
    mov cx, dx
    rep stosb
    add di, bp
    xchg al, ah                 ; the next row's parity
    dec bh
    jnz .row
    pop di
    add di, [ck_ps]
    add si, 2
    dec bl
    jnz .pl
.x:
    pop es
    pop bp
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
    push es
    mov es, [ck_bseg]           ; the band: ours, or a claim
    cmp dx, [ck_lrow]           ; the row's base: the last span's, its next
    jne .nr                     ; one stride on, or a multiply
    mov di, [ck_lrdi]
    jmp short .hr
.nr:
    push ax
    mov ax, dx
    dec ax
    cmp ax, [ck_lrow]
    jne .mu
    mov di, [ck_lrdi]
    add di, [ck_st]
    jmp short .st
.mu:
    mov ax, dx
    push dx
    mul word [ck_st]
    pop dx
    add ax, [ck_buf]
    mov di, ax
.st:
    pop ax
    mov [ck_lrow], dx
    mov [ck_lrdi], di
.hr:
    mov bx, dx                  ; BX = the pattern: this row's parity, plane 0
    add bx, [ck_y0]
    add bl, [ck_par]
    and bx, 1
    add bx, [ck_pp]
    mov dx, ax                  ; DX = x0
    mov si, cx
    shr si, 1
    shr si, 1
    shr si, 1                   ; SI = the last byte column
    push bx
    mov bx, cx
    and bx, 7
    mov ch, [ck_rmt + bx]       ; CH = the last byte's mask
    mov bx, dx
    and bx, 7
    mov cl, [ck_lmt + bx]       ; CL = the first's
    pop bx
    shr dx, 1
    shr dx, 1
    shr dx, 1
    add di, dx                  ; DI = the first byte
    sub si, dx                  ; SI = bytes - 1
    jnz .multi
    and cl, ch                  ; ONE byte: both masks, a plane at a time
    mov ch, [ck_np]
.one:
    mov al, [es:di]
    xor al, [bx]
    and al, cl
    xor [es:di], al
    add di, [ck_ps]
    add bx, 2
    dec ch
    jnz .one
    jmp short .done
.multi:
    mov ah, [ck_np]
    cld
.pl:
    mov dl, [bx]                ; DL = this plane's pattern
    push di
    mov al, [es:di]             ; the first byte, under its mask
    xor al, dl
    and al, cl
    xor [es:di], al
    inc di
    push cx
    mov cx, si
    dec cx
    jcxz .last
    mov al, dl                  ; the middle, whole
    rep stosb
.last:
    pop cx
    mov al, [es:di]             ; the last, under its mask
    xor al, dl
    and al, ch
    xor [es:di], al
    pop di
    add di, [ck_ps]
    add bx, 2
    dec ah
    jnz .pl
.done:
    pop es
    pop di
    pop si
    ret

ck_lmt:     db 0FFh, 7Fh, 3Fh, 1Fh, 0Fh, 07h, 03h, 01h
ck_rmt:     db 80h, 0C0h, 0E0h, 0F0h, 0F8h, 0FCh, 0FEh, 0FFh


; -----------------------------------------------------------------------------
; ck_mrow - ONE ROW OF A BIT PATTERN in the current ink: [ck_mw] (a dword,
; bit 31 the leftmost pixel) at AX = x, DX = y (canvas less the origin),
; clipped to the slab and to [ck_cx0]..[ck_cx1]. A glyph's row or an icon's
; is up to five bytes of plane writes, where a span a run was a call each
; (SPEC.md 110.14.4). Clobbers AX, BX, CX, DX
; -----------------------------------------------------------------------------
ck_mrow:
    add ax, [ck_ox]
    add dx, [ck_oy]
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
    mov es, [ck_bseg]
    mov bp, ax                  ; BP = x
    mov bx, dx
    mov ax, bx                  ; the pattern for this row's parity
    add ax, [ck_y0]
    add al, [ck_par]
    and ax, 1
    add ax, [ck_pp]
    mov si, ax
    mov ax, bx                  ; DI = the row, plane 0
    mul word [ck_st]
    add ax, [ck_buf]
    mov di, ax
    mov ax, [ck_mw+2]           ; the 32 bits onto the byte grid: five bytes
    mov dx, [ck_mw]
    mov cx, bp
    and cx, 7
    xor bx, bx
    jcxz .ns
.sh:
    shr ax, 1
    rcr dx, 1
    rcr bh, 1
    loop .sh
.ns:
    mov [dk_mb], ah
    mov [dk_mb+1], al
    mov [dk_mb+2], dh
    mov [dk_mb+3], dl
    mov [dk_mb+4], bh
    mov ax, bp                  ; AX = the first byte's column
    sar ax, 1
    sar ax, 1
    sar ax, 1
    xor bx, bx                  ; BX = the byte, 0..4
.k:
    mov dl, [dk_mb+bx]
    or dl, dl
    jz .nk
    push ax
    mov cx, ax                  ; the clip: whole bytes from dk_cols, or the
    shl cx, 1                   ; pixel columns of a window's own
    shl cx, 1
    shl cx, 1                   ; CX = the byte's first pixel
    cmp cx, [ck_cx1]
    jg .nk1
    push cx
    add cx, 7
    cmp cx, [ck_cx0]
    pop cx
    jl .nk1
    cmp cx, [ck_cx0]            ; a byte the left edge cuts
    jge .l
    push cx
    mov cx, [ck_cx0]
    and cl, 7
    mov dh, 0FFh
    shr dh, cl
    and dl, dh
    pop cx
.l:
    add cx, 7                   ; ...the right edge
    cmp cx, [ck_cx1]
    jle .w
    mov cx, [ck_cx1]
    and cl, 7
    mov dh, 80h
    sar dh, cl
    and dl, dh
.w:
    push di
    push si
    add di, ax                  ; the byte, plane 0
    mov cl, [ck_np]
.p:
    mov dh, [es:di]
    xor dh, [si]
    and dh, dl
    xor [es:di], dh
    add di, [ck_ps]
    add si, 2
    dec cl
    jnz .p
    pop si
    pop di
.nk1:
    pop ax
.nk:
    inc ax
    inc bx
    cmp bx, 5
    jb .k
    pop es
    pop bp
    pop di
    pop si
    ret

; ck_g1 - dk_gl's eight rows at CX, BP (canvas less the origin), x1 or x2
; across, [ck_gsy] down, the glyph wholly inside [ck_cx0]..[ck_cx1]: the
; glyph's tight loop - a row is one shift and three masked bytes a plane,
; however many rows tall. [ck_gbold] honoured. Clobbers AX, BX, CX, DX, SI,
; DI, BP (ck_glyph's frame saves them)
ck_g1:
    push es
    mov es, [ck_bseg]
    mov ax, cx
    add ax, [ck_ox]
    mov cx, ax
    and cl, 7
    mov [dk_gsh], cl            ; the shift onto the byte grid
    sar ax, 1
    sar ax, 1
    sar ax, 1
    add ax, [ck_buf]
    mov [dk_gx], ax             ; the glyph's first byte column, in the band
    add bp, [ck_oy]
    sub bp, [ck_y0]             ; BP = its first slab row
    mov si, dk_gl
.r:
    mov al, [si]
    cmp byte [ck_gbold], 0
    je .b
    mov ah, al
    shr ah, 1
    or al, ah
.b:
    mov dh, al                  ; DX = the row, x1...
    xor dl, dl
    cmp byte [ck_gsx], 1
    je .e
    mov ah, al                  ; ...or each bit twice, x2
    mov cx, 8
.d:
    shl ah, 1
    pushf
    rcl dx, 1
    popf
    rcl dx, 1
    loop .d
.e:
    xor bl, bl                  ; DH, DL, BL = the three bytes' masks
    mov cl, [dk_gsh]
    xor ch, ch
    jcxz .s
.sh:
    shr dx, 1
    rcr bl, 1
    loop .sh
.s:
    mov bh, [ck_gsy]            ; the row, [ck_gsy] times
.y:
    or bp, bp
    jl .ny
    cmp bp, [ck_sh]
    jge .x
    mov ax, dx
    or al, ah
    or al, bl
    jz .ny                      ; an empty row: nothing to write
    push bx
    push dx
    mov ax, bp
    mul word [ck_st]
    add ax, [dk_gx]
    mov di, ax
    mov ax, bp
    add ax, [ck_y0]
    add al, [ck_par]
    and ax, 1
    add ax, [ck_pp]
    mov cx, ax                  ; CX = the pattern, plane 0
    pop dx
    pop bx
    push bx
    push si
    mov si, cx
    mov ch, [ck_np]
.p:
    mov cl, [si]
    mov al, [es:di]
    xor al, cl
    and al, dh
    xor [es:di], al
    mov al, [es:di+1]
    xor al, cl
    and al, dl
    xor [es:di+1], al
    or bl, bl
    jz .p2
    mov al, [es:di+2]
    xor al, cl
    and al, bl
    xor [es:di+2], al
.p2:
    add di, [ck_ps]
    add si, 2
    dec ch
    jnz .p
    pop si
    pop bx
.ny:
    inc bp
    dec bh
    jnz .y
    inc si
    cmp si, dk_gl + 8
    jb .r
.x:
    pop es
    ret

; ck_expand - AL = 8 pattern bits (bit 7 the leftmost), CL = the scale
; (1..4): [ck_mw] = each bit CL times, from bit 31 down. Keeps everything
ck_expand:
    push ax
    push bx
    push cx
    push dx
    xor bx, bx                  ; DX:BX = the dword, built from the left
    xor dx, dx
    mov ch, 8
.b:
    push cx
    mov ah, al
    xor ch, ch
.r:
    shl ah, 1                   ; this bit, CL times (AH keeps a copy)
    rcl bx, 1
    rcl dx, 1
    mov ah, al
    loop .r
    pop cx
    shl al, 1
    dec ch
    jnz .b
    mov al, 8                   ; left-aligned: shifted up 32 - 8 x scale
    mul cl
    mov cl, 32
    sub cl, al
    xor ch, ch
    jcxz .x
.al:
    shl bx, 1
    rcl dx, 1
    loop .al
.x:
    mov [ck_mw], bx
    mov [ck_mw+2], dx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ck_rect - AX = x, BX = y, CX = w, DX = h (canvas less the origin), filled
; in the current pattern: clipped ONCE, its masks found once, then row after
; row a stride apart - a frame's 1-px side is a bit a plane a row, not a
; span call a row (4,762 calls were a settings paint's). Keeps everything
ck_rect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    or cx, cx
    jle .x
    add dx, bx                  ; the rows, cut to the slab
    call ck_rowcut
    cmp bx, dx
    jge .x
    mov bp, dx
    sub bp, bx                  ; BP = the rows
    add bx, [ck_oy]
    sub bx, [ck_y0]             ; BX = the first slab row
    add ax, [ck_ox]             ; the columns, cut to [ck_cx0]..[ck_cx1]
    mov dx, ax
    add dx, cx
    dec dx
    cmp ax, [ck_cx0]
    jge .a
    mov ax, [ck_cx0]
.a:
    cmp dx, [ck_cx1]
    jle .b
    mov dx, [ck_cx1]
.b:
    cmp ax, dx
    jg .x
    mov es, [ck_bseg]
    push bx                     ; the masks and the bytes
    mov bx, dx
    and bx, 7
    mov ch, [ck_rmt + bx]
    mov bx, ax
    and bx, 7
    mov cl, [ck_lmt + bx]
    pop bx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr dx, 1
    shr dx, 1
    shr dx, 1
    sub dx, ax                  ; DX = bytes - 1
    jnz .w
    and cl, ch
.w:
    mov [dk_rw], dx
    mov [dk_rm], cx
    push ax                     ; DI = the first byte of the first row
    mov ax, bx
    push dx
    mul word [ck_st]
    pop dx
    add ax, [ck_buf]
    pop dx
    add ax, dx
    mov di, ax
    mov ax, bx                  ; the first row's parity
    add ax, [ck_y0]
    add al, [ck_par]
    and ax, 1
    mov [dk_rpar], al
    mov si, [ck_pp]
    mov dh, [ck_np]
    cld
    cmp word [dk_rw], 0         ; ONE byte wide (a frame's side): the tight
    jne .pl                     ; loop - a stride in BX, nothing on the stack
    mov dl, cl                  ; DL = the mask
.vp:
    push di
    push bp
    mov bl, [dk_rpar]
    xor bh, bh
    mov al, [si + bx]
    xor bl, 1
    mov ah, [si + bx]
    mov bx, [ck_st]
.vr:
    mov cl, [es:di]
    xor cl, al
    and cl, dl
    xor [es:di], cl
    add di, bx
    xchg al, ah
    dec bp
    jnz .vr
    pop bp
    pop di
    add di, [ck_ps]
    add si, 2
    dec dh
    jnz .vp
    jmp .x
.pl:                            ; a plane at a time, every row of it
    push di
    push bp
    mov bl, [dk_rpar]
    xor bh, bh
    mov al, [si + bx]           ; AL = the first row's byte, AH = the next's
    xor bl, 1
    mov ah, [si + bx]
.row:
    push di
    mov bx, [dk_rm]             ; BL = the first mask, BH = the last
    mov cx, [dk_rw]
    jcxz .one
    mov dl, [es:di]
    xor dl, al
    and dl, bl
    xor [es:di], dl
    inc di
    dec cx
    jz .lst
    push ax
    rep stosb
    pop ax
.lst:
    mov bl, bh
.one:
    mov dl, [es:di]
    xor dl, al
    and dl, bl
    xor [es:di], dl
    pop di
    add di, [ck_st]
    xchg al, ah                 ; the next row's parity
    dec bp
    jnz .row
    pop bp
    pop di
    add di, [ck_ps]
    add si, 2
    dec dh
    jnz .pl
.x:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ck_rowcut - rows BX up to DX (exclusive, canvas less the origin) cut to
; the slab's: a loop over them never visits a row it would throw away. A
; range it misses comes back empty (BX >= DX). Keeps the rest
ck_rowcut:
    push ax
    mov ax, [ck_y0]
    sub ax, [ck_oy]             ; AX = the slab's first row, origin-relative
    cmp bx, ax
    jge .a
    mov bx, ax
.a:
    add ax, [ck_sh]
    cmp dx, ax
    jle .b
    mov dx, ax
.b:
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
    push ax                     ; ...and its columns: a glyph left or right
    mov ax, cx                  ; of what is being composed is not drawn
    add ax, [ck_ox]
    cmp ax, [ck_cx1]
    jg .cx
    push bx
    mov bl, [ck_gsx]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add ax, bx
    pop bx
    cmp ax, [ck_cx0]
    jle .cx
    pop ax
    jmp short .cin
.cx:
    pop ax
    jmp .adv
.cin:
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
    cmp byte [ck_gsx], 2        ; x1 or x2 and wholly inside the columns:
    ja .nf                      ; three masked bytes a row, a plane at a
    mov ax, cx                  ; time
    add ax, [ck_ox]
    cmp ax, [ck_cx0]
    jl .nf
    add ax, 7
    cmp byte [ck_gsx], 1
    je .w1
    add ax, 8
.w1:
    cmp ax, [ck_cx1]
    jg .nf
    push cx
    call ck_g1
    pop cx
    jmp .adv
.nf:
    cmp byte [ck_gsx], 4        ; up to x4 the row is ONE mask row (ck_mrow);
    ja .row                     ; wider, a span a run
.mr:
    mov al, [si]
    cmp byte [ck_gbold], 0
    je .mb
    mov ah, al
    shr ah, 1
    or al, ah
.mb:
    or al, al
    jz .mn
    push cx
    mov cl, [ck_gsx]
    call ck_expand
    pop cx
    mov bl, [ck_gsy]
    mov dx, bp
.my:
    push bx
    push cx
    push dx
    mov ax, cx
    call ck_mrow
    pop dx
    pop cx
    pop bx
    inc dx
    dec bl
    jnz .my
.mn:
    mov al, [ck_gsy]
    xor ah, ah
    add bp, ax
    inc si
    cmp si, dk_gl + 8
    jb .mr
    jmp .adv
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

; ck_rootg - AX -> AX = floor(sqrt(AX)), from the guess [ck_hwg] (the last
; row's root: the next row's is a step or two from it) - a multiply a step
; instead of ck_isqrt's eight rounds. [ck_hwg] = 0 asks ck_isqrt. The answer
; becomes the next guess. Keeps the rest
ck_rootg:
    push bx
    push cx
    push dx
    mov cx, ax                  ; CX = N
    mov bx, [ck_hwg]
    or bx, bx
    jnz .up
    call ck_isqrt
    mov bx, ax
    jmp short .x
.up:
    mov ax, bx                  ; up while (g + 1)^2 <= N
    inc ax
    mul ax
    cmp ax, cx
    ja .dn
    inc bx
    jmp short .up
.dn:
    mov ax, bx                  ; down while g^2 > N
    mul ax
    cmp ax, cx
    jbe .x
    dec bx
    jnz .dn
.x:
    mov [ck_hwg], bx
    mov ax, bx
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
    call ck_rootg               ; ...from the last row's answer
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
    mov word [ck_hwg], 0        ; the root: from scratch for the first row
    push bx
    sub bx, dx                  ; the rows: cy - ry ..
    shl dx, 1
    call ck_rows
    pop bx
    jc .x
    push bx                     ; j from the slab's first row to its last:
    push dx                     ; cy + j inside it
    mov dx, bx
    add dx, bp                  ; DX = past the last row, BX = the first
    sub bx, bp
    call ck_rowcut
    mov cx, bx                  ; (CX = j = row - cy)
    mov ax, dx
    pop dx
    pop bx
    sub cx, bx
    sub ax, bx
    mov [dk_eend], ax
.j:
    cmp cx, [dk_eend]
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
    mov word [ck_hwg], 0
    push bx                     ; the rows in it the slab holds: CX from
    add dx, bx                  ; there up to [dk_rend]
    call ck_rowcut
    mov cx, bx
    mov ax, dx
    pop bx
    sub cx, bx
    sub ax, bx
    mov [dk_rend], ax
    push ax                     ; the straight middle, ry .. h - ry: ONE
    push bx                     ; rectangle (ck_rect), the corners' rows
    push cx                     ; a span each below
    push dx
    mov ax, si
    add bx, [ck_ry]
    mov cx, di
    mov dx, bp
    sub dx, [ck_ry]
    sub dx, [ck_ry]
    call ck_rect
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    cmp cx, [dk_rend]
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
    jl .cor
    inc cx                      ; the middle: drawn above
    jmp short .r
.cor:
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
; ck_line - AX,BX to CX,DX (canvas less the origin), [ck_bw] px wide and
; [ck_bh] rows thick: ONE SPAN A ROW - the x the line reaches between the
; brush's top and bottom rows at that row (half a row more each way, so a
; shallow line's run is whole), widened by the brush. Rows outside the slab
; cost nothing; inside it the two ends step by an 8.8 slope - an add a row,
; no multiply, no divide - and a row past an end takes the end's x exactly.
; Keeps everything
; -----------------------------------------------------------------------------
ck_line:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp bx, dx                  ; top to bottom
    jle .o
    xchg ax, cx
    xchg bx, dx
.o:
    mov [dk_lx0], ax
    mov [dk_lx1], cx
    shl bx, 1
    mov [dk_ly0], bx            ; doubled rows from here: 2 y0 ...
    shl dx, 1
    mov [dk_ly1], dx            ; ...and 2 y1
    sub cx, ax                  ; CX = dx
    sub dx, bx                  ; DX = 2 dy
    mov word [dk_lk], 0
    or dx, dx
    jz .k
    mov ax, cx                  ; the slope, x a doubled row, 8.8
    mov bx, dx
    mov dx, 256
    imul dx
    idiv bx
    mov [dk_lk], ax
.k:
    mov si, [ck_bh]
    dec si
    mov di, si
    shr si, 1                   ; SI = the brush's rows above its row,
    sub di, si                  ; DI = ...below
    shl si, 1
    inc si
    mov [dk_lhu], si            ; doubled, and half a row more
    shl di, 1
    inc di
    mov [dk_lhd], di
    mov bp, [dk_ly0]            ; BP = the first row: y0 - above, cut to the
    sub bp, [dk_lhu]            ; slab (doubled)
    inc bp
    sar bp, 1
    mov ax, [ck_y0]
    sub ax, [ck_oy]
    cmp bp, ax
    jge .f
    mov bp, ax
.f:
    add ax, [ck_sh]
    dec ax                      ; AX = the slab's last row
    mov dx, [dk_ly1]            ; DX = the last row: y1 + below
    add dx, [dk_lhd]
    dec dx
    sar dx, 1
    cmp dx, ax
    jle .l
    mov dx, ax
.l:
    mov [dk_lend], dx
    mov ax, bp                  ; the two ends' 8.8 offsets at the first row:
    shl ax, 1                   ; (2 row - above - 2 y0) x k, and below's
    sub ax, [dk_lhu]
    mov [dk_lta], ax            ; (their doubled rows, to clamp against)
    sub ax, [dk_ly0]
    imul word [dk_lk]
    mov [dk_lfa], ax
    mov ax, bp
    shl ax, 1
    add ax, [dk_lhd]
    mov [dk_ltb], ax
    sub ax, [dk_ly0]
    imul word [dk_lk]
    mov [dk_lfb], ax
    mov ax, [dk_lk]
    shl ax, 1
    mov [dk_lk2], ax            ; a row is two doubled rows
.row:
    cmp bp, [dk_lend]
    jg .x
    mov ax, [dk_lta]            ; the top end's x
    mov bx, [dk_lfa]
    call .xat
    mov si, ax
    mov ax, [dk_ltb]            ; ...the bottom end's
    mov bx, [dk_lfb]
    call .xat
    mov cx, ax
    cmp si, cx
    jle .s
    xchg si, cx
.s:
    mov ax, [ck_bw]
    dec ax
    mov bx, ax
    shr ax, 1
    sub bx, ax                  ; AX = the brush left of the line, BX right
    sub si, ax
    add cx, bx
    mov ax, si
    mov dx, bp
    call ck_span
    mov ax, [dk_lk2]            ; one row on
    add [dk_lfa], ax
    add [dk_lfb], ax
    add word [dk_lta], 2
    add word [dk_ltb], 2
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
; AX = a doubled row, BX = its 8.8 offset: AX = the line's x there - its
; ends' own x past them
.xat:
    cmp ax, [dk_ly0]
    jg .a
    mov ax, [dk_lx0]
    ret
.a:
    cmp ax, [dk_ly1]
    jl .b
    mov ax, [dk_lx1]
    ret
.b:
    mov ax, bx
    add ax, 128                 ; rounded
    mov al, ah
    cbw
    add ax, [dk_lx0]
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
    mov bx, [dk_tp+2]           ; the rows the slab holds
    mov dx, [dk_tp+10]
    inc dx
    call ck_rowcut
    mov bp, bx
    dec dx
    mov [dk_rend], dx
.row:
    cmp bp, [dk_rend]
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
