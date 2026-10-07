; Optional 8kHz one-shots, played directly by the Sound Blaster's DMA engine.
; One page-safe 13KB claim: three immutable rings prepared once at load.
n_pcm_open:
    SAVE
    cmp word [n_pcmseg],0
    jne .out
    call OSAPI_SND_CAPS
    test ax,SND_CAP_PCM_BG
    jz .out
    or bl,bl
    jz .out
    mov byte [n_pcmflags],SND_OPENF_RING + SND_OPENF_EXT
    test ax,SND_CAP_EXTBLK
    jz .claim
    or byte [n_pcmflags],3 << SND_OPENF_BLKSH
.claim:
    mov ax,13
    mov cx,13
    call OSAPI_MEM_CLAIM_DMA_HI
    jc .out
    mov [n_pcmseg],dx
    mov es,dx
    xor bx,bx
    mov cx,8208
    xor dx,dx
    mov si,n_pcmfile
    call OSAPI_FILE_READ
    jc .bad
    cmp ax,8208
    jne .bad
    or dx,dx
    jnz .bad
    cmp word [es:0],0344eh
    jne .bad
    cmp word [es:2],05332h
    jne .bad
    cmp word [es:4],8208
    jne .bad
    cmp word [es:8],1024
    jne .bad
    cmp word [es:10],3072
    jne .bad
    cmp word [es:12],4096
    jne .bad
    cmp word [es:14],8000
    jne .bad
    xor dx,dx
    mov si,16
    mov cx,8192
.sum:
    xor ax,ax
    mov al,[es:si]
    add dx,ax
    inc si
    loop .sum
    cmp dx,[es:6]
    jne .bad
    ; Expand in place without overwriting unread source bytes. Source bank:
    ; header, shot, explosion, loss. Move loss first, then explosion, then shot.
    push ds
    mov ax,es
    mov ds,ax
    mov si,4112
    mov di,8224
    mov cx,2048
    rep movsw
    mov si,1040
    mov di,4112
    mov cx,1536
    rep movsw
    mov si,16
    xor di,di
    mov cx,512
    rep movsw
    mov ax,8080h
    mov cx,1536
    rep stosw
    mov di,7184
    mov cx,512
    rep stosw
    pop ds
    jmp .out
.bad:
    call n_pcm_close
.out:
    RESTORE
    ret

; Cancel hardware before overwriting or releasing its ring. Pending stays.
n_pcm_halt:
    SAVE
    cmp byte [n_pcmactive],0
    je .out
    mov ah,[n_pcmhandle]
    mov al,2
    call OSAPI_SND_STREAM
    mov byte [n_pcmactive],0
.out:
    mov byte [n_pcmpri],0
    RESTORE
    ret

n_pcm_close:
    SAVE
    call n_pcm_halt
    mov dx,[n_pcmseg]
    or dx,dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [n_pcmseg],0
.out:
    mov byte [n_pcmpending],0
    RESTORE
    ret

n_pcm_tick:
    cmp byte [n_pcmactive],0
    je .pending
    mov al,SND_V_PPOS
    mov ah,[n_pcmhandle]
    call OSAPI_SND_STREAM
    jc .ended
    cmp ax,SND_ST_PLAYING
    jne .ended
    cmp dx,[n_pcmlen]
    jb .pending
.ended:
    call n_pcm_halt
.pending:
    xor bx,bx
    mov bl,[n_pcmpending]
    or bx,bx
    jz .out
    mov bp,bx
    call n_pcm_halt
    mov byte [n_pcmpending],0
    dec bx
    shl bx,1
    mov si,[n_pcmoffset+bx]
    mov cx,[n_pcmlength+bx]
    mov [n_pcmlen],cx
    cmp cx,2048
    jae .length
    mov cx,2048                 ; legacy driver's minimum initial valid data
.length:
    mov di,[n_pcmseg]
    mov es,di
    mov bx,si
    mov [es:bx+4096+SND_EXT_TOTAL],cx
    mov word [es:bx+4096+SND_EXT_CONS],0
    mov dx,8000
    mov ah,[n_pcmflags]
    xor al,al
    call OSAPI_SND_STREAM
    jc .failed
    mov [n_pcmhandle],ah
    mov byte [n_pcmactive],1
    mov ax,bp
    mov [n_pcmpri],al
.out:
    ret
.failed:
    ; Unsupported DSP, IRQ or stream: stop trying until the next entry.
    call n_pcm_close
    mov ax,bp
    add ax,17
    call n_effect
    ret

n_pcmoffset: dw 0,4112,8224
n_pcmlength: dw 1024,3072,4096
n_pcmfile: db '1942.SFX',0
