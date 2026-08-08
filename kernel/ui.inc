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
; A RIGHT mouse-down takes none of that ladder: ui_rdown swallows the bar
; and the desktop, raises a background window without opening anything, and
; pops a context menu (SPEC.md 12.4) on two things only - the frontmost
; file-manager window's content, and a DOCK TILE, whose one item is Close
; (SPEC.md 30.2). It never stamps [ui_click_t], which is what keeps a
; right-click from composing with the next left click into a double-click.
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

UI_TDBLT    equ 9               ; title-bar double-click window, ticks - the
                                ; same half second as DESK_DBLT, FM_DBLCLK
                                ; and FD_DBLCLK, and deliberately the same
                                ; number rather than a shared constant: they
                                ; are four independent detectors and one of
                                ; them may one day want to differ

; -----------------------------------------------------------------------------
; ui_task - the UI task main loop (task 0 jumps here from kmain; never returns)
; in:  nothing
; out: never returns
; clobbers: everything (it is a task entry, not a callable routine)
; -----------------------------------------------------------------------------
ui_task:
.loop:
    ; --- 0. a posted restart, spent with NOTHING held (SPEC.md 20.10) --------
    cmp byte [ui_rebootq], 0
    je .keys
    mov byte [ui_rebootq], 0    ; ...cleared first: ui_cmd_reboot does not
    call ui_cmd_reboot          ; return, but a path that ever did must not
                                ; find the post still standing
.keys:
    ; --- 1. keyboard: poll, and feed the front window's key handler ----------
    mov ah, 0x01                ; any key pending?
    int 0x16
    jz .events
    mov ah, 0x00                ; fetch it: AL = ascii, AH = scan
    int 0x16
    call kbm_key                ; with no mouse on the machine, the arrows ARE
    jc .events                  ; the mouse (SPEC.md 9.6) and this key is not
                                ; the application's. One compare per keystroke
                                ; on a machine that has one
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
    mov bx, ax
    pop ax                      ; key back for the handler's contract
    push dx                     ; start stamp: hi, then lo
    push bx
    push bp
    mov bp, [si + W_ONKEY]      ; wm_pkgcall: record in SI (where the
    call wm_pkgcall             ; contract already puts it), offset in BP
    pop bp
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
    call ui_loc_gate            ; ...and Locator's own items go grey or live
    call gfx_lock               ; before it can draw them (SPEC.md 47 rule 5)
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
    push bp
    mov bp, [si + W_ONCLICK]    ; wm_pkgcall: record in SI, offset in BP
    call wm_pkgcall
    pop bp
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
    call ui_tdbl                ; the second press on THIS title bar inside
    jnc .tb_raise               ; UI_TDBLT ticks ZOOMS it (SPEC.md 11.95)
    test word [si+W_FLAGS], WF_SIZABLE
    jz .tb_raise                ; ...if it is resizable. A fixed-layout window
                                ; has nothing to zoom into, so the press stays
                                ; an ordinary one and drags as before
    mov bx, si
    call gfx_lock
    call wm_zoom                ; out to the desktop band, or back to wherever
    call gfx_unlock             ; it was when it left
    jmp short .yield            ; and no drag: the button is still down, but
                                ; the EVT_MUP behind it is ignored by the
                                ; loop above, which acts on EVT_MDOWN alone
.tb_raise:
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
    call kbm_ui                 ; the keyboard mouse's end-of-pass service
                                ; (SPEC.md 9.6.1): a press held the button so
                                ; that whatever this pass dispatched could
                                ; track it, and if nothing did, this is where
                                ; the press becomes a click
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
    je .chk_assoc               ; ...and NOT .chk_pcmd: a document open posts
                                ; no package index, so skipping past the step
                                ; below on "no package pending" is skipping
                                ; the only case that can be pending
    mov ax, [ld_pending]        ; stored as directory index + 1
    dec ax
    mov word [ld_pending], 0    ; consume before running, not after
    call loader_run             ; in: AX = index; does its own locking
.chk_assoc:
    cmp byte [assoc_dapp], 0xFF ; a document double-click (SPEC.md 54.4):
    je .chk_pcmd                ; find its program, load it, and hand the
    call assoc_run              ; document over by pull. Consumes the slot
                                ; itself, and does its own locking
.chk_pcmd:
    mov ax, [ui_pcmd]           ; a kernel menu command posted from under
    or ax, ax                   ; the lock (ui_post_cmd, SPEC.md 13)
    jz .chk_fmf                 ; every step's "nothing to do" names the NEXT
                                ; step, so a step inserted below is a step
                                ; this jump has to be pointed at
    mov word [ui_pcmd], 0       ; consume before running, not after
    call ui_cmd                 ; in: AX = CMD_*; takes its own locks, and
                                ; CMD_REBOOT never comes back
.chk_fmf:
    cmp byte [fm_fchk], 0       ; a file operation changed a folder, or the
    je .chk_cp                  ; active window changed (SPEC.md 22.8): the
    mov byte [fm_fchk], 0       ; FRONT window may be a Disk window owing a
    call gfx_lock               ; refresh. One compare per pass when it is
    call wm_top                 ; not - which is nearly always, because
    mov al, 1                   ; wm_raise has usually spent the debt on the
    call fm_focus               ; way in. AL = 1: there is nobody here to
    call gfx_unlock             ; defer the repaint to, so fm_focus draws
.chk_cp:
    cmp byte [cp_dirty], 0      ; scheduler mode changed in the Control
    je .chk_zones               ; Panel (SPEC.md 31.2): windows that quote
    mov byte [cp_dirty], 0      ; the mode - the About box's third line -
    call gfx_lock               ; are already painted, so repaint them here,
    call wm_paint_all           ; outside cp_onclick's held lock
    call gfx_unlock
.chk_zones:
    cmp byte [desk_zdirty], 0   ; a volume was mounted or unmounted (26.2),
    je .chk_clk                 ; posted for the same reason and drained in
    mov byte [desk_zdirty], 0   ; the same pass - but it costs the drive-zone
    call gfx_lock               ; grid and the windows over it, not the screen
    call desk_zones_paint
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
    call mou_hotplug            ; a mouse plugged in after boot gets its
                                ; reset edge (SPEC.md 9.4); one cmp per pass
                                ; once any packet has ever arrived
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
;     not. It reaches dock_rclick instead (SPEC.md 30.2), which is the right
;     button's own, much smaller routine and shares only the hit test. The
;     bare desktop and desk_click stay out altogether - there is nothing
;     there a right button should mean, and activating Locator would move a
;     focus the press did not have to.
;   - open anything on a background window. It raises it and stops: the
;     popup always belongs to the window you can already see.
;
; The bar is skipped whole, clock cell included. Of the WINDOWS only a
; file-manager one answers - the ones carrying fm_menus - so no package's
; window changes behaviour and no shipped .o88 needs rebuilding.
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
    jz .strip                   ; no window: the dock gets the press, and the
                                ; bare desktop gets nothing (SPEC.md 30.2)

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
    cmp word [si + W_SEG], 0    ; a kernel window whose set is fm_menus -
    jne .ret                    ; the OFFSET alone is not identity: a
    cmp word [si + W_MENUS], fm_menus
    jne .ret                    ; package's org-0 W_MENUS can collide with
                                ; it numerically (SPEC.md 12.3)

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

.strip:
    call dock_rclick            ; CX/DX; CF = 1 = consumed (locks internally,
    ret                         ; SPEC.md 30.2). CF is nobody's answer here -
                                ; the desktop's case IS "do nothing"

; -----------------------------------------------------------------------------
; ui_tdbl - was this title-bar press the second half of a double-click?
; in:  SI = the window the press landed on; [ui_click_t] = its birth tick
; out: CF = 1 double-click (and the pair is spent), CF = 0 first click (and
;      it is recorded)
; clobbers: AX
;
; The same shape as the double-click detectors of SPEC.md 22/26/38, and for
; the same three reasons. It compares the clicks' BIRTH ticks (EV_C) and not
; [ticks], so two presses queued either side of something slow - a mount, a
; package load - cannot collapse into a pair they never were. It keys on the
; WINDOW as well as on the time, so a press on one title bar followed by a
; press on another is two first clicks rather than a maximize of the second.
; And a completed pair is SPENT, so a third press starts a fresh pair instead
; of maximizing again on every press of a held-down triple.
;
; A window pointer is never 0, so 0 is the "nothing recorded" sentinel - and
; .bss arrives zeroed (the overlay note in kernel.asm), so the machine's very
; first title-bar press takes .first like any other.
; -----------------------------------------------------------------------------
ui_tdbl:
    mov ax, [ui_click_t]
    cmp si, [ui_tclkw]
    jne .first                  ; a different window: this is a first click
    sub ax, [ui_tclkt]          ; wrap-safe - the difference is what matters
    cmp ax, UI_TDBLT
    jae .first                  ; too slow: it restarts the pair below
    mov word [ui_tclkw], 0
    stc
    ret
.first:
    mov ax, [ui_click_t]
    mov [ui_tclkt], ax
    mov [ui_tclkw], si
    clc
    ret

; -----------------------------------------------------------------------------
; ui_drag - title-bar drag: XOR outline follows the mouse, window moves on
;           release (SPEC.md 13 - the ordering below is binding)
; in:  BX = window ptr, CX = mousedown x, DX = mousedown y
; out: nothing; window's W_X/W_Y updated (clamped: title bar fully on screen),
;      and a full repaint IF it moved
; clobbers: AX, BX, CX, DX, DI (internal helper of the UI task)
;
; The gfx lock is only released while the outline is erased: XOR draw/erase is
; self-inverting only if nothing touches the covered pixels in between, and
; the background tasks repaint the instant the lock drops. The release path
; runs entirely under the lock already held - re-locking would deadlock.
;
; **A drag that did not move the window repaints nothing**, and that is not a
; micro-optimisation: clicking a background window's title bar to focus it
; comes through here, and the caller has ALREADY repainted everything to
; raise it (.title_bar's wm_front). Repainting again on release made every
; focus change cost two full screen redraws - visible as a double flash, and
; on a 4.77MHz machine as a wait. The outline is XOR-erased before the clamps
; below, so when neither coordinate changes the screen is already correct.
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
    call kbm_pollm              ; the keyboard mouse drags too (SPEC.md 9.6)
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
    call wm_snap_ax             ; back onto a multiple of 8 if this window
                                ; asked (SPEC.md 11.94). BEFORE the compare,
                                ; not after the store: what "moved" has to
                                ; mean is where the window ENDS UP, and a
                                ; drag inside one 8px step ends up nowhere
    xor di, di                  ; DI = "the window actually moved"
    cmp [bx + W_X], ax
    je .x_same
    inc di
.x_same:
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
    cmp [bx + W_Y], ax
    je .y_same
    inc di
.y_same:
    mov [bx + W_Y], ax

    call wm_dock_snap           ; barely over the dock? then back above it,
    jnc .nosnap                 ; if it still fits (SPEC.md 11.90)
    inc di                      ; ...and it moved after all
.nosnap:

    or di, di                   ; a click that only raised the window (above)
    jz .done                    ; leaves the screen already correct

    ; It moved. Only two rects changed: the one it left and the one it now
    ; occupies - everything outside their union is still correct on screen,
    ; so this is wm_paint_dmg's case, not wm_paint_all's (SPEC.md 11.91). The
    ; union rather than two passes because the two overlap on any short drag,
    ; and a rect that big is still a fraction of the screen.
    mov ax, [ui_origx]          ; where it was, drop shadow included
    mov cx, ax
    add cx, [ui_dragw]
    mov bx, [ui_origy]
    mov dx, bx
    add dx, [ui_dragh]
    mov si, [ui_dragwin]
    call wm_dmg_union           ; ...grown to cover where it is now
    call wm_paint_dmg           ; caller-held lock, per the wm contract
.done:
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
; out: nothing; W_W/W_H updated (clamped WMIN_W/H .. screen edge), and the
;      union of the old and new rects repainted
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
    call kbm_pollm              ; ...and resizes (SPEC.md 9.6)
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
    mov cx, ax

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
    mov dx, ax

    call wm_ask_size            ; the window gets a say BEFORE the record
    mov [bx + W_W], cx          ; changes (SPEC.md 11.1): it answers with the
    mov [bx + W_H], dx          ; size it will take, per axis, and by then
                                ; nothing has been drawn at the other one

    ; A grow that stopped just over the dock is nudged back above it, if it
    ; still fits there (SPEC.md 11.90). It moves the ORIGIN, which nothing
    ; else in this loop does, so the old bottom is banked first: the union
    ; below has to reach it whichever way the origin went.
    mov ax, [bx + W_Y]
    add ax, [ui_origy]          ; the old rect's last row, origin unmoved
    push ax
    call wm_dock_snap
    pop ax

    ; Only the rect it had and the rect it has changed, so the repaint is
    ; their union: wm_paint_dmg's case, not wm_paint_all's (SPEC.md 11.91).
    ; Dragging a corner used to repaint every window on the screen.
    mov dx, [bx + W_Y]
    add dx, [bx + W_H]          ; the new last row...
    cmp dx, ax
    jae .uh
    mov dx, ax                  ; ...or the old one, whichever is lower
.uh:
    mov ax, [bx + W_W]
    cmp ax, [ui_origx]          ; width never moves its origin, so that axis
    jge .uw                     ; is still just the larger of the two
    mov ax, [ui_origx]
.uw:
    mov cx, ax
    add cx, [bx + W_X]          ; x2 = x + w: the drop shadow's own column
    mov ax, [bx + W_X]
    mov bx, [bx + W_Y]          ; (the read completes before BX changes)
    call wm_paint_dmg           ; caller-held lock, per the wm contract
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

; -----------------------------------------------------------------------------
; ui_loc_gate - point Locator's conditional items at their live or disabled
;               string, just before a menu can be dropped (SPEC.md 47 rule 5)
; in:  nothing
; out: nothing (all registers preserved)
;
; 'Close Window' with nothing open used to be a live item that answered with a
; beep. Whether a window is there is a stable, cheap fact - `wm_top` - so it is
; exactly what rule 5 says to grey rather than refuse, and it is the case a
; user meets on the desktop at boot before meeting any other.
;
; The swap is a store into the item ARRAY, which is the same idiom a package
; uses (SPEC.md 12.2: point AMENU_ITEMS at a different string). It runs on the
; press that opens the bar rather than at layout time because nothing relays
; the bar out when the last window closes - `menu_check` hands ownership over,
; but Locator's own item list is static and would keep whatever it last held.
;
; NOT called before a popup (§12.4) or from menu_relayout: this list is only
; ever read while a menu is dropping, and one call site is one thing to keep
; right. The tables-full refusals below (Timer, Bounce, Disk at the instance
; cap) stay beeps - MAX_TASKS is 12, a user reaches it about never, and the
; predicate is per-kind rather than one word.
; -----------------------------------------------------------------------------
ui_loc_gate:
    push ax
    push bx
    mov ax, menu_s_close        ; live: the plain string
    call wm_top                 ; BX = frontmost visible, 0 = nothing to close
    or bx, bx
    jnz .set
    mov ax, menu_s_closex       ; ...or its MENU_DIS twin
.set:
    mov [menu_items_file + 3*2], ax
    pop bx
    pop ax
    ret

ui_dispatch:
    cmp ax, 0xFFFF
    je .ret
    or ah, ah
    jz .kernel                  ; cell 0: the System menu
    mov word [ui_about], 0
    mov bx, [menu_abcell]       ; the app-NAME cell (SPEC.md 12.7)? It is
    or bx, bx                   ; APPENDED after the app's own menus, so it
    jz .notab                   ; is tested before the set - and it is legal
    cmp bl, ah                  ; on a window that has no menu set at all,
    jne .notab                  ; which the test below would have dropped
    cmp al, [menu_abclose]      ; its bottom item is Close and that one is the
    je .appclose                ; KERNEL's, whatever the application is: it is
                                ; answered here rather than handed on, so no
                                ; package ever sees an item it was not written
                                ; for (SPEC.md 12.7)
    mov word [ui_about], 1
    jmp .app
.notab:
    mov bx, [menu_set]
    or bx, bx
    jz .ret                     ; a window with no menus cannot be here
    push es                     ; the set is the OWNER's data (SPEC.md 12.2):
    push cs                     ; Locator's lives in kernel .text, a
    pop es                      ; package's in its own segment - so this
    mov di, [menu_win]          ; read must go through the owner's segment,
    or di, di                   ; like every other read of the set. Through
    jz .ownset                  ; DS it tested a random word of kernel .text
    call wm_strseg              ; at the package set's offset, and dispatched
.ownset:                        ; app menus as Locator commands whenever that
    cmp word [es:bx + AM_ONCMD], 0  ; word happened to be zero
    pop es
    jne .app
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

.appclose:
    ; 'Close' from the app-name menu (SPEC.md 12.7): the close box of
    ; SPEC.md 13 reached from the bar, and the same routine answers both.
    ; It runs inline rather than through ui_post_cmd because ui_dispatch is
    ; reached with NO lock held - menu_track's was released before the call -
    ; which is exactly why .close_box can take its own lock too.
    call gfx_lock
    mov bx, [menu_win]          ; the lock dropped between menu_track and
    or bx, bx                   ; here, so the window that owned the bar may
    jz .apc_done                ; be gone: the same re-check .app makes below,
    test word [bx + W_FLAGS], 2 ; and for the same reasons
    jz .apc_done
    call app_close_win          ; in: BX; lock held, UI task (SPEC.md 29.4).
                                ; Unbilled, like the close box - and here it
                                ; could not be billed anyway: the record the
                                ; cycles would go to is the one being freed
.apc_done:
    call gfx_unlock
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
    mov si, [menu_win]          ; the handler's contract: AL = item,
    mov bx, [menu_set]          ; AH = menu, SI = window, BX = the set
    push es                     ; the set is the OWNER's data (SPEC.md 12.2),
    push di                     ; so AM_ONCMD is read through its segment
    mov di, si
    call wm_strseg              ; ES = the window's string/data segment
    cmp word [ui_about], 0      ; the name cell dispatches the About handler
    je .oncmd                   ; wm_about_set registered, not AM_ONCMD -
    push bx                     ; and BX may be 0 here (no menu set), so the
    mov bx, si                  ; read below must not happen at all
    call wm_about_get           ; AX = the handler's offset in the owner's seg
    mov cx, ax
    pop bx
    jmp short .cbok
.oncmd:
    mov cx, [es:bx + AM_ONCMD]
.cbok:
    pop di
    pop es
    jcxz .nocb                  ; a handler withdrawn while the lock was down
    mov ax, [ui_menusel]        ; (wm_about_set 0 / MENU_SET in the gap) must
    push bp                     ; not be called at offset 0 (SPEC.md 12.2)
    mov bp, cx
    call wm_pkgcall             ; SI = window, BP = the handler
    pop bp
.nocb:
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
    dw CMD_ABOUT, CMD_TIMER, CMD_REBOOT

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

; =============================================================================
; The Task Manager, which lives on the system disk (SPEC.md 28)
;
; It was a built-in kind and is a package now, so the chip menu's item cannot
; be an app_launch. What it does instead is what a Disk window's double-click
; does, with the navigation added: bank the current volume and directory,
; mount A:, go into SYSTEM, find TASKMGR.O88 in the snapshot by name, run the
; loader, put the volume back. drv_boot does the same dance for a .DRV and
; for the same reason - every file name resolves in the CURRENT directory
; (SPEC.md 19.2), which is wherever the user last browsed to.
;
; SYSTEM is a SECOND navigation (SPEC.md 28.3), so the file costs two mounts
; rather than one - the folder has to be listed before its cluster is known.
; That is the price of the kernel's own applications not being loose in the
; root of the user's disk, it is paid once per chip-menu open, and it is
; small beside the seven kilobytes of package that follow it.
;
; **The item stays live, and it says why when it fails.** Greying it would
; mean answering "can this be loaded?" without loading it, and the only
; honest test is the load itself - no disk, a disk without the file, a full
; instance table, a heap too fragmented to hold seven kilobytes. SPEC.md 47
; rule 3 is explicit that a control greys on a FACT and never on a guess, so
; this one is always clickable and the failure is a notice with the reason in
; it. That is also the only reporting channel here: a Disk window's load
; failures land in its status line, and a menu item has no status line.
;
; It is a SINGLETON from this menu, which it was as a kind. Loading a second
; copy would cost another seven kilobytes and another task slot to say the
; same thing twice, so an already-open one is fronted instead. A double-click
; on TASKMGR.O88 in a Disk window is unaffected and still opens as many as
; you like - that is the package rule, and this is the menu item's rule.
; =============================================================================
ui_s_tmdir:  db 'SYSTEM', 0      ; the folder it lives in (SPEC.md 28.3), on
                                 ; the system disk and the apps disk alike -
                                 ; the menu cannot know which is in the drive
ui_s_tmfile: db 'TASKMGR.O88', 0
ui_s_tmname: db 'TaskMgr', 0     ; the 16-byte header name inst_set_name_x
                                 ; copied into the record - what identifies
                                 ; an already-running one

; -----------------------------------------------------------------------------
; ui_tm_open - front the Task Manager, or load it off the system disk
; in:  nothing; UI task, no lock held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
ui_tm_open:
    push ax
    push bx
    push cx                     ; CX/BP/ES too: ld_run_body is documented as
    push dx                     ; "loader_run saves regs", and loader_run
    push si                     ; pushes exactly these three around it - its
    push di                     ; failure exits return with ES = LOW_SEG or a
    push bp                     ; just-freed region segment
    push es

    call ui_tm_find             ; already running? front it and stop
    jc .load
    call gfx_lock
    and byte [di+I_FLAGS], 0xFE ; un-minimize, the app_launch cap path's rule
    mov bx, [di+I_WIN]
    call wm_show
    call gfx_unlock
    jmp short .out

.load:
    call osapi_file_here        ; bank where the user was: DX = the directory
    mov [ui_tm_cwd], dx         ; cluster, BL = the drive. Two module words
    mov [ui_tm_drv], bl         ; rather than the stack, because this is
                                ; UI-task-only and one load at a time, like
                                ; ld_fp

    mov dl, [dsk_bootvol]       ; the disk we booted from - A: on a floppy
                                ; machine, where this byte is 0 and this is
                                ; the `xor dl, dl` it replaced, and the
                                ; installed PARTITION on a machine that boots
                                ; from its hard disk (SPEC.md 52.10.3).
                                ;
                                ; This one mounts for itself rather than going
                                ; through drv_mounted, because it is the one
                                ; caller that needs a LISTING - it hands the
                                ; loader a directory index - so it has its own
                                ; copy of the question and needed its own fix
    call disk_mount
    mov si, ui_s_nodisk
    jc .fail
    call ui_tm_sysdir           ; into SYSTEM (SPEC.md 28.3); CF=1 = no such
    mov si, ui_s_nofile         ; folder, which reads to the user as the file
    jc .fail                    ; not being there, because it is not
    mov si, ui_s_tmfile
    call dsk_find_name          ; AX = its directory index
    mov si, ui_s_nofile
    jc .fail

    mov word [ld_pwin], 0       ; no poster: this is not a Disk window's load,
    call ld_run_body            ; and so not loader_run either - there is no
                                ; listing to refresh and no status line to
                                ; correct ([ld_status] is loader_run's to
                                ; write; storing it here would retitle an
                                ; open Disk window's status line for a load
                                ; it never posted). AL = LD_*; it shows the
                                ; window itself on the way out
    push ds
    pop es                      ; ld_run_body's failure exits leave ES aimed
                                ; at LOW_SEG or a freed region (see the push
                                ; list above); nothing below may inherit that
    or al, al
    jz .back                    ; LD_OK
    mov bl, al                  ; the reason, out of the same codes a Disk
    xor bh, bh                  ; window's status line uses
    shl bx, 1
    mov si, [bx + ui_tm_errs]
.fail:
    push si                     ; the volume FIRST, then the notice: putting
    call ui_tm_back             ; it back touches the floppy and the notice
    pop si                      ; takes the gfx lock, and neither wants the
    mov di, ui_s_tmttl          ; other held while it runs
    mov bx, ui_s_tmlead
    call ui_note
    jmp short .out
.back:
    call ui_tm_back
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ui_tm_sysdir - step into SYSTEM on the volume just mounted (SPEC.md 28.3)
; in:  the volume is mounted and standing in its root
; out: CF = 0 and the volume is inside SYSTEM; CF = 1 = there is no SYSTEM
;      folder here, or entering it failed
; clobbers: AX, DL, SI, flags (ui_tm_open has banked all three)
;
; The entry has to be LISTED before its first cluster is known, which is why
; this is a second mount and not an extra argument to the first: dsk_find_name
; reads the snapshot the mount published (no I/O at all), and dsk_chdir then
; re-runs the one validated pipeline over the folder (SPEC.md 19.2).
;
; The type test is what stops a FILE called SYSTEM being walked as a
; directory: dsk_synth writes 1 for a file and 2 for a subdirectory, and 3 is
; the synthesized '..' (SPEC.md 19.5), which cannot carry this name.
; -----------------------------------------------------------------------------
ui_tm_sysdir:
    mov si, ui_s_tmdir
    call dsk_find_name          ; AX = its index in the root's listing
    jc .no
    call dsk_get_dir            ; SI -> the staged 32-byte entry (SPEC.md 19)
    cmp word [si+16], 2         ; its type: a folder, not a file of that name
    jb .no
    mov ax, [si+18]             ; its first cluster
    or ax, ax
    jz .no                      ; 0 is the root: a directory entry naming it
    mov dl, [dsk_bootvol]       ; would be a loop, not a folder.
                                ;
                                ; The DRIVE is the one ui_tm_open just mounted,
                                ; and this was `xor dl, dl` - the same A:
                                ; assumption its caller had, one call deeper.
                                ; On an installed machine it walked into
                                ; SYSTEM's cluster on the FLOPPY, which reads
                                ; to the user as the file not being there
    jmp dsk_chdir               ; CF is its answer, and it is ours
.no:
    stc
    ret

; ui_tm_back - put the volume the user was on back. Preserves everything; a
; failed remount leaves the banked volume mount-failed at its root with the
; write gate shut (not A: - the bank holds whatever drive the user was on),
; which is a survivable place to be.
ui_tm_back:
    push ax
    push dx
    mov ax, [ui_tm_cwd]
    mov dl, [ui_tm_drv]
    call dsk_chdir
    pop dx
    pop ax
    ret

ui_tm_cwd:  dw 0                ; the directory and drive to go back to. In
ui_tm_drv:  db 0                ; .text: -f bin zeroes no .bss, and A:'s root
                                ; is the right answer before the first load

ui_tm_errs:                     ; indexed by LD_* (loader.inc)
    dw ui_s_nofile              ; LD_OK never reaches here
    dw ui_s_dskerr              ; LD_EDISK
    dw ui_s_badpkg              ; LD_EBAD
    dw ui_s_toobig              ; LD_EBIG
    dw ui_s_abort               ; LD_EABORT - it loaded and refused itself,
                                ; and for this package the dominant cause is
                                ; wm_create failing (window table full), so
                                ; "not a valid package" would be a lie
    dw ui_s_nomem               ; LD_ENOMEM

ui_s_tmttl:  db 'Task Manager', 0
ui_s_tmlead: db 'Cannot open the Task Manager:', 0
ui_s_nodisk: db 'No system disk in drive '
ui_s_nodisk_l: db 'A:', 0       ; the LETTER, stamped once by dsk_boot_from
                                ; (SPEC.md 52.10.3) - a label rather than an
                                ; offset, so rewording the message cannot
                                ; leave the stamp pointing at a space
ui_s_nofile: db 'TASKMGR.O88 is not in SYSTEM', 0    ; ...and it fits the
                                 ; 288px notice window, which a path spelled
                                 ; out in full would not: app_about_center
                                 ; centres it and font_str stops at the SCREEN
                                 ; edge, not the window's
ui_s_dskerr: db 'Disk error reading it', 0
ui_s_badpkg: db 'TASKMGR.O88 is not a valid package', 0
ui_s_toobig: db 'TASKMGR.O88 is too large', 0
ui_s_abort:  db 'It refused to start', 0
ui_s_nomem:  db 'Not enough memory to open it', 0

; -----------------------------------------------------------------------------
; ui_tm_find - the running Task Manager, by the name in its instance record
; in:  nothing
; out: CF = 0 and DI = its record; CF = 1 none open
; clobbers: DI (the output), flags
;
; By NAME and not by a remembered window pointer, because a window slot is
; reused: a pointer banked at load time names some other app's window the
; moment this one closes, and fronting that is a bug with no symptom until
; someone notices the wrong window coming up.
; -----------------------------------------------------------------------------
ui_tm_find:
    push ax
    push cx
    push si
    mov di, inst_tab
    mov cx, INST_MAX
.scan:
    cmp byte [di+I_STATE], 1
    jne .next
    test byte [di+I_KIND], KIND_PKG
    jz .next
    push di
    mov si, ui_s_tmname
    add di, I_NAME
.cmp:
    mov al, [si]
    cmp al, [di]
    jne .nomatch
    inc si
    inc di
    or al, al
    jnz .cmp
    pop di
    clc
    jmp short .out
.nomatch:
    pop di
.next:
    add di, I_RECSZ
    loop .scan
    stc
.out:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ui_note - the kernel's one-line notice window
; in:  SI = a NUL reason, BX = the line above it, DI = the window's title;
;      UI task, no lock held
; out: nothing (all registers preserved)
;
; All three strings come from the CALLER. They used to be one caller's: the
; title was 'Task Manager' and the lead 'Cannot open the Task Manager:',
; baked in because ui_tm_open was the only thing that raised a notice. When
; assoc_run (SPEC.md 54.4) became the second, a document whose program was
; not on the disk reported "Cannot open the Task Manager" over a window
; titled Task Manager - a message about a component the user had not
; mentioned, in place of the one that had actually failed.
;
; The species is the file dialog's (SPEC.md 38): a bare wm_create'd window
; this module owns, with no instance behind it - so no dock tile, no Task
; Manager row, and a close box that reduces to wm_hide, which is exactly what
; dismissing a notice means. ONE window, created on first use and shown
; again after that, so W_TITLE is restamped per call; [ui_note_win] lives in
; .text because -f bin zeroes no .bss and this is read before anything writes
; it (the fdlg_win precedent).
; -----------------------------------------------------------------------------
ui_note_tpl: dw 150, 150, 288, 78, ui_s_tmttl, ui_note_paint, 0, 0
ui_note_win: dw 0
ui_note_msg: dw 0
ui_note_lead: dw 0

ui_note:
    push ax
    push bx
    push si
    push di
    push es
    push ds
    pop es                      ; wm_create reads the template through ES:SI
                                ; and stamps W_SEG from ES (SPEC.md 11/20.1) -
                                ; a caller arriving with ES elsewhere would
                                ; register a garbage PACKAGE window whose
                                ; callbacks far-call into that segment
    mov [ui_note_msg], si
    mov [ui_note_lead], bx
    call gfx_lock
    mov bx, [ui_note_win]
    or bx, bx
    jnz .have
    mov si, ui_note_tpl
    call wm_create              ; CF = the window table is full, in which case
    jc .out                     ; the notice is simply not shown - there is
    mov [ui_note_win], bx       ; nothing further to fall back to
.have:
    mov [bx+W_TITLE], di        ; the window is reused, so the caption belongs
                                ; to whichever caller last asked for it. Set
                                ; before wm_show, which draws the frame whole
    call wm_show                ; ...or bring an already-dismissed one back
.out:
    call gfx_unlock
    pop es
    pop di
    pop si
    pop bx
    pop ax
    ret

ui_note_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    call gfx_pen_live           ; CBLACK and the disabled flag clear (47)
    mov bx, si
    mov dx, [bx+W_Y]
    add dx, TITLE_H + 16
    mov si, [ui_note_lead]
    call app_about_center
    add dx, 16
    mov si, [ui_note_msg]
    call app_about_center
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    cmp ax, CMD_TIMER
    je .timer
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
.timer:
    mov al, KIND_TIMER
    jmp .launch
.bounce:
    mov al, KIND_BOUNCE
    jmp .launch
.tasks:
    call ui_tm_open             ; not a kind any more: it is a package on the
    ret                         ; system disk (SPEC.md 28)
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
; ...and its OTHER entry point, ui_reboot_post's deferred one (SPEC.md 20.10).
; A label rather than a routine: the body below never returns, so there is
; nothing for two callers to disagree about.
ui_cmd_reboot:
    call cp_flush_close         ; the OTHER way the panel stops existing
                                ; (SPEC.md 31.8). Before the lock, because a
                                ; disk write is not something to do with the
                                ; GUI already torn down - and before vid_text,
                                ; so a failure still has a screen to have
                                ; happened on
    call drv_shutdown           ; detach every loaded driver (SPEC.md 51.2):
                                ; int 19h resets no hardware, so a hooked
                                ; card would ride the reboot with its vector
                                ; pointing into a heap the next boot reuses
                                ; and an auto-init DMA still running. Before
                                ; sched_unhook, because the unload WAITS on
                                ; the worker's death, which needs the
                                ; scheduler alive - and before the lock,
                                ; because that wait yields
    call gfx_lock               ; never released - the GUI is done
    call vid_text
    call sched_unhook           ; restores int 08h/0Ch (unhooks the mouse too)
    int 0x19
    jmp ui_cmd_reboot           ; unreachable; int 19h does not return

; -----------------------------------------------------------------------------
; ui_reboot_post - API slot 0x0368 (SPEC.md 20.10). No arguments, no answer.
;
; POSTS a restart; it does not perform one. Every caller of this slot is a
; window callback or a Control Panel page, and all of those run on the UI task
; WITH THE GFX LOCK HELD - while the path above takes that lock itself and,
; before it, waits for every driver's worker to die (SPEC.md 51.2), a wait
; that yields to a task which may want the lock. Called inline from a callback
; it is a deadlock with the machine half torn down.
;
; So it sets a byte and returns, and ui_task spends it at the top of its pass
; where nothing is held - the same reasoning that runs the app menu's Close
; inline in ui_dispatch (SPEC.md 12.7). A second post before the first is
; spent is the same restart, which is why this is a flag and not a count.
; -----------------------------------------------------------------------------
ui_reboot_post:
    mov byte [ui_rebootq], 1
    ret

; -----------------------------------------------------------------------------
section .bss

ui_rebootq: resb 1              ; 1 = a restart is owed (SPEC.md 20.10)

ui_ev:      resb 8              ; popped event record (EV_TYPE/A/B/C)
ui_menusel: resw 1              ; ui_dispatch scratch: AH = menu, AL = item,
                                ; held across the billing calls (SPEC 12.2)
ui_about:   resw 1              ; 1 = the selection came from the app-NAME
                                ; cell, so the callback offset is the one
                                ; wm_about_set stored and not AM_ONCMD
ui_click_t: resw 1              ; EV_C of the event being dispatched: the
                                ; click's birth tick (valid during dispatch,
                                ; which always stores before handlers read)
ui_tclkt:   resw 1              ; birth tick of the last title-bar press, and
ui_tclkw:   resw 1              ; the window it landed on (0 = none recorded):
                                ; ui_tdbl's pair, SPEC.md 11.95
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
