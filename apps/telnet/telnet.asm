; =============================================================================
; os8088 - apps/telnet/telnet.asm
;
; TELNET - docs/plans/completed/NET-STACK-PLAN.md stage C, and the first thing on this machine
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
; It WAS a dumb terminal, deliberately - no cursor addressing, no colour, no
; scroll regions - and SPEC.md 70.8 ended that. The screen is 80x25 cells of a
; CHARACTER AND AN ATTRIBUTE now, which is the screen an ANSI board draws its
; menus, its boxes and its art against: a terminal of any other size does not
; render that art wrongly so much as it renders a different picture.
;
; The parser that ACTS on an escape sequence is apps/telnet/teansi.inc
; (SPEC.md 70.9), %include'd at the foot of this file, and its second reader is
; tools/ansisim.py; the option layer above it is te_byte (SPEC.md 70.10.1) and
; the Zmodem receiver below it is apps/telnet/tezm.inc (SPEC.md 70.11). What is
; in THIS file is the window, the screen underneath all three, the windowed
; renderer and the glyphs.
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
; socket buffer is not: te_rx is the worker's alone, con_scr is under the lock.
;
; Every proc here is a near proc with a near `ret`: the kernel reaches a
; callback through the dispatcher in the package's own header (SPEC.md 20.1),
; and a `retf` returns into the loader's stack frame.
; =============================================================================

%include "os88api.inc"
%include "netpkg.inc"               ; ...THE DRIVER'S OWN HEADER, the same file
                                    ; drivers/net/net.asm includes, so the two
                                    ; ends cannot drift (SPEC.md 20.11)

    OS88_HEADER 'TELNET', te_entry, 1, OS88_STACK_256
                                ; THE WORKER'S STACK, declared
                                ; rather than defaulted (SPEC.md 8.7):
                                ; static 66, plus ETHER.DRV's
                                ; socket verbs - ~126 bytes
                                ; on OUR stack, the driver
                                ; owning no task of its own
                                ; over the 64-byte interrupt floor
                                ; that is 194, and 256 gives 1.32x
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

TE_W        equ 656                 ; 80 columns of 8px, plus chrome - and
TE_H        equ 254                 ; wm_fit clamps it onto the live desktop,
                                    ; so a 640-wide screen opens a window that
                                    ; shows 76 of the 80 and full screen shows
                                    ; the rest (SPEC.md 70.8.10)
                                    ; ...and CON_COLS / CON_ROWS are
                                    ; os88con.inc's, with the rest of the
                                    ; screen's arithmetic. The terminal is
                                    ; 80x25 because that is not a size, it is
                                    ; the BOARD's own screen, so NAWS reports a
                                    ; FACT (SPEC.md 70.8)
TE_RX       equ 512                 ; one NETV_RECV's landing ground
TE_HOSTMAX  equ 48
TE_TX       equ 256                 ; keystrokes and protocol replies waiting
                                    ; for the worker - 64 before SPEC.md
                                    ; 70.10.3, and still a power of two so the
                                    ; index is `and`-masked
TE_PND      equ 32                  ; ...and the ONE refused reply, held whole:
                                    ; a Zmodem hex header is 21 bytes and a
                                    ; binary one at most 18 after escaping,
                                    ; `SB TTYPE IS` is 10 and `SB NAWS` is 9

TE_SLINE    equ 44                  ; the status field, in CELLS. It was 16 -
                                    ; the longest connection state - and
                                    ; SPEC.md 70.11.5's progress line is longer:
                                    ; twelve of name, two spaces, and two counts
                                    ; of up to thirteen characters with the
                                    ; thousands separators in them
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

; --- the four options a board expects to be answered (SPEC.md 70.10.1) -------
TO_BINARY   equ 0                   ; RFC 856 - and Zmodem does not work without
TO_ECHO     equ 1                   ; it, because a data subpacket contains every
TO_SGA      equ 3                   ; byte value and a host that has not agreed
TO_TTYPE    equ 24                  ; is entitled to strip the eighth bit
TO_NAWS     equ 31

; --- the SB state machine's phases, which are [te_ph] 3 and above -----------
TP_SBOPT    equ 3                   ; the option byte of a subnegotiation
TP_SBODY    equ 4                   ; ...its body
TP_SBIAC    equ 5                   ; ...and an IAC inside it
TE_SBMAX    equ 64                  ; ...and the longest body this swallows. The
                                    ; two options with a body here are TTYPE and
                                    ; NAWS, whose longest is a terminal name

; -----------------------------------------------------------------------------
; te_entry - package entry (SPEC.md 20.2)
; -----------------------------------------------------------------------------
te_entry:
    push si
    mov si, te_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [te_win], bx
    push ax                         ; **THE GESTURE'S TWO SLOTS** (SPEC.md
    push bx                         ; 20.5.1.3): neither is a template word
    push si
    push di
    push dx
    mov ax, bx
    mov bx, te_btrec
    mov si, te_onup
    mov di, te_ondrag
    mov dx, te_onclick                ; OUR own click work; the library
                                    ; takes the press FIRST and chains
                                    ; here (SPEC.md 20.5.1.3.3)
    call os88ui_btninit
    pop dx
    pop di
    pop si
    pop bx
    pop ax
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    OS88_ALTENTER_ARM               ; SPEC.md 11.2.1.1: nothing tracks a
                                    ; scancode until something asks, and both
                                    ; halves of the chord ride on that map
    OS88_REGION_MOVABLE
    mov word [te_line + LN_BUF], te_hbuf    ; **THE BLOCK'S BUFFER WORDS, and
    mov word [te_line + LN_MAX], TE_HOSTMAX ; they are not optional**: bss
                                            ; arrives ZEROED (SPEC.md 21 step
                                            ; 5), so an unset LN_BUF points at
                                            ; offset 0 - the package's own
                                            ; header - and the first keystroke
                                            ; writes into it
    mov byte [te_lfg], 0x07         ; SPEC.md 70.9.4's LOGICAL state, and
    call te_derive                  ; [con_attr] is derived from it and never
                                    ; set by hand - which is the whole reason
                                    ; `CSI 7;31m` comes out black on red. bss
                                    ; arrives ZEROED (SPEC.md 21 step 5), and
                                    ; a logical foreground of zero would derive
                                    ; black on black
    mov byte [con_cvis], 1           ; ...and the cursor is drawn until `?25l`
    call con_font                    ; the 256 CP437 glyphs, ROM where there is
    call con_clear                   ; one and the shipped table where not
    mov al, 1
    mov bx, [te_win]                ; ...BX is the window: WM_CREATE left it
    call OSAPI_WM_SNAP              ; there, but con_clear runs in between              ; the screen is 8px cells, so an aligned
                                    ; content origin is what earns font_run's
                                    ; single-store path on the two mono
                                    ; adapters (SPEC.md 11.94)
    push si
    mov si, te_menus
    call OSAPI_MENU_SET
    mov al, 1
    mov bx, [te_win]
    call OSAPI_WM_SIZABLE           ; **RESIZABLE, AND NOTHING REFLOWS.**
                                    ; te_layout derives con_vcols and con_vrows
                                    ; from the LIVE content box on every call,
                                    ; so both axes change how much of the
                                    ; fixed 80x25 screen is on view and
                                    ; neither changes the screen. The window
                                    ; opens wide enough for all of it
    mov ax, tz_wake
    mov bx, [te_win]
    call OSAPI_WM_ONWAKE            ; **THE UI-TASK HALF** (SPEC.md 70.11.3):
                                    ; everything in this package that touches a
                                    ; file happens in there, because a worker
                                    ; may not (SPEC.md 20.6 rule 7). It
                                    ; preserves the flags, which matters here
                                    ; for OSAPI_ABOUT_SET's reason below
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
; in:  SI = window; sets te_ox/con_oy and the line block's four words
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
    ; --- WHAT DEPTH THE SCREEN IS, asked here and not once at launch -------
    ; SPEC.md 70.8.4: the pen is not read on a 1bpp adapter, so the polarity
    ; goes into the BAND instead - and which of those two the composer does is
    ; a fact about the screen the window is on, which a window can be dragged
    ; off. te_layout runs on every paint, click, key and worker draw, so this
    ; is one far call in the place the answer is already being re-derived.
    ; **It is the PRIMARY's depth** (osapi_video's own contract), and on an
    ; extended desktop with a mono card beside a colour one that is the wrong
    ; question for the far display - there is no per-display depth to ask
    ; (SPEC.md 39.14 publishes none), so it is stated rather than hidden.
    call OSAPI_VIDEO                ; DH = bits per pixel, 4 or 1
    cmp dh, 1
    mov dh, 0
    ja .colour
    mov dh, 1
.colour:
    mov [con_mono], dh
    call te_ice_label               ; ...and the Session item follows the
    call tz_label                   ; adapter, and Receive File follows the
                                    ; SESSION (SPEC.md 47 - grey a fact)
                                    ; adapter: a window dragged onto a mono
                                    ; display greys it (SPEC.md 70.8.9)
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [te_ox], ax
    mov [con_oy], dx
    mov bx, si
    call OSAPI_WM_GEOM              ; CX = content width, DX = height - and it
    mov [te_cw], cx                 ; takes BX, which WM_CONTENT has spent
    mov [te_chh], dx
    mov word [con_topy], TE_TOPY     ; ...and where OUR chrome ends, because the
                                    ; console does not know this window has a
                                    ; host field above its text (os88con.inc)
    mov cx, [te_ox]
    add cx, TE_PAD
    mov [te_line + LN_X1], cx
    add cx, [te_cw]
    sub cx, TE_PAD*2 + 88           ; ...the Connect button's width beside it
    mov [te_line + LN_X2], cx
    mov cx, [con_oy]
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
    mov [con_px], cx

    ; --- and how many rows the LIVE content box can show --------------------
    ; CON_ROWS is 25 and the screen is fixed, but the WINDOW is not: wm_fit
    ; clamps a 190-row template to the desktop band, which on CGA is 155. The
    ; rows that did not fit were drawn anyway - the gfx primitives clip to the
    ; SCREEN, not to the window (SPEC.md 39.7) - so the bottom of the terminal
    ; was painted over the dock. The window shows the LAST con_vrows rows,
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
    cmp ax, CON_ROWS
    jbe .rset
    mov ax, CON_ROWS
.rset:
    mov [con_vrows], ax
    mov cx, CON_ROWS
    sub cx, ax
    mov [con_vtop], cx

    ; --- and how many COLUMNS it can show -----------------------------------
    ; **THIS IS NOT REFLOW AND MUST NOT BECOME IT.** The screen is 80 columns
    ; wide whatever the window is: the buffer does not change, the host is
    ; told 80x25 whatever this answers (SPEC.md 70.10.1's NAWS reports the
    ; BUFFER), and its idea of where a line wraps stays exactly what it was.
    ; What a narrow window changes is how
    ; much of each line you can SEE, which is the same bargain con_vrows makes
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
    sub ax, [con_px]                 ; ...from the ALIGNED pen to the far edge
    jns .cols
    xor ax, ax
.cols:
    mov cl, 3
    shr ax, cl
    or ax, ax
    jnz .cnz
    inc ax
.cnz:
    cmp ax, CON_COLS
    jbe .cset
    mov ax, CON_COLS
.cset:
    mov [con_vcols], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret
te_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [te_win], si
    call tz_cancelck                ; **A W_PAINT A SECOND AFTER THE DIALOG WENT
                                    ; UP IS THE CANCEL** (SPEC.md 70.11.4), and
                                    ; the grace period is the whole of the fix:
                                    ; fdlg_open's own wm_show repaints the
                                    ; desktop when the dialog ARRIVES, and this
                                    ; window is 656x254 where the dialog is
                                    ; 300x170, so it is not covered and that
                                    ; paint reaches us. Unarmed, it skipped
                                    ; every transfer before the user could
                                    ; answer
    call te_hire
    call te_layout
    push si
    mov si, te_line
    call os88line_draw
    pop si
    call te_button
    call te_screen
    call te_status
    call te_promise                 ; a full paint settles every debt, so the
                                    ; glass matches the buffer and the window
                                    ; may be banked again (SPEC.md 70.7). This
                                    ; is where the promise comes BACK after a
                                    ; covered session withdrew it
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The record's two arrays. The LABEL one is a single entry and is PATCHED,
; because this button's caption changes with the session (SPEC.md 20.5.1.3's
; arrays are the caller's, so a dynamic caption needs no special case).
te_btlbl:  dw te_s_conn
te_btflg:  dw OS88UI_FILL
    OS88UI_BTNREC te_btrec, te_btn, te_btlbl, te_btflg, 1

; --- te_button - Connect, or Close while a session is up ---------------------
te_button:
    push ax
    push bx
    push si
    push di
    mov si, te_s_conn
    cmp byte [te_state], TS_UP      ; **THE SAME PREDICATE te_toggle USES**, so
    jne .draw                       ; the label and the action cannot disagree
    mov si, te_s_disc               ; (SPEC.md 47 rule 5). It was `jb`, which
.draw:                              ; called a refused session `Close`
    mov [te_btlbl], si
    mov bx, te_btrec
    mov al, 1
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
    call tz_label
    mov si, [tz_why]                ; **A REASON BEATS A COUNT.** A file refused
    or si, si                       ; for its cluster size (SPEC.md 70.11.3) is
    jnz .have                       ; a fact the user has to read, and the batch
                                    ; carries on underneath it - so it stays on
                                    ; the row until the next file is accepted
    cmp byte [tz_pan], 0
    je .st
    call tz_text                    ; `NAME  12,345 / 98,765` (SPEC.md 70.11.5),
    mov si, tz_msg                  ; and the counter moves per COMMITTED CHUNK
    jmp short .have
.st:
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
    mov cx, TE_SLINE                ; a FIXED width, so the run always erases
    cmp cx, [con_vcols]              ; the whole field whatever the last message
    jbe .wid                        ; was - and it is wide enough for the
    mov cx, [con_vcols]              ; progress line, which is the longest thing
.wid:                               ; that ever lands on this row. **CLAMPED TO
                                    ; THE VIEWPORT**: font_run clips to the
                                    ; SCREEN and not to the window (SPEC.md
                                    ; 39.7), so a field wider than a narrow
                                    ; window is text drawn over whatever is
                                    ; beside it
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
    mov cx, [con_px]                 ; ...8-aligned, like every run here
    mov dx, [con_oy]
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
    call te_wscroll                 ; **A FULL PAINT MAY NOT SKIP A FRAME**: it
                                    ; owes every row, and there is no next pass
                                    ; that will come back for it. The hold is a
                                    ; con_cmove of at most 2,000 cells, so the
                                    ; wait is the gfx lock's own shape
    call con_markall
    cmp byte [tz_pan], 0
    je .rows                        ; a transfer is up: the panel has the
    call tz_panel                   ; terminal's area and the marks just set are
    ret                             ; what puts the text back afterwards
.rows:
    call con_rows_owed
    call con_takescroll              ; **AND THE SCROLL DEBT, WHICH IT DID NOT.**
    ret                             ; Every row has just been drawn from the
                                    ; buffer, so a pending "blit the pixels up
                                    ; by N" is not owed - and spending it is
                                    ; not harmless. con_scrollpaint moves the
                                    ; whole terminal up N rows and marks only
                                    ; the N it vacated, so rows 0..17-N are
                                    ; left showing rows N..17 until the next
                                    ; thing that touches them. Reachable
                                    ; before this line: text scrolls while the
                                    ; window is covered (te_show bails at .un
                                    ; ahead of con_scrollpaint, so [con_scrl]
                                    ; accumulates), the window is uncovered
                                    ; into a full te_paint, and the next
                                    ; worker pass blits a screen that was
                                    ; already right

; -----------------------------------------------------------------------------
; te_wscroll - wait out a half-done scroll (the w3 review's MAJOR 1)
; OSAPI_TASK_YIELD and not a spin: the worker holds [con_scrbusy] across one
; con_cmove and needs a slice to finish it, and this runs on the UI task.
; BOUNDED, because a worker that died mid-scroll must not take the UI task with
; it - 2,000 yields is far past any real hold.
; -----------------------------------------------------------------------------
te_wscroll:
    push cx
    mov cx, 2000
.w:
    cmp byte [con_scrbusy], 0
    je .out
    call OSAPI_TASK_YIELD
    loop .w
.out:
    pop cx
    ret
te_onup:
    push ax
    push bx
    push si
    push di
    mov bx, te_btrec
    call os88ui_btnup               ; AX = what FIRED, 0 = cancelled
    or ax, ax
    jz .uout
    call te_toggle
    call te_button                  ; the caption follows the state
.uout:
    pop di
    pop si
    pop bx
    pop ax
    ret

te_ondrag:
    push ax
    push bx
    mov bx, te_btrec
    call os88ui_btndrag
    pop bx
    pop ax
    ret

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
    or ax, ax                       ; mis-aimed press must be cancellable -
    jnz .out                        ; te_onup has the action
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
    cmp ax, KEY_ALTENTER            ; ...and so is Alt+Enter, for the user who
    je .fsx                         ; knows the OTHER one (SPEC.md 11.2.1.1).
                                    ; It has to be caught here whatever is
                                    ; decided about the door: every key below
                                    ; goes to the HOST, and the ascii half of
                                    ; this one is 0 - a NUL down the wire
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

; =============================================================================
; THE KEYS (SPEC.md 70.10.2)
;
; The delivery contract is W_ONKEY's: AL = ASCII, AH = the `int 16h` scan code,
; and the kernel passes int 16h AH=00h's AX through unchanged - there is no
; kernel scan-code table and no translation. **AL = 0 IS HOW AN EXTENDED KEY IS
; RECOGNISED**, tested before AH is looked at, which is SPEC.md 27's rule for
; exactly the reason it gives: the numeric keypad sends `4 6 8 2 7 1 .` with the
; scan codes of Left, Right, Up, Down, Home, End and Delete.
;
; te_tx_keys reads int 16h directly inside the full-screen bracket and calls
; this with the same AX, so one table serves both screens.
;
; **ELEVEN OF THESE KEYS NEVER REACH THIS PACKAGE ON A MACHINE WITH NO MOUSE.**
; SPEC.md 9.6's keyboard-mouse takes Home, Up, PgUp, Left, Right, End, Down and
; PgDn for pointer movement and Ins, Del and Space for the buttons whenever
; [mou_ptr] is 0. ScrollLock is the escape hatch and it is the only one, so the
; About panel says so (SPEC.md 70.10.2, te_ab5).
; =============================================================================
; --- the extended-key table: scan, length, then the bytes -------------------
; ANSI-BBS conventions rather than the VT's - `ESC [ K` for End and `ESC [ V` /
; `ESC [ U` for the page keys are what a DOOR game reads, and every board's own
; help screen names them.
te_keytab:
    db 0x48, 3, 0x1B, '[', 'A'      ; Up
    db 0x50, 3, 0x1B, '[', 'B'      ; Down
    db 0x4D, 3, 0x1B, '[', 'C'      ; Right
    db 0x4B, 3, 0x1B, '[', 'D'      ; Left
    db 0x47, 3, 0x1B, '[', 'H'      ; Home
    db 0x4F, 3, 0x1B, '[', 'K'      ; End
    db 0x49, 3, 0x1B, '[', 'V'      ; PgUp
    db 0x51, 3, 0x1B, '[', 'U'      ; PgDn
    db 0x52, 3, 0x1B, '[', '@'      ; Ins
    db 0x53, 1, 0x7F                ; Del
    db 0x3B, 3, 0x1B, 'O', 'P'      ; F1
    db 0x3C, 3, 0x1B, 'O', 'Q'      ; F2
    db 0x3D, 3, 0x1B, 'O', 'R'      ; F3
    db 0x3E, 3, 0x1B, 'O', 'S'      ; F4
    db 0x3F, 5, 0x1B, '[', '1', '5', '~'    ; F5
    db 0x40, 5, 0x1B, '[', '1', '7', '~'    ; F6
    db 0x41, 5, 0x1B, '[', '1', '8', '~'    ; F7
    db 0x42, 5, 0x1B, '[', '1', '9', '~'    ; F8
    db 0x43, 5, 0x1B, '[', '2', '0', '~'    ; F9
    db 0x44, 5, 0x1B, '[', '2', '1', '~'    ; F10
    db 0x85, 5, 0x1B, '[', '2', '3', '~'    ; F11 - and it MAY NEVER ARRIVE:
    db 0x86, 5, 0x1B, '[', '2', '4', '~'    ; F12   the kernel polls int 16h
    db 0                                    ;       AH=01h/00h and never the
                                            ;       enhanced AH=10h/11h, so an
                                            ;       XT-class BIOS does not
                                            ;       surface 0x85/0x86. The rows
                                            ;       are here so a machine whose
                                            ;       BIOS does sends the right
                                            ;       thing; nothing depends on it
te_k_crlf:  db 13, 10
te_k_iac:   db IAC, IAC

; -----------------------------------------------------------------------------
; te_tx - queue AL (or AH's scan code, for the extended keys) for the worker
; in:  AL = ascii, AH = scan
;
; A RING WITH A DROP, not a block: the UI task must never wait for the wire.
; At 3,741 bytes a second a 256-byte queue is 68 ms of typing ahead of the
; cable, and a human who outruns that has an unresponsive machine either way -
; what they must not have is a frozen one. A key SEQUENCE goes in whole or not
; at all (te_enq), for the same reason a protocol reply does: half of
; `ESC [ 1 5 ~` on the wire is `15~` typed into a board's menu.
; -----------------------------------------------------------------------------
te_tx:
    push cx
    push si
    or al, al
    jz .ext
    cmp al, 13
    je .enter
    cmp al, IAC
    je .iac                         ; **AN OUTGOING 0xFF IS DOUBLED**, which is
                                    ; IAC IAC and is the same rule as the
                                    ; incoming half (SPEC.md 70.10.1)
    call te_txraw
    jmp short .out
.enter:                             ; **CR LF, which is RFC 854's requirement
    mov si, te_k_crlf               ; for an NVT** - or a bare CR when we have
    mov cx, 2                       ; agreed to TRANSMIT-BINARY, which is what
    test byte [te_obin], 1          ; BINARY means. The old code sent CR
    jz .send                        ; unconditionally under the comment "the LF
    mov cx, 1                       ; is the host's business", which every host
    jmp short .send                 ; tolerates and the RFC does not say
.iac:
    mov si, te_k_iac
    mov cx, 2
    jmp short .send
.ext:
    mov si, te_keytab
.scan:
    mov cl, [si]
    or cl, cl
    jz .out                         ; not a key this terminal sends: dropped,
    cmp cl, ah                      ; which is what a bare scan code with no
    je .found                       ; meaning to a board deserves
    mov cl, [si+1]
    xor ch, ch
    add si, cx
    add si, 2
    jmp short .scan
.found:
    mov cl, [si+1]
    xor ch, ch
    add si, 2
.send:
    call te_enq
.out:
    pop si
    pop cx
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
; te_enq - queue CX bytes at DS:SI, WHOLE OR NOT AT ALL (SPEC.md 70.10.3)
; out: CF=1 and nothing enqueued
;
; A KEYSTROKE IS ONE BYTE AND A FULL RING DROPS IT, which SPEC.md 70.2
; justified and which is still right. **A PROTOCOL REPLY IS NOT ONE BYTE AND
; MUST NOT BE CUT**: half of an `SB TTYPE IS "ANSI" SE` on the wire is a
; subnegotiation the host waits for the end of, and half of a Zmodem header is
; a header the sender NAKs for ever.
;
; The space is tested and every byte copied inside ONE critical section, so the
; ring cannot fill between the test and the copy - and [te_txw]'s two writers
; (te_onkey on the UI task, the worker's replies) are why there is one at all.
; -----------------------------------------------------------------------------
te_enq:
    push ax
    push bx
    push cx
    push dx
    push si
    pushf
    cli
    mov bx, [te_txr]
    sub bx, [te_txw]
    dec bx
    and bx, TE_TX-1                 ; BX = the free bytes
    xor dx, dx                      ; DX = 0 taken, 1 refused
    cmp bx, cx
    jae .copy
    inc dx
    jmp short .done
.copy:
    mov bx, [te_txw]
    jcxz .store
.byte:
    mov al, [si]
    inc si
    mov [te_txb + bx], al
    inc bx
    and bx, TE_TX-1
    loop .byte
.store:
    mov [te_txw], bx
.done:
    popf                            ; ...and the answer is carried in DX, not
    or dx, dx                       ; in CF: popf restores the flags this proc
    jnz .full                       ; was entered with (SPEC.md 1's critical
    pop si                          ; section is pushf/cli...popf, never
    pop dx                          ; cli/sti)
    pop cx
    pop bx
    pop ax
    clc
    ret
.full:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; te_reply - a PROTOCOL message: enqueued whole, or held and retried
; in:  DS:SI = the bytes, CX = the length (at most TE_PND); THE WORKER'S TASK
;
; **ONE DEEP IS ENOUGH BECAUSE THE WORKER STOPS CONSUMING WHILE IT IS
; OCCUPIED** (SPEC.md 70.10.3): a pass that could not finish its receive buffer
; issues no new NETV_RECV, so there is never a second message to compose while
; the first is pending. TCP's own window holds the sender, and no byte is
; dropped anywhere in the chain.
;
; te_pnd is the WORKER's alone - te_onkey never writes it - so the one-deep
; slot has one writer and needs no critical section of its own.
; -----------------------------------------------------------------------------
te_reply:
    call te_enq
    jnc .out
    push ax
    push bx
    push cx
    push si
    cmp cx, TE_PND
    ja .drop                        ; longer than the slot: cannot happen, and
                                    ; a silent truncation is what this whole
                                    ; routine exists to prevent
    cmp byte [te_pndn], 0
    jne .drop
    xor bx, bx
.cp:
    mov al, [si]
    mov [te_pnd + bx], al
    inc si
    inc bx
    loop .cp
    mov [te_pndn], bl
.drop:
    pop si
    pop cx
    pop bx
    pop ax
.out:
    ret

; --- te_retry - the held reply, at the TOP of the next worker pass -----------
te_retry:
    push cx
    push si
    cmp byte [te_pndn], 0
    je .out
    mov cl, [te_pndn]
    xor ch, ch
    mov si, te_pnd
    call te_enq
    jc .out
    mov byte [te_pndn], 0
.out:
    pop si
    pop cx
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
    call te_reset
    call con_clear
    call te_promise                 ; AT THE TRANSITION and unconditionally
                                    ; (SPEC.md 70.7): con_clear has just
                                    ; marked every row, so this withdraws here
                                    ; rather than a tick later in te_show. The
                                    ; click handler holds the lock, which is
                                    ; rule 7's condition
    jmp short .out
.down:
    mov byte [te_want], 1           ; ...ASKED, not done: closing is a wire
.out:                               ; command and the worker owns the wire
    pop ax
    ret

; -----------------------------------------------------------------------------
; te_reset - a NEW SESSION starts with none of the last one's state
;
; Every byte here is negotiated, learned or half-parsed, and carrying any of it
; across a connect is a session drawing against the previous host's idea of
; itself: a BINARY agreement the new host never made would send Enter as a bare
; CR (SPEC.md 70.10.2), a parser left mid-CSI would swallow the new host's
; first sequence, and a Zmodem handover that never completed would stop the new
; session before its first byte. The SCREEN is con_clear's, next door.
; -----------------------------------------------------------------------------
; **ONE `rep stosb` AND FOUR STORES**, which is why the parser's whole state is
; one contiguous run of bss ([te_pst] to [te_rxn]) rather than bytes scattered
; where each was convenient. Twenty separate `mov byte [x], 0` is sixty bytes
; of a package on a floppy that has two clusters left, and a state byte added
; later that nobody remembered to add here is a session that starts dirty -
; which is the failure this shape makes impossible rather than unlikely.
te_reset:
    push ax
    push cx
    push di
    push es
    push ds
    pop es                          ; ...ES is the KERNEL's on a callback
    cld
    mov di, te_pst
    mov cx, TE_PSTATE
    xor al, al
    rep stosb
    mov byte [con_cvis], 1           ; ...the one byte of it that is not zero
    mov byte [te_lfg], 0x07         ; ...SGR 0's state, through the one routine
    call te_derive                  ; that owns the derivation
    pop es
    pop di
    pop cx
    pop ax
    ret
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
    ; ...AND THE REGION CANNOT MOVE WITHOUT THIS (SPEC.md 66.6.2): the
    ; kernel wrote our segment into this worker's frame before its
    ; first instruction, so mem_frameless pins a region with an
    ; undeclared worker however that region is declared. What a restart
    ; costs is one pass of the loop - the park is inside
    ; OSAPI_TASK_ALIVE and nowhere else (this package is not
    ; OSAPI_MEM_PARKSAFE), which is the TOP of the loop, and every byte
    ; that outlives a pass is a static and moves with us.
    OS88_WORKER_RESTARTABLE te_worker
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
    jmp .fail
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
    call te_retry                   ; **THE HELD REPLY FIRST, before anything
                                    ; else is enqueued** (SPEC.md 70.10.3)
    call te_flushtx
    jc .busyq
    cmp byte [te_pndn], 0
    jne .done                       ; still owed: this pass consumes NOTHING
                                    ; new, so there is never a second message
                                    ; to compose while the first is pending and
                                    ; TCP's own window holds the sender
    cmp byte [te_zon], 0
    je .rx
    call tz_poll                    ; SPEC.md 70.9.6's handover: the stream is
                                    ; the Zmodem receiver's from [te_rxi] on.
                                    ; tz_poll is everything that must happen
                                    ; WITHOUT a byte arriving - the timeouts,
                                    ; the UI task's answer and a header owed
                                    ; behind a commit - which is exactly the set
                                    ; a byte-driven machine cannot do for itself
    cmp byte [te_zon], 0
    je .rx                          ; ...the transfer ended in there
    call tz_canrx
    jc .done                        ; **BOTH STAGING HALVES ARE SPOKEN FOR**, so
                                    ; no NETV_RECV is issued and TCP's own window
                                    ; holds the sender (SPEC.md 70.11.3)
.rx:
    mov ax, [te_rxi]
    cmp ax, [te_rxn]
    jb .drain                       ; last pass could not finish the buffer
    mov al, [te_hnd]
    mov di, te_rx
    mov cx, TE_RX
    mov bh, NET_CLASS
    mov bl, NETV_RECV
    call OSAPI_DRV_CALL
    jc .busyq
    or cx, cx
    jz .empty
    mov [te_rxn], cx
    mov word [te_rxi], 0
.drain:
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
; the SCROLL ([con_scrl], pixels the buffer has already moved) and the ROWS.
; te_step used to ask about [te_dirty] alone and te_feed set it for arriving
; TEXT, which is how a character ended up redrawing the status line and the
; button as well as all eighteen rows.
te_owed:
    push ax
    cmp byte [te_dirty], 0
    jne .yes
    cmp word [con_scrl], 0
    jne .yes
    mov ax, [con_drb]
    or ax, [con_drb + 2]
    jz .no
.yes:
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; te_promise - "the glass matches the buffer" / "it does not"
; in:  the debt words; THE GFX LOCK HELD BY THE CALLER
; out: nothing (every register and the flags preserved)
;
; SPEC.md 70.7, which is SPEC.md 11.96.1's promise answered per DEBT rather
; than per session.
;
; te_show tests OSAPI_WM_OBSCURED and SKIPS THE DRAW when the answer is yes,
; which is 11.96.1's disqualifier word for word - so a raise cache banked
; while text was arriving would put back a terminal missing whatever came in
; after it. The obvious fix is the browser's (71.11): promise while the
; session is not up. It is also the wrong one HERE, because a telnet window
; spends its life connected and sitting at a prompt - the state that would
; never hold a cache is the state it is almost always in.
;
; te_owed already answers the finer question. Three debts and one test: the
; CHROME ([te_dirty]), the SCROLL ([con_scrl]) and the ROWS ([con_drb], four
; bytes and twenty-five bits since SPEC.md 70.8.1). Nothing owed means the
; buffer and the glass agree, and a cache
; taken then is exactly what a repaint would draw - whether the session is up,
; down or was never dialled.
;
; So it is called at both edges of te_show's lock hold: on the way in, where
; te_owed is true by construction and this WITHDRAWS (taking the cache with
; it, SPEC.md 11.96), and on the way out of a draw that actually happened,
; where it grants again. The obscured, foreign-text-mode and credits paths
; leave through .un instead and the promise stays withdrawn, which is right:
; the debt is still owed and only a paint can settle it.
;
; WHAT IT DOES NOT CLOSE is one worker pass. te_feed writes the buffer with no
; lock held (rule 3 - the wire is 274 ms and the cursor may not stop for it),
; so between the byte landing and te_show taking the lock there is a window in
; which a raise restores the frame before it. The debt survives that raise,
; so the next pass draws it: the cost is up to one tick of a stale line
; against a whole 18-row repaint - 1,152 glyph cells, about a second on the
; machine this is for - every single time the window is uncovered. Closing it
; needs the withdrawal at te_feed, and rule 7 forbids a worker touching this
; without the lock.
;
; **THE DEPTH CLAIM IS NOT TWO COLOURS ANY MORE** (SPEC.md 70.8.5). It was:
; CBLACK and CWHITE, four uses each and nothing else, so OSAPI_SAVEU_1BPP rode
; along and the cache was 11,642 bytes rather than 46,526. After 70.8 the
; content is whatever sixteen colours the board chose, and 11.96.17's promise
; - every pixel of my content is colour 0 or colour 15 - is FALSE on a colour
; screen. So the flag goes per ADAPTER, and per CALL rather than once at
; launch, because 11.96.17 re-states the claim on every wm_saveu and a window
; can move to a display of another depth (39.18.2). What it costs on VGA is a
; cache over wm_su_kb for a window this size: the promise is then refused, the
; window is not banked, and the raise repaints. That is the price of colour.
; -----------------------------------------------------------------------------
te_promise:
    pushf
    push ax
    push bx
    mov bx, [te_win]
    or bx, bx                       ; before wm_create, and after a refused
    jz .out                         ; one: nothing to promise about
    call te_owed
    mov al, 0                       ; something is owed: withdraw
    jc .say
    mov al, OSAPI_SAVEU_ON
    cmp byte [con_mono], 0
    je .say                         ; colour: 1bpp would be a LIE (70.8.5)
    mov al, OSAPI_SAVEU_ON | OSAPI_SAVEU_1BPP
.say:
    call OSAPI_WM_SAVEU             ; BX is [te_win] and not whatever was in it
.out:                               ; (SPEC.md 11.96.11.4)
    pop bx
    pop ax
    popf
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
    mov cx, [te_txw]                ; **ONCE** (the w3 review's MINOR 8). It was
    mov bx, [te_txr]                ; read twice, by the compare and by the
    cmp bx, cx                      ; length, with the UI task writing it in
    je .none                        ; between; every interleave happened to give
    mov si, te_txb                  ; a valid run, and that proof does not
    add si, bx                      ; survive a change to the wrap arm below
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
; te_feed - the receive QUEUE, drained a byte at a time (SPEC.md 70.10.3)
;
; It was a BATCH - CX bytes, all of them, no way to stop. It is a queue now
; ([te_rxn] bytes at te_rx with [te_rxi] taken) for one reason: a pass that
; could not finish the buffer must issue no new NETV_RECV. Two things stop a
; pass, and both need the rest of the bytes still to be here next time:
;
;   * a protocol reply that would not fit the transmit ring (te_reply held it,
;     and it is retried at the top of the next pass), and
;   * SPEC.md 70.9.6's Zmodem handover, where everything from [te_rxi] on
;     belongs to the receiver and the parser is fed nothing more.
;
; The index is advanced BEFORE the byte acts, which is what makes [te_soff] -
; and so [te_zat] - the offset JUST PAST the byte, matching ansisim's `feed()`
; return exactly (SPEC.md 70.12).
; -----------------------------------------------------------------------------
te_feed:
    push ax
    push si
.next:
    mov si, [te_rxi]
    cmp si, [te_rxn]
    jae .out
    cmp byte [te_zon], 0
    je .take
    call tz_canrx
    jc .out                         ; the staging area is full: the bytes stay
.take:                              ; in te_rx and are taken next pass
    mov al, [te_rx + si]
    inc si
    mov [te_rxi], si
    call te_byte
    cmp byte [te_pndn], 0
    je .next                        ; ...and a held reply stops the pass here
.out:
    pop si                          ; ...and NOT [te_dirty]: con_putc marked the
    pop ax                          ; rows it changed, and the chrome did not
    ret

; =============================================================================
; te_byte - one byte through the TELNET OPTION LAYER (SPEC.md 70.10.1)
;
; SPEC.md 70.1's *"refusing every Telnet option is a valid implementation"* was
; true and was right while nothing this drew depended on the host's idea of the
; terminal. **IT STOPPED BEING RIGHT WHEN THE TERMINAL BECAME AN 80x25 ANSI
; SCREEN**: a board asks what the terminal is and draws a different screen for
; the answer, and a board that is not told sends the line-oriented fallback.
;
; Five options are answered and everything else keeps 70.1's mirror. **IAC IAC
; is one literal 0xFF in the application stream in BOTH directions, and that is
; not cosmetic**: Zmodem sends binary and binary is full of 0xFF, so a terminal
; that forgets to halve an incoming pair corrupts every download with one in it
; - while agreeing perfectly with a sender that also forgets, which is why that
; defect survives a test written by one author.
; =============================================================================
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
    cmp bl, TP_SBOPT
    je .sbopt
    cmp bl, TP_SBODY
    je .sbody
    cmp bl, TP_SBIAC
    je .sbiac
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
    mov byte [te_ph], TP_SBOPT
    jmp .out
.want:
    mov [te_verb], al               ; DO/DONT/WILL/WONT: the option follows
    mov byte [te_ph], 2
    jmp .out
.optn:
    mov byte [te_ph], 0
    call te_option
    jmp .out
.sbopt:
    cmp al, IAC
    je .cmd                         ; **A WAY OUT** (the w3 review's MINOR 3):
                                    ; `IAC SB IAC SE` is a degenerate or
                                    ; truncated subnegotiation, and reading its
                                    ; IAC as option 255 swallowed every byte of
                                    ; the rest of the session with nothing but
                                    ; `^]` to recover it. teansi.inc's MUSIC and
                                    ; STRING swallows both abort on CAN and SUB
                                    ; and say why; this one sits ABOVE them and
                                    ; can swallow just as much. [te_ph] is 1 on
                                    ; this path - .cmd is the arm that phase
                                    ; names - so the byte is simply handed on,
                                    ; and `IAC SB IAC IAC` still takes a literal
                                    ; option 255
    mov [te_sbopt], al              ; WHICH subnegotiation, and how far in we
    mov byte [te_sbn], 0            ; are: `SB TTYPE SEND` is the option, then
    mov byte [te_sbsnd], 0          ; a single 1
    mov byte [te_ph], TP_SBODY
    jmp .out
.sbody:
    cmp al, IAC
    je .sbesc                       ; **AND IT IS AN IAC THAT ENDS ONE**, not a
    cmp byte [te_sbn], 0            ; bare 240: a subnegotiation's body is
    jne .sbmore                     ; binary and 0xF0 is an ordinary byte in it
    cmp byte [te_sbopt], TO_TTYPE
    jne .sbmore
    cmp al, 1                       ; TTYPE SEND
    jne .sbmore
    mov byte [te_sbsnd], 1
.sbmore:
    inc byte [te_sbn]
    cmp byte [te_sbn], TE_SBMAX
    jb .out
    mov byte [te_ph], 0             ; ...**AND A BOUND**. No option this terminal
    jmp .out                        ; answers sends a body this long, so a
                                    ; runaway one is by definition a stream that
                                    ; has gone wrong - and abandoning it puts the
                                    ; screen back rather than eating the session
.sbesc:
    mov byte [te_ph], TP_SBIAC
    jmp .out
.sbiac:
    cmp al, T_SE
    je .sbend
    mov byte [te_ph], TP_SBODY      ; IAC IAC inside a body, or a command this
    inc byte [te_sbn]               ; does not act on: back to swallowing - and
    jmp .out                        ; **THE ESCAPED IAC IS COUNTED** (the w3
                                    ; review's MINOR 4). Without it a body whose
                                    ; first byte is a literal 0xFF left the
                                    ; counter at 0 and the SECOND body byte was
                                    ; tested as the first, so
                                    ; `SB TTYPE IAC IAC 1 IAC SE` answered with a
                                    ; terminal type nobody asked for
.sbend:
    mov byte [te_ph], 0
    cmp byte [te_sbsnd], 0
    je .out
    mov byte [te_sbsnd], 0
    call te_say_ttype
    jmp .out
.text:
    cmp byte [te_zon], 0
    jne .zm
    call te_pbyte                   ; ...and the APPLICATION byte goes to the
    jmp short .out                  ; parser (SPEC.md 70.9)
.zm:
    call tz_byte                    ; ...or to the Zmodem receiver, which is
                                    ; BELOW this layer for a reason that is not
                                    ; cosmetic: a data subpacket contains every
                                    ; byte value, and a receiver reading IAC IAC
                                    ; as two 0xFFs corrupts every download with
                                    ; one in it (SPEC.md 70.11)
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; te_option - answer AL's option, [te_verb] being the DO/DONT/WILL/WONT
;
; **THE REPLY'S SENSE IS STILL THE MIRROR OF THE QUESTION** for everything not
; in the table: a host that WILL is told DONT, a host that asks us to DO is told
; WONT. Getting that backwards is an option loop - two ends each answering the
; other for ever - and it is the one way a Telnet client can wedge a connection
; that is working perfectly.
; -----------------------------------------------------------------------------
te_option:
    push ax
    push bx
    push cx
    push si
    mov bl, al                      ; BL = the option
    mov al, [te_verb]
    cmp al, T_DO
    je .do
    cmp al, T_DONT
    je .dont
    cmp al, T_WILL
    je .will
                                    ; WONT
    cmp bl, TO_BINARY
    jne .mirror_dont
    and byte [te_obin], 0xFD          ; they will NOT send 8-bit clean
    jmp short .mirror_dont
.do:
    cmp bl, TO_TTYPE
    je .willit
    cmp bl, TO_SGA
    je .willit
    cmp bl, TO_NAWS
    je .naws
    cmp bl, TO_BINARY
    jne .mirror_wont
    or byte [te_obin], 1            ; **WE** send 8-bit clean, so Enter is a
    jmp short .willit               ; bare CR from here on (SPEC.md 70.10.2)
.dont:
    cmp bl, TO_BINARY
    jne .mirror_wont
    and byte [te_obin], 0xFE
    jmp short .mirror_wont
.will:
    cmp bl, TO_ECHO
    je .doit                        ; the host echoes; this draws what comes
    cmp bl, TO_SGA                  ; back, which is what a board needs
    je .doit
    cmp bl, TO_BINARY
    jne .mirror_dont
    or byte [te_obin], 2            ; **THEY** send 8-bit clean
    jmp short .doit
.naws:
    ; **ONE MESSAGE OF TWELVE BYTES AND NOT TWO**, which is the only arm here
    ; that answers a single received byte with two replies - and te_pnd is ONE
    ; DEEP (SPEC.md 70.10.3). Sent as two, a full ring would hold the `WILL`
    ; and DROP the size behind it, leaving the host told this terminal has a
    ; window and never told how big. The ring is 256 bytes and empty when a
    ; board negotiates, so that is a hole nothing could reach; it is closed by
    ; construction rather than by luck.
    mov si, te_will_naws            ; ...and the size, unasked: a client that
    mov cx, 12                      ; reported a NARROW window would have the
    call te_reply                   ; host wrap its lines where the buffer is
    jmp short .out                  ; not going to wrap them, and every line of
.willit:                            ; art after the first would be in the wrong
    mov al, T_WILL                  ; place. NAWS reports the BUFFER, always
    jmp short .say                  ; 80x25 (SPEC.md 70.8.10)
.doit:
    mov al, T_DO
    jmp short .say
.mirror_wont:
    mov al, T_WONT
    jmp short .say
.mirror_dont:
    mov al, T_DONT
.say:
    call te_say3
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- te_say3 - IAC, AL, BL: one three-byte option reply, whole --------------
te_say3:
    push cx
    push si
    mov byte [te_say + 0], IAC
    mov [te_say + 1], al
    mov [te_say + 2], bl
    mov si, te_say
    mov cx, 3
    call te_reply
    pop si
    pop cx
    ret

; --- te_say_ttype - `SB TTYPE IS "ANSI" SE`, ten bytes on the wire ----------
te_say_ttype:
    push cx
    push si
    mov si, te_sb_ttype
    mov cx, 10
    call te_reply
    pop si
    pop cx
    ret

te_sb_ttype:  db IAC, T_SB, TO_TTYPE, 0, 'A', 'N', 'S', 'I', IAC, T_SE
te_will_naws: db IAC, T_WILL, TO_NAWS
              db IAC, T_SB, TO_NAWS, 0, CON_COLS, 0, CON_ROWS, IAC, T_SE
                                    ; ...and neither 80 nor 25 is 0xFF, so
                                    ; nothing in this message needs doubling
te_show:
    push ax
    push bx
    push si
    cmp byte [te_txm], 0
    jne .gone                       ; a foreign text mode is up: the bracket
                                    ; draws, and the LOCK IS ITS (70.8.8)
    call OSAPI_GFX_LOCK
    call te_promise                 ; te_owed is TRUE by construction here (it
                                    ; is what got us called), so this WITHDRAWS
                                    ; and the raise cache goes with it - before
                                    ; the obscured test below, because the
                                    ; covered case is exactly the one where the
                                    ; debt is not about to be settled
    mov bx, [te_win]
    call OSAPI_WM_OBSCURED
    jc .un                          ; covered: the next paint owes it, and a
                                    ; background painter that drew anyway
                                    ; would paint over the window on top
                                    ; (SPEC.md 11.3)
    cmp byte [te_abon], 0
    jne .un                         ; the credits are up: the worker may not
                                    ; draw over them, and the click that
                                    ; dismisses them repaints everything
    mov si, [te_win]
    call te_layout
    cmp byte [tz_pan], 0
    je .term
    call tz_panel                   ; SPEC.md 70.11.5's takeover: nothing has to
    jmp short .chrome               ; be redrawn to show a number changing
.term:
    call con_scrollpaint             ; ...the pixels the buffer already moved
    call con_rows_owed               ; ...and ONLY the rows that changed
.chrome:
    cmp byte [te_dirty], 0
    je .done                        ; the CHROME is a separate question: a
    call te_status                  ; character arriving is not a state change
    call te_button
    mov byte [te_dirty], 0          ; ...and SPENT, so te_owed can be read as
                                    ; the whole truth (te_promise). te_step
.done:                              ; zeroes it every pass anyway, so this
    call te_promise                 ; changes nothing for the state machine
                                    ; ...and the glass matches the buffer
.un:                                ; again: promise
    call OSAPI_GFX_UNLOCK
.gone:
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
    call con_wpx                     ; nothing here covers them
    mov cx, [con_px]
    add cx, ax
    dec cx
    mov ax, [con_px]
    mov bx, [con_oy]
    add bx, TE_TOPY
    mov dx, [con_oy]
    add dx, [te_chh]
    sub dx, TE_STATH + 1            ; ...and NOT over the status line, which
    call OSAPI_GFX_FILL             ; te_abdismiss does not redraw
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, te_ablines
    mov dx, [con_oy]
    add dx, TE_TOPY + 12
.line:
    mov cx, [si]
    or cx, cx
    jz .done
    push si
    mov si, cx
    mov cx, [con_px]
    add cx, 16
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the box this routine
    call OSAPI_FONT_RUN             ; filled white just above
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
    dw te_ab1, te_ab2, te_ab2b, te_ab3, te_ab4, te_ab5, te_ab6, 0
te_ab1:     db 'Telnet for os8088 - an ANSI-BBS terminal', 0
te_ab2:     db 'RFC 854 over the socket API, 80x25, colour', 0
te_ab2b:    db 'Zmodem receive, CRC-16, to the Save dialog', 0
te_ab3:     db 0                    ; a blank line is a line with no glyphs
te_ab4:     db 'A board needs the arrow keys, and with no', 0
te_ab5:     db 'mouse SCROLL LOCK is what gives them back.', 0
                                    ; **SPEC.md 9.6's keyboard-mouse takes
                                    ; eleven of SPEC.md 70.10.2's keys** - the
                                    ; arrows, Home, End, PgUp, PgDn, Ins, Del
                                    ; and Space - whenever [mou_ptr] is 0, and
                                    ; kbm_slock is the ONLY escape hatch. A
                                    ; board is unusable without them, so this
                                    ; is on the panel rather than in a document
                                    ; nobody has beside the machine
te_ab6:     db 'Contributed by Elendilon', 0

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
        OS88_MENU te_m_sess, te_i_sess, 5
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
    jne .i2
    call con_clear
    jmp short .rp
.i2:
    cmp al, 2
    jne .i3
    call te_ice_flip
    jmp short .rp
.i3:
    cmp al, 4
    jne .fs
    call tz_ask                     ; ASKED, not done: the wire is the worker's
    jmp short .rp                   ; (SPEC.md 70.2) and te_pnd has one writer
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

; -----------------------------------------------------------------------------
; te_ice_flip - iCE COLOURS (SPEC.md 70.8.9)
;
; Bit 7 of an attribute is BLINK, which is what the adapter powers up doing
; and what a board that uses blink expects; a board that uses BRIGHT
; BACKGROUNDS instead is drawing against a terminal the user has told it
; about, which is what a toggle is. OFF by default, therefore.
;
; The label is the state, because the kernel's pull-down has no check mark
; (os88api.inc publishes MENU_DIS and nothing else) and Solitaire's precedent
; is a second string and a pointer swap. **On Hercules the item is GREYED with
; the reason on it** - SPEC.md 47's rule, grey a FACT: MDA text is monochrome
; and has no background colour to make bright, so there is nothing for the
; toggle to do and saying so is better than a toggle that does nothing.
; -----------------------------------------------------------------------------
te_ice_flip:
    push ax
    cmp byte [con_mono], 0
    jne .relabel                    ; ...and the greyed item is unreachable by
    mov al, [con_ice]                ; the mouse anyway; this is the belt
    xor al, 1
    mov [con_ice], al
    call con_markall                 ; every cell's polarity may have moved
.relabel:
    call te_ice_label
    pop ax
    ret

; --- te_ice_label - point the item at the string that is true right now -----
te_ice_label:
    push ax
    mov ax, te_it_ice0
    cmp byte [con_mono], 0
    jne .set                        ; 1bpp: there IS no bright background
    mov ax, te_it_ice
    cmp byte [con_ice], 0
    je .set
    mov ax, te_it_icy
.set:
    mov [te_i_sess + 2*2], ax
    pop ax
    ret

te_name_s:  db 'Telnet', 0
te_m_sess:  db 'Session', 0
te_i_sess:  dw te_it_conn, te_it_clr, te_it_ice, te_it_fs, tz_it_rx
te_it_conn: db 'Connect / Close', 0
te_it_clr:  db 'Clear Screen', 0
te_it_ice:  db 'iCE Colours', 0
te_it_icy:  db 'iCE Colours (on)', 0
te_it_ice0: db MENU_DIS, 'iCE Colours (Mono)', 0
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

%include "os88alt.inc"          ; SPEC.md 11.2.1.1's edge, for the bracket
%include "os88ui.inc"
%include "os88line.inc"
%include "os88sock.inc"         ; net_find (SPEC.md 72, SPEC.md 20.11.1)
%define CON_FSX                     ; ...and its FULL-SCREEN renderer (70.8.13):
                                ; the same buffer onto real text VRAM, which
                                ; tetxt.inc's bracket drives
%include "os88con.inc"          ; THE 80x25 CONSOLE (SPEC.md 70.8), which was
                                ; this package's until SPEC.md 96.32's DOS box
                                ; wanted the same screen. It carries the CP437
                                ; face and its own bss, so [te_win] and the
                                ; rest of ours chain off CON_BSS below
%include "teansi.inc"           ; THE ANSI-BBS PARSER (SPEC.md 70.9), whose
                                ; second reader is tools/ansisim.py and whose
                                ; gate is tests/telansi.py
%include "tetxt.inc"            ; ...and the text-mode screen (SPEC.md 70.6)
%include "tezm.inc"             ; THE ZMODEM RECEIVER (SPEC.md 70.11), whose
                                ; other end is tools/os88bbs.py's sender and
                                ; whose gate is tests/telzm.py

    OS88_BSS TE_BSS
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) ----------------------------------
; **OURS STARTS AT os88_image_end + CON_BSS**, because os88con.inc's chain takes
; the first CON_BSS bytes of it - the screen buffer, the face, the dirty bitmap
; and the geometry this package used to declare itself. Nothing else changes:
; TE_BSS is still measured from os88_image_end to the foot of this chain, so it
; carries the console's bytes without a second term (apps/os88parts.inc's shape
; and for its reason).
;
; ...and a CHAIN rather than literal offsets, which is what made the extraction
; a renumbering job: five words came out of the middle of the old one and every
; line below them had to be re-counted by hand.
te_win      equ os88_image_end + CON_BSS
te_ox       equ te_win + 2           ; the content origin's left...
te_cw       equ te_ox + 2            ; ...and the content box, which THIS
te_chh      equ te_cw + 2            ; package's chrome is laid out against -
                                     ; os88con.inc is told [con_px] and
                                     ; [con_oy] and asks for nothing else
te_txr      equ te_chh + 2
te_txw      equ te_txr + 2
te_port     equ te_txw + 2
te_msg      equ te_port + 2          ; -> the failure text, for TS_ERR
te_state    equ te_msg + 2
te_spawned  equ te_state + 1
te_hnd      equ te_spawned + 1
te_want     equ te_hnd + 1           ; the user asked to close
te_dirty    equ te_want + 1
te_btn      equ te_dirty + 1         ; 8: the Connect button's rect
te_line     equ te_btn + 8           ; OS88LINE_SZ
te_hbuf     equ te_line + OS88LINE_SZ ; TE_HOSTMAX: what the user typed
te_host     equ te_hbuf + TE_HOSTMAX  ; ...and the half before the colon
te_sline    equ te_host + TE_HOSTMAX  ; CON_COLS+1: the padded status field.
                                      ; It used to be con_rowfont's NUL run as
                                      ; well, on the argument that the two
                                      ; cannot overlap in time - true here and
                                      ; not a promise a library may make about
                                      ; a carrier, so [con_run] is its own now
te_txb      equ te_sline + CON_COLS+1  ; TE_TX
te_pnd      equ te_txb + TE_TX        ; TE_PND: the ONE refused reply, held
                                      ; whole and retried at the top of the
                                      ; next worker pass (SPEC.md 70.10.3)
te_say      equ te_pnd + TE_PND       ; 3: an option reply, composed before it
                                      ; is enqueued, because IAC/verb/option
                                      ; goes on the wire whole or not at all
te_ans      equ te_say + 3            ; TE_ANS: `ESC [ <row> ; <col> R`
te_rx       equ te_ans + TE_ANS       ; TE_RX
te_abon     equ te_rx + TE_RX        ; byte: the credits have the screen
te_txm      equ te_abon + 1           ; byte: a FOREIGN TEXT MODE is up, so
                                      ; every kernel drawing slot is off-limits
                                      ; (SPEC.md 53.1) and te_show must not
                                      ; ...and [con_tkind], [con_tcur],
                                      ; [con_thint] and [con_tseg] stood HERE
                                      ; until SPEC.md 70.8.13 took the
                                      ; full-screen renderer into
                                      ; apps/os88con.inc. They are its words
                                      ; now and its chain declares them; four
                                      ; dead equs left here would have SHADOWED
                                      ; them, which assembles perfectly and
                                      ; puts the bracket's framebuffer segment
                                      ; in one word while the renderer reads
                                      ; another - measured as [te_tseg] = 0000
                                      ; and 3,971 of 4,000 VRAM bytes wrong

; --- the parser's own state (SPEC.md 70.9), and every byte of it is HERE ----
; Not one byte of it lives in a local across a call, which is the property the
; whole design turns on: a stream split at any offset behaves like one that is
; not (SPEC.md 70.9, tools/ansisim.py's fragment sweep).
te_pst      equ te_txm + 1            ; byte: PS_*, the state
te_ph       equ te_pst + 1            ; byte: the IAC state machine's phase
te_verb     equ te_ph + 1             ; byte: ...and the DO/WILL it is answering
te_sbopt    equ te_verb + 1           ; byte: the option a subnegotiation is
                                      ; about
te_pn       equ te_sbopt + 1          ; byte: how many parameters are held
te_psink    equ te_pn + 1             ; byte: the ninth parameter onward goes
                                      ; NOWHERE - accumulating its digits into
                                      ; the eighth would set one of 3144
te_ppfx     equ te_psink + 1          ; byte: the private prefix, 0x3C..0x3F,
                                      ; and only the FIRST one is it
te_prm      equ te_ppfx + 1           ; TE_PMAX bytes, each clamped at 255
te_lfg      equ te_prm + TE_PMAX      ; byte: LOGICAL foreground, 4 bits
te_lbg      equ te_lfg + 1            ; byte: ...background, 3 bits
te_blk      equ te_lbg + 1            ; byte: SGR 5
te_rev      equ te_blk + 1            ; byte: SGR 7 - **A FLAG, and not a swap
                                      ; of the attribute's nibbles**: a parser
                                      ; that swapped when it saw the 7 and then
                                      ; wrote a 31 into the low nibble draws
                                      ; blue on white where `CSI 7;31m` means
                                      ; black on red (SPEC.md 70.9.4)
te_con      equ te_rev + 1            ; byte: SGR 8
te_soff     equ te_con + 1            ; word: the application STREAM offset,
                                      ; which is ansisim's `offset` and what
                                      ; [te_zat] is published in
te_zdet     equ te_soff + 2           ; byte: how much of `**\x18B00` matched
te_zon      equ te_zdet + 1           ; byte: the Zmodem receiver has the stream
te_zn       equ te_zon + 1            ; byte: how many times the trigger fired
te_zat      equ te_zn + 1             ; word: ...the offset it fired at
te_sbn      equ te_zat + 2            ; byte: bytes into a subnegotiation body
te_sbsnd    equ te_sbn + 1            ; byte: it was `SB TTYPE SEND`
te_obin     equ te_sbsnd + 1          ; byte: bit 0 = WE send 8-bit clean, bit 1
                                      ; = they do. TWO BITS AND NOT ONE, because
                                      ; the two directions are separate options
                                      ; in RFC 856 and a host may agree to one
                                      ; and refuse the other (SPEC.md 70.10.1)
te_pndn     equ te_obin + 1           ; byte: the held reply's length, 0 = none
te_rxi      equ te_pndn + 1           ; word: bytes of te_rx taken
te_rxn      equ te_rxi + 2            ; word: ...and bytes in it
te_sx       equ te_rxn + 2            ; word: the SAVED cursor - ESC 7 / ESC 8
te_sy       equ te_sx + 2             ; and CSI s / CSI u share this ONE slot,
                                      ; and it holds the POSITION and never the
                                      ; attribute (SPEC.md 70.9.3)
                                      ; ...and te_reset zeroes every byte from
                                      ; te_pst to the foot of the Zmodem block
                                      ; below in one `rep stosb`, which is why
                                      ; they are contiguous and why a byte added
                                      ; there needs no line adding here

; --- the Zmodem receiver's control block (SPEC.md 70.11) --------------------
; **INSIDE te_reset's RUN**, which is deliberate: a new session starts with none
; of the last one's transfer either, and a dialog left up by a Connect is
; answered by tz_dlgdone finding [tz_dlg] clear and doing nothing. tz_begin
; clears the same run with its own `rep stosb`, so a byte added here is covered
; twice by construction and neither place has a line to remember.
; ...and [con_scrbusy] used to sit HERE, inside te_reset's run - os88con.inc
; declares it now, and a session cannot start with it set anyway: con_scrollup
; opens and closes that transaction inside one call.
tz_st       equ te_sy + 2             ; byte: ZR_*, where the PROTOCOL is
tz_ps       equ tz_st + 1             ; byte: ZP_*, where the PARSER is inside a
                                      ; frame - two bytes because they answer
                                      ; two questions
tz_sk       equ tz_ps + 1             ; byte: TZS_*, where a decoded data byte
                                      ; goes
tz_esc      equ tz_sk + 1             ; byte: a ZDLE is held, awaiting its second
tz_fend     equ tz_esc + 1            ; byte: the frame-end terminator just seen
                                      ; (tz_end is the PROC that ends a
                                      ; transfer, and NASM has one namespace)
tz_pend     equ tz_fend + 1            ; byte: a hex header is parsed and waiting
                                      ; for its CR LF (SPEC.md 70.11.1)
tz_lst      equ tz_pend + 1           ; byte: the last header type we SENT, for
                                      ; the timeout's resend
tz_owe      equ tz_lst + 1            ; byte: 1 = a ZRINIT is owed, 2 = a ZRPOS -
                                      ; each waiting on a commit, because
                                      ; [tz_pos] is not final until one lands
tz_half     equ tz_owe + 1            ; byte: which staging half is filling
tz_dbl      equ tz_half + 1           ; byte: double-buffered (a cluster of
                                      ; 4,096 or less)
tz_made     equ tz_dbl + 1            ; byte: the file exists, so the next commit
                                      ; is an APPEND and not a WRITE
tz_req      equ tz_made + 1           ; byte: TZ_* - **THE WHOLE HANDSHAKE**,
                                      ; written last by the worker and cleared
                                      ; last by the UI task (SPEC.md 70.11.3)
tz_rst      equ tz_req + 1            ; byte: TZR_*, the UI task's answer
tz_uh       equ tz_rst + 1            ; byte: ...which half it is to write
tz_dlg      equ tz_uh + 1             ; byte: a Save dialog is up
tz_pcan     equ tz_dlg + 1            ; byte: a W_PAINT SUSPECTS a cancel, and
                                      ; tz_wake's next pass decides - the paint
                                      ; may not, because wm_destroy calls it
                                      ; INLINE on the way to the completion proc
                                      ; (SPEC.md 70.11.4)
tz_dslot    equ tz_pcan + 1           ; byte: the window slot the dialog took,
                                      ; 0xFF = not known. A tally of all twelve
                                      ; is a fact about the DESKTOP; one slot is
                                      ; a fact about the dialog
tz_base     equ tz_dslot + 1          ; word: the live-slot BITMAP before the
                                      ; open, so the new bit names the slot
tz_want     equ tz_base + 2            ; byte: the menu item asked; the WORKER
                                      ; starts the transfer
tz_pan      equ tz_want + 1           ; byte: the progress takeover has the
                                      ; terminal's area
tz_cans     equ tz_pan + 1            ; byte: `ZDLE ZDLE`s in a row - five is
                                      ; the sender's ABORT and not a lost place
tz_oo       equ tz_cans + 1           ; byte: `O`s of the sender's closing `OO`
tz_ferr     equ tz_oo + 1             ; byte: the FERR_* a commit was refused
                                      ; with, because `Disk error` is what all
                                      ; of them look like from outside
tz_try      equ tz_ferr + 1             ; byte: timeouts in a row
tz_dtry     equ tz_try + 1            ; byte: ...and refused dialogs in a row
tz_un       equ tz_dtry + 1            ; word: bytes in the half handed over
tz_fill     equ tz_un + 2             ; word: bytes in the half being filled
tz_chunk    equ tz_fill + 2           ; word: the commit size, a whole number of
                                      ; the DESTINATION's clusters (SPEC.md
                                      ; 18.4.4)
tz_clus     equ tz_chunk + 2          ; word: the DESTINATION's cluster in
                                      ; bytes, so a refusal names the FACT
                                      ; rather than the word "large"
                                      ; (SPEC.md 47)
tz_skip     equ tz_clus + 2          ; word: bytes the sender is re-sending
                                      ; over ones already committed, dropped as
                                      ; they arrive (SPEC.md 70.11.2)
tz_n        equ tz_skip + 2           ; word: HEADER bytes or nibbles, and a
                                      ; subpacket's two CRC bytes
tz_dn       equ tz_n + 2              ; word: ...and a subpacket's DATA bytes,
                                      ; which is a SECOND counter because
                                      ; `.term` resets the first one to count
                                      ; the CRC that follows - and the file
                                      ; name's length is wanted after that
tz_crc      equ tz_dn + 2              ; word: the running CRC-16
tz_tick     equ tz_crc + 2            ; word: when the last byte arrived
tz_dlgt     equ tz_tick + 2           ; word: ...when the dialog went up
tz_kick     equ tz_dlgt + 2           ; word: ...and when the UI was last kicked
tz_dot      equ tz_kick + 2           ; word: tz_mangle's last dot
tz_bw       equ tz_dot + 2            ; word: the progress bar's width
tz_bf       equ tz_bw + 2             ; word: ...and how much of it is filled
tz_why      equ tz_bf + 2             ; word: -> why a transfer stopped
tz_pos      equ tz_why + 2            ; dword: bytes the UI task has WRITTEN -
                                      ; the only offset a ZRPOS may name
tz_rcv      equ tz_pos + 4            ; dword: ...and bytes accepted into
                                      ; staging, which is what a ZACK names
tz_sub0     equ tz_rcv + 4            ; dword: where the subpacket now filling
                                      ; STARTED, which is what a ZRPOS names
                                      ; after a CRC error - a subpacket that
                                      ; straddled a chunk boundary has part of
                                      ; itself already on the disk, and there is
                                      ; no truncate verb to take it back
tz_fsz      equ tz_sub0 + 4            ; dword: the size the sender declared
tz_cb       equ tz_fsz + 4            ; 2: a subpacket's two CRC bytes
tz_hb       equ tz_cb + 2             ; 8: a header - type, four, and its CRC
TZ_CTL      equ (tz_hb + 8) - tz_st   ; ...and tz_begin zeroes the LOT

TE_PSTATE   equ (tz_hb + 8) - te_pst  ; ...as te_reset does, one `rep stosb`

te_fsi      equ tz_hb + 8             ; FSI_SIZE: what OSAPI_FSX_MODE filled.
                                      ; **IT USED TO BE te_rxn + 2, WHICH IS
                                      ; te_sx** - so a bracket's FSI_SEG and the
                                      ; cursor `ESC 7` saves were the same word.
                                      ; tetxt.inc copies FSI_SEG out at entry so
                                      ; nothing visible came of it, and a
                                      ; collision nothing came of is a collision
                                      ; waiting for the next reader of FSI_W
; --- THE ONE DIAGNOSTIC BYTE, and it is OUTSIDE both `rep stosb` runs -------
; A byte a reconnect zeroes cannot answer "did a reconnect happen", which is
; one of the questions it was asked.
tz_diag     equ te_fsi + FSI_SIZE     ; byte: WHAT HAPPENED TO THE DIALOG, one
                                      ; bit per step - 1 asked, 2 the slot
                                      ; refused, 4 a paint inferred a cancel,
                                      ; 8 the completion proc ran, 16 it went
                                      ; up, 32 the receiver asked for one. A
                                      ; cancelled dialog calls nothing back at
                                      ; all (SPEC.md 38.6), so the only way to
                                      ; tell those apart from outside is to
                                      ; write them down
tz_name     equ tz_diag + 1           ; 14: the 8.3 name, mangled then chosen
tz_msg      equ tz_name + 14          ; 48: the progress line
tz_bmsg     equ tz_msg + 48           ; 32: ...and a REFUSAL, which needs a
                                      ; buffer of its own because te_status
                                      ; rewrites the progress line on every draw
tz_dig      equ tz_bmsg + 32          ; 12: tz_num's digits, written backwards.
                                      ; **ONE TASK**: te_status calls tz_text
                                      ; and tz_dlgdone calls tz_bigmsg, and both
                                      ; are the UI task's. A worker-side caller
                                      ; would be the first thing to break that
tz_ob       equ tz_dig + 12           ; 24: one composed header, on its way out
tz_ohdr     equ tz_ob + 24            ; 4: ...and its four header bytes
tz_info     equ tz_ohdr + 4           ; TZ_INFOSZ: the ZFILE info block
tz_stg      equ tz_info + TZ_INFOSZ   ; TZ_STGSZ: **THE STAGING AREA, IN THE
                                      ; PACKAGE'S OWN SEGMENT** and not in a
                                      ; heap claim - OSAPI_DRV_CALL is an X stub
                                      ; and puts the CALLER's segment in ES, so
                                      ; a buffer anywhere else is read out of
                                      ; this package's own image instead
                                      ; (SPEC.md 77.2/70.11.3)
TE_BSS      equ (tz_stg - os88_image_end) + TZ_STGSZ

; --- and the one place a bss ORDER is load-bearing (teansi.inc's te_pclear) --
; That routine clears te_pn and te_psink with a single WORD store, so a reorder
; that separated them would clear somebody else's byte and silently leave the
; parameter sink set - which shows up as `CSI 0;1;1;1;1;1;1;31;44m` losing its
; 31 on the sequence AFTER the one that filled the eight, and nothing else.
%if te_psink != te_pn + 1 || te_ppfx != te_psink + 1
  %error "te_pclear stores a WORD over te_pn/te_psink: the three must be adjacent"
%endif

; --- ...and the SECOND bss-order fact, which cost a blocker to learn --------
; te_fsi named te_rxn + 2 and so did te_sx, so OSAPI_FSX_MODE's sixteen bytes
; landed on the saved cursor: [te_sx] became 0xB800 and the board's next
; `CSI u` sent con_putc 34,696 bytes past this package's claim. Nothing in the
; file said the two runs may not overlap, so nothing caught it - and the fix is
; an `equ` a future reorder can undo just as quietly.
%if te_fsi < te_pst + TE_PSTATE
  %error "te_fsi must lie ABOVE te_pst..the Zmodem block: te_reset zeroes that run in one rep stosb, and OSAPI_FSX_MODE writes FSI_SIZE bytes over whatever te_fsi names"
%endif

; --- ...AND THE BUFFERS, WHICH IS WHAT WAS ACTUALLY UNGUARDED ---------------
; The two assertions above restate their own definitions: `TE_PSTATE` and
; `te_fsi` are both derived from `tz_hb + 8`, so the first compares an
; expression with itself and fires only if somebody moves `te_fsi`'s `equ` -
; which is worth having, being the shape of the wave-3 blocker - and the second
; was a tautology that could never fire at all, so it is gone.
;
; What nothing tied down is the seven fixed-size buffers between `te_fsi` and
; `tz_stg`, each chained with a literal that no code references. All of them fit
; today and all of them are one caption or one separator from not fitting, and
; overrunning any of them writes into its neighbour with no symptom until the
; neighbour is read. These are the sizes the CODE needs, written where the
; sizes are declared.
TZ_NAMEMAX  equ 13                  ; tz_dlgdone copies 12 and terminates
TZ_MSGMAX   equ 44                  ; tz_text: 12 of name, 2, 13, 3, 13, NUL
TZ_BMSGMAX  equ 30                  ; tz_bigmsg: 8 + up to 13 + 8 + NUL, and
                                    ; [tz_clus] is a WORD so it is really 6
TZ_DIGMAX   equ 12                  ; tz_num: ten digits and a NUL, and the
                                    ; NUL goes at tz_dig + 11 - so the SPAN it
                                    ; needs is twelve, not eleven. Guarded at
                                    ; eleven, a future editor who shrank the
                                    ; buffer to eleven would pass and write the
                                    ; NUL into tz_ob, which is the exact failure
                                    ; the guard exists for
TZ_OBMAX    equ 21                  ; tz_hex: 4 + 2 + 8 + 4 + 2 + XON
%if tz_msg - tz_name < TZ_NAMEMAX
  %error "tz_name is smaller than the 8.3 name tz_dlgdone copies into it"
%endif
%if tz_bmsg - tz_msg < TZ_MSGMAX
  %error "tz_msg is smaller than tz_text's longest progress line"
%endif
%if tz_dig - tz_bmsg < TZ_BMSGMAX
  %error "tz_bmsg is smaller than tz_bigmsg's longest refusal"
%endif
%if tz_ob - tz_dig < TZ_DIGMAX
  %error "tz_dig is smaller than tz_num's ten digits and its NUL"
%endif
%if tz_ohdr - tz_ob < TZ_OBMAX
  %error "tz_ob is smaller than the 21-byte hex header tz_hex composes in it"
%endif
%if tz_stg - tz_info < TZ_INFOSZ
  %error "tz_info is smaller than TZ_INFOSZ, which is what tz_sink bounds against"
%endif
%if TE_SLINE + 1 > CON_COLS + 1
  %error "te_sline holds the status field: TE_SLINE cells plus a NUL"
%endif
