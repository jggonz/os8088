; Sound (SPEC.md 102.5, plan section 11): ONE sequencer that speaks either OSAPI_SND_FM
; (AdLib / Sound Blaster, three music voices + one effect voice) or, on the machine every
; XT is, the lead voice through OSAPI_SND_TONE.  Everything it plays is ORIGINAL and
; compiled from apps/excitebike/audio/ into EXB.SND (tools/excitebike_audio.py): five
; songs, nine effects, the engine's pitch table and the note table.  No PCM, no IRQ hook,
; no worker task, no mixer - this runs on the game's own task, inside the frame.
;
; WHEN IT RUNS.  The race frame (xu_race_frame, called at the end of xb_sim_frame, where the
; frame's events are captured in xu_ev) and the menus (xu_menu_tick, one call a frame from
; xb_getkey).  Music plays only outside a race, so a race frame carries the engine and the
; one-shot cues; the finish fanfare is the one song a race frame runs.  Time is in game
; steps (60.1 Hz): a frame's sub-ticks x 282/256 (8.8, error 0.1%), so a slow frame skips
; notes' time forward rather than slowing the tune, and a hidden pause does not run it.
;
; THE COST RULE (SPEC.md 102.5).  A far call is 46.7 us = 223 clk plus the driver's own work - one
; OSAPI_SND_TONE measures ~2.1k clk on the 5150 model - and a memory-operand instruction on the 8088 is
; ~30 clk (its bytes are fetched at 4 clk each), so the frame is built to make FEW of both, and the whole
; of a race's sound runs AFTER the governor has measured the frame's work and BEFORE the idle wait:
;   a frame that changed nothing ..... ~1.2k clk  (the checks, the engine's table read, one compare)
;   a frame whose step changed ....... ~1.6k + the tone call
;   a cue's frame (xu_events + fx) ... up to ~2.6k + the tone call
;   the tone call .................... ~2.1k, on ~16% of the frames
; The speaker gets ONE tone call a frame at most: the frame decides ONE frequency (effect, else engine,
; else the lead) and asks only if it differs from the last or the lease is due.  FM pays two note calls
; when the engine's step changes and key-off + key-on at an effect or music boundary.
; xu_ntone counts driver calls for the gate (tests/excitebike_audio.py); the per-frame count is the
; harness's, from breakpoints on xu_tonecall and xu_fmcall between two xu_race_frame entries.
;
; The PC speaker rolls off below ~100 Hz, so the speaker plays the engine an OCTAVE UP
; (XU_SPK_SHIFT) and folds the lead voice up to at least XU_SPK_FLOOR.

XU_PHASE        equ 282                 ; 8.8 game steps a sub-tick: 60.0988 / 54.6205 = 1.10033
XU_LEASE        equ 20                  ; ticks a speaker tone is asked for...
XU_REFRESH      equ 48                  ; ...and the sub-ticks (54.6 Hz) before it is asked again: 16 ticks
XU_SPK_SHIFT    equ 1                   ; the speaker engine: an octave up (a shift count)
XU_SPK_FLOOR    equ 130                 ; the lowest lead-voice note the speaker is asked for
XU_IDLE_K       equ 8                   ; the idle step of the engine table
XU_KMAX         equ 63
XU_WAIT         equ 3                   ; frames a heavy frame's tone call may wait
XU_HOLD         equ 2                   ; frames between two engine-glide tone calls on the speaker
XU_KUP          equ 2                   ; steps of k a game step the note climbs...
XU_FIFTH        equ 14                  ; 24 x log2(3/2) = 14.04: a fifth below is 14 steps down
XU_NONE         equ 255
XU_NCH          equ 4                   ; claimed channels: 0 lead / growl, 1 harmony / fifth, 2 bass, 3 effects
XU_VSIZE        equ 8                   ; a music voice: next event, loop event, steps left, Hz

%include "exbsnd.inc"

; ---- claim / release -------------------------------------------------------------
; xu_open - at the top of the bracket.  FM only when the machine has a sound driver AND the
; route says the driver (BL), all four channels or none: a refused claim releases what it
; got and the speaker takes over.  A machine with nothing is the speaker's, silently.
xu_open:
    SAVE
    mov byte [xu_live], 0
    mov byte [xu_fm], 0
    mov byte [xu_pat], 0
    mov word [xu_sent], -1
    mov word [xu_ksent], -1
    cmp byte [xu_mute], 0
    jne .out
    call OSAPI_SND_CAPS
    test ax, SND_CAP_FM
    jz .live
    or bl, bl
    jz .live
    xor cx, cx
.patch:
    mov bl, cl
    xor bh, bh
    shl bx, 1
    mov si, [xu_patchtab + bx]
    mov al, 2
    call OSAPI_SND_FM               ; verb 2: patch-load = claim (CL = channel, DS:SI = 11 bytes)
    jc .refused
    inc cl
    cmp cl, XU_NCH
    jb .patch
    mov byte [xu_fm], 1
    jmp .live
.refused:
    mov al, 3
    call OSAPI_SND_FM               ; release what we did get
.live:
    mov byte [xu_live], 1
    cmp byte [xu_race], 0
    je .music
    call xu_want_engine             ; (the M key turned sound back on inside a race)
    jmp .out
.music:
    mov byte [xu_pend], 1           ; ...or inside a menu: the song's notes are re-keyed
    mov byte [xu_pend + 1], 1
    mov byte [xu_pend + 2], 1
.out:
    mov byte [xu_ev], 0             ; (events pile up while it is muted: they are not news at the M key)
    call xu_setrun
    RESTORE
    ret

; xu_start - the bracket's top: a clean sequencer, then the claim
xu_start:
    mov byte [xu_cur], XU_NONE
    mov byte [xu_race], 0
    mov byte [xu_fin], 0
    mov byte [xu_fxchg], 0
    mov byte [xu_fxpri], 0
    mov word [xu_fxp], 0
    mov word [xu_v], 0
    mov word [xu_v + XU_VSIZE], 0
    mov word [xu_v + 2 * XU_VSIZE], 0
    mov word [xu_v + 6], 0
    mov byte [xu_phase], 0
    mov byte [xu_sub], 1
    mov byte [xu_hold], 0
    mov byte [xu_heavy], 0
    mov byte [xu_defer], 0
    mov word [xu_enghz], 0
    jmp xu_open

; xu_close - the bracket's end and the M key: silence, release.  Tone leases end by
; themselves; an FM claim would not.
xu_close:
    cmp byte [xu_live], 0
    je .out
    mov byte [xu_fxpri], 0          ; (no stale effect resumes at the next unmute)
    mov word [xu_fxp], 0
    mov word [xu_fxhz], 0
    mov word [xu_fxw], 0
    push ax
    push cx
    push dx
    cmp byte [xu_fm], 0
    je .spk
    mov al, 3
    call OSAPI_SND_FM
    jmp .clr
.spk:
    xor ax, ax
    xor cx, cx
    mov dl, 40h
    call OSAPI_SND_TONE
.clr:
    mov byte [xu_live], 0
    mov byte [xu_run], 0
    mov byte [xu_fm], 0
    mov word [xu_sent], -1
    mov word [xu_ksent], -1
    pop dx
    pop cx
    pop ax
.out:
    ret

; xu_toggle - the M key
; xu_setrun - [xu_run] = sound live AND a race in progress (the race frame's one compare)
xu_setrun:
    push ax
    mov al, [xu_live]
    and al, [xu_race]
    mov [xu_run], al
    pop ax
    ret

xu_toggle:
    xor byte [xu_mute], 1
    jnz xu_close
    jmp xu_open

; ---- FM helpers (CL = channel, BX = Hz; they preserve BX, CX) ----------------------
; a note that retriggers: key off, then key on
xu_fmnote:
    mov al, 1
    call xu_fmcall
    or bx, bx
    jz .out
    xor al, al
    call xu_fmcall
.out:
    ret

; a note that glides (a channel already sounding just changes pitch); 0 keys off
xu_fmon:
    or bx, bx
    jz .off
    xor al, al
    jmp xu_fmcall
.off:
    mov al, 1
xu_fmcall:
    inc word [xu_ntone]             ; the harness's count of driver calls, all told
    call OSAPI_SND_FM
    ret

; the patch set on channels 0 and 1: music (lead, harmony) or engine (growl, fifth)
xu_want_music:
    cmp byte [xu_pat], 0
    je .out
    mov byte [xu_pat], 0
    mov si, xu_p_lead
    mov di, xu_p_harm
    jmp xu_patch2
.out:
    ret
xu_want_engine:
    cmp byte [xu_pat], 1
    jne .go
    ret
.go:
    mov byte [xu_pat], 1
    mov si, xu_p_eng0
    mov di, xu_p_eng1
xu_patch2:
    cmp byte [xu_fm], 0
    je .out
    push ax
    push cx
    xor cl, cl
    mov al, 2
    call OSAPI_SND_FM
    mov si, di
    inc cl
    mov al, 2
    call OSAPI_SND_FM
    pop cx
    pop ax
.out:
    ret

; ---- silence -----------------------------------------------------------------------
; every voice off, claims kept: FM channels keyed off, the speaker told to stop once
xu_silence:
    cmp byte [xu_live], 0
    je .out
    SAVE
    mov byte [xu_pend], 0
    mov byte [xu_pend + 1], 0
    mov byte [xu_pend + 2], 0
    cmp byte [xu_fm], 0
    je .spk
    xor cl, cl
.ch:
    xor bx, bx
    call xu_fmon
    inc cl
    cmp cl, XU_NCH
    jb .ch
    mov word [xu_ksent], 0
    jmp .done
.spk:
    xor ax, ax
    call xu_spk
.done:
    RESTORE
.out:
    ret

; ---- the songs ---------------------------------------------------------------------
; AL = a song id (EXBSONG_*) or XU_NONE.  Loads the three voices; the next tick plays.
xu_song_if:
    cmp al, [xu_cur]
    jne xu_song
    ret
xu_song:
    SAVE
    mov [xu_cur], al
    cmp byte [xu_fm], 0
    je .quiet                       ; (the speaker: the next tick's emit says the new note or the silence -
    call xu_silence                 ; one tone call, never a tone-off and a tone in one frame)
.quiet:
    mov word [xu_v + 6], 0
    mov word [xu_v + XU_VSIZE + 6], 0
    mov word [xu_v + 2 * XU_VSIZE + 6], 0
    cmp al, XU_NONE
    jne .load
    mov word [xu_v], 0
    mov word [xu_v + XU_VSIZE], 0
    mov word [xu_v + 2 * XU_VSIZE], 0
    jmp .set
.load:
    mov cl, EXB_SONG_REC
    mul cl                          ; AX = id x 12
    add ax, xu_snd + EXB_SND_SONGTAB
    mov si, ax
    mov di, xu_v
    mov cx, 3
.v:
    mov ax, [si]
    add ax, xu_snd                  ; the stream's address
    mov [di], ax
    mov ax, [si + 6]
    or ax, ax
    jz .once
    add ax, xu_snd                  ; the loop's event
.once:
    mov [di + 2], ax
    mov word [di + 4], 0            ; 0 steps left: the first tick loads the first event
    mov word [di + 6], 0
    add si, 2
    add di, XU_VSIZE
    loop .v
    call xu_want_music
.set:
    RESTORE
    ret

; one tick of the music: every voice by [xu_dt] steps, notes that fell inside the tick skipped,
; at most four events a voice (a slow frame slows nothing down and never spirals).  Sets
; xu_pend for each voice whose note changed.
xu_music_tick:
    cmp byte [xu_cur], XU_NONE
    je .none
    push bp
    mov di, xu_v
    xor cx, cx
    mov dl, [xu_dt]
    xor dh, dh
.voice:
    mov si, [di]
    or si, si
    jz .next
    sub [di + 4], dx
    jg .next
    mov bp, 4
.ev:
    mov si, [di]
    lodsb                           ; the pitch: 0 rest, 1..72 note, 255 the end
    cmp al, 255
    je .endm
    xor bx, bx
    or al, al
    jz .set
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov bx, [xu_snd + EXB_SND_NOTES - 2 + bx]
.set:
    lodsb                           ; its steps
    mov [di + 6], bx
    xor ah, ah
    add [di + 4], ax
    mov [di], si
    cmp word [di + 4], 0
    jg .emit
    dec bp
    jnz .ev
    mov word [di + 4], 1
.emit:
    mov bx, cx
    mov byte [xu_pend + bx], 1
    jmp .next
.endm:
    mov bx, [di + 2]                ; the loop event, or 0: the song ends
    or bx, bx
    jz .done
    mov [di], bx
    dec bp
    jnz .ev
    mov word [di + 4], 1
    jmp .next
.done:
    mov word [di], 0
    mov word [di + 6], 0
    mov bx, cx
    mov byte [xu_pend + bx], 1
.next:
    add di, XU_VSIZE
    inc cl
    cmp cl, 3
    jb .voice
    pop bp
.none:
    ret

; ---- effects -----------------------------------------------------------------------
; AL = an effect id (EXBFX_*).  Preserves every register.  A lower priority does not
; interrupt a higher one; equal restarts.
xu_fx:
    pushf
    push ax
    push bx
    push si
    cmp byte [xu_live], 0
    je .out
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov si, [xu_snd + EXB_SND_FXTAB + bx]
    add si, xu_snd
    mov ah, [si]                    ; the priority
    cmp ah, [xu_fxpri]
    jb .out
    mov [xu_fxpri], ah
    mov [xu_fxid], al
    mov al, [si + 1]                ; the record's notes: the first sounds NOW (this frame's steps
    dec al                          ; are taken off it by the frame's xu_fx_step, as they should be)
    mov [xu_fxn], al
    mov al, [si + 2]
    xor ah, ah
    mov [xu_fxw], ax
    mov ax, [si + 3]
    mov [xu_fxhz], ax
    add si, 5
    mov [xu_fxp], si
    mov byte [xu_fxchg], 1
.out:
    pop si
    pop bx
    pop ax
    popf
    ret

; the effect's clock: [xu_dt] steps, frozen by a pause (the pause blip itself runs)
xu_fx_step:
    cmp word [xu_fxp], 0
    je .out
    mov dl, [xu_dt]
    xor dh, dh
    cmp byte [xb_paused], 0
    je .adv
    cmp byte [xu_fxid], EXBFX_PAUSE
    je .adv
    xor dx, dx
.adv:
    mov bx, [xu_fxw]
    sub bx, dx
    mov cx, 3
.load:
    cmp bx, 0
    jg .keep
    cmp byte [xu_fxn], 0
    je .end
    mov si, [xu_fxp]
    lodsb
    xor ah, ah
    add bx, ax
    lodsw
    mov [xu_fxhz], ax
    mov [xu_fxp], si
    dec byte [xu_fxn]
    mov byte [xu_fxchg], 1
    loop .load
    cmp bx, 0
    jg .keep
    mov bx, 1
.keep:
    mov [xu_fxw], bx
.out:
    ret
.end:
    mov word [xu_fxp], 0
    mov word [xu_fxhz], 0
    mov byte [xu_fxpri], 0
    mov word [xu_fxw], 0
    mov byte [xu_fxchg], 1
    ret

; the pause key toggled xb_paused: whatever plays is cancelled (a pause is silence) except the start
; lights, which freeze (silent) and go on at the resume; then the pause blip.  Preserves every register.
xu_pause:
    pushf
    push ax
    cmp byte [xu_live], 0
    je .out
    cmp byte [xu_fxpri], 0
    je .blip
    cmp byte [xu_fxid], EXBFX_START_LIGHT
    jne .cancel
    mov byte [xu_fxchg], 1
    jmp .out
.cancel:
    mov word [xu_fxp], 0
    mov word [xu_fxhz], 0
    mov byte [xu_fxpri], 0
    mov byte [xu_fxchg], 1
.blip:
    mov al, EXBFX_PAUSE
    call xu_fx
.out:
    pop ax
    popf
    ret

; the frame's events -> at most ONE new effect, the highest cue first
xu_events:
    mov al, [xu_ev]
    mov byte [xu_ev], 0
    or al, al
    jz .out
    test al, XEV_FINISH
    jnz .fin
    mov bl, al
    mov al, EXBFX_CRASH
    test bl, XEV_CRASH
    jnz .go
    mov al, EXBFX_LAND_HARD
    test bl, XEV_BOUNCE
    jnz .go
    mov al, EXBFX_LAND_SOFT
    test bl, XEV_LAND
    jnz .go
    mov al, EXBFX_OVERHEAT
    test bl, XEV_OVERHEAT
    jnz .go
    mov al, EXBFX_LAP
    test bl, XEV_LAP
    jnz .go
    mov al, EXBFX_TAKEOFF
    test bl, XEV_TAKEOFF
    jnz .go
    mov al, EXBFX_BUMP
    test bl, XEV_BUMP
    jz .out
.go:
    jmp xu_fx
.fin:
    cmp byte [xu_fin], 0
    jne .out
    mov byte [xu_fin], 1            ; the ride is over: the engine stops, the fanfare plays
    mov word [xu_enghz], 0
    mov word [xu_enghz2], 0
    push bx
    push cx
    cmp byte [xu_fm], 0
    je .fs
    mov word [xu_ksent], 0
    xor cl, cl
    xor bx, bx
    call xu_fmon
    inc cl
    call xu_fmon
.fs:
    pop cx
    pop bx
    mov al, EXBSONG_FINISH
    jmp xu_song
.out:
    ret

; ---- the engine --------------------------------------------------------------------
; The pitch follows the rider's SPEED: a target step k = idle + KTAB[speed / 16] (a table in EXB.SND,
; (16 i + 8) x 51 >> 10: 0..60, no multiply in the frame) + 6 airborne (a wheel spins free) + 2 with
; the throttle held, capped at 63, which the sounding step CHASES - up two a game step, down one - so
; it glides and a jump in speed is a slide, not a click.  A crashed, stalled, paused, finished or
; counting-down bike is silent.  -> [xu_enghz] (the table's Hz), [xu_enghz2] (a fifth below, the FM's
; second voice).  Cost: ~380 clk when nothing moved (the checks, the table read, one compare), +~250
; when the step changed (two table reads).
xu_engine:
    cmp byte [xu_fin], 0
    jne .silent
    cmp byte [xb_paused], 0
    jne .hold
    cmp word [xb_cd], 0
    jne .hold
    mov al, [xm_mode]
    cmp al, XM_CRASH
    jae .silent                     ; a crash (2) and a stall (3)
    mov bx, [xm_speed]
    mov cl, 4
    shr bx, cl
    cmp bx, EXB_KTAB_N - 1
    ja .capbx
.s:
    mov ah, [xu_snd + EXB_SND_KTAB + bx]
    add ah, XU_IDLE_K
    cmp al, XM_AIR
    je .air
.g:
    test byte [xm_inp], INP_A | INP_B
    jz .c
    add ah, 2
.c:
    cmp ah, XU_KMAX
    ja .cap
.t:
    mov bl, [xu_k]
    cmp bl, ah
    jne .glide
    cmp word [xu_enghz], 0          ; the step is where it should be: nothing to say, unless the engine
    je .h                           ; has just come back from a silence
    ret
.glide:
    mov dl, [xu_dt]
    jb .up
    sub bl, dl                      ; down: one step of k a game step
    jc .sett
    cmp bl, ah
    jae .h
    jmp .sett
.up:
    shl dl, 1                       ; up: XU_KUP (= 2) a game step
    add bl, dl
    jc .sett
    cmp bl, ah
    jbe .h
.sett:
    mov bl, ah
.h:
    mov [xu_k], bl
    xor bh, bh
    shl bx, 1
    mov ax, [xu_snd + EXB_SND_ENGINE + bx]
    mov [xu_enghz], ax
    cmp byte [xu_fm], 0
    je .ret                         ; (the speaker plays one voice: the fifth is the FM's)
    mov bl, [xu_k]
    sub bl, XU_FIFTH
    jae .f
    xor bl, bl
.f:
    xor bh, bh
    shl bx, 1
    mov ax, [xu_snd + EXB_SND_ENGINE + bx]
    mov [xu_enghz2], ax
.ret:
    ret
.capbx:
    mov bx, EXB_KTAB_N - 1          ; (above the cap - a turbo overshoot - reads as the cap)
    jmp .s
.air:
    add ah, 6                       ; a wheel in the air spins free
    jmp .g
.cap:
    mov ah, XU_KMAX
    jmp .t
.silent:
    mov byte [xu_k], XU_IDLE_K      ; a fresh start climbs from idle
.hold:
    mov word [xu_enghz], 0
    mov word [xu_enghz2], 0
    ret

; ---- speaking ----------------------------------------------------------------------
; AX = the ONE frequency the speaker should have (0 = none): a tone call only if it differs
; from the last or the lease (XU_LEASE ticks, asked again every XU_REFRESH sub-ticks) is due.
xu_spk:
    cmp ax, [xu_sent]
    jne .send
    or ax, ax
    jz .out                         ; silent stays silent: nothing to refresh
    mov cl, [xu_sub]
    sub [xu_lease], cl
    jg .out
.send:
    jmp xu_tone_send
.out:
    ret

; AX = the frequency (0 = off): THE tone call
xu_tone_send:
    cmp byte [xu_heavy], 0          ; THE PEAK RULE: a frame that already ran an event or an effect step (up to ~2.6k)
    je .now                         ; does not add the ~2.1k tone call on top of it - the call waits for a lighter
    cmp byte [xu_defer], XU_WAIT    ; frame, at most XU_WAIT frames (the sound stays under ~4k a frame at p99)
    jae .now
    inc byte [xu_defer]
    ret
.now:
    mov byte [xu_defer], 0
    mov [xu_sent], ax
    mov byte [xu_lease], XU_REFRESH
    mov cx, XU_LEASE
    or ax, ax
    jnz .go
    xor cx, cx                      ; 0 Hz: off, no duration
.go:
    inc word [xu_ntone]
    mov dl, 40h
xu_tonecall equ $                   ; (a mark for the harness: one per tone call)
    call OSAPI_SND_TONE
    jnc .out
    mov word [xu_sent], -1          ; refused (a higher owner): ask again next frame
.out:
    ret

; what this frame says, after the tick's state is up to date
xu_emit:
    cmp byte [xu_fm], 0
    jne .fm
    cmp byte [xu_fxpri], 0          ; the speaker: an effect owns it (its rests are silence)...
    jne .fx
    cmp byte [xu_race], 0
    je .music
    cmp byte [xu_fin], 0
    jne .music
    mov ax, [xu_enghz]              ; ...else the engine, an octave up (the hot path: ~130 clk to here)...
%rep XU_SPK_SHIFT
    shl ax, 1
%endrep
    cmp ax, [xu_sent]
    je xu_spk                       ; (unchanged: the lease's business)
    or ax, ax
    jz xu_spk                       ; (the engine stopping: at once)
    cmp word [xu_sent], 0
    je xu_spk                       ; (the engine starting: at once)
    cmp byte [xu_hold], 0
    jne .held
    mov byte [xu_hold], XU_HOLD     ; a GLIDE is asked for at most once in XU_HOLD + 1 frames: the pitch of a
    jmp xu_spk                      ; note that moves a semitone in 40 ms does not need every 18 ms, and each
.held:                              ; tone call is ~2.1k clk the frame has to find room for
    dec byte [xu_hold]
    ret
.fx:
    mov ax, [xu_fxhz]
    cmp byte [xb_paused], 0
    je xu_spk
    cmp byte [xu_fxid], EXBFX_PAUSE
    je xu_spk
    xor ax, ax                      ; a frozen effect holds the speaker SILENT
    jmp xu_spk
.music:
    mov ax, [xu_v + 6]              ; ...else the lead voice, folded up out of the speaker's floor
    or ax, ax
    jz xu_spk
.fold:
    cmp ax, XU_SPK_FLOOR
    jae xu_spk
    shl ax, 1
    jmp .fold
.fm:
    xor cl, cl                      ; the music voices' new notes (key off, key on)
.mv:
    mov bl, cl
    xor bh, bh
    cmp byte [xu_pend + bx], 0
    je .mn
    mov byte [xu_pend + bx], 0
    mov bl, cl
    shl bl, 1
    shl bl, 1
    shl bl, 1
    mov bx, [xu_v + bx + 6]
    call xu_fmnote
.mn:
    inc cl
    cmp cl, 3
    jb .mv
    cmp byte [xu_fxchg], 0          ; the effect's voice
    je .eng
    mov byte [xu_fxchg], 0
    mov cl, 3
    mov bx, [xu_fxhz]
    cmp byte [xu_fxpri], 0
    je .fxk
    cmp byte [xb_paused], 0
    je .fxk
    cmp byte [xu_fxid], EXBFX_PAUSE
    je .fxk
    xor bx, bx                      ; a frozen effect's note is keyed off, and back on at the resume
.fxk:
    call xu_fmnote
.eng:
    cmp byte [xu_race], 0           ; the engine, glide: only when the step changed
    je .out
    mov ax, [xu_enghz]
    cmp ax, [xu_ksent]
    je .out
    mov [xu_ksent], ax
    mov bx, ax
    xor cl, cl
    call xu_fmon
    mov bx, [xu_enghz2]
    inc cl
    call xu_fmon
.out:
    ret

xu_dtcalc:                          ; [xu_sub] sub-ticks (1..8) -> [xu_dt] game steps, the fraction carried
    mov al, [xu_sub]
    cmp al, 8
    jbe .k
    mov al, 8
.k:
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [xu_m282 + bx]          ; sub x 282 (8.8 steps): a table, not a multiply
    mov dl, [xu_phase]
    xor dh, dh
    add ax, dx
    mov [xu_phase], al
    mov [xu_dt], ah
    ret
xu_m282: dw 0, 282, 564, 846, 1128, 1410, 1692, 1974, 2256

; ---- the frame hooks ---------------------------------------------------------------
; the race: called once a frame from the frame loop, AFTER xb_gov and before the idle wait (game.inc): the
; governor has measured the frame's work without it, and the whole of the sound - its decisions and its one
; driver call - runs where the machine would otherwise be idle.  The frame's events were captured into xu_ev
; at the end of xb_sim_frame (xm_ev is cleared there).  It
xu_race_frame:
    cmp byte [xu_run], 0            ; sound live AND a race in progress: one flag, one compare
    je .none                        ; (a taken jump is 16 clk, a fall-through 4: the common path falls through)
    cmp byte [xb_simon], 0
    je .none                        ; (the wave-2 scroll test has no rider: nothing to sound)
    cmp byte [xb_period], 1         ; sub-ticks since the last frame began (1..8)
    jne .gen
    add byte [xu_phase], 26         ; the usual frame is one sub-tick = 1 + 26/256 steps: no table, no multiply
    mov al, 0
    adc al, 1
    mov [xu_dt], al
    mov byte [xu_sub], 1            ; (the speaker's lease counts SUB-TICKS: 1 here, the period on the general path)
.dt:
    cmp byte [xu_ev], 0
    jne .ev
.noev:
    cmp word [xu_fxp], 0
    jne .fx
.nofx:
    cmp byte [xu_cur], XU_NONE
    jne .mus
.nomus:
    call xu_engine
    call xu_emit
    mov byte [xu_heavy], 0
    ret
.gen:
    mov al, [xb_period]
    mov [xu_sub], al
    call xu_dtcalc                  ; (xu_sub keeps the period so xu_spk leases per sub-tick)
    jmp .dt
.ev:
    mov byte [xu_heavy], 1
    call xu_events
    jmp .noev
.fx:
    mov byte [xu_heavy], 1
    call xu_fx_step
    jmp .nofx
.mus:
    cmp byte [xb_paused], 0         ; (a pause is silence: the fanfare waits with the sim)
    je .mus2
    mov byte [xu_dt], 0
    jmp .nomus
.mus2:
    call xu_music_tick              ; (the finish fanfare)
    jmp .nomus
.none:
    ret

; the menus: xb_getkey's one call a frame
xu_menu_tick:
    cmp byte [xu_live], 0
    je .none
    cmp byte [xu_race], 0
    jne .none
    SAVE
    mov byte [xu_sub], 1
    call xu_dtcalc
    call xu_fx_step
    call xu_music_tick
    mov word [xu_enghz], 0
    call xu_emit
    RESTORE
.none:
    ret

; a race begins: the music stops, the engine's patches load, the start lights beep
xu_race_begin:
    SAVE
    mov al, XU_NONE
    call xu_song
    mov byte [xu_race], 1
    mov byte [xu_fin], 0
    mov byte [xu_ev], 0
    call xu_setrun
    call xu_want_engine
    mov byte [xu_k], XU_IDLE_K
    mov word [xu_enghz], 0
    mov word [xu_enghz2], 0
    mov word [xu_ksent], 0
    mov word [xu_fxp], 0
    mov byte [xu_fxpri], 0
    mov al, EXBFX_START_LIGHT
    call xu_fx
    RESTORE
    ret

; a race ends (any way): everything off, back to the menus' patches
xu_race_end:
    SAVE
    mov byte [xu_race], 0
    mov byte [xu_run], 0
    mov byte [xu_fin], 0
    mov word [xu_fxp], 0
    mov byte [xu_fxpri], 0
    mov byte [xu_fxchg], 0
    call xu_silence
    mov al, XU_NONE
    call xu_song
    RESTORE
    ret

; ---- FM patches (5 modulator registers 20h 40h 60h 80h E0h, 5 carrier, C0h) ----------
xu_patchtab: dw xu_p_lead, xu_p_harm, xu_p_bass, xu_p_fx
xu_p_lead: db 21h,22h,0f2h,17h,00h, 21h,06h,0f2h,17h,00h, 04h
xu_p_harm: db 21h,30h,0e2h,07h,00h, 21h,10h,0e2h,07h,00h, 00h
xu_p_bass: db 21h,3fh,0e0h,07h,00h, 21h,10h,0e0h,07h,00h, 00h
xu_p_fx:   db 21h,20h,0f4h,06h,00h, 21h,00h,0f4h,06h,00h, 06h
xu_p_eng0: db 22h,10h,0f0h,0f7h,01h, 21h,00h,0f0h,0f7h,01h, 0eh
xu_p_eng1: db 21h,20h,0f0h,0f7h,00h, 21h,08h,0f0h,0f7h,00h, 06h

; the compiled songs, effects and tables (tools/excitebike_audio.py)
xu_snd: incbin "EXB.SND"
