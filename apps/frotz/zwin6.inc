; =============================================================================
; os8088 - apps/frotz/zwin6.inc
;
; THE VERSION 6 SCREEN MODEL: eight windows addressed in PIXELS (Standard 8.8).
; It is a SECOND window model, not a variant of the first, which is why it is
; a second file (docs/FROTZ-PLAN.md 5.2) and why the opcode layer picks
; between zwin.inc and this on [zf_ver] rather than either file branching.
;
; THE ONE DIFFERENCE THAT MATTERS. In zwin.inc a cursor is a CELL: a column
; and a row, and a newline advances by ZF_LEAD (9 - eight pixels of glyph and
; one of leading, because a wall of prose set solid is unreadable). Here a
; cursor is a PIXEL POSITION inside its window, and a newline advances by the
; FONT HEIGHT and by nothing else. That is not a stylistic choice: the story
; reads the font height back out of window property 13 and lays out its own
; screen with it - Zork Zero's newline interrupts reshape margins around a
; picture line by line - so an interpreter that advanced by 9 while declaring
; 8 would disagree with the game's own arithmetic and drift a pixel a line.
; So: CELL_H, everywhere, and no leading. ZF_LEAD does not appear in this file.
;
; NOTHING LEGALLY SHIPPABLE EXERCISES THIS. Every v6 game is Activision's
; (FROTZ-PLAN 2), so this code is verified against synthesised stories and not
; against a game anyone will run. That is the reason for the shape of every
; bounds test below: a window number is checked against 0..7 and every
; coordinate against the window it belongs to AND against our content rect,
; and an out-of-range operation is a NO-OP. An interpreter bug here would
; otherwise be a wild write into another application's window.
;
; NO BACKING STORE. The Standard is explicit that v6 windows are "notional
; transparencies" (8.8.3): once printed, text belongs to the SCREEN and not to
; any window, and moving a window does not move what was printed under it.
; Keeping a per-pixel shadow of the content would cost 640x480/2 = 150KB we do
; not have. So a repaint cannot reproduce what the story drew, and zw6_paint
; does the thing the Standard provides for instead (8.8.2): erase, and set bit
; 2 of 'Flags 2' to ask the story to redraw itself.
;
; CONTEXT AND THE GFX LOCK. Everything here except zw6_paint and zw6_setup is
; called from the VM WORKER with the lock free, so it takes the lock itself,
; in short bursts, around one text run or one fill (SPEC.md 20.6 rule 3).
; zw6_paint is called from W_PAINT with the lock ALREADY HELD and must not
; take it - gfx_lock is not reentrant (rule 4) and a second acquire wedges the
; machine. zw6_setup draws nothing.
;
; ES BELONGS TO THE STORY while the VM runs (FROTZ-PLAN 3.1), so every entry
; point here that calls into the kernel banks it.
; =============================================================================

; --- window attributes (Standard 8.8.3.1), the operand @window_style takes ---
ZW6_A_WRAP   equ 1              ; 0: continue text onto the next line
ZW6_A_SCROLL equ 2              ; 1: scroll when the cursor leaves the bottom
ZW6_A_TRANS  equ 4              ; 2: copy this window's text to output stream 2
ZW6_A_BUF    equ 8              ; 3: buffered printing - wrap after the last
                                ;    WORD that fits rather than the last
                                ;    character (Standard 8.8.3.1.2.2)

; --- text styles (Standard 15, @set_text_style) ------------------------------
; The same bits zwin.inc calls ZST_*, under our own prefix because one flat
; namespace means a second `equ` of the same name is a redefinition error, not
; a compatible restatement (SPEC.md 1, label hygiene). The values are the
; Standard's and cannot drift.
ZW6_ST_REV   equ 1
ZW6_ST_BOLD  equ 2
ZW6_ST_ITAL  equ 4
ZW6_ST_FIX   equ 8

ZW6_LMAX     equ 96             ; characters a pending run may hold. 96 cells
                                ; is 768 pixels - wider than any screen this
                                ; kernel drives, so the cap is a backstop and
                                ; not a layout limit

; --- our private bss, out of ZWIN6_SLACK (zbss.inc 10; 128 bytes, 122 used) --
; The eight window records themselves are zbss.inc's zw6_wins/ZW6_* - this is
; only the state the record layout did not anticipate.
%define zw6_sx    (os88_image_end + ZWIN6_SLACK + 0)   ; word: the Z-machine
%define zw6_sy    (os88_image_end + ZWIN6_SLACK + 2)   ;  screen IS our content
%define zw6_sw    (os88_image_end + ZWIN6_SLACK + 4)   ;  rect: absolute origin
%define zw6_sh    (os88_image_end + ZWIN6_SLACK + 6)   ;  and size, in pixels
%define zw6_mwin  (os88_image_end + ZWIN6_SLACK + 8)   ; word: @mouse_window
%define zw6_ready (os88_image_end + ZWIN6_SLACK + 10)  ; byte: setup has run
%define zw6_splt  (os88_image_end + ZWIN6_SLACK + 11)  ; byte: windows 0 and 1
                                                       ;  are tiled (8.8.4.2)
%define zw6_mono  (os88_image_end + ZWIN6_SLACK + 12)  ; byte: 1bpp adapter, so
                                                       ;  colour is meaningless
%define zw6_pap   (os88_image_end + ZWIN6_SLACK + 13)  ; byte: banked paper
%define zw6_lbn   (os88_image_end + ZWIN6_SLACK + 14)  ; word: chars pending
%define zw6_lbx   (os88_image_end + ZWIN6_SLACK + 16)  ; word: where they start,
%define zw6_lby   (os88_image_end + ZWIN6_SLACK + 18)  ;  window-relative px
%define zw6_lbw   (os88_image_end + ZWIN6_SLACK + 20)  ; byte: whose window
%define zw6_lbuf  (os88_image_end + ZWIN6_SLACK + 22)  ; ZW6_LMAX+1 bytes

; =============================================================================
; zw6_setup - build the eight windows from our content rect
; in:  nothing; UI task, at story-load time. Draws nothing.
; out: CF=1 refused (there is no window yet); preserves all registers
;
; Unlike zw_setup this claims no memory. There is no scrollback in v6: a
; window that scrolls does so by moving pixels the story owns, and there is no
; line history to keep because there are no lines - only a screen.
;
; Geometry that cannot be read (the window is not on screen yet) leaves the
; screen size at zero, which makes every cell test below fail and nothing
; draw, until the first zw6_paint re-derives it. That is deliberate: a story
; that starts while the window is hidden must not be refused outright.
; =============================================================================
zw6_setup:
    push ax
    push bx
    push cx
    push dx
    push di
    push es

    cmp word [zf_win], 0
    je .no

    ; A 1bpp adapter reduces every colour to black, white or a dither
    ; (SPEC.md 39.4), so a story that colours by meaning becomes unreadable.
    ; Decide once, here, and honour it in zw6_pen/zw6_paper.
    call OSAPI_VIDEO                ; DH = bits per pixel, 4 or 1
    mov byte [zw6_mono], 0
    cmp dh, 1
    jne .notmono
    mov byte [zw6_mono], 1
.notmono:
    call zw6_geom

    ; Standard 8.8.3.3: all eight windows begin at (1,1); every one of them
    ; has buffering on and nothing else. Windows 0 and 1 are then widened.
    mov bx, zw6_wins
    mov cx, 8
.wloop:
    push cx
    mov di, bx
    mov cx, ZW6_REC
    xor al, al
.zloop:
    mov [di], al
    inc di
    loop .zloop
    pop cx
    mov byte [bx+ZW6_FG], CBLACK
    mov byte [bx+ZW6_BG], CWHITE
    mov byte [bx+ZW6_FONT], 1       ; font 1 is the normal font, and the only
                                    ; one this kernel has (SPEC.md 6)
    mov word [bx+ZW6_ATTR], ZW6_A_BUF
    add bx, ZW6_REC
    loop .wloop

    ; Window 0 is the whole screen and is initially selected. 8.8.3.3 says it
    ; has "attribute 1 off and 2, 3 and 4 on", numbering from one - which
    ; contradicts 8.8.3.1.2.2 in the same section ("window 0 has wrapping on
    ; by default but all other windows have wrapping off"). The second reading
    ; is the one every interpreter takes and the one a running-text window
    ; needs, so window 0 gets all four.
    mov bx, zw6_wins
    mov ax, [zw6_sw]
    mov [bx+ZW6_W], ax
    mov ax, [zw6_sh]
    mov [bx+ZW6_H], ax
    mov word [bx+ZW6_ATTR], ZW6_A_WRAP | ZW6_A_SCROLL | ZW6_A_TRANS | ZW6_A_BUF
    ; the line count is what [MORE] would be paced from (8.8.3.2.6): the
    ; number of text lines the window holds
    mov cl, 3
    shr ax, cl
    mov [bx+ZW6_CNT], ax

    ; Window 1 is as wide as the screen and has zero height.
    add bx, ZW6_REC
    mov ax, [zw6_sw]
    mov [bx+ZW6_W], ax

    mov byte [zw6_cur], 0
    mov byte [zw6_splt], 0
    mov word [zw6_mwin], 1          ; @mouse_window's default (Standard 15)
    mov word [zw6_lbn], 0
    mov byte [zw6_ready], 1
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw6_geom - re-derive the Z-machine screen from the live window record
; in:  nothing
; out: [zw6_sx/sy/sw/sh]; preserves all registers
;
; Called from setup and from every paint, because the window is sizable and
; the Z-machine screen IS our content rect. A hidden window answers zero, and
; zero disables every draw rather than producing a bogus origin.
; -----------------------------------------------------------------------------
zw6_geom:
    push ax
    push bx
    push cx
    push dx
    mov word [zw6_sw], 0
    mov word [zw6_sh], 0
    mov bx, [zf_win]
    or bx, bx
    jz .out
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [zw6_sx], ax
    mov [zw6_sy], dx
    call OSAPI_WM_GEOM              ; CX/DX = content size, CF=1 not visible
    jc .out
    mov [zw6_sw], cx
    mov [zw6_sh], dx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_screen - the screen size the story should find in its header
; in:  nothing
; out: CX = width in pixels, DX = height in pixels; all else preserved
;
; Standard 8.4: a v6 story reads the screen in units from the header. It is
; zmem/zexec that owns the header bytes, so this only answers the question.
; =============================================================================
zw6_screen:
    mov cx, [zw6_sw]
    mov dx, [zw6_sh]
    ret

; =============================================================================
; zw6_paint - repaint, from W_PAINT, with the gfx lock ALREADY HELD
; in:  nothing
; out: nothing; preserves all registers
;
; There is nothing to repaint FROM (see the banner: no backing store), so this
; erases to window 0's background and takes the escape the Standard put there
; for exactly this case - 8.8.2, bit 2 of 'Flags 2': "the game is expected to
; notice, take action and clear the bit". A v6 story that ignores it comes
; back blank, which is the same answer every interpreter without a shadow
; buffer gives, and it is honest rather than stale.
; =============================================================================
zw6_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push es

    cmp byte [zw6_ready], 0
    je .out
    call zw6_geom                   ; the user may have dragged the grow box
    mov word [zw6_lbn], 0           ; a pending run belongs to the old layout

    mov bx, zw6_wins                ; window 0's background is the screen's
    call zw6_paper
    call OSAPI_SET_COLOR
    call zw6_scrrect                ; AX/BX/CX/DX = the whole content rect
    jc .out
    call OSAPI_GFX_FILL
    call zw6_redraw
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw6_scrrect - the whole Z-machine screen as an absolute inclusive rect
; in:  nothing
; out: AX/BX/CX/DX = x1/y1/x2/y2, CF=1 the screen is empty
; -----------------------------------------------------------------------------
zw6_scrrect:
    mov cx, [zw6_sw]
    or cx, cx
    jz .empty
    mov dx, [zw6_sh]
    or dx, dx
    jz .empty
    mov ax, [zw6_sx]
    mov bx, [zw6_sy]
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    clc
    ret
.empty:
    stc
    ret

; -----------------------------------------------------------------------------
; zw6_redraw - ask the story to repaint its own screen
; in:  nothing
; out: nothing; preserves all registers
;
; Standard 8.8.2. 'Flags 2' is the word at $10, so its bits 0..7 - and the
; redraw bit is bit 2 - live in the BYTE at $11. Dynamic memory, so zm_pb16
; is allowed to write it.
; -----------------------------------------------------------------------------
zw6_redraw:
    push ax
    push bx
    push es
    mov ax, 0x11
    call zm_gb16                    ; AL = the low byte of Flags 2
    mov bl, al
    or bl, 4
    mov ax, 0x11
    call zm_pb16
    pop es
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_select - @set_window
; in:  AL = window number, 0..7
; out: nothing; preserves all registers
;
; The Standard's -3 ("the currently selected window") is resolved by the
; OPCODE layer, not here: as a byte it is 0xFD, which this refuses. Every
; window-taking operation below acts on the SELECTED window, so zexec.inc
; selects, acts, and selects back.
; =============================================================================
zw6_select:
    push ax
    cmp al, 8
    jae .out                        ; unsigned, so negatives are caught too
    cmp al, [zw6_cur]
    je .out
    push ax
    call zw6_flush                  ; the pending run belongs to the old window
    pop ax
    mov [zw6_cur], al
.out:
    pop ax
    ret

; =============================================================================
; zw6_sel_get - which window is selected
; in:  nothing
; out: AL = 0..7; all else preserved
;
; So the opcode layer can bank the selection around a windowed operation
; without reaching into this module's bss (zbss.inc's one rule).
; =============================================================================
zw6_sel_get:
    mov al, [zw6_cur]
    ret

; -----------------------------------------------------------------------------
; zw6_recof - a window number's record
; in:  AL = window number
; out: BX = its record address, CF=1 = no such window (BX untouched)
;      preserves every other register
; -----------------------------------------------------------------------------
zw6_recof:
    push ax
    cmp al, 8
    jae .bad
    mov ah, ZW6_REC
    mul ah                          ; AX = window * 24; 8086 has no imul imm
    mov bx, ax
    add bx, zw6_wins
    pop ax
    clc
    ret
.bad:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; zw6_recsel - the selected window's record
; in:  nothing
; out: BX = its record address; preserves every other register. Cannot fail:
;      [zw6_cur] is only ever written from a checked value.
; -----------------------------------------------------------------------------
zw6_recsel:
    push ax
    mov al, [zw6_cur]
    call zw6_recof
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw6_pen / zw6_paper - the ink and paper a window prints with
; in:  BX = a window record
; out: zw6_pen:   AL = ink, AH = paper
;      zw6_paper: AL = paper, and banked in [zw6_pap]
;      preserves every other register
;
; Reverse video is a swap of the two, and it is a PRINTING attribute only:
; the Standard says every erase paints background "even if the current text
; style is Reverse Video" (8.8.5.2/8.8.5.3), which is why the erase path calls
; zw6_paper and never zw6_pen.
;
; On a 1bpp adapter the story's colours are dropped rather than reduced: two
; EGA indices from the same class are indistinguishable there (SPEC.md 39.4),
; so honouring a story that colours by meaning would make it unreadable, not
; merely plainer.
; -----------------------------------------------------------------------------
zw6_pen:
    mov al, [bx+ZW6_FG]
    mov ah, [bx+ZW6_BG]
    cmp byte [zw6_mono], 0
    je .colour
    mov al, CBLACK
    mov ah, CWHITE
.colour:
    test byte [bx+ZW6_STYLE], ZW6_ST_REV
    jz .done
    xchg al, ah
.done:
    ret

zw6_paper:
    mov al, [bx+ZW6_BG]
    cmp byte [zw6_mono], 0
    je .keep
    mov al, CWHITE
.keep:
    mov [zw6_pap], al
    ret

; -----------------------------------------------------------------------------
; zw6_span - clip one axis of a window against the screen
; in:  AX = origin (screen-relative), CX = extent, SI = the screen's size
; out: AX = first coordinate, CX = last (inclusive, still screen-relative)
;      CF=1 nothing of it is on screen; preserves every other register
;
; It is the only place a window rect meets the screen, so an out-of-range
; @move_window or @window_size is answered in one place rather than eight.
; -----------------------------------------------------------------------------
zw6_span:
    cmp ax, si
    jae .empty                      ; the origin is off the right/bottom edge
    or cx, cx
    jz .empty
    add cx, ax
    jc .clamp                       ; a 16-bit wrap is an absurd size, not a
    cmp cx, si                      ; small one
    jbe .ok
.clamp:
    mov cx, si
.ok:
    dec cx
    clc
    ret
.empty:
    stc
    ret

; -----------------------------------------------------------------------------
; zw6_rect - a window's visible area, absolute and inclusive
; in:  BX = a window record
; out: AX/BX/CX/DX = x1/y1/x2/y2 for the gfx calls, CF=1 = nothing shows
;      preserves every other register (BX is consumed - callers bank it)
; -----------------------------------------------------------------------------
zw6_rect:
    push si
    push di
    mov ax, [bx+ZW6_X]
    mov cx, [bx+ZW6_W]
    mov si, [zw6_sw]
    call zw6_span
    jc .empty
    add ax, [zw6_sx]
    add cx, [zw6_sx]
    mov si, ax                      ; bank x1
    mov di, cx                      ; bank x2
    mov ax, [bx+ZW6_Y]
    mov cx, [bx+ZW6_H]
    push si
    mov si, [zw6_sh]
    call zw6_span
    pop si
    jc .empty
    add ax, [zw6_sy]
    add cx, [zw6_sy]
    mov bx, ax                      ; y1
    mov dx, cx                      ; y2
    mov ax, si                      ; x1
    mov cx, di                      ; x2
    pop di
    pop si
    clc
    ret
.empty:
    pop di
    pop si
    stc
    ret

; -----------------------------------------------------------------------------
; zw6_lock / zw6_unlock - a short drawing burst from the worker
; in:  zw6_lock: nothing
; out: zw6_lock: CF=0 the lock is HELD and the clip is armed to the part of
;      our content that shows; CF=1 nothing is drawable and the lock is NOT
;      held. Preserves every register.
;
; SPEC.md 20.6 rule 5: a worker that draws without one of these two answers
; paints over whatever is on top of it, because the gfx primitives take
; absolute screen coordinates. The clip costs nothing extra here because
; OSAPI_FONT_RUN cannot produce SPEC.md 11.3's granularity failure - fill and
; glyph are one decision about one cell.
; -----------------------------------------------------------------------------
zw6_lock:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_GFX_LOCK
    mov bx, [zf_win]
    or bx, bx
    jz .no
    call OSAPI_WM_GEOM              ; CF=1 = the window is not visible
    jc .no
    call OSAPI_WM_CLIP_SET          ; CF=1 = not one pixel of us shows
    jc .no
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    call OSAPI_GFX_UNLOCK
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

zw6_unlock:
    call OSAPI_GFX_UNLOCK
    ret

; =============================================================================
; zw6_flush - paint the run held in zw6_lbuf
; in:  nothing
; out: nothing; preserves all registers
;
; Printing accumulates into one buffer and draws it in ONE OSAPI_FONT_RUN, for
; the reason PERFORMANCE.md gives as the whole of it: a redraw is priced by
; how many primitive calls it makes. A per-character font_char costs 756us of
; fixed call overhead PER GLYPH; a run of twenty costs it once. The run also
; cannot flash, because the cell's background and its glyph are written in the
; same pass (SPEC.md 6.1) - the erase-then-letter pair is the canonical
; double-draw defect and it is invisible in an emulator.
;
; The run is dropped rather than deferred when nothing is drawable. That is
; correct for v6 and only for v6: there is no backing store to defer INTO, and
; an uncover repaints through zw6_paint, which asks the story to redraw.
; =============================================================================
zw6_flush:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov di, [zw6_lbn]
    or di, di
    jz .out
    mov byte [zw6_lbuf+di], 0

    mov al, [zw6_lbw]
    call zw6_recof
    jc .drop

    call zw6_lock
    jc .drop

    push bx
    mov cx, [zw6_sx]
    add cx, [bx+ZW6_X]
    add cx, [zw6_lbx]
    mov dx, [zw6_sy]
    add dx, [bx+ZW6_Y]
    add dx, [zw6_lby]
    call zw6_pen                    ; AL = ink, AH = paper
    mov si, zw6_lbuf
    call OSAPI_FONT_RUN
    pop bx

    ; Bold is a second pass one pixel right (FROTZ-PLAN 5.1). It has to be
    ; OSAPI_FONT_STR and not a second FONT_RUN: the run paints its cells'
    ; background, so a run at x+1 would erase the first one and leave a
    ; one-pixel stub. FONT_STR's background is transparent, which is exactly
    ; the "drawn twice, one pixel apart" this needs.
    test byte [bx+ZW6_STYLE], ZW6_ST_BOLD
    jz .noem
    push bx
    call zw6_pen
    call OSAPI_SET_COLOR            ; AL = ink
    inc cx
    mov si, zw6_lbuf
    call OSAPI_FONT_STR
    dec cx
    pop bx
.noem:
    ; Italic is an underline: there is no second typeface and no slant in an
    ; 8x8 cell that stays legible on a 1bpp adapter.
    test byte [bx+ZW6_STYLE], ZW6_ST_ITAL
    jz .noit
    push bx
    call zw6_pen
    call OSAPI_SET_COLOR
    mov ax, cx                      ; x1
    mov bx, cx
    mov cx, [zw6_lbn]
    mov si, cx
    mov cl, 3
    shl si, cl
    add bx, si
    dec bx                          ; x2 = x1 + 8*n - 1
    add dx, CELL_H - 1
    xchg bx, ax                     ; gfx_hline wants AX=x1, BX=x2, DX=y
    xchg ax, bx
    call OSAPI_GFX_HLINE
    pop bx
.noit:
    call zw6_unlock
.drop:
    mov word [zw6_lbn], 0
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
; zw6_runsame - does the pending run continue at the pen?
; in:  BX = the selected window's record
; out: CF=0 it does, so the character can simply be appended
;      CF=1 it does not; preserves every register
;
; The invariant a run carries: same window, same pixel row, and its start plus
; eight pixels per character lands exactly on the cursor. Anything that breaks
; it - a cursor move, a window change, a skipped off-screen cell - is
; discovered here rather than being tracked by whoever caused it.
; -----------------------------------------------------------------------------
zw6_runsame:
    push ax
    push cx
    cmp word [zw6_lbn], 0
    je .no
    mov al, [zw6_lbw]
    cmp al, [zw6_cur]
    jne .no
    mov ax, [bx+ZW6_CY]
    cmp ax, [zw6_lby]
    jne .no
    mov ax, [zw6_lbn]
    mov cl, 3
    shl ax, cl
    add ax, [zw6_lbx]
    cmp ax, [bx+ZW6_CX]
    jne .no
    pop cx
    pop ax
    clc
    ret
.no:
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; zw6_fitx - does the cell at the pen lie inside the window's margins?
; in:  BX = a window record
; out: CF=1 it does not; preserves every register
;
; This is the WRAP decision and nothing else: margins are the story's own
; layout (Standard 8.8.3.2.1, text is clipped to stay inside them), while
; zw6_vis below is what keeps a stray coordinate off other people's windows.
; -----------------------------------------------------------------------------
zw6_fitx:
    push ax
    push cx
    mov ax, [bx+ZW6_W]
    mov cx, [bx+ZW6_MR]
    cmp ax, cx
    jbe .no                         ; the margins have eaten the window
    sub ax, cx                      ; AX = first column past the right margin
    mov cx, [bx+ZW6_CX]
    cmp cx, [bx+ZW6_ML]
    jb .no
    cmp cx, ax
    jae .no
    add cx, CELL_W                  ; safe: CX < AX, so this cannot wrap
    cmp cx, ax
    ja .no
    pop cx
    pop ax
    clc
    ret
.no:
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; zw6_vis - is the cell at the pen actually paintable?
; in:  BX = a window record
; out: CF=1 no; preserves every register
;
; Two tests, and both are load-bearing. The window's own height, because a
; cursor is allowed to walk off the bottom of a non-scrolling window and the
; text there is simply clipped away (Standard 8.8.3). And our content rect,
; because @move_window may put a window anywhere at all and drawing outside
; the content is a write into another application's screen.
; -----------------------------------------------------------------------------
zw6_vis:
    push ax
    push cx
    mov ax, [bx+ZW6_CY]
    add ax, CELL_H
    jc .no
    cmp ax, [bx+ZW6_H]
    ja .no
    mov ax, [bx+ZW6_X]
    add ax, [bx+ZW6_CX]
    jc .no
    mov cx, [zw6_sw]
    cmp cx, CELL_W
    jb .no
    sub cx, CELL_W
    cmp ax, cx
    ja .no
    mov ax, [bx+ZW6_Y]
    add ax, [bx+ZW6_CY]
    jc .no
    mov cx, [zw6_sh]
    cmp cx, CELL_H
    jb .no
    sub cx, CELL_H
    cmp ax, cx
    ja .no
    pop cx
    pop ax
    clc
    ret
.no:
    pop cx
    pop ax
    stc
    ret

; =============================================================================
; zw6_putc - print one character into the selected window
; in:  AL = the character, already reduced to something the font can draw
; out: nothing; preserves all registers
;
; The cursor is a PIXEL POSITION (see the banner), so the pen advances by
; CELL_W and a newline by CELL_H - the font height the story reads back out of
; window property 13. Nothing here is expressed in cells.
; =============================================================================
zw6_putc:
    push ax
    push bx
    push cx
    push dx
    push di

    cmp byte [zw6_ready], 0
    je .out
    cmp al, 13
    je .nl
    cmp al, 10
    je .nl

    call zw6_recsel
    call zw6_fitx
    jnc .fits
    ; The cell does not fit inside the margins. With wrapping on the line
    ; breaks; with it off, Standard 8.8.3.1.1 is explicit - the cursor moves
    ; to the right margin, stays there, and further text is IGNORED. A status
    ; window relies on that, which is why wrapping is off in windows 1..7.
    test byte [bx+ZW6_ATTR], ZW6_A_WRAP
    jz .clamp
    call zw6_wrap
    call zw6_recsel
    call zw6_fitx
    jc .out                         ; a margin narrower than one cell
    jmp short .fits
.clamp:
    mov cx, [bx+ZW6_W]
    sub cx, [bx+ZW6_MR]
    mov [bx+ZW6_CX], cx
    jmp short .out
.fits:
    call zw6_vis
    jc .adv                         ; off screen: the pen moves, nothing draws
    call zw6_runsame
    jnc .append
    call zw6_flush                  ; a new run starts here
    mov cx, [bx+ZW6_CX]
    mov [zw6_lbx], cx
    mov cx, [bx+ZW6_CY]
    mov [zw6_lby], cx
    mov cl, [zw6_cur]
    mov [zw6_lbw], cl
.append:
    mov di, [zw6_lbn]
    mov [zw6_lbuf+di], al
    inc di
    mov [zw6_lbn], di
    cmp di, ZW6_LMAX
    jb .adv
    call zw6_flush                  ; the buffer is full; the pen keeps its place
.adv:
    add word [bx+ZW6_CX], CELL_W
    jmp short .out
.nl:
    call zw6_newline
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_newline - end the line in the selected window
; in:  nothing
; out: nothing; preserves all registers
; =============================================================================
zw6_newline:
    push ax
    push bx
    push cx

    cmp byte [zw6_ready], 0
    je .out
    call zw6_flush
    call zw6_recsel
    mov ax, [bx+ZW6_ML]
    mov [bx+ZW6_CX], ax             ; 8.8.3.2.1: back to the LEFT MARGIN

    ; The line count is what the Standard would pace [MORE] from (8.8.3.2.6);
    ; a v6 game sets it deliberately to control where the prompt falls. We
    ; maintain it and report it through get_wind_prop, and never prompt - see
    ; the omissions note at the foot of this file.
    mov ax, [bx+ZW6_CNT]
    cmp ax, -999                    ; 8.8.3.2.2.3: never below -999
    jle .nocnt
    dec ax
    mov [bx+ZW6_CNT], ax
.nocnt:
    mov ax, [bx+ZW6_CY]
    add ax, CELL_H                  ; the FONT HEIGHT, not a leaded row
    mov [bx+ZW6_CY], ax

    mov cx, [bx+ZW6_H]
    cmp cx, CELL_H
    jb .out                         ; a window shorter than a line never scrolls
    sub cx, CELL_H                  ; CX = the last row a line can start on
    cmp ax, cx
    jbe .out
    test byte [bx+ZW6_ATTR], ZW6_A_SCROLL
    jz .out                         ; no scrolling: the cursor walks off and
    mov [bx+ZW6_CY], cx             ; the text is clipped (zw6_vis)
    mov ax, CELL_H
    call zw6_scroll
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw6_wrap - break the line, honouring the buffering attribute
; in:  BX = the selected window's record; the cell at the pen does not fit
; out: the pen is on the next line; preserves every register
;
; Standard 8.8.3.1.2.2 is the whole of this routine. Buffered printing wraps
; after the last WORD that fits; unbuffered wraps after the last CHARACTER.
; The pending run is what makes the first affordable without lookahead: the
; trailing word is still in our buffer and has not been drawn, so breaking the
; run at its last space and re-emitting the tail on the next line costs a
; string copy and no re-decoding of the story's text.
; -----------------------------------------------------------------------------
zw6_wrap:
    push ax
    push cx
    push si
    push di

    test byte [bx+ZW6_ATTR], ZW6_A_BUF
    jz .plain
    call zw6_runsame
    jc .plain                       ; nothing pending: there is no word to carry
    mov cx, [zw6_lbn]
    mov di, cx
    dec di
.scan:
    cmp byte [zw6_lbuf+di], ' '
    je .found
    or di, di
    jz .plain                       ; one word filling the whole line: the only
    dec di                          ; break that helps is a character break
    jmp short .scan
.found:
    inc di                          ; DI = the first character of the tail
    cmp di, cx
    jae .plain                      ; the run ENDS in a space: nothing to carry
    mov [zw6_lbn], di               ; the line keeps the space and all before it
    push cx
    push di
    call zw6_newline                ; flushes the shortened run and moves the pen
    pop di
    pop cx
    sub cx, di                      ; CX = characters to carry down
    mov si, di
    add si, zw6_lbuf
    mov di, zw6_lbuf
.copy:
    mov al, [si]                    ; by hand, not movsb: ES is the STORY here
    mov [di], al                    ; and pointing it at ourselves to move 20
    inc si                          ; bytes would cost more than the loop does
    inc di
    dec cx
    jnz .copy

    call zw6_recsel                 ; zw6_newline did not disturb the selection
    mov cx, [zw6_lbn]               ; (reloaded because BX must come back intact)
    mov ax, [bx+ZW6_CX]
    mov [zw6_lbx], ax
    mov ax, [bx+ZW6_CY]
    mov [zw6_lby], ax
    mov al, [zw6_cur]
    mov [zw6_lbw], al
    mov ax, cx
    mov cl, 3
    shl ax, cl
    add [bx+ZW6_CX], ax             ; the pen sits after the carried word
    jmp short .out
.plain:
    call zw6_newline
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; zw6_move - @move_window: absolute pixel position
; in:  AX = y, BX = x, both 1-based (Standard 8.8.1: (1,1) is the top left)
; out: nothing; preserves all registers
;
; "Nothing actually happens (since windows are entirely notional
; transparencies): but any future plotting happens in the new place."
; So this draws nothing, and the position is stored FAITHFULLY rather than
; clamped - get_wind_prop must report back what the story wrote. A window
; moved off the screen simply stops painting, which zw6_vis and zw6_rect
; decide per cell and per rect.
; =============================================================================
zw6_move:
    push ax
    push bx
    push cx
    push dx
    mov cx, ax
    mov dx, bx
    call zw6_flush                  ; pending text belongs to the OLD position
    call zw6_recsel
    or cx, cx
    jz .y0
    dec cx                          ; 1-based on the wire, 0-based in the record
.y0:
    or dx, dx
    jz .x0
    dec dx
.x0:
    mov [bx+ZW6_Y], cx
    mov [bx+ZW6_X], dx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_size - @window_size
; in:  AX = height, BX = width, in pixels
; out: nothing; preserves all registers
;
; "Does not change the current display." Standard 8.8.3.4: if the cursor now
; lies outside the window it goes to the left margin of the TOP line.
; =============================================================================
zw6_size:
    push ax
    push bx
    push cx
    push dx
    mov cx, ax
    mov dx, bx
    call zw6_flush
    call zw6_recsel
    mov [bx+ZW6_H], cx
    mov [bx+ZW6_W], dx
    call zw6_fixcur
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw6_fixcur - put a stranded cursor back inside its window
; in:  BX = a window record
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
zw6_fixcur:
    push ax
    push cx
    mov ax, [bx+ZW6_CY]
    add ax, CELL_H
    jc .home
    cmp ax, [bx+ZW6_H]
    ja .home
    mov ax, [bx+ZW6_W]
    mov cx, [bx+ZW6_MR]
    cmp ax, cx
    jbe .home
    sub ax, cx
    mov cx, [bx+ZW6_CX]
    cmp cx, [bx+ZW6_ML]
    jb .home
    cmp cx, ax
    jb .out
.home:
    mov ax, [bx+ZW6_ML]
    mov [bx+ZW6_CX], ax
    mov word [bx+ZW6_CY], 0
.out:
    pop cx
    pop ax
    ret

; =============================================================================
; zw6_margins - @set_margins
; in:  AX = left margin, BX = right margin, in pixels
; out: nothing; preserves all registers
;
; Standard 15: "If the cursor is overtaken and now lies outside the margins
; altogether, move it back to the left margin of the current line." Note
; CURRENT line - unlike @window_size, this does not send it to the top.
; =============================================================================
zw6_margins:
    push ax
    push bx
    push cx
    push dx
    mov cx, ax
    mov dx, bx
    call zw6_flush                  ; the run in flight was laid out to the old
    call zw6_recsel                 ; margins
    mov [bx+ZW6_ML], cx
    mov [bx+ZW6_MR], dx
    ; is the cursor still between them?
    mov ax, [bx+ZW6_W]
    cmp ax, dx
    jbe .home
    sub ax, dx
    mov cx, [bx+ZW6_CX]
    cmp cx, [bx+ZW6_ML]
    jb .home
    cmp cx, ax
    jb .out
.home:
    mov ax, [bx+ZW6_ML]
    mov [bx+ZW6_CX], ax
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_setcur - @set_cursor, v6 form
; in:  AX = y, BX = x, 1-based PIXELS inside the selected window
; out: nothing; preserves all registers
;
; Standard 15: "if the cursor would lie outside the current margin settings,
; it is moved to the left margin of the current line". set_cursor -1 turns the
; cursor off and -2 turns it back on; we draw no cursor of our own (the story
; owns every pixel in v6), so both are accepted and do nothing.
; =============================================================================
zw6_setcur:
    push ax
    push bx
    push cx
    push dx
    cmp ax, 0
    jle .out                        ; -1 / -2 / a nonsensical 0
    mov cx, ax
    mov dx, bx
    call zw6_flush
    call zw6_recsel
    dec cx
    or dx, dx
    jz .x0
    dec dx
.x0:
    mov [bx+ZW6_CY], cx
    mov [bx+ZW6_CX], dx
    call zw6_fixcur
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_getcur - @get_cursor
; in:  nothing
; out: AX = y, BX = x, 1-based, inside the selected window; CX/DX preserved
;
; Standard 8.8.3.2.7: text held unprinted in a buffer is flushed FIRST, so the
; position the story reads is the one it would see on screen. Ours is exact
; without the flush - the cursor advances as characters are appended, not as
; they are drawn - but the story is entitled to the screen matching, and a
; game that reads the cursor is about to draw against it.
; =============================================================================
zw6_getcur:
    push cx
    push dx
    call zw6_flush
    call zw6_recsel
    mov ax, [bx+ZW6_CY]
    inc ax
    mov cx, [bx+ZW6_CX]
    mov bx, cx
    inc bx
    pop dx
    pop cx
    ret

; =============================================================================
; zw6_attr - @window_style
; in:  AX = the attribute bitmap, BX = the operation
; out: nothing; preserves all registers
;
; Standard 15: operation 0 (the default) means "set to these settings", 1
; "set the bits supplied", 2 "clear the ones supplied", 3 "reverse them".
; Bits above the four the Standard defines are dropped rather than stored: a
; story that reads the attributes back must not find a bit we invented.
; =============================================================================
zw6_attr:
    push ax
    push bx
    push cx
    mov cx, bx
    and ax, ZW6_A_WRAP | ZW6_A_SCROLL | ZW6_A_TRANS | ZW6_A_BUF
    call zw6_recsel
    cmp cx, 1
    je .set
    cmp cx, 2
    je .clr
    cmp cx, 3
    je .tog
    mov [bx+ZW6_ATTR], ax
    jmp short .out
.set:
    or [bx+ZW6_ATTR], ax
    jmp short .out
.clr:
    not ax
    and [bx+ZW6_ATTR], ax
    jmp short .out
.tog:
    xor [bx+ZW6_ATTR], ax
.out:
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_trans - does the selected window copy its text to the transcript?
; in:  nothing
; out: CF=1 it does (attribute 2); preserves all registers
;
; ztext.inc owns output stream 2; this only answers the question, so the
; attribute lives in one place.
; =============================================================================
zw6_trans:
    push bx
    call zw6_recsel
    test byte [bx+ZW6_ATTR], ZW6_A_TRANS
    jz .no
    pop bx
    stc
    ret
.no:
    pop bx
    clc
    ret

; =============================================================================
; zw6_scroll - @scroll_window
; in:  AX = signed pixels; positive scrolls the content UP, negative down
; out: nothing; preserves all registers
;
; "This can be done to any window and is not related to the 'scrolling'
; attribute" (Standard 15). The vacated pixels are painted in background.
;
; OSAPI_GFX_SCROLL moves the rect in one call - a 78-cell row is ~71ms to
; letter and under 1ms to move (PERFORMANCE.md) - but it is byte-column
; granular on every adapter and REFUSES unless x1 and x2+1 are both multiples
; of 8. A v6 window can sit anywhere, so that is a real refusal and not a
; theoretical one, and it refuses again whenever anything covers us (a blit
; cannot be cut per pixel, SPEC.md 11.3).
;
; The fallback cannot be "repaint the band", because there is nothing to
; repaint it FROM. It is: erase the window to background and set the redraw
; bit, which is the same answer zw6_paint gives and the one the Standard put
; bit 2 of 'Flags 2' there for.
; =============================================================================
zw6_scroll:
    push ax
    push bx
    push cx
    push dx
    push si
    push es

    or ax, ax
    jz .out
    mov si, ax                      ; SI = dy, the sign convention gfx_scroll
                                    ; already uses: positive moves content UP
    call zw6_flush
    call zw6_recsel
    ; |dy| at or beyond the window's height moves nothing into view
    mov ax, si
    or ax, ax
    jns .pos
    neg ax
.pos:
    cmp ax, [bx+ZW6_H]
    jae .wipe

    call zw6_paper                  ; banked in [zw6_pap] across zw6_rect
    call zw6_rect                   ; AX/BX/CX/DX = the window, absolute
    jc .out

    call zw6_lock
    jc .out

    test ax, 7                      ; x1 on a byte column?
    jnz .fall
    push cx
    inc cx
    test cx, 7                      ; and x2+1?
    pop cx
    jnz .fall
    call OSAPI_GFX_SCROLL           ; CF is the whole answer; nothing else moves
    jc .fall

    ; The |dy| rows it vacated are ours to repaint, and background is all we
    ; have to put in them.
    push ax
    mov al, [zw6_pap]
    call OSAPI_SET_COLOR
    pop ax
    or si, si
    js .down
    mov bx, dx                      ; scrolled up: the band is at the bottom
    sub bx, si
    inc bx
    jmp short .band
.down:
    mov dx, bx                      ; scrolled down: at the top
    sub dx, si                      ; SI is negative, so this ADDS |dy|
    dec dx
.band:
    cmp bx, dx
    ja .done
    call OSAPI_GFX_FILL
.done:
    call zw6_unlock
    jmp short .out
.fall:
    mov al, [zw6_pap]
    call OSAPI_SET_COLOR
    call OSAPI_GFX_FILL             ; the whole window, to background
    call zw6_unlock
    call zw6_redraw
    jmp short .out
.wipe:
    call zw6_erase1
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw6_erase1 - paint one window's visible area in its own background
; in:  BX = a window record
; out: nothing; preserves all registers. Takes the gfx lock itself.
; -----------------------------------------------------------------------------
zw6_erase1:
    push ax
    push bx
    push cx
    push dx
    push es
    call zw6_paper
    call zw6_rect
    jc .out
    call zw6_lock
    jc .out
    push ax
    mov al, [zw6_pap]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    call zw6_unlock
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_erasewin - @erase_window
; in:  AX = window number, signed: 0..7, or -1 / -2
; out: nothing; preserves all registers
;
; Standard 8.8.5.3.1: -1 erases the whole screen to WINDOW 0's background,
; unsplits windows 0 and 1, and selects window 0. 8.8.5.3.2: -2 erases the
; screen to the CURRENT background and changes nothing else - not a window
; attribute, not a cursor. A plain window number erases that window only, and
; leaves its cursor alone, because the Standard says only that the space is
; painted over.
; =============================================================================
zw6_erasewin:
    push ax
    push bx
    push cx
    push dx
    push es

    cmp byte [zw6_ready], 0
    je .out
    call zw6_flush
    cmp ax, -1
    je .all0
    cmp ax, -2
    je .allcur
    cmp ax, 8
    jae .out                        ; unsigned: any other negative lands here
    call zw6_recof
    jc .out
    call zw6_erase1
    jmp short .out

.all0:
    xor ax, ax
    call zw6_split                  ; 8.8.4.2: an unsplit, if one had happened
    mov byte [zw6_cur], 0
    call zw6_recsel
    call zw6_fixcur
    call zw6_wipe
    jmp short .out
.allcur:
    call zw6_recsel
    call zw6_wipe
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zw6_wipe - paint the WHOLE Z-machine screen in a window's background
; in:  BX = the window whose background to use
; out: nothing; preserves all registers. Takes the gfx lock itself.
; -----------------------------------------------------------------------------
zw6_wipe:
    push ax
    push bx
    push cx
    push dx
    push es
    call zw6_paper
    call zw6_scrrect
    jc .out
    call zw6_lock
    jc .out
    push ax
    mov al, [zw6_pap]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    call zw6_unlock
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_eraseline - @erase_line, v6 form
; in:  AX = the value the opcode was given
; out: nothing; preserves all registers
;
; Standard 15: 1 erases from the cursor to the end of its line; anything else
; erases that many pixels MINUS ONE across from the cursor, clipped to stay
; inside the right margin. The cursor does not move, and the space is painted
; in background even under reverse video (8.8.5.2).
; =============================================================================
zw6_eraseline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    cmp ax, 1
    jl .out
    mov si, ax                      ; bank the value across the flush
    call zw6_flush
    call zw6_recsel

    ; how many pixels are there between the pen and the right margin?
    mov ax, [bx+ZW6_W]
    mov cx, [bx+ZW6_MR]
    cmp ax, cx
    jbe .out
    sub ax, cx
    mov cx, [bx+ZW6_CX]
    cmp cx, ax
    jae .out
    sub ax, cx                      ; AX = pixels available, CX = the pen
    cmp si, 1
    je .have
    dec si                          ; "the given number of pixels minus one"
    cmp si, ax
    jae .have
    mov ax, si
.have:
    mov bp, ax                      ; BP = width (a scratch register only - we
    mov di, cx                      ; never address through it; SS != DS)
    mov si, [bx+ZW6_CY]
    call zw6_paper
    call zw6_rect                   ; AX/BX/CX/DX = the window, clipped
    jc .out

    add ax, di                      ; x1 = window left + the pen
    add bx, si                      ; y1 = window top  + the cursor row
    mov di, ax
    add di, bp
    dec di                          ; x2 = x1 + width - 1
    cmp di, cx
    jbe .xok
    mov di, cx
.xok:
    mov cx, di
    mov di, bx
    add di, CELL_H - 1
    cmp di, dx
    jbe .yok
    mov di, dx
.yok:
    mov dx, di
    cmp ax, cx
    ja .out
    cmp bx, dx
    ja .out

    call zw6_lock
    jc .out
    push ax
    mov al, [zw6_pap]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    call zw6_unlock
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_split - @split_window, v6 form
; in:  AX = the height of window 1, in PIXELS (Standard 15: "the line count is
;      in units rather than lines")
; out: nothing; preserves all registers
;
; 8.8.4.1: windows 0 and 1 are TILED to fill the screen - window 1 gets the
; given height at the top left, window 0 what is left below it, "possibly
; making it disappear altogether if window 1 occupies the whole screen".
; Neither window is drawn or cleared: this is a layout change.
; =============================================================================
zw6_split:
    push ax
    push bx
    push cx
    push dx
    call zw6_flush
    mov cx, [zw6_sh]
    cmp ax, cx
    jbe .fits
    mov ax, cx                      ; a split taller than the screen is the
.fits:                              ; whole screen, not a wrap to nothing
    mov dx, [zw6_sw]
    mov bx, zw6_wins + ZW6_REC      ; window 1: the top strip
    mov word [bx+ZW6_X], 0
    mov word [bx+ZW6_Y], 0
    mov [bx+ZW6_W], dx
    mov [bx+ZW6_H], ax
    call zw6_fixcur
    mov bx, zw6_wins                ; window 0: everything below it
    mov word [bx+ZW6_X], 0
    mov [bx+ZW6_Y], ax
    mov [bx+ZW6_W], dx
    sub cx, ax
    mov [bx+ZW6_H], cx
    call zw6_fixcur
    mov byte [zw6_splt], 0
    or ax, ax
    jz .out
    mov byte [zw6_splt], 1
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_style - @set_text_style for the selected window
; in:  AL = the style bits (ZW6_ST_*; 0 = roman, which clears every other)
; out: nothing; preserves all registers
;
; Standard 8.8.3.2.3: the property holds the operand of the instruction, so
; the bits are stored verbatim and interpreted only at draw time (zw6_pen and
; the two extra passes in zw6_flush).
; =============================================================================
zw6_style:
    push ax
    push bx
    call zw6_flush                  ; the run in flight is in the OLD style
    call zw6_recsel
    mov [bx+ZW6_STYLE], al
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_colour - @set_colour for the selected window
; in:  AL = foreground, AH = background (Z-machine colour numbers already
;      mapped to EGA indices by the opcode layer)
; out: nothing; preserves all registers
; =============================================================================
zw6_colour:
    push ax
    push bx
    call zw6_flush
    call zw6_recsel
    mov [bx+ZW6_FG], al
    mov [bx+ZW6_BG], ah
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_font - @set_font for the selected window
; in:  AL = the font wanted (0 = ask without changing)
; out: AX = the previous font id, or 0 if the font is unavailable
;
; Standard 15: font 1 is normal, 3 is character graphics, 4 is fixed-pitch.
; This kernel has exactly one 8x8 typeface and it IS fixed-pitch (SPEC.md 6),
; so 1 and 4 are both available and are the same glyphs; 2 and 3 are not, and
; the Standard's answer for an unavailable font is 0 with nothing changed.
; Answering 0 truthfully is what lets a story fall back instead of drawing a
; character-graphics box-frame out of letters.
; =============================================================================
zw6_font:
    push bx
    push cx
    mov cl, al
    call zw6_recsel
    mov al, [bx+ZW6_FONT]
    xor ah, ah
    or cl, cl
    jz .out                         ; 0 = "what font is this?"
    cmp cl, 1
    je .take
    cmp cl, 4
    je .take
    xor ax, ax                      ; unavailable, and nothing changes
    jmp short .out
.take:
    mov [bx+ZW6_FONT], cl
.out:
    pop cx
    pop bx
    ret

; =============================================================================
; zw6_getprop - @get_wind_prop
; in:  AX = window number, BL = property number
; out: AX = the value (0 for a property this interpreter cannot answer)
;
; The 18 properties of Standard 8.8.3.2, driven off a table rather than a
; ladder because sixteen of them are a field of the record and the two that
; are not are the newline interrupt (see the omissions note).
; =============================================================================
ZW6_P_WORD  equ 0                   ; the word at this offset
ZW6_P_WORD1 equ 1                   ; ...plus one, because the wire is 1-based
ZW6_P_ZERO  equ 2                   ; we cannot answer this one
ZW6_P_BYTE  equ 3                   ; the byte at this offset
ZW6_P_COLR  equ 4                   ; background << 8 | foreground (8.8.3.2.4)
ZW6_P_FSIZE equ 5                   ; height << 8 | width (8.8.3.2.5)

zw6_ptab:
    db ZW6_Y,     ZW6_P_WORD1       ; 0  y coordinate
    db ZW6_X,     ZW6_P_WORD1       ; 1  x coordinate
    db ZW6_H,     ZW6_P_WORD        ; 2  y size
    db ZW6_W,     ZW6_P_WORD        ; 3  x size
    db ZW6_CY,    ZW6_P_WORD1       ; 4  y cursor
    db ZW6_CX,    ZW6_P_WORD1       ; 5  x cursor
    db ZW6_ML,    ZW6_P_WORD        ; 6  left margin size
    db ZW6_MR,    ZW6_P_WORD        ; 7  right margin size
    db 0,         ZW6_P_ZERO        ; 8  newline interrupt routine
    db 0,         ZW6_P_ZERO        ; 9  interrupt countdown
    db ZW6_STYLE, ZW6_P_BYTE        ; 10 text style
    db 0,         ZW6_P_COLR        ; 11 colour data
    db ZW6_FONT,  ZW6_P_BYTE        ; 12 font number
    db 0,         ZW6_P_FSIZE       ; 13 font size
    db ZW6_ATTR,  ZW6_P_WORD        ; 14 attributes
    db ZW6_CNT,   ZW6_P_WORD        ; 15 line count
    db 0,         ZW6_P_ZERO        ; 16 true foreground colour - no true
    db 0,         ZW6_P_ZERO        ; 17 true background colour   colour here
ZW6_PMAX    equ 18

zw6_getprop:
    push bx
    push cx
    push si
    cmp bl, ZW6_PMAX
    jae .zero
    call zw6_recof
    jc .zero
    mov al, bl
    xor ah, ah
    shl ax, 1
    mov si, ax
    add si, zw6_ptab
    mov cl, [si]                    ; CL = the field's offset in the record
    mov ch, [si+1]                  ; CH = how to read it
    xor ah, ah
    mov al, cl
    add bx, ax                      ; BX -> the field
    cmp ch, ZW6_P_ZERO
    je .zero
    cmp ch, ZW6_P_BYTE
    je .byte
    cmp ch, ZW6_P_COLR
    je .colr
    cmp ch, ZW6_P_FSIZE
    je .fsize
    mov ax, [bx]
    cmp ch, ZW6_P_WORD1
    jne .out
    inc ax
    jmp short .out
.byte:
    mov al, [bx]
    xor ah, ah
    jmp short .out
.colr:
    sub bx, ax                      ; back to the record base
    mov al, [bx+ZW6_FG]
    mov ah, [bx+ZW6_BG]
    jmp short .out
.fsize:
    mov ax, (CELL_H << 8) | CELL_W
    jmp short .out
.zero:
    xor ax, ax
.out:
    pop si
    pop cx
    pop bx
    ret

; =============================================================================
; zw6_putprop - @put_wind_prop
; in:  AX = window number, BL = property number, CX = the value
; out: nothing; preserves all registers
;
; Standard 8.8.3.2: "a game should only use put_wind_prop to set the newline
; interrupt routine, the interrupt countdown and the line count". Two of those
; three are the newline interrupt, which this interpreter does not run, so
; writing them would be a promise it cannot keep - they are ignored, and
; get_wind_prop answers 0 for them so a story can see that it was. The line
; count is real and is honoured.
; =============================================================================
zw6_putprop:
    push ax
    push bx
    cmp bl, 15
    jne .out
    call zw6_recof
    jc .out
    mov [bx+ZW6_CNT], cx
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; zw6_mousewin - @mouse_window
; in:  AX = the window to hold the pointer in, -1 = no restriction
; out: nothing; preserves all registers
;
; Stored and reported; nothing is constrained. The Standard's own wording is
; that interpreters are "presumably supposed to hold the arrow there by
; hardware means if possible", and this one cannot: the pointer belongs to the
; desktop, not to our window, and a package that confined it would take the
; mouse away from the menu bar and every other application.
; =============================================================================
zw6_mousewin:
    mov [zw6_mwin], ax
    ret

; =============================================================================
; zw6_mouse - @read_mouse
; in:  nothing
; out: AX = y, BX = x, CX = button bits; DX preserved
;
; Standard 10 Remarks: the coordinates are relative to the top of the display
; at (1,1) whatever the mouse window is, the read is realtime, and a pointer
; outside our screen is reported with NEGATIVE values rather than clamped -
; "programs must be prepared to cope with this". So this subtracts and does
; not clamp.
;
; Button bits: bit 0 is the primary button, bit 1 the secondary, which is
; exactly how the kernel latches them (kernel/mouse.inc: bit 0 left, bit 1
; right), so the byte passes straight through.
; =============================================================================
zw6_mouse:
    push dx
    push es
    call OSAPI_MOUSE                ; CX = x, DX = y, AL = the button latches
    mov bx, cx
    sub bx, [zw6_sx]
    inc bx
    mov cx, dx
    sub cx, [zw6_sy]
    inc cx
    mov ah, al
    xchg ax, cx                     ; AX = y, CX = the buttons in CL
    and cx, 0x0300
    mov cl, ch
    xor ch, ch
    pop es
    pop dx
    ret

; =============================================================================
; What this module deliberately does NOT do, so the next reader does not go
; looking for it (SPEC.md 47: say what is missing, do not imply it works):
;
;  - **[MORE].** The line count is maintained per window and answered through
;    get_wind_prop, but no prompt is ever printed. Pausing needs the input
;    path, and a v6 game sets line counts precisely to place its own pauses;
;    prompting on top of that would double them.
;  - **The newline interrupt (properties 8 and 9).** Not run. It calls a
;    routine from inside the printing path, which means re-entering the
;    interpreter from the middle of a text run - and the Standard's own note
;    (8.8.3.2.2.1) is that Infocom's own files and interpreters disagree about
;    the order. Reported as 0, so a story can see it is absent.
;  - **True colour (properties 16 and 17) and @buffer_screen.** No true colour
;    exists on any adapter this kernel drives; buffer_screen may be ignored by
;    contract (8.8.7) and is, so this acts as if it were always 0.
;  - **Confining the pointer** (@mouse_window) - see zw6_mousewin.
