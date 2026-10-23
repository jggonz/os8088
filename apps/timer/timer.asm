; =============================================================================
; os8088 - apps/timer/timer.asm
;
; TIMER (SPEC.md 111): a stopwatch. "HH:MM:SS" over a row of three buttons -
; Start, Stop, Reset - in a fixed 160x60 window. It was the kernel's Timer
; KIND (SPEC.md 14) from the first Clock to SPEC.md 14.8, and this package is
; that kind written as every other program on the disk is written: the same
; window, the same accumulator, the same incremental redraw of the digit
; cells that changed, the same greying rule, loaded from TIMER.O88 instead of
; assembled into every kernel.
;
; THE CLOCK IS THE WINDOW'S ONE-SHOT TIMER (SPEC.md 13.9, 14.7), re-armed for
; TM_TICKS from inside its own handler: it runs on the UI task, under the gfx
; lock, on a stack that is not a 128-byte slice - which is the whole of why
; the kind lost its task in 14.7. A machine whose kernel carries the slot and
; not the body (kern_small refuses OSAPI_WM_TIMER with CF = 1) gets the same
; clock from a WORKER instead, spawned at the first paint, which does from a
; task exactly what the handler does from the UI task: keep time, then draw
; only where the window can be seen. The kind never ran on kern_small at all
; (SPEC.md 14.6); the package runs on both kernels because a package can.
;
; THE TIME IS KEPT WHETHER OR NOT IT IS DRAWN. Every firing samples [ticks]
; and banks the sample, so a stopped interval is thrown away rather than owed
; (that is what Stop means); only a running timer adds the delta, in tenths
; of a tick, and every 182 tenths is one second with carries. A minimized or
; covered Timer keeps time, and OSAPI_WM_CLIP_SET's CF = 1 is what skips the
; drawing; a half-covered one redraws the half you can see (SPEC.md 14.4).
;
; ONLY THE CELLS THAT CHANGED ARE DRAWN (SPEC.md 14.1): tm_shown is the eight
; characters this window is displaying, and a pass letters the span from the
; first cell that differs to the last - one for a second going 30 -> 31,
; nothing at all on the firings that land in the same second. A line an edge
; cuts is drawn whole and FORGOTTEN, so a cell the clip refused is never
; recorded as shown. The digits sit at a content-relative x that is a multiple
; of 8 and the window asks for WF_SNAP, so every run takes OSAPI_FONT_RUN's
; single-store path (SPEC.md 6.1, 11.94).
;
; THE BUTTONS ARE THE STANDARD CONTROL (apps/os88ui.inc, SPEC.md 20.5.1.3):
; one record, rects the painter fills, and the library's own gesture - press,
; drag, release - so a button fires on the RELEASE and un-draws as the
; pointer slides off it (SPEC.md 13.8). Greying follows SPEC.md 47: tm_bok is
; the ONE predicate - Start is refused while running, Stop while stopped,
; Reset never - read by the painter's flags and by the release alike, so the
; grey and the refusal cannot disagree. A click on a greyed button does
; nothing, with no beep: the control already explains itself (47 rule 6).
;
; Opening the Timer STARTS it: the buttons are there to stop it and to put
; it back, not to arm it.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'Timer', tm_entry, 1, OS88_STACK_256 | OS88_HIGH | OS88_HIGHMEM
                                    ; flags bit 0: the icon below. The stack
                                    ; class is the kern_small worker's alone -
                                    ; it draws TEXT through font_run's per-cell
                                    ; path when a window cuts the digit line
                                    ; (SPEC.md 14.7 read 112 of 128 for that),
                                    ; so it asks for the class above the one
                                    ; the kind's task overflowed
    OS88_ICON16                     ; the stopwatch the kind wore (SPEC.md 29.3)
    ; --- mask ---
    dw 0x03C0, 0x03C0, 0x07E0, 0x0FF0
    dw 0x1FF8, 0x3FFC, 0x7FFE, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE
    dw 0x3FFC, 0x1FF8, 0x0FF0, 0x07E0
    ; --- data: crown on top, hands at 12 and 3 ---
    dw 0x03C0, 0x03C0, 0x07E0, 0x0C30
    dw 0x1818, 0x310C, 0x6106, 0x4102
    dw 0x4102, 0x41E2, 0x4002, 0x6006
    dw 0x300C, 0x1818, 0x0C30, 0x07E0
    OS88_ICON16_END

; --- geometry (SPEC.md 111.1) ------------------------------------------------
; The window is fixed-size and never resizable, and 160x60 is inside every
; adapter's desktop band (CGA's is the tightest at 640x157), so wm_fit cannot
; shrink it and the content box is TM_CW x TM_CH on all three - which is what
; lets these be constants rather than another read of OSAPI_VIDEO.
TM_W        equ 160                 ; frame width / height
TM_H        equ 60
TM_CW       equ TM_W - 2            ; content width = 158
TM_CH       equ TM_H - TITLE_H - 1  ; content height = 41
TM_DY       equ 5                   ; digits: top, content-relative
TM_DW       equ 8 * 8               ; ...and their 8-cell width
; ...and their left edge: centred across the content, then rounded to the
; NEAREST multiple of 8. The window carries WF_SNAP (SPEC.md 11.94), so its
; content origin is a multiple of 8; this constant is the other half of that
; bargain, and together they put every digit cell on a byte boundary where
; OSAPI_FONT_RUN's single-store path lives (SPEC.md 6.1). Centring alone
; gives 47, which is not; 1px of asymmetry is the price.
TM_DX       equ ((TM_CW - TM_DW) / 2 + 4) & ~7
%if TM_DX + TM_DW > TM_CW
%error "the Timer's digits do not fit its content once rounded onto a cell"
%endif
TM_NB       equ 3                   ; buttons: count, size and pitch
TM_BW       equ 46
TM_BH       equ 15
TM_BGAP     equ 6
TM_BY       equ 20                  ; ...top, content-relative: rows 20..34 of 41
TM_BX       equ (TM_CW - TM_NB * TM_BW - (TM_NB - 1) * TM_BGAP) / 2  ; = 4

TM_TICKS    equ 9                   ; the clock's period (SPEC.md 14.7): just
                                    ; under half a second, so a displayed
                                    ; second is never more than one firing late
TM_START    equ 0                   ; button indices - the order they are drawn,
TM_STOP     equ 1                   ; hit-tested and dispatched in
TM_RESET    equ 2

TM_HRS      equ 0                   ; the three clock fields, bytes, ADJACENT and
TM_MIN      equ 1                   ; DESCENDING in significance from tm_hms,
TM_SEC      equ 2                   ; which is what makes the carry walk one loop

; -----------------------------------------------------------------------------
; THE REGISTER CONTRACT IS THE CALLBACK'S, NOT "PRESERVE EVERYTHING" (SPEC.md
; 111.6). Every proc here is reached from one of four places, and each of
; those says what it owes: the ENTRY proc returns BX and CF and the loader
; reloads every other register it uses after the far call; W_PAINT owes SI and
; DF and may clobber AX, BX, CX, DX, DI and BP (SPEC.md 11, W_PAINT); the
; W_ONCLICK family - W_ONMOUSEUP, W_ONDRAG and W_ONTIMER are its environment
; exactly - may clobber AX/BX/CX/DX/DI/ES and gives SI back (SPEC.md 13.7,
; 13.9); and the kern_small worker reloads what it needs at the top of its
; loop. So the internal procs below say what they CLOBBER, and nothing pushes
; a register no caller reads - which was 52 bytes of this file and, on the
; per-tick path, twenty-odd pushes and pops a firing.
; -----------------------------------------------------------------------------

; -----------------------------------------------------------------------------
; tm_entry - package entry point (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, SI = the file we came out of,
;      IF = 1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; The loader shows the window; nothing here may show, draw or spawn.
; -----------------------------------------------------------------------------
tm_entry:
    mov si, tm_tpl                  ; the kind's window, where the kind put it.
                                    ; A second Timer opens on the first, as a
                                    ; second Note Pad does: the kind cascaded
                                    ; by pool slot and a package has no pool
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc tm_ret
    OS88_REGION_MOVABLE_NOKEEP tm_ret ; SPEC.md 66.6.1: here, where the window
                                    ; exists. Every byte that outlives a pass
                                    ; is a static and moves with us, so the
                                    ; proc is a bare `ret`; the kern_small
                                    ; worker declares itself restartable at its
                                    ; spawn. Spends AX, DX and the flags
    mov al, 1
    call OSAPI_WM_SNAP              ; content origin onto a multiple of 8, on
                                    ; EVERY adapter (SPEC.md 11.94)
    mov si, tm_about                ; the standard About (SPEC.md 12.2, 20.5.1)
    call OSAPI_ABOUT_SET
    ; --- the clock -----------------------------------------------------------
    mov ax, tm_tick
    call OSAPI_WM_ONTIMER
    mov ax, TM_TICKS
    call OSAPI_WM_TIMER             ; CF = 1: kern_small carries the slot and
    adc [tm_clk], ah                ; not the body (SPEC.md 13.9) - so the
                                    ; clock is a worker's (tm_clk 0 -> 1, AH
                                    ; being TM_TICKS's high byte, 0), hired at
                                    ; the first paint, not here: the loader has
                                    ; not published us yet
    ; --- fresh Timer: 00:00:00 (the bss is zero), seeded now, and RUNNING ---
    inc byte [tm_run]               ; 0 -> 1
    call tm_stamp
    ; --- the buttons' gesture: the library installs the three slots ---------
    xchg ax, bx                     ; AX = the window
    mov bx, tm_btrec
    mov si, tm_onup                 ; W_ONMOUSEUP: fire
    mov di, tm_ondrag               ; W_ONDRAG: track (refused on kern_small,
                                    ; and the library ignores that on purpose)
    mov dx, tm_onclick              ; a press that was not a button's: the
    call os88ui_btninit             ; About card's dismissal. It keeps every
    xchg ax, bx                     ; register, so this is BX = the window -
                                    ; which it has also written into the
                                    ; record (OS88UI_BT_WIN), and that word is
                                    ; the worker's window: no copy of our own
    clc                             ; the CF the loader is owed
tm_ret:
    ret

; -----------------------------------------------------------------------------
; tm_keep - sample the tick and advance the clock (SPEC.md 14: the accumulator
;           design is binding)
; in:  nothing
; out: nothing; clobbers AX, CX, DX, DI - and KEEPS BX and SI, which both of
;      its callers go on to use
;
; delta = ticks - tm_last (wrap-safe by subtraction); tm_last = ticks
; UNCONDITIONALLY - which is tm_stamp, so this calls it rather than saying
; it again, so a stopped interval is discarded rather than banked -
; leaving it alone would make Start jump forward by however long the timer
; had been stopped. Running: tm_acc += delta * 10, and every 182 tenths is a
; second, carried SEC -> MIN -> HRS through tm_lim's wrap limits. A long UI
; stall (a held-open menu) is one late firing owing many seconds, which the
; 32-bit product is for; the quotient is at most 3,602, so the DIV cannot
; fault.
; -----------------------------------------------------------------------------
tm_keep:
    mov dx, [tm_last]
    call tm_stamp                   ; AX = [tm_last] = the tick now
    sub ax, dx                      ; AX = the delta
    cmp byte [tm_run], 0
    je .out                         ; stopped: the delta goes nowhere
    mov dx, 10
    mul dx                          ; DX:AX = delta * 10, tenths of a tick
    add ax, [tm_acc]
    adc dx, 0
    mov cx, 182
    div cx                          ; AX = whole seconds owed, DX = leftover
    mov [tm_acc], dx
    xchg cx, ax
.adv:
    jcxz .out
    dec cx
    mov di, TM_SEC                  ; + 1 second, with carries: one walk from
.carry:                             ; SEC back to HRS over three ADJACENT,
    mov al, [tm_hms+di]             ; DESCENDING fields, each with its own wrap
    inc ax                          ; limit at the same offset in tm_lim (AL <
    cmp al, [tm_lim+di]             ; 60, so the word inc carries nowhere)
    jb .put
    mov al, 0                       ; wrapped - a MOV, so CF is still the cmp's
.put:
    mov [tm_hms+di], al
    jb .adv                         ; no wrap: this second is done
    dec di                          ; 2 -> 1 -> 0 -> -1: the sign ends the
    jns .carry                      ; ladder after hours wrap
    jmp short .adv
.out:
    ret

; -----------------------------------------------------------------------------
; tm_tick - W_ONTIMER (SPEC.md 13.9): re-arm, keep time, redraw the digits
;           that moved
; in:  SI = window ptr, CX = DX = 0; the UI task, gfx lock held - W_ONCLICK's
;      environment exactly. The clip is NOT armed for a timer dispatch, so
;      the handler arms it itself (SPEC.md 11.3)
; out: nothing; spends SI (the timer pass banks its own, SPEC.md 13.9)
;
; It FALLS INTO tm_clipd and that into tm_digits, so a firing is one call
; deep: the worker on kern_small enters at tm_clipd, after its own test.
; -----------------------------------------------------------------------------
tm_tick:
    mov bx, si
    mov ax, TM_TICKS                ; ONE-SHOT, disarmed before we were called,
    call OSAPI_WM_TIMER             ; so the repeat is ours to ask for
    call tm_keep                    ; time-keeping, every firing; keeps BX
; tm_clipd - arm the clip, and letter the digits only where it is visible
; in:  BX = window ptr; gfx lock held
tm_clipd:
    cmp byte [tm_btrec+OS88UI_BT_N], 0 ; the About card is up (tm_about): it
    je tm_keep.out                  ; is opaque over the digits, so the time is
                                    ; kept and NOT drawn - the dismissal's
                                    ; repaint letters the whole line again.
                                    ; Drawing here lettered the seconds through
                                    ; the card's text (SPEC.md 111.6)
    call OSAPI_WM_CLIP_SET         ; CF = 1: not one visible pixel (SPEC.md
    jc tm_keep.out                  ; 11.3), and hidden counts. The region dies
                                    ; at the kernel's own gfx_unlock
                                    ; ...and falls into tm_digits

; -----------------------------------------------------------------------------
; tm_digits - position and draw the HH:MM:SS line, the cells that CHANGED
; in:  BX = window ptr; gfx lock held, the clip armed or not
; out: nothing; clobbers AX, BX, CX, DX, SI, DI - SI being the run, and the
;      painter the one caller that owes it back, so it banks it there and the
;      per-tick path pays nothing for it
;
; ONE OPAQUE RUN rather than an erase and a lettering (SPEC.md 6.1): the
; erase-then-letter pair is PERFORMANCE.md Part 1's double-draw flash, twice
; a second for as long as a Timer is open. THE CLIP QUESTION IS ASKED ONCE,
; over the whole line: OSAPI_FONT_RUN decides per cell on both its paths and
; needs no gate of its own, but a run an edge cuts has cells it REFUSED, and
; tm_shown would then claim pixels nobody drew - so a cut line is drawn whole
; and then forgotten (SPEC.md 14.4, 28.2's rule).
; -----------------------------------------------------------------------------
tm_digits:
    ; --- format "HH:MM:SS" into tm_str ---------------------------------------
    ; The NUL cell, tm_str+8, is never written here: the loop stops at the
    ; seventh cell and the run's own terminator below writes only 1..8, so
    ; the zero the loader left there is the whole line's terminator for ever
    mov si, tm_hms
    mov di, tm_str
    mov cx, 3
    jmp short .pair                 ; no colon in front of the hours
.fmt:
    mov byte [di], ':'
    inc di
.pair:
    lodsb
    aam                             ; AH = tens, AL = units (the field is < 100)
    add ax, 0x3030
    xchg al, ah                     ; tens first in memory
    mov [di], ax
    inc di
    inc di
    loop .fmt
    ; --- may the WHOLE line be drawn? ----------------------------------------
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    add ax, TM_DX
    add dx, TM_DY
    mov bx, dx                      ; the inclusive rect: AX = x1, BX = y1,
    mov cx, ax                      ; CX = x2, DX = y2
    add cx, TM_DW - 1
    add dx, 7
    call OSAPI_WM_CLIP_TEST         ; preserves all four; "free" when nothing
    xchg cx, ax                     ; is armed, which is every W_PAINT.
    mov dx, bx                      ; CX = the line's x, DX = its y - two
    jc .whole                       ; moves, so the CF is still the test's
    ; --- which cells differ from what the window is showing? -----------------
    ; RIGHT TO LEFT, so the first difference met is the LAST cell - which is
    ; where the run is terminated, at once - and the last one met is the
    ; FIRST, which is where it starts
    mov bx, -1                      ; BX = the FIRST differing cell, none yet
    mov si, 7
.cell:
    mov al, [tm_str+si]
    cmp al, [tm_shown+si]
    je .next
    mov [tm_shown+si], al           ; adopted: this cell is about to be drawn
    or bx, bx
    jns .seen
    mov byte [tm_str+1+si], 0       ; the last changed cell ends the run - the
.seen:                              ; scratch is rebuilt every call
    mov bx, si
.next:
    dec si
    jns .cell
    or bx, bx
    js .out                         ; nothing differed: it already shows it
    lea si, [tm_str+bx]             ; SI = the run...
    shl bx, 1
    shl bx, 1
    shl bx, 1                       ; ...and its x, cell * 8
    add cx, bx
    jmp short .draw
.whole:
    call tm_forget                  ; some of these cells are about to be
    mov si, tm_str                  ; refused, so nothing may be remembered
.draw:
    mov al, CBLACK                  ; a button drawn just before may have left
    call OSAPI_SET_COLOR            ; the pen DISABLED (SPEC.md 47 rule 1): the
                                    ; bare store clears that flag, which is
                                    ; both a checkerboard glyph and font_run's
                                    ; slow path
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = ground
    call OSAPI_FONT_RUN             ; CX = x, DX = y, SI = the run
.out:
    ret

; -----------------------------------------------------------------------------
; tm_forget - this window is no longer showing anything we know about
; out: nothing; clobbers AX
; Zero is not a character any cell can hold, so eight of them make every cell
; differ and the next pass letters the whole line.
; -----------------------------------------------------------------------------
tm_forget:
    xor ax, ax
    mov [tm_shown+0], ax
    mov [tm_shown+2], ax
    mov [tm_shown+4], ax
    mov [tm_shown+6], ax
    ret

; -----------------------------------------------------------------------------
; tm_paint - W_PAINT: the digits and the three buttons, the About card last
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing; keeps SI, clobbers what W_PAINT may (AX, BX, CX, DX, DI)
; -----------------------------------------------------------------------------
tm_paint:
    push si                         ; the window, which W_PAINT owes back and
    mov bx, si                      ; tm_digits spends
    ; --- kern_small's clock: a WORKER (SPEC.md 111.3), hired once ------------
    ; OSAPI_WM_TIMER refuses on the kernel that carries the slot and not the
    ; body, so there the clock is a task - the shape the kind had before
    ; SPEC.md 14.7, on a stack class chosen for what 14.7 measured rather than
    ; guessed. Hired from the first paint, with the lock held, which is what
    ; OSAPI_TASK_SPAWN requires and an entry proc is not (the loader has not
    ; published us yet). A refusal is transient (the task table is full), so
    ; the flag is set only on success and the next paint tries again.
    cmp byte [tm_clk], 1            ; owed a worker and not yet holding one?
    jne .clk
    mov ax, tm_worker
    call OSAPI_TASK_SPAWN           ; keeps BX
    jc .clk
    inc byte [tm_clk]               ; 1 -> 2: hired
    OS88_WORKER_RESTARTABLE_NOKEEP tm_worker ; SPEC.md 66.6.2: the kernel wrote
                                    ; our segment into this worker's frame, and
                                    ; the park is inside OSAPI_TASK_ALIVE at the
                                    ; top of the loop - every byte that outlives
                                    ; a pass is a static and moves with us
.clk:
    call tm_forget                  ; the content has just been white-filled,
                                    ; so every cell this window was showing is
                                    ; gone and the redraw letters all eight
    ; --- the buttons' inclusive screen rects, from the content origin -------
    ; The drawn control and the clickable control are these same four words
    ; (SPEC.md 22.2's fm_hit discipline, which the standard control enforces
    ; by construction): the painter fills them and the library's press reads
    ; them.
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    add ax, TM_BX
    add dx, TM_BY
    mov cx, dx
    add cx, TM_BH - 1               ; CX = y2, the same for all three
    mov di, tm_rects
.rect:
    mov [di+0], ax                  ; x1
    mov [di+2], dx                  ; y1
    add ax, TM_BW - 1
    mov [di+4], ax                  ; x2
    mov [di+6], cx                  ; y2
    add ax, TM_BGAP + 1             ; the next button's left edge
    add di, 8
    cmp di, tm_rects + 8 * TM_NB
    jb .rect
    call tm_digits                  ; BX = the window still
    mov dl, TM_NB + 1               ; ...then the greying and all three buttons
    call tm_flags
    pop si
    cmp byte [tm_btrec+OS88UI_BT_N], 0 ; ...and the About card LAST, over the
    jne .out                        ; content it is opaque about (SPEC.md
                                    ; 20.5.1). The card is up exactly while the
                                    ; record has no buttons (tm_about)
    mov bx, si
    mov si, tm_ablines
    call os88ui_about_d             ; _d: this paint's region is already armed;
    mov si, bx                      ; it keeps every register
.out:
    ret

; -----------------------------------------------------------------------------
; tm_flags - the three buttons' flag words from the state (SPEC.md 47), then
;            draw the first DL - 1 of them
; in:  DL = TM_STOP + 2 (Start and Stop, which trade pens) or TM_NB + 1 (all)
; out: nothing; clobbers AX, BX, DI
;
; OS88UI_FILL on all three: a button is redrawn over its previous pen's frame
; after a click, so the erase is never optional. OS88UI_DIS on exactly the one
; tm_bok refuses - the SAME predicate the release reads, so the greying and
; the refusal cannot disagree (47 rule 2).
; -----------------------------------------------------------------------------
tm_flags:
    mov di, tm_bflags + 2 * (TM_NB - 1)
    mov ax, TM_NB - 1
.one:
    call tm_bok                     ; CF = 1: this one would refuse; AX kept
    mov bx, OS88UI_FILL
    jnc .live
    inc bx                          ; ...| OS88UI_DIS
.live:
    mov [di], bx
    dec di
    dec di
    dec ax
    jns .one
    mov bx, tm_btrec
    mov al, 1                       ; index PLUS ONE, os88ui_btn's convention
.btn:                               ; (it reads AL alone)
    call os88ui_btn                 ; keeps every register
    inc ax
    cmp al, dl
    jb .btn
    ret

; -----------------------------------------------------------------------------
; tm_bok - would this button do anything? (SPEC.md 47 rule 2: the ONE
;          predicate, shared by the greying and the click refusal)
; in:  AL = button index
; out: CF = 0 live, CF = 1 refused; every register preserved
;
; The whole ladder is one XOR, because the button order and tm_run's two
; values line up: index XOR run == 1 is EXACTLY the two refusals -
; Start(0) while running(1), Stop(1) while stopped(0) - and nothing else.
; tm_run is only ever written 0 or 1.
; -----------------------------------------------------------------------------
%if TM_START != 0 || TM_STOP != 1 || TM_RESET != 2
%error "tm_bok's XOR needs Start/Stop/Reset at 0/1/2"
%endif
tm_bok:
    push ax
    xor al, [tm_run]                ; 1 exactly for the greyed button...
    dec al                          ; ...which is 0 here...
    cmp al, 1                       ; ...and the only value below 1: CF = greyed
    pop ax                          ; pop touches no flag, which is what
    ret                             ; preserves AX *and* the answer

; -----------------------------------------------------------------------------
; tm_ondrag - W_ONDRAG (SPEC.md 13.8.1): the pressed button follows the pointer
; in:  CX = x, DX = y (SCREEN), SI = window ptr; gfx lock held
; -----------------------------------------------------------------------------
tm_ondrag:
    mov bx, tm_btrec
    jmp os88ui_btndrag              ; redraws only on a CHANGE

; -----------------------------------------------------------------------------
; tm_onup - W_ONMOUSEUP (SPEC.md 13.7): a button fires HERE, and does what it
;           says (SPEC.md 111.2)
; in:  CX = x, DX = y (SCREEN, possibly outside the window), SI = window ptr;
;      gfx lock held
; out: nothing; a Reset spends SI in tm_digits, and the release's dispatch
;      reads nothing back (ui_bill, then the unlock)
;
; Start sets tm_run and re-stamps the sample - up to TM_TICKS of stopped time
; can be sitting unsampled at the click; Stop clears tm_run; Reset zeroes
; h/m/s AND the accumulator AND re-stamps, and leaves tm_run alone, so a
; reset while running keeps running. Start/Stop redraw THOSE TWO buttons and
; nothing else (they traded pens); Reset redraws the digits that changed and
; nothing else.
; -----------------------------------------------------------------------------
tm_onup:
    mov bx, tm_btrec
    call os88ui_btnup               ; AX = what FIRED, index + 1; 0 = cancelled.
    dec ax                          ; The released button is already upright
    js .out                         ; 0-based, and -1 is the cancel
    call tm_bok                     ; a greyed control explains itself, so a
    jc .out                         ; refused click says nothing more (47 r6)
    cmp al, TM_RESET
    je .reset
    xor al, 1                       ; Start (0) runs it, Stop (1) stops it -
    mov [tm_run], al                ; the 0/1 tm_bok asserts
    jz .pens                        ; stopped: nothing to re-stamp
    call tm_stamp                   ; started: the stopped interval is
.pens:                              ; discarded here as well as in tm_keep
    mov dl, TM_STOP + 2             ; Start and Stop just traded pens
    jmp short tm_flags
.reset:
    call tm_zero
    mov bx, si
    jmp tm_digits
.out:
    ret

; -----------------------------------------------------------------------------
; tm_zero / tm_stamp - put the clock back, and re-sample
; out: nothing; clobbers AX
; tm_run is deliberately NOT touched by tm_zero. HRS and MIN are the two bytes
; of one word, SEC the byte after; tm_acc is the part-second at the other end.
; -----------------------------------------------------------------------------
tm_zero:
    xor ax, ax
    mov [tm_hms+TM_HRS], ax         ; +0 and +1 in one store
    mov [tm_hms+TM_SEC], al
    mov [tm_acc], ax
tm_stamp:
    call OSAPI_GET_TICKS
    mov [tm_last], ax
    ret

; -----------------------------------------------------------------------------
; tm_worker - kern_small's clock (SPEC.md 111.3, 20.6): keep time and draw,
;             every TM_TICKS, from a task
; in:  DX = our instance index, ES = KERNEL_SEG (the task frame's, and nothing
;      below changes it - every slot keeps every register it does not
;      document); never returns
; -----------------------------------------------------------------------------
tm_worker:
.loop:
    mov bx, [tm_btrec+OS88UI_BT_WIN] ; our window, as os88ui_btninit wrote it
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here; a clicked
                                    ; close box never returns
    mov ax, TM_TICKS
    call OSAPI_TASK_SLEEP
    call OSAPI_GFX_LOCK             ; 20.6 rule 5's sequence: lock, re-check
    call tm_keep                    ; visibility under the lock, arm the clip.
    test byte [es:bx+W_FLAGS], 2    ; tm_keep keeps BX. Still visible? (hidden
    jz .skip                        ; would answer the clip's CF = 1 too; the
    call tm_clipd                   ; rule names the test)
.skip:
    call OSAPI_GFX_UNLOCK           ; ...which also disarms the clip
    jmp short .loop

; =============================================================================
; 'About Timer' - the standard card (SPEC.md 12.2, 20.5.1)
; =============================================================================
; tm_about - the OSAPI_ABOUT_SET handler
; in:  SI = our window ptr; the UI task, gfx lock HELD
; While the card is up the button record is told it has NO buttons, so a
; press anywhere in the content reaches tm_onclick and takes the card down
; rather than pressing the button under it - and that zero IS the card's
; state: no flag byte of its own, so the two cannot disagree. The count's
; high byte is always 0 (it is 0 or TM_NB), so the stores are bytes.
tm_about:
    mov byte [tm_btrec+OS88UI_BT_N], 0
    mov bx, si
    mov si, tm_ablines
    jmp os88ui_about                ; arms the clip itself; a tail call

; tm_onclick - the library's chain for a press that was not a button's: the
;              About card's dismissal (SPEC.md 20.5.1)
; in:  CX = x, DX = y, SI = our window ptr; gfx lock held
; out: CF = 1 the press was spent on the card; keeps SI, clobbers the rest
;      as a W_ONCLICK may
;
; os88ui_abdismiss's cmp/clear/clip/repaint/answer, written out: with the
; card's state being the button count, the clear and the test are ONE xchg,
; and the repaint is inline rather than a proc handed over in AX.
tm_onclick:
    mov al, TM_NB
    xchg al, [tm_btrec+OS88UI_BT_N] ; the buttons are back, whatever was up,
    cmp al, 1                       ; and CF = 1 exactly when there were none:
    jnc .out                        ; no card - CF = 0, the press is not ours
    mov bx, si
    call OSAPI_WM_CLIP_SET          ; CF = 1: not visible - spent all the same,
    jc .out                         ; and the next paint draws no card
    mov al, CWHITE                  ; --- erase the content, draw it again ---
    call OSAPI_SET_COLOR
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov bx, dx                      ; y1 = top
    mov cx, ax
    add cx, TM_CW - 1               ; x2
    add dx, TM_CH - 1               ; y2
    call OSAPI_GFX_FILL             ; AX = x1 already
    call tm_paint                   ; SI = window ptr still; keeps it
    stc
.out:
    ret

; --- data ----------------------------------------------------------------------
; window template (SPEC.md 11: 16 bytes, 8 words), at the kind's position.
; No W_ONCLICK word: os88ui_btninit installs the library's, which chains to
; tm_onclick.
tm_tpl:     dw 330, 60, TM_W, TM_H
            dw tm_ttl, tm_paint, 0, 0

; The title is the header's NAME FIELD (SPEC.md 20.2, +16): OS88_HEADER
; NUL-pads it to 16 bytes and refuses a name over 15, so it is a terminated
; 'Timer' already, in our segment like any other string.
tm_ttl      equ 16
tm_s_start: db 'Start', 0
tm_s_stop:  db 'Stop', 0
tm_s_reset: db 'Reset', 0
tm_btlbl:   dw tm_s_start, tm_s_stop, tm_s_reset

; The wrap limit of each clock field, INDEXED BY THAT FIELD'S OWN OFFSET, so
; tm_keep's carry walk is one loop rather than three copies of it.
%if TM_HRS != 0 || TM_MIN != 1 || TM_SEC != 2
%error "tm_lim is indexed by TM_HRS/MIN/SEC and needs them at 0/1/2"
%endif
tm_lim:     db 24, 60, 60

; the buttons' record (SPEC.md 20.5.1.3): rects and flags in the bss, filled
; by tm_rects and tm_flags; labels above
    OS88UI_BTNREC tm_btrec, tm_rects, tm_btlbl, tm_bflags, TM_NB

; the standard About card's lines (SPEC.md 20.5.1). The card is 2*7 + lines*12
; tall and the content is 41, so TWO lines is this window's ceiling.
tm_ablines:
    dw tm_ab1, tm_ab2, 0
tm_ab1:     db 'Timer for os8088', 0
tm_ab2:     db 'a stopwatch', 0

; --- the shared controls (SPEC.md 20.5.1) ---------------------------------------
%define OS88UI_ABOUT            ; the standard About card...
                                ; (taken down by tm_onclick itself, not
                                ; OS88UI_ABDISMISS: SPEC.md 111.6)
%define OS88UI_NOGLYPH          ; no check box or radio drawn here (SPEC.md 13.8.9)
%include "os88ui.inc"           ; ...and the button, with its gesture
%if OS88UI_DIS != 1 || (OS88UI_FILL & 1)
%error "tm_flags greys with an inc: it needs OS88UI_DIS at bit 0 and FILL clear of it"
%endif

    OS88_BSS 56
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) --------------------------------------
tm_hms      equ os88_image_end + 0  ; 3 bytes: hours, minutes, seconds (TM_HRS..)
tm_run      equ os88_image_end + 3  ; byte: 0 stopped, 1 running
tm_last     equ os88_image_end + 4  ; word: last [ticks] sample
tm_acc      equ os88_image_end + 6  ; word: tenth-of-tick accumulator
tm_shown    equ os88_image_end + 8  ; 8: the cells this window is DISPLAYING;
                                    ; all zero = nothing here describes the glass
tm_str      equ os88_image_end + 16 ; 9: "HH:MM:SS",0 scratch (under the lock);
                                    ; the NUL at +8 is the loader's, never written
tm_clk      equ os88_image_end + 25 ; byte: whose clock - 0 the window's timer,
                                    ; 1 owed a worker (kern_small), 2 hired
tm_rects    equ os88_image_end + 26 ; 3 x 4 words: the buttons, screen, inclusive
tm_bflags   equ os88_image_end + 50 ; 3 words: OS88UI_FILL, and DIS on the refused one
%if tm_bflags + 2 * TM_NB - os88_image_end != 56
%error "the Timer's bss layout and OS88_BSS disagree"
%endif
