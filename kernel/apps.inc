; =============================================================================
; os8088 - apps.inc
;
; The built-in application KINDS (SPEC.md 14): About dialog, Note Pad, Timer
; and Bounce. Nothing is created at boot - instances are launched on demand
; by app_launch (SPEC.md 29), which allocates a state block from the kind's
; pool, creates the window from the kind's template, and spawns the kind's
; task (Timer/Bounce run one task per instance). Closing an instance frees
; all of it: the UI task sets the die flag (I_STATE=2) and hides the window;
; the task notices at its next wake and tears down via inst_task_die.
;
; Paint procs and key handlers run with the gfx lock already held by the
; caller (SPEC.md 11), find their instance state via inst_of_win + I_SPTR,
; and never lock, block or spawn; the Timer/Bounce tasks take the lock
; themselves and arm a clip region under it (SPEC.md 11.3) before touching
; the screen. Background tasks use only os8088 primitives and [ticks] - no
; BIOS calls (SPEC.md 7).
;
; app_bounce_task below is the REFERENCE IMPLEMENTATION for a package's
; worker task (SPEC.md 20.6): sleep, take the lock, wm_clip_set under it,
; draw briefly, release, check the die flag. Keep its shape - the author
; rules point at it.
; =============================================================================

; --- geometry ----------------------------------------------------------------
APP_BALL_XMAX   equ 150 - 2 - 8             ; bounce content w=148, 8px square
APP_BALL_YMAX   equ 130 - TITLE_H - 1 - 8   ; bounce content rows are
                                            ; y+TITLE_H..y+h-2 inclusive = 111

; Timer (SPEC.md 14): "HH:MM:SS" over a row of three buttons. The window is
; fixed-size and never resizable, and 160x60 is inside every adapter's
; desktop band (CGA's is the tightest at 640x157), so wm_fit cannot shrink it
; and the content box is APP_TMR_CW x 41 on all three - which is what lets
; these be constants rather than another read of [vid_*] (SPEC.md 1/39.7).
APP_TMR_W    equ 160                        ; frame width / height
APP_TMR_H    equ 60
APP_TMR_CW   equ APP_TMR_W - 2              ; content width = 158
APP_TMR_DY   equ 5                          ; digits: top, content-relative
APP_TMR_DW   equ 8 * 8                      ; ...and their 8-cell width
; ...and their left edge: centred across the content, then rounded to the
; NEAREST multiple of 8. The window carries WF_SNAP (SPEC.md 11.94), so its
; content origin is a multiple of 8 on the two mono adapters; this constant is
; the other half of that bargain, and together they put every digit cell on a
; byte boundary where font_run's single-store path lives (SPEC.md 6.1/6.1.4).
; Centring alone gives 47, which is not, and the fast path would never fire.
; The rounding is worth 1px of asymmetry (48 left of the run, 46 right) and it
; is the whole reason app_tmr_pos stopped computing this from [bx+W_W].
APP_TMR_DX   equ ((APP_TMR_CW - APP_TMR_DW) / 2 + 4) & ~7
%if APP_TMR_DX + APP_TMR_DW > APP_TMR_CW
%error "the Timer's digits do not fit its content once rounded onto a cell"
%endif
APP_TMR_NB   equ 3                          ; buttons: count, size and pitch
APP_TMR_BW   equ 46
APP_TMR_BH   equ 15
APP_TMR_BGAP equ 6
APP_TMR_BY   equ 20                         ; ...top, content-relative: rows
                                            ; 20..34 of the 41 the content has
APP_TMR_BX   equ (APP_TMR_CW - APP_TMR_NB * APP_TMR_BW \
                  - (APP_TMR_NB - 1) * APP_TMR_BGAP) / 2   ; = 4, the left inset
APP_TMR_LT   equ (APP_TMR_BH - 8) / 2       ; a label's top within its button

APP_TMR_START equ 0             ; button indices - the order they are drawn,
APP_TMR_STOP  equ 1             ; hit-tested and dispatched in
APP_TMR_RESET equ 2

; --- per-instance state layouts (pool strides pinned in SPEC.md 29.3) --------
TMR_HRS   equ 0                 ; byte
TMR_MIN   equ 1                 ; byte
TMR_SEC   equ 2                 ; byte
TMR_RUN   equ 3                 ; byte: 0 stopped, 1 running (the Clock's old
                                ; padding byte, which is why the buttons cost
                                ; no memory - unlike TMR_SHOWN below)
TMR_LAST  equ 4                 ; word: last [ticks] sample
TMR_ACC   equ 6                 ; word: tenth-of-tick accumulator
TMR_SHOWN equ 8                 ; 8 bytes: the characters this window is
                                ; DISPLAYING, so a redraw can be the cells
                                ; that changed and nothing else (SPEC.md 14).
                                ; All-zero = "nothing here describes the
                                ; screen" - see app_tmr_forget
TMR_SIZE  equ 16                ; stride, and every byte of it is used

BAL_X     equ 0                 ; words: content-relative pos + velocity
BAL_Y     equ 2
BAL_VX    equ 4
BAL_VY    equ 6                 ; stride 8

; --- window templates: {x, y, w, h, title, paint, onkey, onclick} words ------
; The Timer's x keeps its whole cap-10 cascade (+16 per pool slot, SPEC.md
; 29.4) on a 640px screen: 330 + 144 + 160 = 634.
app_tpl_about:  dw 170, 140, 300, 120, app_ttl_about,  app_about_paint,  0, 0
app_tpl_timer:  dw 330,  60, APP_TMR_W, APP_TMR_H, app_ttl_timer, app_tmr_paint
                dw 0, app_tmr_onclick
app_tpl_bounce: dw 300, 150, 150, 130, app_ttl_bounce, app_bounce_paint, 0, 0

app_ttl_about:  db 'About os8088', 0
app_ttl_timer:  db 'Timer', 0
app_ttl_bounce: db 'Bounce', 0

app_tmr_s_start: db 'Start', 0
app_tmr_s_stop:  db 'Stop', 0
app_tmr_s_reset: db 'Reset', 0
app_tmr_lbl:     dw app_tmr_s_start, app_tmr_s_stop, app_tmr_s_reset
%ifdef KERN_BIG
; Which button is DRAWN pressed, 1-based so 0 can mean "none" (SPEC.md 13.8).
; ONE byte for the machine and not one per Timer, which is safe for the reason
; os88ui_armw is: a gesture is one press and one release on the UI task under
; the gfx lock, and the arm that reaches app_tmr_onup is the same singleton.
; The window comes in SI on every edge, so the painter never has to ask WHOSE.
; Inline .text data, like [cp_down] and for its reason.
app_tmr_down:    db 0
%endif

; First line: the version, and the BUILD NUMBER after it (SPEC.md 14.2).
; BUILD_NUM counts the commits behind this checkout, so it goes up by one per
; commit, reads as a number rather than as a hash, and is the SAME number on
; every machine that builds this commit - which is what keeps two builds of
; the same source byte-identical (tools/buildnum.py has the whole argument).
; 0 is 'this build could not determine it' - a shallow clone or no git at all -
; and then the line is exactly the one that shipped before there was a build
; number, because a missing number is cheaper to live with than a wrong one.
%include "buildnum.inc"         ; GENERATED by tools/buildnum.py, found by
                                ; -I $(BUILD)/ like associco.inc above it
%if BUILD_NUM
app_about_s1:   db 'os8088 1.0  Build ', BUILD_STR, 0
%else
app_about_s1:   db 'os8088 1.0', 0
%endif
app_about_s2:   db 'a graphical OS for the 8086', 0
; third line, one word per scheduler mode (SPEC.md 8.2/14)
app_about_s3p:  db 'pre-emptive', 0
app_about_s3c:  db 'cooperative', 0
; fourth line: the screen this machine actually got (SPEC.md 39). Indexed by
; [vid_kind], so it can never disagree with what is on the glass - the old
; third line asserted '640x480 - 16 colors' unconditionally, which stopped
; being true the moment the kernel learned to drive a mono card.
app_about_vid:  dw app_about_v0, app_about_v1, app_about_v2
app_about_v0:   db 'VGA - 640x480 - 16 colors', 0
app_about_v1:   db 'Hercules - 720x348 - mono', 0
app_about_v2:   db 'CGA - 640x200 - mono', 0

; =============================================================================
; EVERYTHING BELOW IS COLD (SPEC.md 2.6). The data above is not, and cannot
; be: DS is KERNEL_SEG in cold code, so rule 1 keeps the templates, the
; titles, the About lines and the Timer's button labels here, where
; wm_create, wm_draw_title, font_str and os88ui_btn all read them through DS.
;
; The kinds pass SPEC.md 2.6's CADENCE test rather than a size test. The
; heaviest thing any of this does per wake is two gfx_fills, ~1.5ms on a
; 4.77MHz 8088, and Bounce - the fastest of the three - wakes every 2 ticks.
; A crossing is ~54 cycles in (PERFORMANCE.md Set 109), so the far calls a
; frame owes are microseconds against that. tests/bouncecost.py is the
; measurement, taken because the estimate is not the evidence.
;
; It also made four crossings DISAPPEAR: os88ui_arm/fire/armed/btn are cold
; (apps/os88ui.inc), and the Timer reached all four through their _f far
; entries while it was resident. Those are near calls now - SPEC.md 2.6's
; "growing the set makes the ones already in it cheaper", and the comment
; above os88ui_btn_f names apps.inc as one of the two callers it exists for.
; The _f entries STAY: ctrl.inc is an on-demand module and still needs them.
; =============================================================================
section .cold

; =============================================================================
; Kind init procs (KD_INIT contract, SPEC.md 29.3): in BX = window ptr,
; DI = state block ptr, SI = instance record ptr; preserve all registers;
; run on the UI task with no lock held, window not yet visible.
; =============================================================================

; ---- app_tmr_kinit - fresh Timer: 00:00:00, seeded now, and RUNNING ---------
; Opening the Timer starts it (SPEC.md 14) - the buttons are there to stop it
; and to put it back, not to arm it.
app_tmr_kinit_x:
    push ax
    mov byte [ss:di+TMR_HRS], 0
    mov byte [ss:di+TMR_MIN], 0
    mov byte [ss:di+TMR_SEC], 0
    mov byte [ss:di+TMR_RUN], 1
    mov ax, [ticks]
    mov [ss:di+TMR_LAST], ax
    mov word [ss:di+TMR_ACC], 0
    call app_tmr_forget         ; a pool slot is REUSED (SPEC.md 29.4), so the
                                ; previous instance's cells are sitting there
                                ; describing a window that has been destroyed
%ifdef KERN_BIG
    mov ax, app_tmr_onup        ; SPEC.md 13.7/13.8.1: the two edges the three
    call KERNEL_SEG:cw_wm_onmouseup           ; buttons fire and track on. Not template
    mov ax, app_tmr_ondrag      ; words, so a KD_INIT is where a kernel window
    call KERNEL_SEG:cw_wm_ondrag              ; installs them - and a REUSED pool slot must
    mov byte [app_tmr_down], 0  ; not hand a pressed button to its successor
%endif
    mov al, 1                   ; the content origin onto a multiple of 8, so
    call KERNEL_SEG:cw_wm_snap                ; every digit run takes font_run's single-store
                                ; path (SPEC.md 6.1/11.94). BX = window ptr by
                                ; the KD_INIT contract, and this is the caller
                                ; that section is written for: before the
                                ; window is shown, where moving it costs
                                ; nothing. EVERY adapter - it was mono only
                                ; until VGA was measured and gained more
                                ; (verified aligned there - content origin
                                ; 328) - so it is still asked for
                                ; unconditionally, just no longer for free
    pop ax
    retf

; ---- app_bounce_kinit - fresh Bounce: ball at (4,4), velocity (3,2) ----------
app_bounce_kinit_x:
    mov word [ss:di+BAL_X], 4
    mov word [ss:di+BAL_Y], 4
    mov word [ss:di+BAL_VX], 3
    mov word [ss:di+BAL_VY], 2
    retf

; --- apf_app_about_center - the far side of app_about_center's thunk --------
; A PAD rather than SPEC.md 2.6.1's retf body, because app_about_paint_x near-
; calls the body four times from in here. One of the two shapes has to give,
; and four near calls are worth more than the pad's four bytes.
apf_app_about_center:
    call app_about_center_x
    retf

; =============================================================================
; About
; =============================================================================

; -----------------------------------------------------------------------------
; app_about_paint - W_PAINT for the About window (stateless)
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; Four black lines, each centered in the content. The third line's
; scheduling word tracks the live mode (sched_mode_get) and the fourth names
; the video adapter the boot probe found (SPEC.md 39.1). app_about_center
; re-measures every string, so unequal lengths are fine.
; -----------------------------------------------------------------------------
app_about_paint_x:
    push ax
    push bx
    push cx
    push dx
    push si

                                ; ...and no pen: app_about_center is an
                                ; OPAQUE run now (SPEC.md 6.6.5) and nothing
                                ; else in this window draws
    mov bx, si                  ; window ptr for the centering helper
    mov dx, [bx+W_Y]
    add dx, TITLE_H + 22        ; first line: content top + 22 (four lines of
                                ; 8px on 16px pitch, centred in 101px)
    mov si, app_about_s1
    call app_about_center_x
    add dx, 16
    mov si, app_about_s2
    call app_about_center_x
    add dx, 16
    mov si, app_about_s3p       ; third line follows the live scheduler mode
    call KERNEL_SEG:cw_sched_mode_get         ; AL = 0 pre-emptive, 1 cooperative
    or al, al
    jz .s3
    mov si, app_about_s3c
.s3:
    call app_about_center_x
    add dx, 16
    push bx                     ; fourth line: the adapter in use. BX is the
    mov bl, [vid_kind]          ; window ptr app_about_center wants, so it
    xor bh, bh                  ; only borrows it as the table index
    shl bx, 1
    mov si, [bx + app_about_vid]
    pop bx
    call app_about_center_x

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; app_about_center - draw one string centered horizontally in a window
; in:  SI = NUL string, BX = window ptr, DX = y
; out: nothing
; clobbers: AX, CX (internal helper)
; -----------------------------------------------------------------------------
app_about_center_x:
    call KERNEL_SEG:cw_font_width             ; AX = string width, px
    mov cx, [bx+W_W]
    sub cx, 2                   ; content width
    sub cx, ax
    shr cx, 1                   ; centering offset
    add cx, [bx+W_X]
    inc cx                      ; content left = W_X + 1
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the content's own ground:
    call KERNEL_SEG:cw_font_run               ; W_PAINT arrives white-filled (6.6.5)
    ret

; =============================================================================
; Timer
; =============================================================================

; -----------------------------------------------------------------------------
; app_tmr_task - per-instance task: keep time, redraw HH:MM:SS twice a
;                second, die on request
; in:  DX = instance index (the spawn argument, SPEC.md 8); never returns
; The accumulator design is binding (SPEC.md 14): delta ticks since the last
; sample (wrap-safe subtraction) are added to TMR_ACC in tenths of a tick;
; every 182 tenths (18.2 ticks) advances one second with carries. The tick is
; SAMPLED every iteration whether the timer runs or not - a stopped interval
; is discarded rather than banked, which is the whole of what Stop means -
; and only a running timer accumulates it. Only the drawing is conditional,
; on the window being visible and on wm_clip_set finding any of it uncovered,
; both under the gfx lock (SPEC.md 11.3). A half-covered Timer redraws the
; half you can see instead of freezing. I_STATE = 2 at wake = close
; requested: tear down via inst_task_die (SPEC.md 29).
;
; The task draws the DIGITS and nothing else. The three buttons change only
; when they are clicked, so they are drawn by W_PAINT and by the click path,
; both of which run unclipped - see app_tmr_btn.
;
; And it usually draws nothing at all: it wakes every 9 ticks, which is just
; under half a second, so on roughly every other wake the displayed second has
; not moved and app_tmr_render returns having touched no pixel. When it has
; moved it is normally ONE cell. See that routine's banner.
; -----------------------------------------------------------------------------
app_tmr_task_x:
    mov al, dl
    call KERNEL_SEG:cw_inst_ptr               ; DI = our instance record
    mov bp, di                  ; BP = record for life
    mov si, [ds:bp+I_SPTR]      ; SI = time state for life (BP addresses
                                ; SS by default - SPEC.md 1 rule 3)
.loop:
    mov ax, 9
    call KERNEL_SEG:cw_task_sleep
    cmp byte [ds:bp+I_STATE], 2    ; close requested?
    je .die

    ; --- time-keeping, every iteration ---
    mov ax, [ticks]
    mov dx, ax
    sub ax, [ss:si+TMR_LAST]       ; delta, safe across the 65536 wrap
    mov [ss:si+TMR_LAST], dx       ; sampled even while stopped, so a stop
                                   ; costs its interval instead of banking it
    cmp byte [ss:si+TMR_RUN], 0
    je .draw                       ; stopped: the delta goes nowhere
    mov dx, 10
    mul dx                      ; DX:AX = delta * 10 (tenths of a tick) - keep
    add ax, [ss:si+TMR_ACC]        ; the high half: one long gfx_lock starvation
    adc dx, 0                   ; (a held-open menu) can owe > 65535 tenths
    mov cx, 182
    div cx                      ; AX = whole seconds owed, DX = leftover
    mov [ss:si+TMR_ACC], dx        ; (quotient <= 3602, so DIV cannot fault)
    mov cx, ax
.adv:
    jcxz .draw
    dec cx
    mov al, [ss:si+TMR_SEC]        ; + 1 second, with carries
    inc al
    mov [ss:si+TMR_SEC], al
    cmp al, 60
    jb .adv
    mov byte [ss:si+TMR_SEC], 0
    mov al, [ss:si+TMR_MIN]
    inc al
    mov [ss:si+TMR_MIN], al
    cmp al, 60
    jb .adv
    mov byte [ss:si+TMR_MIN], 0
    mov al, [ss:si+TMR_HRS]
    inc al
    mov [ss:si+TMR_HRS], al
    cmp al, 24
    jb .adv
    mov byte [ss:si+TMR_HRS], 0
    jmp .adv

    ; --- drawing, only when the window can actually be seen ---
.draw:
    call KERNEL_SEG:cw_gfx_lock
    mov bx, [ds:bp+I_WIN]
    test word [bx+W_FLAGS], 2   ; re-check under the lock (SPEC.md 14)
    jz .skip
    call KERNEL_SEG:cw_wm_clip_set            ; CF = 1: not one visible pixel (SPEC 11.3)
    jc .skip
    mov di, si                  ; DI = time state for the renderer
    call app_tmr_digits         ; black HH:MM:SS on white - the background is
                                ; erased per glyph cell in there, NOT as one
                                ; 64x8 rect (SPEC.md 11.3)
.skip:
    call KERNEL_SEG:cw_gfx_unlock
    jmp .loop

.die:
    mov di, bp
    call KERNEL_SEG:cw_inst_task_die           ; destroys the window, frees the record,
                                ; task_exit - never returns

; -----------------------------------------------------------------------------
; app_tmr_paint - W_PAINT for a Timer window
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
app_tmr_paint_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si                  ; instance state via the owner table
    call KERNEL_SEG:cw_inst_of_win
    jc .done
    mov di, [di+I_SPTR]         ; DI = time state
    call app_tmr_forget         ; wm_draw_win has just white-filled the whole
                                ; content, so every cell this window was
                                ; showing is gone and the incremental redraw
                                ; below must letter all eight of them
    call app_tmr_digits         ; digits FIRST: a button may leave the pen
    call app_tmr_btns           ; disabled, and app_tmr_render's own
                                ; gfx_pen_live is what makes the order not
                                ; matter

.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; app_tmr_onclick - W_ONCLICK for a Timer window (SPEC.md 13/14)
; in:  CX = x, DX = y (screen), SI = window ptr; gfx lock held, and this
;      window is the frontmost one by contract
; out: nothing
; clobbers: nothing (flags only)
;
; Fires on the front window alone, and the clip region is disarmed for the
; whole of a W_ONCLICK dispatch, so what is on screen inside the content is
; exactly what the last paint put there and the redraws below may fill and
; letter freely (the SPEC.md 22.2 argument, one module over).
; -----------------------------------------------------------------------------
app_tmr_onclick_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si                  ; instance state via the owner table
    call KERNEL_SEG:cw_inst_of_win
    jc .done
    mov di, [di+I_SPTR]         ; DI = time state
    call app_tmr_hit            ; AL = button index, CF = 1: not on one
    jc .done
%ifdef KERN_BIG
    ; SPEC.md 13.6/13.8: neither Start, Stop nor Reset has a safe prefix
    ; action, so the press only ARMS and DRAWS and app_tmr_onup acts. The
    ; greying is deliberately NOT tested here - a greyed button arms and draws
    ; itself down, and os88ui_btn's DIS outranks DOWN so it says "pressed"
    ; about nothing. The refusal stays at the release, where it always was.
    inc al                      ; 1-based, so 0 can mean "nothing is down"
    xor ah, ah
    call os88ui_arm  ; apps.inc is .text and the arm is .cold
    call app_tmr_setdown
%else
    call app_tmr_act
%endif
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; app_tmr_onup - W_ONMOUSEUP (SPEC.md 13.7): a Timer button fires HERE
; in:  CX = x, DX = y (SCREEN, possibly outside the window), SI = window ptr;
;      gfx lock held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
app_tmr_onup_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call os88ui_fire ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .out
    push bp
    mov bp, ax                  ; BP = the armed control, 1-based. NOT BX:
                                ; BX is the WINDOW here, which app_tmr_hit
                                ; needs and which app_tmr_setdown and
                                ; app_tmr_btn need after it
    mov bx, si
    call KERNEL_SEG:cw_inst_of_win
    jc .oute
    mov di, [di+I_SPTR]
    mov bx, si
    call app_tmr_hit            ; is the pointer STILL on it?
    jc .up
    inc al
    xor ah, ah
    cmp ax, bp
    jne .up
    dec al
    push ax
    xor al, al
    call app_tmr_setdown        ; upright FIRST, before anything acts - and
    pop ax                      ; the act below redraws two of the three
    call app_tmr_act
    jmp short .oute
.up:
    xor al, al                  ; a different button, or none: CANCELLED
    call app_tmr_setdown
.oute:
    pop bp
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; app_tmr_ondrag - W_ONDRAG (SPEC.md 13.8.1): the pointer moved, press down
; in:  CX = x, DX = y (SCREEN), SI = window ptr; gfx lock held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
app_tmr_ondrag_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call os88ui_armed ; PEEK: the arm is the release's to spend
    or ax, ax
    jz .out
    push bp
    mov bp, ax                  ; BP, for app_tmr_onup's reason: BX is the
    mov bx, si                  ; window every routine below wants
    call KERNEL_SEG:cw_inst_of_win
    jc .oute
    mov di, [di+I_SPTR]
    mov bx, si
    call app_tmr_hit
    jc .off
    inc al
    xor ah, ah
    cmp ax, bp
    je .set
.off:
    xor al, al                  ; off it: nothing is down
.set:
    call app_tmr_setdown        ; ...which draws only if that CHANGED
.oute:
    pop bp
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; app_tmr_setdown - AL becomes the button drawn pressed (0 = none), and DRAWS
; in:  AL = a 1-based index or 0; BX = window ptr, DI = time state; lock held
; out: nothing (all registers preserved)
;
; Draws only what CHANGED - one button rather than three, and never the digits.
; -----------------------------------------------------------------------------
app_tmr_setdown:
    push ax
    cmp al, [app_tmr_down]
    je .out
    push ax
    mov al, [app_tmr_down]      ; the one being let go, upright again
    or al, al
    jz .take
    dec al
    mov byte [app_tmr_down], 0
    call app_tmr_btn
.take:
    pop ax
    mov [app_tmr_down], al
    or al, al
    jz .out
    dec al
    call app_tmr_btn            ; ...and the one being taken, pressed
.out:
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; app_tmr_act - do what button AL says (SPEC.md 14)
; in:  AL = button index (0-based), BX = window ptr, DI = time state ptr;
;      gfx lock held
; out: nothing (all registers preserved)
;
; ONE body for both builds: kern_big reaches it from the release and
; kern_small from the press, and neither has an opinion the other does not.
; -----------------------------------------------------------------------------
app_tmr_act:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call app_tmr_bok            ; a greyed control explains itself, so a
    jc .done                    ; refused click says nothing more (SPEC.md 47
                                ; rule 6) - no beep, no notice
    cmp al, APP_TMR_START
    je .start
    cmp al, APP_TMR_STOP
    je .stop

    mov byte [ss:di+TMR_HRS], 0 ; --- Reset: back to 00:00:00, running or not
    mov byte [ss:di+TMR_MIN], 0
    mov byte [ss:di+TMR_SEC], 0
    mov word [ss:di+TMR_ACC], 0 ; the part-second in flight goes too, at both
    mov ax, [ticks]             ; ends of the accumulator
    mov [ss:di+TMR_LAST], ax
    call app_tmr_digits         ; only the digits moved; both pens stand
    jmp short .done

.start:
    mov ax, [ticks]             ; the stopped interval is discarded here as
    mov [ss:di+TMR_LAST], ax    ; well as in the task: up to 9 ticks of it can
    mov byte [ss:di+TMR_RUN], 1 ; be sitting unsampled at the moment of a click
    jmp short .pens
.stop:
    mov byte [ss:di+TMR_RUN], 0
.pens:
    mov al, APP_TMR_START       ; Start and Stop just traded pens; Reset did
    call app_tmr_btn            ; not move, and the digits did not either
    mov al, APP_TMR_STOP
    call app_tmr_btn
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; app_tmr_bok - would this button do anything? (SPEC.md 47 rule 2: the ONE
;               predicate, shared by the greying and the click refusal)
; in:  AL = button index, DI = time state ptr
; out: CF = 0 live, CF = 1 refused
; clobbers: flags (internal helper)
; -----------------------------------------------------------------------------
app_tmr_bok:
    cmp al, APP_TMR_RESET
    je .live                    ; Reset always means something
    cmp byte [ss:di+TMR_RUN], 0
    jne .running
    cmp al, APP_TMR_STOP        ; stopped: there is nothing to stop
    je .no
    jmp short .live
.running:
    cmp al, APP_TMR_START       ; running: there is nothing to start
    je .no
.live:
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; app_tmr_btns - draw all three buttons
; in:  BX = window ptr, DI = time state ptr (gfx lock held)
; out: nothing
; clobbers: flags (internal helper)
; -----------------------------------------------------------------------------
app_tmr_btns:
    push ax
    mov al, APP_TMR_START
.next:
    call app_tmr_btn
    inc al
    cmp al, APP_TMR_NB
    jb .next
    pop ax
    ret

; -----------------------------------------------------------------------------
; app_tmr_btn - draw ONE button: erase, frame, centred label, all in the pen
;               its own predicate asks for
; in:  AL = button index, BX = window ptr, DI = time state ptr (lock held)
; out: nothing
; clobbers: flags
;
; The erase is unconditional because a redraw after a click lands on the
; previous pen's frame, and the fill and the label are gated TOGETHER on
; wm_clip_test over the whole button (SPEC.md 11.3's granularity rule): a
; button is one control, so it is drawn whole or not at all, and a fill that
; clips per pixel can never leave a cut row the glyphs then refuse to letter.
; Both callers - W_PAINT and W_ONCLICK - run with the clip disarmed, where
; that test answers "free"; the gate is here so the routine stays safe to
; call from anywhere.
; -----------------------------------------------------------------------------
app_tmr_btn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    xor ah, ah
    mov bp, ax                  ; BP = the index for the whole routine, and a
                                ; VALUE rather than an address (SS is not DS,
                                ; SPEC.md 1 rule 3) - AX, BX, CX and DX are
                                ; all about to be rect corners
    call app_tmr_brect          ; AX,BX,CX,DX = its inclusive screen rect
    call KERNEL_SEG:cw_wm_clip_test
    jc .out
    mov [os88ui_krect+0], ax
    mov [os88ui_krect+2], bx
    mov [os88ui_krect+4], cx
    mov [os88ui_krect+6], dx

    mov si, bp                  ; the label for this index
    shl si, 1
    mov si, [si + app_tmr_lbl]
    mov ax, bp
    call app_tmr_bok            ; CF = 1 when this button would refuse
    mov di, OS88UI_FILL         ; the Timer fills its whole rect white; a
                                ; frame over the border makes that the same
                                ; picture as filling the interior, which is
                                ; why one flag covers both (os88ui.inc)
    jnc .live
    or di, OS88UI_DIS           ; SPEC.md 47 rule 1, and rule 2's predicate is
.live:                          ; the same one the click path refuses on
%ifdef KERN_BIG
    mov ax, bp                  ; ...and PRESSED if it is the one being held
    inc ax                      ; (SPEC.md 13.8): ids are 1-based so that 0
    cmp al, [app_tmr_down]      ; can mean "nothing is down". DIS OUTRANKS IT
    jne .nodn                   ; inside the control, which is what a Timer
    or di, OS88UI_DOWN          ; needs more than anything else converted so
.nodn:                          ; far: Start and Stop grey each other, so the
%endif                          ; four combinations of the two flags are all
                                ; reachable in one gesture
    mov bx, os88ui_krect
    call os88ui_btn  ; the SHARED control (SPEC.md 2.6's far entry:
                                ; the body is cold, where fdlg and ctrl are,
                                ; and apps.inc is .text)
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; app_tmr_brect - one button's inclusive screen rect
; in:  AL = button index, BX = window ptr
; out: AX = x1, BX = y1, CX = x2, DX = y2
; clobbers: AX, BX, CX, DX (internal helper)
; -----------------------------------------------------------------------------
app_tmr_brect:
    call app_tmr_bpos           ; CX = x, DX = y
    mov ax, cx
    mov bx, dx
    add cx, APP_TMR_BW - 1
    add dx, APP_TMR_BH - 1
    ret

; -----------------------------------------------------------------------------
; app_tmr_bpos - one button's top-left, in screen coordinates
; in:  AL = button index, BX = window ptr
; out: CX = x, DX = y
; clobbers: flags (internal helper; AX is preserved - the caller's index)
; -----------------------------------------------------------------------------
app_tmr_bpos:
    push ax
    mov cl, APP_TMR_BW + APP_TMR_BGAP
    mul cl                      ; AX = index * pitch (both fit a byte)
    mov cx, ax
    call KERNEL_SEG:cw_wm_content             ; AX = content left, DX = content top
    add cx, ax
    add cx, APP_TMR_BX
    add dx, APP_TMR_BY
    pop ax
    ret

; -----------------------------------------------------------------------------
; app_tmr_hit - which button is under a click? (SPEC.md 14)
; in:  BX = window ptr, CX = x, DX = y (screen)
; out: AL = button index, CF = 0; CF = 1 = no button (AL undefined)
; clobbers: AX, flags (internal helper)
;
; Derived from the same constants app_tmr_bpos draws from, so the hit-test
; and the pixels cannot part company - the fm_hit argument (SPEC.md 22.2).
; -----------------------------------------------------------------------------
app_tmr_hit:
    push bx
    push cx
    push dx
    push si
    mov si, dx                  ; SI = click y (wm_content wants DX)
    call KERNEL_SEG:cw_wm_content             ; AX = content left, DX = content top
    sub cx, ax                  ; CX = content-relative x
    sub si, dx                  ; SI = content-relative y
    cmp si, APP_TMR_BY
    jl .miss
    cmp si, APP_TMR_BY + APP_TMR_BH
    jge .miss
    sub cx, APP_TMR_BX
    jl .miss
    xor bx, bx                  ; BX = the button being tested
.col:
    cmp cx, APP_TMR_BW
    jl .hit
    sub cx, APP_TMR_BW + APP_TMR_BGAP
    jl .miss                    ; landed in a gap between two buttons
    inc bx
    cmp bx, APP_TMR_NB
    jl .col
.miss:
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret
.hit:
    mov ax, bx
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; app_tmr_digits - position and draw the HH:MM:SS line
; in:  BX = window ptr, DI = time state ptr (gfx lock held)
; out: nothing
; clobbers: flags (SI is preserved - the timer task keeps its state ptr there
;           for life, so a clobber would scramble the state)
; -----------------------------------------------------------------------------
app_tmr_digits:
    push ax
    push cx
    push dx
    call app_tmr_pos            ; CX = string x, DX = string y
    call app_tmr_render
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; app_tmr_pos - top-left of the 64x8 "HH:MM:SS" rect: APP_TMR_DX across the
;               content, above the button row
; in:  BX = window ptr
; out: CX = x, DX = y
; clobbers: AX (internal helper)
;
; The x used to be centred here arithmetically, from [bx+W_W]. It is a
; constant now because it has to be a multiple of 8 and an expression
; evaluated at run time cannot be checked: APP_TMR_DX is rounded onto a cell
; where the assembler can see it, and the window is fixed-size and unresizable
; so there was never a second answer for it to produce (SPEC.md 14).
; -----------------------------------------------------------------------------
app_tmr_pos:
    call KERNEL_SEG:cw_wm_content             ; AX = content left, DX = content top
    mov cx, ax
    add cx, APP_TMR_DX
    add dx, APP_TMR_DY
    ret

; -----------------------------------------------------------------------------
; app_tmr_render - format an instance's time as "HH:MM:SS" and put the cells
;                  that CHANGED on screen, as one opaque run
; in:  CX = x, DX = y, DI = time state ptr (gfx lock held by caller)
; out: nothing
; clobbers: flags (SI is preserved - see app_tmr_digits)
;
; Two things here are the whole of SPEC.md 14's redraw story, and both are
; about the glass rather than about work.
;
; ONE OPAQUE RUN, NOT AN ERASE AND A LETTERING. This drew each cell as a white
; gfx_fill followed by a font_char, which is PERFORMANCE.md Part 1's
; double-draw flash in its textbook form: between the two calls the cell is
; BLANK, on a 4.77MHz machine for the better part of a millisecond, twice a
; second for as long as the Timer is open. font_run (SPEC.md 6.1) takes both
; colours and writes each cell from its old content straight to its final
; content in one store, so there is no interval to see - and on a 1bpp adapter
; at a byte-aligned x that store IS the whole operation. The window carries
; WF_SNAP (SPEC.md 11.94, asked for in app_tmr_kinit) and APP_TMR_DX is a
; multiple of 8: that is what earns the byte-aligned path, and missing either
; costs speed and the flash, never correctness.
;
; ONLY THE CELLS THAT CHANGED. TMR_SHOWN is the eight characters this window
; is displaying, and what gets drawn is the span from the first cell that
; differs to the last: ONE cell for a second going 30 -> 31, two when it
; carries, and nothing whatever on the wakes that land inside the same second.
; The eight-cell redraw twice a second was 8 gfx_fills and 8 font_chars, each
; of the sixteen paying PERFORMANCE.md Part 2's ~756us per-call floor whatever
; it drew.
;
; THE CLIP QUESTION IS ASKED ONCE, over the whole line, and it settles both
; halves at once. font_run needs no gate of its own - it decides per cell on
; both its paths, which is exactly SPEC.md 6.1.2's guarantee and the reason
; the old per-cell wm_clip_test dance is gone - but a run an edge cuts has
; cells it REFUSED, and TMR_SHOWN would then be claiming pixels nobody drew.
; So a cut line is drawn whole and then forgotten, and the next pass letters
; everything again: SPEC.md 28.2's rule, that a run which took that path was
; by definition not drawn whole.
; -----------------------------------------------------------------------------
app_tmr_render:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov al, [ss:di+TMR_HRS]
    aam                         ; AH = tens, AL = ones
    add ax, 0x3030              ; both to ASCII, leading zero for free
    mov [app_tmr_str], ah
    mov [app_tmr_str+1], al
    mov byte [app_tmr_str+2], ':'
    mov al, [ss:di+TMR_MIN]
    aam
    add ax, 0x3030
    mov [app_tmr_str+3], ah
    mov [app_tmr_str+4], al
    mov byte [app_tmr_str+5], ':'
    mov al, [ss:di+TMR_SEC]
    aam
    add ax, 0x3030
    mov [app_tmr_str+6], ah
    mov [app_tmr_str+7], al
    mov byte [app_tmr_str+8], 0

    mov ax, cx                  ; the whole line's inclusive rect: AX = x1,
    mov bx, dx                  ; BX = y1, CX = x2, DX = y2
    add cx, APP_TMR_DW - 1
    add dx, 7
    call KERNEL_SEG:cw_wm_clip_test           ; preserves all four (and answers "free" when
    mov cx, ax                  ; nothing is armed, which is every W_PAINT and
    mov dx, bx                  ; every click)
    jc .whole

    call app_tmr_span           ; the differing cells, and TMR_SHOWN adopts
    jc .out                     ; the new line; CF = 1 = it already showed it
    mov bl, [app_tmr_c1]
    xor bh, bh
    mov byte [app_tmr_str+1+bx], 0  ; terminate after the last changed cell.
                                    ; The scratch is rebuilt on every call, so
                                    ; nothing has to put the byte back
    mov bl, [app_tmr_c0]
    mov si, bx
    add si, app_tmr_str         ; SI = the run...
    shl bx, 1
    shl bx, 1
    shl bx, 1                   ; ...and its x, cell * 8 (8086: no shl by an
    add cx, bx                  ; immediate other than 1 - SPEC.md 1 rule 2)
    jmp short .draw
.whole:
    call app_tmr_forget         ; some of these cells are about to be refused,
    mov si, app_tmr_str         ; so nothing may be remembered about any of them
.draw:
    call KERNEL_SEG:cw_gfx_pen_live           ; a button drawn just before may have left
                                ; [gfx_dis] set, which is both a checkerboard
                                ; glyph and font_run's slow path (SPEC.md 47
                                ; rule 1) - and font_run takes its colours in
                                ; AL/AH, so this is here for the FLAG
    mov al, CBLACK
    mov ah, CWHITE
    call KERNEL_SEG:cw_font_run
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; app_tmr_span - which cells differ from what this window is showing, and
;                adopt the new line as what it is about to be showing
; in:  DI = time state ptr, app_tmr_str freshly formatted
; out: [app_tmr_c0] = first differing cell, [app_tmr_c1] = the last;
;      CF = 1 = none differ, and the two bytes are meaningless
; clobbers: flags (internal helper)
;
; The compare and the adoption are one pass because they are one decision:
; every cell this reports is about to be drawn, and its only caller reaches
; here having established that the whole line is drawable. A cut line does not
; come through here at all - it goes to app_tmr_forget instead.
; -----------------------------------------------------------------------------
app_tmr_span:
    push ax
    push bx
    mov byte [app_tmr_c0], 0xFF ; no cell has differed yet
    xor bx, bx
.cell:
    mov al, [app_tmr_str+bx]
    cmp al, [ss:bx+di+TMR_SHOWN]
    je .next
    mov [ss:bx+di+TMR_SHOWN], al
    cmp byte [app_tmr_c0], 0xFF
    jne .last
    mov [app_tmr_c0], bl
.last:
    mov [app_tmr_c1], bl        ; kept overwritten: the last one to get here
.next:                          ; is the far end of the span
    inc bx
    cmp bx, 8
    jb .cell
    cmp byte [app_tmr_c0], 0xFF
    je .none
    pop bx
    pop ax
    clc
    ret
.none:
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; app_tmr_forget - this window is no longer showing anything we know about
; in:  DI = time state ptr
; out: nothing (all registers preserved)
;
; Zero is not a character any cell can hold, so eight of them make every cell
; differ and the next app_tmr_span reports the whole line. Zeroing only the
; first byte would report a span of ONE cell - the span is first-to-last and
; the other seven would still match - which is the failure this routine exists
; to make unavailable to its three callers.
; -----------------------------------------------------------------------------
app_tmr_forget:
    mov word [ss:di+TMR_SHOWN+0], 0
    mov word [ss:di+TMR_SHOWN+2], 0
    mov word [ss:di+TMR_SHOWN+4], 0
    mov word [ss:di+TMR_SHOWN+6], 0
    ret

; =============================================================================
; Bounce
; =============================================================================

; -----------------------------------------------------------------------------
; app_bounce_task - per-instance task: 8x8 black square bouncing in the
;                   content, dies on request
; in:  DX = instance index (the spawn argument); never returns
; Ball position/velocity are content-relative words in the instance state.
;
; The ball ALWAYS steps (SPEC.md 14/11.3). Only the erase and the redraw are
; conditional: wm_clip_set under the lock either arms the visible region -
; and then the two 8x8 fills are cut to it, so a half-covered Bounce shows
; the ball crossing the half you can see - or answers CF = 1, and the frame
; steps invisibly. Stepping while covered is safe because app_bounce_paint
; draws from state, so the repaint on uncover shows the square where it now
; is rather than where it was when the window was buried. (Until SPEC.md
; 11.3 the frame was skipped without stepping, which is why a covered Bounce
; used to look frozen.) Edges: content w = 148, h = 111.
; -----------------------------------------------------------------------------
app_bounce_task_x:
    mov al, dl
    call KERNEL_SEG:cw_inst_ptr               ; DI = our instance record
    mov bp, di                  ; BP = record for life
    mov si, [ds:bp+I_SPTR]      ; SI = ball state for life (ds: override:
                                ; BP addresses SS by default, SPEC.md 1)
.loop:
    mov ax, 2
    call KERNEL_SEG:cw_task_sleep
    cmp byte [ds:bp+I_STATE], 2    ; close requested?
    je .die
    call KERNEL_SEG:cw_gfx_lock
    mov bx, [ds:bp+I_WIN]
    test word [bx+W_FLAGS], 2   ; check under the lock (SPEC.md 14)
    jz .blind
    call KERNEL_SEG:cw_wm_clip_set            ; CF = 1: not one visible pixel (SPEC 11.3)
    jc .blind

    mov di, si                  ; DI = ball state for app_ball_fill
    mov cx, [ss:si+BAL_X]       ; bank where the square is ON THE GLASS
    mov dx, [ss:si+BAL_Y]
    call app_ball_step
    mov byte [gfx_color], CBLACK
    call app_ball_fill          ; the NEW square FIRST (SPEC.md 14.5)...
    mov byte [gfx_color], CWHITE
    call app_ball_wipe          ; ...then the part of the old one it does not
    jmp short .skip             ; cover. Erase-then-draw wrote every pixel the
                                ; two share white and then black, once a
                                ; frame, and the glass caught it
.blind:
    call app_ball_step          ; nothing on screen to keep in step with
.skip:
    call KERNEL_SEG:cw_gfx_unlock             ; ...which also disarms the clip (SPEC 11.3)
    jmp .loop

.die:
    mov di, bp
    call KERNEL_SEG:cw_inst_task_die           ; never returns

; -----------------------------------------------------------------------------
; app_ball_wipe - white out the part of the OLD square the new one does not
;                 cover (SPEC.md 14.5)
; in:  BX = window ptr, DI = ball state (already stepped, so it holds the NEW
;      position), CX = the OLD content x, DX = the OLD content y; [gfx_color]
;      set to the background
; out: nothing
; clobbers: nothing (flags only; internal helper)
;
; Two equal squares offset by (ddx, ddy) leave an L: a COLUMN strip |ddx| wide
; over the old square's full height on the trailing side, plus a ROW strip
; |ddy| tall over the columns the two share. Either can be empty and both are
; skipped then; nothing is written twice, and nothing is ever absent, because
; the caller has already drawn the new square.
;
; IT IS THIS SHORT BECAUSE THE STEP IS SMALLER THAN THE SQUARE. With
; |ddx| and |ddy| under 8 the strips need no clamping against the old rect and
; the no-overlap case cannot arise - the column strip degenerates to the whole
; old square by itself. The velocities are constants and a bounce only ever
; SHORTENS a step (the clamp at the wall), so the assertions below are the
; whole guard; raise a velocity past the square's width and this needs
; apps/arkanoid's general four-strip version instead.
; -----------------------------------------------------------------------------
%if 3 >= 8 || 2 >= 8
%error "app_ball_wipe: a Bounce step must stay under the 8px square"
%endif

app_ball_wipe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov si, [ss:di+BAL_X]
    sub si, cx                  ; SI = ddx, new - old
    mov bp, [ss:di+BAL_Y]
    sub bp, dx                  ; BP = ddy
    mov ax, si
    or ax, bp
    jz .out                     ; a step the clamps ate entirely

    push cx                     ; wm_content answers in AX and DX, which is
    push dx                     ; where the old position is
    call KERNEL_SEG:cw_wm_content             ; AX = content left, DX = content top
    pop cx
    add cx, dx
    mov [app_bwy], cx           ; the old square, ABSOLUTE
    pop cx
    add cx, ax
    mov [app_bwx], cx

    or si, si                   ; --- the column strip, full height ----------
    jz .rows
    mov ax, [app_bwx]
    mov cx, ax
    add cx, si
    dec cx                      ; moved right: from the old left edge
    or si, si                   ; ...and the TEST is of ddx, not of the edge
    jg .colgo                   ; the dec above just computed - which left the
                                ; square trailing a solid diagonal line, the
                                ; erase being an inverted rect on every step
                                ; that went left
    add ax, 8
    add ax, si                  ; moved left: from the far side back
    mov cx, [app_bwx]
    add cx, 7
.colgo:
    mov bx, [app_bwy]
    mov dx, bx
    add dx, 7
    call KERNEL_SEG:cw_gfx_fill

.rows:
    or bp, bp                   ; --- the row strip, over the SHARED columns -
    jz .out
    mov ax, [app_bwx]
    mov cx, ax
    add cx, 7
    or si, si
    jz .ry
    jg .rright
    add cx, si                  ; moved left: they end earlier
    jmp short .ry
.rright:
    add ax, si                  ; moved right: they start later
.ry:
    mov bx, [app_bwy]
    mov dx, bx
    add dx, bp
    dec dx                      ; moved down: from the old top edge
    or bp, bp
    jg .rowgo
    mov bx, [app_bwy]
    add bx, 8
    add bx, bp                  ; moved up: from the bottom back
    mov dx, [app_bwy]
    add dx, 7
.rowgo:
    call KERNEL_SEG:cw_gfx_fill
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; app_ball_step - advance one instance's ball, bouncing off the content edges
; in:  SI = ball state ptr
; out: nothing
; clobbers: nothing (flags only; internal helper)
; -----------------------------------------------------------------------------
app_ball_step:
    push ax
    mov ax, [ss:si+BAL_X]          ; step + bounce, x axis
    add ax, [ss:si+BAL_VX]
    jns .xhigh
    mov ax, 0                   ; hit the left wall
    neg word [ss:si+BAL_VX]
    jmp short .xdone
.xhigh:
    cmp ax, APP_BALL_XMAX
    jle .xdone
    mov ax, APP_BALL_XMAX       ; hit the right wall
    neg word [ss:si+BAL_VX]
.xdone:
    mov [ss:si+BAL_X], ax

    mov ax, [ss:si+BAL_Y]          ; step + bounce, y axis
    add ax, [ss:si+BAL_VY]
    jns .yhigh
    mov ax, 0                   ; hit the top
    neg word [ss:si+BAL_VY]
    jmp short .ydone
.yhigh:
    cmp ax, APP_BALL_YMAX
    jle .ydone
    mov ax, APP_BALL_YMAX       ; hit the bottom
    neg word [ss:si+BAL_VY]
.ydone:
    mov [ss:si+BAL_Y], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; app_bounce_paint - W_PAINT for a Bounce window
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
app_bounce_paint_x:
    push bx
    push di

    mov bx, si                  ; instance state via the owner table
    call KERNEL_SEG:cw_inst_of_win
    jc .done
    mov di, [di+I_SPTR]         ; DI = ball state
    mov byte [gfx_color], CBLACK
    call app_ball_fill

.done:
    pop di
    pop bx
    retf

; -----------------------------------------------------------------------------
; app_ball_fill - fill the 8x8 square at an instance's ball position
; in:  BX = window ptr, DI = ball state ptr; color from [gfx_color]
; out: nothing
; clobbers: nothing (flags only; internal helper)
; -----------------------------------------------------------------------------
app_ball_fill:
    push ax
    push bx
    push cx
    push dx

    call KERNEL_SEG:cw_wm_content             ; AX = content left, DX = content top
    add ax, [ss:di+BAL_X]          ; AX = x1
    add dx, [ss:di+BAL_Y]
    mov bx, dx                  ; BX = y1
    mov cx, ax
    add cx, 7                   ; CX = x2
    add dx, 7                   ; DX = y2
    call KERNEL_SEG:cw_gfx_fill

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .lowbss                 ; LOW_SEG (SPEC.md 2.1): both state pools are
                                ; walked through a POINTER the instance record
                                ; hands over, never addressed by name, so they
                                ; cost one ss: prefix per access and nothing in
                                ; the kernel's own 64KB segment. SS is LOW_SEG
                                ; from kmain onwards and never changes, so the
                                ; prefix needs no setup and cannot be clobbered
app_tmr_pool:   resb 10 * TMR_SIZE  ; TMR_HRS/MIN/SEC/RUN + LAST + ACC, and
                                    ; the 8 cells the window is SHOWING
app_ball_pool:  resb 80         ; 10 x 8: BAL_X/Y/VX/VY

section .bss

app_tmr_str:    resb 9          ; "HH:MM:SS",0 scratch (drawn under the lock)
app_tmr_c0:     resb 1          ; app_tmr_span's answer: the first and last
app_tmr_c1:     resb 1          ; cell of it that changed. Module scratch for
                                ; the same reason app_tmr_str is - every path
                                ; that reads them holds the gfx lock
app_bwx:        resw 1          ; app_ball_wipe's old square, absolute. Module
app_bwy:        resw 1          ; scratch for the same reason again: it runs
                                ; under the lock, which is what makes it safe
                                ; with several Bounce instances live

section .text
