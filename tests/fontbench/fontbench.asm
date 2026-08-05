; =============================================================================
; os8088 - tests/fontbench/fontbench.asm
;
; FONTBENCH: the measurement behind SPEC.md 6.1.1. It times the two ways of
; drawing an opaque text run - the erase-and-letter PAIR every caller wrote
; by hand, and the single FONT_RUN call - against each other on whatever
; adapter it is booted on, and reports both in PIT counts.
;
; It is never shipped on the apps disks (their order is pinned): the Makefile
; builds it into its own scratch image, the fmtest/filetest precedent.
;
;   make test TESTAPPS=build/fontbench.img
;   make test VIDEO=cga TESTAPPS=build/fontbench.img
;   make test VIDEO=herc HERCSEG=0x7000 TESTAPPS=build/fontbench.img
;
; ...but boot it under QEMU with `-icount shift=3,sleep=off` or the numbers
; are the HOST's speed, which is not an 8088's and not worth quoting. With
; icount the PIT counts guest INSTRUCTIONS, so the answer is deterministic
; (+/-1 count across runs) and the same on any machine. What it said:
;
;   adapter            PAIR    PAIR    RUN     RUN
;                      aligned x+5     aligned x+5
;   VGA ([bb_on] = 0)   2694   2957     2763   3023
;   CGA 640x200         3400   3513     2695   3580
;   Hercules 720x348    3369   3483     2673   3548
;
; The SKEWED pair is the status quo - ui_drag writes W_X straight from the
; mouse, so a draggable window's content x is arbitrary mod 8 - and the
; aligned RUN is what a window would cost if something guaranteed alignment.
; 1.30x on mono; 2.4-2.5% AGAINST on VGA, where [bb_on] is 0 and every row
; takes the fallback whatever the alignment.
;
; --- how it is timed ---------------------------------------------------------
;
; NOT with GET_TICKS. A tick is 55ms and the whole point is the difference
; between two loops that take a few milliseconds, so a tick count answers 0
; or 1. Counter 0 of the 8253 is read directly instead: it is already running
; at 1.193182 MHz for the system tick, so latching it costs nothing and
; nobody's state is disturbed (latch is a read command, not a reprogram).
; One unit is 838ns and it wraps every 55ms, which is why FB_N is sized to
; keep a single measured run well inside one tick.
;
; Interrupts are OFF across each measured loop. Without that the scheduler
; pre-empts the benchmark mid-run and bills another task's time to whichever
; row was unlucky, and the numbers move by more than the effect being
; measured. The counter keeps counting with IF=0, so the measurement is
; unaffected - only the tick the kernel would have serviced is late, which
; costs a few milliseconds of clock drift per press and nothing else.
;
; The counter counts DOWN, so elapsed is start - end - and it MUST be the
; modular 16-bit subtraction, not a comparison. The counter reaches zero and
; reloads once every 55ms whatever the measurement is doing, so `end > start`
; does not mean the run overran: it means the run happened to straddle that
; reload, which for a 3ms row is a one-in-eighteen coincidence. Testing for
; it reported a perfectly good CGA row as an overflow, and only on some
; presses - `sub` with the borrow discarded is already right in both cases.
; What IS worth catching is a genuine overrun, and the threshold for that is
; half the range: every row here costs a few thousand counts, so a difference
; past 32768 is a run that lapped the counter and not a measurement.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'FONTBENCH', fb_entry

FB_N      equ 120                 ; runs per row. Sized so the slowest row
                                  ; (PAIR on Hercules) stays inside one 55ms
                                  ; PIT wrap while still costing thousands of
                                  ; counts, so the low digits are signal
FB_LEN    equ 10                  ; characters in the measured string
FB_W      equ FB_LEN * 8          ; ...and its pixel width
FB_SKEW   equ 5                   ; the UNALIGNED rows' offset. NOT 1: the ROM
                                  ; font's rightmost column is blank in every
                                  ; glyph, so a one-pixel shift spills nothing
                                  ; into the second byte and an unaligned run
                                  ; costs almost what an aligned one does -
                                  ; which flatters the status quo and is not
                                  ; where a dragged window usually lands. 5 is
                                  ; an ordinary shift

FB_CONT_W equ 304
FB_CONT_H equ 89

; -----------------------------------------------------------------------------
; fb_entry - package entry (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment; gfx lock NOT held
; out: BX = window ptr, CF set = refused
; -----------------------------------------------------------------------------
fb_entry:
    push si
    call fb_head                    ; name the adapter in the header line
    mov si, fb_tpl
    call OSAPI_WM_CREATE
    pop si
    ret                           ; NEAR: the kernel reaches every proc here
                                    ; through our own dispatcher (SPEC.md 20.1)

; -----------------------------------------------------------------------------
; fb_paint - W_PAINT: the three result lines, or the invitation
; in:  SI = window ptr; gfx lock held; content already white
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
fb_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [fb_cx], ax
    mov [fb_cy], dx
    mov al, CBLACK
    call OSAPI_SET_COLOR

    mov cx, [fb_cx]                 ; line 0: the adapter, so a screenshot
    add cx, 4                       ; says which machine produced the numbers
    mov dx, [fb_cy]
    add dx, 3
    mov si, fb_s_head
    call OSAPI_FONT_STR

    cmp byte [fb_done], 0
    jne .rows
    mov cx, [fb_cx]
    add cx, 4
    mov dx, [fb_cy]
    add dx, 25
    mov si, fb_s_hint
    call OSAPI_FONT_STR
    jmp short .out
.rows:
    mov si, fb_s_pair               ; the four measured rows
    mov dx, [fb_cy]
    add dx, 19
    call fb_line
    mov si, fb_s_runa
    add dx, 12
    call fb_line
    mov si, fb_s_pairu
    add dx, 12
    call fb_line
    mov si, fb_s_runu
    add dx, 12
    call fb_line
    mov si, fb_s_ratio
    add dx, 14
    call fb_line
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; fb_line - one result line at DX; SI = the string. Preserves all registers.
fb_line:
    push cx
    mov cx, [fb_cx]
    add cx, 4
    call OSAPI_FONT_STR
    pop cx
    ret

; -----------------------------------------------------------------------------
; fb_onclick - W_ONCLICK: run the benchmark, then repaint ourselves
; in:  SI = window ptr; gfx lock held
; out: nothing; preserves all registers
;
; A callback must repaint itself - the kernel does not do it afterwards
; (SPEC.md 12.3) - and here that matters twice over, because the measured
; loops have been scribbling the test string over the content the whole time.
; -----------------------------------------------------------------------------
fb_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [fb_win], si
    call fb_run
    mov byte [fb_done], 1

    mov bx, [fb_win]                ; wipe the scribbles, then draw the results
    call OSAPI_WM_CONTENT
    mov [fb_cx], ax
    mov [fb_cy], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [fb_cx]
    mov bx, [fb_cy]
    mov cx, ax
    add cx, FB_CONT_W - 1
    mov dx, bx
    add dx, FB_CONT_H - 1
    call OSAPI_GFX_FILL
    mov si, [fb_win]
    call fb_paint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

fb_onkey:                           ; a keypress runs it too, so the window
    jmp short fb_onclick            ; can be driven without aiming at content

; -----------------------------------------------------------------------------
; fb_run - measure all three rows and format them
; in:  [fb_win]; gfx lock held
; out: fb_s_pair/runa/runu/ratio filled in; preserves all registers
;
; The draw y is the LAST content row so the three loops cannot scribble over
; the result lines while they run, and the x is rounded UP to a multiple of 8
; so RUN-A gets the fast path whatever pixel the window was dragged to.
; -----------------------------------------------------------------------------
fb_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, [fb_win]
    call OSAPI_WM_CONTENT
    mov [fb_cx], ax
    mov [fb_cy], dx
    add ax, 7                       ; the aligned x: content left, rounded UP
    and ax, 0xFFF8
    mov [fb_bx], ax
    mov ax, [fb_cy]
    add ax, FB_CONT_H - 9
    mov [fb_by], ax

    mov word [fb_mode], 0           ; 0 = PAIR
    call fb_time
    mov di, fb_s_pair + FB_COL
    call fb_report

    mov word [fb_mode], 1           ; 1 = FONT_RUN, aligned
    call fb_time
    mov di, fb_s_runa + FB_COL
    call fb_report
    mov [fb_trun], ax               ; counts >> 4: the ratio below is 16-bit
    mov [fb_trun+2], dx             ; arithmetic over what is now a 32-bit
                                    ; measurement, and >> 4 keeps three digits
                                    ; of it while leaving headroom to 879ms
    mov word [fb_mode], 2           ; 2 = PAIR at the skewed x - the STATUS QUO
    call fb_time                    ; for a draggable window, which lands
    mov di, fb_s_pairu + FB_COL     ; unaligned seven times in eight
    call fb_report
    mov [fb_tpair], ax              ; ...and THIS is the ratio's numerator
    mov [fb_tpair+2], dx

    mov word [fb_mode], 3           ; 3 = FONT_RUN at the skewed x (the fallback)
    call fb_time
    mov di, fb_s_runu + FB_COL
    call fb_report

    mov ax, [fb_tpair]              ; skewed PAIR / aligned RUN, x100: what
    mov dx, [fb_tpair+2]            ; snapping a text window would actually buy
    call fb_ms
    mov bx, ax
    mov ax, [fb_trun]
    mov dx, [fb_trun+2]
    call fb_ms
    mov cx, ax                      ; CX = RUN >> 4, BX = PAIR >> 4
    mov ax, bx
    mov bx, 100
    mul bx                          ; DX:AX, so the div below needs a guard:
    or cx, cx                       ; an 8086 divide overflow is an interrupt,
    jz .noratio                     ; not a wrong answer, and a package taking
    cmp dx, cx                      ; int 0 hangs the machine
    jb .div
    mov ax, 0xFFFF                  ; would not fit: say so rather than trap
    jmp short .ratio
.div:
    div cx
.ratio:
    mov di, fb_s_ratio + FB_RCOL
    call fb_dec5
.noratio:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fb_time - run FB_N iterations of [fb_mode] with IF=0 and time them
; out: AX = elapsed PIT counts (65535 = the counter wrapped: overran a tick)
; preserves all other registers
; -----------------------------------------------------------------------------
fb_time:
    push bx
    push cx
    push si
    push di
    pushf
    cli                             ; no pre-emption inside the measurement -
                                    ; another task's timeslice billed to one
                                    ; row moves the answer more than the
                                    ; effect being measured
    mov word [fb_acc], 0
    mov word [fb_acc+2], 0
    call fb_pit
    mov [fb_prev], ax
    mov cx, FB_N
.loop:
    push cx
    call fb_once
    pop cx
    call fb_fold                    ; ...and fold THIS iteration in, before it
    loop .loop                      ; can be confused with the next one
    popf
    mov ax, [fb_acc]
    mov dx, [fb_acc+2]
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; fb_fold - add the counts since the last read into the 32-bit accumulator
; preserves all registers
;
; The counter is 16 bits and reloads every 55ms, so a single start-to-end
; subtraction can only measure a row shorter than that - and on a 4.77MHz 8088
; these rows are not. Reading it once per ITERATION fixes it completely: one
; iteration is a few thousand counts, far short of a reload, so each delta is
; unambiguous and the sum is exact however many times the counter lapped.
;
; This is what the first cut got wrong, and it got it wrong twice. It
; subtracted once across the whole row, so anything past 65536 counts wrapped
; silently into a small plausible number; and it then tried to catch that with
; a `>= 32768 means overrun` guard, which is not the same question and threw
; away every legitimate row between 32768 and 65535. On QEMU under -icount the
; rows were ~3000 counts and neither fault could show. On real hardware every
; row is tens of thousands, and both did.
;
; The per-iteration read costs a few instructions inside the measured window.
; Every row pays it identically, so no comparison between rows moves.
; -----------------------------------------------------------------------------
fb_fold:
    push ax
    push bx
    call fb_pit                     ; AX = the counter now
    mov bx, [fb_prev]
    mov [fb_prev], ax
    sub bx, ax                      ; it counts DOWN, so this is the elapsed
    add [fb_acc], bx                ; count - and modular, so the reload in
    adc word [fb_acc+2], 0          ; the middle of an iteration is free
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fb_once - draw the string once, the way [fb_mode] says
; preserves all registers
;
; The two colours and the rect are identical in all three, so what is being
; compared is only how the bytes get written.
; -----------------------------------------------------------------------------
fb_once:
    push ax
    push bx
    push cx
    push dx
    push si
    mov cx, [fb_bx]
    cmp word [fb_mode], 2           ; rows 2 and 3 draw FB_SKEW pixels right,
    jb .x                           ; which is all it takes to fail x & 7
    add cx, FB_SKEW
.x:
    mov dx, [fb_by]
    mov ax, [fb_mode]               ; 0 and 2 are the PAIR; 1 and 3 the call
    and ax, 1
    jz .pair
    jmp .run
.pair:

    mov al, CWHITE                  ; --- PAIR: fill the rect, letter over it
    call OSAPI_SET_COLOR
    mov ax, cx
    mov bx, dx
    push cx
    push dx
    add cx, FB_W - 1
    add dx, 7
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, fb_s_test
    call OSAPI_FONT_STR
    jmp short .out
.run:
    mov si, fb_s_test               ; --- FONT_RUN: both colours, one pass
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fb_report - format one row: seven digits of counts, then whole milliseconds
; in:  DX:AX = PIT counts, DI -> the row's count column
; out: DX:AX preserved
;
; One count is 838ns, so milliseconds are counts / 1193.18 - and 1193 is close
; enough that the error is under a tenth of a percent at any length this can
; measure. It is a 32-bit dividend by a 16-bit divisor, which the 8086 does in
; one `div` provided the quotient fits 16 bits: 65535 ms is 78 million counts,
; about 65 seconds, so it always does.
; -----------------------------------------------------------------------------
fb_report:
    push ax
    push bx
    push cx
    push dx
    push di
    call fb_dec7
    add di, FB_MSOFF
    mov bx, 1193
    div bx                          ; DX:AX / 1193 -> AX = ms
    xor dx, dx
    call fb_dec5
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; fb_ms - DX:AX counts -> AX milliseconds, for the ratio below
; This replaced a `>> 4 and compare in 16 bits` that was sized against QEMU,
; where a row is ~3500 counts. On a real XT a row is 1.5 MILLION, so >> 4 is
; still 90,000 - it overflowed the word silently and the ratio came out 696
; where it should have been 134. Milliseconds cannot: the same 32-by-16 divide
; the report column already does, and 65535 ms is 65 seconds.
fb_ms:
    push bx
    mov bx, 1193
    div bx
    xor dx, dx
    pop bx
    ret

; fb_dec7 - DX:AX as seven decimal digits at DI, zero-suppressed.
; Preserves DX:AX.
fb_dec7:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    add di, 6                       ; right to left
    mov cx, 7
.dig:
    call fb_div10                   ; DX:AX /= 10, CX-safe, remainder in BX
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

; fb_div10 - DX:AX /= 10, remainder in BX. The 8086 divides 32 by 16 only when
; the quotient fits 16 bits, which it does not here, so it is done in two
; halves with the first remainder carried into the second.
fb_div10:
    push cx
    push si
    mov cx, 10
    mov si, ax                      ; park the low half
    mov ax, dx
    xor dx, dx
    div cx                          ; AX = high quotient, DX = carry
    mov bx, ax                      ; ...park it too
    mov ax, si
    div cx                          ; AX = low quotient, DX = remainder
    mov si, dx
    mov dx, bx                      ; DX:AX = the 32-bit quotient
    mov bx, si                      ; BX = the remainder
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; fb_pit - latch and read counter 0 of the 8253
; out: AX = the counter (counts down, 1.193182 MHz, wraps every 55ms)
; preserves all other registers
;
; Latch (control word 00h) freezes a copy for reading without touching the
; counter's mode or reload value, so the system tick is not disturbed. The
; caller has IF=0 already; a latch that was interrupted between the two byte
; reads would return a torn count.
; -----------------------------------------------------------------------------
fb_pit:
    push dx
    mov al, 0
    out 0x43, al                    ; latch counter 0
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
; fb_dec5 - AX as five decimal digits at ES:DI (DS here), zero-suppressed
; in:  AX = value, DI -> five bytes; out: AX preserved
; -----------------------------------------------------------------------------
fb_dec5:
    push ax
    push bx
    push cx
    push dx
    push di
    add di, 4                       ; write right to left
    mov cx, 5
    mov bx, 10
.dig:
    xor dx, dx
    div bx                          ; AX = quotient, DX = digit
    add dl, '0'
    mov [di], dl
    dec di
    loop .dig
    pop di                          ; the digits' base...
    push di                         ; ...and a copy: the suppression walk below
                                    ; advances DI and the caller needs it back
    mov cx, 4                       ; leading zeros -> spaces, all but the last
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
; fb_head - name the adapter in the header line, once, at entry
; preserves all registers
; -----------------------------------------------------------------------------
fb_head:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_VIDEO                ; DL = 0 VGA / 1 Hercules / 2 CGA
    mov si, fb_n_vga
    cmp dl, VID_HERC
    jne .have
    mov si, fb_n_herc
.have:
    cmp dl, VID_CGA
    jne .copy
    mov si, fb_n_cga
.copy:
    mov di, fb_s_head + FB_ACOL
    mov cx, 4
.c:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .c
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- data --------------------------------------------------------------------

fb_tpl:
    dw 180, 150, 306, 108           ; x, y, w, h -> content 304 x 89
    dw fb_ttl, fb_paint, fb_onkey, fb_onclick

fb_ttl:     db 'Font Bench', 0

; The measured string is a tracker pattern cell - the run this was built for.
fb_s_test:  db 'C-2 01 A0F', 0

FB_ACOL     equ 19                  ; where fb_head writes the adapter name
fb_s_head:  db 'Font Bench  N=120  ....', 0
fb_n_vga:   db 'VGA '
fb_n_herc:  db 'HERC'
fb_n_cga:   db 'CGA '

fb_s_hint:  db 'Click or press a key to run.', 0

FB_COL      equ 15                  ; offsets INTO the rows below, so the
FB_MSOFF    equ 12                  ; layout lives in ONE place: label 0..13,
FB_RCOL     equ 19                  ; counts 15..21, 'cnt' 23..25, ms 27..31,
                                    ; 'ms' 33..34 - and the ratio row's own
                                    ; label is longer, so it has its own column
fb_s_pair:  db 'PAIR aligned :         cnt        ms', 0
fb_s_runa:  db 'RUN  aligned :         cnt        ms', 0
fb_s_pairu: db 'PAIR skewed 5:         cnt        ms', 0
fb_s_runu:  db 'RUN  skewed 5:         cnt        ms', 0
fb_s_ratio: db 'SKEWPAIR/RUN x100:      ', 0

fb_win:     dw 0
fb_cx:      dw 0
fb_cy:      dw 0
fb_bx:      dw 0
fb_by:      dw 0
fb_mode:    dw 0
fb_t0:      dw 0
fb_acc:     dw 0, 0                 ; the 32-bit elapsed count, folded per
fb_prev:    dw 0                    ; iteration by fb_fold
fb_tpair:   dw 0, 0
fb_trun:    dw 0, 0
fb_done:    db 0

    OS88_IMAGE_END
