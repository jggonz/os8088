; =============================================================================
; npbench.inc - Note Pad's WALK, timed on the machine it was written for
;
; apps/notepad/notepad.asm built with -DNPBENCH (`make npbench`), which is the
; only difference: the shipped NOTEPAD.O88 has no bench, no Ctrl-B and no
; buffer, and every hook that reaches this file is inside %ifdef NPBENCH. One
; source, two binaries - tests/trklog.inc's arrangement, for its reason.
;
; WHAT IT IS FOR. SPEC.md 27.7.3's NP_HCHUNK sizes the gfx-lock hold of one
; background count pass, and it has never been measured on iron: it was
; reasoned from ~2 ms per measure row, which is a MartyPC cycle count wearing
; the right units. This answers it directly, by timing THE WALK ITSELF rather
; than something the same shape - the trap PERFORMANCE.md Part 4 exists for.
;
; HOW TO RUN IT. Boot the npbench disk, open a file (README.TXT on the system
; disk is the reference - 15,889 bytes, 781 rows in a default window), and
; press Ctrl-B. The machine freezes for as long as the suite takes, which on a
; 4.77MHz 8088 with README loaded is on the order of ten seconds: np_onkey
; holds the gfx lock throughout, deliberately, because that is the hold being
; measured. The report REPLACES the note when it finishes; Ctrl-S writes it out.
;
; METHOD. Tick-timed, not PIT-timed, and adaptively: each row runs its body
; until NPB_MINT ticks have passed, then reports ticks, iterations and the
; microseconds those two imply. The PIT wraps every 55 ms and one unbounded
; walk of a real note is ninety of those, so a PIT span cannot see it; ticks
; can, and the iteration count is what buys back the resolution on the short
; rows. Every raw number is printed beside the derived one so a reader can
; check the arithmetic rather than trust it.
;
; WHAT IS NOT HERE, and why. The DRAWING walk is not timed. np_rflush skips a
; row whose signature is unchanged (SPEC.md 27.2), so the second iteration of
; a repeated paint draws nothing at all and the row would report a number
; five times too good - measuring the signature cache and calling it the
; renderer. Timing it honestly needs the signatures invalidated per iteration,
; which is a different measurement than the one the shipped code makes.
; PERFORMANCE.md's ~1 ms per glyph cell and tests/fontbench already price
; that side.
; =============================================================================

NPB_MINT    equ 18              ; ticks a row must span before it is believed:
                                ; ~1 s, so the 55 ms tick is ~5% of the answer
                                ; at worst and much less on the long rows
NPB_USTICK  equ 54925           ; microseconds in one 18.2065 Hz tick

; -----------------------------------------------------------------------------
; npb_time - run [BX] until NPB_MINT ticks have gone by
; in:  BX = near routine; out: AX = ticks elapsed, DX = iterations
; -----------------------------------------------------------------------------
npb_time:
    push si
    push di
    xor di, di
    call OSAPI_GET_TICKS
    mov si, ax
.loop:
    push bx                     ; THE BODY MAY CLOBBER ANYTHING, and two of the
    push si                     ; three do: npb_b_chunk and npb_b_measure both
    push di                     ; load SI with [np_win]. Held in registers, the
    call bx                     ; start tick became a window pointer and the
    pop di                      ; first `sub ax, si` reported twenty thousand
    pop si                      ; ticks for one iteration - which is nineteen
    pop bx                      ; minutes, and so was caught on the first run
    inc di                      ; rather than believed
    call OSAPI_GET_TICKS
    sub ax, si                  ; modular, so a wrapping counter is not a
    cmp ax, NPB_MINT            ; special case (SPEC.md 8)
    jb .loop
    mov dx, di
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; npb_div32 - DX:AX / CX, quotient in DX:AX (the two-step, 8086 has no 32-bit
;             divide and one 16-bit DIV would overflow on the long rows)
; -----------------------------------------------------------------------------
npb_div32:
    push bx
    push si
    mov si, ax
    mov ax, dx
    xor dx, dx
    div cx                      ; AX = high quotient, DX = remainder...
    mov bx, ax
    mov ax, si                  ; ...which is the high half of the low divide
    div cx
    mov dx, bx
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; npb_utoa32 - DX:AX as decimal at DI (DS:DI); DI advances past it
; np_utoa is 16-bit and a microsecond figure here reaches seven digits
; -----------------------------------------------------------------------------
npb_utoa32:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    xor bp, bp
    mov cx, 10
.div:
    mov si, ax
    mov ax, dx
    xor dx, dx
    div cx
    mov bx, ax                  ; high quotient
    mov ax, si
    div cx                      ; DX:AX was remainder:low, so this is exact
    push dx                     ; the digit
    inc bp
    mov dx, bx
    or bx, ax                   ; DX:AX zero yet?
    jnz .div
.emit:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    dec bp
    jnz .emit
    mov byte [di], 0
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; npb_str - copy the NUL-terminated string at SI to DI (both DS); DI advances
; -----------------------------------------------------------------------------
npb_str:
    push ax
.l:
    lodsb                       ; DF = 0 per SPEC.md 1
    or al, al
    jz .d
    mov [di], al
    inc di
    jmp short .l
.d:
    pop ax
    ret

; npb_pad - spaces at DI until the line is AL characters wide
; in: AL = the column wanted, [npb_ls] = where this line began
npb_pad:
    push bx
    push cx
    mov bx, di
    sub bx, [npb_ls]            ; BX = columns used
    xor ch, ch
    mov cl, al
    sub cx, bx
    jbe .out
.sp:
    mov byte [di], ' '
    inc di
    loop .sp
.out:
    pop cx
    pop bx
    ret

npb_nl:
    mov byte [di], 13           ; the note's own line break (SPEC.md 27.6 folds
    inc di                      ; CR/LF on the way in, so one CR is a line)
    mov [npb_ls], di
    ret

; -----------------------------------------------------------------------------
; npb_emit - one report row: SI = its name, [npb_t] ticks over [npb_i] iters
; -----------------------------------------------------------------------------
npb_emit:
    push ax
    push cx
    push dx
    call npb_str
    mov al, 13
    call npb_pad
    mov ax, [npb_t]
    call np_utoa
    mov al, 20
    call npb_pad
    mov ax, [npb_i]
    call np_utoa
    mov al, 28
    call npb_pad
    mov ax, [npb_t]             ; us/op = ticks * 54925 / iterations
    mov dx, NPB_USTICK
    mul dx                      ; DX:AX, and ticks*54925 cannot overflow 32
    mov cx, [npb_i]             ; bits for any run this side of an hour
    jcxz .nodiv
    call npb_div32
    call npb_utoa32
.nodiv:
    call npb_nl
    pop dx
    pop cx
    pop ax
    ret

; --- the three bodies --------------------------------------------------------
; Each is the SHIPPED routine, set up the way its shipped caller sets it up.

npb_b_chunk:                    ; one background count pass (SPEC.md 27.7.3):
    mov si, [np_win]            ; what NP_HCHUNK actually costs, which is the
    call np_hmark               ; number this whole file exists for. np_hmark
    call np_hchunk              ; puts it back at the top of the note each time
    ret

npb_b_measure:                  ; the UNBOUNDED walk - what a resize cost before
    mov si, [np_win]            ; SPEC.md 27.7.5 bounded it, and what the
    mov word [np_hity], 0xFFFF  ; caret-follow safety net still costs when the
    mov word [np_wanty], 0xFFFF ; caret is outside the view
    mov byte [np_draw], 0       ; (docs/plans/completed/NOTEPAD-NOTES.md 1.4)
    mov byte [np_sigup], 0
    mov byte [np_clip], 0
    mov byte [np_resume], 0
    call np_walk                ; [np_lastrow] is 0x7FFF by np_walk's own
    ret                         ; one-shot reset, so this is unbounded

npb_b_guess:                    ; SPEC.md 27.7.4's estimate, which was added to
    call np_hguess              ; np_bounds and claimed to be free. This is the
    ret                         ; claim, priced

npb_b_null:                     ; the LOOP's own cost - a call, an increment and
    ret                         ; an OSAPI_GET_TICKS far call per iteration.
                                ; Negligible against hchunk and measure_all and
                                ; NOT against hguess, so it is a row rather than
                                ; a subtraction: the reader can see both numbers
                                ; and take the difference, which is benchlib's
                                ; bl_baseline made visible instead of implicit

; -----------------------------------------------------------------------------
; npb_run - the suite. Ctrl-B, gfx lock held (np_onkey's), and it stays held:
;           the hold is the thing being measured.
; -----------------------------------------------------------------------------
npb_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov di, npb_buf
    mov [npb_ls], di

    mov si, npb_h1
    call npb_str
    call npb_nl

    mov si, npb_h2              ; the note this run describes, because every
    call npb_str                ; number below is per-row and a row count is
    mov ax, [np_len]            ; what turns it into a total
    call np_utoa
    mov si, npb_h3
    call npb_str
    mov ax, [np_drows]
    call np_utoa
    mov si, npb_h4
    call npb_str
    mov ax, [np_vrows]
    call np_utoa
    mov si, npb_h5
    call npb_str
    mov ax, [np_rcols]
    call np_utoa
    call npb_nl

    mov si, npb_h6
    call npb_str
    mov ax, NP_HCHUNK
    call np_utoa
    call npb_nl
    call npb_nl

    mov si, npb_hd
    call npb_str
    call npb_nl

    mov bx, npb_b_chunk
    call npb_time
    mov [npb_t], ax
    mov [npb_i], dx
    mov si, npb_n1
    call npb_emit

    mov bx, npb_b_measure
    call npb_time
    mov [npb_t], ax
    mov [npb_i], dx
    mov si, npb_n2
    call npb_emit

    mov bx, npb_b_guess
    call npb_time
    mov [npb_t], ax
    mov [npb_i], dx
    mov si, npb_n3
    call npb_emit

    mov bx, npb_b_null
    call npb_time
    mov [npb_t], ax
    mov [npb_i], dx
    mov si, npb_n4
    call npb_emit

    call npb_nl
    mov si, npb_ft
    call npb_str
    call npb_nl

    ; --- the report BECOMES the note -----------------------------------------
    mov cx, di
    sub cx, npb_buf             ; CX = bytes written
    mov ax, [np_capkb]          ; ...and the claim will hold them: the smallest
    mov bx, 1024                ; one is NP_KB0 = 1KB and the report is ~400
    mul bx
    cmp cx, ax
    jbe .fits
    mov cx, ax
.fits:
    mov si, npb_buf
    mov es, [np_dseg]
    xor di, di
    rep movsb
    mov ax, di
    mov [np_len], ax
    mov word [np_cur], 0
    mov word [np_top], 0
    call np_editinv             ; the whole buffer changed underneath every
                                ; index into it (SPEC.md 27.4/27.5)
    mov si, [np_win]
    call np_redraw

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

npb_h1: db 'NPBENCH - os8088 Note Pad, the walk timed (docs/plans/completed/NOTEPAD-NOTES.md)', 0
npb_h2: db 'note: len=', 0
npb_h3: db '  rows=', 0
npb_h4: db '  vrows=', 0
npb_h5: db '  cols=', 0
npb_h6: db 'NP_HCHUNK=', 0
npb_hd: db 'row          ticks iters  us/op', 0
npb_n1: db 'hchunk', 0
npb_n2: db 'measure_all', 0
npb_n3: db 'hguess', 0
npb_n4: db 'baseline', 0
npb_ft: db 'hchunk = one background count pass: it sizes the gfx-lock hold, and is the number NP_HCHUNK is chosen from. measure_all = the unbounded walk. Subtract baseline from hguess; it is noise against the other two.', 0
