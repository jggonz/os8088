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
;     bb_avail three-layer idiom, SPEC.md 32) - the app stays fully usable
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
    mov bx, si
    push cx
    push dx
    call OSAPI_WM_CONTENT
    mov [rc_ox], ax
    mov [rc_oy], dx
    pop dx
    pop cx
    sub cx, [rc_ox]                 ; -> content-relative click
    sub dx, [rc_oy]
    call rc_poll                    ; retire a finished stream first, so the
                                    ; button logic sees the current state
    cmp dx, RC_BTN_Y                ; the button row?
    jl .repaint
    cmp dx, RC_BTN_Y+RC_BTN_H
    jge .repaint
    cmp cx, 4
    jl .repaint
    cmp cx, 56
    jl .rec
    cmp cx, 58
    jl .repaint
    cmp cx, 110
    jl .stop
    cmp cx, 112
    jl .repaint
    cmp cx, 164
    jl .play
    cmp cx, 166
    jl .repaint
.demo:
    call rc_do_demo
    jmp .repaint
.rec:
    call rc_do_rec
    jmp .repaint
.stop:
    call rc_do_stop
    jmp .repaint
.play:
    call rc_do_play
.repaint:
    call rc_repaint
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
    mov bx, si
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
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov cx, [rc_ox]
    add cx, 4
    mov dx, [rc_oy]
    add dx, RC_ST1_Y
    mov si, [rc_msg]
    call OSAPI_FONT_STR
    mov ax, [rc_len]                ; render the byte count into line 2
    mov bx, rc_s_line2
    call rc_putu5
    mov cx, [rc_ox]
    add cx, 4
    mov dx, [rc_oy]
    add dx, RC_ST2_Y
    mov si, rc_s_line2
    call OSAPI_FONT_STR
    ret

; -----------------------------------------------------------------------------
; rc_draw_btns - the four buttons; disabled ones draw dark gray (layer one
; of the SPEC.md 32 three-layer idiom)
; in:  [rc_ox]/[rc_oy], [rc_caps], [rc_st], [rc_len]
; out: nothing; clobbers AX, CX, SI (and rc_btn's scratch)
; -----------------------------------------------------------------------------
rc_draw_btns:
    mov cl, 0                       ; REC: needs PCM_IN and idle
    test byte [rc_caps], SND_CAP_PCM_IN
    jz .rec
    cmp byte [rc_st], RC_IDLE
    jne .rec
    mov cl, 1
.rec:
    mov ax, 4
    mov si, rc_l_rec
    call rc_btn

    mov cl, 0                       ; STOP: needs an open stream
    cmp byte [rc_st], RC_IDLE
    je .stop
    mov cl, 1
.stop:
    mov ax, 58
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
    mov ax, 112
    mov si, rc_l_play
    call rc_btn

    mov cl, 0                       ; DEMO: needs idle
    cmp byte [rc_st], RC_IDLE
    jne .demo
    mov cl, 1
.demo:
    mov ax, 166
    mov si, rc_l_demo
    call rc_btn
    ret

; -----------------------------------------------------------------------------
; rc_btn - one button: 1px frame + centered label, black or dark gray
; in:  AX = content-relative x1, SI = label, CL = 1 enabled / 0 disabled
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
rc_btn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, ax                      ; DI = x1, content-relative
    mov al, CBLACK
    or cl, cl
    jnz .col
    mov al, CDGRAY
.col:
    call OSAPI_SET_COLOR
    mov ax, [rc_ox]
    add ax, di
    mov di, ax                      ; DI = x1, absolute
    mov bx, [rc_oy]
    add bx, RC_BTN_Y
    mov cx, ax
    add cx, RC_BTN_W-1
    mov dx, bx
    add dx, RC_BTN_H-1
    call OSAPI_GFX_FRAME
    call OSAPI_FONT_WIDTH           ; AX = label width, px
    mov cx, RC_BTN_W
    sub cx, ax
    shr cx, 1
    add cx, di                      ; centered label x
    mov dx, [rc_oy]
    add dx, RC_BTN_Y+4
    call OSAPI_FONT_STR
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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

    OS88_BSS 4022
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
                                    ; total 4022
