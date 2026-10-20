; =============================================================================
; apps/isotron/itsnd.inc - the sounds (SPEC.md 108.14.1)
;
; THE SPEAKER, THROUGH OSAPI_SND_TONE ONLY (SPEC.md 34.3): worker-safe by
; construction, attributed to this instance and released at its teardown, so
; a closed isoTron can never leave a tone droning. No FM, no PCM, no driver:
; every PC this runs on has the speaker and nothing here needs more.
;
;   event         sound                                       priority calls
;   wall tick     110 Hz (165 boosted), CX = 1, every 4th      40h   <= 2 a
;                 player cell, AT MOST ONE A FRAME; windowed         4 cells
;                 its lease ends it at the tick, full screen
;                 an explicit off on the next frame
;   crash         1,400 Hz x 15/16 a tick, 15 entries (to       60h      16
;                 567 Hz, 0.8 s - CRASH's whole length), off
;   round start   440, 440, 880 Hz: 2 ticks on, 1 off (READY)   50h       6
;   tower rises   523, 659, 784 Hz, 3 ticks each (the sweep)    50h       4
;   game over     523, 392, 330, 262 Hz, 4 ticks each           50h       5
;
; EVERY OFF GOES OUT AT 60h: an off obeys the priority compare like an on
; (SPEC.md 34.3), so an off at the tick's 40h could not silence a phrase's
; 50h. And an off is sent only while a tone of OURS may still be sounding
; (it_sndlive, its lease not yet out): a 60h off is no respecter of owners and
; would silence another package's 40h tone.
;
; ONE WRITER, AND IT RUNS AFTER THE MEASURED WORK. Events only POST (a
; phrase pointer, a tick flag); it_snd_frame - the end of the worker's wake,
; inside its lock hold, after the presentation governor has read the hold, or
; the bracket's frame after its present, before FSXW_FRAME (Excitebike's
; rule, SPEC.md 102.5) - is the one routine that calls the driver, through
; it_tone, the one call site (it_ntone counts it, the it_tonecall mark is its
; breakpoint). The UI task never calls it: its posts happen with the lock
; held (a callback) or in the bracket, where the worker is frozen, and the
; few places that silence directly - a toggle's two sides, Esc, a new game -
; run in those same two contexts.
;
; SILENCE ON every way out of a sound: pause (P, the About card and FOCUS
; LOSS in play - all three set it_paused), the About card in every other
; state too (it_abon: READY's beeps wait for it to go, as READY does), M and
; Game > Sound, a silent tier (it_snd = 0), Esc, a new game, every toggle
; (both sides) and close (the kernel's snd_release_inst at teardown). Focus
; loss outside play pauses nothing (it_focus_ck), so a phrase already
; running there - CRASH's 0.8 s, GAME OVER's 1.1 s, a chime - plays out.
;
; THE COST (SPEC.md 108.14.4, PERFORMANCE.md Set 157): ~1.8k cycles a call
; on the 5150, ~2.5k a frame or wake that calls, ~390 one that does not. At
; most ONE call a wake (windowed: ~1.0% of the XT at the worst rate) or a
; frame (full screen: bound 2.9%). Windowed it is INSIDE the wake's hold, so
; isotronperf gates its Lunatic h4 wake with the sound on.
; =============================================================================

IT_SPRI_TICK  equ 0x40
IT_SPRI_PHR   equ 0x50
IT_SPRI_HIGH  equ 0x60          ; the crash, and every off
IT_SND_TICK   equ 110           ; the wall tick, Hz
IT_SND_TICKB  equ 165           ; ...while boosted (p > 2)
IT_SND_CELLS  equ 4             ; ...every 4th player cell
SP_END        equ 0xFF          ; a phrase's last byte

; -----------------------------------------------------------------------------
; it_snd_post - start the phrase at SI (a priority byte, a start byte - 1:
; its tick 0 is this frame, 2: the next - then (dt, Hz) rows of 3 bytes, dt
; in ticks from its start, Hz 0 = off; SP_END after the last).
; Nothing when the sound is off. Replaces a phrase already playing; the wall
; tick owed goes. Preserves every register.
; -----------------------------------------------------------------------------
it_snd_post:
    cmp byte [it_snd], 0
    je .out
    push ax
    mov ax, [si]
    mov [it_sppri], al
    mov [it_spnew], ah              ; 1: its clock starts now, 2: next frame
    lea ax, [si + 2]
    mov [it_sphn], ax
    mov byte [it_sndtk], 0
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; it_snd_quiet - silence NOW: no phrase, no tick owed, our tone off if one may
; still sound. A toggle's sides, Esc, a new game. Preserves every register.
; -----------------------------------------------------------------------------
it_snd_quiet:
    mov word [it_sphn], 0
    mov byte [it_sndtk], 0
    ; (falls into it_snd_off)

; it_snd_off - our tone off, at 60h, if one may still be sounding: it_sndlive
; and its lease not yet out (a lease that ran out was silenced by the kernel's
; snd_tick, and an off then could only reach somebody else's tone). Preserves
; every register.
it_snd_off:
    cmp byte [it_sndlive], 0
    je .out
    mov byte [it_sndlive], 0
    push ax
    push cx
    push dx
    call OSAPI_GET_TICKS
    sub ax, [it_sndexp]
    jns .x                          ; the lease is out: nothing of ours sounds
    xor ax, ax
    xor cx, cx
    mov dl, IT_SPRI_HIGH
    call it_tone
.x:
    pop dx
    pop cx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; it_tone - THE ONE CALL SITE. AX = Hz (0 = off), CX = lease in ticks, DL =
; priority. A GRANTED on, and only a granted one, marks our tone live until
; the tick it_sndexp: a refused call leaves both as they were, so a lease
; that was never ours is never trusted by it_snd_off. The expiry is read
; BEFORE the call (a tick landing between can only make it early, and an
; early lease skips an off the kernel's snd_tick sends anyway; a late one
; would send an off into somebody else's tone). Preserves all but the flags.
; -----------------------------------------------------------------------------
it_tone:
    inc word [it_ntone]             ; the harness's count of driver calls
    push bx
    or ax, ax
    jz .call                        ; (an off has no lease)
    push ax
    call OSAPI_GET_TICKS
    add ax, cx
    xchg ax, bx                     ; BX = when the lease would end it
    pop ax
.call:
    push ax
    IT_MARK it_tonecall             ; (the row's breakpoint: AX, CX, DL)
    call OSAPI_SND_TONE             ; (CX and DX survive it: kernel/snd.inc)
    IT_MARK it_tonecall1
    pop ax
    jc .out                         ; refused (a louder owner, a clip): ours
    or ax, ax                       ; never sounded
    jz .out
    mov byte [it_sndlive], 1
    mov [it_sndexp], bx
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; it_snd_frame - THE SOUND'S ONE WRITER, once a wake or a frame, after the
; measured work (see the header). DS = the package. Clobbers AX, BX, CX, DX,
; SI (its two callers: the worker's wake, which tail-jumps here, and the
; bracket's frame loop, which reloads everything it uses each frame).
;
;   off, paused       silence (once: it_snd_off sends nothing twice)
;   a phrase          its latest row now due, if not yet sent - a frame that
;                     comes late skips to the newest row, so a stall costs
;                     notes and never stretches the phrase; its last row
;                     ends it
;   a wall tick owed  110 / 165 Hz for one tick (CX = 1)
;   none, a bracket   the last tick's explicit off (it rang one frame)
; -----------------------------------------------------------------------------
it_snd_frame:
    cmp byte [it_snd], 0
    je .mute
    cmp byte [it_paused], 0
    jne .mute
    cmp byte [it_abon], 0
    jne .card
    mov bx, [it_sphn]
    or bx, bx
    jnz .phrase
    xor al, al
    xchg al, [it_sndtk]
    or al, al
    jz .noti
    mov ax, IT_SND_TICK             ; THE WALL TICK: 110 Hz, 165 boosted
    cmp byte [it_pspeed], P_NORMAL
    jbe .tk
    mov ax, IT_SND_TICKB
.tk:
    mov cx, 1                       ; one tick's lease
    mov dl, IT_SPRI_TICK
    jmp it_tone
.noti:
    cmp byte [it_fsm], 0            ; full screen: the tick rang one frame
    je .out
    jmp it_snd_off
.card:                              ; THE ABOUT CARD: silent under it, in
    cmp byte [it_spnew], 0          ; every state. A phrase not yet begun -
    je .mute                        ; READY's, re-posted each wake while the
    mov byte [it_sndtk], 0          ; card holds READY (it_flow) - waits, to
    mov byte [it_spnew], 1          ; begin on the first frame after the card
    jmp it_snd_off
.mute:
    mov word [it_sphn], 0
    mov byte [it_sndtk], 0
    jmp it_snd_off
.phrase:
    call OSAPI_GET_TICKS
    cmp byte [it_spnew], 0          ; A PHRASE POSTED LATE IN A LONG HOLD (the
    je .old                         ; sweep's last item, its first: up to 80 ms)
    dec byte [it_spnew]             ; starts at the FRAME AFTER: a wake starts
    jnz .out                        ; on a tick, and begun at the hold's end its
    mov [it_spt0], ax               ; first note lost a tick (MEASURED: 440 Hz
.old:                               ; for 1.06 ticks, not 2). One posted EARLY
                                    ; in its wake (the crash, GAME OVER: from
                                    ; the iterations, the state machine) starts
                                    ; at once - the crash's off then lands on
                                    ; CRASH's last tick, before WAIT's build
    sub ax, [it_spt0]               ; AX = ticks since the phrase began
    cmp ax, SP_END - 1
    jb .el
    mov ax, SP_END - 1              ; (a phrase is under 255 ticks: clamp)
.el:
    xor si, si                      ; SI = the latest row due, 0 = none new
.find:
    mov cl, [bx]
    cmp cl, SP_END
    je .found
    cmp cl, al
    ja .found
    mov si, bx
    add bx, 3
    jmp short .find
.found:
    or si, si
    jz .out                         ; nothing new is due
    mov [it_sphn], bx               ; the next row to wait for
    mov ax, [si + 1]
    or ax, ax
    jz .poff
    mov cl, [bx]                    ; the lease: to the next row, plus one
    cmp cl, SP_END                  ; tick of slack for a late frame
    jne .ls
    mov cl, [si]
    inc cl
.ls:
    sub cl, [si]
    inc cl
    xor ch, ch
    mov dl, [it_sppri]
    call it_tone
    jmp short .last
.poff:
    call it_snd_off
.last:
    cmp byte [bx], SP_END
    jne .out
    mov word [it_sphn], 0           ; the phrase is over
.out:
    ret

; -----------------------------------------------------------------------------
; THE PHRASES: a priority byte, a start byte (1 now, 2 the next frame), then
; (dt ticks, Hz) rows, then SP_END. Ours
; (SPEC.md 108.1): nothing here is the original's, which used none of them.
; -----------------------------------------------------------------------------
; THE CRASH: 1,400 Hz falling x 15/16 a tick, rounded - 15 entries, one a tick
; for CRASH's 15 ticks, then off. Every entry 60h, so nothing under it
; (a tick, a phrase) cuts it short.
it_ph_crash:
    db IT_SPRI_HIGH, 1
    db 0
    dw 1400
    db 1
    dw 1312
    db 2
    dw 1230
    db 3
    dw 1154
    db 4
    dw 1081
    db 5
    dw 1014
    db 6
    dw 951
    db 7
    dw 891
    db 8
    dw 835
    db 9
    dw 783
    db 10
    dw 734
    db 11
    dw 688
    db 12
    dw 645
    db 13
    dw 605
    db 14
    dw 567
    db 15
    dw 0
    db SP_END
; THE ROUND'S START, over READY's 9 ticks: 440, 440, 880 - 2 on, 1 off
it_ph_ready:
    db IT_SPRI_PHR, 2
    db 0
    dw 440
    db 2
    dw 0
    db 3
    dw 440
    db 5
    dw 0
    db 6
    dw 880
    db 8
    dw 0
    db SP_END
; THE TOWER RISES (a raise, not a reset): C, E, G up, during the sweep
it_ph_tower:
    db IT_SPRI_PHR, 2
    db 0
    dw 523
    db 3
    dw 659
    db 6
    dw 784
    db 9
    dw 0
    db SP_END
; GAME OVER: C, G, E, C down, 4 ticks each, under the message
it_ph_over:
    db IT_SPRI_PHR, 1
    db 0
    dw 523
    db 4
    dw 392
    db 8
    dw 330
    db 12
    dw 262
    db 16
    dw 0
    db SP_END
