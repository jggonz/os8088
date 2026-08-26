; =============================================================================
; os8088 - tests/typebench/typebench.asm
;
; TYPEBENCH: what a KEYSTROKE costs, measured the way Note Pad actually pays
; for one. SPEC.md 6.1.1 measured the primitive; this measures the workload -
; a line of text redrawn once per character typed, which is what np_redraw
; does to its dirty band (SPEC.md 27.2) and the reason that band exists.
;
; It is snappable itself (OSAPI_WM_SNAP, SPEC.md 11.94) and the header says
; whether the snap took - ask rather than assume, because a window too wide to
; snap is left unsnapped and a benchmark that claimed otherwise would be
; measuring the wrong thing and saying so confidently.
;
; THIS HARNESS IS WHAT REMOVED 11.94's [vid_mono] GATE. The flag was a no-op on
; VGA on the reasoning that font_run's single-store path is 1bpp-only, which is
; true and does not settle the question: the rows below say alignment is worth
; 3.4% of a keystroke on Hercules and 9.4% on VGA, because mode 12h is also
; eight pixels to a byte and an unaligned cell spills into a second one there
; too. The gate is gone; the header now reads snap:on everywhere.
;
; Never on the shipped apps disks (their order is pinned, SPEC.md 24): its own
; scratch image, the fmtest/filetest/fontbench precedent.
;
;   make test TESTAPPS=build/typebench.img
;   make test VIDEO=cga TESTAPPS=build/typebench.img
;   make test VIDEO=herc HERCSEG=0x7000 TESTAPPS=build/typebench.img
;
; --- the three rows ----------------------------------------------------------
;
; Every row types TB_N random characters into a TB_N-character line, redrawing
; the WHOLE line after each one - so a row is TB_N line-redraws, not TB_N
; character draws. That is the shape that matters: np_redraw re-erases its band
; and re-letters it, so the cost of a keystroke is the cost of a line.
;
;   CHAR aligned  Note Pad's method at an aligned x: one GFX_FILL of the
;                 line's band, then one FONT_CHAR per cell over it.
;   CHAR skewed   the same, five pixels right. This is what an UNSNAPPED
;                 Note Pad costs, and the difference between it and the row
;                 above is the whole of what OSAPI_WM_SNAP buys a window that
;                 draws character by character.
;   RUN  aligned  one FONT_RUN for the line - background and glyphs in a
;                 single pass, no separate fill. What Note Pad would cost if
;                 it drew its dirty row as a run instead of a character at a
;                 time.
;
; The two visible lines are the CHAR line and the RUN line, left holding the
; random text the last pass typed into them, so the thing being timed is also
; the thing on screen. The skewed pass runs FIRST and shares the CHAR line, so
; what survives is the aligned draw.
;
; --- how it is timed ---------------------------------------------------------
;
; Counter 0 of the 8253, read directly: 1.193182 MHz, so one count is 838ns
; and a row of a few milliseconds has three or four significant digits. A tick
; is 55ms and would answer 0. Latching is a read command, so the system tick is
; not disturbed.
;
; Interrupts are OFF across each measured burst, or the scheduler bills another
; task's timeslice to whichever row was unlucky and the noise swamps the
; effect. The counter keeps counting with IF=0.
;
; Elapsed is the MODULAR subtraction start - end, and it must be: the counter
; reaches zero and reloads every 55ms whatever the measurement is doing, so
; `end > start` means the burst straddled that reload, not that it overran.
; A difference past half the range is the real overrun, and shows as 65535.
;
; --- reading the numbers -----------------------------------------------------
;
; On REAL HARDWARE the microsecond column is the answer: a 4.77MHz 8088 with a
; Hercules card is what this is for, and there the PIT is a wall clock.
;
; Under QEMU it is not. QEMU runs the guest at host speed, so boot it with
; `-icount shift=3,sleep=off` and the PIT counts guest INSTRUCTIONS instead -
; deterministic, reproducible and machine-independent, but NOT microseconds,
; and the us column is then a fiction with a fixed conversion. Instruction
; count also understates the mono win, because what alignment removes is
; disproportionately memory traffic (SPEC.md 6.1.1).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'TYPEBENCH', tb_entry

TB_N      equ 40                  ; characters in the line, and keystrokes per
                                  ; row. A realistic Note Pad line, and TB_N
                                  ; line-redraws is enough to swamp the
                                  ; per-row setup without lapping the counter
TB_W      equ TB_N * 8            ; the line's pixel width
TB_SKEW   equ 5                   ; the unaligned row's offset. NOT 1: every
                                  ; glyph in the ROM font has a blank rightmost
                                  ; column, so a one-pixel shift spills nothing
                                  ; into the second byte and flatters the
                                  ; unaligned case

TB_MARGIN equ 8                   ; our own text inset - a multiple of 8, so
                                  ; that once WF_SNAP has put the content
                                  ; origin on one, everything we draw is
                                  ; aligned (SPEC.md 11.94)
TB_CONT_W equ TB_W + TB_MARGIN * 2 + 8
TB_CONT_H equ 100

TB_Y_HEAD equ 3                   ; content-relative y of each line
TB_Y_CHAR equ 17                  ; the CHAR method's line
TB_Y_RUN  equ 29                  ; the RUN method's line
TB_Y_R0   equ 45                  ; the three result rows
TB_Y_R1   equ 57
TB_Y_R2   equ 69
TB_Y_RAT  equ 85

; -----------------------------------------------------------------------------
; tb_entry - package entry (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment; gfx lock NOT held
; out: BX = window ptr, CF set = refused
; -----------------------------------------------------------------------------
tb_entry:
    push si
    mov si, tb_bufc                 ; two blank lines before the first paint
    call tb_reset
    mov si, tb_bufr
    call tb_reset
    mov si, tb_tpl
    call OSAPI_WM_CREATE
    jc .out                         ; table full: nothing to flag
    mov al, 1
    call OSAPI_WM_SNAP              ; ...and it preserves FLAGS, which is the
                                    ; only reason this is safe here: the CF we
                                    ; owe the loader is riding in them
                                    ; (SPEC.md 11.94)
.out:
    pop si
    ret                             ; NEAR: the kernel reaches every proc here
                                    ; through our own dispatcher (SPEC.md 20.1)

; -----------------------------------------------------------------------------
; tb_paint - W_PAINT: the header, the two lines and the results
; in:  SI = window ptr; gfx lock held; content already white
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
tb_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [tb_cx], ax
    mov [tb_cy], dx
    call tb_head

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, tb_s_head
    mov dx, TB_Y_HEAD
    call tb_line
    mov si, tb_bufc                 ; each line holds what ITS method typed
    mov dx, TB_Y_CHAR
    call tb_line
    mov si, tb_bufr
    mov dx, TB_Y_RUN
    call tb_line

    cmp byte [tb_done], 0
    jne .rows
    mov si, tb_s_hint
    mov dx, TB_Y_R0
    call tb_line
    jmp short .out
.rows:
    mov si, tb_s_r0
    mov dx, TB_Y_R0
    call tb_line
    mov si, tb_s_r1
    mov dx, TB_Y_R1
    call tb_line
    mov si, tb_s_r2
    mov dx, TB_Y_R2
    call tb_line
    mov si, tb_s_rat
    mov dx, TB_Y_RAT
    call tb_line
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tb_line - SI = string, DX = content-relative y. Preserves all registers.
tb_line:
    push cx
    push dx
    mov cx, [tb_cx]
    add cx, TB_MARGIN
    add dx, [tb_cy]
    call OSAPI_FONT_STR_XPARENT
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; tb_onclick / tb_onkey - run the three rows, then repaint ourselves
; in:  SI = window ptr; gfx lock held
; out: nothing; preserves all registers
;
; A callback must repaint itself (SPEC.md 12.3), and here that is not merely
; the rule: the measured passes have been scribbling random text across the
; content for the whole run.
; -----------------------------------------------------------------------------
tb_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [tb_win], si
    call tb_bench
    mov byte [tb_done], 1

    mov bx, [tb_win]                ; wipe the scribbles, then the results
    call OSAPI_WM_CONTENT
    mov [tb_cx], ax
    mov [tb_cy], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [tb_cx]
    mov bx, [tb_cy]
    mov cx, ax
    add cx, TB_CONT_W - 1
    mov dx, bx
    add dx, TB_CONT_H - 1
    call OSAPI_GFX_FILL
    mov si, [tb_win]
    call tb_paint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tb_onkey:                           ; a keypress runs it too, so the window can
    jmp short tb_onclick            ; be driven without aiming at content

; -----------------------------------------------------------------------------
; tb_bench - the three rows, formatted into tb_s_r0..r2 and tb_s_rat
; in:  [tb_win]; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
tb_bench:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, [tb_win]
    call OSAPI_WM_CONTENT
    mov [tb_cx], ax
    mov [tb_cy], dx
    add ax, TB_MARGIN + 7           ; our text x, rounded UP to a multiple of 8.
    and ax, 0xFFF8                  ; Explicitly, and not left to WF_SNAP: the
    mov [tb_tx], ax                 ; row below is LABELLED aligned and has to
                                    ; be one on every adapter, or the VGA
                                    ; numbers compare two unaligned x values
                                    ; and say nothing. What snap:on in the
                                    ; header means is the different question -
                                    ; whether a real app in this window would
                                    ; get that alignment for free, or would
                                    ; have to bend its own layout to a place
                                    ; the window is not on

    mov word [tb_mode], 1           ; 1 = CHAR at x+TB_SKEW. FIRST, so the
    mov ax, TB_Y_CHAR               ; aligned pass overwrites its ghost and
    call tb_row                     ; the line ends up showing the real thing
    mov di, tb_s_r1 + TB_COL
    call tb_report

    mov word [tb_mode], 0           ; 0 = CHAR aligned: Note Pad's method
    mov ax, TB_Y_CHAR
    call tb_row
    mov [tb_tchar], ax
    mov [tb_tchar+2], dx
    mov di, tb_s_r0 + TB_COL
    call tb_report

    mov word [tb_mode], 2           ; 2 = one FONT_RUN for the whole line
    mov ax, TB_Y_RUN
    call tb_row
    mov [tb_trun], ax
    mov [tb_trun+2], dx
    mov di, tb_s_r2 + TB_COL
    call tb_report

    mov ax, [tb_tchar]              ; CHAR aligned / RUN aligned, x100, over
    mov dx, [tb_tchar+2]            ; what are 32-bit measurements now: >> 4
    call tb_ms                    ; brings them into 16 bits with the ratio's
    mov bx, ax                      ; precision intact
    mov ax, [tb_trun]
    mov dx, [tb_trun+2]
    call tb_ms
    mov cx, ax
    mov ax, bx
    mov bx, 100
    mul bx                          ; DX:AX - and an 8086 divide overflow is an
    or cx, cx                       ; interrupt, not a wrong answer, so the
    jz .norat                       ; guard below is not optional
    cmp dx, cx
    jb .div
    mov ax, 0xFFFF
    jmp short .rat
.div:
    div cx
.rat:
    mov di, tb_s_rat + TB_RCOL
    call tb_dec5
.norat:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tb_row - one measured row: TB_N keystrokes into a fresh line at [tb_mode]
; in:  AX = the line's content-relative y
; out: AX = elapsed PIT counts (65535 = the counter lapped); every other
;      register preserved
; -----------------------------------------------------------------------------
tb_row:
    push bx
    push cx
    push si
    push di
    add ax, [tb_cy]
    mov [tb_ty], ax
    mov si, tb_bufc                 ; the CHAR rows share a buffer; the RUN row
    cmp word [tb_mode], 2           ; has its own, so both lines end up showing
    jne .buf                        ; text their own method drew
    mov si, tb_bufr
.buf:
    mov [tb_bufp], si
    call tb_reset                   ; a fresh line, so every row types the same
                                    ; number of characters into the same state
    pushf
    cli                             ; no pre-emption inside the measurement
    mov word [tb_acc], 0
    mov word [tb_acc+2], 0
    call tb_pit
    mov [tb_prev], ax
    mov cx, TB_N
.key:
    push cx
    call tb_key                     ; one keystroke: a character, then the
    pop cx                          ; whole line redrawn
    call tb_fold                    ; ...folded in before the counter can lap
    loop .key
    popf
    mov ax, [tb_acc]
    mov dx, [tb_acc+2]
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tb_fold - add the counts since the last read into the 32-bit accumulator
; preserves all registers
;
; The counter is 16 bits and reloads every 55ms. One keystroke is a couple of
; thousand counts, so reading per keystroke makes each delta unambiguous and
; the sum exact however many times the counter lapped across the row - which
; on a 4.77MHz 8088 it does, because the row is tens of milliseconds.
;
; The first cut subtracted once across the whole row and guarded it with a
; `>= 32768 means overrun` test, which is a different question and answers it
; wrong in both directions: a row past 65536 counts wrapped silently into a
; small plausible number, and a legitimate row between 32768 and 65535 was
; thrown away. On QEMU under -icount the rows are ~3000 counts and neither can
; show. On real hardware every row is tens of thousands, and both did - the
; run that found it reported 15551 for a row that had to be larger than one
; reporting the overrun sentinel, which is how the lap was spotted.
; -----------------------------------------------------------------------------
tb_fold:
    push ax
    push bx
    call tb_pit
    mov bx, [tb_prev]
    mov [tb_prev], ax
    sub bx, ax                      ; it counts DOWN; modular, so a reload in
    add [tb_acc], bx                ; the middle of a keystroke costs nothing
    adc word [tb_acc+2], 0
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tb_key - one keystroke: put a random character in the next cell, then redraw
;          the WHOLE line the way [tb_mode] says
; preserves all registers
;
; The whole line, because that is what a keystroke costs in Note Pad: its
; dirty band is a row, and the row is re-erased and re-lettered (SPEC.md 27.2).
; Redrawing only the new character would measure something nothing does.
; -----------------------------------------------------------------------------
tb_key:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call OSAPI_RAND                 ; a-z with a space every 27th, so the line
    xor dx, dx                      ; has word breaks and a realistic mix of
    mov cx, 27                      ; inked and blank cells
    div cx
    mov al, ' '
    cmp dx, 26
    je .have
    mov al, dl
    add al, 'a'
.have:
    mov bx, [tb_len]
    cmp bx, TB_N
    jae .draw                       ; full: keep redrawing, which is the state
    add bx, [tb_bufp]               ; a long line spends most of its life in
    mov [bx], al
    inc word [tb_len]
.draw:
    mov cx, [tb_tx]
    cmp word [tb_mode], 1
    jne .x
    add cx, TB_SKEW
.x:
    mov dx, [tb_ty]
    cmp word [tb_mode], 2
    je .run

    ; --- Note Pad's method: erase the band, then one FONT_CHAR per cell -----
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop dx
    pop cx
    push cx
    push dx
    mov ax, cx
    mov bx, dx
    add cx, TB_W + TB_SKEW - 1      ; wide enough to cover the skewed pass too,
    add dx, 7                       ; so its ghost cannot outlive it
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    push cx
    push dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop dx
    pop cx
    mov si, [tb_bufp]
    mov di, TB_N
.cell:
    mov al, [si]
    call OSAPI_FONT_CHAR_XPARENT
    inc si
    add cx, 8
    dec di
    jnz .cell
    jmp short .out
.run:
    ; --- one opaque run: background and glyphs in a single pass -------------
    mov si, [tb_bufp]
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tb_reset - TB_N spaces and a NUL at SI, and the line empty again
; in:  SI = the buffer; preserves all registers
;
; SPACES and not zeros: font_run paints a space as background on its fast path
; (the glyph's rows are all clear, so the mask leaves the background byte),
; which is what makes one run erase the whole line. font_str draws nothing for
; one, which is why the CHAR method needs its fill.
; -----------------------------------------------------------------------------
tb_reset:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    cld
    mov di, si
    mov cx, TB_N
    mov al, ' '
    rep stosb
    mov byte [di], 0                ; DI landed on the last cell + 1
    mov word [tb_len], 0
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tb_report - format one row: seven digits of counts, then whole milliseconds
; in:  DX:AX = PIT counts, DI -> the row's count column
; out: DX:AX preserved
;
; One count is 838ns, so milliseconds are counts / 1193.18 - and 1193 is close
; enough that the error is under a tenth of a percent at any length this can
; measure. A 32-bit dividend by a 16-bit divisor is one `div` provided the
; quotient fits 16 bits: 65535 ms is 78 million counts, about 65 seconds.
;
; On REAL HARDWARE that column is the answer. Under QEMU with -icount the PIT
; counts instructions, and it is that count with a fixed factor on it.
; -----------------------------------------------------------------------------
tb_report:
    push ax
    push bx
    push cx
    push dx
    push di
    call tb_dec7
    add di, TB_MSOFF
    mov bx, 1193
    div bx
    xor dx, dx
    call tb_dec5
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tb_ms - DX:AX counts -> AX milliseconds, for the ratio below
; This replaced a `>> 4 and compare in 16 bits` that was sized against QEMU,
; where a row is ~3500 counts. On a real XT a row is 1.5 MILLION, so >> 4 is
; still 90,000 - it overflowed the word silently and the ratio came out 139
; where it should have been 110. Milliseconds cannot: the same 32-by-16 divide
; the report column already does, and 65535 ms is 65 seconds.
tb_ms:
    push bx
    mov bx, 1193
    div bx
    xor dx, dx
    pop bx
    ret

; tb_dec7 - DX:AX as seven decimal digits at DI, zero-suppressed. Preserves DX:AX.
tb_dec7:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    add di, 6
    mov cx, 7
.dig:
    call tb_div10
    add bl, '0'
    mov [di], bl
    dec di
    loop .dig
    pop di                          ; the digits' base...
    push di                         ; ...and a copy, because the suppression
                                    ; walk below advances DI and the caller
                                    ; needs it back: fb_report/tb_report add a
                                    ; column offset to it for the ms figure,
                                    ; and a drifting DI put those digits on top
                                    ; of the 'ms' label
    mov cx, 6
.sup:
    cmp byte [di], '0'
    jne .done
    mov byte [di], ' '
    inc di
    loop .sup
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tb_div10 - DX:AX /= 10, remainder in BX. Two halves, because the 8086
; divides 32 by 16 only when the quotient fits 16 bits and here it does not.
tb_div10:
    push cx
    push si
    mov cx, 10
    mov si, ax
    mov ax, dx
    xor dx, dx
    div cx
    mov bx, ax
    mov ax, si
    div cx
    mov si, dx
    mov dx, bx
    mov bx, si
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; tb_pit - latch and read counter 0 of the 8253
; out: AX = the counter (counts DOWN at 1.193182 MHz, reloads every 55ms)
; preserves all other registers
;
; Control word 00h latches a copy for reading without touching the counter's
; mode or reload value, so the system tick is undisturbed. The caller has
; IF=0 already: a latch interrupted between the two byte reads returns a torn
; count.
; -----------------------------------------------------------------------------
tb_pit:
    push dx
    mov al, 0
    out 0x43, al
    jmp short $+2                   ; I/O settling, the period-hardware idiom
    in al, 0x40
    mov dl, al
    jmp short $+2
    in al, 0x40
    mov ah, al
    mov al, dl
    pop dx
    ret

; -----------------------------------------------------------------------------
; tb_dec5 - AX as five decimal digits at DI, zero-suppressed
; in:  AX = value, DI -> five bytes; out: AX preserved
; -----------------------------------------------------------------------------
tb_dec5:
    push ax
    push bx
    push cx
    push dx
    push di
    add di, 4                       ; right to left
    mov cx, 5
    mov bx, 10
.dig:
    xor dx, dx
    div bx
    add dl, '0'
    mov [di], dl
    dec di
    loop .dig
    pop di                          ; the digits' base...
    push di                         ; ...and a copy, because the suppression
                                    ; walk below advances DI and the caller
                                    ; needs it back: fb_report/tb_report add a
                                    ; column offset to it for the ms figure,
                                    ; and a drifting DI put those digits on top
                                    ; of the 'ms' label
    mov cx, 4
.sup:
    cmp byte [di], '0'
    jne .done
    mov byte [di], ' '
    inc di
    loop .sup
.done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tb_head - name the adapter, and say whether the snap actually took
; preserves all registers
;
; ASKED, not assumed. A window too wide to snap is left unsnapped rather than
; made illegal (SPEC.md 11.94), so the only honest way to label the rows below
; is to look at where the content origin ended up. That habit is what caught
; the [vid_mono] gate: this line read snap:off on VGA while the rows underneath
; it said alignment was worth 9.4% there.
; -----------------------------------------------------------------------------
tb_head:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_VIDEO                ; DL = 0 VGA / 1 Hercules / 2 CGA
    mov si, tb_n_vga
    cmp dl, VID_HERC
    jne .have
    mov si, tb_n_herc
.have:
    cmp dl, VID_CGA
    jne .copy
    mov si, tb_n_cga
.copy:
    mov di, tb_s_head + TB_ACOL
    mov cx, 4
.c:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .c

    mov ax, [tb_cx]                 ; did the content origin land on 8?
    add ax, TB_MARGIN
    test ax, 7
    mov si, tb_n_on
    jz .snap
    mov si, tb_n_off
.snap:
    mov di, tb_s_head + TB_SCOL
    mov cx, 3
.s:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .s
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- data --------------------------------------------------------------------

tb_tpl:
    dw 120, 130, TB_CONT_W + 2, TB_CONT_H + TITLE_H + 1
    dw tb_ttl, tb_paint, tb_onkey, tb_onclick

tb_ttl:     db 'Type Bench', 0

TB_ACOL     equ 25                  ; where tb_head writes the adapter...
TB_SCOL     equ 36                  ; ...and whether the snap took
tb_s_head:  db 'Type Bench  N=40 keys    ....  snap:...', 0
tb_n_vga:   db 'VGA '
tb_n_herc:  db 'HERC'
tb_n_cga:   db 'CGA '
tb_n_on:    db 'on '
tb_n_off:   db 'off'

tb_s_hint:  db 'Click or press a key to type 40 characters.', 0

TB_COL      equ 15                  ; offsets INTO the rows below, so the
TB_MSOFF    equ 12                  ; layout lives in ONE place: label 0..13,
TB_RCOL     equ 15                  ; counts 15..21, 'cnt' 23..25, ms 27..31,
                                    ; 'ms' 34..35
tb_s_r0:    db 'CHAR aligned :         cnt        ms', 0
tb_s_r1:    db 'CHAR skewed 5:         cnt        ms', 0
tb_s_r2:    db 'RUN  aligned :         cnt        ms', 0
tb_s_rat:   db 'CHAR/RUN x100:      ', 0

tb_win:     dw 0
tb_cx:      dw 0
tb_cy:      dw 0
tb_tx:      dw 0
tb_ty:      dw 0
tb_mode:    dw 0
tb_len:     dw 0
tb_acc:     dw 0, 0                 ; the 32-bit elapsed count, folded per
tb_prev:    dw 0                    ; keystroke by tb_fold
tb_tchar:   dw 0, 0
tb_trun:    dw 0, 0
tb_done:    db 0
; One buffer per METHOD, not one shared: the two lines are meant to show what
; each method actually drew, and a single buffer would leave both of them
; holding whatever the last pass typed - two identical lines, one of which was
; never produced by the method labelling it. The skewed pass shares the CHAR
; buffer because it IS the CHAR method.
tb_bufc:    times TB_N + 1 db ' '
tb_bufr:    times TB_N + 1 db ' '
tb_bufp:    dw tb_bufc              ; the one the running row is typing into

    OS88_IMAGE_END
