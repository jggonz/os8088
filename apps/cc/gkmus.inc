; gkmus.inc - the game kit's music and effects player (SPEC.md 133.8).
; Original. C-callable (near cdecl, SPEC.md 73.3): arguments on the stack,
; AX out, BP/DS/DF kept, ES saved by every entry that loads it - a thunk the
; caller reaches next may still want ES = KERNEL_SEG.
;
; The banks are art records compiled by tools/os88gkmus.py and found through
; the band core's art lookup, bx_art_at (apps/breadxt/draw.inc), so this file
; NEEDS THE CORE included before it. A bank is resolved afresh on every use,
; never cached as a segment: the art part is a heap claim and may move.
; Offsets kept between ticks are from the part's segment base, which a move
; does not change.
;
; Routing, asked of OSAPI_SND_CAPS at every gkm_play: three FM voices on
; channels claimed by patch-load (OSAPI_SND_FM verb 2) from 0..7 - channel 8
; is the tone tier's - when a sound driver has FM AND the user's tone route
; is the driver (BL = 1); else the melody voice alone through OSAPI_SND_TONE
; at priority 20h, below every effect. Effects always go through the tone
; tier at 40h (channel 8 on FM, the speaker otherwise), so an effect takes
; the speaker over the melody and the melody comes back at its next note. A
; DUCK effect keys the FM voices off and holds them silent until it ends.
; The channels are released (verb 3) at gkm_stop, at a song's end, at
; gkm_done and the moment a note-on is refused (the driver went away, or the
; channel was revoked): music then stays off until the next gkm_play, so a
; lost claim never retries 18 times a second (Gorillas' rule, grbgm.inc).
;
; gkm_tick is the 18.2 Hz clock: it never draws and never waits. It advances
; by the OS ticks that really passed since the last call (OSAPI_GET_TICKS),
; at most GKM_CATCHUP of them, so a slow repaint skips the tune forward in
; time rather than stretching it, and only the last note of a skipped stretch
; is sounded.
section .text

GKM_V_PTR   equ 0       ; next event, segment offset; 0 = this voice is done
GKM_V_REM   equ 2       ; ticks left on the sounding event (signed)
GKM_V_LOOP  equ 4       ; where END goes when the song loops
GKM_V_RET   equ 6       ; the event after a pattern call
GKM_V_PST   equ 8       ; the pattern's first event
GKM_V_CNT   equ 10      ; the pattern's repeats left
GKM_V_DUR   equ 11      ; the duration a short note repeats
GKM_V_CH    equ 12      ; the FM channel
GKM_V_PEND  equ 13      ; scratch: the note this tick sounds, FFh none
GKM_VSZ     equ 14
GKM_OP_RET  equ 7Dh
GKM_OP_CALL equ 7Eh
GKM_CATCHUP equ 8
GKM_PRIO_MUS equ 20h
GKM_PRIO_FX  equ 40h
GKM_RT_FM   equ 1       ; gk.h's GKM_FM / GKM_SPK
GKM_RT_SPK  equ 2

; int gkm_init(int music_id, int sfx_id) - the bank IDs; music and effects
; on. Answers the route a song would take now: 1 FM, 2 speaker, 0 none.
_gkm_init:
    mov bx,sp
    mov ax,[ss:bx+2]
    mov [gkm_mid],ax
    mov ax,[ss:bx+4]
    mov [gkm_sid],ax
    mov byte [gkm_on],3
; gkm_route: AX = AL = [gkm_rt] = the route (above). Clobbers BX, CX, DX.
gkm_route:
    call OSAPI_SND_CAPS             ; AX = caps, BL = the tone route
    xor cx,cx
    test al,SND_CAP_TONE
    jz .have
    mov cl,GKM_RT_SPK
    test al,SND_CAP_FM
    jz .have
    test bl,bl                      ; the user chose the speaker: honour it
    jz .have
    mov cl,GKM_RT_FM
.have:
    mov [gkm_rt],cl
    xchg ax,cx
    ret

; int gkm_playing(void) - the song playing, -1 none
_gkm_playing:
    mov ax,[gkm_cur]
    dec ax
    ret

; void gkm_volume(int music_on, int sfx_on)
_gkm_volume:
    mov bx,sp
    xor cx,cx
    cmp word [ss:bx+2],0
    je .m
    inc cx
.m:
    cmp word [ss:bx+4],0
    je .s
    or cl,2
.s:
    mov [gkm_on],cl
    test cl,2
    jnz .fx
    call gkm_fxend
.fx:
    test cl,1
    jz _gkm_stop
    ret

; void gkm_done(void) - at close: the effect forgotten, the music stopped and
; every channel released. A tone already granted expires on its own lease.
_gkm_done:
    call gkm_fxend
; void gkm_stop(void) - the music silenced and its FM channels released
_gkm_stop:
    xor ax,ax
    cmp [gkm_cur],ax
    je .r
    mov [gkm_cur],ax
    cmp [gkm_held],al
    je .spk
    mov [gkm_held],al
    mov al,3                        ; all-off: keys off and releases
    call OSAPI_SND_FM
    ret
.spk:
    xor cx,cx                       ; the melody's tone off - refused, rightly,
    mov dl,GKM_PRIO_MUS             ; while an effect holds the tier
    call OSAPI_SND_TONE
.r:
    ret

; gkm_mbank / gkm_xbank: ES:SI = the song / effects bank; CF = 1 absent or
; not a bank (an ID the part does not hold reads as anything, and the magic
; refuses it). Clobbers AX, BX.
gkm_mbank:
    mov bx,[gkm_mid]
    mov ax,'GM'
    jmp short gkm_bank
gkm_xbank:
    mov bx,[gkm_sid]
    mov ax,'GX'
gkm_bank:
    push ax
    call bx_art_at
    pop ax
    jc .r
    cmp ax,[es:si]
    je .r                           ; equal: CF = 0
    stc
.r:
    ret

; void gkm_play(int song, int loop) - the song from its start (whatever was
; playing stops first); loop 1 = forever, 0 = once, then silence and release.
_gkm_play:
    push bp
    mov bp,sp
    push es
    call _gkm_stop
    test byte [gkm_on],1
    jz .out
    call gkm_route
    test al,al
    jz .out
    call gkm_mbank
    jc .out
    mov bx,[bp+4]
    xor ax,ax
    mov al,[es:si+3]
    cmp bx,ax                       ; unsigned: a negative song is refused too
    jae .out
    mov [gkm_base],si
    mov al,18                       ; a song's record: 3 x 6 bytes
    mul bl
    add ax,si
    add ax,8
    xchg ax,bx                      ; ES:BX = voice 0's (start, loop, patch)
    mov di,gkm_v
    xor cx,cx                       ; CL = the next channel to try
.v:
    mov ax,[es:bx+2]
    add ax,si
    mov [di+GKM_V_LOOP],ax
    mov ax,[es:bx]
    test ax,ax
    jz .z                           ; a silent voice stays done
    add ax,si
.z:
    mov [di+GKM_V_PTR],ax
    mov word [di+GKM_V_REM],0
    cmp byte [gkm_rt],GKM_RT_FM
    jne .n
    push si
    mov al,[es:bx+4]                ; the patch's 11 bytes, read in place:
    mov ah,11                       ; verb 2 stages from the caller's DS, so
    mul ah                          ; DS names the bank for the call
    add ax,[es:si+6]
    add si,ax
    push ds
    push es
    pop ds
.try:
    cmp cl,8                        ; 8 is the tone tier's: never ours
    jae .fail
    mov al,2                        ; patch-load claims the channel
    call OSAPI_SND_FM
    inc cx                          ; (CF survives inc)
    jc .try
    pop ds
    mov [di+GKM_V_CH],cl
    dec byte [di+GKM_V_CH]
    mov byte [gkm_held],1
    pop si
.n:
    add bx,6
    add di,GKM_VSZ
    cmp di,gkm_v+3*GKM_VSZ
    jb .v
    mov ax,[bp+4]
    inc ax
    mov [gkm_cur],ax
    mov al,[bp+6]
    mov [gkm_loop],al
    call OSAPI_GET_TICKS
    mov [gkm_last],ax
    xor ax,ax                       ; the first notes now, not a tick late
    call gkm_mstep
.out:
    pop es
    pop bp
    ret
.fail:                              ; three channels or none
    pop ds
    pop si
    mov word [gkm_cur],1            ; (gkm_stop's test)
    call _gkm_stop
    jmp short .out

; void gkm_tick(void) - once per OS tick, from the package's timer
_gkm_tick:
    push es
    call OSAPI_GET_TICKS
    mov dx,ax
    xchg dx,[gkm_last]
    sub ax,dx                       ; the ticks that passed
    jz .out
    cmp ax,GKM_CATCHUP
    jbe .ok
    mov ax,GKM_CATCHUP
.ok:
    push ax
    call gkm_fstep
    pop ax
    call gkm_mstep
.out:
    pop es
    ret

; gkm_mstep: AX = the ticks that passed (0 = sound what is due now). Runs the
; three voices' streams; ends the song (and releases) when all are done.
gkm_mstep:
    cmp word [gkm_cur],0
    je .r
    push ax
    call gkm_mbank
    pop dx                          ; DX = elapsed, all the way down
    jnc .have
    jmp _gkm_stop                   ; the bank went away
.have:
    mov di,gkm_v
.voice:
    mov si,[di+GKM_V_PTR]
    test si,si
    jz .next
    sub [di+GKM_V_REM],dx
    mov byte [di+GKM_V_PEND],0FFh
.fetch:
    cmp word [di+GKM_V_REM],0
    jg .store
.op:
    mov al,[es:si]
    inc si
    mov ah,al
    and al,7Fh
    cmp al,GKM_OP_RET
    jb .note
    je .ret
    cmp al,GKM_OP_CALL
    je .call
    mov si,[di+GKM_V_LOOP]          ; END: loop, or the voice is done
    cmp byte [gkm_loop],0
    jne .op
    xor si,si
    mov byte [di+GKM_V_PEND],0      ; key off
    jmp short .store
.call:
    mov bx,[es:si]
    add bx,[gkm_base]
    mov [di+GKM_V_PST],bx
    mov al,[es:si+2]
    mov [di+GKM_V_CNT],al
    add si,3
    mov [di+GKM_V_RET],si
    mov si,bx
    jmp short .op
.ret:
    mov si,[di+GKM_V_PST]
    dec byte [di+GKM_V_CNT]
    jnz .op
    mov si,[di+GKM_V_RET]
    jmp short .op
.note:
    test ah,ah
    jns .same
    mov ah,[es:si]                  ; a new duration
    inc si
    mov [di+GKM_V_DUR],ah
.same:
    mov [di+GKM_V_PEND],al
    mov bl,[di+GKM_V_DUR]
    xor bh,bh
    add [di+GKM_V_REM],bx
    jmp short .fetch
.store:
    mov [di+GKM_V_PTR],si
    mov al,[di+GKM_V_PEND]
    cmp al,0FFh
    je .next
    call gkm_out
.next:
    add di,GKM_VSZ
    cmp di,gkm_v+3*GKM_VSZ
    jb .voice
    mov ax,[gkm_v+GKM_V_PTR]
    or ax,[gkm_v+GKM_VSZ+GKM_V_PTR]
    or ax,[gkm_v+2*GKM_VSZ+GKM_V_PTR]
    jnz .r
    jmp _gkm_stop                   ; every voice done: a `once` song's end
.r:
    ret

; gkm_out: AL = the note index (0 = a rest), DI = the voice, ES = the bank.
; Clobbers AX, BX, CX, SI.
gkm_out:
    push dx
    xor bx,bx
    test al,al
    jz .hz
    mov bl,al
    shl bx,1
    mov si,[gkm_base]
    add bx,[es:si+4]                ; the Hz table
    mov bx,[es:bx+si-2]
.hz:                                ; BX = Hz, 0 = a rest
    cmp byte [gkm_rt],GKM_RT_FM
    jne .spk
    cmp byte [gkm_held],0
    je .r
    cmp byte [gkm_duck],0
    jne .r
    mov cl,[di+GKM_V_CH]
    mov al,1                        ; key off: every note retriggers
    call OSAPI_SND_FM
    jc .lost
    test bx,bx
    jz .r
    xor al,al
    call OSAPI_SND_FM
    jnc .r
.lost:                              ; refused: release, and no music until
    mov al,3                        ; the next gkm_play
    call OSAPI_SND_FM
    xor ax,ax
    mov [gkm_held],al
    mov [gkm_cur],ax
    jmp short .r
.spk:
    cmp di,gkm_v                    ; the speaker plays the melody alone
    jne .r
    test bx,bx
    jz .r
    xchg ax,bx
    mov cx,[di+GKM_V_REM]           ; the note's ticks, a tick short when it is
    cmp cx,2                        ; long enough to leave a gap
    jbe .t
    dec cx
.t:
    mov dl,GKM_PRIO_MUS
    call OSAPI_SND_TONE
.r:
    pop dx
    ret

; void gkm_sfx(int effect) - an effect from its start, replacing any playing
_gkm_sfx:
    push es
    test byte [gkm_on],2
    jz .out
    call gkm_xbank
    jc .out
    mov bx,sp
    mov bx,[ss:bx+4]
    xor ax,ax
    mov al,[es:si+3]
    cmp bx,ax
    jae .out
    shl bx,1
    add si,[es:bx+si+4]             ; the effect: flags, then its steps
    mov al,[es:si]
    inc si
    mov [gkm_fptr],si
    mov word [gkm_frem],0
    test al,1
    jz .go
    cmp byte [gkm_held],0
    je .go
    mov byte [gkm_duck],1           ; a DUCK effect: the FM voices off
    mov di,gkm_v
.k:
    mov cl,[di+GKM_V_CH]
    mov al,1
    call OSAPI_SND_FM
    add di,GKM_VSZ
    cmp di,gkm_v+3*GKM_VSZ
    jb .k
.go:
    xor ax,ax                       ; the first step now
    call gkm_fstep
.out:
    pop es
    ret

; gkm_fstep: AX = the ticks that passed. Each step is one tone with its own
; lease (CX ticks), so nothing has to turn it off.
gkm_fstep:
    cmp word [gkm_fptr],0
    je gkm_fxret
    sub [gkm_frem],ax
    call gkm_xbank
    jc gkm_fxend
.step:
    cmp word [gkm_frem],0
    jg gkm_fxret
    mov si,[gkm_fptr]
    mov cl,[es:si]                  ; ticks; 0 = the end
    test cl,cl
    jz gkm_fxend
    mov ax,[es:si+1]
    add si,3
    mov [gkm_fptr],si
    xor ch,ch
    add [gkm_frem],cx
    test ax,ax
    jz .step                        ; a rest: the last tone's lease ends it
    mov dl,GKM_PRIO_FX
    call OSAPI_SND_TONE
    jmp short .step
gkm_fxend:
    mov word [gkm_fptr],0
    mov byte [gkm_duck],0
gkm_fxret:
    ret

section .bss
gkm_mid:   resw 1
gkm_sid:   resw 1
gkm_cur:   resw 1           ; the song playing + 1; 0 = none
gkm_base:  resw 1           ; the song bank's offset in its segment
gkm_last:  resw 1           ; OSAPI_GET_TICKS at the last step
gkm_fptr:  resw 1           ; the effect's next step; 0 = none
gkm_frem:  resw 1
gkm_v:     resb 3*GKM_VSZ
gkm_rt:    resb 1
gkm_on:    resb 1           ; bit 0 music, bit 1 effects
gkm_loop:  resb 1
gkm_held:  resb 1           ; FM channels claimed
gkm_duck:  resb 1
section .text
