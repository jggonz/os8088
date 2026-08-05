; =============================================================================
; os8088 - apps/typebench/typebench.asm
;
; TYPEBENCH: what a KEYSTROKE costs, measured the way Note Pad actually pays
; for one. SPEC.md 6.1.1 measured the primitive; this measures the workload -
; a line of text redrawn once per character typed, which is what np_redraw
; does to its dirty band (SPEC.md 27.2) and the reason that band exists.
;
; It is snappable itself (OSAPI_WM_SNAP, SPEC.md 11.94), so on a mono adapter
; its own text lands on multiples of 8 and the aligned rows below really are
; aligned. The header says which it got - ask rather than assume, because the
; flag is a no-op on VGA by design and a benchmark that claimed otherwise
; would be measuring the wrong thing and saying so confidently.
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
    call OSAPI_FONT_STR
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
    mov di, tb_s_r0 + TB_COL
    call tb_report

    mov word [tb_mode], 2           ; 2 = one FONT_RUN for the whole line
    mov ax, TB_Y_RUN
    call tb_row
    mov [tb_trun], ax
    mov di, tb_s_r2 + TB_COL
    call tb_report

    mov ax, [tb_tchar]              ; CHAR aligned / RUN aligned, x100
    mov bx, 100
    mul bx                          ; DX:AX - and an 8086 divide overflow is an
    mov bx, [tb_trun]               ; interrupt, not a wrong answer, so the
    or bx, bx                       ; guard below is not optional
    jz .norat
    cmp dx, bx
    jb .div
    mov ax, 0xFFFF
    jmp short .rat
.div:
    div bx
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
    push dx
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
    call tb_pit
    mov [tb_t0], ax
    mov cx, TB_N
.key:
    push cx
    call tb_key                     ; one keystroke: a character, then the
    pop cx                          ; whole line redrawn
    loop .key
    call tb_pit
    mov bx, [tb_t0]
    sub bx, ax                      ; modular: right across the reload too
    mov ax, bx
    cmp ax, 32768                   ; ...and only THAT is a real overrun
    jb .out
    mov ax, 0xFFFF
.out:
    popf
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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
    call OSAPI_FONT_CHAR
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
; tb_report - format one row: 'nnnnn PIT  nnnnn us'
; in:  AX = PIT counts, DI -> the row's count column
; out: AX preserved
;
; One count is 838ns. counts * 838 fits DX:AX for any 16-bit count, and the
; quotient by 1000 fits AX again, so the microseconds need no clamping - but
; they are only microseconds on real hardware. Under -icount the PIT counts
; instructions and this column is that count with a fixed factor on it.
; -----------------------------------------------------------------------------
tb_report:
    push ax
    push bx
    push dx
    push di
    call tb_dec5
    add di, TB_USOFF
    mov bx, 838
    mul bx
    mov bx, 1000
    div bx
    call tb_dec5
    pop di
    pop dx
    pop bx
    pop ax
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
    pop di
    mov cx, 4                       ; leading zeros -> spaces, all but the last
.sup:
    cmp byte [di], '0'
    jne .done
    mov byte [di], ' '
    inc di
    loop .sup
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tb_head - name the adapter, and say whether the snap actually took
; preserves all registers
;
; ASKED, not assumed. OSAPI_WM_SNAP is a no-op on VGA by design, and a window
; too wide to snap is left unsnapped rather than made illegal (SPEC.md 11.94),
; so the only honest way to label the rows below is to look at where the
; content origin ended up.
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

TB_COL      equ 15                  ; the count column in the three rows...
TB_USOFF    equ 10                  ; ...and how far past it the us column is.
                                    ; Both are offsets INTO the row below, so
                                    ; the layout is one place: label 0..13,
                                    ; count 15..19, 'PIT' 21..23, us 25..29
TB_RCOL     equ 15
tb_s_r0:    db 'CHAR aligned :       PIT       us', 0
tb_s_r1:    db 'CHAR skewed 5:       PIT       us', 0
tb_s_r2:    db 'RUN  aligned :       PIT       us', 0
tb_s_rat:   db 'CHAR/RUN x100:      ', 0

tb_win:     dw 0
tb_cx:      dw 0
tb_cy:      dw 0
tb_tx:      dw 0
tb_ty:      dw 0
tb_mode:    dw 0
tb_len:     dw 0
tb_t0:      dw 0
tb_tchar:   dw 0
tb_trun:    dw 0
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
