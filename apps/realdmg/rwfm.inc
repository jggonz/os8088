; =============================================================================
; os8088 - apps/realdmg/rwfm.inc
;
; RealDMG's FM SOUND (docs/REALDMG-SPEC.md section 17.1): all four of the
; Game Boy's channels on an OPL2, when SOUND.DRV has one and the Control
; Panel sends tones to it - the two squares on channels 0 and 1, the wave on
; 2 and the noise on 3. Without one the speaker plays the louder square as
; it always has (rw_sound). Included by realdmg.asm.
;
; The chip is CLAIMED whole (SND_FM_CLAIM, on the UI task when a game
; starts) and written through SNDV_OPLW, which takes no lock and touches no
; disk - so the worker writes it after each frame, and the full-screen
; bracket does the same on the UI task. A patch per channel is loaded once;
; after that a frame is at most an F-number, a key and a level a channel,
; and only what changed: a shadow of the last value of each register keeps
; a still tone at no writes at all.
;
; The patches hold their level (the OPL's sustain), and the Game Boy's own
; envelopes, which rdgsys keeps (S_SQ1..S_SQ4), set the carrier's attenuation
; frame by frame. A square's duty is its modulator's depth - the thinner the
; pulse, the brighter the tone. The noise is a modulator at full feedback,
; which is as near as an OPL2 comes to noise without the rhythm section the
; driver keeps for itself (SPEC.md 34.12).
; =============================================================================

RW_FMCH         equ 4               ; the channels we play

; rw_fmopen - on the UI task, a game starting: the chip claimed and our four
; patches loaded, when there is a chip and the tones go to it. Quietly
; nothing otherwise: the speaker then
rw_fmopen:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [rw_fmon], 0
    jne .r
    call OSAPI_SND_CAPS             ; AX = caps, BL = where a tone goes
    test ax, SND_CAP_FM
    jz .r
    or bl, bl                       ; the Control Panel's speaker route
    jz .r
    mov bh, DRVC_SOUND              ; ASK IT FIRST (SPEC.md 20.11.1)
    mov bl, SNDV_IDENT
    call OSAPI_DRV_CALL
    jc .r
    cmp ax, SND_SIG
    jne .r
    mov al, SND_FM_CLAIM
    xor cl, cl                      ; OPL2 mode
    call OSAPI_SND_FM
    jc .r
    call rw_quiet                   ; the speaker's tone off, if we had one
    mov byte [rw_fmon], 1
    mov di, rw_fmsh                 ; every shadow unknown: the first frame
    mov cx, RW_FMCH * 4             ;   writes them all
    mov al, 0FFh
    push ds
    pop es
    cld
    rep stosb
    mov si, rw_fmpat                ; the patches: register, value pairs
    mov cx, (rw_fmpate - rw_fmpat) / 2
    call rw_fmsend
.r:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_fmclose - on the UI task: the chip given back (verb 3: our channels
; off, its default patches, and the claim)
rw_fmclose:
    cmp byte [rw_fmon], 0
    je .r
    mov byte [rw_fmon], 0
    mov al, 3
    call OSAPI_SND_FM
.r:
    ret

; rw_fmsend - DS:SI = CX register/value pairs to the chip, in one call
rw_fmsend:
    jcxz .r
    push ax
    push bx
    push es
    push ds
    pop es
    mov bh, DRVC_SOUND
    mov bl, SNDV_OPLW
    call OSAPI_DRV_CALL
    pop es
    pop bx
    pop ax
.r:
    ret

; rw_fmquiet - every channel keyed off (Machine > Sound, a pause, Speed's
; At 100% only), its F-number left as it was
rw_fmquiet:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov di, rw_fmbuf
    xor bx, bx
.c:
    mov si, bx
    shl si, 1
    shl si, 1
    mov al, [rw_fmsh + si]          ; B0h's shadow: its key bit off
    cmp al, 0FFh
    je .n
    test al, 20h
    jz .n
    and al, 1Fh
    mov [rw_fmsh + si], al
    mov ah, al
    mov al, 0B0h
    add al, bl
    stosw
.n:
    inc bx
    cmp bx, RW_FMCH
    jb .c
    mov cx, di
    sub cx, rw_fmbuf
    shr cx, 1
    mov si, rw_fmbuf
    call rw_fmsend
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; rw_fmframe - after a frame: each channel's tone, key and level from the
; machine, and what changed to the chip in one call. DS = ours; any task
rw_fmframe:
    push ds
    pop es                          ; ES = ours: the batch
    mov di, rw_fmbuf
    mov ds, [cs:rdg_viewseg]
    mov al, [0FF24h]                ; NR50: the louder side's master volume
    mov ah, al
    and al, 7
    mov cl, 4
    shr ah, cl
    and ah, 7
    cmp al, ah
    jae .m
    mov al, ah
.m:
    xor ah, ah
    mov si, ax
    mov dl, [cs:rw_fmmv + si]       ; DL = its attenuation
    mov dh, [0FF25h]                ; DH = NR51: who reaches an output
    test byte [0FF26h], 80h
    jnz .p
    xor dh, dh                      ; NR52 off: nobody
.p:
    xor bx, bx                      ; BX = the channel
.ch:
    call rw_fmchan                  ; AX = B0h:A0h, CL, CH = the levels
    push es
    pop ds
    call rw_fmput
    mov ds, [cs:rdg_viewseg]
    inc bx
    cmp bx, RW_FMCH
    jb .ch
    push es
    pop ds
    mov cx, di
    sub cx, rw_fmbuf
    shr cx, 1
    mov si, rw_fmbuf
    jmp rw_fmsend

; rw_fmchan - DS = the view, BX = the channel (0-3), DL = the master's
; attenuation, DH = NR51: AX = what B0h and A0h want (the key in AH bit 5),
; CL = the carrier's attenuation, CH = the modulator's (a square's duty;
; FFh: as its patch has it). Kept: BX, DX, DI
rw_fmchan:
    mov ch, 0FFh
    mov al, dh                      ; routed to neither side: silent
    mov cl, bl
    shr al, cl
    test al, 11h
    jz .off
    mov si, bx
    shl si, 1
    mov si, [cs:rw_fmrec + si]      ; SI = its record (rdgsys keeps them)
    cmp byte [si + SQ_ON], 0
    je .off
    mov al, [si + SQ_VOL]
    cmp bl, 2
    jne .vol
    mov al, [0FF1Ch]                ; the wave: NR32's code - 0, 100, 50, 25%
    mov cl, 5
    shr al, cl
    and al, 3
    jz .off
    xor ah, ah
    mov si, ax
    mov al, [cs:rw_fmwv + si]
.vol:
    and al, 0Fh
    jz .off
    xor ah, ah
    mov si, ax
    mov cl, [cs:rw_fmtl + si]       ; CL = the level, and the master's on it
    add cl, dl
    cmp cl, 63
    jbe .t
    mov cl, 63
.t:
    cmp bl, 2                       ; a square's duty: its modulator's depth
    jae .nd
    mov al, [0FF11h]
    or bl, bl
    jz .d
    mov al, [0FF16h]
.d:
    rol al, 1
    rol al, 1
    and al, 3
    xor ah, ah
    mov si, ax
    mov ch, [cs:rw_fmduty + si]
.nd:
    cmp bl, 3
    je .noise
    mov si, bx                      ; a tone: 2048 - x, from NRx3 and NRx4
    shl si, 1
    mov si, [cs:rw_fmfrq + si]
    mov ax, [si]
    and ah, 7
    mov si, 2048
    sub si, ax
    cmp bl, 2
    jne .hz
    shl si, 1                       ; the wave: 65,536 / (2048 - x) Hz
    jmp short .hz
.noise:                             ; NR43: the noise clocks at 524,288 /
    push cx                         ;   (r x 2^(s+1)) Hz, r = 0 a half - a
    mov al, [0FF22h]                ;   tone's 2048 - x of r x 2^(s-1)
    mov ah, al
    and al, 7
    shl al, 1                       ; 2r, or 1 for r = 0
    jnz .r2
    inc ax
.r2:
    mov cl, 4
    shr ah, cl                      ; AH = s
    mov cl, ah
    xor ah, ah
    mov si, ax
    cmp cl, 13
    jbe .s
    mov si, 0FFFFh                  ; slower than we can say: the slowest
    jmp short .nz
.s:
    sub cl, 2                       ; 2r x 2^(s-2): 14 x 2^11 at most
    jb .rt
    shl si, cl
    jmp short .ck
.rt:
    neg cl
    shr si, cl
.ck:
    cmp si, 22
    jae .nz
    mov si, 22                      ; faster than the chip: the fastest
.nz:
    pop cx
.hz:
    call rw_fmfreq                  ; AX = block:F-number, CF = 1 too high
    jc .off
    or ah, 20h                      ; the key
    ret
.off:
    xor ax, ax
    mov cx, 0FF3Fh                  ; CL = 63, CH = as it is
    ret

; rw_fmfreq - SI = 2048 - x of a square's 131,072 / (2048 - x) Hz: AX = the
; OPL's block (bits 10-12) and F-number (0-9), f = fnum x 49,716 /
; 2^(20 - block); CF = 1 past 5,958 Hz, which the chip cannot say. Kept: all
; but AX
rw_fmfreq:
    push cx
    push dx
    cmp si, 22
    jb .hi
    cmp si, 43
    jae .div
    mov ax, 21597                   ; block 7: 2,764,482 / 128 / (2048 - x)
    xor dx, dx
    div si
    or ah, 1Ch
    jmp short .ok
.div:
    mov dx, 002Ah                   ; 2,764,482 = 2^37 / 49,716: the F-
    mov ax, 2E42h                   ;   number at block 0
    div si
    xor cx, cx
.n:
    cmp ax, 1024
    jb .b
    shr ax, 1
    inc cx
    jmp short .n
.b:
    shl cl, 1
    shl cl, 1
    or ah, cl
.ok:
    clc
    jmp short .r
.hi:
    stc
.r:
    pop dx
    pop cx
    ret

; rw_fmput - BX = the channel, AX = B0h:A0h (the key in AH bit 5), CL = the
; carrier's level, CH = the modulator's (FFh: unchanged): each register not
; already holding it onto ES:DI as a register/value pair, and the shadow
; told. A key off leaves the F-number as it was. DS = ours. Kept: BX, DX
rw_fmput:
    push si
    push dx
    mov si, bx
    shl si, 1
    shl si, 1
    add si, rw_fmsh                 ; 4 a channel: B0h, A0h, car TL, mod TL
    test ah, 20h
    jnz .on
    mov ah, [si]                    ; off: the last B0h, the key bit clear
    and ah, 1Fh
    mov al, [si + 1]
.on:
    cmp al, [si + 1]                ; A0h: the F-number's low 8
    je .a
    mov [si + 1], al
    push ax
    mov ah, al
    mov al, 0A0h
    add al, bl
    stosw
    pop ax
.a:
    mov dl, [rw_fmcar + bx]         ; DL = the carrier's slot
    cmp cl, [si + 2]                ; its level BEFORE the key, so a note
    je .t                           ;   keyed on starts at it
    mov [si + 2], cl
    push ax
    mov al, dl
    add al, 40h
    mov ah, cl
    stosw
    pop ax
.t:
    cmp ch, 0FFh
    je .k
    cmp ch, [si + 3]
    je .k
    mov [si + 3], ch
    push ax
    mov al, dl
    add al, 40h - 3                 ; the modulator: three slots below
    mov ah, ch
    stosw
    pop ax
.k:
    cmp ah, [si]                    ; B0h: key, block, the F-number's top 2
    je .r
    mov [si], ah
    mov al, 0B0h
    add al, bl
    stosw
.r:
    pop dx
    pop si
    ret

; where the shadows are, for tests/gbfm.py to find them in a running package
rw_fmdbg        dw rw_fmsh, rw_fmon

; each channel's record, and the NRx3 its tone starts at
rw_fmrec:       dw S_SQ1, S_SQ2, S_SQ3, S_SQ4
rw_fmfrq:       dw 0FF13h, 0FF18h, 0FF1Dh

; the carrier's operator slot, a channel (its modulator three below)
rw_fmcar:       db 3, 4, 5, 11
; a Game Boy volume 0-15 (an amplitude, v/15) as the OPL's attenuation, in
; 0.75 dB steps: 20 log10(v/15) / 0.75
rw_fmtl:        db 63, 31, 23, 19, 15, 13, 11, 9, 7, 6, 5, 4, 3, 2, 1, 0
; NR50's 0-7 ((m+1)/8 of the full level) the same way
rw_fmmv:        db 24, 16, 11, 8, 5, 3, 2, 0
; the wave's NR32 code 1-3 (100, 50, 25%) as a volume 0-15
rw_fmwv:        db 0, 15, 8, 4
; a square's duty (12.5, 25, 50, 75%) as its modulator's attenuation: the
; thinner pulse is the brighter tone
rw_fmduty:      db 20, 26, 32, 26

; the patches: register, value. Every channel holds its level (EG type 1),
; attacks and releases at once, and is told its level frame by frame
rw_fmpat:
%macro RW_FMPATCH 6                 ; mod slot, mod 20h, car 20h, mod TL,
    db 20h + %1, %2,  23h + %1, %3  ;   mod E0h, car E0h (C0h after it)
    db 40h + %1, %4,  43h + %1, 63
    db 60h + %1, 0F0h, 63h + %1, 0F0h
    db 80h + %1, 0Fh, 83h + %1, 0Fh
    db 0E0h + %1, %5, 0E3h + %1, %6
%endmacro
    RW_FMPATCH 0, 22h, 21h, 32, 0, 0    ; the squares: the modulator at
    db 0C0h, 00h                        ;   TWICE the carrier, which makes
    RW_FMPATCH 1, 22h, 21h, 32, 0, 0    ;   odd harmonics alone - a 50%
    db 0C1h, 00h                        ;   square's - and no feedback
    RW_FMPATCH 2, 21h, 21h, 40, 0, 0    ; the wave: near a sine
    db 0C2h, 00h
    RW_FMPATCH 8, 2Fh, 21h, 0, 0, 0     ; the noise: a modulator at x15 and
    db 0C3h, 0Eh                        ;   full feedback
rw_fmpate:
