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
; menu. TET_X0 and TET_Y0 are gone; a row is at FSI_SEG:(r*160). And te_scr's
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

TET_COLS   equ 80                   ; FSXM_TEXT80, on all three adapters
TET_ROWS   equ 25
TET_ATTR   equ 0x07                 ; grey on black: a terminal's own colours,
                                    ; and the desktop's black-on-white would
                                    ; be a white screen with a hole in it
TET_SATTR  equ 0x70                 ; ...and the leave hint, inverse
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
%if TET_COLS != TE_COLS || TET_ROWS != TE_ROWS
  %error "the buffer maps 1:1 onto text VRAM (SPEC.md 70.8.7): the two sizes ARE one size"
%endif

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
    mov [te_tkind], dl
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
    mov [te_tseg], ax               ; B000 on Hercules and B800 on the rest
    mov ax, 0x0100                  ; park the CRTC cursor - it is ours now,
    mov cx, 0x2000                  ; and the mode set left it blinking at 0,0
    int 0x10                        ; (SPEC.md 53.4)
    call te_tx_ice                  ; ...and the blink bit, which is a per-
                                    ; adapter question (SPEC.md 70.8.9)
    mov byte [te_thint], 0xFF
    call te_markall
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
    call te_markall                 ; repaint finds the app windowed and the
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
; THE SAME MARKS THE WINDOWED PATH USES (SPEC.md 70.8.1). te_putc runs on the
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
    call te_tx_scroll               ; ...FIRST: the rows below are lettered on
    xor bx, bx                      ; top of the pixels this moves
.row:
    call te_takerow
    jnc .next
    call te_tx_row
.next:
    inc bx
    cmp bx, TE_ROWS
    jb .row
    cmp byte [tz_pan], 0
    je .nop
    call te_tx_prog                 ; **ROW 25, OVERWRITTEN** (SPEC.md 70.11.5),
                                    ; which costs one row of the board's screen
                                    ; for the duration and nothing after it:
                                    ; tz_end marks it and the loop above
                                    ; re-emits it from the buffer
.nop:
    call te_tx_cursor
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; te_tx_scroll - spend [te_scrl] as ONE VRAM move (SPEC.md 70.8.8)
;
; **THIS RENDERER DID NOT SCROLL AT ALL.** te_tx_owed used to end with
; `mov word [te_scrl], 0` under the comment "a text row change IS the scroll
; here: the rows are re-emitted, so there is nothing for te_scrollpaint to
; spend". Only the rows in the dirty range were re-emitted, and te_scrollck
; marked exactly one - the row it opened. So after a scroll the buffer had
; moved every row up and the screen had been told about the last one: rows
; 0..N-2 kept showing pre-scroll text for the rest of the session, and the
; bottom row was overwritten again and again. A board's output, which is one
; long scroll, was legible on one line.
;
; The debt is spent the way it was always for: ONE `rep movsw` of (25-N)*160
; bytes inside VRAM, and the N rows it vacates are marked so the loop above
; re-emits them from the buffer.
; -----------------------------------------------------------------------------
te_tx_scroll:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call te_takescroll              ; ...read and zeroed together, because the
    or ax, ax                       ; WORKER adds to that counter while this
    jz .out                         ; runs (SPEC.md 70.8.8)
    cmp ax, TET_ROWS
    jae .all                        ; the whole screen scrolled away: there is
                                    ; nothing under it worth moving
    mov [te_rr], ax                 ; N, out of the registers' way while DS is
                                    ; pointed at VRAM. te_rr is te_compose's
                                    ; scratch and the two renderers are never
                                    ; up at once - [te_txm] is what says so
    mov bx, TET_COLS * 2
    mul bx                          ; **MUL WRITES DX** (SPEC.md 1). AX = the
    mov si, ax                      ; source row's offset, N*160
    xor di, di
    mov ax, TET_ROWS
    sub ax, [te_rr]
    mov bx, TET_COLS
    mul bx                          ; AX = (25-N)*80 WORDS
    mov cx, ax
    mov ax, [te_tseg]
    mov es, ax
    push ds
    mov ds, ax                      ; VRAM to VRAM, and DS comes straight back
    cld
    rep movsw
    pop ds                          ; ...before te_mark reads one of our words
    mov bx, TET_ROWS
    sub bx, [te_rr]
.mk:
    call te_mark                    ; the rows it VACATED are owed, and the
    inc bx                          ; loop above re-emits them from the buffer
    cmp bx, TET_ROWS
    jb .mk
    ; --- AND THE ROW THE LEAVE HINT LANDED ON ------------------------------
    ; The move carried it up with everything else, and the row it arrived on
    ; is not one of the vacated ones - so without this it drifts a row up the
    ; screen per scroll and stays there wherever the board is not drawing. It
    ; is marked instead, and the buffer erases it: "gone the moment the board
    ; draws" (SPEC.md 70.8.7) is then true of a board that scrolls as well as
    ; of one that prints over it.
    mov al, [te_thint]
    cmp al, 0xFF
    je .out                         ; already off the top
    mov bl, [te_rr]
    sub al, bl
    jc .hgone
    mov [te_thint], al
    mov bl, al
    xor bh, bh
    call te_mark
    jmp short .out
.hgone:
    mov byte [te_thint], 0xFF
    jmp short .out                  ; (the CRTC cursor needs no mark: it is
                                    ; hardware, and te_tx_cursor places it
                                    ; from [te_cx]/[te_cy] on every pass)
.all:
    mov byte [te_thint], 0xFF       ; ...AND THE HINT IS GONE WITH IT. Without
                                    ; this the next scroll subtracts from a row
                                    ; number that no longer means anything and
                                    ; marks an unrelated row - one redundant
                                    ; redraw, never wrong pixels, and one line
                                    ; to stop it
    call te_markall
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- te_tx_row - BX = a buffer row, onto the text screen ---------------------
; A COLOUR ROW IS A MOVE: te_scr's cell is character then attribute and so is
; VRAM's, so eighty words go straight across. An MDA row is eighty cells each
; MAPPED first (SPEC.md 70.8.9), because mode 7 has no background colour.
te_tx_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, TE_COLS * 2
    mul bx                          ; **MUL WRITES DX** (SPEC.md 1)
    mov si, te_scr
    add si, ax
    mov di, ax                      ; ...and the VRAM offset is the SAME
    mov es, [te_tseg]               ; number: one cell, one layout
    mov cx, TET_COLS
    cld
    cmp byte [te_tkind], VID_HERC
    je .mda
    rep movsw
    jmp short .out
.mda:
    lodsw                           ; AL = the character, AH = the attribute
    call te_tx_mattr                ; ...which mode 7 cannot say in colour
    stosw
    loop .mda
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- te_tx_mattr - AH = an IBM attribute -> the MDA byte (SPEC.md 70.8.9) ----
; The INVERSE predicate is 70.8.4's, to the bit: it is the same question the
; 1bpp windowed renderer asks about polarity, so the two 1bpp paths cannot
; disagree about which cells are reversed.
;
; **THREE CASES AND THE BLINK BIT, AND NO FOURTH.** An earlier draft mapped
; SGR 4 to MDA's 0x01 underline off a [te_ul] byte; SPEC.md 70.8.9 dropped it,
; because SGR 4 does not reach the attribute at all - the parser keeps
; underline out of the cell, so there is no third byte per cell and nothing
; for this to read. **THE INPUT HERE IS THE DERIVED IBM BYTE** - bits 0-3
; foreground, 4-6 background, 7 blink - and this renderer never sees the
; logical state it came from.
te_tx_mattr:
    push bx
    mov bl, ah
    and bl, 0x80                    ; blink is kept in every case
    test ah, 0x07
    jnz .bright                     ; a foreground that is not 0 or 8 reads
    test ah, 0x70                   ; as text whatever the paper is
    jz .plain
    or bl, 0x70                     ; INVERSE: lit paper, dark glyph
    jmp short .set
.bright:
    test ah, 0x08
    jz .plain
    or bl, 0x0F                     ; ...intensity is all MDA has of colour
    jmp short .set
.plain:
    or bl, 0x07
.set:
    mov ah, bl
    pop bx
    ret

; --- te_tx_cursor - the hardware cursor, positioned by the BRACKET -----------
; SPEC.md 53.7's forbidden list has exactly one int 10h on it - a MODE SET
; outside fsx_mode - and 53.4 names AH=02h and AH=01h as calls the bracket may
; already make. Both existing consumers, this file and trktxt.inc, already do
; the AH=01h park, byte for byte the same three instructions.
te_tx_cursor:
    push ax
    push bx
    push cx
    push dx
    mov al, [te_cvis]
    cmp al, [te_tcur]
    je .place                       ; the SHAPE is only issued on a change: it
    mov [te_tcur], al               ; is what the mode set already chose, and a
    or al, al                       ; BIOS call a frame for a byte that does
    jz .park                        ; not move is a frame the worker wanted
    mov ax, 0x0106                  ; ...an underline in an 8-row cell
    mov cx, 0x0607
    int 0x10
    jmp short .place
.park:
    mov ax, 0x0100                  ; SPEC.md 53.4's park, byte for byte what
    mov cx, 0x2000                  ; trktxt.inc does
    int 0x10
    jmp short .out
.place:
    cmp byte [te_cvis], 0
    je .out
    mov ah, 2
    xor bh, bh
    mov dh, [te_cy]
    mov dl, [te_cx]
    int 0x10
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- te_tx_ice - blink or a bright background, per adapter (SPEC.md 70.8.9) --
; 53.4 gives the app the blink toggle explicitly, "through the BIOS the
; bracket may already call (... AX=1003h blink) or the CRTC ports directly",
; and 53.7 says the video hardware inside a foreign mode is otherwise the
; app's - so port 3D8h is the app's to write. Hercules has neither, and the
; menu item is greyed there rather than doing nothing.
te_tx_ice:
    push ax
    push bx
    push dx
    mov bl, 1
    cmp byte [te_ice], 0
    je .have
    xor bl, bl                      ; iCE ON is BLINK OFF
.have:
    cmp byte [te_tkind], VID_CGA
    je .cga
    cmp byte [te_tkind], VID_HERC
    je .out                         ; MDA text is monochrome: nothing to do
    mov ax, 0x1003
    int 0x10
    jmp short .out
.cga:
    mov al, 0x29                    ; 80x25 text, video on, blink on
    or bl, bl
    jnz .cw
    mov al, 0x09                    ; ...bit 5 clear = a bright background
.cw:
    mov dx, 0x3D8
    out dx, al
.out:
    pop dx
    pop bx
    pop ax
    ret

; --- te_tx_hint - the way out, drawn ONCE on the bottom row ------------------
; **AND THE HOST IS ALLOWED TO OVERWRITE IT** (SPEC.md 70.8.7). A hint that
; survives is a hint that fights the board for the row it needs; a hint that
; is gone the moment the board draws is a hint that did its job.
te_tx_hint:
    push ax
    push cx
    push si
    push di
    push es
    mov es, [te_tseg]
    mov di, ((TET_ROWS - 1) * TET_COLS + TET_COLS - 12) * 2
    mov si, te_s_leave
    mov ah, TET_SATTR               ; ...inverse, which mode 7 spells the same
    mov byte [te_thint], TET_ROWS - 1   ; the row it is on, so a scroll can
                                        ; erase it where it lands
.h:
    mov al, [si]
    or al, al
    jz .out
    inc si
    stosw
    jmp short .h
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

te_s_leave: db ' ^] to leave', 0
