; Finite naval cues through the OS route: speaker, AdLib or Sound Blaster.
wf_sfx:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [wf_muted], 0
    jne .out
    dec al
    cmp al, 9
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
wf_soundtick:
    call OSAPI_GET_TICKS
    cmp ax, [wf_soundtime]
    je .out
    mov [wf_soundtime], ax
    cmp byte [wf_soundwait], 0
    je .out
    dec byte [wf_soundwait]
    jnz .out
    call wf_soundnote
.out:
    ret
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
    ; Every note is finite (at most three ticks). An unconditional off could
    ; silence another instance that has since won the same-priority router.
    ; Close's instance teardown releases only our own actual grant.
    ret
wf_cues: dw wf_cueplace,wf_cueready,wf_cuefire,wf_cuemiss,wf_cuehit,wf_cuesunk,wf_cuelink,wf_cuewin,wf_cuelose
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
