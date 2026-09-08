; =============================================================================
; os8088 - apps/skies/csgame.inc
;
; THE SESSION (SPEC.md 88.8): the fsx bracket, the polled input model a
; bracket has instead of events, the frame, and the panel (88.9).
; =============================================================================

; -----------------------------------------------------------------------------
; cs_cmd_fly - take the machine (SPEC.md 53.1)
; in:  gfx lock held (menu / key / click context).  preserves all registers
; -----------------------------------------------------------------------------
cs_cmd_fly:
    push ax
    push bx
    push cx
    cmp byte [cs_want], CSB_NONE
    je .out                         ; the same predicate the menu greyed on
    mov ax, cs_fsx_main
    mov bx, [cs_win]
    xor cx, cx                      ; no worker to keep: this loop IS the flight
    call OSAPI_FSX_RUN              ; the kernel repaints the desktop on the
.out:                               ; way out (SPEC.md 53.6)
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_fsx_main - the exclusive main (SPEC.md 53.1)
; in:  SI = our window, ES = KERNEL_SEG, DS = CS = ours, gfx lock held for the
;      whole session, every other task frozen
; -----------------------------------------------------------------------------
cs_fsx_main:
    push ds
    pop es
    mov di, cs_fsi
    mov al, [cs_fsxm]
    call OSAPI_FSX_MODE
    jc .out                         ; refused: the desktop is untouched
    mov al, [cs_want]
    mov [cs_back], al
    call cs_r_setup
    jc .out
    call cs_glyphs
    call cs_clearall
    call cs_runway_build            ; the runway, from the record
    cmp byte [cs_inited], 0         ; the first flight starts on the runway;
    jne .keep                       ; a later one carries on where it was
    call cs_reset
.keep:
    mov byte [cs_par], 0
    mov byte [cs_quit], 0
    mov byte [cs_pause], 0
    mov word [cs_tone], 0
    mov ax, [cs_vw]                 ; the panel's horizontal scale, in eighths
    mov bx, 8
    mul bx
    mov bx, 320
    div bx
    mov [cs_hsx], ax
    call OSAPI_GET_TICKS
    mov [cs_last], ax
%ifdef CSDIAG
    call cs_diag_on                 ; the watchdog, for the bracket's length
%endif                              ; (SPEC.md 88.14)
.loop:
    call cs_input
    cmp byte [cs_quit], 0
    jne .leave
    call cs_steps                   ; CX = ticks since the last frame
    call cs_toaststep               ; ...which is what a toast is timed on
    jcxz .draw
    cmp byte [cs_pause], 0
    jne .draw
    mov [cs_tframe], cl             ; THE FRAME'S TICK BUDGET, and what is
.sim:                               ; left of it: cs_ease aims its landing at
    mov [cs_tleft], cl              ; the LAST of them (SPEC.md 88.7.3.2), so
    CSSTAGE 9                       ; the frame that shows the horizon has
    push cx                         ; travelled like any other
    call cs_stick                   ; THE STICK, ONCE A TICK (SPEC.md
    call cs_step                    ; 88.7.5.1): cs_input runs once a FRAME,
    call cs_sound_step              ; which is 137 ms on Mode X, so a tap
    pop cx                          ; shorter than that was never seen at all
    loop .sim
.draw:
    call cs_render
    jmp short .loop
.leave:
%ifdef CSDIAG
    call cs_diag_off
%endif
    xor ax, ax                      ; the engine off with the mode
    xor cx, cx
    mov dl, 0x40
    call OSAPI_SND_TONE
.out:
    ret

; -----------------------------------------------------------------------------
; cs_stick - the two attitude axes into [cs_kpitch] and [cs_kroll]
;            (SPEC.md 88.7.5.1). Clobbers AX, BX
;
; Read ONCE A TICK and not once a frame, which is where it used to live.
; OSAPI_KEY_DOWN is a LEVEL read - the SDK says so, and there is no latched
; form - so a press that came and went between two polls never happened, and
; a poll a frame is 137 ms on Mode X. The stick is the one input where that
; is the difference between a tap being a nudge and a tap being nothing;
; the throttle, the rudder and the brake stay on the frame, where three
; times the sampling would only make them three times as fast.
;
; Four far calls, 46.7 us each: 187 us a tick, so 374 us of a 137 ms frame
; on the two extra ticks - 0.27%.
; -----------------------------------------------------------------------------
cs_stick:
    push bx
    xor bl, bl
    mov al, KSC_UP                  ; up = nose down, down = nose up: the
    call OSAPI_KEY_DOWN             ; stick, as in every flight simulator
    jnc .p1
    dec bl
.p1:
    mov al, KSC_DOWN
    call OSAPI_KEY_DOWN
    jnc .p2
    inc bl
.p2:
    mov al, [cs_waspitch]           ; the PREVIOUS tick's level read...
    mov [cs_waspitch], bl           ; ...and this one's, for the next tick
    or bl, bl                       ; up? then a LATCHED press stands in for
    jnz .p3                         ; it, once (88.7.5.2)
    or al, al                       ; ...unless the key was DOWN last tick,
    jnz .p3                         ; where the latch is a typematic repeat
    mov bl, [cs_tappitch]           ; of a hold that has just ended and
.p3:                                ; spending it overshoots by a tick
    mov byte [cs_tappitch], 0
    mov [cs_kpitch], bl
    xor bl, bl
    mov al, KSC_LEFT
    call OSAPI_KEY_DOWN
    jnc .r1
    dec bl
.r1:
    mov al, KSC_RIGHT
    call OSAPI_KEY_DOWN
    jnc .r2
    inc bl
.r2:
    mov al, [cs_wasroll]
    mov [cs_wasroll], bl
    or bl, bl
    jnz .r3
    or al, al
    jnz .r3
    mov bl, [cs_taproll]
.r3:
    mov byte [cs_taproll], 0
    mov [cs_kroll], bl
    pop bx
    ret

; -----------------------------------------------------------------------------
; cs_steps - how many SIMULATION steps this frame owes, and the wait if none
; out: CX = 1..CS_MAXSTEP.  clobbers AX, BX  (SPEC.md 85.6's tk_steps)
; -----------------------------------------------------------------------------
cs_steps:
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [cs_last]
    jnz .have
    CSSTAGE 10                      ; ...and the KERNEL's tick wait, which is
    mov al, FSXW_TICK               ; a `hlt` (SPEC.md 53.5): a machine stopped
    call OSAPI_FSX_WAIT             ; HERE stopped because nothing woke it
    CSSTAGE 11
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [cs_last]
.have:
    mov [cs_last], bx
    cmp ax, CS_MAXSTEP
    jbe .ok
    mov ax, CS_MAXSTEP
.ok:
    mov cx, ax
    ret

; -----------------------------------------------------------------------------
; cs_clearall - both pages, or the whole shadow, once at entry; and the panel
;               keys stale, so the first frame draws every instrument
; -----------------------------------------------------------------------------
cs_clearall:
    push ds
    pop es
    mov di, cs_pkeys
    mov cx, 2 * 16
    mov ax, 0xFFFF
    cld
    rep stosw
    mov byte [cs_pfirst], 3         ; both pages owe a panel ground
    mov di, cs_rowkind              ; every view row unknown and dirty, so
    mov cx, CS_MAXROW               ; the first frame fills and carries all
    mov al, 0x83
    rep stosb
    mov di, cs_span0                ; ...over empty spans, ONE WORD A ROW and
    mov cx, CS_MAXROW               ; one `rep` per set (cs_r_setup's note)
    mov ax, 0x00FF
    rep stosw
    mov di, cs_span1
    mov cx, CS_MAXROW
    rep stosw
    mov es, [cs_tseg]
    cmp byte [cs_back], CSB_MODEX
    jne .flat
    mov dx, 0x3C4
    mov ax, 0x0F02
    out dx, ax
    xor di, di
    mov cx, 240 * 80 * 2 / 2        ; both pages, in words
    xor ax, ax
    rep stosw
    ret
.flat:
    xor di, di
    mov cx, CS_SHSEG / 2
    xor ax, ax
    rep stosw
    ret

; -----------------------------------------------------------------------------
; cs_input - a bracket dispatches no events, so this is the whole input model
;            (SPEC.md 53.1, 85.6's two readers)
; -----------------------------------------------------------------------------
cs_input:
    CSSTAGE 8
    push ax
    mov byte [cs_hzhold], 0         ; ONCE A FRAME (SPEC.md 88.7.5.2): a
                                    ; capture holds its axis for the rest of
                                    ; the frame it happened in and no longer
    push bx
    push cx
    push dx
.keys:
    mov ah, 1
    int 0x16
    jz .held
    xor ah, ah
    int 0x16
    or al, al                       ; --- AN EXTENDED KEY: AL is 0 and AH the
    jnz .chars                      ; scan code, which is where the ARROWS are
    jmp .ext                        ; (a short jump will not reach it)
.chars:
    cmp al, 27                      ; Esc
    je .quit
    cmp al, 'f'
    je .quit
    cmp al, 'F'
    je .quit
    cmp al, 'p'
    je .pause
    cmp al, 'P'
    je .pause
    cmp al, 'r'
    je .reset
    cmp al, 'R'
    je .reset
    cmp al, 'm'
    je .mute
    cmp al, 'M'
    je .mute
    cmp al, 'b'                     ; THE BRAKE, A LATCH (SPEC.md 88.7.10.1)
    je .brake
    cmp al, 'B'
    je .brake
    call cs_hotkey                  ; --- the settings, in flight (88.13.5)
    jnc .keys
    jmp .keys
.ext:
    ; --- A STICK PRESS, LATCHED (SPEC.md 88.7.5.2). This is Arkanoid's
    ; answer and it is the half Clear Skies did not have: int 16h is an
    ; EVENT, held in the BIOS type-ahead buffer, so a press cannot be missed
    ; however brief - where OSAPI_KEY_DOWN is a LEVEL read and a press that
    ; came and went between two polls never happened. The level read is what
    ; sees a HOLD, which int 16h cannot (no key-up, and a typematic delay
    ; before the repeats); the two are complements and the aeroplane wants
    ; both. cs_stick spends the latch on the next tick it finds the key up.
    mov bl, -1
    cmp ah, KSC_LEFT
    je .taproll
    mov bl, 1
    cmp ah, KSC_RIGHT
    je .taproll
    mov bl, -1
    cmp ah, KSC_UP
    je .tappitch
    mov bl, 1
    cmp ah, KSC_DOWN
    je .tappitch
    call cs_hotkeyx                 ; --- the settings, in flight (88.13.5)
    jmp .keys
.tappitch:
    mov [cs_tappitch], bl
    jmp .keys
.taproll:
    mov [cs_taproll], bl
    jmp .keys
.mute:
    xor byte [cs_sound], 1
    jmp .keys
.brake:
    xor byte [cs_kbrake], 1         ; ...and the panel says which (88.7.10.1)
    jmp .keys
.reset:
    call cs_reset
    jmp .keys
.pause:
    xor byte [cs_pause], 1
    mov byte [cs_msg], CSG_PAUSED
    cmp byte [cs_pause], 0
    jne .quiet
    mov byte [cs_msg], CSG_NONE
    jmp .keys
.quiet:                             ; ...AND A PAUSED AEROPLANE IS SILENT
                                    ; (SPEC.md 88.8.1). cs_sound_step is
                                    ; inside the sim loop, which a pause
                                    ; skips, so the engine tone raised before
                                    ; it simply held for as long as the pause
                                    ; did. Silencing it here rather than
                                    ; testing [cs_pause] once a frame costs
                                    ; nothing per frame, and the first tick
                                    ; after the pause puts it back: [cs_tone]
                                    ; is 0, so cs_sound_step's own compare
                                    ; sees a change and plays it
    xor ax, ax
    mov [cs_tone], ax
    xor cx, cx
    mov dl, 0x40
    call OSAPI_SND_TONE
    jmp .keys
.quit:
    mov byte [cs_quit], 1
    jmp .done

.held:                              ; THE HELD KEYS ARE LATCHED HERE AND SPENT
                                    ; ON THE TICK (SPEC.md 85.6.4). The two
                                    ; STICK axes are NOT read here: cs_stick
                                    ; is the sim loop's now (88.7.5.1) and
                                    ; calling it here as well would spend the
                                    ; tap latch a tick BEFORE the first
                                    ; cs_step, which is the tap going missing
                                    ; (88.7.5.2)
    xor bl, bl
    mov al, CS_KA
    call OSAPI_KEY_DOWN
    jnc .y1
    dec bl
.y1:
    mov al, CS_KD
    call OSAPI_KEY_DOWN
    jnc .y2
    inc bl
.y2:
    mov [cs_kyaw], bl
    xor bl, bl
    mov al, CS_KS
    call OSAPI_KEY_DOWN
    jnc .t1
    dec bl
.t1:
    mov al, CS_KW
    call OSAPI_KEY_DOWN
    jnc .t2
    inc bl
.t2:
    mov [cs_kthr], bl
    ; B IS NOT HERE ANY MORE (SPEC.md 88.7.10.1): the brake is a LATCH the
    ; typed reader toggles, beside P, R and M. The field could not tell a
    ; held B from no B at all - "no difference holding, tapping, or pressing
    ; once" - and a level read gives a pilot nothing to look at, where a
    ; latch puts BRAKES ON on the glass.
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_scrclear - the whole framebuffer, black (SPEC.md 88.13.4)
;
; cs_clearall zeroes the SHADOW, and the blit copies only the view's byte
; columns out of it - so shrinking the view leaves whatever the larger one
; had put in the pixels BESIDE it, which is a band of last size's ground
; standing either side of the picture until the mode is set again. Mode X
; needs none of this: its pages are the framebuffer and cs_clearall has
; already cleared both.
; -----------------------------------------------------------------------------
cs_scrclear:
    cmp byte [cs_back], CSB_MODEX
    je .out
    push ax
    push cx
    push di
    push es
    mov es, [cs_fsi + FSI_SEG]
    xor di, di
    mov cx, 0x4000 / 2              ; THE WHOLE WINDOW, not stride x rows: a
    cmp byte [cs_back], CSB_HERC    ; Hercules page is four interleaved banks
    jne .go                         ; of 8KB and only 7,830 bytes of each are
    mov cx, 0x8000 / 2              ; rows, so the arithmetic leaves the tail
.go:                                ; of the last bank standing
    xor ax, ax
    cmp byte [cs_back], CSB_C160    ; BLACK IS NOT ZERO ON THE TEXT HACK
    jne .z                          ; (88.15.2): a cell is a half block on a
    mov ax, CS_C160_CH              ; black attribute, and zeroing the screen
.z:                                 ; takes the CHARACTERS out - after which
    cld                             ; nothing the blit lays in the attribute
    rep stosw                       ; bytes can be seen at all
    pop es
    pop di
    pop cx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; cs_hotkey - a settings key inside the bracket (SPEC.md 88.13.5)
; in:  AL = the character; out: CF = 1 it was one of ours. Clobbers AX, BX, CX
;
;   -/+   the view's Size, down and up - kept as the ALTERNATE to F3, which
;         is the one setting where a player wants to say which way
;
; A key that changes what is DRAWN costs the next frame and nothing after it,
; so it is one store. Size is the exception - the view's geometry, its byte
; columns, the projection tables and the span sets all derive from it - so
; that one re-runs the raster's own setup, which is idempotent and reuses the
; shadow claim it already holds (88.13.4). Neither costs a cycle while
; flying: no frame reads a setting more than the frame it draws.
; -----------------------------------------------------------------------------
cs_hotkey:
    cmp al, '-'
    je .zdown
    cmp al, '_'
    je .zdown
    cmp al, '+'
    je .zup
    cmp al, '='
    je .zup
    jmp short .no
.zdown:
    mov al, [cs_setsize]
    or al, al
    jz .yes
    dec al
    jmp short .size
.zup:
    mov al, [cs_setsize]
    cmp al, CSZ_FULL
    jae .yes
    inc al
.size:
    mov [cs_setsize], al
    call cs_r_setup                 ; the whole geometry follows the size
    call cs_clearall                ; ...and what was on the glass is stale
    call cs_scrclear                ; ...INCLUDING THE PIXELS BESIDE THE VIEW
.yes:
    mov cl, 2                       ; ...and the strip says which and what to
    call cs_toast                   ; (88.13.8), at the end of the ladder too:
    stc                             ; "SIZE: FULL" is the answer to a `+` that
    ret                             ; could not go up as much as to one that did
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; cs_hotkeyx - the settings' own keys, F1 to F5 (SPEC.md 88.13.5)
; in:  AH = the scan code of an EXTENDED key (AL was 0)
; out: CF = 1 it was one of ours. Clobbers AX, CX
;
; F-keys and not the digits: a flight simulator's number row is where a
; player expects to find something else, and these are keys nobody reaches
; for by accident. They arrive as int 16h EXTENDED codes - AL zero, AH the
; scan code - so this hangs off cs_input's `.ext` arm beside the arrows and
; not off the character one.
;
; ONE KEY A SETTING AND NOT ONE A VALUE. It was ten - a key per rung of the
; detail ladder, a key per rung of the draw distance - and that runs out: the
; detail ladder is five rungs since 88.13.1.1 and the draw distance is four
; since 88.13.2.1, which is eleven keys for four settings before the fills.
; Each of these three steps its ladder ONE UP and round at the top, and what
; a player has lost by not being able to jump straight to a rung they get
; back on the Settings page, which is where a considered choice belongs.
;
;   F1  Detail Level    F2  Draw Distance   F3  Size (also -/+)
;   F4  Terrain fill    F5  Buildings fill  - each a toggle
;
; Every one of them raises a TOAST (88.13.8), which is what makes a cycling
; key usable at all: a key that steps a ladder is no good if the glass does
; not say where the ladder now is.
; -----------------------------------------------------------------------------
CSK_F1     equ 0x3B                 ; ...and F2..F5 follow it
cs_hotkeyx:
    cmp ah, CSK_F1
    jb .no
    cmp ah, CSK_F1 + 4
    ja .no
    push bx
    push si
    mov al, ah
    sub al, CSK_F1                  ; 0..4
    mov cl, al
    xor ch, ch
    cmp al, 3
    jae .fill
    ; --- F1 F2 F3: one step up the ladder, and round at the top -----------
    mov bx, cx
    shl bx, 1
    mov si, [cs_toastset + bx]      ; the setting's own byte...
    mov bx, cx
    mov al, [cs_cycn + bx]          ; ...and how many rungs it has
    mov ah, [si]
    inc ah
    cmp ah, al
    jb .set
    xor ah, ah
.set:
    mov [si], ah
    call cs_toast                   ; FIRST, WHILE CL STILL SAYS WHICH: every
                                    ; call below spends CX - cs_skipclr's own
                                    ; `loop` leaves it ZERO, so a toast raised
                                    ; after one says "Detail Level" whatever
                                    ; key was pressed. The toast reads the
                                    ; setting's byte, which is already written
    cmp cl, 2
    je .size
    call cs_skipclr                 ; DETAIL *AND* DRAW DISTANCE: both change
    jmp short .done                 ; which objects are in range, so one the
                                    ; cull dropped for N ticks has to be
                                    ; looked at again (88.5.2). The old F8..F10
                                    ; did not, so raising the draw distance
                                    ; left the far half of the world absent
                                    ; for up to CS_FAR/16 ticks
.size:
    call cs_r_setup                 ; the whole geometry follows the size
    call cs_clearall                ; ...and what was on the glass is stale
    call cs_scrclear                ; ...INCLUDING THE PIXELS BESIDE THE VIEW
    jmp short .done
.fill:                              ; F4 Terrain, F5 Buildings - each toggled
    mov al, 1
    push cx
    sub cl, 3
    shl al, cl
    pop cx
    xor [cs_setfill], al
    call cs_toast
.done:
    pop si
    pop bx
    stc
    ret
.no:
    clc
    ret

cs_cycn: db CSBL_HIGH + 1, CSL_ULTRA + 1, CSZ_FULL + 1

; -----------------------------------------------------------------------------
; cs_render - one frame (SPEC.md 88.8)
; -----------------------------------------------------------------------------
cs_render:
    inc word [cs_frames]            ; the only instrumentation in here, and
    CSSTAGE 1                       ; tests/skies.py is what reads it
    call cs_r_begin
    CSSTAGE 2
    call cs_matrix
    mov ax, [cs_px+1]               ; the eye, in whole metres
    mov [cs_ex], ax
    cwd
    mov [cs_exh], dx
    mov ax, [cs_pz+1]
    mov [cs_ez], ax
    cwd
    mov [cs_ezh], dx
    mov ax, [cs_py+1]
    mov si, [cs_plane]
    add ax, [si + CSP_EYE]
    mov [cs_ey], ax
    CSSTAGE 3
    call cs_skyground
    CSSTAGE 4
    call cs_scene
    cmp byte [cs_state], CS_ST_CRASH ; ...and the WINDSHIELD, over the last
    jne .whole                       ; picture the pilot got (SPEC.md 88.7.11)
    call cs_crackle
.whole:
    CSSTAGE 5
    call cs_panel
    CSSTAGE 6
    call cs_r_end
    CSSTAGE 7                       ; a frame that FINISHED (SPEC.md 88.14)
%ifdef CSDIAG
    push ax
    mov ax, [cs_dtick]
    mov [cs_dframe], ax
    pop ax
%endif
    ret

; =============================================================================
; The panel (SPEC.md 88.9): items keyed on what they show, redrawn on change
; =============================================================================
CS_PI_SPD  equ 0
CS_PI_ALT  equ 1
CS_PI_HDG  equ 2
CS_PI_ADI  equ 3                    ; the attitude indicator (88.9.2)
CS_PI_THR  equ 4
CS_PI_STATE equ 5
CS_PI_BAR  equ 6
CS_PI_MSG  equ 7
CS_PI_VS   equ 8                    ; the VARIOMETER (SPEC.md 88.7.6) - a
                                    ; cockpit that has none puts its cell at
                                    ; 0,0 and cs_d_vs returns at once
CS_PI_N    equ 9

; the layout is the cockpit's (cspanel.inc): x at 320 wide through
; cs_hscalex, rows below the view

; =============================================================================
; THE WINDSHIELD (SPEC.md 88.7.11) - drawn over the view for as long as the
; crash picture stands, which is CS_CRASHT ticks before cs_reset
;
; A TABLE AND NOT A DRAWING. Eight cracks radiate from a hub left of centre,
; each with one kink, and the kinks are joined into a ring - which is what
; laminated glass actually does and what a generator of random spokes does
; not. The coordinates are 1/256 of the view each way, so the same 96 bytes
; fit Mode X's 320x150, Hercules' 640x112 and every Size rung of both, and
; the hub is off-centre because a symmetrical star reads as a decoration.
; =============================================================================
CS_NCRACK equ 24

cs_crackle:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [cs_pinview]
    push ax
    mov al, CSI_MARK
    call cs_setink
    mov byte [cs_pinview], 0        ; every segment CLIPPED: the ring reaches
    mov si, cs_crackt               ; the edges on a wide view
    mov cx, CS_NCRACK
.s:
    push cx
    push si
    mov al, [si]                    ; ...and the SECOND row last, because the
    call cs_crx                     ; scale is a mul and a mul writes DX
    push ax
    mov al, [si+1]
    call cs_cry
    push ax
    mov al, [si+2]
    call cs_crx
    push ax
    mov al, [si+3]
    call cs_cry
    mov dx, ax
    pop cx
    pop bx
    pop ax
    call cs_seg
    pop si
    pop cx
    add si, 4
    loop .s
    pop ax
    mov [cs_pinview], al
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_crx / cs_cry - AL = 1/256 of the view; out AX = the x or the row in it.
; Clobbers DX.
cs_crx:
    xor ah, ah
    mul word [cs_ww]
    mov al, ah
    mov ah, dl
    add ax, [cs_wx0]
    ret
cs_cry:                             ; ...and 128 is the view's MIDDLE, scaled
    xor ah, ah                      ; by the WIDTH on this axis too, so the
    sub ax, 128                     ; star stays ROUND on a letterbox view and
    imul word [cs_ww]               ; a short one shows the middle of it. Both
    mov al, ah                      ; axes by the height instead squashed the
    mov ah, dl                      ; whole pattern into a 50-row band on
    mov bx, [cs_wh]                 ; Hercules and it read as scratches
    shr bx, 1
    add ax, bx
    ret

cs_crackt:                          ; the impact star, six cracks out of it,
                                    ; and what the glass did in between
    db 104,110, 120,118               ; --- the star round the hub ---
    db 120,118, 122,136
    db 122,136, 108,148
    db 108,148,  92,144
    db  92,144,  86,126
    db  86,126, 104,110
    db 104,110,  96, 70               ; --- out of it, each with one kink ---
    db  96, 70,  86, 42
    db 120,118, 156, 94
    db 156, 94, 200, 60
    db 122,136, 176,144
    db 176,144, 250,150
    db 108,148, 128,198
    db 128,198, 150,254
    db  92,144,  70,190
    db  70,190,  44,254
    db  86,126,  34,124
    db  34,124,   2,114
    db  96, 70, 156, 94              ; --- and what ran BETWEEN three of them:
    db 176,144, 128,198             ;     a whole ring is a spider's web, and
    db  70,190,  34,124              ;     glass does not make one
    db 156, 94, 150,130              ; --- three splinters off the cracks ---
    db  70,190, 108,180
    db  34,124,  66,104

; -----------------------------------------------------------------------------
; cs_panel - every item whose key moved, and the ground under them the first
;            time a target is drawn on
; -----------------------------------------------------------------------------
cs_panel:
    mov al, [cs_par]
    and al, 1
    cmp byte [cs_back], CSB_MODEX
    je .pg
    xor al, al                      ; one target: page 0's keys
.pg:
    mov [cs_ppage], al
    ; --- the instruments read the aeroplane every CS_PRATE ticks, not every
    ;     frame (88.9.1): an altitude in feet changes every tick in a climb,
    ;     and nine glyphs a change at the frame rate was a tenth of the frame
    mov byte [cs_pgate], 1
    mov ax, [cs_last]
    sub ax, [cs_plast]
    cmp ax, CS_PRATE
    jae .read
    mov byte [cs_pgate], 0
    jmp short .first
.read:
    mov ax, [cs_last]
    mov [cs_plast], ax
.first:
    mov al, [cs_ppage]              ; (the gate spent AX)
    mov ah, 1
    mov cl, al
    shl ah, cl
    test [cs_pfirst], ah
    jz .items
    mov byte [cs_pgate], 1          ; the first frame draws everything
    not ah                          ; this target's first frame: the cockpit
    and [cs_pfirst], ah
    call cs_pface                   ; (88.9.2)
    push ds                         ; ...and every key on this page stale
    pop es
    mov al, [cs_ppage]
    xor ah, ah
    mov cl, 5
    shl ax, cl
    add ax, cs_pkeys
    mov di, ax
    mov cx, 16
    mov ax, 0xFFFF
    cld
    rep stosw
.items:
    xor bx, bx
.i:
    push bx
    call cs_pitem
    pop bx
    inc bx
    cmp bx, CS_PI_N
    jb .i
    ret

; -----------------------------------------------------------------------------
; cs_pitem - BX = an item: SAMPLE it on the gate, and PAINT it on any page
;            that has not got that value on it yet (SPEC.md 88.9.4)
;
; The two halves are separate on purpose and were not, which is what made a
; Mode X altimeter read 1,683 feet one frame and 1,666 the next, for ever.
; The gate is in TICKS and the page is the frame's PARITY, so a gate frame
; lands on whichever page happens to be current and the other one keeps
; whatever it last had - and with the two rates near each other the same
; page can win several gates in a row. Both pages then hold real readings,
; taken seconds apart, and the flip shows them alternately.
;
; So the gate now decides when the aeroplane is READ, into one latched set
; both pages share, and each page's own cache decides whether it still needs
; painting. A change is drawn twice - once per page, on consecutive frames -
; which is what a double buffer costs and what it was quietly not paying.
; One target (Hercules, CGA) is unaffected: cs_ppage is always 0 there, and
; this was never visible on either.
; -----------------------------------------------------------------------------
cs_pitem:
    mov [cs_pcur], bx
    ; --- A MESSAGE OWNS THE WHOLE ONE-LINE STRIP (SPEC.md 88.15.6) ----------
    ; Twenty cells hold three readings OR one sentence and not both, so on
    ; that panel the readings stand down while a message is up - here, before
    ; the key is even sampled, so their caches keep the values they were
    ; showing and cs_d_msg has only to STALE them when the message goes.
    cmp byte [cs_back], CSB_C160
    jne .live
    cmp bx, CS_PI_ALT               ; the SPEED is not one of them: it sits in
    jb .live                        ; the five cells the message starts after,
    cmp bx, CS_PI_HDG               ; which is what the ground roll needs to
    ja .live                        ; read while the take-off prompt is up
    cmp byte [cs_msg], CSG_NONE
    jne .gone
.live:
    ; --- AN ITEM THIS COCKPIT HAS NOT GOT IS (0, 0) (SPEC.md 88.9.5.1) ------
    ; ...and only cs_d_vs was testing for it, so a glider's THR - which is
    ; (0, 0) because a sailplane has no throttle - was lettered at cell 0 of
    ; row 0, half of it above the panel's own top edge. It is the same test
    ; for every item, so it belongs here rather than nine times over.
    cmp bx, CS_PI_ADI               ; (the attitude indicator is not a cell at
    je .there                       ; all: it draws from CSK_ADCX)
    call cs_pcell
    or bx, bx
    jnz .there
    or dx, dx
    jz .gone
.there:
    mov bx, [cs_pcur]
    cmp byte [cs_pnow + bx], 0      ; THE ITEMS THAT NEVER WAIT (88.9.1.1) -
    jne .now                        ; a table and not a compare chain, which
                                    ; on an 8088 is half the BYTES and so
                                    ; half the time, and stays that whatever
                                    ; is added to it
    cmp byte [cs_pgate], 0
    je .paint                       ; between gates the latched value stands
.now:
    call cs_pkey                    ; AX = what it would show now...
    mov bx, [cs_pcur]
    shl bx, 1
    mov [cs_pshow + bx], ax         ; ...latched, ONE set for both pages
.paint:
    mov bx, [cs_pcur]
    shl bx, 1
    mov ax, [cs_pshow + bx]
    mov [cs_pkeyv], ax
    mov al, [cs_ppage]
    xor ah, ah
    mov cl, 5
    shl ax, cl                      ; 32 bytes a page of keys
    add bx, ax
    mov ax, [cs_pkeyv]
    cmp ax, [cs_pkeys + bx]
    je .same
    mov [cs_pkeys + bx], ax
    mov bx, [cs_pcur]
    shl bx, 1
    jmp [cs_pdraw + bx]
.same:
.gone:
    ret

; cs_pkey - the key of item [cs_pcur]: the value it would show
cs_pkey:
    mov bx, [cs_pcur]
    shl bx, 1
    jmp [cs_pkeyp + bx]

cs_pkeyp:  dw cs_k_spd, cs_k_alt, cs_k_hdg, cs_k_adi, cs_k_thr, cs_k_state
           dw cs_k_thr, cs_k_msg, cs_k_vs
cs_pdraw:  dw cs_d_spd, cs_d_alt, cs_d_hdg, cs_d_adi, cs_d_thr, cs_d_state
           dw cs_d_bar, cs_d_msg, cs_d_vs
; ...and which of them are read EVERY FRAME rather than at 88.9.1's gate: a
; stall or a crash has to show at once, and the THROTTLE and its bar are the
; only USER INPUT on the panel, so a frame's lag there reads as the machine
; not listening rather than as an instrument settling. The four readings that
; are the aeroplane's own - speed, altitude, heading, vertical speed - keep
; the gate, because those change every tick in a climb and nine opaque glyphs
; a change at the frame rate is a tenth of the frame (88.9.1.1)
;              SPD ALT HDG ADI THR STATE BAR MSG  VS
cs_pnow:   db    0,  0,  0,  0,  1,    1,  1,  1,  0

; --- the keys -----------------------------------------------------------------
cs_k_spd:                           ; knots: v(16.7) x 1.944 / 128
    mov ax, [cs_spd]
    mov bx, 996
    mul bx
    mov ax, dx
    ret
cs_k_alt:                           ; feet: metres x 3.281
    mov ax, [cs_py+1]
    mov bx, 210
    mul bx
    mov cl, 6
    call cs_shr32
    ret
cs_k_hdg:                           ; degrees: hdg x 360 / 65536
    mov ax, [cs_hdg]
    mov bx, 360
    mul bx
    mov ax, dx
    ret
cs_k_thr:
    mov ax, [cs_thr]
    ret
cs_k_state:
    mov al, [cs_state]
    mov ah, [cs_kbrake]             ; BIT 2 the brake (88.7.10.1), bit 1 the
    shl ah, 1                       ; water, bit 0 the stall: every one of
    or ah, [cs_onwater]             ; them changes the LINE, so every one has
    shl ah, 1                       ; to change the key or the strip is never
    or ah, [cs_stall]               ; repainted (88.7.7)
    ret
cs_k_msg:
    mov al, [cs_msg]
    mov ah, [cs_crashwhy]           ; the low byte of the string's address
    cmp al, CSG_TOAST               ; ...and a TOAST from the toast before it
    jne .out                        ; by its own count (88.13.8): two of them
    mov ah, [cs_toastn]             ; are the same cs_msg and the same
.out:                               ; cs_crashwhy, so the panel's key never
    ret                             ; changed and the strip never repainted -
                                    ; a second setting changed under a line
                                    ; still naming the first

; cs_shr32 - DX:AX >>= CL (CL < 16). Clobbers CX.
cs_shr32:
    or cl, cl
    jz .o
.s:
    shr dx, 1
    rcr ax, 1
    dec cl
    jnz .s
.o:
    ret

; --- the draws: every one letters opaquely at a fixed width -------------------
; in: [cs_pkeyv] = the key just cached

; cs_pnum - AX = a value, CL = digits, SI = the label; letters "LABEL nnn" at
; DX = the row, BX = the x at 320 wide
; cs_pnum - AX = the value, CL = digits, SI = the label, BX/DX = the cell:
; THE DIGITS ONLY, four cells along, because cs_plabels drew the label with
; the cockpit and it has not changed since (SPEC.md 88.9.4). SI is taken and
; ignored so the callers read as they did; cs_pnuml is the form that letters
; the label too, which is the variometer's, its label being its sign.
cs_pnum:
    push ax                         ; THE LABEL'S OWN CELLS along, and a cell
    push cx                         ; is not a layout unit (SPEC.md 88.9.5):
    push dx                         ; the old `add bx, 4 * 8` was eight cells
    mov ax, [cs_plabw]              ; on Hercules, which is the gap between
    call cs_pcells                  ; every label and its number in the
    add bx, ax                      ; photographs. Four for 'SPD ', two for
                                    ; the one-line strip's 'S ' (88.15.5)
    pop dx
    pop cx
    pop ax
    push ax
    mov di, cs_numbuf
    pop ax
    call cs_utoa
    mov si, cs_numbuf
    jmp cs_pline

cs_pnuml:
    push si
    push ax
    mov di, cs_numbuf
    call cs_strcpy                  ; the label...
    pop ax
    call cs_utoa                    ; ...and the number after it
    pop si
    mov si, cs_numbuf
    jmp cs_pline

; cs_pline - SI = a string, BX = the x at 320 wide, DX = the row (below the
; view); letters it in the panel's ink
cs_pline:
    mov al, CSI_PFG
    call cs_setink
cs_pline_ink:
    push dx                         ; cs_hscalex multiplies, and DX is the row
    mov ax, bx
    call cs_hscalex
    pop dx
    mov cx, ax
    add dx, [cs_pany]
    call cs_text
    ret

; cs_hscalex - AX = an x at 320 wide; out AX = it in this box. Clobbers DX.
cs_hscalex:
    cmp word [cs_hsx], 8
    je .same
    imul word [cs_hsx]
    sar ax, 1
    sar ax, 1
    sar ax, 1
.same:
    ret

; cs_strcpy - SI -> DI, up to and excluding the NUL; DI left on it
cs_strcpy:
    lodsb
    or al, al
    jz .d
    mov [di], al
    inc di
    jmp short cs_strcpy
.d:
    ret

; -----------------------------------------------------------------------------
; cs_strcpyu - cs_strcpy, UPPER-CASED (SPEC.md 88.13.8)
;
; The toast is lettered out of the SETTINGS PAGE'S OWN strings - 'Detail
; Level', 'Ultra', 'Buildings' - so that the two can never come to disagree
; about what a rung is called; the panel writes in capitals. Ten bytes buys
; that instead of a second set of names to keep in step.
; -----------------------------------------------------------------------------
cs_strcpyu:
    lodsb
    or al, al
    jz .d
    cmp al, 'a'
    jb .put
    cmp al, 'z'
    ja .put
    sub al, 32
.put:
    mov [di], al
    inc di
    jmp short cs_strcpyu
.d:
    ret

; -----------------------------------------------------------------------------
; cs_toast - CL = which setting changed; its name and its new value on the
;            message strip for CS_TOASTT ticks (SPEC.md 88.13.8)
;
; The three ladders are 0, 1, 2 and the two fills 3 and 4, which is the order
; cs_cycset and cs_cycn use. Everything the toast letters is a pointer this
; file already had: the page's own label for the setting, and either the
; page's own name for the value or ON/OFF for a fill.
;
; It does not paint. cs_msg is what the panel reads once a frame (88.9.9), so
; a toast is one byte and the strip redraws itself; what expires it is
; cs_toaststep, off the SAME tick count the flight model is stepped with, so
; it lasts the same second and a half on a machine drawing 3 frames a second
; and on one drawing 18.
; -----------------------------------------------------------------------------
cs_toast:
    push ax
    push bx
    push cx
    push si
    push di
    xor ch, ch
    mov bx, cx
    shl bx, 1
    mov si, [cs_toastlbl + bx]
    mov di, cs_toastbuf
    call cs_strcpyu
    mov word [di], ': '             ; NOT `stosb`: ES is the KERNEL'S in a
    add di, 2                       ; package (SPEC.md 20), so a store through
                                    ; it lands in the wrong segment entirely
    cmp cl, 3
    jae .fill
    mov si, [cs_toastset + bx]      ; the setting's byte...
    mov al, [si]
    xor ah, ah
    shl ax, 1
    mov si, [cs_toastval + bx]      ; ...and the page's own list of names
    add si, ax
    mov si, [si]
    jmp short .val
.fill:
    mov al, cl                      ; the fill BIT this one owns: Terrain 1,
    sub al, 3                       ; Buildings 2 (88.13.3)
    mov cl, al
    mov al, 1
    shl al, cl
    test [cs_setfill], al
    mov si, cs_g_wire               ; a cleared fill bit is WIREFRAME and not
    jz .val                         ; gone (88.13.3), so the toast says which
    mov si, cs_g_fill               ; of the two it is
.val:
    call cs_strcpyu
    mov byte [di], 0
    mov al, [cs_msg]                ; ...and what the strip goes back to
    cmp al, CSG_TOAST               ; (a toast on a toast keeps the first
    je .arm                         ; answer, not itself)
    mov [cs_toastwas], al
.arm:
    inc byte [cs_toastn]            ; the panel's key: see cs_k_msg
    mov byte [cs_msg], CSG_TOAST
    mov byte [cs_toastt], CS_TOASTT
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_toaststep - CX ticks have gone by; expire the toast (SPEC.md 88.13.8)
;
; ON THE FRAME'S OWN TICK COUNT and not inside cs_step, because cs_step is
; the thing a PAUSE stops: a toast raised while paused would otherwise sit
; there for ever. Clobbers nothing. It restores only a strip that is STILL
; the toast, so a stall or a crash raised in the meantime keeps the glass.
; -----------------------------------------------------------------------------
cs_toaststep:
    cmp byte [cs_toastt], 0
    je .out
    push ax
    mov al, [cs_toastt]
    sub al, cl
    ja .keep
    xor al, al
    cmp byte [cs_msg], CSG_TOAST
    jne .keep
    mov ah, [cs_toastwas]
    mov [cs_msg], ah
.keep:
    mov [cs_toastt], al
    pop ax
.out:
    ret

cs_toastlbl: dw cs_s_lbld, cs_s_llod, cs_s_lsize, cs_s_terr, cs_s_bld
cs_toastset: dw cs_setbld, cs_setlod, cs_setsize
cs_toastval: dw cs_i_bld, cs_i_lod, cs_i_size
cs_g_fill:   db 'Fill', 0
cs_g_wire:   db 'Wire', 0

cs_d_spd:
    mov bx, CS_PI_SPD
    call cs_pcell
    mov ax, [cs_pkeyv]
    mov cl, 3
    mov si, cs_s_spd
    jmp cs_pnum
cs_d_alt:
    mov bx, CS_PI_ALT
    call cs_pcell
    mov ax, [cs_pkeyv]
    mov cl, 5
    mov si, cs_s_alt
    jmp cs_pnum
cs_d_hdg:
    mov bx, CS_PI_HDG
    call cs_pcell
    mov ax, [cs_pkeyv]
    mov cl, 3
    mov si, cs_s_hdg
    jmp cs_pnum
cs_k_vs:                            ; the VARIOMETER: tenths of a metre a
    mov ax, [cs_vs]                 ; second, and its SIGN in the high byte -
    mov bx, 10                      ; the key has to change when the needle
    imul bx                         ; crosses zero or the cache holds the
    mov bx, 128                     ; wrong label (88.7.6)
    idiv bx
    ret

cs_d_vs:
    mov bx, CS_PI_VS
    call cs_pcell
    or bx, bx
    jz .none                        ; this cockpit has no variometer
    mov ax, [cs_pkeyv]
    mov si, cs_s_up
    or ax, ax
    jns .up
    neg ax
    mov si, cs_s_dn
.up:
    cmp ax, 999
    jle .n
    mov ax, 999
.n:
    mov cl, 3
    jmp cs_pnuml                    ; the vario letters its own label: UP or
.none:                              ; DN is the reading's SIGN (88.9.4)
    ret

cs_d_thr:
    mov bx, CS_PI_THR
    call cs_pcell
    mov ax, [cs_pkeyv]
    mov cl, 3
    mov si, cs_s_thr
    jmp cs_pnum
cs_d_state:
    mov ax, [cs_pkeyv]
    mov si, cs_s_ground
    cmp al, CS_ST_GROUND
    jne .air
    test ah, 4                      ; BIT 2 IS THE BRAKE (88.7.10.1): a latch
    jz .dry                         ; the pilot can SEE, which is the whole
    mov si, cs_s_brakes             ; reason it stopped being a level read
    jmp short .s
.dry:
    cmp byte [cs_onwater], 0
    je .s
    mov si, cs_s_water
    jmp short .s
.air:
    mov si, cs_s_crash
    cmp al, CS_ST_CRASH
    je .s
    mov si, cs_s_flying
    test ah, 1                      ; BIT 0 IS THE STALL (88.9.4.2) and the
    jz .s                           ; cs_k_state packs cs_onwater in beside it
    mov si, cs_s_stalled            ; so the strip repaints when a hull leaves
                                    ; the water, and testing the whole byte
                                    ; read an amphibian AIRBORNE OFF THE WATER
                                    ; - which is how every A5 flight starts -
                                    ; as stalled, for the whole flight
.s:
    push si
    mov bx, CS_PI_STATE
    call cs_pcell
    pop si
    jmp cs_pline
cs_d_bar:                           ; the throttle bar: 0..100 in its slot
    mov bx, CS_PI_BAR
    call cs_pcell                   ; BX = its left end, DX = its top row
    or bx, bx
    jz .none                        ; a glider has no throttle to draw (88.7.6)
    mov [cs_pbarx], bx
    add dx, [cs_pany]
    mov [cs_pbary], dx
    call cs_ckrec
    mov ax, [si + CSK_BARW]         ; in CELLS, like every other width on the
    call cs_pcells                  ; panel (SPEC.md 88.9.5)
    mov [cs_pbarw], ax
    mov al, CSI_PBG                 ; the whole slot in the ground first...
    call cs_setink
    mov ax, [cs_pbarx]
    add ax, [cs_pbarw]
    call cs_hscalex
    mov cx, ax
    mov ax, [cs_pbarx]
    call cs_hscalex
    mov bx, [cs_pbary]
    mov dx, bx
    add dx, 5
    call cs_prect
    mov ax, [cs_pkeyv]              ; ...then the lit part: 100 is the width
    or ax, ax
    jz .none
    mul word [cs_pbarw]
    mov bx, 100
    div bx
    add ax, [cs_pbarx]
    call cs_hscalex
    mov cx, ax
    mov ax, [cs_pbarx]
    call cs_hscalex
    cmp ax, cx
    jg .none
    mov al, CSI_PHI
    call cs_setink
    mov ax, [cs_pbarx]
    call cs_hscalex
    mov bx, [cs_pbary]
    mov dx, bx
    add dx, 5
    call cs_prect
.none:
    ret

cs_d_msg:                           ; the message line, padded to 40 cells
    mov di, cs_msgbuf
    mov al, [cs_msg]
    xor ah, ah
    mov bx, ax
    shl bx, 1
    add bx, [cs_msgtabp]            ; the strings this panel can fit: a
    mov si, [bx]                    ; cockpit's forty cells, or the strip's
    call cs_strcpy                  ; twenty (88.15.6)
    cmp byte [cs_msg], CSG_CRASH
    jne .pad
    cmp byte [cs_back], CSB_C160
    je .pad                         ; ...and no room for WHAT was hit
    mov si, [cs_crashwhy]
    call cs_strcpy
.pad:
    mov byte [di], 0                ; NO PADDING TO 40 CELLS: the message is
                                    ; lettered on its own length now, so an
                                    ; empty one is no bar at all (88.9.9)
    mov al, CSI_PHI
    cmp byte [cs_msg], CSG_CRASH
    je .ink
    cmp byte [cs_msg], CSG_STALL
    je .ink
    mov al, CSI_PFG
.ink:
    mov [cs_msgink], al
    ; --- THE STRIP, ERASED ON THE FACE (SPEC.md 88.9.9). It used to be a
    ;     window, and a window is painted once with the cockpit and stays:
    ;     a panel with nothing to say carried a white slab across it for
    ;     ever. There is no message window now; what a message gets is the
    ;     opaque cells it letters, which is a bar the size of the message.
    mov al, CSI_PFACE
    call cs_setink
    mov bx, CS_PI_MSG
    call cs_pcell
    add dx, [cs_pany]
    mov [cs_msgy], dx
    mov bx, dx
    sub bx, 2
    mov dx, bx
    add dx, 11
    mov ax, [cs_msgx0]              ; 0 on a cockpit's own strip; cell 6 on the
    mov cx, [cs_vw]                 ; ONE-LINE strip, which keeps the speed
    dec cx                          ; beside the message (88.15.6)
    call cs_prect
    mov si, cs_msgbuf               ; ...and the message, centred on its own
    xor cx, cx                      ; length rather than padded to forty
.len:
    cmp byte [si], 0
    je .have
    inc si
    inc cx
    jmp short .len
.have:
    jcxz .out
    mov ax, cx
    mov cx, 8
    mul cx
    mov cx, [cs_vw]
    sub cx, [cs_msgx0]              ; centred in what is LEFT of the line...
    sub cx, ax
    jg .fits                        ; ...UNLESS IT DOES NOT FIT, and the shift
    xor cx, cx                      ; below is UNSIGNED: one cell too long
.fits:                              ; made the pen 0x7FFC, cs_text's `and cl,
    shr cx, 1                       ; 0xF8` left the high bits, and cs_glyph's
    add cx, [cs_msgx0]              ; own `js .out` then dropped every cell -
    push cx                         ; a whole message that simply vanished
    mov al, [cs_msgink]
    call cs_setink
    pop cx
    mov si, cs_msgbuf
    mov dx, [cs_msgy]
    call cs_text
.out:
    cmp byte [cs_back], CSB_C160    ; --- THE STRIP, HANDED BACK (88.15.6) ---
    jne .done                       ; The erase above took cells 6..19, so the
    cmp byte [cs_msg], CSG_NONE     ; altitude and the heading have to be
    jne .done                       ; lettered again - and in THIS frame, as a
    call cs_plabels                 ; THE LABELS WENT WITH THE READINGS: they
                                    ; are lettered ONCE with the face and the
                                    ; erase above covers the two inside it, so
                                    ; A and H came back as bare numbers until
                                    ; the next target's first frame
    mov bx, CS_PI_ALT               ; message clearing is not a thing to leave
.back:                              ; a blank strip behind. Their caches still
    push bx                         ; hold what they last showed, so staling
    shl bx, 1                       ; them is what makes cs_pitem paint; and
    mov word [cs_pkeys + bx], 0xFFFF ; cs_pitem re-entered from here is safe -
    pop bx                          ; cs_panel reloads the item from ITS stack
    push bx                         ; and cs_d_msg is done with [cs_pcur]
    call cs_pitem
    pop bx
    inc bx
    cmp bx, CS_PI_HDG
    jbe .back
.done:
    ret

cs_s_spd:    db 'SPD ', 0
cs_s_alt:    db 'ALT ', 0
cs_s_hdg:    db 'HDG ', 0
cs_s_thr:    db 'THR ', 0
cs_s_up:     db 'UP  ', 0        ; the variometer's two labels: a needle
cs_s_dn:     db 'DN  ', 0        ; and not a signed number (88.7.6)
cs_s_ground: db 'ON THE GROUND', 0
cs_s_water:  db 'ON THE WATER ', 0
cs_s_brakes: db 'BRAKES ON    ', 0
cs_s_flying: db 'FLYING       ', 0
cs_s_stalled: db 'STALL        ', 0
cs_s_crash:  db 'CRASHED      ', 0

; TWO OF THESE ARE COMPOSED (SPEC.md 88.7.9): the take-off prompt and the tow
; release name a SPEED, and the aeroplane's own record is the only place that
; number lives - 55 knots is the Cessna's, and the jet rotates at 81. They
; cannot both be up at once (an aeroplane either starts on the ground or in
; the air), so one buffer serves them, cs_toastbuf's shape.
cs_msgtab:   dw cs_g_none, cs_promptb, cs_g_stall, cs_g_landed
             dw cs_g_crash, cs_g_paused, cs_g_edge, cs_promptb
             dw cs_g_splash, cs_toastbuf
; ...and the SAME messages in twenty cells, for the one-line strip (88.15.6).
; It is a second table and not a shortened first one because a cockpit panel
; is forty cells wide and has the room: what the strip cannot hold, every
; other backend can, and neither should be cut to the other's width.
cs_msgtab160: dw cs_g_none, cs_promptc, cs_g_s160, cs_g_landed
             dw cs_g_c160, cs_g_paused, cs_g_e160, cs_promptc
             dw cs_g_p160, cs_toastbuf
cs_g_s160:   db 'STALL - NOSE DOWN', 0
cs_g_c160:   db 'CRASHED', 0
cs_g_e160:   db 'TURN BACK', 0
cs_g_p160:   db 'DOWN ON WATER', 0
cs_g_none:   db 0
; opening, suffix - a cockpit's, then the one-line strip's (88.15.6.1)
cs_g_take:   dw cs_g_take0, cs_g_kt, cs_g_take1, cs_g_kt1
cs_g_tow:    dw cs_g_tow0, cs_g_kt, cs_g_tow1, cs_g_kt1
cs_g_take0:  db 'FULL THROTTLE, PULL BACK AT ', 0
cs_g_tow0:   db 'TOW RELEASED - NOSE DOWN FOR ', 0
cs_g_take1:  db 'ROTATE ', 0        ; 'ROTATE 55 KT' - twelve of the fourteen
cs_g_tow1:   db 'DOWN TO ', 0       ; cells the strip lends a message (88.15.6)
cs_g_kt:     db ' KNOTS', 0
cs_g_kt1:    db ' KT', 0           ; ...so 'ROTATE 54 KT' is twelve cells of
                                    ; the fourteen the strip lends a message
cs_g_stall:  db 'STALL - NOSE DOWN, POWER ON', 0
cs_g_landed: db 'LANDED', 0
cs_g_crash:  db 'CRASHED INTO ', 0
cs_g_paused: db 'PAUSED', 0
cs_g_edge:   db 'THE EDGE OF THE WORLD - TURN BACK', 0
cs_g_splash: db 'DOWN ON ', 0    ; ...and the water's own name after it
