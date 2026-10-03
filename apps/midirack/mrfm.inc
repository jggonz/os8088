; =============================================================================
; os8088 - apps/midirack/mrfm.inc
;
; TWO OUTPUTS THAT PLAY IN REAL TIME (SPEC.md 105.6), both driven by the
; worker at the sequencer's own time (mrw_):
;
;   THE FM CHIP (mrf_) - an OPL2's 8 voices or an OPL3's 17, claimed whole
;   through SOUND.DRV (SPEC.md 34.12) and written in BATCHES: every register
;   an event needs goes into mrf_buf and one OSAPI_DRV_CALL takes the lot
;   (mrf_flush), once per tick's worth of events - so a chord is one far
;   call, not forty. Velocity is the carrier's level (mrt_tl's 40 log10
;   curve), pan is an OPL3's L/R bits, a bend rewrites A0h/B0h with the key
;   held, and the drum channel plays the kit's patch at its fixed note.
;
;   THE SPEAKER'S TONE (mrn_) - "Play in Background" on a machine with no card
;   (SPEC.md 105.8.2): one square wave through OSAPI_SND_TONE, on the desktop,
;   behind other windows. It plays the HIGHEST note any melodic channel is
;   holding - the melody, almost always - and follows bends.
; =============================================================================

MRF_BUFN    equ 112                 ; register pairs a batch holds

    MRB mrf_mode                    ; SND_OPL2 / SND_OPL3: what the claim gave
    MRB mrf_kind                    ; ...and what the chip IS (SND_FM_INFO)
    MRB mrf_up                      ; 1 = the chip is ours
    MRW mrf_n                       ; pairs in the batch
    MRB mrf_bank                    ; the bank the batch is writing
    MRBUF mrf_buf, MRF_BUFN * 2
    MRBUF mrf_b0, MR_MAXV           ; per voice: its B0h as last written
    MRBUF mrf_note, MR_MAXV         ; ...the note it sounds (transposed/fixed)
    MRBUF mrf_ptr, MR_MAXV * 2      ; ...its patch, for a level rewrite

mrf_slot:   db 0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12

mrf_vt:     dw mrf_on, mrf_off, mr_nop, mrf_ctl, mrf_bend, mrf_susup, mrf_alloff

mr_nop:
    ret

; mr_x12 - SI x= 12 (a patch's offset), with nothing else touched
mr_x12:
    push ax
    shl si, 1
    shl si, 1                       ; x4
    mov ax, si
    shl si, 1                       ; x8
    add si, ax                      ; x12
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrf_open - the chip claimed: SND_FM_INFO, then SND_FM_CLAIM in OPL3 mode
; when [mr_want] allows it. UI CALLBACK ONLY (the claim's stamp, SPEC.md
; 34.3). out: CF = 1 no chip, or somebody else holds a channel
; -----------------------------------------------------------------------------
mrf_open:
    push ax
    push bx
    push cx
    push si
    cmp byte [mrf_up], 0
    jne .have
    mov bh, DRVC_SOUND              ; ASK IT FIRST (SPEC.md 20.11.1)
    mov bl, SNDV_IDENT
    call OSAPI_DRV_CALL
    jc .no
    cmp ax, SND_SIG
    jne .no
    mov al, SND_FM_INFO
    call OSAPI_SND_FM
    jc .no
    mov [mrf_kind], al
    mov cl, 0                       ; OPL3 mode unless the user chose OPL2
    cmp al, SND_OPL3
    jne .claim
    cmp byte [mr_want], MR_OUT_OPL2
    je .claim
    mov cl, 1
.claim:
    mov al, SND_FM_CLAIM
    call OSAPI_SND_FM
    jc .no
    mov [mrf_mode], al
    mov byte [mrf_up], 1
    mov al, ah
    jmp short .v
.have:
    mov al, SND_FM_TONECH           ; already ours: the voices again
    cmp byte [mrf_mode], SND_OPL3
    jne .v
    mov al, 17
.v:
    call mrv_reset
    xor bx, bx
.b:
    mov byte [mrf_b0+bx], 0
    inc bx
    cmp bx, MR_MAXV
    jb .b
    mov word [mrf_n], 0
    mov byte [mrf_bank], 0
    mov word [mrq_vt], mrf_vt
    clc
    jmp short .out
.no:
    stc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; mrf_close - every voice off and the chip given back (FM verb 3, which also
; puts an OPL3 back in OPL2 mode). UI callback only
mrf_close:
    push ax
    cmp byte [mrf_up], 0
    je .out
    mov word [mrf_n], 0
    mov al, 3
    call OSAPI_SND_FM
    mov byte [mrf_up], 0
.out:
    pop ax
    ret

; mrf_hush - every voice keyed off now (a pause or a stop), the claim kept
mrf_hush:
    push ax
    push bx
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .done
    cmp byte [mrv_st+bx], MRV_FREE
    je .n
    call mrf_keyoff
    mov byte [mrv_st+bx], MRV_REL
.n:
    inc bx
    jmp short .v
.done:
    call mrf_flush
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrf_w - one register into the batch. AH = the register, AL = the value,
; BX = the voice whose bank it is in. Flushes when full. Preserves all
; -----------------------------------------------------------------------------
mrf_w:
    push cx
    push di
    mov cl, 0
    cmp bl, 8
    jb .bk
    mov cl, 1                       ; voices 8..16 are the second bank
.bk:
    cmp [mrf_bank], cl
    je .put
    push ax
    xor al, al                      ; {0, bank}: register 0 selects the bank
    mov ah, cl
    call mrf_put
    mov [mrf_bank], cl
    pop ax
.put:
    xchg al, ah                     ; the pair is {register, value}
    call mrf_put
    xchg al, ah
    pop di
    pop cx
    ret

; mrf_put - AX = the pair (AL first); flushes a full batch first
mrf_put:
    push di
    cmp word [mrf_n], MRF_BUFN
    jb .room
    push cx
    mov cl, [mrf_bank]
    call mrf_flush
    or cl, cl
    jz .b0
    push ax                         ; the new batch starts in bank 0: say
    mov ax, 0x0100                  ; the bank again
    mov di, [mrf_n]
    shl di, 1
    mov [mrf_buf+di], ax
    inc word [mrf_n]
    mov [mrf_bank], cl
    pop ax
.b0:
    pop cx
.room:
    mov di, [mrf_n]
    shl di, 1
    mov [mrf_buf+di], ax
    inc word [mrf_n]
    pop di
    ret

; mrf_flush - the batch to the chip, in one OSAPI_DRV_CALL. Any task
mrf_flush:
    push ax
    push bx
    push cx
    push si
    mov cx, [mrf_n]
    jcxz .out
    mov bh, DRVC_SOUND
    mov bl, SNDV_OPLW
    mov si, mrf_buf
    call OSAPI_DRV_CALL
    mov word [mrf_n], 0
    mov byte [mrf_bank], 0
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; mrf_chan - CL = voice BX's channel inside its bank (0..8)
mrf_chan:
    mov cl, bl
    cmp cl, 8
    jb .r
    sub cl, 8
.r:
    ret

; -----------------------------------------------------------------------------
; mrf_on - MRO_ON: AL = channel, AH = key, CL = velocity
; -----------------------------------------------------------------------------
mrf_on:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dl, cl                      ; DL = the velocity
    cmp al, MR_DRUMCH
    jne .mel
    cmp ah, MRT_DRLO                ; THE KIT: keys 35..81, a patch each,
    jb .out                         ; struck at its own note
    cmp ah, MRT_DRHI
    ja .out
    mov bl, ah
    sub bl, MRT_DRLO
    xor bh, bh
    mov si, bx
    call mr_x12
    add si, mrt_fmdrum
    mov di, 0x100                   ; the tag: 100h + the key
    add di, bx
    mov dh, [si+11]                 ; DH = the note
    jmp short .alloc
.mel:
    mov bl, al
    xor bh, bh
    mov bl, [mrc_prog+bx]           ; BX = the program
    mov di, bx                      ; the tag
    mov si, bx
    call mr_x12
    add si, mrt_fmbank
    mov dh, ah                      ; the note, transposed by the patch
    add dh, [si+11]
    cmp byte [si+11], 0x80
    jb .up
    cmp dh, ah                      ; transposed down past 0: an octave up
    jbe .nt
    add dh, 12
    jmp short .nt
.up:
    cmp dh, 127
    jbe .nt
    sub dh, 12
.nt:
    and dh, 0x7F
.alloc:
    mov [mrv_want], di
    mov cl, dl
    call mrv_alloc                  ; BX = the voice
    mov [mrf_note+bx], dh
    push bx
    shl bx, 1
    mov [mrf_ptr+bx], si
    pop bx
    test byte [mrf_b0+bx], 0x20
    jz .cold
    call mrf_keyoff                 ; a voice still keyed: cut, so the new
.cold:                              ; note's envelope starts from the top
    push bx
    shl bx, 1
    cmp [mrv_tag+bx], di
    mov [mrv_tag+bx], di
    pop bx
    je .same
    call mrf_patch                  ; a different patch: load it
.same:
    call mrf_level
    call mrf_freq                   ; and key it on
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mrf_off - MRO_OFF: AL = channel, AH = key
mrf_off:
    push bx
    call mrv_off
    jc .out
    jnz .out                        ; held by the pedal: still sounding
    call mrf_keyoff
.out:
    pop bx
    ret

; mrf_susup - MRO_SUSUP: channel AL's pedal is up: its held voices let go
mrf_susup:
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
    call mrf_keyoff
.n:
    inc bx
    jmp short .v
.out:
    pop bx
    ret

; mrf_alloff - MRO_ALLOFF: channel AL (0xFF every one): every voice let go
mrf_alloff:
    push bx
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    cmp byte [mrv_st+bx], MRV_ON
    je .c
    cmp byte [mrv_st+bx], MRV_HELD
    jne .n
.c:
    cmp al, 0xFF
    je .k
    cmp [mrv_ch+bx], al
    jne .n
.k:
    mov byte [mrv_st+bx], MRV_REL
    call mrf_keyoff
.n:
    inc bx
    jmp short .v
.out:
    pop bx
    ret

; mrf_ctl - MRO_CTL: channel AL's volume, expression or pan moved
mrf_ctl:
    push bx
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    cmp byte [mrv_st+bx], MRV_FREE
    je .n
    cmp [mrv_ch+bx], al
    jne .n
    call mrf_level
    call mrf_c0
.n:
    inc bx
    jmp short .v
.out:
    pop bx
    ret

; mrf_bend - MRO_BEND: channel AL's bend moved: its sounding voices retuned
mrf_bend:
    push bx
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    cmp byte [mrv_st+bx], MRV_FREE
    je .n
    cmp [mrv_ch+bx], al
    jne .n
    cmp al, MR_DRUMCH               ; a drum is struck at its own note
    je .n
    call mrf_retune
.n:
    inc bx
    jmp short .v
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; the register writers, all BX = the voice
; -----------------------------------------------------------------------------
mrf_keyoff:
    push ax
    push cx
    call mrf_chan
    mov al, [mrf_b0+bx]
    and al, 0xDF
    mov [mrf_b0+bx], al
    mov ah, 0xB0
    add ah, cl
    call mrf_w
    pop cx
    pop ax
    ret

; mrf_patch - voice BX's ten operator registers from its patch (mrf_ptr),
; and C0h
mrf_patch:
    push ax
    push cx
    push dx
    push si
    push bx
    shl bx, 1
    mov si, [mrf_ptr+bx]
    pop bx
    call mrf_chan
    push bx
    mov bl, cl
    xor bh, bh
    mov dl, [mrf_slot+bx]           ; DL = the modulator's slot
    pop bx
    mov dh, 2
.op:
    mov ah, 0x20
    mov cx, 5
.r:
    mov al, [si]
    inc si
    push ax
    add ah, dl
    call mrf_w
    pop ax
    add ah, 0x20                    ; 20h, 40h, 60h, 80h, then E0h
    cmp ah, 0xA0
    jne .rn
    mov ah, 0xE0
.rn:
    loop .r
    add dl, 3                       ; the carrier: the modulator's slot + 3
    dec dh
    jnz .op
    call mrf_c0
    pop si
    pop dx
    pop cx
    pop ax
    ret

; mrf_c0 - C0h: the patch's feedback and connection, and an OPL3's L/R from
; the channel's pan (both bits in OPL2 mode, where they are ignored)
mrf_c0:
    push ax
    push cx
    push si
    push bx
    shl bx, 1
    mov si, [mrf_ptr+bx]
    pop bx
    mov ah, [si+10]
    and ah, 0x0F
    push bx
    mov bl, [mrv_ch+bx]
    xor bh, bh
    mov al, [mrc_pan+bx]
    pop bx
    mov cl, 0x30
    cmp al, 32
    jae .r
    mov cl, 0x10                    ; hard left: A only
.r:
    cmp al, 96
    jb .p
    mov cl, 0x20                    ; hard right: B only
.p:
    cmp byte [mrv_ch+bx], MR_DRUMCH
    jne .set
    mov cl, 0x30                    ; the kit: centre
.set:
    or cl, ah
    mov al, cl
    call mrf_chan
    mov ah, 0xC0
    add ah, cl
    call mrf_w
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrf_level - the carrier's level (and an additive patch's modulator's) from
; the voice's velocity and its channel's volume and expression: the patch's
; own TL + mrt_tl's attenuation, capped at 63, its KSL bits kept
; -----------------------------------------------------------------------------
mrf_level:
    push ax
    push cx
    push dx
    push si
    push di
    push bx
    shl bx, 1
    mov si, [mrf_ptr+bx]
    pop bx
    mov dl, [mrv_vel+bx]
    push bx
    mov bl, [mrv_ch+bx]
    xor bh, bh
    call mr_level                   ; AL = 0..127
    pop bx
    mov di, ax
    mov al, [mrt_tl+di]
    mov dh, al                      ; DH = the attenuation
    call mrf_chan
    push bx
    mov bl, cl
    xor bh, bh
    mov dl, [mrf_slot+bx]           ; DL = the modulator's slot
    pop bx
    mov al, [si+6]                  ; the carrier's 40h
    mov ah, 0x43                    ; 40h + the carrier (slot + 3)
    add ah, dl
    call mrf_tl
    test byte [si+10], 1
    jz .out                         ; FM: the modulator is timbre, not level
    mov al, [si+1]
    mov ah, 0x40
    add ah, dl
    call mrf_tl
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; mrf_tl - AL = the patch's 40h byte, DH = attenuation, AH = the register
mrf_tl:
    push ax
    push cx
    mov cl, al
    and cl, 0xC0                    ; KSL kept
    and al, 0x3F
    add al, dh
    cmp al, 63
    jbe .ok
    mov al, 63
.ok:
    or al, cl
    call mrf_w
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrf_freq - A0h/B0h from the voice's note and its channel's bend, KEYED ON
; mrf_retune - the same with the key as it was (a bend)
; -----------------------------------------------------------------------------
mrf_freq:
    push ax
    mov al, 0x20
    jmp short mrf_fq
mrf_retune:
    push ax
    mov al, [mrf_b0+bx]
    and al, 0x20
mrf_fq:
    push cx
    push dx
    push si
    mov [mrf_kon], al
    mov ah, [mrf_note+bx]
    push bx
    mov bl, [mrv_ch+bx]
    xor bh, bh
    cmp bl, MR_DRUMCH
    je .nob
    call mr_pitch                   ; AX = the pitch, 1/32 semitones
    jmp short .pb
.nob:
    mov al, ah                      ; a drum: its note, no bend
    xor ah, ah
    mov cl, 5
    shl ax, cl
.pb:
    pop bx
    xor dx, dx
    mov cx, 12 * MRT_FINE
    div cx                          ; AX = the octave, DX = the step
    mov si, dx
    shl si, 1
    mov dx, [mrt_fn+si]             ; DX = the F-Number at block octave - 1
    mov cl, al                      ; CL = the octave
    or cl, cl
    jnz .b1
    shr dx, 1                       ; octave 0: block 0, half the number
    jmp short .blk0
.b1:
    dec cl                          ; CL = the block
.hi:
    cmp cl, 7
    jbe .fit
    shl dx, 1                       ; past block 7: the number doubles
    dec cl
    jmp short .hi
.fit:
    cmp dx, 1023
    jbe .ok
    shr dx, 1                       ; ...and an F-Number past 10 bits drops
    jmp short .fit                  ; an octave rather than wrapping
.blk0:
    xor cl, cl
.ok:
    mov ch, cl                      ; CH = the block
    call mrf_chan                   ; CL = the channel in its bank
    mov ah, 0xA0
    add ah, cl
    mov al, dl
    call mrf_w
    mov al, ch
    shl al, 1
    shl al, 1
    mov ah, dh
    and ah, 0x03
    or al, ah
    or al, [mrf_kon]
    mov [mrf_b0+bx], al
    mov ah, 0xB0
    add ah, cl
    call mrf_w
    pop si
    pop dx
    pop cx
    pop ax
    ret

    MRB mrf_kon

; -----------------------------------------------------------------------------
; mr_pitch - AX = channel BX's note AH as a pitch in 1/32 semitones, its bend
; added, clamped to 0..127 x 32 + 31
; -----------------------------------------------------------------------------
mr_pitch:
    push cx
    mov al, ah
    xor ah, ah
    mov cl, 5
    shl ax, cl
    shl bx, 1
    add ax, [mrc_bend+bx]
    shr bx, 1
    jns .lo
    xor ax, ax
.lo:
    cmp ax, 128 * MRT_FINE - 1
    jbe .ok
    mov ax, 128 * MRT_FINE - 1
.ok:
    pop cx
    ret

; =============================================================================
; THE SPEAKER'S TONE (mrn_) - the highest held melodic note, one square wave
; =============================================================================
mrn_vt:     dw mrn_on, mrn_off, mr_nop, mr_nop, mrn_upd, mrn_susup, mrn_alloff

    MRW mrn_hz                      ; what the speaker is sounding (0 = quiet)

mrn_open:
    push ax
    mov al, 8                       ; voices: notes HELD, so the top can fall
    call mrv_reset                  ; back to the next one down
    mov word [mrn_hz], 0
    mov word [mrq_vt], mrn_vt
    pop ax
    clc
    ret

mrn_close:
mrn_hush:
    push ax
    push cx
    push dx
    xor ax, ax
    mov [mrn_hz], ax
    xor cx, cx
    mov dl, 0x40
    call OSAPI_SND_TONE
    pop dx
    pop cx
    pop ax
    ret

mrn_on:
    cmp al, MR_DRUMCH
    je .out                         ; the kit has no pitch to sing
    push bx
    call mrv_alloc
    pop bx
    call mrn_upd
.out:
    ret

mrn_off:
    push bx
    call mrv_off
    pop bx
    call mrn_upd
    ret

mrn_susup:
    push bx
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    cmp byte [mrv_st+bx], MRV_HELD
    jne .n
    cmp [mrv_ch+bx], al
    jne .n
    mov byte [mrv_st+bx], MRV_FREE
.n:
    inc bx
    jmp short .v
.out:
    pop bx
    call mrn_upd
    ret

mrn_alloff:
    push bx
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    cmp al, 0xFF
    je .k
    cmp [mrv_ch+bx], al
    jne .n
.k:
    mov byte [mrv_st+bx], MRV_FREE
.n:
    inc bx
    jmp short .v
.out:
    pop bx
    call mrn_upd
    ret

; mrn_upd - the speaker to the highest ON/HELD note, if it moved
mrn_upd:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, 0xFFFF                  ; SI = the best voice
    xor dh, dh                      ; DH = its key
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .pick
    mov al, [mrv_st+bx]
    cmp al, MRV_ON
    je .c
    cmp al, MRV_HELD
    jne .n
.c:
    mov al, [mrv_key+bx]
    cmp si, 0xFFFF
    je .t
    cmp al, dh
    jbe .n
.t:
    mov dh, al
    mov si, bx
.n:
    inc bx
    jmp short .v
.pick:
    xor ax, ax
    cmp si, 0xFFFF
    je .set
    mov bx, si
    mov ah, dh
    push bx
    mov bl, [mrv_ch+bx]
    xor bh, bh
    call mr_pitch                   ; AX = the pitch
    pop bx
    call mr_hz                      ; AX = Hz
.set:
    cmp ax, [mrn_hz]
    je .out
    mov [mrn_hz], ax
    xor cx, cx                      ; until changed
    mov dl, 0x40
    call OSAPI_SND_TONE
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mr_hz - AX = a pitch (1/32 semitones) -> Hz, from octave 0's x2048 table
mr_hz:
    push cx
    push dx
    push si
    xor dx, dx
    mov cx, 12 * MRT_FINE
    div cx                          ; AX = octave, DX = step
    mov si, dx
    shl si, 1
    mov dx, [mrt_fq+si]
    mov cl, 11
    sub cl, al                      ; F x 2^octave / 2048
    jbe .up
    shr dx, cl
    jmp short .r
.up:
    neg cl
    shl dx, cl
.r:
    mov ax, dx
    cmp ax, 19                      ; the speaker's floor (SPEC.md 34.2)
    jae .ok
    mov ax, 19
.ok:
    pop si
    pop dx
    pop cx
    ret
