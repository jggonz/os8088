; =============================================================================
; os8088 - apps/telnet/tetxt.inc
;
; THE TERMINAL ON A TEXT SCREEN (SPEC.md 53.4/70.6/70.8.7)
;
; SPEC.md 53's exclusive bracket with FSXM_TEXT80, which is the one mode every
; adapter here can set - VGA, CGA and Hercules alike (fsx_caps 0x1EF/0x00F/
; 0x011, bit 0). Tracker's XT mode (SPEC.md 45.13) is the precedent and the
; argument is the same one, a size smaller: a character cell in text mode is
; ONE WORD STORE and the card's own ROM font does the rendering, where the
; windowed terminal composes a band and blits it.
;
; **THE BUFFER MAPS 1:1 ONTO TEXT VRAM** (SPEC.md 70.8.7). 80 columns by 25
; rows against 80 by 25, all of it, no centring and NO STATUS LINE: a board
; uses row 25, and a terminal that reserved it would cut the bottom off every
; menu. TET_X0 and TET_Y0 are gone; a row is at FSI_SEG:(r*160). And con_scr's
; cell IS the cell in VRAM - a character byte then an attribute byte - so a
; colour row is a `rep movsw` and not a translation. That is the whole
; argument for the bracket, one size smaller than SPEC.md 70.6 made it.
;
; FSXF_KEEPWORKER IS NOT OPTIONAL. The worker owns the socket (SPEC.md 70.2),
; so a bracket that froze it would freeze the session: no bytes would arrive
; and nothing would be sent for as long as the screen was up.
;
; **EVERY KERNEL DRAWING SLOT IS OFF-LIMITS IN HERE** (SPEC.md 53.1) - they
; render desktop geometry into a framebuffer that is now character cells. That
; is what [te_txm] is for, and why it is set BEFORE OSAPI_FSX_RUN and cleared
; INSIDE this proc before it returns. It is also why te_show tests that byte
; ABOVE its OSAPI_GFX_LOCK and not below it: the bracket holds the gfx lock
; for its whole life (SPEC.md 53.6), so a worker that reached the lock first
; parked on a byte that could not change until ^] - which for a FEEDER is
; SPEC.md 53.2's death by another name. SPEC.md 70.8.8 is the account.
;
; NO RETRACE GATING, ON ANY ADAPTER. apps/tracker/trktxt.inc is the precedent
; and it reached the opposite conclusion to the obvious one: it REMOVED a
; retrace clock that measured correctly, because polling one is
; indistinguishable from work to a round-robin scheduler and the drawing side
; took half the machine whether it needed it or not. The pacing is FSXW_FRAME,
; which waits by YIELDING, so the frame time this does not use goes to the
; worker that owns the socket.
; =============================================================================

; TET_COLS, TET_ROWS, TET_ATTR and TET_SATTR are os88con.inc's now
; (CON_COLS, CON_ROWS, CON_TATTR, CON_TSATTR - SPEC.md 70.8.13), with the
; renderer they belong to. What is left here is the BRACKET, which is the half
; two carriers do not agree about.
TET_ESC    equ 0x1D                 ; **CTRL+], AND IT IS NOT AN ARBITRARY
                                    ; CHOICE**: it is the escape character
                                    ; every telnet client since 4.2BSD uses to
                                    ; leave the session for its own prompt, so
                                    ; the one key a user of this program
                                    ; already knows is the one that gets them
                                    ; out. SPEC.md 11.2.1 asks for `F` in both
                                    ; directions and exempts an app taking
                                    ; typed text, which a terminal is as
                                    ; completely as anything here: every
                                    ; printable key belongs to the host

; -----------------------------------------------------------------------------
; te_fsx - Session > Full Screen, and Ctrl+] while windowed
; in:  SI = our window; UI task, gfx lock held (a callback's environment)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
te_fsx:
    push ax
    push bx
    push cx
    push si
    mov bx, si
    call OSAPI_FSX_CAPS             ; AX = the modes this adapter allows, DL =
    test ax, 1 << FSXM_TEXT80       ; the DISPLAY's own kind (SPEC.md 53.7.1),
    jz .out                         ; which osapi_video cannot answer for a
                                    ; window that is not on the primary
    mov [con_tkind], dl
    mov byte [te_txm], 1            ; ...BEFORE the bracket: the kept worker
                                    ; must not draw one glyph into it
    mov ax, te_fsx_main
    mov bx, si
    mov cx, FSXF_KEEPWORKER
    call OSAPI_FSX_RUN
    mov byte [te_txm], 0            ; ...and again on the refusal path, where
.out:                               ; te_fsx_main never ran to clear it
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; te_fsx_main - the bracket's body
; in:  SI = our window, ES = KERNEL_SEG, DS = CS = ours; near ret
; -----------------------------------------------------------------------------
te_fsx_main:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; the FSI block is OURS (the gfx_blit4
    mov di, te_fsi                  ; idiom: ES:DI is the caller's own)
    mov al, FSXM_TEXT80
    call OSAPI_FSX_MODE
    jc .done
    mov ax, [te_fsi + FSI_SEG]      ; the ONE thing that is not a constant:
    mov [con_tseg], ax               ; B000 on Hercules and B800 on the rest
    mov ax, 0x0100                  ; park the CRTC cursor - it is ours now,
    mov cx, 0x2000                  ; and the mode set left it blinking at 0,0
    int 0x10                        ; (SPEC.md 53.4)
    call con_tx_ice                  ; ...and the blink bit, which is a per-
                                    ; adapter question (SPEC.md 70.8.9)
    mov byte [con_thint], 0xFF
    OS88_ALTENTER_SEED              ; the Alt+Enter that got us here is still
                                    ; held, and te_tx_keys' read is a LEVEL
                                    ; one: without this the first poll takes
                                    ; that same hold as the press that leaves
    call con_markall
    call te_tx_owed
    call te_tx_hint                 ; ONCE, on entry, and the host is allowed
.loop:                              ; to overwrite it (SPEC.md 70.8.7)
    call te_tx_keys
    jc .done
    ; --- **THE UI TASK IS IN HERE, SO THE HANDSHAKE HAS TO BE TOO** ---------
    ; FSXF_KEEPWORKER keeps the WORKER (SPEC.md 53.5.1) and the UI task is this
    ; loop: it dispatches no events, so OSAPI_WM_WAKE delivers nothing and a
    ; Zmodem commit would never happen. tz_wake is indifferent to being called
    ; with nothing to do - that is OSAPI_WM_ONWAKE's own contract - and it
    ; skips its drawing half while [te_txm] is set, because the lock and the
    ; glass are both this bracket's (SPEC.md 70.8.8).
    call tz_wake
    cmp byte [tz_req], TZ_NAME
    je .done                        ; ...and the SAVE DIALOG needs the desktop:
                                    ; it is a window, and every kernel drawing
                                    ; slot is off-limits in a foreign text mode
                                    ; (SPEC.md 53.1). So a transfer that reaches
                                    ; its ZFILE in here drops back to the window
                                    ; to ask where the file goes
    call te_tx_owed                 ; ...only the rows the worker changed
    mov al, FSXW_FRAME              ; YIELDS while it waits, which is what the
    call OSAPI_FSX_WAIT             ; kept worker needs (SPEC.md 53.5.1)
    jmp short .loop
.done:
    mov byte [te_txm], 0            ; ...INSIDE the bracket, so fsx_restore's
    call con_markall                 ; repaint finds the app windowed and the
    pop es                          ; whole screen owed
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; te_tx_keys - the keyboard, polled. CF=1 = leave the bracket
;
; int 16h directly, because no events are dispatched in here (SPEC.md 53.1) -
; and every key that is not the escape goes to the HOST, exactly as it does
; windowed. te_tx is the queue the worker drains, so this needs no wire.
; -----------------------------------------------------------------------------
te_tx_keys:
    push ax
    call os88alt_edge               ; ALT+ENTER LEAVES TOO (SPEC.md 11.2.1.1),
    jc .quit                        ; beside ^]. It cannot arrive through the
                                    ; int 16h below - no XT BIOS enqueues the
                                    ; combination (9.7.1) - and a bracket
                                    ; dispatches no events (53.1), so the
                                    ; key-state map is what carries it
.next:
    mov ah, 1
    int 0x16
    jz .none
    xor ah, ah
    int 0x16
    cmp al, TET_ESC
    je .quit
    call te_tx                      ; AL = ascii, AH = scan, as windowed
    jmp short .next
.none:
    pop ax
    clc
    ret
.quit:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; te_tx_owed - the scroll, then the rows the bitmap names, then the cursor
; in:  nothing; out: nothing, all registers preserved
;
; THE SAME MARKS THE WINDOWED PATH USES (SPEC.md 70.8.1). con_putc runs on the
; kept worker and marks the row it wrote whichever screen is up, so the two
; renderers share one answer to "what changed" and cannot drift about it.
;
; **AND THIS ONE RUNS ON THE UI TASK WHILE THAT WORKER IS STILL GOING**, which
; is what FSXF_KEEPWORKER means (SPEC.md 53.5.1) and what makes the debt a
; SHARED WORD rather than a private one. Both reads below are te_take*'s now:
; the bit is cleared as the row is drawn and the scroll counter is read and
; zeroed together, so neither a mark nor a scroll can be lost between the read
; and the store. `te_markclr` used to sit at the foot of this loop and threw
; away every mark the worker set during the pass (SPEC.md 70.8.8).
; -----------------------------------------------------------------------------
te_tx_owed:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    call con_tx_scroll               ; ...FIRST: the rows below are lettered on
    xor bx, bx                      ; top of the pixels this moves
.row:
    call con_takerow
    jnc .next
    call con_tx_row
.next:
    inc bx
    cmp bx, CON_ROWS
    jb .row
    cmp byte [tz_pan], 0
    je .nop
    call te_tx_prog                 ; **ROW 25, OVERWRITTEN** (SPEC.md 70.11.5),
                                    ; which costs one row of the board's screen
                                    ; for the duration and nothing after it:
                                    ; tz_end marks it and the loop above
                                    ; re-emits it from the buffer
.nop:
    call con_tx_cursor
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- te_tx_hint - OUR string, on os88con.inc's bottom row (SPEC.md 70.8.13) --
; The PROC is the library's and the STRING is ours, which is the seam that
; matters: the key that leaves a bracket is the carrier's decision, so the
; sentence naming it cannot be the library's.
te_tx_hint:
    push si
    mov si, te_s_leave
    call con_tx_hint
    pop si
    ret

te_s_leave: db ' ^] to leave', 0
