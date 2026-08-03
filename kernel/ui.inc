; =============================================================================
; os8088 - ui.inc
;
; The UI task (SPEC.md 13): task 0's forever-loop. Each pass polls the BIOS
; keyboard, drains one event from the system queue, runs any deferred
; instance launch, any pending package load, any deferred kernel menu command
; and any posted full repaint (all outside the gfx lock - app_launch,
; loader_run and ui_cmd manage their own locking), and yields.
;
; That deferral channel is what lets a handler running UNDER the gfx lock
; ask for work that needs the lock: inst_launch_post for launches,
; ui_post_cmd for the kernel's own CMD_* (SPEC.md 13). The lock is a
; non-recursive spin only this task releases, so the alternative is a hang.
; Step 4 is the system clock (SPEC.md 37): clk_tick every pass - it is cheap
; and keeps time whether or not anything is drawn - and, on the second, one
; gfx_lock around the menu bar's clock cell plus any open Date/Time page.
; Mouse-downs in the menu bar run menu_track and route the returned
; (cell, item) pair three ways (SPEC.md 12/13): the System menu and
; Locator's own menus become a CMD_*, dispatched by ui_cmd with no lock
; held so app commands can call app_launch directly (SPEC.md 29); an
; application's own menu (SPEC.md 12.2) is a window callback reached
; through the bar, so its handler runs under the gfx lock and is billed to
; the owning instance exactly like W_ONCLICK.
; A RIGHT mouse-down takes none of that ladder: ui_rdown swallows the bar,
; the dock and the desktop, raises a background window without opening
; anything, and pops a context menu (SPEC.md 12.4) only on the frontmost
; file-manager window's content. It never stamps [ui_click_t], which is what
; keeps a right-click from composing with the next left click into a
; double-click.
; Mouse-downs on windows also hand the clicked window the menu bar
; (SPEC.md 12 - the raise paths repaint anyway, but a click on the window
; that is already frontmost never reaches wm_front), and QUIT (close box ->
; app_close_win: window, task and state freed), raise, or drag them;
; content clicks on the front window feed its W_ONCLICK under the lock.
; Every wm_* call that repaints is made under the gfx lock, and the drag
; loop's binding rule is honored: the lock is only ever released while the
; XOR outline is erased, and the release path never re-takes the lock it
; already holds (it is non-reentrant, SPEC 7).
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
    call fdlg_top               ; ...unless a modal file dialog is up, which
                                ; takes every key (SPEC.md 38.2)
    or bx, bx
    jz .key_done
    cmp word [bx + W_ONKEY], 0  ; window may not take keys
    je .key_done
    mov si, bx                  ; W_ONKEY contract: AL=ascii, AH=scan, SI=win
    call inst_win_owner         ; bill the handler to its instance, not to
    push di                     ; this task (SPEC.md 8.1); BX/SI/AX survive
    push word [snd_inst]        ; stamp the dispatched instance for sound
    call snd_disp_set           ; grants made inside the handler (SPEC 34.3)
    push ax                     ; the key - task_cycles returns in DX:AX
    call task_cycles            ; DX:AX = start stamp
    mov cx, ax                  ; stamp lo steps aside into CX: BX must
                                ; still name the WINDOW here, which
                                ; inst_cb_seg's unowned fallback consults
                                ; (SPEC.md 11)
    call inst_cb_seg            ; AX = the owner's segment (DI = the record)
    mov bx, ax                  ; segment steps aside...
    pop ax                      ; ...key back for the handler's contract
    push dx                     ; start stamp: hi, then lo
    push cx
    push bx                     ; the resolved segment above the offset:
    push word [si + W_ONKEY]    ; inst_cb_call's argument pair
    call inst_cb_call
    pop ax
    pop dx
    pop word [snd_inst]         ; the outer dispatch's stamp, back
    pop di
    or di, di
    jz .key_done
    call inst_charge
.key_done:
    call gfx_unlock

    ; --- 2. events: pop one, act on EVT_MDOWN ---------------------------------
.events:
    mov di, ui_ev
    call evq_pop
    jc .yield                   ; queue empty
    mov ax, [ui_ev + EV_TYPE]
    cmp ax, EVT_RDOWN           ; the right button goes its own, much
    je .rdown                   ; narrower way (SPEC.md 12.4/13)
    cmp ax, EVT_MDOWN
    jne .yield                  ; only mouse-downs matter here
    mov cx, [ui_ev + EV_A]      ; CX = click x
    mov dx, [ui_ev + EV_B]      ; DX = click y
    mov ax, [ui_ev + EV_C]      ; publish the click's birth tick for the
    mov [ui_click_t], ax        ; double-click detectors (SPEC 22, 26)

    call fdlg_grab              ; a modal file dialog swallows every press
    jc .yield                   ; outside its own frame (SPEC.md 38.2).
                                ; AHEAD of every branch below - modality is
                                ; not a window property, and a press on the
                                ; menu bar must die as firmly as one on
                                ; another window

    cmp word [wm_fs], 0         ; a fullscreen window (SPEC.md 11.2) sits
    jne .on_desktop             ; over the bar: route rows 0..19 to wm_hit
                                ; like any other point
    cmp dx, MBAR_H              ; in the menu bar?
    jae .on_desktop
    cmp cx, [vid_clk_hx]        ; the clock cell at the right end (SPEC 12.1)?
    jae .clock_cell             ; tested BEFORE menu_track: it is not a title
    call gfx_lock               ; menu_track runs the whole interaction
    call menu_track             ; in: CX = mousedown x; out: AX = CMD_* or 0
    call gfx_unlock
    call ui_dispatch
    jmp .yield

.clock_cell:                    ; open the Control Panel on its Date/Time
    mov byte [cp_sel], CP_ITIME ; page (SPEC.md 31.5); no lock held here, so
    mov al, KIND_CTRL           ; app_launch is called directly like any
    call app_launch             ; other menu command - and fronts + repaints
    jnc .yield                  ; an already-open panel, which is what makes
    call snd_beep               ; the page swap visible
    jmp .yield

.on_desktop:
    call wm_hit                 ; BX = window hit (0 = none), AL = region
    or bx, bx
    jz .desk_icons              ; no window: dock, then desktop icons

    call menu_activate          ; clicking a window hands it the menu bar
    jnc .region                 ; (SPEC.md 12), CF=1 = the owner changed.
    call gfx_lock               ; The raise paths below repaint anyway, but
    call menu_draw_bar          ; a click on the window that is ALREADY
    call gfx_unlock             ; frontmost never reaches wm_front, and the
.region:                        ; bar still has to follow. AL/BX/CX/DX all
                                ; survive: every callee preserves them.
    cmp al, 2
    je .close_box
    cmp al, 3
    je .min_box
    cmp al, 4
    je .grow_box                ; wm_hit reports AL=4 on the frontmost
                                ; resizable window only (SPEC.md 11.1)
    cmp al, 1
    je .title_bar

    ; AL = 0: content. Raise a background window; the front one gets its
    ; W_ONCLICK (if any) under the gfx lock, like W_ONKEY above.
    mov si, bx                  ; keep the hit window; wm_top returns in BX
    call wm_top
    cmp bx, si
    je .content_front           ; already frontmost: feed the click handler
    mov bx, si
    call gfx_lock
    call wm_front               ; raise + repaint
    call gfx_unlock
    jmp .yield

.content_front:
    cmp word [si + W_ONCLICK], 0
    je .yield                   ; window does not take clicks
    call gfx_lock               ; W_ONCLICK contract: CX=x, DX=y, SI=win
    mov bx, si                  ; (CX/DX survived wm_hit/wm_top/gfx_lock)
    call inst_win_owner         ; bill the handler to its instance (8.1)
    push di
    push word [snd_inst]        ; stamp the dispatched instance for sound
    call snd_disp_set           ; grants made inside the handler (SPEC 34.3)
    push cx                     ; the click point - task_cycles returns in
    push dx                     ; DX:AX, so CX/DX have to step aside
    call task_cycles            ; DX:AX = start stamp
    mov bx, ax
    mov ax, dx
    pop dx                      ; click y
    pop cx                      ; click x
    push ax                     ; start stamp: hi, then lo
    push bx
    mov bx, si                  ; BX = the window again for inst_cb_seg's
                                ; unowned fallback (the stamp lo it banked
                                ; is already on the stack - SPEC.md 11)
    call inst_cb_seg            ; AX = the owner's segment (DI = the record;
    push ax                     ; both stamps are already banked, so AX is
    push word [si + W_ONCLICK]  ; free). SPEC.md 11 dispatch: near for the
                                ; kernel, far at the package's segment else
    call inst_cb_call
    pop ax
    pop dx
    pop word [snd_inst]         ; the outer dispatch's stamp, back
    pop di
    or di, di
    jz .click_done
    call inst_charge
.click_done:
    call gfx_unlock
    jmp .yield

.desk_icons:
    call dock_click             ; CX/DX; CF=1 = consumed (locks internally,
    jc .yield                   ; SPEC.md 30) - the dock gets first look

    xor bx, bx                  ; the bare desktop: clicking it switches back
    call menu_activate          ; to Locator (SPEC.md 12), CF=1 = it changed
    jnc .desk_go                ; - so the bar is only repainted when the
    call gfx_lock               ; owner really moved, and before desk_click,
    call menu_draw_bar          ; whose double-click-open may front a window
    call gfx_unlock             ; and activate it in turn
.desk_go:
    call desk_click             ; CX/DX = click point, no lock held; select /
    jmp .yield                  ; double-click-open is desk.inc's business

.close_box:                     ; wm_hit only reports AL=2 on the front window
    call gfx_lock
    call app_close_win          ; in: BX; quits the owning instance (or
    call gfx_unlock             ; hides an ownerless window) - SPEC.md 29
    jmp .yield

.min_box:                       ; wm_hit only reports AL=3 on the front window
    call gfx_lock
    call inst_minimize          ; in: BX; flags the instance minimized and
    call gfx_unlock             ; hides - its dock tile inverts (SPEC.md 30)
    jmp .yield

.grow_box:
    call ui_grow                ; in: BX = window, CX/DX = mousedown x/y
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
    jmp short .yield

.rdown:
    mov cx, [ui_ev + EV_A]      ; the right button is swallowed by a modal
    mov dx, [ui_ev + EV_B]      ; dialog too (SPEC.md 38.2) - there is no
    call fdlg_grab              ; context menu it could usefully open. The
    jc .yield                   ; point is loaded here rather than above
                                ; because this branch is taken before the
                                ; left button's own load
    call ui_rdown               ; in: [ui_ev]; the context-menu path (12.4)

    ; --- 3. deferred work (SPEC.md 13/21/29): OUTSIDE the gfx lock ------------
.yield:
    call fdlg_reap              ; a file dialog cancelled by its close or
                                ; minimize box is only HIDDEN by that route
                                ; (SPEC.md 38.1) - collect it here rather
                                ; than leaving the menu bar on Locator
                                ; until the user next clicks something
    mov ax, [inst_launch]       ; deferred instance launch (posted by
    or ax, ax                   ; lock-held handlers via inst_launch_post)
    jz .chk_ld
    dec ax                      ; stored as kind + 1
    mov word [inst_launch], 0   ; consume before running, not after
    call app_launch             ; in: AL = kind; does its own locking
.chk_ld:
    cmp word [ld_pending], 0    ; set by files.inc's click handler
    je .chk_pcmd
    mov ax, [ld_pending]        ; stored as directory index + 1
    dec ax
    mov word [ld_pending], 0    ; consume before running, not after
    call loader_run             ; in: AX = index; does its own locking
.chk_pcmd:
    mov ax, [ui_pcmd]           ; a kernel menu command posted from under
    or ax, ax                   ; the lock (ui_post_cmd, SPEC.md 13)
    jz .chk_cp
    mov word [ui_pcmd], 0       ; consume before running, not after
    call ui_cmd                 ; in: AX = CMD_*; takes its own locks, and
                                ; CMD_REBOOT never comes back
.chk_cp:
    cmp byte [cp_dirty], 0      ; scheduler mode changed in the Control
    je .chk_clk                 ; Panel (SPEC.md 31.2): windows that quote
    mov byte [cp_dirty], 0      ; the mode - the About box's third line -
    call gfx_lock               ; are already painted, so repaint them here,
    call wm_paint_all           ; outside cp_onclick's held lock
    call gfx_unlock
.chk_clk:
    cmp byte [clk_dirty], 0     ; the clock was set from the Date/Time page
    je .clock                   ; (SPEC.md 31.5): the hardware RTC is written
    mov byte [clk_dirty], 0     ; HERE, outside the lock, because a page proc
    call clk_rtc_write          ; may not call BIOS (SPEC.md 31.1)

    ; --- 4. the clock: keep time always, draw only when something changed ----
    ; Taking the gfx lock blinks the cursor, and that blink IS the flicker
    ; the "seconds in menu bar" setting exists to remove - so the lock is
    ; taken only when the bar's text really changed (mask bit 1), or when a
    ; Date/Time page is on screen and wants its seconds (SPEC.md 13/37).
.clock:
    call clk_tick               ; AL: bit0 = a second passed, bit1 = the bar
    mov bl, al                  ; text changed
    or al, al
    jz .idle
    cmp word [wm_fs], 0         ; fullscreen (SPEC.md 11.2): the bar and any
    jne .idle                   ; Date/Time page are covered - time is kept,
                                ; nothing is drawn
    test al, 2
    jnz .clk_lock
    call cp_tick_due            ; CF = 1: a live Date/Time page is showing
    jnc .idle
.clk_lock:
    call gfx_lock
    test bl, 2
    jz .clk_cp
    call menu_draw_clock        ; the menu bar cell (SPEC.md 12.1)...
.clk_cp:
    call cp_tick                ; ...and an open Date/Time page (SPEC 31.5),
    call gfx_unlock             ; which is a no-op unless one is showing

    ; --- 5. give everyone else a slice ----------------------------------------
.idle:
    call task_yield
    jmp .loop

; -----------------------------------------------------------------------------
; ui_rdown - act on one EVT_RDOWN: the context-menu path (SPEC.md 12.4/13)
; in:  [ui_ev] holds the record
; out: nothing
; clobbers: AX, BX, CX, DX, SI, DI (internal helper of the UI task)
;
; Deliberately far narrower than the mouse-down ladder above, and it shares
; none of it. Three things it must NOT do, each one a bug waiting to happen:
;
;   - stamp [ui_click_t]. Binding. The double-click detectors of SPEC.md
;     22/26 compare a stored tick against that word, so a right-press that
;     stamped it would let a right-click followed within 9 ticks by a left
;     click on the same row read as a double-click and launch a package the
;     user never opened. (fm_rclick keeps the other half of that rule: it
;     moves FS_SEL without touching FS_CLKT.)
;   - reach dock_click. A dock tile toggles minimize; a right-press must
;     not. The bare desktop and desk_click are out for the same reason -
;     there is nothing here a right button should mean.
;   - open anything on a background window. It raises it and stops: the
;     popup always belongs to the window you can already see.
;
; The bar is skipped whole, clock cell included. Only a file-manager window
; answers - the ones carrying fm_menus - so no package's window changes
; behaviour and no shipped .o88 needs rebuilding.
; -----------------------------------------------------------------------------
ui_rdown:
    mov cx, [ui_ev + EV_A]      ; CX = press x, DX = press y. NO [ui_click_t]
    mov dx, [ui_ev + EV_B]      ; store - see above.

    cmp word [wm_fs], 0         ; a fullscreen window (SPEC.md 11.2) owns the
    jne .hit                    ; bar's rows like any other point
    cmp dx, MBAR_H
    jb .ret                     ; the menu bar has no right-button behaviour
.hit:
    call wm_hit                 ; BX = window hit (0 = none), AL = region
    or bx, bx
    jz .ret                     ; desktop and dock: nothing at all

    mov si, bx                  ; keep the hit window; wm_top returns in BX
    call wm_top                 ; AL, the region, survives it
    cmp bx, si
    je .front
    mov bx, si                  ; a background window: raise it and stop
    call gfx_lock
    call wm_front               ; activates it too, so the bar follows
    call gfx_unlock
    ret

.front:
    or al, al
    jnz .ret                    ; title bar, close/minimize/grow box: the
                                ; chrome has no context menu
    call fm_is_fmwin            ; only Locator's file-manager windows - the
    jc .ret                     ; W_MENUS = fm_menus identity test made
                                ; OWNER-AWARE (SPEC.md 13, binding since
                                ; v3: a package's org-0 set offset can
                                ; equal fm_menus by coincidence). BX = SI =
                                ; the frontmost window here

    call gfx_lock
    call fm_rclick              ; select + track the popup: UNBILLED, exactly
    jc .ctx_done                ; like menu_track (SPEC.md 13) - the dwell
                                ; time of a held button is nobody's CPU cost

    mov bx, si                  ; the command itself gets the W_ONCLICK
    call inst_win_owner         ; treatment verbatim (SPEC.md 8.1/12.2)
    push di
    push word [snd_inst]        ; stamp the dispatched instance for sound
    call snd_disp_set           ; grants made inside it (SPEC.md 34.3)
    call task_cycles            ; DX:AX = start stamp
    push dx                     ; start stamp: hi, then lo
    push ax
    call fm_rcmd
    pop ax
    pop dx
    pop word [snd_inst]         ; the outer dispatch's stamp, back
    pop di
    or di, di
    jz .ctx_done
    call inst_charge
.ctx_done:
    call gfx_unlock
.ret:
    ret

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
    call ui_drag_xor            ; draw the outline (VRAM-direct, SPEC.md 32:
                                ; a transient overlay never enters the back
                                ; buffer, so no flush is needed here)

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

    ; clamp x: 0 <= x and x + w <= [vid_w] (signed - x may have gone negative)
    mov ax, [ui_curx]
    mov bx, [vid_w]
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

    ; clamp y: MBAR_H <= y and y + TITLE_H <= [vid_h] (title bar on screen)
    mov ax, [ui_cury]
    cmp ax, [vid_ymax]
    jle .y_high_ok
    mov ax, [vid_ymax]
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
    call vga_xor_rect_vram      ; clips, so off-screen edges are fine.
                                ; VRAM-direct (SPEC.md 32): the outline is a
                                ; transient overlay like the cursor - it is
                                ; always XOR-erased before the lock drops, so
                                ; VRAM still equals the back buffer whenever
                                ; anyone else can draw. Going through the back
                                ; buffer instead would dirty the whole window
                                ; rect and flush it twice per pass.
    ret

; -----------------------------------------------------------------------------
; ui_grow - grow-box resize: XOR outline anchored at the window's top-left,
;           its SIZE follows the mouse; W_W/W_H commit on release
;           (SPEC.md 11.1/13 - ui_drag's binding lock/XOR ordering, verbatim)
; in:  BX = window ptr (frontmost, WF_SIZABLE), CX = mousedown x, DX = " y
; out: nothing; W_W/W_H updated (clamped WMIN_W/H .. screen edge), full
;      repaint done
; clobbers: AX, BX, CX, DX, DI (internal helper of the UI task)
;
; Reuses ui_drag's statics - one tracker runs at a time on the single UI
; task: ui_curx/ui_cury hold the FIXED outline origin (W_X/W_Y),
; ui_origx/ui_origy the frame size at mousedown, ui_dragw/ui_dragh the
; tracked size ui_drag_xor draws. The low clamp applies every pass (the
; XOR rect must stay well-formed); the screen-edge clamp applies at
; release. The gfx lock is only ever released while the outline is erased,
; and the release path never re-takes the held lock (SPEC.md 7/13).
; -----------------------------------------------------------------------------
ui_grow:
    mov [ui_dragwin], bx
    mov [ui_startx], cx         ; mousedown point
    mov [ui_starty], dx
    mov ax, [bx + W_X]
    mov [ui_curx], ax           ; outline origin: fixed for the whole grow
    mov ax, [bx + W_Y]
    mov [ui_cury], ax
    mov ax, [bx + W_W]
    mov [ui_origx], ax          ; frame size at mousedown...
    mov [ui_dragw], ax          ; ...and the tracked outline size
    mov ax, [bx + W_H]
    mov [ui_origy], ax
    mov [ui_dragh], ax

    call gfx_lock
    call ui_drag_xor            ; draw the outline (VRAM-direct, SPEC.md 32)

.track:
    mov di, ui_ev               ; button tracked by EDGE, like ui_drag: a
    call evq_pop                ; release + fast re-press inside one pass
    jc .no_evt                  ; must still end the resize
    cmp word [ui_ev + EV_TYPE], EVT_MUP
    jne .track                  ; drain anything else queued mid-resize
    jmp .release                ; exits with outline drawn + lock held
.no_evt:
    test byte [mouse_btn], 1    ; fallback: the EVT_MUP was dropped
    jz .release
    call ui_drag_xor            ; erase...
    call gfx_unlock             ; ...only then may the lock drop
    call task_yield
    call gfx_lock
    mov ax, [mouse_x]           ; tracked size = orig + (mouse - start),
    sub ax, [ui_startx]         ; clamped low so the XOR rect stays
    add ax, [ui_origx]          ; well-formed
    cmp ax, WMIN_W
    jge .wok
    mov ax, WMIN_W
.wok:
    mov [ui_dragw], ax
    mov ax, [mouse_y]
    sub ax, [ui_starty]
    add ax, [ui_origy]
    cmp ax, WMIN_H
    jge .hok
    mov ax, WMIN_H
.hok:
    mov [ui_dragh], ax
    call ui_drag_xor            ; redraw every pass, moved or not

    mov ax, [ticks]             ; linger lit until the next tick (the same
.linger:                        ; duty-cycle fix as ui_drag)
    call task_yield
    cmp ax, [ticks]
    je .linger
    jmp .track

.release:
    call ui_drag_xor            ; erase; lock stays held to the end
    mov bx, [ui_dragwin]

    ; clamp w: WMIN_W <= w <= [vid_w] - W_X (high bound first, low wins)
    mov ax, [ui_dragw]
    mov dx, [vid_w]
    sub dx, [bx + W_X]
    cmp ax, dx
    jle .w_high_ok
    mov ax, dx
.w_high_ok:
    cmp ax, WMIN_W
    jge .w_low_ok
    mov ax, WMIN_W
.w_low_ok:
    mov [bx + W_W], ax

    ; clamp h: WMIN_H <= h <= [vid_h] - W_Y
    mov ax, [ui_dragh]
    mov dx, [vid_h]
    sub dx, [bx + W_Y]
    cmp ax, dx
    jle .h_high_ok
    mov ax, dx
.h_high_ok:
    cmp ax, WMIN_H
    jge .h_low_ok
    mov ax, WMIN_H
.h_low_ok:
    mov [bx + W_H], ax

    call wm_paint_all           ; caller-held lock, per the wm contract
    call gfx_unlock             ; NO second gfx_lock here - already held (§7)
    ret

; -----------------------------------------------------------------------------
; ui_dispatch - route one menu selection (SPEC.md 13)
; in:  AX from menu_track: 0xFFFF = nothing selected, else AH = bar cell
;      index (0 = the System menu), AL = item index
; out: nothing (CMD_REBOOT never returns)
; clobbers: AX, BX, CX, DX, SI, DI (internal helper of the UI task)
;
; Three routes, in this order (SPEC.md 12/12.2/12.3):
;   AH = 0                  the System menu - identical in every
;                           application; cmd = CMD_ABOUT + item
;   AM_ONCMD = 0            a kernel-dispatched set, i.e. Locator;
;                           cmd = ui_loc_base[cell] + item
;   otherwise               the active application's own handler, called
;                           exactly like W_ONCLICK: under the gfx lock, on
;                           this task, billed to the owning instance
; -----------------------------------------------------------------------------
ui_dispatch:
    cmp ax, 0xFFFF
    je .ret
    or ah, ah
    jz .kernel                  ; cell 0: the System menu
    mov bx, [menu_set]
    or bx, bx
    jz .ret                     ; a window with no menus cannot be here
    cmp word [menu_kset + AM_ONCMD], 0  ; the KERNEL COPY (SPEC.md 12.2) -
    jne .app                    ; [menu_set] is an owner-segment offset the
                                ; kernel no longer dereferences
.kernel:
    mov bl, ah                  ; cmd = the cell's base + the item index
    xor bh, bh
    cmp bx, 3                   ; ui_loc_base has three entries; a set with
    jae .ret                    ; more cells must have had a handler
    shl bx, 1
    xor ah, ah
    add ax, [ui_loc_base + bx]
    jmp ui_cmd
.ret:
    ret

.app:
    ; The menu handler is a window callback reached through the bar, so it
    ; gets the W_ONCLICK treatment verbatim (SPEC.md 12.2): gfx lock, the
    ; owning instance stamped for sound grants (SPEC.md 34.3) and billed
    ; for the cycles it spends (SPEC.md 8.1). The selection parks in .bss
    ; rather than on the stack because task_cycles answers in DX:AX and
    ; the UI task is the only thing that can ever be in here.
    dec ah                      ; cell -> the app's own 0-based menu index
    mov [ui_menusel], ax
    call gfx_lock
    mov bx, [menu_win]          ; the lock dropped between menu_track and
    or bx, bx                   ; here, so re-check the owner before calling
    jz .app_done                ; into it. This guard used to be unreachable
    test word [bx + W_FLAGS], 2 ; insurance once - the old reasoning was
    jz .app_done                ; "only packages reach this path, and
                                ; packages had no tasks then", false twice
                                ; over now. Locator's fm_menus made it
                                ; LOAD-BEARING (SPEC.md 12.3): a kernel
                                ; window can be destroyed by a command
                                ; picked from its own bar while the lock
                                ; was down - and a package's own worker
                                ; (SPEC.md 20.6) can tear its window down
                                ; from inst_task_die in that same gap.
                                ; wm_destroy clears the bit under the lock
                                ; we hold, and only the UI task (us) calls
                                ; wm_create, so the slot cannot be recycled
                                ; behind this test.
    call inst_win_owner         ; DI = owning record, 0 = unowned window
    push di
    push word [snd_inst]
    call snd_disp_set
    call task_cycles            ; DX:AX = start stamp
    push dx                     ; start stamp: hi, then lo
    push ax
    call inst_cb_seg            ; AX = the owner's segment (DI = the record
                                ; captured above; SPEC.md 11/12.2 dispatch)
    mov si, [menu_win]          ; the handler's contract: AL = item,
    mov bx, [menu_set]          ; AH = menu, SI = window, BX = the set as
                                ; the application registered it (its own
                                ; offset in its own segment, SPEC.md 12.2 -
                                ; a handler may keep indexing its own
                                ; tables through it)
    push ax                     ; the resolved segment above the offset:
    push word [menu_kset + AM_ONCMD]    ; inst_cb_call's argument pair -
                                ; the handler OFFSET from the kernel copy
                                ; (SPEC.md 12.2: the registered set is
                                ; never dereferenced at dispatch)
    mov ax, [ui_menusel]
    call inst_cb_call
    pop ax                      ; start stamp
    pop dx
    pop word [snd_inst]         ; the outer dispatch's stamp, back
    pop di
    or di, di
    jz .app_done
    call inst_charge
.app_done:
    call gfx_unlock
    ret

; the base CMD_* of each kernel-dispatched bar cell (SPEC.md 12.3): the
; System menu, then Locator's File and Special
ui_loc_base:
    dw CMD_ABOUT, CMD_CLOCK, CMD_REBOOT

; -----------------------------------------------------------------------------
; ui_post_cmd - defer one of the kernel's own menu commands (SPEC.md 13)
; in:  AX = CMD_*
; out: nothing (all registers preserved)
;
; inst_launch_post's counterpart for the commands that are not launches, and
; it is load-bearing rather than stylistic. gfx_lock is a NON-RECURSIVE spin
; released only by the UI task (SPEC.md 7), and a window callback - a
; W_ONCLICK, or an AM_ONCMD handler reached through the bar (SPEC.md 12.2) -
; already holds it. Such a handler calling ui_cmd's CMD_CLOSE or CMD_REBOOT
; would take a lock it is standing on and hang the machine dead: no beep, no
; watchdog, no recovery. One word store here, drained by ui_task step 3 with
; nothing held.
;
; It also keeps the billing honest. ui_dispatch's .app route captures the
; owning instance BEFORE calling the handler and charges it AFTER; CMD_CLOSE
; is the first menu command that can free that very record, so running it
; inline would write cycles into a slot that had just been released.
;
; [ui_pcmd] lives in .text, not .bss: ui_task reads it on its first pass and
; -f bin gives .bss no image bytes (the dsk_cwd lesson, SPEC.md 1).
; -----------------------------------------------------------------------------
ui_post_cmd:
    mov [ui_pcmd], ax
    ret

ui_pcmd:    dw 0                ; posted CMD_*, 0 = none (see above)

; -----------------------------------------------------------------------------
; ui_cmd - act on one of the kernel's own menu commands
; in:  AX = CMD_* (0 = nothing; ignored)
; out: nothing (CMD_REBOOT never returns)
; clobbers: AX, BX (internal helper of the UI task)
; Runs on the UI task with NO lock held, so app commands call app_launch
; directly (SPEC.md 13/29): a new instance per launch, or - for a kind at
; its cap - front + un-minimize the existing one. A refused command beeps
; (snd_beep, SPEC.md 34.3): a failed launch (tables full), or CMD_CLOSE -
; which quits the frontmost instance - with nothing to close.
; -----------------------------------------------------------------------------
ui_cmd:
    or ax, ax
    jz .done
    cmp ax, CMD_ABOUT
    je .about
    cmp ax, CMD_CTRL
    je .ctrl
    cmp ax, CMD_CLOCK
    je .clock
    cmp ax, CMD_BOUNCE
    je .bounce
    cmp ax, CMD_FILES
    je .files
    cmp ax, CMD_CLOSE
    je .close
    cmp ax, CMD_TASKS
    je .tasks
    cmp ax, CMD_REBOOT
    je .reboot
.done:
    ret

.about:
    mov al, KIND_ABOUT
    jmp .launch
.ctrl:
    mov al, KIND_CTRL
    jmp .launch
.clock:
    mov al, KIND_CLOCK
    jmp .launch
.bounce:
    mov al, KIND_BOUNCE
    jmp .launch
.tasks:
    mov al, KIND_TASKMGR
.launch:
    call app_launch             ; takes its own locks; out CF = tables full
    jnc .launched
    call snd_beep               ; audible refusal (SPEC.md 34.3)
.launched:
    ret

.files:
    call files_open             ; fronts, creates or moves a file-manager
    ret                         ; window (SPEC.md 22.1); locks internally

.close:
    call gfx_lock
    call wm_top                 ; BX = frontmost visible, 0 if none
    or bx, bx
    jz .close_none
    call app_close_win          ; in: BX; quit its instance (SPEC.md 29)
    jmp .close_out
.close_none:
    call snd_beep               ; nothing to close: audible refusal (34.3)
.close_out:
    call gfx_unlock
    ret

.reboot:
    call gfx_lock               ; never released - the GUI is done
    call vid_text
    call sched_unhook           ; restores int 08h/0Ch (unhooks the mouse too)
    int 0x19
    jmp .reboot                 ; unreachable; int 19h does not return

; -----------------------------------------------------------------------------
section .bss

ui_ev:      resb 8              ; popped event record (EV_TYPE/A/B/C)
ui_menusel: resw 1              ; ui_dispatch scratch: AH = menu, AL = item,
                                ; held across the billing calls (SPEC 12.2)
ui_click_t: resw 1              ; EV_C of the event being dispatched: the
                                ; click's birth tick (valid during dispatch,
                                ; which always stores before handlers read)
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
