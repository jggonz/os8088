; =============================================================================
; os8088 - apps/win16/w16snd.inc
;
; SOUND (SPEC.md 124.15.1): Windows 3.0's SOUND.DRV voice queue, read off that
; driver, on the PC speaker. One voice; a note is a frequency from Windows'
; own table and a length in the driver's unit, 1/400 s (96,000 units a
; minute's quarter notes at tempo 1), and plays through OSAPI_SND_TONE.
;
; The queue is played from WIN16's tick (OSAPI_WM_TIMER, w16_tarm): each
; note's start is its offset from StartSound rounded to the 18.2 Hz tick, the
; remainder carried in 1/2000 of a tick so the error does not add up.
; =============================================================================

                                    ;   whole units
W16S_PRI      equ 0x40              ; OSAPI_SND_TONE's default priority

S_SERDVNA     equ -1
S_SERQFUL     equ -3
S_SERDNT      equ -5
S_SERDLN      equ -6
S_SERDTP      equ -8
S_SERDMD      equ -10
S_SERDPT      equ -12

; OpenSound (SOUND.1): 1 voice, or S_SERDVNA with no speaker or when it is open
; THE CODE IS THE LIBRARY PART'S (SPEC.md 124.19.3): moved out of the image
; in S14 - a call into the image is ICALL, and its data stays in .text
section .w16dlg
W16LIB w16s_open
    mov ax, S_SERDVNA
    cmp byte [w16s_on], 0
    jne .out
    push ax
    call OSAPI_SND_CAPS             ; DX bit 0: the speaker is there
    test dl, 1
    pop ax
    jz .out
    mov byte [w16s_on], 1
    mov word [w16s_rnd], 1000
    mov byte [w16s_tempo], 120      ; the driver's defaults
    xor ax, ax
    mov [w16s_mode], al
    mov [w16s_pitch], al
    call w16s_flush
    mov ax, 1
.out:
    ret

; CloseSound (SOUND.2)
W16LIB w16s_close
    call w16s_stop
    mov byte [w16s_on], 0
    ret

; StopSound (SOUND.10): the note sounding stopped, the queue emptied
W16LIB w16s_stop
    call w16s_flush
    call w16s_quiet
    ICALL w16_tarm
    xor ax, ax
    ret

; StartSound (SOUND.9): the queue from its first note
W16LIB w16s_start
    cmp byte [w16s_on], 0
    je .out
    cmp byte [w16s_play], 0
    jne .out                        ; playing: what was queued follows
    call OSAPI_GET_TICKS
    mov [w16s_due], ax
    mov word [w16s_frac], 1000      ; half a tick: the starts round
    mov byte [w16s_play], 1
    mov word [w16s_rnd], 2000       ; anywhere in a tick (w16s_tick)
    call w16s_tick
    mov word [w16s_rnd], 1000       ; ...and from now on at a tick's start
    ICALL w16_tarm
.out:
    xor ax, ax
    ret

; SetVoiceAccent (SOUND.5) - args: +0 nPitch, +2 nMode, +4 nVolume,
; +6 nTempo, +8 nVoice
W16LIB w16s_accent
    ICALL w16_args
    xor ax, ax
    cmp byte [w16s_on], 0
    je .out
    cmp word [es:bx + 8], 1
    jne .out
    mov ax, S_SERDTP
    mov cx, [es:bx + 6]
    cmp cx, 255
    ja .out
    cmp cx, 32
    jb .out
    mov [w16s_tempo], cl
    mov ax, S_SERDMD
    mov cx, [es:bx + 2]
    cmp cx, 2
    ja .out
    mov [w16s_mode], cl
    mov ax, S_SERDPT
    mov cx, [es:bx]
    cmp cx, 83
    ja .out
    mov [w16s_pitch], cl
    xor ax, ax
.out:
    ret

; SetVoiceNote (SOUND.4) - args: +0 nCdots (ignored, as Windows 3.0 did),
; +2 nLength, +4 nValue, +6 nVoice
W16LIB w16s_note
    push si
    push di
    ICALL w16_args
    xor ax, ax
    cmp byte [w16s_on], 0
    je .out
    cmp word [es:bx + 6], 1
    jne .out
    mov ax, S_SERDLN                ; the whole note: 96000 / (tempo x length)
    mov cx, [es:bx + 2]
    or cx, cx
    jz .out
    cmp cx, 255
    ja .out
    mov al, [w16s_tempo]
    xor ah, ah
    mul cx                          ; at most 255 x 255: AX
    mov cx, ax
    mov dx, 1
    mov ax, 0x7700                  ; 96,000
    div cx
    mov si, ax                      ; SI = the whole note's units (>= 1)
    mov di, ax                      ; DI = how much of it sounds
    mov cl, [w16s_mode]
    cmp cl, 1
    je .len                         ; legato: all of it
    mov dx, 7                       ; normal 7/8, staccato 3/4
    mov cl, 3
    cmp byte [w16s_mode], 2
    jne .part
    mov dx, 3
    mov cl, 2
.part:
    mul dx
    shr ax, cl
    or ax, ax
    jnz .part1
    inc ax
.part1:
    mov di, ax
.len:
    mov ax, [es:bx + 4]             ; the value: 0 a rest
    or ax, ax
    jz .rest
    cmp ax, 84
    mov dx, S_SERDNT
    ja .err
    add al, [w16s_pitch]
    cmp al, 84
    jbe .p
    sub al, 84
.p:
    dec ax                          ; (value - 1): octave and semitone
    mov cl, 12
    div cl                          ; AL = the octave 0-6, AH = the semitone
    mov bl, ah
    xor bh, bh
    shl bx, 1
    mov dx, [w16s_hz + bx]          ; the eighth octave's
    mov cl, 6
    sub cl, al
    shr dx, cl                      ; ...moved down to this one, rounded
    adc dx, 0
    jmp short .q
.rest:
    xor dx, dx
    mov di, si
.q:
    mov ax, S_SERQFUL
    cmp word [w16s_qn], W16S_QN
    jae .out
    mov bx, [w16s_qt]               ; the tail
    mov ax, W16S_QE
    push dx
    mul bx
    pop dx
    mov bx, ax
    mov [w16s_q + bx], dx
    mov [w16s_q + bx + 2], di
    mov [w16s_q + bx + 4], si
    inc word [w16s_qt]
    cmp word [w16s_qt], W16S_QN
    jb .t
    mov word [w16s_qt], 0
.t:
    inc word [w16s_qn]
    xor ax, ax
.out:
    pop di
    pop si
    ret
.err:
    mov ax, dx
    jmp short .out

; Windows' table: the eighth octave's semitones, C8 to B8, in Hz
section .text
w16s_hz:
    dw 4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902

; w16s_tick - start the queue's next note if its time has come (from WIN16's
; tick, and from StartSound). Keeps every register.
section .w16dlg
w16s_tick:
    push ax
    push bx
    push cx
    push dx
    cmp byte [w16s_play], 0
    je .out
    call OSAPI_GET_TICKS
    sub ax, [w16s_due]
    js .out                         ; not yet
    cmp word [w16s_qn], 0
    jne .next
    mov byte [w16s_play], 0         ; the queue has run out
    jmp short .out
.next:
    mov bx, [w16s_qh]
    mov ax, W16S_QE
    mul bx
    mov bx, ax
    mov ax, [w16s_q + bx + 4]       ; the whole note moves the next start
    mov cx, 91                      ; units x 91 / 2000 = ticks
    mul cx
    add ax, [w16s_frac]
    adc dx, 0
    mov cx, 2000
    div cx
    add [w16s_due], ax
    mov [w16s_frac], dx
    mov ax, [w16s_q + bx + 2]       ; the part that sounds, in ticks, at
    mov cx, 91                      ;   least one: rounded from a tick's start,
    mul cx                          ;   and half a tick longer from StartSound,
    add ax, [w16s_rnd]              ;   which can be anywhere in one
    adc dx, 0
    mov cx, 2000
    div cx
    or ax, ax
    jnz .dur
    inc ax
.dur:
    mov cx, ax
    mov ax, [w16s_q + bx]           ; the Hz: 0 a rest, nothing sent
    or ax, ax
    jz .adv
    push cx
    call OSAPI_GET_TICKS
    pop cx
    add ax, cx
    mov [w16s_until], ax
    mov byte [w16s_sounding], 1
    mov ax, [w16s_q + bx]
    mov dl, W16S_PRI
    call OSAPI_SND_TONE
.adv:
    inc word [w16s_qh]
    cmp word [w16s_qh], W16S_QN
    jb .h
    mov word [w16s_qh], 0
.h:
    dec word [w16s_qn]
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16s_flush - the queue emptied and not playing. Keeps every register.
w16s_flush:
    push ax
    xor ax, ax
    mov [w16s_qh], ax
    mov [w16s_qt], ax
    mov [w16s_qn], ax
    mov [w16s_play], al
    pop ax
    ret

; w16s_quiet - silence our note if it may still be sounding: an off sent after
; ours expired could silence someone else's. Keeps every register.
w16s_quiet:
    cmp byte [w16s_sounding], 0
    je .ret
    push ax
    push cx
    push dx
    mov byte [w16s_sounding], 0
    call OSAPI_GET_TICKS
    sub ax, [w16s_until]
    jns .out
    xor ax, ax
    xor cx, cx
    mov dl, W16S_PRI
    call OSAPI_SND_TONE
.out:
    pop dx
    pop cx
    pop ax
.ret:
    ret

; w16s_endprog - the program has ended: whatever it left playing stops
w16s_endprog:
    call w16s_flush
    call w16s_quiet
    mov byte [w16s_on], 0
    ret

; w16s_wait - AX = ticks until the queue's next note, 0xFFFF none
w16s_wait:
    mov ax, 0xFFFF
    cmp byte [w16s_play], 0
    je .out
    push dx
    call OSAPI_GET_TICKS
    mov dx, ax
    mov ax, [w16s_due]
    sub ax, dx
    jg .ahead
    mov ax, 1
.ahead:
    pop dx
.out:
    ret

section .text
