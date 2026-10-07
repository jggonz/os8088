; Native cartridge note arrangements. Two FM voices on AdLib / Sound Blaster,
; Recorded one-shot effects on Sound Blaster; no mixer or per-sample work.
; Stream records: pointer, wait (BIOS ticks), held Hz, last emitted FM Hz.
n_audio_open:
    SAVE
    mov byte [n_audiofm],0
    mov word [n_musicsent],0
    mov word [n_effectsent],0
    mov word [n_tonesent],0
    call OSAPI_GET_TICKS
    mov [n_audiotick],ax
    call OSAPI_SND_CAPS
    test ax,SND_CAP_FM
    jz .out
    or bl,bl                       ; respect the Control Panel speaker route
    jz .out
    xor cx,cx
    mov si,n_musicpatch
    mov al,2
    call OSAPI_SND_FM
    jc .refused
    inc cl
    mov si,n_effectpatch
    call OSAPI_SND_FM
    jc .refused
    mov byte [n_audiofm],1
    jmp .out
.refused:
    mov al,3                       ; release only this instance's claims
    call OSAPI_SND_FM
.out:
    call n_pcm_open
    RESTORE
    ret

n_audio_close:
    call n_audio_quiet
    call n_pcm_close
    SAVE
    cmp byte [n_audiofm],0
    je .out
    mov al,3
    call OSAPI_SND_FM
    mov byte [n_audiofm],0
.out:
    RESTORE
    ret

; Silence without losing held notes or freeing patches; resume re-emits them.
n_audio_quiet:
    SAVE
    call n_pcm_halt
    mov byte [n_pcmpending],0
    xor ax,ax
    cmp byte [n_audiofm],0
    je .speaker
    mov bx,n_musicptr
    xor cx,cx
    call n_audio_fmnote
    mov bx,n_effectptr
    inc cl
    call n_audio_fmnote
    jmp .out
.speaker:
    call n_tone
.out:
    RESTORE
    ret

n_music:
    SAVE
    shl ax,1
    mov bx,ax
    mov ax,[n_soundtab+bx]
    mov [n_musicptr],ax
    mov word [n_musicwait],0
    RESTORE
    ret
n_effect:
    SAVE
    cmp ax,18
    jb .notes
    cmp ax,20
    ja .notes
    cmp word [n_pcmseg],0
    je .notes
    mov bx,ax
    sub bl,17
    cmp bl,[n_pcmpri]
    jb .out
    cmp bl,[n_pcmpending]
    jb .out
    mov [n_pcmpending],bl
    mov word [n_effectptr],0
    jmp .out
.notes:
    cmp byte [n_audiofm],0
    je .cartridge
    cmp ax,18
    jb .cartridge
    cmp ax,21
    ja .cartridge
    mov bx,ax
    sub bx,18
    shl bx,1
    mov ax,[n_combatfx+bx]
    jmp .set
.cartridge:
    shl ax,1
    mov bx,ax
    mov ax,[n_soundtab+bx]
.set:
    mov [n_effectptr],ax
    mov word [n_effectwait],0
.out:
    RESTORE
    ret

n_audio:
    SAVE
    call OSAPI_GET_TICKS
    mov dx,ax
    sub ax,[n_audiotick]
    mov [n_audiotick],dx
    mov [n_audioelapsed],ax
    cmp byte [n_sound],0
    je .off
    cmp byte [n_paused],0
    jne .off
    call n_pcm_tick
    mov bx,n_effectptr
    call n_audio_step
    cmp byte [n_audiofm],0
    jne .fm
    cmp word [n_effectptr],0
    jne .speaker
    mov bx,n_musicptr
    call n_audio_step
.speaker:
    mov ax,[bx+4]
    call n_tone
    jmp .out
.fm:
    mov bx,n_musicptr
    call n_audio_step
    xor cx,cx
    mov ax,[bx+4]
    call n_audio_fmnote
    mov bx,n_effectptr
    inc cl
    mov ax,[bx+4]
    call n_audio_fmnote
    jmp .out
.off:
    call n_audio_quiet
.out:
    RESTORE
    ret

; BX=record. Bounded to one event per call, even after a long frame/load.
n_audio_step:
    cmp word [bx],0
    je .ended
    mov ax,[n_audioelapsed]
    cmp [bx+2],ax
    jbe .note
    sub [bx+2],ax
    ret
.note:
    mov si,[bx]
    lodsw
    cmp ax,65535
    jne .play
    lodsw
    cmp bx,n_effectptr             ; effects are one-shot, even NES loops
    je .end
    mov [bx],ax
    or ax,ax
    jz .ended
    mov si,ax                     ; generated loop targets begin with a note
    lodsw
.play:
    mov [bx+4],ax
    lodsw
    mov [bx+2],ax
    mov [bx],si
    mov word [bx+6],65535          ; retrigger repeated FM notes, too
    ret
.end:
    mov word [bx],0
.ended:
    mov word [bx+4],0
    ret

; AX=Hz, BX=record, CL=channel. Chip sustains held notes without more calls.
n_audio_fmnote:
    push ax
    push bx
    or ax,ax
    jz .ready
.high:
    cmp ax,6208
    jbe .low
    shr ax,1
    jmp .high
.low:
    cmp ax,19
    jae .ready
    shl ax,1
    jmp .low
.ready:
    cmp ax,[bx+6]
    je .out
    mov [bx+6],ax
    mov bx,ax
    mov al,1
    call OSAPI_SND_FM
    or bx,bx
    jz .out
    xor al,al
    call OSAPI_SND_FM
.out:
    pop bx
    pop ax
    ret

n_tone:
    SAVE
    cmp ax,[n_tonesent]
    je .out
    mov cx,0
    mov dl,40h
    push ax
    call OSAPI_SND_TONE
    pop ax
    jc .out
    mov [n_tonesent],ax
.out:
    RESTORE
    ret

; Sustained melodic voice and brighter effect voice; envelopes run in OPL.
; Five modulator registers, five carrier registers, feedback/connection.
n_musicpatch:  db 21h,24h,0e2h,07h,00h,21h,0ch,0e2h,07h,00h,04h
n_effectpatch: db 0fh,00h,0f8h,0fh,00h,01h,08h,0f8h,0fh,00h,0eh
n_combatfx: dw n_gunfx,n_boomfx,n_lossfx,n_rollfx
n_gunfx:  dw 180,1,90,1,0,1,65535,0
n_boomfx: dw 80,2,40,2,0,1,65535,0
n_lossfx: dw 60,3,30,3,0,1,65535,0
n_rollfx: dw 120,1,90,1,60,1,0,1,65535,0
%include "pcm.inc"
