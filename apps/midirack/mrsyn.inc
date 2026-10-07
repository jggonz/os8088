; =============================================================================
; os8088 - apps/midirack/mrsyn.inc
;
; THE SYNTH (SPEC.md 105.7): eight-bit samples made from the notes, for the
; two outputs that play SAMPLES - the PC speaker (mrk_, in a rate bracket) and
; a Sound Blaster's DSP (mrb_, behind the desktop). Prefix mry_.
;
; IT IS PRICED BY EDGES, NOT BY SAMPLES TIMES VOICES. A voice is a pulse wave
; - a square, a 25% or a 12.5% pulse, or for a drum a run of random-length
; pulses (noise) - and a pulse wave is flat between its edges. So a voice
; writes NOTHING per sample: at each edge it adds +-2a into a DELTA buffer at
; that sample (mry_voice), and one pass over the buffer (mry_mix) keeps the
; running sum and turns it into the output byte through a 2,048-entry table.
; A 440 Hz voice at 8 kHz is 880 edges a second, ~60 cycles each on an 8088;
; the pass is ~60 cycles a sample whatever the voice count. Six voices and the
; pass are ~20% of a 4.77 MHz machine (SPEC.md 105.7.4).
;
; THE TABLE IS WHERE THE OUTPUTS DIFFER, and is also the mixer's compressor:
; the running level is -1023..+1023 and the table maps it through a soft clip
; y = 127 x / (|x| + 64) - gain 2 near silence, approaching full scale only
; asymptotically - to an unsigned sample for the card, or straight to the PWM
; count os88spk_init tabled for the speaker (SPEC.md 34.11.2), so the speaker
; pays no second translation.
;
; TIME. A span is MRY_SPAN samples; edges are kept in 1/128 sample (9.7) from
; the span's start, so pitch is exact to a few cents up to the top octave. An
; event lands on its own sample: mry_span renders the voices up to it, lets
; the sequencer play it, and goes on. An envelope moves at span boundaries,
; every ~16 ms (mry_etick).
; =============================================================================

MRY_SPAN    equ 128                 ; samples a span
MRY_SPANF   equ MRY_SPAN * 128      ; ...in 1/128 sample
MRY_MAXV    equ 8
MRY_AMAX    equ 120                 ; one voice's peak, of the level's 1023
MRY_LMAX    equ 49024               ; the longest half-period: 383 samples -
                                    ; MRY_SPANF + this must fit a word (an edge
                                    ; is at most one half past the span), and
                                    ; at 44.1 kHz that is down to 57 Hz
MRY_LMIN    equ 160                 ; ...and the shortest: past ~Nyquist, mute

    MRW mry_rate                    ; the sample rate the pitches are for
    MRB mry_nv                      ; voices
    MRB mry_ediv                    ; spans an envelope step
    MRB mry_ecnt
    MRW mry_lev                     ; the running level, biased by 1024
    MRW mry_lfsr                    ; the noise
    MRW mry_slo                     ; the span's first sample, 32 bits
    MRW mry_shi
    MRW mry_pos                     ; where the voices have been rendered to
    MRBUF mry_t, MRY_MAXV * 2       ; per voice: the next edge (1/128 sample)
    MRBUF mry_l0, MRY_MAXV * 2      ; ...the half-length after it
    MRBUF mry_l1, MRY_MAXV * 2      ; ...and the one after that (a noise
                                    ;    voice: its run mask)
    MRBUF mry_a, MRY_MAXV * 2       ; ...its amplitude, 0..MRY_AMAX
    MRBUF mry_slide, MRY_MAXV * 2   ; ...a drum's fall in pitch, a step
    MRBUF mry_hi, MRY_MAXV          ; ...1 = the wave is high now
    MRBUF mry_kind, MRY_MAXV        ; ...0 tone / 1 noise
    MRBUF mry_stage, MRY_MAXV       ; ...0 silent, 1 attack, 2 decay,
                                    ;    3 sustain, 4 release
    MRBUF mry_lvl, MRY_MAXV         ; ...the envelope, 0..255
    MRBUF mry_atk, MRY_MAXV         ; ...and its four numbers
    MRBUF mry_dec, MRY_MAXV
    MRBUF mry_sus, MRY_MAXV
    MRBUF mry_rel, MRY_MAXV
    MRBUF mry_vamp, MRY_MAXV        ; ...velocity x volume, 0..255
    MRBUF mry_duty, MRY_MAXV
    MRBUF mry_dbuf, (MRY_SPAN + 2) * 2      ; the deltas
    MRBUF mry_otab, 2048            ; level -> the output byte

mry_vt:     dw mry_on, mry_off, mr_nop, mry_ctl, mry_bend, mry_susup, mry_alloff

; -----------------------------------------------------------------------------
; mry_open - the synth for rate AX and voices CL, with the output table built
; from the 256-byte map at DS:SI (0 = the identity: a card's unsigned bytes;
; a speaker's PWM table otherwise - os88spk's at RL + SND_EXT_TAB, copied in)
; -----------------------------------------------------------------------------
mry_open:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [mry_rate], ax
    mov [mry_nv], cl
    mov al, cl
    call mrv_reset
    mov ax, [mry_rate]              ; an envelope step every ~16 ms: R / 8000
    add ax, 4000                    ; spans of 128, rounded
    xor dx, dx
    mov bx, 8000
    div bx
    or al, al
    jnz .ed
    inc ax
.ed:
    mov [mry_ediv], al
    mov byte [mry_ecnt], 1
    mov word [mry_lev], 1024
    mov word [mry_lfsr], 0xACE1
    xor ax, ax
    mov [mry_slo], ax
    mov [mry_shi], ax
    xor bx, bx
.v:
    mov byte [mry_stage+bx], 0
    mov byte [mry_hi+bx], 0
    push bx
    shl bx, 1
    mov word [mry_a+bx], 0
    pop bx
    inc bx
    cmp bx, MRY_MAXV
    jb .v
    ; --- the table: soft clip, then the map ----------------------------------
    mov di, mry_otab
    mov bx, -1024                   ; BX = x
.tab:
    mov ax, bx                      ; y = 127 x / (|x| + 64)
    or ax, ax
    jns .abs
    neg ax
.abs:
    add ax, 64
    mov cx, ax                      ; CX = |x| + 64
    mov ax, bx
    mov dx, 127
    imul dx                         ; DX:AX = 127 x
    idiv cx                         ; AX = y, -126..126
    add al, 128                     ; unsigned
    or si, si
    jz .put
    push bx
    mov bl, al
    xor bh, bh
    mov al, [si+bx]                 ; ...through the map
    pop bx
.put:
    mov [di], al
    inc di
    inc bx
    cmp bx, 1024
    jl .tab
    mov word [mrq_vt], mry_vt
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mry_span - one span into the ring at ES:DI (ring offset), CX = the bytes to
; the ring's end (a span that crosses it wraps to ES:0). The sequencer is
; played up to the span's end on the way. out: nothing; the clock advanced
; -----------------------------------------------------------------------------
mry_span:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push di
    push cx
    push ds
    pop es
    mov di, mry_dbuf                ; the deltas, cleared
    mov cx, MRY_SPAN + 2
    xor ax, ax
    cld
    rep stosw
    mov word [mry_pos], 0           ; (an envelope's step lands at 0)
    dec byte [mry_ecnt]             ; the envelopes, every [mry_ediv] spans
    jnz .ev
    mov al, [mry_ediv]
    mov [mry_ecnt], al
    call mry_etick
.ev:
    cmp byte [mr_seqon], 0
    je .rest
    call mrq_due                    ; DX:AX = the next event's sample
    jc .rest
    sub ax, [mry_slo]
    sbb dx, [mry_shi]
    jb .now                         ; overdue: at the span's start
    or dx, dx
    jnz .rest
    cmp ax, MRY_SPAN
    jae .rest
    jmp short .at
.now:
    xor ax, ax
.at:
    cmp ax, [mry_pos]
    jb .play
    call mry_render                 ; the voices up to the event
.play:
    call mrq_step                   ; ...and every event at its tick
    jmp short .ev
.rest:
    mov ax, MRY_SPAN
    call mry_render
    xor bx, bx                      ; every edge back by a span
.sh:
    cmp bl, [mry_nv]
    jae .mix
    push bx
    shl bx, 1
    sub word [mry_t+bx], MRY_SPANF
    jae .t
    mov word [mry_t+bx], 0
.t:
    pop bx
    inc bx
    jmp short .sh
.mix:
    pop cx                          ; CX = bytes to the ring's end
    pop di
    pop es                          ; ES:DI = where the span goes
    mov si, mry_dbuf
    mov dx, [mry_lev]
    cmp cx, MRY_SPAN
    jae .one
    push cx
    call mry_mix                    ; up to the ring's end...
    pop ax
    mov cx, MRY_SPAN
    sub cx, ax
    xor di, di                      ; ...and the rest from its start
.one:
    cmp cx, MRY_SPAN
    jbe .m
    mov cx, MRY_SPAN
.m:
    call mry_mix
    mov [mry_lev], dx
    add word [mry_slo], MRY_SPAN
    adc word [mry_shi], 0
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mry_mix - CX samples: DS:SI = deltas (advanced), DX = the running level
; (biased), ES:DI = the output (advanced). The one per-sample loop
; -----------------------------------------------------------------------------
mry_mix:
    jcxz .out
    cld
    shr cx, 1                       ; TWO A TURN: the loop's 17 cycles are a
    jnc .two                        ; fifth of a sample's cost on an 8088
    lodsw                           ; (an odd count: one first)
    add dx, ax
    mov bx, dx
    mov al, [mry_otab+bx]
    stosb
    jcxz .out
.two:
    lodsw                           ; NO MASK: the level is the sum of at most
    add dx, ax                      ; MRY_MAXV x MRY_AMAX = 960 either side of
    mov bx, dx                      ; the 1,024 bias, inside the table by
    mov al, [mry_otab+bx]           ; construction
    stosb
    lodsw
    add dx, ax
    mov bx, dx
    mov al, [mry_otab+bx]
    stosb
    loop .two
.out:
    ret

; -----------------------------------------------------------------------------
; mry_render - every sounding voice from [mry_pos] up to sample AX
; -----------------------------------------------------------------------------
mry_render:
    push ax
    push bx
    push cx
    push ax
    mov cl, 7
    shl ax, cl
    mov cx, ax                      ; CX = the limit in 1/128 sample
    pop ax
    xor bx, bx
.v:
    cmp bl, [mry_nv]
    jae .done
    cmp byte [mry_stage+bx], 0
    je .n
    call mry_voice
.n:
    inc bx
    jmp short .v
.done:
    pop cx
    pop bx
    pop ax
    mov [mry_pos], ax
    ret

; -----------------------------------------------------------------------------
; mry_voice - voice BX's edges up to CX (1/128 sample), into the deltas
; -----------------------------------------------------------------------------
mry_voice:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [mry_cur], bx
    mov si, bx                      ; SI = the voice, for the tables
    shl bx, 1
    mov bp, [mry_t+bx]
    cmp bp, cx
    jae .out                        ; no edge before the limit
    mov dx, [mry_l0+bx]
    mov di, [mry_l1+bx]
    mov ax, [mry_a+bx]              ; the delta: +2a rising, -2a falling
    shl ax, 1
    cmp byte [mry_hi+si], 0
    je .up
    neg ax
.up:
    cmp byte [mry_kind+si], 0
    jne .noise
    xor si, si                      ; SI = the edges, for their parity
.e:
    mov bx, bp                      ; (t >> 7) x 2
    shl bx, 1
    mov bl, bh
    xor bh, bh
    shl bx, 1
    add [mry_dbuf+bx], ax
    neg ax
    add bp, dx
    xchg dx, di
    xor si, 1
    cmp bp, cx
    jb .e
    mov bx, [mry_cur]
    mov ax, si
    xor [mry_hi+bx], al             ; an odd count turned it over
    shl bx, 1
    mov [mry_l0+bx], dx
    mov [mry_l1+bx], di
    mov [mry_t+bx], bp
    jmp short .out
.noise:
    mov si, [mry_lfsr]
.ne:
    mov bx, bp
    shl bx, 1
    mov bl, bh
    xor bh, bh
    shl bx, 1
    add [mry_dbuf+bx], ax
    neg ax
    shr si, 1                       ; a Galois LFSR: the next run
    jnc .nx
    xor si, 0xB400
.nx:
    mov dx, si
    and dx, di                      ; DI = the run mask
    add dx, 128                     ; at least a sample
    add bp, dx
    cmp bp, cx
    jb .ne
    mov [mry_lfsr], si
    mov bx, [mry_cur]
    mov byte [mry_hi+bx], 0         ; (a noise voice's sign is in its delta)
    or ax, ax
    jns .nl
    mov byte [mry_hi+bx], 1
.nl:
    shl bx, 1
    mov [mry_t+bx], bp
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

    MRW mry_cur

; -----------------------------------------------------------------------------
; mry_etick - every voice's envelope one step, at the span's start: its new
; amplitude's difference goes into delta 0, and a voice whose envelope has
; reached nothing is silent and free
; -----------------------------------------------------------------------------
mry_etick:
    push ax
    push bx
    push cx
    push dx
    xor bx, bx
.v:
    cmp bl, [mry_nv]
    jae .out
    mov al, [mry_stage+bx]
    or al, al
    jz .n
    mov cl, [mry_lvl+bx]
    cmp al, 1
    je .atk
    cmp al, 2
    je .dec
    cmp al, 4
    je .rel
    jmp short .amp                  ; sustain: held
.atk:
    add cl, [mry_atk+bx]
    jnc .a1
    mov cl, 255
.a1:
    cmp cl, 255
    jb .amp
    mov byte [mry_stage+bx], 2
    jmp short .amp
.dec:
    mov ch, [mry_dec+bx]
    or ch, ch
    jz .tosus
    sub cl, ch
    jc .floor
    cmp cl, [mry_sus+bx]
    ja .amp
.floor:
    mov cl, [mry_sus+bx]
.tosus:
    mov byte [mry_stage+bx], 3
    or cl, cl
    jnz .amp
    jmp short .done                 ; decayed to nothing: the voice is over
.rel:
    sub cl, [mry_rel+bx]
    jc .done
    jz .done
.amp:
    mov [mry_lvl+bx], cl
    call mry_drift                  ; a drum's falling pitch
    call mry_setamp
    jmp short .n
.done:
    mov byte [mry_lvl+bx], 0
    xor cl, cl
    call mry_setamp                 ; its contribution out...
    mov byte [mry_stage+bx], 0      ; ...and the voice free
    mov byte [mrv_st+bx], MRV_FREE
.n:
    inc bx
    jmp .v
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mry_drift - voice BX's pitch falls by its slide (a kick, a tom)
mry_drift:
    push ax
    push bx
    shl bx, 1
    mov ax, [mry_slide+bx]
    or ax, ax
    jz .out
    add [mry_l0+bx], ax
    add [mry_l1+bx], ax
    cmp word [mry_l0+bx], MRY_LMAX
    jbe .out
    mov word [mry_l0+bx], MRY_LMAX  ; held at the longest: one past it would
    mov word [mry_l1+bx], MRY_LMAX  ; let an edge pass the word
    mov word [mry_slide+bx], 0
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mry_setamp - voice BX's amplitude from its envelope (CL) and its vamp; the
; change goes in as a step at [mry_pos] - 0 at a span's start, the event's
; sample at a note-on
; -----------------------------------------------------------------------------
mry_setamp:
    push ax
    push cx
    push dx
    push di
    mov al, cl
    mul byte [mry_vamp+bx]          ; AX = level x vamp
    mov cx, MRY_AMAX
    mul cx                          ; DX = ... x AMAX >> 16
    mov ax, dx                      ; AX = the new amplitude
    push bx
    shl bx, 1
    mov cx, [mry_a+bx]              ; CX = the old
    mov [mry_a+bx], ax
    pop bx
    sub cx, ax                      ; CX = old - new
    jz .out
    cmp byte [mry_hi+bx], 0         ; high: the contribution was +old, is
    je .lo                          ; +new: add new - old
    neg cx
.lo:                                ; low: it was -old, is -new: old - new
    mov di, [mry_pos]
    shl di, 1
    add [mry_dbuf+di], cx
.out:
    pop di
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mry_cut - voice BX silenced at [mry_pos] (a steal, a restart): its
; contribution out of the sum, its amplitude 0
; -----------------------------------------------------------------------------
mry_cut:
    push cx
    xor cl, cl
    mov byte [mry_lvl+bx], 0
    call mry_setamp
    mov byte [mry_stage+bx], 0
    pop cx
    ret

; -----------------------------------------------------------------------------
; mry_on - MRO_ON: AL = channel, AH = key, CL = velocity
; -----------------------------------------------------------------------------
mry_on:
    push ax
    push bx
    push cx
    push dx
    push si
    mov dl, cl                      ; DL = the velocity
    mov word [mrv_want], 0xFFFF
    cmp al, MR_DRUMCH
    je .drum
    call mrv_alloc                  ; BX = the voice
    call mry_cut                    ; whatever it was playing, gone
    push bx
    mov bl, al
    xor bh, bh
    mov si, bx                      ; SI = the channel
    mov bl, [mrc_prog+bx]
    mov cl, [mrt_syn+bx]            ; CL = duty | env << 2
    pop bx
    mov byte [mry_kind+bx], 0
    push bx
    shl bx, 1
    mov word [mry_slide+bx], 0
    pop bx
    mov al, cl
    and al, 3
    mov [mry_duty+bx], al
    push cx                         ; the envelope class's four numbers
    and cl, 0x1C                    ; class x 4: the row
    xor ch, ch
    push si
    mov si, cx
    mov al, [mrt_env+si]
    mov [mry_atk+bx], al
    mov al, [mrt_env+si+1]
    mov [mry_dec+bx], al
    mov al, [mrt_env+si+2]
    mov [mry_sus+bx], al
    mov al, [mrt_env+si+3]
    mov [mry_rel+bx], al
    pop si
    pop cx
    call mry_vel                    ; vamp from DL and channel SI
    call mry_tune                   ; the lengths from the key (AH), the bend
    mov al, [mry_atk+bx]
    mov cl, al
    cmp al, 255
    jne .att
    mov byte [mry_stage+bx], 2      ; instant: straight to the decay
    jmp short .go
.att:
    mov byte [mry_stage+bx], 1
.go:
    mov [mry_lvl+bx], cl
    call mry_start
    jmp .out
.drum:
    cmp ah, MRT_DRLO
    jb .out
    cmp ah, MRT_DRHI
    ja .out
    call mrv_alloc
    call mry_cut
    mov si, 9                       ; the kit's channel, for the volume
    call mry_vel
    push ax
    mov al, ah
    sub al, MRT_DRLO
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov si, ax
    pop ax
    mov al, [mrt_syndr+si+3]        ; the drum's own level, x vamp
    mul byte [mry_vamp+bx]
    mov [mry_vamp+bx], ah
    mov al, [mrt_syndr+si]
    mov [mry_kind+bx], al
    mov al, [mrt_syndr+si+2]
    mov [mry_dec+bx], al
    mov byte [mry_atk+bx], 255
    mov byte [mry_sus+bx], 0
    mov byte [mry_rel+bx], 64
    mov byte [mry_duty+bx], 0
    mov byte [mry_stage+bx], 2
    mov byte [mry_lvl+bx], 255
    mov cl, [mrt_syndr+si+1]        ; pitch, or colour
    cmp byte [mry_kind+bx], 0
    jne .noise
    mov ah, cl                      ; a pitched drum: its note, no bend
    mov si, 0xFFFF
    call mry_tune
    push bx
    shl bx, 1
    mov ax, [mry_l0+bx]             ; ...falling an eighth of itself a step
    mov cl, 3
    shr ax, cl
    mov [mry_slide+bx], ax
    pop bx
    jmp short .dgo
.noise:
    mov ax, 128                     ; the run mask: 2^colour samples
    shl ax, cl
    dec ax
    push bx
    shl bx, 1
    mov [mry_l1+bx], ax
    mov word [mry_l0+bx], 128
    mov word [mry_slide+bx], 0
    pop bx
.dgo:
    mov cl, 255
    call mry_start
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mry_start - voice BX begins at [mry_pos] at envelope level CL: its first
; edge is now, rising
mry_start:
    push ax
    push di
    mov byte [mry_hi+bx], 0
    mov di, [mry_pos]
    push di
    mov ax, di
    push cx
    mov cl, 7
    shl ax, cl
    pop cx
    push bx
    shl bx, 1
    mov [mry_t+bx], ax
    pop bx
    pop di
    call mry_setamp                 ; low at -a: the first edge lifts it to +a
    pop di
    pop ax
    ret

; mry_vel - voice BX's vamp from velocity DL and channel SI's volume
mry_vel:
    push ax
    push bx
    push di
    mov di, bx
    mov bx, si
    call mr_level                   ; AL = 0..127
    mov bx, ax
    mov al, [mrt_amp+bx]
    mov [mry_vamp+di], al
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mry_tune - voice BX's two half-lengths from key AH and channel SI's bend
; (SI = 0xFFFF: no bend), at its duty. The period, in 1/128 sample:
;   P = (R << 18) / (F << octave), F = octave 0's Hz x 2048 (mrt_fq)
; done as q = (R << 16) / F, P = (q << 2) >> octave. Too long and it climbs
; an octave; too short (past ~Nyquist) and the voice is muted.
; -----------------------------------------------------------------------------
mry_tune:
    push ax
    push cx
    push dx
    push si
    push di
    cmp si, 0xFFFF
    je .nob
    push bx
    mov bx, si
    call mr_pitch                   ; AX = the pitch
    pop bx
    jmp short .p
.nob:
    mov al, ah
    xor ah, ah
    mov cl, 5
    shl ax, cl
.p:
    xor dx, dx
    mov cx, 12 * MRT_FINE
    div cx                          ; AX = octave, DX = step
    mov [mry_oct], al
    mov si, dx
    shl si, 1
    mov cx, [mrt_fq+si]             ; CX = F
    mov ax, [mry_rate]              ; q = (R << 16) / F, in two divides:
    xor dx, dx                      ; R may pass F at 22 kHz
    div cx                          ; AX = the high word, DX = the rest
    push ax
    xor ax, ax
    div cx                          ; AX = the low word
    pop dx                          ; DX:AX = q, then << 2 >> octave
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov cl, [mry_oct]
    xor ch, ch
    jcxz .sh0
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
.sh0:
.fit:                               ; DX:AX = P, 32 bits: the short half is
    mov cl, [mry_duty+bx]           ; P >> (duty + 1), the long one the rest,
    inc cl                          ; and the long one must fit MRY_LMAX
    mov si, ax
    mov di, dx                      ; DI:SI = P...
.s:
    shr di, 1
    rcr si, 1
    dec cl
    jnz .s                          ; ...>> (duty + 1): the short half
    push ax
    push dx
    sub ax, si
    sbb dx, di                      ; DX:AX = the long half
    jnz .long
    cmp ax, MRY_LMAX
    ja .long
    mov cx, ax                      ; CX = the long half
    pop dx
    pop ax
    cmp si, MRY_LMIN / 2
    jb .mute                        ; (DI is 0: short <= long <= LMAX)
    push bx
    shl bx, 1
    mov [mry_l0+bx], si             ; high for the short half (the duty)
    mov [mry_l1+bx], cx
    pop bx
    jmp short .out
.long:
    pop dx
    pop ax
    shr dx, 1                       ; too long: an octave up
    rcr ax, 1
    jmp short .fit
.mute:
    mov byte [mry_vamp+bx], 0       ; past Nyquist: nothing to hear
    push bx
    shl bx, 1
    mov word [mry_l0+bx], MRY_LMAX
    mov word [mry_l1+bx], MRY_LMAX
    pop bx
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

    MRB mry_oct

; mry_off - MRO_OFF: the voice into its release
mry_off:
    push bx
    call mrv_off
    jc .out
    jnz .out                        ; held by the pedal
    call mry_relv
.out:
    pop bx
    ret

; mry_relv - voice BX into its release (an ended envelope stays ended)
mry_relv:
    cmp byte [mry_stage+bx], 0
    je .out
    mov byte [mry_stage+bx], 4
.out:
    ret

mry_susup:
    push bx
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    cmp byte [mrv_st+bx], MRV_HELD
    jne .n
    cmp [mrv_ch+bx], al
    jne .n
    mov byte [mrv_st+bx], MRV_REL
    call mry_relv
.n:
    inc bx
    jmp short .v
.out:
    pop bx
    ret

mry_alloff:
    push bx
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    cmp byte [mrv_st+bx], MRV_FREE
    je .n
    cmp al, 0xFF
    je .k
    cmp [mrv_ch+bx], al
    jne .n
.k:
    mov byte [mrv_st+bx], MRV_REL
    call mry_relv
.n:
    inc bx
    jmp short .v
.out:
    pop bx
    ret

; mry_ctl - MRO_CTL: channel AL's voices take its new volume at the next step
mry_ctl:
    push bx
    push dx
    push si
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    cmp byte [mrv_st+bx], MRV_FREE
    je .n
    cmp [mrv_ch+bx], al
    jne .n
    cmp al, MR_DRUMCH
    je .n                           ; a struck drum keeps its level
    mov dl, [mrv_vel+bx]
    mov si, ax
    and si, 0x0F
    call mry_vel
.n:
    inc bx
    jmp short .v
.out:
    pop si
    pop dx
    pop bx
    ret

; mry_bend - MRO_BEND: channel AL's sounding voices retuned
mry_bend:
    push ax
    push bx
    push si
    cmp al, MR_DRUMCH
    je .out
    mov si, ax
    and si, 0x0F
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    cmp byte [mry_stage+bx], 0
    je .n
    cmp [mrv_ch+bx], al
    jne .n
    mov ah, [mrv_key+bx]
    call mry_tune
.n:
    inc bx
    jmp short .v
.out:
    pop si
    pop bx
    pop ax
    ret

; mry_hush - every voice silent now (a pause, a stop): the level to rest
mry_hush:
    push bx
    xor bx, bx
.v:
    cmp bx, MRY_MAXV
    jae .out
    mov byte [mry_stage+bx], 0
    mov byte [mrv_st+bx], MRV_FREE
    push bx
    shl bx, 1
    mov word [mry_a+bx], 0
    pop bx
    inc bx
    jmp short .v
.out:
    mov word [mry_lev], 1024
    pop bx
    ret

%if MRY_MAXV * MRY_AMAX > 1023
  %error "mry_mix reads its table unmasked: the voices' sum must stay inside it"
%endif

%if MRY_SPANF + MRY_LMAX > 65535
  %error "an edge one half past the span must still fit a word"
%endif
