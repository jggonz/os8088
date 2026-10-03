; =============================================================================
; os8088 - apps/midirack/mrmid.inc
;
; MIDI OUT, through an MPU-401 (SPEC.md 105.8.5, SOUND.DRV's SNDV_MID* verbs,
; SPEC.md 34.13). Prefix mrm_.
;
; THE DAUGHTERBOARD'S ROAD. Every other output here makes the sound: the FM
; chip from patches, the synth from pulse waves. This one sends the song's own
; events to whatever is on the connector - an SC-55, an MT-32, a Waveblaster,
; a Dreamblaster, an emulator's FluidSynth - and the instrument there plays
; them with its own samples. The machine does almost nothing: three bytes a
; note, which an 8088 at 4.77 MHz sends with time to spare.
;
; So nothing here allocates a voice. mr_chan_event FORWARDS each channel
; event, untouched, the moment it is played (mrm_ev), and goes on keeping the
; channel state the rack shows - its levels and programs - exactly as it does
; for the FM; the voice table is mrm_vt, whose only live entry is the song's
; end. SysEx is forwarded as well (mrm_sysex): a GM or GS file says GM ON and
; sets its parts up with it, and a module is owed that.
;
; THE CLOCK is the FM's: the worker, in real time, off the PIT (mrout.inc).
; The bytes are batched a pass at a time and sent with one OSAPI_DRV_CALL,
; which SNDV_MIDW makes worker-safe (port I/O only).
; =============================================================================

MRM_BUFN    equ 96                  ; bytes batched before a send

    MRBUF mrm_buf, MRM_BUFN
    MRW mrm_n                       ; ...how many are in it
    MRB mrm_up                      ; the interface is ours (SNDV_MIDOPEN)

mrm_vt:     dw mr_nop, mr_nop, mr_nop, mr_nop, mr_nop, mr_nop, mrm_alloff

; -----------------------------------------------------------------------------
; mrm_info - at entry: is there an MPU-401? [mr_hasmpu] and [mr_mpubase].
; Asks the driver; a machine without SOUND.DRV answers no, the same as one
; without the card. Registers preserved
; -----------------------------------------------------------------------------
mrm_info:
    push ax
    push bx
    mov byte [mr_hasmpu], 0
    mov bh, DRVC_SOUND
    mov bl, SNDV_IDENT
    call OSAPI_DRV_CALL
    jc .out
    cmp ax, SND_SIG
    jne .out
    mov bh, DRVC_SOUND
    mov bl, SNDV_MIDINFO
    call OSAPI_DRV_CALL
    jc .out
    mov [mr_mpubase], ax
    mov byte [mr_hasmpu], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrm_open - the interface ours, GM ON sent, the voice table ours. UI task.
; out: CF = 1 refused (none, or another program's), [mr_msg] says which
; -----------------------------------------------------------------------------
mrm_open:
    push ax
    push bx
    push cx
    push si
    mov word [mr_msg], mr_s_nompu
    cmp byte [mr_hasmpu], 0
    je .no
    mov bh, DRVC_SOUND
    mov bl, SNDV_MIDOPEN
    call OSAPI_DRV_CALL
    jnc .ok
    mov word [mr_msg], mr_s_mpubusy
.no:
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret
.ok:
    mov byte [mrm_up], 1
    mov word [mrm_n], 0
    mov word [mrq_vt], mrm_vt
    mov si, mrm_gmon                ; GENERAL MIDI ON: a module left in another
    mov cx, mrm_gmon_n              ; mode, or holding another song's parts,
    cld                             ; starts from the standard
.g:
    lodsb
    call mrm_put
    loop .g
    call mrm_flush
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret

mrm_gmon:   db 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7
mrm_gmon_n  equ $ - mrm_gmon

; mrm_close - silenced and given back (the driver hushes every channel)
mrm_close:
    cmp byte [mrm_up], 0
    je .r
    push ax
    push bx
    mov byte [mrm_up], 0
    mov bh, DRVC_SOUND
    mov bl, SNDV_MIDCLOSE
    call OSAPI_DRV_CALL
    pop bx
    pop ax
.r:
    ret

; -----------------------------------------------------------------------------
; mrm_ev - a channel event as it is played: CL = status, CH = d1, DL = d2
; (mr_chan_event, before it decodes them). Program change and channel
; pressure carry one data byte; everything else two. Registers preserved
; -----------------------------------------------------------------------------
mrm_ev:
    push ax
    mov al, cl
    call mrm_put
    mov al, ch
    call mrm_put
    mov al, cl
    and al, 0xF0
    cmp al, 0xC0
    je .out
    cmp al, 0xD0
    je .out
    mov al, dl
    call mrm_put
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrm_sysex - a SysEx event: CL = F0 or F7, AX = its length, ES:SI = its bytes
; (the song's segment). F0's bytes follow the F0 the file left implicit; an
; F7 event is an escape - its bytes go as they are. Registers preserved
; -----------------------------------------------------------------------------
mrm_sysex:
    push ax
    push cx
    push si
    push ax
    cmp cl, 0xF0
    jne .esc
    mov al, 0xF0
    call mrm_put
.esc:
    pop cx                          ; CX = the length
    jcxz .out
.b:
    mov al, [es:si]
    inc si
    call mrm_put
    loop .b
.out:
    pop si
    pop cx
    pop ax
    ret

; mrm_put - AL into the batch, which goes when it is full
mrm_put:
    push bx
    mov bx, [mrm_n]
    mov [mrm_buf+bx], al
    inc bx
    mov [mrm_n], bx
    pop bx
    cmp word [mrm_n], MRM_BUFN
    jb .r
    call mrm_flush
.r:
    ret

; mrm_flush - the batch to the interface. A send that times out drops the
; batch: a module that has stopped listening is not waited for
mrm_flush:
    push ax
    push bx
    push cx
    push si
    mov cx, [mrm_n]
    jcxz .out
    cmp byte [mrm_up], 0
    je .drop
    mov si, mrm_buf
    mov bh, DRVC_SOUND
    mov bl, SNDV_MIDW
    call OSAPI_DRV_CALL
.drop:
    mov word [mrm_n], 0
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mrm_hush - every note off NOW, on all sixteen channels: All Notes Off, All
; Sound Off and the pedal up (a pause; the song's end). The channels' other
; state stands - a resume carries on from it
; -----------------------------------------------------------------------------
mrm_hush:
    push ax
    push cx
    xor cl, cl
.c:
    mov al, 0xB0
    or al, cl
    call mrm_put
    mov al, 64
    call mrm_put
    xor al, al
    call mrm_put
    mov al, 0xB0
    or al, cl
    call mrm_put
    mov al, 123
    call mrm_put
    xor al, al
    call mrm_put
    mov al, 0xB0
    or al, cl
    call mrm_put
    mov al, 120
    call mrm_put
    xor al, al
    call mrm_put
    inc cl
    cmp cl, 16
    jb .c
    call mrm_flush
    pop cx
    pop ax
    ret

; mrm_alloff - MRO_ALLOFF: AL = 0xFF is the song's end (the raw 120/123 of a
; song's own were forwarded as they were played, so a single channel's is
; already done)
mrm_alloff:
    cmp al, 0xFF
    jne .r
    jmp mrm_hush
.r:
    ret

; -----------------------------------------------------------------------------
; mrm_resend - a resume: each channel's program, volume, pan and expression
; as the song left them, and the bend centred, so the notes that follow
; sound as they did before the pause even if the module was touched meanwhile
; -----------------------------------------------------------------------------
mrm_resend:
    push ax
    push bx
    push cx
    xor bx, bx
.c:
    mov cl, bl
    mov al, 0xC0
    or al, cl
    call mrm_put
    mov al, [mrc_prog+bx]
    call mrm_put
    mov al, 0xB0
    or al, cl
    mov ah, 7
    mov ch, [mrc_vol+bx]
    call mrm_cc
    mov ah, 10
    mov ch, [mrc_pan+bx]
    call mrm_cc
    mov ah, 11
    mov ch, [mrc_expr+bx]
    call mrm_cc
    mov al, 0xE0                    ; the bend, centred
    or al, cl
    call mrm_put
    xor al, al
    call mrm_put
    mov al, 0x40
    call mrm_put
    inc bl
    cmp bl, 16
    jb .c
    call mrm_flush
    pop cx
    pop bx
    pop ax
    ret

; mrm_cc - AL = B0+n, AH = the controller, CH = its value
mrm_cc:
    push ax
    call mrm_put
    mov al, ah
    call mrm_put
    mov al, ch
    and al, 0x7F
    call mrm_put
    pop ax
    ret
