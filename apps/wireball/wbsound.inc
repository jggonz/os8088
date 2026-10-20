; Finite, tick-driven cues through the OS's tone route (SPEC.md 109.4, 34).
; No direct PIT/speaker/OPL access, no blocking playback or sleeps.
WB_FX_WALL equ 1
WB_FX_PADDLE equ 2
WB_FX_POINT equ 3
WB_FX_SERVE equ 4
WB_FX_WIN equ 5
WB_FX_LINK equ 6

; AL=cue; keeps every register. An event starts its first note immediately.
wb_sound_play:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [wb_muted], 0
    jne .out
    xor ah, ah
    dec ax
    cmp ax, 6
    jae .out
    shl ax, 1
    mov bx, ax
    mov ax, [wb_soundtable+bx]
    mov [wb_soundseq], ax
    mov byte [wb_soundwait], 0
    call wb_sound_tick
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

wb_sound_tick:
    push ax
    push cx
    push dx
    push si
    cmp byte [wb_muted], 0
    jne .out
    mov si, [wb_soundseq]
    or si, si
    jz .out
    cmp byte [wb_soundwait], 0
    je .note
    dec byte [wb_soundwait]
    jnz .out
.note:
    mov ax, [si]
    or ax, ax
    jz .end
    xor cx, cx
    mov cl, [si+2]
    mov [wb_soundwait], cl
    add si, 3
    mov [wb_soundseq], si
    mov [wb_soundhz], ax
    mov dl, 0x40
    call OSAPI_SND_TONE
    jc .out                     ; sound ownership refusal never delays the wire
    mov byte [wb_soundactive], 1
    inc word [wb_soundnotes]
    jmp .out
.end:
    mov byte [wb_soundactive], 0
    mov word [wb_soundseq], 0
    mov word [wb_soundhz], 0
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

wb_sound_quiet:
    push ax
    push cx
    push dx
    mov word [wb_soundseq], 0
    mov byte [wb_soundwait], 0
    mov word [wb_soundhz], 0
    cmp byte [wb_soundactive], 0
    je .out                     ; a splash mute has no tone to release
    mov byte [wb_soundactive], 0
    xor ax, ax
    xor cx, cx
    mov dl, 0x40
    call OSAPI_SND_TONE          ; inside FSX: other instances are frozen/silenced
.out:
    pop dx
    pop cx
    pop ax
    ret

wb_sound_toggle:
    xor byte [wb_muted], 1
    cmp byte [wb_muted], 0
    je .out
    call wb_sound_quiet
.out:
    ret

; DI=validated STATE packet. Infer remote cues BEFORE copying its state.
; Full snapshots may repeat; only a transition produces a cue.
wb_sound_remote:
    push ax
    cmp byte [wb_phase], 2
    jne .link
    mov al, [di+20]
    cmp al, [wb_scorel]
    jne .score
    mov al, [di+21]
    cmp al, [wb_scorer]
    jne .score
    test byte [di+22], 1
    jz .resume
    test byte [wb_flags], 1
    jnz .out
    call wb_sound_quiet
    jmp .out
.resume:
    test byte [wb_flags], 1
    jnz .serve
    cmp byte [di+23], 0
    jne .out
    cmp byte [wb_count], 0
    jne .serve
    mov al, [di+18]
    cmp al, [wb_vx]
    jne .paddle
    mov al, [di+19]
    xor al, [wb_vy]
    test al, 0x80
    jz .out
    mov al, WB_FX_WALL
    jmp .play
.paddle:
    mov al, WB_FX_PADDLE
    jmp .play
.score:
    mov al, WB_FX_POINT
    test byte [di+22], 2
    jz .play
    mov al, WB_FX_WIN
    jmp .play
.link:
    mov al, WB_FX_LINK
    jmp .play
.serve:
    mov al, WB_FX_SERVE
.play:
    call wb_sound_play
.out:
    pop ax
    ret

wb_soundtable: dw wb_soundwall,wb_soundpaddle,wb_soundpoint,wb_soundserve,wb_soundwin,wb_soundlink
wb_soundwall: dw 440
    db 1
    dw 0
    db 0
wb_soundpaddle: dw 880
    db 1
    dw 0
    db 0
wb_soundpoint: dw 220
    db 2
    dw 165
    db 2
    dw 0
    db 0
wb_soundserve: dw 660
    db 1
    dw 0
    db 0
wb_soundwin: dw 784
    db 2
    dw 988
    db 2
    dw 1175
    db 3
    dw 0
    db 0
wb_soundlink: dw 440
    db 1
    dw 880
    db 1
    dw 0
    db 0
