; =============================================================================
; jop - ui.inc
;
; The UI task (SPEC.md 13): task 0's forever-loop. Each pass polls the BIOS
; keyboard, drains one event from the system queue, and yields. Mouse-downs
; in the menu bar run menu_track and dispatch the returned CMD_*; mouse-downs
; on windows close, raise, or drag them. Every wm_* call that repaints is
; made under the gfx lock, and the drag loop's binding rule is honored: the
; lock is only ever released while the XOR outline is erased, and the release
; path never re-takes the lock it already holds (it is non-reentrant, SPEC 7).
; =============================================================================

; -----------------------------------------------------------------------------
; ui_task - the UI task main loop (task 0 jumps here from kmain; never returns)
; in:  nothing
; out: never returns
; clobbers: everything (it is a task entry, not a callable routine)
; -----------------------------------------------------------------------------
ui_task:
.loop:
    ; --- 1. keyboard: poll, and feed the front window's key handler ----------
    mov ah, 0x01                ; any key pending?
    int 0x16
    jz .events
    mov ah, 0x00                ; fetch it: AL = ascii, AH = scan
    int 0x16
    call gfx_lock               ; handler draws; it runs under the lock
    call wm_top                 ; BX = frontmost visible window, 0 if none
    or bx, bx
    jz .key_done
    cmp word [bx + W_ONKEY], 0  ; window may not take keys
    je .key_done
    mov si, bx                  ; W_ONKEY contract: AL=ascii, AH=scan, SI=win
    call [bx + W_ONKEY]
.key_done:
    call gfx_unlock

    ; --- 2. events: pop one, act on EVT_MDOWN ---------------------------------
.events:
    mov di, ui_ev
    call evq_pop
    jc .yield                   ; queue empty
    cmp word [ui_ev + EV_TYPE], EVT_MDOWN
    jne .yield                  ; only mouse-downs matter here
    mov cx, [ui_ev + EV_A]      ; CX = click x
    mov dx, [ui_ev + EV_B]      ; DX = click y

    cmp dx, MBAR_H              ; in the menu bar?
    jae .on_desktop
    call gfx_lock               ; menu_track runs the whole interaction
    call menu_track             ; in: CX = mousedown x; out: AX = CMD_* or 0
    call gfx_unlock
    call ui_dispatch
    jmp .yield

.on_desktop:
    call wm_hit                 ; BX = window hit (0 = none), AL = region
    or bx, bx
    jz .yield                   ; desktop click: nothing to do
    cmp al, 2
    je .close_box
    cmp al, 1
    je .title_bar

    ; AL = 0: content. Raise a background window; the front one is ignored.
    mov si, bx                  ; keep the hit window; wm_top returns in BX
    call wm_top
    cmp bx, si
    je .yield                   ; already frontmost
    mov bx, si
    call gfx_lock
    call wm_front               ; raise + repaint
    call gfx_unlock
    jmp .yield

.close_box:                     ; wm_hit only reports AL=2 on the front window
    call gfx_lock
    call wm_hide                ; in: BX; clears visible + repaints
    call gfx_unlock
    jmp .yield

.title_bar:
    mov si, bx                  ; keep the hit window; wm_top returns in BX
    call wm_top
    cmp bx, si
    mov bx, si                  ; restore hit window (flags survive the mov)
    je .drag
    call gfx_lock
    call wm_front               ; raise first, then drag
    call gfx_unlock
.drag:
    call ui_drag                ; in: BX = window, CX/DX = mousedown x/y

    ; --- 3. give everyone else a slice ----------------------------------------
.yield:
    call task_yield
    jmp .loop

; -----------------------------------------------------------------------------
; ui_drag - title-bar drag: XOR outline follows the mouse, window moves on
;           release (SPEC.md 13 - the ordering below is binding)
; in:  BX = window ptr, CX = mousedown x, DX = mousedown y
; out: nothing; window's W_X/W_Y updated (clamped: title bar fully on screen),
;      full repaint done
; clobbers: AX, BX, CX, DX (internal helper of the UI task)
;
; The gfx lock is only released while the outline is erased: XOR draw/erase is
; self-inverting only if nothing touches the covered pixels in between, and
; the background tasks repaint the instant the lock drops. The release path
; runs entirely under the lock already held - re-locking would deadlock.
; -----------------------------------------------------------------------------
ui_drag:
    mov [ui_dragwin], bx
    mov [ui_startx], cx         ; mousedown point = drag origin
    mov [ui_starty], dx
    mov ax, [bx + W_X]
    mov [ui_origx], ax
    mov [ui_curx], ax           ; outline starts on the window rect
    mov ax, [bx + W_Y]
    mov [ui_origy], ax
    mov [ui_cury], ax
    mov ax, [bx + W_W]
    mov [ui_dragw], ax
    mov ax, [bx + W_H]
    mov [ui_dragh], ax

    call gfx_lock
    call ui_drag_xor            ; draw the outline

.track:
    ; The linger below stretches each pass to a whole tick, so the button is
    ; tracked by EDGE, not level: the ISR queues every press/release, and a
    ; release + fast re-press inside one pass must still end the drag (the
    ; re-press stays queued and is dispatched as a fresh click afterwards).
    mov di, ui_ev
    call evq_pop
    jc .no_evt
    cmp word [ui_ev + EV_TYPE], EVT_MUP
    jne .track                  ; drain anything else queued mid-drag
    jmp .release                ; exits with outline drawn + lock held
.no_evt:
    test byte [mouse_btn], 1    ; fallback: the EVT_MUP was dropped
    jz .release                 ; (queue full) - the level still says so
    call ui_drag_xor            ; erase...
    call gfx_unlock             ; ...only then may the lock drop
    call task_yield             ; background tasks stay live here
    call gfx_lock
    mov ax, [mouse_x]           ; outline pos = orig + (mouse - start)
    sub ax, [ui_startx]
    add ax, [ui_origx]
    mov [ui_curx], ax
    mov ax, [mouse_y]
    sub ax, [ui_starty]
    add ax, [ui_origy]
    mov [ui_cury], ax
    call ui_drag_xor            ; redraw every pass, moved or not

    ; Linger with the outline drawn (lock held) until the next timer tick.
    ; The erase window that follows is short but includes the unlock/yield
    ; that lets background tasks draw; without this linger the cursor blits
    ; inside gfx_unlock/gfx_lock dominate the loop and the outline is dark
    ; more often than lit.
    mov ax, [ticks]
.linger:
    call task_yield
    cmp ax, [ticks]
    je .linger
    jmp .track

.release:
    call ui_drag_xor            ; erase; lock stays held to the end

    ; clamp x: 0 <= x and x + w <= SCREEN_W (signed - x may have gone negative)
    mov ax, [ui_curx]
    mov bx, SCREEN_W
    sub bx, [ui_dragw]          ; BX = rightmost legal x
    cmp ax, bx
    jle .x_high_ok
    mov ax, bx
.x_high_ok:
    or ax, ax                   ; low bound last so it wins
    jns .x_low_ok
    xor ax, ax
.x_low_ok:
    mov bx, [ui_dragwin]
    mov [bx + W_X], ax

    ; clamp y: MBAR_H <= y and y + TITLE_H <= SCREEN_H (title bar on screen)
    mov ax, [ui_cury]
    cmp ax, SCREEN_H - TITLE_H
    jle .y_high_ok
    mov ax, SCREEN_H - TITLE_H
.y_high_ok:
    cmp ax, MBAR_H
    jge .y_low_ok
    mov ax, MBAR_H
.y_low_ok:
    mov [bx + W_Y], ax

    call wm_paint_all           ; caller-held lock, per the wm contract
    call gfx_unlock             ; NO second gfx_lock here - already held (§7)
    ret

; -----------------------------------------------------------------------------
; ui_drag_xor - draw (or erase - XOR is its own inverse) the drag outline at
;               the current tracked position
; in:  [ui_curx]/[ui_cury] top-left, [ui_dragw]/[ui_dragh] size
; out: nothing
; clobbers: AX, BX, CX, DX
; -----------------------------------------------------------------------------
ui_drag_xor:
    mov ax, [ui_curx]           ; x1
    mov bx, [ui_cury]           ; y1
    mov cx, ax
    add cx, [ui_dragw]
    dec cx                      ; x2 = x1 + w - 1
    mov dx, bx
    add dx, [ui_dragh]
    dec dx                      ; y2 = y1 + h - 1
    call gfx_xor_rect           ; clips, so off-screen edges are fine
    ret

; -----------------------------------------------------------------------------
; ui_dispatch - act on a menu command
; in:  AX = CMD_* (0 = tracking ended on no item; ignored)
; out: nothing (CMD_REBOOT never returns)
; clobbers: AX, BX (internal helper of the UI task)
; wm_show/wm_hide repaint internally (SPEC 11) - no extra wm_paint_all here.
; -----------------------------------------------------------------------------
ui_dispatch:
    or ax, ax
    jz .done
    cmp ax, CMD_ABOUT
    je .about
    cmp ax, CMD_NOTE
    je .note
    cmp ax, CMD_CLOCK
    je .clock
    cmp ax, CMD_BOUNCE
    je .bounce
    cmp ax, CMD_CLOSE
    je .close
    cmp ax, CMD_REBOOT
    je .reboot
.done:
    ret

.about:
    mov bx, [app_win_about]
    jmp .show
.note:
    mov bx, [app_win_note]
    jmp .show
.clock:
    mov bx, [app_win_clock]
    jmp .show
.bounce:
    mov bx, [app_win_bounce]
.show:
    call gfx_lock
    call wm_show                ; in: BX; shows + raises + repaints
    call gfx_unlock
    ret

.close:
    call gfx_lock
    call wm_top                 ; BX = frontmost visible, 0 if none
    or bx, bx
    jz .close_none
    call wm_hide                ; in: BX
.close_none:
    call gfx_unlock
    ret

.reboot:
    call gfx_lock               ; never released - the GUI is done
    call vga_text
    call sched_unhook           ; restores int 08h/0Ch (unhooks the mouse too)
    int 0x19
    jmp .reboot                 ; unreachable; int 19h does not return

; -----------------------------------------------------------------------------
section .bss

ui_ev:      resb 8              ; popped event record (EV_TYPE/A/B/C)
ui_dragwin: resw 1              ; window being dragged
ui_startx:  resw 1              ; mousedown position (drag origin)
ui_starty:  resw 1
ui_origx:   resw 1              ; window frame position at drag start
ui_origy:   resw 1
ui_curx:    resw 1              ; where the outline currently sits
ui_cury:    resw 1
ui_dragw:   resw 1              ; frame size, cached across the drag
ui_dragh:   resw 1

section .text
