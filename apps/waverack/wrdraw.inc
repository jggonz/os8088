; =============================================================================
; os8088 - apps/waverack/wrdraw.inc - WaveRack's drawing primitives
;          (SPEC.md 122.11; apps/cdrack/crdraw.inc's shape, SPEC.md 119.6.3)
;
; Everything WaveRack puts on the glass goes through these, so a THEME is one
; table: a primitive is handed a ROLE (R_*), never a colour, and looks it up
; in [wd_th] - the colour theme's table or the black-and-white one. A role's
; byte is a colour 0..15, or 80h + a pattern for the black-and-white face's
; dithered surfaces (OSAPI_GFX_FILL_PAT, which writes only 0 and 15).
;
; COORDINATES ARE CONTENT-RELATIVE: every primitive adds [wd_ox]/[wd_oy], the
; content origin wd_origin banked at the top of this pass.
;
; PICTURES AND THE SMALL CAPS ARE COMPOSED INTO ONE BAND and put down in ONE
; OSAPI_GFX_BLIT1 under a pen (SPEC.md 5.4.2): an LCD field, a caption, a
; whole button's face is one call whatever is in it, and the band's clear
; bits are its paper - so a field is erased and lettered in the same pass and
; no pixel is written twice (SPEC.md 6.1's rule, said for pictures).
;
; THE 1BPP POLARITY IS OURS: a band there is lit-is-white and the pen is not
; read (OSAPI_GFX_BLIT1_PEN), so black ink on white paper is the band
; INVERTED before the blit (wd_bout). A greyed caption is the band ANDed with
; a checkerboard first (SPEC.md 47 rule 1, the 1bpp way) - on the colour face
; it is the R_BTNDIS ink instead.
;
; THE CUT FLAG: every primitive asks OSAPI_WM_CLIP_TEST about what it drew
; and sets [wd_cutf] when the clip cut it, which is how a cached field that
; was drawn half-hidden is forgotten and drawn again (wrface.inc, wf_ekdone).
;
; Every routine here preserves every register unless it says otherwise.
; =============================================================================

; -----------------------------------------------------------------------------
; wd_origin - bank the content origin, and the button and drop-down rects
;             from it
; in:  BX = the window
;
; A window that MOVES does not call its paint (SPEC.md 11.96.12), so this runs
; at the top of every pass that draws or hit-tests - the paint, the tick and
; every click - never once.
; -----------------------------------------------------------------------------
wd_origin:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [wd_ox], ax
    mov [wd_oy], dx
    mov si, [wd_lp]                 ; the layout's relative rects...
    mov si, [si+WL_BTNS]
    mov di, wd_rects                ; ...made absolute (a parked one stays
    mov cx, WR_NB                   ; parked: it is off every screen)
.one:
    mov bx, [si]
    cmp bx, WR_PARK
    jne .live
    mov [di], bx
    mov [di+2], bx
    mov [di+4], bx
    mov [di+6], bx
    jmp short .next
.live:
    add bx, ax
    mov [di], bx
    mov bx, [si+4]
    add bx, ax
    mov [di+4], bx
    mov bx, [si+2]
    add bx, dx
    mov [di+2], bx
    mov bx, [si+6]
    add bx, dx
    mov [di+6], bx
.next:
    add si, 8
    add di, 8
    loop .one
    mov si, [wd_lp]                 ; the drop-downs' boxes, which os88ui
    add si, WL_DROPS                ; reads in SCREEN coordinates
    mov di, wr_drops
    mov cx, WR_NDROP
.drop:
    mov bx, [si]
    add bx, ax
    mov [di+OS88UI_DR_RECT], bx
    mov bx, [si+4]
    add bx, ax
    mov [di+OS88UI_DR_RECT+4], bx
    mov bx, [si+2]
    add bx, dx
    mov [di+OS88UI_DR_RECT+2], bx
    mov bx, [si+6]
    add bx, dx
    mov [di+OS88UI_DR_RECT+6], bx
    add si, 8
    add di, OS88UI_DR_SIZE
    loop .drop
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wd_rv - SI = a role -> SI = its byte in this theme (0..15, or 80h + a
; pattern). Keeps everything else
wd_rv:
    add si, [wd_th]
    mov si, [si]
    and si, 0xFF
    ret

; wd_abs - AX/BX/CX/DX content-relative -> screen. Keeps everything else
wd_abs:
    add ax, [wd_ox]
    add cx, [wd_ox]
    add bx, [wd_oy]
    add dx, [wd_oy]
    ret

; wd_cut - AX/BX/CX/DX a SCREEN rect: [wd_cutf] = 1 when the clip cuts it
wd_cut:
    call OSAPI_WM_CLIP_TEST         ; keeps all four
    jnc .out
    mov byte [wd_cutf], 1
.out:
    ret

; -----------------------------------------------------------------------------
; wd_rect - fill AX,BX - CX,DX (inclusive, content) with role SI
; wd_frame - the same rect's one-pixel outline
; wd_hl - a row: AX..CX at y BX.   wd_vl - a column: BX..DX at x AX
; -----------------------------------------------------------------------------
wd_hl:
    push dx
    mov dx, bx
    call wd_rect
    pop dx
    ret

wd_vl:
    push cx
    mov cx, ax
    call wd_rect
    pop cx
    ret

wd_rect:
    cmp ax, cx                      ; nothing to fill: nothing to call
    jg .none
    cmp bx, dx
    jg .none
    push ax
    push bx
    push cx
    push dx
    push si
    call wd_abs
    call wd_cut
    call wd_rv
    test si, 0x80
    jnz .pat
    push ax
    mov ax, si
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    jmp short .out
.pat:
    and si, 0x7F                    ; the pattern's eight bytes
    shl si, 1
    shl si, 1
    shl si, 1
    add si, wd_pats
    call OSAPI_GFX_FILL_PAT
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.none:
    ret

wd_frame:
    push ax
    push bx
    push cx
    push dx
    push si
    call wd_abs
    call wd_cut
    call wd_rv
    push ax
    mov ax, si                      ; a frame's role is never a pattern
    and al, 0x0F
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_text - a run of the SYSTEM font, ground and glyph in one pass (SPEC.md 6.1)
; in:  CX = x, DX = y (content), SI = the NUL string, AL = the ink role,
;      AH = the ground role
; -----------------------------------------------------------------------------
wd_text:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, si                      ; its width: 8 a character
    xor bx, bx
.len:
    cmp byte [di+bx], 0
    je .lenok
    inc bx
    jmp short .len
.lenok:
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add cx, [wd_ox]
    add dx, [wd_oy]
    push ax                         ; the run's rect, for the cut flag
    push cx
    push dx
    mov ax, cx
    add cx, bx
    dec cx
    mov bx, dx
    add dx, 7
    call wd_cut
    pop dx
    pop cx
    pop ax
    push si
    mov si, ax                      ; the ink and the ground, through the theme
    and si, 0xFF
    call wd_rv
    mov bx, si                      ; BL = the ink
    mov al, ah
    xor ah, ah
    mov si, ax
    call wd_rv
    mov ax, si
    mov ah, al                      ; AH = the ground
    mov al, bl                      ; AL = the ink
    pop si
    push ax
    call OSAPI_SET_COLOR            ; the pen out of any DISABLED state a
    pop ax                          ; standard draw left it in (SPEC.md 47 r1)
    and ax, 0x0F0F
    call OSAPI_FONT_RUN             ; CX = x, DX = y, SI = the run
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE BAND
; =============================================================================

; -----------------------------------------------------------------------------
; wd_bnew - a clear band, CX pixels by DX rows
; Its stride is one byte MORE than the width needs: wd_bor's shifted OR spills
; into the byte after a row's last, and that byte is never blitted.
; -----------------------------------------------------------------------------
wd_bnew:
    push ax
    push cx
    push dx
    push di
    push es
    mov [wd_bw], cx
    mov [wd_bh], dx
    add cx, 7 + 8
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov [wd_bst], cx
    mov ax, cx
    mul dx                          ; AX = the bytes (DX = 0: a band is small)
    cmp ax, WD_BANDSZ
    jbe .fit
    mov ax, WD_BANDSZ               ; a caller's mistake: clear what there is
.fit:
    mov cx, ax
    push ds
    pop es
    mov di, wd_band
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    mov byte [wd_bdim], 0
    ret

; -----------------------------------------------------------------------------
; wd_bor - OR 1bpp rows into the band at any x
; in:  SI = the source's first row, CX = its stride in bytes, DX = rows,
;      AX = x, BX = y (band coordinates). The rows must fit the band
; -----------------------------------------------------------------------------
wd_bor:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, cx                      ; BP = the source stride
    push dx
    mov di, ax
    mov ax, bx
    mul word [wd_bst]               ; AX = the band row's offset (DX spent)
    mov cx, di
    shr di, 1
    shr di, 1
    shr di, 1
    add di, ax
    add di, wd_band                 ; DI = the first byte the row touches
    and cl, 7                       ; CL = the shift into it
    pop dx
.row:
    push si
    push di
    mov bx, bp
.byte:
    mov ah, [si]
    xor al, al
    shr ax, cl
    or [di], ah
    or [di+1], al
    inc si
    inc di
    dec bx
    jnz .byte
    pop di
    pop si
    add si, bp
    add di, [wd_bst]
    dec dx
    jnz .row
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_bpic - a picture record (dw width; db height, stride; rows) into the band
; in:  SI = the record, AX = x, BX = y.  out: AX = x + its width
; -----------------------------------------------------------------------------
wd_bpic:
    or si, si
    jz .none                        ; no picture: nothing, and no advance
    push cx
    push dx
    push si
    mov cl, [si+3]
    xor ch, ch
    mov dl, [si+2]
    xor dh, dh
    push word [si]
    add si, 4
    call wd_bor
    pop cx
    add ax, cx
    pop si
    pop dx
    pop cx
.none:
    ret

; wd_picw - SI = a picture record -> CX = its width, DX = its height
wd_picw:
    mov cx, [si]
    mov dl, [si+2]
    xor dh, dh
    ret

; -----------------------------------------------------------------------------
; wd_bcaps - the 5x7 small caps (tools/os88wrart.py) into the band
; in:  SI = the NUL string, AX = x, BX = y; [wd_cgap] = the extra tracking
;      (0 = one pixel between letters).  out: AX = x past the last letter's
;      advance. Lower case is set as upper case; a character the face has no
;      letter for is a space
; -----------------------------------------------------------------------------
wd_bcaps:
    push cx
    push dx
    push si
    push di
.next:
    mov dl, [si]
    inc si
    or dl, dl
    jz .out
    cmp dl, 'a'
    jb .up
    cmp dl, 'z'
    ja .up
    sub dl, 'a' - 'A'
.up:
    cmp dl, WRART_CAPS_FIRST
    jb .adv
    cmp dl, WRART_CAPS_LAST
    ja .adv
    mov di, dx
    and di, 0xFF
    mov dl, [wrart_caps_ix + di - WRART_CAPS_FIRST]
    or dl, dl
    jz .adv
    dec dl                          ; DI = the glyph: 7 bytes an entry
    xor dh, dh
    mov di, dx
    shl di, 1
    shl di, 1
    shl di, 1
    sub di, dx
    add di, wrart_caps
    push si
    mov si, di
    mov cx, 1
    mov dx, 7
    call wd_bor
    pop si
.adv:
    add ax, 6
    mov dl, [wd_cgap]
    xor dh, dh
    add ax, dx
    jmp short .next
.out:
    pop di
    pop si
    pop dx
    pop cx
    ret

; wd_capsw - SI = a NUL string -> CX = its width in the small caps at the
; current tracking. Keeps everything else
wd_capsw:
    push ax
    push si
    xor cx, cx
.n:
    lodsb
    or al, al
    jz .done
    add cx, 6
    mov al, [wd_cgap]
    xor ah, ah
    add cx, ax
    jmp short .n
.done:
    jcxz .out
    dec cx                          ; the last letter's advance is not ink
    mov al, [wd_cgap]
    xor ah, ah
    sub cx, ax
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_bdigs - LCD digits into the band, from an ASCII string of '0'-'9', ' ',
;            '-', ':' and '.'
; in:  SI = the string, DI = a digit set (WDD_* below), AX = x, BX = y
; out: AX = x past the last
; -----------------------------------------------------------------------------
wd_bdigs:
    push dx
    push si
.next:
    mov dl, [si]
    inc si
    or dl, dl
    jz .out
    push si
    mov si, [di+WDD_COL]
    cmp dl, ':'
    je .put
    mov si, [di+WDD_DOT]
    cmp dl, '.'
    je .put
    mov si, [di+WDD_TAB]
    push bx
    mov bx, 10 * 2                  ; ' ' is the set's tenth picture...
    cmp dl, ' '
    je .pick
    mov bx, 11 * 2                  ; ...and '-' its eleventh
    cmp dl, '-'
    je .pick
    mov bl, dl
    sub bl, '0'
    xor bh, bh
    shl bx, 1
.pick:
    mov si, [si+bx]
    pop bx
.put:
    call wd_bpic
    pop si
    add ax, [di+WDD_GAP]
    jmp short .next
.out:
    sub ax, [di+WDD_GAP]
    pop si
    pop dx
    ret

; -----------------------------------------------------------------------------
; wd_bout - put the band down, wd_bw x wd_bh, at AX, BX (content)
; in:  DL = the ink role, DH = the paper role; [wd_bdim] = 1 greys the ink
; -----------------------------------------------------------------------------
wd_bout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    pop es
    mov cx, [wd_bst]                ; the band's bytes
    push ax
    mov ax, cx
    push dx
    mul word [wd_bh]
    pop dx
    mov cx, ax
    pop ax
    mov si, dx                      ; the two colours, through the theme
    and si, 0xFF
    call wd_rv
    mov di, si                      ; DI = the ink
    mov dl, dh
    xor dh, dh
    mov si, dx
    call wd_rv                      ; SI = the paper
    mov dx, di
    push ax
    mov ax, si
    mov dh, al
    pop ax
    and dx, 0x0F0F                  ; DL = the ink, DH = the paper
    cmp byte [wd_bdim], 0
    je .nodim
    push cx                         ; greyed: half the ink, a checkerboard
    push si
    push bx
    mov si, wd_band
    xor bx, bx
.drow:
    push cx
    mov cx, [wd_bst]
    mov bl, 0xAA
    test bh, 1
    jz .dodd
    mov bl, 0x55
.dodd:
    and [si], bl
    inc si
    loop .dodd
    inc bh
    pop cx
    sub cx, [wd_bst]
    ja .drow
    pop bx
    pop si
    pop cx
    mov byte [wd_bdim], 0
.nodim:
    cmp byte [wr_bpp1], 0
    je .pen
    or dl, dl                       ; 1bpp: lit is white, so black ink is the
    jnz .pen                        ; band turned over
    mov si, wd_band
.inv:
    not byte [si]
    inc si
    loop .inv
.pen:
    push ax
    mov ax, dx                      ; AL = ink, AH = paper
    call OSAPI_GFX_BLIT1_PEN
    pop ax
    add ax, [wd_ox]
    add bx, [wd_oy]
    push ax
    push bx
    mov cx, ax
    add cx, [wd_bw]
    dec cx
    mov dx, bx
    add dx, [wd_bh]
    dec dx
    call wd_cut
    pop bx
    pop ax
    mov cx, ax                      ; off the byte grid: the band starts at
    and cx, 7                       ; the byte column holding x, its first
    jz .grid                        ; x & 7 pixels undrawn (SPEC.md 5.4.2.8)
    call wd_bshift
.grid:
    mov si, wd_band
    mov bp, [wd_bst]
    mov cx, [wd_bw]
    mov dx, [wd_bh]
    call OSAPI_GFX_BLIT1
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wd_bshift - every band row moved CL (1..7) pixels right, into the slack
; byte wd_bnew left on each. keeps everything
wd_bshift:
    push ax
    push bx
    push dx
    push si
    mov dx, [wd_bh]
    mov si, wd_band
    add si, [wd_bst]
    dec si                          ; SI = the first row's last byte
.row:
    push si
    mov bx, [wd_bst]
    dec bx
    jz .first
.byte:
    mov ah, [si-1]
    mov al, [si]
    shr ax, cl
    mov [si], al
    dec si
    dec bx
    jnz .byte
.first:
    shr byte [si], cl
    pop si
    add si, [wd_bst]
    dec dx
    jnz .row
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; The helpers every field is drawn with: a band the size of the FIELD, the
; thing composed in it, and one blit - so a shorter value erases a longer one
; in the same call.
;
; wd_capsat - SI = a string in the small caps at AX, BX; DX = roles; the band
;             is the text's own width (a static label)
; wd_fcaps  - the same into a field CX wide, the text at the field's left,
;             or centred in it when [wd_ctr] = 1
; wd_picat  - SI = a picture at AX, BX; DX = roles
; -----------------------------------------------------------------------------
wd_capsat:
    push cx
    call wd_capsw
    call wd_fcaps
    pop cx
    ret

wd_fcaps:
    push ax
    push bx
    push cx
    push dx
    push ax
    push dx
    mov dx, 7
    call wd_bnew
    xor ax, ax
    cmp byte [wd_ctr], 0
    je .left
    push cx
    mov ax, cx
    call wd_capsw
    sub ax, cx
    sar ax, 1
    jns .c
    xor ax, ax
.c:
    pop cx
.left:
    push bx
    xor bx, bx
    call wd_bcaps
    pop bx
    pop dx
    pop ax
    call wd_bout
    pop dx
    pop cx
    pop bx
    pop ax
    ret

wd_picat:
    push cx
    push dx
    push ax
    push dx
    call wd_picw
    call wd_bnew
    xor ax, ax
    push bx
    xor bx, bx
    call wd_bpic
    pop bx
    pop dx
    pop ax
    call wd_bout
    pop dx
    pop cx
    ret

; the black-and-white face's surfaces (OSAPI_GFX_FILL_PAT: a set bit is white)
wd_pats:
    db 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55   ; P_DITHER, 50%
    db 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA   ; P_VSTR, ribbed
P_DITHER    equ 0x80
P_VSTR      equ 0x81
