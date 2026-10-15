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
;     keeps running) and falls back to os88pcm_play chunks read back via
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
RC_CHUNK    equ 250                 ; demo synth/stage chunk (divides RC_D_LEN/2,
                                    ; so the sweep turns on a chunk boundary).
                                    ; It is most of rc_pbuf: 32 verb-6 calls
                                    ; stage the 1 s take where 8 did, for 750
                                    ; bytes of bss. rc_pbuf is 256 whatever
                                    ; this is, because it is also the speaker
                                    ; play's rescale table (SPEC.md 34.4), so
                                    ; 250 - the largest divisor of 4,000 that
                                    ; fits - costs nothing over a smaller
                                    ; chunk, and each call it saves is ~2,600
                                    ; cycles of the demo
%if RC_CHUNK > 256
  %error "RC_CHUNK: rc_pbuf is 256 bytes"
%endif
RC_PBUF_SZ  equ 4000                ; speaker play: 0.5 s PCM_EXCL chunks, in a
                                    ; claim held for one play (rc_do_play)
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
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    OS88_REGION_MOVABLE
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
    call rc_org
    call rc_poll
    call rc_draw_all
    cmp byte [rc_abon], 0           ; ...and the About card LAST, over the
    je rc_pop6                      ; face it is opaque about (SPEC.md 20.5.1)
    mov si, rc_ablines              ; (SI is the window no longer: the pops
    call os88ui_about_d             ; put it back. _d: this paint's region is
    jmp rc_pop6                     ; already armed)

; -----------------------------------------------------------------------------
; rc_org - stash the window's content origin in [rc_ox]/[rc_oy]
; in:  SI = window ptr
; out: BX = SI; clobbers AX - and KEEPS DX, which is the whole point
;
; OSAPI_WM_CONTENT answers the top in DX, and three of the six callers hold
; the POINTER's y in DX across this: W_ONCLICK, W_ONDRAG and W_ONMOUSEUP all
; hit-test (CX, DX) afterwards. rc_onclick saved it around the call and the
; other two did not, so the release and the drag tested (x, content top) - a
; point that is never inside a button - and NO BUTTON EVER FIRED; only the
; Sound menu worked (tests/recorder.py step 1, red on the old code).
; -----------------------------------------------------------------------------
rc_org:
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [rc_ox], ax
    mov [rc_oy], dx
    pop dx
    ret

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
    call rc_org
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
    jmp rc_pop6                     ; four buttons rather than one, and a press
                                    ; is a human-rate event

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
    call rc_org                     ; the window may have moved since the press
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
    jmp rc_pop6

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
    call rc_org                     ; the window may have moved since the press
    call rc_layout
    call os88ui_fire                ; AX = what the press armed, and it is
    or ax, ax                       ; cleared - one release per press, so
    jz .out                         ; there is no stale arm to guard against
    mov di, ax
    cmp word [rc_down], 0           ; ...and the pressed button comes back UP
    je .find                        ; first and unconditionally, whatever the
    mov word [rc_down], 0           ; release turns out to mean. Every path
    push cx
    call rc_draw_btns               ; below either acts (and repaints) or
    pop cx                          ; cancels (and must leave it upright). CX
.find:                              ; is the pointer's x, and the painter's to
                                    ; lose: its CL is each button's enable, and
                                    ; this hit-tested (CL = 1, DX = top) too
    mov ax, 4
    mov bx, rc_rects
    call os88ui_bfind               ; AX = the button under the RELEASE
    cmp ax, di
    jne .out                        ; a different button, or none: cancelled
    call rc_poll                    ; retire a finished stream first, so the
                                    ; button logic sees the current state
    dec ax                          ; index+1 -> the RC_CMD_* the menu sends
    call rc_cmd
.out:
                                    ; THE SHARED EPILOGUE, written out by hand
                                    ; (OS88_POPLADDER's shape: the stkapps gate
                                    ; could not then see that macro's labels;
                                    ; it can since SPEC.md 20.5's round 2): the
                                    ; five routines that save all six end here
rc_pop6:
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
    push ax                         ; and then does what it says. The
    call rc_org                     ; selection: wm_content answers in AX
    call rc_poll                    ; retire a finished stream first, so the
    pop ax                          ; rc_do_* guards below see the live state
                                    ; ...and fall into rc_cmd, which is the
                                    ; whole of the rest

; -----------------------------------------------------------------------------
; rc_cmd - run one of the four transitions, then repaint
; in:  AL = RC_CMD_* (the menu's item index, or the button's index); the
;      state already polled; [rc_ox]/[rc_oy] stashed; gfx lock held
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; THE ONE DISPATCH both ways of asking share - the button path and the menu
; path carried a compare ladder each. Record, Play and Demo all need an idle
; machine, so that refusal is made here once rather than in each of them. A
; fifth index cannot happen - the pull-down only offers the four items we
; declared - but the poll may have moved the state, so it still repaints.
; -----------------------------------------------------------------------------
rc_cmd:
    cmp al, RC_CMD_DEMO
    ja rc_repaint
    cmp al, RC_CMD_STOP
    je .go                          ; Stop is the one that runs while busy
    cmp byte [rc_st], RC_IDLE
    je .go
    mov word [rc_msg], rc_s_busy
    jmp short rc_repaint
.go:
    cbw
    xchg bx, ax
    shl bx, 1
    call [rc_cmdtab+bx]             ; ...and fall into the repaint

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
    jmp rc_draw_all

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
    mov ax, rc_s_pdonesb
    jmp short .say
.pwdog:
    call rc_close
    mov ax, rc_s_pwdog
    jmp short .say
.pstale:
    mov byte [rc_st], RC_IDLE       ; stale: torn down under us
    mov ax, rc_s_stopped
    jmp short .say

    ; --- recording (PCM_IN) ---------------------------------------------------
.rec:
    or ax, ax                       ; 0 = still recording
    jnz .r1
.rlive:
    mov [rc_len], dx                ; live captured count
    jmp short .out
.r1:
    cmp ax, SND_ST_ENDED            ; watchdog: input IRQs stopped arriving
    je .rwdog                       ; (on QEMU they never start - SPEC.md 35)
    cmp ax, SND_ST_UNDER            ; state 1 is BOTH pauses (SPEC.md 20.3):
    jne .pstale                     ; (not 1: stale, torn down under us)
    cmp dx, [rc_cap]                ; consumed < capacity = drain starve -
    jb .rlive                       ; track the count, kernel resumes (34.6)
    mov [rc_len], dx                ; consumed == capacity: full - close
    call rc_close
    mov ax, rc_s_rfull
    jmp short .say
.rwdog:
    mov [rc_len], dx
    call rc_close
    mov ax, rc_s_rwdog
.say:
    mov [rc_msg], ax
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
    mov si, rc_s_nodrv
    cmp ax, 4                       ; 4 = no streaming sink at all: no tier
    je .fail                        ; is going to help, and 'NO SOUND MEMORY'
                                    ; would send the user looking for RAM
    inc bx                          ; anything else: the next tier down
    inc bx
    cmp bx, rc_captab + RC_TIERS*2
    jb .try
    mov si, rc_s_nomem
.fail:
    mov [rc_msg], si
    pop bx
    stc
    ret
.got:
    mov [rc_goff], si
    mov ax, [bx]                    ; the size we actually got is the capacity
    mov [rc_cap], ax
    mov byte [rc_have], 1
    mov bx, rc_s_line2+6            ; ...and line 2 says so, because it is not
    call rc_putu5                   ; the same number on every machine now
    pop bx
.ok:
    clc
    ret

; the tiers, largest first (RC_TIERS entries). The last MUST be >= RC_D_LEN.
rc_captab:  dw RC_CAP_MAX, 24000, 16000, RC_D_LEN

; rc_cmd's table, in RC_CMD_* order - the menu's item order (rc_i_sound)
rc_cmdtab:  dw rc_do_rec, rc_do_stop, rc_do_play, rc_do_demo

; -----------------------------------------------------------------------------
; rc_do_rec - the REC button: verb 4 open-in into the grant
; in:  nothing
; out: nothing; clobbers AX, CX, DX, SI
; The three-layer refusal (SPEC.md 32 idiom): no SND_CAP_PCM_IN means the
; button was drawn gray AND this click only writes the explanation. rc_cmd
; has already refused it while a stream is open.
; -----------------------------------------------------------------------------
rc_do_rec:
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
    cmp word [rc_len], 0            ; (idle already: rc_cmd's refusal)
    je .nodata
    test byte [rc_caps], SND_CAP_PCM_BG
    jz .spk

    ; --- SB background stream: the grant is already fully staged ---------------
    xor ax, ax                      ; verb 0 open-out, AH = 0 flags (the
                                    ; byte it saves keeps everything below
                                    ; at its parity across the table's BX)
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
    ;
    ; THE CHUNK BUFFER IS A CLAIM FOR THE LENGTH OF ONE PLAY (SPEC.md 35.2),
    ; not 4,000 bytes of every instance's bss: it is the only thing that
    ; needs it, and a machine with a card - the only one that can stage a
    ; take at all today - never comes here. The chunk stays 0.5 s because
    ; every chunk boundary is a gap in the sound (the verb-5 copy and
    ; os88pcm_play rebuilding its rescale table), so a smaller one would be
    ; more gaps. Verb 5 copies into the CALLER's segment, which its X stub
    ; takes from DS - so DS is the claim for that one call, and nothing
    ; between it and the restore touches our own variables.
.spk:
    test byte [rc_caps], SND_CAP_PCM_EXCL
    jz .nosink
    mov ax, (RC_PBUF_SZ+1023)/1024
    call OSAPI_MEM_CLAIM            ; DX = the play buffer's segment
    jc .nomem
    push bp
    mov bp, dx                      ; BP = it, for the whole play
    call .play
    mov dx, bp
    call OSAPI_MEM_FREE             ; every way out of .play comes back here
    pop bp
    ret
.nomem:
    mov word [rc_msg], rc_s_nomem
    ret
.play:
    call OSAPI_MOUSE                ; BL = abort baseline: buttons already
    mov bl, al                      ; down now never abort until re-pressed
    xor di, di                      ; DI = bytes played so far
.chunk:
    mov cx, [rc_len]
    sub cx, di
    jz .done
    cmp cx, RC_PBUF_SZ
    jbe .n
    mov cx, RC_PBUF_SZ              ; 0.5 s chunks (callers chunk <= 2 s)
.n:
    mov ax, 0x0005                  ; verb 5 read: grant -> the claim at 0
    mov si, [rc_goff]
    add si, di
    push di
    push ds
    mov ds, bp                      ; the CALLER's segment, for this one call
    xor di, di
    call OSAPI_SND_STREAM
    pop ds
    pop di
    or ax, ax
    jnz .err
    push es                         ; ES:SI = the clip, in the claim
    mov es, bp
    xor si, si
    mov dx, RC_RATE
    push bx                         ; (BL is the abort baseline)
    mov bx, rc_pbuf                 ; the rescale table's 256 bytes: the
    call os88pcm_play               ; demo's stage buffer, idle while this
    pop bx                          ; BLOCKS; out AX = 0 ok / 1..5 err
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
; in:  nothing (idle already: rc_cmd's refusal)
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; Synthesised RC_CHUNK bytes at a time into rc_pbuf and staged with verb 6
; (DS:SI -> the grant at DI), so the stage buffer is RC_CHUNK and not the
; take. Each sample reads the sine at the phase accumulator's top byte; then
; the increment is Bresenham-stepped by [rc_dstep] - RC_INC800-RC_INC400
; steps spread evenly over RC_D_LEN/2 samples, one linear octave per half.
;
; THE SINE IS A QUARTER WAVE, 65 bytes for 256 (rc_sine below): the index's
; low 7 bits fold about 64 and bit 7 is the sign, which is EXACT for this
; table - tests/recorder.py step 2 checks all 8,000 samples against the
; formula. What pays for the fold is the loop's own state in registers: the
; Bresenham error is BP and the phase SI, where they were two words of bss
; read and written every sample, and the store is a stosb.
; -----------------------------------------------------------------------------
rc_do_demo:
    call rc_grant
    jc .out
    push bp
    push es
    push ds
    pop es                          ; ES:DI = rc_pbuf, for the stosb
    mov word [rc_dstep], 1          ; first half sweeps upward
    mov bx, rc_sine                 ; BX = the quarter wave, for xlatb
    xor si, si                      ; SI = phase accumulator
    mov dx, RC_INC400               ; DX = phase increment (400 Hz)
    mov bp, 0x10000 - RC_D_LEN/2    ; BP = Bresenham error, biased by the
                                    ; half-length so the carry is the test
    mov ax, [rc_goff]               ; AX = grant staging cursor
.chunk:
    push ax
    mov di, rc_pbuf
    mov cx, RC_CHUNK
.s:
    mov ax, si
    mov al, ah                      ; AH = AL = i, the phase's top byte
    test al, 64                     ; the second quarter of either half is
    jz .q                           ; the first backwards: -i & 127 is 128 - i
    neg al                          ; there, and on the second half it is
.q:                                 ; 256 - i = 128 - (i - 128) - the same
    and al, 127                     ; fold, with nothing to add
    xlatb                           ; AL = 128 + the quarter wave, 128..216
    or ah, ah
    jns .p
    neg al                          ; the second half: 256 - (128 + q) is
.p:                                 ; 128 - q, the first half inverted, and
    stosb                           ; already 8-bit unsigned (SPEC.md 34.2)
    add si, dx                      ; phase += increment
    add bp, RC_INC800-RC_INC400     ; err += span; on overflow past the
    jnc .k                          ; half-length (the bias: a carry out of
    sub bp, RC_D_LEN/2              ; BP) the increment takes one dstep step
    add dx, [rc_dstep]
.k:
    loop .s
    pop di                          ; DI = the grant cursor
    push si
    mov si, rc_pbuf
    mov cx, RC_CHUNK
    mov ax, 0x0006                  ; verb 6 stage: DS:SI -> grant at DI
    call OSAPI_SND_STREAM
    pop si
    or ax, ax
    jnz .bad
    lea ax, [di+RC_CHUNK]
    mov cx, ax
    sub cx, [rc_goff]               ; CX = bytes staged so far
    cmp cx, RC_D_LEN/2              ; halfway: the sweep turns around
    jne .on
    neg word [rc_dstep]
.on:
    cmp cx, RC_D_LEN
    jb .chunk
    mov word [rc_len], RC_D_LEN
    mov word [rc_msg], rc_s_demo
    jmp short .done
.bad:
    call rc_errmsg
.done:
    pop es
    pop bp
.out:
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
; rc_draw_wave - the strip: frame, white field, centerline, then one sample
; per column read back through verb 5 (decimated: offset = col*len/208)
; in:  [rc_ox]/[rc_oy], [rc_goff]/[rc_len]
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
rc_draw_wave:
    mov si, [rc_ox]                 ; every API cell keeps every register but
    mov di, [rc_oy]                 ; its outputs (os88api.inc), so the rect is
    lea bx, [di+RC_WAVE_Y1]         ; computed once and walked in by one
    lea cx, [si+RC_WAVE_X2]
    lea dx, [di+RC_WAVE_Y2]
    mov al, CBLACK
    call OSAPI_SET_COLOR
    lea ax, [si+RC_WAVE_X1]
    call OSAPI_GFX_FRAME
    mov al, CWHITE                  ; the field (click-repaints reuse this
    call OSAPI_SET_COLOR            ; path with stale pixels underneath)
    lea ax, [si+RC_WAVE_X1+1]
    inc bx
    dec cx
    dec dx
    call OSAPI_GFX_FILL
    mov al, CLGRAY                  ; centerline
    call OSAPI_SET_COLOR
    lea ax, [si+RC_WAVE_X1+1]
    mov bx, cx                      ; x2 = the field's right edge
    lea dx, [di+RC_WAVE_MID]
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
    add dx, RC_ST2_Y-RC_ST1_Y       ; FONT_RUN kept CX/DX (os88api.inc)
    mov si, bx
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    ret

; -----------------------------------------------------------------------------
; rc_draw_all - waveform strip + status lines + buttons (content is white)
; in:  [rc_ox]/[rc_oy] stashed; caller holds the gfx lock
; out: nothing; clobbers AX, BX, CX, DX, SI, DI - every caller has saved them
;      or may lose them (rc_paint and rc_abdismiss save all six)
; -----------------------------------------------------------------------------
rc_draw_all:
    call rc_draw_wave
    call rc_draw_status             ; ...and fall into the buttons

; -----------------------------------------------------------------------------
; rc_draw_btns - the four buttons; disabled ones draw dark gray (layer one
; of the SPEC.md 32 three-layer idiom)
; in:  [rc_ox]/[rc_oy], [rc_caps], [rc_st], [rc_len]
; out: nothing; clobbers AX, CX, SI (and rc_btn's scratch)
; -----------------------------------------------------------------------------
rc_draw_btns:
    call rc_layout
    mov ch, 0                       ; CH = 1 idle - three of the four ask
    cmp byte [rc_st], RC_IDLE
    jne .b
    inc ch
.b:
    mov cl, ch                      ; REC: needs PCM_IN and idle
    test byte [rc_caps], SND_CAP_PCM_IN
    jnz .rec
    mov cl, 0
.rec:
    xor ax, ax
    mov si, rc_l_rec
    call rc_btn                     ; (keeps every register)

    mov cl, ch                      ; STOP: needs an open stream
    xor cl, 1
    inc ax
    mov si, rc_l_stop
    call rc_btn

    mov cl, ch                      ; PLAY: needs a take, idle, and a sink
    cmp word [rc_len], 0
    jne .take
    mov cl, 0
.take:
    test byte [rc_caps], SND_CAP_PCM_BG | SND_CAP_PCM_EXCL
    jnz .play
    mov cl, 0
.play:
    inc ax
    mov si, rc_l_play
    call rc_btn

    mov cl, ch                      ; DEMO: needs idle
    inc ax
    mov si, rc_l_demo
    jmp rc_btn

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

; --- the one control's staging (SPEC.md 20.5.1.3) --------------------------
; One button at a time: this package's rects are not one contiguous group,
; so the record is pointed at whichever rect the caller staged.
rc_btlbl: dw 0
rc_btflg: dw 0
    OS88UI_BTNREC rc_btrec, 0, rc_btlbl, rc_btflg, 1

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
    push ax                     ; THE ONE CONTROL (SPEC.md 20.5.1.3): BX
    push bx                     ; already holds this button's rect, SI its
    mov [rc_btlbl], si          ; label and DI its flags, so the record takes
    mov [rc_btflg], di          ; all three and the picture is identical
    mov [rc_btrec+OS88UI_BT_RECTS], bx
    mov bx, rc_btrec
    mov al, 1
    call os88ui_btn
    pop bx
    pop ax
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
; rc_about - the OSAPI_ABOUT_SET handler (slot 0x018A)
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
    cmp byte [rc_abon], 0           ; CF = 0 whatever it holds, so the `je`
    je .none                        ; lands on a ret that says "not spent"
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [rc_abon], 0
    call rc_org                     ; the window may have been dragged since
                                    ; the card went up - and BX = SI after it
    call OSAPI_WM_CLIP_SET          ; ...and nothing armed a region for a
    jc .gone                        ; click either (SPEC.md 11.3)
    call rc_poll
    call rc_draw_all
.gone:
    stc                             ; the pops leave CF alone
    jmp rc_pop6
.none:
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
rc_s_pstop:  db 'PLAY '            ; ...which runs on into
rc_s_stopped: db 'STOPPED', 0       ; this one: one NUL, two messages
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

; THE DEMO'S SINE, A QUARTER WAVE: 128 + round(88*sin(2*pi*i/256)) for
; i = 0..64, range 128..216. The whole wave - range 40..216, the amplitude
; the square demo used (0x28/0xD8) - is this folded about i = 64 on each
; half, and NEGATED on the second (256 - (128 + q) = 128 - q). rc_do_demo
; does the fold; 191 bytes of the image were the other three quarters
rc_sine:
    db 128,130,132,134,137,139,141,143,145,147,149,151,154,156,158,160
    db 162,164,166,168,169,171,173,175,177,179,180,182,184,185,187,189
    db 190,192,193,195,196,197,199,200,201,202,203,205,206,207,208,208
    db 209,210,211,212,212,213,213,214,214,215,215,215,216,216,216,216
    db 216

; --- the shared controls (SPEC.md 47 rule 1 in one place) -------------------
%define OS88UI_ABOUT            ; ...and the standard About card beside the
%define OS88UI_NOGLYPH          ; no check box or radio drawn here (SPEC.md 13.8.9)
%define OS88UI_NOGEST           ; presses routed by hand: no gesture install (SPEC.md 20.5.1.3.4)
%include "os88ui.inc"           ; buttons this already drew
%include "os88pcm.inc"          ; the PWM clip player (SPEC.md 34.4)

    OS88_BSS 276
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
rc_abon  equ os88_image_end + 15    ; byte: the About card is up
rc_cap   equ os88_image_end + 16    ; word: the grant size rc_grant actually
                                    ; got - a TIER, not RC_CAP_MAX (see the
                                    ; parameters above); read on every poll,
                                    ; so it stays even-addressed
rc_dstep equ os88_image_end + 18    ; word: sweep direction, +1 up / -1 down
rc_pbuf  equ os88_image_end + 20    ; 256 B: the demo's synth/stage buffer
                                    ; (RC_CHUNK of it), the waveform's
                                    ; one-sample read-back, and os88pcm_play's
                                    ; rescale table for the length of a
                                    ; speaker play (SPEC.md 34.4) - which is
                                    ; why it is 256 and not RC_CHUNK: the
                                    ; table was 256 bytes of the IMAGE until
                                    ; the library took it from its caller.
                                    ; It was 4,000 B, sized for the speaker
                                    ; play's chunks, which take a claim of
                                    ; their own now (rc_do_play) - and the
                                    ; Bresenham error is BP inside rc_do_demo
                                    ; total 20 + 256 = 276
                                    ; total 4023
