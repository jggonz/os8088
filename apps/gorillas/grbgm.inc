; Three independent FM voices through SOUND.DRV, on AdLib or Sound Blaster.
; The tone API keeps channel 8 for the reference intro and gameplay effects.
; No port I/O, PCM buffers, IRQ changes or additional tasks. Calls run under
; the game's graphics lock; rendering services the same clock between strips.

; Select by completed points: every skyline advances, every match resets.
gr_bgmlevel:
    call gr_bgmstop
    xor ax, ax
    mov al, [gr_scores]
    add al, [gr_scores+1]
    mov bl, 3
    div bl
    mov [gr_bgmtrack], ah
    xor bx, bx
    mov bl, ah
    shl bx, 1
    mov ax, [gr_bgmtracks+bx]
    mov [gr_bgmstart], ax
    mov [gr_bgmptr], ax
    ret

gr_bgmstop:
    call gr_bgmhold
    mov word [gr_bgmptr], 0
    mov byte [gr_bgmstatus], 0
    ret

; M is accepted throughout gameplay, including flight, AI turns and pause.
; Muting releases voices but retains the phrase position and reference SFX.
gr_bgmtoggle:
    xor byte [gr_bgmenabled], 1
    cmp byte [gr_bgmenabled], 0
    je gr_bgmhold
    mov byte [gr_bgmstatus], 0   ; an explicit enable retries unavailable FM
    cmp byte [gr_paused], 0
    jne .out
    cmp byte [gr_abon], 0
    jne .out
    jmp gr_bgmtick
.out:
    ret

; Release claims when paused, obscured, unfocused, or showing About. Resume
; at the next row with a fresh deadline, never a burst of overdue notes.
; Percussive envelopes also decay naturally if the UI stops servicing us.
gr_bgmhold:
    cmp byte [gr_bgmstatus], 1
    jne .out
    mov al, 3
    call OSAPI_SND_FM
    mov byte [gr_bgmstatus], 0
.out:
    ret

; Claim any three free channels (0..7). Failure releases partial claims and
; disables BGM until the next skyline, leaving the original effects working.
gr_bgmacquire:
    call OSAPI_SND_CAPS
    test ax, SND_CAP_FM
    jz .fail
    xor cx, cx
    xor di, di
    xor bx, bx
    mov bl, [gr_bgmtrack]
    shl bx, 1
    mov si, [gr_bgmpatches+bx]
.claim:
    mov al, 2
    call OSAPI_SND_FM
    jc .next
    mov [gr_bgmchannels+di], cl
    add si, 11
    inc di
    cmp di, 3
    je .ready
.next:
    inc cl
    cmp cl, 8
    jb .claim
.fail:
    mov al, 3
    call OSAPI_SND_FM
    mov byte [gr_bgmstatus], 2
    ret
.ready:
    mov byte [gr_bgmstatus], 1
    call OSAPI_GET_TICKS
    mov [gr_bgmdue], ax
    ret

gr_bgmtick:
    cmp byte [gr_bgmenabled], 0
    je .out
    cmp word [gr_bgmptr], 0
    je .out
    cmp byte [gr_bgmstatus], 2
    je .out
    cmp byte [gr_bgmstatus], 1
    je .clock
    call gr_bgmacquire
    cmp byte [gr_bgmstatus], 1
    jne .out
.clock:
    call OSAPI_GET_TICKS
    mov dx, ax
    sub ax, [gr_bgmdue]
    js .out
    cmp ax, 9
    ja .resync
    mov dx, [gr_bgmdue]
.resync:
    mov si, [gr_bgmptr]
    cmp byte [si], 0
    jne .row
    mov si, [gr_bgmstart]
.row:
    lodsb
    xor ah, ah
    add dx, ax
    mov [gr_bgmdue], dx
    xor di, di
.voice:
    mov cl, [gr_bgmchannels+di]
    mov al, 1
    call OSAPI_SND_FM       ; retrigger even repeated notes
    jc .lost
    lodsw
    mov bx, ax
    or bx, bx
    jz .next
    xor al, al
    call OSAPI_SND_FM
    jc .lost
.next:
    inc di
    cmp di, 3
    jb .voice
    mov [gr_bgmptr], si
.out:
    ret
.lost:
    ; Driver unloaded, or channels revoked: don't keep trying every tick.
    call gr_bgmhold
    mov byte [gr_bgmstatus], 2
    ret

; 5 modulator registers, 5 carrier registers, then feedback/connection.
; EGT clear on both operators: notes decay even without the next key-off.
; Carrier TL +18 = 13.5 dB attenuation (OPL steps are 0.75 dB).
; The additional 14 steps reduce the previous mix by approximately 70%.
; Leave modulators and tone/SFX unchanged.
GR_BGM_QUIET equ 18
gr_bgmpatches: dw gr_bgmbright, gr_bgmsoft, gr_bgmtwang
gr_bgmbright:
    db 01h,22h,0f3h,57h,00h, 01h,08h+GR_BGM_QUIET,0f4h,67h,00h, 04h ; mallet
    db 01h,20h,0f3h,47h,00h, 01h,0ch+GR_BGM_QUIET,0f3h,57h,00h, 06h ; bass
    db 02h,29h,0f5h,77h,00h, 01h,14h+GR_BGM_QUIET,0f5h,77h,00h, 02h ; pluck
gr_bgmsoft:
    db 02h,28h,0d3h,67h,00h, 01h,09h+GR_BGM_QUIET,0e3h,67h,00h, 02h ; bell
    db 01h,28h,0f3h,57h,00h, 01h,0ch+GR_BGM_QUIET,0f3h,57h,00h, 02h ; round bass
    db 01h,30h,0c4h,77h,00h, 02h,18h+GR_BGM_QUIET,0d4h,77h,00h, 00h ; soft chime
gr_bgmtwang:
    db 01h,1ch,0f4h,57h,00h, 01h,09h+GR_BGM_QUIET,0f4h,67h,00h, 06h ; twang
    db 01h,20h,0f3h,47h,00h, 01h,0ch+GR_BGM_QUIET,0f3h,57h,00h, 06h ; bass
    db 02h,25h,0f5h,77h,00h, 01h,15h+GR_BGM_QUIET,0f5h,77h,00h, 04h ; offbeat pluck

%include "grbgmdata.inc"
