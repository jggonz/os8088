; Local NES arrangements, native bounded sequencer. SPEC 99.
; Music records: phrase-list ptr, event ptr, remaining NES frames, note Hz.
; No allocations, sample loops, IRQ hooks or per-frame patch loads.
DM_AV_SIZE equ 8
DM_FX_CURSOR equ 1
DM_FX_MOVE equ 2
DM_FX_ROTATE equ 3
DM_FX_FALL equ 4
DM_FX_LAND equ 5
DM_FX_SPEED equ 6
DM_FX_CLEAR equ 7
DM_FX_PAUSE equ 8

; Claims are all-or-nothing, acquired only at entry. Respect OS route policy.
dm_audio_open:
    call OSAPI_SND_CAPS
    mov byte [dm_audio_fm], 0
    test ax, SND_CAP_FM
    jz .done
    or bl, bl
    jz .done
    xor cx, cx
.patch:
    mov si, dm_audio_patch
    cmp cl, 3
    jne .music
    mov si, dm_audio_fxpatch
    jmp .load
.music:
    cmp cl, 2
    jne .load
    mov si, dm_audio_bass
.load:
    mov al, 2
    call OSAPI_SND_FM
    jc .refused
    inc cl
    cmp cl, 4
    jb .patch
    mov byte [dm_audio_fm], 1
.done:
    mov byte [dm_audio_live], 1
    mov word [dm_audio_sent], -1
    ret
.refused:
    mov al, 3
    call OSAPI_SND_FM
    jmp .done

; Silence only our FM claims. Tone requests obey the OS priority arbiter.
; Speaker notes have finite leases, so a suspended/closed instance cannot hang.
dm_audio_quiet:
    cmp byte [dm_audio_live], 0
    je .out
    cmp byte [dm_audio_fm], 0
    je .speaker
    mov al, 3
    call OSAPI_SND_FM
    jmp .clear
.speaker:
    xor ax, ax
    xor cx, cx
    mov dl, 40h
    call OSAPI_SND_TONE
.clear:
    mov word [dm_audio_pending], 0
    mov byte [dm_audio_pending+2], 0
    mov byte [dm_audio_live], 0
    mov word [dm_audio_sent], -1
    mov word [dm_fx_ptr], 0
    mov byte [dm_fx_wait], 0
    mov byte [dm_fx_priority], 0
.out: ret

; AL=song index, 0/1 gameplay, 2/3 clear, 4 title, 5 options, 6 loss.
; Loading changes no hardware; the next tick emits the first note.
dm_audio_song:
    push ax
    cmp byte [dm_audio_live], 0
    je .reset
    cmp byte [dm_audio_fm], 0
    je .reset
    xor cx, cx
.mute:
    mov al, 1
    call OSAPI_SND_FM
    inc cl
    cmp cl, 4
    jb .mute
.reset:
    pop ax
    xor ah, ah
    mov bx, ax
    shl ax, 1
    shl bx, 1
    shl bx, 1
    add bx, ax
    mov di, dm_audio_voices
    mov cx, 3
.voice:
    mov ax, [dm_song_table+bx]
    mov [di], ax
    mov si, ax
    mov ax, [si]
    mov [di+2], ax
    mov word [di+4], 1
    mov word [di+6], 0
    add bx, 2
    add di, DM_AV_SIZE
    loop .voice
    mov word [dm_audio_phase], 0
    mov byte [dm_fx_priority], 0
    mov word [dm_fx_ptr], 0
    mov byte [dm_fx_wait], 0
    mov word [dm_audio_sent], -1
    mov word [dm_audio_pending], 0
    mov byte [dm_audio_pending+2], 0
    mov byte [dm_audio_turn], 0
    ret

dm_audio_game:
    mov al, [dm_music]
    cmp al, 2
    jb .load
    xor al, al
.load:
    jmp dm_audio_song

; Re-key current pitches after releasing claims for pause or desktop exit.
dm_audio_restore:
    cmp byte [dm_music], 2
    je .out
    cmp byte [dm_paused], 0
    jne .out
    cmp byte [dm_audio_fm], 0
    je .out
    mov di, dm_audio_voices
    xor cx, cx
.voice:
    mov bx, [di+6]
    call dm_audio_fmnote
    add di, DM_AV_SIZE
    inc cl
    cmp cl, 3
    jb .voice
.out: ret

dm_music_cycle:
    mov byte [dm_huddirty], 1
    call dm_audio_quiet
    inc byte [dm_music]
    cmp byte [dm_music], 3
    jb .set
    mov byte [dm_music], 0
.set:
    cmp byte [dm_fs], 0
    jne .game
    cmp byte [dm_started], 0
    je .options
    call dm_audio_game
    jmp .desktop
.options:
    mov al, 5
    call dm_audio_song
.desktop:
    mov byte [dm_paused], 0
    mov bx, [dm_win]
    mov ax, 1
    call OSAPI_WM_TIMER
    jmp .open
.game:
    call dm_audio_game
    cmp byte [dm_state], 3
    jb .open
    mov al, 6
    je .terminal
    mov al, [dm_music]
    and al, 1
    add al, 2
.terminal:
    call dm_audio_song
.open:
    call dm_audio_open
    mov al, DM_FX_CURSOR
    jmp dm_effect

; Desktop title/options use the existing 18.2Hz window timer. Stop on loss
; of focus. After fullscreen, only a newly requested reveal rearms the timer;
; the game's saved music stays paused until play resumes or setup changes.
dm_audio_timer:
    SAVE
    call dm_fronttick
    cmp byte [dm_paused], 0
    jne .arm
    call OSAPI_WM_TOP
    cmp bx, [dm_win]
    jne .quiet
    cmp byte [dm_audio_live], 0
    jne .tick
    call dm_audio_open
    call dm_audio_restore
.tick:
    mov bp, 3
.frame:
    push bp
    call dm_audio_tick
    pop bp
    dec bp
    jnz .frame
    jmp .arm
.quiet:
    call dm_audio_quiet
.arm:
    mov bx, [dm_win]
    mov ax, 1
    cmp byte [dm_paused], 0
    je .timer
    mov dx, [dm_reveal]
    cmp dx, [dm_frontheight]
    jb .timer
    xor ax, ax
.timer:
    call OSAPI_WM_TIMER
    RESTORE
    ret

; Event requests preserve ALL gameplay registers. Larger ID wins; equal
; effects restart, lower-priority movement cannot interrupt a clear cue.
dm_effect:
    pushf
    push ax
    push bx
    cmp al, [dm_fx_priority]
    jb .out
    mov [dm_fx_priority], al
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [dm_effects+bx-2]
    mov [dm_fx_ptr], ax
    mov byte [dm_fx_wait], 1
.out:
    pop bx
    pop ax
    popf
    ret

; One frame. Music is frozen during pause; a pause cue can finish independently.
; 600/546 advances 1 or 2 NES frames, never a catch-up loop. Only actual note
; boundaries call the OS. Each voice decodes at most two events per tick.
dm_audio_tick:
    cmp byte [dm_audio_live], 0
    jne .live
    ret
.live:
    mov bp, 2                 ; at most two FM notes per gameplay frame
    cmp byte [dm_paused], 0
    jne .effects
    cmp byte [dm_music], 2
    je .effects
    mov dx, 1
    add word [dm_audio_phase], 54
    cmp word [dm_audio_phase], 546
    jb .clock
    sub word [dm_audio_phase], 546
    inc dx
.clock:
    mov di, dm_audio_voices
    xor cx, cx
.voice:
    cmp word [di+2], 0
    je .next
    sub [di+4], dx
    jg .next
.event:
    mov si, [di+2]
    lodsb
    or al, al
    jnz .note
    mov bx, [di]
    add bx, 2
    mov si, [bx]
    or si, si
    jnz .phrase
    mov bx, [bx+2]
    or bx, bx
    jz .end
    mov si, [bx]
.phrase:
    mov [di], bx
    lodsb
.note:
    xor ah, ah
    add [di+4], ax
    lodsb
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [dm_note_hz+bx]
    mov [di+6], ax
    mov [di+2], si
    cmp word [di+4], 0
    jle .event
    jmp .emit
.end:
    mov word [di+2], 0
    mov word [di+6], 0
.emit:
    mov bx, cx
    mov byte [dm_audio_pending+bx], 1
.next:
    ; Speaker only walks the lead; accompaniment costs it nothing.
    cmp byte [dm_audio_fm], 0
    je .effects
    add di, DM_AV_SIZE
    inc cl
    cmp cl, 3
    jb .voice
.effects:
    cmp byte [dm_fx_wait], 0
    je .speaker
    dec byte [dm_fx_wait]
    jnz .speaker
    mov si, [dm_fx_ptr]
    lodsb
    or al, al
    jz .fxend
    mov [dm_fx_wait], al
    lodsw
    mov [dm_fx_hz], ax
    mov [dm_fx_ptr], si
    jmp .fxemit
.fxend:
    mov word [dm_fx_hz], 0
    mov byte [dm_fx_priority], 0
.fxemit:
    cmp byte [dm_audio_fm], 0
    je .speaker
    mov bx, [dm_fx_hz]
    mov cl, 3
    call dm_audio_fmnote
    dec bp                    ; effects have first claim on the write budget
.speaker:
    cmp byte [dm_audio_fm], 0
    jne .fmflush
    xor ax, ax
    cmp byte [dm_music], 2
    je .fx
    cmp byte [dm_paused], 0
    jne .fx
    mov ax, [dm_audio_voices+6]
.fx:
    cmp byte [dm_fx_priority], 0
    je .send
    mov ax, [dm_fx_hz]
.send:
    cmp ax, [dm_audio_sent]
    jne .tone
    ; Lease refresh is needed only once every 48 fast ticks (16 OS ticks).
    dec byte [dm_audio_lease]
    jnz .out
    or ax, ax
    jz .out
.tone:
    mov [dm_audio_sent], ax
    mov byte [dm_audio_lease], 48
    mov cx, 20
    mov dl, 40h
    call OSAPI_SND_TONE
    jnc .out
    mov word [dm_audio_sent], -1
.out: ret
.fmflush:
    cmp word [dm_audio_pending], 0
    jne .pending
    cmp byte [dm_audio_pending+2], 0
    je .out
.pending:
    ; Fair rotation: a chord's third note waits at most one frame normally,
    ; two with an effect on both frames. Never replay a backlog of old notes.
    xor cx, cx
    mov cl, [dm_audio_turn]
    mov dx, 3
.scan:
    mov bx, cx
    cmp byte [dm_audio_pending+bx], 0
    je .advance
    mov byte [dm_audio_pending+bx], 0
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov bx, [dm_audio_voices+bx+6]
    call dm_audio_fmnote
    dec bp
.advance:
    inc cl
    cmp cl, 3
    jb .turn
    xor cl, cl
.turn:
    mov [dm_audio_turn], cl
    or bp, bp
    jz .out
    dec dx
    jnz .scan
    ret

; CL=channel BX=Hz; key-off retriggers equal adjacent notes. Patch stays loaded.
dm_audio_fmnote:
    mov al, 1
    call OSAPI_SND_FM
    or bx, bx
    jz .out
    xor al, al
    call OSAPI_SND_FM
.out: ret

dm_audio_patch:
    ; AR=14 softens music onsets in hardware, without extra tick work.
    ; Keep decay, sustain, release, levels and note articulation unchanged.
    db 21h,24h,0e2h,07h,00h,21h,0ch,0e2h,07h,00h,04h
dm_audio_fxpatch:
    ; Short gameplay cues retain their original immediate attack.
    db 21h,24h,0f2h,07h,00h,21h,0ch,0f2h,07h,00h,04h
dm_audio_bass:
    db 21h,3fh,0e0h,07h,00h,21h,10h,0e0h,07h,00h,00h
; Native tonal arrangements of NES event roles ($8D10/$8DDE/$8E39/$8F3A,
; $926F/$927C); no per-frame NES envelopes or sampled percussion.
dm_effects: dw dm_fx_cursor,dm_fx_move,dm_fx_rotate,dm_fx_fall
            dw dm_fx_land,dm_fx_speed,dm_fx_clear,dm_fx_pause
%macro DM_NOTE 2
    db %1
    dw %2
%endmacro
dm_fx_cursor:
    DM_NOTE 2,880
    db 0
dm_fx_move:
    DM_NOTE 1,392
    db 0
dm_fx_rotate:
    DM_NOTE 1,660
    DM_NOTE 2,880
    db 0
dm_fx_fall:
    DM_NOTE 1,220
    db 0
dm_fx_land:
    DM_NOTE 2,165
    DM_NOTE 2,110
    db 0
dm_fx_speed:
    DM_NOTE 3,523
    DM_NOTE 3,659
    DM_NOTE 4,784
    db 0
dm_fx_clear:
    DM_NOTE 2,784
    DM_NOTE 2,988
    DM_NOTE 4,1175
    db 0
dm_fx_pause:
    DM_NOTE 3,784
    DM_NOTE 3,523
    db 0
%include "dm-music.inc"
