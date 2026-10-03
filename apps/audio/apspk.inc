; =============================================================================
; os8088 - apps/audio/apspk.inc
;
; THE PC SPEAKER, WITH NO CARD (SPEC.md 86.21, docs/plans/completed/SPEAKER-PCM-PLAN.md).
; A machine with no Sound Blaster plays a track through 34.11's pulse width -
; automatically, because the owner's rule is that sound and nothing else is
; better than no sound. The pulses need IRQ0 at the sample rate, which only a
; package's own FSXF_RATE bracket may have, so a speaker play is a BRACKET
; (SPEC.md 53) and the desktop is frozen for as long as it plays: the IMPOSTER
; WINDOW - the Video Player's in-window play (98.3.7) - with this window's
; clock and bar the only pixels that move, the pointer gone, a click or Space
; back to the desktop paused, and Esc a stop.
;
; THE BRACKET'S BODY IS THE PRODUCER. No worker: under a sample ISR a task
; switch rides the 18.2 Hz tick and a yield is ~2,200 cycles at IF = 0
; (SPEC.md 34.11.3), so aps_main reads the disk (the UI task may), decodes,
; resamples, shapes (apps/os88spkfx.inc) and fills the ring os88spk.inc
; plays, and waits in OSAPI_FSX_WAIT when the ring is full.
;
;   aps_open    the current track prepared by ap_prep_track: its plan, the
;               ring and the tables, then the bracket. UI task, gfx lock held
;   aps_resume  a paused session's bracket again
;   aps_close   the session over (ap_stream_close calls it)
;   aps_free    ...and the ring claim given back (the window's close)
;
; THE PLAN (aps_plan): the speaker's rate R from the file's S. A file of
; counts (os88spkfx.py shape --counts) plays 1:1 or not at all. Otherwise R
; is S when S is at most the machine's TOP - 8,000 Hz on an 8088 (5,512 for
; IMA ADPCM, whose decode is paid at the source rate), 24,858 past it
; (34.11.8) - else S / k averaged over k (a box, the cheapest anti-alias) when
; k divides S, else a step through the source to TOP.
;
; A COVOX PLAYS THROUGH THE SAME BRACKET (SPEC.md 86.21.3, 34.14). aps_dev asks
; os88spk_lptq at every play's start from the desktop and names the port to
; os88spk, which then plays the ring's bytes as voltages. The plan and the
; ladder are the speaker's; what changes is the ring's bytes: the SAMPLES,
; COPIED as a card would play them - no shaper, no leveller - and a file of
; counts turned back into its samples through ap_cinv on the way in.
; =============================================================================

APS_RSIZE   equ 0x20                ; ring size code (bits 4-5): 4096 << 2
APS_RL      equ 16384               ; ...16 KB: 2 s at 8 kHz
APS_LOW     equ APS_RL / 4          ; a lead under this, still producing:
                                    ; the machine is behind (aps_fall)
APS_RMIN    equ 4679                ; a pulse of 255 counts, mode 0's lobyte
APS_SPANS   equ 6                   ; spans made between polls of the keys
APS_COPY    equ 0xFF                ; aps_mode: copied - a file of counts,
                                    ; or anything on a Covox...
APS_XL      equ 0xFE                ; ...or through ap_cinv: counts, Covox

APX_DESK    equ 1                   ; paused, back to the desktop
APX_STOP    equ 2                   ; stopped
APX_END     equ 3                   ; the playlist ran out
APX_ERR     equ 4                   ; ap_msg says why

    APB aps_on                      ; a session: its ring holds this track
    APW aps_rseg                    ; the ring's claim (RL + 272)
    APW aps_rate                    ; R
    APB aps_k                       ; box k (1: none), 0: a step
    APW aps_stepf                   ; the step, 16.16: fraction...
    APW aps_stepi                   ; ...and whole samples
    APW aps_frac                    ; the step's position carried: fraction...
    APW aps_posi                    ; ...and whole samples past the next read
    APB aps_dir                     ; aps_skip's direction
    APB aps_rung                    ; the ladder's rung, for the session
    APB aps_lag                     ; the last rung is still behind: play on
    APW aps_crem                    ; the clock's remainder, in 1/R samples
    APB aps_mode                    ; SPKFX_PRE_*, APS_COPY or APS_XL
    APW aps_lpt                     ; the Covox's port this play, 0 = speaker
    APW aps_span                    ; samples a span (34.11.9)
    APB aps_exit                    ; APX_*
    APW aps_hk                      ; rate periods, counted by aps_hook
    APB aps_eot                     ; this track's source is all made
    APW aps_cons0                   ; CONS at the last clock update
    APB aps_btn                     ; mouse buttons as last seen
    APW aps_drawt                   ; aps_hk at the last redraw
    APW aps_dtab                    ; the box's table (sum -> average)
    APBUF aps_src, 2048             ; the source, before the box or the step
    APBUF aps_stage, 512            ; ...and a span of the speaker's rate
    APBUF aps_fam, SPKFX_NLEV * 256 ; the shaper's level family
    APBUF aps_dbuf, 6 * 256         ; the box's table, k up to 6

; THE LADDER (aps_plan, aps_fall): the speaker's top rate, rung by rung - an
; 8088's from its 8,000 Hz (VP_SPKMAX) down, a 286's from a 48-count pulse
; (34.11.8) down. A session starts at rung 0 and comes down one each time a
; play cannot keep its ring full, and stays down: it has learned the machine
aps_tops88:  dw 8000, 5512, 4800, 0
aps_topsat:  dw 24858, 16000, 11025, 8000, 5512, 0

aps_hook:                           ; FSXF_RATE's hook: a period, counted
    inc word [aps_hk]
    ret

; -----------------------------------------------------------------------------
; aps_plan - the speaker's rate and the path to it, from the prepared track.
; out: CF = 1 refused (ap_msg set)
; -----------------------------------------------------------------------------
aps_plan:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_CPU_INFO             ; AL = the tier
    mov bx, aps_topsat              ; THE LADDER: TOP is the rung this session
    xor cl, cl                      ; has come down to (aps_fall) - and an
    cmp al, CPU_8086                ; 8088 starts IMA ADPCM a rung down, its
    jne .rung                       ; decode being paid at the source rate
    mov bx, aps_tops88
    cmp byte [apw_fmt], AP_FMT_ADPCM
    jne .rung
    inc cx
.rung:
    mov al, [aps_rung]
    cmp al, cl
    jae .r
    mov al, cl
    mov [aps_rung], al              ; (the rung it starts on is the one the
.r:                                 ; ladder comes down FROM)
    xor ah, ah
    shl ax, 1
    add bx, ax
    mov bx, [bx]                    ; BX = TOP
    or bx, bx
    jz .slow                        ; past the last rung
    mov ax, [ap_rate]               ; S
    mov byte [aps_k], 1
    mov byte [aps_mode], SPKFX_PRE_DIFF
    cmp byte [apw_spk], 1
    jne .nshp
    mov byte [aps_mode], SPKFX_PRE_NONE     ; shaped on the host: tables only
.nshp:
    cmp word [aps_lpt], 0           ; A COVOX: the samples COPIED, as a card
    je .nlpt                        ; plays them - a WAV's eight bits are the
    mov byte [aps_mode], APS_COPY   ; DAC's, so nothing to shape (86.21.3)
.nlpt:
    cmp byte [apw_spk], 2           ; COUNTS: 1:1, or not at all - and
    jne .pcm                        ; nothing to come down a rung to
    mov byte [aps_mode], APS_COPY
    cmp byte [aps_rung], 0
    jne .slow
    cmp ax, bx
    ja .fast
    cmp word [aps_lpt], 0           ; ...and on a Covox turned back into the
    je .rate                        ; samples they came from, as on a card
    mov byte [aps_mode], APS_XL     ; (86.21.1): copied raw they are a quiet
    call ap_cinv_build              ; wave off the DAC's middle
    jmp short .rate
.pcm:
    cmp ax, bx
    jbe .rate                       ; S itself
    mov cx, bx                      ; k = ceil(S / TOP)
    xor dx, dx
    add ax, bx
    adc dx, 0
    sub ax, 1
    sbb dx, 0
    div cx                          ; AX = k
    mov cx, ax
    mov ax, [ap_rate]
    xor dx, dx
    div cx                          ; AX = S / k, DX = the remainder
    or dx, dx
    jnz .step
    cmp ax, APS_RMIN
    jb .step
    cmp cl, 3                       ; (a box of more than 3 costs more than
    ja .step                        ; the step it would save: PERFORMANCE)
    mov [aps_k], cl                 ; S / k exactly: the box
    jmp short .rate
.step:
    mov byte [aps_k], 0             ; a step through the source to TOP:
    mov ax, bx                      ; S * 65536 / TOP, 16.16
    mov cx, bx
    mov dx, [ap_rate]               ; (S / TOP < 12: the quotient fits)
    push ax
    mov ax, dx
    xor dx, dx
    div cx                          ; AX = S / TOP, DX = the remainder
    mov [aps_stepi], ax
    mov [aps_dostep.adc+2], ax      ; the step's whole part, into the loop
    mov [aps_dostep.adc2+2], ax
    xor ax, ax
    div cx                          ; AX = the fraction
    mov [aps_stepf], ax
    pop ax
.rate:
    mov [aps_rate], ax
    mov word [aps_span], 256        ; ~32 ms a span (os88spkfx's block)
    cmp ax, 11025
    jbe .sp
    mov word [aps_span], 512
.sp:
    mov word [aps_frac], 0
    mov word [aps_posi], 0
    cmp byte [aps_k], 2
    jb .ok
    call aps_dtab_build
.ok:
    clc
    jmp short .out
.fast:
    mov word [ap_msg], ap_s_spkfast
    stc
    jmp short .out
.slow:
    mov ax, ap_s_spkslow
    call aps_say
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; aps_dtab_build - aps_dbuf[sum] = sum / k, the box's average without a div
aps_dtab_build:
    push ax
    push bx
    push cx
    push dx
    mov cl, [aps_k]
    xor bx, bx
    xor ax, ax                      ; AL = the average, DL counts to k
    xor dl, dl
.l:
    mov [aps_dbuf+bx], al
    inc dl
    cmp dl, cl
    jb .n
    xor dl, dl
    inc al
.n:
    inc bx
    cmp bx, 6 * 256
    jb .l
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; aps_setup - the ring claimed (once), its table and the shaper's family for
; R, and the ring empty. out: CF = 1 refused (ap_msg set)
; -----------------------------------------------------------------------------
aps_setup:
    push ax
    push cx
    push dx
    push di
    push es
    cmp word [aps_rseg], 0
    jne .have
    mov ax, (APS_RL + 272 + 1023) / 1024
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [aps_rseg], dx
.have:
    mov di, [aps_rseg]
    mov ah, APS_RSIZE
    mov dx, [aps_rate]
    xor cl, cl                      ; one pulse a sample
    call os88spk_init
    jc .rate
    cmp byte [aps_mode], APS_XL     ; (APS_COPY or APS_XL: no shaper)
    jae .ring
    mov di, aps_fam
    mov al, [aps_mode]
    mov ah, 1                       ; the carrier put away in the quiet
    call os88spkfx_init
.ring:
    mov es, [aps_rseg]
    xor ax, ax
    mov [es:APS_RL], ax             ; TOTAL
    mov [es:APS_RL+2], ax           ; CONS
    mov [aps_cons0], ax
    mov byte [aps_on], 1
    clc
    jmp short .out
.nomem:
    mov word [ap_msg], ap_s_nomem
    stc
    jmp short .out
.rate:
    mov word [ap_msg], ap_s_spkfast
    stc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; aps_trk_reset - a new track's clock and source flags
aps_trk_reset:
    mov byte [aps_eot], 0
    mov byte [aps_lag], 0
    mov word [ap_played], 0
    mov word [ap_played+2], 0
    mov word [ap_elapsed_s], 0
    mov word [aps_crem], 0
    ret

; -----------------------------------------------------------------------------
; aps_open - play the current, prepared track on the speaker. UI task, gfx
; lock held (a callback, or ap_onwake with it taken)
; -----------------------------------------------------------------------------
aps_open:
    call aps_dev                    ; the speaker or the Covox, asked now
    call aps_trk_reset
    call aps_plan
    jc .no
    call aps_setup
    jc .no
    call aps_bracket
    clc
    ret
.no:
    mov byte [ap_state], AP_ST_STOP
    stc
    ret

; aps_resume - a paused session, played on
aps_resume:
    cmp byte [aps_on], 0
    je .no
    call aps_bracket                ; (on the device it opened on: the ring
    clc                             ; holds that device's bytes, and the
    ret                             ; session keeps it as it keeps its rung)
.no:
    stc
    ret

; aps_dev - WHICH DEVICE: the Covox's data port while the sound tier is the
; Covox and SOUND.DRV found one (os88spk_lptq, SPEC.md 34.14), else 0 - the
; speaker - into aps_lpt and os88spk_lpt. Asked when a play starts from the
; desktop: the tier is the Control Panel's and may have moved since the last
; one. Not on a resume (the session's ring is its device's bytes), and never
; inside the bracket, where nothing can reach the Control Panel
aps_dev:
    push dx
    call os88spk_lptq               ; DX = the port, or CF = 1
    jnc .have
    xor dx, dx
.have:
    call os88spk_lpt
    mov [aps_lpt], dx
    pop dx
    ret

; aps_say - ap_msg = AX, a speaker sentence - or on a Covox play the same
; sentence told again in aps_cvxs with the Covox where the speaker was: the
; two bytes before it say where and how long (audio.asm). DS = CS; AX spent
aps_say:
    cmp word [aps_lpt], 0
    je .s
    push cx
    push si
    push di
    push es
    push cs
    pop es
    mov si, ax
    mov di, aps_cvxs
    mov cx, [si-2]                  ; CL = where the device is, CH how long
    mov al, ch
    xor ch, ch
    cld
    rep movsb                       ; the sentence up to it...
    cbw
    add si, ax                      ; ...past it...
    mov ax, 'Co'
    stosw
    mov ax, 'vo'
    stosw
    mov al, 'x'
    stosb                           ; ...the Covox...
.c:
    lodsb                           ; ...and the rest, its NUL too
    stosb
    or al, al
    jnz .c
    mov ax, aps_cvxs
    pop es
    pop di
    pop si
    pop cx
.s:
    mov [ap_msg], ax
    ret

    APBUF aps_cvxs, 40              ; the sentence, told for the Covox

; aps_close - the session over. The ring's claim is kept for the next
aps_close:
    mov byte [aps_on], 0
    ret

; aps_free - ...and given back
aps_free:
    push dx
    mov byte [aps_on], 0
    mov dx, [aps_rseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [aps_rseg], 0
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; aps_bracket - the play, in an FSXF_RATE bracket; back when the user pauses,
; stops, or the list ends, with ap_state and ap_msg saying which
; -----------------------------------------------------------------------------
aps_bracket:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, ap_s_spk                ; the window as the bracket takes it: the
    call aps_say                    ; (or the Covox's)
    mov byte [ap_state], AP_ST_PLAY ; status line and the clock say playing
    call apu_repaint
    mov byte [aps_exit], APX_ERR
    mov word [ap_msg], ap_s_nosb    ; (kern_small: FSXF_RATE refuses)
    mov bx, [ap_win]
    mov ax, aps_main
    mov cx, FSXF_RATE
    mov dx, 0xFFFF                  ; a period ~ a tick: fewest period entries
    mov di, aps_hook                ; (each costs ~a pulse, 34.11.3)
    call OSAPI_FSX_RUN
    mov al, [aps_exit]
    cmp al, APX_DESK
    jne .ndesk
    mov byte [ap_state], AP_ST_PAUSE
    mov ax, ap_s_spkpaused
    call aps_say
    jmp short .out
.ndesk:
    mov byte [ap_state], AP_ST_STOP
    mov word [ap_elapsed_s], 0
    call aps_close
    cmp al, APX_STOP
    jne .nstop
    mov word [ap_msg], ap_s_stopped
    jmp short .out
.nstop:
    cmp al, APX_END
    jne .out                        ; APX_ERR: ap_msg already says why
    mov word [ap_msg], ap_s_endlist
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; aps_main - THE BRACKET'S BODY. DS = CS, SI = our window
; -----------------------------------------------------------------------------
aps_main:
    mov byte [aps_btn], 0xFF        ; a button already down is not a click
    xor al, al
    call aps_prefill
    call os88spk_go
    jnc .go
    mov ax, ap_s_spkbusy
    call aps_say
    mov byte [aps_exit], APX_ERR
    ret
.go:
    mov ax, [aps_hk]
    mov [aps_drawt], ax
.loop:
    call aps_fill                   ; CF = 0: something was made
    pushf
    call aps_behind                 ; AL = 0, or APX_ERR past the last rung
    or al, al
    jnz .leave
    call aps_refill                 ; the disk, when the look-ahead is low
    call aps_clock
    call aps_draw
    call aps_input                  ; AL = APX_* to leave, 0 to stay
    or al, al
    jnz .leave
    call aps_trackend               ; AL = APX_END at the list's end
    or al, al
    jnz .leave
    popf
    jnc .loop                       ; made a span: more may be due
    mov al, FSXW_FRAME              ; the ring full: wait out the period
    call OSAPI_FSX_WAIT
    jmp short .loop
.leave:
    popf
    mov [aps_exit], al
    call os88spk_stop               ; CONS exact, for the resume
    call aps_clock
    ret

; aps_prefill - THE RING FILLED BEFORE THE DOOR OPENS: the whole of it at a
; play's start - two seconds of cushion against the disk - and half after the
; ladder came down a rung (aps_behind), since that fill IS the gap the user
; hears: at the lower rate the producer outruns the pulses and fills the rest
; while they play. AL = 0 the whole ring, 1 half of it
aps_prefill:
    push ax
    push bx
    push cx
    mov bx, APS_RL
    or al, al
    jz .pre
    shr bx, 1
.pre:
    push es                         ; filled to BX yet?
    mov es, [aps_rseg]
    mov ax, [es:APS_RL]
    sub ax, [es:APS_RL+2]
    pop es
    cmp ax, bx
    jae .out
    call aps_fill
    jnc .pre
    cmp byte [aps_eot], 0
    jne .out                        ; (the track is all made)
    cmp byte [ap_src_done], 0
    jne .out                        ; (the file is all in)
    sub ax, APS_RL                  ; the RING decides full, not the look-
    neg ax                          ; ahead: aps_get also refuses one holding
    cmp ax, [aps_span]              ; less than a span's inputs, which a step
    jb .out                         ; at 44.1 kHz is ~1.4 KB (the ring is full)
    call ap_la_avail
    mov cx, ax
    call ap_refill_chunk            ; ...and a look-ahead short of a span has
    call ap_la_avail                ; room for a cluster, so a read that
    cmp ax, cx                      ; brought nothing is the disk's: out,
    jne .pre                        ; rather than spin on it
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; aps_behind - IS THE MACHINE KEEPING UP? A lead under APS_LOW with the file
; still coming means the producer cannot fill the ring as fast as the pulses
; empty it - this file, this rate, this CPU and this disk together. Then the
; LADDER: the ring played out, the door shut, a rung down (the session keeps
; it), the tables for the new rate, the ring full again and the door open -
; a gap of about a second, once, and a play that keeps time after it.
; out: AL = 0, or APX_ERR past the last rung (ap_msg says so)
; -----------------------------------------------------------------------------
aps_behind:
    xor al, al
    cmp byte [aps_eot], 0
    jne .out                        ; the tail: the lead falls by design
    cmp byte [aps_lag], 0
    jne .out                        ; (the last rung, and still behind: on)
    push bx
    push es
    mov es, [aps_rseg]
    mov bx, [es:APS_RL]
    sub bx, [es:APS_RL+2]
    pop es
    cmp bx, APS_LOW
    pop bx
    jae .out
.drain:                             ; the ring played out first: the rest
    push es                         ; of it is at the rate it was made for
    mov es, [aps_rseg]
    mov ax, [es:APS_RL]
    cmp ax, [es:APS_RL+2]
    pop es
    je .down
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jmp short .drain
.down:
    call os88spk_stop
    call aps_clock
    inc byte [aps_rung]
    call aps_plan
    jnc .set
    dec byte [aps_rung]             ; PAST THE LAST RUNG. IMA ADPCM stops and
    cmp byte [apw_fmt], AP_FMT_ADPCM    ; says so (the owner: only if it is
    je .err                         ; viable); PCM plays on behind the pulses,
    mov byte [aps_lag], 1           ; gaps and all - sound and nothing else
    jmp short .go                   ; is better than no sound
.set:
    call aps_setup
    jc .err
.go:
    mov al, 1
    call aps_prefill
    call os88spk_go
    mov al, 0
    jnc .out
    mov ax, ap_s_spkbusy
    call aps_say
.err:
    mov al, APX_ERR
.out:
    ret

; -----------------------------------------------------------------------------
; aps_fill - make up to APS_SPANS spans while the ring has room.
; out: CF = 0 at least one was made; CF = 1 none (full, dry, or done)
; -----------------------------------------------------------------------------
aps_fill:
    push cx
    mov cx, APS_SPANS
    stc
    pushf
.l:
    call aps_span1
    jc .out
    popf
    clc
    pushf
    loop .l
.out:
    popf
    pop cx
    ret

; -----------------------------------------------------------------------------
; aps_span1 - one span of the speaker's rate into the ring.
; out: CF = 1 nothing made
; -----------------------------------------------------------------------------
aps_span1:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    cmp byte [aps_eot], 0
    jne .none
    mov es, [aps_rseg]              ; room: RL - (TOTAL - CONS) >= a span
    mov ax, [es:APS_RL]
    sub ax, [es:APS_RL+2]
    mov bx, APS_RL
    sub bx, ax
    cmp bx, [aps_span]
    jb .none
    call aps_source                 ; CX samples at the speaker's rate
    jc .none
    mov si, [aps_soff]              ; DS:SI = the span, wherever it is
    mov ds, [aps_sseg]
    mov bx, cx                      ; BX = the span's length
    cmp byte [cs:aps_mode], APS_XL
    jae .put
    call os88spkfx_level
.put:
    mov di, [es:APS_RL]             ; into the ring at TOTAL, split at its end
    and di, APS_RL - 1
    mov ax, APS_RL
    sub ax, di
    cmp ax, cx
    jae .one
    mov cx, ax
    call aps_put
    xor di, di
    mov cx, bx
    sub cx, ax
.one:
    call aps_put
    add [es:APS_RL], bx             ; TOTAL, LAST: the ISR reads it
    clc
    jmp short .out
.none:
    stc
.out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; aps_put - DS:SI -> ES:DI, CX samples: shaped, or copied as they are - or,
; a file of counts on a Covox, through ap_cinv (in our segment: `cs xlatb`,
; os88spkfx's own idiom, DS being the source). CX > 0
aps_put:
    cmp byte [cs:aps_mode], APS_XL
    jae .copy
    jmp os88spkfx_emit
.copy:
    cld                             ; (ZF: APS_XL)
    je .xl
    rep movsb
    ret
.xl:
    push ax
    push bx
    mov bx, ap_cinv
.x:
    lodsb
    cs xlatb
    stosb
    loop .x
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; aps_source - the next span's samples at the speaker's rate.
; out: CF = 0, CX = how many (a span, or the track's last few), and the span
;      at [aps_sseg]:[aps_soff] - STRAIGHT IN THE LOOK-AHEAD RING when it is
;      1:1 and does not cross the ring's seam, so a plain 8 kHz file is never
;      copied at all; CF = 1 none yet (the disk), or none left (aps_eot)
; -----------------------------------------------------------------------------
aps_source:
    push ax
    push dx
    mov cx, [aps_span]
    mov al, [aps_k]
    cmp al, 1
    je .want
    ja .box
    ; a STEP: CX outputs need ((P + (CX - 1) step) >> 16) + 1 inputs, P the
    ; position carried (whole samples past the next read, and a fraction)
    mov ax, [aps_stepf]
    dec cx
    mul cx
    add ax, [aps_frac]
    adc dx, 0
    push dx                         ; the fraction's carry, in whole samples
    mov ax, [aps_stepi]
    mul cx
    pop dx
    add ax, dx
    add ax, [aps_posi]
    inc ax
    mov cx, ax
    jmp short .want
.box:
    xor ah, ah
    mul cx
    mov cx, ax                      ; k x a span
.want:
    call aps_get                    ; CX inputs at [aps_iseg]:[aps_ioff]
    jc .out
    mov ax, [aps_ioff]              ; (1:1: the span IS the input)
    mov [aps_soff], ax
    mov ax, [aps_iseg]
    mov [aps_sseg], ax
    mov al, [aps_k]
    cmp al, 1
    je .have
    mov word [aps_soff], aps_stage  ; (else the span is made in aps_stage)
    mov [aps_sseg], cs
    ja .dobox
    call aps_dostep                 ; CX = inputs -> CX = outputs
    jmp short .have
.dobox:
    call aps_dobox
.have:
    cmp cx, 1                       ; CF = 1 when none
.out:
    pop dx
    pop ax
    ret

    APW aps_ioff                    ; the inputs aps_get found...
    APW aps_iseg
    APW aps_soff                    ; ...and the span made of them
    APW aps_sseg

; aps_get - CX source samples: PCM8 in the look-ahead ring (in place when they
; do not cross its seam, else copied to aps_src), ADPCM decoded into apd_out.
; out: CF = 0, CX = how many (CX, or at the end of the file what is left),
;      at [aps_iseg]:[aps_ioff]; CF = 1 not yet (the disk) or none left -
;      aps_eot, and aps_tend
aps_get:
    push ax
    push di
    mov [aps_iseg], cs
    mov byte [aps_tail], 0
    cmp byte [apw_fmt], AP_FMT_ADPCM
    je .adpcm
    call ap_la_avail
    cmp ax, cx
    jae .pcm
    cmp byte [ap_src_done], 0
    je .wait                        ; the disk has more: wait for it
    mov cx, ax                      ; the tail
    mov byte [aps_tail], 1
    jcxz .end
.pcm:
    mov ax, [ap_la_rd]              ; IN PLACE, if it does not cross the seam
    and ax, AP_LA_MASK
    mov di, AP_LA_SZ
    sub di, ax
    cmp di, cx
    jb .copy
    mov [aps_ioff], ax
    mov ax, [ap_la_seg]
    mov [aps_iseg], ax
    add [ap_la_rd], cx
    jmp short .ok
.copy:
    mov di, aps_src
    mov [aps_ioff], di
    call ap_la_get
    jmp short .ok
.adpcm:
    mov word [aps_ioff], apd_out
    cmp byte [ap_src_done], 0       ; ap_stage_half's rule: decode only with a
    jne .dec                        ; whole span's INPUT in hand, or the
    call ap_la_avail                ; decoder's state runs on past a gap
    cmp ax, cx
    jb .wait
.dec:
    call apd_pull                   ; AX = PCM8 in apd_out
    cmp ax, cx
    je .full
    mov byte [aps_tail], 1
.full:
    mov cx, ax
    jcxz .end
.ok:
    pop di
    pop ax
    clc
    ret
.end:
    mov byte [aps_eot], 1
    push es
    mov es, [aps_rseg]
    mov ax, [es:APS_RL]
    pop es
    mov [aps_tend], ax
.wait:
    pop di
    pop ax
    stc
    ret

    APW aps_tend                    ; TOTAL at this track's last sample

; aps_dobox - CX inputs -> CX / k averages in aps_stage. out: CX = outputs
aps_dobox:
    push ax
    push bx
    push dx
    push si
    push di
    push ds
    push es
    mov ax, cx
    xor dx, dx
    mov bl, [aps_k]
    xor bh, bh
    div bx
    mov cx, ax                      ; the outputs
    mov dl, bl                      ; DL = k
    push cx
    push cs
    pop es
    mov di, aps_stage
    mov si, [aps_ioff]
    mov ds, [aps_iseg]
    cld
    jcxz .done
    xor ah, ah
.o:
    xor bx, bx
    mov dh, dl
.s:
    lodsb
    add bx, ax
    dec dh
    jnz .s
    mov al, [cs:aps_dbuf+bx]
    stosb
    loop .o
.done:
    pop cx
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; aps_dostep - CX inputs -> the outputs they reach, stepping 16.16 from the
; carried position, which is carried on past the inputs consumed (all CX of
; them). out: CX = outputs (at most a span). The step's whole part is PATCHED
; into the loop's `adc bx, imm16` (aps_plan), so the loop is registers only
aps_dostep:
    push ax
    push bx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    push cs
    pop es
    mov di, aps_stage
    mov [aps_nin], cx               ; the inputs
    mov cx, [aps_span]
    mov dx, [aps_frac]              ; BX.DX = the position
    mov bx, [aps_posi]
    mov bp, [aps_stepf]
    cmp byte [aps_tail], 0
    mov si, [aps_ioff]
    mov ds, [aps_iseg]
    cld
    jne .slow                       ; (the file's tail: fewer inputs)
.o:                                 ; A WHOLE SPAN: aps_source asked for
    mov al, [bx+si]                 ; exactly the inputs it reaches
    stosb
    add dx, bp
.adc:
    adc bx, 0x7FFF                  ; (PATCHED: the step's whole part)
    loop .o
    jmp short .done
.slow:
    cmp bx, [cs:aps_nin]
    jae .done
    mov al, [bx+si]
    stosb
    add dx, bp
.adc2:
    adc bx, 0x7FFF                  ; (PATCHED, the same)
    loop .slow
.done:
    pop es
    pop ds
    mov [aps_frac], dx
    sub bx, [aps_nin]               ; whole samples past what was read
    jae .p
    xor bx, bx
.p:
    mov [aps_posi], bx
    sub di, aps_stage
    mov cx, di
    pop bp
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

    APW aps_nin                     ; aps_dostep's inputs
    APB aps_tail                    ; aps_get gave fewer than asked: the end

; -----------------------------------------------------------------------------
; aps_refill - one disk read when the look-ahead ring is low. The UI task
; may; the pulses play on through it (int 13h runs with IF = 1)
; -----------------------------------------------------------------------------
aps_refill:
    cmp byte [ap_src_done], 0
    jne .out
    push ax
    call ap_la_avail
    cmp ax, AP_LA_LOW
    pop ax
    jae .out
    call ap_refill_chunk
.out:
    ret

; -----------------------------------------------------------------------------
; aps_clock - CONS since the last look, into the track's played samples:
; ap_elapsed_s and ap_played (the bar's, in the FILE's samples)
; -----------------------------------------------------------------------------
aps_clock:
    push ax
    push bx
    push cx
    push dx
    push es
    mov es, [aps_rseg]              ; what CONS has played since the last
    mov ax, [es:APS_RL+2]           ; look, in the FILE's samples: x S / R,
    mov cx, ax                      ; the remainder carried - so a play that
    sub cx, [aps_cons0]             ; comes down a rung mid-track keeps its
    mov [aps_cons0], ax             ; clock
    mov bx, [aps_rate]
    mov ax, cx                      ; (delta / R) x S ...
    xor dx, dx
    div bx
    push dx
    mul word [ap_rate]
    add [ap_played], ax
    adc [ap_played+2], dx
    pop ax                          ; ...+ ((delta % R) x S + rem) / R
    mul word [ap_rate]
    add ax, [aps_crem]
    adc dx, 0
    div bx
    mov [aps_crem], dx
    add [ap_played], ax
    adc word [ap_played+2], 0
    mov ax, [ap_played]             ; seconds = played / S
    mov dx, [ap_played+2]
    mov bx, [ap_rate]
    cmp dx, bx
    jae .out
    div bx
    mov [ap_elapsed_s], ax
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; aps_draw - the clock and the bar, when either moved, at most every
; AP_DRAW_TICKS periods, through our own clip. The only pixels that move
; -----------------------------------------------------------------------------
aps_draw:
    push ax
    push bx
    mov ax, [aps_hk]
    sub ax, [aps_drawt]
    cmp ax, AP_DRAW_TICKS
    jb .out
    mov ax, [ap_elapsed_s]
    cmp ax, [ap_last_s]
    jne .draw
    call apu_progress
    cmp ax, [ap_last_bar]
    je .out
.draw:
    mov ax, [aps_hk]
    mov [aps_drawt], ax
    mov bx, [ap_win]
    call OSAPI_WM_OBSCURED
    jc .out
    call OSAPI_WM_CLIP_SET
    jc .out
    call apu_draw_dyn
    mov ax, [ap_elapsed_s]
    mov [ap_last_s], ax
    call apu_progress
    mov [ap_last_bar], ax
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; aps_input - the keys and the mouse, polled (no events in a bracket).
; out: AL = APX_* to leave, 0 to stay
; -----------------------------------------------------------------------------
aps_input:
    push bx
    push cx
    push dx
    call OSAPI_MOUSE                ; a PRESS anywhere: back to the desktop
    mov ah, [aps_btn]
    mov [aps_btn], al
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
    mov al, APX_DESK
    jmp short .out
.stop:
    mov al, APX_STOP
    jmp short .out
.next:
    mov al, 1
    call aps_skip
    jmp short .out
.prev:
    mov al, 0xFF
    call aps_skip
.out:
    pop dx
    pop cx
    pop bx
    ret

; aps_skip - N or P inside the bracket: the ring dropped where it is, the
; next (AL = 1) or previous (0xFF) track prepared and played from its start.
; out: AL = 0 to stay, or APX_* to leave
aps_skip:
    mov [aps_dir], al
    call os88spk_stop
    call apl_advance
    jnc .go
    mov al, APX_END
    cmp byte [aps_dir], 1
    je .out                         ; past the last: the list's end
.go:
    call aps_next                   ; AL = 0 or APX_ERR
    or al, al
    jnz .out
    call os88spk_go
    mov al, 0
    jnc .out
    mov ax, ap_s_spkbusy
    call aps_say
    mov al, APX_ERR
.out:
    ret

; aps_next - apl_cur prepared, planned and set up from an empty ring.
; out: AL = 0, or APX_ERR (ap_msg set)
aps_next:
    call ap_prep_track
    jc .err
    call aps_trk_reset
    call aps_plan
    jc .err
    call aps_setup
    jc .err
    call apu_repaint                ; the name, the list's marker, the clock
    xor al, al
    ret
.err:
    mov al, APX_ERR
    ret

; -----------------------------------------------------------------------------
; aps_trackend - this track's source all made and PLAYED OUT: the next one,
; prepared and set up from an empty ring. (A gap of the next file's opening
; read: joining two tracks of the same plan with none is left for later.)
; out: AL = 0 to stay, APX_END at the list's end, APX_ERR
; -----------------------------------------------------------------------------
aps_trackend:
    xor al, al
    cmp byte [aps_eot], 0
    je .out
    push es
    mov es, [aps_rseg]              ; is the ring played out?
    mov bx, [es:APS_RL]
    cmp bx, [es:APS_RL+2]
    pop es
    jne .out                        ; not yet (a different plan must wait,
                                    ; and so does the list's end)
    mov al, 1
    call apl_advance
    mov al, APX_END
    jc .out
    call os88spk_stop
    call aps_next
    or al, al
    jnz .out
    call os88spk_go
    mov al, 0
    jnc .out
    mov ax, ap_s_spkbusy
    call aps_say
    mov al, APX_ERR
.out:
    ret
