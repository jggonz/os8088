; =============================================================================
; os8088 - apps/midirack/mrchan.inc
;
; THE SIXTEEN CHANNELS (SPEC.md 105.5.2) and THE VOICES (105.5.4). Prefixes
; mrc_ and mrv_.
;
; A channel is what the file talks to: a program, a volume, an expression, a
; pan, a bend and its range, a sustain pedal. The sequencer hands every
; channel event to mr_chan_event, which keeps that state and tells the
; output what moved through [mrq_vt] - so the output never parses MIDI.
;
; A VOICE is what the output sounds a note on, and there are fewer of them
; than a file can ask for: 8 on an OPL2, 17 on an OPL3, 4..8 for the synth.
; mrv_alloc is the one policy for all three - a free voice (one already
; carrying the patch first), then the oldest released one, then the oldest
; held by the pedal, then the oldest still sounding.
; =============================================================================

MR_NCH      equ 16
MR_MAXV     equ 17
MR_DRUMCH   equ 9                   ; channel 10, counted from 0

MRV_FREE    equ 0
MRV_ON      equ 1
MRV_HELD    equ 2                   ; released, held by the pedal
MRV_REL     equ 3                   ; released: its envelope is falling

    MRBUF mrc_prog, MR_NCH          ; program
    MRBUF mrc_first, MR_NCH         ; the file's first program (dry run)
    MRBUF mrc_vol, MR_NCH           ; CC 7
    MRBUF mrc_expr, MR_NCH          ; CC 11
    MRBUF mrc_pan, MR_NCH           ; CC 10
    MRBUF mrc_sus, MR_NCH           ; CC 64 >= 64
    MRBUF mrc_brange, MR_NCH        ; the bend range, semitones (RPN 0)
    MRBUF mrc_rpnl, MR_NCH          ; RPN LSB / MSB (0x7F = null)
    MRBUF mrc_rpnm, MR_NCH
    MRBUF mrc_bend, MR_NCH * 2      ; the bend, in 1/32 semitones, signed
    MRBUF mrc_lev, MR_NCH           ; the Channel Rack's level, 0..127
    MRBUF mrc_dirty, MR_NCH         ; bit 0 the program moved (repaint the
                                    ; name), bit 1 the level
    MRW mr_nnotlo                   ; notes in the file (the dry run)
    MRW mr_nnothi
    MRW mr_chused                   ; channels the file plays (bit per channel)

    MRBUF mrv_ch, MR_MAXV
    MRBUF mrv_key, MR_MAXV
    MRBUF mrv_st, MR_MAXV
    MRBUF mrv_vel, MR_MAXV
    MRBUF mrv_age, MR_MAXV * 2
    MRBUF mrv_tag, MR_MAXV * 2      ; the output's: FM keeps the patch here
    MRB mrv_n                       ; voices the output has
    MRW mrv_clock                   ; the age counter
    MRW mrv_want                    ; mrv_alloc's preferred tag

; -----------------------------------------------------------------------------
; mr_chan_reset - every channel to General MIDI's power-on state, with each
; channel's program the file's FIRST one, so the rack names the instruments
; before a note has played
; -----------------------------------------------------------------------------
mr_chan_reset:
    push ax
    push bx
    xor bx, bx
.c:
    mov al, [mrc_first+bx]
    cmp al, 0x80
    jb .p
    xor al, al
.p:
    mov [mrc_prog+bx], al
    mov byte [mrc_vol+bx], 100
    mov byte [mrc_expr+bx], 127
    mov byte [mrc_pan+bx], 64
    mov byte [mrc_sus+bx], 0
    mov byte [mrc_brange+bx], 2
    mov byte [mrc_rpnl+bx], 0x7F
    mov byte [mrc_rpnm+bx], 0x7F
    mov byte [mrc_lev+bx], 0
    mov byte [mrc_dirty+bx], 3
    push bx
    shl bx, 1
    mov word [mrc_bend+bx], 0
    pop bx
    inc bx
    cmp bx, MR_NCH
    jb .c
    pop bx
    pop ax
    ret

; mr_first_reset - before a dry run: no first program known yet
mr_first_reset:
    push ax
    push bx
    xor bx, bx
.c:
    mov byte [mrc_first+bx], 0xFF
    inc bx
    cmp bx, MR_NCH
    jb .c
    xor ax, ax
    mov [mr_nnotlo], ax
    mov [mr_nnothi], ax
    mov [mr_chused], ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mr_vt_call - call the output's verb BP (an MRO_* offset); preserves all
; -----------------------------------------------------------------------------
mr_vt_call:
    push bx
    mov bx, [mrq_vt]
    add bx, bp
    call [bx]
    pop bx
    ret

; -----------------------------------------------------------------------------
; mr_chan_event - one channel event. CL = the status, CH = data 1, DL = data 2
; (both already 0..127). Preserves every register.
; -----------------------------------------------------------------------------
mr_chan_event:
    push ax
    push bx
    push cx
    push dx
    push bp
    mov al, cl
    and al, 0x0F                    ; AL = the channel
    mov bl, al
    xor bh, bh                      ; BX = the channel, for the tables
    and cl, 0xF0
    cmp cl, 0x80
    je .off
    cmp cl, 0x90
    je .on
    cmp cl, 0xB0
    je .cc
    cmp cl, 0xC0
    je .prog
    cmp cl, 0xE0
    je .bend
    jmp .out                        ; aftertouch and pressure: not heard here
.on:
    or dl, dl
    jz .off                         ; velocity 0 is a note-off
    push ax
    push cx
    mov cl, al                      ; the channel's bit in mr_chused
    mov ax, 1
    shl ax, cl
    or [mr_chused], ax
    pop cx
    pop ax
    cmp byte [mr_dry], 0
    je .play
    add word [mr_nnotlo], 1
    adc word [mr_nnothi], 0
    jmp .out
.play:
    call mr_level_hit               ; the rack's bar, from velocity x volume
    mov ah, ch                      ; AH = the key
    mov cl, dl                      ; CL = the velocity
    mov bp, MRO_ON
    call mr_vt_call
    jmp .out
.off:
    mov ah, ch
    mov bp, MRO_OFF
    call mr_vt_call
    jmp .out
.prog:
    cmp byte [mrc_first+bx], 0xFF
    jne .pset
    mov [mrc_first+bx], ch          ; the dry run's: the file's first
.pset:
    mov [mrc_prog+bx], ch
    or byte [mrc_dirty+bx], 1
    mov bp, MRO_PROG
    call mr_vt_call
    jmp .out
.bend:
    mov al, dl                      ; (d2 << 7 | d1) - 8192, signed
    xor ah, ah
    mov cl, 7
    shl ax, cl
    or al, ch
    sub ax, 8192
    mov cl, [mrc_brange+bx]         ; x range / 256: 1/32 semitones
    xor ch, ch
    imul cx                         ; DX:AX = bend x range
    mov al, ah                      ; >> 8, as a signed word
    mov ah, dl
    shl bx, 1
    mov [mrc_bend+bx], ax
    shr bx, 1
    mov al, bl
    mov bp, MRO_BEND
    call mr_vt_call
    jmp .out
.cc:
    cmp ch, 7
    je .vol
    cmp ch, 10
    je .pan
    cmp ch, 11
    je .expr
    cmp ch, 64
    je .sus
    cmp ch, 6
    je .data
    cmp ch, 100
    je .rpnl
    cmp ch, 101
    je .rpnm
    cmp ch, 121
    je .reset
    cmp ch, 120
    jb .out
    cmp ch, 121
    je .out
    cmp ch, 122
    je .out                         ; local control: nothing here
    mov bp, MRO_ALLOFF              ; 120 and 123..127: every note off
    call mr_vt_call
    jmp .out
.vol:
    mov [mrc_vol+bx], dl
    jmp short .ctl
.pan:
    mov [mrc_pan+bx], dl
    jmp short .ctl
.expr:
    mov [mrc_expr+bx], dl
.ctl:
    mov bp, MRO_CTL
    call mr_vt_call
    jmp .out
.sus:
    cmp dl, 64
    jb .susup
    mov byte [mrc_sus+bx], 1
    jmp .out
.susup:
    cmp byte [mrc_sus+bx], 0
    je .out
    mov byte [mrc_sus+bx], 0
    mov bp, MRO_SUSUP
    call mr_vt_call
    jmp .out
.rpnl:
    mov [mrc_rpnl+bx], dl
    jmp .out
.rpnm:
    mov [mrc_rpnm+bx], dl
    jmp .out
.data:
    cmp byte [mrc_rpnm+bx], 0       ; RPN 0,0: the bend range, in semitones
    jne .out
    cmp byte [mrc_rpnl+bx], 0
    jne .out
    cmp dl, 24
    jbe .rng
    mov dl, 24
.rng:
    mov [mrc_brange+bx], dl
    jmp .out
.reset:
    mov byte [mrc_expr+bx], 127
    mov byte [mrc_rpnl+bx], 0x7F
    mov byte [mrc_rpnm+bx], 0x7F
    shl bx, 1
    mov word [mrc_bend+bx], 0
    shr bx, 1
    mov bp, MRO_BEND
    call mr_vt_call
    mov bp, MRO_CTL
    call mr_vt_call
    cmp byte [mrc_sus+bx], 0
    je .out
    mov byte [mrc_sus+bx], 0
    mov bp, MRO_SUSUP
    call mr_vt_call
.out:
    pop bp
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mr_level - AL = 0..127, the loudness a note on channel BX at velocity DL
; would have: vel x vol x expr / 127^2. Preserves all but AX
; -----------------------------------------------------------------------------
mr_level:
    push dx
    mov al, dl
    mul byte [mrc_vol+bx]           ; AX = vel x vol (<= 16129)
    shl ax, 1                       ; / 128, rounded the cheap way: x2, high
    mov al, ah                      ; byte
    mul byte [mrc_expr+bx]
    shl ax, 1
    mov al, ah
    cmp al, 127
    jbe .ok
    mov al, 127
.ok:
    xor ah, ah
    pop dx
    ret

; mr_level_hit - the rack's bar for channel BX takes a note at velocity DL
mr_level_hit:
    push ax
    call mr_level
    cmp al, [mrc_lev+bx]
    jbe .out
    mov [mrc_lev+bx], al
    or byte [mrc_dirty+bx], 2
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; mr_text_take - the song's first text meta becomes MIDI Info's line: AX
; bytes at ES:SI, printable ones only, at most MR_TEXTN. Preserves all
; -----------------------------------------------------------------------------
MR_TEXTN    equ 40
    MRBUF mr_text, MR_TEXTN + 1

mr_text_take:
    push ax
    push cx
    push si
    push di
    mov cx, ax
    cmp cx, MR_TEXTN
    jbe .n
    mov cx, MR_TEXTN
.n:
    mov di, mr_text
    jcxz .done
.c:
    mov al, [es:si]
    inc si
    cmp al, ' '
    jb .skip
    cmp al, 0x7E
    ja .skip
    mov [di], al
    inc di
.skip:
    loop .c
.done:
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; THE VOICES
; =============================================================================

; mrv_reset - AL = how many the output has; every one free
mrv_reset:
    push bx
    mov [mrv_n], al
    xor bx, bx
.v:
    mov byte [mrv_st+bx], MRV_FREE
    mov byte [mrv_ch+bx], 0xFF
    push bx
    shl bx, 1
    mov word [mrv_tag+bx], 0xFFFF
    mov word [mrv_age+bx], 0
    pop bx
    inc bx
    cmp bx, MR_MAXV
    jb .v
    mov word [mrv_clock], 0
    pop bx
    ret

; -----------------------------------------------------------------------------
; mrv_find - the voice sounding (ON or HELD) channel AL's key AH
; out: CF = 0, BX = the voice; CF = 1 none
; -----------------------------------------------------------------------------
mrv_find:
    push cx
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .none
    mov cl, [mrv_st+bx]
    cmp cl, MRV_ON
    je .t
    cmp cl, MRV_HELD
    jne .n
.t:
    cmp [mrv_ch+bx], al
    jne .n
    cmp [mrv_key+bx], ah
    je .hit
.n:
    inc bx
    jmp short .v
.hit:
    pop cx
    clc
    ret
.none:
    pop cx
    stc
    ret

; -----------------------------------------------------------------------------
; mrv_alloc - a voice for channel AL's key AH, at velocity CL. A voice
; already sounding that key is retriggered; else a free voice, the one
; already carrying [mrv_want] first; else the oldest released, then the
; oldest pedal-held, then the oldest sounding. Stamps it ON, with its age.
; out: BX = the voice; CF = 1 it was STOLEN while sounding (the output owes
;      it a cut) - or 0 for any other kind
; -----------------------------------------------------------------------------
mrv_alloc:
    push ax
    push cx
    push dx
    push si
    push di
    mov [mrv_tmpv], cl
    call mrv_find
    jnc .steal                      ; the same key again: that voice
    mov si, 0xFFFF                  ; SI = a free voice
    xor bx, bx
.free:
    cmp bl, [mrv_n]
    jae .nofree
    cmp byte [mrv_st+bx], MRV_FREE
    jne .fn
    mov si, bx
    push bx
    shl bx, 1
    mov cx, [mrv_tag+bx]
    pop bx
    cmp cx, [mrv_want]
    je .take                        ; free AND already carrying the patch
.fn:
    inc bx
    jmp short .free
.nofree:
    cmp si, 0xFFFF
    je .old
    mov bx, si
.take:
    clc
    jmp short .stamp
.old:
    mov dl, MRV_REL                 ; the oldest in each class, in turn
    call mrv_oldest
    jnc .relv
    mov dl, MRV_HELD
    call mrv_oldest
    jnc .steal
    mov dl, MRV_ON
    call mrv_oldest
    jnc .steal
    xor bx, bx                      ; (no voices at all cannot happen)
.relv:
    clc
    jmp short .stamp
.steal:
    stc
.stamp:
    pushf
    mov [mrv_ch+bx], al
    mov [mrv_key+bx], ah
    mov cl, [mrv_tmpv]
    mov [mrv_vel+bx], cl
    mov byte [mrv_st+bx], MRV_ON
    inc word [mrv_clock]
    mov cx, [mrv_clock]
    push bx
    shl bx, 1
    mov [mrv_age+bx], cx
    pop bx
    popf
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

    MRB mrv_tmpv

; mrv_oldest - DL = a state: BX = the oldest voice in it; CF = 1 none
mrv_oldest:
    push ax
    push cx
    push si
    mov si, 0xFFFF
    mov cx, 0xFFFF                  ; the oldest age seen, as a distance
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .done
    cmp [mrv_st+bx], dl
    jne .n
    push bx
    shl bx, 1
    mov ax, [mrv_clock]
    sub ax, [mrv_age+bx]            ; how long ago
    pop bx
    cmp si, 0xFFFF
    je .t
    cmp ax, cx
    jbe .n
.t:
    mov cx, ax
    mov si, bx
.n:
    inc bx
    jmp short .v
.done:
    mov bx, si
    cmp si, 0xFFFF
    je .none
    pop si
    pop cx
    pop ax
    clc
    ret
.none:
    pop si
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; mrv_off - channel AL's key AH released: its voice goes HELD under the
; pedal, else RELEASED. out: CF = 0 BX = the voice, ZF = 1 it is RELEASED
; now (the output should key it off); CF = 1 no such voice
; -----------------------------------------------------------------------------
mrv_off:
    push cx
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .none
    cmp byte [mrv_st+bx], MRV_ON
    jne .n
    cmp [mrv_ch+bx], al
    jne .n
    cmp [mrv_key+bx], ah
    je .hit
.n:
    inc bx
    jmp short .v
.hit:
    push bx
    mov bl, al
    xor bh, bh
    mov cl, [mrc_sus+bx]
    pop bx
    or cl, cl
    jz .rel
    mov byte [mrv_st+bx], MRV_HELD
    or cl, cl                       ; ZF = 0: held, not released
    pop cx
    clc
    ret
.rel:
    mov byte [mrv_st+bx], MRV_REL
    cmp cl, cl                      ; ZF = 1
    pop cx
    clc
    ret
.none:
    pop cx
    stc
    ret
