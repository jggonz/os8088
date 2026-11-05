; =============================================================================
; os8088 - apps/mousechk/ui.inc - layout, drawing, controls (SPEC.md 126.2,
;                                 126.3)
;
; The shell keeps two frames (SPEC.md 126.2.3): the rows being built, in
; rows.inc's mc_buf, and the rows SHOWN - one hash per screen line, mc_shown[k],
; of what that line last drew. mc_drawdiff hashes each visible row and draws
; only the lines whose hash moved, every line ONE opaque OSAPI_FONT_RUN of a
; fixed width, so a redraw in place needs no erase (SPEC.md 6.1) and a text x
; is a multiple of 8 from a snapped content origin. A heading's lamp is one
; opaque 8x8 BLIT1 beside its run, so no pixel is written twice.
;
; Every rect derives from the window as it is (OSAPI_WM_CONTENT and _GEOM) at
; each paint, and mc_regeo lays out again in every handler, because a title-bar
; drag replays the window and never calls W_PAINT (SPEC.md 11.96.12.0).
; =============================================================================

MC_BTH      equ 15              ; a button's height (Timer's, SPEC.md 111.1)
MC_MAXFIT   equ 48              ; screen lines the shown-hash table holds
MC_TICKS    equ 9               ; the refresh: 9 ticks is ~0.5 s at 18.2 Hz
MC_INK      equ (CWHITE << 8) | CBLACK      ; black on white
MC_INV      equ (CBLACK << 8) | CWHITE      ; white on black: a heading

; -----------------------------------------------------------------------------
; mc_layout - every rect, from the window as it is now
; in:  BX = window ptr; out: nothing; clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
mc_layout:
    push bx
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [mc_cx], ax
    mov [mc_cy], dx
    call OSAPI_WM_GEOM              ; CX = width, DX = height
    mov [mc_cw], cx
    mov [mc_ch], dx
    push bx
    call OSAPI_WM_DISPLAY           ; DH = bits per pixel
    mov [mc_bpp], dh
    pop bx
    mov ax, [mc_ch]
    mov cx, 12                      ; the pitch: 12 on VGA's height, 10 on
    cmp ax, 380                     ; Hercules', 9 on CGA's
    jae .lp
    mov cx, 10
    cmp ax, 250
    jae .lp
    mov cx, 9
.lp:
    mov [mc_pitch], cx
    mov ax, [mc_cx]
    add ax, 8
    mov [mc_gx], ax
    mov ax, [mc_cy]
    add ax, 6
    mov [mc_gy], ax
    mov ax, [mc_cy]
    add ax, [mc_ch]
    sub ax, MC_BTH + 6
    mov [mc_bty], ax
    sub ax, 6                       ; the pane's bottom
    mov [mc_sb+6], ax               ; the bar's y2
    sub ax, [mc_gy]
    xor dx, dx
    div cx                          ; AX = the lines that fit
    cmp ax, MC_MAXFIT
    jbe .f1
    mov ax, MC_MAXFIT
.f1:
    or ax, ax
    jnz .f2
    inc ax
.f2:
    mov [mc_fit], ax
    mov [mc_sb+10], ax
    mov ax, [mc_gy]
    sub ax, 2
    mov [mc_sb+2], ax               ; y1
    mov ax, [mc_cx]
    add ax, [mc_cw]
    sub ax, 5
    mov [mc_sb+4], ax               ; x2
    sub ax, 13
    mov [mc_sb+0], ax               ; x1: 14 wide, the standard bar
    sub ax, 4
    sub ax, [mc_gx]
    mov cl, 3
    shr ax, cl
    cmp ax, 96
    jbe .c
    mov ax, 96
.c:
    mov [mc_cells], ax
    mov byte [mc_laid], 1
    pop bx
    ret

; -----------------------------------------------------------------------------
; mc_regeo - lay out again when the window is no longer where mc_gx was laid
;            out. Every handler that draws or hit-tests calls it first
; in:  nothing ([mc_win]); out: nothing; keeps every register
; -----------------------------------------------------------------------------
mc_regeo:
    cmp byte [mc_laid], 0
    je .ret
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [mc_win]
    call OSAPI_WM_CONTENT
    cmp ax, [mc_cx]
    jne .lay
    cmp dx, [mc_cy]
    je .out
.lay:
    call mc_layout
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; -----------------------------------------------------------------------------
; mc_fixtop - the scroll position within the list; total and fit into the bar
;             The shown frame is dropped when the view moved
; -----------------------------------------------------------------------------
mc_fixtop:
    push ax
    push cx
    mov ax, [mc_nrows]
    mov [mc_sb+8], ax
    sub ax, [mc_fit]
    jns .m
    xor ax, ax
.m:
    mov cx, [mc_top]
    cmp cx, ax
    jbe .set
    mov cx, ax
    call mc_forget
.set:
    mov [mc_top], cx
    mov [mc_sb+12], cx
    pop cx
    pop ax
    ret

; mc_forget - nothing on the glass is known any more: every line draws next
mc_forget:
    push ax
    push cx
    push di
    mov di, mc_shown
    mov cx, MC_MAXFIT
    xor ax, ax
.z:
    mov [di], ax
    inc di
    inc di
    loop .z
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_clip - arm the clip for the window: CF = 1 not one pixel of it is visible
; -----------------------------------------------------------------------------
mc_clip:
    push bx
    mov bx, [mc_win]
    call OSAPI_WM_CLIP_SET
    pop bx
    ret

; -----------------------------------------------------------------------------
; mc_drawall - everything, once (W_PAINT: the content is white, the clip armed)
; in:  nothing; out: nothing; clobbers every register
; -----------------------------------------------------------------------------
mc_drawall:
    call mc_fixtop
    xor si, si                      ; a line the model has no row for is already
.s:                                 ; white: its hash is the blank's
    cmp si, MC_MAXFIT
    jae .go
    mov di, si
    shl di, 1
    xor ax, ax
    mov bx, [mc_top]
    add bx, si
    cmp bx, [mc_nrows]
    jb .st
    mov ax, 1
.st:
    mov [mc_shown+di], ax
    inc si
    jmp short .s
.go:
    call mc_drawdiff
    mov bx, mc_sb
    call os88ui_sbupdw              ; the whole bar, and what it drew
    call mc_buttons
    ret

; -----------------------------------------------------------------------------
; mc_drawdiff - the lines whose row differs from what they show
; in:  the clip armed, gfx lock held; clobbers every register
; -----------------------------------------------------------------------------
mc_drawdiff:
    xor si, si
.k:
    cmp si, [mc_fit]
    jae .done
    mov bx, [mc_top]
    add bx, si
    call mc_hash
    mov di, si
    shl di, 1
    cmp ax, [mc_shown+di]
    je .nx
    mov [mc_shown+di], ax
    mov ax, si
    call mc_drawline
.nx:
    inc si
    jmp short .k
.done:
    ret

; -----------------------------------------------------------------------------
; mc_drawline - screen line AX, as its row says (a blank when there is none)
; in:  AX = k; the clip armed; out: nothing; keeps every register
; -----------------------------------------------------------------------------
mc_drawline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, ax
    mov cx, [mc_pitch]
    mul cx
    add ax, [mc_gy]
    mov dx, ax                      ; DX = y
    add bx, [mc_top]                ; BX = the row
    mov cx, [mc_gx]                 ; CX = x
    cmp bx, [mc_nrows]
    jae .blank
    shl bx, 1
    mov si, [mc_roff+bx]
    mov al, [si]                    ; kind
    mov ah, [si+1]                  ; lamp
    add si, 2
    or al, al
    jnz .plain
    push si                         ; a heading: its lamp, then its text bar
    mov al, ah
    xor ah, ah
    call mc_lampdraw
    pop si
    push cx
    push dx
    xor bl, bl
    mov ax, [mc_cells]
    dec ax
    dec ax
    call mc_runbuild
    pop dx
    pop cx
    add cx, 16                      ; the bar starts where the labels do
    mov ax, MC_INV
    jmp short .run
.plain:
    push cx
    push dx
    mov bl, 2
    mov ax, [mc_cells]
    call mc_runbuild
    pop dx
    pop cx
    mov ax, MC_INK
    jmp short .run
.blank:
    push cx
    push dx
    mov si, mc_s_none
    xor bl, bl
    mov ax, [mc_cells]
    call mc_runbuild
    pop dx
    pop cx
    mov ax, MC_INK
.run:
    mov si, mc_run
    call OSAPI_FONT_RUN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_runbuild - mc_run = BL spaces, the text at SI, then spaces: AX cells
; in:  SI = NUL text, AX = cells, BL = the leading spaces
; out: mc_run, NUL-ended; clobbers AX, BX, CX, SI, DI
; -----------------------------------------------------------------------------
mc_runbuild:
    mov di, mc_run
    mov cx, ax
    cmp cx, 96
    jbe .c
    mov cx, 96
.c:
    jcxz .end
    mov al, ' '
    or bl, bl
    jz .t
    dec bl
    jmp short .st
.t:
    mov al, [si]
    or al, al
    jz .pad
    inc si
    jmp short .st
.pad:
    mov al, ' '
.st:
    mov [di], al
    inc di
    dec cx
    jmp short .c
.end:
    mov byte [di], 0
    ret

; -----------------------------------------------------------------------------
; mc_lampdraw - the lamp, in the SHAPE that carries it on 1bpp and the COLOUR
;               that carries it on 4bpp (SYSCHECK's, SPEC.md 118.5)
; in:  CX = x, DX = y, AL = the lamp (LP_*); the clip armed; keeps everything
; -----------------------------------------------------------------------------
mc_lampdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    pop es                          ; the bitmap is ours
    mov bl, al
    xor bh, bh
    mov si, bx
    shl si, 1
    shl si, 1
    shl si, 1                       ; SI = its eight bytes' offset
    cmp byte [mc_bpp], 1
    ja .colour
    add si, mc_lmono                ; a lit bit is WHITE on 1bpp, so on the
    mov di, mc_lband                ; white ground the shape is inverted
    push cx
    mov cx, 8
.m:
    mov al, [si]
    not al
    mov [di], al
    inc si
    inc di
    loop .m
    pop cx
    mov si, mc_lband
    jmp short .blit
.colour:
    add si, mc_lcolsh
    mov al, [mc_lcol+bx]            ; AL = the ink...
    mov ah, CWHITE                  ; ...AH = the ground
    call OSAPI_GFX_BLIT1_PEN
.blit:
    mov ax, cx                      ; AX = x
    mov bx, dx                      ; BX = y
    mov cx, 8
    mov dx, 8
    mov bp, 1
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

; --- the lamps, eight rows each, in LP_* order --------------------------------
mc_lmono:                           ; 1bpp: SHAPE carries it
    db 0x40, 0x60, 0x70, 0x78, 0x70, 0x60, 0x40, 0x00   ; INFO: an arrow
    db 0x02, 0x06, 0x8C, 0xD8, 0x70, 0x20, 0x00, 0x00   ; OK: a tick
    db 0xAA, 0x54, 0xAA, 0x54, 0xAA, 0x54, 0xAA, 0x00   ; WARN: checkers
    db 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0x00   ; BAD: solid
    db 0xFE, 0x82, 0x82, 0x82, 0x82, 0x82, 0xFE, 0x00   ; N/A: hollow
mc_lcolsh:                          ; 4bpp: COLOUR carries it
    db 0x40, 0x60, 0x70, 0x78, 0x70, 0x60, 0x40, 0x00
    db 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0x00
    db 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0x00
    db 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0x00
    db 0xFE, 0x82, 0x82, 0x82, 0x82, 0x82, 0xFE, 0x00
mc_lcol:    db CBLUE, CGREEN, CBROWN, CRED, CDGRAY

; -----------------------------------------------------------------------------
; the buttons (the standard control, SPEC.md 20.5.1.3)
; -----------------------------------------------------------------------------
MB_REFRESH  equ 0
MB_PROBE    equ 1
MB_SAVE     equ 2
MB_COPY     equ 3
MB_QUIT     equ 4
MC_NB       equ 5

; mc_bok - would this button do anything? (SPEC.md 47 rule 2's one predicate)
; in:  AL = index; out CF = 1 greyed; every register kept
; p2_canprobe is a SECTION's proc and promises only CF and SI, so everything
; mc_buttons' loop keeps live across it (BX, BP) is saved here, and ES is put
; on the kernel for it as for every section call (SPEC.md 126.2.1)
mc_bok:
    cmp al, MB_PROBE
    jne .yes
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call mc_kes
    call p2_canprobe
    pop es                          ; (pops keep CF)
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.yes:
    clc
    ret

; mc_place - OS88UI_BT_PLACE: the rects where the window IS (SPEC.md 20.5.1.3.5)
; in:  BX = the record, SI = the window; keeps every register
mc_place:
    cmp byte [mc_laid], 0
    je .ret
    call mc_regeo
    jmp short mc_brects
.ret:
    ret

; mc_brects - the five buttons' inclusive screen rects: the ONE description
;             mc_buttons draws from and mc_place hit-tests from
mc_brects:
    push ax
    push cx
    push dx
    push si
    push di
    mov ax, [mc_cx]
    add ax, 8
    mov dx, [mc_bty]
    mov cx, dx
    add cx, MC_BTH - 1
    mov di, mc_rects
    mov si, mc_btw
.r:
    mov [di+0], ax
    mov [di+2], dx
    add ax, [si]
    dec ax
    mov [di+4], ax
    mov [di+6], cx
    add ax, 9                       ; the next one, 8 pixels along
    add di, 8
    inc si
    inc si
    cmp si, mc_btw + 2 * MC_NB
    jb .r
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; mc_buttons - the buttons' rects and greying, then the buttons (keeps SI)
mc_buttons:
    push si
    push bp
    call mc_brects
    mov bx, mc_bflags
    xor bp, bp
.r:
    mov ax, bp
    call mc_bok                     ; CF = 1: greyed (SPEC.md 47)
    mov ax, OS88UI_FILL
    jnc .live
    or ax, OS88UI_DIS
.live:
    mov [bx], ax
    inc bx
    inc bx
    inc bp
    cmp bp, MC_NB
    jb .r
    mov bx, mc_btrec
    mov al, 1                       ; index PLUS ONE
.d:
    call os88ui_btn
    inc ax
    cmp al, MC_NB + 1
    jb .d
    mov al, CBLACK
    call OSAPI_SET_COLOR
    call mc_pg                      ; ...and remember what Probe was drawn as
    pop bp
    pop si
    ret

; mc_pg - AL = FF when the Probe button is greyed now, else 00 (and [mc_pgrey])
mc_pg:
    mov al, MB_PROBE
    call mc_bok
    mov al, 0
    sbb al, 0
    mov [mc_pgrey], al
    ret

; -----------------------------------------------------------------------------
; mc_after - a rebuild is done: draw what moved
;            Every refresh, tick or probe ends here. With the About card up the
;            model is kept and nothing is drawn: its dismissal repaints
; -----------------------------------------------------------------------------
mc_after:
    cmp byte [mc_laid], 0
    je .out
    call mc_regeo
    cmp byte [mc_btrec+OS88UI_BT_N], 0
    je .out
    call mc_clip
    jc .out
    call mc_fixtop
    call mc_drawdiff
    mov bx, mc_sb
    call os88ui_sbupd
    mov al, MB_PROBE                ; Probe's greying follows the machine
    call mc_bok
    mov al, 0
    sbb al, 0
    cmp al, [mc_pgrey]
    je .out
    call mc_buttons                 ; (which banks the new state)
.out:
    ret

; -----------------------------------------------------------------------------
; scrolling (keys and the bar)
; -----------------------------------------------------------------------------
; mc_scroll - move the view by AX lines (keeps SI)
mc_scroll:
    push si
    mov cx, [mc_top]
    add ax, cx
    jns .p
    xor ax, ax
.p:
    mov dx, [mc_nrows]
    sub dx, [mc_fit]
    jns .m
    xor dx, dx
.m:
    cmp ax, dx
    jbe .set
    mov ax, dx
.set:
    cmp ax, cx
    je .out
    mov [mc_top], ax
    mov [mc_sb+12], ax
    call mc_forget
    call mc_clip
    jc .out
    call mc_drawdiff
    mov bx, mc_sb
    call os88ui_sbupd
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; mc_paint - W_PAINT: lay out for the window as it is and draw it all
; in:  SI = window ptr, content white-filled, gfx lock held; keeps SI
; -----------------------------------------------------------------------------
mc_paint:
    push si
    mov bx, si
    call mc_layout
    call mc_drawall
    pop si
    cmp byte [mc_btrec+OS88UI_BT_N], 0 ; the About card is up: drawn LAST,
    jne .out                        ; over what it is opaque about
    push si
    mov bx, si
    mov si, mc_ablines
    call os88ui_about_d
    pop si
.out:
    ret

; mc_repaint - erase the content and paint it again, from a handler
; in:  SI = window ptr; gfx lock held
mc_repaint:
    push si
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .out
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call OSAPI_WM_CONTENT
    push ax
    push dx
    call OSAPI_WM_GEOM              ; CX = width, DX = height
    pop bx                          ; BX = y1
    pop ax                          ; AX = x1
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call OSAPI_GFX_FILL
    pop si
    push si
    call mc_paint
.out:
    pop si
    ret

; --- the buttons' record, here with the calls that draw it (t_btnrules) -------
mc_s_refresh: db 'Refresh', 0
mc_s_probe:   db 'Probe PS/2', 0
mc_s_save:    db 'Save Report', 0
mc_s_copy:    db 'Copy', 0
mc_s_quit:    db 'Quit', 0
mc_btlbl:   dw mc_s_refresh, mc_s_probe, mc_s_save, mc_s_copy, mc_s_quit
mc_btw:     dw 64, 96, 104, 48, 48  ; each button's width, left to right
    OS88UI_BTNREC mc_btrec, mc_rects, mc_btlbl, mc_bflags, MC_NB, mc_place

