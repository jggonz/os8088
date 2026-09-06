; =============================================================================
; os8088 - apps/tank/tkattr.inc
;
; THE ATTRACT WINDOW (SPEC.md 85.10). This is the WINDOWED half of the game
; and the only part of it that draws with the kernel's own slots - the game
; itself is past SPEC.md 53.7's fence and owns every pixel. So it is also
; where OSAPI_GFX_LINIT and OSAPI_GFX_LSTEP (SPEC.md 5.6.7) finally get a
; consumer in this package: the logo's gleam is two resumable walks chasing
; each other along a stroke that never lifts.
;
; ONE CLOSED LOOP A GLYPH (SPEC.md 85.10.1). The lettering is an OUTLINED
; heavy geometric face - the letters drawn as their own edges with empty
; centres - and every glyph is a single closed polygon listed from its
; lower-left vertex. A counter (only A has one) is reached by a bridge that is
; retraced, which is invisible because the same ink is laid twice.
;
; THE GAPS ARE EXACTLY WHAT 5.6.7 IS FOR. Two cursors leave a glyph's
; lower-left vertex, one with the point order and one against it, and stop at
; the vertex tk_lgg names - which is the lower-RIGHT corner wherever the letter
; has one. Each carries its OWN per-wake step, sized as its half over
; TK_LGWAKES, because the two halves of a letter are rarely the same length and
; the pair has to arrive together. Each gap is a HEAD laying the moving ink and
; a TAIL, a fixed number of wakes behind, laying the tube's own back over it -
; so there are four cursors, and a glyph is finished only when all four have
; spent what they owed it. Neither ever redraws a whole line, and the erase is
; the same recurrence as the draw, which is the one thing a whole-line call
; cannot promise (5.6.2). Which of the two inks is the brighter depends on the
; adapter: see the ink block below.
;
; THE PANEL IS THREE BLOCKS, NOT FOUR COLUMNS OF AIR. The logo is centred
; across the top and the score table and the instructions sit under it side by
; side, which is what the window's width is now measured from - see the layout
; block below and SPEC.md 85.10. The play line under the instructions blinks
; (85.10.3), on its own clock and drawn by nothing else.
; =============================================================================

TK_GLLAG  equ 2                 ; wakes the gap's tail runs behind its head
TK_LGWAKES equ 9                ; wakes a glyph's two gaps take to meet
TK_INSEG  equ 6                 ; whole segments drawn per wake, drawing in
TK_SCRT   equ 9                 ; wakes between score rolls
TK_ROWH   equ 9                 ; a score row, in pixels
TK_NROW   equ 6                 ; ...and how many the band shows: ALL OF THEM,
TK_NLIST  equ TK_NHS + 2        ; plus two blank ones, so the list still turns
                                ; over instead of standing still (85.10.2)
TK_BANDX  equ 8                 ; the band, in content coords: 8-ALIGNED both
TK_BANDW  equ 128               ; ends, which OSAPI_GFX_SCROLL requires

; A GAP CURSOR: a resumable walk, the glyph segment it is on, how far into it,
; how long that segment is, how many pixels it still owes this glyph, what it
; spends a wake, and which way round the loop it goes. Four of them - a head
; and a tail each way - and the STEP is per cursor because the two halves of a
; letter are rarely the same length and the pair has to arrive together.
TKC_BLK   equ 0
TKC_SEG   equ GLS_SZ
TKC_PIX   equ GLS_SZ + 2
TKC_LEN   equ GLS_SZ + 4
TKC_REM   equ GLS_SZ + 6
TKC_STEP  equ GLS_SZ + 8
TKC_DIR   equ GLS_SZ + 10       ; 0 = with the point order, 1 = against it
TKC_SZ    equ GLS_SZ + 11

; --- the panel's inks. SPEC.md 39.4 decides all of them: only 12, 14 and 15
; come out white on a 1bpp adapter, and a GLYPH in anything else rounds to
; black - so every string here is one of those three, on every adapter.
;
; THE LOGO IS A PAIR, AND THE PAIR TURNS OVER ON A 1bpp ADAPTER. A line in the
; dither class really does dither, and a dithered logo is a logo made of holes.
; So on Hercules and CGA the tube is drawn SOLID and the moving cursor lays
; BLACK - a gap chasing along the tube, which is the one contrast a 1bpp
; adapter has left once the tube is already white. On VGA the tube is cyan and
; the cursor is the brighter of the two, which is the effect the other way up.
TK_C_TUBE  equ CLCYAN               ; ...on a colour display
TK_C_GLEAM equ CWHITE
TK_M_TUBE  equ CWHITE               ; ...and on a 1bpp one
TK_M_GLEAM equ CBLACK
TK_C_HEAD  equ CYELLOW
TK_C_ROW   equ CWHITE
TK_C_INST  equ CWHITE
TK_C_PLAY  equ CLRED

; --- where everything sits (SPEC.md 85.10) ------------------------------------
; THE LOGO IS CENTRED ACROSS THE TOP and the two lists sit UNDER it, side by
; side: the score table left, the instructions right, and the play line centred
; on the INSTRUCTIONS rather than on the window (85.10.3). The logo used to
; share its row with the table, which is what made the window 480 wide with a
; band of nothing under the logo's left half; the width is now decided by the
; bottom pair alone - the band's rect, a gutter, the widest instruction line -
; and the logo, TK_LGINK wide, takes the margin that leaves either side.
TK_COLX   equ TK_BANDX + TK_BANDW + 8   ; the right column, 8 clear of the
TK_INSTW  equ 24 * 8                    ; band's RECT (which reaches further
                                        ; right than its digits do), and as
                                        ; wide as its own longest line
TK_CELLH  equ 8                         ; the 8x8 face's own height
TK_LOGOY  equ 8
TK_HEADY  equ 52
TK_BANDY  equ 63                    ; six rows of nine reach 117
TK_I1Y    equ 63                    ; the instructions start on the band's
TK_I2Y    equ 73                    ; FIRST ROW, so the two columns read as one
TK_I3Y    equ 83                    ; block with the header labelling the left
TK_PLAYY  equ 105                   ; one

; THE WINDOW IS THAT BOTTOM PAIR PLUS ITS MARGINS. TK_WINW is declared in
; tank.asm - before any of this, and it is what the window MANAGER is handed -
; so the arithmetic is asserted here rather than written down twice. The
; template's width IS the content width once wm_snap_w has rounded the frame up
; (SPEC.md 11.94.5), which is what makes the two comparable at all.
%if TK_WINW != TK_COLX + TK_INSTW + TK_BANDX
  %error "TK_WINW is not the attract panel's two columns plus their margins"
%endif

; --- the play line's blink (SPEC.md 85.10.3) ----------------------------------
; Two ticks a wake, so nine wakes is about a second: lit for a second and dark
; for half of one, which is the attract-screen cadence and not a flash.
TK_BLINKON  equ 9
TK_BLINKOFF equ 5

tk_s_press:  db 'PRESS F TO PLAY', 0
tk_s_i1:     db 'ARROWS OR W A S D DRIVE', 0
tk_s_i2:     db 'SPACE FIRES    P PAUSES', 0
tk_s_i3:     db 'ESC OR F LEAVES THE GAME', 0

; -----------------------------------------------------------------------------
; tk_at_layout - where everything sits, from the live content box
; -----------------------------------------------------------------------------
tk_at_layout:
    push ax
    push bx
    mov ax, [tk_cw]                 ; the logo is CENTRED across the top and its
    sub ax, TK_LGINK                ; width is fixed, so a window narrower than
    jns .lx                         ; the word pins it at the left edge rather
    xor ax, ax                      ; than hanging it off both. It is the INK
.lx:                                ; that is centred and not the advance: the
    shr ax, 1                       ; last cell's four blank columns are not
    mov [tk_at_lx], ax              ; part of the word the eye reads
    mov word [tk_at_ly], TK_LOGOY
    mov ax, [tk_winox]              ; the band's ends are settled HERE and in
    add ax, TK_BANDX + 7            ; absolute coordinates: OSAPI_GFX_SCROLL
    and al, 0F8h                    ; refuses an x1 or an x2+1 off the byte
    mov [tk_bx], ax                 ; grid, and TK_BANDW is a multiple of 8, so
                                    ; rounding one end settles both. A snapped
                                    ; window rounds by nothing; this is what
                                    ; makes the band right when it does not
    mov al, TK_C_TUBE               ; and the logo's pair, off the display this
    mov ah, TK_C_GLEAM              ; window is on rather than a build-time
    cmp byte [tk_vidk], VID_VGA     ; guess: on a two-card machine the two
    je .ink                         ; halves are different adapters
    cmp byte [tk_vidk], VID_EGA     ; ...and EGA is that same planar 16-colour
    je .ink                         ; path at 640x350 (SPEC.md 39.24)
    mov al, TK_M_TUBE
    mov ah, TK_M_GLEAM
.ink:
    mov [tk_ctube], al
    mov [tk_cgleam], ah
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_lgcell - adopt word cell [tk_lgi]: its glyph, its origin, its two halves
;
; Spaces are skipped here rather than by the caller, so "next cell" is one
; call and cannot land on one.
; -----------------------------------------------------------------------------
tk_lgcell:
    push ax
    push bx
    push cx
    push dx
    push si
.pick:
    mov bx, [tk_lgi]
    cmp bx, TK_LGCELLS
    jb .have
    xor bx, bx
    mov [tk_lgi], bx
.have:
    mov al, [tk_lgword + bx]
    cmp al, 0FFh                    ; a space carries no outline
    jne .glyph
    inc word [tk_lgi]
    jmp short .pick
.glyph:
    xor ah, ah                      ; the glyph's row is three bytes
    mov si, ax
    shl ax, 1
    add ax, si
    mov si, ax
    mov al, [tk_lgg + si]           ; where its points start...
    xor ah, ah
    mov [tk_lgp], ax
    mov al, [tk_lgg + si + 1]       ; ...how many...
    mov [tk_lgnp], ax
    mov al, [tk_lgg + si + 2]       ; ...and the segment its gaps meet at
    mov [tk_lge], ax

    mov ax, [tk_lgi]                ; the cell's own origin
    mov dx, TK_LGADV
    mul dx
    add ax, [tk_at_lx]
    add ax, [tk_winox]
    mov [tk_lgox], ax
    mov ax, [tk_at_ly]
    add ax, [tk_winoy]
    mov [tk_lgoy], ax
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tk_lgxy - point AX of the current glyph; out AX = screen x, BX = screen y
tk_lgxy:
    push cx
    push dx
    push si
    add ax, [tk_lgp]
    mov si, ax
    shl si, 1
    mov al, [tk_lgpts + si]
    xor ah, ah
    add ax, [tk_lgox]
    mov dx, ax
    mov al, [tk_lgpts + si + 1]
    xor ah, ah
    add ax, [tk_lgoy]
    mov bx, ax
    mov ax, dx
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; tk_lgseg - segment AX of the current glyph into tk_e1x/y and tk_e2x/y,
;            in the direction DL says (0 with the point order, 1 against it)
; -----------------------------------------------------------------------------
tk_lgseg:
    push ax
    push bx
    push cx
    mov cx, ax                      ; CX = the segment, AX = its far point
    inc ax
    cmp ax, [tk_lgnp]               ; the loop closes, so the last segment's
    jb .w                           ; far end is point 0
    xor ax, ax
.w:
    or dl, dl
    jz .fwd
    xchg ax, cx                     ; against the order: far point first
.fwd:
    push cx
    call tk_lgxy
    mov [tk_e1x], ax
    mov [tk_e1y], bx
    pop ax
    call tk_lgxy
    mov [tk_e2x], ax
    mov [tk_e2y], bx
    pop cx
    pop bx
    pop ax
    ret

; tk_seglen - the walk length of the segment now in tk_e1*/tk_e2*
; out: AX = max(|dx|,|dy|) + 1
tk_seglen:
    push cx
    mov ax, [tk_e2x]
    sub ax, [tk_e1x]
    jns .dx
    neg ax
.dx:
    mov cx, ax
    mov ax, [tk_e2y]
    sub ax, [tk_e1y]
    jns .dy
    neg ax
.dy:
    cmp ax, cx
    jae .len
    mov ax, cx
.len:
    inc ax
    pop cx
    ret

; -----------------------------------------------------------------------------
; tk_cur_init - arm cursor SI on the segment its TKC_SEG names
; clobbers AX, BX, CX, DX, DI, ES
; -----------------------------------------------------------------------------
tk_cur_init:
    mov dl, [si + TKC_DIR]
    mov ax, [si + TKC_SEG]
    call tk_lgseg
    call tk_seglen
    mov [si + TKC_LEN], ax
    mov word [si + TKC_PIX], 0
    push ds
    pop es
    mov di, si
    mov ax, [tk_e1x]
    mov bx, [tk_e1y]
    mov cx, [tk_e2x]
    mov dx, [tk_e2y]
    call OSAPI_GFX_LINIT
    ret

; tk_cur_next - move cursor SI onto the next segment its direction gives
tk_cur_next:
    push ax
    mov ax, [si + TKC_SEG]
    cmp byte [si + TKC_DIR], 0
    jne .back
    inc ax
    cmp ax, [tk_lgnp]
    jb .set
    xor ax, ax
    jmp short .set
.back:
    or ax, ax
    jnz .dec
    mov ax, [tk_lgnp]
.dec:
    dec ax
.set:
    mov [si + TKC_SEG], ax
    call tk_cur_init
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_cur_adv - spend one wake of cursor SI in ink AL
;
; It advances by its own TKC_STEP, bounded by what it still owes the glyph, so
; the two gaps of a letter run at whatever speeds make them meet together.
; clobbers everything but SI
; -----------------------------------------------------------------------------
tk_cur_adv:
    mov [tk_cink], al
    mov ax, [si + TKC_REM]
    or ax, ax
    jz .out                         ; this half is finished with
    cmp ax, [si + TKC_STEP]
    jbe .n
    mov ax, [si + TKC_STEP]
.n:
    mov [tk_cadv], ax
    sub [si + TKC_REM], ax
.more:
    cmp word [tk_cadv], 0
    je .out
    mov ax, [si + TKC_LEN]          ; what is left of THIS segment
    sub ax, [si + TKC_PIX]
    jg .have
    call tk_cur_next
    jmp short .more
.have:
    cmp ax, [tk_cadv]
    jbe .take
    mov ax, [tk_cadv]
.take:
    mov [tk_cn], ax
    mov al, [tk_cink]
    call OSAPI_SET_COLOR
    push ds
    pop es
    mov di, si
    mov cx, [tk_cn]
    call OSAPI_GFX_LSTEP
    mov ax, [tk_cn]
    add [si + TKC_PIX], ax
    sub [tk_cadv], ax
    mov ax, [si + TKC_LEN]
    cmp ax, [si + TKC_PIX]
    ja .more
    call tk_cur_next
    jmp short .more
.out:
    ret

; -----------------------------------------------------------------------------
; tk_lgarm - both gaps to the bottom-left of the cell [tk_lgi] is on
;
; The two halves of a glyph's outline are almost never the same length - a K
; is nearly three to one - so each cursor gets its own per-wake STEP, sized so
; that both spend TK_LGWAKES wakes whatever they have to cover. That is what
; makes the pair leave together and MEET together (SPEC.md 85.10.1).
; -----------------------------------------------------------------------------
tk_lgarm:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call tk_lgcell
    xor bx, bx                      ; --- the forward half, segments 0..e-1 ---
    xor cx, cx
.f:
    cmp cx, [tk_lge]
    jae .fd
    mov ax, cx
    xor dl, dl
    call tk_lgseg
    call tk_seglen
    add bx, ax
    inc cx
    jmp short .f
.fd:
    mov [tk_lgflen], bx
    xor bx, bx                      ; --- and the backward half, e..np-1 -----
.b:
    cmp cx, [tk_lgnp]
    jae .bd
    mov ax, cx
    xor dl, dl
    call tk_lgseg
    call tk_seglen
    add bx, ax
    inc cx
    jmp short .b
.bd:
    mov [tk_lgblen], bx

    mov si, tk_curfh                ; the two that lay the moving ink...
    mov di, tk_curbh
    call tk_lgpair
    mov si, tk_curft                ; ...and the two that lay it back
    mov di, tk_curbt
    call tk_lgpair
    mov byte [tk_at_lag], TK_GLLAG
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tk_lgpair - arm the forward cursor SI and the backward cursor DI
;
; DI IS BANKED IN MEMORY and not in the register, because tk_cur_init ends in
; OSAPI_GFX_LINIT and hands it the block in ES:DI - so the first init below
; leaves DI pointing at the FORWARD cursor. The backward one then never got a
; length or a step, and only one gap ever moved.
tk_lgpair:
    push si
    push di
    mov [tk_lgbp], di
    mov word [si + TKC_SEG], 0      ; forward: from point 0, with the order
    mov byte [si + TKC_DIR], 0
    mov ax, [tk_lgflen]
    mov [si + TKC_REM], ax
    call tk_lgstep1
    mov [si + TKC_STEP], ax
    call tk_cur_init

    mov si, [tk_lgbp]               ; backward: from point 0 too, against it,
    mov ax, [tk_lgnp]               ; which is the loop's LAST segment
    dec ax
    mov [si + TKC_SEG], ax
    mov byte [si + TKC_DIR], 1
    mov ax, [tk_lgblen]
    mov [si + TKC_REM], ax
    call tk_lgstep1
    mov [si + TKC_STEP], ax
    call tk_cur_init
    pop di
    pop si
    ret

; tk_lgstep1 - AX pixels over TK_LGWAKES wakes, rounded up, never zero
tk_lgstep1:
    push bx
    push dx
    add ax, TK_LGWAKES - 1
    xor dx, dx
    mov bx, TK_LGWAKES
    div bx
    or ax, ax
    jnz .ok
    mov ax, 1
.ok:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tk_logo_full - every glyph the draw-in has reached, as plain lines
; -----------------------------------------------------------------------------
tk_logo_full:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [tk_lgi]                ; tk_lgcell moves both of these, so the
    push ax                         ; caller's place in the word is banked
    mov ax, [tk_at_prog]
    push ax
    mov al, [tk_ctube]
    call OSAPI_SET_COLOR
    mov word [tk_lgi], 0
.cell:
    mov ax, [tk_lgi]
    cmp ax, [tk_at_prog]
    ja .done
    mov bx, ax                      ; a space has nothing to draw and tk_lgcell
    cmp byte [tk_lgword + bx], 0FFh    ; would skip PAST it, which would draw the
    je .next                        ; wrong glyph here
    call tk_lgcell
    xor cx, cx                      ; how many of its segments are up yet
    mov dx, [tk_lgnp]
    mov ax, [tk_lgi]
    cmp ax, [tk_at_prog]
    jne .whole
    mov dx, [tk_at_pseg]
.whole:
    cmp cx, dx
    jae .next
    push cx
    push dx
    mov ax, cx
    xor dl, dl
    call tk_lgseg
    mov ax, [tk_e1x]
    mov bx, [tk_e1y]
    mov cx, [tk_e2x]
    mov dx, [tk_e2y]
    xor si, si
    call OSAPI_GFX_LINE
    pop dx
    pop cx
    inc cx
    jmp short .whole
.next:
    inc word [tk_lgi]
    mov ax, [tk_lgi]
    cmp ax, TK_LGCELLS
    jb .cell
.done:
    pop ax
    mov [tk_at_prog], ax
    pop ax
    mov [tk_lgi], ax
    call tk_lgcell                  ; ...and the cell state put back with it
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_at_resume - re-arm the gaps after a full repaint
;
; A walk block holds ABSOLUTE screen coordinates (SPEC.md 5.6.7), so a window
; that moved leaves all four cursors walking the logo's old position; and the
; repaint has in any case erased the gaps they were half way through. Both are
; answered by arming the current glyph again from the top - a glyph is a
; second, so at worst the letter under the pointer replays.
; -----------------------------------------------------------------------------
tk_at_resume:
    push ax
    cmp byte [tk_at_glow], 0
    je .out
    call tk_lgarm
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_band_text - row AX of the table into tk_wbuf: "1 ABC   7777"
; -----------------------------------------------------------------------------
tk_band_text:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, tk_wbuf
    cmp ax, TK_NHS                  ; the two rows past the end of the table
    jb .row                         ; are the GAP the list turns over through
    mov cx, 13                      ; (85.10.2), and they have to be lettered
.blank:                             ; rather than skipped: the row arriving at
    mov byte [di], ' '              ; the foot of a scroll is whatever the blit
    inc di                          ; left there until something opaque covers
    loop .blank                     ; it
    mov byte [di], 0
    jmp .out
.row:
    mov bx, ax
    add al, '1'
    mov [di], al
    mov byte [di+1], ' '
    inc di
    inc di
    mov ax, bx                      ; row * 3 = the initials
    mov cx, ax
    shl ax, 1
    add ax, cx
    mov si, tk_hsn
    add si, ax
    mov al, [si]
    mov [di], al
    mov al, [si+1]
    mov [di+1], al
    mov al, [si+2]
    mov [di+2], al
    add di, 3
    mov ax, bx                      ; row * 4 = the score
    shl ax, 1
    shl ax, 1
    mov si, tk_hs
    add si, ax
    push di
    mov ax, [si]
    mov dx, [si+2]
    mov di, tk_numbuf
    call tk_utoa
    mov si, tk_numbuf
    call tk_strlen                  ; CX = its digits
    pop di
    mov ax, 8                       ; right-align in an eight-cell field
    sub ax, cx
    jns .pad
    xor ax, ax
.pad:
    or ax, ax
    jz .num
    mov cx, ax
.sp:
    mov byte [di], ' '
    inc di
    loop .sp
.num:
    mov si, tk_numbuf
    call tk_strcpy
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tk_band_row - table row AX, lettered at content row BX
tk_band_row:
    push ax
    push bx
    push cx
    push dx
    push si
    call tk_band_text
    mov cx, [tk_bx]
    mov dx, [tk_winoy]
    add dx, bx
    mov si, tk_wbuf
    mov al, TK_C_ROW
    mov ah, CBLACK
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tk_band_full - all TK_NROW visible rows, from [tk_at_scr]
tk_band_full:
    push ax
    push bx
    push cx
    mov cx, TK_NROW
    mov bx, TK_BANDY
    mov ax, [tk_at_scr]
.r:
    push ax
    push cx
    call tk_band_row
    pop cx
    pop ax
    inc ax
    cmp ax, TK_NLIST
    jb .w
    xor ax, ax
.w:
    add bx, TK_ROWH
    loop .r
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_band_roll - the band up one row, and letter the one that appears
;
; ONE OSAPI_GFX_SCROLL and ONE font_run, which is what a scrolling list costs
; when the band moves as a blit and only the row that arrived is drawn. The
; refusal path is a full redraw: a clip region that does not wholly contain
; the rect refuses, because a blit cannot be cut per pixel (SPEC.md 11.3).
; -----------------------------------------------------------------------------
tk_band_roll:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [tk_at_scr]             ; the top row scrolls off
    inc ax
    cmp ax, TK_NLIST
    jb .s
    xor ax, ax
.s:
    mov [tk_at_scr], ax
    mov ax, [tk_bx]
    mov bx, [tk_winoy]
    add bx, TK_BANDY
    mov cx, ax
    add cx, TK_BANDW - 1
    mov dx, bx
    add dx, TK_NROW * TK_ROWH - 1
    mov si, TK_ROWH
    call OSAPI_GFX_SCROLL
    jc .whole
    mov ax, [tk_at_scr]             ; the row that just appeared at the foot
    add ax, TK_NROW - 1
    cmp ax, TK_NLIST
    jb .row
    sub ax, TK_NLIST
.row:
    mov bx, TK_BANDY + (TK_NROW - 1) * TK_ROWH
    call tk_band_row
    jmp short .out
.whole:
    call tk_band_full
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_at_text - the parts that never move
; -----------------------------------------------------------------------------
tk_at_text:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, tk_s_hshead             ; the left column's header, over the band
    mov bx, TK_HEADY
    mov al, TK_C_HEAD
    mov cx, [tk_bx]
    call tk_at_left
    mov cx, [tk_winox]              ; ...and the right one as a BLOCK: three
    add cx, TK_COLX                 ; lines from one x, which is what makes it
    mov al, TK_C_INST               ; read as a column beside the table rather
    mov si, tk_s_i1                 ; than as three centred captions. tk_at_left
    mov bx, TK_I1Y                  ; preserves every register, so the ink and
    call tk_at_left                 ; the x are set once
    mov si, tk_s_i2
    mov bx, TK_I2Y
    call tk_at_left
    mov si, tk_s_i3
    mov bx, TK_I3Y
    call tk_at_left
    cmp byte [tk_at_blink], 0       ; the play line BLINKS (85.10.3), and the
    je .out                         ; ground under it was filled black by the
    call tk_at_play                 ; caller - so a dark phase draws nothing
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_at_playx - where the play line goes: AX = its absolute x, CX = its width
;
; ONE PLACE ANSWERS IT, because the lit run and the dark fill below have to
; agree to the pixel: a fill that is a cell narrow leaves a stripe of red
; standing for the whole dark phase.
; -----------------------------------------------------------------------------
tk_at_playx:
    push si
    mov si, tk_s_press
    call tk_strlen
    shl cx, 1
    shl cx, 1
    shl cx, 1
    mov ax, TK_INSTW                ; centred on the INSTRUCTION COLUMN and not
    sub ax, cx                      ; on the content box: the line belongs to
    jns .ok                         ; the block above it, and centring it on the
    xor ax, ax                      ; window would hang it half over the table
.ok:
    shr ax, 1
    and al, 0F8h                    ; ...and onto a cell boundary, which is what
    add ax, TK_COLX                 ; SPEC.md 6.1's single-store path wants
    add ax, [tk_winox]
    pop si
    ret

; -----------------------------------------------------------------------------
; tk_at_play - the play line, lit or dark as [tk_at_blink] says (SPEC.md 85.10.3)
;
; The lit half is one opaque run: ground and glyph in the same pass (6.1). The
; dark half is ONE FILL rather than a run in black ink - fifteen cells to write
; what a single rect writes is the same picture for fifteen times the work, and
; a phase turns over three times every two seconds for as long as the window is
; open.
; -----------------------------------------------------------------------------
tk_at_play:
    push ax
    push bx
    push cx
    push dx
    push si
    call tk_at_playx                ; AX = its x, CX = its width
    cmp byte [tk_at_blink], 0
    je .dark
    mov si, tk_s_press
    mov bx, TK_PLAYY
    mov cx, ax
    mov al, TK_C_PLAY
    call tk_at_left
    jmp short .out
.dark:
    add cx, ax                      ; the same rect, eight rows deep
    dec cx
    mov bx, [tk_winoy]
    add bx, TK_PLAYY
    mov dx, bx
    add dx, TK_CELLH - 1
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tk_at_left - SI = string, CX = absolute x, BX = content row, AL = ink
tk_at_left:
    push ax
    push bx
    push cx
    push dx
    push si
    mov dx, [tk_winoy]
    add dx, bx
    mov ah, CBLACK
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_at_step - one wake of the attract animation. Lock NOT held on entry.
; -----------------------------------------------------------------------------
tk_at_step:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_GFX_LOCK             ; **THE LOCK FIRST, THEN THE QUESTION**
    cmp byte [tk_abon], 0           ; ...and the credits are the same question:
    jne .unlock                     ; while they are up the UI task owns the
                                    ; content, and tk_abdismiss repaints the
                                    ; whole panel when they come down
    mov bx, [tk_win]                ; (SPEC.md 20.6 rule 5). wm_obscured's own
    call OSAPI_WM_OBSCURED          ; header says it is only trustworthy while
    jc .unlock                      ; the caller holds the lock, because the UI
                                    ; task mutates the z-order and the rects
                                    ; under it - and this runs on a WORKER,
                                    ; pre-emptively against that task. Asked
                                    ; above the lock it could answer "clear",
                                    ; park in gfx_lock behind a raise, and then
                                    ; draw the logo and the score band at
                                    ; ABSOLUTE screen coordinates over whatever
                                    ; had come to the front: covered or hidden,
                                    ; the repaint that uncovers us will put us
                                    ; back
    mov bx, [tk_win]
    call OSAPI_WM_CONTENT           ; A MOVED WINDOW IS A MOVED ORIGIN, and a
    cmp ax, [tk_winox]              ; move does not have to come through
    jne .moved                      ; W_PAINT - the window manager can carry
    cmp dx, [tk_winoy]              ; the content to its new place as a blit.
    je .same                        ; Everything this file holds is ABSOLUTE:
.moved:                             ; the band's left edge and both walk
    mov [tk_winox], ax              ; blocks. Re-settle them here or the next
    mov [tk_winoy], dx              ; roll scrolls a rect the band no longer
    call tk_at_layout               ; occupies, which smears one cell of it
    call tk_at_resume               ; sideways and leaves it there.
.same:
    cmp byte [tk_at_glow], 0
    jne .gleam

    mov cx, TK_INSEG                ; --- drawing in, a glyph at a time ------
    mov al, [tk_ctube]
    call OSAPI_SET_COLOR
.dseg:
    push cx
    mov bx, [tk_at_prog]
    cmp bx, TK_LGCELLS
    jae .done
    cmp byte [tk_lgword + bx], 0FFh    ; a space: nothing to draw, next cell
    je .dnext
    mov [tk_lgi], bx                ; tk_lgcell works off tk_lgi, so the
    call tk_lgcell                  ; draw-in's own cursor has to BE tk_lgi -
                                    ; it drew cell 0 eleven times otherwise
    mov ax, [tk_at_pseg]
    cmp ax, [tk_lgnp]
    jae .dnext
    xor dl, dl
    call tk_lgseg
    mov ax, [tk_e1x]
    mov bx, [tk_e1y]
    mov cx, [tk_e2x]
    mov dx, [tk_e2y]
    xor si, si
    call OSAPI_GFX_LINE
    inc word [tk_at_pseg]
    mov ax, [tk_at_pseg]
    cmp ax, [tk_lgnp]
    jb .dgo
.dnext:
    inc word [tk_at_prog]
    mov word [tk_at_pseg], 0
.dgo:
    pop cx
    loop .dseg
    jmp .scroll
.done:
    pop cx
    mov byte [tk_at_glow], 1        ; the outline is whole: the gaps take over
    mov word [tk_lgi], 0
    call tk_lgarm
    jmp .scroll

.gleam:
    mov si, tk_curfh                ; the two heads, each at its own pace...
    mov al, [tk_cgleam]
    call tk_cur_adv
    mov si, tk_curbh
    mov al, [tk_cgleam]
    call tk_cur_adv
    cmp byte [tk_at_lag], 0
    je .tails
    dec byte [tk_at_lag]            ; ...and the two tails, a fixed number of
    jmp short .lgdone               ; wakes behind, laying the tube back
.tails:
    mov si, tk_curft
    mov al, [tk_ctube]
    call tk_cur_adv
    mov si, tk_curbt
    mov al, [tk_ctube]
    call tk_cur_adv
.lgdone:
    cmp word [tk_curfh + TKC_REM], 0    ; the letter is finished when all four
    jne .scroll                         ; have spent what they owed it - the
    cmp word [tk_curbh + TKC_REM], 0    ; heads meet at the marked vertex and
    jne .scroll                         ; the tails catch up TK_GLLAG wakes
    cmp word [tk_curft + TKC_REM], 0    ; later, so nothing is left bright
    jne .scroll
    cmp word [tk_curbt + TKC_REM], 0
    jne .scroll
    inc word [tk_lgi]
    call tk_lgarm

.scroll:
    dec byte [tk_at_scrt]
    jnz .blink
    mov byte [tk_at_scrt], TK_SCRT
    call tk_band_roll
.blink:
    dec byte [tk_at_blt]            ; the play line's own clock (85.10.3): a
    jnz .unlock                     ; lit stretch and a shorter dark one, each
    xor byte [tk_at_blink], 1       ; reloaded as the phase turns over
    mov al, TK_BLINKOFF
    cmp byte [tk_at_blink], 0
    je .blset
    mov al, TK_BLINKON
.blset:
    mov [tk_at_blt], al
    call tk_at_play
.unlock:
    call OSAPI_GFX_UNLOCK
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_worker - the attract screen's clock (SPEC.md 20.6)
; -----------------------------------------------------------------------------
tk_worker:
    call OSAPI_GET_TICKS
    mov [tk_due], ax
.loop:
    mov bx, [tk_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here; if our
                                    ; close box was clicked this never returns
    add word [tk_due], 2
    call OSAPI_GET_TICKS
    mov bx, [tk_due]
    sub bx, ax
    jle .behind
    mov ax, bx
    call OSAPI_TASK_SLEEP
    jmp short .frame
.behind:
    mov [tk_due], ax                ; late: re-anchor rather than catch up
.frame:
    call tk_at_step
    jmp .loop

; -----------------------------------------------------------------------------
; tk_hire - the worker, from the first paint: OSAPI_TASK_SPAWN wants the gfx
; lock held and refuses from the entry proc. The refusal is transient, so this
; retries on every paint rather than giving up.
; -----------------------------------------------------------------------------
tk_hire:
    cmp byte [tk_hired], 0
    jne .out
    push ax
    push bx
    mov ax, tk_worker
    mov bx, [tk_win]
    call OSAPI_TASK_SPAWN
    jc .nope
    mov byte [tk_hired], 1
.nope:
    pop bx
    pop ax
.out:
    ret
