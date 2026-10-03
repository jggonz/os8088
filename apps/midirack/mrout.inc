; =============================================================================
; os8088 - apps/midirack/mrout.inc
;
; THE OUTPUTS, OPENED, PLAYED, PAUSED AND CLOSED (SPEC.md 105.8). Prefixes
; mro_ (the UI task's half), mrw_ (the worker's) and mrk_ (the speaker's
; bracket).
;
; WHO PLAYS WHAT:
;   FM, TONE  the WORKER, in real time: the clock is the PIT (os88pit.inc's
;             pit_now / 64, 18,643 units a second) and every event due is
;             played the moment the clock passes it. Between events the worker
;             sleeps whole ticks while the next one is far, and yields while it
;             is near - so an event is late by one task switch, not by a tick
;   SB        the WORKER, AHEAD: it renders spans of the synth into an
;             external ring the card plays in place (SPEC.md 34.5.3), keeping
;             about half a second queued. The card is the clock
;   SPK       the UI task, in an FSXF_RATE bracket (SPEC.md 34.11): the
;             desktop is frozen and the window is an imposter, the Audio
;             Player's shape (86.21). The bracket's body renders; the pulses
;             are os88spk.inc's
;
; THE HANDSHAKE (the Tracker rule, SPEC.md 45.2): [mr_inpass] brackets the
; worker's sequencing and nothing else - never its drawing, which takes the
; gfx lock - and every UI path that resets engine state stops the run first
; and drains it (mro_drain), so the two never touch the sequencer at once.
; =============================================================================

MRB_RL      equ 16384               ; the card's ring: 0.37 s at 44.1 kHz
MRB_RLCODE  equ 2                   ; ...4096 << 2
MRB_LEADMIN equ 4096                ; samples kept queued: half a second, and
MRB_LEADMAX equ MRB_RL - 2048       ; never under 4096 nor past the ring
MRB_HALF    equ 2048                ; a grant ring's unit (mrb_half1)
MRK_RL      equ 4096                ; the speaker's ring (code 0)
MRW_URATE   equ 18643               ; FM / tone units a second: PIT / 64
MRW_TAIL    equ 18                  ; ticks a finished song rings on (~1 s)

    MRW mrb_seg                     ; the card's ring claim
    MRB mrb_hand                    ; its stream handle
    MRB mrb_open                    ; a stream is open
    MRW mrb_rate
    MRW mrk_seg                     ; the speaker's ring claim
    MRW mrk_rate
    MRB mrk_on                      ; a speaker session (its ring holds it)
    MRB mrk_exit                    ; why the bracket ended: MRX_*
    MRB mrk_btn                     ; the mouse as last seen
    MRW mrk_hk                      ; rate periods (the hook)
    MRW mrk_drawt
    MRBUF mrk_map, 256              ; the PWM table, for the synth's
    MRW mr_pblo                     ; FM/tone: the PIT the song started at
    MRW mr_pbhi
    MRW mr_pplo                     ; ...and the PIT a pause stood at
    MRW mr_pphi
    MRW mr_nowlo                    ; the worker's clock, as last read
    MRW mr_nowhi
    MRB mr_tail                     ; the song has run out: ringing on
    MRW mr_tailt                    ; ...since this tick
    MRB mr_ehint                    ; why mr_ended: 0 end / 1 next / 2 prev
    MRW mr_plays                    ; songs started this session

MRX_DESK    equ 1                   ; a click or Space: paused, on the desktop
MRX_STOP    equ 2
MRX_END     equ 3                   ; the song played out
MRX_NEXT    equ 4
MRX_PREV    equ 5
MRX_ERR     equ 6

mr_nullvt:  dw mr_nop, mr_nop, mr_nop, mr_nop, mr_nop, mr_nop, mr_nop

; -----------------------------------------------------------------------------
; mro_pick - AL = the output to open (MRO_*), from [mr_want] and what the
; machine answered at entry. Never MRO_NONE: the speaker is always there
; -----------------------------------------------------------------------------
mro_pick:
    mov al, [mr_want]
    cmp al, MR_OUT_MIDI             ; the two that are only ever CHOSEN
    je .mid                         ; (Settings): Automatic never picks them,
    cmp al, MR_OUT_WT               ; so a machine plays as it always has
    je .wt                          ; until somebody asks
    cmp al, MR_OUT_SPK
    je .spk
    cmp al, MR_OUT_SB
    je .sb
    cmp al, MR_OUT_AUTO
    jne .fm                         ; OPL2 / OPL3, asked for by name
    cmp byte [mr_fmkind], 0         ; AUTO: an FM chip first - it costs the
    jne .fm                         ; machine nothing while it sounds
    cmp byte [mr_hassb], 0
    jne .sb
    jmp short .spk
.fm:
    mov al, MRO_FM
    ret
.sb:
    mov al, MRO_SB
    ret
.mid:
    mov al, MRO_MIDI
    ret
.wt:
    mov al, MRO_WT
    ret
.spk:
    mov al, MRO_SPK
    cmp byte [mr_bg], 0
    je .r
    mov al, MRO_TONE
.r:
    ret

; -----------------------------------------------------------------------------
; mro_drain - the worker out of its pass, with the run stopped. UI task
; -----------------------------------------------------------------------------
mro_drain:
    mov byte [mr_seqon], 0
.w:
    cmp byte [mr_inpass], 0
    jne .w                          ; bounded: the worker re-checks mr_seqon
    ret

; -----------------------------------------------------------------------------
; mro_play - play the loaded song from its start on the chosen output.
; UI task, gfx lock held (a callback). out: CF = 1 refused, [mr_msg] says why
; -----------------------------------------------------------------------------
mro_play:
    push ax
    push bx
    push cx
    push dx
    call mro_close                  ; whatever was open, gone
    call mro_pick
    mov [mr_out], al
    cmp al, MRO_FM
    jne .nfm
    call mrf_open
    jnc .fmok
    mov word [mr_msg], mr_s_nofm
    cmp byte [mr_fmkind], 0
    je .no
    mov word [mr_msg], mr_s_fmbusy
    jmp .no
.fmok:
    mov word [mrq_urate], MRW_URATE
    jmp short .rt
.nfm:
    cmp al, MRO_MIDI
    jne .nmid
    call mrm_open                   ; (its own message on a refusal)
    jc .no
    mov word [mrq_urate], MRW_URATE
    jmp short .rt
.nmid:
    cmp al, MRO_TONE
    jne .ntone
    call mrn_open
    mov word [mrq_urate], MRW_URATE
.rt:
    call mro_rewind
    call mro_clock0
    call mro_worker
    jc .no
    jmp short .go
.ntone:
    cmp al, MRO_SB
    je .card
    cmp al, MRO_WT                  ; THE WAVETABLE: the bank first (once),
    jne .nsb                        ; then the card exactly as the synth has
    call mwt_load                   ; it - mrb_start opens whichever
    jc .no
.card:
    call mrb_start
    jc .no
    call mro_worker
    jc .no
    jmp short .go
.nsb:
    mov byte [mr_state], MR_ST_PLAY ; THE SPEAKER: the bracket, now
    call mrk_start                  ; (it returns when the user leaves it)
    jc .no
    jmp short .ok
.go:
    mov byte [mr_state], MR_ST_PLAY
    mov word [mr_msg], mr_s_playing
    mov byte [mr_seqon], 1
.ok:
    inc word [mr_plays]             ; (the gate counts songs started)
    clc
    jmp short .out
.no:
    mov byte [mr_out], MRO_NONE
    mov byte [mr_state], MR_ST_STOP
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mro_rewind - the song from its start, the channels to power-on
mro_rewind:
    mov byte [mr_dry], 0
    mov byte [mr_tail], 0
    mov byte [mr_ended], 0
    call mrq_rewind
    call mr_chan_reset
    ret

; mro_worker - the worker hired, once. out: CF = 1 no task slot
mro_worker:
    cmp byte [mr_have_worker], 0
    jne .ok
    push ax
    push bx
    mov ax, mr_worker
    mov bx, [mr_win]
    call OSAPI_TASK_SPAWN
    pop bx
    pop ax
    jc .no
    mov byte [mr_have_worker], 1
    OS88_WORKER_RESTARTABLE mr_worker
.ok:
    clc
    ret
.no:
    mov word [mr_msg], mr_s_notask
    stc
    ret

; -----------------------------------------------------------------------------
; mro_pause / mro_resume - UI task. The FM and the tone hold their clock;
; the card is halted where it is (SND_V_PAUSE) and resumed with verb 1
; -----------------------------------------------------------------------------
mro_pause:
    cmp byte [mr_state], MR_ST_PLAY
    jne .out
    call mro_drain
    mov al, [mr_out]
    cmp al, MRO_SB
    je .sb
    cmp al, MRO_WT
    je .sb
    call mro_hush                   ; FM / tone: silence, the clock held
    call pit_now
    sub ax, [mr_pblo]
    sbb dx, [mr_pbhi]
    jns .pp
    xor ax, ax
    xor dx, dx
.pp:
    mov [mr_pplo], ax
    mov [mr_pphi], dx
    jmp short .set
.sb:
    cmp byte [mrb_grant], 0         ; a grant ring pauses by not being fed:
    jne .set                        ; it plays out what is queued and halts
    mov al, SND_V_PAUSE
    mov ah, [mrb_hand]
    call OSAPI_SND_STREAM
.set:
    mov byte [mr_state], MR_ST_PAUSE
    mov word [mr_msg], mr_s_paused
.out:
    ret

mro_resume:
    cmp byte [mr_state], MR_ST_PAUSE
    jne .out
    mov al, [mr_out]
    cmp al, MRO_SPK
    jne .nspk
    jmp mrk_resume                  ; the bracket again
.nspk:
    cmp al, MRO_SB
    je .sb
    cmp al, MRO_WT
    je .sb
    call pit_now                    ; the clock from where it stood
    sub ax, [mr_pplo]
    sbb dx, [mr_pphi]
    mov [mr_pblo], ax
    mov [mr_pbhi], dx
    cmp byte [mr_out], MRO_MIDI     ; the module told the channels again
    jne .go
    call mrm_resend
    jmp short .go
.sb:
    call mrb_tc
    mov cx, ax
    mov al, 1
    mov ah, [mrb_hand]
    call OSAPI_SND_STREAM           ; verb 1 resumes a paused or dry ring
.go:
    mov byte [mr_state], MR_ST_PLAY
    mov word [mr_msg], mr_s_playing
    mov byte [mr_seqon], 1
.out:
    ret

; mro_hush - the open output silent now
mro_hush:
    push ax
    mov al, [mr_out]
    cmp al, MRO_FM
    jne .n1
    call mrf_hush
    jmp short .out
.n1:
    cmp al, MRO_TONE
    jne .n2
    call mrn_hush
    mov al, 0xFF
    call mrn_alloff
    jmp short .out
.n2:
    cmp al, MRO_MIDI
    jne .n3
    call mrm_hush
    jmp short .out
.n3:
    cmp al, MRO_WT
    jne .n4
    call mwt_hush
    jmp short .out
.n4:
    call mry_hush
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; mro_close - the run stopped and the output closed. UI task. Harmless when
; nothing is open
; -----------------------------------------------------------------------------
mro_close:
    push ax
    call mro_drain
    mov al, [mr_out]
    cmp al, MRO_FM
    jne .n1
    call mrf_hush
    call mrf_close
    jmp short .done
.n1:
    cmp al, MRO_TONE
    jne .n2
    call mrn_close
    jmp short .done
.n2:
    cmp al, MRO_WT
    je .n2c
    cmp al, MRO_SB
    jne .n2m
.n2c:
    call mrb_close
    jmp short .done
.n2m:
    cmp al, MRO_MIDI
    jne .n3
    call mrm_close
    jmp short .done
.n3:
    mov byte [mrk_on], 0            ; the speaker's session is over
.done:
    mov byte [mr_out], MRO_NONE
    mov byte [mr_ended], 0
    mov byte [mr_tail], 0
    pop ax
    ret

; mro_clock0 - the FM/tone clock starts now
mro_clock0:
    push ax
    push dx
    call pit_now
    mov [mr_pblo], ax
    mov [mr_pbhi], dx
    xor ax, ax
    mov [mr_lclklo], ax
    mov [mr_lclkhi], ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrw_clock - DX:AX = units since the song started (PIT / 64), NEVER BACKWARDS.
;
; pit_now can read a tick EARLY: the counter has reloaded and IRQ0 is still
; pending, so the BIOS tick is the old one and the pair is 65,536 counts
; behind the truth. Read right after the start that is a NEGATIVE span, and
; a negative span as unsigned is every event of the song due at once - which
; is how the first Play of a session ended its song in a frame and moved on
; (seen on QEMU, and the same race is on every real machine). So a span is
; floored at 0 and the clock is held to the last value it answered
; -----------------------------------------------------------------------------
mrw_clock:
    push cx
    call pit_now
    sub ax, [mr_pblo]
    sbb dx, [mr_pbhi]
    jns .pos
    xor ax, ax                      ; before the start: the start
    xor dx, dx
.pos:
    mov cx, 6
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    cmp dx, [mr_lclkhi]             ; and never behind the last answer
    ja .new
    jb .old
    cmp ax, [mr_lclklo]
    jae .new
.old:
    mov ax, [mr_lclklo]
    mov dx, [mr_lclkhi]
.new:
    mov [mr_lclklo], ax
    mov [mr_lclkhi], dx
    pop cx
    ret

    MRW mr_lclklo                   ; the clock's last answer
    MRW mr_lclkhi

; =============================================================================
; THE WORKER
; =============================================================================
mr_worker:
.loop:
    mov bx, [mr_win]
    call OSAPI_TASK_ALIVE
    mov byte [mr_inpass], 1         ; FIRST, then the guards
    cmp byte [mr_seqon], 0
    je .idle
    cmp byte [mr_state], MR_ST_PLAY
    jne .idle
    mov al, [mr_out]
    cmp al, MRO_SB
    je .sb
    cmp al, MRO_WT
    je .sb
    cmp al, MRO_FM
    je .rt
    cmp al, MRO_TONE
    je .rt
    cmp al, MRO_MIDI
    je .rt
.idle:
    mov byte [mr_inpass], 0
    call mrw_draw
    mov ax, 3
    call OSAPI_TASK_SLEEP
    jmp short .loop
.rt:
    call mrw_rt                     ; AX = ticks to sleep, 0 = just yield
    mov byte [mr_inpass], 0
    call mrw_draw
    or ax, ax
    jz .y
    call OSAPI_TASK_SLEEP
    jmp short .loop
.y:
    call OSAPI_TASK_YIELD
    jmp short .loop
.sb:
    call mrw_sb
    mov byte [mr_inpass], 0
    call mrw_draw
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp short .loop

; -----------------------------------------------------------------------------
; mrw_rt - one real-time pass: every event the clock has reached, played and
; flushed. out: AX = whole ticks the worker may sleep before the next one is
; due, 0 when it is close (yield instead)
; -----------------------------------------------------------------------------
mrw_rt:
    push bx
    push cx
    push dx
    push bp                         ; (mr_vt_call's selector, at the end)
    call mrw_clock
    mov [mr_nowlo], ax
    mov [mr_nowhi], dx
.ev:
    call mrq_due                    ; DX:AX = the next event's units
    jc .end
    cmp dx, [mr_nowhi]
    ja .wait
    jb .play
    cmp ax, [mr_nowlo]
    ja .wait
.play:
    call mrq_step
    jmp short .ev
.wait:
    call mrw_flush
    sub ax, [mr_nowlo]              ; how far off it is
    sbb dx, [mr_nowhi]
    or dx, dx
    jnz .far
    cmp ax, 2048                    ; two ticks and more: sleep all but one
    jb .near
.far:
    mov cl, 10                      ; units / 1024 = ticks
    shr ax, cl
    or dx, dx
    jz .t
    mov ax, 8
.t:
    dec ax
    cmp ax, 8
    jbe .ret
    mov ax, 8
    jmp short .ret
.near:
    xor ax, ax
    jmp short .ret
.end:
    call mrw_flush
    cmp byte [mr_tail], 0           ; THE SONG HAS RUN OUT: every note let go,
    jne .ring                       ; then a second for the releases
    mov byte [mr_tail], 1
    mov al, 0xFF
    mov bp, MRO_ALLOFF
    call mr_vt_call
    call mrw_flush
    call OSAPI_GET_TICKS
    mov [mr_tailt], ax
.ring:
    call OSAPI_GET_TICKS
    sub ax, [mr_tailt]
    cmp ax, MRW_TAIL
    jb .zz
    call mrw_over
.zz:
    mov ax, 2
.ret:
    pop bp
    pop dx
    pop cx
    pop bx
    ret

; mrw_flush - the FM batch out (the tone needs none)
mrw_flush:
    cmp byte [mr_out], MRO_FM
    jne .n
    call mrf_flush
.n:
    cmp byte [mr_out], MRO_MIDI
    jne .out
    call mrm_flush
.out:
    ret

; mrw_over - the song is over: the UI's turn (once)
mrw_over:
    cmp byte [mr_ended], 0
    jne .out
    mov byte [mr_ehint], 0
    mov byte [mr_ended], 1
    mov byte [mr_seqon], 0
    push bx
    mov bx, [mr_win]
    call OSAPI_WM_WAKE
    pop bx
.out:
    ret

; -----------------------------------------------------------------------------
; mrw_sb - the card's ring topped up to [mrb_lead] samples ahead, and an
; underrun resumed. Worker context: verbs 1, 3 and 6 are any-task.
;
; TWO KINDS OF RING (SPEC.md 105.8.4). An EXTERNAL one (34.5.3) is ours and
; the card plays it in place: a span is made straight into it and TOTAL is a
; word the block IRQ reads. It needs an auto-init DSP - and the first SB in
; MartyPC's machine room has none (verb 0 answers 7) - so the fallback is the
; GRANT ring every other player uses (34.5.2): a 2,048-sample half made here
; (mrb_half), staged into the driver's pool with verb 6 and published with
; verb 1, exactly the Audio Player's ap_stage_half. Both count TOTAL and
; CONS the same way, so mrb_tc is the one question either answers.
; -----------------------------------------------------------------------------
mrw_sb:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov al, 3
    mov ah, [mrb_hand]
    call OSAPI_SND_STREAM           ; AX = the state, DX = consumed
    cmp ax, SND_ST_ENDED
    je .dead
    cmp ax, SND_ST_STALE
    je .dead
    mov [mrb_st], al
    cmp al, SND_ST_UNDER
    jne .nu
    inc word [mrb_unders]           ; (the gate reads it: a dry card)
.nu:
    cmp byte [mrb_grant], 0
    je .k
    mov [mrb_gcons], dx             ; the grant ring's CONS is the verb's
.k:
    cmp byte [mr_tail], 0
    jne .tail
.fill:
    call mrb_room                   ; CF = 1: enough queued, or no room
    jc .fed
    call mrb_put1
    cmp byte [mrq_ended], 0
    je .fill
    mov byte [mr_tail], 1           ; the song is all made: let the voices
    mov al, 0xFF                    ; ring out, then drain the ring
    call mrb_alloff
    call OSAPI_GET_TICKS
    mov [mr_tailt], ax
    jmp short .fed
.tail:
    call OSAPI_GET_TICKS            ; ringing on: made until a second has
    sub ax, [mr_tailt]              ; passed, then nothing - the card plays
    cmp ax, MRW_TAIL                ; out what is queued
    jae .drain
    call mrb_room
    jc .fed
    call mrb_put1
    jmp short .tail
.drain:
    call mrb_tc
    cmp ax, dx
    je .over
    cmp byte [mrb_st], SND_ST_UNDER
    jne .out
.over:
    call mrw_over
    jmp short .out
.fed:
    cmp byte [mrb_grant], 0         ; (a grant ring's feed restarts it)
    jne .out
    cmp byte [mrb_st], SND_ST_UNDER ; an external ring ran dry: verb 1
    jne .out                        ; restarts it
    call mrb_tc
    mov cx, ax
    mov al, 1
    mov ah, [mrb_hand]
    call OSAPI_SND_STREAM
    jmp short .out
.dead:
    call mrw_over
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

    MRB mrb_st
    MRW mrb_unders                  ; passes that found the card dry
    MRB mrb_err                     ; verb 0's refusal, for the gate
    MRB mrb_grant                   ; 1 = the GRANT ring (no auto-init DSP)
    MRW mrb_lead                    ; samples kept queued, by the rate
    MRW mrb_pre                     ; ...and the pre-roll: a regime's half
    MRW mrb_goff                    ; ...its offset in the driver's pool
    MRW mrb_grl                     ; ...its length
    MRW mrb_gtot                    ; ...TOTAL and CONS, ours to keep
    MRW mrb_gcons
    MRBUF mrb_half, MRB_HALF        ; ...and the half it is staged from

    MRW mrb_spanfn                  ; the span maker: mry_span / mwt_span

; mrb_alloff - AL = 0xFF: every voice of whichever engine feeds the card
mrb_alloff:
    cmp byte [mr_out], MRO_WT
    je mwt_alloff
    jmp mry_alloff

; mrb_tc - AX = TOTAL, DX = CONS, of whichever ring
mrb_tc:
    cmp byte [mrb_grant], 0
    jne .g
    push es
    mov es, [mrb_seg]
    mov ax, [es:MRB_RL+SND_EXT_TOTAL]
    mov dx, [es:MRB_RL+SND_EXT_CONS]
    pop es
    ret
.g:
    mov ax, [mrb_gtot]
    mov dx, [mrb_gcons]
    ret

; mrb_room - CF = 0 another unit may be made (a span, or a half): the lead is
; under [mrb_lead] and the ring has room for it
mrb_room:
    push ax
    push dx
    call mrb_tc
    sub ax, dx                      ; AX = the lead
    cmp ax, [mrb_lead]
    jae .no
    cmp byte [mrb_grant], 0
    je .yes
    add ax, MRB_HALF                ; a half must FIT: never stage behind CONS
    cmp ax, [mrb_grl]
    ja .no
.yes:
    pop dx
    pop ax
    clc
    ret
.no:
    pop dx
    pop ax
    stc
    ret

; mrb_put1 - one unit made and queued: a span into the external ring, or a
; half staged into the grant ring and published
mrb_put1:
    cmp byte [mrb_grant], 0
    jne mrb_half1
    ; (falls into mrb_span1)

; mrb_span1 - one span into the external ring at TOTAL; TOTAL moved LAST
; (the block IRQ reads it)
mrb_span1:
    push ax
    push cx
    push di
    push es
    mov es, [mrb_seg]
    mov di, [es:MRB_RL+SND_EXT_TOTAL]
    and di, MRB_RL - 1
    mov cx, MRB_RL
    sub cx, di
    call [mrb_spanfn]               ; the synth's, or the wavetable's
    add word [es:MRB_RL+SND_EXT_TOTAL], MRY_SPAN
    pop es
    pop di
    pop cx
    pop ax
    ret

; mrb_half1 - sixteen spans into mrb_half, staged at TOTAL (verb 6, which
; copies out of OUR segment) and published (verb 1)
mrb_half1:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov di, mrb_half
.s:
    mov cx, 0xFFFF                  ; (the half is not a ring: no wrap)
    call [mrb_spanfn]
    add di, MRY_SPAN
    cmp di, mrb_half + MRB_HALF
    jb .s
    mov di, [mrb_gtot]
    mov ax, [mrb_grl]
    dec ax
    and di, ax
    add di, [mrb_goff]
    mov si, mrb_half
    mov cx, MRB_HALF
    mov al, 6
    call OSAPI_SND_STREAM
    add word [mrb_gtot], MRB_HALF
    cmp byte [mrb_open], 0          ; (the pre-roll is before the open)
    je .out
    mov cx, [mrb_gtot]
    mov al, 1
    mov ah, [mrb_hand]
    call OSAPI_SND_STREAM
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrb_start - the synth onto the card: an external ring claimed (once,
; page-safe and high, SPEC.md 34.5.3) and opened, or - refused for want of
; auto-init - a grant ring. UI task. out: CF = 1, [mr_msg] says why
; -----------------------------------------------------------------------------
mrb_start:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [mr_msg], mr_s_nosb
    cmp byte [mr_hassb], 0
    je .no
    mov byte [mrb_grant], 0
    cmp word [mrb_seg], 0
    jne .have
    mov ax, (MRB_RL + 16 + 1023) / 1024
    mov cx, ax
    call OSAPI_MEM_CLAIM_DMA_HI
    mov word [mr_msg], mr_s_nomem
    jc .no
    mov [mrb_seg], dx
.have:
    cmp byte [mr_out], MRO_WT       ; the WAVETABLE's rate and voices
    jne .syn                        ; (mrwt.inc), else the synth's
    call mwt_rate_for
    call mwt_voices
    jmp short .r
.syn:
    call mrb_rate_for               ; THE RATE: the user's (Settings), else
    mov cl, 6                       ; the CPU's. THE VOICES BY THE CPU: an
    cmp byte [mr_cpu], CPU_8086     ; 8088 has ~60% of itself for 8 kHz and
    je .r                           ; six; a 286 or better takes eight
    mov cl, 8
.r:
    mov [mrb_rate], ax
    mov [mrq_urate], ax
    push ax                         ; the lead: half a second of the rate,
    shr ax, 1                       ; inside MRB_LEADMIN..MRB_LEADMAX
    cmp ax, MRB_LEADMIN
    jae .l1
    mov ax, MRB_LEADMIN
.l1:
    cmp ax, MRB_LEADMAX
    jbe .l2
    mov ax, MRB_LEADMAX
.l2:
    mov [mrb_lead], ax
    pop ax
    mov word [mrb_pre], 2048        ; the pre-roll is one of the regime's
    cmp ax, 22222                   ; halves: 4,096 above 22,222 Hz, where
    jbe .p                          ; verb 0 refuses less (SPEC.md 34.5)
    mov word [mrb_pre], 4096
.p:
    cmp byte [mr_out], MRO_WT
    jne .psyn
    call mwt_open                   ; (the synth's output table, and ours)
    mov word [mrb_spanfn], mwt_span
    mov word [mr_msg], mr_s_nomem
    jc .no
    jmp short .pdone
.psyn:
    xor si, si                      ; a card plays the bytes as they are
    call mry_open
    mov word [mrb_spanfn], mry_span
.pdone:
    call mro_rewind
    mov byte [mr_seqon], 1
    mov es, [mrb_seg]
    xor ax, ax
    mov [es:MRB_RL+SND_EXT_TOTAL], ax
    mov [es:MRB_RL+SND_EXT_CONS], ax
.pre:
    call mrb_span1                  ; the pre-roll
    mov ax, [mrb_pre]
    cmp [es:MRB_RL+SND_EXT_TOTAL], ax
    jb .pre
    mov bl, SND_OPENF_RING + SND_OPENF_EXT + (MRB_RLCODE << SND_OPENF_RLSH)
    call OSAPI_SND_CAPS
    test ax, SND_CAP_EXTBLK
    jz .blk
    or bl, 2 << SND_OPENF_BLKSH     ; 512-byte blocks: a pause is prompt
.blk:
    xor al, al
    mov ah, bl
    xor si, si
    mov di, [mrb_seg]
    mov cx, [es:MRB_RL+SND_EXT_TOTAL]
    mov dx, [mrb_rate]
    call OSAPI_SND_STREAM           ; AL = 0 and AH = the handle, or an error
    mov [mrb_err], al
    or al, al
    jz .opened
    cmp al, 7                       ; no auto-init: the grant ring
    jne .refused
    call mrb_grantopen
    jnc .opened
.refused:
    mov word [mr_msg], mr_s_sberr
    jmp short .no
.opened:
    mov [mrb_hand], ah
    mov byte [mrb_open], 1
    clc
    jmp short .out
.no:
    mov byte [mr_seqon], 0
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mrb_grantopen - the grant ring: 16 KB of the driver's pool (8, then 4, if
; that is refused), the song from its start again, the ring all but a half
; full of pre-roll and verb 0 in ring mode. out: CF = 0 and AH = the handle;
; CF = 1 refused
mrb_grantopen:
    push bx
    push cx
    push dx
    push si
    mov cx, 16384
.try:
    mov al, 7
    mov ah, 0                       ; verb 7, sub-op 0: a grant
    call OSAPI_SND_STREAM           ; AX = 0 and SI = its offset
    or ax, ax
    jz .got
    shr cx, 1                       ; refused: half of it, down to 4 KB
    cmp cx, 4096
    jae .try
    jmp short .no
.got:
    mov [mrb_goff], si
    mov [mrb_grl], cx
    mov byte [mrb_grant], 1
    xor ax, ax
    mov [mrb_gtot], ax
    mov [mrb_gcons], ax
    mov [mry_slo], ax               ; the external pre-roll is thrown away:
    mov [mry_shi], ax               ; from the start, into halves
    call mro_rewind
    mov byte [mr_seqon], 1
    mov word [mry_lev], 1024
.pre:
    call mrb_half1
    mov ax, [mrb_gtot]
    add ax, MRB_HALF
    cmp ax, [mrb_grl]
    jb .pre                         ; ...until one half short of full
    xor al, al
    mov ah, SND_OPENF_RING
    mov si, [mrb_goff]
    mov cx, [mrb_gtot]
    mov dx, [mrb_rate]
    call OSAPI_SND_STREAM
    or al, al
    jnz .free
    clc
    jmp short .out
.free:
    call mrb_gfree
.no:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; mrb_gfree - the grant back to the driver (verb 7, sub-op 1)
mrb_gfree:
    push ax
    push si
    cmp byte [mrb_grant], 0
    je .out
    mov si, [mrb_goff]
    mov al, 7
    mov ah, 1
    call OSAPI_SND_STREAM
    mov byte [mrb_grant], 0
.out:
    pop si
    pop ax
    ret

; mrb_close - the stream closed (verb 2: UI task), and a grant given back.
; The external ring's claim is kept for the next play
mrb_close:
    push ax
    cmp byte [mrb_open], 0
    je .g
    mov al, 2
    mov ah, [mrb_hand]
    call OSAPI_SND_STREAM
    mov byte [mrb_open], 0
.g:
    call mrb_gfree
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrb_rate_for / mrk_rate_for - AX = the rate a synth play asks for: the one
; Settings chose ([mr_rate], an index into mr_rates), else the CPU's own -
; the card 8,000 on an 8088, 16,000 on a 286, 22,050 past it; the speaker
; 5,512 on an 8088 (its pulses are half the machine, SPEC.md 105.7.1) and
; 16,000 past it. The speaker clamps what it is given (mrk_start)
; -----------------------------------------------------------------------------
mr_rates:   dw 0, 8000, 11025, 16000, 22050, 32000, 44100
MR_NRATE    equ 7

mrb_rate_for:
    call mr_rate_user
    jnc .r
    mov ax, 8000
    cmp byte [mr_cpu], CPU_8086
    je .r
    mov ax, 16000
    cmp byte [mr_cpu], CPU_286
    je .r
    mov ax, 22050
.r:
    ret

mrk_rate_for:
    call mr_rate_user
    jnc .r
    mov ax, 5512
    cmp byte [mr_cpu], CPU_8086
    je .r
    mov ax, 16000
.r:
    ret

; mrk_real - AX = a rate asked of the speaker -> AX = the rate it plays.
; N = 1,193,182 / R CLAMPED to what a pulse can be - 48..255 counts past an
; 8088 (SPEC.md 34.11.8) - and on an 8088 to 149..255, 8 kHz: a pulse is ~400
; of its cycles, so past the Video Player's VP_SPKMAX the pulses alone are
; more than the machine (SPEC.md 34.11.4). R = 1,193,182 / N back: 8,007 Hz
; at most on an 8088, 24,857 past it, 4,679 at least
mrk_real:
    push bx
    push dx
    mov bx, ax
    mov dx, 0x0012
    mov ax, 0x34DE
    div bx
    mov bx, 149
    cmp byte [mr_cpu], CPU_8086
    je .nmin
    mov bx, 48
.nmin:
    cmp ax, bx
    jae .nlo
    mov ax, bx
.nlo:
    cmp ax, 255
    jbe .nhi
    mov ax, 255
.nhi:
    mov bx, ax
    mov dx, 0x0012
    mov ax, 0x34DE
    div bx
    pop dx
    pop bx
    ret

; mr_rate_user - CF = 0 and AX = the rate Settings chose; CF = 1 Automatic
mr_rate_user:
    push bx
    mov bl, [mr_rate]
    xor bh, bh
    cmp bx, MR_NRATE
    jae .auto
    shl bx, 1
    mov ax, [mr_rates+bx]
    or ax, ax
    jz .auto
    pop bx
    clc
    ret
.auto:
    pop bx
    stc
    ret

; mro_free - every claim the outputs hold, given back (the window's close)
mro_free:
    push dx
    mov dx, [mrb_seg]
    or dx, dx
    jz .k
    call OSAPI_MEM_FREE
    mov word [mrb_seg], 0
.k:
    mov dx, [mrk_seg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [mrk_seg], 0
.out:
    pop dx
    ret

; =============================================================================
; THE SPEAKER, in a bracket (SPEC.md 105.8.3)
; =============================================================================

; mrk_hook - FSXF_RATE's hook: a period, counted
mrk_hook:
    inc word [mrk_hk]
    ret

; -----------------------------------------------------------------------------
; mrk_start - a speaker session from the song's start: the ring (once), the
; pulse table, the synth through it, then the bracket. UI task, lock held.
; out: CF = 1 refused before any sound ([mr_msg])
; -----------------------------------------------------------------------------
mrk_start:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [mrk_seg], 0
    jne .have
    mov ax, (MRK_RL + 272 + 1023) / 1024
    call OSAPI_MEM_CLAIM
    mov word [mr_msg], mr_s_nomem
    jc .no
    mov [mrk_seg], dx
.have:
    call mrk_rate_for               ; AX = the rate asked for
    mov cl, 4                       ; four voices on an 8088, six past it
    cmp byte [mr_cpu], CPU_8086
    je .r
    mov cl, 6
.r:
    push cx
    call mrk_real                   ; the rate the pulses can really make
    mov [mrk_rate], ax
    mov [mrq_urate], ax
    pop cx
    mov dx, ax
    mov di, [mrk_seg]
    mov ah, 0                       ; size code 0: 4 KB
    push cx
    xor cl, cl
    call os88spk_init
    pop cx
    mov word [mr_msg], mr_s_spkslow
    jc .no
    push ds                         ; the PWM table, copied in: the synth's
    pop es                          ; output table goes through it
    mov di, mrk_map
    push ds
    mov ds, [mrk_seg]
    mov si, MRK_RL + SND_EXT_TAB
    push cx
    mov cx, 256
    cld
    rep movsb
    pop cx
    pop ds
    mov ax, [mrk_rate]
    mov si, mrk_map
    call mry_open
    call mro_rewind
    mov es, [mrk_seg]
    xor ax, ax
    mov [es:MRK_RL+SND_EXT_TOTAL], ax
    mov [es:MRK_RL+SND_EXT_CONS], ax
    mov byte [mrk_on], 1
    mov byte [mr_seqon], 1
    call mrk_bracket
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mrk_resume - a paused session, on again
mrk_resume:
    cmp byte [mrk_on], 0
    je .no
    mov byte [mr_seqon], 1
    call mrk_bracket
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; mrk_bracket - the play, in an FSXF_RATE bracket; back on a pause, a stop,
; a skip or the song's end, with [mr_state] and [mr_msg] saying which. A skip
; or an end is the UI's to act on (mr_ended, the same door the worker uses)
; -----------------------------------------------------------------------------
mrk_bracket:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [mr_msg], mr_s_spkplay
    mov byte [mr_state], MR_ST_PLAY
    call mru_update
    mov byte [mrk_exit], MRX_ERR
    mov word [mr_msg], mr_s_spkbusy
    mov bx, [mr_win]
    mov ax, mrk_main
    mov cx, FSXF_RATE
    mov dx, 0xFFFF                  ; a period ~ a tick: the fewest entries
    mov di, mrk_hook
    call OSAPI_FSX_RUN
    mov byte [mr_seqon], 0
    mov al, [mrk_exit]
    cmp al, MRX_DESK
    jne .nd
    mov byte [mr_state], MR_ST_PAUSE
    mov word [mr_msg], mr_s_spkpaused
    jmp short .out
.nd:
    mov byte [mr_state], MR_ST_STOP
    mov byte [mrk_on], 0
    cmp al, MRX_STOP
    jne .ns
    mov word [mr_msg], mr_s_stopped
    jmp short .out
.ns:
    cmp al, MRX_ERR
    je .out                         ; [mr_msg] says why
    mov ah, 0                       ; END, NEXT, PREV: the UI moves on
    cmp al, MRX_NEXT
    jne .np
    mov ah, 1
.np:
    cmp al, MRX_PREV
    jne .nq
    mov ah, 2
.nq:
    mov [mr_ehint], ah
    mov byte [mr_ended], 1
    push bx
    mov bx, [mr_win]
    call OSAPI_WM_WAKE
    pop bx
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrk_main - THE BRACKET'S BODY. DS = CS, SI = our window
; -----------------------------------------------------------------------------
mrk_main:
    mov byte [mrk_btn], 0xFF        ; a button already down is not a click
.pre:
    call mrk_fill                   ; the ring FULL before the door opens
    jnc .pre
    call os88spk_go
    jnc .go
    mov word [mr_msg], mr_s_spkbusy
    mov byte [mrk_exit], MRX_ERR
    ret
.go:
    mov ax, [mrk_hk]
    mov [mrk_drawt], ax
.loop:
    call mrk_fill                   ; CF = 0: a span was made
    pushf
    call mrk_input                  ; AL = MRX_* to leave, 0 to stay
    or al, al
    jnz .leave
    call mrk_isover
    or al, al
    jnz .leave
    call mrk_draw
    popf
    jnc .loop
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jmp short .loop
.leave:
    popf
    mov [mrk_exit], al
    call os88spk_stop               ; CONS exact, for a resume
    ret

; mrk_fill - one span into the ring if it has room; CF = 1 it had none, or
; the song is over and its tail rung out
mrk_fill:
    push ax
    push cx
    push di
    push es
    mov es, [mrk_seg]
    mov ax, [es:MRK_RL+SND_EXT_TOTAL]
    sub ax, [es:MRK_RL+SND_EXT_CONS]
    mov cx, MRK_RL
    sub cx, ax
    cmp cx, MRY_SPAN
    jb .full
    cmp byte [mrq_ended], 0
    je .make
    cmp byte [mr_tail], 0
    jne .ringing
    mov byte [mr_tail], 1
    mov al, 0xFF
    call mry_alloff
    mov word [mr_tailt], 0          ; spans of tail, counted in mr_tailt
.ringing:
    inc word [mr_tailt]
    cmp word [mr_tailt], 160        ; ~1.3 s at 16 kHz, ~2.5 s at 8: done
    ja .full
.make:
    mov di, [es:MRK_RL+SND_EXT_TOTAL]
    and di, MRK_RL - 1
    mov cx, MRK_RL
    sub cx, di
    call mry_span
    call mrk_xlat                   ; (nothing: the synth's table is the PWM)
    add word [es:MRK_RL+SND_EXT_TOTAL], MRY_SPAN
    clc
    jmp short .out
.full:
    stc
.out:
    pop es
    pop di
    pop cx
    pop ax
    ret

mrk_xlat:
    ret

; mrk_isover - AL = MRX_END once the tail has rung out and the ring played
mrk_isover:
    xor al, al
    cmp byte [mr_tail], 0
    je .out
    cmp word [mr_tailt], 160
    jbe .out
    push es
    push bx
    mov es, [mrk_seg]
    mov bx, [es:MRK_RL+SND_EXT_TOTAL]
    cmp bx, [es:MRK_RL+SND_EXT_CONS]
    pop bx
    pop es
    jne .out
    mov al, MRX_END
.out:
    ret

; mrk_input - the keys and the mouse, polled (no events in a bracket)
mrk_input:
    push bx
    push cx
    push dx
    call OSAPI_MOUSE                ; a PRESS anywhere: back to the desktop
    mov ah, [mrk_btn]
    mov [mrk_btn], al
    not ah
    and al, ah
    test al, 1
    jnz .desk
    mov ah, 1
    int 0x16
    jz .stay
    xor ah, ah
    int 0x16
    cmp al, ' '
    je .desk
    cmp al, 0x0D
    je .desk
    cmp al, 0x1B
    je .stop
    cmp ah, KSC_RIGHT
    je .next
    cmp ah, KSC_LEFT
    je .prev
    or al, 0x20
    cmp al, 's'
    je .stop
    cmp al, 'n'
    je .next
    cmp al, 'p'
    je .prev
.stay:
    xor al, al
    jmp short .out
.desk:
    mov al, MRX_DESK
    jmp short .out
.stop:
    mov al, MRX_STOP
    jmp short .out
.next:
    mov al, MRX_NEXT
    jmp short .out
.prev:
    mov al, MRX_PREV
.out:
    pop dx
    pop cx
    pop bx
    ret

; mrk_draw - the clock, the bar and the rack, at most every 4 periods
mrk_draw:
    push ax
    push bx
    mov ax, [mrk_hk]
    sub ax, [mrk_drawt]
    cmp ax, 4
    jb .out
    mov ax, [mrk_hk]
    mov [mrk_drawt], ax
    mov bx, [mr_win]
    call OSAPI_WM_OBSCURED
    jc .out
    call OSAPI_WM_CLIP_SET
    jc .out
    call mru_dyn
    call OSAPI_WM_CLIP_CLEAR
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; mrw_draw - the worker's redraw: the clock, the bar, the measure, the tempo
; and the rack's bars, when something the eye can see moved, at most every
; 4 ticks, and never while covered (the Audio Player's shape, SPEC.md 86.5.1)
; =============================================================================
    MRW mrw_drawt

mrw_draw:
    push ax
    push bx
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [mrw_drawt]
    cmp ax, 4
    jb .out
    mov [mrw_drawt], bx
    call mr_decay                   ; the rack's bars fall
    cmp byte [mr_card], 0
    jne .out                        ; a card is up: it owns the content
    call mru_changed                ; anything to draw?
    jnc .out
    mov bx, [mr_win]
    call OSAPI_WM_OBSCURED
    jc .out
    call OSAPI_GFX_LOCK
    mov bx, [mr_win]
    call OSAPI_WM_CLIP_SET
    jc .un
    call mru_dyn
.un:
    call OSAPI_GFX_UNLOCK
.out:
    pop bx
    pop ax
    ret

; mr_decay - every channel's level falls a step (the rack's bars)
mr_decay:
    push ax
    push bx
    xor bx, bx
.c:
    mov al, [mrc_lev+bx]
    or al, al
    jz .n
    sub al, 12
    jnc .s
    xor al, al
.s:
    mov [mrc_lev+bx], al
    or byte [mrc_dirty+bx], 2
.n:
    inc bx
    cmp bx, MR_NCH
    jb .c
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mr_elapsed - AX = seconds played, DX = the bar's thousandths of the song
; (0..1000), from whichever clock the output has
; -----------------------------------------------------------------------------
mr_elapsed:
    push bx
    push cx
    push es
    mov al, [mr_out]
    cmp al, MRO_FM
    je .rt
    cmp al, MRO_TONE
    je .rt
    cmp al, MRO_MIDI
    je .rt
    cmp al, MRO_SB
    je .sb
    cmp al, MRO_WT
    je .sb
    cmp al, MRO_SPK
    je .spk
    xor ax, ax                      ; nothing open: the start
    xor dx, dx
    jmp short .sec
.rt:
    mov ax, [mr_nowlo]              ; the worker's clock, as last read
    mov dx, [mr_nowhi]
    cmp byte [mr_state], MR_ST_PAUSE
    jne .sec
    mov ax, [mr_pplo]               ; paused: where it stood
    mov dx, [mr_pphi]
    mov cx, 6
.ps:
    shr dx, 1
    rcr ax, 1
    loop .ps
    jmp short .sec
.sb:
    push dx
    call mrb_tc
    mov cx, ax
    sub cx, dx                      ; the lead
    pop dx
    jmp short .lead
.spk:
    mov es, [mrk_seg]
    mov bx, MRK_RL
    mov cx, [es:bx+SND_EXT_TOTAL]   ; samples made, less those still queued
    sub cx, [es:bx+SND_EXT_CONS]
.lead:
    mov ax, [mry_slo]
    mov dx, [mry_shi]
    sub ax, cx
    sbb dx, 0
    jnc .sec
    xor ax, ax
    xor dx, dx
.sec:
    mov bx, [mrq_urate]             ; DX:AX units -> seconds and a remainder
    or bx, bx
    jz .z
    cmp dx, bx
    jae .z
    div bx                          ; AX = seconds, DX = units over
    push ax
    mov ax, dx                      ; ms = units x 1000 / rate
    mov cx, 1000
    mul cx
    div bx
    mov cx, ax                      ; CX = ms over the second
    pop ax
    call mr_permil                  ; DX = the bar's thousandths
    jmp short .out
.z:
    xor ax, ax
    xor dx, dx
.out:
    pop es
    pop cx
    pop bx
    ret

; mr_permil - DX = (AX s + CX ms) / the song's length in thousandths
mr_permil:
    push ax
    push bx
    push cx
    push si
    mov si, cx
    mov bx, 1000
    mul bx                          ; DX:AX = ms
    add ax, si
    adc dx, 0
    mov bx, [mr_lenlo]
    mov cx, [mr_lenhi]
    mov si, bx
    or si, cx
    jz .zero
    push bx
    push cx
    mov cx, 1000                    ; x 1000 / length: scale both down until
    pop si                          ; the length fits a word
    pop bx
.fit:
    or si, si
    jz .w
    shr si, 1
    rcr bx, 1
    shr dx, 1
    rcr ax, 1
    jmp short .fit
.w:
    or bx, bx
    jz .zero
    cmp dx, 0
    jne .full
    mov cx, 1000
    mul cx                          ; DX:AX = ms x 1000
    cmp dx, bx
    jae .full
    div bx
    cmp ax, 1000
    jbe .ok
.full:
    mov ax, 1000
    jmp short .ok
.zero:
    xor ax, ax
.ok:
    mov dx, ax
    pop si
    pop cx
    pop bx
    pop ax
    ret
