; Finite naval cues through the OS route: speaker, AdLib or Sound Blaster.
; SPEC.md 114.4 and 114.8; the effects, the samples and the music are 114.11.
WF_SND_LAUNCH equ 0             ; wf_fxsound's AL
WF_SND_EXPLODE equ 1
WF_SND_SPLASH equ 2
WF_FX_TICKS equ 10              ; what an effect lasts: a clip is 0.512 s
WF_BGM_REST equ 9               ; ...and the quiet the music leaves after it
WF_BGM_PRI equ 0x20             ; below every cue's 40h (SPEC.md 34.3)

; AL = cue 1..12 (wf_cues). A sunk, victory or defeat cue that arrives while
; an effect plays waits for it (wf_cueq) rather than talking over it.
wf_sfx:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [wf_muted], 0
    jne .out
    cmp al, 6
    je .wait
    cmp al, 8
    jb .now
    cmp al, 9
    ja .now
.wait:
    call wf_fxlive
    jnc .now
    mov [wf_cueq], al
    jmp short .out
.now:
    dec al
    cmp al, 12
    jae .out
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov ax, [wf_cues+bx]
    mov [wf_soundseq], ax
    mov byte [wf_soundwait], 0
    call OSAPI_GET_TICKS
    mov [wf_soundtime], ax
    call wf_soundnote
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; CF=1 while an effect's sound is still playing. wf_fxleft counts down in
; ticks (wf_soundtick), so no comparison can wrap however long the game runs.
wf_fxlive:
    cmp byte [wf_fxleft], WF_BGM_REST+1   ; CF = fxleft <= REST: over
    cmc
    ret

; AL = WF_SND_*: the sampled clip on a Sound Blaster route, else its tone
; sequence. Either way the music is keyed off now and stays silent until
; WF_BGM_REST ticks after the effect (114.11).
wf_fxsound:
    cmp byte [wf_muted], 0
    jne .out
    mov byte [wf_fxleft], WF_FX_TICKS+WF_BGM_REST
    call wf_bgmquiet
    cmp word [wf_pcmseg], 0
    je .tones
    push ax
    call wf_pcm_play
    pop ax
    jnc .out
.tones:
    add al, 10
    jmp wf_sfx
.out:
    ret

wf_soundtick:
    call OSAPI_GET_TICKS
    mov dx, ax
    sub dx, [wf_soundtime]
    jz .music
    mov [wf_soundtime], ax
    cmp dx, WF_FX_TICKS+WF_BGM_REST
    jb .count
    mov dl, WF_FX_TICKS+WF_BGM_REST
.count:
    sub [wf_fxleft], dl         ; the effect's countdown, by elapsed ticks
    jae .cue
    mov byte [wf_fxleft], 0
.cue:
    cmp byte [wf_soundwait], 0
    je .queued
    dec byte [wf_soundwait]
    jnz .music
    call wf_soundnote
.queued:
    cmp byte [wf_cueq], 0
    je .music
    call wf_fxlive
    jc .music
    xor ax, ax
    xchg al, [wf_cueq]
    call wf_sfx
.music:
    call wf_pcm_tick
    jmp wf_bgmtick
wf_soundnote:
    mov si, [wf_soundseq]
    or si, si
    jz .out
    mov ax, [si]
    or ax, ax
    jz .done
    xor cx, cx
    mov cl, [si+2]
    mov [wf_soundwait], cl
    add si, 3
    mov [wf_soundseq], si
    mov [wf_soundhz], ax
    mov dl, 0x40
    call OSAPI_SND_TONE
    jc .out
    mov byte [wf_soundactive], 1
    inc word [wf_soundnotes]
.out:
    ret
.done:
    mov word [wf_soundseq], 0
    mov byte [wf_soundactive], 0
    mov word [wf_soundhz], 0
    ret
wf_soundquiet:
    mov word [wf_soundseq], 0
    mov byte [wf_soundwait], 0
    mov byte [wf_soundactive], 0
    mov word [wf_soundhz], 0
    mov byte [wf_cueq], 0
    ; Every note is finite (at most three ticks). An unconditional off could
    ; silence another instance that has since won the same-priority router.
    ; Close's instance teardown releases only our own actual grant.
    call wf_pcm_halt
    jmp wf_bgmstop

; --- the music (114.11) ----------------------------------------------------
; U, and Game > Music: the track alone, said on the status line. The tick
; does the stopping and the starting (wf_bgmtick).
wf_musictoggle:
    xor byte [wf_musicoff], 1
    mov word [wf_message], wf_musicon
    cmp byte [wf_musicoff], 0
    je .said
    mov word [wf_message], wf_musicquiet
.said:
    mov byte [wf_dirty], 1
    ret
wf_musicon: db 'Music on. U turns the music off; M silences every sound.',0
wf_musicquiet: db 'Music off. U plays it again; the cues still sound.',0
; wf_bgmmode: 0 not yet asked, 1 FM (three claimed channels), 2 the tone
; route's melody, 3 refused - asked again after wf_bgmdue.
wf_bgmtick:
    cmp byte [wf_muted], 0
    jne wf_bgmstop
    cmp byte [wf_musicoff], 0
    jne wf_bgmstop
    call OSAPI_GET_TICKS
    mov dx, ax
    cmp byte [wf_bgmmode], 0
    je .acquire
    cmp byte [wf_bgmmode], 3
    jne .live
    sub ax, [wf_bgmdue]
    js .out
.acquire:
    call wf_bgmacquire
    jmp short .out
.live:
    cmp byte [wf_fxleft], 0
    jne .duck                   ; an effect, and the rest after it
    cmp byte [wf_bgmmode], 2
    jne .clock
    cmp word [wf_soundseq], 0   ; the speaker has one voice: every cue wins
    jne .duck
.clock:
    mov ax, dx
    sub ax, [wf_bgmdue]
    js .out
    cmp ax, 9
    jbe .row
    mov [wf_bgmdue], dx         ; a long stall restarts the clock, no burst
.row:
    jmp wf_bgmrow
.duck:
    call wf_bgmquiet
    mov [wf_bgmdue], dx         ; resume at once, at the row it stopped on
.out:
    ret

; Claim three free FM channels (0..7; 8 is the tone tier's) by patch-load,
; or fall back to the tone route's melody. Partial claims are released.
wf_bgmacquire:
    push dx
    call OSAPI_SND_CAPS
    test ax, SND_CAP_FM
    jz .tone
    xor cx, cx
    xor di, di
    mov si, wf_bgmpatches
.claim:
    mov al, 2
    call OSAPI_SND_FM
    jc .next
    mov [wf_bgmch+di], cl
    add si, 11
    inc di
    cmp di, 3
    je .fm
.next:
    inc cl
    cmp cl, 8
    jb .claim
    mov al, 3
    call OSAPI_SND_FM
.tone:
    mov byte [wf_bgmmode], 2
    jmp short .ready
.fm:
    mov byte [wf_bgmmode], 1
.ready:
    pop dx
    mov [wf_bgmdue], dx
    cmp word [wf_bgmptr], 0
    jne .out
    mov word [wf_bgmptr], wf_song
.out:
    ret

; One row: each FM voice whose byte is not HOLD is keyed off and, for a
; note, on again; the tone route plays the melody's note for its length.
wf_bgmrow:
    mov si, [wf_bgmptr]
    cmp byte [si], 0
    jne .play
    mov si, wf_song
.play:
    xor ax, ax
    lodsb
    add [wf_bgmdue], ax
    mov byte [wf_bgmsound], 1
    cmp byte [wf_bgmmode], 1
    jne .speaker
    xor di, di
.voice:
    lodsb
    cmp al, WF_BGM_HOLD
    je .next
    push ax
    mov cl, [wf_bgmch+di]
    mov al, 1
    call OSAPI_SND_FM
    pop bx
    jc .lost
    xor bh, bh
    or bl, bl
    jz .next
    shl bx, 1
    mov bx, [wf_bgmhz+bx]
    xor al, al
    call OSAPI_SND_FM
    jc .lost
.next:
    inc di
    cmp di, 3
    jb .voice
    inc si                      ; the melody's length is the speaker's
    jmp short .done
.speaker:
    lodsb
    add si, 3
    cmp al, WF_BGM_HOLD
    je .done
    xor bh, bh
    mov bl, al
    shl bx, 1
    mov ax, [wf_bgmhz+bx]
    or ax, ax
    jz .done
    xor cx, cx
    mov cl, [si-1]
    cmp cl, 1
    jbe .tone
    dec cx                      ; a tick of air between notes
.tone:
    mov dl, WF_BGM_PRI
    call OSAPI_SND_TONE
.done:
    mov [wf_bgmptr], si
    ret
.lost:
    ; Driver unloaded or a channel revoked: ask again in five seconds.
    call wf_bgmstop
    mov byte [wf_bgmmode], 3
    call OSAPI_GET_TICKS
    add ax, 91
    mov [wf_bgmdue], ax
    ret

; Key every voice off, keeping the channels and the place in the track.
wf_bgmquiet:
    cmp byte [wf_bgmsound], 0
    je .out
    mov byte [wf_bgmsound], 0
    push ax
    push cx
    push dx
    push di
    cmp byte [wf_bgmmode], 1
    jne .tone
    xor di, di
.voice:
    mov cl, [wf_bgmch+di]
    mov al, 1
    call OSAPI_SND_FM
    inc di
    cmp di, 3
    jb .voice
    jmp short .done
.tone:
    ; Our own note only: the router refuses an off below its owner's
    ; priority, so this cannot cut a cue (SPEC.md 34.3).
    xor ax, ax
    xor cx, cx
    mov dl, WF_BGM_PRI
    call OSAPI_SND_TONE
.done:
    pop di
    pop dx
    pop cx
    pop ax
.out:
    ret

; Stop and give the FM channels back; the track restarts from its top.
wf_bgmstop:
    call wf_bgmquiet
    cmp byte [wf_bgmmode], 1
    jne .reset
    mov al, 3
    call OSAPI_SND_FM
.reset:
    mov byte [wf_bgmmode], 0
    mov word [wf_bgmptr], 0
    ret

; --- the samples (114.11): 1942's shape, SPEC.md 101.6 ---------------------
; Three 4,096-sample external rings in one page-safe claim, 4,112 bytes
; apart (the ring, then its two control words). Asked at window open: only
; the Sound Blaster tier, only when the heap holds the claim and the part.
WF_PCM_STEP equ 4112
wf_pcm_open:
    cmp word [wf_pcmseg], 0
    jne .out
    call OSAPI_SND_CAPS
    test ax, SND_CAP_PCM_BG
    jz .out
    or bl, bl
    jz .out
    mov byte [wf_pcmflags], SND_OPENF_RING + SND_OPENF_EXT
    test ax, SND_CAP_EXTBLK
    jz .price
    or byte [wf_pcmflags], 3 << SND_OPENF_BLKSH
.price:
    call op_clbget
    jc .out
    mov al, WF_P_SFX
    call op_row
    call op_lazykb
    jc .out
    add ax, 13
    xchg ax, bx
    call OSAPI_MEM_AVAIL
    cmp ax, bx
    jb .out
    mov ax, 13
    mov cx, 13
    call OSAPI_MEM_CLAIM_DMA_HI
    jc .out
    mov [wf_pcmseg], dx
    mov al, WF_P_SFX
    call op_fetch
    jc .bad
    mov al, WF_P_SFX
    call op_seg
    push es
    push ds
    mov es, [wf_pcmseg]
    mov ds, ax
    cmp word [0], 'WF'          ; the part is this file's own, but a header
    jne .wrong                  ; that does not say so is not played
    cmp word [8], 3
    jne .wrong
    mov si, 16
    xor di, di
    mov dx, 3
.clip:
    mov cx, 2048
    rep movsw
    add di, WF_PCM_STEP-4096
    dec dx
    jnz .clip
    pop ds
    pop es
    mov al, WF_P_SFX
    call op_drop
.out:
    ret
.wrong:
    pop ds
    pop es
    mov al, WF_P_SFX
    call op_drop
.bad:
    jmp wf_pcm_close

wf_pcm_halt:
    cmp byte [wf_pcmactive], 0
    je .out
    mov ah, [wf_pcmhandle]
    mov al, 2
    call OSAPI_SND_STREAM
    mov byte [wf_pcmactive], 0
.out:
    ret

wf_pcm_close:
    call wf_pcm_halt
    mov dx, [wf_pcmseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [wf_pcmseg], 0
.out:
    ret

; Halt a clip that has played to its end.
wf_pcm_tick:
    cmp byte [wf_pcmactive], 0
    je .out
    mov al, SND_V_PPOS
    mov ah, [wf_pcmhandle]
    call OSAPI_SND_STREAM
    jc .ended
    cmp ax, SND_ST_PLAYING
    jne .ended
    cmp dx, 4096
    jb .out
.ended:
    jmp wf_pcm_halt
.out:
    ret

; AL = WF_SND_*. CF=1 = the card refused, and the samples are given up for
; the session: the tone sequences play from then on.
wf_pcm_play:
    push es
    push ax
    call wf_pcm_halt
    pop ax
    xor ah, ah
    mov bx, WF_PCM_STEP
    mul bx
    mov si, ax
    mov es, [wf_pcmseg]
    mov word [es:si+4096+SND_EXT_TOTAL], 4096
    mov word [es:si+4096+SND_EXT_CONS], 0
    mov di, es
    mov dx, 8000
    mov ah, [wf_pcmflags]
    xor al, al
    call OSAPI_SND_STREAM
    jc .failed
    mov [wf_pcmhandle], ah
    mov byte [wf_pcmactive], 1
    pop es
    clc
    ret
.failed:
    call wf_pcm_close
    pop es
    stc
    ret

wf_cues: dw wf_cueplace,wf_cueready,wf_cuefire,wf_cuemiss,wf_cuehit,wf_cuesunk,wf_cuelink,wf_cuewin,wf_cuelose
    dw wf_cuelaunch,wf_cueboom,wf_cuesplash
wf_cueplace: dw 660
    db 1
    dw 0
wf_cueready: dw 660
    db 1
    dw 880
    db 2
    dw 0
wf_cuefire: dw 165
    db 1
    dw 110
    db 1
    dw 0
wf_cuemiss: dw 440
    db 1
    dw 220
    db 1
    dw 0
wf_cuehit: dw 110
    db 2
    dw 82
    db 2
    dw 0
wf_cuesunk: dw 220
    db 2
    dw 165
    db 2
    dw 110
    db 3
    dw 0
wf_cuelink: dw 440
    db 1
    dw 880
    db 1
    dw 0
wf_cuewin: dw 784
    db 2
    dw 988
    db 2
    dw 1175
    db 3
    dw 0
wf_cuelose: dw 330
    db 2
    dw 220
    db 2
    dw 165
    db 3
    dw 0
; 114.11's tone forms of the three effects, a tick a note: a rising launch,
; a falling low rumble, and a scatter of spray that drops away.
wf_cuelaunch: dw 196
    db 1
    dw 262
    db 1
    dw 370
    db 1
    dw 523
    db 1
    dw 740
    db 1
    dw 932
    db 1
    dw 0
wf_cueboom: dw 131
    db 1
    dw 98
    db 1
    dw 147
    db 1
    dw 73
    db 1
    dw 110
    db 1
    dw 62
    db 1
    dw 82
    db 1
    dw 55
    db 2
    dw 0
wf_cuesplash: dw 1760
    db 1
    dw 1175
    db 1
    dw 2093
    db 1
    dw 880
    db 1
    dw 1397
    db 1
    dw 659
    db 1
    dw 1047
    db 1
    dw 494
    db 1
    dw 0

; 5 modulator registers, 5 carrier registers, then feedback/connection -
; Gorillas' tested voices (SPEC.md 99), carriers 18 steps (13.5 dB) quieter
; so the music sits under the cues on channel 8.
WF_BGM_QUIET equ 18
wf_bgmpatches:
    db 02h,28h,0d3h,67h,00h, 01h,09h+WF_BGM_QUIET,0e3h,67h,00h, 02h ; bell
    db 01h,20h,0f3h,47h,00h, 01h,0ch+WF_BGM_QUIET,0f3h,57h,00h, 06h ; bass
    db 01h,30h,0c4h,77h,00h, 02h,18h+WF_BGM_QUIET,0d4h,77h,00h, 00h ; chime

%include "wirefleet/wfsong.inc"
