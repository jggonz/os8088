; =============================================================================
; os8088 - apps/recorder/recorder.asm
;
; Recorder, the fourth software package (SPEC.md 35): a sound wave recorder
; and player over the SPEC.md 34 sound layer, all of it through the API
; table (os88api.inc) - grants, staging, streams and the PCM_EXCL clip.
; One window: a waveform strip, two status lines and four buttons
; (REC / STOP / PLAY / DEMO), plus a "Sound" menu in the bar (SPEC.md 12.2)
; carrying those same four transitions - Record / Stop / Play / Demo, each
; item calling the very routine its button calls, guards and all, so a
; command picked in the wrong state is refused with the same status line a
; click on the grayed button would have written.
;
; The Sound Blaster is a loadable driver (SPEC.md 51.4), and two things
; follow from that which are worth
; knowing. Every callback is a NEAR proc with a near `ret` - the kernel
; reaches us through our own header dispatcher, not a far call - so there is
; no `retf` anywhere in this file and a stray one would return into the
; loader's frame and hang the machine at the first paint. And the caps this
; app branches on are RUNTIME state rather than boot state: with no driver
; loaded OSAPI_SND_CAPS answers TONE|PCM_EXCL, both card slots refuse CF=1,
; and the app degrades to exactly what it already does on a speaker-only
; machine. "Query the caps, do not assume" is not politeness
; here - it is the only thing that works.
;
;   - REC needs SND_CAP_PCM_IN (a Sound Blaster): verb 7 grants a 40,000 B
;     capture buffer (5 s at 8 kHz), verb 4 opens the input stream, and the
;     kernel drain task fills the grant. Without the cap the button is drawn
;     grayed AND a click on it only explains in the status line (the
;     three-layer refusal idiom, SPEC.md 31.3) - the app stays fully usable
;     as a player on speaker-only machines.
;   - Progress is POLLED (there are no sound events, SPEC.md 34.3): every
;     paint and click runs rc_poll, which reads STREAM verb 3 and retires
;     finished/watchdog-stopped streams. On QEMU no input IRQ ever arrives,
;     so a recording always lands on the watchdog path ("ended") - shown
;     honestly as REC STOPPED (WATCHDOG).
;   - PLAY prefers PCM_BG (verb 0 on the staged grant - background, the GUI
;     keeps running) and falls back to OSAPI_SND_PLAY chunks read back via
;     verb 5 (PCM_EXCL: blocking, click-aborts) - the status line says
;     which device played.
;   - DEMO stages a built-in 1 s sine sweep (400 Hz -> 800 Hz -> 400 Hz
;     at 8 kHz) into the grant as if it had been recorded, so the real
;     playback paths are exercisable on any machine.
;   - The waveform strip draws the grant's samples read back one per column
;     through verb 5, decimated to the strip width.
;
; Close-box teardown needs nothing from us: snd_release_inst (SPEC.md 34.3)
; force-closes any live stream and frees the grant. Window procs run with
; the gfx lock held (SPEC.md 11) and preserve all registers.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'RECORDER', rc_entry, 1

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A microphone: rounded capsule, holder arc, stem and base. The mask is the
; silhouette dilated 1px (8-connected), mines-icon style.
;
;   data                          mask
;   .......##.......              ......####......
;   ......####......              .....######.....
;   ......####......              .....######.....
;   ......####......              ...##########...
;   ....#.####.#....              ...##########...
;   ....#.####.#....              ...##########...
;   ....#.####.#....              ...##########...
;   ....#..##..#....              ...##########...
;   ....#......#....              ...##########...
;   .....######.....              ...##########...
;   .......##.......              ....########....
;   .......##.......              ......####......
;   .......##.......              ....########....
;   .....######.....              ...##########...
;   ....########....              ...##########...
;   ................              ...##########...
    OS88_ICON16
    dw 0x07E0                       ; 16 mask rows (white underlay)
    dw 0x07E0
    dw 0x07E0
    dw 0x1FF8
    dw 0x1FF8
    dw 0x1FF8
    dw 0x1FF8
    dw 0x1FF8
    dw 0x1FF8
    dw 0x1FF8
    dw 0x0FF0
    dw 0x03C0
    dw 0x0FF0
    dw 0x1FF8
    dw 0x1FF8
    dw 0x1FF8
    dw 0x0180                       ; 16 data rows (black pixels)
    dw 0x03C0
    dw 0x03C0
    dw 0x03C0
    dw 0x0BD0
    dw 0x0BD0
    dw 0x0BD0
    dw 0x0990
    dw 0x0810
    dw 0x07E0
    dw 0x0180
    dw 0x0180
    dw 0x0180
    dw 0x07E0
    dw 0x0FF0
    dw 0x0000
    OS88_ICON16_END

; --- geometry (content-relative; the window is 220x140 -> content 218x121) -----
RC_CONT_W   equ 218
RC_CONT_H   equ 121
RC_WAVE_X1  equ 4                   ; waveform strip frame
RC_WAVE_Y1  equ 2
RC_WAVE_X2  equ 213
RC_WAVE_Y2  equ 69
RC_WAVE_W   equ 208                 ; inner columns (x 5..212)
RC_WAVE_MID equ 36                  ; centerline row (inner rows 3..68)
RC_ST1_Y    equ 76                  ; status line 1 (state + device)
RC_ST2_Y    equ 88                  ; status line 2 (bytes + input device)
RC_BTN_Y    equ 100                 ; button row
RC_BTN_H    equ 16
RC_BTN_W    equ 52                  ; buttons at x 4 / 58 / 112 / 166

; --- capture / playback parameters ---------------------------------------------
; The grant is asked for in TIERS, not taken as a constant (rc_grant). A
; pinned staging segment would always have 40,000 B in it;
; here the pool is whatever the loaded driver claimed off the heap - the
; Sound Blaster's is SBL_WANT minus its DMA-visible head, 20,480 B today -
; and a driver written tomorrow may have a different one. So this asks for 5
; seconds, then 3, then 2, then 1, and records into whatever it got. The
; floor is RC_D_LEN on purpose: DEMO stages exactly that much, so the
; built-in sweep fits every tier and the app is never useless.
RC_TIERS    equ 4
RC_CAP_MAX  equ 40000               ; tier 0: 5 s at 8 kHz
RC_RATE     equ 8000                ; one rate everywhere (PWM N=149, in range)
RC_CHUNK    equ 1000                ; demo synth/stage chunk (divides RC_D_LEN/2,
                                    ; so the sweep turns on a chunk boundary)
RC_PBUF_SZ  equ 4000                ; read-back buffer: 0.5 s PCM_EXCL chunks
RC_D_LEN    equ 8000                ; demo take: 1 s at RC_RATE
RC_INC400   equ 3277                ; phase increment 400*65536/8000 (400 Hz)
RC_INC800   equ 6554                ; 800*65536/8000 (800 Hz)

; --- app states ([rc_st]) -------------------------------------------------------
RC_IDLE     equ 0                   ; no stream open (rc_len may hold data)
RC_REC      equ 1                   ; input stream open
RC_PLAY     equ 2                   ; PCM_BG output stream open

; --- Sound menu item indices (must match rc_i_sound's order) --------------------
RC_CMD_REC  equ 0
RC_CMD_STOP equ 1
RC_CMD_PLAY equ 2
RC_CMD_DEMO equ 3

; -----------------------------------------------------------------------------
; rc_entry - package entry point (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; Queries the sound caps once (they are static after boot) and stamps the
; status template's input-device field; the loader has zeroed our bss.
; The menu set is registered here, before the loader shows the window, so
; the first bar drawn already says "Recorder" and carries Sound (SPEC.md
; 12.2). A failed wm_create leaves BX meaningless, so the registration sits
; behind the same CF the entry propagates.
; -----------------------------------------------------------------------------
rc_entry:
    push ax
    push dx
    push si
    call OSAPI_SND_CAPS             ; AX = merged caps word (BL/DX clobbered
    mov [rc_caps], ax               ; per the contract - both dead here)
    test al, SND_CAP_PCM_IN
    jz .noin
    mov word [rc_s_line2+18], 'SB'  ; the IN:-- field becomes IN:SB
.noin:
    mov word [rc_msg], rc_s_idle
    mov si, rc_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc .out
    push ax                         ; SPEC.md 13.7: the buttons fire on the
    mov ax, rc_onup                 ; RELEASE, over the button the press
    call OSAPI_WM_ONMOUSEUP         ; landed on - so a mis-aimed press can be
    mov ax, rc_ondrag               ; slid off and cancelled - and 13.8.1's
    call OSAPI_WM_ONDRAG            ; tracking edge, so it comes back UP while
    pop ax                          ; the pointer is off it rather than at the
                                    ; release. Not template words; set after
                                    ; wm_create, like MENU_SET
    mov si, rc_about                ; 'About Recorder' above the Close the
    call OSAPI_ABOUT_SET            ; kernel already puts in our pull-down
                                    ; (SPEC.md 12.2) - flags preserved, so the
                                    ; CF the branch above cleared still stands
    mov si, rc_menus                ; BX = the window wm_create just returned
    call OSAPI_MENU_SET             ; (draws nothing, takes no lock, and
                                    ; preserves the flags as well as the
                                    ; registers - SPEC.md 20.3 - so the CF
                                    ; the branch above cleared still stands)
.out:                               ; the pops leave CF alone, so a wm_create
    pop si                          ; failure still propagates unchanged
    pop dx
    pop ax
    ret                             ; the loader reaches us through our own
                                    ; dispatcher, so this is a NEAR ret

; -----------------------------------------------------------------------------
; rc_paint - W_PAINT: poll the stream, then draw the full content
; in:  SI = window ptr; caller holds the gfx lock; content already white
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
rc_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [rc_ox], ax
    mov [rc_oy], dx
    call rc_poll
    call rc_draw_all
    cmp byte [rc_abon], 0           ; ...and the About card LAST, over the
    je .out                         ; face it is opaque about (SPEC.md 20.5.1)
    push si
    mov si, rc_ablines
    call os88ui_about_d             ; _d: this paint's region is already armed
    pop si
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                             ; near: dispatched (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; rc_onclick - W_ONCLICK: poll, hit-test the buttons, act, repaint content
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
rc_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call rc_abdismiss               ; the credits are up: this click is spent
    jc .out                         ; taking them down
    mov bx, si
    push cx
    push dx
    call OSAPI_WM_CONTENT
    mov [rc_ox], ax
    mov [rc_oy], dx
    pop dx
    pop cx
    call rc_layout                  ; the rects follow the window; CX/DX are
    mov ax, 4                       ; already SCREEN coords, which is what
    mov bx, rc_rects                ; os88ui_bfind wants
    call os88ui_bfind               ; AX = button+1, 0 = none
    call os88ui_arm                 ; ...and that is all a press ACTS on
    mov [rc_down], ax               ; (SPEC.md 13.7): no action, nothing to
    or ax, ax                       ; undo if the user slides off. What it now
    jz .out                         ; DRAWS is the pressed state (SPEC.md 13.8)
    call rc_draw_btns               ; ...through the ONE painter, which is what
.out:                               ; keeps the enable rules in a single place -
                                    ; four buttons rather than one, and a press
                                    ; is a human-rate event
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                             ; near: dispatched (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; rc_ondrag - W_ONDRAG (SPEC.md 13.8.1): the pointer moved, press still down
; in:  CX = x, DX = y (SCREEN), SI = the window; gfx lock held
; out: nothing (all registers preserved)
;
; The same question rc_onup asks at the release, one pass early, so what is
; drawn pressed is exactly what would fire. It redraws only on a CHANGE, so a
; pointer moving inside the button it pressed costs one compare and no pixels.
; -----------------------------------------------------------------------------
rc_ondrag:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call os88ui_armed               ; PEEK: the arm is the release's to spend
    or ax, ax
    jz .out
    mov di, ax
    mov bx, si
    call OSAPI_WM_CONTENT           ; the window may have moved since the press
    mov [rc_ox], ax
    mov [rc_oy], dx
    call rc_layout
    mov ax, 4
    mov bx, rc_rects
    call os88ui_bfind               ; AX = the button under the pointer now
    cmp ax, di
    je .same
    xor ax, ax                      ; off it: nothing is down
.same:
    cmp ax, [rc_down]
    je .out                         ; unchanged: no pixels
    mov [rc_down], ax
    call rc_draw_btns
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rc_onup - W_ONMOUSEUP (SPEC.md 13.7): the button fires HERE
; in:  CX = x, DX = y (SCREEN), SI = window; UI task, gfx lock held
; out: nothing
;
; The action happens on the release, and only if the release is over the
; button the press armed - so a press on Stop that the user thinks better of
; is cancelled by sliding off it, which is what every control in this OS's
; Macintosh model does and what a hand on a 1200-baud mouse needs.
;
; CX/DX may be OUTSIDE the window entirely; that is the contract, and it is
; why os88ui_bfind simply answers 0 there and the compare below fails.
; -----------------------------------------------------------------------------
rc_onup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT           ; the window may have moved since the press
    mov [rc_ox], ax
    mov [rc_oy], dx
    call rc_layout
    call os88ui_fire                ; AX = what the press armed, and it is
    or ax, ax                       ; cleared - one release per press, so
    jz .out                         ; there is no stale arm to guard against
    mov di, ax
    cmp word [rc_down], 0           ; ...and the pressed button comes back UP
    je .find                        ; first and unconditionally, whatever the
    mov word [rc_down], 0           ; release turns out to mean. Every path
    call rc_draw_btns               ; below either acts (and repaints) or
.find:                              ; cancels (and must leave it upright)
    mov ax, 4
    mov bx, rc_rects
    call os88ui_bfind               ; AX = the button under the RELEASE
    cmp ax, di
    jne .out                        ; a different button, or none: cancelled
    call rc_poll                    ; retire a finished stream first, so the
                                    ; button logic sees the current state
    dec ax
    jnz .n1
    call rc_do_rec
    jmp short .repaint
.n1:
    dec ax
    jnz .n2
    call rc_do_stop
    jmp short .repaint
.n2:
    dec ax
    jnz .n3
    call rc_do_play
    jmp short .repaint
.n3:
    call rc_do_demo
.repaint:
    call rc_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                             ; near: dispatched (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; rc_oncmd - the Sound menu's command handler (SPEC.md 12.2)
; in:  AL = item index, AH = menu index (0 - Sound is our only menu),
;      SI = the owning window, BX = the menu set. UI task, gfx lock HELD.
; out: nothing; clobbers AX, BX, CX, DX, SI, DI (a window callback may)
;
; The menu is a second set of buttons, so this is rc_onclick with the
; hit-test replaced by the item index: same content-origin stash, same
; rc_poll first (a finished stream must be retired before the rc_do_*
; guards read [rc_st], or a menu pick would see a state one poll stale),
; the same rc_do_* routines with their own refusals, and the same repaint.
; Nothing is guarded here that the button path does not guard there:
; picking Record with no Sound Blaster, or Play with an open stream, lands
; in exactly the branch the grayed button's click lands in. AH is ignored
; on purpose - the kernel only ever routes cell 1..AM_COUNT here, and we
; declare one menu.
; -----------------------------------------------------------------------------
rc_oncmd:
    call rc_abdismiss               ; a menu pick takes the credits down first,
    mov bx, si                      ; and then does what it says
    push ax                         ; the selection: wm_content answers in AX
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [rc_ox], ax
    mov [rc_oy], dx
    call rc_poll                    ; retire a finished stream first, so the
    pop ax                          ; rc_do_* guards below see the live state
    cmp al, RC_CMD_REC
    jne .n1
    call rc_do_rec
    jmp .repaint
.n1:
    cmp al, RC_CMD_STOP
    jne .n2
    call rc_do_stop
    jmp .repaint
.n2:
    cmp al, RC_CMD_PLAY
    jne .n3
    call rc_do_play
    jmp .repaint
.n3:
    cmp al, RC_CMD_DEMO             ; a fifth index cannot happen - the pull-
    jne .repaint                    ; down only offers the AMENU_NITEM items
    call rc_do_demo                 ; we declared - but the poll above may
                                    ; have moved the state, so repaint anyway
.repaint:
    call rc_repaint
    ret                             ; near: dispatched (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; rc_repaint - white-fill our own content and draw it again
; in:  [rc_ox]/[rc_oy] set for this call; caller holds the gfx lock
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; The files.inc idiom: a callback that changed what the window shows repaints
; it itself, because the kernel repaints after neither W_ONCLICK nor a menu
; command (SPEC.md 12.2) and the lock is already held, so this is a plain
; redraw and not a wm_invalidate. Shared by the button path and the menu
; path - the two ways of asking for the same four transitions have to leave
; the same pixels behind. Unlike rc_paint's, this content is NOT already
; white: the previous state's waveform and labels are still under us.
; -----------------------------------------------------------------------------
rc_repaint:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [rc_ox]
    mov bx, [rc_oy]
    mov cx, ax
    add cx, RC_CONT_W-1
    mov dx, bx
    add dx, RC_CONT_H-1
    call OSAPI_GFX_FILL
    call rc_draw_all
    ret

; -----------------------------------------------------------------------------
; rc_poll - read STREAM verb 3 and retire state changes (SPEC.md 34.3: the
; poll IS the notification mechanism; the kernel's own task does the work)
; in:  nothing ([rc_st]/[rc_hand] live)
; out: nothing; [rc_st]/[rc_len]/[rc_msg] updated; preserves all registers
; (rc_onclick hit-tests CX/DX AFTER polling - the verb-3 DX must not leak)
; -----------------------------------------------------------------------------
rc_poll:
    push ax
    push dx
    cmp byte [rc_st], RC_IDLE
    je .out
    mov ah, [rc_hand]
    mov al, 3                       ; verb 3: AX = state, DX = consumed
    call OSAPI_SND_STREAM           ; (input: bytes captured into the grant)
    cmp byte [rc_st], RC_REC
    je .rec

    ; --- playing (PCM_BG) -----------------------------------------------------
    or ax, ax                       ; 0 = still playing
    jz .out
    cmp ax, SND_ST_ENDED            ; watchdog stop
    je .pwdog
    cmp ax, SND_ST_UNDER            ; state 1 is BOTH pauses (SPEC.md 20.3):
    jne .pstale
    cmp dx, [rc_len]                ; consumed < length = refill starve -
    jb .out                         ; the kernel auto-resumes (SPEC.md 34.5)
.pdone:                             ; consumed == length: data exhausted -
    call rc_close                   ; the owner's cue to close
    mov word [rc_msg], rc_s_pdonesb
    jmp .out
.pwdog:
    call rc_close
    mov word [rc_msg], rc_s_pwdog
    jmp .out
.pstale:
    mov byte [rc_st], RC_IDLE       ; stale: torn down under us
    mov word [rc_msg], rc_s_stopped
    jmp .out

    ; --- recording (PCM_IN) ---------------------------------------------------
.rec:
    or ax, ax                       ; 0 = still recording
    jnz .r1
.rlive:
    mov [rc_len], dx                ; live captured count
    jmp .out
.r1:
    cmp ax, SND_ST_ENDED            ; watchdog: input IRQs stopped arriving
    je .rwdog                       ; (on QEMU they never start - SPEC.md 35)
    cmp ax, SND_ST_UNDER            ; state 1 is BOTH pauses (SPEC.md 20.3):
    jne .pstale                     ; (not 1: stale, torn down under us)
    cmp dx, [rc_cap]                ; consumed < capacity = drain starve -
    jb .rlive                       ; track the count, kernel resumes (34.6)
    mov [rc_len], dx                ; consumed == capacity: full - close
    call rc_close
    mov word [rc_msg], rc_s_rfull
    jmp .out
.rwdog:
    mov [rc_len], dx
    call rc_close
    mov word [rc_msg], rc_s_rwdog
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rc_close - close the open stream and go idle (the grant is kept: captured
; bytes stay readable after the stream closes, SPEC.md 20.3 verb 5)
; in:  [rc_hand] live
; out: nothing; [rc_st] = RC_IDLE; clobbers AX
; -----------------------------------------------------------------------------
rc_close:
    mov ah, [rc_hand]
    mov al, 2                       ; verb 2: close
    call OSAPI_SND_STREAM
    mov byte [rc_st], RC_IDLE
    ret

; -----------------------------------------------------------------------------
; rc_grant - ensure the capture/staging grant exists, as big as we can get
; in:  nothing
; out: CF=0 with [rc_goff]/[rc_cap]/[rc_have] set, CF=1 refused ([rc_msg]
;      explains); clobbers AX, CX, SI
;
; Walks rc_caps down (see RC_TIERS above) and keeps the first size the driver
; will part with, so a small pool costs seconds of recording rather than the
; whole app. Err 7 is "bigger than the pool" and err 8 is "the pool is
; fragmented"; both are just "try smaller" here, and running out of tiers is
; the only refusal - which on a machine with no driver at all is what the
; very first call gets, because the slot itself answers CF=1.
; -----------------------------------------------------------------------------
rc_grant:
    cmp byte [rc_have], 0
    jne .ok
    push bx
    mov bx, rc_captab
.try:
    mov cx, [bx]
    mov ax, 0x0007                  ; verb 7 grant, sub-op 0: alloc CX bytes
    call OSAPI_SND_STREAM           ; out AX = 0 + SI = grant offset
    or ax, ax
    jz .got
    cmp ax, 4                       ; 4 = no streaming sink at all: no tier
    je .nodrv                       ; is going to help, and 'NO SOUND MEMORY'
                                    ; would send the user looking for RAM
    add bx, 2                       ; anything else: the next tier down
    cmp bx, rc_captab + RC_TIERS*2
    jb .try
.nodrv:
    pop bx
    cmp ax, 4
    je .nosink
    jmp short .fail
.got:
    mov [rc_goff], si
    mov cx, [bx]                    ; the size we actually got is the capacity
    mov [rc_cap], cx
    mov byte [rc_have], 1
    mov ax, cx                      ; ...and line 2 says so, because it is not
    mov bx, rc_s_line2+6            ; the same number on every machine now
    call rc_putu5
    pop bx
.ok:
    clc
    ret
.fail:
    mov word [rc_msg], rc_s_nomem
    stc
    ret
.nosink:
    mov word [rc_msg], rc_s_nodrv
    stc
    ret

; the tiers, largest first (RC_TIERS entries). The last MUST be >= RC_D_LEN.
rc_captab:  dw RC_CAP_MAX, 24000, 16000, RC_D_LEN

; -----------------------------------------------------------------------------
; rc_do_rec - the REC button: verb 4 open-in into the grant
; in:  nothing
; out: nothing; clobbers AX, CX, DX, SI
; The three-layer refusal (SPEC.md 32 idiom): no SND_CAP_PCM_IN means the
; button was drawn gray AND this click only writes the explanation.
; -----------------------------------------------------------------------------
rc_do_rec:
    cmp byte [rc_st], RC_IDLE
    jne .busy
    test byte [rc_caps], SND_CAP_PCM_IN
    jz .noin
    call rc_grant
    jc .out
    mov ax, 0x0004                  ; verb 4 open-in: capture CX bytes into
    mov si, [rc_goff]               ; the grant at SI (SPEC.md 20.3)
    mov cx, [rc_cap]
    mov dx, RC_RATE
    call OSAPI_SND_STREAM           ; out AL = err, AH = handle
    or al, al
    jnz .err
    mov [rc_hand], ah
    mov byte [rc_st], RC_REC
    mov word [rc_len], 0            ; the old take is gone
    mov word [rc_msg], rc_s_rec
.out:
    ret
.busy:
    mov word [rc_msg], rc_s_busy
    ret
.noin:
    mov word [rc_msg], rc_s_noin
    ret
.err:
    mov ah, 0
    jmp rc_errmsg

; -----------------------------------------------------------------------------
; rc_do_stop - the STOP button: close whatever is open
; in:  nothing
; out: nothing; clobbers AX, DX
; -----------------------------------------------------------------------------
rc_do_stop:
    cmp byte [rc_st], RC_REC
    je .rec
    cmp byte [rc_st], RC_PLAY
    je .play
    mov word [rc_msg], rc_s_notrun
    ret
.rec:
    mov ah, [rc_hand]
    mov al, 3                       ; last status first: DX = bytes captured
    call OSAPI_SND_STREAM
    cmp ax, 0FFFFh
    je .recdone                     ; stale: keep whatever count we had
    mov [rc_len], dx
.recdone:
    call rc_close
    mov word [rc_msg], rc_s_stopped
    ret
.play:
    call rc_close
    mov word [rc_msg], rc_s_pstop
    ret

; -----------------------------------------------------------------------------
; rc_do_play - the PLAY button: PCM_BG stream when the caps allow it, else
; PCM_EXCL speaker chunks (blocking, click-aborts) - SPEC.md 34's honest
; degradation, chosen by the caller
; in:  nothing
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
rc_do_play:
    cmp byte [rc_st], RC_IDLE
    jne .busy
    cmp word [rc_len], 0
    je .nodata
    test byte [rc_caps], SND_CAP_PCM_BG
    jz .spk

    ; --- SB background stream: the grant is already fully staged ---------------
    mov ax, 0x0000                  ; verb 0 open-out
    mov si, [rc_goff]
    mov cx, [rc_len]
    mov dx, RC_RATE
    call OSAPI_SND_STREAM           ; out AL = err, AH = handle
    or al, al
    jnz .err
    mov [rc_hand], ah
    mov byte [rc_st], RC_PLAY
    mov word [rc_msg], rc_s_psb
    ret

    ; --- speaker fallback: verb-5 read-back + PCM_EXCL chunks ------------------
    ; Blocks inside this callback for up to rc_len/8000 seconds - the
    ; PCM_EXCL contract (SPEC.md 34.4); a mouse click aborts mid-chunk,
    ; and BL mirrors the kernel's release-folded baseline across the
    ; chunk gaps so a press landing between chunks aborts too (SPEC.md 35).
.spk:
    test byte [rc_caps], SND_CAP_PCM_EXCL
    jz .nosink
    call OSAPI_MOUSE                ; BL = abort baseline: buttons already
    mov bl, al                      ; down now never abort until re-pressed
    mov di, 0                       ; DI = bytes played so far
.chunk:
    mov cx, [rc_len]
    sub cx, di
    jz .done
    cmp cx, RC_PBUF_SZ
    jbe .n
    mov cx, RC_PBUF_SZ              ; 0.5 s chunks (callers chunk <= 2 s)
.n:
    push cx
    mov ax, 0x0005                  ; verb 5 read: grant -> rc_pbuf
    mov si, [rc_goff]
    add si, di
    push di
    mov di, rc_pbuf
    call OSAPI_SND_STREAM
    pop di
    pop cx
    or ax, ax
    jnz .err
    push es                         ; the clip lives in our own image: ES=DS
    push cx
    push ds
    pop es
    mov si, rc_pbuf
    mov dx, RC_RATE
    call OSAPI_SND_PLAY             ; BLOCKS; out AX = 0 ok / 1..5 err
    pop cx
    pop es
    or ax, ax
    jnz .pres
    add di, cx
    call OSAPI_MOUSE                ; a press in the inter-chunk gap: AH =
    mov ah, bl                      ; buttons down now that are not in the
    not ah                          ; baseline = newly pressed -> abort
    and ah, al
    jnz .gapab
    mov bl, al                      ; no new press: fold releases into the
    jmp .chunk                      ; baseline (AL is a subset of BL here)
.done:
    mov word [rc_msg], rc_s_pdonesp
    ret
.pres:
    cmp ax, 5                       ; aborted by a click: the kernel already
    jne .p3                         ; drained the aborting press (SPEC.md 34.4)
.gapab:                             ; (gap-abort presses are NOT drained -
    mov word [rc_msg], rc_s_pabort  ; they also land as an ordinary click)
    ret
.p3:
    cmp ax, 3                       ; disabled by the user (Control Panel)
    jne .err
    mov word [rc_msg], rc_s_spkoff
    ret
.busy:
    mov word [rc_msg], rc_s_busy
    ret
.nodata:
    mov word [rc_msg], rc_s_nodata
    ret
.nosink:
    mov word [rc_msg], rc_s_nosink
    ret
.err:
    jmp rc_errmsg

; -----------------------------------------------------------------------------
; rc_do_demo - the DEMO button: stage the built-in sine sweep as if recorded
; (1 s at 8 kHz: 400 Hz rising linearly to 800 Hz over the first half
; second, falling back to 400 Hz over the second = 8,000 samples)
; in:  nothing
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
rc_do_demo:
    cmp byte [rc_st], RC_IDLE
    jne .busy
    call rc_grant
    jc .out
    mov di, [rc_goff]               ; DI = staging cursor
    xor bx, bx                      ; BX = phase accumulator
    mov dx, RC_INC400               ; DX = phase increment (400 Hz)
    mov word [rc_dstep], 1          ; first half sweeps upward
    mov word [rc_err], 0
.chunk:
    call rc_synth
    call rc_stage
    jc .out
    mov ax, di                      ; AX = bytes staged so far
    sub ax, [rc_goff]
    cmp ax, RC_D_LEN/2              ; halfway: the sweep turns around
    jne .on
    neg word [rc_dstep]
.on:
    cmp ax, RC_D_LEN
    jb .chunk
    mov word [rc_len], RC_D_LEN
    mov word [rc_msg], rc_s_demo
.out:
    ret
.busy:
    mov word [rc_msg], rc_s_busy
    ret

; -----------------------------------------------------------------------------
; rc_stage - verb 6: stage one RC_CHUNK from rc_pbuf into the grant at DI
; in:  DI = grant staging cursor
; out: CF=0 with DI advanced, CF=1 refused ([rc_msg] = the error);
;      clobbers AX, CX, SI
; -----------------------------------------------------------------------------
rc_stage:
    mov ax, 0x0006                  ; verb 6 stage: DS:SI -> grant at DI
    mov si, rc_pbuf
    mov cx, RC_CHUNK
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .bad
    add di, RC_CHUNK
    clc
    ret
.bad:
    call rc_errmsg
    stc
    ret

; -----------------------------------------------------------------------------
; rc_synth - fill rc_pbuf's first RC_CHUNK bytes of sweeping sine, 8-bit
; unsigned (the SPEC.md 34.2 wire format): each sample reads the 256-entry
; table at the phase accumulator's top byte, then the increment is
; Bresenham-stepped by [rc_dstep] - RC_INC800-RC_INC400 steps spread
; evenly over RC_D_LEN/2 samples, one linear octave per demo half
; in:  BX = phase accumulator, DX = increment; [rc_err]/[rc_dstep] live
; out: BX, DX, [rc_err] advanced; preserves the other registers
; -----------------------------------------------------------------------------
rc_synth:
    push ax
    push cx
    push si
    push di
    mov di, rc_pbuf
    mov cx, RC_CHUNK
.s:
    mov al, bh                      ; top phase byte indexes the table
    mov ah, 0
    mov si, ax
    mov al, [rc_sine+si]
    mov [di], al
    inc di
    add bx, dx                      ; phase += increment
    mov ax, [rc_err]                ; err += span; on overflow past the
    add ax, RC_INC800-RC_INC400     ; half-length the increment takes
    cmp ax, RC_D_LEN/2              ; one dstep step
    jb .keep
    sub ax, RC_D_LEN/2
    add dx, [rc_dstep]
.keep:
    mov [rc_err], ax
    loop .s
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rc_errmsg - point the status line at 'ERROR n' (or 'ERROR S' for stale)
; in:  AX = error code (1..8, or 0FFFFh stale)
; out: nothing; [rc_msg] set; preserves all registers
; -----------------------------------------------------------------------------
rc_errmsg:
    push ax
    cmp ax, 0FFFFh
    jne .digit
    mov al, 'S'
    jmp .put
.digit:
    add al, '0'
.put:
    mov [rc_s_err+6], al
    mov word [rc_msg], rc_s_err
    pop ax
    ret

; -----------------------------------------------------------------------------
; rc_draw_all - waveform strip + status lines + buttons (content is white)
; in:  [rc_ox]/[rc_oy] stashed; caller holds the gfx lock
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
rc_draw_all:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call rc_draw_wave
    call rc_draw_status
    call rc_draw_btns
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rc_draw_wave - the strip: frame, white field, centerline, then one sample
; per column read back through verb 5 (decimated: offset = col*len/208)
; in:  [rc_ox]/[rc_oy], [rc_goff]/[rc_len]
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
rc_draw_wave:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [rc_ox]
    add ax, RC_WAVE_X1
    mov bx, [rc_oy]
    add bx, RC_WAVE_Y1
    mov cx, [rc_ox]
    add cx, RC_WAVE_X2
    mov dx, [rc_oy]
    add dx, RC_WAVE_Y2
    call OSAPI_GFX_FRAME
    mov al, CWHITE                  ; the field (click-repaints reuse this
    call OSAPI_SET_COLOR            ; path with stale pixels underneath)
    mov ax, [rc_ox]
    add ax, RC_WAVE_X1+1
    mov bx, [rc_oy]
    add bx, RC_WAVE_Y1+1
    mov cx, [rc_ox]
    add cx, RC_WAVE_X2-1
    mov dx, [rc_oy]
    add dx, RC_WAVE_Y2-1
    call OSAPI_GFX_FILL
    mov al, CLGRAY                  ; centerline
    call OSAPI_SET_COLOR
    mov ax, [rc_ox]
    add ax, RC_WAVE_X1+1
    mov bx, [rc_ox]
    add bx, RC_WAVE_X2-1
    mov dx, [rc_oy]
    add dx, RC_WAVE_MID
    call OSAPI_GFX_HLINE
    cmp word [rc_len], 0
    je .out                         ; nothing captured: centerline only
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov di, 0                       ; DI = column 0..207
.col:
    mov ax, di                      ; sample offset = col * len / 208
    mul word [rc_len]               ; DX:AX (fits: < 208 * 65536)
    mov cx, RC_WAVE_W
    div cx                          ; AX = offset into the take
    mov si, [rc_goff]
    add si, ax
    push di
    mov ax, 0x0005                  ; verb 5: one sample -> rc_pbuf
    mov di, rc_pbuf
    mov cx, 1
    call OSAPI_SND_STREAM
    pop di
    or ax, ax
    jnz .out                        ; grant gone (cannot happen mid-draw)
    mov al, [rc_pbuf]
    xor al, 0x80                    ; unsigned 0..255 -> signed -128..127
    cbw
    sar ax, 1
    sar ax, 1                       ; scale /4: -32..+31 fits the 66px field
    mov bx, [rc_oy]
    add bx, RC_WAVE_MID
    mov dx, bx
    sub bx, ax                      ; positive samples plot upward
    cmp bx, dx
    jle .ord
    xchg bx, dx
.ord:
    mov ax, [rc_ox]
    add ax, RC_WAVE_X1+1
    add ax, di
    call OSAPI_GFX_VLINE            ; AX = x, BX = y1, DX = y2
    inc di
    cmp di, RC_WAVE_W
    jb .col
.out:
    ret

; -----------------------------------------------------------------------------
; rc_draw_status - line 1: the state message; line 2: byte count + in-device
; in:  [rc_ox]/[rc_oy], [rc_msg], [rc_len]
; out: nothing; clobbers AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
rc_draw_status:
    mov cx, [rc_ox]
    add cx, 4
    mov dx, [rc_oy]
    add dx, RC_ST1_Y
    mov si, [rc_msg]
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the content's own ground
    call OSAPI_FONT_RUN             ; (rc_draw_all's header; SPEC.md 6.6.5)
    mov ax, [rc_len]                ; render the byte count into line 2
    mov bx, rc_s_line2
    call rc_putu5
    mov cx, [rc_ox]
    add cx, 4
    mov dx, [rc_oy]
    add dx, RC_ST2_Y
    mov si, rc_s_line2
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    ret

; -----------------------------------------------------------------------------
; rc_draw_btns - the four buttons; disabled ones draw dark gray (layer one
; of the SPEC.md 32 three-layer idiom)
; in:  [rc_ox]/[rc_oy], [rc_caps], [rc_st], [rc_len]
; out: nothing; clobbers AX, CX, SI (and rc_btn's scratch)
; -----------------------------------------------------------------------------
rc_draw_btns:
    call rc_layout
    mov cl, 0                       ; REC: needs PCM_IN and idle
    test byte [rc_caps], SND_CAP_PCM_IN
    jz .rec
    cmp byte [rc_st], RC_IDLE
    jne .rec
    mov cl, 1
.rec:
    mov ax, 0
    mov si, rc_l_rec
    call rc_btn

    mov cl, 0                       ; STOP: needs an open stream
    cmp byte [rc_st], RC_IDLE
    je .stop
    mov cl, 1
.stop:
    mov ax, 1
    mov si, rc_l_stop
    call rc_btn

    mov cl, 0                       ; PLAY: needs a take, idle, and a sink
    cmp byte [rc_st], RC_IDLE
    jne .play
    cmp word [rc_len], 0
    je .play
    test byte [rc_caps], SND_CAP_PCM_BG | SND_CAP_PCM_EXCL
    jz .play
    mov cl, 1
.play:
    mov ax, 2
    mov si, rc_l_play
    call rc_btn

    mov cl, 0                       ; DEMO: needs idle
    cmp byte [rc_st], RC_IDLE
    jne .demo
    mov cl, 1
.demo:
    mov ax, 3
    mov si, rc_l_demo
    call rc_btn
    ret

; -----------------------------------------------------------------------------
; rc_btn - one button, through the shared control (apps/os88ui.inc)
; in:  AX = content-relative x1, SI = label, CL = 1 enabled / 0 disabled
; out: nothing; preserves all registers
;
; The drawing used to be here: frame, centred label, and OSAPI_GFX_PEN rather
; than OSAPI_SET_COLOR so that a disabled control is CDGRAY *and* [gfx_dis]
; (SPEC.md 47 rule 1 - with the colour alone this drew a dotted frame around a
; solid-black caption, which is rule 2's own failure). All of that is
; os88ui_btn now, and this is the adapter: it turns Recorder's
; content-relative x into the rect the shared routine takes.
;
; The rect is a STORED 4-word block rather than four registers, because
; os88ui_bhit reads the same words - so the drawn button and the clickable
; button cannot drift (fm_hit's discipline, SPEC.md 22).
;
; THE DOWN STATE IS READ HERE AND NOT PASSED IN, which is what makes every
; repaint agree with it for free (SPEC.md 13.8): rc_draw_btns is the one
; painter, a W_PAINT goes through it, and [rc_down] is consulted per button.
; Passing it as an argument would mean the press path knew and W_PAINT did not.
; -----------------------------------------------------------------------------
rc_btn:
    push ax
    push bx
    push di
    mov bx, ax                      ; AX = the button index
    shl bx, 1
    shl bx, 1
    shl bx, 1                       ; *8: four words a rect (8086: no shl imm)
    add bx, rc_rects
    mov di, OS88UI_FILL             ; NOT `xor di, di`, and this was the bug
                                    ; reported as "the buttons stay black with
                                    ; no text visible after mouse-on
                                    ; mouse-off". A release redraws this button
                                    ; over a PRESSED one, whose interior is
                                    ; black - so without the fill the upright
                                    ; redraw keeps that black interior and
                                    ; letters a black caption onto it, and the
                                    ; button goes INVISIBLE rather than coming
                                    ; up. OS88UI_FILL asks "can this be drawn a
                                    ; second time without the ground being
                                    ; repainted first?", not "is my background
                                    ; white" - os88ui.inc's own header, and the
                                    ; same defect os88net's Connect had
                                    ; (SPEC.md 13.8.4)
    or cl, cl
    jnz .live
    or di, OS88UI_DIS               ; ADDS to the fill rather than replacing
                                    ; it, as the DOWN branch below does - a
                                    ; control does not stop needing the erase
                                    ; by being greyed
.live:
    inc ax                          ; [rc_down] is index+1, os88ui_bfind's own
    cmp ax, [rc_down]               ; sentinel, so 0 can mean "none"
    jne .draw
    or di, OS88UI_DOWN              ; ...and DIS still outranks it inside
.draw:                              ; os88ui_btn, which is where that rule lives
    call os88ui_btn
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rc_layout - rebuild the four button rects from the live content origin
; in:  [rc_ox]/[rc_oy]
; out: nothing; preserves all registers
;
; ONE table, read by the drawing (os88ui_btn) and by the hit test
; (os88ui_bfind) alike. The hit test used to be a ladder of literals in
; rc_onclick - 4/56, 58/110, 112/164, 166 - against a drawing that used
; 4/58/112/166 and RC_BTN_W: two descriptions of one row of buttons, agreeing
; by hand. They cannot disagree now (SPEC.md 22's fm_hit discipline).
; -----------------------------------------------------------------------------
rc_layout:
    push ax
    push bx
    push cx
    push si
    mov si, rc_bx                   ; the four content-relative x1s
    mov bx, rc_rects
    mov cx, 4
.one:
    lodsw
    add ax, [rc_ox]
    mov [bx+0], ax                  ; x1
    add ax, RC_BTN_W-1
    mov [bx+4], ax                  ; x2
    mov ax, [rc_oy]
    add ax, RC_BTN_Y
    mov [bx+2], ax                  ; y1
    add ax, RC_BTN_H-1
    mov [bx+6], ax                  ; y2
    add bx, 8
    loop .one
    pop si
    pop cx
    pop bx
    pop ax
    ret

rc_bx:      dw 4, 58, 112, 166      ; the button row, content-relative
rc_rects:   times 16 dw 0           ; ...as four {x1,y1,x2,y2}, screen coords
rc_down:    dw 0                    ; SPEC.md 13.8: which button is being HELD,
                                    ; as os88ui_bfind's index+1 (0 = none). It
                                    ; is DATA rather than bss for os88ui_armw's
                                    ; own reason - a package's bss is one total
                                    ; with equ offsets (OS88_BSS) and two bytes
                                    ; of image is cheaper than renumbering it.
                                    ; rc_draw_btns reads it, so a full repaint
                                    ; arriving mid-press draws the held button
                                    ; down and the state cannot part from the
                                    ; glass (13.8's idempotence, which is the
                                    ; whole reason DOWN is drawn and not XOR-ed)

; -----------------------------------------------------------------------------
; rc_putu5 - AX as 5 zero-padded decimal chars at DS:BX
; in:  AX = value, BX -> destination
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
rc_putu5:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, 10
    add bx, 4                       ; write the digits backwards
    mov cx, 5
.d:
    xor dx, dx
    div si
    add dl, '0'
    mov [bx], dl
    dec bx
    loop .d
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
; =============================================================================
; 'About Recorder' - the credit card (SPEC.md 12.2, 20.5.1)
; =============================================================================
; The card is os88ui.inc's; the flag, the painter drawing it last and the two
; handlers taking it down are what a widget cannot own.

; -----------------------------------------------------------------------------
; rc_about - the OSAPI_ABOUT_SET handler (slot 0x01E0)
; in:  SI = our window ptr; the UI task, gfx lock HELD
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
rc_about:
    push bx
    push si
    mov byte [rc_abon], 1
    mov bx, si
    mov si, rc_ablines
    call os88ui_about               ; arms the clip itself: a menu dispatch
    pop si                          ; arrives without one (SPEC.md 11.3)
    pop bx
    ret

; -----------------------------------------------------------------------------
; rc_abdismiss - take the card down if it is up
; in:  SI = our window ptr; gfx lock held
; out: CF = 1 the click was spent doing it; preserves every register
;
; rc_draw_all is the whole face, which is what the card covered - and it is
; what rc_paint draws. The poll goes with it, because the stream may have
; retired while the credits were up.
; -----------------------------------------------------------------------------
rc_abdismiss:
    cmp byte [rc_abon], 0
    je .none
    push ax
    push bx
    push dx
    mov byte [rc_abon], 0
    mov bx, si
    call OSAPI_WM_CONTENT           ; the window may have been dragged since
    mov [rc_ox], ax                 ; the card went up
    mov [rc_oy], dx
    mov bx, si
    call OSAPI_WM_CLIP_SET          ; ...and nothing armed a region for a
    jc .gone                        ; click either (SPEC.md 11.3)
    call rc_poll
    call rc_draw_all
.gone:
    pop dx
    pop bx
    pop ax
    stc
    ret
.none:
    clc
    ret

; --- the About card's lines (SPEC.md 20.5.1) ----------------------------------
; SHORT: the card is clamped to the content box and this window's is 218px.
rc_ablines:
    dw rc_ab1, rc_ab2, rc_ab3, rc_ab4, 0
rc_ab1:     db 'Recorder for os8088', 0
rc_ab2:     db 0
rc_ab3:     db 'Contributed by', 0
rc_ab4:     db 'Jorge Gonzalez', 0

rc_tpl:
    dw 210, 150, 220, 140           ; x, y, w, h -> content 218 x 121
    dw rc_ttl, rc_paint, 0, rc_onclick

rc_ttl:      db 'Recorder', 0

; --- app menu set (SPEC.md 12.2) -----------------------------------------------
; One menu, because the app has exactly one axis: the [rc_st] state machine.
; The item order is pinned by the RC_CMD_* constants rc_oncmd compares
; against - the indices ARE the wire format between the two. AM_NAME
; reuses rc_ttl: the bar label and the window title are the same eight
; characters by construction, and a package pays for every duplicate byte.
    OS88_MENUSET rc_menus, rc_ttl, rc_oncmd
        OS88_MENU rc_m_sound, rc_i_sound, 4
    OS88_MENUSET_END rc_menus

rc_m_sound:  db 'Sound', 0
rc_i_sound:  dw rc_it_rec, rc_it_stop, rc_it_play, rc_it_demo
rc_it_rec:   db 'Record', 0
rc_it_stop:  db 'Stop', 0
rc_it_play:  db 'Play', 0
rc_it_demo:  db 'Demo', 0

; button labels
rc_l_rec:    db 'REC', 0
rc_l_stop:   db 'STOP', 0
rc_l_play:   db 'PLAY', 0
rc_l_demo:   db 'DEMO', 0

; status messages (all <= 26 chars: the line is 214 px at 8 px/char)
rc_s_idle:   db 'READY', 0
rc_s_rec:    db 'RECORDING...', 0
rc_s_rwdog:  db 'REC STOPPED (WATCHDOG)', 0
rc_s_rfull:  db 'REC STOPPED (FULL)', 0
rc_s_stopped: db 'STOPPED', 0
rc_s_pstop:  db 'PLAY STOPPED', 0
rc_s_psb:    db 'PLAYING (SB)', 0
rc_s_pdonesb: db 'PLAY DONE (SB)', 0
rc_s_pdonesp: db 'PLAY DONE (SPEAKER)', 0
rc_s_pwdog:  db 'PLAY STOPPED (WATCHDOG)', 0
rc_s_pabort: db 'PLAY ABORTED', 0
rc_s_busy:   db 'BUSY - STOP FIRST', 0
rc_s_nodata: db 'NOTHING RECORDED', 0
rc_s_noin:   db 'NO REC DEVICE (SB NEEDED)', 0
rc_s_nomem:  db 'NO SOUND MEMORY', 0
rc_s_nodrv:  db 'NO SOUND DRIVER', 0
rc_s_spkoff: db 'SPEAKER PCM DISABLED', 0
rc_s_nosink: db 'NO OUTPUT DEVICE', 0
rc_s_notrun: db 'NOT RUNNING', 0
rc_s_demo:   db 'DEMO STAGED - 400-800 HZ', 0
rc_s_err:    db 'ERROR -', 0        ; [+6] patched by rc_errmsg
rc_s_line2:  db '00000/00000 B  IN:--', 0 ; [+0..4] captured, [+6..10] the
                                    ; CAPACITY rc_grant actually got (it is
                                    ; not 40,000 on every machine any more),
                                    ; [+18..19] the input device

; 256-entry sine table for the demo sweep: 128 + round(88*sin(2*pi*i/256)),
; range 40..216 - the amplitude the square demo used (0x28/0xD8)
rc_sine:
    db 128,130,132,134,137,139,141,143,145,147,149,151,154,156,158,160
    db 162,164,166,168,169,171,173,175,177,179,180,182,184,185,187,189
    db 190,192,193,195,196,197,199,200,201,202,203,205,206,207,208,208
    db 209,210,211,212,212,213,213,214,214,215,215,215,216,216,216,216
    db 216,216,216,216,216,215,215,215,214,214,213,213,212,212,211,210
    db 209,208,208,207,206,205,203,202,201,200,199,197,196,195,193,192
    db 190,189,187,185,184,182,180,179,177,175,173,171,169,168,166,164
    db 162,160,158,156,154,151,149,147,145,143,141,139,137,134,132,130
    db 128,126,124,122,119,117,115,113,111,109,107,105,102,100, 98, 96
    db  94, 92, 90, 88, 87, 85, 83, 81, 79, 77, 76, 74, 72, 71, 69, 67
    db  66, 64, 63, 61, 60, 59, 57, 56, 55, 54, 53, 51, 50, 49, 48, 48
    db  47, 46, 45, 44, 44, 43, 43, 42, 42, 41, 41, 41, 40, 40, 40, 40
    db  40, 40, 40, 40, 40, 41, 41, 41, 42, 42, 43, 43, 44, 44, 45, 46
    db  47, 48, 48, 49, 50, 51, 53, 54, 55, 56, 57, 59, 60, 61, 63, 64
    db  66, 67, 69, 71, 72, 74, 76, 77, 79, 81, 83, 85, 87, 88, 90, 92
    db  94, 96, 98,100,102,105,107,109,111,113,115,117,119,122,124,126

; --- the shared controls (SPEC.md 47 rule 1 in one place) -------------------
%define OS88UI_ABOUT            ; ...and the standard About card beside the
%include "os88ui.inc"           ; buttons this already drew

    OS88_BSS 4023
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
; All zero = fresh state: no grant, no take, idle. rc_msg is stamped by
; rc_entry before the first paint can read it.
rc_caps  equ os88_image_end + 0     ; word: merged caps (queried once)
rc_goff  equ os88_image_end + 2     ; word: grant offset (SND_SEG-absolute)
rc_len   equ os88_image_end + 4     ; word: valid bytes in the grant
rc_ox    equ os88_image_end + 6     ; word: content left (per handler call)
rc_oy    equ os88_image_end + 8     ; word: content top
rc_msg   equ os88_image_end + 10    ; word: current status string ptr
rc_hand  equ os88_image_end + 12    ; byte: stream handle
rc_st    equ os88_image_end + 13    ; byte: RC_IDLE / RC_REC / RC_PLAY
rc_have  equ os88_image_end + 14    ; byte: the grant exists
                                    ; +15: pad
rc_pbuf  equ os88_image_end + 16    ; 4,000 B: synth / read-back / clip buffer
rc_err   equ os88_image_end + 4016  ; word: demo sweep Bresenham error accum
rc_dstep equ os88_image_end + 4018  ; word: sweep direction, +1 up / -1 down
rc_cap   equ os88_image_end + 4020  ; word: the grant size rc_grant actually
                                    ; got - a TIER, not RC_CAP_MAX (see the
                                    ; parameters above). Appended rather than
                                    ; slotted into the pad at +15, because an
                                    ; odd-addressed word is legal and slow and
                                    ; this is read on every poll
rc_abon  equ os88_image_end + 4022  ; byte: the About card is up
                                    ; total 4023
