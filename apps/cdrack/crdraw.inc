; =============================================================================
; os8088 - apps/cdrack/crdraw.inc - CDRack's drawing primitives (SPEC.md 119.6.3)
;
; Everything CDRack puts on the glass goes through these, so a THEME is one
; table: a primitive is handed a ROLE (R_*), never a colour, and looks it up
; in [cr_th] - the colour theme's table or the black-and-white one. A role's
; byte is a colour 0..15, or 80h + a pattern for the black-and-white face's
; dithered surfaces (OSAPI_GFX_FILL_PAT, which writes only 0 and 15).
;
; COORDINATES ARE CONTENT-RELATIVE: every primitive adds [cr_ox]/[cr_oy], the
; content origin cr_origin banked at the top of this pass.
;
; PICTURES AND THE SMALL CAPS ARE COMPOSED INTO ONE BAND and put down in ONE
; OSAPI_GFX_BLIT1 under a pen (SPEC.md 5.4.2): an LCD field, a caption, a
; whole button's face is one call whatever is in it, and the band's clear
; bits are its paper - so a field is erased and lettered in the same pass and
; no pixel is written twice (SPEC.md 6.1's rule, said for pictures).
;
; THE 1BPP POLARITY IS OURS: a band there is lit-is-white and the pen is not
; read (OSAPI_GFX_BLIT1_PEN), so black ink on white paper is the band
; INVERTED before the blit (cr_bout). A greyed caption is the band ANDed with
; a checkerboard first (SPEC.md 47 rule 1, the 1bpp way) - on the colour face
; it is the R_BTNDIS ink instead.
;
; THE CUT FLAG: every primitive asks OSAPI_WM_CLIP_TEST about what it drew
; and sets [cr_cutf] when the clip cut it, which is how a cached field that
; was drawn half-hidden is forgotten and drawn again (crface.inc, cr_ekdone).
;
; Every routine here preserves every register unless it says otherwise.
; =============================================================================

; -----------------------------------------------------------------------------
; cr_origin - bank the content origin, and the button rects from it
; in:  BX = the window
;
; A window that MOVES does not call its paint (SPEC.md 11.96.12), so this runs
; at the top of every pass that draws or hit-tests - the paint, the tick and
; every click - never once.
; -----------------------------------------------------------------------------
cr_origin:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [cr_ox], ax
    mov [cr_oy], dx
    mov si, [cr_lp]                 ; the layout's relative rects...
    mov si, [si+L_BTNS]
    mov di, cr_rects                ; ...made absolute (a parked one stays
    mov cx, CR_NB                   ; parked: it is off every screen)
.one:
    mov bx, [si]
    cmp bx, CR_PARK
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
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cr_rv - SI = a role -> SI = its byte in this theme (0..15, or 80h + a
; pattern). Keeps everything else
cr_rv:
    add si, [cr_th]
    mov si, [si]
    and si, 0xFF
    ret

; cr_abs - AX/BX/CX/DX content-relative -> screen. Keeps everything else
cr_abs:
    add ax, [cr_ox]
    add cx, [cr_ox]
    add bx, [cr_oy]
    add dx, [cr_oy]
    ret

; cr_cut - AX/BX/CX/DX a SCREEN rect: [cr_cutf] = 1 when the clip cuts it
cr_cut:
    call OSAPI_WM_CLIP_TEST         ; keeps all four
    jnc .out
    mov byte [cr_cutf], 1
.out:
    ret

; -----------------------------------------------------------------------------
; cr_rect - fill AX,BX - CX,DX (inclusive, content) with role SI
; cr_frame - the same rect's one-pixel outline
; cr_hl - a row: AX..CX at y BX.   cr_vl - a column: BX..DX at x AX
; -----------------------------------------------------------------------------
cr_hl:
    push dx
    mov dx, bx
    call cr_rect
    pop dx
    ret

cr_vl:
    push cx
    mov cx, ax
    call cr_rect
    pop cx
    ret

cr_rect:
    cmp ax, cx                      ; nothing to fill: nothing to call
    jg .none
    cmp bx, dx
    jg .none
    push ax
    push bx
    push cx
    push dx
    push si
    call cr_abs
    call cr_cut
    call cr_rv
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
    add si, cr_pats
    call OSAPI_GFX_FILL_PAT
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.none:
    ret

cr_frame:
    push ax
    push bx
    push cx
    push dx
    push si
    call cr_abs
    call cr_cut
    call cr_rv
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
; cr_text - a run of the SYSTEM font, ground and glyph in one pass (SPEC.md 6.1)
; in:  CX = x, DX = y (content), SI = the NUL string, AL = the ink role,
;      AH = the ground role
; -----------------------------------------------------------------------------
cr_text:
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
    add cx, [cr_ox]
    add dx, [cr_oy]
    push ax                         ; the run's rect, for the cut flag
    push cx
    push dx
    mov ax, cx
    add cx, bx
    dec cx
    mov bx, dx
    add dx, 7
    call cr_cut
    pop dx
    pop cx
    pop ax
    push si
    mov si, ax                      ; the ink and the ground, through the theme
    and si, 0xFF
    call cr_rv
    mov bx, si                      ; BL = the ink
    mov al, ah
    xor ah, ah
    mov si, ax
    call cr_rv
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
; cr_bnew - a clear band, CX pixels by DX rows
; Its stride is one byte MORE than the width needs: cr_bor's shifted OR spills
; into the byte after a row's last, and that byte is never blitted.
; -----------------------------------------------------------------------------
cr_bnew:
    push ax
    push cx
    push dx
    push di
    push es
    mov [cr_bw], cx
    mov [cr_bh], dx
    add cx, 7 + 8
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov [cr_bst], cx
    mov ax, cx
    mul dx                          ; AX = the bytes (DX = 0: a band is small)
    cmp ax, CR_BANDSZ
    jbe .fit
    mov ax, CR_BANDSZ               ; a caller's mistake: clear what there is
.fit:
    mov cx, ax
    push ds
    pop es
    mov di, cr_band
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    mov byte [cr_bdim], 0
    ret

; -----------------------------------------------------------------------------
; cr_bor - OR 1bpp rows into the band at any x
; in:  SI = the source's first row, CX = its stride in bytes, DX = rows,
;      AX = x, BX = y (band coordinates). The rows must fit the band
; -----------------------------------------------------------------------------
cr_bor:
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
    mul word [cr_bst]               ; AX = the band row's offset (DX spent)
    mov cx, di
    shr di, 1
    shr di, 1
    shr di, 1
    add di, ax
    add di, cr_band                 ; DI = the first byte the row touches
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
    add di, [cr_bst]
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
; cr_bpic - a picture record (dw width; db height, stride; rows) into the band
; in:  SI = the record, AX = x, BX = y.  out: AX = x + its width
; -----------------------------------------------------------------------------
cr_bpic:
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
    call cr_bor
    pop cx
    add ax, cx
    pop si
    pop dx
    pop cx
.none:
    ret

; cr_picw - SI = a picture record -> CX = its width, DX = its height
cr_picw:
    mov cx, [si]
    mov dl, [si+2]
    xor dh, dh
    ret

; -----------------------------------------------------------------------------
; cr_bcaps - the 5x7 small caps (tools/os88cdart.py) into the band
; in:  SI = the NUL string, AX = x, BX = y; [cr_cgap] = the extra tracking
;      (0 = one pixel between letters).  out: AX = x past the last letter's
;      advance. Lower case is set as upper case; a character the face has no
;      letter for is a space
; -----------------------------------------------------------------------------
cr_bcaps:
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
    cmp dl, CDART_CAPS_FIRST
    jb .adv
    cmp dl, CDART_CAPS_LAST
    ja .adv
    mov di, dx
    and di, 0xFF
    mov dl, [cdart_caps_ix + di - CDART_CAPS_FIRST]
    or dl, dl
    jz .adv
    dec dl                          ; DI = the glyph: 7 bytes an entry
    xor dh, dh
    mov di, dx
    shl di, 1
    shl di, 1
    shl di, 1
    sub di, dx
    add di, cdart_caps
    push si
    mov si, di
    mov cx, 1
    mov dx, 7
    call cr_bor
    pop si
.adv:
    add ax, 6
    mov dl, [cr_cgap]
    xor dh, dh
    add ax, dx
    jmp short .next
.out:
    pop di
    pop si
    pop dx
    pop cx
    ret

; cr_capsw - SI = a NUL string -> CX = its width in the small caps at the
; current tracking. Keeps everything else
cr_capsw:
    push ax
    push si
    xor cx, cx
.n:
    lodsb
    or al, al
    jz .done
    add cx, 6
    mov al, [cr_cgap]
    xor ah, ah
    add cx, ax
    jmp short .n
.done:
    jcxz .out
    dec cx                          ; the last letter's advance is not ink
    mov al, [cr_cgap]
    xor ah, ah
    sub cx, ax
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; cr_bdigs - LCD digits into the band, from an ASCII string of '0'-'9', ' ',
;            '-' and ':'
; in:  SI = the string, DI = a digit set (CRD_* below), AX = x, BX = y
; out: AX = x past the last
; -----------------------------------------------------------------------------
cr_bdigs:
    push dx
    push si
.next:
    mov dl, [si]
    inc si
    or dl, dl
    jz .out
    push si
    mov si, [di+CRD_COL]
    cmp dl, ':'
    je .put
    mov si, [di+CRD_TAB]
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
    call cr_bpic
    pop si
    add ax, [di+CRD_GAP]
    jmp short .next
.out:
    sub ax, [di+CRD_GAP]
    pop si
    pop dx
    ret

; -----------------------------------------------------------------------------
; cr_bout - put the band down, cr_bw x cr_bh, at AX, BX (content)
; in:  DL = the ink role, DH = the paper role; [cr_bdim] = 1 greys the ink
; -----------------------------------------------------------------------------
cr_bout:
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
    mov cx, [cr_bst]                ; the band's bytes
    push ax
    mov ax, cx
    push dx
    mul word [cr_bh]
    pop dx
    mov cx, ax
    pop ax
    mov si, dx                      ; the two colours, through the theme
    and si, 0xFF
    call cr_rv
    mov di, si                      ; DI = the ink
    mov dl, dh
    xor dh, dh
    mov si, dx
    call cr_rv                      ; SI = the paper
    mov dx, di
    push ax
    mov ax, si
    mov dh, al
    pop ax
    and dx, 0x0F0F                  ; DL = the ink, DH = the paper
    cmp byte [cr_bdim], 0
    je .nodim
    push cx                         ; greyed: half the ink, a checkerboard
    push si
    push bx
    mov si, cr_band
    xor bx, bx
.drow:
    push cx
    mov cx, [cr_bst]
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
    sub cx, [cr_bst]
    ja .drow
    pop bx
    pop si
    pop cx
    mov byte [cr_bdim], 0
.nodim:
    cmp byte [cr_bpp1], 0
    je .pen
    or dl, dl                       ; 1bpp: lit is white, so black ink is the
    jnz .pen                        ; band turned over
    mov si, cr_band
.inv:
    not byte [si]
    inc si
    loop .inv
.pen:
    push ax
    mov ax, dx                      ; AL = ink, AH = paper
    call OSAPI_GFX_BLIT1_PEN
    pop ax
    add ax, [cr_ox]
    add bx, [cr_oy]
    push ax
    push bx
    mov cx, ax
    add cx, [cr_bw]
    dec cx
    mov dx, bx
    add dx, [cr_bh]
    dec dx
    call cr_cut
    pop bx
    pop ax
    mov cx, ax                      ; off the byte grid: the band starts at
    and cx, 7                       ; the byte column holding x, its first
    jz .grid                        ; x & 7 pixels undrawn (SPEC.md 5.4.2.8)
    call cr_bshift
.grid:
    mov si, cr_band
    mov bp, [cr_bst]
    mov cx, [cr_bw]
    mov dx, [cr_bh]
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

; cr_bshift - every band row moved CL (1..7) pixels right, into the slack
; byte cr_bnew left on each. keeps everything
cr_bshift:
    push ax
    push bx
    push dx
    push si
    mov dx, [cr_bh]
    mov si, cr_band
    add si, [cr_bst]
    dec si                          ; SI = the first row's last byte
.row:
    push si
    mov bx, [cr_bst]
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
    add si, [cr_bst]
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
; cr_capsat - SI = a string in the small caps at AX, BX; DX = roles; the band
;             is the text's own width (a static label)
; cr_fcaps  - the same into a field CX wide, the text at the field's left,
;             or centred in it when [cr_ctr] = 1
; cr_picat  - SI = a picture at AX, BX; DX = roles
; -----------------------------------------------------------------------------
cr_capsat:
    push cx
    call cr_capsw
    call cr_fcaps
    pop cx
    ret

cr_fcaps:
    push ax
    push bx
    push cx
    push dx
    push ax
    push dx
    mov dx, 7
    call cr_bnew
    xor ax, ax
    cmp byte [cr_ctr], 0
    je .left
    push cx
    mov ax, cx
    call cr_capsw
    sub ax, cx
    sar ax, 1
    jns .c
    xor ax, ax
.c:
    pop cx
.left:
    push bx
    xor bx, bx
    call cr_bcaps
    pop bx
    pop dx
    pop ax
    call cr_bout
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cr_picat:
    push cx
    push dx
    push ax
    push dx
    call cr_picw
    call cr_bnew
    xor ax, ax
    push bx
    xor bx, bx
    call cr_bpic
    pop bx
    pop dx
    pop ax
    call cr_bout
    pop dx
    pop cx
    ret

; the black-and-white face's surfaces (OSAPI_GFX_FILL_PAT: a set bit is white)
cr_pats:
    db 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55   ; P_DITHER, 50%
    db 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA   ; P_VSTR, ribbed
P_DITHER    equ 0x80
P_VSTR      equ 0x81
