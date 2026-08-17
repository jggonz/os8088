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

; What a posted restart ASKED FOR (SPEC.md 20.10). Two values rather than a
; bare flag, because the two doors labelled Restart know different things: the
; System menu's is the user quitting the GUI with the Control Panel possibly
; open and unsaved, and a driver's dialog may have just told them to take the
; floppy OUT - in which case the flush is a mount of an empty drive, seconds of
; frozen UI, and a toast about a disk the user was asked to remove.
UI_RBQ_FLUSH   equ 1            ; flush the Control Panel on the way out
UI_RBQ_NOFLUSH equ 2            ; ...and do not go near a disk at all

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
%ifdef KFZTRACE
    inc word [khb_uipass]       ; ONE PASS OF THE UI TASK. The watchdog keyed
                                ; on a HELD mutex and never fired in the field,
                                ; which is itself the finding: the lock is
                                ; taken and released over and over, so the
                                ; machine is LIVELOCKED rather than halted and
                                ; no lock-held counter can see it. What does
                                ; not happen is this line.
%endif
    ; --- 0. a posted restart, spent with NOTHING held (SPEC.md 20.10) --------
    cmp byte [ui_rebootq], 0
    je .keys
    mov al, [ui_rebootq]        ; what the poster ASKED for, carried into AL:
    sub al, UI_RBQ_FLUSH        ; 0 = flush the panel first, non-0 = do not
    mov byte [ui_rebootq], 0    ; ...cleared first: ui_cmd_reboot does not
    call ui_cmd_reboot          ; return, but a path that ever did must not
                                ; find the post still standing
.keys:
    ; --- 1. keyboard: poll, and feed the front window's key handler ----------
    mov ah, 0x01                ; any key pending?
    int 0x16
    jz .events
    KFZ 1                       ; 1: a key is pending
    call blk_wake               ; a key is USER INPUT whatever it turns out to
                                ; be, so the idle clock restarts here (SPEC.md
                                ; 64.2) and CF says whether this keystroke is
                                ; the one that lit the screen
    KFZ 2                       ; 2: blk_wake returned (SPEC.md 64)
    mov ah, 0x00                ; fetch it: AL = ascii, AH = scan - the wake
    pushf                       ; case still has to take the key OUT of the
    int 0x16                    ; BIOS buffer, so only the DELIVERY differs.
    popf                        ; The verdict is banked across the call: int
                                ; 16h is a BIOS service and the flags it
                                ; returns are its own business, not ours
    KFZ 3                       ; 3: the key is out of the BIOS buffer
    jc .events                  ; the screen was dark, so the user never saw
                                ; the window this would have gone to: drop it
    call kbm_key                ; with no mouse on the machine, the arrows ARE
    jc .events                  ; the mouse (SPEC.md 9.6) and this key is not
                                ; the application's. One compare per keystroke
                                ; on a machine that has one
    KFZ 4                       ; 4: kbm_key passed it through (SPEC.md 9.6)
    call gfx_lock               ; handler draws; it runs under the lock
    KFZ 5                       ; 5: WE HAVE THE GFX LOCK. A bar that stops at
                                ;    4 is a mutex nobody released
    call wm_top                 ; BX = frontmost visible window, 0 if none
    call fdlg_top               ; ...unless a modal file dialog is up, which
                                ; takes every key (SPEC.md 38.2)
    KFZ 6                       ; 6: the target window is resolved
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
    KFZ 7                       ; 7: about to far-call the package
    call wm_pkgcall             ; contract already puts it), offset in BP
    KFZ 8                       ; 8: the handler returned
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
    KFZ 9                       ; 9: a healthy keystroke, start to finish

    ; --- 2. events: pop one, act on EVT_MDOWN ---------------------------------
.events:
    call blk_pass               ; spend the mouse ISR's activity flag, and dark
                                ; every monitor once the idle period is up
                                ; (SPEC.md 64.1). AHEAD of evq_pop and not
                                ; down in .yield: the press that woke the
                                ; screen is still in the ring here, and this
                                ; is what drains it rather than letting it be
                                ; dispatched at a window nobody could see
    mov di, ui_ev
    call evq_pop
    jc .yield                   ; queue empty
    mov ax, [ui_ev + EV_TYPE]
    cmp ax, EVT_RDOWN           ; the right button goes its own, much
    je .rdown                   ; narrower way (SPEC.md 12.4/13)
    cmp ax, EVT_MUP             ; a release SPENDS an armed chrome press
    je .mup                     ; (SPEC.md 13.5), and is otherwise ignored -
                                ; which is what this loop did with every
                                ; mouse-up before that section existed
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

    call wm_fs_vis              ; a fullscreen window (SPEC.md 11.2) sits
    jc .on_desktop              ; over the bar: route rows 0..19 to wm_hit
                                ; like any other point. The PREDICATE and not
                                ; the latch (SPEC.md 39.17.1): one covering
                                ; the second monitor covers no bar, and
                                ; testing [wm_fs] would make a bar that is
                                ; plainly on screen unclickable
    cmp dx, MBAR_H              ; in the menu bar?
    jae .on_desktop
%ifdef KERN_BIG
    cmp cx, [vid_pw]            ; ...on the display that HAS one (39.16.2).
    jae .on_desktop             ; Display 0 is at the virtual origin, so this
                                ; one compare is the whole test - and without
                                ; it a window in the second monitor's top rows
                                ; cannot be grabbed at all, because every
                                ; press there is swallowed by a menu bar that
                                ; is on another screen
%endif
    cmp cx, [vid_clk_hx]        ; the clock cell at the right end (SPEC 12.1)?
    jae .clock_cell             ; tested BEFORE menu_track: it is not a title
    call ui_loc_gate            ; ...and Locator's own items go grey or live
%ifndef KERN_SMALL
    call fm_bar_gate            ; ...as does a Disk window's Format (22.12),
                                ; whose owner is a WINDOW rather than the
                                ; desktop but whose timing problem is the
                                ; identical one. kern_big only - kern_small
                                ; has no Format item to point anywhere
%endif
    call gfx_lock               ; before it can draw them (SPEC.md 47 rule 5)
    call menu_track             ; in: CX = mousedown x; out: AX = CMD_* or 0
    call gfx_unlock
    call ui_dispatch
    jmp .yield

.clock_cell:                    ; open the Control Panel on its Date/Time
    mov byte [cp_sel], CP_ITIME ; page (SPEC.md 31.5); no lock held here, so
    call cp_open                ; cp_open is app_launch with the module load
    jnc .yield                  ; in front of it (SPEC.md 2.8.4) - and it owns
    jmp .yield                  ; the refusal, beep or toast, so there is
                                ; nothing left here to say

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
    mov bp, [si + W_ONCLICK]
    or bp, bp
    jz .yield                   ; window does not take clicks
    call gfx_lock               ; W_ONCLICK contract: CX=x, DX=y, SI=win
    call ui_ptcall              ; (CX/DX survived wm_hit/wm_top/gfx_lock)
    mov [ui_armw], si           ; ...and the RELEASE is owed to this window
    mov byte [ui_armr], 0       ; (SPEC.md 13.7): region 0, armed only because
                                ; W_ONCLICK actually ran. A press that merely
                                ; raised a window, or that this window refused
                                ; above, arms nothing
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

    ; --- the chrome boxes: the press only ARMS (SPEC.md 13.5) ----------------
    ; Both boxes arm identically, so they are one body with two names rather
    ; than two that have to be kept in step. wm_hit reports AL=2/AL=3 on the
    ; FRONT window only, so there is no "which window" question here beyond
    ; the one .mup asks again at the release.
    ;
    ; No lock is taken and nothing is drawn: the press is not the action any
    ; more, and a pressed-state overlay is deliberately not part of this
    ; (docs/MOUSEUP-PLAN.md 7 - arm-and-return never sees the pointer between
    ; the two edges, so a static invert would stay lit while the user slid
    ; off, which says the opposite of what is true).
.close_box:                     ; wm_hit only reports AL=2 on the front window
.min_box:                       ; ...and AL=3, likewise
    mov [ui_armw], bx           ; BX = the window, AL = wm_hit's region
    mov [ui_armr], al
    jmp .yield

.mup:
    ; The release. Two species, and they differ in exactly one way:
    ;
    ;   region 2/3 - the CHROME (SPEC.md 13.5). Fires only if wm_hit at THIS
    ;                point still answers the same window AND the same region.
    ;   region 0   - a PACKAGE's content release (SPEC.md 13.7). Delivered
    ;                WHEREVER it landed, this window's own, because a package
    ;                that is never told cannot un-draw a pressed state or
    ;                decline to act. The kernel guarantees delivery; the
    ;                package decides identity, having no widget layer to ask.
    ;
    ; The arm is spent either way.
    cmp word [ui_armw], 0
    je .yield                   ; nothing armed: one compare, and the release
                                ; is ignored exactly as it always was
    mov cx, [ui_ev + EV_A]      ; NO [ui_click_t] STORE HERE, and that is
    mov dx, [ui_ev + EV_B]      ; binding: SPEC.md 22/26/38's four detectors
                                ; compare PRESS birth ticks and the
                                ; double-click stays on the down edge
                                ; (SPEC.md 13.6). Stamping here would make
                                ; double-click spacing release-to-release,
                                ; silently - os88mouse.py's dblclick measures
                                ; press-to-press and would keep passing
    mov si, [ui_armw]
    mov ah, [ui_armr]
    mov word [ui_armw], 0       ; spent: every path below leaves it clear
    or ah, ah
    jz .mup_pkg                 ; region 0: no identity test at all
    call wm_hit                 ; BX = window under the release, AL = region;
                                ; AH survives it by wm_hit's own contract
    cmp bx, si
    jne .yield                  ; a different window - two windows' boxes can
                                ; share a point, so this half is not optional
    cmp al, ah
    jne .yield                  ; ...or the other box on the same title bar
    call gfx_lock
    test word [si + W_FLAGS], 2 ; the window can have gone between the two
    jz .mup_done                ; passes - a package worker reaching
                                ; inst_task_die, or a command off its own bar.
                                ; wm_hit has answered already, so this is the
                                ; belt to that braces; it costs a test, and
                                ; what it guards is a dispatch through a freed
                                ; record (the ui_dispatch guard, verbatim)
    mov bx, si
    cmp ah, 2
    jne .mup_min
    call app_close_win          ; in: BX; quits the owning instance (or
    jmp short .mup_done         ; hides an ownerless window) - SPEC.md 29
.mup_min:
    call inst_minimize          ; in: BX; flags the instance minimized and
.mup_done:                      ; hides - its dock tile inverts (SPEC.md 30)
    call gfx_unlock
    jmp .yield

.mup_pkg:                       ; SPEC.md 13.7 - the package's release
    call gfx_lock
    test word [si + W_FLAGS], 2 ; ...unless the window went away between the
    jz .mup_pdone               ; two passes, which a worker can do
    mov bp, [si + W_ONMOUSEUP]
    or bp, bp
    jz .mup_pdone               ; the package never asked for releases, which
    call ui_ptcall              ; is every shipped package today
.mup_pdone:
    call gfx_unlock
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
    call ui_arm_chk             ; ...and a chrome press whose release was
                                ; DROPPED must not stay armed (SPEC.md 13.5)
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
    je .chk_curs                ; posted for the same reason and drained in
    mov byte [desk_zdirty], 0   ; the same pass - but it costs the drive-zone
    call gfx_lock               ; grid and the windows over it, not the screen
    call desk_zones_paint
    call gfx_unlock
.chk_curs:
    call cur_shape_pass         ; which window is the pointer over, and does it
                                ; want a different picture (SPEC.md 7.2)? One
                                ; compare per pass when neither the pointer nor
                                ; the z-order moved, which is nearly always -
                                ; and it belongs HERE rather than in the mouse
                                ; ISR because the UI task is the only mutator
                                ; of wm_zord and the window rects, so wm_hit
                                ; answers from it with no lock and no race
.chk_toast:
    call toast_pass             ; put a staged toast up, or take an expired
                                ; one down (SPEC.md 59). One compare per pass
                                ; when nothing is up, and the ONE place
                                ; either edge is drawn - toast_show is
                                ; reachable from a lock-holding callback, a
                                ; worker holding nothing, and kernel code
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
    call wm_fs_vis              ; fullscreen (SPEC.md 11.2): the bar and any
    jc .idle                    ; Date/Time page are covered - time is kept,
                                ; nothing is drawn. The predicate, 39.17.1
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
; ui_ptcall - dispatch a POINT callback on a window, billed (SPEC.md 8.1/34.3)
; in:  SI = window, BP = the handler's offset in that window's segment,
;      CX = x, DX = y; the caller holds the gfx lock
; out: nothing; CX/DX preserved across the call so the caller may reuse them
; clobbers: AX, BX, DI (and whatever the handler clobbers)
;
; W_ONCLICK and W_ONMOUSEUP have the identical contract and the identical
; environment - UI task, gfx lock held, billed to the owning instance, with
; the dispatched instance stamped for sound grants (SPEC.md 34.3) - so this is
; the one body rather than two that have to be kept in step. It was factored
; out of .content_front when W_ONMOUSEUP arrived; before that there was only
; one caller and no reason.
;
; The caller checks the handler is non-zero. BP is the offset because that is
; what wm_pkgcall wants and what the record already holds.
; -----------------------------------------------------------------------------
ui_ptcall:
    mov bx, si
    call inst_win_owner         ; DI = the owning record, 0 = unowned window
    push di
    push word [snd_inst]        ; stamp the dispatched instance for sound
    call snd_disp_set           ; grants made inside the handler (SPEC 34.3)
    push cx                     ; the point - task_cycles answers in DX:AX,
    push dx                     ; so CX/DX have to step aside
    call task_cycles            ; DX:AX = start stamp
    mov bx, ax
    mov ax, dx
    pop dx                      ; y
    pop cx                      ; x
    push ax                     ; start stamp: hi, then lo
    push bx
    push bp
    call wm_pkgcall             ; SI = window, BP = the handler
    pop bp
    pop ax
    pop dx
    pop word [snd_inst]         ; the outer dispatch's stamp, back
    pop di
    or di, di
    jz .out
    call inst_charge
.out:
    ret

; -----------------------------------------------------------------------------
; ui_arm_chk - drop a chrome arm whose release will never arrive (SPEC.md 13.5)
; in:  nothing
; out: nothing (all registers preserved; flags clobbered)
;
; evq_push DROPS silently when the ring is full (16 records), and ui_drag,
; ui_grow and fm_drag each carry the same fallback in as many words - "the
; EVT_MUP was dropped (queue full) - the level still says so". Here the failure
; is worse than a stuck drag: an arm nobody spends would be fired by the NEXT
; unrelated release, on whatever window happened to be under it.
;
; THE QUEUE TEST IS NOT DECORATION, and it is the whole reason this routine can
; sit anywhere in the ladder. Without it:
;
;   - the keyboard mouse could never close a window. kbm_ui posts the EVT_MUP
;     at the end of the very pass that armed (SPEC.md 9.6.1), so a level-only
;     check running after it sees bit 0 already clear and throws the arm away
;     before the release it just queued can be popped.
;   - a real mouse would lose the release intermittently: anything queued ahead
;     of it means this runs with the button already up and the EVT_MUP still in
;     the ring.
;
; With it, the case that would depend on ordering is exactly the case the queue
; is non-empty in - so ui_arm_chk before or after kbm_ui is the same routine.
; It is placed after only because that is the order the two were written in.
;
; One compare per pass when nothing is armed, which is nearly always.
; -----------------------------------------------------------------------------
ui_arm_chk:
    cmp word [ui_armw], 0
    je .out                     ; nothing armed - the common case
    test byte [mouse_btn], 1
    jnz .out                    ; still held: the release is yet to come
    cmp word [evq_count], 0
    jne .out                    ; ...or it is queued behind something else
    mov word [ui_armw], 0       ; the button is up and nothing is pending, so
.out:                           ; the release is never going to arrive
    ret

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

    call wm_fs_vis              ; a fullscreen window (SPEC.md 11.2) owns the
    jc .hit                     ; bar's rows like any other point (39.17.1)
    cmp dx, MBAR_H
    jb .rbar                    ; the menu bar has no right-button behaviour
    jmp short .hit
.rbar:
%ifdef KERN_BIG
    cmp cx, [vid_pw]            ; ...but only where there IS a bar (39.16.2)
    jae .hit
%endif
    jmp .ret
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
%ifdef KERN_BIG
    call ui_ylow                ; SPEC.md 39.16.2: MBAR_H is the PRIMARY's
    cmp ax, si                  ; floor, and no other card has a menu bar
    jge .y_low_ok
    mov ax, si
%else
    cmp ax, MBAR_H
    jge .y_low_ok
    mov ax, MBAR_H
%endif
.y_low_ok:
    cmp [bx + W_Y], ax
    je .y_same
    inc di
.y_same:
    mov [bx + W_Y], ax

%ifdef KERN_BIG
    call ui_drag_dead           ; ...and the title bar has to land somewhere a
                                ; person can reach (SPEC.md 39.16.1)
%endif
    call wm_dock_snap           ; barely over the dock? then back above it,
    jnc .nosnap                 ; if it still fits (SPEC.md 11.90)
    inc di                      ; ...and it moved after all
.nosnap:

    or di, di                   ; a click that only raised the window (above)
    jz .done                    ; leaves the screen already correct

%ifndef NODRAGCACHE
    mov bx, [ui_dragwin]
    call ui_drag_phase          ; ...onto a multiple of 8 FIRST (SPEC.md
                                ; 11.96.13): the cache can only replay the
                                ; pixels at a dx whose low three bits are 0,
                                ; and a hand drops a window on an arbitrary
                                ; one - so seven drags in eight were refused
                                ; and redrew in full. This is where that is
                                ; paid, in up to 7px of drop precision
    mov bx, [ui_dragwin]        ; THE DRAG CACHE (SPEC.md 11.96.12): bank the
    call wm_dc_take             ; content while it is still on the glass at the
                                ; old place, so wm_draw_win below replays it at
                                ; the new one instead of ordering a W_PAINT.
                                ; HERE and not after the move: two lines down
                                ; wm_paint_dmg paints the desktop over exactly
                                ; the pixels this is saving
%endif

    mov bx, [ui_dragwin]        ; A DRAG IS ALLOWED TO CROSS THE SEAM (SPEC.md
%ifdef KERN_BIG                 ; 39.16.1) and this is what it costs when it
    call wm_strad_fit           ; does: a frame reaching rows or columns only
%endif                          ; one of the two displays has gives them back
                                ; (39.16.3). BEFORE the bank, so what is
                                ; remembered is what the record holds
    call wm_nat_bank            ; where the user put it is where it goes back to
                                ; when the adapter changes (SPEC.md 39.11.2.1)
                                ; - and BELOW ui_drag_phase, not above it. That
                                ; snap moves x by up to 7px (SPEC.md 11.96.13)
                                ; and it is the LAST thing to touch the rect, so
                                ; banking first records a rect the record does
                                ; not hold and the window shifts by that much on
                                ; the next adapter change. The two arrived from
                                ; different branches and the merge of them
                                ; assembled perfectly with the bank in the wrong
                                ; place, which is what a merge is for catching

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
    call wm_dmg_vacate          ; WHERE IT WAS is the whole of what can have
                                ; gone stale (SPEC.md 11.91.2) - recorded
                                ; BEFORE the union below grows AX..DX past it.
                                ; It is what makes dragging a window ACROSS
                                ; another one cost the other one nothing
    call wm_dmg_union           ; ...grown to cover where it is now
    call wm_paint_dmg           ; caller-held lock, per the wm contract
%ifndef NODRAGCACHE
    call wm_dc_done             ; ...and the waiver is spent (SPEC.md 11.96.12)
%endif
.done:
    call gfx_unlock             ; NO second gfx_lock here - already held (§7)
    ret

; -----------------------------------------------------------------------------
; ui_drag_phase - put a dropped window's x AND y where the drag cache can
;                 replay it (SPEC.md 11.96.13, 11.96.13.1)
; in:  BX = the window, its W_X/W_Y already clamped and snapped; ui_origx and
;      ui_origy = the old ones
; out: nothing; preserves every register
;
; 11.96.12's byte phase is not a bug and cannot be argued away: gfx_save lays
; the buffer out from x1 rounded DOWN to a framebuffer byte, so the pixels can
; be replayed at another x only where the content sits at the same offset
; inside its byte. What was wrong was leaving that to CHANCE - a mouse drops a
; window on an arbitrary pixel, so dx & 7 was 0 one time in eight and the other
; seven redrew the window card by card. Measured on 5150/Hercules, Solitaire:
; 210 calls at dx = -64 against 1,007 at dx = -60.
;
; So the drop is moved onto the phase instead, and the direction is the whole
; safety argument: it always moves TOWARD the origin, never away. The window was
; legal where it started and legal where the clamps left it, the legal x's are
; an INTERVAL, so every point between the two is legal as well - which is why
; this needs no clamp of its own and cannot fight wm_dock_snap, ui_drag_dead or
; the dual-display bounds above. Rounding to the NEAREST multiple would be
; half the error and is not worth re-deriving those bounds here.
;
; ...AND THE SAME IS DONE TO dy, FOR A DIFFERENT REASON (SPEC.md 11.96.13.1).
; There is no LAYOUT reason: rows are whole, so any dy replays correctly as far
; as gfx_save's buffer is concerned, and that is why "snap dy too" has been
; asked before and answered "no benefit". The benefit is not the layout, it is
; the DITHER. gfx_fill_gray is "(x + y) even = white, odd = black" - phased on
; ABSOLUTE screen coordinates, deliberately, because that is what lets
; wm_dmg_gray draw the desktop as fragments and get the pixels drawing it whole
; would give - and files.inc draws a Disk window's scroll-bar track with it,
; fdlg.inc the file dialog's, and the Task Manager its whole memory map with
; gfx_fill_pat, which is screen-aligned on y & 7. Replay any of that an ODD
; number of rows away and every dithered pixel comes back INVERTED; a multiple
; of 8 covers the 8x8 patterns as well, and 8 is even, so one rule serves both.
;
; The safety argument carries over word for word: the legal y's are an interval
; too (MBAR_H at the top, wm_dock_snap's ceiling at the bottom), both ends of
; the move are in it, so every point between them is. It runs AFTER
; ui_drag_dead and wm_dock_snap for exactly that reason - it is the last thing
; to touch the rect before wm_dc_take banks it.
;
; It is inside the caller's %ifndef NODRAGCACHE, and so is the body below, so a
; DRAGCACHE=0 reference build drops the window exactly where it always did AND
; carries not one byte of this - the A/B stays honest in both.
; -----------------------------------------------------------------------------
%ifndef NODRAGCACHE
ui_drag_phase:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, bx
    mov cx, [si + W_X]          ; the drop as the clamps left it, banked
    mov dx, [si + W_Y]
    mov ax, cx
    sub ax, [ui_origx]          ; AX = dx, signed
    call ui_drag_ph8
    add ax, [ui_origx]
    mov [si + W_X], ax
    mov ax, dx                  ; ...AND dy, FOR THE DITHER (SPEC.md 11.96.13.1)
    sub ax, [ui_origy]
    call ui_drag_ph8
    add ax, [ui_origy]
    mov [si + W_Y], ax
%ifdef KERN_BIG                 ; kern_small has one display and no
                                ; vid_disp_find (SPEC.md 39.12)
    cmp byte [vid_ndisp], 1     ; ONE DISPLAY: the interval argument above is
    jbe .out                    ; the whole proof and nothing more is asked.
    mov ax, [si + W_X]          ; TWO: the legal top-lefts are a UNION of two
    mov bx, [si + W_Y]          ; rectangles and not an interval - a CGA over
    call vid_disp_find          ; a taller Hercules has rows 200..206 on the
    jnc .out                    ; second display only, and a window dropped
    mov [si + W_X], cx          ; on the CGA at y 193..199 with its origin on
    mov [si + W_Y], dx          ; the Hercules phased UP toward it into rows no
                                ; display has, title bar and all. ui_drag_dead
                                ; ran before this and cannot see it. So the
                                ; phased point is asked the same question, and
                                ; a NO undoes the phase: the drop stands where
                                ; the clamps left it and replays by cards, the
                                ; one-time-in-eight cost this exists to avoid,
                                ; paid only on the seam
%endif
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ui_drag_ph8 - AX = a signed delta -> AX truncated TOWARD ZERO to a multiple
;               of 8; clobbers nothing else (flags)
;
; The sign is why this is done on the magnitude rather than in place:
; `and ax, 0xFFF8` rounds toward MINUS INFINITY, so on a leftward or upward
; drag it would move the window further from the origin - out of the interval
; the safety argument above rests on.
ui_drag_ph8:
    or ax, ax
    jns .pos
    neg ax
    and ax, 0xFFF8
    neg ax
    ret
.pos:
    and ax, 0xFFF8
    ret
%endif

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
                                ; the overlay is always erased before
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
%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; ui_drag_dead - pull a dragged window's origin out of the DEAD ZONE
;                (SPEC.md 39.16.1)
; in:       BX = the window, W_X/W_Y just written by the clamps above
; out:      an origin inside some display
; clobbers: flags
;
; The two clamps above are per-AXIS, and the virtual desktop is a union
; (SPEC.md 39.2.1) - so between two displays of different heights they can
; agree on a point no display covers, and a title bar dropped there is one
; nobody can grab back. It is mou_clamp's problem one layer up, and it takes
; the same two routines.
;
; INTO THE POINTER'S DISPLAY, because during a drag the title bar is under the
; hand and [cur_disp] is where the hand is - and 39.15.4 guarantees the
; pointer itself is inside a display, so there is no second dead zone to fall
; into. Clamping into the display the window CAME from would be the smaller
; move, and the record no longer holds that origin by the time this runs.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; ui_ylow - the lowest y a title bar may take, where this window is
;           (SPEC.md 39.16.2)
;
; in:       BX = the window; [ui_curx] = the x the drag is proposing
; out:      SI = the floor
; clobbers: SI, flags
;
; MBAR_H on display 0 and the display's own vy anywhere else - because the
; menu bar is the primary's and nothing else has one. Asked with the drag's
; PROPOSED x rather than the window's current one: the clamp runs before
; W_X is stored on some passes, and a window being dragged onto the second
; monitor must be floored by where it is GOING.
; -----------------------------------------------------------------------------
ui_ylow:
    mov si, MBAR_H
    cmp byte [vid_ndisp], 1
    jbe .out
    push ax
    push bx
    push di
    mov ax, [ui_curx]
    mov bx, MBAR_H              ; any row the primary's band contains, so a
                                ; point on display 0 resolves to it
    call vid_disp_of
    or al, al
    jz .done                    ; display 0 keeps the chrome's floor
    call vid_ctx_ptr
    mov si, [di+VID_CTX_VY]
.done:
    pop di
    pop bx
    pop ax
.out:
    ret

ui_drag_dead:
    cmp byte [vid_ndisp], 1
    jbe .out
    push ax
    push bx
    push cx
    push si
    mov si, bx                  ; SI, because vid_disp_find wants BX = y and
    mov ax, [si+W_X]            ; an 8086 addresses through BX/BP/SI/DI only
    mov bx, [si+W_Y]
    call vid_disp_find
    jnc .done                   ; on a display already: nothing to correct
    mov cl, [cur_disp]
    call vid_pt_clamp
    mov [si+W_X], ax
    mov [si+W_Y], bx
.done:
    pop si
    pop cx
    pop bx
    pop ax
.out:
    ret
%endif

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

    ; Where it WAS is the whole of what a resize can have uncovered (SPEC.md
    ; 11.91.2), and the origin is the one banked at mousedown - fixed for the
    ; whole grow, even though wm_dock_snap may move it below. A window that
    ; only GREW uncovers nothing at all, so its neighbours cost nothing.
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, bx
    mov ax, [ui_curx]
    mov cx, ax
    add cx, [ui_origx]          ; x2 = x + w: the drop shadow's own column
    mov bx, [ui_cury]
    mov dx, bx
    add dx, [ui_origy]
    call wm_dmg_vacate
    pop si
    pop dx
    pop cx
    pop bx
    pop ax

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
%ifdef KERN_BIG
    call wm_strad_fit           ; a grow can push a frame across the seam too
%endif                          ; (SPEC.md 39.16.3)
    call wm_snap_win            ; ...and the ALIGNED x is a question about the
                                ; WIDTH now (SPEC.md 11.95.2): a window shrunk
                                ; off the screen's right edge stops being flush,
                                ; so column 0 stops being an aligned content
                                ; origin and 7 is the leftmost one again. Last,
                                ; after both fits, for wm_fit's reason - the
                                ; snap moves left and only stays legal once the
                                ; right edge has been dealt with
    call wm_nat_bank            ; the size the user chose, banked AFTER the snap
                                ; because the snap may have moved the origin,
                                ; and after the straddle fit for its reason
                                ; (SPEC.md 39.11.2.1)

    ; Only the rect it had and the rect it has changed, so the repaint is
    ; their union: wm_paint_dmg's case, not wm_paint_all's (SPEC.md 11.91).
    ; Dragging a corner used to repaint every window on the screen.
    mov dx, [bx + W_Y]
    add dx, [bx + W_H]          ; the new last row...
    cmp dx, ax
    jae .uh
    mov dx, ax                  ; ...or the old one, whichever is lower
.uh:
    ; x is the union of BOTH rects, and it did not used to have to be: a width
    ; change never moved this origin until wm_snap_win above started answering
    ; a different x once the window stops spanning the screen (SPEC.md
    ; 11.95.2). Taking the new rect alone leaves the seven columns the window
    ; VACATED standing on the desktop. With the origin unmoved this is the
    ; larger of the two widths off the one x, exactly as before.
    mov cx, [ui_curx]           ; old x2 = old x + old w
    add cx, [ui_origx]
    mov ax, [bx + W_X]          ; ...against new x2 = x + w, the drop shadow's
    add ax, [bx + W_W]          ; own column
    cmp ax, cx
    jbe .uw
    mov cx, ax
.uw:
    mov ax, [bx + W_X]          ; x1 = the leftmost of the two
    cmp ax, [ui_curx]
    jbe .ux
    mov ax, [ui_curx]
.ux:
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
    mov [menu_items_file], ax   ; Close Window is item 0 now: Locator's File
                                ; menu is that item and nothing else, the
                                ; builtins having moved to Builtins (12.3.1)
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
; System menu, then Locator's File and Builtins (SPEC.md 12.3.1)
ui_loc_base:
    dw CMD_ABOUT, CMD_CLOSE, CMD_TIMER

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
    call cp_open                ; NOT .launch: the panel's code is a file now
    ret                         ; (SPEC.md 2.8.4), so opening it can fail for
                                ; a reason app_launch knows nothing about
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
    xor al, al                  ; the System menu's Restart FLUSHES: the user
                                ; is quitting the GUI from the bar, and the
                                ; panel they may have left open is a window
                                ; they can see
; ...and its OTHER entry point, ui_reboot_post's deferred one (SPEC.md 20.10).
; A label rather than a routine: the body below never returns, so there is
; nothing for two callers to disagree about.
;
; in: AL = 0 flush the Control Panel first, non-0 skip it (SPEC.md 20.10).
; The flag is the CALLER'S statement about the disk, not a preference: a
; driver's dialog that has just asked the user to remove the floppy knows the
; write cannot land, and the kernel has no way to work that out for itself.
ui_cmd_reboot:
    or al, al
    jnz .nosave                 ; ...and then nothing on this path touches a
                                ; disk at all: drv_shutdown's detaches are
                                ; memory and ports (hd_cfg_mark stages its
                                ; blob and writes nothing - SPEC.md 51.9)
    call cp_flush_close         ; the OTHER way the panel stops existing
                                ; (SPEC.md 31.8). Before the lock, because a
                                ; disk write is not something to do with the
                                ; GUI already torn down - and before vid_text,
                                ; so a failure still has a screen to have
                                ; happened on
.nosave:
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
    call vid_reboot             ; a text mode on the primary AND on whichever
                                ; card int 10h is going to print on after the
                                ; int 19h below (SPEC.md 39.20.1)
    call sched_unhook           ; restores int 08h/0Ch (unhooks the mouse too)
    int 0x19
    jmp ui_cmd_reboot           ; unreachable; int 19h does not return

; -----------------------------------------------------------------------------
; ui_reboot_post - API slot 0x0368 (SPEC.md 20.10). AL in, no answer.
;
; AL = 0 restart the way the System menu does, flushing the Control Panel's
; settings on the way out; AL non-0 restart WITHOUT going near a disk. The
; second is for a caller that has just asked the user to take the floppy out -
; the hard disk installer's Restart (SPEC.md 52.10.6) is the case it exists
; for, and there the flush is a mount of an empty drive, seconds of frozen UI
; under a stopped cursor, and a toast telling the user to put back the disk
; they were told to remove.
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
;
; Two posts that DISAGREE about the flush settle towards not touching a disk,
; and that is not "the safe default" wearing a disguise: a caller asking to
; skip is reporting a FACT nobody else has - the drive is empty - while a
; plain post is merely not saying anything about it. Unreachable today, there
; being one caller; written down because the rule has to be somebody's.
; -----------------------------------------------------------------------------
ui_reboot_post:
    pushf                       ; ...AND THE FLAGS, which this cell used to
                                ; preserve by ACCIDENT: it was two mov's, and a
                                ; mov touches none. The tests below do. A
                                ; package's entry proc hands CF back to the
                                ; loader, so a stray one aborts a launch -
                                ; wm_snap's own bug (SPEC.md 11.94), and the
                                ; reason a slot's contract is never widened
                                ; quietly
    cmp byte [ui_rebootq], UI_RBQ_NOFLUSH
    je .out
    mov byte [ui_rebootq], UI_RBQ_FLUSH
    or al, al
    jz .out
    mov byte [ui_rebootq], UI_RBQ_NOFLUSH
.out:
    popf
    ret

; -----------------------------------------------------------------------------
section .bss

ui_rebootq: resb 1              ; 0 = nothing owed, else UI_RBQ_* - a restart
                                ; is owed AND whether it may touch a disk on
                                ; the way out (SPEC.md 20.10)

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
ui_armw:    resw 1              ; SPEC.md 13.5: the window a chrome press is
                                ; ARMED on, 0 = nothing armed. A window pointer
                                ; is never 0, so 0 is the sentinel and .bss
                                ; arrives zeroed (kernel.asm) - ui_tclkw's
                                ; convention exactly
ui_armr:    resb 1              ; ...and which box: wm_hit's AL, 2 = close,
                                ; 3 = minimize. Both halves are compared at the
                                ; release, because the two boxes share a title
                                ; bar and two windows' boxes can share a point
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
