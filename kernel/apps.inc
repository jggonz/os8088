; =============================================================================
; os8088 - apps.inc
;
; The built-in application KINDS (SPEC.md 14): About dialog, Note Pad, Clock
; and Bounce. Nothing is created at boot - instances are launched on demand
; by app_launch (SPEC.md 29), which allocates a state block from the kind's
; pool, creates the window from the kind's template, and spawns the kind's
; task (Clock/Bounce run one task per instance). Closing an instance frees
; all of it: the UI task sets the die flag (I_STATE=2) and hides the window;
; the task notices at its next wake and tears down via inst_task_die.
;
; Paint procs and key handlers run with the gfx lock already held by the
; caller (SPEC.md 11), find their instance state via inst_of_win + I_SPTR,
; and never lock, block or spawn; the Clock/Bounce tasks take the lock
; themselves and re-check visibility + wm_obscured under it before touching
; the screen. Background tasks use only os8088 primitives and [ticks] - no
; BIOS calls (SPEC.md 7).
; =============================================================================

; --- geometry ----------------------------------------------------------------
APP_BALL_XMAX   equ 150 - 2 - 8             ; bounce content w=148, 8px square
APP_BALL_YMAX   equ 130 - TITLE_H - 1 - 8   ; bounce content rows are
                                            ; y+TITLE_H..y+h-2 inclusive = 111

; --- per-instance state layouts (pool strides pinned in SPEC.md 29.3) --------
CLK_H     equ 0                 ; byte     (byte 3 is padding)
CLK_M     equ 1                 ; byte
CLK_S     equ 2                 ; byte
CLK_LAST  equ 4                 ; word: last [ticks] sample
CLK_ACC   equ 6                 ; word: tenth-of-tick accumulator; stride 8

BAL_X     equ 0                 ; words: content-relative pos + velocity
BAL_Y     equ 2
BAL_VX    equ 4
BAL_VY    equ 6                 ; stride 8

; --- window templates: {x, y, w, h, title, paint, onkey, onclick} words ------
app_tpl_about:  dw 170, 140, 300, 120, app_ttl_about,  app_about_paint,  0, 0
app_tpl_clock:  dw 350,  60, 130,  60, app_ttl_clock,  app_clock_paint,  0, 0
app_tpl_bounce: dw 300, 150, 150, 130, app_ttl_bounce, app_bounce_paint, 0, 0

app_ttl_about:  db 'About os8088', 0
app_ttl_clock:  db 'Clock', 0
app_ttl_bounce: db 'Bounce', 0

app_about_s1:   db 'os8088 1.0', 0
app_about_s2:   db 'a graphical OS for the 8086', 0
; third line, one whole string per scheduler mode (SPEC.md 8.2/14): both
; scheduling words are exactly 11 chars, so the line width - and therefore
; the centered x app_about_center computes - is identical either way
app_about_s3p:  db 'pre-emptive - 640x480 - 16 colors', 0
app_about_s3c:  db 'cooperative - 640x480 - 16 colors', 0

; =============================================================================
; Kind init procs (KD_INIT contract, SPEC.md 29.3): in BX = window ptr,
; DI = state block ptr, SI = instance record ptr; preserve all registers;
; run on the UI task with no lock held, window not yet visible.
; =============================================================================

; ---- app_clock_kinit - fresh Clock: 00:00:00, accumulator seeded now ---------
app_clock_kinit:
    push ax
    mov byte [di+CLK_H], 0
    mov byte [di+CLK_M], 0
    mov byte [di+CLK_S], 0
    mov ax, [ticks]
    mov [di+CLK_LAST], ax
    mov word [di+CLK_ACC], 0
    pop ax
    ret

; ---- app_bounce_kinit - fresh Bounce: ball at (4,4), velocity (3,2) ----------
app_bounce_kinit:
    mov word [di+BAL_X], 4
    mov word [di+BAL_Y], 4
    mov word [di+BAL_VX], 3
    mov word [di+BAL_VY], 2
    ret

; =============================================================================
; About
; =============================================================================

; -----------------------------------------------------------------------------
; app_about_paint - W_PAINT for the About window (stateless)
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; Three black lines, each centered in the content; the block of three
; (8px lines, 8px gaps) is centered vertically in the 101px content. The
; third line's scheduling word tracks the live mode (sched_mode_get).
; -----------------------------------------------------------------------------
app_about_paint:
    push ax
    push bx
    push cx
    push dx
    push si

    mov byte [gfx_color], CBLACK
    mov bx, si                  ; window ptr for the centering helper
    mov dx, [bx+W_Y]
    add dx, TITLE_H + 30        ; first line: content top + 30
    mov si, app_about_s1
    call app_about_center
    add dx, 16
    mov si, app_about_s2
    call app_about_center
    add dx, 16
    mov si, app_about_s3p       ; third line follows the live scheduler mode
    call sched_mode_get         ; AL = 0 pre-emptive, 1 cooperative
    or al, al
    jz .s3
    mov si, app_about_s3c
.s3:
    call app_about_center

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; app_about_center - draw one string centered horizontally in a window
; in:  SI = NUL string, BX = window ptr, DX = y
; out: nothing
; clobbers: AX, CX (internal helper)
; -----------------------------------------------------------------------------
app_about_center:
    call font_width             ; AX = string width, px
    mov cx, [bx+W_W]
    sub cx, 2                   ; content width
    sub cx, ax
    shr cx, 1                   ; centering offset
    add cx, [bx+W_X]
    inc cx                      ; content left = W_X + 1
    call font_str
    ret

; =============================================================================
; Clock
; =============================================================================

; -----------------------------------------------------------------------------
; app_clock_task - per-instance task: keep time, redraw HH:MM:SS twice a
;                  second, die on request
; in:  DX = instance index (the spawn argument, SPEC.md 8); never returns
; The accumulator design is binding (SPEC.md 14): delta ticks since the last
; sample (wrap-safe subtraction) are added to CLK_ACC in tenths of a tick;
; every 182 tenths (18.2 ticks) advances one second with carries. The
; time-keeping runs every iteration; only the drawing is conditional on
; visible + not wm_obscured, re-checked under the gfx lock. I_STATE = 2 at
; wake = close requested: tear down via inst_task_die (SPEC.md 29).
; -----------------------------------------------------------------------------
app_clock_task:
    mov al, dl
    call inst_ptr               ; DI = our instance record
    mov bp, di                  ; BP = record for life
    mov si, [ds:bp+I_SPTR]      ; SI = time state for life (BP addresses
                                ; SS by default - SPEC.md 1 rule 3)
.loop:
    mov ax, 9
    call task_sleep
    cmp byte [ds:bp+I_STATE], 2    ; close requested?
    je .die

    ; --- time-keeping, every iteration ---
    mov ax, [ticks]
    mov dx, ax
    sub ax, [si+CLK_LAST]       ; delta, safe across the 65536 wrap
    mov [si+CLK_LAST], dx
    mov dx, 10
    mul dx                      ; DX:AX = delta * 10 (tenths of a tick) - keep
    add ax, [si+CLK_ACC]        ; the high half: one long gfx_lock starvation
    adc dx, 0                   ; (a held-open menu) can owe > 65535 tenths
    mov cx, 182
    div cx                      ; AX = whole seconds owed, DX = leftover
    mov [si+CLK_ACC], dx        ; (quotient <= 3602, so DIV cannot fault)
    mov cx, ax
.adv:
    jcxz .draw
    dec cx
    mov al, [si+CLK_S]          ; + 1 second, with carries
    inc al
    mov [si+CLK_S], al
    cmp al, 60
    jb .adv
    mov byte [si+CLK_S], 0
    mov al, [si+CLK_M]
    inc al
    mov [si+CLK_M], al
    cmp al, 60
    jb .adv
    mov byte [si+CLK_M], 0
    mov al, [si+CLK_H]
    inc al
    mov [si+CLK_H], al
    cmp al, 24
    jb .adv
    mov byte [si+CLK_H], 0
    jmp .adv

    ; --- drawing, only when the window can actually be seen ---
.draw:
    call gfx_lock
    mov bx, [ds:bp+I_WIN]
    test word [bx+W_FLAGS], 2   ; re-check under the lock (SPEC.md 14)
    jz .skip
    call wm_obscured
    jc .skip
    call app_clk_pos            ; CX = string x, DX = string y
    push cx
    push dx
    mov ax, cx                  ; white-fill the 64x8 string rect (font
    mov bx, dx                  ; background is transparent, SPEC.md 6)
    add cx, 63
    add dx, 7
    mov byte [gfx_color], CWHITE
    call gfx_fill
    pop dx
    pop cx
    mov di, si                  ; DI = time state for the renderer
    call app_clk_render         ; black HH:MM:SS at CX,DX
.skip:
    call gfx_unlock
    jmp .loop

.die:
    mov di, bp
    jmp inst_task_die           ; destroys the window, frees the record,
                                ; task_exit - never returns

; -----------------------------------------------------------------------------
; app_clock_paint - W_PAINT for a Clock window
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
app_clock_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si                  ; instance state via the owner table
    call inst_of_win
    jc .done
    push di
    call app_clk_pos            ; CX,DX = string origin (clobbers AX)
    pop di
    mov di, [di+I_SPTR]         ; DI = time state
    call app_clk_render

.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; app_clk_pos - top-left of the 64x8 "HH:MM:SS" rect, centered in content
; in:  BX = window ptr
; out: CX = x, DX = y
; clobbers: AX (internal helper)
; -----------------------------------------------------------------------------
app_clk_pos:
    call wm_content             ; AX = content left, DX = content top
    mov cx, ax
    mov ax, [bx+W_W]
    sub ax, 2 + 64              ; content width - string width
    shr ax, 1
    add cx, ax
    mov ax, [bx+W_H]
    sub ax, TITLE_H + 1 + 8     ; content height - string height
    shr ax, 1
    add dx, ax
    ret

; -----------------------------------------------------------------------------
; app_clk_render - format an instance's time as "HH:MM:SS" and draw it black
; in:  CX = x, DX = y, DI = time state ptr (gfx lock held by caller)
; out: nothing
; clobbers: AX (internal helper; SI is preserved - the clock task keeps its
;           state ptr there for life, so a clobber would scramble the state)
; -----------------------------------------------------------------------------
app_clk_render:
    push si
    mov al, [di+CLK_H]
    aam                         ; AH = tens, AL = ones
    add ax, 0x3030              ; both to ASCII, leading zero for free
    mov [app_clk_str], ah
    mov [app_clk_str+1], al
    mov byte [app_clk_str+2], ':'
    mov al, [di+CLK_M]
    aam
    add ax, 0x3030
    mov [app_clk_str+3], ah
    mov [app_clk_str+4], al
    mov byte [app_clk_str+5], ':'
    mov al, [di+CLK_S]
    aam
    add ax, 0x3030
    mov [app_clk_str+6], ah
    mov [app_clk_str+7], al
    mov byte [app_clk_str+8], 0
    mov byte [gfx_color], CBLACK
    mov si, app_clk_str
    call font_str
    pop si
    ret

; =============================================================================
; Bounce
; =============================================================================

; -----------------------------------------------------------------------------
; app_bounce_task - per-instance task: 8x8 black square bouncing in the
;                   content, dies on request
; in:  DX = instance index (the spawn argument); never returns
; Ball position/velocity are content-relative words in the instance state.
; If the visible + not-obscured check fails under the lock, the frame is
; skipped WITHOUT erasing or stepping (SPEC.md 14) - the square stays put
; until the window can be seen again. Edges: content w = 148, h = 111.
; -----------------------------------------------------------------------------
app_bounce_task:
    mov al, dl
    call inst_ptr               ; DI = our instance record
    mov bp, di                  ; BP = record for life
    mov si, [ds:bp+I_SPTR]      ; SI = ball state for life (ds: override:
                                ; BP addresses SS by default, SPEC.md 1)
.loop:
    mov ax, 2
    call task_sleep
    cmp byte [ds:bp+I_STATE], 2    ; close requested?
    je .die
    call gfx_lock
    mov bx, [ds:bp+I_WIN]
    test word [bx+W_FLAGS], 2   ; check under the lock (SPEC.md 14)
    jz .skip
    call wm_obscured
    jc .skip

    mov di, si                  ; DI = ball state for app_ball_fill
    mov byte [gfx_color], CWHITE
    call app_ball_fill          ; erase at the old position

    mov ax, [si+BAL_X]          ; step + bounce, x axis
    add ax, [si+BAL_VX]
    jns .xhigh
    mov ax, 0                   ; hit the left wall
    neg word [si+BAL_VX]
    jmp .xdone
.xhigh:
    cmp ax, APP_BALL_XMAX
    jle .xdone
    mov ax, APP_BALL_XMAX       ; hit the right wall
    neg word [si+BAL_VX]
.xdone:
    mov [si+BAL_X], ax

    mov ax, [si+BAL_Y]          ; step + bounce, y axis
    add ax, [si+BAL_VY]
    jns .yhigh
    mov ax, 0                   ; hit the top
    neg word [si+BAL_VY]
    jmp .ydone
.yhigh:
    cmp ax, APP_BALL_YMAX
    jle .ydone
    mov ax, APP_BALL_YMAX       ; hit the bottom
    neg word [si+BAL_VY]
.ydone:
    mov [si+BAL_Y], ax

    mov byte [gfx_color], CBLACK
    call app_ball_fill          ; draw at the new position
.skip:
    call gfx_unlock
    jmp .loop

.die:
    mov di, bp
    jmp inst_task_die           ; never returns

; -----------------------------------------------------------------------------
; app_bounce_paint - W_PAINT for a Bounce window
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
app_bounce_paint:
    push bx
    push di

    mov bx, si                  ; instance state via the owner table
    call inst_of_win
    jc .done
    mov di, [di+I_SPTR]         ; DI = ball state
    mov byte [gfx_color], CBLACK
    call app_ball_fill

.done:
    pop di
    pop bx
    ret

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

    call wm_content             ; AX = content left, DX = content top
    add ax, [di+BAL_X]          ; AX = x1
    add dx, [di+BAL_Y]
    mov bx, dx                  ; BX = y1
    mov cx, ax
    add cx, 7                   ; CX = x2
    add dx, 7                   ; DX = y2
    call gfx_fill

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

app_clk_pool:   resb 80         ; 10 x 8: CLK_H/M/S + CLK_LAST + CLK_ACC
app_clk_str:    resb 9          ; "HH:MM:SS",0 scratch (drawn under the lock)

app_ball_pool:  resb 80         ; 10 x 8: BAL_X/Y/VX/VY

section .text
