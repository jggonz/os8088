; =============================================================================
; os8088 - apps/telnet/tetxt.inc
;
; THE TERMINAL ON A TEXT SCREEN (SPEC.md 53.4/70.6)
;
; SPEC.md 53's exclusive bracket with FSXM_TEXT80, which is the one mode every
; adapter here can set - VGA, CGA and Hercules alike (fsx_caps 0x1EF/0x00F/
; 0x011, bit 0). Tracker's XT mode (SPEC.md 45.13) is the precedent and the
; argument is the same one, a size smaller: a character cell in text mode is
; ONE WORD STORE and the card's own ROM font does the rendering, where the
; windowed terminal spends a glyph cell - PERFORMANCE.md's ~900us on the
; machine this is for - on every character it draws. A row change goes from
; 64 glyph cells to a 64-word `rep movsw`.
;
; **THE SCREEN IS STILL 64x18 AND IS CENTRED**, not grown to 80x24. That is
; deliberate: the buffer is what the host's idea of where a line wraps is
; measured against (SPEC.md 70.5), and a terminal that is 64 columns in a
; window and 80 in full screen would be reflowing at the worst possible
; moment. Growing the screen to 80x24 for good is a separate decision with a
; separate cost - every windowed adapter shows fewer than 80 columns - and it
; is not made here.
;
; FSXF_KEEPWORKER IS NOT OPTIONAL. The worker owns the socket (SPEC.md 70.2),
; so a bracket that froze it would freeze the session: no bytes would arrive
; and nothing would be sent for as long as the screen was up.
;
; **EVERY KERNEL DRAWING SLOT IS OFF-LIMITS IN HERE** (SPEC.md 53.1) - they
; render desktop geometry into a framebuffer that is now character cells. That
; is what [te_txm] is for, and why it is set BEFORE OSAPI_FSX_RUN and cleared
; INSIDE this proc before it returns: te_show runs on the kept worker and
; would otherwise letter a glyph into the text screen on its very first turn,
; and fsx_restore's repaint at the end must find the app windowed again.
; =============================================================================

TET_COLS   equ 80                   ; FSXM_TEXT80, on all three adapters
TET_ROWS   equ 25
TET_X0     equ (TET_COLS - TE_COLS) / 2         ; ...the terminal, centred
TET_Y0     equ 2
TET_STATY  equ TET_ROWS - 1         ; the status line, at the foot
TET_ATTR   equ 0x07                 ; grey on black: a terminal's own colours,
                                    ; and the desktop's black-on-white would
                                    ; be a white screen with a hole in it
TET_SATTR  equ 0x70                 ; ...and the status line, inverse
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
    call OSAPI_FSX_CAPS             ; AX = the modes this adapter allows
    test ax, 1 << FSXM_TEXT80
    jz .out                         ; no text mode: nothing to say, because the
                                    ; menu item is greyed on the same test
                                    ; (SPEC.md 47 rule 5)
    mov byte [te_txm], 1             ; ...BEFORE the bracket: the kept worker
                                    ; must not draw one glyph into it
    mov ax, te_fsx_main
    mov bx, si
    mov cx, FSXF_KEEPWORKER
    call OSAPI_FSX_RUN
    mov byte [te_txm], 0             ; ...and again on the refusal path, where
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
    mov ax, 0x0100                  ; park the CRTC cursor out of sight - it is
    mov cx, 0x2000                  ; ours now, and the mode set left it
    int 0x10                        ; blinking at 0,0 (SPEC.md 53.4)
    call te_tx_all
.loop:
    call te_tx_keys
    jc .done
    call te_tx_owed                 ; ...only the rows the worker changed
    mov al, FSXW_FRAME              ; YIELDS while it waits, which is what the
    call OSAPI_FSX_WAIT             ; kept worker needs (SPEC.md 53.5.1)
    jmp short .loop
.done:
    mov byte [te_txm], 0             ; ...INSIDE the bracket, so fsx_restore's
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
; te_tx_all - the whole screen: the frame, every row, the status line
; -----------------------------------------------------------------------------
te_tx_all:
    call te_markall
    call te_tx_owed
    ret

; -----------------------------------------------------------------------------
; te_tx_owed - the rows in the dirty range, and the status line
; in:  nothing; out: nothing, all registers preserved
;
; THE SAME MARKS THE WINDOWED PATH USES (SPEC.md 70.4). te_putc runs on the
; kept worker and marks the row it wrote whichever screen is up, so the two
; renderers share one answer to "what changed" and cannot drift about it.
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
    mov bx, [te_dr0]
.row:
    cmp bx, [te_dr1]
    ja .done
    cmp bx, TE_ROWS
    jae .done
    call te_tx_row
    inc bx
    jmp short .row
.done:
    call te_markclr
    mov word [te_scrl], 0           ; a text row change IS the scroll here:
                                    ; the rows are re-emitted, so there is
                                    ; nothing for te_scrollpaint to spend
    call te_tx_stat
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- te_tx_row - BX = a buffer row, onto the text screen ---------------------
te_tx_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, TE_COLS
    mul bx
    mov si, te_scr
    add si, ax
    mov ax, bx
    add ax, TET_Y0
    mov cx, TET_COLS * 2
    mul cx                          ; **MUL WRITES DX** (SPEC.md 1)
    add ax, TET_X0 * 2
    mov di, ax
    mov es, [te_tseg]
    mov cx, TE_COLS
    mov ah, TET_ATTR
    cld
.c:
    lodsb
    stosw
    loop .c
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- te_tx_stat - the status line, inverse, across the whole width -----------
; It says the same thing the windowed one does and picks it the same way: the
; REASON when there is one and the state otherwise (SPEC.md 54.4.1's rule).
te_tx_stat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bl, [te_state]
    xor bh, bh
    shl bx, 1
    mov si, [te_names + bx]
    cmp byte [te_state], TS_ERR
    jne .have
    cmp word [te_msg], 0
    je .have
    mov si, [te_msg]
.have:
    mov es, [te_tseg]
    mov di, TET_STATY * TET_COLS * 2
    mov cx, TET_COLS
    mov ah, TET_SATTR
    cld
.c:
    mov al, [si]
    or al, al
    jz .pad
    inc si
    jmp short .put
.pad:
    mov al, ' '                     ; ...padded to the full width, so a shorter
.put:                               ; message erases the last one
    stosw
    loop .c
    mov di, (TET_STATY * TET_COLS + TET_COLS - 12) * 2
    mov si, te_s_leave              ; ...and the way out, where a terminal
    mov ah, TET_SATTR               ; puts it
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
    pop dx
    pop cx
    pop bx
    pop ax
    ret

te_s_leave: db ' ^] to leave', 0
