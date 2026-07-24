; =============================================================================
; jop - apps.inc
;
; The built-in applications (SPEC.md 14): About dialog, Note Pad, Clock and
; Bounce. apps_init creates all four window records at boot (About hidden,
; the rest marked visible for the boot-time wm_paint_all) and spawns the two
; background tasks. Paint procs and the key handler run with the gfx lock
; already held by the caller (SPEC.md 11) and never lock, block or spawn;
; the Clock/Bounce tasks take the lock themselves and re-check visibility +
; wm_obscured under it before touching the screen. Background tasks use only
; jop primitives and [ticks] - no BIOS calls (SPEC.md 7).
; =============================================================================

; --- geometry ----------------------------------------------------------------
APP_NOTE_CAP    equ 512                     ; Note Pad text buffer, bytes
APP_BALL_XMAX   equ 150 - 2 - 8             ; bounce content w=148, 8px square
APP_BALL_YMAX   equ 130 - TITLE_H - 1 - 8   ; bounce content rows are
                                            ; y+TITLE_H..y+h-2 inclusive = 111

; -----------------------------------------------------------------------------
; apps_init - create the four app windows and spawn the background tasks
; in:  nothing
; out: nothing; window ptrs stored in app_win_about/note/clock/bounce
; clobbers: nothing (flags only)
; Note Pad / Clock / Bounce get W_FLAGS bit1 (visible) set directly - the
; boot sequence does a single wm_paint_all afterwards (SPEC.md 14/15).
; -----------------------------------------------------------------------------
apps_init:
    push ax
    push bx
    push si

    ; module state first: .bss is not zeroed by the loader, and the two
    ; background tasks start running the moment they are spawned
    mov word [app_note_len], 0
    mov byte [clk_h], 0
    mov byte [clk_m], 0
    mov byte [clk_s], 0
    mov ax, [ticks]
    mov [clk_last], ax
    mov word [clk_acc], 0
    mov word [app_ball_x], 4
    mov word [app_ball_y], 4
    mov word [app_ball_vx], 3
    mov word [app_ball_vy], 2

    mov si, app_tpl_about
    call wm_create
    mov [app_win_about], bx     ; stays hidden until CMD_ABOUT

    mov si, app_tpl_note
    call wm_create
    mov [app_win_note], bx
    or word [bx+W_FLAGS], 2     ; visible at boot

    mov si, app_tpl_clock
    call wm_create
    mov [app_win_clock], bx
    or word [bx+W_FLAGS], 2     ; visible at boot

    mov si, app_tpl_bounce
    call wm_create
    mov [app_win_bounce], bx
    or word [bx+W_FLAGS], 2     ; visible at boot

    mov ax, app_clock_task
    call task_spawn
    mov ax, app_bounce_task
    call task_spawn

    pop si
    pop bx
    pop ax
    ret

; --- window templates: {x, y, w, h, title, paint, onkey} words ---------------
app_tpl_about:  dw 170, 140, 300, 120, app_ttl_about,  app_about_paint,  0
app_tpl_note:   dw  60,  60, 260, 180, app_ttl_note,   app_note_paint,   app_note_onkey
app_tpl_clock:  dw 450,  80, 130,  60, app_ttl_clock,  app_clock_paint,  0
app_tpl_bounce: dw 440, 260, 150, 130, app_ttl_bounce, app_bounce_paint, 0

app_ttl_about:  db 'About jop', 0
app_ttl_note:   db 'Note Pad', 0
app_ttl_clock:  db 'Clock', 0
app_ttl_bounce: db 'Bounce', 0

app_about_s1:   db 'jop 1.0', 0
app_about_s2:   db 'a graphical OS for the 8086', 0
app_about_s3:   db 'pre-emptive - 640x480 - 16 colors', 0

; =============================================================================
; About
; =============================================================================

; -----------------------------------------------------------------------------
; app_about_paint - W_PAINT for the About window
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; Three black lines, each centered in the content; the block of three
; (8px lines, 8px gaps) is centered vertically in the 101px content.
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
    mov si, app_about_s3
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
; Note Pad
; =============================================================================

; -----------------------------------------------------------------------------
; app_note_paint - W_PAINT for the Note Pad window
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; Renders the text buffer with 6px left/top margins, wrapping at the content
; width; byte 13 is a line break. Rows whose bottom would pass the content
; bottom are discarded, not drawn (no scrolling); the 1px black caret after
; the text is drawn only if its own row fits (SPEC.md 14).
; -----------------------------------------------------------------------------
app_note_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si
    call wm_content             ; AX = content left, DX = content top
    add ax, 6
    mov [app_note_tx], ax       ; text origin x (wrap column)
    mov di, ax                  ; DI = pen x
    add dx, 6
    mov bp, dx                  ; BP = pen y
    mov ax, [bx+W_X]
    add ax, [bx+W_W]
    sub ax, 2
    mov [app_note_rgt], ax      ; content right (inclusive)
    mov ax, [bx+W_Y]
    add ax, [bx+W_H]
    sub ax, 2
    mov [app_note_bot], ax      ; content bottom (inclusive)

    mov byte [gfx_color], CBLACK
    mov si, app_note_buf
    mov bx, [app_note_len]      ; BX = characters remaining

.chloop:
    test bx, bx
    jz .caret
    lodsb                       ; DF=0 on entry per SPEC.md 1
    dec bx
    cmp al, 13
    jne .glyph
    mov di, [app_note_tx]       ; newline: carriage return + line feed
    add bp, 8
    jmp .chloop

.glyph:
    mov cx, di                  ; wrap if the 8px cell would pass the edge
    add cx, 7
    cmp cx, [app_note_rgt]
    jbe .fits
    mov di, [app_note_tx]
    add bp, 8
.fits:
    mov cx, bp                  ; vertical clip: drop rows that overflow,
    add cx, 7                   ; but keep advancing the pen so the caret
    cmp cx, [app_note_bot]      ; position stays true
    ja .advance
    mov cx, di
    mov dx, bp
    call font_char
.advance:
    add di, 8
    jmp .chloop

.caret:
    mov cx, di                  ; caret occupies the next cell: wrap like
    add cx, 7                   ; a character would
    cmp cx, [app_note_rgt]
    jbe .cfits
    mov di, [app_note_tx]
    add bp, 8
.cfits:
    mov cx, bp
    add cx, 7
    cmp cx, [app_note_bot]
    ja .done                    ; caret row does not fit: no caret
    mov ax, di                  ; 1px black caret, 8 rows tall
    mov bx, bp
    mov dx, bp
    add dx, 7
    call gfx_vline

.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; app_note_onkey - W_ONKEY for the Note Pad window
; in:  AL = ascii, AH = scan, SI = window ptr (gfx lock held by caller)
; out: nothing
; clobbers: nothing (flags only)
; Printable 32..126 appends (silent drop when the buffer is full),
; backspace (8) deletes, Enter (13) stores a newline byte. Whenever the
; buffer changed, repaints its own content only: white-fill the content
; rect, then redraw text + caret. Unhandled keys touch nothing.
; -----------------------------------------------------------------------------
app_note_onkey:
    push ax
    push bx
    push cx
    push dx

    cmp al, 8
    je .bksp
    cmp al, 13
    je .append
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out

.append:
    mov bx, [app_note_len]
    cmp bx, APP_NOTE_CAP
    jae .out                    ; full: drop silently
    mov [app_note_buf+bx], al
    inc word [app_note_len]
    jmp .redraw

.bksp:
    cmp word [app_note_len], 0
    je .out
    dec word [app_note_len]

.redraw:
    mov bx, si
    call wm_content             ; AX = x1, DX = y1
    mov cx, [bx+W_X]
    add cx, [bx+W_W]
    sub cx, 2                   ; CX = x2
    push dx
    mov dx, [bx+W_Y]
    add dx, [bx+W_H]
    sub dx, 2                   ; DX = y2
    pop bx                      ; BX = y1
    mov byte [gfx_color], CWHITE
    call gfx_fill               ; white-fill content
    call app_note_paint         ; SI still = window ptr

.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Clock
; =============================================================================

; -----------------------------------------------------------------------------
; app_clock_task - background task: keep time, redraw HH:MM:SS twice a second
; in:  nothing (task entry; never returns)
; The accumulator design is binding (SPEC.md 14): delta ticks since the last
; sample (wrap-safe subtraction) are added to clk_acc in tenths of a tick;
; every 182 tenths (18.2 ticks) advances one second with carries. The
; time-keeping runs every iteration; only the drawing is conditional on
; visible + not wm_obscured, re-checked under the gfx lock.
; -----------------------------------------------------------------------------
app_clock_task:
.loop:
    mov ax, 9
    call task_sleep

    ; --- time-keeping, every iteration ---
    mov ax, [ticks]
    mov dx, ax
    sub ax, [clk_last]          ; delta, safe across the 65536 wrap
    mov [clk_last], dx
    mov dx, 10
    mul dx                      ; DX:AX = delta * 10 (tenths of a tick) - keep
    add ax, [clk_acc]           ; the high half: one long gfx_lock starvation
    adc dx, 0                   ; (a held-open menu) can owe > 65535 tenths
    mov cx, 182
    div cx                      ; AX = whole seconds owed, DX = leftover
    mov [clk_acc], dx           ; (quotient <= 3602, so DIV cannot fault)
    mov cx, ax
.adv:
    jcxz .draw
    dec cx
    mov al, [clk_s]             ; + 1 second, with carries
    inc al
    mov [clk_s], al
    cmp al, 60
    jb .adv
    mov byte [clk_s], 0
    mov al, [clk_m]
    inc al
    mov [clk_m], al
    cmp al, 60
    jb .adv
    mov byte [clk_m], 0
    mov al, [clk_h]
    inc al
    mov [clk_h], al
    cmp al, 24
    jb .adv
    mov byte [clk_h], 0
    jmp .adv

    ; --- drawing, only when the window can actually be seen ---
.draw:
    call gfx_lock
    mov bx, [app_win_clock]
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
    call app_clk_render         ; black HH:MM:SS at CX,DX
.skip:
    call gfx_unlock
    jmp .loop

; -----------------------------------------------------------------------------
; app_clock_paint - W_PAINT for the Clock window
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

    mov bx, si
    call app_clk_pos            ; CX,DX = string origin
    call app_clk_render

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
; app_clk_render - format clk_h/m/s as "HH:MM:SS" and draw it black
; in:  CX = x, DX = y (gfx lock held by caller)
; out: nothing
; clobbers: AX, SI (internal helper)
; -----------------------------------------------------------------------------
app_clk_render:
    mov al, [clk_h]
    aam                         ; AH = tens, AL = ones
    add ax, 0x3030              ; both to ASCII, leading zero for free
    mov [app_clk_str], ah
    mov [app_clk_str+1], al
    mov byte [app_clk_str+2], ':'
    mov al, [clk_m]
    aam
    add ax, 0x3030
    mov [app_clk_str+3], ah
    mov [app_clk_str+4], al
    mov byte [app_clk_str+5], ':'
    mov al, [clk_s]
    aam
    add ax, 0x3030
    mov [app_clk_str+6], ah
    mov [app_clk_str+7], al
    mov byte [app_clk_str+8], 0
    mov byte [gfx_color], CBLACK
    mov si, app_clk_str
    call font_str
    ret

; =============================================================================
; Bounce
; =============================================================================

; -----------------------------------------------------------------------------
; app_bounce_task - background task: 8x8 black square bouncing in the content
; in:  nothing (task entry; never returns)
; Ball position/velocity are content-relative words. If the visible +
; not-obscured check fails under the lock, the frame is skipped WITHOUT
; erasing or stepping (SPEC.md 14) - the square stays put until the window
; can be seen again. Edges: content w = 148, h = 111.
; -----------------------------------------------------------------------------
app_bounce_task:
.loop:
    mov ax, 2
    call task_sleep
    call gfx_lock
    mov bx, [app_win_bounce]
    test word [bx+W_FLAGS], 2   ; check under the lock (SPEC.md 14)
    jz .skip
    call wm_obscured
    jc .skip

    mov byte [gfx_color], CWHITE
    call app_ball_fill          ; erase at the old position

    mov ax, [app_ball_x]        ; step + bounce, x axis
    add ax, [app_ball_vx]
    jns .xhigh
    mov ax, 0                   ; hit the left wall
    neg word [app_ball_vx]
    jmp .xdone
.xhigh:
    cmp ax, APP_BALL_XMAX
    jle .xdone
    mov ax, APP_BALL_XMAX       ; hit the right wall
    neg word [app_ball_vx]
.xdone:
    mov [app_ball_x], ax

    mov ax, [app_ball_y]        ; step + bounce, y axis
    add ax, [app_ball_vy]
    jns .yhigh
    mov ax, 0                   ; hit the top
    neg word [app_ball_vy]
    jmp .ydone
.yhigh:
    cmp ax, APP_BALL_YMAX
    jle .ydone
    mov ax, APP_BALL_YMAX       ; hit the bottom
    neg word [app_ball_vy]
.ydone:
    mov [app_ball_y], ax

    mov byte [gfx_color], CBLACK
    call app_ball_fill          ; draw at the new position
.skip:
    call gfx_unlock
    jmp .loop

; -----------------------------------------------------------------------------
; app_bounce_paint - W_PAINT for the Bounce window
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
app_bounce_paint:
    push bx

    mov bx, si
    mov byte [gfx_color], CBLACK
    call app_ball_fill

    pop bx
    ret

; -----------------------------------------------------------------------------
; app_ball_fill - fill the 8x8 square at the current ball position
; in:  BX = window ptr; color from [gfx_color]
; out: nothing
; clobbers: nothing (flags only; internal helper)
; -----------------------------------------------------------------------------
app_ball_fill:
    push ax
    push bx
    push cx
    push dx

    call wm_content             ; AX = content left, DX = content top
    add ax, [app_ball_x]        ; AX = x1
    add dx, [app_ball_y]
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

app_win_about:  resw 1          ; window record ptrs (ui.inc dispatches on
app_win_note:   resw 1          ; these exact names)
app_win_clock:  resw 1
app_win_bounce: resw 1

app_note_buf:   resb 512        ; Note Pad text (APP_NOTE_CAP; literal per
app_note_len:   resw 1          ; the no-forward-ref-in-bss rule)
app_note_tx:    resw 1          ; paint scratch: text origin x (wrap column)
app_note_rgt:   resw 1          ; paint scratch: content right, inclusive
app_note_bot:   resw 1          ; paint scratch: content bottom, inclusive

clk_h:          resb 1          ; wall clock, boots at 00:00:00
clk_m:          resb 1
clk_s:          resb 1
clk_last:       resw 1          ; last [ticks] sample
clk_acc:        resw 1          ; tenth-of-tick accumulator (182 = 1s)
app_clk_str:    resb 9          ; "HH:MM:SS",0 scratch

app_ball_x:     resw 1          ; content-relative, top-left of the square
app_ball_y:     resw 1
app_ball_vx:    resw 1
app_ball_vy:    resw 1

section .text
