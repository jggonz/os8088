; =============================================================================
; apps/stiknote/snpad.inc - THE PAD: the Sticky App's own window (SPEC.md
; 110.13), one card in os88stik.inc's widget treatment
;
; The card is a SHEET on a STACK: the sheet at face x 0..187, rows 0..H-5,
; and the sheet under it showing SNP_S px to the right and below - so the
; card's top-right and bottom-left SNP_S x SNP_S corners are the DESKTOP,
; which on a desk window is OSAPI_GFX_DESK's to lay (two calls a paint) and
; is never in a blit. The sheet's own bottom-right corner is turned: a fold
; of SNPG_K px, two bit records (the corner papered off, then the fold's
; three edges inked).
;
; Inside: a pin, the current note's title and `+` over a rule; the body's
; visual lines - boxes drawn, text wrapped to the sheet - as many as fit;
; and `<`, `n / N`, `>` and the open-in-window arrow along the foot.
;
; COLOUR: the slabs are blitted with the note's paper as the pen's INK
; (sn_setpen): composed 1 = paper, 0 = ink, so the sheet is the paper and the
; text, the rules and the edges black. The tag, which the include draws after
; us, gets the default pen back (CWHITE).
; =============================================================================

SNP_S       equ 4               ; the stack's offset, both ways
SNP_SW      equ STIK_FACE_W - SNP_S     ; 188: the sheet's width

; --- the layouts (os88stik.inc's STIK_LAYOUT: band, face y, tag, card, gap),
; and the pad's own geometry for each, in [stik_li]'s order. Rows are
; card-local; x is face-relative
snp_lays:
    STIK_LAYOUT 436, 260, 12, 156, 4      ; TALL, VGA: 192 x 172
    STIK_LAYOUT 304, 150, 11, 124, 3      ; MID, Hercules and EGA: 138
    STIK_LAYOUT 156, 60, 11, 92, 3        ; COMPACT, CGA: 106

SNPG_H      equ 0               ; db the card's rows
SNPG_BW     equ 1               ; db the sheet's border
SNPG_HY     equ 2               ; db the header's text row
SNPG_PINY   equ 3               ; db ...the pin's
SNPG_RY     equ 4               ; db the rule's first row
SNPG_RH     equ 5               ; db ...its rows
SNPG_B0     equ 6               ; db body line 0's text row
SNPG_P      equ 7               ; db ...the pitch
SNPG_NV     equ 8               ; db ...the lines that fit
SNPG_FY     equ 9               ; db the foot's text row
SNPG_OY     equ 10              ; db ...the arrow's row
SNPG_K      equ 11              ; db the turned corner's size
SNPG_BOXY   equ 12              ; db a box's row against its text's (signed)
SNPG_LOFF   equ 13              ; db a line's rows begin this far above its text
SNPG_PIN    equ 14              ; dw the pin's record
SNPG_BOX    equ 16              ; dw the open box's record
SNPG_BOXX   equ 18              ; dw ...the ticked one's
SNPG_OPEN   equ 20              ; dw the arrow's
SNPG_EARP   equ 22              ; dw the corner's paper record
SNPG_EARI   equ 24              ; dw ...and its ink
SNPG_SZ     equ 26

snp_geos:
    ;  H  BW  HY PINY RY RH  B0  P  NV  FY  OY  K  BOXY LOFF
    db 156, 2,  8,  6, 21, 2, 29, 12, 8, 134, 133, 12, -1, 2
    dw sn_pin11, sn_box10, sn_box10x, sn_open11, snp_ear12p, snp_ear12i
.mid:
    db 124, 1,  6,  6, 17, 1, 23, 10, 7, 104, 102, 10, -1, 1
    dw sn_pin8, sn_box9, sn_box9x, sn_open11, snp_ear10p, snp_ear10i
.comp:
    db  92, 1,  4,  4, 14, 1, 18,  9, 6,  76,  76,  8,  0, 1
    dw sn_pin8, sn_box8, sn_box8x, sn_open8, snp_ear8p, snp_ear8i
%if snp_geos.mid - snp_geos != SNPG_SZ || snp_geos.comp - snp_geos.mid != SNPG_SZ
  %error "snpad: a geometry entry is SNPG_SZ bytes"
%endif
; each layout's body ends above its foot, and its foot above the sheet's edge
%if 29 + 8 * 12 - 2 > 134 - 4 || 134 + 11 > 156 - SNP_S - 2
  %error "snpad: TALL's lines overrun its foot"
%endif
%if 23 + 7 * 10 - 1 > 104 - 4 || 104 + 11 > 124 - SNP_S - 1
  %error "snpad: MID's lines overrun its foot"
%endif
%if 18 + 6 * 9 - 1 > 76 - 2 || 76 + 8 > 92 - SNP_S - 1
  %error "snpad: COMPACT's lines overrun its foot"
%endif

SNP_TX      equ 8               ; a text line's x
SNP_BX      equ 10              ; a box's x
SNP_BTX     equ 26              ; a box line's text x
SNP_TW      equ (SNP_SW - 6 - SNP_TX) / 8   ; 21 cells
SNP_BTW     equ (SNP_SW - 6 - SNP_BTX) / 8  ; 19
SNP_TTX     equ 24              ; the title's x
SNP_TTN     equ 17              ; ...its cells
SNP_PLUSX   equ 170             ; `+`
SNP_PINX    equ 7
SNP_PREVX   equ 56              ; the foot: `<`
SNP_NEXTX   equ 128             ; ...`>`
SNP_CNTX    equ 96              ; ...the count's middle
SNP_OPENX   equ 150             ; ...the arrow

SNP_H_NONE  equ 0               ; snp_press: what a press went to
SNP_H_PLUS  equ 1
SNP_H_OPEN  equ 2
SNP_H_PREV  equ 3
SNP_H_NEXT  equ 4
SNP_H_BOX   equ 5               ; (the rest of the card: nothing - a press
                                ; there only hands the widget the bar)

; snp_geo - DI = this layout's geometry. Every other register kept
snp_geo:
    push ax
    mov al, [stik_li]
    mov ah, SNPG_SZ
    mul ah
    add ax, snp_geos
    mov di, ax
    pop ax
    ret

; snp_widths - the walk's widths for the pad's lines
snp_widths:
    mov byte [it_w], SNP_TW
    mov byte [it_wb], SNP_BTW
    ret

; -----------------------------------------------------------------------------
; snp_paint - W_PAINT (SI = the pad, lock held, the clip armed): the
; include's paint - the ground, the card's slabs, the tag - then the About
; card over it while it is up there
; -----------------------------------------------------------------------------
snp_paint:
    jmp stik_paint

; snp_redraw - the card again, whole, outside a paint (lock held)
snp_redraw:
    push bx
    push dx
    push di
    call snp_geo
    xor bx, bx
    mov dl, [di + SNPG_H]
    xor dh, dh
    dec dx
    call snp_rrows
    pop di
    pop dx
    pop bx
    ret

; snp_rrows - card rows BX..DX again (lock held): os88stik.inc's stik_redrawr
; over them, which arms the clip and reaches snp_pcard for just those rows.
; Nothing when the pad is not on the glass. Every register kept
snp_rrows:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, [stik_win]
    test byte [es:si + W_FLAGS], 2
    jz .x
    test byte [stik_fl], STIK_F_QUIT
    jnz .x
    mov di, dx                  ; DI = the last card row
    push bx
    call stik_org               ; AX, DX = the face: the card's top is its top
    mov cx, ax
    add cx, STIK_FACE_W - 1
    pop bx
    add bx, dx                  ; the first screen row...
    add dx, di                  ; ...and the last
    call stik_redrawr
.x:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; snp_pcard - STIK_PAINTCARD: AL = the card (0), BX..DX its damaged rows,
; card-local; [stik_fx], [stik_cy] as stik_paint left them. Each 32-row slab
; the rows touch is papered, composed and put down - the stack's two notches
; left to the desktop on a desk window (snp_put). Every register kept
; -----------------------------------------------------------------------------
snp_pcard:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [snp_r0], bx
    mov [snp_r1], dx
    call sn_pencur
    call sn_setpen              ; the note's paper
    mov ax, bx
    xor dx, dx
    mov cx, GFXE_BAND_H
    div cx
    mul cx
    mov [snp_ba], ax            ; the first slab touched
.slab:
    call snp_geo
    mov cl, [di + SNPG_H]
    xor ch, ch
    sub cx, [snp_ba]
    cmp cx, GFXE_BAND_H
    jbe .n
    mov cx, GFXE_BAND_H
.n:
    mov [gfxe_bh], cx
    mov [snp_bn], cx
    mov ax, 0FFFFh
    call gfxe_bclear            ; paper
    mov bx, [snp_ba]
    mov cx, [snp_bn]
    call snp_compose
    mov bx, [snp_r0]            ; the slab's damaged rows
    cmp bx, [snp_ba]
    jge .b0
    mov bx, [snp_ba]
.b0:
    mov dx, [snp_ba]
    add dx, [snp_bn]
    dec dx
    cmp dx, [snp_r1]
    jle .b1
    mov dx, [snp_r1]
.b1:
    call snp_put
    mov ax, [snp_ba]
    add ax, GFXE_BAND_H
    mov [snp_ba], ax
    cmp ax, [snp_r1]
    jle .slab
    mov al, CWHITE              ; the tag after us is the include's, in the
    call sn_setpen              ; default pen
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; snp_put - card rows BX..DX of the slab at [snp_ba] onto the glass. A desk
; window's top SNP_S rows leave the notch at the right to the desktop, its
; bottom SNP_S the one at the left - a blit from face x 4, off the byte grid,
; which kern_big's BLIT1 takes (SPEC.md 5.4.2.8). The pad is ONLY ever a
; desk window (STIK_DESKONLY: no titled fallback, whose notches were paper)
snp_put:
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jnz .desk
    mov cx, STIK_FACE_W         ; titled: the notches are paper
    xor ax, ax
    jmp snp_blit
.desk:
%endif
    call snp_geo
    mov al, [di + SNPG_H]
    xor ah, ah
    sub ax, SNP_S               ; AX = the first bottom row
    cmp bx, SNP_S
    jge .mid
    push dx                     ; --- the top rows: the sheet, and the notch
    cmp dx, SNP_S - 1
    jle .t
    mov dx, SNP_S - 1
.t:
    push ax
    xor ax, ax
    mov cx, SNP_SW
    call snp_blit
    mov ax, SNP_SW
    mov cx, STIK_FACE_W - 1
    call snp_desk
    pop ax
    pop dx
    mov bx, SNP_S
    cmp bx, dx
    jg .r
.mid:
    cmp bx, ax
    jge .bot
    push dx                     ; --- the middle: whole
    cmp dx, ax
    jl .m
    mov dx, ax
    dec dx
.m:
    push ax
    xor ax, ax
    mov cx, STIK_FACE_W
    call snp_blit
    pop ax
    pop dx
    mov bx, ax
    cmp bx, dx
    jg .r
.bot:                           ; --- the bottom rows: notch, strip, the rest
    push ax
    xor ax, ax
    mov cx, SNP_S - 1
    call snp_desk
    pop ax
    mov ax, SNP_S               ; off the byte grid: BLIT1 skips the byte
    mov cx, STIK_FACE_W - SNP_S ; column's first four (SPEC.md 5.4.2.8)
    jmp snp_blit
.r:
    ret

; snp_blit - band columns AX.. (the byte column AX is in), CX wide, card rows BX..DX
; of the slab at [snp_ba], onto the glass. Every register kept
snp_blit:
    push ax
    push bx
    push cx
    push dx
    push si
    sub dx, bx
    inc dx                      ; the rows
    mov si, bx
    sub si, [snp_ba]            ; the band row
    push ax
    push dx
    mov ax, GFXE_BAND_ST
    mul si
    mov si, ax
    pop dx
    pop ax
    push ax
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add si, ax                  ; ...its byte
    pop ax
    add ax, [stik_fx]
    add bx, [stik_cy]
    call sn_b1                  ; ES:SI = the band, BP its stride
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sn_b1 - one OSAPI_GFX_BLIT1 of our band from DS:SI+band: AX = x, BX = y, CX
; = width, DX = rows. Every register kept
sn_b1:
    push si
    push bp
    push es
    add si, GFXE_BAND_BUF
    push ds
    pop es
    mov bp, GFXE_BAND_ST
    call OSAPI_GFX_BLIT1
    pop es
    pop bp
    pop si
    ret

; snp_desk - the desktop over face columns AX..CX, card rows BX..DX (clipped
; to the paint's damage by stik_desk). Every register kept
snp_desk:
    push ax
    push bx
    push cx
    push dx
    add ax, [stik_fx]
    add cx, [stik_fx]
    add bx, [stik_cy]
    add dx, [stik_cy]
    call stik_desk
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; snp_compose - the card's picture on card rows BX .. BX+CX-1, into the band
; (row - BX). Every register kept
; -----------------------------------------------------------------------------
snp_compose:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [sn_t2], bx             ; the slab's first row
    mov [sn_t3], cx             ; ...and its rows
    mov byte [gfxe_tsty], 0
    call snp_geo
    ; --- the sheet's edge, and the stack's
    xor ax, ax
    xor bx, bx
    mov cx, SNP_SW
    mov dl, [di + SNPG_BW]
    xor dh, dh
    call snp_fill               ; top
    call snp_geo
    xor bx, bx
    mov bl, [di + SNPG_H]
    sub bx, SNP_S
    mov dl, [di + SNPG_BW]
    xor dh, dh
    sub bx, dx
    xor ax, ax
    mov cx, SNP_SW
    call snp_fill               ; bottom
    call snp_geo
    xor dx, dx
    mov dl, [di + SNPG_H]
    sub dx, SNP_S
    mov cl, [di + SNPG_BW]
    xor ch, ch
    xor ax, ax
    xor bx, bx
    call snp_fill               ; left
    call snp_geo
    xor dx, dx
    mov dl, [di + SNPG_H]
    sub dx, SNP_S
    mov cl, [di + SNPG_BW]
    xor ch, ch
    mov ax, SNP_SW
    sub ax, cx
    xor bx, bx
    call snp_fill               ; right
    call snp_geo
    xor dx, dx
    mov dl, [di + SNPG_H]
    sub dx, SNP_S
    mov ax, STIK_FACE_W - 1
    mov bx, SNP_S
    mov cx, 1
    call snp_fill               ; the stack: its right edge...
    call snp_geo
    xor bx, bx
    mov bl, [di + SNPG_H]
    dec bx
    mov ax, SNP_S
    mov cx, SNP_SW
    mov dx, 1
    call snp_fill               ; ...its bottom...
    mov ax, SNP_SW
    mov bx, SNP_S
    mov cx, SNP_S - 1
    mov dx, 1
    call snp_fill               ; ...its top, right of the sheet...
    call snp_geo
    xor bx, bx
    mov bl, [di + SNPG_H]
    sub bx, SNP_S
    mov ax, SNP_S
    mov cx, 1
    mov dx, SNP_S - 1
    call snp_fill               ; ...and its left, under it
    ; --- the turned corner: papered off, then the fold
    call snp_geo
    xor dx, dx
    mov dl, [di + SNPG_H]
    sub dx, SNP_S
    xor ax, ax
    mov al, [di + SNPG_K]
    sub dx, ax                  ; its top row
    mov si, [di + SNPG_EARP]
    mov cx, SNP_SW - 16
    mov byte [gfxe_tsty], GFXE_INV
    push dx
    call snp_rec
    pop dx
    mov byte [gfxe_tsty], 0
    call snp_geo
    mov si, [di + SNPG_EARI]
    mov cx, SNP_SW - 16
    call snp_rec
    ; --- the header: the pin, the title, `+`, the rule
    call snp_geo
    mov dl, [di + SNPG_PINY]
    xor dh, dh
    mov si, [di + SNPG_PIN]
    mov cx, SNP_PINX
    call snp_rec
    call snp_geo
    mov dl, [di + SNPG_HY]
    xor dh, dh
    call snp_title
    call snp_geo
    mov dl, [di + SNPG_HY]
    xor dh, dh
    mov byte [gfxe_tsty], GFXE_BOLD
    mov al, '+'
    mov cx, SNP_PLUSX
    call snp_glyph
    mov byte [gfxe_tsty], 0
    call snp_geo
    mov bl, [di + SNPG_RY]
    xor bh, bh
    mov dl, [di + SNPG_RH]
    xor dh, dh
    mov ax, 6
    mov cx, SNP_SW - 12
    call snp_fill
    ; --- the body
    call snp_body
    ; --- the foot
    call snp_foot
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; snp_fill - ink (or, with GFXE_INV, paper) over face x AX, card row BX, CX
; wide, DX rows - into the slab. AX..BP spent
snp_fill:
    sub bx, [sn_t2]
    jmp gfxe_bfill

; snp_rec - the bit record at SI at face x CX, card row DX. AX..BP spent
snp_rec:
    sub dx, [sn_t2]
    jmp gfxe_bits

; snp_glyph - AL at face x CX, card row DX. AX..BP spent but CX: past it
snp_glyph:
    sub dx, [sn_t2]
    jmp gfxe_glyph

; snp_vis - card rows DX - 2 .. DX + 9 (a line's) against the slab: CF = 1
; it misses. Every register kept
snp_vis:
    push ax
    mov ax, dx
    add ax, 9
    cmp ax, [sn_t2]
    jl .no
    mov ax, [sn_t2]
    add ax, [sn_t3]
    push dx
    sub dx, 2
    cmp dx, ax
    pop dx
    jge .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; sn_glyphs - BX bytes of DS:SI at band x CX, band y DX, in [gfxe_tsty]: a
; byte outside 32..126 is not drawn and takes no cell (Note Pad drops it). out
; CX past them; AX, BX, SI, DI, BP spent
sn_glyphs:
    or bx, bx
    jz .r
    lodsb
    dec bx
    cmp al, ' '
    jb sn_glyphs
    cmp al, 126
    ja sn_glyphs
    push si
    push bx
    push dx
    call gfxe_glyph
    pop dx
    pop bx
    pop si
    jmp short sn_glyphs
.r:
    ret

; snp_title - the header's title at card row DX: the note's first line, cut
; to SNP_TTN cells - or `Untitled`, or with no notes the program's name
snp_title:
    call snp_vis
    jc .r
    sub dx, [sn_t2]
    mov cx, SNP_TTX
    cmp byte [sn_n], 0
    je .none
    call sn_tend                ; AX = its length
    or ax, ax
    jz .unt
    cmp ax, SNP_TTN
    jbe .n
    mov ax, SNP_TTN
.n:
    mov bx, ax
    push dx
    push cx
    call sn_txt
    pop cx
    pop dx
    jmp sn_glyphs
.unt:
    mov si, sn_s_untit
    jmp gfxe_text
.none:
    mov si, sn_title
    jmp gfxe_text
.r:
    ret

; snp_body - the body's lines that fit, or the empty pad's two lines
snp_body:
    call snp_geo
    mov dl, [di + SNPG_B0]
    xor dh, dh
    cmp byte [sn_n], 0
    jne .lines
    call snp_vis                ; --- no notes: what to do about it
    jc .e2
    push dx
    sub dx, [sn_t2]
    mov si, sn_s_none
    mov cx, SNP_TX
    call gfxe_text
    pop dx
.e2:
    call snp_geo
    add dl, [di + SNPG_P]
    adc dh, 0
    add dl, [di + SNPG_P]
    adc dh, 0
    call snp_vis
    jc .r
    sub dx, [sn_t2]
    mov si, sn_s_plus
    mov cx, SNP_TX
    jmp gfxe_text
.lines:
    call snp_widths
    call sn_body
    jc .r                       ; a title and nothing else
    mov word [sn_t0], 0         ; the line
.l:
    call snp_geo
    mov al, [di + SNPG_P]
    mul byte [sn_t0]
    add al, [di + SNPG_B0]
    adc ah, 0
    mov dx, ax                  ; DX = its text row
    call snp_vis
    jc .next
    call snp_line
.next:
    inc word [sn_t0]
    call snp_geo
    mov al, [di + SNPG_NV]
    cmp [sn_t0], al
    jae .r
    call sn_vadv
    jnc .l
.r:
    ret

; snp_line - the walk's line at card row DX: its box on a box paragraph's
; first line, then its text
snp_line:
    mov cx, SNP_TX
    cmp byte [it_k], 0
    je .t
    mov cx, SNP_BTX
    cmp byte [it_f], 0
    je .t
    push dx
    call snp_geo
    mov al, [di + SNPG_BOXY]
    cbw
    add dx, ax
    mov si, [di + SNPG_BOX]
    cmp byte [it_k], SN_BOX1
    jne .b
    mov si, [di + SNPG_BOXX]
.b:
    mov cx, SNP_BX
    call snp_rec
    pop dx
    mov cx, SNP_BTX
.t:
    sub dx, [sn_t2]
    mov si, [it_p]
    mov bx, [it_le]
    sub bx, si
    jmp sn_glyphs

; snp_foot - `<  n / N  >` and the arrow, with a note to show
snp_foot:
    cmp byte [sn_n], 0
    je .r
    call snp_geo
    mov dl, [di + SNPG_FY]
    xor dh, dh
    call snp_vis
    jc .arrow
    push dx
    mov byte [gfxe_tsty], GFXE_BOLD
    mov al, '<'
    mov cx, SNP_PREVX
    call snp_glyph
    pop dx
    push dx
    mov al, '>'
    mov cx, SNP_NEXTX
    call snp_glyph
    mov byte [gfxe_tsty], 0
    call sn_count               ; sn_line = `n / N`, AX = its cells
    shl ax, 1
    shl ax, 1
    mov cx, SNP_CNTX
    sub cx, ax
    pop dx
    sub dx, [sn_t2]
    mov si, sn_line
    call gfxe_text
.arrow:
    call snp_geo
    mov dl, [di + SNPG_OY]
    xor dh, dh
    mov si, [di + SNPG_OPEN]
    mov cx, SNP_OPENX
    jmp snp_rec
.r:
    ret

; sn_count - sn_line = `n / N` for the current note; AX = its length
sn_count:
    push di
    mov di, sn_line
    mov al, [sn_cur]
    inc ax
    call sn_dec
    mov ax, ' ' | ('/' << 8)
    stosw
    mov al, ' '
    stosb
    mov al, [sn_n]
    call sn_dec
    mov byte [di], 0
    mov ax, di
    sub ax, sn_line
    pop di
    ret

; sn_dec - AL (0..99) in decimal at DI, DI past it. AX spent
sn_dec:
    push ds
    pop es
    xor ah, ah
    push cx
    mov cl, 10
    div cl
    pop cx
    or al, al
    jz .one
    add al, '0'
    stosb
.one:
    mov al, ah
    add al, '0'
    stosb
    ret

; -----------------------------------------------------------------------------
; the pad's presses: STIK_ONCARD (AL = the card, CX, DX the press,
; card-relative) records what it went to, STIK_ONCARDUP acts when the
; release is on the same thing (SPEC.md 13.5's slide-off)
; -----------------------------------------------------------------------------
snp_oncard:
    call sn_age                 ; an event: Delete's latch one older
    call snp_hit
    mov [snp_press], al
    ret
.none:
    mov byte [snp_press], SNP_H_NONE
    ret

snp_oncardup:
    mov al, [snp_press]
    mov byte [snp_press], SNP_H_NONE
    or al, al
    jz .r
    push ax
    call snp_hit
    pop bx
    cmp al, bl
    jne .r                      ; slid off
    cmp al, SNP_H_BOX
    je .box
    xor ah, ah
    mov bx, ax
    shl bx, 1
    jmp [snp_acts + bx - 2]
.r:
    ret
.box:
    jmp sn_flip                 ; the walk on the box's line, [sn_t1] it

snp_acts:   dw sn_new, sn_open, sn_prev, sn_next

; snp_hit - CX, DX card-relative: AL = what is there, SNP_H_*; on a box the
; walk is left on its line and [sn_t1] = the line
snp_hit:
    push bx
    push cx
    push dx
    push di
    call snp_geo
    mov al, SNP_H_NONE
    call .plus
    cmp byte [sn_n], 0
    je .x                       ; no notes: `+` is the only thing
.some:
    cmp al, SNP_H_PLUS
    je .x
    mov al, [di + SNPG_FY]      ; the foot: rows FY - 4 .. FY + 11
    xor ah, ah
    mov bx, ax
    sub bx, 4
    cmp dx, bx
    jl .body
    add bx, 15
    cmp dx, bx
    jg .edit
    mov al, SNP_H_PREV
    cmp cx, SNP_PREVX - 8
    jl .edit
    cmp cx, SNP_PREVX + 16
    jl .x
    mov al, SNP_H_NEXT
    cmp cx, SNP_NEXTX - 8
    jl .edit
    cmp cx, SNP_NEXTX + 16
    jl .x
    mov al, SNP_H_OPEN
    cmp cx, SNP_OPENX - 4
    jl .edit
    cmp cx, SNP_OPENX + 16
    jl .x
    jmp short .edit
.body:
    mov al, [di + SNPG_B0]      ; a box: its line's rows, x 8 .. 23
    sub al, [di + SNPG_LOFF]
    xor ah, ah
    sub dx, ax
    jl .edit
    cmp cx, SNP_BX - 2
    jl .edit
    cmp cx, SNP_BTX - 2
    jge .edit
    mov ax, dx
    div byte [di + SNPG_P]
    xor ah, ah
    cmp al, [di + SNPG_NV]
    jae .edit
    mov [sn_t1], ax
    call snp_widths
    call sn_vseek
    jc .edit
    cmp byte [it_k], 0
    je .edit
    cmp byte [it_f], 0
    je .edit
    mov al, SNP_H_BOX
    jmp short .x
.edit:
    mov al, SNP_H_NONE          ; the rest of the card is no control
.x:
    pop di
    pop dx
    pop cx
    pop bx
    ret
.plus:                          ; `+`: x 164 .. 183, rows HY - 4 .. HY + 11
    mov bl, [di + SNPG_HY]
    xor bh, bh
    sub bx, 4
    cmp dx, bx
    jl .pn
    add bx, 15
    cmp dx, bx
    jg .pn
    cmp cx, SNP_PLUSX - 6
    jl .pn
    cmp cx, SNP_PLUSX + 14
    jge .pn
    mov al, SNP_H_PLUS
.pn:
    ret

; snp_rline - the pad's body line [sn_t1] again, now (lock held)
snp_rline:
    push ax
    push bx
    push dx
    push di
    call snp_geo
    mov al, [di + SNPG_P]
    mul byte [sn_t1]
    add al, [di + SNPG_B0]
    adc ah, 0
    sub al, [di + SNPG_LOFF]
    sbb ah, 0
    mov bx, ax
    mov dl, [di + SNPG_P]
    xor dh, dh
    add dx, bx
    dec dx
    call snp_rrows
    pop di
    pop dx
    pop bx
    pop ax
    ret

; snp_onkey - W_ONKEY while the pad owns the keys: Left and Right page, Enter
; opens the note, Del deletes it (twice), Ctrl+N a new one
snp_onkey:
    call sn_age
    cmp ah, KSC_LEFT
    jne .k1
    jmp sn_prev
.k1:
    cmp ah, KSC_RIGHT
    jne .k2
    jmp sn_next
.k2:
    cmp ah, KSC_DEL
    jne .k3
    jmp sn_askdel
.k3:
    cmp al, 13
    jne .k4
    jmp sn_open
.k4:
    cmp al, SN_C_NEW
    jne .r
    jmp sn_new
.r:
    ret

sn_s_untit: db 'Untitled', 0
sn_s_none:  db 'No notes.', 0
sn_s_plus:  db 'Press + to add one.', 0

; --- the bit records (GFXE_BITS: db rows, then a word a row, bit 15 leftmost)
sn_pin11:   db 11
            dw 03F00h, 01E00h, 01E00h, 01E00h, 03F00h, 0FFC0h
            dw 00C00h, 00C00h, 00C00h, 00400h, 00400h
sn_pin8:    db 8
            dw 03C00h, 01800h, 01800h, 03C00h, 0FF00h, 01800h, 01800h, 00800h
sn_box10:   db 10
            dw 0FFC0h, 08040h, 08040h, 08040h, 08040h
            dw 08040h, 08040h, 08040h, 08040h, 0FFC0h
sn_box10x:  db 10
            dw 0FFC0h, 08040h, 08140h, 08340h, 0C640h
            dw 0EC40h, 0B840h, 09040h, 08040h, 0FFC0h
sn_box9:    db 9
            dw 0FF80h, 08080h, 08080h, 08080h, 08080h
            dw 08080h, 08080h, 08080h, 0FF80h
sn_box9x:   db 9
            dw 0FF80h, 08080h, 08280h, 08680h, 0CC80h
            dw 0B880h, 09080h, 08080h, 0FF80h
sn_box8:    db 8
            dw 0FF00h, 08100h, 08100h, 08100h, 08100h, 08100h, 08100h, 0FF00h
sn_box8x:   db 8
            dw 0FF00h, 08300h, 08500h, 0C900h, 0B100h, 08100h, 08100h, 0FF00h
sn_open11:  db 11
            dw 001F0h, 00030h, 0FC50h, 08090h, 08110h, 08240h
            dw 08440h, 08840h, 08040h, 08040h, 0FFC0h
sn_open8:   db 8
            dw 00780h, 00180h, 0F280h, 08480h, 08A00h, 09200h, 08200h, 0FE00h

; the turned corner, K rows ending on the sheet's last: the record's column
; 15 is the sheet's right edge. PAPER: everything past the diagonal; INK: the
; diagonal, and the fold's top and left edges
%macro SNP_EAR 2                ; label, K
%1 %+ p:
    db %2
  %assign %%r 0
  %rep %2
    dw (1 << %%r) - 1
    %assign %%r %%r + 1
  %endrep
%1 %+ i:
    db %2
    dw (1 << %2) - 1
  %assign %%r 1
  %rep %2 - 1
    dw (1 << (%2 - 1)) | (1 << %%r)
    %assign %%r %%r + 1
  %endrep
%endmacro
    SNP_EAR snp_ear12, 12
    SNP_EAR snp_ear10, 10
    SNP_EAR snp_ear8, 8
