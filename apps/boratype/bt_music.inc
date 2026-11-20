; Boratype game-kit music pause/resume. No PCM, files or private driver.
section .text
; Preserve phase while releasing FM channels at focus loss/pause/speech.
; Save the sequencer's per-voice stream/duration state, reacquire patches
; on resume, then restore only the first12 bytes (the new channels stay).
_bt_mhold:
    push bp
    mov bp,sp
    push es
    mov ax,[bp+4]
    or ax,ax
    jz .resume
    cmp byte [btm_held],0
    jne .out
    mov byte [btm_held],1
    mov ax,[gkm_cur]
    mov [btm_song],ax
    mov al,[gkm_loop]
    mov [btm_loop],al
    push ds
    pop es
    mov si,gkm_v
    mov di,btm_voices
    mov cx,3*GKM_VSZ
    cld
    rep movsb
    call gkm_fxend
    call _gkm_stop
    jmp short .out
.resume:
    cmp byte [btm_held],0
    je .out
    mov byte [btm_held],0
    mov ax,[btm_song]
    or ax,ax
    jz .out
    dec ax
    xor bx,bx
    mov bl,[btm_loop]
    push bx
    push ax
    call _gkm_play
    add sp,4
    cmp word [gkm_cur],0
    je .out                       ; refused: do not resurrect a lost claim
    push ds
    pop es
    mov si,btm_voices
    mov di,gkm_v
    mov bx,3
.copy:
    mov cx,12
    rep movsb
    add si,2
    add di,2                      ; newly granted FM channel IDs retained
    dec bx
    jnz .copy
.out:
    cld
    pop es
    pop bp
    ret
section .bss
btm_held: resb 1
btm_song: resw 1
btm_loop: resb 1
btm_voices: resb 3*GKM_VSZ
section .text
