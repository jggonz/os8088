; =============================================================================
; os8088 - apps/xtmp3/x3draw.inc - XTMP3's drawing layer (SPEC.md 121.8)
;
; CDRack's apps/cdrack/crdraw.inc (SPEC.md 119.6.3) with its prefix renamed
; x3d_ - the retro-face skill's Step 3 - and four things it did not need:
;
;   FONTS BY DESCRIPTOR  x3d_bstr sets any of the generated faces (the panel's
;                        small caps, bold or not, the LCD's two title faces,
;                        the dial's scale digits) from one record (X3FD_*), so
;                        a layout names a face rather than a routine.
;   A COLOUR DITHER      a role byte of 40h + n is a TWO-COLOUR pattern
;                        (x3d_dtab): a band of the pattern's bits under a pen,
;                        tiled down the rect, phase content-relative. The black
;                        and white face's 80h + n patterns are still
;                        OSAPI_GFX_FILL_PAT's, which writes only 0 and 15.
;   A 4BPP CANVAS        the knob and the VU dials are composed in sixteen
;                        colours in x3_work and put down with ONE
;                        OSAPI_GFX_BLIT4 (x3d_c4out), which may be a SUB-RECT of
;                        the canvas: a needle that moved redraws the box its
;                        old and new positions share, not the dial.
;   LINES                x3d_line walks Bresenham through x3d_plot into the
;                        canvas or the band - the needles, the indicator and
;                        the knob's tick ring are tables of end points
;                        (tools/os88x3art.py), never pictures.
;
; Everything else is CDRack's and keeps its contract:
;
; A THEME IS ONE TABLE: a primitive is handed a ROLE (R_*), never a colour,
; and looks it up in [x3d_th]. A role's byte is a colour 0..15, 80h + a
; pattern for FILL_PAT, or 40h + a colour dither.
;
; COORDINATES ARE CONTENT-RELATIVE: every primitive adds [x3d_ox]/[x3d_oy],
; the content origin x3d_origin banked at the top of this pass.
;
; PICTURES AND TEXT ARE COMPOSED INTO ONE BAND and put down in ONE
; OSAPI_GFX_BLIT1 under a pen (SPEC.md 5.4.2); the band's clear bits are its
; paper, so a field is erased and lettered in the same pass.
;
; THE 1BPP POLARITY IS OURS: lit is white and the pen is not read there, so
; black ink on white paper is the band INVERTED before the blit (x3d_bput).
; A greyed band is its ink ANDed with a checkerboard (SPEC.md 47 rule 1).
;
; THE CUT FLAG: every primitive asks OSAPI_WM_CLIP_TEST about what it drew
; and sets [x3d_cutf] when the clip cut it, so an element drawn half-hidden is
; forgotten and drawn again (x3face.inc, x3f_ekdone).
;
; Every routine preserves every register unless it says otherwise.
; =============================================================================

; --- a font descriptor (x3_f* in xtmp3.asm) --------------------------------------
X3FD_IX     equ 0                   ; word: the ASCII index (X3ART_FONT_FIRST..)
X3FD_GL     equ 2                   ; word: the glyphs, X3FD_H bytes each
X3FD_H      equ 4                   ; byte: rows
X3FD_ADV    equ 5                   ; byte: the advance, before [x3d_cgap]
X3FD_W      equ 6                   ; byte: the ink's width (bold included)
X3FD_BOLD   equ 7                   ; byte: 1 = each glyph ORed again a pixel
                                    ;       right - two-pixel stems
X3FD_SIZE   equ 8

; --- a digit set (xtmp3.asm's X3DSET) -----------------------------------------------
X3DS_TAB    equ 0                   ; twelve pictures: '0'-'9', ' ', '-'
X3DS_COL    equ 2                   ; the colon
X3DS_GAP    equ 4                   ; pixels between characters
X3DS_W      equ 6                   ; a digit's width and height
X3DS_H      equ 8
X3DS_CW     equ 10                  ; the colon's width
X3DS_SL     equ 12                  ; a thin set's '/' and '~' (0 = none)
X3DS_TL     equ 14
X3DS_SW     equ 16                  ; ...and the '/''s width

; --- the plotter's targets (x3d_plot) ----------------------------------------------
X3P_C4      equ 0                   ; the 4bpp canvas, colour [x3d_pcol]
X3P_SET     equ 1                   ; the band: ink
X3P_CLR     equ 2                   ; the band: paper

; -----------------------------------------------------------------------------
; x3d_origin - bank the content origin, and the button rects from it
; in:  BX = the window
;
; A window that MOVES does not call its paint (SPEC.md 11.96.12), so this runs
; at the top of every pass that draws or hit-tests.
; -----------------------------------------------------------------------------
x3d_origin:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [x3d_ox], ax
    mov [x3d_oy], dx
    mov si, [x3_lp]                 ; the layout's relative rects...
    mov si, [si+L_BTNS]
    mov di, x3_rects                ; ...made absolute (a parked one stays
    mov cx, X3_NB                   ; parked: it is off every screen)
.one:
    mov bx, [si]
    cmp bx, X3_PARK
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

; x3d_rv - SI = a role -> SI = its byte in this theme. Keeps everything else
x3d_rv:
    add si, [x3d_th]
    mov si, [si]
    and si, 0xFF
    ret

; x3d_abs - AX/BX/CX/DX content-relative -> screen. Keeps everything else
x3d_abs:
    add ax, [x3d_ox]
    add cx, [x3d_ox]
    add bx, [x3d_oy]
    add dx, [x3d_oy]
    ret

; x3d_cut - AX/BX/CX/DX a SCREEN rect: [x3d_cutf] = 1 when the clip cuts it
x3d_cut:
    call OSAPI_WM_CLIP_TEST         ; keeps all four
    jnc .out
    mov byte [x3d_cutf], 1
.out:
    ret

; -----------------------------------------------------------------------------
; x3d_rect - fill AX,BX - CX,DX (inclusive, content) with role SI
; x3d_frame - the same rect's one-pixel outline (a plain colour role)
; x3d_hl - a row: AX..CX at y BX.   x3d_vl - a column: BX..DX at x AX
; -----------------------------------------------------------------------------
x3d_hl:
    push dx
    mov dx, bx
    call x3d_rect
    pop dx
    ret

x3d_vl:
    push cx
    mov cx, ax
    call x3d_rect
    pop cx
    ret

x3d_rect:
    cmp ax, cx                      ; nothing to fill: nothing to call
    jg .none
    cmp bx, dx
    jg .none
    push ax
    push bx
    push cx
    push dx
    push si
    call x3d_rv
    test si, 0x40
    jz .abs
    and si, 0x3F                    ; a colour dither: content coordinates
    call x3d_dith
    jmp short .out
.abs:
    call x3d_abs
    call x3d_cut
    test si, 0x80
    jnz .pat
    push ax
    mov ax, si
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    jmp short .out
.pat:
    and si, 0x3F                    ; the pattern's eight bytes
    shl si, 1
    shl si, 1
    shl si, 1
    add si, x3d_pats
    call OSAPI_GFX_FILL_PAT
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.none:
    ret

x3d_frame:
    push ax
    push bx
    push cx
    push dx
    push si
    call x3d_abs
    call x3d_cut
    call x3d_rv
    push ax
    mov ax, si                      ; a frame's role is a plain colour
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
; x3d_dith - a TWO-COLOUR pattern over AX,BX - CX,DX (content): dither SI of
;            x3d_dtab, composed as bands of the pattern and put down under its
;            pen, as many bands as the work buffer needs. The phase is the
;            CONTENT's, so a strip added later meets the strip beside it.
; -----------------------------------------------------------------------------
x3d_dith:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov di, si                      ; DI = the entry: ink, paper, pattern
    add di, si
    add di, si
    add di, x3d_dtab
    sub cx, ax
    inc cx                          ; CX = the width
    sub dx, bx
    inc dx                          ; DX = the rows still to do
    mov bp, ax                      ; BP = x
.chunk:
    push dx
    push cx
    mov ax, cx                      ; the rows one band holds
    add ax, 7 + 8
    shr ax, 1
    shr ax, 1
    shr ax, 1                       ; AX = its stride
    push ax
    mov ax, X3_WORKSZ
    xor dx, dx
    pop si
    div si                          ; AX = rows that fit
    pop cx
    pop dx
    cmp ax, dx
    jbe .rows
    mov ax, dx
.rows:
    push dx
    mov dx, ax
    call x3d_bnew                   ; CX x DX, clear
    ; each row: the pattern's byte for (y & 7), turned left by (x & 7), so
    ; band pixel k is the pattern's column (x + k) & 7
    push cx
    push bx
    mov si, [x3d_bst]
    mov cx, si                      ; CX = the stride (the bytes a row)
    push di
    mov al, [di+2]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, x3d_pats
    mov si, ax                      ; SI = the pattern's eight bytes
    mov di, x3_work
    push dx
.prow:
    push bx
    and bx, 7
    mov al, [si+bx]
    pop bx
    push cx
    mov cx, bp
    and cl, 7
    rol al, cl
    pop cx
    push cx
    push es
    push ds
    pop es
    cld
    rep stosb
    pop es
    pop cx
    inc bx
    dec dx
    jnz .prow
    pop dx
    pop di
    pop bx
    pop cx
    push dx
    mov dx, [di]                    ; DL = ink, DH = paper
    mov ax, bp
    call x3d_bput
    pop dx
    add bx, dx                      ; the next band below
    mov ax, dx
    pop dx
    sub dx, ax
    jnz .chunk
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3d_text - a run of the SYSTEM font, ground and glyph in one pass (SPEC.md 6.1)
; in:  CX = x, DX = y (content), SI = the NUL string, AL = the ink role,
;      AH = the ground role
; -----------------------------------------------------------------------------
x3d_text:
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
    add cx, [x3d_ox]
    add dx, [x3d_oy]
    push ax                         ; the run's rect, for the cut flag
    push cx
    push dx
    mov ax, cx
    add cx, bx
    dec cx
    mov bx, dx
    add dx, 7
    call x3d_cut
    pop dx
    pop cx
    pop ax
    push si
    mov si, ax                      ; the ink and the ground, through the theme
    and si, 0xFF
    call x3d_rv
    mov bx, si                      ; BL = the ink
    mov al, ah
    xor ah, ah
    mov si, ax
    call x3d_rv
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
; x3d_bnew - a clear band, CX pixels by DX rows
; Its stride is one byte MORE than the width needs: x3d_bor's shifted OR
; spills into the byte after a row's last, and that byte is never blitted.
; -----------------------------------------------------------------------------
x3d_bnew:
    push ax
    push cx
    push dx
    push di
    push es
    mov [x3d_bw], cx
    mov [x3d_bh], dx
    add cx, 7 + 8
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov [x3d_bst], cx
    mov ax, cx
    mul dx                          ; AX = the bytes (DX = 0: a band is small)
    cmp ax, X3_WORKSZ
    jbe .fit
    mov ax, X3_WORKSZ               ; a caller's mistake: clear what there is
.fit:
    mov cx, ax
    push ds
    pop es
    mov di, x3_work
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    mov byte [x3d_bdim], 0
    mov word [x3d_cvy0], 0
    ret

; -----------------------------------------------------------------------------
; x3d_bor - OR 1bpp rows into the band at any x
; in:  SI = the source's first row, CX = its stride in bytes, DX = rows,
;      AX = x, BX = y (band coordinates). The rows must fit the band
; -----------------------------------------------------------------------------
x3d_bor:
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
    mul word [x3d_bst]              ; AX = the band row's offset (DX spent)
    mov cx, di
    shr di, 1
    shr di, 1
    shr di, 1
    add di, ax
    add di, x3_work                 ; DI = the first byte the row touches
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
    add di, [x3d_bst]
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
; x3d_bpic - a picture record (dw width; db height, stride; rows) into the band
; in:  SI = the record, AX = x, BX = y.  out: AX = x + its width
; -----------------------------------------------------------------------------
x3d_bpic:
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
    call x3d_bor
    pop cx
    add ax, cx
    pop si
    pop dx
    pop cx
.none:
    ret

; x3d_picw - SI = a picture record -> CX = its width, DX = its height
x3d_picw:
    mov cx, [si]
    mov dl, [si+2]
    xor dh, dh
    ret

; -----------------------------------------------------------------------------
; x3d_bstr - a string in a generated face into the band
; in:  SI = the NUL string, DI = the face (X3FD_*), AX = x, BX = y;
;      [x3d_cgap] = extra tracking.  out: AX = x past the last letter's
;      advance. A letter the face lacks is set as its upper case if that
;      exists, else as a space
; -----------------------------------------------------------------------------
x3d_bstr:
    push cx
    push dx
    push si
.next:
    mov dl, [si]
    inc si
    or dl, dl
    jz .out
    push si
    push bx
.look:
    cmp dl, X3ART_FONT_FIRST
    jb .skip
    cmp dl, X3ART_FONT_LAST
    ja .skip
    mov bl, dl
    xor bh, bh
    mov si, [di+X3FD_IX]
    mov dh, [si+bx-X3ART_FONT_FIRST]
    or dh, dh
    jnz .have
    cmp dl, 'a'                     ; not in the face: its upper case?
    jb .skip
    cmp dl, 'z'
    ja .skip
    sub dl, 'a' - 'A'
    jmp short .look
.skip:
    pop bx
    jmp short .adv
.have:
    dec dh                          ; SI = the glyph: X3FD_H bytes an entry
    mov bl, dh
    xor bh, bh
    push ax
    mov al, [di+X3FD_H]
    xor ah, ah
    mul bx                          ; DX spent
    mov si, ax
    add si, [di+X3FD_GL]
    pop ax
    pop bx                          ; BX = y again
    mov cx, 1
    mov dl, [di+X3FD_H]
    xor dh, dh
    call x3d_bor
    cmp byte [di+X3FD_BOLD], 0
    je .adv
    inc ax                          ; bold: the same glyph a pixel right
    call x3d_bor
    dec ax
.adv:
    pop si
    mov dl, [di+X3FD_ADV]
    xor dh, dh
    add ax, dx
    mov dl, [x3d_cgap]
    add ax, dx
    jmp short .next
.out:
    pop si
    pop dx
    pop cx
    ret

; x3d_strw - SI = a NUL string, DI = a face -> CX = its inked width at the
; current tracking: (n - 1) advances and the last letter's ink. Keeps the rest
x3d_strw:
    push ax
    push si
    xor cx, cx
.n:
    lodsb
    or al, al
    jz .done
    mov al, [di+X3FD_ADV]
    xor ah, ah
    add cx, ax
    mov al, [x3d_cgap]
    add cx, ax
    jmp short .n
.done:
    jcxz .out
    mov al, [di+X3FD_ADV]           ; the last advance becomes its ink
    xor ah, ah
    sub cx, ax
    mov al, [x3d_cgap]
    sub cx, ax
    mov al, [di+X3FD_W]
    add cx, ax
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3d_bdigs - LCD digits into the band, from an ASCII string of '0'-'9', ' ',
;             '-', ':', '/' and '~' (the last two a thin set's alone)
; in:  SI = the string, DI = a digit set (X3DS_*), AX = x, BX = y
; out: AX = x past the last
; -----------------------------------------------------------------------------
x3d_bdigs:
    push dx
    push si
.next:
    mov dl, [si]
    inc si
    or dl, dl
    jz .out
    push si
    mov si, [di+X3DS_COL]
    cmp dl, ':'
    je .put
    mov si, [di+X3DS_SL]
    cmp dl, '/'
    je .put
    mov si, [di+X3DS_TL]
    cmp dl, '~'
    je .put
    mov si, [di+X3DS_TAB]
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
    call x3d_bpic
    pop si
    add ax, [di+X3DS_GAP]
    jmp short .next
.out:
    sub ax, [di+X3DS_GAP]
    pop si
    pop dx
    ret

; -----------------------------------------------------------------------------
; x3d_bout - put the band down, x3d_bw x x3d_bh, at AX, BX (content)
; in:  DL = the ink role, DH = the paper role; [x3d_bdim] = 1 greys the ink
; x3d_bput - the same with DL, DH COLOURS (0..15), not roles
; -----------------------------------------------------------------------------
x3d_bout:
    push dx
    push si
    push ax
    mov si, dx                      ; the two colours, through the theme
    and si, 0xFF
    call x3d_rv
    mov ax, si                      ; AL = the ink
    mov dl, dh
    xor dh, dh
    mov si, dx
    call x3d_rv
    mov dx, si
    mov dh, dl                      ; DH = the paper
    mov dl, al                      ; DL = the ink
    pop ax
    call x3d_bput
    pop si
    pop dx
    ret

x3d_bput:
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
    and dx, 0x0F0F
    mov cx, [x3d_bst]               ; the band's bytes
    push ax
    push dx
    mov ax, cx
    mul word [x3d_bh]
    mov cx, ax
    pop dx
    pop ax
    cmp byte [x3d_bdim], 0
    je .nodim
    push cx                         ; greyed: half the ink, a checkerboard
    push si
    push bx
    mov si, x3_work
    xor bx, bx
.drow:
    push cx
    mov cx, [x3d_bst]
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
    sub cx, [x3d_bst]
    ja .drow
    pop bx
    pop si
    pop cx
    mov byte [x3d_bdim], 0
.nodim:
    cmp byte [x3d_bpp1], 0
    je .pen
    or dl, dl                       ; 1bpp: lit is white, so black ink is the
    jnz .pen                        ; band turned over
    mov si, x3_work
.inv:
    not byte [si]
    inc si
    loop .inv
.pen:
    push ax
    mov ax, dx                      ; AL = ink, AH = paper
    call OSAPI_GFX_BLIT1_PEN
    pop ax
    add ax, [x3d_ox]
    add bx, [x3d_oy]
    push ax
    push bx
    mov cx, ax
    add cx, [x3d_bw]
    dec cx
    mov dx, bx
    add dx, [x3d_bh]
    dec dx
    call x3d_cut
    pop bx
    pop ax
    mov cx, ax                      ; off the byte grid: the band starts at
    and cx, 7                       ; the byte column holding x, its first
    jz .grid                        ; x & 7 pixels undrawn (SPEC.md 5.4.2.8)
    call x3d_bshift
.grid:
    mov si, x3_work
    mov bp, [x3d_bst]
    mov cx, [x3d_bw]
    mov dx, [x3d_bh]
    call OSAPI_GFX_BLIT1
    jnc .done
    mov di, ax                      ; refused: kern_small takes x on the byte
    and di, 7                       ; grid only. The shifted band from its
    jz .done                        ; byte column, the x & 7 pixels before x
    add cx, di                      ; laid in its paper - which is the ground
    sub ax, di                      ; it sits on, for every field but a few
    call OSAPI_GFX_BLIT1
.done:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3d_bshift - every band row moved CL (1..7) pixels right, into the slack
; byte x3d_bnew left on each. keeps everything
x3d_bshift:
    push ax
    push bx
    push dx
    push si
    mov dx, [x3d_bh]
    mov si, x3_work
    add si, [x3d_bst]
    dec si                          ; SI = the first row's last byte
.row:
    push si
    mov bx, [x3d_bst]
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
    add si, [x3d_bst]
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
; x3d_strat - SI = a string in face DI at AX, BX; DX = roles; the band is the
;             text's own width (a static label)
; x3d_fstr  - the same into a field CX wide, the text at the field's left,
;             or centred in it when [x3d_ctr] = 1
; x3d_picat - SI = a picture at AX, BX; DX = roles
; -----------------------------------------------------------------------------
x3d_strat:
    push cx
    call x3d_strw
    call x3d_fstr
    pop cx
    ret

x3d_fstr:
    push ax
    push bx
    push cx
    push dx
    push ax
    push dx
    mov dl, [di+X3FD_H]
    xor dh, dh
    call x3d_bnew
    xor ax, ax
    cmp byte [x3d_ctr], 0
    je .left
    push cx
    mov ax, cx
    call x3d_strw
    sub ax, cx
    sar ax, 1
    jns .c
    xor ax, ax
.c:
    pop cx
.left:
    push bx
    xor bx, bx
    call x3d_bstr
    pop bx
    pop dx
    pop ax
    call x3d_bout
    pop dx
    pop cx
    pop bx
    pop ax
    ret

x3d_picat:
    push cx
    push dx
    push ax
    push dx
    call x3d_picw
    call x3d_bnew
    xor ax, ax
    push bx
    xor bx, bx
    call x3d_bpic
    pop bx
    pop dx
    pop ax
    call x3d_bout
    pop dx
    pop cx
    ret

; =============================================================================
; THE CANVAS (4bpp) AND THE PLOTTER
; =============================================================================

; -----------------------------------------------------------------------------
; x3d_c4new - a canvas CX pixels wide and DX rows in x3_work, every pixel AL
;             (a colour 0..15); its first row is row [x3d_cvy0] of the
;             picture, which the caller sets AFTER this (0 here)
; -----------------------------------------------------------------------------
x3d_c4new:
    push ax
    push cx
    push dx
    push di
    push es
    mov [x3d_cvw], cx
    mov [x3d_cvh], dx
    mov word [x3d_cvy0], 0
    inc cx
    shr cx, 1
    mov [x3d_cvst], cx
    and al, 0x0F
    mov ah, al
    shl ah, 1
    shl ah, 1
    shl ah, 1
    shl ah, 1
    or al, ah
    push ax
    mov ax, cx
    mul dx                          ; AX = its bytes (DX spent)
    mov cx, ax
    pop ax
    cmp cx, X3_WORKSZ
    jbe .fit
    mov cx, X3_WORKSZ
.fit:
    push ds
    pop es
    mov di, x3_work
    cld
    rep stosb
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3d_plot - one pixel at AX, BX (picture coordinates) into the target
;            [x3d_pmode]: the canvas in [x3d_pcol], or the band's ink or paper.
;            Off the target it is nothing; with [x3d_pdim] = 1 a pixel whose
;            x + y is odd is skipped (the greyed indicator)
; -----------------------------------------------------------------------------
x3d_plot:
    push ax
    push bx
    push cx
    push si
    cmp byte [x3d_pdim], 0
    je .live
    mov cx, ax
    add cx, bx
    test cl, 1
    jnz .out
.live:
    sub bx, [x3d_cvy0]              ; the row in the buffer
    jl .out
    cmp byte [x3d_pmode], X3P_C4
    jne .band
    cmp bx, [x3d_cvh]
    jge .out
    or ax, ax
    jl .out
    cmp ax, [x3d_cvw]
    jge .out
    push ax
    push dx
    mov ax, bx
    mul word [x3d_cvst]
    mov si, ax
    pop dx
    pop ax
    mov cx, ax
    shr ax, 1
    add si, ax
    add si, x3_work
    mov al, [x3d_pcol]
    test cl, 1
    jnz .low
    mov cl, 4                       ; an even pixel is the HIGH nibble
    shl al, cl
    and byte [si], 0x0F
    or [si], al
    jmp short .out
.low:
    and byte [si], 0xF0
    or [si], al
    jmp short .out
.band:
    cmp bx, [x3d_bh]
    jge .out
    or ax, ax
    jl .out
    cmp ax, [x3d_bw]
    jge .out
    push ax
    push dx
    mov ax, bx
    mul word [x3d_bst]
    mov si, ax
    pop dx
    pop ax
    mov cl, al
    and cl, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add si, ax
    add si, x3_work
    mov al, 0x80
    shr al, cl
    cmp byte [x3d_pmode], X3P_SET
    jne .clr
    or [si], al
    jmp short .out
.clr:
    not al
    and [si], al
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3d_line - AX,BX to CX,DX, both ends included, through x3d_plot: the same
;            Bresenham walk as tools/os88x3art.py's line_px, so a table there
;            and a line here agree to the pixel
; -----------------------------------------------------------------------------
x3d_line:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [x3d_lx2], cx
    mov [x3d_ly2], dx
    mov si, 1                       ; SI = sx
    sub cx, ax
    jge .dxp
    neg cx
    neg si
.dxp:
    mov [x3d_ldx], cx               ; dx = |x2 - x1|
    mov di, 1                       ; DI = sy
    sub dx, bx
    jge .dyp
    neg dx
    neg di
.dyp:
    neg dx
    mov [x3d_ldy], dx               ; dy = -|y2 - y1|
    mov bp, cx
    add bp, dx                      ; BP = err
.step:
    call x3d_plot
    cmp ax, [x3d_lx2]
    jne .go
    cmp bx, [x3d_ly2]
    je .done
.go:
    mov cx, bp
    shl cx, 1                       ; e2
    cmp cx, [x3d_ldy]
    jl .noy
    add bp, [x3d_ldy]
    add ax, si
.noy:
    cmp cx, [x3d_ldx]
    jg .step
    add bp, [x3d_ldx]
    add bx, di
    jmp short .step
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3d_lines - SI = KSTEPS-style records of four bytes (x1, y1, x2, y2), CX =
; how many: each line drawn, offset AX, BX. keeps everything
x3d_lines:
    push ax
    push bx
    push cx
    push dx
    push si
.one:
    push cx
    push ax
    push bx
    call x3d_line4
    pop bx
    pop ax
    pop cx
    add si, 4
    loop .one
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; x3d_line4 - the line of the four bytes at SI, offset AX, BX. clobbers CX, DX
x3d_line4:
    push ax
    push bx
    mov cl, [si+2]
    xor ch, ch
    add cx, ax
    mov dl, [si+3]
    xor dh, dh
    add dx, bx
    push cx
    mov cl, [si]
    xor ch, ch
    add ax, cx
    mov cl, [si+1]
    add bx, cx
    pop cx
    call x3d_line
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3d_c4out - the canvas, or columns CX..DX of it (CX even), every row it
;             holds, put down in ONE OSAPI_GFX_BLIT4 with its column 0 at
;             AX and its row [x3d_cvy0] at BX + [x3d_cvy0] (content)
; -----------------------------------------------------------------------------
x3d_c4out:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    push ds
    pop es
    mov si, cx
    shr si, 1
    add si, x3_work                 ; SI = the first column's byte
    add ax, cx                      ; AX = its x
    sub dx, cx
    inc dx
    mov cx, dx                      ; CX = the width
    add ax, [x3d_ox]
    add bx, [x3d_cvy0]
    add bx, [x3d_oy]
    mov dx, [x3d_cvh]
    push ax
    push bx
    push cx
    push dx
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call x3d_cut
    pop dx
    pop cx
    pop bx
    pop ax
    mov bp, [x3d_cvst]
    call OSAPI_GFX_BLIT4
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; the black-and-white face's surfaces (OSAPI_GFX_FILL_PAT: a set bit is white)
; and the colour dithers' patterns (a set bit is the dither's ink)
x3d_pats:
    db 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55   ; P_DITHER, 50%
    db 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF   ; P_SOLID
P_DITHER    equ 0x80
D_DITHER    equ 0x40
; the colour dithers: ink, paper, pattern (x3d_dith). An entry is a ROLE's
; byte as 40h + its number
x3d_dtab:
    db CLGREEN, CGREEN, 0           ; 40h: the progress bar's lit glass
