; =============================================================================
; os8088 - drivers/sound/rad2.inc  (RADPLAY.DRV)
;
; The version 2.1 engine: SPEC.md 96.4.5, tools/radsim.py's EngineV2 routine
; for routine, to the byte of the register stream. Every wrap the reference
; has is kept - 8-bit counters, 16-bit frequencies, signed slides - and so is
; the order of every write, because the stream IS the contract (radopl3.py
; compares it with radsim's, record for record).
;
; Register convention for every r2_ routine: CL = the tracker channel (CH = 0),
; SI = its channel record, DS = the claim. CX, SI, DI and BP are preserved;
; AX, BX and DX are not. A routine with locals keeps them in a BP frame, which
; is only ever built inside a replay frame, on the private stack, SS = DS.
; =============================================================================

; --- a channel record (radsim _Chan), CH_SIZE bytes in r2_chans --------------
CH_LAST     equ 0               ; byte: last instrument (the reuse bit's holder)
CH_INST     equ 1               ; byte: current instrument, 0 = none
CH_VOL      equ 2               ; byte: volume
CH_DETA     equ 3
CH_DETB     equ 4
CH_KEYS     equ 5               ; F_* below
CH_FREQ     equ 6               ; word
CH_OCT      equ 8
CH_FX       equ 9               ; an FX record
CH_RIFF     equ 16              ; the channel riff (an RF record)
CH_IRIFF    equ 33              ; the instrument riff
CH_SIZE     equ 50

; --- an effect record (radsim _FX) --------------------------------------------
FX_PORT     equ 0               ; s8
FX_VOL      equ 1               ; s8
FX_TFREQ    equ 2               ; word
FX_TOCT     equ 4
FX_TSPEED   equ 5
FX_TDIR     equ 6               ; s8

; --- a riff record (radsim _Riff): its FX record first, so a riff pointer is
; also its effect pointer
RF_TRACK    equ 7               ; word: claim offset of the next line, 0 = none
RF_START    equ 9               ; word
RF_LINE     equ 11
RF_SPEED    equ 12
RF_CNT      equ 13
RF_TOCT     equ 14              ; s8
RF_TNOTE    equ 15              ; s8
RF_LAST     equ 16              ; the reuse bit's holder

F_KEYON     equ 1
F_KEYOFF    equ 2
F_KEYEDON   equ 4
S_NONE      equ 0
S_RIFF      equ 1
S_IRIFF     equ 2

CM_PORTUP   equ 1
CM_PORTDN   equ 2
CM_TONESLIDE equ 3
CM_TONEVOL  equ 5
CM_VOLSLIDE equ 10
CM_SETVOL   equ 12
CM_JUMP     equ 13
CM_SPEED    equ 15
CM_IGNORE   equ 'I' - 55
CM_MULT     equ 'M' - 55
CM_RIFF     equ 'R' - 55
CM_TRANSPOSE equ 'T' - 55
CM_FEEDBACK equ 'U' - 55
CM_VOLUME   equ 'V' - 55

; =============================================================================
; Parse (radsim EngineV2._parse) - once, at load, over a validated file
; =============================================================================
r2_parse:
    push ax
    push bx
    push cx
    push si
    push di
    cld
    mov bx, r2_insptr           ; every instrument the file does not define is
    mov cx, 127                 ; the all-zero one (deviation 2)
.z:
    mov word [bx], rd_zins
    add bx, 2
    loop .z
    mov si, RAD_TUNE+0x11
    lodsb
    test al, 0x20
    jz .desc
    add si, 2
.desc:
    lodsb
    or al, al
    jnz .desc
.inst:
    lodsb                       ; num
    or al, al
    jz .order
    xor ah, ah
    dec ax
    shl ax, 1
    mov di, ax                  ; DI = (num - 1) * 2
    lodsb                       ; name length
    xor ah, ah
    add si, ax
    mov [r2_insptr+di], si
    lodsb                       ; the algorithm byte
    mov bl, al
    and al, 7
    cmp al, 7
    je .midi
    add si, 23
    jmp short .riff
.midi:
    add si, 6
.riff:
    test bl, 0x80
    jz .inst
    lodsw
    mov [r2_insriff+di], si
    add si, ax
    jmp .inst
.order:
    lodsb
    mov [rd_ordlen], al
    mov [rd_ordlist], si
    xor ah, ah
    add si, ax
.pat:
    lodsb
    cmp al, 100
    jae .riffs
    xor ah, ah
    shl ax, 1
    mov di, ax
    lodsw
    mov [r2_tracks+di], si
    add si, ax
    jmp .pat
.riffs:
    lodsb                       ; id: FFh ends (validated)
    cmp al, 0xA0
    jae .done
    mov bh, al
    and bh, 0x0F                ; channel 1..9 (mul takes AH)
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1                   ; riff 0..9
    mov bl, 9
    mul bl
    add al, bh
    dec al
    xor ah, ah
    shl ax, 1
    mov di, ax
    lodsw
    mov [r2_riffs+di], si
    add si, ax
    jmp .riffs
.done:
    mov di, r2_insptr           ; every instrument's register values, once
    mov bx, r2_insreg
    mov cx, 127
.vals:
    push di
    mov di, [di]
    call r2_insvals             ; BX advances by 22
    pop di
    add di, 2
    loop .vals
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The start and stop body (radsim EngineV2.stop): the start sequence less its
; leading 105h, and the play position back to the top
; =============================================================================
r2_body:
    push ax
    push bx
    push cx
    push si
    mov bx, 0x20
.reg:
    xor al, al
    cmp bl, 0x60
    jb .w
    cmp bl, 0xA0
    jae .w
    mov al, 0xFF
.w:
    call rd_set
    inc bh
    call rd_set
    dec bh
    inc bx
    cmp bx, 0xF6
    jb .reg
    mov bx, 0x01
    mov al, 0x20
    call rd_set
    mov bl, 0x08
    xor al, al
    call rd_set
    mov bl, 0xBD
    call rd_set
    mov bx, 0x104
    call rd_set
    mov bl, 0x05
    mov al, 1
    call rd_set
    mov byte [rd_speedcnt], 1
    mov byte [rd_order], 0
    call r2_gettrack
    mov [r2_track], bx
    mov byte [rd_line], 0
    mov byte [r2_entr], 0
    mov si, r2_chans
    mov cx, 9
.ch:
    mov byte [si+CH_LAST], 0
    mov byte [si+CH_INST], 0
    mov byte [si+CH_VOL], 0
    mov byte [si+CH_DETA], 0
    mov byte [si+CH_DETB], 0
    mov byte [si+CH_KEYS], 0
    mov byte [si+CH_RIFF+RF_CNT], 0
    mov byte [si+CH_IRIFF+RF_CNT], 0
    add si, CH_SIZE
    loop .ch
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; r2_gettrack - the order entry's pattern (radsim get_track)
; out: BX = the track, 0 = none; [rd_order] resolved; clobbers AX
; -----------------------------------------------------------------------------
r2_gettrack:
    mov al, [rd_order]
    cmp al, [rd_ordlen]
    jb .in
    xor al, al
    mov [rd_order], al
.in:
    xor ah, ah
    mov bx, [rd_ordlist]
    add bx, ax
    mov al, [bx]
    test al, 0x80
    jz .pat
    and al, 0x7F
    mov [rd_order], al
    mov bx, [rd_ordlist]
    add bx, ax
    mov al, [bx]
    and al, 0x7F
.pat:
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov bx, [r2_tracks+bx]
    ret

; =============================================================================
; One frame (radsim EngineV2.update)
; =============================================================================
r2_update:
    push cx
    push si
    push di
    mov byte [rd_notes], 0
    xor cx, cx
    mov si, r2_chans
.riffs:
    cmp byte [si+CH_IRIFF+RF_CNT], 0    ; a stopped riff only resets its
    jne .irun                           ; effects: inline, no call
    mov word [si+CH_IRIFF+FX_PORT], 0   ; (FX_PORT and FX_VOL)
    mov byte [si+CH_IRIFF+FX_TDIR], 0
    jmp short .criff
.irun:
    lea di, [si+CH_IRIFF]
    xor dl, dl
    call r2_tickriff
.criff:
    cmp byte [si+CH_RIFF+RF_CNT], 0
    jne .crun
    mov word [si+CH_RIFF+FX_PORT], 0
    mov byte [si+CH_RIFF+FX_TDIR], 0
    jmp short .rnext
.crun:
    lea di, [si+CH_RIFF]
    mov dl, 1
    call r2_tickriff
.rnext:
    add si, CH_SIZE
    inc cl
    cmp cl, 9
    jb .riffs
    call r2_playline
    xor cx, cx
    mov si, r2_chans
.fx:
    mov al, [si+CH_IRIFF+FX_PORT]       ; continue_fx does nothing on an
    or al, [si+CH_IRIFF+FX_VOL]         ; effect record with all three at
    or al, [si+CH_IRIFF+FX_TDIR]        ; zero, which is most of them
    jz .fx2
    lea di, [si+CH_IRIFF]
    call r2_contfx
.fx2:
    mov al, [si+CH_RIFF+FX_PORT]
    or al, [si+CH_RIFF+FX_VOL]
    or al, [si+CH_RIFF+FX_TDIR]
    jz .fx3
    lea di, [si+CH_RIFF]
    call r2_contfx
.fx3:
    mov al, [si+CH_FX+FX_PORT]
    or al, [si+CH_FX+FX_VOL]
    or al, [si+CH_FX+FX_TDIR]
    jz .fxn
    lea di, [si+CH_FX]
    call r2_contfx
.fxn:
    add si, CH_SIZE
    inc cl
    cmp cl, 9
    jb .fx
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; r2_status - the status block's channel half, built when verb 6 copies it
;             (rd_stat, under its cli) rather than every frame - a frame on the
;             8088 is priced in instructions, and nothing reads these between
;             two polls (SPEC.md 34.12.7)
; -----------------------------------------------------------------------------
r2_status:
    push ax
    push bx
    push cx
    push si
    push di
    call rd_status_pos
    mov si, r2_chans
    mov di, rd_st + RST_CHAN
    mov cx, 9
.ch:
    mov al, [si+CH_INST]
    mov [di+RSC_INST], al
    mov ah, [si+CH_VOL]
    mov [di+RSC_VOL], ah
    xor ah, ah                  ; the flags
    test byte [si+CH_KEYS], F_KEYEDON
    jz .k
    or ah, RSCF_KEYED
.k:
    or al, al
    jz .r
    call r2_insp
    mov bl, [bx]
    and bl, 7
    cmp bl, 2
    jb .r
    cmp bl, 7
    je .r
    or ah, RSCF_4OP
.r:
    mov al, [si+CH_RIFF+RF_CNT]
    or al, [si+CH_IRIFF+RF_CNT]
    jz .f
    or ah, RSCF_RIFF
.f:
    mov [di+RSC_FLAGS], ah
    add si, CH_SIZE
    add di, RSC_SIZE
    loop .ch
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; r2_chptr - CL = channel -> SI = its record; clobbers AX
; r2_insp  - AL = instrument 1..127 -> BX = its +0 byte; clobbers nothing else
; r2_fxptr - AL = S_* -> DI = the FX record that source's effects live in
; r2_resetfx - DI = an FX record: portamento, volume slide, tone slide off
; -----------------------------------------------------------------------------
r2_chptr:
    mov al, CH_SIZE
    mul cl
    add ax, r2_chans
    mov si, ax
    ret

r2_insp:
    push ax
    xor ah, ah
    dec ax
    shl ax, 1
    mov bx, ax
    mov bx, [r2_insptr+bx]
    pop ax
    ret

r2_fxptr:
    lea di, [si+CH_FX]
    cmp al, S_RIFF
    jb .out
    lea di, [si+CH_RIFF]
    je .out
    lea di, [si+CH_IRIFF]
.out:
    ret

r2_resetfx:
    mov byte [di+FX_PORT], 0
    mov byte [di+FX_VOL], 0
    mov byte [di+FX_TDIR], 0
    ret

; -----------------------------------------------------------------------------
; r2_opreg - BX = OP_OFF3[CL][AH] (a register offset 000h..115h)
; r2_chanoff / r2_chn2off - BX = CHAN_OFF3[CL] / CHN2_OFF3[CL]
; -----------------------------------------------------------------------------
r2_opreg:
    push ax
    mov al, cl
    shl al, 1
    shl al, 1
    add al, ah
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov bx, [r2_opoff3+bx]
    pop ax
    ret

r2_chanoff:
    mov bl, cl
    xor bh, bh
    shl bx, 1
    mov bx, [r2_chanoff3+bx]
    ret

r2_chn2off:
    mov bl, cl
    xor bh, bh
    shl bx, 1
    mov bx, [r2_chn2off3+bx]
    ret

; -----------------------------------------------------------------------------
; r2_unpack - one entry off a track (radsim unpack_note)
; in:  BX = the entry, DI -> the holder's last-instrument byte
; out: BX past it, AL = the entry byte; r2_notenum/octnum/instnum/effect/param
; clobbers: AH, DX
; -----------------------------------------------------------------------------
r2_unpack:
    mov al, [bx]
    inc bx
    xor dx, dx
    mov [r2_instnum], dl
    mov [r2_effect], dl
    mov [r2_param], dl
    test al, 0x40
    jz .noinst
    mov dl, [bx]
    inc bx
    test dl, 0x80
    jz .n7
    mov ah, [di]
    mov [r2_instnum], ah
.n7:
    and dl, 0x7F
.noinst:
    test al, 0x20
    jz .noeff
    mov ah, [bx]
    inc bx
    mov [r2_instnum], ah
    mov [di], ah
.noeff:
    test al, 0x10
    jz .note
    mov ah, [bx]
    mov [r2_effect], ah
    mov ah, [bx+1]
    mov [r2_param], ah
    add bx, 2
.note:
    mov ah, dl
    and ah, 0x0F
    mov [r2_notenum], ah
    shr dl, 1
    shr dl, 1
    shr dl, 1
    shr dl, 1
    mov [r2_octnum], dl
    ret

; -----------------------------------------------------------------------------
; r2_skipto - radsim skip_to_line
; in:  BX = a track's first line, AL = the line wanted, DL = 1 channel riff
; out: BX = the first line at or after it, or 0; clobbers AH
; -----------------------------------------------------------------------------
r2_skipto:
    push cx
.line:
    mov ah, [bx]
    and ah, 0x7F
    cmp ah, al
    jae .out
    test byte [bx], 0x80
    jnz .none
    inc bx
.ent:
    mov ah, [bx]
    inc bx
    push bx
    mov bl, ah
    mov cl, 4
    shr bl, cl
    and bl, 7
    xor bh, bh
    mov cl, [r2_notesize+bx]
    pop bx
    xor ch, ch
    add bx, cx
    test ah, 0x80
    jnz .line
    or dl, dl
    jnz .line
    jmp .ent
.none:
    xor bx, bx
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; r2_transpose - radsim transpose, on the unpacked note
; in:  AL = the riff's note (s8), AH = its octave (s8)
; clobbers: AX, DX
; -----------------------------------------------------------------------------
r2_transpose:
    mov dl, [r2_notenum]
    cmp dl, 1
    jb .out
    cmp dl, 12
    ja .out
    sub ah, 3                   ; toct
    jz .noct
    mov dh, [r2_octnum]
    add dh, ah
    jns .c7
    xor dh, dh
    jmp short .so
.c7:
    cmp dh, 7
    jle .so
    mov dh, 7
.so:
    mov [r2_octnum], dh
.noct:
    sub al, 12                  ; tnot
    jz .out
    add dl, al
    cmp dl, 1
    jge .sn
    add dl, 12
    mov dh, [r2_octnum]
    cmp dh, 0
    jle .nz
    dec dh
    mov [r2_octnum], dh
    jmp short .sn
.nz:
    mov dl, 1
.sn:
    mov [r2_notenum], dl
.out:
    ret

; =============================================================================
; r2_playline - radsim play_line
; =============================================================================
r2_playline:
    dec byte [rd_speedcnt]
    jz .go
    ret
.go:
    push cx
    push si
    push di
    mov al, [rd_speed]
    mov [rd_speedcnt], al
    mov si, r2_chans            ; every channel's main-track effects off, and
    mov di, rd_st + RST_CHAN    ; the status block's "effect on this line"
    mov cx, 9
.rst:
    push di
    lea di, [si+CH_FX]
    call r2_resetfx
    pop di
    mov byte [di+RSC_FX], 0
    add si, CH_SIZE
    add di, RSC_SIZE
    loop .rst
    mov byte [r2_ljump], 0
    mov bx, [r2_track]
    or bx, bx
    jz .adv
    mov al, [bx]
    and al, 0x7F
    cmp al, [rd_line]
    ja .adv
    mov al, [bx]
    push ax                     ; the line byte
    inc bx
.ent:
    mov cl, [bx]
    and cl, 0x0F
    xor ch, ch
    call r2_chptr
    mov di, si                  ; the holder is the channel (CH_LAST = 0)
    call r2_unpack
    push bx
    push ax
    xor dx, dx                  ; S_NONE, operator 0
    call r2_playnote
    pop ax
    pop bx
    test al, 0x80
    jz .ent
    pop ax
    test al, 0x80
    jz .keep
    xor bx, bx
.keep:
    mov [r2_track], bx
.adv:
    mov al, [rd_line]
    inc al
    mov [rd_line], al
    cmp al, 64
    jae .next
    cmp byte [r2_ljump], 0
    je .done
.next:
    mov al, [r2_ljump]
    or al, al
    jz .z
    dec al
.z:
    mov [rd_line], al
    mov al, [rd_order]
    inc al
    mov [rd_order], al
    push ax
    call r2_gettrack
    mov [r2_track], bx
    pop ax
    cmp al, [rd_order]
    je .done
    or byte [rd_st+RST_FLAGS], RSTF_LOOPED
.done:
    pop di
    pop si
    pop cx
    ret

; =============================================================================
; r2_playnote - radsim play_note
; in:  CL, SI; DL = S_*, DH = the operator; the unpacked note in r2_notenum..
; locals [bp+0] note, +1 octave, +2 instrument, +3 effect, +4 parameter,
;        +5 source, +6 operator, +7 transposing
; =============================================================================
r2_playnote:
    cmp byte [r2_entr], 8
    jb .d1
    ret
.d1:
    cmp byte [rd_notes], RAD_PNMAX
    jb .d2
    mov byte [rd_halted], 1     ; deviation 5: the refused play ends the tune
    ret
.d2:
    inc byte [rd_notes]
    inc byte [r2_entr]
    push di
    push bp
    sub sp, 8
    mov bp, sp
    mov al, [r2_notenum]
    mov [bp+0], al
    mov al, [r2_octnum]
    mov [bp+1], al
    mov al, [r2_instnum]
    mov [bp+2], al
    mov al, [r2_effect]
    mov [bp+3], al
    mov al, [r2_param]
    mov [bp+4], al
    mov [bp+5], dl
    mov [bp+6], dh
    mov byte [bp+7], 0
    or dl, dl
    jnz .nostat
    mov al, RSC_SIZE            ; the main track's effect, for the status block
    mul cl
    mov bx, ax
    mov al, [bp+3]
    mov [rd_st+RST_CHAN+RSC_FX+bx], al
.nostat:
    cmp byte [bp+3], CM_TONESLIDE
    jne .notts
    mov al, [bp+5]
    call r2_fxptr
    mov al, [bp+0]
    dec al
    cmp al, 12
    jae .tsx
    mov ah, [bp+1]
    mov [di+FX_TOCT], ah
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov ax, [rd_notefreq+bx]
    mov [di+FX_TFREQ], ax
.tsx:
    mov al, [bp+4]
    call r2_toneslide
    jmp .exit
.notts:
    mov al, [bp+2]
    or al, al
    jz .noinst
    mov ah, [si+CH_INST]        ; AH = the old instrument, AL = the new
    mov [si+CH_INST], al
    call r2_insp
    mov dl, [bx]
    and dl, 7
    cmp dl, 7
    jne .fm
    jmp .exit                   ; a MIDI instrument: nothing more here
.fm:
    push ax
    call r2_loadinst
    or byte [si+CH_KEYS], F_KEYOFF | F_KEYON
    lea di, [si+CH_IRIFF]
    call r2_resetfx
    pop ax
    cmp byte [bp+5], S_IRIFF
    jne .rstart
    cmp al, ah
    je .noinst
.rstart:
    call r2_insp
    mov dl, [bx+2]
    and dl, 0x0F                ; the riff's speed
    jz .noriff
    xor ah, ah
    dec ax
    shl ax, 1
    mov bx, ax
    mov bx, [r2_insriff+bx]
    or bx, bx
    jz .noriff
    lea di, [si+CH_IRIFF]
    mov [di+RF_TRACK], bx
    mov [di+RF_START], bx
    mov byte [di+RF_LINE], 0
    mov [di+RF_SPEED], dl
    mov byte [di+RF_LAST], 0
    mov al, [bp+0]
    dec al
    cmp al, 12
    jae .idft
    mov al, [bp+1]
    mov [di+RF_TOCT], al
    mov al, [bp+0]
    mov [di+RF_TNOTE], al
    mov byte [bp+7], 1
    jmp short .icnt
.idft:
    mov byte [di+RF_TOCT], 3
    mov byte [di+RF_TNOTE], 12
.icnt:
    mov byte [di+RF_CNT], 1
    xor dl, dl
    call r2_tickriff
    jmp short .noinst
.noriff:
    mov byte [si+CH_IRIFF+RF_CNT], 0
.noinst:
    mov al, [bp+3]
    cmp al, CM_RIFF
    je .riff
    cmp al, CM_TRANSPOSE
    jne .note
.riff:
    lea di, [si+CH_RIFF]
    call r2_resetfx
    mov al, [bp+4]
    xor ah, ah
    mov dl, 10
    div dl                      ; AL = riff, AH = channel
    xor bx, bx
    or ah, ah
    jz .rtrk
    mov dh, ah
    mov dl, 9
    mul dl
    add al, dh
    dec al
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov bx, [r2_riffs+bx]
.rtrk:
    mov [di+RF_TRACK], bx
    or bx, bx
    jz .rnone
    mov [di+RF_START], bx
    mov byte [di+RF_LINE], 0
    mov al, [rd_speed]
    mov [di+RF_SPEED], al
    mov byte [di+RF_LAST], 0
    cmp byte [bp+3], CM_TRANSPOSE
    jne .rdft
    mov al, [bp+0]
    dec al
    cmp al, 12
    jae .rdft
    mov al, [bp+1]
    mov [di+RF_TOCT], al
    mov al, [bp+0]
    mov [di+RF_TNOTE], al
    mov byte [bp+7], 1
    jmp short .rcnt
.rdft:
    mov byte [di+RF_TOCT], 3
    mov byte [di+RF_TNOTE], 12
.rcnt:
    mov byte [di+RF_CNT], 1
    mov dl, 1
    call r2_tickriff
    jmp short .note
.rnone:
    mov byte [di+RF_CNT], 0
.note:
    cmp byte [bp+7], 0
    jne .effects
    mov al, [bp+0]
    or al, al
    jz .effects
    cmp al, 15
    jne .n2
    or byte [si+CH_KEYS], F_KEYOFF
.n2:
    mov al, [si+CH_INST]
    or al, al
    jz .play
    call r2_insp
    mov al, [bx]
    and al, 7
    cmp al, 7
    je .effects
.play:
    mov al, [bp+0]
    mov ah, [bp+1]
    call r2_opl3note
.effects:
    mov al, [bp+5]
    call r2_fxptr
    mov al, [bp+3]
    mov ah, [bp+4]
    cmp al, CM_SETVOL
    jne .e1
    mov al, ah
    call r2_setvol
    jmp .exit
.e1:
    cmp al, CM_SPEED
    jne .e2
    mov al, [bp+5]
    or al, al
    jnz .espr
    mov [rd_speed], ah
    mov [rd_speedcnt], ah
    jmp .exit
.espr:
    cmp al, S_RIFF
    jne .espi
    mov [si+CH_RIFF+RF_SPEED], ah
    mov [si+CH_RIFF+RF_CNT], ah
    jmp .exit
.espi:
    mov [si+CH_IRIFF+RF_SPEED], ah
    mov [si+CH_IRIFF+RF_CNT], ah
    jmp .exit
.e2:
    cmp al, CM_PORTUP
    jne .e3
    mov [di+FX_PORT], ah
    jmp .exit
.e3:
    cmp al, CM_PORTDN
    jne .e4
    neg ah
    mov [di+FX_PORT], ah
    jmp .exit
.e4:
    cmp al, CM_TONEVOL
    je .evol
    cmp al, CM_VOLSLIDE
    jne .e5
.evol:
    mov dl, ah
    cmp dl, 50
    jb .evs
    sub dl, 50
    neg dl
.evs:
    mov [di+FX_VOL], dl
    cmp al, CM_TONEVOL
    jne .exit
    mov al, ah
    call r2_toneslide
    jmp .exit
.e5:
    cmp al, CM_JUMP
    jne .e6
    cmp ah, 64
    jae .exit
    cmp byte [bp+5], S_NONE
    jne .exit
    inc ah
    mov [r2_ljump], ah
    jmp .exit
.e6:
    cmp byte [bp+5], S_IRIFF
    jne .exit
    cmp al, CM_MULT
    jne .e7
    push ax
    mov ah, [bp+6]
    call r2_opreg
    add bx, 0x20
    pop ax
    mov dl, ah
    and dl, 0x0F
    mov al, [rd_shadow+bx]
    and al, 0xF0
    or al, dl
    call rd_set
    jmp .exit
.e7:
    cmp al, CM_VOLUME
    jne .e8
    push ax
    mov ah, [bp+6]
    call r2_opreg
    add bx, 0x40
    pop ax
    mov dl, ah
    and dl, 0x3F
    xor dl, 0x3F
    mov al, [rd_shadow+bx]
    and al, 0xC0
    or al, dl
    call rd_set
    jmp .exit
.e8:
    cmp al, CM_FEEDBACK
    jne .exit
    mov al, ah
    xor ah, ah
    mov dl, 10
    div dl                      ; AL = which, AH = the feedback
    cmp al, 1
    ja .exit
    jb .fb2
    call r2_chanoff
    jmp short .fbw
.fb2:
    call r2_chn2off
.fbw:
    add bx, 0xC0
    mov dl, ah
    and dl, 7
    shl dl, 1
    mov al, [rd_shadow+bx]
    and al, 0x31
    or al, dl
    call rd_set
.exit:
    add sp, 8
    pop bp
    pop di
    dec byte [r2_entr]
    ret

; -----------------------------------------------------------------------------
; r2_toneslide - radsim _toneslide + get_slide_dir
; in:  AL = the parameter, DI = the FX record, SI = the channel
; clobbers: AX, BX
; -----------------------------------------------------------------------------
r2_toneslide:
    or al, al
    jz .dir
    mov [di+FX_TSPEED], al
.dir:
    mov al, [di+FX_TSPEED]
    test al, al
    jle .set
    mov ah, [si+CH_OCT]
    cmp ah, [di+FX_TOCT]
    ja .neg
    jb .set
    mov bx, [si+CH_FREQ]
    cmp bx, [di+FX_TFREQ]
    ja .neg
    jb .set
    xor al, al
    jmp short .set
.neg:
    neg al
.set:
    mov [di+FX_TDIR], al
    ret

; =============================================================================
; r2_insvals - an instrument's 22 register values, computed once at load
; in:  DI = the instrument's +0 byte, BX -> 22 bytes out: +0 the C0h value for
;      CHAN_OFF3 (operators 3+4), +1 for CHN2_OFF3 (1+2), then per operator
;      0..3 its 20h, 40h (volume-scaled), 60h, 80h and E0h values
; clobbers: AX, DX
;
; Everything load_instrument writes but 104h is a function of the instrument
; alone - the master volume is fixed at 64 (96.4.5) - so a note that loads one
; walks this table instead of recomputing it: on the 8088 that was 34% of a
; heavy frame's instructions (tests/radcost.py).
; =============================================================================
r2_insvals:
    push cx
    push si
    mov dl, [di]
    and dl, 7                   ; DL = the algorithm
    mov al, [di]                ; operators 3+4's pan and feedback; the
    mov cl, 5                   ; connection bit on algorithms 3, 5, 6
    shr al, cl
    and al, 3
    xor al, 3
    mov cl, 4
    shl al, cl
    mov ah, [di+1]
    shr ah, cl
    shl ah, 1
    or al, ah
    cmp dl, 3
    je .con1
    cmp dl, 5
    je .con1
    cmp dl, 6
    jne .w1
.con1:
    or al, 1
.w1:
    mov [bx], al
    mov al, [di]                ; operators 1+2; the connection bit on 1 and 6
    mov cl, 3
    shr al, cl
    and al, 3
    xor al, 3
    mov cl, 4
    shl al, cl
    mov ah, [di+1]
    and ah, 0x0F
    shl ah, 1
    or al, ah
    cmp dl, 1
    je .con2
    cmp dl, 6
    jne .w2
.con2:
    or al, 1
.w2:
    mov [bx+1], al
    add bx, 2
    xor dh, dh                  ; DH = the operator
.op:
    mov si, rd_blankop          ; operators 3 and 4 of a 2-op algorithm
    cmp dl, 2
    jae .real
    cmp dh, 2
    jae .gotop
.real:
    mov al, dh
    mov ah, 5
    mul ah
    add ax, 4
    add ax, di
    mov si, ax
.gotop:
    mov al, [si+1]
    not al
    and al, 0x3F                ; the level as a volume
    push bx
    mov bl, dl
    xor bh, bh
    mov bl, [r2_algcar+bx]
    mov cl, dh
    shr bl, cl
    test bl, 1
    pop bx
    jz .nocar
    mul byte [di+3]             ; a carrier: x the instrument's volume / 64
    mov cl, 6
    shr ax, cl
.nocar:
    xor al, 0x3F
    and al, 0x3F
    mov ah, [si+1]
    and ah, 0xC0
    or ah, al
    mov al, [si]
    mov [bx], al                ; 20h
    mov [bx+1], ah              ; 40h
    mov al, [si+2]
    mov [bx+2], al              ; 60h
    mov al, [si+3]
    mov [bx+3], al              ; 80h
    mov al, [si+4]
    mov [bx+4], al              ; E0h
    add bx, 5
    inc dh
    cmp dh, 4
    jb .op
    pop si
    pop cx
    ret

; =============================================================================
; r2_loadinst - radsim load_instrument, on CH_INST, from r2_insvals' table
; =============================================================================
r2_loadinst:
    push di
    mov al, [si+CH_INST]
    or al, al
    jnz .have
    pop di
    ret
.have:
    call r2_insp
    mov di, bx                  ; DI = the instrument's +0 byte
    mov ah, [di+3]
    mov [si+CH_VOL], ah
    mov ah, [di+2]
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1                   ; detune
    mov dl, ah
    inc ah
    shr ah, 1
    mov [si+CH_DETA], ah
    shr dl, 1
    mov [si+CH_DETB], dl
    mov dl, [di]
    and dl, 7                   ; DL = the algorithm
    xor ah, ah                  ; DI -> this instrument's 22 values
    dec ax
    mov di, ax
    shl ax, 1
    add di, ax                  ; x 3
    shl ax, 1
    shl ax, 1                   ; x 8 (from x 2)
    add di, ax                  ; x 11
    shl di, 1                   ; x 22
    add di, r2_insreg
    cmp cl, 6
    jae .c0
    mov al, 1                   ; the channel's 4-op bit in 104h
    shl al, cl
    mov ah, al
    not ah
    mov bx, 0x104
    and ah, [rd_shadow+bx]
    cmp dl, 2
    je .four
    cmp dl, 3
    jne .w104
.four:
    or ah, al
.w104:
    mov al, ah
    call rd_set
.c0:
    call r2_chanoff
    add bx, 0xC0
    mov al, [di]
    call rd_set
    call r2_chn2off
    add bx, 0xC0
    mov al, [di+1]
    call rd_set
    add di, 2
    xor ah, ah                  ; AH = the operator
.op:
    call r2_opreg
    add bx, 0x20
    mov al, [di]
    call rd_set
    add bx, 0x20
    mov al, [di+1]
    call rd_set
    add bx, 0x20
    mov al, [di+2]
    call rd_set
    add bx, 0x20
    mov al, [di+3]
    call rd_set
    add bx, 0x60
    mov al, [di+4]
    call rd_set
    add di, 5
    inc ah
    cmp ah, 4
    jb .op
    pop di
    ret

; =============================================================================
; r2_opl3note - radsim play_note_opl3
; in:  AL = note 1..12 or 15, AH = octave; CL, SI
; locals: +0 note, +1 octave, +2 o1, +4 o2, +6 freq, +8 frq2, +10 op4, +11 key
; =============================================================================
r2_opl3note:
    push di
    push bp
    sub sp, 12
    mov bp, sp
    mov [bp+0], al
    mov [bp+1], ah
    call r2_chanoff
    mov [bp+2], bx
    call r2_chn2off
    mov [bp+4], bx
    test byte [si+CH_KEYS], F_KEYOFF
    jz .keyed
    and byte [si+CH_KEYS], ~(F_KEYOFF | F_KEYEDON)
    mov bx, [bp+2]
    add bx, 0xB0
    mov al, [rd_shadow+bx]
    and al, 0xDF
    call rd_set
    mov bx, [bp+4]
    add bx, 0xB0
    mov al, [rd_shadow+bx]
    and al, 0xDF
    call rd_set
.keyed:
    cmp byte [bp+0], 15
    jne .play
    jmp .out
.play:
    mov byte [bp+10], 0
    mov al, [si+CH_INST]
    or al, al
    jz .two
    call r2_insp
    mov al, [bx]
    and al, 7
    cmp al, 2
    jb .two
    mov byte [bp+10], 1
.two:
    mov bl, [bp+0]
    dec bl
    xor bh, bh
    shl bx, 1
    mov ax, [rd_notefreq+bx]
    mov [si+CH_FREQ], ax
    mov dl, [bp+1]
    mov [si+CH_OCT], dl
    mov dx, ax
    mov bl, [si+CH_DETB]
    xor bh, bh
    sub dx, bx
    mov [bp+8], dx              ; frq2
    mov bl, [si+CH_DETA]
    add ax, bx
    mov [bp+6], ax              ; freq
    cmp byte [bp+10], 0
    je .a2
    mov bx, [bp+2]
    add bx, 0xA0
    mov al, [bp+8]
    call rd_set
.a2:
    mov bx, [bp+4]
    add bx, 0xA0
    mov al, [bp+6]
    call rd_set
    test byte [si+CH_KEYS], F_KEYON
    jz .key
    and byte [si+CH_KEYS], ~F_KEYON
    or byte [si+CH_KEYS], F_KEYEDON
    mov al, RSC_SIZE            ; a key-on: the status block's counter
    mul cl
    mov bx, ax
    inc byte [rd_st+RST_CHAN+RSC_KEYS+bx]
.key:
    mov byte [bp+11], 0
    test byte [si+CH_KEYS], F_KEYEDON
    jz .b0
    mov byte [bp+11], 0x20
.b0:
    mov dl, [bp+1]
    shl dl, 1
    shl dl, 1
    or dl, [bp+11]              ; octave << 2 | key
    mov bx, [bp+2]
    add bx, 0xB0
    xor al, al
    cmp byte [bp+10], 0
    je .b1
    mov al, [bp+9]
    or al, dl
.b1:
    call rd_set
    mov bx, [bp+4]
    add bx, 0xB0
    mov al, [bp+7]
    or al, dl
    call rd_set
    mov al, RSC_SIZE
    mul cl
    mov bx, ax
    mov al, [bp+1]
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    or al, [bp+0]
    mov [rd_st+RST_CHAN+RSC_NOTE+bx], al
.out:
    add sp, 12
    pop bp
    pop di
    ret

; =============================================================================
; r2_setvol - radsim set_volume
; in:  AL = volume; CL, SI
; =============================================================================
r2_setvol:
    cmp al, 64
    jbe .k
    mov al, 64
.k:
    mov [si+CH_VOL], al
    mov dl, al                  ; DL = the volume
    mov al, [si+CH_INST]
    or al, al
    jz .ret
    call r2_insp
    mov al, [bx]
    and al, 7
    cmp al, 7
    je .ret                     ; deviation 4: a MIDI instrument has no carriers
    push di
    mov di, bx
    xor dh, dh
.op:
    mov bl, [di]
    and bl, 7
    xor bh, bh
    mov bl, [r2_algcar+bx]
    push cx
    mov cl, dh
    shr bl, cl
    pop cx
    test bl, 1
    jz .next
    mov al, dh
    mov ah, 5
    mul ah
    mov bx, ax
    mov al, [di+bx+5]           ; the operator's 40h-style byte
    and al, 0x3F
    xor al, 0x3F
    mul dl
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    xor al, 0x3F
    mov ah, al
    push ax
    mov ah, dh
    call r2_opreg
    add bx, 0x40
    pop ax
    mov al, [rd_shadow+bx]
    and al, 0xC0
    or al, ah
    call rd_set
.next:
    inc dh
    cmp dh, 4
    jb .op
    pop di
.ret:
    ret

; =============================================================================
; r2_contfx - radsim continue_fx
; in:  DI = an FX record; CL, SI
; =============================================================================
r2_contfx:
    mov al, [di+FX_PORT]
    or al, al
    jz .vol
    xor dl, dl
    call r2_porta
.vol:
    mov al, [di+FX_VOL]
    or al, al
    jz .tone
    mov ah, [si+CH_VOL]
    sub ah, al
    jns .pos
    xor ah, ah
.pos:
    mov al, ah
    call r2_setvol
.tone:
    mov al, [di+FX_TDIR]
    or al, al
    jz .out
    mov dl, 1
    call r2_porta
.out:
    ret

; =============================================================================
; r2_porta - radsim portamento
; in:  AL = the amount (s8), DL = 1 a tone slide; DI = the FX record; CL, SI
; locals: +0 amount, +1 tone slide, +2 freq, +4 octave, +6 frq2
; =============================================================================
r2_porta:
    push bp
    sub sp, 8
    mov bp, sp
    mov [bp+0], al
    mov [bp+1], dl
    cbw
    add ax, [si+CH_FREQ]
    mov bx, ax                  ; BX = the frequency, 16 bits unsigned
    mov ah, [si+CH_OCT]
    cmp bx, 0x156
    jae .hi
    or ah, ah
    jz .lo
    dec ah
    add bx, 0x158
    jmp short .ts
.lo:
    mov bx, 0x156
    jmp short .ts
.hi:
    cmp bx, 0x2AE
    jbe .ts
    cmp ah, 7
    jae .top
    inc ah
    sub bx, 0x158
    jmp short .ts
.top:
    mov bx, 0x2AE
.ts:
    cmp byte [bp+1], 0
    je .set
    test byte [bp+0], 0x80
    jnz .down
    cmp ah, [di+FX_TOCT]
    ja .clamp
    jb .set
    cmp bx, [di+FX_TFREQ]
    jae .clamp
    jmp short .set
.down:
    cmp ah, [di+FX_TOCT]
    jb .clamp
    ja .set
    cmp bx, [di+FX_TFREQ]
    jbe .clamp
    jmp short .set
.clamp:
    mov bx, [di+FX_TFREQ]
    mov ah, [di+FX_TOCT]
.set:
    mov [si+CH_FREQ], bx
    mov [si+CH_OCT], ah
    mov [bp+4], ah
    mov ax, bx
    mov dl, [si+CH_DETB]
    xor dh, dh
    sub ax, dx
    mov [bp+6], ax              ; frq2
    mov dl, [si+CH_DETA]
    add bx, dx
    mov [bp+2], bx              ; freq
    call r2_chn2off
    push bx
    add bx, 0xA0
    mov al, [bp+2]
    call rd_set
    pop bx
    add bx, 0xB0
    mov al, [bp+3]
    call r2_b0mix
    call rd_set
    call r2_chanoff
    push bx
    add bx, 0xA0
    mov al, [bp+6]
    call rd_set
    pop bx
    add bx, 0xB0
    mov al, [bp+7]
    call r2_b0mix
    call rd_set
    add sp, 8
    pop bp
    ret

; AL = a frequency's high byte, BX = its B0h register, [bp+4] = the octave
; (r2_porta's frame) -> AL = high & 3 | octave << 2 | the shadow's top 3 bits
r2_b0mix:
    and al, 3
    mov ah, [bp+4]
    shl ah, 1
    shl ah, 1
    or al, ah
    mov ah, [rd_shadow+bx]
    and ah, 0xE0
    or al, ah
    ret

; =============================================================================
; r2_tickriff - radsim tick_riff
; in:  DI = the riff record, DL = 1 a channel riff; CL, SI
; locals: +0 trk, +2 line, +3 the line byte, +4 channel riff, +5 column
; =============================================================================
r2_tickriff:
    cmp byte [di+RF_CNT], 0
    jne .go
    jmp r2_resetfx
.go:
    dec byte [di+RF_CNT]
    jz .line
    ret
.line:
    push bp
    sub sp, 6
    mov bp, sp
    mov [bp+4], dl
    mov al, [di+RF_SPEED]
    mov [di+RF_CNT], al
    mov al, [di+RF_LINE]
    mov [bp+2], al
    inc al
    mov [di+RF_LINE], al
    cmp al, 64
    jb .n
    mov byte [di+RF_CNT], 0
.n:
    call r2_resetfx
    mov bx, [di+RF_TRACK]
    mov [bp+0], bx
    or bx, bx
    jz .peek
    mov al, [bx]
    and al, 0x7F
    cmp al, [bp+2]
    jne .peek
    mov al, [bx]
    mov [bp+3], al
    inc bx
    mov [bp+0], bx
    cmp byte [bp+4], 0
    je .irl
    add di, RF_LAST             ; a channel riff: one entry, always transposed
    call r2_unpack
    sub di, RF_LAST
    mov [bp+0], bx
    mov al, [di+RF_TNOTE]
    mov ah, [di+RF_TOCT]
    call r2_transpose
    mov dx, S_RIFF              ; DL = S_RIFF, DH = operator 0
    call r2_playnote
    jmp short .settrk
.irl:
    mov bx, [bp+0]              ; an instrument riff: entries to the line's end
    mov al, [bx]
    and al, 0x0F
    mov [bp+5], al
    add di, RF_LAST
    call r2_unpack
    sub di, RF_LAST
    mov [bp+0], bx
    push ax                     ; the entry byte: its bit 7 ends the line
    cmp byte [r2_effect], CM_IGNORE
    je .noT
    mov al, [di+RF_TNOTE]
    mov ah, [di+RF_TOCT]
    call r2_transpose
.noT:
    mov dh, [bp+5]
    or dh, dh
    jz .op0
    dec dh
    and dh, 3
.op0:
    mov dl, S_IRIFF
    call r2_playnote
    pop ax
    test al, 0x80
    jz .irl
.settrk:
    mov bx, [bp+0]
    test byte [bp+3], 0x80
    jz .st
    xor bx, bx
.st:
    mov [di+RF_TRACK], bx
    mov [bp+0], bx
.peek:
    mov bx, [bp+0]
    or bx, bx
    jz .out
    mov al, [bx]
    and al, 0x7F
    cmp al, [di+RF_LINE]
    jne .out
    inc bx
    push di
    mov di, r2_dummy
    mov byte [di], 0
    call r2_unpack
    pop di
    cmp byte [r2_effect], CM_JUMP
    jne .out
    mov al, [r2_param]
    cmp al, 64
    jae .out
    mov [di+RF_LINE], al
    mov bx, [di+RF_START]
    mov dl, [bp+4]
    call r2_skipto
    mov [di+RF_TRACK], bx
.out:
    add sp, 6
    pop bp
    ret

; =============================================================================
; Tables
; =============================================================================
r2_chanoff3: dw 0x000, 0x001, 0x002, 0x100, 0x101, 0x102, 0x006, 0x007, 0x008
r2_chn2off3: dw 0x003, 0x004, 0x005, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108
r2_opoff3:   dw 0x00B, 0x008, 0x003, 0x000
             dw 0x00C, 0x009, 0x004, 0x001
             dw 0x00D, 0x00A, 0x005, 0x002
             dw 0x10B, 0x108, 0x103, 0x100
             dw 0x10C, 0x109, 0x104, 0x101
             dw 0x10D, 0x10A, 0x105, 0x102
             dw 0x113, 0x110, 0x013, 0x010
             dw 0x114, 0x111, 0x014, 0x011
             dw 0x115, 0x112, 0x015, 0x012
r2_algcar:   db 0x01, 0x03, 0x01, 0x09, 0x05, 0x0D, 0x0F  ; bit i = operator i
r2_notesize: db 0, 2, 1, 3, 1, 3, 2, 4
rd_blankop:  db 0, 0x3F, 0, 0xF0, 0
