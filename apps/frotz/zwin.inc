; =============================================================================
; os8088 - apps/frotz/zwin.inc
;
; The text window for v1-v5, v7 and v8: geometry, word wrap, the scrollback
; ring, the [MORE] prompt, the upper window, the v1-3 status line, styles and
; colour. v6 replaces the whole model with pixel-addressed windows and is
; zwin6.inc; the two never run together (docs/FROTZ-PLAN.md 5).
;
; THE DECISION THIS FILE IS BUILT AROUND: every cell it paints is painted by
; OSAPI_FONT_RUN and by nothing else. Not one gfx_fill touches a cell.
;
; That is not a style preference, it is the answer to SPEC.md 11.3's
; granularity rule. The fills clip per pixel and the glyphs clip per whole
; 8x8 cell, so a module that erases a band and then letters it goes BLANK
; rather than stale wherever a clip edge cuts the band - and this module draws
; from a background worker into a window other windows sit on top of, which is
; exactly where that happens. FONT_RUN is one decision about one cell, so the
; failure cannot arise and no OSAPI_WM_CLIP_TEST gate is needed anywhere
; below. Note Pad's trick comes with it: a row buffer padded with SPACES to
; the column count IS the erase (apps/notepad/notepad.asm np_rstart), because
; FONT_RUN paints a space as pure background.
;
; WHAT A LINE OF PROSE COSTS (PERFORMANCE.md Part 5 is the standing budget,
; and this is the row in it that belongs to Frotz):
;
;   1 x OSAPI_GFX_SCROLL      move the band up one row
;   1 x OSAPI_FONT_RUN        blank the row the blit vacated
;   1 x OSAPI_FONT_RUN        per WORD - spaces are never drawn at all,
;                             because the row they land on was blanked when
;                             it was vacated
;
; A 69-column line of ordinary prose is about 12 words, so 14 primitive calls:
; ~10.6 ms of the 756 us fixed per-call floor plus ~62 ms of glyph time on a
; 4.77 MHz 8088. The full repaint it replaces is 46 rows x 69 cells ~ 2.9 s.
; Nothing here repaints more than it changed; when OSAPI_GFX_SCROLL refuses
; (an x-range it cannot byte-align, an armed clip region that does not contain
; the rect - SPEC.md 5.5) the fall-back is zw_paint, which is a full repaint
; and is the documented price of a refusal, not the normal path.
;
; THE GFX LOCK. Every routine here is called with the lock ALREADY HELD - by
; W_PAINT, W_ONKEY, W_ONCLICK, the About handler, or by the worker around its
; output burst. This module never takes it. The single exception is the
; [MORE] wait, which drops the caller's lock and takes it back, because a
; worker may not hold the lock across a yield (SPEC.md 20.6) and that wait is
; unbounded. It is called out again at zw_morewait.
;
; ES. The worker runs with ES = the story segment (docs/FROTZ-PLAN.md 3.1),
; so every routine below that touches the scrollback claim or does a string
; instruction saves and restores ES itself. Callers never have to think
; about it.
;
; ABI NOTE: the design brief names the style setter `zw_style`, but zbss.inc
; already spends that identifier on the live style byte and a `%define`
; cannot be a label. The routine is `zw_setstyle`; see the report.
; =============================================================================

; --- text styles (Standard 8.7.1). The bit values are the story's, not ours.
ZST_ROMAN    equ 0
ZST_REVERSE  equ 1
ZST_BOLD     equ 2
ZST_ITALIC   equ 4
ZST_FIXED    equ 8                  ; the kernel font IS fixed pitch (SPEC.md 6)

; --- the scrollback claim ----------------------------------------------------
; One 128-byte record per line: a length byte, the style the line was written
; in, then up to 126 characters. Fixed stride so line -> offset is a shift and
; nothing has to be scanned; 24 KB is 192 records, of which the first ZW_UPMAX
; are the upper window's grid and the rest are the ring.
ZW_LSTRIDE   equ 128
ZW_LHDR      equ 2                  ; the length byte and the style byte
ZW_LMAX      equ ZW_LSTRIDE - ZW_LHDR
ZW_UPMAX     equ 12                 ; upper-window rows the grid can hold
ZW_SBKB      equ 24                 ; the claim we want...
ZW_SBKB2     equ 8                  ; ...and what we will settle for

ZW_MAXCOL    equ 100                ; 720/8 is 90 on the widest adapter; the
                                    ; cap is what makes zw_rbuf a real bound
ZW_STNAME    equ 48                 ; status-line name, captured (Standard
                                    ; 8.2.2.2 asks for at least 49)
ZW_STRIGHT   equ 56                 ; where the right-hand field is composed

; --- the scroll bar, matching Note Pad's so the two look like one machine ----
ZW_SB_STEP   equ 3                  ; rows an arrow click steps
ZW_GROW      equ 13                 ; the kernel's grow box, bottom right

; --- THE TWO UPPER-WINDOW COUNTS, and why there are two ----------------------
; [zw_upper] is how many rows the STORY owns. [zw_uphold] is how many are still
; ON THE GLASS, and it is never smaller.
;
; They differ because shrinking the upper window does not erase it. Standard
; 8.7.2 leaves the case open and every interpreter worth matching leaves those
; pixels alone: the rows revert to the lower window and keep what was in them
; until its own text scrolls over them. That is not a curiosity - it is how
; every Inform quote box works. `box` splits the window large, prints the box,
; selects the lower window and splits back to the status line's height, so a
; window model that repaints on a shrink erases every quote box in existence
; the instant it is drawn. BEAR.Z5 and CURSES.Z5 both open on one.
;
; Keeping the rows in the GRID rather than merely leaving them on screen is the
; part that matters here. Anything on the glass that no model holds is erased
; by the first uncover, and this window is repainted from the model by
; construction (SPEC.md 11, 59.5) - so "do not repaint on a shrink" would trade
; a guaranteed defect for an intermittent one. zw_up_draw draws [zw_uphold]
; rows, the lower window starts below them, and the hold is given back the
; moment the lower window actually needs the space (zw_scroll1).
;
; PERFORMANCE: this REMOVES the repaint a shrink used to cost and adds one, at
; most once per box, when the lower window first scrolls afterwards. A story
; that never boxes never pays anything (PERFORMANCE.md Part 5).
;
; --- private state, from this module's slack range (zbss.inc 10) -------------
; ZWIN_SLACK is 256 bytes and this uses 232 of them.
%define zw_tx     (os88_image_end + ZWIN_SLACK + 0)   ; word: text origin x
%define zw_ty     (os88_image_end + ZWIN_SLACK + 2)   ; word: text origin y
%define zw_sbr    (os88_image_end + ZWIN_SLACK + 4)   ; word: bar's right edge
%define zw_sbb    (os88_image_end + ZWIN_SLACK + 6)   ; word: bar's bottom
%define zw_bpp    (os88_image_end + ZWIN_SLACK + 8)   ; byte: 4 = colour, 1 = not
%define zw_vidok  (os88_image_end + ZWIN_SLACK + 9)   ; byte: asked OSAPI_VIDEO
%define zw_geom   (os88_image_end + ZWIN_SLACK + 10)  ; byte: geometry is good
%define zw_nomore (os88_image_end + ZWIN_SLACK + 11)  ; byte: suppress [MORE]
%define zw_capon  (os88_image_end + ZWIN_SLACK + 12)  ; byte: capture output
%define zw_capn   (os88_image_end + ZWIN_SLACK + 13)  ; byte: bytes captured
%define zw_sbmax  (os88_image_end + ZWIN_SLACK + 14)  ; word: ring capacity
%define zw_bktop  (os88_image_end + ZWIN_SLACK + 16)  ; word: the bar's banked
%define zw_bkcnt  (os88_image_end + ZWIN_SLACK + 18)  ; word: pair (SPEC.md 27.2)
%define zw_lstyle (os88_image_end + ZWIN_SLACK + 20)  ; byte: live style, banked
%define zw_rbuf   (os88_image_end + ZWIN_SLACK + 22)  ; 112: one row, for FONT_RUN
%define zw_stat   (os88_image_end + ZWIN_SLACK + 134) ; 96: the status line
%define zw_uphold (os88_image_end + ZWIN_SLACK + 230) ; word: upper rows still
                                                      ; displayed; >= zw_upper

; =============================================================================
; Geometry
; =============================================================================

; -----------------------------------------------------------------------------
; zw_bounds - derive every coordinate from the LIVE window record
; in:  nothing ([zf_win] is the window)
; out: CF=1 = there is nothing drawable (hidden, or too small); preserves all
;
; Derived every time rather than cached at create time, because the window is
; sizable and re-wrapping to the drag is the whole reason Frotz is a window
; (frotz.asm zf_onsize). It is two kernel calls, ~93 us, and it runs once per
; line of output - against ~10 ms for the line itself.
;
; The text origin is ZF_MARGIN in from the content edge and then rounded UP to
; a multiple of 8. That is not cosmetic: OSAPI_FONT_RUN's one-store-per-cell
; fast path on a 1bpp adapter needs x & 7 == 0, and OSAPI_GFX_SCROLL REFUSES
; an x-range that is not byte-aligned (SPEC.md 5.5). Frotz does not ask for
; WF_SNAP, so the alignment has to come from here.
; -----------------------------------------------------------------------------
zw_bounds:
    push ax
    push bx
    push cx
    push dx
    mov byte [zw_geom], 0
    mov bx, [zf_win]
    or bx, bx
    jz .no

    ; The adapter is a fact we test once, never a guess about it (SPEC.md
    ; 39.4): DH = bits per pixel, and 1 means every colour rounds to black,
    ; white or a dither, so @set_colour must be ignored there.
    cmp byte [zw_vidok], 0
    jne .havevid
    push si                         ; this proc PRESERVES ALL and does not
    call OSAPI_WM_DISPLAY           ; frame SI - the cell answers the band's
    pop si                          ; top there (SPEC.md 39.16.4). One push per
                                    ; adapter change, so it costs nothing.
                                    ;
                                    ; **THE CARD THIS WINDOW IS ON**, not the
                                    ; primary: zf_onresize drops the latch on a
                                    ; DRAG across the seam as well as on an
                                    ; adapter switch, and only this cell can
                                    ; tell the two apart
    mov [zw_bpp], dh
    mov byte [zw_vidok], 1
    mov bx, [zf_win]                ; ...which answered in AX/BX/CX/DX
.havevid:
    call OSAPI_WM_GEOM              ; CX/DX = content w/h, CF=1 = not visible
    jc .no
    mov [zw_w], cx
    mov [zw_h], dx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [zw_x0], ax
    mov [zw_y0], dx
    mov [zw_ty], dx

    add ax, ZF_MARGIN + 7
    and ax, 0xFFF8                  ; the alignment the two fast paths need
    mov [zw_tx], ax

    mov cx, [zw_x0]                 ; the content's own right edge is the bar's
    add cx, [zw_w]
    dec cx
    mov [zw_sbr], cx
    sub cx, ZF_SBW                  ; ...and the last pixel the text may use
    sub cx, ax
    jc .no
    mov ax, cx
    mov cl, 3
    shr ax, cl                      ; whole cells that fit
    cmp ax, 4
    jb .no                          ; narrower than this is not worth wrapping
    cmp ax, ZW_MAXCOL
    jbe .colok
    mov ax, ZW_MAXCOL
.colok:
    cmp ax, [zw_cols]
    je .colsame
    mov byte [zw_dirty], 1          ; the wrap width moved: everything on
.colsame:                           ; screen was laid out for the old one
    mov [zw_cols], ax

    mov ax, [zw_h]
    xor dx, dx
    mov bx, ZF_LEAD
    div bx                          ; AX = rows of leading that fit
    cmp ax, [zw_rows]
    je .rowsame
    mov byte [zw_dirty], 1
.rowsame:
    mov [zw_rows], ax

    mov ax, [zw_y0]                 ; the bar stops short of the grow box, or
    add ax, [zw_h]                  ; its down arrow comes out a filled square
    dec ax
    sub ax, ZW_GROW
    mov [zw_sbb], ax

    ; The upper window may take every row but one: the lower window is where
    ; the story talks to the player and a lower window of zero rows has
    ; nowhere to print.
    call zw_statrows
    mov bx, [zw_rows]
    sub bx, ax
    jbe .no                         ; not even one lower row: refuse
    dec bx
    cmp bx, ZW_UPMAX
    jbe .upcap
    mov bx, ZW_UPMAX
.upcap:
    cmp [zw_upper], bx
    jbe .upok
    mov [zw_upper], bx
.upok:
    cmp [zw_uphold], bx             ; the held rows are clamped by the same
    jbe .holdok                     ; window, or a resize leaves the lower
    mov [zw_uphold], bx             ; one drawing from below the bottom
.holdok:
    mov ax, [zw_upper]              ; ...and the invariant, restated rather
    cmp [zw_uphold], ax             ; than assumed: never fewer held than owned
    jae .holdinv
    mov [zw_uphold], ax
.holdinv:
    mov byte [zw_geom], 1
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_statrows - out AX = 1 when the INTERPRETER owns the top row, else 0
;
; Standard 8.2: in v1-3 the status line is drawn by the interpreter and is not
; part of either window; in v4+ the story writes its own upper window and
; there is no such row. Version 0 means no story is loaded yet.
; -----------------------------------------------------------------------------
zw_statrows:
    push bx
    xor ax, ax
    mov bl, [zf_ver]
    or bl, bl
    jz .out
    cmp bl, 3
    ja .out
    inc ax
.out:
    pop bx
    ret

; zw_lowtop  - out AX = the first screen row the lower window may DRAW in
;
; [zw_uphold] and not [zw_upper]: rows the story has given up but which are
; still on the glass belong to the lower window in the Standard's terms and are
; not available to it here until zw_scroll1 takes them back. Everything that
; lays the lower window out goes through this, which is what keeps the two
; counts from having to be reasoned about anywhere else.
zw_lowtop:
    call zw_statrows
    add ax, [zw_uphold]
    ret

; zw_lowrows - out AX = how many rows it owns; never 0 (zw_bounds guarantees
;              at least one, and the clamp here is the second belt)
zw_lowrows:
    push bx
    call zw_lowtop
    mov bx, ax
    mov ax, [zw_rows]
    sub ax, bx
    ja .out
    mov ax, 1
.out:
    pop bx
    ret

; zw_rowy - in AX = screen row; out DX = the top pixel row of its 8-px cell
zw_rowy:
    push ax
    push bx
    mov bx, ZF_LEAD
    mul bx
    add ax, [zw_ty]
    mov dx, ax
    pop bx
    pop ax
    ret

; zw_colx - in AX = column; out CX = its absolute x
zw_colx:
    push ax
    mov cl, 3
    shl ax, cl
    add ax, [zw_tx]
    mov cx, ax
    pop ax
    ret

; =============================================================================
; Drawing primitives
; =============================================================================

; -----------------------------------------------------------------------------
; zw_rowfill - fill zw_rbuf with spaces to [zw_cols] and NUL-terminate it
;
; The padding IS the erase: OSAPI_FONT_RUN paints a space as pure background,
; so a row buffer built this way rewrites every cell of the row in one call
; and there is no separate fill to disagree with a clip edge.
; -----------------------------------------------------------------------------
zw_rowfill:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, zw_rbuf
    mov cx, [zw_cols]
    mov al, ' '
    cld
    rep stosb
    mov byte [es:di], 0
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_runsel - the ink and background the live style and colour ask for
; out: AL = ink, AH = background; preserves everything else
;
; [zw_fg]/[zw_bg] hold our palette index PLUS ONE, so that zero can mean "the
; story has not set one" - CBLACK is 0 and the all-zero bss has to be a valid
; fresh state (zbss.inc).
; -----------------------------------------------------------------------------
zw_runsel:
    push bx
    mov al, CBLACK
    mov ah, CWHITE
    cmp byte [zw_bpp], 1
    je .style                       ; 1bpp: colour is not a thing we have
    mov bl, [zw_fg]
    or bl, bl
    jz .nofg
    dec bl
    mov al, bl
.nofg:
    mov bl, [zw_bg]
    or bl, bl
    jz .style
    dec bl
    mov ah, bl
.style:
    mov bl, [zw_style]
    test bl, ZST_REVERSE
    jnz .rev
    test bl, ZST_BOLD
    jz .out
    cmp byte [zw_bpp], 1
    jne .out                        ; on VGA bold is the double strike below
.rev:
    xchg al, ah                     ; ...and on 1bpp it is reverse video, the
.out:                               ; only second weight the adapter has
    pop bx
    ret

; -----------------------------------------------------------------------------
; zw_run - draw one run of text in the live style
; in:  CX = x, DX = y (the cell's TOP row - the font has no baseline, SPEC.md
;      6), SI = NUL string in OUR segment; gfx lock held
; out: nothing; preserves every register
;
; One OSAPI_FONT_RUN does the whole run, background and glyphs together.
; Bold on a colour adapter is the same run struck again one pixel to the
; right, through OSAPI_FONT_STR_XPARENT because its background is transparent - a
; second FONT_RUN would re-fill the cells and erase the first strike. Italic
; is an underline in the leading row, which is why ZF_LEAD is 9 and not 8:
; at 8 there is no row to put it in.
; -----------------------------------------------------------------------------
zw_run:
    push ax
    push bx
    push cx
    push dx
    cmp byte [zw_geom], 0
    je .out
    call zw_runsel                  ; AL = ink, AH = background
    call OSAPI_FONT_RUN
    mov bl, [zw_style]
    test bl, ZST_BOLD
    jz .noem
    cmp byte [zw_bpp], 1
    je .noem                        ; already reversed by zw_runsel
    call zw_runsel
    call OSAPI_SET_COLOR            ; AL = ink
    inc cx
    call OSAPI_FONT_STR_XPARENT
    dec cx
.noem:
    test byte [zw_style], ZST_ITALIC
    jz .out
    call OSAPI_FONT_WIDTH           ; AX = 8 * the run's length
    or ax, ax
    jz .out
    mov bx, cx
    add bx, ax
    dec bx                          ; BX = x2
    add dx, CELL_H                  ; the row ZF_LEAD reserves under the cells
    push bx
    call zw_runsel
    call OSAPI_SET_COLOR
    pop bx
    mov ax, cx                      ; AX = x1
    call OSAPI_GFX_HLINE
.out:
    mov al, CBLACK                  ; the pen is one global shared by the whole
    call OSAPI_SET_COLOR            ; machine (apps/os88api.inc)
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_lowrect - the lower window's band, as OSAPI_GFX_SCROLL takes it
; out: AX = x1, BX = y1, CX = x2, DX = y2, inclusive and absolute; SI and DI
;      untouched, which is what lets a caller hold the blit's dy across it
;
; ONE description of the band, where there were two the moment a second thing
; wanted to blit it. x1 is [zw_tx], which zw_bounds has already rounded UP to a
; multiple of 8 for exactly this - SPEC.md 5.5 refuses an x-range that is not
; byte-aligned, and Frotz does not ask for WF_SNAP.
; -----------------------------------------------------------------------------
zw_lowrect:
    call zw_lowtop
    call zw_rowy
    mov bx, dx                      ; BX = y1, the band's top
    call zw_lowrows
    mov cx, ZF_LEAD
    mul cx
    add ax, bx
    dec ax
    mov dx, ax                      ; DX = y2, its last pixel row
    mov ax, [zw_cols]
    mov cl, 3
    shl ax, cl
    add ax, [zw_tx]
    dec ax
    mov cx, ax                      ; CX = x2
    mov ax, [zw_tx]                 ; AX = x1
    ret

; zw_rowdraw - in AX = screen row; draws zw_rbuf across it. ONE primitive call.
zw_rowdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    call zw_rowy
    mov cx, [zw_tx]
    mov si, zw_rbuf
    call zw_run

    ; THE LEADING ROW. OSAPI_FONT_RUN paints the eight rows of the cell and
    ; nothing else, but a text row is ZF_LEAD (9) tall - so the ninth row is
    ; painted by no one and keeps whatever the window had there before. On a
    ; freshly white-filled content that is invisible; anywhere else it is a
    ; rule under every line, which is exactly how it looked.
    ;
    ; One OSAPI_GFX_HLINE, not a fill: it is a single pixel row, and at 756us
    ; a primitive call on the target machine (PERFORMANCE.md) the difference
    ; between one call and one call is the point - this adds ONE to the row's
    ; cost, and the row already costs a FONT_RUN per word.
    push ax
    call zw_runsel                  ; AH = the background the style asks for
    mov al, ah
    call OSAPI_SET_COLOR
    pop ax
    call zw_rowy
    add dx, CELL_H                  ; the row ZF_LEAD reserves under the cells
    mov ax, [zw_cols]
    mov cl, 3
    shl ax, cl
    add ax, [zw_tx]
    dec ax
    mov bx, ax                      ; BX = x2
    mov ax, [zw_tx]                 ; AX = x1
    call OSAPI_GFX_HLINE
    mov al, CBLACK                  ; the pen is one global (apps/os88api.inc)
    call OSAPI_SET_COLOR

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_blank - in AX = screen row; erase it
;
; Standard 8.7.3.1/8.7.3.2 are explicit that a blanked line is NOT reversed
; even when reverse video is the live style, so the style is forced to Roman
; across the call rather than trusted.
; -----------------------------------------------------------------------------
zw_blank:
    push ax
    push bx
    mov bl, [zw_style]
    mov byte [zw_style], ZST_ROMAN
    call zw_rowfill
    call zw_rowdraw
    mov [zw_style], bl
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_copyn - append CX bytes of DS:SI to DS:DI, bounded by the row buffer
; in:  SI = source, DI = destination inside zw_rbuf, CX = count
; out: DI advanced; preserves everything else
; -----------------------------------------------------------------------------
zw_copyn:
    push ax
    push bx
    push cx
    push si
    mov bx, zw_rbuf
    add bx, [zw_cols]               ; one past the last drawable cell
    jcxz .out
.cp:
    cmp di, bx
    jae .out
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .cp
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The scrollback ring
;
; [zw_sbhead] is where the next completed line goes, [zw_sbcount] how many are
; held, and logical line 0 is the OLDEST one still in the ring. Line
; [zw_sbcount] is the one being assembled and is not in the ring at all, which
; is what makes "the live row" and "a stored row" the same case to the painter.
;
; With no claim the ring is absent but the COUNTERS still run, capped at the
; row count, so the text scrolls exactly as it would otherwise. What is lost
; is the ability to put the history back: a repaint of a scrollback-less Frotz
; draws blanks above the live line. That is the graceful half of zw_setup's
; refusal.
; =============================================================================

; zw_ringcap - out AX = the ring's capacity in lines (never 0)
zw_ringcap:
    mov ax, [zw_sbmax]
    or ax, ax
    jnz .out
    mov ax, [zw_rows]
    or ax, ax
    jnz .out
    mov ax, 1
.out:
    ret

; -----------------------------------------------------------------------------
; zw_lineptr - in AX = logical line; out ES:DI = its record, CF=1 = no such
;              record (no claim, or the index is past what is held)
; preserves every register but DI/ES
; -----------------------------------------------------------------------------
zw_lineptr:
    push ax
    push bx
    push cx
    push dx
    mov bx, [zw_sbseg]
    or bx, bx
    jz .no
    cmp ax, [zw_sbcount]
    jae .no
    mov dx, [zw_sbhead]
    sub dx, [zw_sbcount]
    add dx, ax                      ; the ring slot, possibly below zero
    mov ax, [zw_sbmax]
    cmp dx, ax
    jb .have
    add dx, ax                      ; it had wrapped below zero; head < max and
.have:                              ; L < count <= max, so one add is enough
    mov ax, dx
    mov cl, 7
    shl ax, cl                      ; * ZW_LSTRIDE
    add ax, ZW_UPMAX*ZW_LSTRIDE     ; ...past the upper window's grid
    mov di, ax
    mov es, bx
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; zw_headptr - out ES:DI = the record [zw_sbhead] names, CF=1 = no claim
zw_headptr:
    push ax
    push bx
    push cx
    mov bx, [zw_sbseg]
    or bx, bx
    jz .no
    mov ax, [zw_sbhead]
    cmp ax, [zw_sbmax]
    jae .no
    mov cl, 7
    shl ax, cl
    add ax, ZW_UPMAX*ZW_LSTRIDE
    mov di, ax
    mov es, bx
    clc
    jmp short .out
.no:
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

; zw_upptr - in AX = upper-window row (0-based); out ES:DI = its grid record,
;            CF=1 = no claim or past ZW_UPMAX. Preserves every register but
;            DI/ES.
zw_upptr:
    push ax
    push bx
    push cx
    mov bx, [zw_sbseg]
    or bx, bx
    jz .no
    cmp ax, ZW_UPMAX
    jae .no
    mov cl, 7
    shl ax, cl
    mov di, ax
    mov es, bx
    clc
    jmp short .out
.no:
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_commit - the assembled line joins the ring
; in:  zw_lbuf / [zw_lbn]; out: nothing; preserves all
; -----------------------------------------------------------------------------
zw_commit:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    call zw_headptr
    jc .noring
    mov cx, [zw_lbn]
    cmp cx, ZW_LMAX
    jbe .len
    mov cx, ZW_LMAX
.len:
    mov [es:di], cl
    mov al, [zw_style]
    mov [es:di+1], al               ; the style the line was written in, so a
    mov si, zw_lbuf                 ; repaint puts back what was on screen
    add di, ZW_LHDR
    jcxz .noring
.cp:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    loop .cp
.noring:
    call zw_ringcap
    mov bx, [zw_sbhead]
    inc bx
    cmp bx, ax
    jb .head
    xor bx, bx
.head:
    mov [zw_sbhead], bx
    mov bx, [zw_sbcount]
    cmp bx, ax
    jae .out
    inc bx
    mov [zw_sbcount], bx
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; zw_snap - pin the view to the bottom: [zw_sbtop] = max(count + 1 - rows, 0)
zw_snap:
    push ax
    push bx
    call zw_lowrows
    mov bx, [zw_sbcount]
    inc bx                          ; the line being assembled is on screen too
    sub bx, ax
    jnc .set
    xor bx, bx
.set:
    mov [zw_sbtop], bx
    pop bx
    pop ax
    ret

; zw_liverow - out AX = the screen row the assembled line occupies,
;              CF=1 = it is not on screen (the reader has paged back)
zw_liverow:
    push bx
    push cx
    mov ax, [zw_sbcount]
    sub ax, [zw_sbtop]
    jc .no
    mov cx, ax
    call zw_lowrows
    cmp cx, ax
    jae .no
    call zw_lowtop
    add ax, cx
    clc
    jmp short .out
.no:
    stc
.out:
    pop cx
    pop bx
    ret

; =============================================================================
; Output
; =============================================================================

; -----------------------------------------------------------------------------
; zw_scroll1 - move the lower window up one text row
; in:  gfx lock held; out: nothing; preserves all
;
; The whole point of the module, in two primitive calls. The x-range is
; [zw_tx] .. [zw_tx] + 8*cols - 1: both edges are byte-aligned by construction
; (zw_bounds rounded the origin up), which is what OSAPI_GFX_SCROLL requires
; and the reason it does not have to be rounded outward here the way Note Pad
; rounds np_vshift. The strip between the last cell and the scroll bar is
; never written by anything, so leaving it out of the blit costs nothing.
;
; A refusal is a normal path, not an error (SPEC.md 5.5), and the documented
; answer to it is a repaint of the band - which for us is zw_paint.
; -----------------------------------------------------------------------------
zw_scroll1:
    push ax
    push bx
    push cx
    push dx
    push si
    ; THE MOMENT THE HELD ROWS GO BACK. A scroll is the lower window saying it
    ; has run out of room, and that is exactly when a real interpreter's scroll
    ; carries a quote box off the top. Giving the rows back instead of scrolling
    ; is the same outcome by a shorter route: the band grows by what was held,
    ; so the line that needed a new row now has one, and the repaint draws the
    ; lower window over where the box was. Once per box, never per line.
    mov ax, [zw_upper]
    cmp [zw_uphold], ax
    jbe .band
    mov [zw_uphold], ax
    call zw_snap                    ; the view is pinned against a bigger band
    call zw_liverow                 ; ...so the live row moved; zw_break set it
    jc .repaint                     ; from the old one and would be stale
    mov [zw_row], ax
.repaint:
    call zw_paint
    jmp .out
.band:
    call zw_lowrect                 ; AX/BX/CX/DX = the band
    mov si, ZF_LEAD                 ; positive scrolls the content UP
    call OSAPI_GFX_SCROLL
    jc .refuse
    call zw_lowrows                 ; blank the row the blit vacated: it is
    mov bx, ax                      ; ours and its content is unspecified
    call zw_lowtop
    add ax, bx
    dec ax
    call zw_blank
    jmp short .out
.refuse:
    call zw_paint
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_break - end the line being assembled and open the next one
; in:  gfx lock held; out: nothing; preserves all
;
; The scroll is conditional on the band already being full. Until it is, the
; new live row is simply the next one down, and that row has been blank since
; the last repaint or the last blit vacated it - so there is nothing at all to
; draw, which is the cheapest case a text window has.
; -----------------------------------------------------------------------------
zw_break:
    push ax
    push bx
    call zw_liverow
    jc .noold
    mov bx, ax
    jmp short .have
.noold:
    mov bx, 0xFFFF
.have:
    call zw_commit
    mov word [zw_lbn], 0
    call zw_snap
    call zw_liverow
    jc .done
    mov [zw_row], ax
    cmp bx, 0xFFFF
    je .done
    cmp ax, bx
    ja .done                        ; the band was not full: nothing moved
    call zw_scroll1
.done:
    mov word [zw_col], 0
    inc word [zw_more]
    call zw_morechk
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_morechk / zw_morewait - the [MORE] prompt (Standard 8.4.1)
;
; [zw_more] counts rows emitted since the last input; at one short of the
; window height the reader is about to lose a line they have not read, so the
; prompt goes up and the VM stops until a key arrives.
;
; THIS IS THE ONE PLACE THIS MODULE LETS GO OF THE CALLER'S GFX LOCK. The
; wait is unbounded and it runs on the worker, and a worker that holds the
; lock across a yield freezes the clock, the mouse and every other window
; (SPEC.md 20.6). Dropping it means another window may repaint over the
; prompt while we wait; the cost of that is one stale prompt, against a
; deadlocked machine.
; -----------------------------------------------------------------------------
zw_morechk:
%ifdef ZHARNESS
    ret                             ; THE HARNESS DOES NOT PAGE. [MORE] is a
                                    ; screen affordance - it waits on a key so
                                    ; a person can read a full window - and the
                                    ; harness's "person" is a script that only
                                    ; types when the story asks for a command.
                                    ; Left in, the run deadlocks somewhere in
                                    ; the sixth room, which reads exactly like
                                    ; the story hanging. dfrotz -p does not
                                    ; page either, so leaving it out is also
                                    ; what makes the two transcripts comparable.
%endif
    cmp byte [zw_nomore], 0
    jne .out
    push ax
    call zw_lowrows
    dec ax
    jz .pop                         ; a one-row window can never page
    cmp [zw_more], ax
    jb .pop
    call zw_morewait
.pop:
    pop ax
.out:
    ret

zw_morewait:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, [zw_style]
    mov byte [zw_style], ZST_REVERSE ; so it cannot be read as the story's text
    call zw_liverow
    jc .nodraw
    call zw_rowy
    xor ax, ax
    call zw_colx
    mov si, zw_s_more
    call zw_run
.nodraw:
    mov [zw_style], bl
    call OSAPI_GFX_UNLOCK
.wait:
    call zi_getkey                  ; CF=1 = the ring is empty
    jnc .got
    call zi_yield                   ; OSAPI_TASK_ALIVE + a yield
    jmp short .wait
.got:
    call OSAPI_GFX_LOCK
    call zw_bounds                  ; the window may have been dragged
    jc .done
    call zw_liverow
    jc .done
    call zw_blank
.done:
    mov word [zw_more], 0
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_wflush - the pending word joins the line, wrapping under it if it must
; in:  gfx lock held; out: nothing; preserves all
;
; This is the whole of word wrap. The decision is taken when the word ENDS,
; not character by character, which is why the word is held in zw_wbuf and not
; drawn as it arrives: a word that turns out not to fit has to appear on the
; next row and there is no un-drawing it. It also makes a word one
; OSAPI_FONT_RUN instead of one per letter.
;
; zw_putc caps a word at [zw_cols] characters, so after a break it always
; fits and this cannot loop.
; -----------------------------------------------------------------------------
zw_wflush:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov cx, [zw_wbn]
    jcxz .out
    mov ax, [zw_lbn]
    add ax, cx
    cmp ax, [zw_cols]
    jbe .fits
    cmp word [zw_lbn], 0
    je .fits                        ; already at the left margin: breaking
    call zw_break                   ; would ask the same question forever
    mov cx, [zw_wbn]
.fits:
    mov di, zw_wbuf
    add di, cx
    mov byte [di], 0                ; terminate it where it stands
    call zw_liverow
    jc .store                       ; paged back: the model still advances
    call zw_rowy                    ; DX = y
    mov ax, [zw_lbn]
    call zw_colx                    ; CX = x
    mov si, zw_wbuf
    call zw_run                     ; ONE call for the whole word
.store:
    mov si, zw_wbuf
    mov di, zw_lbuf
    add di, [zw_lbn]
    mov cx, [zw_wbn]
    jcxz .done
.cp:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .cp
.done:
    mov ax, [zw_wbn]
    add [zw_lbn], ax
    mov word [zw_wbn], 0
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
; zw_sync - pick up a resize before printing into the window
; in:  gfx lock held; out: nothing; preserves all
;
; Called once per line rather than once per character. A drag changes the
; wrap width, and everything already on screen was laid out for the old one -
; so zw_bounds raises [zw_dirty] and this pays for it with the one repaint
; that is genuinely owed.
; -----------------------------------------------------------------------------
zw_sync:
    push ax
    call zw_bounds
    jc .out
    cmp byte [zw_dirty], 0
    je .out
    call zw_snap
    call zw_paint
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_newline
; in:  gfx lock held; out: nothing; preserves all
;
; The upper window never scrolls (Standard 8.7.3.1): a newline there is just
; the cursor moving down, and off the bottom it simply stops mattering.
; -----------------------------------------------------------------------------
zw_newline:
    push ax
    cmp byte [zw_geom], 0
    jne .have
    call zw_bounds
    jc .out
.have:
    cmp byte [zw_cur], 0
    jne .upper
    call zw_sync
    call zw_wflush
    call zw_break
    jmp short .out
.upper:
    inc word [zw_urow]
    mov word [zw_ucol], 1
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_ensuresnap - any output pins the view back to the bottom
;
; Text arriving off screen is text lost, and the reader who paged up did not
; ask to miss the answer to their command.
; -----------------------------------------------------------------------------
zw_ensuresnap:
    push ax
    mov ax, [zw_sbtop]
    push ax
    call zw_snap
    pop ax
    cmp ax, [zw_sbtop]
    je .out
    call zw_paint
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_capture - AL into the status-line capture buffer
;
; zw_status needs an object's short name as CHARACTERS, and the only thing
; that can decode a Z-string is zt_print, which prints. So the capture flag
; turns this module's sink into a buffer for the length of that one call.
; Bounded at ZW_STNAME, which is what Standard 8.2.2.2 asks an interpreter to
; expect.
; -----------------------------------------------------------------------------
zw_capture:
    push bx
    mov bl, [zw_capn]
    cmp bl, ZW_STNAME
    jae .out
    xor bh, bh
    add bx, zw_stat
    mov [bx], al
    inc byte [zw_capn]
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; zw_putc - one ZSCII character to the current window
; in:  AL = the character; gfx lock held
; out: nothing; preserves every register
;
; Characters above 126 are passed through rather than substituted: the font
; draws them blank BUT STILL ADVANCES THE PEN (SPEC.md 6), so the column count
; stays honest, and folding extended ZSCII onto ASCII is ztext.inc's job
; (zt_zscii_out) where the alphabet tables are.
; -----------------------------------------------------------------------------
zw_putc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [zw_capon], 0
    je .live
    call zw_capture
    jmp .out
.live:
    cmp byte [zw_geom], 0
    jne .have
    call zw_bounds
    jc .out
.have:
    cmp al, 13
    je .nl
    cmp byte [zw_cur], 0
    jne .up
    ; --- the lower window ---------------------------------------------------
    call zw_ensuresnap
    cmp al, ' '
    je .space
    jb .out                         ; other control codes occupy no cell
    mov bx, [zw_wbn]
    cmp bx, [zw_cols]
    jb .word
    call zw_wflush                  ; a word as long as a whole row can never
    mov bx, [zw_wbn]                ; be helped by wrapping: break under it
.word:
    cmp bx, ZW_LMAX
    jae .out
    mov di, zw_wbuf
    add di, bx
    mov [di], al
    inc word [zw_wbn]
    jmp short .out
.space:
    call zw_wflush
    mov bx, [zw_lbn]
    cmp bx, [zw_cols]
    jae .out                        ; a space at the margin hangs and is
    mov di, zw_lbuf                 ; dropped: kept, it would indent the next
    add di, bx                      ; row by a space nobody typed
    mov byte [di], ' '
    inc word [zw_lbn]
    jmp short .out                  ; and it needs no ink - the row it lands
                                    ; on was blanked when it was vacated
.nl:
    call zw_newline
    jmp short .out
.up:
    call zw_upputc
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
; zw_typec / zw_rubc - the line editor's echo, which is NOT story output
; in:  AL = the character (zw_typec only); gfx lock held
; out: nothing; preserves every register
;
; zw_putc holds a word back in [zw_wbuf] until a space or a newline arrives,
; because a word is the unit word-wrap has to be able to move to the next row
; and drawing it early would mean erasing it. That is right for prose and
; wrong for a keystroke: it means nothing at all appears on screen until the
; typist reaches a space, so "help" is invisible until Enter and the person at
; the keyboard cannot see what they are typing.
;
; So the echo flushes the word after every character. The line then breaks at
; the margin mid-word rather than wrapping - which is what every terminal
; line editor does, and the alternative is not showing the typing.
;
; The rub-out is the other half. Backspace reaches zw_putc as a control
; character below ' ', which occupies no cell, so it fell out of the bottom of
; the dispatch and did nothing at all: [zi_linen] went down and the glass did
; not change.
; -----------------------------------------------------------------------------
zw_typec:
    call zw_putc
    push ax
    ; The harness deliberately does NOT mirror the echo. The host typed the
    ; line and can put it back in its own log; on the wire it would be text
    ; the reference interpreter never prints, and every comparison would then
    ; begin by trying to subtract it back out again.
    cmp byte [zw_capon], 0
    jne .out                        ; captured, not drawn
    cmp byte [zw_cur], 0
    jne .out                        ; the upper window draws each cell already
    call zw_wflush
.out:
    pop ax
    ret

zw_rubc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [zw_capon], 0
    jne .out
    cmp byte [zw_cur], 0
    jne .out                        ; no rub-out in the upper window
    cmp byte [zw_geom], 0
    jne .have
    call zw_bounds
    jc .out
.have:
    cmp word [zw_wbn], 0            ; unflushed ink is the easy case: it was
    je .online                      ; never drawn, so drop it and stop
    dec word [zw_wbn]
    jmp short .out
.online:
    mov bx, [zw_lbn]
    or bx, bx
    jz .out                         ; at the left margin: the character being
    dec bx                          ; rubbed out is on the row above, which
    mov [zw_lbn], bx                ; this deliberately does not chase
    call zw_liverow
    jc .out                         ; paged back: the model advanced, no ink
    call zw_rowy                    ; DX = y
    mov ax, bx
    call zw_colx                    ; CX = x
    mov byte [zw_rbuf], ' '
    mov byte [zw_rbuf+1], 0
    mov si, zw_rbuf
    call zw_run                     ; one cell, blanked, one call
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
; zw_upputc - one character into the upper window's character grid
; in:  AL = the character; gfx lock held
;
; No wrap and no scroll (Standard 8.7.3.1); a character past the right edge is
; lost, and the cursor is left where it is, which is what the Standard's
; author suggests for the bottom-right case.
; -----------------------------------------------------------------------------
zw_upputc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [zw_rbuf], al               ; the one-character run this cell needs
    mov byte [zw_rbuf+1], 0
    mov ax, [zw_urow]
    or ax, ax
    jz .out
    dec ax
    cmp ax, [zw_upper]
    jae .out
    mov bx, [zw_ucol]
    or bx, bx
    jz .out
    dec bx
    cmp bx, [zw_cols]
    jae .out
    call zw_upptr                   ; the grid, so a repaint can put it back
    jc .nogrid
    mov al, [zw_rbuf]
    mov [es:bx+di+ZW_LHDR], al
    mov al, [zw_style]              ; ...IN THE STYLE IT WAS WRITTEN IN. The
    mov [es:di+1], al               ; row header has always had this byte and
                                    ; nothing filled it, so a repaint drew the
                                    ; upper window roman - and an Inform quote
                                    ; box is `style reverse`, so it came back
                                    ; from an uncover as black text on white
                                    ; with its box gone. One style per row is
                                    ; the same compromise the scrollback makes
                                    ; and is documented with it (SPEC.md
                                    ; 59.11): a status line or a box is written
                                    ; in one style, which is the real case.
    mov al, bl
    inc al
    cmp al, [es:di]
    jbe .nogrid
    mov [es:di], al
.nogrid:
    call zw_statrows
    add ax, [zw_urow]
    dec ax
    call zw_rowy                    ; DX = y
    mov ax, bx
    call zw_colx                    ; CX = x
    mov si, zw_rbuf
    call zw_run                     ; ONE cell, ONE primitive call
    inc word [zw_ucol]
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
; zw_notice - an interpreter message, on a line of its own
; in:  SI = a NUL string in OUR segment; gfx lock held
; out: nothing; preserves all
;
; It runs on the UI task as often as on the worker (frotz.asm raises them from
; callbacks), so [MORE] is suppressed for its duration: there is nobody to
; press the key.
; -----------------------------------------------------------------------------
zw_notice:
    push ax
    push bx
    push si
%ifdef ZHARNESS
    call zh_nl                      ; interpreter messages are the harness's
    call zh_puts                    ; whole point: a halt has to reach the host
    call zh_nl
%endif
    mov byte [zw_nomore], 1
    mov bl, [zw_style]
    mov byte [zw_style], ZST_ROMAN
    cmp word [zw_lbn], 0
    jne .fresh
    cmp word [zw_wbn], 0
    je .text
.fresh:
    call zw_newline
.text:
    cld
.loop:
    lodsb
    or al, al
    jz .done
    call zw_putc
    jmp short .loop
.done:
    call zw_newline
    mov [zw_style], bl
    mov byte [zw_nomore], 0
    pop si
    pop bx
    pop ax
    ret

; =============================================================================
; Repainting
; =============================================================================

; -----------------------------------------------------------------------------
; zw_paint - repaint the whole window from the scrollback
; in:  SI = anything; gfx lock HELD and the content already white-filled when
;      this comes from W_PAINT
; out: nothing; preserves every register (frotz.asm's zf_paint owes the kernel
;      an unchanged SI)
;
; This is the fall-back, not the normal path: a full repaint is 46 rows of
; glyphs, ~2.9 s on a 4.77 MHz 8088. It runs on an uncover, a resize, a
; scroll-bar action and a refused blit, and on nothing else.
; -----------------------------------------------------------------------------
zw_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call zw_bounds
    jc .out
%ifdef ZHARNESS
    push si                         ; a repaint is the moment an upper window
    mov si, zh_m_paint              ; with no backing grid loses its contents,
    call zh_evbeg                   ; so the host has to know one happened
    call zh_evend
    pop si
%endif
    call zw_status_draw
    call zw_up_draw
    call zw_low_draw
    call zw_sbar
    mov bx, [zf_win]
    call OSAPI_WM_GROW              ; only here: the corner is out of reach of
    mov byte [zw_dirty], 0          ; everything on the per-line path
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
; zw_up_draw - the upper window's rows, from the grid in the claim
; -----------------------------------------------------------------------------
zw_up_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [zw_style]
    mov [zw_lstyle], al
    mov cx, [zw_uphold]             ; HELD rows are drawn too - that is what
    jcxz .done                      ; makes a quote box survive a repaint
    call zw_statrows
    mov dx, ax                      ; DX = screen row
    xor bx, bx                      ; BX = grid row
.row:
    call zw_rowfill
    mov byte [zw_style], ZST_ROMAN  ; the default for a row nothing has written
    mov ax, bx
    call zw_upptr
    jc .draw
    push cx
    mov cl, [es:di]
    xor ch, ch
    jcxz .nostyle                   ; an empty row keeps ROMAN whatever byte
    mov al, [es:di+1]               ; the claim happens to hold: a stale
    mov [zw_style], al              ; REVERSE would paint a bar of background
.nostyle:
    cmp cx, [zw_cols]
    jbe .len
    mov cx, [zw_cols]
.len:
    jcxz .copied
    mov si, di
    add si, ZW_LHDR
    push di
    mov di, zw_rbuf
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    pop di
.copied:
    pop cx
.draw:
    mov ax, dx
    call zw_rowdraw
    inc bx
    inc dx
    loop .row
.done:
    mov al, [zw_lstyle]
    mov [zw_style], al
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_low_draw - the lower window's rows: stored lines, then the live one,
;               then blanks
;
; Note the resize caveat this design pays for: the ring holds lines ALREADY
; WRAPPED, so a window made narrower shows old lines truncated at the new
; width rather than re-wrapped. Note Pad avoids that by storing the raw
; stream and re-wrapping (apps/notepad/notepad.asm), which a scrollback cannot
; do without keeping the pre-wrap text as well. New text wraps to the new
; width immediately; only the history is stale.
; -----------------------------------------------------------------------------
zw_low_draw:
    push ax
    push bx
    push cx
    push dx
    call zw_lowrows
    mov cx, ax
    call zw_lowtop
    mov dx, ax                      ; DX = the first screen row
    mov bx, [zw_sbtop]              ; BX = the first logical line
    call zw_low_rows
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_low_rows - draw a RANGE of the lower window's rows (SPEC.md 61.15)
; in:  DX = the first screen row, BX = the first logical line, CX = how many
;      (0 draws nothing); gfx lock held
; out: nothing; preserves all
;
; zw_low_draw's loop, with its three starting values as arguments instead of
; derived. That is the whole of what a scroll needed: a blit moves the rows
; that are still good and this letters the |d| that are not, where a scroll
; used to reach zw_paint and letter FORTY-SIX (SPEC.md 61.15).
; -----------------------------------------------------------------------------
zw_low_rows:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [zw_style]
    mov [zw_lstyle], al
    jcxz .done
.row:
    call zw_rowfill
    mov byte [zw_style], ZST_ROMAN
    mov ax, bx
    cmp ax, [zw_sbcount]
    ja .draw                        ; below the live line: a blank row
    jb .stored
    call zw_liveinto
    jmp short .draw
.stored:
    call zw_storedinto
.draw:
    mov ax, dx
    call zw_rowdraw
    inc bx
    inc dx
    loop .row
.done:
    mov al, [zw_lstyle]
    mov [zw_style], al
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; zw_storedinto - in AX = logical line; fill zw_rbuf and adopt its style
zw_storedinto:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    call zw_lineptr
    jc .out
    mov al, [es:di+1]
    mov [zw_style], al
    mov cl, [es:di]
    xor ch, ch
    cmp cx, [zw_cols]
    jbe .len
    mov cx, [zw_cols]
.len:
    jcxz .out
    mov si, di
    add si, ZW_LHDR
    mov di, zw_rbuf
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; zw_liveinto - fill zw_rbuf from the line being assembled plus the pending
;               word, in the live style
zw_liveinto:
    push ax
    push cx
    push si
    push di
    mov al, [zw_lstyle]
    mov [zw_style], al
    mov di, zw_rbuf
    mov si, zw_lbuf
    mov cx, [zw_lbn]
    call zw_copyn
    mov si, zw_wbuf
    mov cx, [zw_wbn]
    call zw_copyn
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_clear - erase the content rect with the CALLER's pen
; in:  gfx lock held; out: nothing; preserves all
;
; The odd one out: this is a plain fill, because frotz.asm's About handler
; wants the window blanked before it draws a card over it, not a grid of
; spaces. Nothing on the text path calls it.
; -----------------------------------------------------------------------------
zw_clear:
    push ax
    push bx
    push cx
    push dx
    cmp byte [zw_geom], 0
    jne .have
    call zw_bounds
    jc .out
.have:
    mov ax, [zw_x0]
    mov bx, [zw_y0]
    mov cx, ax
    add cx, [zw_w]
    dec cx
    mov dx, bx
    add dx, [zw_h]
    dec dx
    call OSAPI_GFX_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The status line (Standard 8.2) - v1-3 only, and the interpreter's own work
; =============================================================================

; -----------------------------------------------------------------------------
; zw_status
; in:  gfx lock held; out: nothing; preserves all
;
; Left: the short name of the object in global 0 (8.2.2), captured through the
; ordinary output path because zt_print is the only thing that can decode a
; Z-string. Right: score and moves, or the time, according to bit 1 of Flags 1
; (8.2.1). Drawn in reverse video, in ONE OSAPI_FONT_RUN.
;
; 8.2.2.1 asks interpreters to protect themselves against a bad object number
; here, and this does: object 0 is left blank rather than followed.
; -----------------------------------------------------------------------------
zw_status:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [zw_geom], 0
    jne .have
    call zw_bounds
    jc .out
.have:
    call zw_statrows
    or ax, ax
    jz .out                         ; v4+: the story writes its own

    mov byte [zw_capn], 0
    mov byte [zw_capon], 1
    mov ax, [zf_gvar]
    call zm_gw16                    ; global 0 = the current object
    or ax, ax
    jz .noname
    call zo_name                    ; DX:AX = its short-name Z-string
    call zt_print                   ; ...into zw_stat, through zw_capture
.noname:
    mov byte [zw_capon], 0

    ; --- the right-hand field, composed at zw_stat+ZW_STRIGHT ---------------
    mov di, zw_stat + ZW_STRIGHT
    test byte [zf_flags1], 2        ; Flags 1 bit 1 set = a time game (8.2.1)
    jnz .time
    mov si, zw_s_score
    call zw_puts_di
    mov ax, [zf_gvar]
    add ax, 2
    call zm_gw16                    ; global 1 = the score (-99..999)
    call zw_sdec
    mov si, zw_s_moves
    call zw_puts_di
    mov ax, [zf_gvar]
    add ax, 4
    call zm_gw16                    ; global 2 = the turn count
    call zw_dec
    jmp short .fielded
.time:
    mov si, zw_s_time
    call zw_puts_di
    mov ax, [zf_gvar]
    add ax, 2
    call zm_gw16                    ; global 1 = hours, 0..23
    push ax
    cmp ax, 12
    jb .am
    sub ax, 12
.am:
    or ax, ax
    jnz .hour
    mov ax, 12                      ; 8.2.3.2: reduced modulo 12, so 0 is 12
.hour:
    call zw_dec
    mov al, ':'
    mov [di], al
    inc di
    mov ax, [zf_gvar]
    add ax, 4
    call zm_gw16                    ; global 2 = minutes, 0..59
    call zw_dec2
    pop ax
    mov si, zw_s_am
    cmp ax, 12
    jb .ampm
    mov si, zw_s_pm
.ampm:
    call zw_puts_di
.fielded:
    mov byte [di], 0
    sub di, zw_stat + ZW_STRIGHT
    mov cx, di                      ; CX = the field's length

    ; --- lay the row out ----------------------------------------------------
    call zw_rowfill
    mov bx, [zw_cols]
    sub bx, cx
    jc .noright
    cmp bx, 4
    jb .noright                     ; no room for it: the name wins (8.2.3)
    mov si, zw_stat + ZW_STRIGHT
    mov di, zw_rbuf
    add di, bx
    call zw_copyn
    sub bx, 2                       ; two spaces between name and field
    jmp short .name
.noright:
    mov bx, [zw_cols]
.name:
    mov cl, [zw_capn]
    xor ch, ch
    cmp cx, bx
    jbe .fits
    mov cx, bx
    sub cx, 3
    jbe .drawn                      ; nothing would be left of it
    mov si, zw_stat
    mov di, zw_rbuf
    call zw_copyn
    mov si, zw_s_ell                ; 8.2.2.2: break it and mark the break
    mov cx, 3
    call zw_copyn
    jmp short .drawn
.fits:
    jcxz .drawn
    mov si, zw_stat
    mov di, zw_rbuf
    call zw_copyn
.drawn:
    mov bl, [zw_style]
    mov byte [zw_style], ZST_REVERSE
    xor ax, ax
    call zw_rowdraw
    mov [zw_style], bl
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; zw_status_draw - the repaint's view of the status line: v1-3 only, and it
;                  must not disturb the capture flag a caller may be inside
zw_status_draw:
    cmp byte [zw_capon], 0
    jne .out
    call zw_status
.out:
    ret

; zw_puts_di - copy the NUL string at DS:SI to DS:DI, advancing DI
zw_puts_di:
    push ax
    push si
    cld
.loop:
    lodsb
    or al, al
    jz .out
    mov [di], al
    inc di
    jmp short .loop
.out:
    pop si
    pop ax
    ret

; zw_dec - in AX = unsigned value, DI = destination; out DI advanced
zw_dec:
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.emit:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .emit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; zw_dec2 - the same, always two digits (minutes)
zw_dec2:
    push ax
    push bx
    push dx
    xor dx, dx
    mov bx, 10
    div bx
    add al, '0'
    mov [di], al
    inc di
    mov al, dl
    add al, '0'
    mov [di], al
    inc di
    pop dx
    pop bx
    pop ax
    ret

; zw_sdec - signed, for the score (8.2.3.1 allows -99)
zw_sdec:
    or ax, ax
    jns zw_dec
    push ax
    mov byte [di], '-'
    inc di
    neg ax
    call zw_dec
    pop ax
    ret

; =============================================================================
; The window model the opcodes drive
; =============================================================================

; -----------------------------------------------------------------------------
; zw_split - in AX = rows the upper window should own (Standard 8.6.1.1/8.7.2.1)
; in:  gfx lock held; out: nothing; preserves all
;
; The size test before the repaint is what makes this affordable: a v5 status
; bar splits to the same height every single turn, and re-laying the lower
; window out for a split that did not move would be a full repaint per turn -
; a regression against PERFORMANCE.md Part 5 dressed up as correctness.
; -----------------------------------------------------------------------------
zw_split:
    push ax
    push bx
    cmp byte [zw_geom], 0
    jne .have
    call zw_bounds
    jc .out
.have:
    cmp ax, ZW_UPMAX
    jbe .cap
    mov ax, ZW_UPMAX
.cap:
%ifdef ZHARNESS
    push si                         ; the size AFTER the cap, which is the one
    mov si, zh_m_split              ; that decides what the reader sees
    call zh_ev1
    pop si
%endif
    mov bx, ax
    cmp byte [zf_ver], 4
    jae .nov3
    call zw_upclear                 ; 8.6.1.1.2: a v3 split clears the upper
.nov3:                              ; window, and this is the only place it does
    cmp bx, [zw_upper]
    je .cursor
    mov [zw_upper], bx
    cmp byte [zf_ver], 4
    jb .moved                       ; v3 CLEARED the window a few lines above
                                    ; (8.6.1.1.2), so there is nothing left to
                                    ; hold and the bands really did move -
                                    ; holding rows it had just blanked would
                                    ; leave a blank strip above the story
    cmp bx, [zw_uphold]
    jbe .shrunk                     ; A SHRINK. The rows the story gave up keep
                                    ; what is in them, on the glass and in the
                                    ; grid, so the drawn bands have not moved
                                    ; and there is NOTHING TO REPAINT - which
                                    ; is both the correct screen and one fewer
                                    ; full repaint than this used to cost.
.moved:
    mov [zw_uphold], bx             ; a grow past what is displayed: the lower
    call zw_bounds                  ; window really is a different band now
    call zw_snap
    call zw_paint
    jmp short .cursor
.shrunk:
    call zw_bounds                  ; re-clamp only; nothing has moved
.cursor:
    ; 8.7.2.1.1 / 8.7.2.2: a cursor the new size swallowed goes back to (1,1)
    mov ax, [zw_urow]
    cmp ax, [zw_upper]
    jbe .out
    mov word [zw_urow], 1
    mov word [zw_ucol], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_setwin - in AL = 0 lower / 1 upper (Standard 8.6.1, 8.7.2)
; Selecting the upper window resets its cursor to the top left; the Standard
; says so for every version that has one.
; -----------------------------------------------------------------------------
zw_setwin:
    push ax
    cmp al, 1
    je .up
    mov byte [zw_cur], 0
    jmp short .out
.up:
    mov byte [zw_cur], 1
    mov word [zw_urow], 1
    mov word [zw_ucol], 1
.out:
%ifdef ZHARNESS
    push ax
    push si
    mov al, [zw_cur]
    xor ah, ah
    mov si, zh_m_win
    call zh_ev1
    pop si
    pop ax
%endif
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_cursor - in AX = row, BX = column, both 1-based (Standard 8.7.2.3)
; It addresses the UPPER window and has no effect when the lower is selected.
; Out-of-range is illegal by the Standard, so it is clamped rather than obeyed.
; -----------------------------------------------------------------------------
zw_cursor:
    push ax
    push bx
    cmp byte [zw_cur], 0
    je .out
    or ax, ax
    jz .out
    or bx, bx
    jz .out
    cmp ax, [zw_upper]
    jbe .rowok
    mov ax, [zw_upper]
    or ax, ax
    jz .out
.rowok:
    cmp bx, [zw_cols]
    jbe .colok
    mov bx, [zw_cols]
.colok:
    mov [zw_urow], ax
    mov [zw_ucol], bx
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_getcur - where the cursor is, for @get_cursor (v4+, Standard 15)
; out: AX = row, BX = column, both 1-BASED; preserves every other register
;
; It answers for the SELECTED window, which is the pair zw_cursor writes when
; the upper one is selected and the pair the wrap engine maintains when the
; lower one is. The lower window's [zw_col]/[zw_row] are 0-based internally -
; they index a cell grid - so both are converted here rather than at the eight
; places that maintain them.
; -----------------------------------------------------------------------------
zw_getcur:
    cmp byte [zw_cur], 0
    je .lower
    mov ax, [zw_urow]               ; the upper window's pair is already 1-based
    mov bx, [zw_ucol]
    or ax, ax
    jnz .haverow
    inc ax
.haverow:
    or bx, bx
    jnz .out
    inc bx
    ret
.lower:
    mov ax, [zw_row]
    inc ax
    mov bx, [zw_col]
    inc bx
.out:
    ret

; -----------------------------------------------------------------------------
; zw_upclear - empty the upper window's grid and blank its rows
; -----------------------------------------------------------------------------
zw_upclear:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
%ifdef ZHARNESS
    push si                         ; how many rows are about to go blank; a
    mov si, zh_m_upclr              ; box that vanishes leaves this behind it
    mov ax, [zw_upper]
    call zh_ev1
    pop si
%endif
    mov cx, [zw_uphold]             ; every row that is DISPLAYED, not only the
    jcxz .out                       ; ones the story still owns: a clear that
    call zw_statrows                ; left a held box on the glass would leave
    mov dx, ax                      ; it there for ever, with nothing able to
    xor bx, bx                      ; address it
.row:
    mov ax, bx
    call zw_upptr
    jc .blank
    mov word [es:di], 0             ; length AND style: a cleared row that kept
                                    ; a REVERSE byte would come back from the
                                    ; next repaint as a bar of black
.blank:
    mov ax, dx
    call zw_blank
    inc bx
    inc dx
    loop .row
.out:
    mov ax, [zw_upper]              ; nothing is held any more; the lower
    cmp [zw_uphold], ax             ; window gets those rows back - and if it
    je .noheld                      ; really did, its band just grew and the
    mov [zw_uphold], ax             ; rows it gained are blank on screen while
    call zw_snap                    ; the scrollback has content for them.
    call zw_paint                   ; @erase_window 1 over a held box is the
.noheld:                            ; path that gets here; the others are
    mov word [zw_urow], 1           ; followed by a repaint of their own and
    mov word [zw_ucol], 1           ; find nothing to release
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_lowclear - empty the scrollback and blank the lower window
; -----------------------------------------------------------------------------
zw_lowclear:
    push ax
    push bx
    push cx
    mov ax, [zw_upper]              ; FIRST: the held rows are the lower
    mov [zw_uphold], ax             ; window's, and a story clearing it means
                                    ; the whole band, box included
    mov word [zw_sbhead], 0
    mov word [zw_sbcount], 0
    mov word [zw_sbtop], 0
    mov word [zw_lbn], 0
    mov word [zw_wbn], 0
    mov word [zw_more], 0
    mov word [zw_col], 0
    call zw_lowrows
    mov cx, ax
    jcxz .out
    call zw_lowtop
    mov bx, ax
.row:
    mov ax, bx
    call zw_blank
    inc bx
    loop .row
    call zw_lowtop
    mov [zw_row], ax
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_erasewin - in AX = window number (Standard 8.7.3.2/8.7.3.3)
; in:  gfx lock held; out: nothing; preserves all
;
; -1 clears the screen, collapses the upper window to nothing and selects the
; lower one; -2 clears both and leaves the split alone.
; -----------------------------------------------------------------------------
zw_erasewin:
    push ax
%ifdef ZHARNESS
    push si                         ; unsigned: -1 and -2 arrive as 65535/65534
    mov si, zh_m_erase
    call zh_ev1
    pop si
%endif
    cmp byte [zw_geom], 0
    jne .have
    call zw_bounds
    jc .out
.have:
    cmp ax, 0xFFFF
    je .all
    cmp ax, 0xFFFE
    je .both
    or ax, ax
    jz .lower
    cmp ax, 1
    je .upper
    jmp short .out
.all:
    call zw_upclear
    mov word [zw_upper], 0
    mov byte [zw_cur], 0
    call zw_lowclear
    call zw_paint                   ; the bands moved; put the frame back
    jmp short .out
.both:
    call zw_upclear
    call zw_lowclear
    jmp short .out
.upper:
    call zw_upclear
    jmp short .out
.lower:
    call zw_lowclear
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_eraseline - from the cursor to the right-hand edge (Standard 8.7.3.4)
; in:  gfx lock held; out: nothing; preserves all
; -----------------------------------------------------------------------------
zw_eraseline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [zw_geom], 0
    je .out
    call zw_rowfill                 ; a full row of spaces; the tail of it is
    cmp byte [zw_cur], 0            ; the part we will actually draw
    je .lower
    mov ax, [zw_ucol]
    or ax, ax
    jz .out
    dec ax
    push ax
    mov ax, [zw_urow]
    or ax, ax
    jz .popout
    dec ax
    call zw_upptr
    jc .noshrink
    mov al, [es:di]                 ; the record ends at the cursor now
    pop bx
    push bx
    cmp al, bl
    jbe .noshrink
    mov [es:di], bl
.noshrink:
    pop ax
    push ax
    call zw_statrows
    add ax, [zw_urow]
    dec ax
    call zw_rowy
    pop ax
    jmp short .paint
.popout:
    pop ax
    jmp short .out
.lower:
    mov ax, [zw_lbn]
    mov word [zw_wbn], 0
    call zw_liverow
    jc .out
    call zw_rowy
    mov ax, [zw_lbn]
.paint:
    ; AX = the first column to erase, DX = the row's y
    cmp ax, [zw_cols]
    jae .out
    mov bx, ax
    call zw_colx
    mov si, zw_rbuf
    add si, bx                      ; a shorter run of the same spaces
    mov bl, [zw_style]
    mov byte [zw_style], ZST_ROMAN  ; 8.7.3.4: never reversed
    call zw_run
    mov [zw_style], bl
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
; zw_setstyle - in AL = the style bits (Standard 8.7.1)
;
; Named zw_setstyle and not zw_style because zbss.inc spends that identifier
; on the live style byte. Roman (0) turns everything off; anything else is
; OR-ed in, because the Standard lets a story ask for combinations and 8.7.1.3
; expects an interpreter to report honestly which it took.
; -----------------------------------------------------------------------------
zw_setstyle:
    push ax
    or al, al
    jz .roman
    or [zw_style], al
    jmp short .out
.roman:
    mov byte [zw_style], ZST_ROMAN
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_colour - in AL = foreground, AH = background (Standard 8.3)
; in:  nothing else; out: nothing; preserves all
;
; Ignored outright on a 1bpp adapter. There every colour rounds to black,
; white or a 50% dither (SPEC.md 39.4), so honouring a story that colours by
; meaning would turn readable prose into a checkerboard - a fact we can test
; rather than a guess about the machine (PERFORMANCE.md rule 7).
; -----------------------------------------------------------------------------
zw_colour:
    push ax
    push bx
    cmp byte [zw_bpp], 1
    je .out
    mov bl, al
    cmp bl, 1
    jne .fg
    mov byte [zw_fg], 0             ; 1 = back to the interpreter default
    jmp short .bgin
.fg:
    call zw_zcol
    or bl, bl
    jz .bgin
    mov [zw_fg], bl
.bgin:
    mov bl, ah
    cmp bl, 1
    jne .bg
    mov byte [zw_bg], 0
    jmp short .out
.bg:
    call zw_zcol
    or bl, bl
    jz .out
    mov [zw_bg], bl
.out:
    pop bx
    pop ax
    ret

; zw_zcol - in BL = a Z-machine colour; out BL = our palette index PLUS ONE,
;           or 0 for "leave it alone" (0 = current, and anything unknown).
;           Clobbers BH.
zw_zcol:
    push ax
    push si
    xor bh, bh
    cmp bl, 2
    jb .none
    cmp bl, 9
    ja .none
    mov si, bx
    add si, zw_ctab - 2
    mov bl, [si]
    inc bl
    jmp short .out
.none:
    xor bl, bl
.out:
    pop si
    pop ax
    ret

; zw_buflen - out AX = how many characters are on the line so far
zw_buflen:
    mov ax, [zw_lbn]
    add ax, [zw_wbn]
    mov [zw_col], ax
    ret

; =============================================================================
; The scroll bar (SPEC.md 22's geometry, so it looks like the Disk window's)
;
; It is REAL: the ring is a real extent, so the thumb's size and position mean
; something. It is reserved always - whether a bar is needed depends on the
; line count, which depends on the wrap width, which depends on the bar.
; =============================================================================

; zw_sbtotal - out AX = lines the bar is measuring (the ring plus the live one)
zw_sbtotal:
    mov ax, [zw_sbcount]
    inc ax
    ret

; -----------------------------------------------------------------------------
; zw_sbset - fill zw_sb with this window's bar (SPEC.md 13.10)
; out: BX = the block; every other register preserved
;
; **THIS BAR WAS PIXEL-IDENTICAL TO THE SHARED ELEMENT** and its own header
; said why - "SPEC.md 22's geometry, so it looks like the Disk window's". It
; was the last of the seven private copies (SPEC.md 13.10.6.5). Checked rule by
; rule before anything moved: arrow-cell rules at ty+10 and sbb-10, the track
; between ty+11 and sbb-11, the arrow glyphs on x1+6 (the element COMPUTES
; x1 + (x2-x1)/2, which is 6 on a 14px bar), an 8px minimum thumb and an 8px
; minimum track, view*track/total for the height and pos*track/total for the
; top, and the same clamp keeping its bottom inside the track.
;
; The three numbers are this app's own idea of a view: the ring plus the live
; line is the total, the low window's rows are the fit, and [zw_sbtop] is the
; position (SPEC.md 61).
; -----------------------------------------------------------------------------
zw_sbset:
    push ax
    mov ax, [zw_sbr]
    sub ax, ZF_SBW-1
    mov [zw_sb+0], ax
    mov ax, [zw_ty]
    mov [zw_sb+2], ax
    mov ax, [zw_sbr]
    mov [zw_sb+4], ax
    mov ax, [zw_sbb]
    mov [zw_sb+6], ax
    call zw_sbtotal
    mov [zw_sb+8], ax
    call zw_lowrows
    mov [zw_sb+10], ax
    mov ax, [zw_sbtop]
    mov [zw_sb+12], ax
    mov bx, zw_sb
%ifdef OS88UI_SBDRAG
    call os88ui_sbfix           ; ...and while a thumb drag is live, word 6 is
%endif                          ; the HAND's line (SPEC.md 13.10.5.6)
    pop ax
    ret

zw_sb:      dw 0,0,0,0,0,0,0
%ifdef OS88UI_SBDRAG
zw_nodrag:  db 0                ; 0xFF = this kernel has no tracking edge
%endif                          ; (SPEC.md 13.8.2/13.10.7.1)

; -----------------------------------------------------------------------------
; zw_sbar - draw the bar
; in:  gfx lock held, zw_bounds run; out: nothing; preserves all
; -----------------------------------------------------------------------------
zw_sbar:
    push ax
    push bx
    push cx
    push dx
    mov bx, [zw_ty]
    mov dx, [zw_sbb]
    cmp bx, dx
    jge .done                       ; DEGENERATE: a bar whose top is at or
                                    ; below its bottom draws NOTHING here, not
                                    ; even a frame - the private painter's own
                                    ; guard, kept because the shared element
                                    ; has no opinion about an inverted rect and
                                    ; would hand one to gfx_frame
    call zw_sbset
    call os88ui_sbar
.done:
    mov ax, [zw_sbtop]              ; banked, and NOT yet read by anything: the
    mov [zw_bktop], ax              ; "redraw only if it changed" wrapper these
    mov ax, [zw_sbcount]            ; were written for does not exist, because
    mov [zw_bkcnt], ax              ; zw_sbar's one caller is the whole-window
                                    ; repaint and skipping the bar inside one
                                    ; would leave the rect it just erased
                                    ; (SPEC.md 13.10.6.5)
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; zw_sbhit - is (CX, DX) inside the bar? out CF=0 = yes; preserves everything
zw_sbhit:
    push ax
    mov ax, [zw_sbr]
    sub ax, ZF_SBW-1
    cmp cx, ax
    jb .no
    cmp cx, [zw_sbr]
    ja .no
    cmp dx, [zw_ty]
    jb .no
    cmp dx, [zw_sbb]
    ja .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; zw_click - a press in the content (W_ONCLICK)
; in:  CX = x, DX = y, ABSOLUTE screen coords; gfx lock held
; out: nothing; preserves all
;
; The arrow cells step, the track pages against the thumb, and the thumb
; itself is inert - there is no drag, here or in the Disk window.
; -----------------------------------------------------------------------------
zw_click:
    push ax
    push bx
    push cx
    push dx
    call zw_bounds
    jc .out
    call zw_sbhit
    jc .out
    call zw_sbset                   ; the SHARED element's parts (SPEC.md
    call os88ui_sbhit               ; 13.10) - BX = the block zw_sbset just
                                    ; filled, and CX/DX are still the point,
                                    ; absolute, as it takes them
    cmp al, OS88UI_SBUP
    je .lineup
    cmp al, OS88UI_SBDOWN
    je .linedn
    cmp al, OS88UI_SBPGUP
    je .pageup
    cmp al, OS88UI_SBPGDN
    je .pagedn
%ifdef OS88UI_SBDRAG
    cmp al, OS88UI_SBTHUMB          ; SPEC.md 13.10.5: the thumb is DRAGGED
    jne .out                        ; now. BX is still the block and DX still
    cmp byte [zw_nodrag], 0         ; the press, absolute
    jne .out
    mov al, ZF_SBRATE
    call os88ui_sbgrab
%endif
    jmp short .out                  ; the thumb itself, or an inert track
.lineup:
    mov ax, [zw_sbtop]
    sub ax, ZW_SB_STEP
    jmp short .set
.linedn:
    mov ax, [zw_sbtop]
    add ax, ZW_SB_STEP
    jmp short .set
.pageup:
    mov ax, -1
    call zw_scrollpage
    jmp short .out
.pagedn:
    mov ax, 1
    call zw_scrollpage
    jmp short .out
.set:
    call zw_scrollto
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%ifdef OS88UI_SBDRAG
; -----------------------------------------------------------------------------
; zw_sbtrack / zw_sbcommit / zw_sbdrop - the thumb gesture, from zf_ondrag,
;   zf_onup and zf_onclick's stale net (SPEC.md 13.10.5)
; in:  DX = the pointer's y (ABSOLUTE) for zw_sbtrack; gfx lock held
; out: nothing; preserves all registers
;
; zw_bounds first, because the block's rect and both of its counts come from
; the live window and this app re-wraps to whatever width the user drags to.
; zw_scrollto does the clamp and the paint, so the tier 61.15 built is what a
; dragged thumb rides on: a blit and |d| rows, not a whole window.
; -----------------------------------------------------------------------------
zw_sbtrack:
    push ax
    push bx
    push cx
    push dx
    call os88ui_sbdragging
    jc .out
    call zw_bounds
    jc .out
    call zw_sbset               ; BX = the block; DX is still the pointer
    call os88ui_sbtrack         ; CF = 1: nothing owed - the rate, or the same
    jc .out                     ; line
    call zw_scrollto
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

zw_sbcommit:
    push ax
    push bx
    push cx
    push dx
    call os88ui_sbdragging
    jc .out
    call zw_bounds
    jc .out
    call zw_sbset
    call os88ui_sbdrop          ; the release COMMITS, unconditionally: a
    jc .out                     ; position the rate refused is never lost
    call zw_scrollto
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ...and the same without the commit, for a press that found one still live
zw_sbdrop:
    push ax
    push bx
    push cx
    push dx
    call zw_bounds
    jc .out
    call zw_sbset
    call os88ui_sbdrop
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; zw_scrollpage - in AX = a SIGNED number of pages (PgUp is -1)
; in:  gfx lock held; out: nothing; preserves all
; -----------------------------------------------------------------------------
zw_scrollpage:
    push ax
    push bx
    push cx
    push dx
    cmp byte [zw_geom], 0
    jne .have
    call zw_bounds
    jc .out
.have:
    mov bx, ax
    call zw_lowrows
    mov cx, ax
    mov ax, bx
    imul cx                         ; DX:AX = pages * rows, signed
    add ax, [zw_sbtop]
    call zw_scrollto
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_scrollto - in AX = the line wanted at the top of the lower window (signed)
; Clamps, and repaints only if it actually moved.
; -----------------------------------------------------------------------------
zw_scrollto:
    push ax
    push bx
    push cx
    or ax, ax
    jns .pos
    xor ax, ax
.pos:
    mov cx, ax
    call zw_sbtotal
    mov bx, ax
    call zw_lowrows
    sub bx, ax
    jnc .havemax
    xor bx, bx
.havemax:
    mov ax, cx
    cmp ax, bx
    jbe .set
    mov ax, bx
.set:
    cmp ax, [zw_sbtop]
    je .out
    mov bx, ax
    sub bx, [zw_sbtop]              ; BX = the SIGNED line delta...
    mov [zw_sbtop], ax
    mov ax, bx
    call zw_sbpaint                 ; ...and this is what it costs now
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw_sbpaint - the scrollback moved: the LOW WINDOW and the THUMB (SPEC.md
;              61.15), where this used to be zw_paint
; in:  AX = the signed line delta, [zw_sbtop] already the new one; gfx lock
;      held
; out: nothing; preserves all
;
; **A SCROLL-BAR ACTION USED TO REPAINT THE WHOLE WINDOW**, and zw_paint's own
; header prices that: "a full repaint is 46 rows of glyphs, ~2.9 s on a
; 4.77 MHz 8088. It runs on an uncover, a resize, a SCROLL-BAR ACTION and a
; refused blit." Three of those four are unavoidable. This one was not: a
; scroll changes the lower window and the thumb, and the status line, the
; upper window and the grow box are all exactly where they were.
;
; So the low window MOVES instead of being re-lettered - the same blit the
; per-line path already made for one line (zw_lowrect is now one description
; for both), with the |d| rows the blit could not know lettered after it. An
; arrow step is 3 rows against 46 - arithmetic says ~0.19 s against ~2.9 s, and
; SPEC.md 61.15 says plainly that the figure is PREDICTED rather than measured,
; because three attempts to take it here measured the instrument instead.
;
; A PAGE ALWAYS TAKES THE FALLBACK, by construction: zw_scrollpage moves by
; exactly lowrows lines, so PgUp, PgDn and a track click are each a windowful
; and there is nothing for a blit to move. The arrow cells are what this is
; for - the case a hand holds down.
;
; It falls back to zw_low_draw for those, and for a REFUSED blit (SPEC.md 5.5's byte-column rule, an
; armed clip region, an edge off the screen) - and even that path is cheaper
; than before, because it is still not the status line, the upper window or
; the grow box.
; -----------------------------------------------------------------------------
zw_sbpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call zw_bounds                  ; preserves AX, so the delta survives
    jc .out
    mov di, ax                      ; DI = d, signed
    or di, di
    jz .bar                         ; no movement: the thumb, and nothing else
    call zw_lowrows
    mov cx, ax                      ; CX = the rows the low window shows
    mov ax, di                      ; ...against |d|. THE `or` IS NOT
    or ax, ax                       ; DECORATION: `mov` sets no flags, so a
    jns .pos                        ; jns after it tests whatever zw_lowrows
    neg ax                          ; happened to leave
.pos:
    cmp ax, cx
    jae .whole                      ; a windowful or more: every row is new
    mov ax, di                      ; SI = the signed PIXEL delta
    mov cx, ZF_LEAD
    imul cx
    mov si, ax
    call zw_lowrect                 ; AX/BX/CX/DX = the band; SI survives
    call OSAPI_GFX_SCROLL
    jc .whole                       ; refused: letter the lot (SPEC.md 5.5)
    call zw_lowrows
    mov cx, ax
    call zw_lowtop
    mov dx, ax                      ; DX = the first screen row
    mov bx, [zw_sbtop]              ; BX = the first logical line
    or di, di
    js .back
    mov ax, cx                      ; FORWARD: the exposed rows are the LAST d
    sub ax, di
    add dx, ax
    add bx, ax
    mov cx, di
    jmp short .rows
.back:
    mov cx, di                      ; BACK: they are the FIRST |d|, and the
    neg cx                          ; two starting values are already right
.rows:
    call zw_low_rows
    jmp short .bar
.whole:
    call zw_low_draw
.bar:
    call zw_sbtrans
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
; zw_sbtrans - the thumb moved: TRANSLATE it (SPEC.md 13.10.3)
; in:  gfx lock held, zw_bounds run; out: nothing; preserves all
;
; **AND THIS IS WHAT [zw_bktop]/[zw_bkcnt] WERE FOR.** zw_sbar has banked them
; since the bar existed, under a comment saying they are "what is on screen,
; so a redraw that moved neither number draws nothing" - and until this routine
; nothing read either of them, because zw_sbar's only caller was the
; whole-window repaint and skipping the bar inside one would leave the rect it
; had just erased.
;
; A scroll moves neither the total nor the fit, so the thumb's HEIGHT is
; unchanged and only its top moves: three drawing calls against the bar's
; sixteen. The count changing is the case that cannot translate - a new line
; of output resizes the thumb - so that one still draws the bar whole.
; -----------------------------------------------------------------------------
zw_sbtrans:
    push ax
    push bx
    mov ax, [zw_sbcount]
    cmp ax, [zw_bkcnt]
    jne .full                       ; the TOTAL moved: the height moved with it
    mov ax, [zw_sbtop]
    cmp ax, [zw_bktop]
    je .out                         ; neither moved: draw nothing at all
    mov bx, [zw_ty]
    cmp bx, [zw_sbb]
    jge .out                        ; degenerate, zw_sbar's own guard
    call zw_sbset                   ; BX = the block, holding the NEW pos...
    mov ax, [zw_bktop]              ; ...and this is where the thumb is DRAWN
    call os88ui_sbmove
    mov ax, [zw_sbtop]
    mov [zw_bktop], ax
    jmp short .out
.full:
    call zw_sbar
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; zw_setup - geometry, and the scrollback claim
; in:  UI TASK, gfx lock not required; called by zi_load once the story is in
; out: CF=1 = there is no scrollback (everything else still works)
;
; OSAPI_MEM_CLAIM is not on the worker's list of callable slots (SPEC.md 20.6
; rule 7), which is why this is the UI task's job and why the claim is made
; once, before the worker exists, and never resized.
;
; A refusal is a normal path (SPEC.md 50.3 rule 2): Frotz falls back to a
; window with no history rather than declining to run the story. It asks for
; ZW_SBKB, settles for ZW_SBKB2, and takes neither as fatal.
; =============================================================================
zw_setup:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov word [zw_sbseg], 0
    mov word [zw_sbmax], 0
    mov word [zw_sbhead], 0
    mov word [zw_sbcount], 0
    mov word [zw_sbtop], 0
    mov word [zw_lbn], 0
    mov word [zw_wbn], 0
    mov word [zw_upper], 0
    mov word [zw_more], 0
    mov word [zw_col], 0
    mov word [zw_row], 0
    mov word [zw_urow], 1
    mov word [zw_ucol], 1
    mov byte [zw_cur], 0
    mov byte [zw_style], ZST_ROMAN
    mov byte [zw_fg], 0
    mov byte [zw_bg], 0
    mov byte [zw_capon], 0
    mov byte [zw_nomore], 0
    call zw_bounds

    mov ax, ZW_SBKB
    call OSAPI_MEM_CLAIM            ; out CF=0 and DX = the base segment
    jnc .got
    mov ax, ZW_SBKB2
    call OSAPI_MEM_CLAIM
    jc .none
    mov ax, ZW_SBKB2
    jmp short .have
.got:
    mov ax, ZW_SBKB
.have:
    mov [zw_sbkb], ax
    mov [zw_sbseg], dx
    mov es, dx
    push ax
    mov cl, 3
    shl ax, cl                      ; KB * 8 = 128-byte records
    sub ax, ZW_UPMAX                ; ...less the upper window's grid
    mov [zw_sbmax], ax
    pop ax
    mov cx, 512                     ; an all-zero record is an empty line,
    mul cx                          ; which is what a fresh scrollback is
    mov cx, ax
    xor ax, ax
    xor di, di
    cld
    rep stosw
    clc
    jmp short .out
.none:
    stc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Strings
; =============================================================================
zw_s_more:   db '[MORE]', 0
zw_s_score:  db 'Score: ', 0
zw_s_moves:  db '  Moves: ', 0
zw_s_time:   db 'Time: ', 0
zw_s_am:     db ' am', 0
zw_s_pm:     db ' pm', 0
zw_s_ell:    db '...', 0
; Z colours 2..9 (Standard 8.3.1) onto the EGA/VGA indices os88api.inc names
zw_ctab:     db CBLACK, CRED, CGREEN, CYELLOW, CBLUE, CMAGENTA, CCYAN, CWHITE
