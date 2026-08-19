; =============================================================================
; os8088 - apps/telnet/telnet.asm
;
; TELNET - docs/NET-STACK-PLAN.md stage C, and the first thing on this machine
; that is USEFUL over a network rather than a demonstration of one.
;
; It is stage C rather than the browser because it exercises the whole socket
; API (SPEC.md 62.11) with NOTHING in the way: connect, poll, send, receive,
; close, and a screen that is characters. A terminal is also the one network
; application whose data rate is a HUMAN'S TYPING SPEED, so the cable's 3,741
; bytes a second (PERFORMANCE.md Set 39) is not a compromise here - it is more
; than the application can use.
;
; --- WHAT IT IS ---------------------------------------------------------------
; A dumb terminal, deliberately. No cursor addressing, no colour, no scroll
; regions: a printing terminal with a scrolling window, which is what makes a
; BBS, a MUD and a Unix login prompt all readable. Escape sequences are
; RECOGNISED AND DISCARDED rather than acted on (te_esc), because the honest
; failure for a sequence this does not implement is nothing appearing - and
; the dishonest one is `[2J` printed as four characters into the middle of a
; sentence.
;
; --- THE WORKER OWNS THE SOCKET ----------------------------------------------
; SPEC.md 20.6's shape, and netpkg.inc's rule: every NETV_* verb is
; non-blocking, so a connection is STARTED by a click and finished by a worker
; that polls. The UI task never waits for the network, which is what keeps the
; desktop live while a host is not answering.
;
; The worker DRAWS, which socktest deliberately does not: this one has a
; screen a person is reading, so it takes the gfx lock for the rows it
; changed and releases it (SPEC.md 20.6 rule 3). What it must never do is hold
; it across a wire exchange - a NETV_RECV is up to 274 ms and the cursor would
; stop dead for it.
;
; --- THE RING IS WHY THERE ARE TWO BUFFERS -----------------------------------
; Bytes arrive on the worker and are consumed by the worker, but the SCREEN is
; redrawn by the UI task on a paint. So the screen is the shared thing and the
; socket buffer is not: te_rx is the worker's alone, te_scr is under the lock.
;
; Every proc here is a near proc with a near `ret`: the kernel reaches a
; callback through the dispatcher in the package's own header (SPEC.md 20.1),
; and a `retf` returns into the loader's stack frame.
; =============================================================================

%include "os88api.inc"
%include "netpkg.inc"               ; ...THE DRIVER'S OWN HEADER, the same file
                                    ; drivers/net/net.asm includes, so the two
                                    ; ends cannot drift (SPEC.md 20.11)

    OS88_HEADER 'TELNET', te_entry, 1
    OS88_ICON16
    ; 16 mask rows (the white underlay), then 16 of ink: a terminal
    ; screen with a `<` prompt on it and a stand under the case.
    dw 0x0000
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x0FF0
    dw 0x3FFC
    dw 0x3FFC
    dw 0x0000
    dw 0x0000
    dw 0x7FFE
    dw 0x4002
    dw 0x5002
    dw 0x4802
    dw 0x5002
    dw 0x4002
    dw 0x4782
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x7FFE
    dw 0x0810
    dw 0x3FFC
    dw 0x0000
    dw 0x0000
    OS88_ICON16_END

TE_W        equ 528                 ; 66 columns of 8px, plus chrome
TE_H        equ 190
TE_COLS     equ 64                  ; ...and the screen is FIXED, not derived:
TE_ROWS     equ 18                  ; a terminal that reflows on a resize is a
                                    ; terminal whose host has the wrong idea of
                                    ; how wide it is, and this one never tells
                                    ; the host anything (no NAWS - see te_opt)
TE_SCR      equ TE_COLS * TE_ROWS
TE_RX       equ 512                 ; one NETV_RECV's landing ground
TE_HOSTMAX  equ 48
TE_TX       equ 64                  ; keystrokes waiting for the worker

TE_BAR      equ 16                  ; the host box's height
TE_PAD      equ 3
TE_TOPY     equ TE_BAR + TE_PAD*2   ; ...and the first text row, from the top
                                    ; of the content box
TE_STATH    equ 10                  ; the status line's band at the FOOT of the
                                    ; content, which the rows may not reach:
                                    ; te_status draws at [te_chh] - 10, and on
                                    ; CGA the fourteenth row ran straight
                                    ; through it. Two painters on one band is
                                    ; a screen whose pixels depend on which
                                    ; drew last - and it is what made the
                                    ; scroll blit move the status line up
%if TE_COLS != 64
  %error "te_markcur shifts by 6 for the row: TE_COLS must be 64"
%endif

; --- the connection's state --------------------------------------------------
TS_IDLE     equ 0
TS_OPEN     equ 1
TS_WAIT     equ 2
TS_UP       equ 3
TS_DOWN     equ 4
TS_ERR      equ 5

; --- Telnet's own three bytes (RFC 854) --------------------------------------
IAC         equ 255
T_SE        equ 240
T_SB        equ 250
T_WILL      equ 251
T_WONT      equ 252
T_DO        equ 253
T_DONT      equ 254

; -----------------------------------------------------------------------------
; te_entry - package entry (SPEC.md 20.2)
; -----------------------------------------------------------------------------
te_entry:
    push si
    mov si, te_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [te_win], bx
    mov word [te_line + LN_BUF], te_hbuf    ; **THE BLOCK'S BUFFER WORDS, and
    mov word [te_line + LN_MAX], TE_HOSTMAX ; they are not optional**: bss
                                            ; arrives ZEROED (SPEC.md 21 step
                                            ; 5), so an unset LN_BUF points at
                                            ; offset 0 - the package's own
                                            ; header - and the first keystroke
                                            ; writes into it
    call te_clear
    mov al, 1
    mov bx, [te_win]                ; ...BX is the window: WM_CREATE left it
    call OSAPI_WM_SNAP              ; there, but te_clear runs in between              ; the screen is 8px cells, so an aligned
                                    ; content origin is what earns font_run's
                                    ; single-store path on the two mono
                                    ; adapters (SPEC.md 11.94)
    push si
    mov si, te_menus
    call OSAPI_MENU_SET
    mov al, 1
    mov bx, [te_win]
    call OSAPI_WM_SIZABLE           ; **RESIZABLE, AND NOTHING REFLOWS.**
                                    ; te_layout derives te_vcols and te_vrows
                                    ; from the LIVE content box on every call,
                                    ; so both axes change how much of the
                                    ; fixed 64x18 screen is on view and
                                    ; neither changes the screen. The window
                                    ; opens wide enough for all of it
    mov si, te_about
    call OSAPI_ABOUT_SET            ; 'About Telnet' under our name in the bar
    pop si                          ; (SPEC.md 12.2). It preserves the flags,
                                    ; which matters here: wm_create's CF above
                                    ; still has to ride out of this proc
    clc
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; te_layout - the host box's rect, from the LIVE content box
; in:  SI = window; sets te_ox/te_oy and the line block's four words
;
; Re-derived on every paint rather than stored, because a window can be
; resized, moved across an extended desktop's seam, or find itself on an
; adapter of a different size (SPEC.md 39.7/11.98) - and a control drawn from
; a remembered rect is a control the hit-test cannot find.
; -----------------------------------------------------------------------------
te_layout:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [te_ox], ax
    mov [te_oy], dx
    mov bx, si
    call OSAPI_WM_GEOM              ; CX = content width, DX = height - and it
    mov [te_cw], cx                 ; takes BX, which WM_CONTENT has spent
    mov [te_chh], dx
    mov cx, [te_ox]
    add cx, TE_PAD
    mov [te_line + LN_X1], cx
    add cx, [te_cw]
    sub cx, TE_PAD*2 + 88           ; ...the Connect button's width beside it
    mov [te_line + LN_X2], cx
    mov cx, [te_oy]
    add cx, TE_PAD
    mov [te_line + LN_Y1], cx
    add cx, TE_BAR - 1
    mov [te_line + LN_Y2], cx
    mov cx, [te_line + LN_X2]
    add cx, 6
    mov [te_btn + 0], cx
    add cx, 78
    mov [te_btn + 4], cx
    mov cx, [te_line + LN_Y1]
    mov [te_btn + 2], cx
    mov cx, [te_line + LN_Y2]
    mov [te_btn + 6], cx

    ; --- THE TEXT PEN, AND IT IS 8-ALIGNED (SPEC.md 6.1) --------------------
    ; font_run's fast path - the one that writes each cell old-to-final in a
    ; SINGLE STORE, so a run is never momentarily blank - needs the pen on a
    ; multiple of 8. It was [te_ox] + TE_PAD, and WF_SNAP makes the content
    ; origin 8-aligned, so every one of the eighteen rows was drawn at skew 3
    ; and fell back to gfx_fill + font_str: the box goes white, then the
    ; glyphs arrive. That is the flicker. os88line_pen is the same fix in the
    ; host box above it, and here it also earns the SCROLL, which refuses a
    ; rect whose x1 is not a byte column.
    mov cx, [te_ox]
    add cx, TE_PAD + 7
    and cx, 0FFF8h
    mov [te_px], cx

    ; --- and how many rows the LIVE content box can show --------------------
    ; TE_ROWS is 18 and the screen is fixed, but the WINDOW is not: wm_fit
    ; clamps a 190-row template to the desktop band, which on CGA is 155. The
    ; rows that did not fit were drawn anyway - the gfx primitives clip to the
    ; SCREEN, not to the window (SPEC.md 39.7) - so the bottom of the terminal
    ; was painted over the dock. The window shows the LAST te_vrows rows,
    ; because a terminal's new output is at the bottom and showing the top of
    ; the buffer on a short screen means never seeing what just arrived.
    mov ax, [te_chh]
    sub ax, TE_TOPY + TE_STATH
    jns .rows
    xor ax, ax
.rows:
    mov cl, 3
    shr ax, cl
    or ax, ax
    jnz .rnz
    inc ax                          ; ...never zero: one row is drawn even on
.rnz:                               ; a window nothing can be read in
    cmp ax, TE_ROWS
    jbe .rset
    mov ax, TE_ROWS
.rset:
    mov [te_vrows], ax
    mov cx, TE_ROWS
    sub cx, ax
    mov [te_vtop], cx

    ; --- and how many COLUMNS it can show -----------------------------------
    ; **THIS IS NOT REFLOW AND MUST NOT BECOME IT.** The screen is 64 columns
    ; wide whatever the window is: the buffer does not change, the host is
    ; still told nothing (no NAWS - see te_opt), and its idea of where a line
    ; wraps stays exactly what it was. What a narrow window changes is how
    ; much of each line you can SEE, which is the same bargain te_vrows makes
    ; vertically. Reflowing instead would be a terminal quietly disagreeing
    ; with the host about its own width, and every wrapped line on screen
    ; would be in the wrong place.
    ;
    ; Without it a narrow window painted its text over whatever was beside
    ; it, because font_run clips to the SCREEN and not to the window
    ; (SPEC.md 39.7) - which is why this had to land with WM_SIZABLE and not
    ; after it.
    mov ax, [te_ox]
    add ax, [te_cw]
    sub ax, [te_px]                 ; ...from the ALIGNED pen to the far edge
    jns .cols
    xor ax, ax
.cols:
    mov cl, 3
    shr ax, cl
    or ax, ax
    jnz .cnz
    inc ax
.cnz:
    cmp ax, TE_COLS
    jbe .cset
    mov ax, TE_COLS
.cset:
    mov [te_vcols], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; WHICH ROWS ARE OWED (SPEC.md 70.4)
;
; A RANGE and not a bitmap, because terminal output is sequential: a burst of
; bytes touches a contiguous run of rows, so one compare a mark and one bound
; at the draw is the whole cost. Empty is dr0 > dr1.
; =============================================================================

; --- te_wpx - AX = the visible text width in PIXELS, from te_vcols ----------
; One routine because two rects are cut to it - the scroll blit's and the
; About panel's fill - and a second copy of `<< 3` is a second opinion about
; how wide the terminal is.
te_wpx:
    push cx
    mov ax, [te_vcols]
    mov cl, 3
    shl ax, cl
    pop cx
    ret

; --- te_mark - BX = a row that must be redrawn ------------------------------
te_mark:
    cmp bx, [te_dr0]
    jae .hi
    mov [te_dr0], bx
.hi:
    cmp bx, [te_dr1]
    jbe .out
    mov [te_dr1], bx
.out:
    ret

; --- te_markcur - the row the cursor is on ----------------------------------
te_markcur:
    push bx
    push cx
    mov bx, [te_cur]
    mov cl, 6                       ; TE_COLS = 64, asserted at the top
    shr bx, cl
    cmp bx, TE_ROWS
    jae .out                        ; the cursor may sit one past the end
    call te_mark
.out:
    pop cx
    pop bx
    ret

; --- te_markall / te_markclr - everything, and nothing ----------------------
te_markall:
    mov word [te_dr0], 0
    mov word [te_dr1], TE_ROWS - 1
    ret

te_markclr:
    mov word [te_dr0], TE_ROWS
    mov word [te_dr1], 0
    ret

; -----------------------------------------------------------------------------
; te_paint - W_PAINT: the host box, the button, the screen
; -----------------------------------------------------------------------------
te_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [te_win], si
    call te_hire
    call te_layout
    push si
    mov si, te_line
    call os88line_draw
    pop si
    call te_button
    call te_screen
    call te_status
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- te_button - Connect, or Close while a session is up ---------------------
te_button:
    push ax
    push bx
    push si
    push di
    mov bx, te_btn
    mov si, te_s_conn
    cmp byte [te_state], TS_UP      ; **THE SAME PREDICATE te_toggle USES**, so
    jne .draw                       ; the label and the action cannot disagree
    mov si, te_s_disc               ; (SPEC.md 47 rule 5). It was `jb`, which
.draw:                              ; called a refused session `Close`
    mov di, OS88UI_FILL
    call os88ui_btn
    pop di
    pop si
    pop bx
    pop ax
    ret

; --- te_status - one line under the screen: what the connection is doing -----
; ONE OPAQUE font_run, space-padded (SPEC.md 6.1): the field changes on every
; state edge and a fill-then-letter pair would blank it each time.
te_status:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bl, [te_state]
    xor bh, bh
    shl bx, 1
    mov si, [te_names + bx]
    cmp byte [te_state], TS_ERR
    jne .have
    cmp word [te_msg], 0
    je .have
    mov si, [te_msg]                ; **THE REASON AND NOT THE STATE.** `Failed`
                                    ; is what every one of these looks like from
                                    ; the outside; `No partner` and `Refused`
                                    ; are different things to go and do
                                    ; something about (SPEC.md 54.4.1's rule,
                                    ; one application down)
.have:
    mov di, te_sline
    mov cx, 16
.copy:
    mov al, [si]
    or al, al
    jnz .ch
    mov al, ' '
    dec si                          ; ...pad to a FIXED width and stay on the
.ch:                                ; NUL, so the run always erases the whole
    mov [di], al                    ; field whatever the last message was
    inc si
    inc di
    loop .copy
    mov byte [di], 0
    mov cx, [te_px]                 ; ...8-aligned, like every run here
    mov dx, [te_oy]
    add dx, [te_chh]
    sub dx, 10
    mov si, te_sline
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- te_screen - every row of the terminal (a repaint owes all of them) -----
te_screen:
    call te_markall
    call te_rows_owed
    ret

; -----------------------------------------------------------------------------
; te_rows_owed - draw the rows in the dirty range, and clear it
; in:  gfx lock held; out: nothing, all registers preserved
;
; **THIS IS WHAT STOPPED TYPING REDRAWING THE WHOLE TERMINAL.** A character
; echoed back by the host reaches te_putc, which used to leave te_show to
; repaint all eighteen rows - 1,152 glyph cells at PERFORMANCE.md's ~900us
; each on the machine this is for - to change one of them.
; -----------------------------------------------------------------------------
te_rows_owed:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [te_dr0]
.row:
    cmp bx, [te_dr1]
    ja .done
    cmp bx, TE_ROWS
    jae .done
    call te_row
    inc bx
    jmp short .row
.done:
    call te_markclr
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; te_scrollpaint - spend [te_scrl] as a BLIT rather than as eighteen runs
; in:  gfx lock held, te_layout run; out: nothing, all registers preserved
;
; The buffer scroll is a rep movsb in te_scrollck and always was; what this
; adds is that the SCREEN follows it by moving the pixels it already has.
; OSAPI_GFX_SCROLL needs the rect on byte columns, which is what the aligned
; pen above bought, and it REFUSES under a clip region that does not wholly
; contain the rect - so every failure falls back to marking the lot, which is
; exactly what happened before this existed.
; -----------------------------------------------------------------------------
te_scrollpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [te_scrl]
    or ax, ax
    jz .out
    mov word [te_scrl], 0
    cmp ax, [te_vrows]
    jae .all                        ; the whole window scrolled away: the blit
                                    ; would move nothing anybody can see
    push ax                         ; ...the ROWS, for the repaint below
    mov cl, 3
    shl ax, cl
    mov si, ax                      ; SI = the pixels to move, positive = up
    mov ax, [te_vrows]
    shl ax, cl
    dec ax                          ; ...the window's last pixel row, from y1
    mov bx, [te_oy]
    add bx, TE_TOPY                 ; BX = y1
    mov dx, bx
    add dx, ax                      ; DX = y2
    call te_wpx                     ; AX = the visible width in pixels - the
    mov cx, ax                      ; routine that owns the `<< 3`, so DX (y2)
    add cx, [te_px]                 ; stays out of the width entirely
    dec cx                          ; CX = x2, so x2+1 is a byte column too
    mov ax, [te_px]                 ; AX = x1, a byte column (te_layout)
    call OSAPI_GFX_SCROLL
    pop ax
    jc .all
    mov bx, TE_ROWS                 ; the rows it vacated are OURS to repaint
    sub bx, ax
.mk:
    call te_mark
    inc bx
    cmp bx, TE_ROWS
    jb .mk
    jmp short .out
.all:
    call te_markall
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- te_row - BX = a row index. One opaque run, NUL-terminated in place ------
te_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp bx, [te_vtop]
    jb .done                        ; above the visible window on a short one
    mov ax, TE_COLS
    mul bx
    mov si, te_scr
    add si, ax
    mov di, si
    add di, [te_vcols]              ; ...the run ends where the WINDOW does
    mov al, [di]                    ; bank the byte the terminator covers -
    push ax                         ; os88line_draw's trick, and for its reason
    mov byte [di], 0
    mov ax, bx                      ; the y FIRST, while CX is still free
    sub ax, [te_vtop]
    mov cl, 3
    shl ax, cl
    mov dx, [te_oy]
    add dx, TE_TOPY
    add dx, ax
    mov cx, [te_px]                 ; ...8-aligned (te_layout)
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop ax
    mov [di], al
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; te_onclick - CX = x, DX = y, SI = window; lock held
; -----------------------------------------------------------------------------
te_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [te_win], si
    call te_layout
    call te_abdismiss               ; ...and by the click that dismisses them
    jc .out
    mov bx, te_btn
    call os88ui_bhit
    jc .field
    call te_toggle
    jmp short .redraw
.field:
    push si
    mov si, te_line
    call os88line_click
    pop si
    jc .out
.redraw:
    push si
    mov si, te_line
    call os88line_draw
    pop si
    call te_button
    call te_status
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; te_onkey - AL = ascii, AH = scan, SI = window; lock held
;
; A KEYSTROKE GOES TO THE FIELD OR TO THE HOST, never to both, and which is
; decided by LN_FOCUS alone. That is what lets Return mean `connect` in the
; box and `send a CR` on the screen without a mode anybody has to remember.
; -----------------------------------------------------------------------------
te_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [te_win], si
    call te_layout
    call te_abdismiss               ; the credits are dismissed by the key
    jc .out                         ; that dismisses them, and that key does
                                    ; nothing else (SPEC.md 12.2's shape)
    push si
    mov si, te_line
    call os88line_key
    pop si
    jnc .drawfield                  ; the field edited: redraw it and stop
    cmp byte [te_line + LN_FOCUS], 0
    je .host
    cmp al, 13
    je .go
    cmp al, 9
    jne .host
.defocus:
    mov byte [te_line + LN_FOCUS], 0    ; Tab leaves the box, which is the
    jmp short .drawfield                ; only way to reach the screen with
.go:                                    ; the keyboard
    mov byte [te_line + LN_FOCUS], 0
    call te_toggle
    jmp short .drawfield
.host:
    cmp al, TET_ESC                 ; ^] is the way IN as well as the way out,
    je .fsx                         ; which is the one key a telnet user
                                    ; already knows (tetxt.inc)
    cmp byte [te_state], TS_UP
    jne .out
    call te_tx                      ; ...QUEUED, not sent: the wire belongs to
    jmp short .out                  ; the worker and a callback that touched it
.fsx:
    push si
    mov si, [te_win]
    call te_fsx
    pop si
    jmp short .out
.drawfield:                         ; would hold the gfx lock across it
    push si
    mov si, te_line
    call os88line_draw
    pop si
    call te_button
    call te_status
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; te_tx - queue AL (and AH's scan code, for the arrows) for the worker
; in:  AL = ascii, AH = scan
;
; A RING WITH A DROP, not a block: the UI task must never wait for the wire.
; At 3,741 bytes a second a 64-byte queue is seventeen milliseconds of typing
; ahead of the cable, and a human who outruns that has an unresponsive machine
; either way - what they must not have is a frozen one.
; -----------------------------------------------------------------------------
te_tx:
    or al, al
    jz .out                         ; a bare scan code: no arrows, no function
                                    ; keys. This terminal sends CHARACTERS
    cmp al, 13
    jne te_txraw
    mov al, 13                      ; CR, and the LF is the host's business:
    jmp short te_txraw              ; RFC 854 says CR LF and every host in
.out:                               ; practice accepts a bare CR
    ret

; -----------------------------------------------------------------------------
; te_txraw - queue AL exactly as given, with no ASCII filter
; in:  AL = the byte
;
; The PROTOCOL replies come through here (te_refuse), because an option byte
; of 0 - TRANSMIT-BINARY, an ordinary thing for a host to offer - is a legal
; option and te_tx's scan-code filter eats it. What went on the wire then was
; a two-byte IAC WONT, which RFC 854 says is incomplete: the host takes the
; user's next keystroke as the option number and the two ends disagree about
; that option for the rest of the session.
;
; **te_txw HAS TWO WRITERS** - te_onkey on the UI task and te_refuse on the
; worker - so the read-modify-write below is a critical section (SPEC.md 1).
; An interleave rolls te_txw BACKWARDS past te_txr, and te_flushtx's wrap arm
; then hands the host 56 bytes of stale ring.
; -----------------------------------------------------------------------------
te_txraw:
    push bx
    push cx
    pushf
    cli
    mov bx, [te_txw]
    mov cx, bx
    inc cx
    and cx, TE_TX-1
    cmp cx, [te_txr]
    je .out                         ; full: DROPPED, and deliberately silent
    mov [te_txb + bx], al
    mov [te_txw], cx
.out:
    popf
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; te_toggle - the Connect / Close button, and Return in the host box
; -----------------------------------------------------------------------------
te_toggle:
    push ax
    mov al, [te_state]
    cmp al, TS_UP
    je .down                        ; **ONLY A LIVE SESSION CLOSES.** This was
                                    ; `jae`, and TS_DOWN and TS_ERR are both
                                    ; ABOVE TS_UP - so a refused connection,
                                    ; or a host that hung up, left the button
                                    ; asking the worker to CLOSE. The worker
                                    ; only reads [te_want] in its TS_UP arm,
                                    ; so the request sat there for ever and
                                    ; every later click set it again: one
                                    ; refusal and the app could never connect
                                    ; to anything again. They are FINISHED
                                    ; states, exactly as BN_DONE and BN_ERR
                                    ; are in the browser (SPEC.md 71.8), and
                                    ; what follows a finished session is a new
                                    ; one
    cmp al, TS_OPEN
    je .out                         ; ...on its way: a second click is not a
    cmp al, TS_WAIT                 ; second connection
    je .out
    mov byte [te_state], TS_OPEN    ; TS_IDLE, TS_DOWN and TS_ERR all connect
    mov byte [te_want], 0           ; ...and a close asked for on a session
    mov word [te_msg], 0            ; that has already ended is not owed
    call te_clear
    jmp short .out
.down:
    mov byte [te_want], 1           ; ...ASKED, not done: closing is a wire
.out:                               ; command and the worker owns the wire
    pop ax
    ret

; --- te_clear - blank the screen and home the cursor -------------------------
te_clear:
    push ax
    push cx
    push di
    mov di, te_scr
    mov cx, TE_SCR
    mov al, ' '
.z:
    mov [di], al
    inc di
    loop .z
    mov word [te_cur], 0
    mov word [te_scrl], 0           ; ...and a cleared screen owes no blit
    call te_markall
    pop di
    pop cx
    pop ax
    ret

; --- te_hire - one worker, retried until granted (SPEC.md 20.6) --------------
te_hire:
    push ax
    push bx
    cmp byte [te_spawned], 1
    je .out
    mov ax, te_worker
    mov bx, [te_win]
    call OSAPI_TASK_SPAWN
    jc .out
    mov byte [te_spawned], 1
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; THE WORKER - one verb per turn, and never a blocking call
; =============================================================================
te_worker:
.loop:
    mov bx, [te_win]
    call OSAPI_TASK_ALIVE           ; the death door: may never return
    call te_step
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp .loop

te_step:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; ES = OURS for every NETV_* buffer: the
                                    ; door is the one driver entry point handed
                                    ; the CALLER's segment (SPEC.md 20.11)
    mov byte [te_dirty], 0
    mov al, [te_state]
    cmp al, TS_OPEN
    je .open
    cmp al, TS_WAIT
    je .wait
    cmp al, TS_UP
    je .up
    jmp .done

; --- OPEN: who is there, then start a connection ----------------------------
.open:
    call net_find                   ; WHICH DRIVER: the card's class or the
    jc .nodrv                       ; cable's, and not the RAM disk sharing the
                                    ; second (apps/os88sock.inc, SPEC.md
                                    ; 20.11.1)
    mov bh, NET_CLASS
    mov bl, NETV_STATE
    call OSAPI_DRV_CALL
    jc .nodrv
    test al, NSTF_SOCK
    jz .nolink
    call te_split                   ; the box into a host and a port
    mov bh, NET_CLASS
    mov bl, NETV_OPEN
    mov si, te_host
    mov cx, [te_port]
    call OSAPI_DRV_CALL
    jc .busyq
    mov [te_hnd], al
    mov byte [te_state], TS_WAIT
    mov byte [te_dirty], 1
    jmp .done
.nodrv:
    mov si, te_s_nodrv
    jmp .fail
.nolink:
    mov si, te_s_nolink
    jmp .fail

; --- WAIT: poll until the connection resolves -------------------------------
.wait:
    call te_qstat
    jc .busyq
    cmp ah, NSK_UP
    je .isup
    cmp ah, NSK_CONNECT
    je .done
    mov si, te_s_refused
    jmp short .fail
.isup:
    mov byte [te_state], TS_UP
    mov byte [te_dirty], 1
    jmp .done

; --- UP: send what is queued, then take what has arrived --------------------
.up:
    cmp byte [te_want], 0
    je .send
    call te_close
    jmp .done
.send:
    call te_flushtx
    jc .busyq
    mov al, [te_hnd]
    mov di, te_rx
    mov cx, TE_RX
    mov bh, NET_CLASS
    mov bl, NETV_RECV
    call OSAPI_DRV_CALL
    jc .busyq
    or cx, cx
    jz .empty
    call te_feed                    ; ...the bytes onto the screen
    jmp .done
.empty:
    call te_qstat                    ; NOTHING READ IS NOT AN END (netpkg.inc):
    jc .busyq                       ; the end is NSK_CLOSING with nothing left
    cmp ah, NSK_UP
    je .done
    cmp ah, NSK_CONNECT
    je .done
    or cx, cx
    jnz .done                       ; closing, and there is still data behind
    mov si, te_s_closed
    jmp short .fail

.busyq:
    cmp ax, NETE_BUSY
    je .done                        ; the Link volume has the wire: come back
    mov si, te_s_lost               ; next tick. NOT a failure (netpkg.inc)
.fail:
    mov [te_msg], si
    mov byte [te_state], TS_ERR
    mov byte [te_dirty], 1
    call te_drop
.done:
    call te_owed
    jnc .out
    call te_show
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- te_owed - CF=1 if anything on screen is out of date ---------------------
; Three separate debts and one test: the CHROME ([te_dirty], a state change),
; the SCROLL ([te_scrl], pixels the buffer has already moved) and the ROWS.
; te_step used to ask about [te_dirty] alone and te_feed set it for arriving
; TEXT, which is how a character ended up redrawing the status line and the
; button as well as all eighteen rows.
te_owed:
    push ax
    cmp byte [te_dirty], 0
    jne .yes
    cmp word [te_scrl], 0
    jne .yes
    mov ax, [te_dr0]
    cmp ax, [te_dr1]
    ja .no
.yes:
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

; --- te_qstat - NETV_STATUS on our handle. out AH = state, CX = readable -----
te_qstat:
    mov al, [te_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_STATUS
    call OSAPI_DRV_CALL
    ret

; --- te_close / te_drop - end the session, asked for or not ------------------
te_close:
    push ax
    push bx
    mov al, [te_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    mov si, te_s_closed
    mov [te_msg], si
    pop bx
    pop ax
te_drop:
    mov byte [te_hnd], 0
    mov byte [te_want], 0
    cmp byte [te_state], TS_ERR
    je .out
    mov byte [te_state], TS_DOWN
.out:
    mov byte [te_dirty], 1
    ret

; --- te_flushtx - hand the typed queue to the wire --------------------------
; out: CF=1 and AX = a NETE_* the caller reports
te_flushtx:
    push bx
    push cx
    push si
    mov bx, [te_txr]
    cmp bx, [te_txw]
    je .none
    mov si, te_txb
    add si, bx
    mov cx, [te_txw]
    cmp cx, bx
    ja .run
    mov cx, TE_TX                   ; the queue wrapped: send to the end of the
.run:                               ; buffer now and the rest next turn, which
    sub cx, bx                      ; is one round trip and no copy
    mov al, [te_hnd]
    push bx
    mov bh, NET_CLASS
    mov bl, NETV_SEND
    call OSAPI_DRV_CALL
    pop bx
    jc .out
    add bx, cx                      ; ...ADVANCE BY WHAT IT TOOK, which may be
    and bx, TE_TX-1                 ; less than offered (netpkg.inc)
    mov [te_txr], bx
.none:
    clc
.out:
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; te_feed - CX bytes at te_rx onto the screen
;
; The Telnet protocol is three bytes wide and this is all of it: IAC
; introduces a command, DO/WILL are answered with WONT/DONT, and a subnegotia-
; tion is swallowed to its IAC SE. **Refusing everything is a valid Telnet
; implementation** and it is the right one here: the options worth accepting
; (ECHO, SGA) change what the HOST does and not what this draws, and NAWS
; would have to report a window size this app does not let the user change.
; -----------------------------------------------------------------------------
te_feed:
    push ax
    push bx
    push cx
    push si
    mov si, te_rx
.next:
    mov al, [si]
    inc si
    push cx
    call te_byte
    pop cx
    loop .next                      ; ...and NOT [te_dirty]: te_putc marked the
    pop si                          ; rows it changed, and the chrome did not
    pop cx
    pop bx
    pop ax
    ret

; --- te_byte - one byte through the protocol and onto the screen ------------
te_byte:
    push bx
    mov bl, [te_ph]
    or bl, bl
    jnz .proto
    cmp al, IAC
    jne .text
    mov byte [te_ph], 1
    jmp .out
.proto:
    cmp bl, 1
    je .cmd
    cmp bl, 2
    je .optn
    cmp bl, 3
    je .sb
    mov byte [te_ph], 0
    jmp .out
.cmd:
    cmp al, IAC
    je .escaped                     ; IAC IAC is a literal 255
    cmp al, T_SB
    je .startsb
    cmp al, T_WILL
    jae .want
    mov byte [te_ph], 0             ; a two-byte command we ignore
    jmp .out
.escaped:
    mov byte [te_ph], 0
    jmp .text
.startsb:
    mov byte [te_ph], 3
    jmp .out
.want:
    mov [te_verb], al               ; DO/DONT/WILL/WONT: the option follows
    mov byte [te_ph], 2
    jmp .out
.optn:
    mov byte [te_ph], 0
    call te_refuse
    jmp .out
.sb:
    cmp al, T_SE                    ; ...swallowed to the end. A subnegotiation
    jne .out                        ; we never agreed to cannot be one we have
    mov byte [te_ph], 0             ; to parse
    jmp .out
.text:
    call te_putc
.out:
    pop bx
    ret

; --- te_refuse - answer AL's option with WONT or DONT ------------------------
; The reply's SENSE is the mirror of the question: a host that WILL is told
; DONT, a host that asks us to DO is told WONT. Getting that backwards is an
; option loop - two ends each answering the other for ever - which is the one
; way a Telnet client can wedge a connection that is working.
te_refuse:
    push ax
    push bx
    mov bl, al                      ; the option
    mov al, IAC
    call te_txraw                   ; RAW, all three: option 0 is legal and
    mov al, T_WONT                  ; te_tx's scan-code filter would eat it,
    cmp byte [te_verb], T_DO        ; leaving a two-byte command the host
    je .send                        ; completes with the user's next keystroke
    cmp byte [te_verb], T_DONT
    je .send
    mov al, T_DONT                  ; it said WILL or WONT
.send:
    call te_txraw
    mov al, bl
    call te_txraw
    pop bx
    pop ax
    ret

; --- te_putc - AL onto the screen at the cursor ------------------------------
te_putc:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp al, 27
    je .esc
    cmp byte [te_ansi], 0
    jne .inesc
    cmp al, 13
    je .cr
    cmp al, 10
    je .lf
    cmp al, 8
    je .bs
    cmp al, 9
    je .tab
    cmp al, ' '
    jb .out                         ; every other control byte is DISCARDED:
    cmp al, 0x7E                    ; a printing terminal prints characters
    ja .out
    mov di, [te_cur]
    mov [te_scr + di], al
    call te_markcur                 ; ...BEFORE the cursor moves: this row is
    inc di                          ; the one whose pixels changed
    mov [te_cur], di
    mov ax, TE_COLS
    call te_wrapck
    jmp short .out
.esc:
    mov byte [te_ansi], 1
    jmp short .out
.inesc:
    cmp al, '['                     ; RECOGNISED AND DISCARDED. A sequence this
    je .out                         ; does not implement showing up as text in
    cmp al, ';'                     ; the middle of a sentence is worse than
    je .out                         ; the sequence doing nothing (file header)
    cmp al, '0'
    jb .endesc
    cmp al, '9'
    jbe .out
.endesc:
    mov byte [te_ansi], 0
    jmp short .out
.cr:
    mov ax, [te_cur]
    mov bl, TE_COLS
    div bl                          ; AH = the column
    mov al, ah
    xor ah, ah
    mov bx, [te_cur]
    sub bx, ax
    mov [te_cur], bx
    jmp short .out
.lf:
    mov ax, [te_cur]
    add ax, TE_COLS
    mov [te_cur], ax
    call te_scrollck
    jmp short .out
.bs:
    cmp word [te_cur], 0
    je .out
    dec word [te_cur]
    jmp short .out
.tab:
    mov al, ' '
    call .space8
    jmp short .out
.space8:
    push cx
    mov cx, 8
.sp:
    push cx
    mov al, ' '
    call te_putc
    pop cx
    loop .sp
    pop cx
    ret
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- te_wrapck / te_scrollck - the cursor past the edge, and past the bottom -
te_wrapck:
    push ax
    push bx
    push dx
    mov ax, [te_cur]
    xor dx, dx
    mov bx, TE_COLS
    div bx
    or dx, dx
    jnz .out                        ; not at a row boundary: nothing to do
    call te_scrollck
.out:
    pop dx
    pop bx
    pop ax
    ret

te_scrollck:
    push ax
    push bx
    push cx
    push si
    push di
    cmp word [te_cur], TE_SCR
    jb .out
    mov si, te_scr + TE_COLS        ; ...one row up, and the last row blanked
    mov di, te_scr
    mov cx, TE_SCR - TE_COLS
.mv:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .mv
    mov cx, TE_COLS
    mov al, ' '
.bl:
    mov [di], al
    inc di
    loop .bl
    mov word [te_cur], TE_SCR - TE_COLS
    inc word [te_scrl]              ; ...and te_scrollpaint spends it as ONE
    mov bx, TE_ROWS - 1             ; blit; only the row it opens is lettered
    call te_mark
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; te_show - the worker's own repaint, under the lock
;
; SPEC.md 20.6 rule 3: take the lock, draw, release. The lock is NEVER held
; across a NETV_* call above - a recv is up to 274 ms of cable and the cursor
; would stop dead for every one of them.
; -----------------------------------------------------------------------------
te_show:
    push ax
    push bx
    push si
    call OSAPI_GFX_LOCK
    mov bx, [te_win]
    call OSAPI_WM_OBSCURED
    jc .un                          ; covered: the next paint owes it, and a
                                    ; background painter that drew anyway
                                    ; would paint over the window on top
                                    ; (SPEC.md 11.3)
    cmp byte [te_txm], 0
    jne .un                         ; A FOREIGN TEXT MODE IS UP: every drawing
                                    ; slot renders desktop geometry into what
                                    ; are now character cells (SPEC.md 53.1),
                                    ; and the bracket draws instead
    cmp byte [te_abon], 0
    jne .un                         ; the credits are up: the worker may not
                                    ; draw over them, and the click that
                                    ; dismisses them repaints everything
    mov si, [te_win]
    call te_layout
    call te_scrollpaint             ; ...the pixels the buffer already moved
    call te_rows_owed               ; ...and ONLY the rows that changed
    cmp byte [te_dirty], 0
    je .un                          ; the CHROME is a separate question: a
    call te_status                  ; character arriving is not a state change
    call te_button
.un:
    call OSAPI_GFX_UNLOCK
    pop si
    pop bx
    pop ax
    ret

; =============================================================================
; te_about - the OSAPI_ABOUT_SET handler (slot 0x01E0, SPEC.md 12.2)
; in:  SI = our window; UI task, gfx lock HELD, far-called at our segment
; out: nothing; preserves all registers
;
; Frotz's shape rather than the browser's card: this window's whole content is
; a screen we can redraw at will, so the credits take it over and the next key
; or click puts the terminal back. [te_abon] is what keeps the WORKER off it -
; a live session would otherwise letter arriving text over the credits.
; =============================================================================
te_about:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [te_win], si
    call te_layout
    mov byte [te_abon], 1
    mov al, CWHITE                  ; the terminal's own area, cleared - the
    call OSAPI_SET_COLOR            ; host box and the button stay, because
    call te_wpx                     ; nothing here covers them
    mov cx, [te_px]
    add cx, ax
    dec cx
    mov ax, [te_px]
    mov bx, [te_oy]
    add bx, TE_TOPY
    mov dx, [te_oy]
    add dx, [te_chh]
    sub dx, TE_STATH + 1            ; ...and NOT over the status line, which
    call OSAPI_GFX_FILL             ; te_abdismiss does not redraw
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, te_ablines
    mov dx, [te_oy]
    add dx, TE_TOPY + 12
.line:
    mov cx, [si]
    or cx, cx
    jz .done
    push si
    mov si, cx
    mov cx, [te_px]
    add cx, 16
    call OSAPI_FONT_STR
    pop si
    add si, 2
    add dx, 12
    jmp short .line
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- te_abdismiss - CF=1 if this event was spent putting the terminal back ---
te_abdismiss:
    cmp byte [te_abon], 0
    je .no
    mov byte [te_abon], 0
    call te_screen                  ; ...all of it: the credits covered it all
    stc
    ret
.no:
    clc
    ret

te_ablines:
    dw te_ab1, te_ab2, te_ab3, te_ab4, 0
te_ab1:     db 'Telnet for os8088', 0
te_ab2:     db 'RFC 854 over the socket API', 0
te_ab3:     db 0                    ; a blank line is a line with no glyphs
te_ab4:     db 'Contributed by Elendilon', 0

; -----------------------------------------------------------------------------
; te_split - the host box into te_host and te_port
;
; `host`, `host:port`. A missing port is 23, which is the only default a
; Telnet client may have.
; -----------------------------------------------------------------------------
te_split:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, te_hbuf
    mov di, te_host
    mov cx, TE_HOSTMAX-1
    mov word [te_port], 23
.copy:
    mov al, [si]
    or al, al
    jz .done
    cmp al, ':'
    je .port
    mov [di], al
    inc di
    inc si
    loop .copy
.done:
    mov byte [di], 0
    jmp short .out
.port:
    mov byte [di], 0
    inc si
    xor ax, ax
    mov word [te_port], 0
.dig:
    mov bl, [si]
    inc si
    cmp bl, '0'
    jb .pdone
    cmp bl, '9'
    ja .pdone
    mov ax, [te_port]
    mov cx, 10
    mul cx
    sub bl, '0'
    xor bh, bh
    add ax, bx
    mov [te_port], ax
    jmp short .dig
.pdone:
    cmp word [te_port], 0
    jne .out
    mov word [te_port], 23
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- the app menu set (SPEC.md 12.2) -----------------------------------------
; No Close: SPEC.md 12.7 puts one in the app-NAME cell for every application.
    OS88_MENUSET te_menus, te_name_s, te_oncmd
        OS88_MENU te_m_sess, te_i_sess, 3
    OS88_MENUSET_END te_menus

te_oncmd:
    push ax
    push si
    or al, al                       ; **AL IS THE ITEM AND AH IS THE MENU.**
    jnz .i1                         ; This tested AH, and there is exactly one
    call te_toggle                  ; menu here - so it was always 0 and Clear
    jmp short .rp                   ; Screen could never be reached. The
.i1:                                ; browser's Go menu had the same fault at
    cmp al, 1                       ; the same place (SPEC.md 71.8)
    jne .fs
    call te_clear
    jmp short .rp
.fs:
    mov si, [te_win]
    call te_fsx                     ; ...and the bracket repaints the world on
    jmp short .out                  ; its way out (SPEC.md 53.6), so this one
.rp:                                ; owes no paint
    mov si, [te_win]
    call te_paint
.out:
    pop si
    pop ax
    ret

te_name_s:  db 'Telnet', 0
te_m_sess:  db 'Session', 0
te_i_sess:  dw te_it_conn, te_it_clr, te_it_fs
te_it_conn: db 'Connect / Close', 0
te_it_clr:  db 'Clear Screen', 0
te_it_fs:   db 'Full Screen  ^]', 0

te_names:   dw te_s_idle, te_s_open, te_s_wait, te_s_up, te_s_down, te_s_err
te_s_idle:  db 'Not connected', 0
te_s_open:  db 'Looking up', 0
te_s_wait:  db 'Connecting', 0
te_s_up:    db 'Connected', 0
te_s_down:  db 'Disconnected', 0
te_s_err:   db 'Failed', 0        ; ...and [te_msg] says WHY, on the row
                                     ; below - a state name is not a reason
te_s_nodrv: db 'No link driver', 0
te_s_nolink: db 'No partner', 0
te_s_refused: db 'Refused', 0
te_s_closed: db 'Host closed', 0
te_s_lost:  db 'Link lost', 0
te_s_conn:  db 'Connect', 0
te_s_disc:  db 'Close', 0
te_ttl:     db 'Telnet', 0

; --- window template (SPEC.md 11) -------------------------------------------
te_tpl:
    dw 40, 40, TE_W, TE_H
    dw te_ttl, te_paint, te_onkey, te_onclick

%include "os88ui.inc"
%include "os88line.inc"
%include "os88sock.inc"         ; net_find (SPEC.md 72, SPEC.md 20.11.1)
%include "tetxt.inc"            ; ...and the text-mode screen (SPEC.md 70.6)

    OS88_BSS TE_BSS
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) ----------------------------------
te_win      equ os88_image_end + 0
te_ox       equ os88_image_end + 2
te_oy       equ os88_image_end + 4
te_cw       equ os88_image_end + 6
te_chh      equ os88_image_end + 8
te_cur      equ os88_image_end + 10   ; the cursor, as a screen offset
te_txr      equ os88_image_end + 12
te_txw      equ os88_image_end + 14
te_port     equ os88_image_end + 16
te_msg      equ os88_image_end + 18   ; -> the failure text, for TS_ERR
te_state    equ os88_image_end + 20
te_spawned  equ os88_image_end + 21
te_hnd      equ os88_image_end + 22
te_want     equ os88_image_end + 23   ; the user asked to close
te_dirty    equ os88_image_end + 24
te_ph       equ os88_image_end + 25   ; the IAC state machine's phase
te_verb     equ os88_image_end + 26   ; ...and the DO/WILL it is answering
te_ansi     equ os88_image_end + 27   ; inside an escape sequence
te_btn      equ os88_image_end + 28   ; 8: the Connect button's rect
te_line     equ os88_image_end + 36   ; OS88LINE_SZ
te_hbuf     equ te_line + OS88LINE_SZ ; TE_HOSTMAX: what the user typed
te_host     equ te_hbuf + TE_HOSTMAX  ; ...and the half before the colon
te_sline     equ te_host + TE_HOSTMAX  ; 20: the padded status field
te_txb      equ te_sline + 20          ; TE_TX
te_rx       equ te_txb + TE_TX        ; TE_RX
te_scr      equ te_rx + TE_RX         ; TE_SCR + 1 (the run's terminator)
te_px       equ te_scr + TE_SCR + 1   ; word: the text pen, 8-ALIGNED
te_vcols    equ te_px + 2             ; word: columns the LIVE box shows
te_vrows    equ te_vcols + 2          ; word: rows the LIVE content box fits
te_vtop     equ te_vrows + 2          ; word: the first buffer row it shows
te_dr0      equ te_vtop + 2           ; word: the dirty range, dr0 > dr1 = none
te_dr1      equ te_dr0 + 2
te_scrl     equ te_dr1 + 2            ; word: rows the BUFFER has scrolled
                                      ; since the screen last agreed with it
te_abon     equ te_scrl + 2           ; byte: the credits have the screen
te_txm      equ te_abon + 1           ; byte: a FOREIGN TEXT MODE is up, so
                                      ; every kernel drawing slot is off-limits
                                      ; (SPEC.md 53.1) and te_show must not
te_tseg     equ te_txm + 1           ; word: ...its framebuffer segment
te_fsi      equ te_tseg + 2           ; FSI_SIZE: what OSAPI_FSX_MODE filled
TE_BSS      equ (te_fsi - os88_image_end) + FSI_SIZE
